#include "System6Fonts.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "System6FontData.h"
#include "fontIds.h"

namespace {
constexpr int smallId = 0x536601;
constexpr int bodyId = 0x536602;
constexpr int largeId = 0x536603;
const EpdFont smallFont(&system6_small);
const EpdFont bodyFont(&system6_body);
const EpdFont largeFont(&system6_large);

int resolveSystem6Font(int original, const char* text, EpdFontFamily::Style style) {
  (void)style;
  if (SETTINGS.uiTheme != CrossPointSettings::SYSTEM6) return original;
  const EpdFont* font = nullptr;
  int replacement = original;
  if (original == SMALL_FONT_ID) {
    font = &smallFont;
    replacement = smallId;
  } else if (original == UI_10_FONT_ID) {
    font = &bodyFont;
    replacement = bodyId;
  } else if (original == UI_12_FONT_ID) {
    font = &largeFont;
    replacement = largeId;
  }
  if (!font) return original;
  // Keep whole-string measure/draw consistent and retain all upstream language
  // coverage. A string this small UI subset cannot render stays in Inter/CJK.
  const char* cursor = text;
  if (cursor) {
    uint32_t cp;
    while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
      if (cp != '\n' && cp != '\r' && !font->hasCodepoint(cp)) return original;
    }
  }
  return replacement;
}
}  // namespace

void registerSystem6Fonts(GfxRenderer& renderer) {
  // Three font-map entries allocated once, like upstream UI fonts. All glyph
  // data is constexpr flash storage, uncompressed: no decompression buffers.
  renderer.insertFont(smallId, EpdFontFamily(&smallFont));
  renderer.insertFont(bodyId, EpdFontFamily(&bodyFont));
  renderer.insertFont(largeId, EpdFontFamily(&largeFont));
  renderer.setFontResolver(resolveSystem6Font);
}
