#include "BookMoveUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>
#include <uzlib.h>

#include <cstring>

#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"

namespace {
constexpr char READ_FOLDER[] = "/Read";
constexpr char JOURNAL[] = "/.retroink/move.journal";
constexpr char MOVE_HINTS[] = "/.retroink/library.moves";
constexpr uint32_t JOURNAL_MAGIC = 0x52494d56;  // RIMV
constexpr uint32_t MOVE_HINT_MAGIC = 0x52494d48;  // RIMH

std::string cacheFor(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return Epub::cachePathForFilePath(path, "/.crosspoint");
  if (FsHelpers::hasXtcExtension(path)) return Xtc(path, "/.crosspoint").getCachePath();
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path))
    return Txt(path, "/.crosspoint").getCachePath();
  return {};
}

const char* bookType(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return "epub";
  if (FsHelpers::hasXtcExtension(path)) return "xtc";
  return "txt";
}

bool writeJournal(const std::string& oldPath, const std::string& newPath, bool folder, bool keep,
                  bool recovery = false) {
  if (oldPath.size() > 1023 || newPath.size() > 1023 ||
      (!Storage.exists("/.retroink") && !Storage.mkdir("/.retroink"))) return false;
  auto retroinkDir = Storage.open("/.retroink");
  const bool retroinkDirReady = retroinkDir && retroinkDir.isDirectory();
  retroinkDir.close();
  if (!retroinkDirReady) return false;
  FsFile file;
  if (!Storage.openFileForWrite("BookMove", JOURNAL, file)) return false;
  const uint16_t oldLen = static_cast<uint16_t>(oldPath.size());
  const uint16_t newLen = static_cast<uint16_t>(newPath.size());
  const uint8_t flags = (folder ? 1 : 0) | (keep ? 2 : 0) | (recovery ? 4 : 0);
  const bool ok = file.write(&JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) == sizeof(JOURNAL_MAGIC) &&
                  file.write(&flags, sizeof(flags)) == sizeof(flags) &&
                  file.write(&oldLen, sizeof(oldLen)) == sizeof(oldLen) &&
                  file.write(&newLen, sizeof(newLen)) == sizeof(newLen) &&
                  file.write(oldPath.data(), oldLen) == oldLen && file.write(newPath.data(), newLen) == newLen &&
                  file.sync();
  file.close();
  return ok;
}

bool readJournal(std::string& oldPath, std::string& newPath, bool& folder, bool& keep,
                 bool& recovery, bool& backedUp) {
  FsFile file;
  if (!Storage.openFileForRead("BookMove", JOURNAL, file)) return false;
  uint32_t magic = 0;
  uint16_t oldLen = 0, newLen = 0;
  uint8_t flags = 0;
  bool ok = file.read(&magic, sizeof(magic)) == sizeof(magic) &&
            file.read(&flags, sizeof(flags)) == sizeof(flags) &&
            file.read(&oldLen, sizeof(oldLen)) == sizeof(oldLen) &&
            file.read(&newLen, sizeof(newLen)) == sizeof(newLen) && magic == JOURNAL_MAGIC &&
            oldLen < 1024 && newLen < 1024;
  if (ok) {
    oldPath.resize(oldLen);
    newPath.resize(newLen);
    ok = file.read(&oldPath[0], oldLen) == oldLen && file.read(&newPath[0], newLen) == newLen;
  }
  file.close();
  folder = flags & 1;
  keep = flags & 2;
  recovery = flags & 4;
  backedUp = flags & 8;
  return ok;
}

bool appendLibraryMoveHint(const std::string& oldPath, const std::string& newPath, bool folder) {
  FsFile file = Storage.open(MOVE_HINTS, O_RDWR | O_CREAT);
  if (!file) return false;
  if (!file.fileSize64() && file.write(&MOVE_HINT_MAGIC, 4) != 4) { file.close(); return false; }
  const uint16_t oldLen = oldPath.size(), newLen = newPath.size();
  const uint8_t type = folder ? 1 : 0;
  const bool ok = file.seek64(file.fileSize64()) && file.write(&oldLen, 2) == 2 &&
                  file.write(&newLen, 2) == 2 && file.write(&type, 1) == 1 &&
                  file.write(oldPath.data(), oldLen) == oldLen &&
                  file.write(newPath.data(), newLen) == newLen && file.sync();
  file.close();
  return ok;
}

