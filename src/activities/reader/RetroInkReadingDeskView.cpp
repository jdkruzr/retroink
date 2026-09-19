#include "RetroInkReadingDeskView.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReadingDeskStore.h"
#include "ReadingStatsUtils.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void window(const GfxRenderer& r, const int x, const int y, const int w, const int h, const char* title) {
  r.fillRect(x + 3, y + 3, w, h);
  r.fillRect(x, y, w, h, false);
  r.drawRect(x, y, w, h);
  r.drawRect(x + 2, y + 2, w - 4, h - 4);
  r.fillRect(x + 8, y + 6, w - 16, 24, false);
  r.drawCenteredText(UI_10_FONT_ID, y + 7, title, true, EpdFontFamily::BOLD);
}

void dither(const GfxRenderer& r, const int x, const int y, const int w, const int h) {
  for (int row = 0; row < h; row += 2)
    for (int col = ((row / 2) & 1) * 2; col < w; col += 4) r.fillRect(x + col, y + row, 2, std::min(2, h - row));
}

void footer(GfxRenderer& r, const MappedInputManager* input, const char* left, const char* right) {
  if (!input) return;
  const auto labels = input->mapLabels(left, right, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
}

ReadingStatsDate today() {
  ReadingStatsDateTime now;
  return getCurrentLocalReadingStatsDateTime(now) ? now.date : ReadingStatsDate{};
}

uint8_t cellState(const uint32_t seconds) {
  const uint32_t goal = static_cast<uint32_t>(SETTINGS.readingGoalMinutes) * 60U;
  if (seconds == 0) return 0;
  if (seconds < goal) return 1;
  return seconds > goal ? 3 : 2;
}

void drawCell(const GfxRenderer& r, const int x, const int y, const int size, const uint8_t state) {
  r.fillRect(x, y, size, size, false);
  r.drawRect(x, y, size, size);
  if (state == 1) dither(r, x + 2, y + 2, size - 4, size - 4);
  if (state == 2) r.fillRect(x + 2, y + 2, size - 4, size - 4);
  if (state == 3) {
    r.fillRect(x + 2, y + 2, size - 4, size - 4);
    r.fillRect(x + size / 2 - 1, y + 4, 3, size - 8, false);
    r.fillRect(x + 4, y + size / 2 - 1, size - 8, 3, false);
  }
}
}  // namespace

