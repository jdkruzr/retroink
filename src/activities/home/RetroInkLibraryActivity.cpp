#include "RetroInkLibraryActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "FileBrowserActivity.h"
#include "RecentBookProgress.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/EpubReaderUtils.h"
#include "fontIds.h"
#include "util/BookMoveUtils.h"

namespace {
constexpr int SPINE_GAP = 6;
constexpr int SPINE_MIN_W = 56;
constexpr int SPINE_MAX_COUNT = 8;
constexpr int BANNER_H = 40;
constexpr int BOARD_H = 6;
constexpr int DETAIL_H = 130;

const char* shelfLabel(RetroInkLibraryCatalog::Shelf shelf) {
  switch (shelf) {
    case RetroInkLibraryCatalog::Shelf::ToRead: return tr(STR_LIBRARY_TO_READ);
    case RetroInkLibraryCatalog::Shelf::Reading: return tr(STR_LIBRARY_READING);
    case RetroInkLibraryCatalog::Shelf::Finished: return tr(STR_LIBRARY_FINISHED);
    case RetroInkLibraryCatalog::Shelf::Favorites: return tr(STR_LIBRARY_FAVORITES);
    default: return tr(STR_LIBRARY_ALL);
  }
}
const char* sortLabel(RetroInkLibraryCatalog::Sort sort) {
  switch (sort) {
    case RetroInkLibraryCatalog::Sort::Filename: return tr(STR_LIBRARY_FILENAME_SORT);
    case RetroInkLibraryCatalog::Sort::Author: return tr(STR_LIBRARY_AUTHOR_SORT);
    case RetroInkLibraryCatalog::Sort::Recent: return tr(STR_LIBRARY_RECENT_SORT);
    default: return tr(STR_LIBRARY_TITLE_SORT);
  }
}

void drawFitted(const GfxRenderer& r, int font, int x, int y, int width, const char* text, bool bold) {
  char line[224];
  snprintf(line, sizeof(line), "%s", text);
  if (r.getTextWidth(font, line) > width) {
    size_t len = strlen(line);
    while (len > 4 && r.getTextWidth(font, line) > width) line[--len] = '\0';
    if (len > 3) { line[len - 3] = '.'; line[len - 2] = '.'; line[len - 1] = '.'; }
  }
  r.drawText(font, x, y, line, true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void drawTwoLineTitle(const GfxRenderer& r, int x, int y, int width, const char* title) {
  char first[224];
  snprintf(first, sizeof(first), "%s", title);
  size_t split = strlen(first);
  while (split > 1 && r.getTextWidth(UI_10_FONT_ID, first) > width) first[--split] = '\0';
  if (split < strlen(title)) {
    size_t space = split;
    while (space > split / 2 && title[space] != ' ') --space;
    if (space > split / 2) { split = space; first[split] = '\0'; }
  }
  r.drawText(UI_10_FONT_ID, x, y, first, true, EpdFontFamily::BOLD);
  const char* second = title + split;
  while (*second == ' ') ++second;
  if (*second) drawFitted(r, UI_10_FONT_ID, x, y + 21, width, second, true);
}

std::string cacheFor(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return Epub::cachePathForFilePath(path, "/.crosspoint");
  if (FsHelpers::hasXtcExtension(path)) return Xtc(path, "/.crosspoint").getCachePath();
  return Txt(path, "/.crosspoint").getCachePath();
}

std::string progressFor(const std::string& path) {
  const RecentBook book{path, "", "", ""};
  float percent = FsHelpers::hasEpubExtension(path) ? RecentBookProgress::loadCachedEpubPercent(book)
                                                   : RecentBookProgress::loadPercent(book);
  char text[56];
  if (percent >= 0) {
    snprintf(text, sizeof(text), tr(STR_LIBRARY_PROGRESS_PERCENT), static_cast<unsigned>(percent + 0.5f));
    return text;
  }
  const std::string cache = cacheFor(path);
  if (FsHelpers::hasEpubExtension(path)) {
    EpubReaderUtils::Progress progress;
    if (EpubReaderUtils::readProgressFile("Library", cache + "/progress.bin", progress)) {
      snprintf(text, sizeof(text), tr(STR_LIBRARY_POSITION_CHAPTER_PAGE),
               static_cast<unsigned>(progress.spineIndex + 1), static_cast<unsigned>(progress.pageNumber + 1));
      return text;
    }
  } else {
    FsFile file;
    if (Storage.openFileForRead("Library", cache + "/progress.bin", file)) {
      uint8_t page[4] = {};
      const int count = file.read(page, sizeof(page)); file.close();
      if (count >= 2) {
        const unsigned p = static_cast<unsigned>(page[0]) | (static_cast<unsigned>(page[1]) << 8);
        snprintf(text, sizeof(text), tr(STR_LIBRARY_POSITION_PAGE), p + 1);
        return text;
      }
    }
  }
  return tr(STR_LIBRARY_PROGRESS_UNKNOWN);
}

std::string durationFor(const std::string& path) {
  char text[32];
  BookReadingStats::formatDuration(BookReadingStats::load(cacheFor(path)).totalReadingSeconds, text, sizeof(text));
  return text;
}
}  // namespace

void RetroInkLibraryActivity::onEnter() {
  Activity::onEnter();
  previousOrientation_ = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  customShelves_.load();
  rebuildShelfCycle();
  shelfIndex_ = shelfIndexForToken(SETTINGS.libraryDefaultShelf);
  scanFinished_ = catalog_.openCached();
  if (!scanFinished_) scanFinished_ = !catalog_.beginScan();
  selected_ = stripStart_ = 0;
  rowShelfIdValid_ = false;
  filteredRows_.clear();
  // openCached() already hands back the All Books / Title A-Z view for free;
  // anything else (including any custom shelf) needs its own view built.
  if (scanFinished_ && (shelfIndex_ != 0 || sort_ != RetroInkLibraryCatalog::Sort::Title)) rebuildView();
  requestUpdate();
}

void RetroInkLibraryActivity::onExit() {
  if (catalog_.isScanning()) catalog_.pauseScan();
  catalog_.close();
  rowShelfId_.clear();
  rowShelfId_.shrink_to_fit();
  filteredRows_.clear();
  filteredRows_.shrink_to_fit();
  shelfCycle_.clear();
  shelfCycle_.shrink_to_fit();
  renderer.setOrientation(previousOrientation_);
  Activity::onExit();
}

void RetroInkLibraryActivity::rebuildShelfCycle() {
  const uint16_t previousCustom = shelfCycle_.empty() ? RetroInkCustomShelves::kNoShelf : activeShelf().customId;
  const auto previousBuiltin = shelfCycle_.empty() ? RetroInkLibraryCatalog::Shelf::All : activeShelf().builtin;
  shelfCycle_.clear();
  using Shelf = RetroInkLibraryCatalog::Shelf;
  for (const auto builtin : {Shelf::All, Shelf::ToRead, Shelf::Reading, Shelf::Finished, Shelf::Favorites})
    shelfCycle_.push_back({RetroInkCustomShelves::kNoShelf, builtin, shelfLabel(builtin)});
  for (const auto& shelf : customShelves_.shelves())
    shelfCycle_.push_back({shelf.id, Shelf::All, shelf.name});
  // Keep the user on the same shelf across a rebuild (e.g. after creating a
  // shelf elsewhere) when it still exists; otherwise fall back to All.
  shelfIndex_ = 0;
  for (uint32_t i = 0; i < shelfCycle_.size(); ++i) {
    if (shelfCycle_[i].customId == previousCustom &&
        (previousCustom != RetroInkCustomShelves::kNoShelf || shelfCycle_[i].builtin == previousBuiltin)) {
      shelfIndex_ = i;
      break;
    }
  }
}

uint32_t RetroInkLibraryActivity::shelfIndexForToken(const char* token) const {
  if (!token || !token[0] || strcmp(token, "all") == 0) return 0;
  if (strcmp(token, "toread") == 0) return 1;
  if (strcmp(token, "reading") == 0) return 2;
  if (strcmp(token, "finished") == 0) return 3;
  if (strcmp(token, "favorites") == 0) return 4;
  if (strncmp(token, "c:", 2) == 0) {
    const uint16_t wantId = static_cast<uint16_t>(strtoul(token + 2, nullptr, 10));
    for (uint32_t i = 5; i < shelfCycle_.size(); ++i)
      if (shelfCycle_[i].customId == wantId) return i;
  }
  return 0;  // deleted or unrecognized shelf: fall back to All, don't rewrite the setting
}

void RetroInkLibraryActivity::writeDefaultShelfToken() {
  char token[sizeof(SETTINGS.libraryDefaultShelf)];
  if (activeIsCustom()) {
    snprintf(token, sizeof(token), "c:%u", static_cast<unsigned>(activeShelf().customId));
  } else {
    static const char* kBuiltinTokens[] = {"all", "toread", "reading", "finished", "favorites"};
    snprintf(token, sizeof(token), "%s", kBuiltinTokens[static_cast<size_t>(activeShelf().builtin)]);
  }
  strncpy(SETTINGS.libraryDefaultShelf, token, sizeof(SETTINGS.libraryDefaultShelf) - 1);
  SETTINGS.libraryDefaultShelf[sizeof(SETTINGS.libraryDefaultShelf) - 1] = '\0';
  SETTINGS.saveToFile();
}

void RetroInkLibraryActivity::ensureRowShelfIds() {
  if (rowShelfIdValid_) return;
  rowShelfId_.assign(catalog_.viewCount(), RetroInkCustomShelves::kNoShelf);
  for (uint32_t row = 0; row < catalog_.viewCount(); ++row) {
    if (!catalog_.viewRecord(row, selectedRecord_)) break;
    if (selectedRecord_.fingerprint)
      rowShelfId_[row] = customShelves_.shelfForFingerprint(selectedRecord_.fingerprint);
  }
  rowShelfIdValid_ = true;
}

void RetroInkLibraryActivity::rebuildCustomFilter() {
  filteredRows_.clear();
  if (!activeIsCustom()) {
    rowShelfId_.clear();
    rowShelfId_.shrink_to_fit();
    rowShelfIdValid_ = false;
    return;
  }
  ensureRowShelfIds();
  const uint16_t want = activeShelf().customId;
  for (uint32_t row = 0; row < rowShelfId_.size(); ++row)
    if (rowShelfId_[row] == want) filteredRows_.push_back(row);
}

void RetroInkLibraryActivity::clampSelection() {
  const uint32_t count = shelfBookCount();
  if (!count) { selected_ = stripStart_ = 0; return; }
  if (selected_ >= count) selected_ = count - 1;
  const uint32_t perPage = static_cast<uint32_t>(spinesPerPage());
  stripStart_ = (selected_ / perPage) * perPage;
}

void RetroInkLibraryActivity::applyShelfChange(uint32_t newIndex) {
  const bool wasAllView = activeUsesAllView();
  shelfIndex_ = newIndex;
  selected_ = stripStart_ = 0;
  // Custom shelves (and All) all share the same underlying view, so hopping
  // between them is free - no re-sort, no SD re-read.
  if (wasAllView && activeUsesAllView()) {
    rebuildCustomFilter();
    requestUpdate();
  } else {
    rebuildView();
  }
}

int RetroInkLibraryActivity::spinesPerPage() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int innerW = renderer.getScreenWidth() - m.contentSidePadding * 2 - 18;
  int n = SPINE_MAX_COUNT;
  while (n > 1 && (innerW - SPINE_GAP * (n - 1)) / n < SPINE_MIN_W) --n;
  return n;
}

int RetroInkLibraryActivity::spineWidth() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int innerW = renderer.getScreenWidth() - m.contentSidePadding * 2 - 18;
  const int n = spinesPerPage();
  return (innerW - SPINE_GAP * (n - 1)) / n;
}

