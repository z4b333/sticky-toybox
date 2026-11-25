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
inline constexpr const char* MINES[] = {"ms_w0", "ms_w1", "ms_w2", "ms_l0", "ms_l1", "ms_l2",
                                        "ms_strk", "ms_best"};
inline constexpr const char* G2048[] = {"t_best", "t_tile"};
inline constexpr const char* XO[] = {"x_w", "x_l", "x_d", "x_strk", "x_best"};
inline constexpr const char* SUDOKU[] = {"sd_w0", "sd_w1", "sd_w2"};
inline constexpr const char* SHIPS[] = {"bs_w", "bs_l", "bs_strk", "bs_shots", "bs_hits"};
// C4
inline constexpr const char* FOURROW[] = {"c4_w0", "c4_w1", "c4_w2", "c4_l0", "c4_l1",
                                          "c4_l2", "c4_d0", "c4_d1", "c4_d2"};
inline constexpr const char* BLACKJACK[] = {"bj_w", "bj_l", "bj_p", "bj_best"};
inline constexpr const char* HILO[] = {"hl_best", "hl_runs"};
inline constexpr const char* CODE[] = {"cb_g0", "cb_g1", "cb_g2", "cb_w0", "cb_w1",
                                       "cb_w2", "cb_b0", "cb_b1", "cb_b2"};
inline constexpr const char* HANOI[] = {"hn_b3", "hn_b4", "hn_b5", "hn_b6", "hn_b7", "hn_won"};
inline constexpr const char* MEMORY[] = {"mp_w0", "mp_w1", "mp_w2", "mp_b0", "mp_b1", "mp_b2"};

// Removed, not zeroed, so a cleared record reads exactly like a new device's.
template <size_t N>
inline void clear(Preferences& p, const char* const (&keys)[N]) {
  for (size_t i = 0; i < N; i++) p.remove(keys[i]);
}

inline void clearAll(Preferences& p) {
  clear(p, COIN);
  clear(p, WORDLE);
  clear(p, MINES);
  clear(p, G2048);
  clear(p, XO);
  clear(p, SUDOKU);
  clear(p, SHIPS);
  clear(p, FOURROW);
  clear(p, BLACKJACK);
  clear(p, HILO);
  clear(p, CODE);
  clear(p, HANOI);
  clear(p, MEMORY);
}

// One button, two taps. The first only changes the label, so a stray tap on a
// destructive button costs nothing and says so.
inline constexpr int BTN_W = 214, BTN_H = 48;
inline const char* label(bool armed) { return armed ? "TAP AGAIN" : "CLEAR RECORD"; }

}  // namespace record
