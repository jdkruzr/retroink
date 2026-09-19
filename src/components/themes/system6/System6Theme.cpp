#include "System6Theme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "RecentBooksStore.h"
#include "components/TouchRegistry.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Bounded UTF-8 truncation: a small stack buffer, no temporary strings.
int fitLabel(const GfxRenderer& r, int font, const char* text, int width, char (&out)[160]) {
  size_t n = 0;
  if (!text || width <= 0) {
    out[0] = '\0';
    return 0;
  }
  while (n < sizeof(out) - 1 && text[n]) {
    out[n] = text[n];
    ++n;
  }
  if (text[n])
    while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
  out[n] = '\0';
  int measured = r.getTextWidth(font, out);
  if (measured <= width) return measured;

  // Doesn't fit: shrink and append an ellipsis so a cut label reads as
  // truncated rather than as a different, complete word (e.g. a button hint
  // for "Controls" silently becoming "Control").
  constexpr char kEllipsis[] = "...";
  constexpr size_t kEllipsisLen = sizeof(kEllipsis) - 1;
  while (true) {
    if (n == 0) {
      std::memcpy(out, kEllipsis, kEllipsisLen + 1);
      measured = r.getTextWidth(font, out);
      if (measured > width) {
        out[0] = '\0';
        measured = 0;
      }
      return measured;
    }
    --n;
    while (n && (static_cast<unsigned char>(out[n]) & 0xc0) == 0x80) --n;
    if (n + kEllipsisLen < sizeof(out) - 1) {
      std::memcpy(out + n, kEllipsis, kEllipsisLen + 1);
      measured = r.getTextWidth(font, out);
      if (measured <= width) return measured;
    } else {
      out[n] = '\0';
    }
  }
}
void label(const GfxRenderer& r, int font, int x, int y, int width, const char* text, bool black = true) {
  char clipped[160];
  fitLabel(r, font, text, width, clipped);
  r.drawText(font, x, y, clipped, black);
}
void frame(const GfxRenderer& r, Rect b) {
  if (b.width < 8 || b.height < 8) return;
  r.fillRect(b.x + 3, b.y + 3, b.width, b.height);
  r.fillRect(b.x, b.y, b.width, b.height, false);
  r.drawRect(b.x, b.y, b.width, b.height);
  r.drawRect(b.x + 2, b.y + 2, b.width - 4, b.height - 4);
}
void desktop(const GfxRenderer& r, int top, int bottom) {
  // One-bit checkerboard, drawn into the existing buffer. No background bitmap.
  for (int y = top; y < bottom; y += 2) {
    for (int x = ((y / 2) & 1) * 2; x < r.getScreenWidth(); x += 4) r.fillRect(x, y, 2, std::min(2, bottom - y));
  }
}
void macIcon(const GfxRenderer& r, int x, int y, int scale = 1, bool black = true) {
  r.drawRect(x, y, 24 * scale, 29 * scale, black);
  r.drawRect(x + 3 * scale, y + 3 * scale, 18 * scale, 17 * scale, black);
  r.fillRect(x + 7 * scale, y + 8 * scale, 2 * scale, 2 * scale, black);
  r.fillRect(x + 15 * scale, y + 8 * scale, 2 * scale, 2 * scale, black);
  r.drawLine(x + 8 * scale, y + 14 * scale, x + 15 * scale, y + 14 * scale, black);
  r.drawLine(x + 10 * scale, y + 24 * scale, x + 20 * scale, y + 24 * scale, black);
  r.drawRect(x + 2 * scale, y + 30 * scale, 20 * scale, 3 * scale, black);
}
void documentIcon(const GfxRenderer& r, int x, int y, bool black) {
  r.drawRect(x, y, 22, 26, black);
  r.drawLine(x + 15, y, x + 15, y + 7, black);
  r.drawLine(x + 15, y + 7, x + 21, y + 7, black);
  for (int line = 12; line <= 20; line += 4) r.drawLine(x + 5, y + line, x + 16, y + line, black);
}
void menuIcon(const GfxRenderer& r, UIIcon icon, int x, int y, bool black) {
  switch (icon) {
    case Folder:
    case Library:
      r.drawRect(x, y + 6, 26, 19, black);
      r.drawRect(x + 2, y + 2, 11, 5, black);
      break;
    case Settings:
      r.drawRect(x, y + 1, 26, 24, black);
      for (int i = 0; i < 3; ++i) {
        r.drawLine(x + 4, y + 6 + i * 7, x + 22, y + 6 + i * 7, black);
        r.fillRect(x + 7 + (i % 2) * 9, y + 4 + i * 7, 3, 5, black);
      }
      break;
    case Transfer:
    case Wifi:
    case Hotspot:
      r.drawRect(x, y + 1, 26, 18, black);
      r.drawLine(x + 13, y + 19, x + 13, y + 24, black);
      r.drawLine(x + 6, y + 25, x + 20, y + 25, black);
      break;
    case Chart:
      for (int i = 0; i < 3; ++i) r.drawRect(x + i * 9, y + 16 - i * 6, 6, 10 + i * 6, black);
      break;
    case Hourglass:
      // A 26-pixel Macintosh-style hourglass, drawn into the menu buffer.
      r.drawLine(x + 3, y + 2, x + 23, y + 2, black);
      r.drawLine(x + 3, y + 24, x + 23, y + 24, black);
      r.drawLine(x + 5, y + 3, x + 21, y + 23, black);
      r.drawLine(x + 21, y + 3, x + 5, y + 23, black);
      r.fillRect(x + 10, y + 7, 6, 2, black);
      r.fillRect(x + 12, y + 13, 2, 3, black);
      r.fillRect(x + 10, y + 19, 6, 2, black);
      break;
    case Recent:
      r.drawRect(x + 3, y + 2, 22, 22, black);
      r.drawLine(x + 14, y + 6, x + 14, y + 14, black);
      r.drawLine(x + 14, y + 14, x + 20, y + 14, black);
      break;
    case Book:
      r.drawRect(x + 3, y + 1, 22, 25, black);
      r.drawLine(x + 8, y + 2, x + 8, y + 25, black);
      r.drawLine(x + 11, y + 7, x + 21, y + 7, black);
      break;
    case Dictionary:
      r.drawLine(x + 2, y + 4, x + 13, y + 8, black);
      r.drawLine(x + 13, y + 8, x + 24, y + 4, black);
      r.drawLine(x + 2, y + 4, x + 2, y + 25, black);
      r.drawLine(x + 24, y + 4, x + 24, y + 25, black);
      r.drawLine(x + 2, y + 25, x + 13, y + 22, black);
      r.drawLine(x + 13, y + 8, x + 13, y + 22, black);
      r.drawLine(x + 13, y + 22, x + 24, y + 25, black);
      break;
    default:
      documentIcon(r, x + 2, y, black);
      break;
  }
}
}  // namespace

