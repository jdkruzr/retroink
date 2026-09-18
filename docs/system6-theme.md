# RetroInk theme for CrossInk 1.5.0

This is an unofficial, additive theme for the Xteink X3. The firmware retains
CrossInk 1.5.0's reader and existing seven themes. RetroInk is appended as theme
value 7, so existing settings retain their meaning. It does not switch your
saved theme automatically.

After installing a verified build, select **Settings > Display > UI Theme >
RetroInk**. Choose any other theme in the same menu to switch back.

The visual design uses monochrome striped window bars, square controls,
document/folder icons, and inverted selections. The clock and battery indicators
share the striped window bar and follow the existing clock and battery percentage
visibility settings. Checkerboard desktop texture surrounds the bottom controls,
including unused button positions. The labeled controls are inset as floating
keycaps with simple drop shadows. Footer content reserves its own space above
that strip, and long button labels are clipped within their frames without
switching to the smaller UI font. Settings keeps the active category tab inverted
while its rows are selected. RetroInk always uses the Large UI scale; choosing
Small while the theme is active leaves it at Large. Home renders an available
book cover with a coarse monochrome dither and uses a document icon when no cover
exists. Reader fonts, reading options, bookmarks, transfers, and sleep-screen
preferences remain available. The UI uses a small, uncompressed ChicagoFLF subset stored in flash. The
font author's public-domain statement is embedded in the source TTF; see
[font attribution](../assets/system6/README.md). Unsupported UI strings fall
back to the existing fonts, preserving international text coverage. Custom sleep images keep their own appearance.

Cold boots show a RetroInk startup window and Macintosh mark, identify RetroInk
as an AltFlow project, credit CrossInk,
and move the Macintosh's eyes left, right, then center when RetroInk was the saved
theme. The animation uses three code-drawn frames and no stored bitmap. CrossInk's
quick-resume and silent network boots intentionally skip splash screens, so those
paths continue directly to their destination. The default RetroInk sleep screen
says RetroInk is resting between chapters, shows a sleeping Macintosh, and
includes a quiet wake prompt. User-selected cover, custom,
statistics, overlay, and quick-resume sleep modes retain their requested content.

RetroInk also supplies square Macintosh-style checkbox labels, buttons, option
dialogs, alert icons, and toggle geometry. File lists distinguish folders, books,
text, image, dictionary, and generic document files. Empty collections pair their
message with an appropriate monochrome icon. Operation popups and reader status
bars use striped progress fills.

## Memory design

- No second framebuffer, background image, stored animation, or full-screen bitmap.
- Theme object has exactly the same size as BaseTheme (compile-time assertion).
- Theme-specific labels use a bounded 160-byte stack buffer, not temporary strings.
- Three font-map entries are allocated once at startup; a single resolver pointer
  selects the UI font. Book font IDs are never redirected.
- Desktop icons use line/rectangle drawing, not decoded image assets.
- The startup logo and floating button shadows are drawn directly into the
  existing one-bit framebuffer and add no bitmap asset allocation.
- Sleep-error art, controls, empty-state decoration, and progress stripes are
  geometry drawn directly into that same framebuffer.
- Home reuses CrossInk's compact cover thumbnail and streams it one row at a
  time through the existing renderer scratch buffer. It does not allocate or
  retain a cover-sized snapshot.
- Shared reader/font caches and SDK allocations retain upstream behavior.

Linker RAM is not runtime peak heap. Physical X3 testing must compare free heap,
largest free allocation, and task stack watermarks during the same book and
navigation sequence. No measured runtime memory reduction is claimed yet.

## Build and validation

Source base: upstream `v1.5.0`; preserve its pinned `freeink-sdk` submodule.

```
git submodule update --init freeink-sdk
pio run -e default
pio run -e simulator-X3
python scripts/run_simulator_smoke_test.py --env simulator-X3 --theme system6 --no-build
```

`default` is CrossInk's combined X3/X4 ESP32-C3 app image. Do not use Sticky or
X4 Pro images on the X3. Firmware installation follows the upstream
[SD card update instructions](installation.md): copy the built app `.bin` to
SD, then use Settings > System > SD Card Firmware Update. Keep a backup of the
SD card and a known working official 1.5.0 image before physical testing.

On hardware, check selecting/persisting RetroInk and switching back; empty and
populated Home with and without embedded cover art; every menu page; long and
non-Latin titles; the enforced Large UI scale;
portrait and landscape; opening a book, page turns, reader menus and bookmarks;
sleep/wake; and transfers. Confirm legibility, focus indicators, and refresh
behavior. No book-cache reset is required for this UI-only change.

The optional simulator-only `CROSSINK_SIMULATOR_CAPTURE_DIR` environment variable
writes native-panel PBM snapshots after each smoke-test screen. The directory
must exist; portrait captures need a clockwise quarter-turn to view upright.
