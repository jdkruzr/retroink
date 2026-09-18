#pragma once
class GfxRenderer;
// Register once during renderer setup. Only UI font IDs are redirected and only
// while RetroInk is selected. Book body fonts retain upstream behavior.
void registerSystem6Fonts(GfxRenderer& renderer);