void System6Theme::drawHeader(const GfxRenderer& r, Rect rect, const char* title, const char* subtitle,
                              bool readerContext) const {
  const bool home = title == nullptr;
  int top, right, bottom, left;
  r.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int x = std::max(rect.x + 8, left + 4);
  const int end = std::min(rect.x + rect.width - 8, r.getScreenWidth() - right - 4);
  const int h = 40;
  const int y = std::max(rect.y, top);
  if (end - x < 80 || y < top) return;
  const int contentBottom = r.getScreenHeight() - System6Metrics::values.buttonHintsHeight - 4;
  desktop(r, 0, r.getScreenHeight());
  if (!home) {
    frame(r, Rect{x, y, end - x, std::max(h, contentBottom - y)});
    // Window grow box, as on the original monochrome desktop.
    for (int d = 5; d < 18; d += 4) r.drawLine(end - d - 3, contentBottom - 5, end - 4, contentBottom - d - 4);
  }
  frame(r, Rect{x, y, end - x, h});
  for (int dy = 7; dy < h - 5; dy += 4) r.drawLine(x + 6, y + dy, end - 7, y + dy);

  const auto& m = System6Metrics::values;
  constexpr int statusFont = UI_10_FONT_ID;
  const int statusTextY = y + (h - r.getLineHeight(statusFont)) / 2;
  const bool showPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = end - 12 - m.batteryWidth;
  const int batteryY = y + (h - m.batteryHeight) / 2;
  const uint16_t percentage = powerManager.getBatteryPercentage();
  char percentageText[8] = {};
  std::snprintf(percentageText, sizeof(percentageText), "%u%%", static_cast<unsigned>(percentage));
  const int percentageWidth = showPercentage ? r.getTextWidth(statusFont, percentageText) : 0;
  const int percentageX = batteryX - BaseTheme::batteryPercentSpacing - percentageWidth;
  int statusLeft = showPercentage ? percentageX : batteryX;

  char timeText[9] = {};
  const bool showClock =
      (readerContext ? SETTINGS.shouldShowClockInReader() : SETTINGS.shouldShowClockOutsideReader()) &&
      halClock.isAvailable() &&
      halClock.formatTime(timeText, sizeof(timeText), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  int clockX = statusLeft;
  if (showClock) {
    const int clockWidth = r.getTextWidth(statusFont, timeText);
    clockX = statusLeft - 12 - clockWidth;
    statusLeft = clockX;
  }

  int subtitleX = statusLeft;
  char clippedSubtitle[160] = {};
  if (subtitle && subtitle[0]) {
    const int subtitleWidth = fitLabel(r, statusFont, subtitle, std::max(1, (end - x) / 4), clippedSubtitle);
    subtitleX = statusLeft - 12 - subtitleWidth;
    statusLeft = subtitleX;
  }

  r.fillRect(statusLeft - 6, y + 3, end - statusLeft + 1, h - 6, false);
  if (subtitle && subtitle[0]) r.drawText(statusFont, subtitleX, statusTextY, clippedSubtitle);
  if (showClock) r.drawText(statusFont, clockX, statusTextY, timeText);
  if (showClock && showPercentage) {
    const int dividerX = percentageX - 6;
    r.drawLine(dividerX, y + 8, dividerX, y + h - 9);
  }
  if (showPercentage) r.drawText(statusFont, percentageX, statusTextY, percentageText);
  drawBatteryOutline(r, batteryX, batteryY, m.batteryWidth, m.batteryHeight);
  fillBatteryIcon(r, Rect{batteryX, batteryY, m.batteryWidth, m.batteryHeight}, percentage);

  const int font = uiScaleSpec().bodyFontId;
  const char* text = title && title[0] ? title : tr(STR_THEME_SYSTEM6);
  char clipped[160];
  const int titleLeft = x + (home ? 42 : 32);
  const int titleRight = std::max(titleLeft + 1, statusLeft - 10);
  const int width = fitLabel(r, font, text, titleRight - titleLeft, clipped);
  const int centeredX = x + (end - x - width) / 2;
  const int tx = std::clamp(centeredX, titleLeft, std::max(titleLeft, titleRight - width));
  r.fillRect(tx - 7, y + 3, width + 14, h - 6, false);
  r.drawText(font, tx, y + (h - r.getLineHeight(font)) / 2, clipped);
  if (home) {
    r.fillRect(x + 6, y + 2, 32, h - 4, false);
    macIcon(r, x + 9, y + 3);
  } else {
    // Decorative window box; navigation still uses the physical Back button.
    r.fillRect(x + 9, y + 13, 14, 14, false);
    r.drawRect(x + 9, y + 13, 14, 14);
  }
}

void System6Theme::drawButtonHints(GfxRenderer& r, const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4, bool allowInvertedText) const {
  if (gpio.hasTouch()) return;

  const auto orientation = r.getOrientation();
  const bool invertText = allowInvertedText && orientation == GfxRenderer::Orientation::PortraitInverted;
  r.setOrientation(GfxRenderer::Orientation::Portrait);
  const int pageHeight = r.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonBandHeight = System6Metrics::values.buttonHintsHeight;
  constexpr int narrowButtonPositions[] = {25, 130, 245, 350};
  constexpr int wideButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = r.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  int topMargin, rightMargin, bottomMargin, leftMargin;
  r.getOrientedViewableTRBL(&topMargin, &rightMargin, &bottomMargin, &leftMargin);
  (void)topMargin;
  (void)rightMargin;
  (void)leftMargin;
  const int buttonTop = pageHeight - buttonBandHeight;
  constexpr int keyTopInset = 5;
  constexpr int keyBottomInset = 5;
  constexpr int keyShadowOffset = 3;
  const int keyTop = buttonTop + keyTopInset;
  const int buttonHeight = std::max(1, buttonBandHeight - bottomMargin - keyTopInset - keyBottomInset);

  // The header paints the same checker over the whole screen. Clearing this
  // band first also makes screens without a header use the identical pattern.
  r.fillRect(0, buttonTop, r.getScreenWidth(), buttonBandHeight, false);
  desktop(r, buttonTop, pageHeight);
  for (int i = 0; i < 4; ++i) {
    const int x = buttonPositions[i];
    if (labels[i] && labels[i][0])
      TouchRegistry::getInstance().add(Rect{x, keyTop, buttonWidth, buttonHeight}, i, TouchRegistry::Button);
    r.fillRect(x + keyShadowOffset, keyTop + keyShadowOffset, buttonWidth, buttonHeight);
    r.fillRect(x, keyTop, buttonWidth, buttonHeight, false);
    r.drawRect(x, keyTop, buttonWidth, buttonHeight);
    r.drawLine(x + 2, keyTop + 2, x + buttonWidth - 3, keyTop + 2);
  }

  r.setOrientation(invertText ? GfxRenderer::Orientation::PortraitInverted : GfxRenderer::Orientation::Portrait);
  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || !labels[i][0]) continue;
    const int x = buttonPositions[invertText ? 3 - i : i];
    constexpr int preferredPadding = 8;
    constexpr int minPadding = 2;
    constexpr int font = UI_10_FONT_ID;
    char clipped[160];
    int textWidth = fitLabel(r, font, labels[i], buttonWidth - preferredPadding * 2, clipped);
    if (strcmp(clipped, labels[i]) != 0) {
      // Didn't fit at the normal padding: before accepting a truncated
      // label, try again with the tightest padding that still looks like a
      // button, so a label that's only barely too wide (a category name
      // like "Controls" cycling through this same slot) still shows in
      // full instead of getting cut off.
      char tighter[160];
      const int tighterWidth = fitLabel(r, font, labels[i], buttonWidth - minPadding * 2, tighter);
      if (strcmp(tighter, labels[i]) == 0) {
        textWidth = tighterWidth;
        std::memcpy(clipped, tighter, sizeof(clipped));
      }
    }
    const int textOffset = std::max(0, (buttonHeight - r.getLineHeight(font)) / 2);
    const int textY = invertText ? bottomMargin + keyBottomInset + textOffset : keyTop + textOffset;
    r.drawText(font, x + (buttonWidth - textWidth) / 2, textY, clipped);
  }
  r.setOrientation(orientation);
}

