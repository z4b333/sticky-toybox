#include "hub.h"

#include <math.h>

#include "applist.h"
#include "appvis.h"
#include "hub_marks.h"
#include "tools/decor.h"
#include "tools/lock_image.h"
#include "tools/tool_icons.h"
#include "tools/tools_draw.h"

namespace {
using namespace hubui;
using applist::GROUPS;
using applist::Group;
using applist::Item;
using applist::NGROUPS;

static_assert(appvis::GAMES == gicons::COUNT, "game count out of step with appvis");
static_assert(appvis::TOOLS == ticons::COUNT, "tool count out of step with appvis");
static_assert(NGROUPS == 3, "the dock draws exactly three folders");

// --- the battery, small, with its number -------------------------------------
// Drawn in two parts on the home screen, because the halo treatment that keeps
// the frame readable erases the fill: the final white pass paints the bar over
// its own black offsets, and a 99% battery comes out looking empty. So the
// frame, nub, bolt and number take the halo, and the bar itself is laid on
// afterwards as solid black inside a white rim -- readable over a white home,
// a black mountain, and everything between.
constexpr int BATT_W = 36, BATT_H = 18;

void batteryFrame(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black) {
  const int pct = host.batteryPercent();
  if (pct < 0) return;  // no gauge answered: an empty outline would be a lie
  const int x = right - 4 - BATT_W;  // 4 px of nub
  c.drawRect(x, top, BATT_W, BATT_H, 2, black);
  c.fillRect(x + BATT_W, top + 5, 4, 8, black);
  if (host.charging()) {
    decor::triangle(c, x + BATT_W / 2 + 3, top - 2, x + BATT_W / 2 - 4, top + BATT_H / 2 + 1,
                    x + BATT_W / 2 + 2, top + BATT_H / 2 + 1, black);
    decor::triangle(c, x + BATT_W / 2 - 3, top + BATT_H + 2, x + BATT_W / 2 + 4,
                    top + BATT_H / 2 - 1, x + BATT_W / 2 - 2, top + BATT_H / 2 - 1, black);
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  c.text(x - 8 - c.textWidth(buf, TS_MED), top - 3, buf, TS_MED, black);
}

void batteryFill(ToolsCanvas& c, const ToolsHost& host, int right, int top) {
  const int pct = host.batteryPercent();
  if (pct < 0) return;
  const int x = right - 4 - BATT_W;
  const int clamped = pct > 100 ? 100 : pct;
  const int fill = ((BATT_W - 8) * clamped) / 100;
  if (fill <= 0) return;
  c.fillRect(x + 3, top + 3, fill + 2, BATT_H - 6, false);  // the white rim
  c.fillRect(x + 4, top + 4, fill, BATT_H - 8, true);       // the bar
}

// The two halves together, plain, for the harness to measure.
void battery(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black) {
  batteryFrame(c, host, right, top, black);
  if (black) batteryFill(c, host, right, top);
}

// --- the folder pages --------------------------------------------------------
// One walk of the layout, shared by drawing and by hit-testing, so the two can
// never disagree about where a tile is. Hidden apps are dropped here and the
// rest close up; the block of rows is centred in the space the page has.
//   f(item, cx, cy, col, rowTop, rowBottom)
template <typename F>
void walkFolder(int folder, F f) {
  const Group& grp = GROUPS[folder];
  Item vis[6];
  int n = 0;
  for (int i = 0; i < grp.n; i++)
    if (appvis::visible(grp.items[i].game, grp.items[i].idx)) vis[n++] = grp.items[i];
  if (n == 0) return;

  const int rows = (n + 1) / 2;
  const int block = rows * ROW_STEP;
  const int avail = FOLDER_BOTTOM - FOLDER_TOP;
  int y0 = FOLDER_TOP + (avail - block) / 2;
  if (y0 < FOLDER_TOP) y0 = FOLDER_TOP;

  for (int i = 0; i < n; i++) {
    const int col = i % 2, row = i / 2;
    const int cx = SCREEN_W / 4 + col * (SCREEN_W / 2);
    const int rowTop = y0 + row * ROW_STEP;
    f(vis[i], cx, rowTop + TILE / 2, col, rowTop, rowTop + ROW_STEP);
  }
}

int visibleCount(int folder) {
  const Group& grp = GROUPS[folder];
  int n = 0;
  for (int i = 0; i < grp.n; i++)
    if (appvis::visible(grp.items[i].game, grp.items[i].idx)) n++;
  return n;
}

void renderFolder(ToolsHost& host, ToolsCanvas& c, int folder) {
  (void)host;
  const Group& grp = GROUPS[folder];

  // The header: a chevron home on the left, the folder's name in the middle.
  const int cy = 26;
  c.drawLine(22, cy, 32, cy - 9, 3, true);
  c.drawLine(22, cy, 32, cy + 9, 3, true);
  c.text(40, 14, "HOME", TS_MED, true);
  c.textTrackedCentered(SCREEN_W / 2, 10, grp.name, TS_LARGE, true, true, 2);
  c.fillRect(16, 56, SCREEN_W - 32, 2, true);

  const int n = visibleCount(folder);
  if (n == 0) {
    c.textTrackedCentered(SCREEN_W / 2, 360, "everything here is hidden", TS_LARGE, true, false, 1);
    c.textCentered(SCREEN_W / 2, 404, "bring apps back in settings", TS_MED, true);
    return;
  }

  walkFolder(folder, [&c](const Item& it, int cx, int cy2, int, int, int) {
    if (it.game)
      gicons::draw(c, it.idx, cx, cy2, TILE);
    else
      ticons::draw(c, it.idx, cx, cy2, TILE);
    const char* name = it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx];
    const TSize sz = c.textWidth(name, TS_MED) <= SCREEN_W / 2 - 24 ? TS_MED : TS_SMALL;
    c.textCentered(cx, cy2 + TILE / 2 + 18, name, sz, true);
  });

  char buf[48];
  snprintf(buf, sizeof(buf), "%d apps   tap HOME to go back", n);
  c.text(16, 768, buf, TS_SMALL, true);
}
}  // namespace

