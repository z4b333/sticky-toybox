// The shape both readers' lists share: series folders, books, and a page at
// a time of either.
//
// A card that has been read for a year has more books than a panel has rows,
// and a series has more volumes than anyone wants to scroll past. So a list
// is two levels -- the folders under /books, then the books inside one -- and
// each level pages rather than scrolls. Paging suits this panel: a scroll is
// a partial refresh that ghosts, while a page is one clean full refresh, and
// the buttons are large enough to hit without looking.
//
// Only the geometry and the pager live here. Each reader draws its own rows,
// because a .tbk row says how many pages it has and an EPUB row says whether
// the card remembers your place, and a shared row drawer would end up taking
// both as arguments and satisfying neither.
#pragma once
#include "tools_ui.h"

namespace shelf {

inline constexpr int Y0 = 64;         // under the top bar
inline constexpr int ROW_H = 88;
inline constexpr int PER_PAGE = 7;    // 7 x 88 = 616, ending clear of the pager
inline constexpr int PAGER_Y = 700, PAGER_H = 58, PAGER_W = 150;
// Per level. A list entry is ~176 bytes, so 32 books and 12 folders cost
// about 6 KB of the reader's own allocation -- affordable because only one
// app exists at a time, and still four pages deep.
inline constexpr int MAX_ITEMS = 32;
inline constexpr int MAX_FOLDERS = 12;

// The top of the shelf. Books sitting loose in the card's root are listed
// here too, so "/" and "/books" are the same place as far as a reader is
// concerned, and every other directory is a series.
inline constexpr const char* TOP = "/books";
inline bool isTop(const char* dir) { return !dir || !*dir || strcmp(dir, TOP) == 0 || strcmp(dir, "/") == 0; }

// The directory a card path belongs to, in the terms above: the series folder
// it sits in, or TOP for anything loose.
inline void dirOf(const char* path, char* out, int n) {
  const char* slash = path ? strrchr(path, '/') : nullptr;
  const int len = slash ? (int)(slash - path) : 0;
  if (len <= 0 || len >= n) {
    snprintf(out, n, "%s", TOP);
    return;
  }
  memcpy(out, path, (size_t)len);
  out[len] = 0;
  if (isTop(out)) snprintf(out, n, "%s", TOP);
}

inline TRect rowRect(int i) { return TRect{0, Y0 + i * ROW_H, SCREEN_W, ROW_H}; }
inline TRect prevRect() { return TRect{16, PAGER_Y, PAGER_W, PAGER_H}; }
inline TRect nextRect() { return TRect{SCREEN_W - 16 - PAGER_W, PAGER_Y, PAGER_W, PAGER_H}; }

inline int pageCount(int items) { return items <= 0 ? 1 : (items + PER_PAGE - 1) / PER_PAGE; }

// Which row of `items` a tap landed on, or -1. `page` is zero-based.
inline int hitRow(int x, int y, int items, int page) {
  (void)x;
  if (y < Y0) return -1;
  const int i = (y - Y0) / ROW_H;
  if (i < 0 || i >= PER_PAGE) return -1;
  const int idx = page * PER_PAGE + i;
  return idx < items ? idx : -1;
}

// The rule under a row. The last row on a page does without one, because the
// pager draws its own and two lines a few pixels apart read as a mistake.
inline bool rowSep(int rowOnPage, int idx, int items) {
  return rowOnPage < PER_PAGE - 1 && idx + 1 < items;
}

// Drawn only when there is somewhere to go: a list that fits on one page
// should look like a list, not like page one of one.
inline void drawPager(ToolsCanvas& c, int page, int items) {
  const int pages = pageCount(items);
  if (pages <= 1) return;
  c.fillRect(16, PAGER_Y - 12, SCREEN_W - 32, 1, true);
  const TRect p = prevRect(), n = nextRect();
  if (page > 0) c.button(p.x, p.y, p.w, p.h, "< PREV", false, TS_MED);
  if (page < pages - 1) c.button(n.x, n.y, n.w, n.h, "NEXT >", false, TS_MED);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d / %d", page + 1, pages);
  c.textCentered(SCREEN_W / 2, PAGER_Y + (PAGER_H - c.textHeight(TS_MED)) / 2, buf, TS_MED, true);
}

// A book: its whole name, and a word about where it will open. Release
// filenames run long -- "The Apothecary Diaries - Volume 6" is short for the
// genre -- and a clipped name is the wrong half of the information on a
// shelf, where the name is the ONLY thing telling two volumes apart. So a
// long title takes a second line and the row tightens to fit it; a short one
// keeps the roomier single-line layout. Three lines will not fit a row, so
// the second is clipped in the rare case a title outruns both.
inline void drawBookRow(ToolsCanvas& c, int rowOnPage, const char* title, const char* sub,
                        bool sep = true) {
  const int y = Y0 + rowOnPage * ROW_H;
  const int maxW = SCREEN_W - 48;
  if (c.textWidth(title, TS_MED, true) <= maxW) {
    c.text(24, y + 10, title, TS_MED, true, true);
    // Clipped, not drawn straight: the second line carries a chapter's own
    // name now, and publishers write chapter names as long as they like.
    c.textClipped(24, y + 44, maxW, sub, TS_SMALL, true);
  } else {
    // The longest run of whole words that fits, measured forward.
    char buf[48];
    int fit = 0, brk = 0;
    for (int i = 0; title[i] && i < (int)sizeof(buf) - 1; i++) {
      buf[i] = title[i];
      buf[i + 1] = 0;
      if (c.textWidth(buf, TS_MED, true) > maxW) break;
      fit = i + 1;
      if (title[i] == ' ') brk = i;
    }
    const int take = brk > 0 ? brk : (fit > 0 ? fit : 1);
    memcpy(buf, title, (size_t)take);
    buf[take] = 0;
    const char* rest = title + take;
    while (*rest == ' ') rest++;
    c.text(24, y + 4, buf, TS_MED, true, true);
    c.textClipped(24, y + 32, maxW, rest, TS_MED, true, true);
    c.textClipped(24, y + 62, maxW, sub, TS_SMALL, true);
  }
  if (sep) c.fillRect(16, y + ROW_H - 6, SCREEN_W - 32, 1, true);
}

// A series: its name, and how many of this reader's books are inside. Drawn
// with the same rule as the hub's drawers -- a mark, a name, a count.
inline void drawFolderRow(ToolsCanvas& c, int rowOnPage, const char* name, int count,
                          bool sep = true) {
  const int y = Y0 + rowOnPage * ROW_H;
  // A folder mark: a tab and a body, small enough to sit beside the name.
  // The name and the count sit on the same two baselines a book row uses, so
  // the two kinds of row read as one list rather than two.
  c.fillRect(24, y + 22, 20, 5, true);
  c.drawRect(24, y + 26, 44, 30, 2, true);
  // Clear of the chevron on the right, which a long series name would
  // otherwise run into.
  c.textClipped(84, y + 10, SCREEN_W - 84 - 52, name, TS_MED, true, true);
  char sub[32];
  snprintf(sub, sizeof(sub), "%d book%s", count, count == 1 ? "" : "s");
  c.text(84, y + 44, sub, TS_SMALL, true);
  // The chevron that says this row opens rather than acts.
  const int cx = SCREEN_W - 34, cy = y + ROW_H / 2 - 4;
  c.drawLine(cx - 8, cy - 10, cx, cy, 3, true);
  c.drawLine(cx - 8, cy + 10, cx, cy, 3, true);
  if (sep) c.fillRect(16, y + ROW_H - 6, SCREEN_W - 32, 1, true);
}

}  // namespace shelf
