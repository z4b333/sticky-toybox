#include "hub.h"

#include <math.h>

#include "tools/decor.h"

#include "applist.h"
#include "appvis.h"
#include "tools/tool_icons.h"

namespace {
// A three-column icon grid with a heading over each group: the recognisable
// icons and roomy targets of a home screen, plus the one thing a bare grid
// cannot tell you — what an app is actually for.
//
// The numbers behind the shape. At this panel's 235 DPI a row band is
// 155 x 110 px, or 17 x 12 mm, comfortably over the ~7 mm a fingertip needs.
// A two-column list was tried first and read beautifully, but two columns
// instead of three plus taller rows dropped a page from eighteen apps to
// fourteen and its 8 mm rows sat flush against each other. Labels cost three
// slots here rather than four, and cost nothing in tap accuracy.
constexpr int COLS = 3;
constexpr int TILE_W = 140, GRID_X0 = 15, STEP_X = 155;
constexpr int TOP = 58, ROW_H = 110, HEAD_H = 32, GROUP_GAP = 10, ICON = 56;

// The gear sits in the title row, where it cannot be hidden by the very setting
// it leads to. Its tap band is the whole top-right corner -- 80 x 58 px, or
// 8.5 x 6.2 mm -- so the small drawn glyph is not a small target.
constexpr int GEAR_W = 80, GEAR_H = TOP;
constexpr int GEAR_X = SCREEN_W - GEAR_W;

// The way out, mirrored into the opposite corner and only drawn when there is
// somewhere to go. Same corner as every app's "< HUB", so back is always in one
// place and always one level at a time.
constexpr int EXIT_W = 120, EXIT_H = TOP;

static_assert(appvis::GAMES == gicons::COUNT, "game count out of step with appvis");
static_assert(appvis::TOOLS == ticons::COUNT, "tool count out of step with appvis");

using applist::GROUPS;
using applist::Group;
using applist::Item;
using applist::NGROUPS;

// One walk of the layout, shared by drawing and by hit-testing, so the two can
// never disagree about where a tile is. Hidden apps are dropped here and the
// rest close up behind them; a group with nothing left in it takes no heading
// and no space at all, rather than leaving a labelled hole.
//   f(item, group, headTop, rowTop, col, firstRow, lastRow)
template <typename F>
void walkLayout(F f) {
  int y = TOP;
  for (int g = 0; g < NGROUPS; g++) {
    const Group& grp = GROUPS[g];
    Item vis[6];
    int n = 0;
    for (int i = 0; i < grp.n; i++)
      if (appvis::visible(grp.items[i].game, grp.items[i].idx)) vis[n++] = grp.items[i];
    if (n == 0) continue;

    const int contentTop = y + HEAD_H;
    const int rows = (n + COLS - 1) / COLS;
    for (int i = 0; i < n; i++) {
      const int col = i % COLS, row = i / COLS;
      f(vis[i], grp, y, contentTop + row * ROW_H, col, row == 0, row == rows - 1);
    }
    y = contentTop + rows * ROW_H + GROUP_GAP;
  }
}

// Tiles are drawn without borders, so a tap takes the whole band it lands in --
// the gaps between columns, the heading strip and the space under a group all
// belong to the nearest tile rather than being dead pixels.
bool bandHit(int x, int y, int headTop, int rowTop, int col, bool firstRow, bool lastRow) {
  const int half = (STEP_X - TILE_W) / 2;
  const int left = (col == 0) ? 0 : GRID_X0 + col * STEP_X - half;
  const int right = (col == COLS - 1) ? SCREEN_W : GRID_X0 + (col + 1) * STEP_X - half;
  if (x < left || x >= right) return false;
  const int top = firstRow ? headTop : rowTop;
  const int bottom = rowTop + ROW_H + (lastRow ? GROUP_GAP : 0);
  return y >= top && y < bottom;
}
}  // namespace

