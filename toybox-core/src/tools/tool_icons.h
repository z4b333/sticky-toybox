// App icons for the drawers and the rules cards (v2, C3: kit "Play",
// "Utility", "Study").
//
// Line drawings, as the kit draws them: each is the kit's own SVG -- mostly a
// 26-unit box, a few 48 or 64 -- traced here with the canvas's lines,
// circles and rectangles, so the drawer on the panel and the drawer in the kit
// are the same picture. The stroke scales with the icon (2 units in 26), which
// is 3-4 px in a drawer tile and heavier on the rules cards.
#pragma once
#include <math.h>

#include "decor.h"
#include "tools_draw.h"

namespace icons {

// A pen that draws in the kit's units, centred on (cx, cy), `s` px across.
struct Pen {
  ToolsCanvas& c;
  int cx, cy, s;
  float vb;  // the viewBox the kit drew in
  int t;     // stroke, px
  bool ink;
  Pen(ToolsCanvas& c_, int cx_, int cy_, int s_, float vb_, bool ink_ = true)
      : c(c_), cx(cx_), cy(cy_), s(s_), vb(vb_), ink(ink_) {
    t = (int)lroundf((float)s * 2.0f / 26.0f);
    if (t < 2) t = 2;
  }
  int X(float u) const { return cx + (int)lroundf((u - vb / 2) * (float)s / vb); }
  int Y(float v) const { return cy + (int)lroundf((v - vb / 2) * (float)s / vb); }
  int L(float u) const { return (int)lroundf(u * (float)s / vb); }

  void line(float x1, float y1, float x2, float y2) const {
    // The canvas widens a line across its shallower axis, so a diagonal comes
    // out thinner than a straight one (by 1/sqrt 2 at 45 degrees). Scale the
    // width back up so every stroke of an icon has the same weight.
    const float ax = fabsf(x2 - x1), ay = fabsf(y2 - y1), mx = ax > ay ? ax : ay;
    int w = t;
    if (mx > 0) w = (int)lroundf((float)t * sqrtf(ax * ax + ay * ay) / mx);
    c.drawLine(X(x1), Y(y1), X(x2), Y(y2), w, ink);
    // Round the joint: lines meeting at a corner leave a notch otherwise.
    if (t >= 3) {
      c.fillCircle(X(x1), Y(y1), t / 2, ink);
      c.fillCircle(X(x2), Y(y2), t / 2, ink);
    }
  }
  void poly(const float* p, int n, bool closed) const {
    for (int i = 0; i + 1 < n; i++) line(p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3]);
    if (closed) line(p[2 * n - 2], p[2 * n - 1], p[0], p[1]);
  }
  void rect(float x, float y, float w, float h) const {
    const int x0 = X(x), y0 = Y(y), x1 = X(x + w), y1 = Y(y + h);
    c.drawRect(x0 - t / 2, y0 - t / 2, x1 - x0 + t, y1 - y0 + t, t, ink);
  }
  void fill(float x, float y, float w, float h) const {
    const int x0 = X(x), y0 = Y(y), x1 = X(x + w), y1 = Y(y + h);
    c.fillRect(x0 - t / 2, y0 - t / 2, x1 - x0 + t, y1 - y0 + t, ink);
  }
  void rrect(float x, float y, float w, float h, float r) const {
    const int x0 = X(x), y0 = Y(y), x1 = X(x + w), y1 = Y(y + h);
    tdraw::roundRect(c, x0 - t / 2, y0 - t / 2, x1 - x0 + t, y1 - y0 + t, L(r) + t / 2, t, ink);
  }
  void circle(float x, float y, float r) const { c.drawCircle(X(x), Y(y), L(r) + t / 2, t, ink); }
  void dot(float x, float y, float r) const { c.fillCircle(X(x), Y(y), L(r), ink); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2) const {
    decor::triangle(c, X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), ink);
  }
  // An arc of a circle, degrees, 0 = east, 90 = south (screen y down).
  void arc(float x, float y, float r, float a0, float a1) const {
    const int n = 16;
    float px = x + r * cosf(a0 * 0.0174533f), py = y + r * sinf(a0 * 0.0174533f);
    for (int i = 1; i <= n; i++) {
      const float a = (a0 + (a1 - a0) * i / n) * 0.0174533f;
      const float qx = x + r * cosf(a), qy = y + r * sinf(a);
      line(px, py, qx, qy);
      px = qx;
      py = qy;
    }
  }
  void cubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) const {
    const int n = 16;
    float px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
      const float u = (float)i / n, v = 1 - u;
      const float qx = v * v * v * x0 + 3 * v * v * u * x1 + 3 * v * u * u * x2 + u * u * u * x3;
      const float qy = v * v * v * y0 + 3 * v * v * u * y1 + 3 * v * u * u * y2 + u * u * u * y3;
      line(px, py, qx, qy);
      px = qx;
      py = qy;
    }
  }
};

}  // namespace icons