bool markRecoveryBackedUp() {
  FsFile file = Storage.open(JOURNAL, O_RDWR);
  uint8_t flags = 0;
  bool ok = file && file.seek(4) && file.read(&flags, 1) == 1 && file.seek(4);
  flags |= 8;
  ok = ok && file.write(&flags, 1) == 1 && file.sync();
  file.close();
  return ok;
}

bool backupIfPresent(const std::string& path) {
  if (!Storage.exists(path.c_str())) return true;
  for (unsigned suffix = 1; suffix < 100; ++suffix) {
    const std::string backup = path + ".retroink_before_" + std::to_string(suffix);
    if (!Storage.exists(backup.c_str())) return Storage.rename(path.c_str(), backup.c_str());
  }
  return false;
}

bool backupCurrentRecord(const std::string& path) {
  const std::string cache = cacheFor(path);
  if (cache.empty()) return false;
  const char* type = bookType(path);
  const uint32_t crc = uzlib_crc32(path.data(), static_cast<unsigned int>(path.size()), 0);
  const std::string bookmark = std::string("/.crosspoint/bookmarks/") + type + "_" + std::to_string(crc) + ".bin";
  const std::string legacyBookmark = std::string("/.crosspoint/bookmarks/") + type + "_" +
                                     std::to_string(std::hash<std::string>{}(path)) + ".bin";
  const std::string clipping = std::string("/.crosspoint/clippings/") + type + "_" + std::to_string(crc) + ".bin";
  return backupIfPresent(cache) && backupIfPresent(bookmark) &&
         (legacyBookmark == bookmark || backupIfPresent(legacyBookmark)) && backupIfPresent(clipping);
}

bool hasPriorState(const std::string& path) {
  if (Storage.exists(cacheFor(path).c_str())) return true;
  const char* type = bookType(path);
  const uint32_t crc = uzlib_crc32(path.data(), static_cast<unsigned int>(path.size()), 0);
  const std::string suffix = std::string(type) + "_" + std::to_string(crc) + ".bin";
  const std::string legacy = std::string(type) + "_" +
                             std::to_string(std::hash<std::string>{}(path)) + ".bin";
  return Storage.exists((std::string("/.crosspoint/bookmarks/") + suffix).c_str()) ||
         Storage.exists((std::string("/.crosspoint/bookmarks/") + legacy).c_str()) ||
         Storage.exists((std::string("/.crosspoint/clippings/") + suffix).c_str());
}

bool migrateFolder(const std::string& oldRoot, const std::string& newRoot, const std::string& subDir, bool keep) {
  if (!Storage.ready()) return false;
  auto dir = Storage.open(subDir.c_str());
  if (!dir || !dir.isDirectory()) return false;
  char name[256];
  bool ok = true;
  for (auto child = dir.openNextFile(); child; child = dir.openNextFile()) {
    if (!Storage.ready()) { ok = false; child.close(); break; }
    child.getName(name, sizeof(name));
    const bool isDir = child.isDirectory();
    child.close();
    const std::string newPath = subDir + "/" + name;
    if (isDir) {
      if (!migrateFolder(oldRoot, newRoot, newPath, keep)) ok = false;
    } else if (!cacheFor(newPath).empty()) {
      const std::string oldPath = oldRoot + newPath.substr(newRoot.size());
      if (!BookMoveUtils::migrateExistingPath(oldPath, newPath, keep)) ok = false;
    }
  }
  dir.close();
  return ok && Storage.ready();
}