void System6Theme::drawProgressBar(const GfxRenderer& r, const Rect rect, const size_t current,
                                   const size_t total) const {
  if (total == 0 || rect.width < 5 || rect.height < 5) return;
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);
  const int fillWidth = std::max(0, (rect.width - 4) * std::clamp(percent, 0, 100) / 100);
  r.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  r.drawRect(rect.x, rect.y, rect.width, rect.height);
  r.drawRect(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4);
  for (int x = rect.x + 3; x < rect.x + 2 + fillWidth; x += 4) {
    r.fillRect(x, rect.y + 3, std::min(2, rect.x + 2 + fillWidth - x), rect.height - 6);
  }
  char percentText[8];
  std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
  r.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText);
}

void System6Theme::fillProgressIndicator(const GfxRenderer& r, const Rect rect, const bool foregroundBlack) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  for (int x = rect.x; x < rect.x + rect.width; x += 4) {
    r.fillRect(x, rect.y, std::min(2, rect.x + rect.width - x), rect.height, foregroundBlack);
  }
}

Rect System6Theme::drawPopup(const GfxRenderer& r, const char* message) const {
  constexpr int titleHeight = 31;
  const int maxWidth = r.getScreenWidth() - 48;
  const int messageWidth = r.getTextWidth(UI_10_FONT_ID, message ? message : "");
  const int width = std::clamp(messageWidth + 72, 230, maxWidth);
  constexpr int height = 112;
  const int x = (r.getScreenWidth() - width) / 2;
  const int y = std::max(18, r.getScreenHeight() / 12);
  frame(r, Rect{x, y, width, height});
  for (int stripeY = y + 8; stripeY < y + titleHeight - 5; stripeY += 4) {
    r.drawLine(x + 7, stripeY, x + width - 8, stripeY);
  }
  const char* title = tr(STR_THEME_SYSTEM6);
  const int titleWidth = r.getTextWidth(UI_10_FONT_ID, title);
  const int titleX = x + (width - titleWidth) / 2;
  r.fillRect(titleX - 7, y + 4, titleWidth + 14, titleHeight - 6, false);
  r.drawText(UI_10_FONT_ID, titleX, y + (titleHeight - r.getLineHeight(UI_10_FONT_ID)) / 2, title);
  r.drawLine(x + 3, y + titleHeight, x + width - 4, y + titleHeight);
  documentIcon(r, x + 15, y + 48, true);
  char clipped[160];
  fitLabel(r, UI_10_FONT_ID, message, width - 66, clipped);
  r.drawText(UI_10_FONT_ID, x + 50, y + 50, clipped);
  r.displayBuffer();
  return Rect{x, y, width, height};
}

