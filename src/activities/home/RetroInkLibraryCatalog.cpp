#include "RetroInkLibraryCatalog.h"

#include <Epub.h>
#include <Arduino.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>
#include <Txt.h>
#include <Xtc.h>
#include <ZipFile.h>
#include <expat.h>
#include <uzlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "Epub/parsers/ContainerParser.h"

namespace {
constexpr char CATALOG[] = "/.retroink/library.bin";
constexpr char PATH_INDEX[] = "/.retroink/library.path.idx";
constexpr char SCAN[] = "/.retroink/library.scan";
constexpr char CURSOR[] = "/.retroink/library.cursor";
constexpr char VIEW[] = "/.retroink/library.view.idx";
constexpr char DEFAULT_VIEW[] = "/.retroink/library.title.idx";
constexpr char VIEW_TMP[] = "/.retroink/library.view.tmp";
constexpr char PATH_TMP[] = "/.retroink/library.path.tmp";
constexpr char CATALOG_BAK[] = "/.retroink/library.bin.bak";
constexpr char PATH_BAK[] = "/.retroink/library.path.idx.bak";
constexpr char ORPHANS[] = "/.retroink/library.orphans.bin";
constexpr char MOVE_HINTS[] = "/.retroink/library.moves";
constexpr char DIRTY[] = "/.retroink/library.dirty";
constexpr uint32_t ORPHAN_MAGIC = 0x52494f52;  // RIOR
constexpr uint32_t MOVE_HINT_MAGIC = 0x52494d48;  // RIMH
constexpr uint8_t ACTIVITY = 1, FINISHED = 2, FAVORITE = 4;

template <size_t N>
void copyText(char (&to)[N], const std::string& from) {
  const size_t len = std::min(from.size(), N - 1);
  memcpy(to, from.data(), len);
  size_t safe = len;
  while (safe > 0 && (static_cast<uint8_t>(to[safe - 1]) & 0xc0) == 0x80) --safe;
  if (safe < len) --safe;
  to[safe] = '\0';
}

int compareCasefold(const char* a, const char* b) {
  while (*a && *b) {
    const int left = std::tolower(static_cast<uint8_t>(*a));
    const int right = std::tolower(static_cast<uint8_t>(*b));
    if (left != right) return left - right;
    ++a;
    ++b;
  }
  return static_cast<uint8_t>(*a) - static_cast<uint8_t>(*b);
}

uint8_t letterBucket(const char* title) {
  const uint8_t first = static_cast<uint8_t>(title[0]);
  const int upper = std::toupper(first);
  return upper >= 'A' && upper <= 'Z' ? static_cast<uint8_t>(upper - 'A' + 1) : 0;
}

const char* filename(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

uint32_t liveRecentRank(const char* path) {
  const auto& recent = RECENT_BOOKS.getBooks();
  for (uint32_t i = 0; i < recent.size(); ++i)
    if (recent[i].path == path) return i;
  return UINT32_MAX;
}

bool excludedDirectory(const char* name) {
  return name[0] == '.' || strcmp(name, "sleep") == 0 || strcmp(name, "XTCache") == 0 ||
         strcmp(name, "System Volume Information") == 0;
}

bool stateForPath(const char* path) {
  const std::string bookPath(path);
  const char* type = FsHelpers::hasEpubExtension(bookPath) ? "epub" :
                     FsHelpers::hasXtcExtension(bookPath) ? "xtc" : "txt";
  const std::string cache = FsHelpers::hasEpubExtension(bookPath)
                                ? Epub::cachePathForFilePath(bookPath, "/.crosspoint")
                                : FsHelpers::hasXtcExtension(bookPath)
                                      ? Xtc(bookPath, "/.crosspoint").getCachePath()
                                      : Txt(bookPath, "/.crosspoint").getCachePath();
  if (Storage.exists(cache.c_str())) return true;
  const uint32_t crc = uzlib_crc32(bookPath.data(), static_cast<unsigned int>(bookPath.size()), 0);
  const std::string id = std::string(type) + "_" + std::to_string(crc) + ".bin";
  const std::string legacy = std::string(type) + "_" +
                             std::to_string(std::hash<std::string>{}(bookPath)) + ".bin";
  return Storage.exists((std::string("/.crosspoint/bookmarks/") + id).c_str()) ||
         Storage.exists((std::string("/.crosspoint/bookmarks/") + legacy).c_str()) ||
         Storage.exists((std::string("/.crosspoint/clippings/") + id).c_str());
}

// EPUB metadata is parsed directly from the OPF stream. It does not build a
// spine, CSS rules, pages, images, or a reading cache.
class OpfMetadataSink final : public Print {
  XML_Parser xml_ = nullptr;
  enum class Field : uint8_t { None, Title, Author } field_ = Field::None;
  static void XMLCALL start(void* ctx, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<OpfMetadataSink*>(ctx);
    const char* local = strrchr(name, ':');
    local = local ? local + 1 : name;
    if (strcmp(local, "title") == 0) self->field_ = Field::Title;
    else if (strcmp(local, "creator") == 0) self->field_ = Field::Author;
  }
  static void XMLCALL end(void* ctx, const XML_Char* name) {
    auto* self = static_cast<OpfMetadataSink*>(ctx);
    const char* local = strrchr(name, ':');
    local = local ? local + 1 : name;
    if (strcmp(local, "title") == 0 || strcmp(local, "creator") == 0) self->field_ = Field::None;
  }
  static void XMLCALL chars(void* ctx, const XML_Char* text, int len) {
    auto* self = static_cast<OpfMetadataSink*>(ctx);
    std::string* dest = self->field_ == Field::Title ? &self->title :
                        self->field_ == Field::Author ? &self->author : nullptr;
    const size_t limit = self->field_ == Field::Title ? 223 : 111;
    if (dest && dest->size() < limit) dest->append(text, std::min<size_t>(len, limit - dest->size()));
  }
 public:
  std::string title, author;
  bool begin() {
    xml_ = XML_ParserCreate(nullptr);
    if (!xml_) return false;
    XML_SetUserData(xml_, this);
    XML_SetElementHandler(xml_, start, end);
    XML_SetCharacterDataHandler(xml_, chars);
    title.reserve(224);
    author.reserve(112);
    return true;
  }
  ~OpfMetadataSink() override { if (xml_) XML_ParserFree(xml_); }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t size) override {
    if (!xml_ || XML_Parse(xml_, reinterpret_cast<const char*>(buf), static_cast<int>(size), false) == XML_STATUS_ERROR)
      return 0;
    return size;
  }
};

bool epubMetadata(const std::string& path, std::string& title, std::string& author) {
  Epub cached(path, "/.crosspoint");
  if (cached.load(false, true, Epub::XLocationLoadMode::Skip)) {
    title = cached.getTitle(); author = cached.getAuthor();
    if (!title.empty()) return true;
  }
  ZipFile zip(path);
  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize) || containerSize > 65536) return false;
  ContainerParser container(containerSize);
  if (!container.setup() || !zip.readFileToStream("META-INF/container.xml", container, 512)) return false;
  if (container.fullPath.empty()) return false;
  size_t opfSize = 0;
  if (!zip.getInflatedFileSize(container.fullPath.c_str(), &opfSize) || opfSize > 2U * 1024U * 1024U) return false;
  OpfMetadataSink sink;
  if (!sink.begin() || !zip.readFileToStream(container.fullPath.c_str(), sink, 512)) return false;
  title = std::move(sink.title); author = std::move(sink.author);
  return !title.empty();
}
}  // namespace

