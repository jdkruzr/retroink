#include "FocusSessionActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#ifndef SIMULATOR
#include <HalPowerManager.h>
#endif
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "ReadingStatsUtils.h"

namespace {
constexpr char kFocusStatePath[] = "/.retroink/focus_session.bin";
constexpr uint32_t kFocusMagic = 0x464B4E52;  // RNKF
constexpr uint32_t kFocusVersion = 1;
constexpr size_t kMaxBookPath = 192;

struct FocusFileHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t durationSeconds;
  uint32_t remainingBeforeSleep;
  uint32_t scheduledSleepSeconds;
  uint32_t finishClockSeconds;
  uint32_t redrawCount;
  uint32_t pathLength;
};

void drawPixelCountdown(const GfxRenderer& r, const int x, const int y, const int w, const int h,
                        const char* countdown) {
  static const uint8_t digits[10][5] = {
      {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
      {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};
  const int length = static_cast<int>(strlen(countdown));
  int units = length - 1;
  for (int i = 0; i < length; ++i) units += countdown[i] == ':' ? 1 : 3;
  const int pixel = std::max(8, std::min((w - 48) / units, h / 9));
  const int px = x + (w - units * pixel) / 2;
  const int py = y + h / 4 - (5 * pixel) / 2;
  int column = 0;
  for (int i = 0; i < length; ++i) {
    const char symbol = countdown[i];
    const int columns = symbol == ':' ? 1 : 3;
    for (int row = 0; row < 5; ++row) {
      const uint8_t bits = symbol == ':' ? (row == 1 || row == 3 ? 1 : 0)
                                         : digits[static_cast<unsigned>(symbol - '0')][row];
      for (int col = 0; col < columns; ++col) {
        if (bits & (1U << (columns - 1 - col)))
          r.fillRect(px + (column + col) * pixel + 1, py + row * pixel + 1, pixel - 2, pixel - 2);
      }
    }
    column += columns + (i == length - 1 ? 0 : 1);
  }
}

uint32_t clockSeconds() {
  const time_t systemTime = time(nullptr);
  if (systemTime >= 1577836800 && systemTime <= UINT32_MAX) return static_cast<uint32_t>(systemTime);
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return 0;
  const ReadingStatsDate date{year, month, day};
  if (!date.isValid()) return 0;
  return readingStatsDayIndex(date) * 86400U + static_cast<uint32_t>(hour) * 3600U +
         static_cast<uint32_t>(minute) * 60U;
}

bool saveFocusState(const uint32_t duration, const uint32_t remaining, const uint32_t sleepSeconds,
                    const uint32_t redrawCount,
                    const std::string& bookPath) {
  if (bookPath.size() > kMaxBookPath) {
    LOG_ERR("FOCUS", "Book path too long to preserve for timer sleep");
    return false;
  }
  Storage.mkdir("/.retroink");
  FsFile file;
  if (!Storage.openFileForWrite("FOCUS", kFocusStatePath, file)) {
    LOG_ERR("FOCUS", "Could not save focus timer state");
    return false;
  }
  const uint32_t now = clockSeconds();
  const FocusFileHeader header{kFocusMagic, kFocusVersion, duration, remaining, sleepSeconds,
                               now ? now + remaining : 0, redrawCount, static_cast<uint32_t>(bookPath.size())};
  const bool okay = file.write(&header, sizeof(header)) == sizeof(header) &&
                    file.write(bookPath.data(), bookPath.size()) == bookPath.size();
  file.flush();
  file.close();
  if (!okay) LOG_ERR("FOCUS", "Short write saving focus timer state");
  return okay;
}

void frame(const GfxRenderer& r, const int x, const int y, const int w, const int h) {
  r.fillRect(x + 3, y + 3, w, h);
  r.fillRect(x, y, w, h, false);
  r.drawRect(x, y, w, h);
  r.drawRect(x + 2, y + 2, w - 4, h - 4);
}
}  // namespace

FocusSessionActivity::FocusSessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint16_t minutes,
                                           std::string returnBookPath)
    : Activity("FocusSession", renderer, mappedInput),
      durationSeconds_(static_cast<uint32_t>(minutes) * 60U),
      remainingAtEntry_(durationSeconds_),
      returnBookPath_(std::move(returnBookPath)) {}

