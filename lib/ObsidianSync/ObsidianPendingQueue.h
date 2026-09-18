#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ObsidianPendingClipping {
  std::string book;
  std::string author;
  std::string chapter;
  int page = 0;
  std::string text;
  uint32_t timestamp = 0;  // Unix seconds; 0 when the clock is unavailable.
};

/**
 * Append-only queue of clippings waiting to be pushed to Obsidian, stored as
 * one JSON object per line on the SD card. Decoupled from ClippingStore's
 * binary per-book layout on purpose: a clipping is enqueued once, at the
 * moment it is saved (see EpubReaderActivity's clip-selection callback), and
 * only ever consumed by ObsidianSyncClient::syncPending().
 */
namespace ObsidianPendingQueue {

inline constexpr const char* PENDING_PATH = "/.crosspoint/obsidian-pending.jsonl";
// Byte cap, checked with a single already-open FsFile::size() call so append()
// stays O(1) on the interactive reading path (no full-file parse per
// highlight). ~512 KB is several thousand clippings of headroom before a
// user who never syncs starts losing the oldest ones.
inline constexpr size_t MAX_PENDING_BYTES = 512 * 1024;

// Appends one clipping. Cheap: opens O_APPEND, writes one line, closes. The
// same cost profile as ClippingsManager::saveClipping's "My Clippings.txt"
// write. Returns false (and drops the clipping) once MAX_PENDING_BYTES is hit;
// the clipping is still recorded in the human-readable "My Clippings.txt".
bool append(const ObsidianPendingClipping& clipping);

bool hasPending();
// O(n): parses the whole file. Only call from sync/status UI, not the reading path.
size_t count();

// Reads every pending entry in FIFO order.
std::vector<ObsidianPendingClipping> readAll();

// Removes the first `sentCount` entries (in the order readAll() returned
// them) by rewriting the remainder to a temp file and renaming it into
// place, so a crash or power loss mid-drain leaves either the old file or
// the new one, never a truncated one.
bool removeFirst(size_t sentCount);

}  // namespace ObsidianPendingQueue
