#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ReadingStatsUtils.h"

// RetroInk-owned daily reading ledger.  It deliberately stays separate from
// GlobalReadingStats so CrossInk sync and its on-disk format do not change.
class ReadingDeskStore {
 public:
  static constexpr size_t HISTORY_DAYS = 366;

  static void recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t seconds);
  static uint32_t secondsForDate(const ReadingStatsDate& date);
  static uint32_t secondsForOffset(uint16_t daysBeforeAnchor);
  static bool hasUsableClock();
  static ReadingStatsDate anchorDate();
  static void resetForTests();

 private:
  static bool ensureLoaded();
  static bool save();
  static void advanceTo(uint32_t dayIndex);
  static void addSeconds(uint32_t dayIndex, uint32_t seconds);

  static bool loaded_;
  static uint32_t anchorDay_;
  static std::array<uint16_t, HISTORY_DAYS> seconds_;
};
