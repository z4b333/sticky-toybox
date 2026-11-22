// A number pad: 1-9, clear, 0, Done, three across, under a shown value.
// Shared by the Calendar's time and Focus's minutes.
#pragma once
#include "tools_draw.h"

namespace numpad {
inline constexpr int Y = 300, KEY_H = 100, GAP = 8;
inline constexpr int CLEAR = 9, ZERO = 10, DONE = 11;
inline TRect key(int i) {  // 0..11
  const int w = (SCREEN_W - 32 - 2 * GAP) / 3;
  return TRect{16 + (i % 3) * (w + GAP), Y + (i / 3) * (KEY_H + GAP), w, KEY_H};
}
inline int hit(int x, int y) {
  for (int i = 0; i < 12; i++)
    if (key(i).hit(x, y)) return i;
  return -1;
}
// The digit a key types, or -1 for clear / Done.
inline int digit(int k) { return k == ZERO ? 0 : (k >= 0 && k < 9 ? k + 1 : -1); }
inline void draw(ToolsCanvas& c) {
  static const char* LAB[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "clear", "0", "Done"};
  for (int i = 0; i < 12; i++) {
    const TRect r = key(i);
    const bool dark = i == DONE;
    if (dark) c.fillRect(r.x, r.y, r.w, r.h, true);
    else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
    c.textInBox(r.x, r.y, r.w, r.h, LAB[i], i == CLEAR ? TS_MED : TS_LARGE, !dark, true);
  }
}
}  // namespace numpad
