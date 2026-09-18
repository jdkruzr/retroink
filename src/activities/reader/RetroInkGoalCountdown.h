#pragma once

#include <cstdint>

#include "ReadingStatsUtils.h"

class GfxRenderer;

// Small, allocation-free reader badge. The active session is held in RAM until
// reader exit, so it must be added to the committed daily ledger for display.
class RetroInkGoalCountdown {
 public:
  struct Sample {
    uint32_t day = 0;
    uint16_t minutesLeft = 0;
    bool valid = false;
  };

  static Sample sample(const ReadingStatsDateTime& sessionStart, uint32_t sessionSeconds, uint32_t pageSeconds);
  void drawOnPage(const GfxRenderer& renderer, const Sample& value, bool darkMode);
  void tick(const GfxRenderer& renderer, const Sample& value, bool darkMode);
  bool needsUpdate(const Sample& value) const {
    return value.valid && (value.day != day_ || lastMinutesLeft_ != static_cast<int16_t>(value.minutesLeft));
  }
  bool shouldPoll(uint32_t nowMs) {
    if (lastPollMs_ && nowMs - lastPollMs_ < 1000U) return false;
    lastPollMs_ = nowMs;
    return true;
  }

 private:
  void drawBadge(const GfxRenderer& renderer, const Sample& value, bool darkMode, bool flash = false) const;
  static void badgeRect(const GfxRenderer& renderer, int& x, int& y, int& w, int& h);

  uint32_t day_ = 0;
  uint32_t celebratedDay_ = 0;
  int16_t lastMinutesLeft_ = -1;
  uint32_t lastPollMs_ = 0;
};
