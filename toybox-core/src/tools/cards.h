// Playing cards on the panel, for Blackjack and Higher or Lower (C4).
//
// There is no red on this glass, so hearts and diamonds are drawn hollow and
// spades and clubs solid -- the old two-colour deck's split, kept by fill
// instead of by colour. The deck itself is deck.h.
#pragma once
#include "deck.h"
#include "game_kit.h"
#include "tools_draw.h"

namespace cards {

// --- drawing -------------------------------------------------------------------
inline void suit(ToolsCanvas& c, int card, int cx, int cy, int size) {
  const int s = suitOf(card);
  tdraw::suit(c, s, cx, cy, size, true);
  if (hollow(card)) {
    // Knock the middle out of the same shape, a stroke's width in.
    const int t = size / 8 < 3 ? 3 : size / 8;
    tdraw::suit(c, s, cx, cy + (s == 1 ? -t / 3 : 0), size - 2 * t - (s == 1 ? t / 2 : 0), false);
  }
}

// Face up: a rounded 2 px frame, the rank and a small suit in the top-left
// corner, the same upside down in the bottom-right, a big suit in the middle.
inline void face(ToolsCanvas& c, int x, int y, int w, int h, int card, TSize rankSz = TS_LARGE) {
  tdraw::fillRound(c, x, y, w, h, 10, false);
  tdraw::roundRect(c, x, y, w, h, 10, 2, true);
  const char* r = tdraw::rankName(rankOf(card));
  const int rh = c.textHeight(rankSz);
  const int small = rh * 2 / 3;
  c.text(x + 10, y + 6, r, rankSz, true, true);
  suit(c, card, x + 10 + small / 2 + 2, y + 10 + rh + small / 2, small);
  const int big = (w < h ? w : h) * 2 / 5;
  suit(c, card, x + w / 2, y + h / 2 + rh / 4, big);
  if (h >= 200) {  // room for the corner again, at the foot
    const int rw = c.textWidth(r, rankSz, true);
    c.text(x + w - 10 - rw, y + h - 10 - rh, r, rankSz, true, true);
  }
}

// Face down: the frame and the kit's hatch inside it.
inline void back(ToolsCanvas& c, int x, int y, int w, int h) {
  tdraw::fillRound(c, x, y, w, h, 10, false);
  tdraw::roundRect(c, x, y, w, h, 10, 2, true);
  gk::stripes(c, x + 8, y + 8, w - 16, h - 16);
}

}  // namespace cards
