#pragma once
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool canSnapshotOverlayBackground,
                         std::string currentBookPath = {}, bool fromTimeout = false,
                         GfxRenderer::Orientation sleepPopupOrientation = GfxRenderer::Orientation::Portrait)
      : Activity("Sleep", renderer, mappedInput),
        canSnapshotOverlayBackground(canSnapshotOverlayBackground),
        currentBookPath(std::move(currentBookPath)),
        fromTimeout(fromTimeout),
        sleepPopupOrientation(sleepPopupOrientation) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalSleepScreen() const;
  void renderMinimalStatsSleepScreen() const;
  void renderBookWeekStatsSleepScreen() const;
  void renderDashboardSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool forceFastNoGreyscale = false) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderOverlaySleepScreen() const;
  bool canSnapshotOverlayBackground = false;
  bool overlayBackgroundBufferStored = false;
  uint8_t clockVisibilityBeforeSleep = CrossPointSettings::HIDE_CLOCK_ALWAYS;
  std::string currentBookPath;
  bool fromTimeout = false;
  GfxRenderer::Orientation sleepPopupOrientation = GfxRenderer::Orientation::Portrait;
};
