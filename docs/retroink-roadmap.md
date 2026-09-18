# RetroInk feature roadmap

## Recommended first feature: Reading Session Timer

Add a lightweight Desk Accessory that starts a 15, 30, 45, or 60 minute reading
session. The remaining time appears in the RetroInk menu bar when the page is
redrawn, and a classic alert appears when the session ends.

This fits the theme, adds a useful reading function, and suits the X3:

- Track one deadline with `millis()`; no task, heap allocation, or extra buffer.
- Reuse the existing option dialog and alert activity.
- Update the visible countdown on normal page turns to avoid periodic e-ink
  refreshes and battery drain.
- Persist only the selected duration if desired; an active timer can remain
  session-only.

## Later possibilities

- **Finder labels:** mark books To Read, Reading, Finished, or Favorite and filter
  the library by label.
- **Scrapbook:** present saved clippings and bookmarks as one Mac-style desk
  accessory, with book and date filters.
- **Owner card:** store an owner/contact line and optionally show it on the sleep
  screen, useful if a reader is lost.
