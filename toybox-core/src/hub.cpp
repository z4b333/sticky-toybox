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

// --- the drawer pages --------------------------------------------------------
// One walk of the layout, shared by drawing and by hit-testing, so the two can
// never disagree about where a cell is. Hidden apps are dropped and the rest
// close up; when some are hidden, a "+ add" ghost cell follows the last app --
// the way back to them -- and it walks like any other cell, carrying ADD_IDX.
constexpr uint8_t ADD_IDX = 255;

//   f(item, cx, cy, col, rowTop, rowBottom)
template <typename F>
void walkFolder(int folder, F f) {
  const Group& grp = GROUPS[folder];
  Item vis[7];
  int n = 0;
  for (int i = 0; i < grp.n; i++)
    if (appvis::visible(grp.items[i].game, grp.items[i].idx)) vis[n++] = grp.items[i];
  if (n < grp.n) vis[n++] = Item{false, ADD_IDX};
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

// Sentence case, the design's register: "Wordle", "Flashcards". A name of one
// or two letters (XO) is an initialism and keeps its capitals; digits pass
// through untouched.
void sentence(char* out, size_t cap, const char* in) {
  const size_t len = strlen(in);
  if (len <= 2) {
    strncpy(out, in, cap - 1);
    out[cap - 1] = 0;
    return;
  }
  size_t i = 0;
  for (; in[i] && i < cap - 1; i++) {
    const char ch = in[i];
    out[i] = (i > 0 && ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
  }
  out[i] = 0;
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

  // The header, per the design: a back arrow, the count small in the far
  // corner, and the drawer's name big, left-aligned, in sentence case.
  c.drawLine(24, 34, 52, 34, 3, true);
  c.drawLine(24, 34, 36, 24, 3, true);
  c.drawLine(24, 34, 36, 44, 3, true);
  const int n = visibleCount(folder);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d apps", n);
  c.text(SCREEN_W - 16 - c.textWidth(buf, TS_SMALL), 24, buf, TS_SMALL, true);
  char title[16];
  sentence(title, sizeof(title), grp.name);
  c.text(24, 68, title, TS_HUGE, true, true);
  c.fillRect(16, 140, SCREEN_W - 32, 2, true);

  // Where the block of cells starts and ends, for the hairline dividers. The
  // walk is the authority; this only records what it did.
  int top = SCREEN_H, bottom = 0, cells = 0;
  walkFolder(folder, [&](const Item& it, int cx, int cy2, int, int rowTop, int rowBottom) {
    cells++;
    if (rowTop < top) top = rowTop;
    if (rowBottom > bottom) bottom = rowBottom;
    if (it.idx == ADD_IDX && !it.game) {
      c.textCentered(cx, cy2 + TILE / 2 - 14, "+ add", TS_MED, true);
      return;
    }
    // A touch smaller and lower than the cell's midpoint: the timer's crown
    // and the ship's mast reach above their nominal box, and the hairline
    // divider runs right along the row top.
    if (it.game)
      gicons::draw(c, it.idx, cx, cy2 + 8, TILE - 8);
    else
      ticons::draw(c, it.idx, cx, cy2 + 8, TILE - 8);
    char label[24];
    sentence(label, sizeof(label), it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx]);
    const TSize sz = c.textWidth(label, TS_MED) <= SCREEN_W / 2 - 24 ? TS_MED : TS_SMALL;
    c.textCentered(cx, cy2 + TILE / 2 + 26, label, sz, true);
  });
  if (cells == 0) return;

  // Hairline dividers between cells, not boxes around them.
  const int rows = (cells + 1) / 2;
  if (cells > 1) c.fillRect(SCREEN_W / 2, top, 1, bottom - top, true);
  for (int r = 1; r < rows; r++)
    c.fillRect(16, top + r * ROW_STEP, SCREEN_W - 32, 1, true);
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
    c.textCentered(SCREEN_W / 2, 412, "settings > wallpaper", TS_SMALL, true);
  }

  // The dock: a hairline and three thin marks, the one part of home that
  // answers touch. No bar behind it -- the design took the boxes away -- so
  // everything on it is haloed against the picture.
  hubmarks::haloed([&](int dx, int dy, bool black) {
    c.fillRect(0, DOCK_Y + dy, SCREEN_W, 2, black);
    (void)dx;
  });
  for (int f = 0; f < NGROUPS; f++) {
    const int cx = (SCREEN_W / 6) + f * (SCREEN_W / 3);
    hubmarks::haloed([&](int dx, int dy, bool black) {
      hubmarks::folder(c, f, cx + dx, DOCK_Y + DOCK_H / 2 + 3 + dy, DOCK_ICON, black);
    });
  }

  // The wordmark: each letter in its own small plate, the T and B plates
  // filled. Plates carry their own background, so no halo is needed.
  {
    static const char* L[6] = {"T", "O", "Y", "B", "O", "X"};
    static const bool FILLED[6] = {true, false, false, true, false, false};
    for (int i = 0; i < 6; i++) {
      const int bx = 16 + i * 30;
      c.fillRect(bx, 14, 26, 26, FILLED[i]);
      if (!FILLED[i]) c.drawRect(bx, 14, 26, 26, 2, true);
      c.textInBox(bx, 14, 26, 26, L[i], TS_SMALL, !FILLED[i], true);
    }
  }

  // Clock, percentage, cell, right-aligned in that order. The clock only
  // exists when an RTC has been set; the loop ticks it with a partial refresh
  // once a minute while home is showing.
  hubmarks::haloed([&](int dx, int dy, bool black) {
    batteryFrame(c, host, SCREEN_W - 14 + dx, 16 + dy, black);
  });
  batteryFill(c, host, SCREEN_W - 14, 16);
  {
    int hh = 0, mm = 0;
    if (host.clockHHMM(hh, mm)) {
      char clk[8];
      snprintf(clk, sizeof(clk), "%02d:%02d", hh, mm);
      int pctW = 0;
      if (host.batteryPercent() >= 0) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", host.batteryPercent());
        pctW = c.textWidth(pct, TS_MED) + 8;
      }
      const int x = SCREEN_W - 14 - 4 - 36 - pctW - 12 - c.textWidth(clk, TS_MED);
      hubmarks::haloed(
          [&](int dx, int dy, bool black) { c.text(x + dx, 13 + dy, clk, TS_MED, black); });
    }
  }
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
    if (!got.game && got.idx == ADD_IDX) {
      t.kind = Tap::Settings;  // the "+ add" cell: hidden apps live in settings
      return t;
    }
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