bool RetroInkLibraryCatalog::supported(const char* path) {
  const std::string_view name(path);
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) ||
         FsHelpers::hasTxtExtension(name) || FsHelpers::hasMarkdownExtension(name);
}

uint64_t RetroInkLibraryCatalog::fingerprintFor(const std::string& path, uint64_t& size) {
  FsFile file;
  if (!Storage.openFileForRead("Library", path, file)) return 0;
  size = file.fileSize64();
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned i = 0; i < 8; ++i) { hash ^= (size >> (i * 8)) & 0xff; hash *= 1099511628211ULL; }
  uint8_t buf[128];  // bounded stack; sample only the first and last 4 KB
  for (int sample = 0; sample < 2; ++sample) {
    if (sample && !file.seek64(size > 4096 ? size - 4096 : 0)) break;
    uint64_t left = std::min<uint64_t>(size, 4096);
    while (left) {
      const int got = file.read(buf, std::min<uint64_t>(left, sizeof(buf)));
      if (got <= 0) break;
      for (int j = 0; j < got; ++j) { hash ^= buf[j]; hash *= 1099511628211ULL; }
      left -= got;
    }
  }
  file.close();
  return hash;
}

bool RetroInkLibraryCatalog::readRecord(FsFile& file, uint32_t id, Record& out) {
  const uint64_t offset = 12ULL + static_cast<uint64_t>(id) * sizeof(Record);
  return file.seek64(offset) && file.read(&out, sizeof(out)) == sizeof(out);
}

bool RetroInkLibraryCatalog::findOldRecord(const char* path, Record& out) {
  if (!oldFile_ || !oldPathIndex_) return false;
  const uint32_t oldCount = static_cast<uint32_t>(oldPathIndex_.fileSize64() / sizeof(uint32_t));
  uint32_t lo = 0, hi = oldCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    uint32_t id = 0;
    if (!oldPathIndex_.seek(mid * sizeof(id)) || oldPathIndex_.read(&id, sizeof(id)) != sizeof(id) ||
        !readRecord(oldFile_, id, out)) return false;
    const int cmp = compareCasefold(out.path, path);
    if (cmp < 0) lo = mid + 1;
    else hi = mid;
  }
  uint32_t id = 0;
  return lo < oldCount && oldPathIndex_.seek(lo * sizeof(id)) &&
         oldPathIndex_.read(&id, sizeof(id)) == sizeof(id) && readRecord(oldFile_, id, out) &&
         compareCasefold(out.path, path) == 0;
}

