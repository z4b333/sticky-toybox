// Mines: the board, the first safe tap, opening, chording and flags.
//
// No display and no Arduino, so the host test can play thousands of boards and
// check what this file promises: the first tap is never a mine and always
// opens a patch (its whole neighbourhood is kept clear), a zero floods to the
// edge of the numbers, a chord only opens when the flags around it are
// complete, and the game is won exactly when every safe square is open.
//
// 9 x 9, the size of the Sudoku board it shares its geometry with (owner).
#pragma once
#include <stdint.h>
#include <string.h>

namespace mns {

constexpr int N = 9;
constexpr int CELLS = N * N;
constexpr int LEVELS = 3;
constexpr int MINES[LEVELS] = {10, 15, 20};
inline const char* levelName(int l) { return l == 0 ? "Easy" : l == 1 ? "Medium" : "Hard"; }

enum : uint8_t { HIDDEN = 0, OPEN = 1, FLAG = 2 };

struct Board {
  uint8_t mine[CELLS];  // 1 = a mine
  uint8_t st[CELLS];    // HIDDEN / OPEN / FLAG
  uint8_t mines;        // how many were laid
  uint8_t laid;         // 0 until the first tap lays them
  uint8_t boom;         // 1 + the cell that went off, 0 if none
  uint8_t level;
};

inline int xOf(int k) { return k % N; }
inline int yOf(int k) { return k / N; }

inline void reset(Board& b, int level) {
  memset(&b, 0, sizeof(b));
  if (level < 0 || level >= LEVELS) level = 0;
  b.level = (uint8_t)level;
  b.mines = (uint8_t)MINES[level];
}

// Calls f(n) for each square touching k.
template <typename F>
inline void around(int k, F f) {
  const int x = xOf(k), y = yOf(k);
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      const int nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < N && ny >= 0 && ny < N) f(ny * N + nx);
    }
}

inline int count(const Board& b, int k) {
  int n = 0;
  around(k, [&](int j) { n += b.mine[j]; });
  return n;
}
inline int flagsAround(const Board& b, int k) {
  int n = 0;
  around(k, [&](int j) { n += b.st[j] == FLAG; });
  return n;
}
inline int flags(const Board& b) {
  int n = 0;
  for (int k = 0; k < CELLS; k++) n += b.st[k] == FLAG;
  return n;
}
inline bool lost(const Board& b) { return b.boom != 0; }
inline bool won(const Board& b) {
  if (!b.laid || b.boom) return false;
  for (int k = 0; k < CELLS; k++)
    if (!b.mine[k] && b.st[k] != OPEN) return false;
  return true;
}
inline bool over(const Board& b) { return lost(b) || won(b); }

// Lays the mines away from `safe` and its eight neighbours, so the first tap
// always opens a patch rather than a lone number.
template <typename Rnd>
inline void lay(Board& b, int safe, Rnd rnd) {
  uint8_t keep[CELLS] = {};
  keep[safe] = 1;
  around(safe, [&](int j) { keep[j] = 1; });
  int free[CELLS], n = 0;
  for (int k = 0; k < CELLS; k++)
    if (!keep[k]) free[n++] = k;
  for (int i = 0; i < b.mines && n > 0; i++) {
    const int pick = (int)(rnd() % (uint32_t)n);
    b.mine[free[pick]] = 1;
    free[pick] = free[--n];
  }
  b.laid = 1;
}

// Opens k. A zero opens its neighbours, and theirs, to the edge of the numbers
// (an explicit stack: no recursion on an 8 KB task stack). Returns false when
// it was a mine.
inline bool open(Board& b, int k) {
  if (b.st[k] != HIDDEN) return true;
  if (b.mine[k]) {
    b.st[k] = OPEN;
    b.boom = (uint8_t)(k + 1);
    return false;
  }
  int stack[CELLS], sp = 0;
  stack[sp++] = k;
  b.st[k] = OPEN;
  while (sp > 0) {
    const int c = stack[--sp];
    if (count(b, c) != 0) continue;
    around(c, [&](int j) {
      if (b.st[j] == HIDDEN && !b.mine[j]) {
        b.st[j] = OPEN;
        stack[sp++] = j;
      }
    });
  }
  return true;
}

// What a tap does. The first one lays the mines. Tapping an open number whose
// flags are all placed opens everything else around it (a chord) -- one tap
// and one refresh instead of up to eight. Returns false when a mine went off.
template <typename Rnd>
inline bool tap(Board& b, int k, Rnd rnd) {
  if (over(b) || k < 0 || k >= CELLS) return true;
  if (!b.laid) lay(b, k, rnd);
  if (b.st[k] == FLAG) return true;  // a flag protects its square
  if (b.st[k] == HIDDEN) return open(b, k);
  const int n = count(b, k);
  if (n == 0 || flagsAround(b, k) != n) return true;
  bool ok = true;
  around(k, [&](int j) {
    if (b.st[j] == HIDDEN && !open(b, j)) ok = false;
  });
  return ok;
}

// A hold: flag a hidden square, or take the flag off again.
inline bool toggleFlag(Board& b, int k) {
  if (over(b) || k < 0 || k >= CELLS) return false;
  if (b.st[k] == HIDDEN) {
    b.st[k] = FLAG;
    return true;
  }
  if (b.st[k] == FLAG) {
    b.st[k] = HIDDEN;
    return true;
  }
  return false;
}

}  // namespace mns
