// Battleship rules: the board, the fleet, and what a shot does.
//
// No display, no radio, no Arduino -- both the solo opponent and the two-device
// game run on exactly this, so one set of host tests covers both.
//
// v2 (C3, kit "Ships"): the usual 10x10 board and five ships -- Carrier 5,
// Battleship 4, Cruiser 3, Sub 2, Boat 2 -- placed by hand or shuffled. The
// board now fills the panel's width, so a cell is 44 px, about the 8x8 board's
// old size.
#pragma once
#include <stdint.h>
#include <string.h>

namespace sea {

constexpr int N = 10;              // board edge
constexpr int CELLS = N * N;       // 100
constexpr int SHIPS = 5;
constexpr uint8_t LEN[SHIPS] = {5, 4, 3, 2, 2};  // 16 cells of 100
constexpr int SHIP_CELLS = 5 + 4 + 3 + 2 + 2;
inline const char* shipName(int s) {
  static const char* const NAMES[SHIPS] = {"Carrier", "Battleship", "Cruiser", "Sub", "Boat"};
  return (s >= 0 && s < SHIPS) ? NAMES[s] : "";
}

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

// Takes one ship off the board (placing by hand: a placed ship picked up again).
inline void lift(Board& b, int shipId) {
  for (int c = 0; c < CELLS; c++)
    if (b.ship[c] == (uint8_t)(shipId + 1)) b.ship[c] = 0;
}
inline bool placed(const Board& b, int shipId) {
  for (int c = 0; c < CELLS; c++)
    if (b.ship[c] == (uint8_t)(shipId + 1)) return true;
  return false;
}
inline bool fleetPlaced(const Board& b) {
  for (int s = 0; s < SHIPS; s++)
    if (!placed(b, s)) return false;
  return true;
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
// Three levels (owner, C3):
//   Easy   -- fires at random, and follows up a hit only half the time.
//   Normal -- hunts on one colour of a checkerboard (the smallest ship is two
//             long, so it must cross one), then works outwards from a hit.
//   Hard   -- counts, for every square, how many ways the ships still afloat
//             could lie across it given everything seen so far, and fires at
//             the likeliest; with a hit still unexplained it only counts the
//             ways that cover that hit.
enum Level : uint8_t { EASY = 0, NORMAL = 1, HARD = 2 };
constexpr int LEVELS = 3;
inline const char* levelName(int l) { return l == EASY ? "Easy" : l == HARD ? "Hard" : "Normal"; }

struct Gunner {
  uint8_t tried[CELLS];  // 0 not yet, 1 miss, 2 hit, 3 hit on a ship since sunk
  int8_t queue[48];      // follow-up cells around a live hit (Easy, Normal)
  uint8_t qn;
  uint8_t afloat[SHIPS];  // 1 while that ship is still up
  uint8_t level;

  void reset(int lvl = NORMAL) {
    memset(tried, 0, sizeof(tried));
    qn = 0;
    for (int s = 0; s < SHIPS; s++) afloat[s] = 1;
    level = (uint8_t)(lvl < 0 || lvl >= LEVELS ? NORMAL : lvl);
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

  // Hard: the likeliest square. Ties are broken by rnd so the same board does
  // not always draw the same first shot.
  template <typename Rnd>
  int densest(Rnd rnd) {
    uint16_t score[CELLS] = {};
    bool live = false;
    for (int c = 0; c < CELLS; c++)
      if (tried[c] == 2) live = true;
    for (int s = 0; s < SHIPS; s++) {
      if (!afloat[s]) continue;
      const int len = LEN[s];
      for (int h = 0; h < 2; h++)
        for (int y = 0; y < N; y++)
          for (int x = 0; x < N; x++) {
            int covers = 0;
            bool ok = true;
            for (int i = 0; i < len && ok; i++) {
              const int cx = h ? x + i : x, cy = h ? y : y + i;
              if (!inBoard(cx, cy)) {
                ok = false;
                break;
              }
              const uint8_t t = tried[cellAt(cx, cy)];
              if (t == 1 || t == 3) ok = false;  // water, or a wreck already
              if (t == 2) covers++;
            }
            if (!ok || (live && covers == 0)) continue;
            const int w = live ? 1 + 8 * covers : 1;
            for (int i = 0; i < len; i++) {
              const int c = h ? cellAt(x + i, y) : cellAt(x, y + i);
              if (!tried[c]) score[c] = (uint16_t)(score[c] + w);
            }
          }
    }
    int best = -1, n = 0;
    int pick[CELLS];
    for (int c = 0; c < CELLS; c++) {
      if (tried[c] || !score[c]) continue;
      if (best < 0 || score[c] > score[best]) {
        best = c;
        n = 0;
      }
      if (score[c] == score[best]) pick[n++] = c;
    }
    if (n > 0) return pick[rnd() % (uint32_t)n];
    for (int c = 0; c < CELLS; c++)
      if (!tried[c]) return c;
    return -1;
  }

  template <typename Rnd>
  int choose(Rnd rnd) {
    if (level == HARD) return densest(rnd);
    while (qn > 0) {  // finish what we started before hunting again
      const int c = queue[--qn];
      if (!tried[c]) return c;
    }
    for (int parity = (level == EASY ? 1 : 0); parity < 2; parity++) {
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

  // Told the outcome so it can decide where to go next. A sunk ship ends the
  // chase: its hits become wreck, and anything still queued belonged to it.
  template <typename Rnd>
  void observe(int cell, const Shot& r, Rnd rnd) {
    if (cell < 0 || cell >= CELLS) return;
    tried[cell] = r.hit ? 2 : 1;
    if (r.hit && r.sunk < 0) {
      if (level != EASY || (rnd() & 1)) pushNeighbours(cell);
    }
    if (r.sunk >= 0) {
      afloat[r.sunk] = 0;
      qn = 0;
      wreck(cell, LEN[r.sunk]);
    }
  }
  void observe(int cell, const Shot& r) {
    observe(cell, r, [] { return 0u; });
  }

  // The sunk ship's squares stop being "a hit to explain": the run of live
  // hits through the last shot that is exactly that long, across or down.
  void wreck(int cell, int len) {
    const int x = xOf(cell), y = yOf(cell);
    for (int h = 0; h < 2; h++) {
      for (int start = -(len - 1); start <= 0; start++) {
        bool ok = true;
        for (int i = 0; i < len && ok; i++) {
          const int cx = h ? x + start + i : x, cy = h ? y : y + start + i;
          if (!inBoard(cx, cy) || tried[cellAt(cx, cy)] != 2) ok = false;
        }
        if (!ok) continue;
        for (int i = 0; i < len; i++) {
          const int cx = h ? x + start + i : x, cy = h ? y : y + start + i;
          tried[cellAt(cx, cy)] = 3;
        }
        return;
      }
    }
    tried[cell] = 3;  // could not tell which run: at least this one is wreck
  }
};

}  // namespace sea
