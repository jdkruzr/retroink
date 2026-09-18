#include "RetroInkFocusDeskActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "FocusSessionActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void formatFocusDuration(const uint16_t minutes, char* buffer, const size_t size) {
  if (minutes >= 60) {
    snprintf(buffer, size, tr(STR_FOCUS_DURATION_HOURS_FORMAT), static_cast<unsigned>(minutes / 60),
             static_cast<unsigned>(minutes % 60));
  } else {
    snprintf(buffer, size, tr(STR_SLEEP_TIMER_VALUE_FORMAT), static_cast<unsigned>(minutes));
  }
}

uint8_t cadenceIndex(const uint8_t mode) {
  constexpr uint8_t modes[] = {CrossPointSettings::FOCUS_TIMER_EVERY_SECOND,
                               CrossPointSettings::FOCUS_TIMER_EVERY_MINUTE,
                               CrossPointSettings::FOCUS_TIMER_EVERY_FIVE_MINUTES};
  for (uint8_t i = 0; i < 3; ++i)
    if (modes[i] == mode) return i;
  return 1;
}

uint8_t cadenceMode(const uint8_t index) {
  constexpr uint8_t modes[] = {CrossPointSettings::FOCUS_TIMER_EVERY_SECOND,
                               CrossPointSettings::FOCUS_TIMER_EVERY_MINUTE,
                               CrossPointSettings::FOCUS_TIMER_EVERY_FIVE_MINUTES};
  return modes[std::min<uint8_t>(index, 2)];
}
}  // namespace

RetroInkFocusDeskActivity::RetroInkFocusDeskActivity(GfxRenderer& renderer, MappedInputManager& input, Entry entry,
                                                     std::string returnBookPath)
    : Activity("RetroInkFocusDesk", renderer, input),
      entry_(entry),
      returnBookPath_(std::move(returnBookPath)) {}

