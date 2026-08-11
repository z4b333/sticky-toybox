// The one list of what is on this device, and what each thing is for.
//
// The hub lays it out and the settings screen offers it as checkboxes; both
// read it from here. Kept in one place because two copies would drift, and a
// settings screen that disagrees with the hub about which app is which is
// worse than no settings screen at all.
#pragma once
#include <stdint.h>

namespace applist {

// Grouped by what an app is for, not by what it is built on -- Battleship is a
// game whether or not it happens to run on the tool shell.
struct Item {
  bool game;
  uint8_t idx;
};
struct Group {
  const char* name;
  Item items[6];
  uint8_t n;
};

inline constexpr Group GROUPS[] = {
    {"PLAY", {{true, 0}, {true, 1}, {true, 2}, {true, 3}, {false, 7}, {false, 8}}, 6},
    {"DECIDE", {{false, 0}, {false, 1}, {false, 3}, {false, 4}}, 4},
    // BOOKS first: the drawer was drawn around "the thing you were reading on
    // top", and the reader is that thing.
    {"STUDY", {{false, 9}, {false, 5}, {false, 6}, {false, 2}}, 4},
};
inline constexpr int NGROUPS = (int)(sizeof(GROUPS) / sizeof(GROUPS[0]));

}  // namespace applist
