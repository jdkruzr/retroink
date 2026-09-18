#pragma once
#include <cstdint>
#include "activities/Activity.h"

namespace RetroInkBoot {
void drawScreen(const GfxRenderer& renderer, int pageWidth, int pageHeight, int eyeOffset = 0);
void drawSleepScreen(const GfxRenderer& renderer, int pageWidth, int pageHeight);
void drawFunnySleepScreen(const GfxRenderer& renderer, int pageWidth, int pageHeight, uint8_t sleepMode);
// batteryPercent: 0-100, cosmetic fill level of the animated battery glyph.
// lineIndex: rotates through the joke headline pool.
// realBatteryPercent: 0-100, the actual gauge reading, shown as text in the
// title bar (separate from the cosmetic fill, which cycles for visual
// interest rather than tracking the real charge level over what can be a
// multi-hour session).
void drawChargingScreen(const GfxRenderer& renderer, int pageWidth, int pageHeight, int batteryPercent,
                        int lineIndex, int realBatteryPercent);
}

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;
};
