// The panel behind the power button, shared by both readers.
//
// The power button used to close the book. That was the only physical way out
// for a hand holding the device by its edge, and it stays the only physical
// way out -- but "out" is now one row of a panel that also carries the things
// a reader wants mid-book and had nowhere to ask for: where am I in this book,
// take me to that chapter, keep this place, make the text bigger.
//
// Drawn as full screens rather than an overlay, for two reasons. E-paper has
// no cheap way to put something translucent over a page and take it away
// again -- every appearance and dismissal costs a full refresh either way --
// and the panel's lists want the whole glass, because a chapter list on a
// 480x800 panel is seven rows at a time whatever else is on the screen.
//
// The geometry is shelf.h's, so a chapter list and a book list are the same
// object to a finger that has used one of them.
#pragma once
#include "shelf.h"
#include "tools_ui.h"

namespace rmenu {

// Which page of the panel is showing. `None` means the book itself.
enum class Page : uint8_t { None, Root, Contents, Marks, Text };

// The root is buttons, not rows: five things, each worth a thumb.
inline constexpr int ROOT_Y0 = 120, ROOT_H = 104, ROOT_GAP = 12;
inline constexpr int ROOT_MARGIN = 28;

inline TRect rootRect(int i, int w) {
  return {ROOT_MARGIN, ROOT_Y0 + i * (ROOT_H + ROOT_GAP), w - 2 * ROOT_MARGIN, ROOT_H};
}

// A root item. `sub` is the line under the label -- the state of the thing, so
// the panel answers as much as it offers ("3 kept", "medium, normal").
struct Item {
  const char* label;
  const char* sub;
};

inline void drawRoot(ToolsHost& h, ToolsCanvas& c, const char* title, const Item* items, int n) {
  h.topBar(title, false, "READ");
  for (int i = 0; i < n; i++) {
    const TRect r = rootRect(i, c.width());
    c.drawRect(r.x, r.y, r.w, r.h, 2, true);
    c.text(r.x + 22, r.y + 22, items[i].label, TS_LARGE, true);
    if (items[i].sub && items[i].sub[0])
      c.textClipped(r.x + 22, r.y + 62, r.w - 44, items[i].sub, TS_SMALL, true);
  }
}

inline int hitRoot(int x, int y, int n, int w) {
  for (int i = 0; i < n; i++)
    if (rootRect(i, w).hit(x, y)) return i;
  return -1;
}

// A list page: the same seven rows and the same pager as a shelf, with a
// second line per row for whatever the row is about.
inline void drawRow(ToolsCanvas& c, int slot, const char* label, const char* sub, bool rule,
                    bool marked = false) {
  const int y = shelf::Y0 + slot * shelf::ROW_H;
  if (marked) c.fillRect(16, y + 8, 6, shelf::ROW_H - 24, true);
  c.textClipped(marked ? 34 : 24, y + 10, c.width() - (marked ? 58 : 48), label, TS_MED, true);
  if (sub && sub[0]) c.text(marked ? 34 : 24, y + 44, sub, TS_SMALL, true);
  if (rule) c.fillRect(16, y + shelf::ROW_H - 6, c.width() - 32, 1, true);
}

inline void drawEmpty(ToolsCanvas& c, const char* what, const char* how) {
  c.textCentered(c.width() / 2, 320, what, TS_LARGE, true);
  c.textCentered(c.width() / 2, 368, how, TS_SMALL, true);
}

}  // namespace rmenu
