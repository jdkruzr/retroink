#include "RetroInkCustomShelves.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr char kShelvesPath[] = "/.retroink/library.shelves.bin";
constexpr char kAssignmentsPath[] = "/.retroink/library.shelf_assignments.bin";

struct FileHeader {
  uint32_t magic;
  uint16_t version;
  uint32_t count;
};

void copyName(char* dest, size_t destSize, const std::string& name) {
  const size_t n = std::min(name.size(), destSize - 1);
  std::memcpy(dest, name.data(), n);
  dest[n] = '\0';
}
}  // namespace

bool RetroInkCustomShelves::hasShelf(const uint16_t id) const {
  return id != kNoShelf &&
         std::any_of(shelves_.begin(), shelves_.end(), [id](const Shelf& s) { return s.id == id; });
}

bool RetroInkCustomShelves::load() {
  shelves_.clear();
  assignments_.clear();
  nextId_ = 1;

  if (Storage.exists(kShelvesPath)) {
    FsFile file;
    if (!Storage.openFileForRead("SHELF", kShelvesPath, file)) return false;
    FileHeader header{};
    bool ok = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kShelvesMagic &&
              header.version == kVersion && header.count <= kMaxShelves;
    if (ok) {
      shelves_.resize(header.count);
      for (auto& shelf : shelves_) {
        if (file.read(&shelf, sizeof(shelf)) != sizeof(shelf)) { ok = false; break; }
        shelf.name[kMaxNameLength] = '\0';
        nextId_ = std::max<uint16_t>(nextId_, static_cast<uint16_t>(shelf.id + 1));
      }
    }
    file.close();
    if (!ok) {
      LOG_ERR("SHELF", "Discarded invalid shelf file");
      shelves_.clear();
      return false;
    }
  }

  if (Storage.exists(kAssignmentsPath)) {
    FsFile file;
    if (!Storage.openFileForRead("SHELF", kAssignmentsPath, file)) return false;
    FileHeader header{};
    bool ok = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kAssignMagic &&
              header.version == kVersion && header.count <= kMaxAssignments;
    if (ok) {
      assignments_.resize(header.count);
      for (auto& assignment : assignments_) {
        if (file.read(&assignment, sizeof(assignment)) != sizeof(assignment)) { ok = false; break; }
      }
    }
    file.close();
    if (!ok) {
      LOG_ERR("SHELF", "Discarded invalid shelf assignment file");
      assignments_.clear();
      return false;
    }
  }
  return true;
}

bool RetroInkCustomShelves::save() const {
  Storage.mkdir("/.retroink");

  FsFile shelvesFile;
  if (!Storage.openFileForWrite("SHELF", kShelvesPath, shelvesFile)) return false;
  const FileHeader shelvesHeader{kShelvesMagic, kVersion, static_cast<uint32_t>(shelves_.size())};
  bool ok = shelvesFile.write(&shelvesHeader, sizeof(shelvesHeader)) == sizeof(shelvesHeader);
  for (const auto& shelf : shelves_) {
    ok = ok && shelvesFile.write(&shelf, sizeof(shelf)) == sizeof(shelf);
  }
  shelvesFile.flush();
  shelvesFile.close();
  if (!ok) {
    LOG_ERR("SHELF", "Short write saving shelves");
    return false;
  }

  FsFile assignFile;
  if (!Storage.openFileForWrite("SHELF", kAssignmentsPath, assignFile)) return false;
  const FileHeader assignHeader{kAssignMagic, kVersion, static_cast<uint32_t>(assignments_.size())};
  ok = assignFile.write(&assignHeader, sizeof(assignHeader)) == sizeof(assignHeader);
  for (const auto& assignment : assignments_) {
    ok = ok && assignFile.write(&assignment, sizeof(assignment)) == sizeof(assignment);
  }
  assignFile.flush();
  assignFile.close();
  if (!ok) {
    LOG_ERR("SHELF", "Short write saving shelf assignments");
    return false;
  }
  return true;
}

uint16_t RetroInkCustomShelves::createShelf(const std::string& name) {
  if (name.empty() || name.size() > kMaxNameLength) return kNoShelf;
  if (shelves_.size() >= kMaxShelves) return kNoShelf;
  Shelf shelf;
  shelf.id = nextId_++;
  copyName(shelf.name, sizeof(shelf.name), name);
  shelf.sortOrder = static_cast<uint16_t>(shelves_.size());
  shelves_.push_back(shelf);
  return shelf.id;
}

bool RetroInkCustomShelves::renameShelf(const uint16_t id, const std::string& name) {
  if (name.empty() || name.size() > kMaxNameLength) return false;
  const auto it = std::find_if(shelves_.begin(), shelves_.end(), [id](const Shelf& s) { return s.id == id; });
  if (it == shelves_.end()) return false;
  copyName(it->name, sizeof(it->name), name);
  return true;
}

bool RetroInkCustomShelves::deleteShelf(const uint16_t id) {
  const auto it = std::find_if(shelves_.begin(), shelves_.end(), [id](const Shelf& s) { return s.id == id; });
  if (it == shelves_.end()) return false;
  shelves_.erase(it);
  assignments_.erase(std::remove_if(assignments_.begin(), assignments_.end(),
                                    [id](const Assignment& a) { return a.shelfId == id; }),
                     assignments_.end());
  return true;
}

uint16_t RetroInkCustomShelves::shelfForFingerprint(const uint64_t fingerprint) const {
  const auto it = std::find_if(assignments_.begin(), assignments_.end(),
                               [fingerprint](const Assignment& a) { return a.fingerprint == fingerprint; });
  return it == assignments_.end() ? kNoShelf : it->shelfId;
}

bool RetroInkCustomShelves::assignToShelf(const uint64_t fingerprint, const uint16_t shelfId) {
  if (shelfId != kNoShelf && !hasShelf(shelfId)) return false;
  const auto it = std::find_if(assignments_.begin(), assignments_.end(),
                               [fingerprint](const Assignment& a) { return a.fingerprint == fingerprint; });
  if (shelfId == kNoShelf) {
    if (it != assignments_.end()) assignments_.erase(it);
    return true;
  }
  if (it != assignments_.end()) {
    it->shelfId = shelfId;
    return true;
  }
  if (assignments_.size() >= kMaxAssignments) return false;
  assignments_.push_back(Assignment{fingerprint, shelfId});
  return true;
}
