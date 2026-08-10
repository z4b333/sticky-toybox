// Hub tile icons for the utility apps, drawn from canvas primitives so the
// standalone and CrossPoint hubs show the same artwork.
#pragma once
#include "tools_draw.h"

namespace ticons {

inline constexpr int COUNT = 9;
inline const char* const NAMES[COUNT] = {"COIN",  "DICE",   "TIMER", "RANDOM",
                                         "PICKER", "FLASHCARDS", "NOTES",  "SHIPS",
                                         "SUDOKU"};
// Kept short: a hub tile is 140 px, which is about twelve characters.
inline const char* const DESCS[COUNT] = {"heads/tails", "D4 - D20",   "countdown", "number/card",
                                         "from a list", "flashcards", "from phone", "battleship",
                                         "9x9 numbers"};

inline void coin(ToolsCanvas& c, int cx, int cy, int s) {
  c.drawCircle(cx, cy, s / 2, 3, true);
  c.drawCircle(cx, cy, s / 2 - 6, 1, true);
  c.textInBox(cx - s / 2, cy - s / 2, s, s, "H", TS_MED, true, true);
}

inline void dice(ToolsCanvas& c, int cx, int cy, int s) {
  // Same rounded face as the dice screen draws, so the icon and the thing it
  // opens are recognisably the same object.
  tdraw::roundRect(c, cx - s / 2, cy - s / 2, s, s, s / 6, 3, true);
  tdraw::dicePips(c, cx - s / 2, cy - s / 2, s, 5, false);
}

inline void timer(ToolsCanvas& c, int cx, int cy, int s) {
  const int r = s / 2;
  c.drawCircle(cx, cy + 2, r - 2, 3, true);
  c.fillRect(cx - 8, cy - r - 6, 16, 6, true);          // crown
  c.drawLine(cx, cy + 2, cx, cy - r / 2 + 2, 3, true);  // minute hand
  c.drawLine(cx, cy + 2, cx + r / 2, cy + 2, 3, true);  // hour hand
}

inline void randomIcon(ToolsCanvas& c, int cx, int cy, int s) {
  // a card with a big numeral on it
  const int w = (s * 3) / 4;
  c.drawRect(cx - w / 2, cy - s / 2, w, s, 3, true);
  char buf[2] = {'7', 0};
  tdraw::seg7Centered(c, cx, cy - s / 4, s / 2, buf, true);
  tdraw::diamond(c, cx, cy + s / 4, s / 4, true);
}

inline void picker(ToolsCanvas& c, int cx, int cy, int s) {
  const int w = s, x = cx - s / 2;
  for (int i = 0; i < 3; i++) {
    const int y = cy - s / 2 + i * (s / 3);
    const bool sel = (i == 1);
    c.drawRect(x, y, w, s / 4, sel ? 3 : 1, true);
    if (sel) c.fillRect(x, y, w, s / 4, true);
  }
}

// A stack of cards, the top one flipped forward.
inline void study(ToolsCanvas& c, int cx, int cy, int s) {
  const int w = (s * 3) / 4, h = (s * 5) / 6;
  c.drawRect(cx - w / 2 + 6, cy - h / 2 - 6, w, h, 2, true);   // card behind
  c.fillRect(cx - w / 2 - 6, cy - h / 2 + 2, w, h, false);     // clear overlap
  c.drawRect(cx - w / 2 - 6, cy - h / 2 + 2, w, h, 3, true);   // front card
  c.fillRect(cx - w / 2 + 2, cy - 6, w - 16, 4, true);         // text lines
  c.fillRect(cx - w / 2 + 2, cy + 4, w - 24, 4, true);
}

// A sheet with a folded corner and a ticked line.
inline void notes(ToolsCanvas& c, int cx, int cy, int s) {
  const int w = (s * 3) / 4, h = s;
  const int x = cx - w / 2, y = cy - h / 2;
  const int fold = s / 4;
  c.drawRect(x, y, w, h, 3, true);
  c.fillRect(x + w - fold, y, fold, fold, false);   // clip the corner
  c.drawLine(x + w - fold, y, x + w, y + fold, 3, true);
  c.fillRect(x + 7, y + h / 2 - 6, w - 20, 3, true);
  c.fillRect(x + 7, y + h / 2 + 4, w - 26, 3, true);
  c.fillRect(x + 7, y + h / 2 + 14, w - 14, 3, true);
}

// A ship on the water: hull, bridge, mast, and two lines of sea beneath.
inline void battleship(ToolsCanvas& c, int cx, int cy, int s) {
  const int deckY = cy + s / 12;
  const int hullH = s / 4;
  c.fillRect(cx - s / 2, deckY - 3, s, 3, true);  // deck
  for (int i = 0; i < hullH; i++) {               // hull tapering to the keel
    const int inset = (i * (s / 3)) / hullH;
    c.fillRect(cx - s / 2 + inset, deckY + i, s - 2 * inset, 1, true);
  }
  c.fillRect(cx - s / 8, deckY - 3 - s / 6, s / 4, s / 6, true);          // bridge
  c.fillRect(cx - 1, deckY - 3 - s / 6 - s / 7, 3, s / 7, true);          // mast
  for (int k = 0; k < 2; k++) {                                          // sea
    const int y = deckY + hullH + 3 + k * 6;
    for (int x = -s / 2; x < s / 2 - 2; x += 10) c.fillRect(cx + x, y, 6, 2, true);
  }
}

// Three digits in a three-by-three frame: the box structure of the real grid,
// with enough numerals to say which puzzle this is.
inline void sudoku(ToolsCanvas& c, int cx, int cy, int s) {
  const int cell = s / 3;
  const int x0 = cx - (3 * cell) / 2, y0 = cy - (3 * cell) / 2;
  for (int i = 0; i <= 3; i++) {
    const int t = (i == 0 || i == 3) ? 3 : 2;
    c.fillRect(x0 + i * cell - t / 2, y0, t, 3 * cell + 1, true);
    c.fillRect(x0, y0 + i * cell - t / 2, 3 * cell + 1, t, true);
  }
  static const char* kD[3] = {"5", "3", "9"};
  static const int kR[3] = {0, 1, 2}, kC[3] = {0, 2, 1};
  for (int i = 0; i < 3; i++) {
    const int w = c.textWidth(kD[i], TS_MED);
    c.text(x0 + kC[i] * cell + (cell - w) / 2, y0 + kR[i] * cell + (cell - c.textHeight(TS_MED)) / 2, kD[i],
           TS_MED, true);
  }
}

inline void draw(ToolsCanvas& c, int idx, int cx, int cy, int s) {
  switch (idx) {
    case 0: coin(c, cx, cy, s); break;
    case 1: dice(c, cx, cy, s); break;
    case 2: timer(c, cx, cy, s); break;
    case 3: randomIcon(c, cx, cy, s); break;
    case 4: picker(c, cx, cy, s); break;
    case 5: study(c, cx, cy, s); break;
    case 6: notes(c, cx, cy, s); break;
    case 7: battleship(c, cx, cy, s); break;
    default: sudoku(c, cx, cy, s); break;
  }
}

}  // namespace ticons