FocusSessionActivity::FocusSessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Snapshot snapshot)
    : Activity("FocusSession", renderer, mappedInput),
      durationSeconds_(snapshot.durationSeconds),
      remainingAtEntry_(snapshot.remainingSeconds),
      redrawCount_(snapshot.redrawCount),
      completed_(snapshot.remainingSeconds == 0),
      restored_(true),
      returnBookPath_(std::move(snapshot.returnBookPath)) {}

bool FocusSessionActivity::readSnapshot(const bool timerWake, Snapshot& snapshot) {
  if (!Storage.exists(kFocusStatePath)) return false;
  FsFile file;
  if (!Storage.openFileForRead("FOCUS", kFocusStatePath, file)) return false;
  FocusFileHeader header{};
  const bool valid = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kFocusMagic &&
                     header.version == kFocusVersion && header.durationSeconds >= 5U * 60U &&
                     header.durationSeconds <= 24U * 60U * 60U && header.remainingBeforeSleep <= header.durationSeconds &&
                     header.scheduledSleepSeconds <= header.remainingBeforeSleep &&
                     header.pathLength <= kMaxBookPath && file.fileSize() == sizeof(header) + header.pathLength;
  if (!valid) {
    file.close();
    Storage.remove(kFocusStatePath);
    LOG_ERR("FOCUS", "Discarded invalid timer state");
    return false;
  }
  char path[kMaxBookPath + 1] = {};
  if (header.pathLength && file.read(path, header.pathLength) != header.pathLength) {
    file.close();
    Storage.remove(kFocusStatePath);
    LOG_ERR("FOCUS", "Discarded short timer path");
    return false;
  }
  file.close();
  snapshot.durationSeconds = header.durationSeconds;
  snapshot.redrawCount = header.redrawCount;
  snapshot.returnBookPath.assign(path, header.pathLength);
  uint32_t remaining = timerWake ? header.remainingBeforeSleep - header.scheduledSleepSeconds
                                 : header.remainingBeforeSleep;
  const uint32_t now = clockSeconds();
  if (header.finishClockSeconds && now && now >= header.finishClockSeconds) {
    remaining = 0;
  } else if (header.finishClockSeconds && now) {
    remaining = std::min(remaining, header.finishClockSeconds - now);
  }
  snapshot.remainingSeconds = remaining;
  return true;
}

void FocusSessionActivity::onEnter() {
  Activity::onEnter();
#ifndef SIMULATOR
  powerManager.disableWiFiForFocusSession();
#endif
  previousOrientation_ = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  startedMs_ = millis();
  lastCheckpointMs_ = startedMs_;
  saveFocusState(durationSeconds_, remainingAtEntry_, 0, redrawCount_, returnBookPath_);
  requestUpdate();
}

void FocusSessionActivity::onExit() {
  if (Storage.exists(kFocusStatePath)) Storage.remove(kFocusStatePath);
  renderer.setOrientation(previousOrientation_);
  Activity::onExit();
}

uint32_t FocusSessionActivity::remainingSeconds() const {
  const uint32_t elapsed = (millis() - startedMs_) / 1000U;
  return elapsed >= remainingAtEntry_ ? 0 : remainingAtEntry_ - elapsed;
}

uint32_t FocusSessionActivity::refreshIntervalSeconds() const {
  switch (SETTINGS.focusTimerRefresh) {
    case CrossPointSettings::FOCUS_TIMER_EVERY_SECOND:
      return 1U;
    case CrossPointSettings::FOCUS_TIMER_EVERY_FIVE_MINUTES:
      return 5U * 60U;
    default:
      return 60U;
  }
}

void FocusSessionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    if (restored_ && !returnBookPath_.empty()) {
      onSelectBook(returnBookPath_);
    } else {
      finish();
    }
    return;
  }
  const uint32_t remaining = remainingSeconds();
  if (remaining == 0 && !completed_) {
    completed_ = true;
    requestUpdate();
    return;
  }
  if (!rendered_.load()) return;
  const uint32_t now = millis();
  if (now - lastCheckpointMs_ >= 60000U) {
    if (saveFocusState(durationSeconds_, remaining, 0, redrawCount_, returnBookPath_))
      lastCheckpointMs_ = now;
  }
  if (!completed_) {
    const uint32_t cadence = refreshIntervalSeconds();
    const uint32_t toBoundary = lastDrawnRemaining_ % cadence ? lastDrawnRemaining_ % cadence : cadence;
    if (lastDrawnRemaining_ >= toBoundary && remaining <= lastDrawnRemaining_ - toBoundary) {
      ++redrawCount_;
      rendered_.store(false);
      requestUpdate();
    }
  }
}