bool RetroInkLibraryCatalog::findMoveSource(const std::string& path, std::string& oldPath) {
  if (!hasMoveHints_) return false;
  FsFile hints = Storage.open(MOVE_HINTS);
  if (!hints) return false;
  uint32_t magic = 0;
  if (hints.read(&magic, 4) != 4 || magic != MOVE_HINT_MAGIC) { hints.close(); return false; }
  while (hints.available() > 5) {
    uint16_t oldLen = 0, newLen = 0; uint8_t folder = 0;
    if (hints.read(&oldLen, 2) != 2 || hints.read(&newLen, 2) != 2 ||
        hints.read(&folder, 1) != 1 || oldLen > 1023 || newLen > 1023) break;
    std::string oldRoot(oldLen, '\0'), newRoot(newLen, '\0');
    if (hints.read(oldRoot.data(), oldLen) != oldLen || hints.read(newRoot.data(), newLen) != newLen) break;
    if (path == newRoot || (folder && path.rfind(newRoot + "/", 0) == 0)) {
      oldPath = oldRoot + path.substr(newRoot.size());
      hints.close(); return true;
    }
  }
  hints.close(); return false;
}

bool RetroInkLibraryCatalog::metadataFor(const std::string& path, const uint64_t sizeHint, const uint16_t mtimeDate,
                                         const uint16_t mtimeTime, Record& record) {
  record = Record{};
  if (path.size() >= sizeof(record.path)) {
    LOG_ERR("Library", "Book path too long for catalog: %s", path.c_str());
    return false;
  }
  copyText(record.path, path);
  record.size = sizeHint;
  record.mtimeDate = mtimeDate;
  record.mtimeTime = mtimeTime;

  const bool haveOld = findOldRecord(path.c_str(), scratchB_);
  // A real FAT timestamp is never all-zero, so an all-zero reading (no RTC,
  // or the wrapper couldn't read it) is treated as "unknown" rather than
  // risking two different timestamp-less files being mistaken for a match.
  const bool mtimeKnown = mtimeDate != 0 || mtimeTime != 0;
  const bool cheapUnchanged = haveOld && mtimeKnown && scratchB_.size == sizeHint &&
                              scratchB_.mtimeDate == mtimeDate && scratchB_.mtimeTime == mtimeTime;
  bool unchanged;
  if (cheapUnchanged) {
    // Same size and modification time as the last scan: skip opening the
    // file a second time just to hash it, and trust the content hasn't
    // changed. This is the common case on every scan after the first, and
    // is what makes adding one book to a large library fast again.
    record.fingerprint = scratchB_.fingerprint;
    unchanged = true;
  } else {
    uint64_t hashedSize = 0;
    record.fingerprint = fingerprintFor(path, hashedSize);
    if (!record.fingerprint) return false;
    unchanged = haveOld && scratchB_.fingerprint == record.fingerprint;
  }
  if (unchanged) {
    memcpy(record.title, scratchB_.title, sizeof(record.title));
    memcpy(record.author, scratchB_.author, sizeof(record.author));
    record.flags = scratchB_.flags & FAVORITE;
  } else {
    std::string title, author;
    if (FsHelpers::hasEpubExtension(path)) epubMetadata(path, title, author);
    else if (FsHelpers::hasXtcExtension(path)) {
      Xtc xtc(path, "/.crosspoint");
      if (xtc.load()) { title = xtc.getTitle(); author = xtc.getAuthor(); }
    }
    if (title.empty()) title = filename(path.c_str());
    copyText(record.title, title);
    copyText(record.author, author);
    std::string previousPath;
    if (findMoveSource(path, previousPath) && findOldRecord(previousPath.c_str(), scratchB_) &&
        scratchB_.fingerprint == record.fingerprint) record.flags |= scratchB_.flags & FAVORITE;
  }
  const std::string cache = FsHelpers::hasEpubExtension(path) ? Epub::cachePathForFilePath(path, "/.crosspoint") :
                            FsHelpers::hasXtcExtension(path) ? Xtc(path, "/.crosspoint").getCachePath() :
                            Txt(path, "/.crosspoint").getCachePath();
  if (Storage.exists((cache + "/progress.bin").c_str())) record.flags |= ACTIVITY;
  if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path)) {
    const BookReadingStats stats = BookReadingStats::load(cache);
    if (stats.sessionCount || stats.totalReadingSeconds || stats.totalPagesTurned) record.flags |= ACTIVITY;
    if (stats.isCompleted) record.flags |= FINISHED;
  }
  const auto& recent = RECENT_BOOKS.getBooks();
  for (uint32_t i = 0; i < recent.size(); ++i)
    if (recent[i].path == path) { record.recentRank = i; break; }
  return true;
}

bool RetroInkLibraryCatalog::writeRecord(const Record& record) {
  if (count_ >= kMaxBooks) { LOG_ERR("Library", "Catalog exceeds %u books", kMaxBooks); return false; }
  const uint64_t offset = 12ULL + static_cast<uint64_t>(count_) * sizeof(Record);
  if (!scanFile_.seek64(offset) || scanFile_.write(&record, sizeof(record)) != sizeof(record)) return false;
  ++count_;
  return true;
}

