#include "AlertActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointState.h"
#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AlertActivity::onEnter() {
  Activity::onEnter();
  title = APP_STATE.pendingAlertTitle;
  body = APP_STATE.pendingAlertBody;
  goHomeOnBack = APP_STATE.pendingAlertGoHomeOnBack.exchange(false, std::memory_order_relaxed);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    LOG_ERR("ALERT", "Alert screen could not be rendered synchronously");
    requestUpdate();
  }
}

void AlertActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (goHomeOnBack) {
      onGoHome();
    } else {
      finish();
    }
  }
}

void AlertActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const bool retroInk = SETTINGS.uiTheme == CrossPointSettings::SYSTEM6;
  const auto x = metrics.contentSidePadding + (retroInk ? 64 : 0);
  const auto contentWidth = pageWidth - x - metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (retroInk) {
    const int iconX = metrics.contentSidePadding + 10;
    renderer.drawRect(iconX, y + 2, 38, 38);
    renderer.drawRect(iconX + 3, y + 5, 32, 32);
    renderer.fillRect(iconX + 17, y + 11, 4, 15);
    renderer.fillRect(iconX + 17, y + 30, 4, 4);
  }

  auto bodyLines = renderer.wrappedText(UI_10_FONT_ID, body.c_str(), contentWidth, 10);
  for (const auto& line : bodyLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(goHomeOnBack ? tr(STR_HOME) : tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
