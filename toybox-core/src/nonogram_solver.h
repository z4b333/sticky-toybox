// Nonogram puzzle logic: clue computation, line solver (forced-cell
// propagation), full logic-solvability check, and random puzzle generation.
// Pure C++ with no Arduino dependencies so it can be unit-tested on the host.
#pragma once
#include <cstdint>
#include <cstring>

namespace nono {

constexpr int MAXN = 10;
constexpr int MAXG = (MAXN + 1) / 2;

inline void cluesOfLine(const uint8_t* line, int n, int stride, uint8_t* clue, uint8_t* cnt) {
  *cnt = 0;
  int run = 0;
  for (int i = 0; i < n; i++) {
    if (line[i * stride]) {
      run++;
    } else if (run) {
      clue[(*cnt)++] = run;
      run = 0;
    }
  }
  if (run) clue[(*cnt)++] = run;
}

// Enumerate all placements of the clue groups consistent with known cells;
// accumulate which cells can be filled / empty across every valid placement.
// cells: -1 unknown, 0 empty, 1 fill.
inline void enumeratePlacements(int len, const uint8_t* clue, int nClue, const int8_t* cells,
                                int gi, int pos, uint8_t* canFill, uint8_t* canEmpty,
                                uint8_t* line, long& found) {
  if (found > 20000) return;  // safety bound
  if (gi == nClue) {
    for (int i = pos; i < len; i++) {
      if (cells[i] == 1) return;  // leftover cell demanded filled: invalid
    }
    for (int i = 0; i < len; i++) {
      const uint8_t v = (i < pos) ? line[i] : 0;
      if (v)
        canFill[i] = 1;
      else
        canEmpty[i] = 1;
    }
    found++;
    return;
  }
  const int g = clue[gi];
  int need = g;
  for (int k = gi + 1; k < nClue; k++) need += 1 + clue[k];
  for (int start = pos; start + need <= len; start++) {
    // cells[pos..start-1] must be allowed empty
    bool ok = true;
    for (int i = pos; i < start && ok; i++)
      if (cells[i] == 1) ok = false;
    if (!ok) break;  // a demanded-fill cell can never be skipped; later starts fail too
    for (int i = start; i < start + g && ok; i++)
      if (cells[i] == 0) ok = false;
    if (!ok) continue;
    const int after = start + g;
    for (int i = pos; i < start; i++) line[i] = 0;
    for (int i = start; i < after; i++) line[i] = 1;
    if (gi + 1 < nClue) {
      if (after < len && cells[after] != 1) {
        line[after] = 0;
        enumeratePlacements(len, clue, nClue, cells, gi + 1, after + 1, canFill, canEmpty,
                            line, found);
      }
    } else {
      enumeratePlacements(len, clue, nClue, cells, gi + 1, after, canFill, canEmpty, line,
                          found);
    }
  }
}

// Apply forced cells for one line. Returns false on contradiction.
inline bool lineSolve(int len, const uint8_t* clue, int nClue, int8_t* cells) {
  uint8_t canFill[MAXN] = {}, canEmpty[MAXN] = {}, line[MAXN] = {};
  long found = 0;
  if (nClue == 0) {
    for (int i = 0; i < len; i++) {
      if (cells[i] == 1) return false;
      canEmpty[i] = 1;
    }
    found = 1;
  } else {
    enumeratePlacements(len, clue, nClue, cells, 0, 0, canFill, canEmpty, line, found);
  }
  if (found == 0) return false;
  for (int i = 0; i < len; i++) {
    if (cells[i] != -1) continue;
    if (canFill[i] && !canEmpty[i]) cells[i] = 1;
    if (!canFill[i] && canEmpty[i]) cells[i] = 0;
  }
  return true;
}

struct Clues {
  uint8_t row[MAXN][MAXG], rowN[MAXN];
  uint8_t col[MAXN][MAXG], colN[MAXN];
};

inline void cluesFromSolution(const uint8_t sol[MAXN][MAXN], int n, Clues& out) {
  for (int r = 0; r < n; r++) cluesOfLine(&sol[r][0], n, 1, out.row[r], &out.rowN[r]);
  for (int c = 0; c < n; c++) cluesOfLine(&sol[0][c], n, MAXN, out.col[c], &out.colN[c]);
}

// True if line-by-line forced-cell propagation fully solves the puzzle
// (which also implies the solution is unique).
inline bool logicSolvable(int n, const Clues& cl, int8_t out[MAXN][MAXN] = nullptr) {
  int8_t work[MAXN][MAXN];
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++) work[r][c] = -1;

  bool changed = true;
  int guard = 0;
  while (changed && guard++ < 60) {
    changed = false;
    int8_t line[MAXN];
    for (int r = 0; r < n; r++) {
      for (int c = 0; c < n; c++) line[c] = work[r][c];
      if (!lineSolve(n, cl.row[r], cl.rowN[r], line)) return false;
      for (int c = 0; c < n; c++)
        if (line[c] != work[r][c]) {
          work[r][c] = line[c];
          changed = true;
        }
    }
    for (int c = 0; c < n; c++) {
      int8_t colLine[MAXN];
      for (int r = 0; r < n; r++) colLine[r] = work[r][c];
      if (!lineSolve(n, cl.col[c], cl.colN[c], colLine)) return false;
      for (int r = 0; r < n; r++)
        if (colLine[r] != work[r][c]) {
          work[r][c] = colLine[r];
          changed = true;
        }
    }
  }
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++)
      if (work[r][c] == -1) return false;
  if (out) memcpy(out, work, sizeof(work));
  return true;
}

// Generate a random logic-solvable puzzle. rnd() must return a uniform uint32.
// Returns the number of attempts used (0 = gave up; caller keeps last grid).
template <typename Rnd>
inline int generateSolvable(int n, uint8_t sol[MAXN][MAXN], Clues& cl, Rnd rnd,
                            int maxAttempts = 400) {
  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    const int densityPct = 45 + rnd() % 20;  // 45..64%
    for (int r = 0; r < n; r++)
      for (int c = 0; c < n; c++) sol[r][c] = (int)(rnd() % 100) < densityPct;
    cluesFromSolution(sol, n, cl);
    if (logicSolvable(n, cl)) return attempt;
  }
  return 0;
}

}  // namespace nono
