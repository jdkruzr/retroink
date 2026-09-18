#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

// Fixed records live on SD; only a 4-byte-per-book sort index is kept in RAM
// while building a view. At 1,100 books that transient index is 4.4 KB.
class RetroInkLibraryCatalog {
 public:
  enum class Shelf : uint8_t { All, ToRead, Reading, Finished, Favorites };
  enum class Sort : uint8_t { Title, Filename, Author, Recent };
  // Record::flags bit meanings, mirrored from the anonymous-namespace
  // constants in RetroInkLibraryCatalog.cpp (kept in sync manually; bumping
  // the record layout already requires touching both, per the static_assert
  // below). Exposed so callers outside this file (e.g. the web API) can
  // read a Record's shelf membership without duplicating the Shelf-from-
  // flags logic in sortAndWriteIndex().
  static constexpr uint8_t FLAG_ACTIVITY = 1, FLAG_FINISHED = 2, FLAG_FAVORITE = 4;
  struct Record {
    char path[512] = {};
    char title[224] = {};
    char author[112] = {};
    uint64_t size = 0;
    uint64_t fingerprint = 0;
    uint32_t recentRank = UINT32_MAX;
    uint8_t flags = 0;  // bit 0 activity, bit 1 finished, bit 2 favorite
  };
  static_assert(sizeof(Record) == 872, "Bump Library catalog version if record layout changes");

 private:
  struct Frame {
    std::string path;
    HalFile dir;
    uint32_t entryNo = 0;
  };
  static constexpr uint32_t kMagic = 0x52494c42;  // RILB
  static constexpr uint16_t kVersion = 1;
  static constexpr uint32_t kMaxBooks = 4096;
  std::vector<Frame> frames_;
  FsFile scanFile_;
  FsFile oldFile_;
  FsFile oldPathIndex_;
  FsFile catalogFile_;
  FsFile viewFile_;
  Record scratchA_;
  Record scratchB_;
  uint32_t count_ = 0;
  uint32_t viewCount_ = 0;
  bool scanning_ = false;
  bool failed_ = false;
  bool hasMoveHints_ = false;
  std::string currentPath_;

  bool loadResumeCursor();
  bool saveResumeCursor();
  bool findOldRecord(const char* path, Record& out);
  bool findMoveSource(const std::string& path, std::string& oldPath);
  bool readRecord(FsFile& file, uint32_t id, Record& out);
  bool writeRecord(const Record& record);
  bool openComplete();
  bool sortAndWriteIndex(Sort sort, Shelf shelf, const char* output);
  bool metadataFor(const std::string& path, Record& record);
  bool appendOrphan(const Record& record);
  bool preserveOrphans();
  bool scanLegacyHeaders(const char* directory, bool bookmarks);
  static uint64_t fingerprintFor(const std::string& path, uint64_t& size);
  static bool supported(const char* path);

 public:
  ~RetroInkLibraryCatalog() { close(); }
  // File-transfer callers mark book additions/removals. The next Library
  // entry then reconciles the SD catalog once.
  static void noteChangedBook(const char* path, bool folder = false);
  // Update one path's virtual shelf flags after a reader commits progress or
  // completed status. Does not scan folders or re-read EPUB metadata.
  static bool syncBookState(const char* path);
  // Reuse the completed default All Books / Title A-Z view without walking
  // folders or sorting records again. A partial scan or move hint resumes the
  // regular scan instead.
  bool openCached();
  bool beginScan();
  // Processes at most one directory entry; call from Activity::loop.
  bool stepScan();
  bool finishScan();
  void pauseScan();
  void close();
  bool buildView(Shelf shelf, Sort sort);
  bool viewRecord(uint32_t row, Record& out);
  bool setFavorite(uint32_t row, bool favorite);
  uint32_t recoveryCount(const Record& current);
  bool recoveryCandidate(const Record& current, uint32_t match, Record& out);
  bool isScanning() const { return scanning_; }
  bool failed() const { return failed_; }
  uint32_t scannedCount() const { return count_; }
  uint32_t viewCount() const { return viewCount_; }
  const std::string& currentPath() const { return currentPath_; }
};