namespace {
// A small outlined cell with a fill proportional to charge, and a nub on the
// right. Drawn only when a gauge answered: on a device without one, an empty
// battery outline would be a lie.
void drawBattery(const ToolsHost& host, ToolsCanvas& c) {
  const int pct = host.batteryPercent();
  if (pct < 0) return;
  const int w = 34, h = 16, x = c.width() - 20 - w, y = 768;
  c.drawRect(x, y, w, h, 2, true);
  c.fillRect(x + w, y + 5, 3, 6, true);           // the nub
  const int fill = ((w - 6) * (pct < 0 ? 0 : pct > 100 ? 100 : pct)) / 100;
  if (fill > 0) c.fillRect(x + 3, y + 3, fill, h - 6, true);
  if (host.charging()) {
    // A bolt over the cell, so charging reads at a glance even at 100%.
    decor::triangle(c, x + w / 2 + 3, y - 2, x + w / 2 - 4, y + h / 2 + 1,
                    x + w / 2 + 2, y + h / 2 + 1, true);
    decor::triangle(c, x + w / 2 - 3, y + h + 2, x + w / 2 + 4, y + h / 2 - 1,
                    x + w / 2 - 2, y + h / 2 - 1, true);
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  c.text(x - 6 - c.textWidth(buf, TS_SMALL), y + 1, buf, TS_SMALL, true);
}
}  // namespace

void HubScreen::render(ToolsHost& host, ToolsCanvas& c) {

  c.textTrackedCentered(SCREEN_W / 2, 12, "TOYBOX", TS_HUGE, true, false, 3);
  decor::gear(c, GEAR_X + GEAR_W / 2, 28, 19, 8, true);
  if (host.canExit()) c.button(8, 8, EXIT_W - 16, 40, "< BACK", false, TS_MED);

  if (appvis::shown() == 0) {
    c.textTrackedCentered(SCREEN_W / 2, 360, "every app is hidden", TS_LARGE, true, false, 2);
    c.textCentered(SCREEN_W / 2, 400, "tap the gear to bring some back", TS_MED, true);
    c.text(20, 770, "hold OK 2s = power off", TS_SMALL, true);
  drawBattery(host, c);
    return;
  }

  walkLayout([&c](const Item& it, const Group& grp, int headTop, int rowTop, int col,
                  bool firstRow, bool) {
    // The heading belongs to the walk too, so a group can never be titled in
    // one place and laid out in another.
    if (firstRow && col == 0) {
      c.textTracked(GRID_X0, headTop, grp.name, TS_MED, true, false, 1);
      c.fillRect(GRID_X0, headTop + 22, SCREEN_W - 2 * GRID_X0, 1, true);
    }
    const int cx = GRID_X0 + col * STEP_X + TILE_W / 2;
    const int mid = rowTop + ROW_H / 2;
    if (it.game)
      gicons::draw(c, it.idx, cx, mid - 16, ICON);
    else
      ticons::draw(c, it.idx, cx, mid - 16, ICON);
    const char* name = it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx];
    // A tile fits eight characters at this size; shrink rather than abbreviate.
    const TSize sz = c.textWidth(name, TS_MED) <= TILE_W ? TS_MED : TS_SMALL;
    c.textCentered(cx, mid + 26, name, sz, true);
  });

  c.text(20, 770, "hold OK 2s = power off", TS_SMALL, true);
  drawBattery(host, c);
}

HubScreen::Tap HubScreen::hit(const ToolsHost& host, int x, int y) const {
  Tap t;
  if (inRect(x, y, GEAR_X, 0, GEAR_W, GEAR_H)) {
    t.kind = Tap::Settings;
    return t;
  }
  if (host.canExit() && inRect(x, y, 0, 0, EXIT_W, EXIT_H)) {
    t.kind = Tap::Exit;
    return t;
  }

  bool found = false;
  Item got{};
  walkLayout([&](const Item& it, const Group&, int headTop, int rowTop, int col, bool first,
                 bool last) {
    if (found || !bandHit(x, y, headTop, rowTop, col, first, last)) return;
    found = true;
    got = it;
  });
  if (!found) return t;
  t.kind = Tap::App;
  t.game = got.game;
  t.idx = got.idx;
  return t;
}
