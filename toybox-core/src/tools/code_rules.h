// Codebreaker (C4): find the hidden row of symbols in ten tries. After each
// guess, a solid peg for every symbol in the right place and a ring for every
// other one that is in the code but somewhere else.
//
// Easy: four slots, no symbol twice. Normal: four slots, repeats allowed.
// Hard: five slots, repeats allowed. Six symbols throughout (game_symbols.h
// draws them). No display here, so the host test can check the scoring --
// including the repeated-symbol cases the classic game is known for.
#pragma once
#include <stdint.h>
#include <string.h>

namespace cb {

constexpr int SYMS = 6;
constexpr int TRIES = 10;
constexpr int MAXS = 5;
constexpr int LEVELS = 3;
constexpr uint8_t EMPTY = 0xFF;
inline const char* levelName(int l) { return l == 0 ? "Easy" : l == 1 ? "Normal" : "Hard"; }
inline int slotsFor(int level) { return level == 2 ? 5 : 4; }
inline bool repeatsFor(int level) { return level > 0; }

struct Game {
  uint8_t secret[MAXS];
  uint8_t guess[TRIES][MAXS];
  uint8_t exact[TRIES];  // solid pegs
  uint8_t near[TRIES];   // rings
  uint8_t slots;
  uint8_t level;
  uint8_t row;   // the guess being made (== TRIES once they are all used)
  uint8_t done;  // 0 playing, 1 broken, 2 out of tries
};

// Scores one guess against the code: exact matches first, then each remaining
// symbol counted as often as it is left over on both sides.
inline void score(const uint8_t* secret, const uint8_t* guess, int slots, int& exact, int& near) {
  exact = near = 0;
  int s[SYMS] = {}, g[SYMS] = {};
  for (int i = 0; i < slots; i++) {
    if (guess[i] == secret[i]) {
      exact++;
    } else {
      if (secret[i] < SYMS) s[secret[i]]++;
      if (guess[i] < SYMS) g[guess[i]]++;
    }
  }
  for (int k = 0; k < SYMS; k++) near += s[k] < g[k] ? s[k] : g[k];
}

template <typename Rnd>
inline void reset(Game& g, int level, Rnd rnd) {
  memset(&g, 0, sizeof(g));
  memset(g.guess, EMPTY, sizeof(g.guess));
  if (level < 0 || level >= LEVELS) level = 1;
  g.level = (uint8_t)level;
  g.slots = (uint8_t)slotsFor(level);
  bool used[SYMS] = {};
  for (int i = 0; i < g.slots; i++) {
    int k;
    do {
      k = (int)(rnd() % SYMS);
    } while (!repeatsFor(level) && used[k]);
    used[k] = true;
    g.secret[i] = (uint8_t)k;
  }
}

inline bool rowFull(const Game& g) {
  if (g.row >= TRIES) return false;
  for (int i = 0; i < g.slots; i++)
    if (g.guess[g.row][i] == EMPTY) return false;
  return true;
}

// Easy forbids a symbol twice in a guess too, so a guess can never say
// something the code cannot.
inline bool allowed(const Game& g, int slot, int sym) {
  if (repeatsFor(g.level)) return true;
  for (int i = 0; i < g.slots; i++)
    if (i != slot && g.guess[g.row][i] == sym) return false;
  return true;
}

// Locks in the current row. False if it is not complete or the game is over.
inline bool submit(Game& g) {
  if (g.done || !rowFull(g)) return false;
  int e, n;
  score(g.secret, g.guess[g.row], g.slots, e, n);
  g.exact[g.row] = (uint8_t)e;
  g.near[g.row] = (uint8_t)n;
  g.row++;
  if (e == g.slots)
    g.done = 1;
  else if (g.row >= TRIES)
    g.done = 2;
  return true;
}

}  // namespace cb