bool RetroInkLibraryCatalog::saveResumeCursor() {
  FsFile out;
  if (!Storage.openFileForWrite("Library", CURSOR, out)) return false;
  const uint32_t depth = frames_.size();
  bool ok = out.write(&depth, sizeof(depth)) == sizeof(depth);
  for (const auto& frame : frames_) {
    const uint16_t len = static_cast<uint16_t>(frame.path.size());
    ok = ok && out.write(&len, sizeof(len)) == sizeof(len) &&
         out.write(&frame.entryNo, sizeof(frame.entryNo)) == sizeof(frame.entryNo) &&
         out.write(frame.path.data(), len) == len;
  }
  out.sync(); out.close(); scanFile_.sync();
  return ok;
}

bool RetroInkLibraryCatalog::loadResumeCursor() {
  FsFile in;
  if (!Storage.openFileForRead("Library", CURSOR, in)) return false;
  uint32_t depth = 0;
  bool ok = in.read(&depth, sizeof(depth)) == sizeof(depth) && depth <= 24;
  frames_.clear(); frames_.reserve(depth);
  for (uint32_t i = 0; ok && i < depth; ++i) {
    uint16_t len = 0; uint32_t entryNo = 0;
    ok = in.read(&len, sizeof(len)) == sizeof(len) &&
         in.read(&entryNo, sizeof(entryNo)) == sizeof(entryNo) && len > 0 && len < 512;
    if (!ok) break;
    std::string path(len, '\0');
    ok = in.read(path.data(), len) == len;
    if (!ok) break;
    auto dir = Storage.open(path.c_str());
    ok = dir && dir.isDirectory();
    for (uint32_t j = 0; ok && j < entryNo; ++j) {
      auto child = dir.openNextFile();
      ok = !!child;
      child.close();
    }
    if (ok) frames_.push_back(Frame{std::move(path), std::move(dir), entryNo});
  }
  in.close();
  return ok;
}

bool RetroInkLibraryCatalog::beginScan() {
  close();
  count_ = viewCount_ = 0;
  currentPath_.clear();
  hasMoveHints_ = Storage.exists(MOVE_HINTS);
  if (!Storage.ready() ||
      (!Storage.exists("/.retroink") && !Storage.mkdir("/.retroink"))) {
    failed_ = true;
    return false;
  }
  auto retroinkDir = Storage.open("/.retroink");
  const bool retroinkDirReady = retroinkDir && retroinkDir.isDirectory();
  retroinkDir.close();
  if (!retroinkDirReady) { failed_ = true; return false; }
  oldFile_ = Storage.open(CATALOG);
  oldPathIndex_ = Storage.open(PATH_INDEX);
  if (oldFile_ && oldPathIndex_) {
    uint32_t magic = 0; uint16_t version = 0;
    if (oldFile_.read(&magic, 4) != 4 || oldFile_.read(&version, 2) != 2 ||
        magic != kMagic || version != kVersion) { oldFile_.close(); oldPathIndex_.close(); }
  }
  const bool resume = Storage.exists(SCAN) && Storage.exists(CURSOR);
  scanFile_ = Storage.open(SCAN, O_RDWR | O_CREAT | (resume ? 0 : O_TRUNC));
  if (!scanFile_) { failed_ = true; return false; }
  if (resume) {
    if (scanFile_.fileSize64() < 12) { failed_ = true; return false; }
    count_ = static_cast<uint32_t>((scanFile_.fileSize64() - 12) / sizeof(Record));
    if (!loadResumeCursor()) { failed_ = true; return false; }
  } else {
    const uint16_t version = kVersion, reserved = 0;
    const uint32_t zero = 0;
    if (scanFile_.write(&kMagic, 4) != 4 || scanFile_.write(&version, 2) != 2 ||
        scanFile_.write(&reserved, 2) != 2 || scanFile_.write(&zero, 4) != 4) { failed_ = true; return false; }
    frames_.reserve(16);  // a folder path and handle per nesting level; no book list
    auto root = Storage.open("/");
    if (!root || !root.isDirectory()) { failed_ = true; return false; }
    frames_.push_back(Frame{"/", std::move(root), 0});
  }
  failed_ = false; scanning_ = true;
  return true;
}

bool RetroInkLibraryCatalog::openCached() {
  close();
  count_ = viewCount_ = 0;
  failed_ = false;
  if (!Storage.ready() || Storage.exists(MOVE_HINTS) || Storage.exists(DIRTY) ||
      (Storage.exists(SCAN) && Storage.exists(CURSOR)) || !openComplete()) return false;
  if (!Storage.exists(DEFAULT_VIEW) &&
      !sortAndWriteIndex(Sort::Title, Shelf::All, DEFAULT_VIEW)) {
    catalogFile_.close();
    return false;
  }
  viewFile_ = Storage.open(DEFAULT_VIEW);
  if (!viewFile_ || viewFile_.fileSize64() % sizeof(uint32_t) != 0) {
    viewFile_.close();
    catalogFile_.close();
    return false;
  }
  viewCount_ = static_cast<uint32_t>(viewFile_.fileSize64() / sizeof(uint32_t));
  if (viewCount_ > count_) {
    viewFile_.close();
    catalogFile_.close();
    return false;
  }
  if (viewCount_) {
    uint32_t first = 0, last = 0;
    if (!viewFile_.seek(0) || viewFile_.read(&first, sizeof(first)) != sizeof(first) ||
        !viewFile_.seek((viewCount_ - 1) * sizeof(last)) ||
        viewFile_.read(&last, sizeof(last)) != sizeof(last) || first >= count_ || last >= count_) {
      viewFile_.close();
      catalogFile_.close();
      return false;
    }
  }
  LOG_INF("Library", "Opened cached catalog: %lu books, no folder scan",
          static_cast<unsigned long>(count_));
  return true;
}

