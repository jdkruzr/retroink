#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RetroInkCustomShelves.h"
#include "RetroInkLibraryCatalog.h"
#include "activities/Activity.h"

class RetroInkLibraryActivity final : public Activity {
  // One entry per selectable shelf: the 5 built-ins, then the custom shelves
  // from RetroInkCustomShelves::shelves() in order. customId == kNoShelf
  // means this entry is a built-in smart shelf (use `builtin`); otherwise
  // it's a custom shelf and `builtin` is unused.
  struct ShelfEntry {
    uint16_t customId = RetroInkCustomShelves::kNoShelf;
    RetroInkLibraryCatalog::Shelf builtin = RetroInkLibraryCatalog::Shelf::All;
    std::string name;
  };

  RetroInkLibraryCatalog catalog_;
  RetroInkCustomShelves customShelves_;
  std::vector<ShelfEntry> shelfCycle_;
  uint32_t shelfIndex_ = 0;
  RetroInkLibraryCatalog::Sort sort_ = RetroInkLibraryCatalog::Sort::Title;

  // Custom-shelf filtering lives here, not in RetroInkLibraryCatalog (see
  // that header's note on why it must not depend on custom shelves). Built
  // once per All-view build: rowShelfId_[viewRow] caches which custom shelf
  // (if any) owns that row, so switching between custom shelves - or between
  // a custom shelf and All - never re-reads the catalog. filteredRows_ is
  // the current custom shelf's list of matching view-row indices.
  std::vector<uint16_t> rowShelfId_;
  bool rowShelfIdValid_ = false;
  std::vector<uint32_t> filteredRows_;

  RetroInkLibraryCatalog::Record selectedRecord_;  // one reusable SD row, never a RAM book list
  RetroInkLibraryCatalog::Record reviewCurrent_;
  RetroInkLibraryCatalog::Record reviewOld_;
  uint32_t selected_ = 0;
  uint32_t stripStart_ = 0;  // leftmost visible spine (page-aligned)
  uint32_t lastShownScanCount_ = UINT32_MAX;
  bool scanFinished_ = false;
  bool moveFailed_ = false;
  GfxRenderer::Orientation previousOrientation_ = GfxRenderer::Orientation::Portrait;

  // --- shelf cycle ---
  void rebuildShelfCycle();
  const ShelfEntry& activeShelf() const { return shelfCycle_[shelfIndex_]; }
  bool activeIsCustom() const { return activeShelf().customId != RetroInkCustomShelves::kNoShelf; }
  // Custom shelves and the All built-in are both served by buildView(All, sort_),
  // so switching between them never needs a re-sort or SD re-read.
  bool activeUsesAllView() const {
    return activeIsCustom() || activeShelf().builtin == RetroInkLibraryCatalog::Shelf::All;
  }
  void applyShelfChange(uint32_t newIndex);
  uint32_t shelfIndexForToken(const char* token) const;
  void writeDefaultShelfToken();

  // --- indirection: shelfBookCount()/shelfRecord() are what everything
  // below should call instead of catalog_.viewCount()/viewRecord() directly,
  // so custom-shelf filtering is transparent to the rest of the Activity.
  uint32_t shelfBookCount() const {
    return activeIsCustom() ? static_cast<uint32_t>(filteredRows_.size()) : catalog_.viewCount();
  }
  uint32_t catalogRow(uint32_t i) const { return activeIsCustom() ? filteredRows_[i] : i; }
  bool shelfRecord(uint32_t i, RetroInkLibraryCatalog::Record& out) {
    return i < shelfBookCount() && catalog_.viewRecord(catalogRow(i), out);
  }
  void ensureRowShelfIds();
  void rebuildCustomFilter();
  void clampSelection();

  // --- layout (shared by loop() and render(), depends only on screen width) ---
  int spinesPerPage() const;
  int spineWidth() const;

  void rebuildView();
  void refreshLibrary();
  void showActions();
  void showShelves();
  void showSort();
  void showLetterJump();
  void moveSelectedBook(const std::string& path);
  void reviewRecovery();
  void reviewCandidate(uint32_t match);
  void openSelectedBook();

 public:
  RetroInkLibraryActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("RetroInkLibrary", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
