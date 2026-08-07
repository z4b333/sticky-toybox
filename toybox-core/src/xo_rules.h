// Noughts-and-crosses rules and search, with no dependency on the display or
// the Arduino core — so the host test can play whole tournaments against it.
//
// Two rule sets share one representation. CLASSIC is the familiar game.
// THREE keeps only three marks per side: placing a fourth lifts your oldest,
// which removes the draw-by-full-board ending and with it most of the draws.
#pragma once
#include <stdint.h>
#include <string.h>

namespace xorules {

inline constexpr uint8_t LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                                        {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

// Cells hold 0 (empty), 1 (X) or 2 (O). queue[p] is that player's marks in the
// order they were placed, oldest first — only meaningful under THREE.
struct State {
  uint8_t cell[9];
  uint8_t queue[2][3];
  uint8_t qn[2];
};

inline void reset(State& s) { memset(&s, 0, sizeof(s)); }

inline void applyMove(State& s, int side, int cell, bool three) {
  s.cell[cell] = (uint8_t)side;
  if (!three) return;
  const int p = side - 1;
  if (s.qn[p] == 3) {
    s.cell[s.queue[p][0]] = 0;
    s.queue[p][0] = s.queue[p][1];
    s.queue[p][1] = s.queue[p][2];
    s.qn[p] = 2;
  }
  s.queue[p][s.qn[p]++] = (uint8_t)cell;
}

inline int winLine(const State& s) {
  for (int i = 0; i < 8; i++) {
    const uint8_t a = s.cell[LINES[i][0]];
    if (a && a == s.cell[LINES[i][1]] && a == s.cell[LINES[i][2]]) return i;
  }
  return -1;
}

inline int winner(const State& s) {
  const int l = winLine(s);
  return l < 0 ? 0 : s.cell[LINES[l][0]];
}

inline bool boardFull(const State& s) {
  for (int i = 0; i < 9; i++)
    if (!s.cell[i]) return false;
  return true;
}

// The mark that will lift when this side places again, or -1 if none will.
inline int doomed(const State& s, int side, bool three) {
  if (!three) return -1;
  const int p = side - 1;
  return s.qn[p] == 3 ? s.queue[p][0] : -1;
}

// Scores carry the remaining depth, so the search prefers the fastest win and
// the slowest loss rather than treating all of them as equal.
inline int negamax(const State& s, int side, int depth, int alpha, int beta, bool three) {
  const int w = winner(s);
  if (w) return (w == side) ? (1000 + depth) : -(1000 + depth);
  if (!three && boardFull(s)) return 0;
  if (depth == 0) return 0;

  int best = -30000;
  for (int c = 0; c < 9; c++) {
    if (s.cell[c]) continue;
    State n = s;
    applyMove(n, side, c, three);
    const int v = -negamax(n, 3 - side, depth - 1, -beta, -alpha, three);
    if (v > best) best = v;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;  // the opponent would never allow this line
  }
  return best == -30000 ? 0 : best;
}

// CLASSIC is small enough to solve outright. THREE has no terminal depth, so it
// searches a fixed horizon and scores unfinished lines as level.
inline int searchDepth(bool three) { return three ? 6 : 9; }

// rnd only breaks ties, so the machine varies between equally good replies.
inline int bestMove(const State& s, int side, bool three, uint32_t rnd) {
  int best = -30000, picks[9], n = 0;
  for (int c = 0; c < 9; c++) {
    if (s.cell[c]) continue;
    State t = s;
    applyMove(t, side, c, three);
    const int v = -negamax(t, 3 - side, searchDepth(three) - 1, -30000, 30000, three);
    if (v > best) {
      best = v;
      n = 0;
    }
    if (v == best) picks[n++] = c;
  }
  return n == 0 ? -1 : picks[rnd % (uint32_t)n];
}

// Finishes a win it can see, but only spots yours about half the time.
inline int easyMove(const State& s, int side, bool three, uint32_t rnd) {
  const int foe = 3 - side;
  int empty[9], n = 0;
  for (int c = 0; c < 9; c++)
    if (!s.cell[c]) empty[n++] = c;
  if (n == 0) return -1;

  for (int i = 0; i < n; i++) {
    State t = s;
    applyMove(t, side, empty[i], three);
    if (winner(t) == side) return empty[i];
  }
  if (rnd & 1) {
    for (int i = 0; i < n; i++) {
      State t = s;
      applyMove(t, foe, empty[i], three);
      if (winner(t) == foe) return empty[i];
    }
  }
  return empty[(rnd >> 1) % (uint32_t)n];
}

}  // namespace xorules
