#include "ChargingActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "BootActivity.h"
#include "HalGPIO.h"
#include "MappedInputManager.h"

namespace {
// Redraw cadence: slow enough to avoid needless e-ink wear over a long
// charging session, fast enough that the animation still reads as "alive".
constexpr uint32_t kRedrawIntervalMs = 4000U;
// Every Nth redraw gets a full refresh instead of fast, to clear the
// checkerboard desktop's accumulated ghosting (same reasoning as
// SleepActivity's sleepRefreshMode(), just on a plain counter here since this
// screen doesn't key off the UI theme).
constexpr uint32_t kFullRefreshEvery = 10U;
// Battery fill ramps over this many redraws, then wraps back to empty.
constexpr uint32_t kFillCycleSteps = 8U;
}  // namespace

void ChargingActivity::onEnter() {
  Activity::onEnter();
  enteredMs_ = millis();
  lastDrawMs_ = enteredMs_;
  requestUpdate();
}

void ChargingActivity::loop() {
  if (!gpio.isUsbConnected() || mappedInput.wasAnyReleased()) {
    activityManager.goHome();
    return;
  }
  if (!rendered_.load()) return;
  const uint32_t now = millis();
  if (now - lastDrawMs_ >= kRedrawIntervalMs) {
    ++redrawCount_;
    rendered_.store(false);
    requestUpdate();
  }
}

void ChargingActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int batteryPercent = static_cast<int>((redrawCount_ % kFillCycleSteps) * 100U / (kFillCycleSteps - 1));
  const int lineIndex = static_cast<int>(redrawCount_ / kFillCycleSteps);
  RetroInkBoot::drawChargingScreen(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), batteryPercent,
                                   lineIndex);
  lastDrawMs_ = millis();
  const bool fullRefresh = redrawCount_ % kFullRefreshEvery == 0;
  renderer.displayBuffer(fullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  rendered_.store(true);
}
