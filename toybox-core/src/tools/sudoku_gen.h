// Sudoku: the board, the solver, and a generator that only emits puzzles with
// exactly one answer.
//
// No display and no Arduino, so the host test can generate thousands of boards
// and check every claim this file makes. Uniqueness is the claim that matters:
// a puzzle with two answers is not a Sudoku, and it is invisible to the player
// until they have wasted an hour on it.
#pragma once
#include <stdint.h>
#include <string.h>

namespace sud {

constexpr int N = 9;
constexpr int CELLS = N * N;

enum Level : uint8_t { EASY = 0, MEDIUM = 1, HARD = 2 };
constexpr int LEVELS = 3;
// Clue counts, not solving techniques. Technique-rating a puzzle needs a solver
// that mimics human deduction; clue count is the honest, cheap proxy and lines
// up well enough with how hard these actually feel.
constexpr int CLUES[LEVELS] = {40, 32, 26};
inline const char* levelName(Level l) {
  return l == EASY ? "EASY" : (l == MEDIUM ? "MEDIUM" : "HARD");
}

inline int rowOf(int c) { return c / N; }
inline int colOf(int c) { return c % N; }
inline int boxOf(int c) { return (rowOf(c) / 3) * 3 + colOf(c) / 3; }

// Can v be written at `cell` without clashing along its row, column or box?
inline bool allowed(const uint8_t* g, int cell, uint8_t v) {
  const int r = rowOf(cell), c = colOf(cell);
  for (int i = 0; i < N; i++) {
    if (g[r * N + i] == v && r * N + i != cell) return false;
    if (g[i * N + c] == v && i * N + c != cell) return false;
  }
  const int br = (r / 3) * 3, bc = (c / 3) * 3;
  for (int dr = 0; dr < 3; dr++)
    for (int dc = 0; dc < 3; dc++) {
      const int k = (br + dr) * N + bc + dc;
      if (g[k] == v && k != cell) return false;
    }
  return true;
}

// Counts solutions, stopping as soon as `limit` have been found. Generation
// only ever needs to know "is there more than one", so it passes limit = 2 and
// the search collapses the moment a second answer appears.
//
// Two things make this fast enough to run on the device. It works from bitmasks
// of what each row, column and box already holds, rebuilt once per node rather
// than re-scanned for every candidate. And it always branches on the emptiest
// cell it can find -- a cell with one candidate forces the move instead of
// spawning nine, and a cell with none ends the branch immediately. Taking the
// first empty cell instead made hard puzzles take seconds to generate.
inline int countSolutions(uint8_t* g, int limit) {
  uint16_t rowUsed[N] = {}, colUsed[N] = {}, boxUsed[N] = {};
  for (int i = 0; i < CELLS; i++) {
    const uint8_t v = g[i];
    if (!v) continue;
    const uint16_t bit = (uint16_t)(1u << v);
    rowUsed[rowOf(i)] |= bit;
    colUsed[colOf(i)] |= bit;
    boxUsed[boxOf(i)] |= bit;
  }

  int best = -1, bestCount = 10;
  uint16_t bestMask = 0;
  for (int i = 0; i < CELLS; i++) {
    if (g[i]) continue;
    const uint16_t used = rowUsed[rowOf(i)] | colUsed[colOf(i)] | boxUsed[boxOf(i)];
    const uint16_t mask = (uint16_t)(~used & 0x03FE);  // bits 1..9
    int n = 0;
    for (uint16_t m = mask; m; m &= (uint16_t)(m - 1)) n++;
    if (n == 0) return 0;  // this cell can hold nothing: the branch is dead
    if (n < bestCount) {
      bestCount = n;
      best = i;
      bestMask = mask;
      if (n == 1) break;  // forced move; nothing will beat it
    }
  }
  if (best < 0) return 1;  // nothing empty left: this is a solution

  int found = 0;
  for (uint8_t v = 1; v <= 9; v++) {
    if (!(bestMask & (uint16_t)(1u << v))) continue;
    g[best] = v;
    found += countSolutions(g, limit - found);
    g[best] = 0;
    if (found >= limit) break;
  }
  return found;
}

// Fills an empty grid with a random complete solution.
template <typename Rnd>
inline bool fillFull(uint8_t* g, Rnd rnd) {
  int cell = -1;
  for (int i = 0; i < CELLS; i++)
    if (!g[i]) {
      cell = i;
      break;
    }
  if (cell < 0) return true;

  uint8_t order[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 8; i > 0; i--) {  // Fisher-Yates, so boards do not repeat
    const int j = (int)(rnd() % (uint32_t)(i + 1));
    const uint8_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int i = 0; i < 9; i++) {
    if (!allowed(g, cell, order[i])) continue;
    g[cell] = order[i];
    if (fillFull(g, rnd)) return true;
    g[cell] = 0;
  }
  return false;
}

inline bool complete(const uint8_t* g) {
  for (int i = 0; i < CELLS; i++)
    if (!g[i]) return false;
  return true;
}

// Every row, column and box is a permutation of 1..9.
inline bool consistent(const uint8_t* g) {
  for (int u = 0; u < N; u++) {
    bool row[10] = {}, col[10] = {}, box[10] = {};
    for (int i = 0; i < N; i++) {
      const uint8_t a = g[u * N + i];
      const uint8_t b = g[i * N + u];
      const int br = (u / 3) * 3 + i / 3, bc = (u % 3) * 3 + i % 3;
      const uint8_t c = g[br * N + bc];
      if (!a || !b || !c) return false;
      if (row[a] || col[b] || box[c]) return false;
      row[a] = col[b] = box[c] = true;
    }
  }
  return true;
}

inline int wrongCount(const uint8_t* g, const uint8_t* solution) {
  int n = 0;
  for (int i = 0; i < CELLS; i++)
    if (g[i] && g[i] != solution[i]) n++;
  return n;
}

inline int remaining(const uint8_t* g) {
  int n = 0;
  for (int i = 0; i < CELLS; i++)
    if (!g[i]) n++;
  return n;
}

// Builds a full solution, then removes clues in rotationally symmetric pairs
// while the answer stays unique. Symmetric digging is the convention printed
// puzzles use and costs nothing; it just looks deliberate rather than chewed.
template <typename Rnd>
inline void generate(uint8_t* puzzle, uint8_t* solution, Level level, Rnd rnd) {
  memset(solution, 0, CELLS);
  fillFull(solution, rnd);
  memcpy(puzzle, solution, CELLS);

  const int target = CLUES[level];
  int clues = CELLS;

  int order[CELLS];
  for (int i = 0; i < CELLS; i++) order[i] = i;
  for (int i = CELLS - 1; i > 0; i--) {
    const int j = (int)(rnd() % (uint32_t)(i + 1));
    const int t = order[i];
    order[i] = order[j];
    order[j] = t;
  }

  // One pass, one cell at a time. Removing in rotationally symmetric pairs is
  // prettier and is what printed puzzles do, but it forces two removals to
  // succeed together, and at the hard target that fails often enough to leave
  // a "hard" board with medium's clue count. Hitting the intended difficulty
  // beats a tidy-looking pattern of blanks.
  for (int k = 0; k < CELLS && clues > target; k++) {
    const int a = order[k];
    if (!puzzle[a]) continue;
    const uint8_t saved = puzzle[a];
    puzzle[a] = 0;
    uint8_t work[CELLS];
    memcpy(work, puzzle, CELLS);
    if (countSolutions(work, 2) == 1)
      clues--;
    else
      puzzle[a] = saved;  // removing this would have made the answer ambiguous
  }
}

}  // namespace sud
