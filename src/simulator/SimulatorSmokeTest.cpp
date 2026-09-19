#ifdef SIMULATOR

#include "SimulatorSmokeTest.h"

#include <HalStorage.h>
#include <Epub.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/ChargingActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/FocusSessionActivity.h"
#include "activities/reader/ReadingDeskStore.h"
#include "activities/reader/RetroInkGoalCountdown.h"
#include "activities/reader/RetroInkFocusDeskActivity.h"
#include "activities/reader/ReaderOptionsActivity.h"
#include "activities/home/RetroInkCustomShelves.h"
#include "activities/home/RetroInkLibraryCatalog.h"
#include "activities/home/RetroInkLibraryActivity.h"
#include "util/BookMoveUtils.h"
#include <Txt.h>
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "simulator/SimulatorHomeKeyInput.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {

enum class SmokeStep : uint8_t {
  Start,
  Home,
  Library,
  LibraryShelfDownPress,
  LibraryShelfDownRelease,
  LibraryShelfDown,
  LibraryPageRightPress,
  LibraryPageRightRelease,
  LibraryPageRight,
  LibraryShelfUpPress,
  LibraryShelfUpRelease,
  LibraryShelfUp,
  LibraryActionsPress,
  LibraryActionsRelease,
  LibraryActions,
  LibraryBack,
  LibraryReturnHome,
  HomeFocusNavigatePress,
  HomeFocusNavigateRelease,
  HomeFocusConfirmPress,
  HomeFocusConfirmRelease,
  FocusDesk,
  FocusSettingsSelectRelease,
  FocusSettingsOpenRelease,
  FocusSettingsMenu,
  FocusLengthOpenRelease,
  FocusLengthEditor,
  FocusLengthDoneRelease,
  FocusRefreshSelectPress,
  FocusRefreshSelectRelease,
  FocusRefreshOpenRelease,
  FocusRefreshSecondPress,
  FocusRefreshSecondRelease,
  FocusRefreshEditor,
  FocusRefreshChosenRelease,
  FocusRefreshSaved,
  FocusDeskStart,
  FocusStartRelease,
  FocusTimer,
  FocusTimerRedrawWait,
  FocusTimerPostRedraw,
  FocusBackPress,
  FocusBackRelease,
  FocusBackExited,
  FocusEndActivePress,
  FocusEndActiveRelease,
  FocusEndActiveExited,
  FocusCompletion,
  FocusEndPress,
  FocusEndRelease,
  FocusEndExited,
  StatsToday,
  StatsYear,
  StatsBookStatus,
  StatsActionsPress,
  StatsActionsRelease,
  StatsActions,
  StatsGoalPress,
  StatsGoalRelease,
  StatsGoalEditor,
  StatsGoalAdjust,
  FileBrowserRoot,
  FileBrowser,
  RecentBooks,
  HomeSettingsSelected,
  HomeSettingsOpen,
  Settings,
  SettingsAdvancePress,
  SettingsAdvanceRelease,
  SettingsSystem,
  ReaderOptions,
  ReaderMenu,
  Sleep,
  Reader,
  ReaderInput,
  Done,
};

class SimulatorSmokeTest {
 public:
  void tick() {
    if (!enabled()) return;

    try {
      tickImpl();
    } catch (const std::exception& e) {
      fail("Unhandled exception: %s", e.what());
    } catch (...) {
      fail("Unhandled non-standard exception");
    }
  }

 private:
  enum class ScriptActionType : uint8_t {
    Press,
    Release,
    HomeTap,
    HomeLongPress,
    OpenSmokeBook,
    DisableReaderTouch,
    EnableReaderTouch,
    TouchDown,
    TouchMove,
    TouchRelease,
    AssertActivity,
    Render
  };

  struct ScriptAction {
    ScriptActionType type;
    MappedInputManager::Button button;
    const char* label;
    int settleFrames;
    int x;
    int y;
  };

  SmokeStep step = SmokeStep::Start;
  int settleFrames = 0;
  const char* activeStepName = nullptr;
  std::vector<ScriptAction> inputScript;
  size_t scriptIndex = 0;
  int settingsCategoryAdvances = 0;
  int homeFocusAdvances = 0;
  uint32_t focusRedrawWaitStartedMs = 0;

  static bool enabled() { return std::getenv("CROSSINK_SIMULATOR_SMOKE_TEST") != nullptr; }

