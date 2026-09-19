#pragma once

#include <string>
#include <vector>

#include "I18nKeys.h"
#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  struct ChangelogLine {
    std::string text;
    bool bold = false;
  };
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  ScreenTransitionRefresh screenTransitionRefresh;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  StrId failureMessage = StrId::STR_UPDATE_FAILED;
  OtaUpdater updater;
  std::vector<ChangelogLine> changelogLines;
  int changelogVisibleLines = 0;  // computed on first render, once layout is known
  int changelogScrollLine = 0;

  void onWifiSelectionComplete(bool success);
  void runUpdateInstall();
  void buildChangelogLines(int maxWidth);
  void scrollChangelog(int delta);
  void changelogGeometry(int& top, int& bottom, int& lineHeight) const;

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
