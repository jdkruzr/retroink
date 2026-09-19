#include "SleepCoverAssets.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Txt.h>
#include <Xtc.h>

#include <cstdint>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "components/themes/dashboard/DashboardTheme.h"
#include "components/themes/minimal/MinimalTheme.h"

namespace {

constexpr int kMinimalSleepCoverHeight = MinimalMetrics::homeCoverImageHeight;
constexpr int kMinimalSleepCoverWidth = MinimalMetrics::homeCoverImageWidth;
constexpr int kDashboardSleepCoverHeight = DashboardMetrics::homeCoverImageHeight;
constexpr int kDashboardSleepCoverWidth = DashboardMetrics::homeCoverImageWidth;
// A small thumbnail alongside the title and weekly bar chart, not a
// full-width cover -- see RetroInkReadingDeskView::renderBookWeekStatus.
constexpr int kBookWeekCoverWidth = 110;
constexpr int kBookWeekCoverHeight = 160;

bool shouldPrepareFullCover() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER ||
         SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM;
}

bool shouldPrepareMinimalCover() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_SLEEP ||
         SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS_SLEEP;
}

bool shouldPrepareDashboardCover() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::DASHBOARD_SLEEP;
}

bool shouldPrepareBookWeekCover() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::BOOK_WEEK_STATS_SLEEP;
}

bool fileExists(const std::string& path) { return !path.empty() && Storage.exists(path.c_str()); }

int readerFontIdForRenderer(const GfxRenderer* renderer) { return renderer ? SETTINGS.getReaderFontId() : 0; }

}  // namespace

namespace SleepCoverAssets {

bool prepareXtc(const Xtc& xtc) {
  bool success = true;
  if (shouldPrepareFullCover()) {
    success = xtc.generateCoverBmp() && success;
  }
  if (shouldPrepareMinimalCover()) {
    success = xtc.generateThumbBmp(static_cast<uint16_t>(kMinimalSleepCoverWidth),
                                   static_cast<uint16_t>(kMinimalSleepCoverHeight)) &&
              success;
  }
  if (shouldPrepareDashboardCover()) {
    success = xtc.generateThumbBmp(static_cast<uint16_t>(kDashboardSleepCoverWidth),
                                   static_cast<uint16_t>(kDashboardSleepCoverHeight)) &&
              success;
  }
  if (shouldPrepareBookWeekCover()) {
    success = xtc.generateThumbBmp(static_cast<uint16_t>(kBookWeekCoverWidth),
                                   static_cast<uint16_t>(kBookWeekCoverHeight)) &&
              success;
  }
  return success;
}

bool prepareTxt(const Txt& txt) {
  if (!shouldPrepareFullCover() && !shouldPrepareMinimalCover() && !shouldPrepareDashboardCover() &&
      !shouldPrepareBookWeekCover()) {
    return true;
  }
  return txt.generateCoverBmp();
}

bool prepareFullCoverForPath(const std::string& bookPath, const bool cropped, const GfxRenderer* renderer) {
  if (bookPath.empty()) {
    return false;
  }

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true, Epub::XLocationLoadMode::Skip)) {
      return false;
    }
    return epub.generateCoverBmp(cropped, renderer, readerFontIdForRenderer(renderer));
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) {
      return false;
    }
    return xtc.generateCoverBmp();
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    return txt.generateCoverBmp();
  }
  return false;
}

bool prepareMinimalCoverForPath(const std::string& bookPath, const GfxRenderer* renderer) {
  if (bookPath.empty()) {
    return false;
  }

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true, Epub::XLocationLoadMode::Skip)) {
      return false;
    }
    return epub.generateAdaptiveThumbBmp(kMinimalSleepCoverWidth, kMinimalSleepCoverHeight, renderer,
                                         readerFontIdForRenderer(renderer));
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) {
      return false;
    }
    return xtc.generateThumbBmp(static_cast<uint16_t>(kMinimalSleepCoverWidth),
                                static_cast<uint16_t>(kMinimalSleepCoverHeight));
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    return txt.generateCoverBmp();
  }
  return false;
}