  static void runLibraryFixtureChecks() {
    const char* sourceRaw = std::getenv("CROSSINK_SIMULATOR_LIBRARY_SOURCE");
    if (!sourceRaw || !sourceRaw[0]) fail("Library fixture source missing");
    const std::string source(sourceRaw);
    const std::string sourceCache = Txt(source, "/.crosspoint").getCachePath();
    if (!Storage.mkdir(sourceCache.c_str())) fail("Could not make TXT cache fixture");
    FsFile progress;
    if (!Storage.openFileForWrite("SMOKE", sourceCache + "/progress.bin", progress))
      fail("Could not write TXT progress fixture");
    const uint8_t page[] = {4, 0, 0, 0, 0, 0};
    if (progress.write(page, sizeof(page)) != sizeof(page)) fail("Short TXT progress fixture write");
    progress.close();
    BookReadingStats sourceStats;
    sourceStats.sessionCount = 1;
    sourceStats.totalReadingSeconds = 420;
    sourceStats.totalPagesTurned = 4;
    sourceStats.save(sourceCache);
    if (!BOOKMARKS.loadForBook(source, "Book 0000", "", "txt")) fail("Cannot create bookmark fixture");
    BOOKMARKS.addBookmark(0, 0.25f, 10, "Chapter");
    BOOKMARKS.saveToFile(); BOOKMARKS.unload();
    RECENT_BOOKS.addOrUpdateBook(source, "Book 0000", "", "");
    APP_STATE.openEpubPath = source; APP_STATE.saveToFile();
    const std::string finishedPath = "/books/Genre 10/Author 04/Book 1099.epub";
    const std::string finishedCache = Epub::cachePathForFilePath(finishedPath, "/.crosspoint");
    if (!Storage.mkdir(finishedCache.c_str())) fail("Cannot make finished EPUB fixture cache");
    BookReadingStats finishedStats;
    finishedStats.sessionCount = 1;
    finishedStats.isCompleted = true;
    finishedStats.save(finishedCache);
    if (!CLIPPINGS.loadForBook(finishedPath, "The Retro Macintosh Reader", "AltFlow", "epub"))
      fail("Cannot create EPUB clipping fixture");
    if (CLIPPINGS.addClipping(0, 0, 0, 1, 0, 1, 2, "Chapter", UINT16_MAX, "example clipping", 0) ==
        ClippingStore::AddResult::SaveFailed) fail("Cannot create EPUB clipping fixture");
    CLIPPINGS.saveToFile(); CLIPPINGS.unload();

    auto scan = [](RetroInkLibraryCatalog& catalog) {
      if (!catalog.beginScan()) fail("Library scan did not start");
      unsigned steps = 0;
      while (catalog.isScanning()) {
        if (!catalog.stepScan()) fail("Library scan failed");
        if (++steps > 5000) fail("Library scan did not finish");
      }
    };
    RetroInkLibraryCatalog catalog;
    scan(catalog);
    if (catalog.scannedCount() != 1100 || catalog.viewCount() != 1100)
      fail("Library did not catalog 1,100 nested books: %u", catalog.scannedCount());
    RetroInkLibraryCatalog::Record row;
    bool streamedMetadata = false, fallbackTitle = false;
    for (uint32_t i = 0; i < catalog.viewCount(); ++i) {
      if (!catalog.viewRecord(i, row)) fail("Library row read failed");
      if (std::strstr(row.path, "Book 1099.epub"))
        streamedMetadata = std::strcmp(row.title, "The Retro Macintosh Reader") == 0 &&
                           std::strcmp(row.author, "AltFlow") == 0;
      if (std::strstr(row.path, "Book 1098.epub"))
        fallbackTitle = std::strcmp(row.title, "Book 1098.epub") == 0;
    }
    if (!streamedMetadata || !fallbackTitle) fail("EPUB streamed metadata/fallback title failed");
    if (!catalog.viewRecord(0, row) || row.title[0] != '#') fail("Title sort did not put # bucket first");
    if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::ToRead, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1098) fail("To Read shelf did not exclude active and finished books");
    if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::Reading, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1 || !catalog.viewRecord(0, row) || source != row.path)
      fail("Reading shelf did not find the active TXT book");
    if (!catalog.setFavorite(0, true) ||
        !catalog.buildView(RetroInkLibraryCatalog::Shelf::Favorites, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1) fail("Favorites pin was not persisted in catalog");
    if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::Finished, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1 || !catalog.viewRecord(0, row) || finishedPath != row.path)
      fail("Finished shelf did not use the CrossInk completed flag");

    // Custom shelves: verify the fingerprint-based row filtering that
    // RetroInkLibraryActivity::rebuildCustomFilter() relies on, against the
    // real 1,100-book catalog (not just the isolated store round-trip test
    // elsewhere in this file).
    {
      Storage.remove("/.retroink/library.shelves.bin");
      Storage.remove("/.retroink/library.shelf_assignments.bin");
      RetroInkCustomShelves customShelves;
      if (!customShelves.load()) fail("Fresh shelf store should load as empty");
      const uint16_t beachId = customShelves.createShelf("Beach Reads");
      if (beachId == RetroInkCustomShelves::kNoShelf) fail("createShelf failed for the Library fixture");
      if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::All, RetroInkLibraryCatalog::Sort::Title))
        fail("Could not build All view for the custom-shelf filtering check");
      unsigned assigned = 0;
      for (uint32_t i = 0; i < catalog.viewCount() && assigned < 5; ++i) {
        if (!catalog.viewRecord(i, row)) fail("Row read failed while assigning to a custom shelf");
        if (!row.fingerprint) continue;
        if (!customShelves.assignToShelf(row.fingerprint, beachId))
          fail("assignToShelf failed for a real catalog fingerprint");
        ++assigned;
      }
      if (assigned != 5) fail("Could not find 5 fingerprinted books to assign");
      if (!customShelves.save()) fail("Could not save custom shelves for the Library fixture");
      unsigned matched = 0;
      for (uint32_t i = 0; i < catalog.viewCount(); ++i) {
        if (!catalog.viewRecord(i, row)) fail("Row read failed while verifying custom-shelf filtering");
        if (row.fingerprint && customShelves.shelfForFingerprint(row.fingerprint) == beachId) ++matched;
      }
      if (matched != 5) fail("Custom-shelf row filtering did not find exactly the assigned books: %u", matched);
      customShelves.deleteShelf(beachId);
      Storage.remove("/.retroink/library.shelves.bin");
      Storage.remove("/.retroink/library.shelf_assignments.bin");
      LOG_INF("SMOKE", "Library custom-shelf row filtering passed");
    }

    for (const auto sort : {RetroInkLibraryCatalog::Sort::Title, RetroInkLibraryCatalog::Sort::Filename,
                            RetroInkLibraryCatalog::Sort::Author, RetroInkLibraryCatalog::Sort::Recent}) {
      if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::All, sort) || catalog.viewCount() != 1100)
        fail("Library sort failed");
    }
    if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::All, RetroInkLibraryCatalog::Sort::Recent) ||
        !catalog.viewRecord(0, row) || source != row.path) fail("Recently Opened did not put recent book first");
    catalog.close();
    Storage.remove("/.retroink/library.title.idx");  // simulate an existing 0.2.0 catalog upgrade
    if (!catalog.openCached() || catalog.isScanning() || catalog.viewCount() != 1100 ||
        !catalog.viewRecord(0, row)) fail("Completed Library did not reopen instantly from its default index");
    if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::Finished, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1) fail("Cached shelf change failed");
    catalog.close();
    if (!catalog.openCached() || catalog.viewCount() != 1100)
      fail("Library reopened the last shelf instead of All Books / Title A-Z");
    catalog.close();
    const std::string newReadingPath = "/books/Genre 00/Author 00/Book 0001.txt";
    const std::string newReadingCache = Txt(newReadingPath, "/.crosspoint").getCachePath();
    if (!Storage.mkdir(newReadingCache.c_str()) ||
        !Storage.openFileForWrite("SMOKE", newReadingCache + "/progress.bin", progress) ||
        progress.write(page, sizeof(page)) != sizeof(page)) fail("Could not create new reading progress");
    progress.close();
    if (!RetroInkLibraryCatalog::syncBookState(newReadingPath.c_str()))
      fail("Single-book progress sync failed");
    if (!catalog.openCached() ||
        !catalog.buildView(RetroInkLibraryCatalog::Shelf::Reading, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 2)
      fail("Reading shelf did not update one committed book without rescanning: %u", catalog.viewCount());
    catalog.close();
    Storage.remove((newReadingCache + "/progress.bin").c_str());
    if (!RetroInkLibraryCatalog::syncBookState(newReadingPath.c_str()) || !catalog.openCached() ||
        !catalog.buildView(RetroInkLibraryCatalog::Shelf::Reading, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1) fail("Removing progress did not update one cached book");
    catalog.close();
    RECENT_BOOKS.addOrUpdateBook(newReadingPath, "Book 0001", "", "");
    if (!catalog.openCached() ||
        !catalog.buildView(RetroInkLibraryCatalog::Shelf::All, RetroInkLibraryCatalog::Sort::Recent) ||
        !catalog.viewRecord(0, row) || newReadingPath != row.path)
      fail("Recently Opened did not use live recents with a cached catalog");
    catalog.close();
    RECENT_BOOKS.addOrUpdateBook(source, "Book 0000", "", "");
    RetroInkLibraryCatalog::noteChangedBook(source.c_str());
    if (!Storage.exists("/.retroink/library.dirty") || catalog.openCached())
      fail("Changed books did not invalidate the cached Library");
    Storage.remove("/.retroink/library.dirty");

    // Back during scan must keep the partial SD catalog and resume without
    // duplicating any row, even through nested folders.
    if (!catalog.beginScan()) fail("Partial scan start failed");
    for (unsigned i = 0; i < 40; ++i) if (!catalog.stepScan()) fail("Partial scan step failed");
    catalog.pauseScan(); catalog.close();
    if (catalog.openCached()) fail("Paused Library scan was incorrectly skipped");
    scan(catalog);
    if (catalog.scannedCount() != 1100)
      fail("Resumed scan duplicated or skipped books: %u", catalog.scannedCount());
    catalog.close();

    if (!Storage.mkdir("/Read")) fail("Cannot create Read fixture folder");
    const std::string readPath = std::string("/Read/") + source.substr(source.find_last_of('/') + 1);
    if (!BookMoveUtils::moveWithState(source, readPath)) fail("Safe TXT move failed");
    const std::string readCache = Txt(readPath, "/.crosspoint").getCachePath();
    if (!Storage.exists((readCache + "/progress.bin").c_str()) || Storage.exists(sourceCache.c_str()) ||
        Storage.exists("/.retroink/move.journal")) fail("Safe TXT move lost progress or journal stayed behind");
    const BookReadingStats movedStats = BookReadingStats::load(readCache);
    if (movedStats.totalReadingSeconds != 420 || movedStats.totalPagesTurned != 4 ||
        RECENT_BOOKS.getBooks().empty() || RECENT_BOOKS.getBooks()[0].path != readPath ||
        APP_STATE.openEpubPath != readPath) fail("Safe move lost stats, recents, or resume path");
    if (!BOOKMARKS.loadForBook(readPath, "Book 0000", "", "txt") || BOOKMARKS.getBookmarks().size() != 1)
      fail("Safe move lost bookmarks");
    BOOKMARKS.unload();
    const std::string finishedReadPath = "/Read/Book 1099.epub";
    if (!BookMoveUtils::moveWithState(finishedPath, finishedReadPath)) fail("Safe EPUB move failed");
    const std::string finishedReadCache = Epub::cachePathForFilePath(finishedReadPath, "/.crosspoint");
    if (!BookReadingStats::load(finishedReadCache).isCompleted || Storage.exists(finishedCache.c_str()))
      fail("Safe EPUB move lost completion/stat cache");
    if (!CLIPPINGS.loadForBook(finishedReadPath, "The Retro Macintosh Reader", "AltFlow", "epub") ||
        CLIPPINGS.clippingCount() != 1) fail("Safe EPUB move lost clippings");
    CLIPPINGS.unload();
    scan(catalog);
    if (!catalog.buildView(RetroInkLibraryCatalog::Shelf::Favorites, RetroInkLibraryCatalog::Sort::Title) ||
        catalog.viewCount() != 1 || !catalog.viewRecord(0, row) || readPath != row.path)
      fail("Favorite pin did not follow the safe move");
    catalog.close();

    if (!Storage.mkdir("/Outside")) fail("Cannot create external move fixture folder");
    const std::string outsidePath = std::string("/Outside/") + source.substr(source.find_last_of('/') + 1);
    if (!Storage.rename(readPath.c_str(), outsidePath.c_str())) fail("External move fixture failed");
    const std::string outsideCache = Txt(outsidePath, "/.crosspoint").getCachePath();
    if (!Storage.mkdir(outsideCache.c_str())) fail("Cannot create current-progress fixture");
    FsFile current;
    if (!Storage.openFileForWrite("SMOKE", outsideCache + "/progress.bin", current))
      fail("Cannot write current-progress fixture");
    const uint8_t currentPage[] = {1, 0, 0, 0, 0, 0};
    current.write(currentPage, sizeof(currentPage)); current.close();
    scan(catalog);
    bool found = false;
    for (uint32_t i = 0; i < catalog.viewCount(); ++i) {
      if (catalog.viewRecord(i, row) && outsidePath == row.path) { found = true; break; }
    }
    if (!found || catalog.recoveryCount(row) != 1) fail("External move was not offered for review");
    RetroInkLibraryCatalog::Record prior;
    if (!catalog.recoveryCandidate(row, 0, prior) || readPath != prior.path)
      fail("External move matched the wrong old record");
    catalog.close();
    // Choosing the current record is a no-op. Choosing the old one then backs
    // up current progress and restores the previous cache to the new path.
    if (!Storage.exists((outsideCache + "/progress.bin").c_str())) fail("Current record vanished before review");
    if (!BookMoveUtils::restorePriorState(readPath, outsidePath)) fail("Old record restore failed");
    if (!Storage.exists((outsideCache + ".retroink_before_1/progress.bin").c_str()) ||
        !Storage.exists((outsideCache + "/progress.bin").c_str()) || Storage.exists(readCache.c_str()))
      fail("Recovery did not preserve current and old progress");

    const std::string folderSource = "/books/Genre 09";
    const std::string folderTarget = "/books/Moved Genre 09";
    const std::string nestedSource = "/books/Genre 09/Author 00/Book 0900.txt";
    const std::string nestedTarget = "/books/Moved Genre 09/Author 00/Book 0900.txt";
    const std::string nestedCache = Txt(nestedSource, "/.crosspoint").getCachePath();
    if (!Storage.mkdir(nestedCache.c_str())) fail("Cannot make folder-move cache fixture");
    FsFile nestedProgress;
    if (!Storage.openFileForWrite("SMOKE", nestedCache + "/progress.bin", nestedProgress))
      fail("Cannot write folder-move progress");
    nestedProgress.write(page, sizeof(page)); nestedProgress.close();
    if (!BookMoveUtils::moveWithState(folderSource, folderTarget)) fail("Safe folder move failed");
    if (Storage.exists(nestedSource.c_str()) || !Storage.exists(nestedTarget.c_str()) ||
        !Storage.exists((Txt(nestedTarget, "/.crosspoint").getCachePath() + "/progress.bin").c_str()))
      fail("Folder move skipped nested book state");

    auto interruptedMove = [&page](const std::string& from, const std::string& to, bool cacheAlreadyMoved) {
      const std::string oldCache = Txt(from, "/.crosspoint").getCachePath();
      const std::string newCache = Txt(to, "/.crosspoint").getCachePath();
      if (!Storage.mkdir(oldCache.c_str())) fail("Cannot make interrupted-move cache");
      FsFile p;
      if (!Storage.openFileForWrite("SMOKE", oldCache + "/progress.bin", p))
        fail("Cannot write interrupted-move progress");
      p.write(page, sizeof(page)); p.close();
      FsFile journal;
      if (!Storage.openFileForWrite("SMOKE", "/.retroink/move.journal", journal))
        fail("Cannot write interrupted-move journal");
      const uint32_t magic = 0x52494d56;
      const uint8_t flags = 2;
      const uint16_t oldLen = from.size(), newLen = to.size();
      bool ok = journal.write(&magic, 4) == 4 && journal.write(&flags, 1) == 1 &&
                journal.write(&oldLen, 2) == 2 && journal.write(&newLen, 2) == 2 &&
                journal.write(from.data(), oldLen) == oldLen &&
                journal.write(to.data(), newLen) == newLen && journal.sync();
      journal.close();
      if (!ok || !Storage.rename(from.c_str(), to.c_str())) fail("Interrupted-move rename fixture failed");
      if (cacheAlreadyMoved && !Storage.rename(oldCache.c_str(), newCache.c_str()))
        fail("Interrupted-move cache-stage fixture failed");
      if (!BookMoveUtils::recoverPendingMove() || Storage.exists("/.retroink/move.journal") ||
          !Storage.exists((newCache + "/progress.bin").c_str()))
        fail("Interrupted move did not recover");
    };
    interruptedMove("/books/Genre 08/Author 00/Book 0800.txt", "/Outside/Book 0800.txt", false);
    interruptedMove("/books/Genre 08/Author 00/Book 0801.txt", "/Outside/Book 0801.txt", true);
    if (std::getenv("CROSSINK_SIMULATOR_CAPTURE_DIR")) {
      activityManager.replaceActivity(std::make_unique<RetroInkLibraryActivity>(renderer, mappedInputManager));
      for (unsigned i = 0; i < 1800; ++i) activityManager.loop();
      renderCurrentStep("Library 1100 books");
    }
    LOG_INF("SMOKE", "RetroInk Library 1,100-book test passed");
    std::_Exit(0);
  }

  static int pageTurnCount() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS");
    if (raw == nullptr || raw[0] == '\0') {
      return 2;
    }
    return std::max(0, std::atoi(raw));
  }

  static void applyRequestedTheme() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    const char* scaleRaw = std::getenv("CROSSINK_SIMULATOR_SMOKE_UI_SCALE");
    const bool showClock = std::getenv("CROSSINK_SIMULATOR_SMOKE_SHOW_CLOCK") != nullptr;
    if ((raw == nullptr || raw[0] == '\0') && (scaleRaw == nullptr || scaleRaw[0] == '\0') && !showClock) return;

    RenderLock lock;  // A startup Home repaint may still own the old theme.
    if (raw != nullptr && raw[0] != '\0') {
      const int theme = std::atoi(raw);
      if (theme < 0 || theme >= CrossPointSettings::UI_THEME_COUNT) {
        fail("Invalid smoke test theme index: %d", theme);
      }
      SETTINGS.uiTheme = static_cast<uint8_t>(theme);
      LOG_INF("SMOKE", "Using theme index %d", theme);
    }
    if (scaleRaw != nullptr && scaleRaw[0] != '\0') {
      const int scale = std::atoi(scaleRaw);
      if (scale < 0 || scale >= CrossPointSettings::UI_SCALE_COUNT) fail("Invalid smoke test UI scale: %d", scale);
      SETTINGS.uiScale = static_cast<uint8_t>(scale);
      LOG_INF("SMOKE", "Using UI scale %d", scale);
    }
    if (showClock) {
      SETTINGS.hideClock = CrossPointSettings::HIDE_CLOCK_NEVER;
      LOG_INF("SMOKE", "Showing clock in all contexts");
    }
    UITheme::getInstance().reload();
  }

  [[noreturn]] static void fail(const char* message) {
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(2);
  }

  template <typename... Args>
  [[noreturn]] static void fail(const char* format, Args... args) {
    logPrintf("ERR", "SMOKE", format, args...);
    logPrintf("ERR", "SMOKE", "\n");
    std::_Exit(2);
  }

  static void captureFrameUnlocked(const char* name) {
    const char* dir = std::getenv("CROSSINK_SIMULATOR_CAPTURE_DIR");
    if (!dir || !dir[0]) return;
    char path[512];
    const int len = std::snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
    if (len < 0 || len >= static_cast<int>(sizeof(path))) fail("Capture path too long");
    FILE* file = std::fopen(path, "wb");
    if (!file) fail("Cannot write capture: %s", path);
    std::fprintf(file, "P4\n%d %d\n", renderer.getDisplayWidth(), renderer.getDisplayHeight());
    const auto* bytes = renderer.getFrameBuffer();
    for (size_t i = 0; i < renderer.getBufferSize(); ++i) std::fputc(bytes[i] ^ 0xff, file);
    const bool error = std::ferror(file);
    if (std::fclose(file) != 0 || error) fail("Capture write failed: %s", path);
  }

  static void captureFrame(const char* name) {
    RenderLock lock;
    captureFrameUnlocked(name);
  }

  static void renderCurrentStep(const char* name) {
    LOG_INF("SMOKE", "Rendering %s", name);
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail("Render was rejected for %s", name);
    }
    captureFrame(name);
  }

  void queueStep(const char* name, SmokeStep nextStep, int framesToSettle = 3) {
    activeStepName = name;
    settleFrames = framesToSettle;
    step = nextStep;
  }

  void tickImpl() {
    mappedInputManager.simulatorClearInputFrame();

    if (settleFrames > 0) {
      --settleFrames;
      if (settleFrames == 0 && activeStepName != nullptr) {
        renderCurrentStep(activeStepName);
        activeStepName = nullptr;
      }
      return;
    }

    switch (step) {
      case SmokeStep::Start:
        LOG_INF("SMOKE", "Starting simulator smoke test");
        if (std::getenv("CROSSINK_SIMULATOR_LIBRARY_TEST")) runLibraryFixtureChecks();
        if (!CrossPointSettings::verifySleepTimeoutMigrationContract()) {
          fail("Sleep timeout migration contract failed");
        }
        if (!CrossPointSettings::verifySleepScreenMigrationContract()) {
          fail("Sleep screen migration contract failed");
        }
        if (!SimulatorHomeKeyInput::verifyTimingContract()) {
          fail("Simulator Home key timing contract failed");
        }
#if CROSSINK_APP_CAP_TOUCH && defined(SIMULATOR_DEVICE_X4_PRO)
        if (!mappedInputManager.hasHomeKey()) fail("X4 Pro simulator must expose its Home key");
#endif
        applyRequestedTheme();
        if (SETTINGS.uiTheme == CrossPointSettings::SYSTEM6) {
          ReadingStatsDateTime today;
          if (getCurrentLocalReadingStatsDateTime(today)) {
            today.hour = today.minute = today.second = 0;
            const uint32_t extraSeconds = (static_cast<uint32_t>(SETTINGS.readingGoalMinutes) + 1U) * 60U;
            ReadingDeskStore::recordReadingSpan(today, extraSeconds);
            if (ReadingDeskStore::secondsForDate(today.date) != extraSeconds)
              fail("Reading Desk did not save an exceeded-goal day");
            const uint8_t savedGoal = SETTINGS.readingGoalMinutes;
            SETTINGS.readingGoalMinutes = static_cast<uint8_t>(savedGoal + 5U);
            SETTINGS.showReadingGoalCountdown = 1;
            const auto before = RetroInkGoalCountdown::sample(today, 0, 0);
            const auto complete = RetroInkGoalCountdown::sample(today, 5U * 60U, 0);
            if (!before.valid || before.minutesLeft != 4 || !complete.valid || complete.minutesLeft != 0)
              fail("Reader goal countdown did not include active session minutes");
            {
              RenderLock lock;
              RetroInkGoalCountdown indicator;
              renderer.clearScreen();
              indicator.drawOnPage(renderer, before, false);
              renderer.displayBuffer();
              indicator.tick(renderer, complete, false);
              if (indicator.needsUpdate(complete)) fail("Goal completion animation did not settle");
              captureFrameUnlocked("Reader goal reached");
            }
            SETTINGS.readingGoalMinutes = savedGoal;
            if (!SETTINGS.saveToFile()) fail("Could not save reader goal countdown setting");
            SETTINGS.showReadingGoalCountdown = 0;
            if (!SETTINGS.loadFromFile() || !SETTINGS.showReadingGoalCountdown)
              fail("Reader goal countdown setting did not survive reload");
          }
          RenderLock lock;
          renderer.clearScreen();
          RetroInkBoot::drawScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight());
          captureFrameUnlocked("Boot requested theme");
          renderer.clearScreen();
          RetroInkBoot::drawSleepScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight());
          captureFrameUnlocked("Sleep");
          const uint8_t savedSleepChoice = SETTINGS.sleepScreen;
          constexpr uint8_t funnyModes[] = {CrossPointSettings::RETROINK_ERROR_404_SLEEP,
                                            CrossPointSettings::RETROINK_INSERT_BOOKMARK_SLEEP,
                                            CrossPointSettings::RETROINK_SYSTEM_NAP_SLEEP};
          for (const uint8_t mode : funnyModes) {
            renderer.clearScreen();
            RetroInkBoot::drawFunnySleepScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), mode);
            captureFrameUnlocked(mode == CrossPointSettings::RETROINK_ERROR_404_SLEEP
                                     ? "Sleep Error 404"
                                     : mode == CrossPointSettings::RETROINK_INSERT_BOOKMARK_SLEEP
                                           ? "Sleep Insert Bookmark"
                                           : "Sleep System Nap");
            SETTINGS.sleepScreen = mode;
            if (!SETTINGS.saveToFile()) fail("Funny sleep choice did not save");
            SETTINGS.sleepScreen = CrossPointSettings::DARK;
            if (!SETTINGS.loadFromFile() || SETTINGS.sleepScreen != mode)
              fail("Funny sleep choice did not persist after reload");
          }
          SETTINGS.sleepScreen = savedSleepChoice;
          SETTINGS.saveToFile();

          // Charging screen: not a settings enum like the sleep screens, so
          // exercise its drawing entry point directly at a few animation
          // steps, plus the activity's own preventAutoSleep() contract that
          // main.cpp's USB-plug guard relies on to keep the screen up.
          renderer.clearScreen();
          RetroInkBoot::drawChargingScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), 0, 0, 12);
          captureFrameUnlocked("Charging empty");
          renderer.clearScreen();
          RetroInkBoot::drawChargingScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), 57, 1, 57);
          captureFrameUnlocked("Charging mid");
          renderer.clearScreen();
          RetroInkBoot::drawChargingScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), 100, 2,
                                           100);
          captureFrameUnlocked("Charging full");
          {
            ChargingActivity chargingCheck(renderer, mappedInputManager);
            if (!chargingCheck.preventAutoSleep()) fail("ChargingActivity must prevent auto-sleep while shown");
          }

          // Custom library shelves: round-trip create/rename/assign/delete
          // through real SD-card-equivalent files, since this is new binary
          // storage that the web API build+compile check alone can't verify.
          {
            Storage.remove("/.retroink/library.shelves.bin");
            Storage.remove("/.retroink/library.shelf_assignments.bin");
            RetroInkCustomShelves shelves;
            if (!shelves.load()) fail("Fresh shelf store should load as empty, not fail");
            if (!shelves.shelves().empty()) fail("Fresh shelf store should have no shelves");

            const uint16_t beachId = shelves.createShelf("Beach Reads");
            const uint16_t workId = shelves.createShelf("Work Reference");
            if (beachId == RetroInkCustomShelves::kNoShelf || workId == RetroInkCustomShelves::kNoShelf ||
                beachId == workId)
              fail("createShelf did not return distinct ids");
            if (!shelves.assignToShelf(0x1234ULL, beachId)) fail("assignToShelf failed for a valid shelf");
            if (shelves.shelfForFingerprint(0x1234ULL) != beachId)
              fail("shelfForFingerprint did not return the just-assigned shelf");
            if (shelves.assignToShelf(0x1234ULL, 9999)) fail("assignToShelf should reject an unknown shelf id");
            if (!shelves.save()) fail("Could not save the shelf store");

            RetroInkCustomShelves reloaded;
            if (!reloaded.load()) fail("Could not reload the shelf store");
            if (reloaded.shelves().size() != 2) fail("Reloaded shelf store lost a shelf");
            if (reloaded.shelfForFingerprint(0x1234ULL) != beachId)
              fail("Reloaded shelf store lost the fingerprint assignment");
            if (!reloaded.renameShelf(beachId, "Poolside Reads")) fail("renameShelf failed for a valid id");
            const auto renamedIt = std::find_if(reloaded.shelves().begin(), reloaded.shelves().end(),
                                                [beachId](const auto& s) { return s.id == beachId; });
            if (renamedIt == reloaded.shelves().end() || strcmp(renamedIt->name, "Poolside Reads") != 0)
              fail("renameShelf did not take effect");
            if (!reloaded.deleteShelf(beachId)) fail("deleteShelf failed for a valid id");
            if (reloaded.shelfForFingerprint(0x1234ULL) != RetroInkCustomShelves::kNoShelf)
              fail("deleteShelf should clear assignments pointing at the deleted shelf");
            if (reloaded.shelves().size() != 1) fail("deleteShelf left the wrong shelf count");
            if (!reloaded.save()) fail("Could not save after delete");
            Storage.remove("/.retroink/library.shelves.bin");
            Storage.remove("/.retroink/library.shelf_assignments.bin");
            LOG_INF("SMOKE", "Custom library shelves round-trip passed");
          }

          const uint8_t savedClockChoice = SETTINGS.hideClock;
          SETTINGS.sleepScreen = CrossPointSettings::READING_STATS_SLEEP;
          SleepActivity sleepPreview(renderer, mappedInputManager, false);
          sleepPreview.onEnter();
          if (SETTINGS.hideClock != savedClockChoice) fail("Sleep rendering changed the saved clock setting");
          captureFrameUnlocked("Today Sleep with clock hidden");
          renderer.clearScreen();
          const auto& metrics = UITheme::getInstance().getMetrics();
          GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, nullptr);
          captureFrameUnlocked("Quick Resume source with clock");
          const bool savedSleepFromReader = APP_STATE.lastSleepFromReader;
          APP_STATE.lastSleepFromReader = false;
          SETTINGS.sleepScreen = CrossPointSettings::QUICK_RESUME;
          SleepActivity quickResumePreview(renderer, mappedInputManager, false);
          quickResumePreview.onEnter();
          if (SETTINGS.hideClock != savedClockChoice) fail("Quick Resume changed the saved clock setting");
          captureFrameUnlocked("Quick Resume with clock hidden");
          APP_STATE.lastSleepFromReader = savedSleepFromReader;
          SETTINGS.sleepScreen = savedSleepChoice;
          renderer.clearScreen();
          const Rect popup = GUI.drawPopup(renderer, "Preparing Library");
          GUI.fillPopupProgress(renderer, popup, 55);
          captureFrameUnlocked("RetroInk progress dialog");
          renderer.clearScreen();
          GUI.drawOptionPopup(renderer, "Move this book to Trash?", std::vector<std::string>{"Cancel", "Move"}, 1);
          captureFrameUnlocked("RetroInk confirmation dialog");
        }
        activityManager.goHome();
        queueStep("Home", SmokeStep::Home);
        break;

      case SmokeStep::Home:
        if (SETTINGS.uiTheme == CrossPointSettings::SYSTEM6) {
          activityManager.replaceActivity(std::make_unique<RetroInkLibraryActivity>(renderer, mappedInputManager));
          queueStep("RetroInk Library", SmokeStep::Library, 40);
          break;
        }
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::Library:
        if (!activityManager.isCurrentActivityNamed("RetroInkLibrary")) fail("Library did not open");
        step = SmokeStep::LibraryShelfDownPress;
        break;

      // Up/Down must switch shelves in place (no picker), Left/Right must
      // page within the current shelf - this is the regression the old
      // showShelves()/showSort() bindings on Up/Down would produce if a
      // binding were left in place.
      case SmokeStep::LibraryShelfDownPress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        step = SmokeStep::LibraryShelfDownRelease;
        break;

      case SmokeStep::LibraryShelfDownRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        queueStep("RetroInk Library shelf +1", SmokeStep::LibraryShelfDown);
        break;

      case SmokeStep::LibraryShelfDown:
        if (!activityManager.isCurrentActivityNamed("RetroInkLibrary"))
          fail("Down must switch shelves in place, not open a menu");
        step = SmokeStep::LibraryPageRightPress;
        break;

      case SmokeStep::LibraryPageRightPress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Right);
        step = SmokeStep::LibraryPageRightRelease;
        break;

      case SmokeStep::LibraryPageRightRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Right);
        queueStep("RetroInk Library next book", SmokeStep::LibraryPageRight);
        break;

      case SmokeStep::LibraryPageRight:
        if (!activityManager.isCurrentActivityNamed("RetroInkLibrary")) fail("Right must page within the shelf");
        step = SmokeStep::LibraryShelfUpPress;
        break;

      case SmokeStep::LibraryShelfUpPress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Up);
        step = SmokeStep::LibraryShelfUpRelease;
        break;

      case SmokeStep::LibraryShelfUpRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Up);
        queueStep("RetroInk Library shelf -1", SmokeStep::LibraryShelfUp);
        break;

      case SmokeStep::LibraryShelfUp:
        if (!activityManager.isCurrentActivityNamed("RetroInkLibrary"))
          fail("Up must switch shelves in place, not open a menu");
        step = SmokeStep::LibraryActionsPress;
        break;

      case SmokeStep::LibraryActionsPress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::LibraryActionsRelease;
        break;

      case SmokeStep::LibraryActionsRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Library Book Menu", SmokeStep::LibraryActions);
        break;

      case SmokeStep::LibraryActions:
        if (!activityManager.isCurrentActivityNamed("LibraryActions")) fail("Library Book Menu did not open");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Back);
        step = SmokeStep::LibraryBack;
        break;

      case SmokeStep::LibraryBack:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
        step = SmokeStep::LibraryReturnHome;
        break;

      case SmokeStep::LibraryReturnHome:
        activityManager.goHome();
        homeFocusAdvances = 0;
        queueStep("Home after Library", SmokeStep::HomeFocusNavigatePress);
        break;

      case SmokeStep::HomeFocusNavigatePress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        step = SmokeStep::HomeFocusNavigateRelease;
        break;

      case SmokeStep::HomeFocusNavigateRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        // Home starts on the current-book tile. RetroInk hides Files by
        // default, so Focus is two steps away (three when Files is enabled).
        if (++homeFocusAdvances == 2 + (SETTINGS.showFilesOnHome ? 1 : 0)) {
          queueStep("Home Focus selected", SmokeStep::HomeFocusConfirmPress);
        } else {
          step = SmokeStep::HomeFocusNavigatePress;
        }
        break;

      case SmokeStep::HomeFocusConfirmPress:
        if (!activityManager.isCurrentActivityNamed("Home")) fail("Home Focus selection left Home");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::HomeFocusConfirmRelease;
        break;

      case SmokeStep::HomeFocusConfirmRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Focus Desk", SmokeStep::FocusDesk);
        break;

      case SmokeStep::FocusDesk:
        if (!activityManager.isCurrentActivityNamed("RetroInkFocusDesk")) fail("Focus Desk did not open");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        step = SmokeStep::FocusSettingsSelectRelease;
        break;

      case SmokeStep::FocusSettingsSelectRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusSettingsOpenRelease;
        break;

      case SmokeStep::FocusSettingsOpenRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Focus settings menu", SmokeStep::FocusSettingsMenu);
        break;

      case SmokeStep::FocusSettingsMenu:
        if (!activityManager.isCurrentActivityNamed("RetroInkFocusDesk")) fail("Focus Settings did not open");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusLengthOpenRelease;
        break;

      case SmokeStep::FocusLengthOpenRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        SETTINGS.setFocusDurationMinutes(1440);
        SETTINGS.saveToFile();
        queueStep("24-hour Focus editor", SmokeStep::FocusLengthEditor);
        break;

      case SmokeStep::FocusLengthEditor:
        if (SETTINGS.focusDurationMinutes() != 1440) fail("Focus editor cannot hold 24 hours");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusLengthDoneRelease;
        break;

      case SmokeStep::FocusLengthDoneRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusRefreshSelectPress;
        break;

      case SmokeStep::FocusRefreshSelectPress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        step = SmokeStep::FocusRefreshSelectRelease;
        break;

      case SmokeStep::FocusRefreshSelectRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusRefreshOpenRelease;
        break;

      case SmokeStep::FocusRefreshOpenRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusRefreshSecondPress;
        break;

      case SmokeStep::FocusRefreshSecondPress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Left);
        step = SmokeStep::FocusRefreshSecondRelease;
        break;

      case SmokeStep::FocusRefreshSecondRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Left);
        queueStep("One-second refresh warning", SmokeStep::FocusRefreshEditor);
        break;

      case SmokeStep::FocusRefreshEditor:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusRefreshChosenRelease;
        break;

      case SmokeStep::FocusRefreshChosenRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusRefreshSaved;
        break;

      case SmokeStep::FocusRefreshSaved:
        if (SETTINGS.focusTimerRefresh != CrossPointSettings::FOCUS_TIMER_EVERY_SECOND)
          fail("Focus refresh selection did not save one second");
        {
          const auto& settings = getBaseSettingsList();
          const auto it = std::find_if(settings.begin(), settings.end(), [](const SettingInfo& info) {
            return info.key && std::strcmp(info.key, "focusTimerRefresh") == 0;
          });
          if (it == settings.end() || it->enumRawValues.size() != 3 ||
              std::find(it->enumRawValues.begin(), it->enumRawValues.end(),
                        CrossPointSettings::FOCUS_TIMER_MANUAL) != it->enumRawValues.end())
            fail("Manual Focus refresh is still exposed in settings");
        }
        SETTINGS.focusSessionHours = 0;
        SETTINGS.focusSessionMinutes = 25;
        SETTINGS.focusTimerRefresh = CrossPointSettings::FOCUS_TIMER_EVERY_MINUTE;
        SETTINGS.loadFromFile();
        if (SETTINGS.focusDurationMinutes() != 1440 ||
            SETTINGS.focusTimerRefresh != CrossPointSettings::FOCUS_TIMER_EVERY_SECOND)
          fail("Focus duration and cadence did not persist after settings reload");
        {
          JsonDocument oldSettings;
          SETTINGS.toJson(oldSettings);
          oldSettings["focusTimerRefresh"] = CrossPointSettings::FOCUS_TIMER_MANUAL;
          if (!SETTINGS.fromJson(oldSettings.as<JsonVariantConst>()) ||
              SETTINGS.focusTimerRefresh != CrossPointSettings::FOCUS_TIMER_EVERY_MINUTE)
            fail("Saved Manual Focus cadence did not migrate to one minute");
          SETTINGS.loadFromFile();
        }
        activityManager.replaceActivity(std::make_unique<RetroInkFocusDeskActivity>(
            renderer, mappedInputManager, RetroInkFocusDeskActivity::Entry::Home));
        queueStep("Focus Desk 24 hours", SmokeStep::FocusDeskStart);
        break;

      case SmokeStep::FocusDeskStart:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::FocusStartRelease;
        break;

      case SmokeStep::FocusStartRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Focus timer", SmokeStep::FocusTimer, 5);
        break;

      case SmokeStep::FocusTimer: {
        if (!activityManager.isCurrentActivityNamed("FocusSession")) fail("Start Focus did not open the timer");
        if (activityManager.focusSleepSeconds() != 0)
          fail("Focus would sleep through X3 front-button presses");
        FocusSessionActivity::Snapshot snapshot;
        if (!FocusSessionActivity::readSnapshot(false, snapshot) ||
            snapshot.durationSeconds != static_cast<uint32_t>(SETTINGS.focusDurationMinutes()) * 60U) {
          fail("Focus timer did not save a valid session");
        }
        focusRedrawWaitStartedMs = millis();
        step = SmokeStep::FocusTimerRedrawWait;
        break;
      }

      case SmokeStep::FocusTimerRedrawWait:
        if (millis() - focusRedrawWaitStartedMs < 1500U) break;
        queueStep("Focus timer after one second", SmokeStep::FocusTimerPostRedraw);
        break;

      case SmokeStep::FocusTimerPostRedraw: {
        if (!activityManager.isCurrentActivityNamed("FocusSession")) fail("Focus timer did not stay open for redraw");
        FocusSessionActivity::Snapshot snapshot;
        if (!FocusSessionActivity::readSnapshot(false, snapshot)) fail("One-second Focus lost its restart state");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Back);
        step = SmokeStep::FocusBackPress;
        break;
      }

      case SmokeStep::FocusBackPress:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
        step = SmokeStep::FocusBackRelease;
        break;

      case SmokeStep::FocusBackRelease:
        if (activityManager.isCurrentActivityNamed("FocusSession"))
          fail("Displayed Back key did not dismiss active Focus");
        step = SmokeStep::FocusBackExited;
        break;

      case SmokeStep::FocusBackExited:
        SETTINGS.focusTimerRefresh = CrossPointSettings::FOCUS_TIMER_EVERY_MINUTE;
        activityManager.replaceActivity(std::make_unique<FocusSessionActivity>(
            renderer, mappedInputManager, 5));
        queueStep("Focus minutes only", SmokeStep::FocusEndActivePress);
        break;

      case SmokeStep::FocusEndActivePress:
        if (!activityManager.isCurrentActivityNamed("FocusSession"))
          fail("Second Focus did not start");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Right);
        step = SmokeStep::FocusEndActiveRelease;
        break;

      case SmokeStep::FocusEndActiveRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Right);
        step = SmokeStep::FocusEndActiveExited;
        break;

      case SmokeStep::FocusEndActiveExited: {
        if (activityManager.isCurrentActivityNamed("FocusSession"))
          fail("Displayed End key did not dismiss active Focus");
        FocusSessionActivity::Snapshot snapshot;
        snapshot.durationSeconds = 300;
        snapshot.remainingSeconds = 0;
        activityManager.replaceActivity(std::make_unique<FocusSessionActivity>(
            renderer, mappedInputManager, std::move(snapshot)));
        queueStep("Focus completion", SmokeStep::FocusCompletion);
        break;
      }

      case SmokeStep::FocusCompletion: {
        if (!activityManager.isCurrentActivityNamed("FocusSession")) fail("Focus completion did not render");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Right);
        step = SmokeStep::FocusEndPress;
        break;
      }

      case SmokeStep::FocusEndPress:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Right);
        step = SmokeStep::FocusEndRelease;
        break;

      case SmokeStep::FocusEndRelease:
        if (activityManager.isCurrentActivityNamed("FocusSession"))
          fail("Displayed End key did not dismiss Focus");
        step = SmokeStep::FocusEndExited;
        break;

      case SmokeStep::FocusEndExited: {
        activityManager.replaceActivity(std::make_unique<BookStatsActivity>(
            renderer, mappedInputManager, std::string(tr(STR_READING_STATS)), std::string{}, BookReadingStats{}, 0.0f,
            false, 0, GlobalReadingStats{}, true));
        queueStep("Reading Desk Today", SmokeStep::StatsToday);
        break;
      }

      case SmokeStep::StatsToday:
        if (!activityManager.isCurrentActivityNamed("BookStats")) fail("Reading Stats did not open");
        if (Storage.exists("/.retroink/focus_session.bin")) fail("Leaving Focus kept its active timer state");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        queueStep("Reading Year without synced stats", SmokeStep::StatsYear);
        break;

      case SmokeStep::StatsYear:
        if (!activityManager.isCurrentActivityNamed("BookStats")) fail("Reading Year navigation exited Stats");
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        step = SmokeStep::StatsBookStatus;
        break;

      case SmokeStep::StatsBookStatus:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        queueStep("Book Status", SmokeStep::StatsActionsPress);
        break;

      case SmokeStep::StatsActionsPress:
        if (!activityManager.isCurrentActivityNamed("BookStats")) fail("Book Status navigation exited Stats");
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::StatsActionsRelease;
        break;

      case SmokeStep::StatsActionsRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Reading Stats Actions", SmokeStep::StatsActions);
        break;

      case SmokeStep::StatsActions:
        if (!activityManager.isCurrentActivityNamed("RetroInkFocusDesk")) fail("Stats Actions did not open");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        step = SmokeStep::StatsGoalPress;
        break;

      case SmokeStep::StatsGoalPress:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::StatsGoalRelease;
        break;

      case SmokeStep::StatsGoalRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Daily Goal Editor", SmokeStep::StatsGoalEditor);
        break;

      case SmokeStep::StatsGoalEditor:
        if (!activityManager.isCurrentActivityNamed("RetroInkFocusDesk")) fail("Daily Goal Editor exited Stats Actions");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
        step = SmokeStep::StatsGoalAdjust;
        break;

      case SmokeStep::StatsGoalAdjust:
        if (SETTINGS.readingGoalMinutes != 30) fail("Daily Goal button did not save its change");
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        activityManager.goToFileBrowser("/");
        queueStep("File Browser root", SmokeStep::FileBrowserRoot);
        break;

      case SmokeStep::FileBrowserRoot:
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::FileBrowser:
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::RecentBooks:
        if (SETTINGS.uiTheme == CrossPointSettings::SYSTEM6) {
          activityManager.goHome(HomeMenuItem::SETTINGS_MENU);
          queueStep("Home Settings selected", SmokeStep::HomeSettingsSelected);
        } else {
          activityManager.goToSettings();
          queueStep("Settings", SmokeStep::Settings);
        }
        break;

      case SmokeStep::HomeSettingsSelected:
        if (!activityManager.isCurrentActivityNamed("Home")) fail("Settings selection left Home");
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::HomeSettingsOpen;
        break;

      case SmokeStep::HomeSettingsOpen:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        queueStep("Settings from Home", SmokeStep::Settings);
        break;

      case SmokeStep::Settings:
        if (!activityManager.isCurrentActivityNamed("Settings")) fail("Home Settings entry did not open");
        settingsCategoryAdvances = 0;
        step = SmokeStep::SettingsAdvancePress;
        break;

      case SmokeStep::SettingsAdvancePress:
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        step = SmokeStep::SettingsAdvanceRelease;
        break;

      case SmokeStep::SettingsAdvanceRelease:
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
        ++settingsCategoryAdvances;
        if (settingsCategoryAdvances >= 3) {
          queueStep("Settings System footer", SmokeStep::SettingsSystem, 4);
        } else {
          step = SmokeStep::SettingsAdvancePress;
        }
        break;

      case SmokeStep::SettingsSystem:
        activityManager.replaceActivity(std::make_unique<ReaderOptionsActivity>(renderer, mappedInputManager));
        queueStep("Reader Options", SmokeStep::ReaderOptions);
        break;

      case SmokeStep::ReaderOptions:
        activityManager.replaceActivity(
            std::make_unique<EpubReaderMenuActivity>(renderer, mappedInputManager, "Smoke Test", 1, 1, 0,
                                                     SETTINGS.orientation, false, false, false, false, false, false));
        queueStep("Reader Menu", SmokeStep::ReaderMenu);
        break;

      case SmokeStep::ReaderMenu:
        activityManager.goToSleep();
        queueStep("Sleep", SmokeStep::Sleep);
        break;

      case SmokeStep::Sleep: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') {
          LOG_INF("SMOKE", "Skipping Reader step; CROSSINK_SIMULATOR_SMOKE_BOOK is not set");
          step = SmokeStep::Reader;
          break;
        }
        if (!Storage.exists(bookPath)) {
          fail("Smoke test book is missing: %s", bookPath);
        }
        activityManager.goToReader(bookPath, true);
        queueStep("Reader", SmokeStep::Reader, 8);
        break;
      }

      case SmokeStep::Reader:
        buildReaderInputScript();
        step = SmokeStep::ReaderInput;
        break;

      case SmokeStep::ReaderInput:
        runReaderInputScript();
        break;

      case SmokeStep::Done:
        LOG_INF("SMOKE", "Simulator smoke test passed");
        std::_Exit(0);
    }
  }

  static ScriptAction press(MappedInputManager::Button button) {
    return {ScriptActionType::Press, button, nullptr, 0, 0, 0};
  }

  static ScriptAction release(MappedInputManager::Button button) {
    return {ScriptActionType::Release, button, nullptr, 0, 0, 0};
  }

  static ScriptAction homeTap() {
    return {ScriptActionType::HomeTap, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction homeLongPress() {
    return {ScriptActionType::HomeLongPress, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction openSmokeBook() {
    return {ScriptActionType::OpenSmokeBook, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction disableReaderTouch() {
    return {ScriptActionType::DisableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction enableReaderTouch() {
    return {ScriptActionType::EnableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction render(const char* label, int framesToSettle = 3) {
    return {ScriptActionType::Render, MappedInputManager::Button::Back, label, framesToSettle, 0, 0};
  }

#if CROSSINK_APP_CAP_TOUCH
  static ScriptAction touchDown(const int x, const int y) {
    return {ScriptActionType::TouchDown, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchMove(const int x, const int y) {
    return {ScriptActionType::TouchMove, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchRelease(const int x, const int y) {
    return {ScriptActionType::TouchRelease, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction assertActivity(const char* name) {
    return {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, name, 0, 0, 0};
  }
#endif

  void addTap(MappedInputManager::Button button) {
    inputScript.push_back(press(button));
    inputScript.push_back(release(button));
  }

  void buildReaderInputScript() {
    inputScript.clear();
    scriptIndex = 0;

    const int turns = pageTurnCount();
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      if (width <= 0 || height <= 0) fail("Touch smoke test has invalid screen dimensions");
      LOG_INF("SMOKE", "Running touch reader input script with %d page turn(s)", turns);
      for (int i = 0; i < turns; ++i) {
        inputScript.push_back(touchDown(width * 5 / 6, height / 2));
        inputScript.push_back(touchRelease(width * 5 / 6, height / 2));
        inputScript.push_back(render("Reader after touch page forward", 4));
      }
      if (mappedInputManager.hasHomeKey()) {
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Home opened from reader with touch enabled", 4));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after Home with touch enabled", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        // X4 Pro reserves the top-edge swipe for its frontlight overlay and
        // moves the reader menu to the bottom edge.
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel opened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader restored after dismissing Frontlight Panel", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from simulated Home key hold", 4));
        inputScript.push_back(assertActivity("EpubReaderMenu"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Reader restored after top-edge swipe dismisses Reader Menu", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(disableReaderTouch());
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Home opened from simulated Home key tap", 4));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(enableReaderTouch());
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after simulated Home key tap", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      } else {
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
      }
      inputScript.push_back(render("Reader Menu opened from touch gesture", 4));
      inputScript.push_back(assertActivity("EpubReaderMenu"));

      // The menu itself registers its tab and list hit areas through the active
      // theme. Exercise both before activating Reader Options from list row 1.
      const auto& metrics = UITheme::getInstance().getMetrics();
      const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInputManager.hasTouch(), false);
      const int tabHeight = metrics.tabBarHeight * 2;
      const bool tabsAtBottom = mappedInputManager.hasHomeKey();
      const int tabTop = tabsAtBottom ? safe.y + safe.height - tabHeight
                                      : safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight;
      const int listTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                          (tabsAtBottom ? 0 : tabHeight) + metrics.verticalSpacing;
      const int rowHeight = uiThemeTokens(makeUiTarget(renderer)).rowHeight;
      if (rowHeight <= 0) fail("Touch smoke test has invalid list row height");

      inputScript.push_back(touchDown(safe.x + safe.width / 2, tabTop + tabHeight / 2));
      inputScript.push_back(touchRelease(safe.x + safe.width / 2, tabTop + tabHeight / 2));
      inputScript.push_back(render("Reader Menu tab touch navigation", 3));
      inputScript.push_back(assertActivity("EpubReaderMenu"));
      inputScript.push_back(touchDown(safe.x + safe.width / 6, tabTop + tabHeight / 2));
      inputScript.push_back(touchRelease(safe.x + safe.width / 6, tabTop + tabHeight / 2));
      inputScript.push_back(render("Reader Menu main tab restored", 3));
      inputScript.push_back(assertActivity("EpubReaderMenu"));

      const int readerOptionsY = listTop + rowHeight + rowHeight / 2;
      inputScript.push_back(touchDown(safe.x + safe.width / 2, readerOptionsY));
      inputScript.push_back(touchRelease(safe.x + safe.width / 2, readerOptionsY));
      inputScript.push_back(render("Reader Options opened by touch list activation", 4));
      inputScript.push_back(assertActivity("ReaderOptions"));

      const int optionsListTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int optionsStartY = optionsListTop + rowHeight * 4 + rowHeight / 2;
      inputScript.push_back(touchDown(width / 2, optionsStartY));
      inputScript.push_back(touchMove(width / 2, optionsListTop + rowHeight / 2));
      inputScript.push_back(touchRelease(width / 2, optionsListTop + rowHeight / 2));
      inputScript.push_back(render("Reader Options touch swipe navigation", 3));
      inputScript.push_back(assertActivity("ReaderOptions"));

      if (SETTINGS.uiTheme == CrossPointSettings::SYSTEM6) {
        // Tap the visible Mac close box, then the reader menu's Home icon.
        const int titleBarY = metrics.topPadding + 24;
        inputScript.push_back(touchDown(24, titleBarY));
        inputScript.push_back(touchRelease(24, titleBarY));
        inputScript.push_back(render("Reader Menu restored by title bar close box", 4));
        inputScript.push_back(assertActivity("EpubReaderMenu"));
        inputScript.push_back(touchDown(width - 32, titleBarY));
        inputScript.push_back(touchRelease(width - 32, titleBarY));
        inputScript.push_back(render("Home opened by reader menu Home icon", 4));
        inputScript.push_back(assertActivity("Home"));
      }
      return;
    }
#endif
    for (int i = 0; i < turns; i++) {
      addTap(MappedInputManager::Button::PageForward);
      inputScript.push_back(render("Reader after page forward", 4));
    }

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Menu opened from EPUB", 4));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Reader Menu Reader Options selection", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Options opened from Reader Menu", 4));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Reader Options after navigation", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Options after toggle", 3));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Menu after closing Reader Options", 4));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after closing Reader Menu", 4));

    inputScript.push_back(render("Home after closing Reader", 4));
    inputScript.push_back(render("Home after cover generation", 4));

    LOG_INF("SMOKE", "Running reader input script with %d page turn(s)", turns);
  }

  void runReaderInputScript() {
    if (scriptIndex >= inputScript.size()) {
      step = SmokeStep::Done;
      return;
    }

    const auto& action = inputScript[scriptIndex++];
    switch (action.type) {
      case ScriptActionType::Press:
        mappedInputManager.simulatorInjectPress(action.button);
        break;
      case ScriptActionType::Release:
        mappedInputManager.simulatorInjectRelease(action.button);
        break;
      case ScriptActionType::HomeTap:
        simulatorHomeKeyInput.injectTap();
        break;
      case ScriptActionType::HomeLongPress:
        simulatorHomeKeyInput.injectLongPress();
        break;
      case ScriptActionType::OpenSmokeBook: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') fail("Smoke test book path is missing");
        activityManager.goToReader(bookPath, true);
        break;
      }
      case ScriptActionType::DisableReaderTouch:
        SETTINGS.disableReaderTouchscreen = true;
        break;
      case ScriptActionType::EnableReaderTouch:
        SETTINGS.disableReaderTouchscreen = false;
        break;
      case ScriptActionType::TouchDown:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchDown(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchMove:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchMove(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchRelease:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchRelease(action.x, action.y);
#endif
        break;
      case ScriptActionType::AssertActivity:
        if (!activityManager.isCurrentActivityNamed(action.label)) fail("Expected current activity: %s", action.label);
        break;
      case ScriptActionType::Render:
        queueStep(action.label, SmokeStep::ReaderInput, action.settleFrames);
        break;
    }
  }
};

SimulatorSmokeTest smokeTest;

}  // namespace

void runSimulatorSmokeTestTick() { smokeTest.tick(); }

#endif
