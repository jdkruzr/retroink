#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

// User-created library shelves: an additive, separate store from
// RetroInkLibraryCatalog's fixed-size, versioned book records. A custom
// shelf is a simple organizational layer (one shelf per book, like a
// Kanban board) independent of the reading-activity-derived smart shelves
// (To Read / Reading / Finished) and the Favorite flag, which already live
// on the main catalog. Kept separate so this feature can't destabilize the
// catalog's on-disk format (RetroInkLibraryCatalog::Record is
// static_assert'd to a fixed size that other tooling depends on).
//
// Books are identified by RetroInkLibraryCatalog::Record::fingerprint (a
// content-based hash, not the path), the same identity the catalog already
// uses to survive renames and moves - so an assignment here needs no
// separate move-recovery hook.
class RetroInkCustomShelves {
 public:
  static constexpr uint16_t kNoShelf = 0;
  static constexpr size_t kMaxNameLength = 31;
  static constexpr uint16_t kMaxShelves = 64;
  static constexpr uint32_t kMaxAssignments = 4096;

  struct Shelf {
    uint16_t id = kNoShelf;
    char name[kMaxNameLength + 1] = {};
    uint16_t sortOrder = 0;
  };

  // Loads from /.retroink/library.shelves.bin and
  // library.shelf_assignments.bin. A missing file means "no shelves yet",
  // not a failure. Returns false only on a genuine read error (bad magic,
  // truncated file), in which case the store is left empty rather than
  // partially populated.
  bool load();
  bool save() const;

  const std::vector<Shelf>& shelves() const { return shelves_; }

  // Returns the new shelf's id, or kNoShelf on failure (blank/too-long
  // name, or kMaxShelves already reached).
  uint16_t createShelf(const std::string& name);
  bool renameShelf(uint16_t id, const std::string& name);
  // Also clears every assignment pointing at this shelf.
  bool deleteShelf(uint16_t id);

  uint16_t shelfForFingerprint(uint64_t fingerprint) const;
  // shelfId == kNoShelf removes the assignment (book goes back to
  // "unassigned"). Returns false if shelfId doesn't name an existing shelf.
  bool assignToShelf(uint64_t fingerprint, uint16_t shelfId);

 private:
  struct Assignment {
    uint64_t fingerprint = 0;
    uint16_t shelfId = kNoShelf;
  };

  static constexpr uint32_t kShelvesMagic = 0x53484c46;    // SHLF
  static constexpr uint32_t kAssignMagic = 0x41534c46;     // ASLF
  static constexpr uint16_t kVersion = 1;

  std::vector<Shelf> shelves_;
  std::vector<Assignment> assignments_;
  uint16_t nextId_ = 1;

  bool hasShelf(uint16_t id) const;
};