void RetroInkLibraryActivity::rebuildView() {
  const auto builtin = activeIsCustom() ? RetroInkLibraryCatalog::Shelf::All : activeShelf().builtin;
  if (!catalog_.buildView(builtin, sort_)) { moveFailed_ = true; requestUpdate(); return; }
  rowShelfIdValid_ = false;  // row order (and, for a fresh scan, membership) just changed
  rebuildCustomFilter();
  clampSelection();
  requestUpdate();
}

void RetroInkLibraryActivity::refreshLibrary() {
  catalog_.close();
  scanFinished_ = !catalog_.beginScan();
  selected_ = stripStart_ = 0;
  lastShownScanCount_ = UINT32_MAX;
  rowShelfIdValid_ = false;
  filteredRows_.clear();
  requestUpdate();
}

void RetroInkLibraryActivity::showShelves() {
  std::vector<std::string> choices;
  choices.reserve(shelfCycle_.size());
  for (const auto& entry : shelfCycle_) choices.push_back(entry.name);
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibraryShelves",
                                                           StrId::STR_LIBRARY_SHELVES, std::move(choices),
                                                           static_cast<uint8_t>(shelfIndex_)),
                         [this](const ActivityResult& result) {
                           if (const auto* choice = std::get_if<OptionSelectionResult>(&result.data))
                             if (choice->index < shelfCycle_.size()) applyShelfChange(choice->index);
                         });
}

