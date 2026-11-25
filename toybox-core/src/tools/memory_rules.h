// Memory pairs (C4): cards face down in a grid, turn two at a time, keep the
// pairs that match. Grids 3x4 (6 pairs), 4x4 (8) and 5x5 (12 pairs and a
// blank square in the middle).
//
// Two cards that don't match stay up until the next tap, which turns them
// back -- no timer, so the panel never has to redraw on its own. That next
// tap also turns its own card if it landed on a face-down one.
//
// No display here: the host test checks the deal (every symbol exactly
// twice), the turning rules and the count.
#pragma once
#include <stdint.h>
#include <string.h>

namespace mp {

constexpr int SIZES = 3;
constexpr int MAXC = 25;
constexpr int COLS[SIZES] = {3, 4, 5};
constexpr int ROWS[SIZES] = {4, 4, 5};
inline int cellsFor(int size) { return COLS[size] * ROWS[size]; }
inline int pairsFor(int size) { return cellsFor(size) / 2; }
inline const char* sizeName(int s) { return s == 0 ? "3 x 4" : s == 1 ? "4 x 4" : "5 x 5"; }
constexpr uint8_t BLANK = 0xFF;  // the 5x5 centre

enum : uint8_t { DOWN = 0, UP = 1, KEPT = 2 };

struct Game {
  uint8_t sym[MAXC];
  uint8_t st[MAXC];
  uint8_t size;
  int8_t a, b;     // the cards turned up and not yet matched, -1 when none
  uint16_t flips;  // every card turned face up counts one
  uint8_t found;
};

inline bool isBlank(const Game& g, int k) { return g.sym[k] == BLANK; }

template <typename Rnd>
inline void reset(Game& g, int size, Rnd rnd) {
  memset(&g, 0, sizeof(g));
  if (size < 0 || size >= SIZES) size = 1;
  g.size = (uint8_t)size;
  g.a = g.b = -1;
  const int n = cellsFor(size), pairs = pairsFor(size);
  uint8_t deck[MAXC];
  for (int i = 0; i < pairs * 2; i++) deck[i] = (uint8_t)(i / 2);
  for (int i = pairs * 2 - 1; i > 0; i--) {
    const int j = (int)(rnd() % (uint32_t)(i + 1));
    const uint8_t t = deck[i];
    deck[i] = deck[j];
    deck[j] = t;
  }
  // The symbols themselves are picked at random from the twelve, so a 3x4
  // game does not always show the same six.
  uint8_t pick[12];
  for (int i = 0; i < 12; i++) pick[i] = (uint8_t)i;
  for (int i = 11; i > 0; i--) {
    const int j = (int)(rnd() % (uint32_t)(i + 1));
    const uint8_t t = pick[i];
    pick[i] = pick[j];
    pick[j] = t;
  }
  const int centre = (n % 2) ? n / 2 : -1;
  for (int k = 0, d = 0; k < n; k++) g.sym[k] = (k == centre) ? BLANK : pick[deck[d++]];
}

inline bool won(const Game& g) { return g.found >= pairsFor(g.size); }

enum Event : uint8_t { NOTHING = 0, TURNED, PAIR, MISS, BACK };

// A tap on card k. What happened, for the beep and the refresh.
inline Event tap(Game& g, int k) {
  if (k < 0 || k >= cellsFor(g.size) || isBlank(g, k) || won(g)) return NOTHING;
  Event back = NOTHING;
  if (g.a >= 0 && g.b >= 0) {
    // Two odd cards were left up: this tap puts them back first.
    g.st[g.a] = g.st[g.b] = DOWN;
    const bool onOne = k == g.a || k == g.b;
    g.a = g.b = -1;
    if (onOne) return BACK;
    back = BACK;
  }
  if (g.st[k] != DOWN) return back;
  g.st[k] = UP;
  g.flips++;
  if (g.a < 0) {
    g.a = (int8_t)k;
    return TURNED;
  }
  g.b = (int8_t)k;
  if (g.sym[g.a] == g.sym[g.b]) {
    g.st[g.a] = g.st[g.b] = KEPT;
    g.a = g.b = -1;
    g.found++;
    return PAIR;
  }
  return MISS;
}

}  // namespace mp