#ifdef TOYBOX_HOST
void hubHostBattery(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black) {
  battery(c, host, right, top, black);
}
#endif

void HubScreen::openFolder(int f) {
  _folder = (int8_t)(f < 0 ? -1 : f >= NGROUPS ? NGROUPS - 1 : f);
}

void HubScreen::render(ToolsHost& host, ToolsCanvas& c) {
  if (_folder >= 0) {
    renderFolder(host, c, _folder);
    return;
  }

  // --- home ------------------------------------------------------------------
  const bool wall = wallimg::draw(c);
  if (!wall) {
    // A blank home says how to make it not blank. Small, centred, and gone the
    // moment a picture arrives.
    c.textCentered(SCREEN_W / 2, 380, "this screen can show a picture", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 412, "settings > lock screen > send picture", TS_SMALL, true);
  }

  // The dock: the one part of home that answers touch.
  c.fillRect(0, DOCK_Y, SCREEN_W, DOCK_H, false);
  c.fillRect(0, DOCK_Y, SCREEN_W, 3, true);
  for (int f = 0; f < NGROUPS; f++) {
    const int cx = (SCREEN_W / 6) + f * (SCREEN_W / 3);
    hubmarks::folder(c, f, cx, DOCK_Y + DOCK_H / 2 + 1, DOCK_ICON, true);
  }

  // The overlays. On a bare white background the white strokes vanish and the
  // black edge carries the shape alone, which still reads.
  hubmarks::haloed([&](int dx, int dy, bool black) {
    c.textTracked(18 + dx, 10 + dy, "TOYBOX", TS_LARGE, black, true, 3);
  });
  hubmarks::haloed([&](int dx, int dy, bool black) {
    batteryFrame(c, host, SCREEN_W - 14 + dx, 16 + dy, black);
  });
  batteryFill(c, host, SCREEN_W - 14, 16);
  hubmarks::haloed([&](int dx, int dy, bool black) {
    decor::gear(c, HINT_X + dx, HINT_GEAR_Y + dy, HINT_R, 8, black);
  });
  hubmarks::haloed([&](int dx, int dy, bool black) {
    hubmarks::resume(c, HINT_X + dx, HINT_RESUME_Y + dy, HINT_R, black);
  });

  // A guest host may have no side buttons at all, so the two things the holds
  // do get plates when there is a host to go back to.
  if (host.canExit()) {
    c.fillRect(0, 0, 110, 46, false);
    c.drawRect(0, 0, 110, 46, 2, true);
    c.textCentered(55, 12, "< BACK", TS_MED, true);
    const int gx = SCREEN_W - 55;
    c.fillRect(SCREEN_W - 110, 50, 110, 46, false);
    c.drawRect(SCREEN_W - 110, 50, 110, 46, 2, true);
    decor::gear(c, gx, 73, 16, 8, true);
  }
}

HubScreen::Tap HubScreen::hit(const ToolsHost& host, int x, int y) const {
  Tap t;

  if (_folder >= 0) {
    if (tappedBack(x, y)) {
      t.kind = Tap::Home;
      return t;
    }
    bool found = false;
    Item got{};
    walkFolder(_folder, [&](const Item& it, int cx, int, int col, int rowTop, int rowBottom) {
      if (found) return;
      const int left = col == 0 ? 0 : SCREEN_W / 2;
      if (x < left || x >= left + SCREEN_W / 2) return;
      if (y < rowTop || y >= rowBottom) return;
      found = true;
      got = it;
    });
    if (!found) return t;
    t.kind = Tap::App;
    t.game = got.game;
    t.idx = got.idx;
    return t;
  }

  // Home. The dock is the only touch surface; the guest plates come first so
  // the dock cannot shadow them.
  if (host.canExit()) {
    if (inRect(x, y, 0, 0, 110, 50)) {
      t.kind = Tap::Exit;
      return t;
    }
    if (inRect(x, y, SCREEN_W - 110, 50, 110, 50)) {
      t.kind = Tap::Settings;
      return t;
    }
  }
  if (y >= DOCK_Y) {
    t.kind = Tap::Folder;
    t.idx = x / (SCREEN_W / 3);
    if (t.idx > 2) t.idx = 2;
    return t;
  }
  return t;
}
