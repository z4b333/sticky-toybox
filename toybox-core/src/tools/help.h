// The "how to play" card every game shows the first time you open it.
//
// One screen, one layout, one pair of buttons, so the rules for a new game read
// the same as the rules for the last one. GOT IT dismisses it for now; DON'T
// SHOW AGAIN writes a flag to NVS and it never appears on its own again.
//
// That flag is only safe because the top bar carries a "?" -- dismissing the
// card permanently hides it, it does not destroy it. Without a way back, "don't
// show again" would be a door that locks behind you, and nobody would dare
// press it.
#pragma once
#include <string.h>
#include "../chrome.h"
#include "decor.h"
#include "tool_icons.h"
#include "tools_ui.h"

namespace help {

constexpr int MAX_LINES = 12;

struct Text {
  const char* lines[MAX_LINES];
  bool game;    // which icon table the emblem comes from
  int8_t icon;  // index into it, -1 for a card with no emblem
};

// v2 (kit "SysHelp"): the dark button at the foot starts; above it a row with
// a switch, "Don't show this again", which the button then honours.
inline constexpr TRect OK_BTN{20, 714, 440, 60};
inline constexpr TRect NEVER_BTN{0, 628, 480, 76};  // the switch row
inline bool g_never = false;                        // the switch, until the card closes
enum class Tap : uint8_t { None, Dismiss, Never, Toggle };  // Toggle: repaint

// NVS keys are short; "h_" plus the game's own name keeps them unique and
// readable in a dump.
inline void keyFor(const char* game, char* out, size_t n) {
  snprintf(out, n, "h_%s", game);
}

inline bool suppressed(Preferences& p, const char* game) {
  char k[16];
  keyFor(game, k, sizeof(k));
  return p.getBool(k, false);
}

inline void suppress(Preferences& p, const char* game) {
  char k[16];
  keyFor(game, k, sizeof(k));
  p.putBool(k, true);
}

// The heading is a parameter because not every card is about a game: the
// flashcards card explains a tool, and "HOW TO PLAY" over a study screen reads
// like a mistake. The caller's bar carries the app's name; the heading sits
// under it, bold, at the left (kit "SysHelp").
inline void footer(ToolsCanvas& c, const TRect& ok, const TRect& sw, const char* label,
                   const char* way_back) {
  const int W = c.width();
  const int sy = sw.y + (sw.h - 34) / 2;
  c.text(sw.x + 20, sw.y + 12, "Don't show this again", TS_MED, true);
  c.text(sw.x + 20, sw.y + 12 + c.textHeight(TS_MED) + 2, way_back, TS_SMALL, true);
  const int sx = sw.x + sw.w - 20 - 68;
  if (g_never) c.fillRect(sx, sy, 68, 34, true);
  else c.drawRect(sx, sy, 68, 34, 2, true);
  c.fillRect(g_never ? sx + 68 - 32 : sx + 4, sy + 4, 28, 26, !g_never);
  c.fillRect(sw.x + 20, sw.y + sw.h - 1, (sw.w < W ? sw.w : W) - 40, 1, true);
  c.button(ok.x, ok.y, ok.w, ok.h, label, true, TS_LARGE);
}
inline const char* okLabel(const char* heading) {
  return strcmp(heading, "HOW TO PLAY") == 0 ? "Play" : "Got it";
}

inline void render(ToolsCanvas& c, const Text& t, const char* heading = "HOW TO PLAY",
                   const char* way_back = "the ? above brings this back") {
  // "HOW TO PLAY" in caps belongs to a bar; here it reads as a sentence.
  char head[32];
  size_t n = 0;
  for (const char* p = heading; *p && n < sizeof(head) - 1; p++, n++)
    head[n] = (n == 0 || *p < 'A' || *p > 'Z') ? *p : (char)(*p + 32);
  head[n] = 0;
  constexpr int TOP = TOPBAR_H + 20, ICON = 64, LINE = 34;
  c.text(20, TOP, head, TS_LARGE, true, true);
  int lines = 0;
  for (int i = 0; i < MAX_LINES && t.lines[i]; i++) lines++;
  int y = TOP + c.textHeight(TS_LARGE) + 18;
  if (t.icon >= 0) {
    if (t.game)
      gicons::draw(c, t.icon, 20 + ICON / 2, y + ICON / 2, ICON);
    else
      ticons::draw(c, t.icon, 20 + ICON / 2, y + ICON / 2, ICON);
    y += ICON + 16;
  }
  for (int i = 0; i < lines; i++) {
    if (t.lines[i][0]) c.text(20, y, t.lines[i], TS_LARGE, true);
    y += LINE;  // blank entries are deliberate paragraph breaks
  }
  footer(c, OK_BTN, NEVER_BTN, okLabel(heading), way_back);
}

inline Tap hit(int x, int y) {
  if (NEVER_BTN.hit(x, y)) {
    g_never = !g_never;
    return Tap::Toggle;
  }
  if (OK_BTN.hit(x, y)) {
    const bool never = g_never;
    g_never = false;
    return never ? Tap::Never : Tap::Dismiss;
  }
  return Tap::None;
}

// The same card on a landscape screen (C4: Hanoi is played on its side): the
// emblem on the left, the rules beside it, the two buttons side by side along
// the foot. A card of up to nine lines fits under the heading.
inline constexpr TRect OK_WIDE{420, 384, 360, 60};
inline constexpr TRect NEVER_WIDE{0, 372, 410, 84};
inline void renderWide(ToolsCanvas& c, const Text& t, const char* heading, const char* way_back) {
  char head[32];
  size_t n = 0;
  for (const char* p = heading; *p && n < sizeof(head) - 1; p++, n++)
    head[n] = (n == 0 || *p < 'A' || *p > 'Z') ? *p : (char)(*p + 32);
  head[n] = 0;
  constexpr int TOP = TOPBAR_H + 12, LINE = 30, ICON = 100;
  c.text(20, TOP, head, TS_LARGE, true, true);
  int lines = 0;
  for (int i = 0; i < MAX_LINES && t.lines[i]; i++) lines++;
  const int y0 = TOP + c.textHeight(TS_LARGE) + 14;
  if (t.icon >= 0) {
    if (t.game)
      gicons::draw(c, t.icon, 20 + ICON / 2, y0 + ICON / 2, ICON);
    else
      ticons::draw(c, t.icon, 20 + ICON / 2, y0 + ICON / 2, ICON);
  }
  int y = y0;
  for (int i = 0; i < lines; i++) {
    if (t.lines[i][0]) c.text(t.icon >= 0 ? 150 : 20, y, t.lines[i], TS_MED, true);
    y += LINE;
  }
  footer(c, OK_WIDE, NEVER_WIDE, okLabel(heading), way_back);
}
inline Tap hitWide(int x, int y) {
  if (NEVER_WIDE.hit(x, y)) {
    g_never = !g_never;
    return Tap::Toggle;
  }
  if (OK_WIDE.hit(x, y)) {
    const bool never = g_never;
    g_never = false;
    return never ? Tap::Never : Tap::Dismiss;
  }
  return Tap::None;
}

// --- the cards ---------------------------------------------------------------
// Kept to about 26 characters a line, which is what fits at this text size
// without the eye having to track back across the whole panel.

inline constexpr Text WORDLE{{
    "Guess the hidden 5-letter",
    "word in six tries. One word",
    "a day; New word for more.",
    "",
    "After each guess:",
    "black - right letter, right",
    "place",
    "striped - in the word, but",
    "somewhere else",
    "plain - not in the word",
},
                            true,
                            0};

inline constexpr Text MINES{{
    "Clear every square that",
    "has no mine under it.",
    "",
    "Tap to dig. A number says",
    "how many mines touch it.",
    "Hold to plant a flag.",
    "",
    "Your first tap is always",
    "safe. Tap a number whose",
    "flags are all placed to dig",
    "round it in one go.",
},
                            true,
                            1};

inline constexpr Text G2048{{
    "Swipe the board and every",
    "tile slides that way.",
    "",
    "Two equal tiles that meet",
    "merge into one, doubled.",
    "A new tile appears after",
    "every move.",
    "",
    "Reach 2048. Keep going",
    "until the board jams.",
},
                            true,
                            2};

inline constexpr Text XO{{
    "Three in a row wins.",
    "",
    "Play the Sticky, or hand",
    "it to a friend each turn.",
    "",
    "More > Rule: with 3 marks",
    "you only ever have three",
    "on the board. Place a",
    "fourth and your oldest",
    "lifts off - it is drawn",
    "faint for a turn first.",
},
                            true,
                            3};

inline constexpr Text SHIPS{{
    "Both fleets are hidden.",
    "Take turns firing.",
    "",
    "Place your five ships, or",
    "Shuffle them. Then tap a",
    "square to aim and tap it",
    "again to fire.",
    "",
    "Dot is a miss, black is a",
    "hit. A hit fires again",
    "(More can turn that off).",
},
                            false,
                            7};

inline constexpr Text FLASHCARDS{{
    "Cards come from your phone,",
    "or from the SD card.",
    "",
    "IMPORT: scan the QR, then",
    "paste them on your phone.",
    "CARD: a file in /decks, one",
    "card a line, front then back.",
    "",
    "SPACED REPEAT brings hard",
    "cards back sooner. JUST FLIP",
    "shuffles and keeps nothing.",
    "Side buttons grade cards too.",
},
                                 false,
                                 5};

// The readers' card earns its place differently from the games'. A game with
// no rules card is unplayable; a reader with no card is merely silent about
// everything it can do -- the page view has no chrome at all, so the corner
// that leaves, the middle that answers "where am I", and above all the power
// button that opens the options are all invisible until told.
inline constexpr Text BOOKS{{
    "A page fills the glass.",
    "",
    "Tap the left or right edge",
    "to turn. Manga volumes",
    "turn the other way round.",
    "",
    "Tap the middle to see the",
    "page number, the top-left",
    "corner to leave.",
    "",
    "The power button opens",
    "options and bookmarks.",
},
                            false,
                            9};

inline constexpr Text EPUB{{
    "Tap the left or right edge",
    "to turn the page, the",
    "top-left corner to leave.",
    "",
    "The power button opens",
    "options: contents, book-",
    "marks, and the text size.",
    "",
    "Your place is kept on the",
    "card itself, so it follows",
    "the card between devices.",
},
                           false,
                           10};

inline constexpr Text SUDOKU{{
    "Fill every empty square so",
    "that each row, column and",
    "3x3 box holds 1 to 9 once.",
    "",
    "Tap a square, then a digit.",
    "Bold digits came with the",
    "puzzle; yours are slanted.",
    "",
    "Notes writes small pencil",
    "digits. Check strikes",
    "through any that are wrong.",
},
                            false,
                            8};

// --- C4 ------------------------------------------------------------------------

inline constexpr Text FOURROW{{
    "Drop discs into the seven",
    "columns. Four of yours in",
    "a row wins: across, up or",
    "on a slant.",
    "",
    "Tap a column to drop. The",
    "Sticky answers at once.",
    "",
    "Undo takes back your move",
    "and the Sticky's reply.",
},
                             false,
                             14};

inline constexpr Text BLACKJACK{{
    "Get nearer 21 than the",
    "dealer without going over.",
    "Picture cards count 10,",
    "aces 1 or 11.",
    "",
    "Hit takes a card, Stand",
    "keeps your total. Double",
    "doubles the bet for one",
    "more card.",
    "",
    "The dealer draws to 17.",
    "A blackjack pays 3 to 2.",
},
                               false,
                               15};

inline constexpr Text HILO{{
    "One card is showing. Will",
    "the next be higher or",
    "lower?",
    "",
    "Aces are high. The same",
    "rank is a push: the run",
    "goes on.",
    "",
    "One wrong guess ends the",
    "run. How long a streak",
    "can you make?",
},
                          false,
                          16};

inline constexpr Text CODE{{
    "The Sticky hides a row of",
    "symbols. Find it in ten",
    "tries.",
    "",
    "Fill a row, then Check.",
    "A solid peg: right symbol,",
    "right place. A ring: right",
    "symbol, wrong place.",
    "",
    "Easy never repeats a",
    "symbol; Hard hides five.",
},
                          false,
                          17};

// Shown on its side: nine lines at most, and wider ones.
inline constexpr Text HANOI{{
    "Move the whole tower to the",
    "goal peg on the right.",
    "",
    "Tap a peg to lift its top disc,",
    "then tap another to put it down.",
    "A disc never goes on a smaller one.",
    "",
    "Four discs take 15 moves at best;",
    "every disc more doubles it.",
},
                           false,
                           18};

inline constexpr Text MEMORY{{
    "Every symbol is on two",
    "cards. Turn two at a time.",
    "",
    "A pair stays face up. Two",
    "that differ go back down",
    "on your next tap.",
    "",
    "Find every pair in as few",
    "flips as you can.",
},
                            false,
                            19};

// --- C5 ------------------------------------------------------------------------

inline constexpr Text LEVEL{{
    "Lay the Sticky flat: the",
    "bubble shows both ways it",
    "leans.",
    "",
    "Stand it on an edge: one",
    "angle, and the screen",
    "turns to read upright.",
    "",
    "Set zero makes the surface",
    "it is on the level one.",
    "OK on the side does it too.",
},
                           false,
                           20};

inline constexpr Text CLIMATE{{
    "The room: temperature,",
    "humidity, dew point, how",
    "warm it feels.",
    "",
    "The Sticky runs a little",
    "warm: More > Offset trims",
    "it.",
    "",
    "For the weather outside:",
    "More > Outdoor place sets",
    "it from your phone, then",
    "tap Refresh.",
},
                             false,
                             21};

}  // namespace help
