// The frame around every screen: the top bar, and the geometry its two buttons
// occupy. Drawn entirely through the canvas, so it belongs to the core rather
// than to either firmware.
#pragma once
#include "tools/decor.h"
#include "tools/tools_ui.h"

// Both hosts drive the same 3.97" panel, held portrait. The layouts here are
// tuned to these numbers rather than to a runtime size, which is why they read
// as pixel constants throughout.
constexpr int SCREEN_W = 480, SCREEN_H = 800;

constexpr int TOPBAR_H = 40;
constexpr int BACK_W = 110;
constexpr int HELP_W = 56;

// The two corner buttons are tapped more than anything else and sit where a
// thumb is least accurate, so their touch areas reach ten pixels below the bar
// they are drawn in. Nothing is drawn there; it is depth you cannot see.
//
// 50 is the ceiling, not a preference: several screens put their first row of
// controls at y=50, and a control inside this zone would become unreachable
// rather than merely hard to hit, because the back tap is tested first. The
// preview harness holds that line with static assertions.
constexpr int BAR_TOUCH_H = 50;

inline void drawTopBar(ToolsCanvas& c, const char* title, bool withHelp = false) {
  const int w = c.width();
  c.fillRect(0, 0, w, TOPBAR_H, false);
  c.fillRect(0, TOPBAR_H - 2, w, 2, true);
  // A chevron and the word, with no box around either. The bar is the only
  // chrome on the screen and the same two controls sit in the same two corners
  // on every screen, so a drawn button is a frame around something already
  // learned. The chevron does the work a box was doing: it is the one mark
  // people read as "back" without being told.
  const int cy = TOPBAR_H / 2 - 1;
  c.drawLine(22, cy, 32, cy - 9, 3, true);
  c.drawLine(22, cy, 32, cy + 9, 3, true);
  c.text(40, (TOPBAR_H - c.textHeight(TS_MED)) / 2, "HUB", TS_MED, true);

  // Only drawn where something is actually behind it, so a "?" always means
  // "there are rules here" rather than sometimes doing nothing.
  if (withHelp)
    c.textCentered(w - HELP_W / 2, (TOPBAR_H - c.textHeight(TS_MED)) / 2, "?", TS_MED, true);

  // Keep the centred title clear of the back button on the narrow portrait bar.
  // A Thai title cannot take the shrink-to-small escape -- below TS_LARGE it is
  // not readable at all -- so it starts at its floor and gives up tracking
  // instead of size when the bar gets tight.
  const int room = w - 2 * BACK_W - 8;
  TSize sz = scriptFloor(title, TS_MED);
  int sp = 2;
  if (c.textTrackedWidth(title, sz, false, sp) > room) sp = 0;
  if (c.textTrackedWidth(title, sz, false, sp) > room && sz == TS_MED) {
    sz = TS_SMALL;
    sp = 1;
  }
  c.textTrackedCentered(w / 2, (TOPBAR_H - c.textHeight(sz)) / 2, title, sz, true, false, sp);
  // No ornaments beside the title. There used to be a diamond either side, but
  // they squeezed out on long titles, so some bars had them and some did not --
  // a uniform nothing beats an occasional something.
}

inline bool tappedBack(int x, int y) { return tHit(x, y, 0, 0, BACK_W, BAR_TOUCH_H); }
inline bool tappedHelp(int x, int y, int screenW) {
  return tHit(x, y, screenW - HELP_W, 0, HELP_W, BAR_TOUCH_H);
}

inline bool inRect(int px, int py, int x, int y, int w, int h) {
  return tHit(px, py, x, y, w, h);
}