namespace RetroInkReadingDeskView {
void renderToday(GfxRenderer& r, const MappedInputManager* input, const GlobalReadingStats& globalStats) {
  r.clearScreen();
  CompactHeader::drawTitle(r, tr(STR_READING_DESK));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int w = r.getScreenWidth() - x * 2;
  const int top = metrics.topPadding + metrics.headerHeight + 10;
  if (!ReadingDeskStore::hasUsableClock()) {
    window(r, x, top, w, 150, tr(STR_TODAY));
    r.drawCenteredText(UI_10_FONT_ID, top + 70, tr(STR_SET_DATE_TIME));
    footer(r, input, tr(STR_BACK), tr(STR_ACTIONS));
    return;
  }
  const ReadingStatsDate date = today();
  const uint32_t seconds = ReadingDeskStore::secondsForDate(date);
  const uint32_t minutes = seconds / 60U;
  const uint32_t goal = SETTINGS.readingGoalMinutes;
  window(r, x, top, w, 164, tr(STR_TODAY));
  char progress[32];
  snprintf(progress, sizeof(progress), tr(STR_READING_GOAL_PROGRESS), static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(goal));
  r.drawCenteredText(UI_12_FONT_ID, top + 48, progress, true, EpdFontFamily::BOLD);
  const int barX = x + 34;
  const int barW = w - 68;
  r.drawRect(barX, top + 88, barW, 22);
  const int fill = static_cast<int>(std::min<uint32_t>(barW - 4, minutes * (barW - 4) / std::max<uint32_t>(1, goal)));
  if (fill > 0) r.fillRect(barX + 2, top + 90, fill, 18);
  char left[24];
  snprintf(left, sizeof(left), tr(STR_MINUTES_LEFT), static_cast<unsigned>(minutes >= goal ? 0 : goal - minutes));
  r.drawCenteredText(UI_10_FONT_ID, top + 122,
                     seconds > goal * 60U ? tr(STR_GOAL_EXCEEDED)
                                          : (seconds >= goal * 60U ? tr(STR_GOAL_MET) : left));

  const int weekTop = top + 184;
  window(r, x, weekTop, w, 190, tr(STR_STATS_DAY_OF_WEEK));
  ReadingStatsDate cursor = date;
  addDaysToReadingStatsDate(cursor, -6);
  const int cell = 26;
  const int gap = (w - 40 - cell * 7) / 6;
  for (int i = 0; i < 7; ++i) {
    const int cx = x + 20 + i * (cell + gap);
    drawCell(r, cx, weekTop + 64, cell, cellState(ReadingDeskStore::secondsForDate(cursor)));
    char label[4];
    snprintf(label, sizeof(label), "%u", static_cast<unsigned>(cursor.day));
    const int labelWidth = r.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    r.drawText(UI_10_FONT_ID, cx + (cell - labelWidth) / 2, weekTop + 105, label, true, EpdFontFamily::BOLD);
    addDaysToReadingStatsDate(cursor, 1);
  }
  char streak[30];
  snprintf(streak, sizeof(streak), tr(STR_READING_STREAK), static_cast<unsigned>(globalStats.currentReadingStreak(&date)));
  r.drawCenteredText(UI_10_FONT_ID, weekTop + 147, streak);
  footer(r, input, tr(STR_BACK), tr(STR_ACTIONS));
}

void renderYear(GfxRenderer& r, const MappedInputManager* input) {
  r.clearScreen();
  CompactHeader::drawTitle(r, tr(STR_READING_YEAR));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int w = r.getScreenWidth() - x * 2;
  const int top = metrics.topPadding + metrics.headerHeight + 10;
  const int bottom = r.getScreenHeight() - metrics.buttonHintsHeight - 10;
  const bool hasClock = ReadingDeskStore::hasUsableClock();
  const ReadingStatsDate current = hasClock ? today() : ReadingStatsDate{};
  char rangeTitle[40] = {};
  if (hasClock)
    snprintf(rangeTitle, sizeof(rangeTitle), tr(STR_READING_YEAR_RANGE), static_cast<unsigned>(current.year));
  window(r, x, top, w, bottom - top, hasClock ? rangeTitle : tr(STR_READING_YEAR));
  if (!hasClock) {
    r.drawCenteredText(UI_10_FONT_ID, top + 75, tr(STR_SET_DATE_TIME));
    footer(r, input, tr(STR_BACK), tr(STR_ACTIONS));
    return;
  }
  // Read across each row from January 1, leaving the unused positions after
  // December 31 at the bottom-right, like the end of a line in a book.
  constexpr int columns = 16;
  constexpr int rows = 23;
  static_assert(columns * rows >= 366);
  const int gridTop = top + 42;
  const int legendTop = bottom - 78;
  const int availableW = w - 36;
  const int availableH = legendTop - gridTop - 8;
  const int pitch = std::max(5, std::min(availableW / columns, availableH / rows));
  const int cell = pitch - 2;
  const int gridX = x + (w - columns * pitch) / 2;
  const int gridY = gridTop + (availableH - rows * pitch) / 2;
  const uint32_t todayIndex = readingStatsDayIndex(current);
  const ReadingStatsDate firstDay{current.year, 1, 1};
  const uint32_t firstDayIndex = readingStatsDayIndex(firstDay);
  const int daysInYear = isLeapYear(current.year) ? 366 : 365;
  for (int row = 0; row < rows; ++row) {
    const int daysInRow = std::min(columns, daysInYear - row * columns);
    for (int col = 0; col < daysInRow; ++col) {
      const int dayOfYear = row * columns + col;
      uint8_t state = 0;
      const uint32_t day = firstDayIndex + static_cast<uint32_t>(dayOfYear);
      ReadingStatsDate dayDate;
      if (day <= todayIndex && readingStatsDateFromDayIndex(day, dayDate))
        state = cellState(ReadingDeskStore::secondsForDate(dayDate));
      drawCell(r, gridX + col * pitch, gridY + row * pitch, cell, state);
    }
  }
  const int segment = (w - 38) / 2;
  const char* legendLabels[4] = {tr(STR_NO_READING), tr(STR_SOME_READING), tr(STR_GOAL_STATE),
                                 tr(STR_EXTRA_READING)};
  for (int i = 0; i < 4; ++i) {
    const int sx = x + 19 + (i % 2) * segment;
    const int sy = legendTop + (i / 2) * 29;
    drawCell(r, sx, sy, 16, static_cast<uint8_t>(i));
    r.drawText(UI_10_FONT_ID, sx + 22, sy, legendLabels[i]);
  }
  footer(r, input, tr(STR_BACK), tr(STR_ACTIONS));
}

void renderBookStatus(GfxRenderer& r, const MappedInputManager* input, const std::string& title,
                      const BookReadingStats& stats, const float progressPercent, const uint32_t estimatedTimeLeftSeconds) {
  r.clearScreen();
  CompactHeader::drawTitle(r, tr(STR_BOOK_STATUS));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int w = r.getScreenWidth() - x * 2;
  const int top = metrics.topPadding + metrics.headerHeight + 10;
  const int bottom = r.getScreenHeight() - metrics.buttonHintsHeight - 12;
  const int titleLineHeight = r.getLineHeight(UI_10_FONT_ID);
  const int titleLimit = std::max(100, bottom - top - 250);
  const int maxTitleLines = std::max(1, (titleLimit - 54) / std::max(1, titleLineHeight));
  const auto titleLines = r.wrappedText(UI_10_FONT_ID, title.c_str(), w - 46, maxTitleLines,
                                        EpdFontFamily::BOLD);
  const int titleHeight = std::min(titleLimit, std::max(100, 54 + static_cast<int>(titleLines.size()) * titleLineHeight));
  window(r, x, top, w, titleHeight, tr(STR_BOOK_STATUS));
  for (size_t i = 0; i < titleLines.size(); ++i)
    r.drawText(UI_10_FONT_ID, x + 23, top + 43 + static_cast<int>(i) * titleLineHeight,
               titleLines[i].c_str(), true, EpdFontFamily::BOLD);
  const int statsTop = top + titleHeight + 14;
  window(r, x, statsTop, w, std::max(216, bottom - statsTop), tr(STR_STATS_THIS_BOOK));
  char value[32];
  snprintf(value, sizeof(value), "%.0f%%", std::max(0.0f, progressPercent));
  r.drawCenteredText(UI_12_FONT_ID, statsTop + 50, value, true, EpdFontFamily::BOLD);
  r.drawRect(x + 34, statsTop + 92, w - 68, 22);
  const int fill = static_cast<int>(std::clamp(progressPercent, 0.0f, 100.0f) * (w - 72) / 100.0f);
  if (fill) r.fillRect(x + 36, statsTop + 94, fill, 18);
  BookReadingStats::formatDuration(stats.totalReadingSeconds, value, sizeof(value));
  r.drawText(UI_10_FONT_ID, x + 38, statsTop + 140, tr(STR_STATS_TIME_LBL));
  r.drawText(UI_10_FONT_ID, x + w / 2, statsTop + 140, value, true, EpdFontFamily::BOLD);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.totalPagesTurned));
  r.drawText(UI_10_FONT_ID, x + 38, statsTop + 173, tr(STR_STATS_PAGES_LBL));
  r.drawText(UI_10_FONT_ID, x + w / 2, statsTop + 173, value, true, EpdFontFamily::BOLD);
  if (estimatedTimeLeftSeconds) {
    BookReadingStats::formatDuration(estimatedTimeLeftSeconds, value, sizeof(value));
    r.drawCenteredText(UI_10_FONT_ID, statsTop + 205, value);
  }
  footer(r, input, tr(STR_BACK), tr(STR_ACTIONS));
}