// Matching icon set for the four games, so both hub rows read the same way.
namespace gicons {

inline constexpr int COUNT = 4;
inline const char* const NAMES[COUNT] = {"WORDLE", "NONOGRAM", "2048", "XO"};
inline const char* const DESCS[COUNT] = {"guess the word", "picture logic",
                                         "swipe to merge", "3 in a row"};

// Two guess rows of letter tiles: solved letters filled, the rest still open.
inline void wordle(ToolsCanvas& c, int cx, int cy, int s) {
  const int cw = s / 5;              // near-square tiles read as letters,
  const int ch = (cw * 6) / 5;       // not as a barcode
  const int gap = 3;
  const int x0 = cx - (5 * cw) / 2;
  const int y0 = cy - ch - gap / 2;
  static const uint8_t kFilled[2] = {0b10100, 0b01001};
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < 5; i++) {
      const int x = x0 + i * cw;
      const int y = y0 + r * (ch + gap);
      if (kFilled[r] & (1 << (4 - i)))
        c.fillRect(x, y, cw - 2, ch, true);
      else
        c.drawRect(x, y, cw - 2, ch, 2, true);
    }
}

// A partly-solved 4x4 picture grid.
inline void nonogram(ToolsCanvas& c, int cx, int cy, int s) {
  const int cell = s / 4;
  const int x0 = cx - 2 * cell, y0 = cy - 2 * cell;
  static const uint8_t kPattern[4] = {0b0110, 0b1111, 0b0110, 0b0110};
  for (int r = 0; r < 4; r++)
    for (int col = 0; col < 4; col++)
      if (kPattern[r] & (1 << (3 - col)))
        c.fillRect(x0 + col * cell + 1, y0 + r * cell + 1, cell - 2, cell - 2, true);
  for (int i = 0; i <= 4; i++) {
    c.fillRect(x0 + i * cell, y0, 1, 4 * cell, true);
    c.fillRect(x0, y0 + i * cell, 4 * cell, 1, true);
  }
}

// Four tiles, each a step heavier than the last — the merge ladder.
inline void g2048(ToolsCanvas& c, int cx, int cy, int s) {
  const int cell = s / 2;
  const int x0 = cx - cell, y0 = cy - cell;
  const int weight[4] = {2, 4, 6, 0};  // 0 == solid; thin borders vanish at this size
  for (int i = 0; i < 4; i++) {
    const int x = x0 + (i % 2) * cell;
    const int y = y0 + (i / 2) * cell;
    if (weight[i] == 0)
      c.fillRect(x + 1, y + 1, cell - 2, cell - 2, true);
    else
      c.drawRect(x + 1, y + 1, cell - 2, cell - 2, weight[i], true);
  }
}

// A won board: three solid marks straight down the diagonal, two rings that
// did not get there. Solid-plus-grid so it carries the same weight as the
// nonogram icon next to it.
inline void xo(ToolsCanvas& c, int cx, int cy, int s) {
  const int cell = s / 3;
  const int x0 = cx - (3 * cell) / 2, y0 = cy - (3 * cell) / 2;
  static const uint8_t kSolid[3] = {0, 4, 8};
  static const uint8_t kRing[2] = {2, 6};
  for (int i = 0; i < 3; i++) {
    const int k = kSolid[i];
    c.fillRect(x0 + (k % 3) * cell + 3, y0 + (k / 3) * cell + 3, cell - 6, cell - 6, true);
  }
  for (int i = 0; i < 2; i++) {
    const int k = kRing[i];
    c.drawCircle(x0 + (k % 3) * cell + cell / 2, y0 + (k / 3) * cell + cell / 2,
                 cell / 2 - 3, 2, true);
  }
  for (int i = 1; i < 3; i++) {
    c.fillRect(x0 + i * cell - 1, y0, 2, 3 * cell, true);
    c.fillRect(x0, y0 + i * cell - 1, 3 * cell, 2, true);
  }
}

inline void draw(ToolsCanvas& c, int idx, int cx, int cy, int s) {
  switch (idx) {
    case 0: wordle(c, cx, cy, s); break;
    case 1: nonogram(c, cx, cy, s); break;
    case 2: g2048(c, cx, cy, s); break;
    default: xo(c, cx, cy, s); break;
  }
}

}  // namespace gicons