void System6Theme::fillPopupProgress(const GfxRenderer& r, const Rect& layout, const int progress) const {
  const int barX = layout.x + 50;
  const int barY = layout.y + layout.height - 25;
  const int barWidth = layout.width - 65;
  constexpr int barHeight = 10;
  const int fillWidth = std::max(0, (barWidth - 4) * std::clamp(progress, 0, 100) / 100);
  r.fillRect(barX, barY, barWidth, barHeight, false);
  r.drawRect(barX, barY, barWidth, barHeight);
  for (int x = barX + 2; x < barX + 2 + fillWidth; x += 4) {
    r.fillRect(x, barY + 2, std::min(2, barX + 2 + fillWidth - x), barHeight - 4);
  }
  r.displayBuffer(HalDisplay::FAST_REFRESH);
}

void System6Theme::drawButtonMenu(GfxRenderer& r, Rect rect, int count, int selected,
                                  const std::function<const char*(int)>& buttonLabel,
                                  const std::function<UIIcon(int)>& rowIcon) const {
  const auto& m = System6Metrics::values;
  const int step = m.menuRowHeight + m.menuSpacing;
  const int rows = std::min(7, (rect.height - 44) / step);
  if (count <= 0 || rows <= 0 || rect.width < 80) return;
  const int start = std::clamp(selected, 0, count - 1) / rows * rows;
  const int visible = std::min(rows, count - start);
  const int x = rect.x + 14;
  const int width = rect.width - 32;
  frame(r, Rect{x, rect.y + 2, width, visible * step + 40});
  for (int dy = 8; dy < 28; dy += 4) r.drawLine(x + 6, rect.y + dy, x + width - 7, rect.y + dy);
  const int titleWidth = r.getTextWidth(UI_10_FONT_ID, tr(STR_MENU));
  const int titleX = x + (width - titleWidth) / 2;
  r.fillRect(titleX - 8, rect.y + 4, titleWidth + 16, 26, false);
  r.drawText(UI_10_FONT_ID, titleX, rect.y + 6, tr(STR_MENU));
  r.drawLine(x + 3, rect.y + 32, x + width - 4, rect.y + 32);
  for (int n = 0; n < visible; ++n) {
    const int i = start + n;
    const int y = rect.y + 39 + n * step;
    const bool active = i == selected;
    if (active) r.fillRect(x + 5, y, width - 10, m.menuRowHeight);
    menuIcon(r, rowIcon ? rowIcon(i) : File, x + 12, y + 9, !active);
    const int font = uiScaleSpec().bodyFontId;
    label(r, font, x + 49, y + (m.menuRowHeight - r.getLineHeight(font)) / 2, width - 72,
          buttonLabel ? buttonLabel(i) : "", !active);
    TouchRegistry::getInstance().add(Rect{x + 5, y, width - 10, m.menuRowHeight}, i, TouchRegistry::Item);
  }
  if (start > 0) {
    r.drawLine(x + width - 18, rect.y + 46, x + width - 14, rect.y + 42);
    r.drawLine(x + width - 14, rect.y + 42, x + width - 10, rect.y + 46);
  }
  if (start + visible < count) {
    const int y = rect.y + 32 + visible * step;
    r.drawLine(x + width - 18, y - 4, x + width - 14, y);
    r.drawLine(x + width - 14, y, x + width - 10, y - 4);
  }
}

