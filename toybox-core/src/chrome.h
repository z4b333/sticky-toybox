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

// v2 chrome (kit "Toybox v2 UI", 21 Nov 2026). Back is a peeled paper corner
// with an arrow on its black underside -- no word beside it -- and the bar is
// plain white with no rule under it (owner, 21 Nov: "remove dot line from
// every top bar"), so the page and its title read as one sheet. The bar keeps its 40 px height for now: every app below it is
// laid out against 40, and those layouts move to the kit's 52 one app at a
// time. The corner is scaled from the kit's 64-unit drawing.
namespace chrome2 {
// The corner, top-left, `s` px square: the black underside, the lifted flap,
// its crease, and a white back arrow on the black.
inline void peel(ToolsCanvas& c, int s) {
  auto P = [s](int v) { return (v * s + 32) / 64; };
  decor::triangle(c, 0, 0, P(60), 0, 0, P(60), true);
  // The flap: the folded-over paper, white, lying across the corner.
  decor::triangle(c, P(60), 0, P(45), P(38), 0, P(60), false);
  decor::triangle(c, P(45), P(38), P(38), P(45), 0, P(60), false);
  c.drawLine(P(60), 0, P(45), P(38), 2, true);
  c.drawLine(P(45), P(38), P(38), P(45), 2, true);
  c.drawLine(P(38), P(45), 0, P(60), 2, true);
  c.drawLine(P(60), 0, 0, P(60), 1, true);
  c.drawLine(P(41), P(33), P(33), P(41), 1, true);
  // The arrow, white on the black underside.
  const int t = s >= 40 ? 3 : 2;
  c.drawLine(P(21), P(9), P(10), P(20), t, false);
  c.drawLine(P(10), P(20), P(21), P(31), t, false);
  c.drawLine(P(10), P(20), P(32), P(20), t, false);
}
// A 2 px dotted rule: 2x2 dots on a 4 px pitch.
inline void dottedRule(ToolsCanvas& c, int x, int y, int w) {
  for (int i = 0; i + 2 <= w; i += 4) c.fillRect(x + i, y, 2, 2, true);
}
// The three-dot More mark (the screen's own menu, also hold UP).
inline void moreDots(ToolsCanvas& c, int cx, int cy, bool black = true) {
  for (int k = -1; k <= 1; k++) c.fillCircle(cx + k * 8, cy, 2, black);
}

// A system banner (kit "Banners"): a full-width white strip across the
// middle of whatever is on the panel, ruled 3 px top and bottom, with a
// small tracked label, a bold line, and an optional quiet line. The caller
// fades the screen behind first (ToolsHost::fadeBehind). Returns the strip's
// bottom edge, for a caller that adds a button under the words.
enum BannerIcon : uint8_t { BI_NONE, BI_POWER, BI_BATTERY, BI_DOWNLOAD, BI_WORKING };
inline int banner(ToolsCanvas& c, int cy, BannerIcon icon, const char* label, const char* title,
                  const char* sub = nullptr, int extraH = 0, int batteryPct = -1) {
  const int w = c.width();
  const int lh = c.textHeight(TS_SMALL), th = c.textHeight(TS_LARGE), sh = c.textHeight(TS_SMALL);
  const int body = lh + 4 + th + (sub ? 8 + sh : 0);
  const int h = 28 + body + 28 + extraH;
  const int top = cy - h / 2;
  c.fillRect(0, top - 6, w, h + 12, false);
  c.fillRect(0, top, w, 3, true);
  c.fillRect(0, top + h - 3, w, 3, true);
  int tx = 28;
  const int iy = top + 28 + (lh + 4 + th) / 2;
  switch (icon) {
    case BI_POWER:
      c.drawCircle(46, iy + 2, 16, 3, true);
      c.fillRect(42, iy - 22, 8, 16, false);
      c.fillRect(44, iy - 20, 3, 20, true);
      tx = 82;
      break;
    case BI_BATTERY: {
      c.drawRect(28, iy - 14, 56, 28, 3, true);
      c.fillRect(84, iy - 6, 5, 12, true);
      const int fill = batteryPct < 0 ? 0 : (48 * batteryPct + 50) / 100;
      if (fill > 0) c.fillRect(32, iy - 10, fill < 4 ? 4 : fill, 20, true);
      tx = 104;
      break;
    }
    case BI_DOWNLOAD:
      c.drawLine(46, iy - 18, 46, iy + 6, 3, true);
      c.drawLine(36, iy - 4, 46, iy + 6, 3, true);
      c.drawLine(56, iy - 4, 46, iy + 6, 3, true);
      c.fillRect(30, iy + 12, 33, 3, true);
      tx = 82;
      break;
    case BI_WORKING:
      c.fillRect(30, iy - 16, 14, 14, true);
      c.drawRect(48, iy - 16, 14, 14, 2, true);
      c.drawRect(30, iy + 2, 14, 14, 2, true);
      c.fillRect(48, iy + 2, 14, 14, true);
      tx = 82;
      break;
    default:
      break;
  }
  int y = top + 28;
  if (label && label[0]) c.textTracked(tx, y, label, TS_SMALL, true, false, 3);
  y += lh + 4;
  c.textClipped(tx, y, w - tx - 20, title, TS_LARGE, true, true);
  y += th;
  if (sub && sub[0]) c.textClipped(28, y + 8, w - 56, sub, TS_SMALL, true);
  return top + h;
}
// The kit's Confirm (owner rule: the first tap arms it, the second does it).
// An armed control turns black and asks: the question at the left, "tap
// again" at the right; on a narrow one the two stack in the middle. Any tap
// elsewhere disarms it, which each screen already does.
inline void armed(ToolsCanvas& c, const TRect& r, const char* question) {
  c.fillRect(r.x, r.y, r.w, r.h, true);
  const int qw = c.textWidth(question, TS_MED, true), aw = c.textWidth("tap again", TS_MED, true);
  if (qw + aw + 48 <= r.w) {
    const int ty = r.y + (r.h - c.textHeight(TS_MED)) / 2;
    c.text(r.x + 16, ty, question, TS_MED, false, true);
    c.text(r.x + r.w - 16 - aw, ty, "tap again", TS_MED, false, true);
    return;
  }
  const int lh = c.textHeight(TS_SMALL);
  if (r.h >= 2 * lh + 10) {
    const int ty = r.y + (r.h - 2 * lh - 2) / 2;
    c.textCentered(r.x + r.w / 2, ty, question, TS_SMALL, false, true);
    c.textCentered(r.x + r.w / 2, ty + lh + 2, "tap again", TS_SMALL, false);
  } else {
    c.textInBox(r.x, r.y, r.w, r.h, "tap again", TS_SMALL, false, true);
  }
}
}  // namespace chrome2

