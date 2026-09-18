#pragma once

#include "components/themes/BaseTheme.h"

namespace System6Metrics {
constexpr ThemeMetrics makeValues() {
  auto v = BaseMetrics::values;
  v.batteryBarHeight = 0;
  v.headerHeight = 50;
  v.homeTopPadding = 50;
  v.homeCoverHeight = 180;
  v.homeCoverTileHeight = 210;
  v.menuRowHeight = 44;
  v.menuSpacing = 6;
  v.listInset = 12;
  v.listSidePadding = 12;
  v.listScrollWidth = 8;
  v.listRowRadius = 0;
  v.listSelectionStyle = 0;
  v.tabBarAppearance = ThemeTabBarAppearance::BorderedText;
  v.popupFrameThickness = 3;
  v.popupCornerRadius = 0;
  v.optionPopupSelectionRadius = 0;
  v.keyboardKeySpacing = 3;
  v.keyboardCenteredText = true;
  return v;
}
constexpr ThemeMetrics values = makeValues();
}  // namespace System6Metrics

// No state, image assets, or extra framebuffer. Decoration is drawn in place.
class System6Theme final : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool readerContext = false) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool allowInvertedText = false) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<const char*(int)>& buttonLabel,
                      const std::function<UIIcon(int)>& rowIcon) const override;
  void drawProgressBar(const GfxRenderer& renderer, Rect rect, size_t current, size_t total) const override;
  void fillProgressIndicator(const GfxRenderer& renderer, Rect rect, bool foregroundBlack = true) const override;
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
  void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, int progress) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f, const GlobalReadingStats* globalStats = nullptr,
                           const char* currentChapterTitle = nullptr) const override;
};
static_assert(sizeof(System6Theme) == sizeof(BaseTheme), "Theme must not add resident state");