void System6Theme::drawRecentBookCover(GfxRenderer& r, Rect rect, const std::vector<RecentBook>& books, int selected,
                                       bool& rendered, bool& stored, bool& restored, const std::function<bool()>& store,
                                       const BookReadingStats* stats, float progress,
                                       const GlobalReadingStats* globalStats, const char* chapter) const {
  (void)store;
  (void)stats;
  (void)globalStats;
  (void)chapter;
  (void)progress;
  // The cover is streamed a row at a time through a 2x2 monochrome dither. No
  // image-sized buffer or Home cover snapshot is retained.
  rendered = false;
  stored = false;
  restored = false;
  if (rect.height < 70 || rect.width < 100) return;
  const Rect tile{rect.x + 24, rect.y + 14, rect.width - 52, rect.height - 30};
  frame(r, tile);
  const bool active = !books.empty() && selected == 0;
  if (active) r.fillRect(tile.x + 5, tile.y + 5, tile.width - 10, tile.height - 10);
  bool coverDrawn = false;
  int textX = tile.x + 82;
  if (!books.empty() && !books.front().coverBmpPath.empty()) {
    const std::string coverPath =
        UITheme::getCoverThumbPath(books.front().coverBmpPath, System6Metrics::values.homeCoverHeight);
    FsFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        const int maxCoverWidth = std::min(104, tile.width / 3);
        const int maxCoverHeight = std::max(1, tile.height - 28);
        int coverHeight = maxCoverHeight;
        int coverWidth = bitmap.getWidth() * coverHeight / bitmap.getHeight();
        if (coverWidth > maxCoverWidth) {
          coverWidth = maxCoverWidth;
          coverHeight = bitmap.getHeight() * coverWidth / bitmap.getWidth();
        }
        coverWidth = std::max(1, coverWidth);
        coverHeight = std::max(1, coverHeight);
        const int coverX = tile.x + 18 + (maxCoverWidth - coverWidth) / 2;
        const int coverY = tile.y + (tile.height - coverHeight) / 2;
        r.fillRect(coverX - 3, coverY - 3, coverWidth + 6, coverHeight + 6, false);
        r.drawBitmapDithered(bitmap, coverX, coverY, coverWidth, coverHeight);
        r.drawRect(coverX - 2, coverY - 2, coverWidth + 4, coverHeight + 4);
        coverDrawn = true;
        textX = tile.x + 18 + maxCoverWidth + 18;
      }
    }
  }
  if (!coverDrawn) documentIcon(r, tile.x + 28, tile.y + (tile.height - 26) / 2, !active);
  const int font = uiScaleSpec().bodyFontId;
  const int textWidth = tile.x + tile.width - textX - 18;
  const int lineHeight = r.getLineHeight(font);
  const int titleY = tile.y + 10;
  const int maxTitleLines = std::max(1, (tile.height - 20) / std::max(1, lineHeight));
  const auto titleLines = r.wrappedText(font, books.empty() ? tr(STR_NO_RECENT_BOOKS) : books.front().title.c_str(),
                                        textWidth, maxTitleLines, EpdFontFamily::BOLD);
  for (size_t i = 0; i < titleLines.size(); ++i) {
    r.drawText(font, textX, titleY + static_cast<int>(i) * lineHeight, titleLines[i].c_str(), !active,
               EpdFontFamily::BOLD);
  }
  if (!books.empty()) {
    TouchRegistry::getInstance().add(tile, 0, TouchRegistry::Cover);
  }
}
