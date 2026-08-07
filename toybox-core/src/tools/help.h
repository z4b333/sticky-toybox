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

inline void render(ToolsCanvas& c, const Text& t) {
  c.textCentered(c.width() / 2, 46, "HOW TO PLAY", TS_LARGE, true, true);
  decor::ornament(c, c.width() / 2, 80, 300, true);

  // The game's own hub icon, big, above its rules -- so the card that opens
  // every game still looks like that particular game.
  if (t.icon >= 0) {
    if (t.game)
      gicons::draw(c, t.icon, c.width() / 2, 130, 76);
    else
      ticons::draw(c, t.icon, c.width() / 2, 130, 76);
  }

  // The rules read at the next size up now that the face is proportional: the
  // same 26-character lines that filled the panel in the old pixel font take
  // barely half of it, and rules are the one screen you actually read.
  int y = t.icon >= 0 ? 186 : 118;
  for (int i = 0; i < MAX_LINES && t.lines[i]; i++) {
    if (t.lines[i][0]) c.text(34, y, t.lines[i], TS_LARGE, true);
    y += 34;  // blank entries are deliberate paragraph breaks
  }

  c.button(OK_BTN.x, OK_BTN.y, OK_BTN.w, OK_BTN.h, "GOT IT", true, TS_LARGE);
  c.button(NEVER_BTN.x, NEVER_BTN.y, NEVER_BTN.w, NEVER_BTN.h, "DON'T SHOW AGAIN", false,
           TS_MED);
  c.textCentered(c.width() / 2, NEVER_BTN.y + NEVER_BTN.h + 8, "the ? above brings this back",
                 TS_SMALL, true);
}

inline Tap hit(int x, int y) {
  if (OK_BTN.hit(x, y)) return Tap::Dismiss;
  if (NEVER_BTN.hit(x, y)) return Tap::Never;
  return Tap::None;
}

// --- the cards ---------------------------------------------------------------
// Kept to about 26 characters a line, which is what fits at this text size
// without the eye having to track back across the whole panel.

inline constexpr Text WORDLE{{
    "Guess the hidden 5-letter",
    "word in six tries.",
    "",
    "After each guess:",
    "solid tile - right letter,",
    "right place",
    "outlined - right letter,",
    "wrong place",
    "hatched - not in the word",
},
                            true,
                            0};

inline constexpr Text NONOGRAM{{
    "The numbers say how many",
    "squares to fill in that",
    "row or column, in order,",
    "with a gap between runs.",
    "",
    "Tap to fill. Switch to X",
    "to mark squares you have",
    "worked out are empty.",
    "",
    "HINT reveals one square.",
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
    "In 3 MARKS mode you only",
    "ever have three marks on",
    "the board. Place a fourth",
    "and your oldest lifts off",
    "- it is drawn faint for a",
    "turn before it goes, so",
    "you can plan around it.",
},
                            true,
                            3};

inline constexpr Text SHIPS{{
    "Both fleets are hidden.",
    "Take turns firing.",
    "",
    "Tap a square on ENEMY",
    "WATERS to aim, then press",
    "FIRE to commit. Aiming",
    "costs nothing.",
    "",
    "Dot is a miss, block is a",
    "hit. Sink all four ships.",
},
                            false,
                            7};

inline constexpr Text SUDOKU{{
    "Fill every empty square so",
    "that each row, column and",
    "3x3 box holds 1 to 9 once",
    "each.",
    "",
    "Tap a square, then tap a",
    "number. CLR empties it.",
    "",
    "Large bold digits came",
    "with the puzzle and cannot",
    "be changed.",
},
                            false,
                            8};

}  // namespace help