void RetroInkLibraryActivity::showSort() {
  std::vector<std::string> choices = {tr(STR_LIBRARY_TITLE_SORT), tr(STR_LIBRARY_FILENAME_SORT),
                                      tr(STR_LIBRARY_AUTHOR_SORT), tr(STR_LIBRARY_RECENT_SORT)};
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibrarySort",
                                                           StrId::STR_LIBRARY_SORT, std::move(choices),
                                                           static_cast<uint8_t>(sort_)),
                         [this](const ActivityResult& result) {
                           if (const auto* choice = std::get_if<OptionSelectionResult>(&result.data)) {
                             sort_ = static_cast<RetroInkLibraryCatalog::Sort>(choice->index);
                             selected_ = stripStart_ = 0;
                             rebuildView();
                           }
                         });
}

void RetroInkLibraryActivity::showLetterJump() {
  std::vector<std::string> letters;
  letters.reserve(27);
  letters.push_back("#");
  for (char letter = 'A'; letter <= 'Z'; ++letter) letters.emplace_back(1, letter);
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibraryLetters",
                                                           StrId::STR_LIBRARY_JUMP, std::move(letters), 0),
                         [this](const ActivityResult& result) {
                           const auto* choice = std::get_if<OptionSelectionResult>(&result.data);
                           if (!choice) return;
                           const uint32_t count = shelfBookCount();
                           const uint32_t perPage = static_cast<uint32_t>(spinesPerPage());
                           for (uint32_t row = 0; row < count; ++row) {
                             if (!shelfRecord(row, selectedRecord_)) break;
                             const uint8_t first = static_cast<uint8_t>(selectedRecord_.title[0]);
                             const char upper = first >= 'a' && first <= 'z' ? first - 32 : first;
                             const bool isLetter = upper >= 'A' && upper <= 'Z';
                             if ((choice->index == 0 && !isLetter) ||
                                 (choice->index > 0 && upper == 'A' + choice->index - 1)) {
                               selected_ = row;
                               stripStart_ = (row / perPage) * perPage;
                               requestUpdate();
                               break;
                             }
                           }
                         });
}