namespace ticons {

inline constexpr int COUNT = 22;
inline const char* const NAMES[COUNT] = {"COIN",  "DICE",   "TIMER", "RANDOM",
                                         "PICKER", "FLASHCARDS", "NOTES",  "SHIPS",
                                         "SUDOKU", "MANGA", "BOOKS", "RECIPES",
                                         "CALENDAR", "FOCUS",
                                         // C4: the new games, PLAY's second page
                                         "FOUR IN A ROW", "BLACKJACK", "HIGHER LOWER",
                                         "CODEBREAKER", "HANOI", "MEMORY",
                                         // C5: UTILITY
                                         "LEVEL", "CLIMATE"};
// Kept short: a hub tile is 140 px, which is about twelve characters.
inline const char* const DESCS[COUNT] = {"heads/tails", "D4 - D20",   "countdown", "number/card",
                                         "from a list", "flashcards", "from phone", "battleship",
                                         "9x9 numbers", "removed", "ebooks", "what to cook",
                                         "the month", "no countdown", "drop four",
                                         "beat the dealer", "next card", "crack the code",
                                         "move the tower", "find pairs", "spirit level",
                                         "room and outside"};

using icons::Pen;

inline void coin(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.circle(13, 13, 9);
  p.circle(13, 13, 5);
}

inline void dice(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(5, 5, 16, 16, 3);
  p.dot(10, 10, 1.6f);
  p.dot(13, 13, 1.6f);
  p.dot(16, 16, 1.6f);
}

inline void timer(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.circle(13, 14, 9);
  p.line(13, 9, 13, 14);
  p.line(13, 14, 16, 16);
  p.line(10, 3, 16, 3);
}

inline void randomIcon(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  static const float a[] = {4, 7, 8, 7, 18, 19, 22, 19};
  static const float b[] = {4, 19, 8, 19, 18, 7, 22, 7};
  static const float h1[] = {19, 4, 22, 7, 19, 10};
  static const float h2[] = {19, 16, 22, 19, 19, 22};
  p.poly(a, 4, false);
  p.poly(b, 4, false);
  p.poly(h1, 3, false);
  p.poly(h2, 3, false);
}

// A list with a pointer at the line that was picked (owner's choice, 25 Nov).
inline void picker(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.tri(2, 9.5f, 2, 16.5f, 7, 13);
  p.line(10, 6, 24, 6);
  p.line(10, 13, 24, 13);
  p.line(10, 20, 24, 20);
}

// Two cards, the back one's top and right edges showing behind the front
// (kit "Flashcards"), redrawn in the 26 box with room between the two so the
// edges never touch at drawer size.
inline void flashcards(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(2.5f, 9, 16, 13, 2);
  p.line(7, 4.5f, 21, 4.5f);
  p.arc(21, 6.5f, 2, -90, 0);
  p.line(23, 6.5f, 23, 17.5f);
}

inline void notes(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 48);
  p.rrect(11, 7, 26, 34, 3);
  p.line(17, 17, 31, 17);
  p.line(17, 24, 31, 24);
  p.line(17, 31, 25, 31);
}

inline void ships(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  static const float hull[] = {3, 16, 23, 16, 20, 21, 6, 21};
  static const float cabin[] = {9, 16, 9, 9, 16, 9, 16, 16};
  p.poly(hull, 4, true);
  p.poly(cabin, 4, false);
  p.line(12, 9, 12, 4);
}

inline void sudoku(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rect(4, 4, 18, 18);
  p.line(10, 4, 10, 22);
  p.line(16, 4, 16, 22);
  p.line(4, 10, 22, 10);
  p.line(4, 16, 22, 16);
}

// An open book seen from the front: two pages whose top and bottom edges
// curve down to the spine (owner's choice, 25 Nov).
inline void books(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.cubic(13, 7, 10, 4.5f, 5.5f, 4.5f, 2, 5.5f);
  p.line(2, 5.5f, 2, 20);
  p.cubic(2, 20, 5.5f, 19, 10, 19, 13, 21.5f);
  p.cubic(13, 7, 16, 4.5f, 20.5f, 4.5f, 24, 5.5f);
  p.line(24, 5.5f, 24, 20);
  p.cubic(24, 20, 20.5f, 19, 16, 19, 13, 21.5f);
  p.line(13, 7, 13, 21.5f);
}

