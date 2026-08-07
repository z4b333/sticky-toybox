// What each app has saved about how you have done, and how to throw it away.
//
// Every app clears its own record from its own screen, next to where the
// numbers are shown -- that is the only place you can see what you are about to
// lose. The settings screen's global reset walks the same lists, so the two can
// never fall out of step, and nothing else on the device writes these keys.
//
// Preferences are deliberately absent: the dice you like, your timer length,
// the sudoku level, XO's opponent. Those say how you use the thing, not how
// well you did, and wiping them with a score is a surprise.
#pragma once
#include <Preferences.h>
#include <stddef.h>

namespace record {

inline constexpr const char* COIN[] = {"c_heads", "c_tails"};
inline constexpr const char* WORDLE[] = {"w_games", "w_wins", "w_streak", "w_max",
                                         "w_d1",    "w_d2",   "w_d3",     "w_d4",
                                         "w_d5",    "w_d6"};
inline constexpr const char* NONOGRAM[] = {"n5_best", "n10_best", "n5_solved", "n10_solved"};
inline constexpr const char* G2048[] = {"t_best", "t_tile"};
inline constexpr const char* XO[] = {"x_w", "x_l", "x_d", "x_strk", "x_best"};
inline constexpr const char* SUDOKU[] = {"sd_w0", "sd_w1", "sd_w2"};
inline constexpr const char* SHIPS[] = {"bs_w", "bs_l"};

// Removed, not zeroed, so a cleared record reads exactly like a new device's.
template <size_t N>
inline void clear(Preferences& p, const char* const (&keys)[N]) {
  for (size_t i = 0; i < N; i++) p.remove(keys[i]);
}

inline void clearAll(Preferences& p) {
  clear(p, COIN);
  clear(p, WORDLE);
  clear(p, NONOGRAM);
  clear(p, G2048);
  clear(p, XO);
  clear(p, SUDOKU);
  clear(p, SHIPS);
}

// One button, two taps. The first only changes the label, so a stray tap on a
// destructive button costs nothing and says so.
inline constexpr int BTN_W = 214, BTN_H = 48;
inline const char* label(bool armed) { return armed ? "TAP AGAIN" : "CLEAR RECORD"; }

}  // namespace record
