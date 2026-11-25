// Tower of Hanoi (C4): move the whole stack from the left peg to the right
// one, one disc at a time, never a bigger disc on a smaller. 3 to 7 discs;
// the fewest moves for n discs is 2^n - 1.
//
// No display here: the host test checks that illegal moves are refused, that
// Undo puts every disc back, and that the textbook solution solves it in
// exactly the minimum.
#pragma once
#include <stdint.h>
#include <string.h>

namespace hn {

constexpr int PEGS = 3;
constexpr int MIN_DISCS = 3, MAX_DISCS = 7, DEFAULT_DISCS = 4;
constexpr int HIST = 128;  // moves Undo can walk back

struct Game {
  uint8_t peg[PEGS][MAX_DISCS];  // disc sizes bottom to top, 1 is the smallest
  uint8_t n[PEGS];
  uint8_t discs;
  int8_t held;     // the peg whose top disc is lifted, -1 when none
  uint16_t moves;
  uint8_t hist[HIST];  // from * 3 + to, a ring of the last HIST moves
  uint8_t nh;          // how many of them Undo may take back
  uint8_t hpos;
};

inline int minMoves(int discs) { return (1 << discs) - 1; }

inline void reset(Game& g, int discs) {
  memset(&g, 0, sizeof(g));
  if (discs < MIN_DISCS || discs > MAX_DISCS) discs = DEFAULT_DISCS;
  g.discs = (uint8_t)discs;
  g.held = -1;
  for (int i = 0; i < discs; i++) g.peg[0][i] = (uint8_t)(discs - i);
  g.n[0] = (uint8_t)discs;
}

inline int top(const Game& g, int p) { return g.n[p] ? g.peg[p][g.n[p] - 1] : 0; }
inline bool solved(const Game& g) { return g.n[PEGS - 1] == g.discs && g.held < 0; }

inline bool canMove(const Game& g, int from, int to) {
  if (from < 0 || from >= PEGS || to < 0 || to >= PEGS || from == to) return false;
  if (!g.n[from]) return false;
  return !g.n[to] || top(g, to) > top(g, from);
}

inline bool move(Game& g, int from, int to) {
  if (!canMove(g, from, to)) return false;
  g.peg[to][g.n[to]++] = g.peg[from][--g.n[from]];
  g.moves++;
  g.hist[g.hpos] = (uint8_t)(from * 3 + to);
  g.hpos = (uint8_t)((g.hpos + 1) % HIST);
  if (g.nh < HIST) g.nh++;
  return true;
}

inline bool undo(Game& g) {
  if (!g.nh) return false;
  g.hpos = (uint8_t)((g.hpos + HIST - 1) % HIST);
  g.nh--;
  const int from = g.hist[g.hpos] / 3, to = g.hist[g.hpos] % 3;
  g.peg[from][g.n[from]++] = g.peg[to][--g.n[to]];
  g.moves--;
  g.held = -1;
  return true;
}

// A tap on peg p: lift its top disc, or put the lifted disc down there.
// Putting it back where it came from just lowers it. Returns false for a
// move the rules refuse (and for lifting from an empty peg).
inline bool tap(Game& g, int p) {
  if (p < 0 || p >= PEGS) return false;
  if (g.held < 0) {
    if (!g.n[p]) return false;
    g.held = (int8_t)p;
    return true;
  }
  if (p == g.held) {
    g.held = -1;
    return true;
  }
  if (!canMove(g, g.held, p)) return false;
  move(g, g.held, p);
  g.held = -1;
  return true;
}

}  // namespace hn