void RetroInkFocusDeskActivity::onEnter() {
  Activity::onEnter();
  previousOrientation_ = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void RetroInkFocusDeskActivity::onExit() {
  renderer.setOrientation(previousOrientation_);
  Activity::onExit();
}

void RetroInkFocusDeskActivity::activate() {
  if (entry_ == Entry::Home && settingsMenu_) {
    editor_ = selected_ == 0 ? Editor::Length : Editor::Refresh;
    if (editor_ == Editor::Refresh) selectedCadence_ = cadenceIndex(SETTINGS.focusTimerRefresh);
    requestUpdate();
    return;
  }
  if (selected_ == 0) {
    startActivityForResult(std::make_unique<FocusSessionActivity>(renderer, mappedInput, SETTINGS.focusDurationMinutes(),
                                                                 returnBookPath_),
                           [](const ActivityResult&) {});
    return;
  }
  if (entry_ == Entry::Stats) {
    editor_ = Editor::Goal;
  } else {
    settingsMenu_ = true;
    selected_ = 0;
  }
  requestUpdate();
}

void RetroInkFocusDeskActivity::adjust(int delta) {
  if (editor_ == Editor::Goal) {
    const int value = std::clamp<int>(SETTINGS.readingGoalMinutes + delta * 5, 5, 180);
    SETTINGS.readingGoalMinutes = static_cast<uint8_t>(value);
  } else if (editor_ == Editor::Length) {
    const int value = std::clamp<int>(SETTINGS.focusDurationMinutes() + delta * 5, 5, 1440);
    SETTINGS.setFocusDurationMinutes(static_cast<uint16_t>(value));
  } else if (editor_ == Editor::Refresh) {
    selectedCadence_ = static_cast<uint8_t>(std::clamp<int>(selectedCadence_ + delta, 0, 2));
    requestUpdate();
    return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void RetroInkFocusDeskActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (editor_ != Editor::None) {
      editor_ = Editor::None;
      requestUpdate();
    } else if (settingsMenu_) {
      settingsMenu_ = false;
      selected_ = 1;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (editor_ != Editor::None) {
      if (editor_ == Editor::Refresh) {
        SETTINGS.focusTimerRefresh = cadenceMode(selectedCadence_);
        SETTINGS.saveToFile();
      }
      editor_ = Editor::None;
      requestUpdate();
    } else {
      activate();
    }
    return;
  }
  const bool sidePrevious = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool frontPrevious = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool sideNext = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool frontNext = mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool previous = sidePrevious || frontPrevious;
  const bool next = sideNext || frontNext;
  if (editor_ != Editor::None) {
    if (previous) adjust(editor_ == Editor::Length && sidePrevious ? -12 : -1);
    if (next) adjust(editor_ == Editor::Length && sideNext ? 12 : 1);
  } else if (previous || next) {
    selected_ = static_cast<uint8_t>((selected_ + itemCount() + (previous ? -1 : 1)) % itemCount());
    requestUpdate();
  }
}

void RetroInkFocusDeskActivity::render(RenderLock&&) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, entry_ == Entry::Home ? tr(STR_FOCUS_SESSION) : tr(STR_READING_DESK));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int w = renderer.getScreenWidth() - 2 * x;
  const int top = metrics.topPadding + metrics.headerHeight + 14;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 14;
  renderer.drawRect(x, top, w, bottom - top);
  renderer.drawRect(x + 2, top + 2, w - 4, bottom - top - 4);
  char value[32];
  if (editor_ == Editor::None) {
    if (entry_ == Entry::Stats) {
      snprintf(value, sizeof(value), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned>(SETTINGS.readingGoalMinutes));
    } else {
      formatFocusDuration(SETTINGS.focusDurationMinutes(), value, sizeof(value));
    }
    renderer.drawCenteredText(UI_12_FONT_ID, top + 43, value, true, EpdFontFamily::BOLD);
    const char* rows[2] = {
        settingsMenu_ ? tr(STR_FOCUS_SESSION_LENGTH) : tr(STR_START_FOCUS),
        settingsMenu_ ? tr(STR_FOCUS_TIMER_REFRESH)
                      : entry_ == Entry::Stats ? tr(STR_READING_GOAL) : tr(STR_SETTINGS_TITLE),
    };
    const int rowTop = top + 94;
    const int rowH = std::min(76, std::max(52, (bottom - rowTop - 20) / itemCount()));
    for (int i = 0; i < itemCount(); ++i) {
      const int y = rowTop + i * rowH;
      renderer.drawRect(x + 20, y, w - 40, rowH - 8);
      if (selected_ == i) renderer.fillRect(x + 24, y + 4, 8, rowH - 16);
      renderer.drawText(UI_10_FONT_ID, x + 42, y + 16, rows[i], true, EpdFontFamily::BOLD);
    }
  } else {
    const int editorTop = top + 48;
    const int editorHeight = std::min(240, bottom - editorTop - 28);
    renderer.drawRect(x + 20, editorTop, w - 40, editorHeight);
    const char* label = editor_ == Editor::Goal ? tr(STR_READING_GOAL)
                        : editor_ == Editor::Length ? tr(STR_FOCUS_SESSION_LENGTH)
                                                    : tr(STR_FOCUS_TIMER_REFRESH);
    renderer.drawCenteredText(UI_10_FONT_ID, editorTop + 26, label, true, EpdFontFamily::BOLD);
    if (editor_ == Editor::Refresh) {
      const char* cadences[3] = {tr(STR_FOCUS_REFRESH_SECOND), tr(STR_FOCUS_REFRESH_MINUTE),
                                 tr(STR_FOCUS_REFRESH_FIVE_MINUTES)};
      const int rowHeight = std::max(40, std::min(53, (editorHeight - 86) / 3));
      for (int i = 0; i < 3; ++i) {
        const int rowY = editorTop + 58 + i * rowHeight;
        renderer.drawRect(x + 34, rowY, w - 68, rowHeight - 5);
        if (selectedCadence_ == i) renderer.fillRect(x + 38, rowY + 4, 8, rowHeight - 13);
        renderer.drawText(UI_10_FONT_ID, x + 58, rowY + 10, cadences[i], true, EpdFontFamily::BOLD);
      }
      if (selectedCadence_ == 0)
        renderer.drawCenteredText(UI_10_FONT_ID, editorTop + editorHeight + 13, tr(STR_FOCUS_SECOND_WARNING));
    } else {
      if (editor_ == Editor::Goal) {
        snprintf(value, sizeof(value), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                 static_cast<unsigned>(SETTINGS.readingGoalMinutes));
      } else {
        formatFocusDuration(SETTINGS.focusDurationMinutes(), value, sizeof(value));
      }
      renderer.drawCenteredText(UI_12_FONT_ID, editorTop + 82, value, true, EpdFontFamily::BOLD);
      if (editor_ == Editor::Length)
        renderer.drawCenteredText(UI_10_FONT_ID, editorTop + 147, tr(STR_FOCUS_SIDE_HOUR_HINT));
    }
  }
  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK), editor_ == Editor::None ? tr(STR_SELECT) : tr(STR_DONE),
      editor_ == Editor::Length || editor_ == Editor::Goal ? tr(STR_FOCUS_MINUS_FIVE) : tr(STR_DIR_UP),
      editor_ == Editor::Length || editor_ == Editor::Goal ? tr(STR_FOCUS_PLUS_FIVE) : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
