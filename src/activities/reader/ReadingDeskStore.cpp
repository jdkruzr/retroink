#include "ReadingDeskStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <limits>

namespace {
constexpr char kDeskDir[] = "/.retroink";
constexpr char kDeskPath[] = "/.retroink/reading_desk.bin";
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderBytes = 5;

void writeLe32(uint8_t (&buf)[4], const uint32_t value) {
  buf[0] = static_cast<uint8_t>(value);
  buf[1] = static_cast<uint8_t>(value >> 8);
  buf[2] = static_cast<uint8_t>(value >> 16);
  buf[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t readLe32(const uint8_t (&buf)[4]) {
  return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
         (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}
}  // namespace

bool ReadingDeskStore::loaded_ = false;
uint32_t ReadingDeskStore::anchorDay_ = 0;
std::array<uint16_t, ReadingDeskStore::HISTORY_DAYS> ReadingDeskStore::seconds_{};

bool ReadingDeskStore::ensureLoaded() {
  if (loaded_) return true;
  loaded_ = true;
  seconds_.fill(0);

  FsFile file;
  if (!Storage.openFileForRead("RDESK", kDeskPath, file)) return true;
  uint8_t version = 0;
  uint8_t dayBytes[4] = {};
  const bool valid = file.read(&version, 1) == 1 && file.read(dayBytes, sizeof(dayBytes)) == sizeof(dayBytes) &&
                     version == kVersion &&
                     file.fileSize() == kHeaderBytes + HISTORY_DAYS * sizeof(uint16_t);
  if (!valid) {
    file.close();
    LOG_ERR("RDESK", "Ignoring invalid Reading Desk history");
    return true;
  }
  anchorDay_ = readLe32(dayBytes);
  for (size_t i = 0; i < seconds_.size(); ++i) {
    uint8_t pair[2] = {};
    if (file.read(pair, sizeof(pair)) != sizeof(pair)) {
      seconds_.fill(0);
      anchorDay_ = 0;
      LOG_ERR("RDESK", "Short Reading Desk history");
      break;
    }
    seconds_[i] = static_cast<uint16_t>(pair[0]) | (static_cast<uint16_t>(pair[1]) << 8);
  }
  file.close();
  return true;
}

bool ReadingDeskStore::save() {
  Storage.mkdir(kDeskDir);
  FsFile file;
  if (!Storage.openFileForWrite("RDESK", kDeskPath, file)) {
    LOG_ERR("RDESK", "Could not save Reading Desk history");
    return false;
  }
  uint8_t dayBytes[4];
  writeLe32(dayBytes, anchorDay_);
  bool ok = file.write(&kVersion, 1) == 1 && file.write(dayBytes, sizeof(dayBytes)) == sizeof(dayBytes);
  for (const uint16_t seconds : seconds_) {
    const uint8_t pair[2] = {static_cast<uint8_t>(seconds), static_cast<uint8_t>(seconds >> 8)};
    ok = ok && file.write(pair, sizeof(pair)) == sizeof(pair);
  }
  file.flush();
  file.close();
  if (!ok) LOG_ERR("RDESK", "Short write saving Reading Desk history");
  return ok;
}

void ReadingDeskStore::advanceTo(const uint32_t dayIndex) {
  if (anchorDay_ == 0) {
    anchorDay_ = dayIndex;
    return;
  }
  if (dayIndex <= anchorDay_) return;
  const uint32_t delta = dayIndex - anchorDay_;
  if (delta >= HISTORY_DAYS) {
    seconds_.fill(0);
  } else {
    for (size_t i = HISTORY_DAYS; i-- > delta;) seconds_[i] = seconds_[i - delta];
    std::fill_n(seconds_.begin(), delta, static_cast<uint16_t>(0));
  }
  anchorDay_ = dayIndex;
}

void ReadingDeskStore::addSeconds(const uint32_t dayIndex, const uint32_t seconds) {
  advanceTo(dayIndex);
  if (dayIndex > anchorDay_ || anchorDay_ - dayIndex >= HISTORY_DAYS) return;
  const size_t offset = anchorDay_ - dayIndex;
  const uint32_t value = static_cast<uint32_t>(seconds_[offset]) + seconds;
  seconds_[offset] = static_cast<uint16_t>(std::min<uint32_t>(value, std::numeric_limits<uint16_t>::max()));
}

void ReadingDeskStore::recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t seconds) {
  if (!localStart.isValid() || seconds == 0 || !ensureLoaded()) return;
  ReadingStatsDateTime cursor = localStart;
  while (seconds > 0) {
    const uint32_t toMidnight = 24U * 3600U - (static_cast<uint32_t>(cursor.hour) * 3600U +
                                                 static_cast<uint32_t>(cursor.minute) * 60U + cursor.second);
    const uint32_t segment = std::min(seconds, toMidnight);
    addSeconds(readingStatsDayIndex(cursor.date), segment);
    seconds -= segment;
    addSecondsToReadingStatsDateTime(cursor, segment);
  }
  save();  // once per committed reader session, never per page turn
}

uint32_t ReadingDeskStore::secondsForDate(const ReadingStatsDate& date) {
  if (!date.isValid() || !ensureLoaded() || anchorDay_ == 0) return 0;
  const uint32_t day = readingStatsDayIndex(date);
  if (day > anchorDay_ || anchorDay_ - day >= HISTORY_DAYS) return 0;
  return seconds_[anchorDay_ - day];
}

uint32_t ReadingDeskStore::secondsForOffset(const uint16_t daysBeforeAnchor) {
  if (!ensureLoaded() || daysBeforeAnchor >= HISTORY_DAYS) return 0;
  return seconds_[daysBeforeAnchor];
}

bool ReadingDeskStore::hasUsableClock() {
  ReadingStatsDateTime now;
  return getCurrentLocalReadingStatsDateTime(now);
}

ReadingStatsDate ReadingDeskStore::anchorDate() {
  ReadingStatsDate date;
  if (ensureLoaded() && anchorDay_ != 0) readingStatsDateFromDayIndex(anchorDay_, date);
  return date;
}

void ReadingDeskStore::resetForTests() {
  loaded_ = true;
  anchorDay_ = 0;
  seconds_.fill(0);
}
