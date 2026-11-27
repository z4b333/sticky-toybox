// The games' shared look (C3, kit "Toybox v2 UI": 2048, XO, Wordle, Sudoku,
// Ships, Game result).
//
// Every game screen is built from the same few parts, so a person who has
// learnt one game has learnt where everything lives in all of them:
//
//   - the bar: Back top-left, the game's name, and More (three dots) top-right,
//     with a short corner note beside the dots ("3/6", "vs Sticky");
//   - stat boxes: a small label over a bold number, 2 px frame;
//   - choices: a thicker outline for the one that is on (owner: selection by
//     outline, not by a black fill -- the black slab is kept for THE action);
//   - stripes: the kit's 45-degree hatch, the 1-bit "half way" mark;
//   - the foot line: one short hint at 24 px under a dotted rule;
//   - More: a page of rows -- How to play, the level, settings, Clear record;
//   - the result screen: a tracked label, a black slab with the verdict, a
//     line of detail, up to three stat boxes, and two buttons at the foot.
//
// Nothing in here knows which game it is drawing for.
#pragma once
#include "chrome.h"
#include "help.h"

namespace gk {

// --- the bar -----------------------------------------------------------------
// More sits where the "?" used to: the same touch zone (HELP_W wide, reaching
// below the bar), so the host's help tap is the More tap.
inline void bar(ToolsHost& h, ToolsCanvas& c, const char* title, const char* corner = nullptr) {
  h.topBar(title, false);
  const int w = c.width();
  chrome2::moreDots(c, w - HELP_W / 2, TOPBAR_H / 2);
  if (corner && corner[0]) {
    const int cw = c.textWidth(corner, TS_SMALL);
    // Right-aligned against the dots, and only if it stays clear of the title.
    if (cw <= BACK_W - 12)
      c.text(w - HELP_W - cw, (TOPBAR_H - c.textHeight(TS_SMALL)) / 2, corner, TS_SMALL, true);
  }
}
inline bool isMore(ToolsHost& h, int x, int y) { return h.isHelpTap(x, y); }

// --- parts -------------------------------------------------------------------
inline constexpr int STAT_H = 66;
// A small label over a bold value, in a 2 px frame (kit "SCORE / BEST TILE").
inline void stat(ToolsCanvas& c, const TRect& r, const char* label, const char* value) {
  c.fillRect(r.x, r.y, r.w, r.h, false);
  c.drawRect(r.x, r.y, r.w, r.h, 2, true);
  c.text(r.x + 10, r.y + 6, label, TS_SMALL, true);
  c.textClipped(r.x + 10, r.y + 6 + c.textHeight(TS_SMALL) + 2, r.w - 16, value, TS_LARGE, true,
                true);
}
inline void statInt(ToolsCanvas& c, const TRect& r, const char* label, int v) {
  char b[16];
  snprintf(b, sizeof(b), "%d", v);
  stat(c, r, label, b);
}
// Same box, centred: the value big, the label under it (kit result screens).
inline void statCentred(ToolsCanvas& c, const TRect& r, const char* value, const char* label) {
  c.fillRect(r.x, r.y, r.w, r.h, false);
  c.drawRect(r.x, r.y, r.w, r.h, 2, true);
  const int vh = c.textHeight(TS_LARGE), lh = c.textHeight(TS_SMALL);
  const int top = r.y + (r.h - vh - 4 - lh) / 2;
  c.textCentered(r.x + r.w / 2, top, value, TS_LARGE, true, true);
  c.textCentered(r.x + r.w / 2, top + vh + 4, label, TS_SMALL, true);
}

// A choice: 3 px outline and a bold label when on, a hairline when off.
inline void choice(ToolsCanvas& c, const TRect& r, const char* label, bool on,
                   TSize sz = TS_MED) {
  c.fillRect(r.x, r.y, r.w, r.h, false);
  c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
  c.textInBox(r.x, r.y, r.w, r.h, label, sz, true, on);
}

// The 45-degree hatch: 2 px of ink on a 5 px pitch (kit's
// repeating-linear-gradient(45deg, #fff 0 3px, #000 3px 5px)).
inline void stripes(ToolsCanvas& c, int x, int y, int w, int h) {
  for (int yy = 0; yy < h; yy++) {
    // Ink where (xx + yy) % 5 is 3 or 4: one 2 px run every 5 px, shifting a
    // pixel a row so the runs line up into diagonals.
    int start = (3 - yy % 5 + 5) % 5;
    if (start >= 4) {  // a run that began one pixel before the left edge
      c.fillRect(x, y + yy, 1, 1, true);
    }
    for (int xx = start; xx < w; xx += 5) c.fillRect(x + xx, y + yy, xx + 2 <= w ? 2 : 1, 1, true);
  }
}

// One short hint at the foot of the screen, under a dotted rule.
// Measured from the canvas's own foot, so a game turned on its side (Hanoi)
// gets its hint at the bottom of the landscape screen too.
inline constexpr int FOOT_Y = SCREEN_H - 46;
inline void foot(ToolsCanvas& c, const char* text) {
  const int fy = c.height() - 46;
  chrome2::dottedRule(c, 0, fy, c.width());
  if (text && text[0])
    c.textCentered(c.width() / 2, fy + (46 - c.textHeight(TS_MED)) / 2 + 1, text, TS_MED, true);
}

// The black slab a verdict sits on: white bold text, padded to its width.
inline void slab(ToolsCanvas& c, int cx, int y, const char* text, TSize sz = TS_HUGE) {
  const int w = c.textWidth(text, sz, true) + 32, h = c.textHeight(sz) + 16;
  c.fillRect(cx - w / 2, y, w, h, true);
  c.textInBox(cx - w / 2, y, w, h, text, sz, false, true);
}

// --- More --------------------------------------------------------------------
// A page of rows under the game's bar. A row is a label and, for a setting,
// its value on the right; tapping a setting moves it on. Clear record is
// armed by the first tap (it says "Tap again to clear") and done by the second.
struct Row {
  const char* label;
  const char* value;  // nullptr for an action
  bool armed;         // drawn outlined and bold: one tap from doing it
};
inline constexpr int MENU_Y = TOPBAR_H + 16, MENU_ROW = 76, MENU_X = 20;
inline constexpr int MENU_MAX = 8;
// `w` is the canvas width: a landscape game (Hanoi) gets rows across its
// 800 px. Five rows still fit under the bar of a 480 px tall screen.
inline TRect menuRect(int i, int w = SCREEN_W) {
  return TRect{MENU_X, MENU_Y + i * MENU_ROW, w - 2 * MENU_X, MENU_ROW - 6};
}
inline void menu(ToolsHost& h, ToolsCanvas& c, const char* title, const Row* rows, int n) {
  h.topBar(title, false);
  for (int i = 0; i < n && i < MENU_MAX; i++) {
    const TRect r = menuRect(i, c.width());
    if (rows[i].armed) {
      chrome2::armed(c, r, "Clear the record?");
      continue;
    }
    c.text(r.x + 12, r.y + (r.h - c.textHeight(TS_MED)) / 2, rows[i].label, TS_MED, true);
    if (rows[i].value) {
      const int vw = c.textWidth(rows[i].value, TS_MED, true);
      c.text(r.x + r.w - 12 - vw, r.y + (r.h - c.textHeight(TS_MED)) / 2, rows[i].value, TS_MED,
             true, true);
    } else {
      c.text(r.x + r.w - 12 - c.textWidth(">", TS_MED), r.y + (r.h - c.textHeight(TS_MED)) / 2, ">",
             TS_MED, true);
    }
    chrome2::dottedRule(c, r.x, r.y + r.h + 2, r.w);
  }
  foot(c, "tap a row");
}
inline int menuHit(int x, int y, int n, int w = SCREEN_W) {
  for (int i = 0; i < n && i < MENU_MAX; i++)
    if (menuRect(i, w).hit(x, y)) return i;
  return -1;
}

// --- How to play ---------------------------------------------------------------
// The rules card, with More named as the way back to it.
// An app that is not a game (Level, Climate) calls it "How to use".
inline void rules(ToolsCanvas& c, const help::Text& t, bool game = true) {
  if (game)
    help::render(c, t, "HOW TO PLAY", "More > How to play brings this back");
  else
    help::render(c, t, "HOW TO USE", "More > How to use brings this back");
}

// --- the result screen -----------------------------------------------------------
inline constexpr TRect RES_LEFT{20, FOOT_Y - 84, 214, 66};
inline constexpr TRect RES_RIGHT{246, FOOT_Y - 84, 214, 66};
inline constexpr int RES_STAT_Y = 380;
struct Stat {
  const char* value;
  const char* label;
};
// label: small tracked caps over the verdict ("GAME OVER", "SOLVED IN 3").
inline void result(ToolsCanvas& c, const char* label, const char* verdict, const char* line,
                   const Stat* stats, int nStats, const char* left, const char* right) {
  const int w = c.width();
  c.textTrackedCentered(w / 2, 200, label, TS_SMALL, true, false, 3);
  slab(c, w / 2, 232, verdict);
  if (line && line[0]) c.textCentered(w / 2, 312, line, TS_MED, true);
  if (nStats > 0) {
    const int gap = 10, bw = (w - 40 - (nStats - 1) * gap) / nStats;
    for (int i = 0; i < nStats; i++)
      statCentred(c, TRect{20 + i * (bw + gap), RES_STAT_Y, bw, 82}, stats[i].value,
                  stats[i].label);
  }
  if (left) c.button(RES_LEFT.x, RES_LEFT.y, RES_LEFT.w, RES_LEFT.h, left, false, TS_LARGE);
  if (right) c.button(RES_RIGHT.x, RES_RIGHT.y, RES_RIGHT.w, RES_RIGHT.h, right, true, TS_LARGE);
}

}  // namespace gk
