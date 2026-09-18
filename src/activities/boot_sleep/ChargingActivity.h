#pragma once

#include <atomic>
#include <cstdint>

#include "activities/Activity.h"

// Fun, System6-styled screen shown while the device is plugged in and not
// being read (see main.cpp's USB-plug guard). Not a real battery gauge -
// purely cosmetic, redrawn on a slow cadence with an occasional full refresh
// to clear checkerboard ghosting over a long charging session.
class ChargingActivity final : public Activity {
  uint32_t enteredMs_ = 0;
  uint32_t lastDrawMs_ = 0;
  uint32_t redrawCount_ = 0;
  std::atomic<bool> rendered_{false};

 public:
  explicit ChargingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Charging", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