// backLabel is kept for the callers that pass one (a series shelf names the
// level it climbs to); v2 draws the corner alone, so it is not shown.
inline void drawTopBar(ToolsCanvas& c, const char* title, bool withHelp = false,
                       const char* backLabel = "HUB", const char* corner = nullptr) {
  (void)backLabel;
  const int w = c.width();
  c.fillRect(0, 0, w, TOPBAR_H, false);
  chrome2::peel(c, TOPBAR_H);

  // Only drawn where something is actually behind it, so a "?" always means
  // "there are rules here" rather than sometimes doing nothing.
  if (withHelp)
    c.textCentered(w - HELP_W / 2, (TOPBAR_H - c.textHeight(TS_MED)) / 2, "?", TS_MED, true);
  // The far corner, for a screen that has something short and true to say
  // there -- the clock, so far.
  else if (corner && corner[0]) {
    const int cw = c.textWidth(corner, TS_SMALL);
    if (cw <= BACK_W - 16)
      c.text(w - 16 - cw, (TOPBAR_H - c.textHeight(TS_SMALL)) / 2, corner, TS_SMALL, true);
  }

  // The title, tracked caps, centred and clear of both corners. A Thai title
  // keeps its size and gives up tracking instead.
  const int room = w - 2 * BACK_W - 8;
  TSize sz = scriptFloor(title, TS_MED);
  int sp = 2;
  if (c.textTrackedWidth(title, sz, true, sp) > room) sp = 0;
  if (c.textTrackedWidth(title, sz, true, sp) > room && sz == TS_MED) {
    sz = TS_SMALL;
    sp = 1;
  }
  if (c.textTrackedWidth(title, sz, true, sp) > room)
    c.textClipped(BACK_W + 4, (TOPBAR_H - c.textHeight(sz)) / 2, room, title, sz, true, true);
  else
    c.textTrackedCentered(w / 2, (TOPBAR_H - c.textHeight(sz)) / 2, title, sz, true, true, sp);
}

inline bool tappedBack(int x, int y) { return tHit(x, y, 0, 0, BACK_W, BAR_TOUCH_H); }
inline bool tappedHelp(int x, int y, int screenW) {
  return tHit(x, y, screenW - HELP_W, 0, HELP_W, BAR_TOUCH_H);
}

inline bool inRect(int px, int py, int x, int y, int w, int h) {
  return tHit(px, py, x, y, w, h);
}