// A bowl with steam (kit "Recipes").
inline void recipes(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.line(5, 12, 21, 12);
  p.line(21, 12, 21, 15);
  p.arc(15, 15, 6, 0, 90);
  p.line(15, 21, 11, 21);
  p.arc(11, 15, 6, 90, 180);
  p.line(5, 15, 5, 12);
  p.line(9, 4, 9, 8);
  p.line(13, 3, 13, 8);
  p.line(17, 4, 17, 8);
}

inline void calendar(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(4, 5, 18, 17, 2);
  p.line(4, 10, 22, 10);
  p.line(9, 3, 9, 7);
  p.line(17, 3, 17, 7);
}

// --- C4 games ---------------------------------------------------------------
// Four in a Row: the upright frame, open at the top, two rows of discs in
// it and one on its way down.
inline void fourRow(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.line(3, 8, 3, 23);
  p.line(23, 8, 23, 23);
  p.line(3, 23, 23, 23);
  p.dot(8, 18.5f, 2.8f);
  p.circle(13, 18.5f, 1.8f);
  p.dot(18, 18.5f, 2.8f);
  p.circle(8, 12.5f, 1.8f);
  p.dot(13, 12.5f, 2.8f);
  p.dot(18, 4, 2.8f);
}

// Blackjack: two cards fanned, the front one with an ace's diamond.
inline void blackjack(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(3, 6, 11, 16, 1.5f);
  c.fillRect(p.X(11) - p.t / 2, p.Y(4) - p.t / 2, p.X(23) - p.X(11) + p.t, p.Y(20) - p.Y(4) + p.t,
             false);
  p.rrect(11, 4, 12, 16, 1.5f);
  const float q[] = {17, 8, 20, 12, 17, 16, 14, 12};
  p.poly(q, 4, true);
}

// Higher or Lower: a card and the two ways the next one can go.
inline void hiLo(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(3, 4, 12, 18, 1.5f);
  const float up[] = {17.5f, 10, 20.5f, 6.5f, 23.5f, 10};
  const float dn[] = {17.5f, 16, 20.5f, 19.5f, 23.5f, 16};
  p.poly(up, 3, false);
  p.poly(dn, 3, false);
}

// Codebreaker: a row of three tokens and the pegs that answer it.
inline void codebreaker(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.dot(5.5f, 9, 3);
  p.circle(13, 9, 2.2f);
  p.fill(18, 6, 5, 6);
  p.line(2, 15.5f, 24, 15.5f);
  p.dot(8, 20.5f, 1.7f);
  p.dot(13, 20.5f, 1.7f);
  p.circle(18, 20.5f, 1);
}

// Tower of Hanoi: a pole on a floor and three solid discs, largest at the
// bottom.
inline void hanoi(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.line(2, 23, 24, 23);
  p.line(13, 3, 13, 23);
  p.fill(5, 18, 16, 2);
  p.fill(7.5f, 13, 11, 2);
  p.fill(10, 8, 6, 2);
}

// Memory: four cards, two turned up showing the same mark.
inline void memory(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(3, 3, 8.5f, 8.5f, 1.5f);
  p.rrect(14.5f, 3, 8.5f, 8.5f, 1.5f);
  p.rrect(3, 14.5f, 8.5f, 8.5f, 1.5f);
  p.rrect(14.5f, 14.5f, 8.5f, 8.5f, 1.5f);
  p.dot(7.25f, 7.25f, 2);
  p.dot(18.75f, 18.75f, 2);
}

// --- C5 -----------------------------------------------------------------------
// Level: a spirit level's vial, the bubble between its two marks.
inline void level(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rrect(2, 8.5f, 22, 9, 4.5f);
  p.line(9.5f, 8.5f, 9.5f, 17.5f);
  p.line(16.5f, 8.5f, 16.5f, 17.5f);
  p.dot(13, 13, 2.2f);
}

