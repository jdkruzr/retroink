#pragma once

#include <cstdint>
#include <atomic>
#include <string>

#include "activities/Activity.h"

class FocusSessionActivity final : public Activity {
  uint32_t durationSeconds_ = 0;
  uint32_t remainingAtEntry_ = 0;
  uint32_t redrawCount_ = 0;
  uint32_t startedMs_ = 0;
  uint32_t lastDrawMs_ = 0;
  uint32_t lastCheckpointMs_ = 0;
  uint32_t lastDrawnRemaining_ = UINT32_MAX;
  bool completed_ = false;
  bool completionFlashed_ = false;
  bool restored_ = false;
  std::atomic<bool> rendered_{false};
  std::string returnBookPath_;
  GfxRenderer::Orientation previousOrientation_ = GfxRenderer::Orientation::Portrait;

  uint32_t remainingSeconds() const;
  uint32_t refreshIntervalSeconds() const;

 public:
  struct Snapshot {
    uint32_t durationSeconds = 0;
    uint32_t remainingSeconds = 0;
    uint32_t redrawCount = 0;
    std::string returnBookPath;
  };

  FocusSessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint16_t minutes,
                       std::string returnBookPath = {});
  FocusSessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Snapshot snapshot);
  static bool readSnapshot(bool timerWake, Snapshot& snapshot);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