void RetroInkLibraryCatalog::noteChangedBook(const char* path, bool folder) {
  if (!path || (!folder && !supported(path)) || !Storage.exists(CATALOG)) return;
  FsFile marker;
  if (!Storage.openFileForWrite("Library", DIRTY, marker)) return;
  const uint8_t one = 1;
  marker.write(&one, sizeof(one));
  marker.sync();
  marker.close();
}

bool RetroInkLibraryCatalog::syncBookState(const char* path) {
  if (!path || !supported(path) || !Storage.exists(CATALOG) ||
      Storage.exists(MOVE_HINTS)) return false;
  FsFile catalog = Storage.open(CATALOG, O_RDWR);
  FsFile index = Storage.open(PATH_INDEX);
  if (!catalog || !index || catalog.fileSize64() < 12) {
    LOG_ERR("Library", "Single-book sync cannot open catalog/index for %s", path);
    return false;
  }
  uint32_t magic = 0, count = 0;
  uint16_t version = 0;
  if (catalog.read(&magic, 4) != 4 || catalog.read(&version, 2) != 2 ||
      !catalog.seek(8) || catalog.read(&count, 4) != 4 ||
      magic != kMagic || version != kVersion || count > kMaxBooks ||
      index.fileSize64() != static_cast<uint64_t>(count) * sizeof(uint32_t)) {
    LOG_ERR("Library", "Single-book sync invalid index: books=%lu bytes=%llu",
            static_cast<unsigned long>(count), static_cast<unsigned long long>(index.fileSize64()));
    return false;
  }
  Record record;
  uint32_t lo = 0, hi = count, id = 0;
  auto readAt = [&](uint32_t row) {
    return index.seek(row * sizeof(id)) && index.read(&id, sizeof(id)) == sizeof(id) &&
           catalog.seek64(12ULL + static_cast<uint64_t>(id) * sizeof(record)) &&
           catalog.read(&record, sizeof(record)) == sizeof(record);
  };
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!readAt(mid)) {
      LOG_ERR("Library", "Single-book sync index read failed at %lu", static_cast<unsigned long>(mid));
      return false;
    }
    if (compareCasefold(record.path, path) < 0) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= count || !readAt(lo) || compareCasefold(record.path, path) != 0) {
    LOG_ERR("Library", "Single-book sync path not indexed: %s (row=%lu candidate=%s)", path,
            static_cast<unsigned long>(lo), record.path);
    return false;
  }
  uint8_t flags = record.flags & FAVORITE;
  const std::string bookPath(path);
  const std::string cache = FsHelpers::hasEpubExtension(bookPath)
                                ? Epub::cachePathForFilePath(bookPath, "/.crosspoint")
                                : FsHelpers::hasXtcExtension(bookPath)
                                      ? Xtc(bookPath, "/.crosspoint").getCachePath()
                                      : Txt(bookPath, "/.crosspoint").getCachePath();
  if (Storage.exists((cache + "/progress.bin").c_str())) flags |= ACTIVITY;
  if (FsHelpers::hasEpubExtension(bookPath) || FsHelpers::hasXtcExtension(bookPath)) {
    const BookReadingStats stats = BookReadingStats::load(cache);
    if (stats.sessionCount || stats.totalReadingSeconds || stats.totalPagesTurned) flags |= ACTIVITY;
    if (stats.isCompleted) flags |= FINISHED;
  }
  if (flags == record.flags) return true;
  record.flags = flags;
  return catalog.seek64(12ULL + static_cast<uint64_t>(id) * sizeof(record)) &&
         catalog.write(&record, sizeof(record)) == sizeof(record) && catalog.sync();
}

bool RetroInkLibraryCatalog::stepScan() {
  if (!scanning_ || failed_) return false;
  if (frames_.empty()) return finishScan();
  auto& frame = frames_.back();
  auto child = frame.dir.openNextFile();
  if (!child) {
    frame.dir.close(); frames_.pop_back();
    if (frames_.empty()) return finishScan();
    return true;
  }
  ++frame.entryNo;
  char name[256]; child.getName(name, sizeof(name));
  const bool dir = child.isDirectory();
  // Captured from the walk's own open handle -- the directory entry is
  // already in memory here, so this costs nothing extra. Passed through to
  // metadataFor so it can skip a second open+read when nothing changed.
  const uint64_t sizeHint = child.fileSize64();
  uint16_t mtimeDate = 0, mtimeTime = 0;
  child.getLastWriteTime(mtimeDate, mtimeTime);
  child.close();
  if (name[0] == '.' || strcmp(name, "") == 0) return true;
  const std::string path = frame.path == "/" ? std::string("/") + name : frame.path + "/" + name;
  currentPath_ = path;
  if (dir) {
    if (excludedDirectory(name)) return true;
    if (frames_.size() >= 24) { LOG_ERR("Library", "Folder nesting exceeds 24 at %s", path.c_str()); failed_ = true; return false; }
    auto nested = Storage.open(path.c_str());
    if (!nested || !nested.isDirectory()) { failed_ = true; return false; }
    frames_.push_back(Frame{path, std::move(nested), 0});
  } else if (supported(path.c_str())) {
    if (!metadataFor(path, sizeHint, mtimeDate, mtimeTime, scratchA_) || !writeRecord(scratchA_)) {
      failed_ = true; LOG_ERR("Library", "Scan paused at %s", path.c_str()); return false;
    }
  }
  return true;
}

