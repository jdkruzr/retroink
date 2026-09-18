#pragma once

#include <string>

#include "BookReadingStats.h"
#include "GlobalReadingStats.h"

class GfxRenderer;
class MappedInputManager;

namespace RetroInkReadingDeskView {
void renderToday(GfxRenderer& renderer, const MappedInputManager* input, const GlobalReadingStats& globalStats);
void renderYear(GfxRenderer& renderer, const MappedInputManager* input);
void renderBookStatus(GfxRenderer& renderer, const MappedInputManager* input, const std::string& title,
                      const BookReadingStats& stats, float progressPercent, uint32_t estimatedTimeLeftSeconds);
}  // namespace RetroInkReadingDeskView