bool preflightFolder(const std::string& oldRoot, const std::string& newRoot, const std::string& subDir) {
  if (!Storage.ready()) return false;
  auto dir = Storage.open(subDir.c_str());
  if (!dir || !dir.isDirectory()) return false;
  char name[256];
  bool ok = true;
  for (auto child = dir.openNextFile(); child; child = dir.openNextFile()) {
    child.getName(name, sizeof(name));
    const bool isDir = child.isDirectory(); child.close();
    const std::string oldPath = subDir + "/" + name;
    const std::string newPath = newRoot + oldPath.substr(oldRoot.size());
    if (isDir) ok = preflightFolder(oldRoot, newRoot, oldPath) && ok;
    else if (!cacheFor(oldPath).empty() && hasPriorState(newPath)) ok = false;
  }
  dir.close();
  return ok && Storage.ready();
}
}

namespace BookMoveUtils {

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

bool migrateMovedEpubState(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                           const std::string& title, const std::string& author, const bool keepInRecents) {
  bool ok = true;

  const std::string newCachePath = Epub::cachePathForFilePath(newPath, "/.crosspoint");
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("BookMove", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(),
              newCachePath.c_str());
      ok = false;
    }
  }

  if (!BookmarkStore::migrateForFilePath(oldPath, newPath, title, author, "epub")) {
    LOG_ERR("BookMove", "Failed to migrate bookmarks for moved book %s -> %s", oldPath.c_str(), newPath.c_str());
    ok = false;
  }

  if (!ClippingStore::migrateForFilePath(oldPath, newPath, title, author, "epub")) {
    LOG_ERR("BookMove", "Failed to migrate clippings for moved book %s -> %s", oldPath.c_str(), newPath.c_str());
    ok = false;
  }

  if (keepInRecents) {
    RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  } else {
    RECENT_BOOKS.removeByPath(oldPath);
    RECENT_BOOKS.removeByPath(newPath);
  }

  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    APP_STATE.saveToFile();
  }

  return ok && Storage.ready();
}

bool migrateExistingPath(const std::string& oldPath, const std::string& newPath, bool keepInRecents) {
  if (!Storage.ready()) return false;
  const std::string oldCache = cacheFor(oldPath);
  const std::string newCache = cacheFor(newPath);
  if (oldCache.empty() || newCache.empty()) return true;
  if (Storage.exists(oldCache.c_str()) && Storage.exists(newCache.c_str())) {
    LOG_ERR("BookMove", "Destination reading cache already exists: %s", newCache.c_str());
    return false;
  }
  if (FsHelpers::hasEpubExtension(oldPath)) {
    const auto& books = RECENT_BOOKS.getBooks();
    std::string title, author;
    for (const auto& book : books) if (book.path == oldPath) { title = book.title; author = book.author; break; }
    if (title.empty()) {
      Epub cached(oldPath, "/.crosspoint");
      if (cached.load(false, true, Epub::XLocationLoadMode::Skip)) {
        title = cached.getTitle(); author = cached.getAuthor();
      }
    }
    return migrateMovedEpubState(oldPath, newPath, oldCache, title, author, keepInRecents);
  }
  if (Storage.exists(oldCache.c_str()) && !Storage.rename(oldCache.c_str(), newCache.c_str())) {
    LOG_ERR("BookMove", "Cannot migrate reader cache: %s", oldCache.c_str());
    return false;
  }
  const char* type = bookType(oldPath);
  std::string title, author;
  for (const auto& book : RECENT_BOOKS.getBooks())
    if (book.path == oldPath) { title = book.title; author = book.author; break; }
  if (title.empty() && FsHelpers::hasXtcExtension(newPath)) {
    Xtc xtc(newPath, "/.crosspoint");
    if (xtc.load()) { title = xtc.getTitle(); author = xtc.getAuthor(); }
  }
  if (title.empty()) title = newPath.substr(newPath.find_last_of('/') + 1);
  if (!BookmarkStore::migrateForFilePath(oldPath, newPath, title, author, type) ||
      !ClippingStore::migrateForFilePath(oldPath, newPath, title, author, type)) return false;
  if (keepInRecents) RECENT_BOOKS.updatePath(oldPath, newPath, oldCache, newCache);
  else RECENT_BOOKS.removeByPath(oldPath);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    APP_STATE.saveToFile();
  }
  return Storage.ready();
}