bool RetroInkLibraryCatalog::sortAndWriteIndex(Sort sort, Shelf shelf, const char* output) {
  const uint32_t total = static_cast<uint32_t>((catalogFile_.fileSize64() - 12) / sizeof(Record));
  if (total > kMaxBooks) return false;
  // At most 16 KB for 4,096 record IDs. Stack/static storage would reserve
  // that much RAM on every screen; this one fallible allocation is released
  // immediately after the SD-backed index has been written.
  auto ids = makeUniqueNoThrow<uint32_t[]>(total);
  if (total && !ids) { LOG_ERR("Library", "No heap for %u catalog sort IDs", total); return false; }
  uint32_t used = 0;
  LOG_INF("Library", "Sort %u books: freeHeap=%u maxAlloc=%u", total,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  for (uint32_t id = 0; id < total; ++id) {
    if (!readRecord(catalogFile_, id, scratchA_)) return false;
    const uint8_t flags = scratchA_.flags;
    const bool include = shelf == Shelf::All ||
                         (shelf == Shelf::ToRead && !(flags & ACTIVITY) && !(flags & FINISHED)) ||
                         (shelf == Shelf::Reading && (flags & ACTIVITY) && !(flags & FINISHED)) ||
                         (shelf == Shelf::Finished && (flags & FINISHED)) ||
                         (shelf == Shelf::Favorites && (flags & FAVORITE));
    if (include) ids[used++] = id;
  }
  const bool pathOrder = strcmp(output, PATH_INDEX) == 0;
  if (used > 1) std::sort(ids.get(), ids.get() + used, [this, sort, pathOrder](uint32_t a, uint32_t b) {
    if (!readRecord(catalogFile_, a, scratchA_) || !readRecord(catalogFile_, b, scratchB_)) return a < b;
    if (pathOrder) return compareCasefold(scratchA_.path, scratchB_.path) < 0;
    if (sort == Sort::Recent) {
      const uint32_t leftRank = liveRecentRank(scratchA_.path);
      const uint32_t rightRank = liveRecentRank(scratchB_.path);
      if (leftRank != rightRank) return leftRank < rightRank;
    }
    if (sort == Sort::Title && letterBucket(scratchA_.title) != letterBucket(scratchB_.title))
      return letterBucket(scratchA_.title) < letterBucket(scratchB_.title);
    const char* left = sort == Sort::Author ? scratchA_.author :
                       sort == Sort::Filename ? filename(scratchA_.path) :
                       sort == Sort::Recent ? scratchA_.title : scratchA_.title;
    const char* right = sort == Sort::Author ? scratchB_.author :
                        sort == Sort::Filename ? filename(scratchB_.path) : scratchB_.title;
    int cmp = compareCasefold(left, right);
    if (cmp == 0) cmp = compareCasefold(scratchA_.path, scratchB_.path);
    return cmp < 0;
  });
  FsFile out;
  const char* tmp = strcmp(output, PATH_INDEX) == 0 ? PATH_TMP : VIEW_TMP;
  if (!Storage.openFileForWrite("Library", tmp, out)) return false;
  bool ok = true;
  for (uint32_t i = 0; i < used; ++i)
    if (out.write(&ids[i], sizeof(uint32_t)) != sizeof(uint32_t)) { ok = false; break; }
  ok = out.sync() && ok;
  out.close();
  if (!ok) return false;
  Storage.remove(output);
  if (!Storage.rename(tmp, output)) return false;
  if (strcmp(output, VIEW) == 0 || strcmp(output, DEFAULT_VIEW) == 0) viewCount_ = used;
  return true;
}

bool RetroInkLibraryCatalog::appendOrphan(const Record& record) {
  FsFile orphan = Storage.open(ORPHANS, O_RDWR | O_CREAT);
  if (!orphan) return false;
  if (orphan.fileSize64() < 12) {
    const uint16_t version = 1, reserved = 0;
    const uint32_t zero = 0;
    if (orphan.write(&ORPHAN_MAGIC, 4) != 4 || orphan.write(&version, 2) != 2 ||
        orphan.write(&reserved, 2) != 2 || orphan.write(&zero, 4) != 4) { orphan.close(); return false; }
  }
  const uint32_t count = static_cast<uint32_t>((orphan.fileSize64() - 12) / sizeof(Record));
  for (uint32_t i = 0; i < count; ++i) {
    if (!readRecord(orphan, i, scratchB_)) { orphan.close(); return false; }
    if (strcmp(record.path, scratchB_.path) == 0) { orphan.close(); return true; }
  }
  const uint32_t newCount = count + 1;
  const bool ok = orphan.seek64(12ULL + static_cast<uint64_t>(count) * sizeof(Record)) &&
                  orphan.write(&record, sizeof(record)) == sizeof(record) && orphan.seek(8) &&
                  orphan.write(&newCount, 4) == 4 && orphan.sync();
  orphan.close();
  return ok;
}

bool RetroInkLibraryCatalog::preserveOrphans() {
  if (oldFile_ && oldFile_.fileSize64() >= 12) {
    const uint32_t oldCount = static_cast<uint32_t>((oldFile_.fileSize64() - 12) / sizeof(Record));
    for (uint32_t i = 0; i < oldCount; ++i) {
      if (!readRecord(oldFile_, i, scratchA_)) return false;
      if ((scratchA_.flags & (ACTIVITY | FINISHED)) && !Storage.exists(scratchA_.path) &&
          stateForPath(scratchA_.path) &&
          !appendOrphan(scratchA_)) return false;
    }
  }
  // Older CrossInk books can still be located through recents even when there
  // was no RetroInk fingerprint at the time of the external move.
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (Storage.exists(recent.path.c_str()) || !supported(recent.path.c_str())) continue;
    const std::string cache = FsHelpers::hasEpubExtension(recent.path)
                                  ? Epub::cachePathForFilePath(recent.path, "/.crosspoint")
                                  : FsHelpers::hasXtcExtension(recent.path)
                                        ? Xtc(recent.path, "/.crosspoint").getCachePath()
                                        : Txt(recent.path, "/.crosspoint").getCachePath();
    if (!Storage.exists(cache.c_str())) continue;
    scratchA_ = Record{};
    copyText(scratchA_.path, recent.path); copyText(scratchA_.title, recent.title);
    copyText(scratchA_.author, recent.author); scratchA_.flags = ACTIVITY;
    if (!appendOrphan(scratchA_)) return false;
  }
  return scanLegacyHeaders("/.crosspoint/bookmarks", true) &&
         scanLegacyHeaders("/.crosspoint/clippings", false);
}

