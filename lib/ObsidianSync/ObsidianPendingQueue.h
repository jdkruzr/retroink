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
  // Consecutive delivery failures for this specific clipping (not the queue
  // as a whole). Persisted so a clipping that keeps getting rejected is
  // eventually given up on instead of blocking every clipping behind it
  // forever. See MAX_DELIVERY_ATTEMPTS.
  uint8_t failCount = 0;
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

// A clipping that fails to deliver this many times in a row is given up on
// (see ObsidianSyncClient::syncPending) rather than left blocking the queue
// indefinitely. Small enough that a genuinely broken target still gives up
// within a handful of manual "Sync Now" presses, large enough that an
// ordinary transient network hiccup never trips it.
inline constexpr uint8_t MAX_DELIVERY_ATTEMPTS = 5;

// Clippings given up on after MAX_DELIVERY_ATTEMPTS land here instead of
// vanishing silently, one JSON object per line like PENDING_PATH plus a
// "reason" field. Nothing currently reads this back on-device; it exists so
// the SD card can be inspected directly to see what didn't make it.
inline constexpr const char* FAILED_PATH = "/.crosspoint/obsidian-failed.jsonl";

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

// Replaces the whole queue with `remaining` (e.g. a suffix of a previous
// readAll(), with the new head's failCount already bumped), atomically like
// removeFirst. An empty `remaining` deletes the file.
bool replaceAll(const std::vector<ObsidianPendingClipping>& remaining);

// Appends a clipping that's been given up on (see MAX_DELIVERY_ATTEMPTS) to
// FAILED_PATH along with why, for later inspection. Does not touch the
// pending queue itself; the caller is responsible for removing the clipping
// from it (typically via replaceAll).
bool appendFailed(const ObsidianPendingClipping& clipping, const std::string& reason);

}  // namespace ObsidianPendingQueue