// Climate: a thermometer and a drop.
inline void climate(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.line(8, 4, 8, 15);
  p.line(12, 4, 12, 15);
  p.arc(10, 4, 2, 180, 360);
  p.circle(10, 19, 3.5f);
  p.dot(10, 19, 1.6f);
  const float drop[] = {19, 8, 22.5f, 14};
  p.line(drop[0], drop[1], drop[2], drop[3]);
  p.line(19, 8, 15.5f, 14);
  p.arc(19, 15, 3.5f, 0, 180);
}

// A sprout (kit "Focus"), drawn in a 64-unit box.
inline void focus(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 64);
  p.t = (int)lroundf((float)s * 2.4f / 64.0f * 1.6f);
  if (p.t < 2) p.t = 2;
  p.line(32, 56, 32, 30);
  p.cubic(32, 34, 22, 34, 16, 26, 16, 16);
  p.cubic(16, 16, 26, 16, 32, 24, 32, 34);
  p.cubic(32, 30, 32, 20, 39, 13, 49, 13);
  p.cubic(49, 13, 49, 23, 42, 30, 32, 30);
}

inline void draw(ToolsCanvas& c, int idx, int cx, int cy, int s) {
  switch (idx) {
    case 0: coin(c, cx, cy, s); break;
    case 1: dice(c, cx, cy, s); break;
    case 2: timer(c, cx, cy, s); break;
    case 3: randomIcon(c, cx, cy, s); break;
    case 4: picker(c, cx, cy, s); break;
    case 5: flashcards(c, cx, cy, s); break;
    case 6: notes(c, cx, cy, s); break;
    case 7: ships(c, cx, cy, s); break;
    case 8: sudoku(c, cx, cy, s); break;
    case 12: calendar(c, cx, cy, s); break;
    case 13: focus(c, cx, cy, s); break;
    case 10: books(c, cx, cy, s); break;
    case 11: recipes(c, cx, cy, s); break;
    case 14: fourRow(c, cx, cy, s); break;
    case 15: blackjack(c, cx, cy, s); break;
    case 16: hiLo(c, cx, cy, s); break;
    case 17: codebreaker(c, cx, cy, s); break;
    case 18: hanoi(c, cx, cy, s); break;
    case 19: memory(c, cx, cy, s); break;
    case 20: level(c, cx, cy, s); break;
    case 21: climate(c, cx, cy, s); break;
    default: break;  // 9, the removed manga reader: no icon
  }
}

}  // namespace ticons

// The games, in the same hand.
namespace gicons {

inline constexpr int COUNT = 4;
inline const char* const NAMES[COUNT] = {"WORDLE", "MINES", "2048", "XO"};
inline const char* const DESCS[COUNT] = {"guess the word", "dig, flag, clear",
                                         "swipe to merge", "3 in a row"};

using icons::Pen;

// Three letter tiles, the middle one solved (kit "Wordle"), set a little
// further apart than the kit's so they stay three tiles at drawer size.
inline void wordle(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rect(1.5f, 9, 6, 8);
  p.fill(10, 9, 6, 8);
  p.rect(18.5f, 9, 6, 8);
}

// A framed square with a mine in it: a ball and four spikes. No kit artboard
// for Mines, so it is drawn in the same hand as the Sudoku and 2048 icons.
inline void mines(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rect(4, 4, 18, 18);
  p.dot(13, 13, 3.6f);
  p.line(13, 7.5f, 13, 18.5f);
  p.line(7.5f, 13, 18.5f, 13);
  p.line(9.3f, 9.3f, 16.7f, 16.7f);
  p.line(16.7f, 9.3f, 9.3f, 16.7f);
}

// Four tiles, two solid (kit "2048").
inline void g2048(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.rect(4, 4, 8, 8);
  p.fill(14, 4, 8, 8);
  p.fill(4, 14, 8, 8);
  p.rect(14, 14, 8, 8);
}

// A cross beside a ring. The kit's version put them in the corners of a
// grid, but at drawer size the grid's lines ran into the cross; the two marks
// alone read as XO at a glance.
inline void xo(ToolsCanvas& c, int cx, int cy, int s) {
  Pen p(c, cx, cy, s, 26);
  p.line(2.5f, 8, 11, 16.5f);
  p.line(11, 8, 2.5f, 16.5f);
  p.circle(18.5f, 12.25f, 4.5f);
}

inline void draw(ToolsCanvas& c, int idx, int cx, int cy, int s) {
  switch (idx) {
    case 0: wordle(c, cx, cy, s); break;
    case 1: mines(c, cx, cy, s); break;
    case 2: g2048(c, cx, cy, s); break;
    default: xo(c, cx, cy, s); break;
  }
}

}  // namespace gicons