bool recoverPendingMove() {
  if (!Storage.ready()) return false;
  if (!Storage.exists(JOURNAL)) return true;
  std::string oldPath, newPath;
  bool folder = false, keep = true, recovery = false, backedUp = false;
  if (!readJournal(oldPath, newPath, folder, keep, recovery, backedUp)) {
    LOG_ERR("BookMove", "Move journal is invalid; keeping it for review");
    return false;
  }
  if (!recovery && Storage.exists(oldPath.c_str()) && !Storage.exists(newPath.c_str())) {
    // Restart occurred before the file rename; no reading state was touched.
    Storage.remove(JOURNAL);
    return true;
  }
  if (!Storage.exists(newPath.c_str())) {
    LOG_ERR("BookMove", "Both move paths missing: %s", oldPath.c_str());
    return false;
  }
  if (!recovery && Storage.exists(oldPath.c_str())) {
    LOG_ERR("BookMove", "Both move paths exist; refusing ambiguous migration");
    return false;
  }
  if (recovery && !backedUp) {
    if (!backupCurrentRecord(newPath) || !markRecoveryBackedUp()) {
      LOG_ERR("BookMove", "Could not back up current reading record");
      return false;
    }
  }
  const bool ok = folder ? migrateFolder(oldPath, newPath, newPath, keep)
                         : migrateExistingPath(oldPath, newPath, keep);
  if (!ok) return false;
  if (!recovery && !appendLibraryMoveHint(oldPath, newPath, folder)) {
    LOG_ERR("BookMove", "Could not record Library move hint");
    return false;
  }
  Storage.remove(JOURNAL);
  return true;
}

bool restorePriorState(const std::string& oldPath, const std::string& currentPath) {
  if (oldPath.empty() || currentPath.empty() || oldPath == currentPath ||
      Storage.exists(oldPath.c_str()) || !Storage.exists(currentPath.c_str()) ||
      cacheFor(oldPath).empty() || !hasPriorState(oldPath) ||
      !recoverPendingMove()) return false;
  if (!writeJournal(oldPath, currentPath, false, true, true)) return false;
  return recoverPendingMove();
}

bool moveWithState(const std::string& oldPath, const std::string& newPath, bool keepInRecents) {
  if (!Storage.ready() || oldPath.empty() || newPath.empty() || oldPath == newPath ||
      !Storage.exists(oldPath.c_str()) || Storage.exists(newPath.c_str()) ||
      !recoverPendingMove()) return false;
  auto source = Storage.open(oldPath.c_str());
  if (!source) return false;
  const bool folder = source.isDirectory();
  source.close();
  if (folder && newPath.rfind(oldPath + "/", 0) == 0) return false;
  if (folder && !preflightFolder(oldPath, newPath, oldPath)) return false;
  if (!folder && !cacheFor(oldPath).empty() && hasPriorState(newPath)) {
    LOG_ERR("BookMove", "Target reading record already exists: %s", newPath.c_str());
    return false;
  }
  if (!writeJournal(oldPath, newPath, folder, keepInRecents)) {
    LOG_ERR("BookMove", "Could not write move journal");
    return false;
  }
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    Storage.remove(JOURNAL);
    LOG_ERR("BookMove", "File rename failed: %s -> %s", oldPath.c_str(), newPath.c_str());
    return false;
  }
  if (recoverPendingMove()) return true;
  if (!folder) {
    const std::string oldCache = cacheFor(oldPath), newCache = cacheFor(newPath);
    if (!oldCache.empty() && Storage.exists(oldCache.c_str()) && !Storage.exists(newCache.c_str()) &&
        !Storage.exists(oldPath.c_str()) && Storage.rename(newPath.c_str(), oldPath.c_str())) {
      Storage.remove(JOURNAL);
      LOG_ERR("BookMove", "Rolled back file rename after cache migration failure");
      return false;
    }
  }
  LOG_ERR("BookMove", "State migration pending for %s", newPath.c_str());
  return false;
}

}  // namespace BookMoveUtils