bool RetroInkLibraryCatalog::scanLegacyHeaders(const char* directory, bool bookmarks) {
  if (!Storage.exists(directory)) return true;
  auto dir = Storage.open(directory);
  if (!dir || !dir.isDirectory()) return false;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    const bool isDir = entry.isDirectory(); entry.close();
    if (isDir || !FsHelpers::checkFileExtension(std::string_view{name}, ".bin")) continue;
    FsFile file;
    const std::string fullPath = std::string(directory) + "/" + name;
    if (!Storage.openFileForRead("Library", fullPath, file)) continue;
    uint8_t version = 0; uint16_t count = 0;
    bool ok = file.read(&version, 1) == 1;
    if (bookmarks) {
      if (version == 2) { uint8_t tiny = 0; ok = ok && file.read(&tiny, 1) == 1; count = tiny; }
      else ok = ok && version >= 3 && version <= 5 && file.read(&count, 2) == 2;
    } else ok = ok && version >= 1 && version <= 3 && file.read(&count, 2) == 2;
    scratchA_ = Record{};
    auto readText = [&file](char* dest, size_t capacity) {
      uint32_t len = 0;
      if (file.read(&len, 4) != 4 || len >= capacity) return false;
      if (file.read(dest, len) != static_cast<int>(len)) return false;
      dest[len] = '\0'; return true;
    };
    ok = ok && count > 0 && readText(scratchA_.title, sizeof(scratchA_.title)) &&
         readText(scratchA_.author, sizeof(scratchA_.author)) &&
         readText(scratchA_.path, sizeof(scratchA_.path));
    file.close();
    if (!ok || !supported(scratchA_.path) || Storage.exists(scratchA_.path)) continue;
    scratchA_.flags = ACTIVITY;
    if (!appendOrphan(scratchA_)) { dir.close(); return false; }
  }
  dir.close();
  return true;
}

bool RetroInkLibraryCatalog::finishScan() {
  if (!scanFile_) return false;
  scanning_ = false;
  const uint32_t count = count_;
  const bool headerOk = scanFile_.seek(8) && scanFile_.write(&count, sizeof(count)) == sizeof(count) && scanFile_.sync();
  const bool orphansOk = headerOk && preserveOrphans();
  scanFile_.close(); oldFile_.close(); oldPathIndex_.close();
  if (!orphansOk) { failed_ = true; return false; }
  if (Storage.exists(CATALOG_BAK) && !Storage.remove(CATALOG_BAK)) { failed_ = true; return false; }
  if (Storage.exists(PATH_BAK) && !Storage.remove(PATH_BAK)) { failed_ = true; return false; }
  const bool hadCatalog = Storage.exists(CATALOG);
  const bool hadIndex = Storage.exists(PATH_INDEX);
  if (hadCatalog && !Storage.rename(CATALOG, CATALOG_BAK)) { failed_ = true; return false; }
  if (hadIndex && !Storage.rename(PATH_INDEX, PATH_BAK)) {
    if (hadCatalog) Storage.rename(CATALOG_BAK, CATALOG);
    failed_ = true; return false;
  }
  if (!Storage.rename(SCAN, CATALOG) || !openComplete() ||
      !sortAndWriteIndex(Sort::Filename, Shelf::All, PATH_INDEX)) {
    catalogFile_.close();
    Storage.remove(CATALOG);
    if (hadCatalog) Storage.rename(CATALOG_BAK, CATALOG);
    if (hadIndex) Storage.rename(PATH_BAK, PATH_INDEX);
    failed_ = true; return false;
  }
  Storage.remove(CURSOR);
  Storage.remove(MOVE_HINTS);  // all hinted paths are now reflected in this complete catalog
  Storage.remove(DIRTY);
  if (!sortAndWriteIndex(Sort::Title, Shelf::All, DEFAULT_VIEW)) { failed_ = true; return false; }
  viewFile_.close();
  viewFile_ = Storage.open(DEFAULT_VIEW);
  if (!viewFile_) { failed_ = true; return false; }
  return true;
}

