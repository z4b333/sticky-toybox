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

inline constexpr TRect OK_BTN{40, 620, 400, 66};
inline constexpr TRect NEVER_BTN{40, 702, 400, 56};

enum class Tap : uint8_t { None, Dismiss, Never };

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
// like a mistake.
inline void render(ToolsCanvas& c, const Text& t, const char* heading = "HOW TO PLAY",
                   const char* way_back = "the ? above brings this back") {
  c.textCentered(c.width() / 2, 46, heading, TS_LARGE, true, true);
  decor::ornament(c, c.width() / 2, 80, 300, true);

  // The buttons are pinned to the bottom where the thumb is, so the emblem and
  // the rules are centred in what is left rather than pushed against the top.
  // A six-line card and an eleven-line one then sit the same way on the panel
  // instead of one of them leaving a hole above GOT IT.
  constexpr int TOP = 96, ICON = 76, ICON_GAP = 20, LINE = 34;
  int lines = 0;
  for (int i = 0; i < MAX_LINES && t.lines[i]; i++) lines++;

  const int blockH = (t.icon >= 0 ? ICON + ICON_GAP : 0) + lines * LINE;
  int y = TOP + (OK_BTN.y - 16 - TOP - blockH) / 2;
  if (y < TOP) y = TOP;

  // The game's own hub icon, big, above its rules -- so the card that opens
  // every game still looks like that particular game.
  if (t.icon >= 0) {
    if (t.game)
      gicons::draw(c, t.icon, c.width() / 2, y + ICON / 2, ICON);
    else
      ticons::draw(c, t.icon, c.width() / 2, y + ICON / 2, ICON);
    y += ICON + ICON_GAP;
  }

  // The rules read at the next size up now that the face is proportional: the
  // same 26-character lines that filled the panel in the old pixel font take
  // barely half of it, and rules are the one screen you actually read.
  for (int i = 0; i < lines; i++) {
    if (t.lines[i][0]) c.text(34, y, t.lines[i], TS_LARGE, true);
    y += LINE;  // blank entries are deliberate paragraph breaks
  }

  c.button(OK_BTN.x, OK_BTN.y, OK_BTN.w, OK_BTN.h, "GOT IT", true, TS_LARGE);
  c.button(NEVER_BTN.x, NEVER_BTN.y, NEVER_BTN.w, NEVER_BTN.h, "DON'T SHOW AGAIN", false,
           TS_MED);
  c.textCentered(c.width() / 2, NEVER_BTN.y + NEVER_BTN.h + 8, way_back, TS_SMALL, true);
}

inline Tap hit(int x, int y) {
  if (OK_BTN.hit(x, y)) return Tap::Dismiss;
  if (NEVER_BTN.hit(x, y)) return Tap::Never;
  return Tap::None;
}

// The same card on a landscape screen (C4: Hanoi is played on its side): the
// emblem on the left, the rules beside it, the two buttons side by side along
// the foot. A card of up to nine lines fits under the heading.
inline constexpr TRect OK_WIDE{40, 392, 344, 60};
inline constexpr TRect NEVER_WIDE{416, 392, 344, 60};
inline void renderWide(ToolsCanvas& c, const Text& t, const char* heading, const char* way_back) {
  c.textCentered(c.width() / 2, 46, heading, TS_LARGE, true, true);
  decor::ornament(c, c.width() / 2, 80, 300, true);
  constexpr int TOP = 100, LINE = 30, ICON = 120;
  int lines = 0;
  for (int i = 0; i < MAX_LINES && t.lines[i]; i++) lines++;
  int y = TOP + (OK_WIDE.y - 12 - TOP - lines * LINE) / 2;
  if (y < TOP) y = TOP;
  if (t.icon >= 0) {
    const int iy = TOP + (OK_WIDE.y - 12 - TOP) / 2;
    if (t.game)
      gicons::draw(c, t.icon, 150, iy, ICON);
    else
      ticons::draw(c, t.icon, 150, iy, ICON);
  }
  for (int i = 0; i < lines; i++) {
    if (t.lines[i][0]) c.text(290, y, t.lines[i], TS_MED, true);
    y += LINE;
  }
  c.button(OK_WIDE.x, OK_WIDE.y, OK_WIDE.w, OK_WIDE.h, "GOT IT", true, TS_LARGE);
  c.button(NEVER_WIDE.x, NEVER_WIDE.y, NEVER_WIDE.w, NEVER_WIDE.h, "DON'T SHOW AGAIN", false,
           TS_MED);
  c.textCentered(c.width() / 2, OK_WIDE.y + OK_WIDE.h + 6, way_back, TS_SMALL, true);
}
inline Tap hitWide(int x, int y) {
  if (OK_WIDE.hit(x, y)) return Tap::Dismiss;
  if (NEVER_WIDE.hit(x, y)) return Tap::Never;
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

}  // namespace help
