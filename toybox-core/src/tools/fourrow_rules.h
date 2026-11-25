// Four in a Row: the board, the moves, the win, and the Sticky's play.
//
// No display and no Arduino, so the host test can check what this promises:
// a disc lands on the lowest free row, a full column refuses, four in a line
// wins in every direction, a full board with no line is a draw, Undo puts the
// board back exactly, and the Sticky takes a win and blocks a loss when its
// level says it should. Written so a network game (multiplayer plan) can drive
// it later: a move is a column number and nothing else.
//
// The board is two bitboards, one per player, seven bits to a column -- six
// rows and an empty one on top, so a line can never wrap from one column into
// the next. Bit (col * 7 + row), row 0 at the bottom.
#pragma once
#include <stdint.h>
#include <string.h>

namespace c4 {

constexpr int W = 7, H = 6;
constexpr int CELLS = W * H;
constexpr int LEVELS = 3;
enum : uint8_t { EASY = 0, NORMAL = 1, HARD = 2 };
inline const char* levelName(int l) { return l == EASY ? "Easy" : l == NORMAL ? "Normal" : "Hard"; }

// Players by who they are, not by who moved first: 0 is you (or player 1), 1 is
// the Sticky (or player 2). `first` says which of them opened this game.
struct Game {
  uint64_t pos[2];
  uint8_t height[W];   // discs in each column
  uint8_t moves;       // discs on the board
  uint8_t first;       // who opened
  uint8_t hist[CELLS];  // the columns played, in order, for Undo
};

// (Not "bit": the Arduino core defines a macro by that name.)
inline int bitIndex(int col, int row) { return col * (H + 1) + row; }
inline uint64_t mask(int col, int row) { return 1ULL << bitIndex(col, row); }

inline void reset(Game& g, int first) {
  memset(&g, 0, sizeof(g));
  g.first = (uint8_t)(first & 1);
}

// Whose move it is now (0 you, 1 Sticky).
inline int toMove(const Game& g) { return (g.first + g.moves) & 1; }

// 0 empty, 1 player 0's disc, 2 player 1's.
inline int at(const Game& g, int col, int row) {
  const uint64_t m = mask(col, row);
  return (g.pos[0] & m) ? 1 : (g.pos[1] & m) ? 2 : 0;
}

inline bool canPlay(const Game& g, int col) { return col >= 0 && col < W && g.height[col] < H; }
inline bool full(const Game& g) { return g.moves >= CELLS; }

// Four in a line anywhere in one player's discs. Shifting by 1 looks up a
// column, by 7 across a row, by 6 and 8 along the two diagonals.
inline bool fourIn(uint64_t b) {
  static const int DIRS[4] = {1, H + 1, H, H + 2};
  for (int d : DIRS) {
    const uint64_t m = b & (b >> d);
    if (m & (m >> (2 * d))) return true;
  }
  return false;
}

// The winner so far: -1 nobody, 0 or 1.
inline int winner(const Game& g) {
  if (fourIn(g.pos[0])) return 0;
  if (fourIn(g.pos[1])) return 1;
  return -1;
}
inline bool over(const Game& g) { return winner(g) >= 0 || full(g); }

// Drops a disc for the side to move. False (and nothing changes) for a full
// column, a column off the board, or a game already decided.
inline bool play(Game& g, int col) {
  if (!canPlay(g, col) || winner(g) >= 0) return false;
  const int p = toMove(g);
  g.pos[p] |= mask(col, g.height[col]);
  g.height[col]++;
  g.hist[g.moves++] = (uint8_t)col;
  return true;
}

// Takes back the last disc. False when the board is empty.
inline bool undo(Game& g) {
  if (!g.moves) return false;
  const int col = g.hist[--g.moves];
  const int p = toMove(g);  // after the decrement: the side that played it
  g.height[col]--;
  g.pos[p] &= ~mask(col, g.height[col]);
  return true;
}

inline int lastCol(const Game& g) { return g.moves ? g.hist[g.moves - 1] : -1; }

// The four winning cells, for drawing. Fills cols/rows and returns true when
// someone has won.
inline bool winLine(const Game& g, int cols[4], int rows[4]) {
  const int w = winner(g);
  if (w < 0) return false;
  static const int DC[4] = {0, 1, 1, 1}, DR[4] = {1, 0, 1, -1};
  for (int c = 0; c < W; c++)
    for (int r = 0; r < H; r++)
      for (int d = 0; d < 4; d++) {
        int k = 0;
        for (; k < 4; k++) {
          const int cc = c + DC[d] * k, rr = r + DR[d] * k;
          if (cc < 0 || cc >= W || rr < 0 || rr >= H || at(g, cc, rr) != w + 1) break;
        }
        if (k < 4) continue;
        for (k = 0; k < 4; k++) {
          cols[k] = c + DC[d] * k;
          rows[k] = r + DR[d] * k;
        }
        return true;
      }
  return false;
}

// --- the Sticky ------------------------------------------------------------------

namespace detail {
// Every line of four on the board, as a bit mask: 69 of them.
struct Windows {
  uint64_t m[69];
  int n = 0;
  Windows() {
    static const int DC[4] = {0, 1, 1, 1}, DR[4] = {1, 0, 1, -1};
    for (int c = 0; c < W; c++)
      for (int r = 0; r < H; r++)
        for (int d = 0; d < 4; d++) {
          const int ec = c + DC[d] * 3, er = r + DR[d] * 3;
          if (ec < 0 || ec >= W || er < 0 || er >= H) continue;
          uint64_t x = 0;
          for (int k = 0; k < 4; k++) x |= mask(c + DC[d] * k, r + DR[d] * k);
          m[n++] = x;
        }
  }
};
inline const Windows& windows() {
  static const Windows w;
  return w;
}
inline int pop(uint64_t x) { return __builtin_popcountll(x); }

// How good the board looks for `me`: open lines of three and two, and the
// centre column, which takes part in the most lines.
inline int eval(const Game& g, int me) {
  static const int WT[4] = {0, 1, 4, 32};
  const uint64_t a = g.pos[me], b = g.pos[me ^ 1];
  int s = 0;
  const Windows& w = windows();
  for (int i = 0; i < w.n; i++) {
    const int na = pop(a & w.m[i]), nb = pop(b & w.m[i]);
    if (nb == 0)
      s += WT[na];
    else if (na == 0)
      s -= WT[nb];
  }
  uint64_t centre = 0;
  for (int r = 0; r < H; r++) centre |= mask(3, r);
  s += 3 * (pop(a & centre) - pop(b & centre));
  return s;
}

// Centre first: the columns nearest the middle are tried first, and of two
// equal scores the earlier one is kept, so a tie goes to the centre.
constexpr int ORDER[W] = {3, 2, 4, 1, 5, 0, 6};
constexpr int WIN = 1000000;

struct Search {
  long nodes = 0;
  long budget = 0;
  bool out = false;  // ran past the budget: this depth's answer is not used
};

inline int negamax(Game& g, int depth, int alpha, int beta, Search& s) {
  if (++s.nodes > s.budget) {
    s.out = true;
    return 0;
  }
  const int me = toMove(g);
  // A win on the spot ends the search here.
  for (int c = 0; c < W; c++) {
    if (!canPlay(g, c)) continue;
    if (fourIn(g.pos[me] | mask(c, g.height[c]))) return WIN - g.moves;
  }
  if (full(g)) return 0;
  if (depth == 0) return eval(g, me);
  int best = -WIN * 2;
  for (int i = 0; i < W; i++) {
    const int c = ORDER[i];
    if (!canPlay(g, c)) continue;
    play(g, c);
    const int v = -negamax(g, depth - 1, -beta, -alpha, s);
    undo(g);
    if (s.out) return 0;
    if (v > best) best = v;
    if (v > alpha) alpha = v;
    if (alpha >= beta) break;
  }
  return best;
}

// The best column at exactly `depth`, or -1 if the budget ran out.
inline int bestAt(Game& g, int depth, Search& s) {
  int best = -1, bestV = -WIN * 2, alpha = -WIN * 2;
  for (int i = 0; i < W; i++) {
    const int c = ORDER[i];
    if (!canPlay(g, c)) continue;
    play(g, c);
    const int v = -negamax(g, depth - 1, -WIN * 2, -alpha, s);
    undo(g);
    if (s.out) return -1;
    if (v > bestV) {
      bestV = v;
      best = c;
    }
    if (v > alpha) alpha = v;
  }
  return best;
}

inline bool winsAt(const Game& g, int who, int col) {
  return canPlay(g, col) && fourIn(g.pos[who] | mask(col, g.height[col]));
}
}  // namespace detail

// How deep each level looks, in single moves, and how much work it may do
// before it settles for the last depth it finished. The budget is counted in
// positions, not milliseconds, so the same board always gets the same reply.
constexpr int DEPTH[LEVELS] = {2, 5, 8};
constexpr long BUDGET[LEVELS] = {4000, 20000, 80000};

// The Sticky's move for the side to move. `rnd` is only used at Easy, which
// plays loosely: it always takes a win, blocks your four three times in four,
// and otherwise picks among the columns its shallow look likes, leaning to the
// middle. Normal and Hard are deterministic.
template <typename Rnd>
inline int bestMove(const Game& g0, int level, Rnd rnd, long* nodesOut = nullptr) {
  Game g = g0;
  const int me = toMove(g), you = me ^ 1;
  if (level < 0 || level >= LEVELS) level = HARD;
  // A win now, whatever the level.
  for (int i = 0; i < W; i++)
    if (detail::winsAt(g, me, detail::ORDER[i])) return detail::ORDER[i];
  int block = -1;
  for (int i = 0; i < W && block < 0; i++)
    if (detail::winsAt(g, you, detail::ORDER[i])) block = detail::ORDER[i];
  if (level == EASY) {
    // One time in four Easy simply does not see your three.
    const bool missed = block >= 0 && (rnd() % 4) == 0;
    if (block >= 0 && !missed) return block;
    // Otherwise anything that does not hand over a win straight away (a miss
    // looks at nothing), weighted to the middle: a beginner's idea of a good
    // column.
    static const int WEIGHT[W] = {1, 2, 3, 4, 3, 2, 1};
    int cand[W], wt[W], n = 0, total = 0;
    for (int c = 0; c < W; c++) {
      if (!canPlay(g, c)) continue;
      if (missed && c == block) continue;
      play(g, c);
      bool gives = false;
      for (int d = 0; d < W && !gives && !missed; d++) gives = detail::winsAt(g, you, d);
      undo(g);
      if (gives) continue;
      cand[n] = c;
      wt[n] = WEIGHT[c];
      total += WEIGHT[c];
      n++;
    }
    if (n == 0) {
      for (int i = 0; i < W; i++)
        if (canPlay(g, detail::ORDER[i])) return detail::ORDER[i];
      return -1;
    }
    int pick = (int)(rnd() % (uint32_t)total);
    for (int i = 0; i < n; i++) {
      if (pick < wt[i]) return cand[i];
      pick -= wt[i];
    }
    return cand[n - 1];
  }
  if (block >= 0) return block;
  // Iterative deepening: each finished depth's answer stands until a deeper
  // one finishes inside the budget.
  detail::Search s;
  s.budget = BUDGET[level];
  int best = -1;
  for (int d = 1; d <= DEPTH[level]; d++) {
    s.out = false;
    const int c = detail::bestAt(g, d, s);
    if (c < 0) break;
    best = c;
  }
  if (nodesOut) *nodesOut = s.nodes;
  if (best < 0)
    for (int i = 0; i < W; i++)
      if (canPlay(g, detail::ORDER[i])) return detail::ORDER[i];
  return best;
}

}  // namespace c4
