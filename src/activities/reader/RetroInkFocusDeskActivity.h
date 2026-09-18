#pragma once

#include "activities/Activity.h"

class RetroInkFocusDeskActivity final : public Activity {
 public:
  enum class Entry : uint8_t { Home, Stats };

 private:
  Entry entry_;
  std::string returnBookPath_;
  uint8_t selected_ = 0;
  bool settingsMenu_ = false;
  enum class Editor : uint8_t { None, Goal, Length, Refresh };
  Editor editor_ = Editor::None;
  uint8_t selectedCadence_ = 1;
  GfxRenderer::Orientation previousOrientation_ = GfxRenderer::Orientation::Portrait;

  uint8_t itemCount() const { return 2; }
  void activate();
  void adjust(int delta);

 public:
  RetroInkFocusDeskActivity(GfxRenderer& renderer, MappedInputManager& input, Entry entry,
                            std::string returnBookPath = {});
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
};
