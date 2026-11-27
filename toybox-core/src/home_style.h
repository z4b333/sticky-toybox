// The home screen's four styles (v2 kit "Home"): Wallpaper (the default),
// Book collection, Calendar, Today. Chosen in setup and in Settings > LOOK >
// Home screen. Also the reading streak Book collection and Today show, and the
// small drawn thumbnails both choosers use.
#pragma once
#include <Preferences.h>
#include <stdint.h>
#include <stdlib.h>

#include "tools/cal_store.h"
#include "tools/tools_ui.h"

namespace homestyle {

enum : uint8_t { WALL = 0, BOOKS = 1, CALENDAR = 2, TODAY = 3, COUNT = 4 };
inline constexpr const char* KEY = "h_style";
inline const char* const NAMES[COUNT] = {"Wallpaper", "Book collection", "Calendar", "Today"};

inline uint8_t get(Preferences& p) {
  const uint8_t v = (uint8_t)p.getUInt(KEY, WALL);
  return v < COUNT ? v : WALL;
}
inline void set(Preferences& p, uint8_t v) { p.putUInt(KEY, v < COUNT ? v : WALL); }

// --- the reading streak -------------------------------------------------------
// Days in a row with a book opened. "rk_last" = the last such day (as a day
// number), "rk_run" = the run ending then, "rk_best" = the longest run, and
// "rk_week" = bits for the seven days ending at rk_last (bit 0 = that day).
inline void noteRead(Preferences& p, int y, int m, int d) {
  if (y < 2020) return;  // no clock yet: nothing honest to count
  const int32_t today = cal::dayNum(y, m, d);
  const int32_t last = p.getInt("rk_last", 0);
  int run = p.getInt("rk_run", 0);
  uint32_t week = p.getUInt("rk_week", 0);
  if (last == today) return;
  if (last > 0 && today - last == 1) run++;
  else run = 1;
  const int32_t gap = last > 0 ? today - last : 7;
  week = gap >= 7 ? 1 : ((week << gap) | 1) & 0x7F;
  p.putInt("rk_last", today);
  p.putInt("rk_run", run);
  p.putUInt("rk_week", week);
  if (run > p.getInt("rk_best", 0)) p.putInt("rk_best", run);
}
// The run as of today: a day missed (more than yesterday) breaks it.
inline int streak(Preferences& p, int y, int m, int d) {
  const int32_t last = p.getInt("rk_last", 0);
  if (last <= 0 || y < 2020) return 0;
  const int32_t today = cal::dayNum(y, m, d);
  return today - last <= 1 ? p.getInt("rk_run", 0) : 0;
}
inline int best(Preferences& p) { return p.getInt("rk_best", 0); }
// Was day `daysAgo` (0 = today) a reading day?
inline bool readOn(Preferences& p, int y, int m, int d, int daysAgo) {
  const int32_t last = p.getInt("rk_last", 0);
  if (last <= 0 || y < 2020) return false;
  const int32_t day = cal::dayNum(y, m, d) - daysAgo;
  const int32_t back = last - day;
  if (back < 0 || back >= 7) return false;
  return (p.getUInt("rk_week", 0) >> back) & 1;
}

// --- a style, drawn small -------------------------------------------------------
// For setup's Home screen step and the Settings page: a phone-shaped outline
// with the style's idea in it. `ink` false draws white on a black tile.
inline void thumb(ToolsCanvas& c, uint8_t style, int x, int y, int w, int h, bool ink) {
  c.drawRect(x, y, w, h, 2, ink);
  const int ix = x + 6, iw = w - 12;
  switch (style) {
    case WALL: {  // a sun over two mountains
      const int gy = y + h - 26;
      c.fillCircle(x + w * 2 / 3, y + h / 3, w / 9, !ink);
      c.drawCircle(x + w * 2 / 3, y + h / 3, w / 9, 2, ink);
      for (int k = 0; k < iw; k++) {
        const int px = ix + k;
        const int t1 = gy - (int)((iw / 2 - abs(k - iw / 3)) * 0.9f);
        const int t2 = gy - (int)((iw / 2 - abs(k - iw * 3 / 4)) * 0.7f);
        const int top = t1 < t2 ? t1 : t2;
        if (top < gy) c.fillRect(px, top, 1, gy - top, ink);
      }
      c.fillRect(ix, gy, iw, 6, ink);
      c.fillRect(ix + 4, y + 8, w / 4, h / 14, ink);
      break;
    }
    case BOOKS:  // a cover, a grid of dots, lines; a bar under
      c.drawRect(ix, y + 8, iw / 3 - 2, h / 3, 2, ink);
      for (int k = 0; k < h / 3; k += 6) c.drawLine(ix, y + 8 + k, ix + iw / 3 - 2, y + 8 + k + 10 > y + 8 + h / 3 ? y + 8 + h / 3 : y + 8 + k + 10, 1, ink);
      for (int r = 0; r < 3; r++)
        for (int q = 0; q < 2; q++) c.fillCircle(ix + iw / 3 + 10 + q * 10, y + 16 + r * 12, 2, ink);
      for (int r = 0; r < 3; r++) c.fillRect(ix + iw * 2 / 3 + 2, y + 12 + r * 12, iw / 3 - 4, 3, ink);
      c.fillRect(ix, y + 14 + h / 3, iw, 4, ink);
      c.fillRect(ix, y + h - 22, iw, 1, ink);
      break;
    case CALENDAR: {  // a month grid, one day filled
      const int gx = ix + 2, gy = y + 10, cw = (iw - 4) / 7, ch = cw;
      for (int r = 0; r < 5; r++)
        for (int q = 0; q < 7; q++) {
          if (r == 2 && q == 4) c.fillRect(gx + q * cw, gy + r * ch, cw - 2, ch - 2, ink);
          else c.drawRect(gx + q * cw, gy + r * ch, cw - 2, ch - 2, 1, ink);
        }
      c.fillRect(ix, y + h - 22, iw, 1, ink);
      break;
    }
    default: {  // a clock line, then four boxes, one dark
      c.text(ix + 2, y + 6, "17:24", TS_SMALL, ink, true);
      const int bw = (iw - 6) / 2, bh = h / 7, by = y + 34;
      for (int k = 0; k < 4; k++) {
        const int bx = ix + (k % 2) * (bw + 6), yy = by + (k / 2) * (bh + 6);
        if (k == 2) c.fillRect(bx, yy, bw, bh, ink);
        else c.drawRect(bx, yy, bw, bh, 1, ink);
      }
      c.fillRect(ix, y + h - 22, iw, 1, ink);
      break;
    }
  }
}

}  // namespace homestyle