void RetroInkLibraryActivity::openSelectedBook() {
  if (shelfRecord(selected_, selectedRecord_) && Storage.exists(selectedRecord_.path))
    onSelectBook(selectedRecord_.path);
}

void RetroInkLibraryActivity::moveSelectedBook(const std::string& path) {
  startActivityForResult(std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/",
                                                              FileBrowserActivity::Mode::PickDirectory),
                         [this, path](const ActivityResult& result) {
                           const auto* picked = std::get_if<FilePathResult>(&result.data);
                           if (!picked || picked->path.empty()) return;
                           const char* name = strrchr(path.c_str(), '/');
                           name = name ? name + 1 : path.c_str();
                           const std::string dst = (picked->path == "/" ? "" : picked->path) + "/" + name;
                           if (dst == path) return;
                           moveFailed_ = !BookMoveUtils::moveWithState(path, dst);
                           if (!moveFailed_) refreshLibrary();
                           requestUpdate();
                         });
}

void RetroInkLibraryActivity::reviewCandidate(uint32_t match) {
  if (!catalog_.recoveryCandidate(reviewCurrent_, match, reviewOld_)) return;
  const std::string oldProgress = progressFor(reviewOld_.path), oldTime = durationFor(reviewOld_.path);
  const std::string currentProgress = progressFor(reviewCurrent_.path), currentTime = durationFor(reviewCurrent_.path);
  char oldLabel[128], currentLabel[128];
  snprintf(oldLabel, sizeof(oldLabel), tr(STR_LIBRARY_RESTORE_WITH_PROGRESS), oldProgress.c_str(), oldTime.c_str());
  snprintf(currentLabel, sizeof(currentLabel), tr(STR_LIBRARY_KEEP_WITH_PROGRESS),
           currentProgress.c_str(), currentTime.c_str());
  std::vector<std::string> choices = {oldLabel, currentLabel};
  const std::string oldPath = reviewOld_.path, currentPath = reviewCurrent_.path;
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibraryRestore",
                                                           StrId::STR_LIBRARY_RECOVER, std::move(choices), 1),
                         [this, oldPath, currentPath](const ActivityResult& result) {
                           const auto* choice = std::get_if<OptionSelectionResult>(&result.data);
                           if (!choice || choice->index != 0) return;
                           moveFailed_ = !BookMoveUtils::restorePriorState(oldPath, currentPath);
                           if (!moveFailed_) {
                             if (reviewOld_.flags & 4) catalog_.setFavorite(catalogRow(selected_), true);
                             refreshLibrary();
                           }
                           requestUpdate();
                         });
}