bool RetroInkLibraryCatalog::openComplete() {
  catalogFile_.close();
  catalogFile_ = Storage.open(CATALOG, O_RDWR);
  if (!catalogFile_) return false;
  uint32_t magic = 0, count = 0; uint16_t version = 0;
  if (catalogFile_.read(&magic, 4) != 4 || catalogFile_.read(&version, 2) != 2 ||
      !catalogFile_.seek(8) || catalogFile_.read(&count, 4) != 4 || magic != kMagic || version != kVersion ||
      catalogFile_.fileSize64() < 12ULL + static_cast<uint64_t>(count) * sizeof(Record)) return false;
  count_ = count;
  return true;
}

bool RetroInkLibraryCatalog::buildView(Shelf shelf, Sort sort) {
  if (!catalogFile_ && !openComplete()) return false;
  viewFile_.close();
  if (!sortAndWriteIndex(sort, shelf, VIEW)) return false;
  viewFile_ = Storage.open(VIEW);
  return !!viewFile_;
}

bool RetroInkLibraryCatalog::viewRecord(uint32_t row, Record& out) {
  if (!viewFile_ || row >= viewCount_) return false;
  uint32_t id = 0;
  return viewFile_.seek(row * sizeof(id)) && viewFile_.read(&id, sizeof(id)) == sizeof(id) &&
         readRecord(catalogFile_, id, out);
}

bool RetroInkLibraryCatalog::setFavorite(uint32_t row, bool favorite) {
  if (!viewFile_ || row >= viewCount_) return false;
  uint32_t id = 0;
  if (!viewFile_.seek(row * sizeof(id)) || viewFile_.read(&id, sizeof(id)) != sizeof(id) ||
      !readRecord(catalogFile_, id, scratchA_)) return false;
  if (favorite) scratchA_.flags |= FAVORITE;
  else scratchA_.flags &= ~FAVORITE;
  return catalogFile_.seek64(12ULL + static_cast<uint64_t>(id) * sizeof(Record)) &&
         catalogFile_.write(&scratchA_, sizeof(scratchA_)) == sizeof(scratchA_) && catalogFile_.sync();
}

uint32_t RetroInkLibraryCatalog::recoveryCount(const Record& current) {
  FsFile orphan = Storage.open(ORPHANS);
  if (!orphan || orphan.fileSize64() < 12) return 0;
  const uint32_t count = static_cast<uint32_t>((orphan.fileSize64() - 12) / sizeof(Record));
  uint32_t matches = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (!readRecord(orphan, i, scratchB_)) break;
    const bool same = scratchB_.fingerprint
                          ? scratchB_.fingerprint == current.fingerprint && scratchB_.size == current.size
                          : compareCasefold(scratchB_.title, current.title) == 0 &&
                                (scratchB_.author[0] == 0 || compareCasefold(scratchB_.author, current.author) == 0);
    if (same && strcmp(scratchB_.path, current.path) != 0 && !Storage.exists(scratchB_.path) &&
        stateForPath(scratchB_.path)) ++matches;
  }
  orphan.close();
  return matches;
}

bool RetroInkLibraryCatalog::recoveryCandidate(const Record& current, uint32_t match, Record& out) {
  FsFile orphan = Storage.open(ORPHANS);
  if (!orphan || orphan.fileSize64() < 12) return false;
  const uint32_t count = static_cast<uint32_t>((orphan.fileSize64() - 12) / sizeof(Record));
  uint32_t seen = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (!readRecord(orphan, i, scratchB_)) break;
    const bool same = scratchB_.fingerprint
                          ? scratchB_.fingerprint == current.fingerprint && scratchB_.size == current.size
                          : compareCasefold(scratchB_.title, current.title) == 0 &&
                                (scratchB_.author[0] == 0 || compareCasefold(scratchB_.author, current.author) == 0);
    if (same && strcmp(scratchB_.path, current.path) != 0 && !Storage.exists(scratchB_.path) &&
        stateForPath(scratchB_.path)) {
      if (seen++ == match) { out = scratchB_; orphan.close(); return true; }
    }
  }
  orphan.close();
  return false;
}

void RetroInkLibraryCatalog::pauseScan() {
  if (scanning_) saveResumeCursor();
  scanning_ = false;
}

void RetroInkLibraryCatalog::close() {
  for (auto& frame : frames_) frame.dir.close();
  frames_.clear();
  scanFile_.close(); oldFile_.close(); oldPathIndex_.close();
  catalogFile_.close(); viewFile_.close();
  scanning_ = false;
}
