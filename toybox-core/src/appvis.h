// Which apps appear on the hub.
//
// Thirteen apps is a lot to face at once, and most people use four or five.
// Hiding the rest is the difference between a home screen and a catalogue.
//
// Two rules this file exists to keep. Hiding never touches an app's saved data
// -- the coin tally, the flashcard decks, the notes and the streaks all sit
// exactly where they were, so unhiding restores the app whole. And the mask is
// only ever read by the hub: nothing else in the firmware asks whether an app
// is visible, so a hidden app that is somehow reached still works normally.
#pragma once
#include <Preferences.h>
#include <stdint.h>

namespace appvis {

constexpr int GAMES = 4;   // wordle, nonogram, 2048, xo
constexpr int TOOLS = 9;   // the tool shell's own apps, in ticons order
constexpr int COUNT = GAMES + TOOLS;
constexpr uint32_t ALL = (1u << COUNT) - 1;

// Games take the low bits and tools the rest, so the numbering is fixed by
// position rather than by hub order -- regrouping the hub must not silently
// hide someone's apps.
inline int bitOf(bool game, int idx) { return game ? idx : GAMES + idx; }

inline uint32_t g_mask = ALL;

inline bool visible(bool game, int idx) {
  return (g_mask & (1u << bitOf(game, idx))) != 0;
}

inline int shown() {
  int n = 0;
  for (int i = 0; i < COUNT; i++)
    if (g_mask & (1u << i)) n++;
  return n;
}

inline void set(bool game, int idx, bool on) {
  const uint32_t bit = 1u << bitOf(game, idx);
  if (on)
    g_mask |= bit;
  else
    g_mask &= ~bit;
}

inline void toggle(bool game, int idx) { g_mask ^= 1u << bitOf(game, idx); }

inline void load(Preferences& p) { g_mask = p.getUInt("vis", ALL) & ALL; }
inline void save(Preferences& p) { p.putUInt("vis", g_mask); }

}  // namespace appvis