bool prepareDashboardCoverForPath(const std::string& bookPath, const GfxRenderer* renderer) {
  if (bookPath.empty()) {
    return false;
  }

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true, Epub::XLocationLoadMode::Skip)) {
      return false;
    }
    return epub.generateAdaptiveThumbBmp(kDashboardSleepCoverWidth, kDashboardSleepCoverHeight, renderer,
                                         readerFontIdForRenderer(renderer));
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) {
      return false;
    }
    return xtc.generateThumbBmp(static_cast<uint16_t>(kDashboardSleepCoverWidth),
                                static_cast<uint16_t>(kDashboardSleepCoverHeight));
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    return txt.generateCoverBmp();
  }
  return false;
}

bool prepareBookWeekCoverForPath(const std::string& bookPath, const GfxRenderer* renderer) {
  if (bookPath.empty()) {
    return false;
  }

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true, Epub::XLocationLoadMode::Skip)) {
      return false;
    }
    return epub.generateAdaptiveThumbBmp(kBookWeekCoverWidth, kBookWeekCoverHeight, renderer,
                                         readerFontIdForRenderer(renderer));
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) {
      return false;
    }
    return xtc.generateThumbBmp(static_cast<uint16_t>(kBookWeekCoverWidth),
                                static_cast<uint16_t>(kBookWeekCoverHeight));
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    return txt.generateCoverBmp();
  }
  return false;
}

std::string reusableCoverPathFor(const std::string& bookPath) {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    return Epub(bookPath, "/.crosspoint").getThumbBmpPath();
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    return Xtc(bookPath, "/.crosspoint").getThumbBmpPath();
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    return Txt(bookPath, "/.crosspoint").getCoverBmpPath();
  }
  return {};
}

std::string cachedCoverPathFor(const std::string& bookPath, const bool cropped) {
  std::string coverPath;
  if (FsHelpers::hasEpubExtension(bookPath)) {
    coverPath = Epub(bookPath, "/.crosspoint").getCoverBmpPath(cropped);
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    coverPath = Xtc(bookPath, "/.crosspoint").getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    coverPath = Txt(bookPath, "/.crosspoint").getCoverBmpPath();
  }

  return fileExists(coverPath) ? coverPath : std::string{};
}

std::string cachedMinimalCoverPathFor(const std::string& bookPath) {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    const Epub epub(bookPath, "/.crosspoint");
    const std::string coverPath = epub.getAdaptiveThumbBmpPath(kMinimalSleepCoverWidth, kMinimalSleepCoverHeight);
    return fileExists(coverPath) ? epub.getThumbBmpPath() : std::string{};
  }

  const std::string reusablePath = reusableCoverPathFor(bookPath);
  const std::string coverPath =
      UITheme::getCoverThumbPath(reusablePath, kMinimalSleepCoverWidth, kMinimalSleepCoverHeight);
  return fileExists(coverPath) ? reusablePath : std::string{};
}

std::string cachedDashboardCoverPathFor(const std::string& bookPath) {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    const Epub epub(bookPath, "/.crosspoint");
    const std::string coverPath = epub.getAdaptiveThumbBmpPath(kDashboardSleepCoverWidth, kDashboardSleepCoverHeight);
    return fileExists(coverPath) ? epub.getThumbBmpPath() : std::string{};
  }

  const std::string reusablePath = reusableCoverPathFor(bookPath);
  const std::string coverPath =
      UITheme::getCoverThumbPath(reusablePath, kDashboardSleepCoverWidth, kDashboardSleepCoverHeight);
  return fileExists(coverPath) ? reusablePath : std::string{};
}

std::string cachedBookWeekCoverPathFor(const std::string& bookPath) {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    const Epub epub(bookPath, "/.crosspoint");
    const std::string coverPath = epub.getAdaptiveThumbBmpPath(kBookWeekCoverWidth, kBookWeekCoverHeight);
    return fileExists(coverPath) ? epub.getThumbBmpPath() : std::string{};
  }

  const std::string reusablePath = reusableCoverPathFor(bookPath);
  const std::string coverPath = UITheme::getCoverThumbPath(reusablePath, kBookWeekCoverWidth, kBookWeekCoverHeight);
  return fileExists(coverPath) ? reusablePath : std::string{};
}

}  // namespace SleepCoverAssets