void renderBookWeekStatus(GfxRenderer& r, const MappedInputManager* input, const std::string& title,
                          const BookReadingStats& stats, const std::string& coverBmpPath) {
  r.clearScreen();
  CompactHeader::drawTitle(r, tr(STR_BOOK_STATUS));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int w = r.getScreenWidth() - x * 2;
  const int top = metrics.topPadding + metrics.headerHeight + 10;
  const int bottom = r.getScreenHeight() - metrics.buttonHintsHeight - 12;

  // Cover + title window.
  constexpr int coverW = 110;
  constexpr int coverH = 160;
  constexpr int coverPad = 14;
  const int headerH = coverH + coverPad * 2 + 24;
  window(r, x, top, w, headerH, tr(STR_BOOK_STATUS));
  const int coverX = x + coverPad;
  const int coverY = top + 30 + coverPad;
  bool coverDrawn = false;
  if (!coverBmpPath.empty()) {
    FsFile file;
    if (Storage.openFileForRead("RDV", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        r.drawRect(coverX - 1, coverY - 1, coverW + 2, coverH + 2);
        r.drawBitmap(bitmap, coverX, coverY, coverW, coverH);
        coverDrawn = true;
      }
    }
  }
  if (!coverDrawn) {
    r.drawRect(coverX, coverY, coverW, coverH);
    dither(r, coverX + 2, coverY + 2, coverW - 4, coverH - 4);
  }
  const int textX = coverX + coverW + 16;
  const int textW = std::max(20, x + w - coverPad - textX);
  const int titleLineHeight = r.getLineHeight(UI_10_FONT_ID);
  const int maxTitleLines = std::max(1, coverH / titleLineHeight);
  const auto titleLines = r.wrappedText(UI_10_FONT_ID, title.c_str(), textW, maxTitleLines, EpdFontFamily::BOLD);
  int textY = coverY + std::max(0, (coverH - static_cast<int>(titleLines.size()) * titleLineHeight) / 2);
  for (const auto& line : titleLines) {
    r.drawText(UI_10_FONT_ID, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += titleLineHeight;
  }

  // "This Week" bar chart: cumulative reading time per weekday for this book.
  const int weekTop = top + headerH + 14;
  const int weekH = bottom - weekTop;
  window(r, x, weekTop, w, weekH, tr(STR_STATS_DAY_OF_WEEK));
  constexpr std::array<StrId, READING_DAY_OF_WEEK_COUNT> DAY_LABELS = {
      StrId::STR_STATS_MON, StrId::STR_STATS_TUE, StrId::STR_STATS_WED, StrId::STR_STATS_THU,
      StrId::STR_STATS_FRI, StrId::STR_STATS_SAT, StrId::STR_STATS_SUN};
  const uint32_t maxSeconds =
      *std::max_element(stats.dayOfWeekSeconds.begin(), stats.dayOfWeekSeconds.end());
  const int labelHeight = r.getLineHeight(UI_10_FONT_ID);
  const int chartTop = weekTop + 40;
  const int chartBottom = weekTop + weekH - labelHeight - 12;
  const int chartHeight = std::max(10, chartBottom - chartTop);
  constexpr int barGap = 10;
  const int barAreaW = w - 32;
  const int barW = (barAreaW - barGap * (static_cast<int>(READING_DAY_OF_WEEK_COUNT) - 1)) /
                   static_cast<int>(READING_DAY_OF_WEEK_COUNT);
  for (size_t i = 0; i < READING_DAY_OF_WEEK_COUNT; ++i) {
    const int bx = x + 16 + static_cast<int>(i) * (barW + barGap);
    const int barHeight = maxSeconds > 0 ? static_cast<int>((static_cast<uint64_t>(stats.dayOfWeekSeconds[i]) *
                                                             static_cast<uint64_t>(chartHeight)) /
                                                            maxSeconds)
                                         : 0;
    r.drawRect(bx, chartTop, barW, chartHeight);
    if (barHeight > 0) r.fillRect(bx, chartBottom - barHeight, barW, barHeight);
    const char* label = I18N.get(DAY_LABELS[i]);
    const int labelW = r.getTextWidth(UI_10_FONT_ID, label);
    r.drawText(UI_10_FONT_ID, bx + (barW - labelW) / 2, chartBottom + 8, label);
  }
  footer(r, input, tr(STR_BACK), tr(STR_ACTIONS));
}
}  // namespace RetroInkReadingDeskView