void RetroInkLibraryActivity::reviewRecovery() {
  if (!shelfRecord(selected_, reviewCurrent_)) return;
  const uint32_t count = catalog_.recoveryCount(reviewCurrent_);
  if (!count) return;
  if (count == 1) { reviewCandidate(0); return; }
  std::vector<std::string> choices;
  choices.reserve(std::min<uint32_t>(count, 32));
  for (uint32_t i = 0; i < count && i < 32; ++i) {
    if (!catalog_.recoveryCandidate(reviewCurrent_, i, reviewOld_)) break;
    choices.push_back(std::string(reviewOld_.path) + "  (" + progressFor(reviewOld_.path) + ")");
  }
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibraryMatches",
                                                           StrId::STR_LIBRARY_RECOVER, std::move(choices), 0),
                         [this](const ActivityResult& result) {
                           if (const auto* choice = std::get_if<OptionSelectionResult>(&result.data))
                             reviewCandidate(choice->index);
                         });
}

void RetroInkLibraryActivity::showActions() {
  if (!shelfBookCount() || !shelfRecord(selected_, selectedRecord_)) {
    std::vector<std::string> choices = {tr(STR_LIBRARY_SHELVES), tr(STR_LIBRARY_SORT), tr(STR_LIBRARY_SET_DEFAULT),
                                        tr(STR_LIBRARY_REFRESH)};
    startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibraryActions",
                                                             StrId::STR_LIBRARY_BOOK_MENU, std::move(choices), 0),
                           [this](const ActivityResult& result) {
                             if (const auto* choice = std::get_if<OptionSelectionResult>(&result.data)) {
                               switch (choice->index) {
                                 case 0: showShelves(); break;
                                 case 1: showSort(); break;
                                 case 2: writeDefaultShelfToken(); break;
                                 case 3: refreshLibrary(); break;
                               }
                             }
                           });
    return;
  }
  const std::string path = selectedRecord_.path;
  const bool favorite = (selectedRecord_.flags & 4) != 0;
  std::vector<std::string> choices = {tr(STR_LIBRARY_OPEN),
                                      favorite ? tr(STR_LIBRARY_UNPIN) : tr(STR_LIBRARY_PIN),
                                      tr(STR_LIBRARY_MOVE), tr(STR_LIBRARY_SHELVES),
                                      tr(STR_LIBRARY_SORT), tr(STR_LIBRARY_JUMP),
                                      tr(STR_LIBRARY_SET_DEFAULT), tr(STR_LIBRARY_REFRESH)};
  if (catalog_.recoveryCount(selectedRecord_)) choices.push_back(tr(STR_LIBRARY_RECOVER));
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "LibraryActions",
                                                           StrId::STR_LIBRARY_BOOK_MENU, std::move(choices), 0),
                         [this, path, favorite](const ActivityResult& result) {
                           const auto* choice = std::get_if<OptionSelectionResult>(&result.data);
                           if (!choice) return;
                           switch (choice->index) {
                             case 0: openSelectedBook(); break;
                             case 1:
                               if (catalog_.setFavorite(catalogRow(selected_), !favorite)) rebuildView();
                               break;
                             case 2: moveSelectedBook(path); break;
                             case 3: showShelves(); break;
                             case 4: showSort(); break;
                             case 5: showLetterJump(); break;
                             case 6: writeDefaultShelfToken(); break;
                             case 7: refreshLibrary(); break;
                             case 8: reviewRecovery(); break;
                           }
                         });
}

void RetroInkLibraryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (catalog_.isScanning()) catalog_.pauseScan();
    finish(); return;
  }
  if (catalog_.isScanning()) {
    if (!catalog_.stepScan()) {
      scanFinished_ = !catalog_.isScanning();
      requestUpdate();
      return;
    }
    if (!catalog_.isScanning()) { scanFinished_ = true; rebuildView(); return; }
    const uint32_t count = catalog_.scannedCount();
    if (count != lastShownScanCount_ && (count % 24 == 0 || lastShownScanCount_ == UINT32_MAX)) {
      lastShownScanCount_ = count; requestUpdate();
    }
    return;
  }
  if (!scanFinished_ || catalog_.failed()) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { showActions(); return; }

  // Up/Down: previous/next shelf, wrapping across the whole cycle (5
  // built-ins then any custom shelves). Release-only, no held-repeat - a
  // held Up could queue SD re-sorts faster than the panel can repaint them.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    applyShelfChange(shelfIndex_ ? shelfIndex_ - 1 : static_cast<uint32_t>(shelfCycle_.size()) - 1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    applyShelfChange((shelfIndex_ + 1) % static_cast<uint32_t>(shelfCycle_.size()));
    return;
  }

  // Left/Right: previous/next book within this shelf, wrapping. The strip
  // window is page-aligned, so it only repaints when the selection crosses
  // a page edge instead of sliding one spine per press.
  const uint32_t count = shelfBookCount();
  const uint32_t perPage = static_cast<uint32_t>(spinesPerPage());
  if (count && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selected_ = selected_ ? selected_ - 1 : count - 1;
    stripStart_ = (selected_ / perPage) * perPage;
    requestUpdate(); return;
  }
  if (count && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selected_ = (selected_ + 1) % count;
    stripStart_ = (selected_ / perPage) * perPage;
    requestUpdate();
  }
}

void RetroInkLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_RETRO_LIBRARY));
  const auto& m = UITheme::getInstance().getMetrics();
  const int x = m.contentSidePadding;
  const int w = renderer.getScreenWidth() - x * 2;
  const int top = CompactHeader::contentTop(m) + 8;
  const int bottom = renderer.getScreenHeight() - m.buttonHintsHeight - 8;
  renderer.fillRect(x + 3, top + 3, w, bottom - top, true);
  renderer.fillRect(x, top, w, bottom - top, false);
  renderer.drawRect(x, top, w, bottom - top);
  if (catalog_.isScanning()) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 35, tr(STR_LIBRARY_SCAN), true, EpdFontFamily::BOLD);
    char count[32]; snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(catalog_.scannedCount()));
    renderer.drawCenteredText(UI_12_FONT_ID, top + 100, count, true, EpdFontFamily::BOLD);
    drawFitted(renderer, UI_10_FONT_ID, x + 20, top + 165, w - 40, catalog_.currentPath().c_str(), false);
  } else if (catalog_.failed()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + 100, tr(STR_LIBRARY_SD_ERROR));
  } else {
    const int innerX = x + 9;
    const int innerW = w - 18;
    const int bannerY = top + 4;

    // --- shelf banner: BootActivity.cpp's pinstripe-title-strip idiom ---
    for (int sy = bannerY + 6; sy < bannerY + BANNER_H - 6; sy += 4)
      renderer.drawLine(innerX, sy, innerX + innerW - 1, sy);
    const std::string shelfName = activeShelf().name;
    const int nameWidth = renderer.getTextWidth(UI_12_FONT_ID, shelfName.c_str());
    const int nameX = innerX + (innerW - nameWidth) / 2;
    renderer.fillRect(nameX - 10, bannerY + 2, nameWidth + 20, BANNER_H - 4, false);
    renderer.drawText(UI_12_FONT_ID, nameX, bannerY + (BANNER_H - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                       shelfName.c_str(), true, EpdFontFamily::BOLD);
    char posBuf[16];
    snprintf(posBuf, sizeof(posBuf), tr(STR_LIBRARY_SHELF_OF), static_cast<unsigned>(shelfIndex_ + 1),
             static_cast<unsigned>(shelfCycle_.size()));
    const int posWidth = renderer.getTextWidth(SMALL_FONT_ID, posBuf, EpdFontFamily::BOLD);
    renderer.fillRect(innerX + innerW - posWidth - 22, bannerY + 4, posWidth + 20, BANNER_H - 8, false);
    renderer.drawText(SMALL_FONT_ID, innerX + innerW - posWidth - 12,
                       bannerY + (BANNER_H - renderer.getLineHeight(SMALL_FONT_ID)) / 2, posBuf, true,
                       EpdFontFamily::BOLD);
    if (moveFailed_) {
      drawFitted(renderer, UI_10_FONT_ID, innerX + 4, bannerY + BANNER_H + 14, innerW - 8,
                 tr(STR_LIBRARY_MOVE_FAILED), true);
    }

    // --- spine strip + shelf board ---
    const int stripTop = bannerY + BANNER_H + (moveFailed_ ? 30 : 12);
    const int detailY = bottom - 8 - DETAIL_H;
    const int boardY = detailY - 14 - BOARD_H;
    const int spineAreaH = std::max(80, boardY - stripTop);
    const uint32_t count = shelfBookCount();
    if (!count) {
      renderer.drawCenteredText(UI_10_FONT_ID, stripTop + spineAreaH / 2, tr(STR_LIBRARY_NO_BOOKS));
    } else {
      const int n = spinesPerPage();
      const int sw = spineWidth();
      for (int i = 0; i < n && stripStart_ + static_cast<uint32_t>(i) < count; ++i) {
        const uint32_t idx = stripStart_ + static_cast<uint32_t>(i);
        if (!shelfRecord(idx, selectedRecord_)) break;
        const bool isSelected = idx == selected_;
        const int sx = innerX + i * (sw + SPINE_GAP);
        const uint64_t fp = selectedRecord_.fingerprint;
        int sh = spineAreaH - static_cast<int>((fp ^ (fp >> 32)) % 5) * 10;
        if (isSelected) sh = spineAreaH + 14;  // stands the tallest, pulled out of the shelf
        sh = std::max(sh, 60);
        const int sy = boardY - sh;
        renderer.fillRect(sx + 3, sy + 3, sw, sh, true);
        renderer.fillRect(sx, sy, sw, sh, false);
        renderer.drawRect(sx, sy, sw, sh, isSelected ? 2 : 1, true);
        if (isSelected) {
          renderer.drawRect(sx + 5, sy + 5, sw - 10, sh - 10, 1, true);
          renderer.fillRect(sx + 8, sy + 10, sw - 16, 5, true);
        }
        const std::string label =
            renderer.truncatedText(UI_10_FONT_ID, selectedRecord_.title, sh - 50, EpdFontFamily::BOLD);
        const int textHeight = renderer.getTextHeight(UI_10_FONT_ID);
        const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
        const int tx = sx + (sw - textHeight) / 2;
        const int ty = sy + (sh + textWidth) / 2;
        renderer.drawTextRotated90CW(UI_10_FONT_ID, tx, ty, label.c_str(), true, EpdFontFamily::BOLD);
      }
    }
    renderer.fillRect(innerX, boardY, innerW, BOARD_H, true);
    renderer.fillRect(innerX + 6, boardY + BOARD_H, innerW - 12, 3, true);

    // --- detail panel for the selected book: spine labels are necessarily
    // terse, so this is where the current selection is actually readable ---
    renderer.drawRect(x + 9, detailY, innerW, DETAIL_H);
    if (count && shelfRecord(selected_, selectedRecord_)) {
      drawTwoLineTitle(renderer, x + 22, detailY + 10, innerW - 26, selectedRecord_.title);
      drawFitted(renderer, UI_10_FONT_ID, x + 22, detailY + 56, innerW - 26,
                 selectedRecord_.author[0] ? selectedRecord_.author : selectedRecord_.path, false);
      char bookOf[48];
      snprintf(bookOf, sizeof(bookOf), tr(STR_LIBRARY_BOOK_OF), static_cast<unsigned>(selected_ + 1),
               static_cast<unsigned>(count));
      char metaLine[160];
      snprintf(metaLine, sizeof(metaLine), "%s  /  %s", bookOf, sortLabel(sort_));
      drawFitted(renderer, SMALL_FONT_ID, x + 22, detailY + 84, innerW - 26, metaLine, false);
      const std::string progress = progressFor(selectedRecord_.path);
      drawFitted(renderer, SMALL_FONT_ID, x + 22, detailY + 106, innerW - 26, progress.c_str(), false);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, detailY + DETAIL_H / 2, tr(STR_LIBRARY_NO_BOOKS));
    }
  }
  const bool ready = !catalog_.isScanning() && !catalog_.failed();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ready ? tr(STR_ACTIONS) : "",
                                             ready && shelfBookCount() ? tr(STR_LIBRARY_PREV) : "",
                                             ready && shelfBookCount() ? tr(STR_LIBRARY_NEXT) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
