// Battleship rules: the board, the fleet, and what a shot does.
//
// No display, no radio, no Arduino — both the solo opponent and the two-device
// game run on exactly this, so one set of host tests covers both. The board is
// 8x8 rather than the usual 10x10: it fits the portrait screen at a thumb-sized
// cell, and a shorter game suits a panel that takes a third of a second to
// redraw.
#pragma once
#include <stdint.h>
#include <string.h>

namespace sea {

constexpr int N = 8;               // board edge
constexpr int CELLS = N * N;       // 64
constexpr int SHIPS = 4;
constexpr uint8_t LEN[SHIPS] = {4, 3, 3, 2};  // 12 cells of 64, ~19% density
constexpr int SHIP_CELLS = 4 + 3 + 3 + 2;

// shot[] values. Kept distinct from "no ship here" so a miss on an empty cell
// and an unexplored cell never render the same way.
enum : uint8_t { UNSHOT = 0, MISS = 1, HIT = 2 };

struct Board {
  uint8_t ship[CELLS];  // 0 = water, otherwise shipId + 1
  uint8_t shot[CELLS];  // UNSHOT / MISS / HIT
};

struct Shot {
  bool hit = false;
  int8_t sunk = -1;    // ship id that this shot finished, or -1
  bool allSunk = false;
  bool repeat = false;  // the cell had already been fired at
};

inline void clear(Board& b) { memset(&b, 0, sizeof(b)); }

inline int xOf(int cell) { return cell % N; }
inline int yOf(int cell) { return cell / N; }
inline int cellAt(int x, int y) { return y * N + x; }
inline bool inBoard(int x, int y) { return x >= 0 && x < N && y >= 0 && y < N; }

// --- placement ---------------------------------------------------------------

inline bool canPlace(const Board& b, int x, int y, int len, bool horizontal) {
  for (int i = 0; i < len; i++) {
    const int cx = horizontal ? x + i : x;
    const int cy = horizontal ? y : y + i;
    if (!inBoard(cx, cy) || b.ship[cellAt(cx, cy)]) return false;
  }
  return true;
}

inline void put(Board& b, int x, int y, int len, bool horizontal, int shipId) {
  for (int i = 0; i < len; i++) {
    const int cx = horizontal ? x + i : x;
    const int cy = horizontal ? y : y + i;
    b.ship[cellAt(cx, cy)] = (uint8_t)(shipId + 1);
  }
}

// Ships may touch, as they may on a paper board. rnd() supplies randomness so
// the host tests can drive it deterministically. Rejection sampling is fine at
// this density; the bound stops a pathological seed from spinning forever.
template <typename Rnd>
inline bool placeRandom(Board& b, Rnd rnd) {
  for (int attempt = 0; attempt < 200; attempt++) {
    memset(b.ship, 0, sizeof(b.ship));
    bool ok = true;
    for (int s = 0; s < SHIPS && ok; s++) {
      bool placed = false;
      for (int tries = 0; tries < 300 && !placed; tries++) {
        const bool horizontal = (rnd() & 1) != 0;
        const int span = LEN[s];
        const int x = (int)(rnd() % (uint32_t)(horizontal ? N - span + 1 : N));
        const int y = (int)(rnd() % (uint32_t)(horizontal ? N : N - span + 1));
        if (!canPlace(b, x, y, span, horizontal)) continue;
        put(b, x, y, span, horizontal, s);
        placed = true;
      }
      ok = placed;
    }
    if (ok) return true;
  }
  return false;
}

// --- firing ------------------------------------------------------------------

inline bool sunk(const Board& b, int shipId) {
  for (int c = 0; c < CELLS; c++)
    if (b.ship[c] == (uint8_t)(shipId + 1) && b.shot[c] != HIT) return false;
  return true;
}

inline int afloat(const Board& b) {
  int n = 0;
  for (int s = 0; s < SHIPS; s++)
    if (!sunk(b, s)) n++;
  return n;
}

// Resolves a shot against the board that owns the fleet. Each device answers
// for its own waters, exactly as a person does behind a paper screen.
inline Shot fire(Board& b, int cell) {
  Shot r;
  if (cell < 0 || cell >= CELLS) {
    r.repeat = true;
    return r;
  }
  if (b.shot[cell] != UNSHOT) {
    r.repeat = true;
    r.hit = b.shot[cell] == HIT;
    return r;
  }
  const uint8_t id = b.ship[cell];
  r.hit = id != 0;
  b.shot[cell] = r.hit ? HIT : MISS;
  if (r.hit && sunk(b, id - 1)) r.sunk = (int8_t)(id - 1);
  r.allSunk = r.hit && afloat(b) == 0;
  return r;
}

// --- solo opponent -----------------------------------------------------------
// Hunt until something is hit, then work outwards from it. While hunting it
// only tries cells of one colour: the smallest ship is two long, so it must
// cross one of them, and that halves the search without weakening it.
struct Gunner {
  uint8_t tried[CELLS];  // cells this gunner has already fired at
  int8_t queue[16];      // follow-up cells around a live hit
  uint8_t qn;

  void reset() {
    memset(tried, 0, sizeof(tried));
    qn = 0;
  }

  void pushNeighbours(int cell) {
    const int x = xOf(cell), y = yOf(cell);
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
      const int nx = x + dx[d], ny = y + dy[d];
      if (!inBoard(nx, ny)) continue;
      const int c = cellAt(nx, ny);
      if (tried[c] || qn >= (uint8_t)sizeof(queue)) continue;
      bool dup = false;
      for (int i = 0; i < qn; i++)
        if (queue[i] == (int8_t)c) dup = true;
      if (!dup) queue[qn++] = (int8_t)c;
    }
  }

  template <typename Rnd>
  int choose(Rnd rnd) {
    while (qn > 0) {  // finish what we started before hunting again
      const int c = queue[--qn];
      if (!tried[c]) return c;
    }
    for (int parity = 0; parity < 2; parity++) {
      int options[CELLS], n = 0;
      for (int c = 0; c < CELLS; c++) {
        if (tried[c]) continue;
        if (parity == 0 && ((xOf(c) + yOf(c)) & 1)) continue;
        options[n++] = c;
      }
      if (n) return options[rnd() % (uint32_t)n];
    }
    return -1;
  }

  // Told the outcome so it can decide whether to keep probing. A sunk ship ends
  // the chase: anything still queued belonged to it.
  void observe(int cell, const Shot& r) {
    if (cell >= 0 && cell < CELLS) tried[cell] = 1;
    if (r.hit && r.sunk < 0) pushNeighbours(cell);
    if (r.sunk >= 0) qn = 0;
  }
};

}  // namespace sea