void FocusSessionActivity::render(RenderLock&&) {
  if (completed_ && !completionFlashed_) {
    // Two fast polarity changes announce completion; the final dialog is the
    // only timer-managed full refresh, which also clears accumulated ghosting.
    for (int i = 0; i < 2; ++i) {
      renderer.clearScreen();
      renderer.fillRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      renderer.clearScreen();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    completionFlashed_ = true;
  }
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_FOCUS_SESSION));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int w = renderer.getScreenWidth() - x * 2;
  const int y = metrics.topPadding + metrics.headerHeight + 9;
  const int h = renderer.getScreenHeight() - y - metrics.buttonHintsHeight - 12;
  frame(renderer, x, y, w, h);
  const uint32_t remaining = remainingSeconds();
  lastDrawnRemaining_ = remaining;
  if (completed_) {
    renderer.drawCenteredText(UI_12_FONT_ID, y + h / 3, tr(STR_SESSION_COMPLETE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, y + h / 2, tr(STR_FOCUS_COMPLETE_BREAK));
  } else {
    char countdown[12];
    const bool showSeconds = SETTINGS.focusTimerRefresh == CrossPointSettings::FOCUS_TIMER_EVERY_SECOND;
    if (showSeconds && durationSeconds_ >= 3600U) {
      snprintf(countdown, sizeof(countdown), "%02lu:%02lu:%02lu", static_cast<unsigned long>(remaining / 3600U),
               static_cast<unsigned long>((remaining / 60U) % 60U), static_cast<unsigned long>(remaining % 60U));
    } else if (showSeconds) {
      snprintf(countdown, sizeof(countdown), "%02lu:%02lu", static_cast<unsigned long>(remaining / 60U),
               static_cast<unsigned long>(remaining % 60U));
    } else {
      const uint32_t minutes = (remaining + 59U) / 60U;
      if (durationSeconds_ >= 3600U)
        snprintf(countdown, sizeof(countdown), "%02lu:%02lu", static_cast<unsigned long>(minutes / 60U),
                 static_cast<unsigned long>(minutes % 60U));
      else
        snprintf(countdown, sizeof(countdown), "%02lu", static_cast<unsigned long>(minutes));
    }
    drawPixelCountdown(renderer, x, y, w, h, countdown);
    if (!showSeconds)
      renderer.drawCenteredText(UI_10_FONT_ID, y + h / 2 - 10,
                                durationSeconds_ >= 3600U ? tr(STR_FOCUS_COUNTDOWN_HOURS_MINUTES)
                                                           : tr(STR_FOCUS_COUNTDOWN_MINUTES),
                                true, EpdFontFamily::BOLD);
    // One bounded, framebuffer-only tile per session minute. The grid grows
    // denser for long sessions; black tiles disappear as minutes elapse.
    const int blocks = static_cast<int>(durationSeconds_ / 60U);
    const int areaW = w - 52;
    const int areaH = std::max(38, h / 2 - 48);
    int columns = 1;
    int bestSize = 0;
    for (int c = 1; c <= blocks; ++c) {
      const int rows = (blocks + c - 1) / c;
      const int size = std::min(areaW / c, areaH / rows);
      if (size > bestSize) { bestSize = size; columns = c; }
      if (size <= 2 && c > areaW / 2) break;
    }
    const int rows = (blocks + columns - 1) / columns;
    const int pitchX = areaW / columns;
    const int pitchY = areaH / rows;
    const int size = std::max(2, std::min(pitchX, pitchY) - 2);
    const int startX = x + (w - columns * pitchX) / 2;
    const int startY = y + h / 2 + 20;
    const int active = static_cast<int>(std::min<uint32_t>(blocks, (remaining + 59U) / 60U));
    for (int i = 0; i < blocks; ++i) {
      const int bx = startX + (i % columns) * pitchX;
      const int by = startY + (i / columns) * pitchY;
      renderer.drawRect(bx, by, size, size);
      if (i < active && size > 3) renderer.fillRect(bx + 2, by + 2, size - 4, size - 4);
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", tr(STR_END_SESSION));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  lastDrawMs_ = millis();
  renderer.displayBuffer(completed_ ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  rendered_.store(true);
}
