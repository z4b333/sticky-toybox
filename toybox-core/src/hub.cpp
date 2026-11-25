#include "hub.h"
#include "tools/cal_store.h"

#include "tools/book_thumbs.h"
#include "tools/recents.h"

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

// The calendar buffer for home's due popup: 2.5 KB, so static, not stack.
static cal::Store& calStore() {
  static cal::Store s;
  return s;
}

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
// Nothing is drawn for charging. The board has an LED beside the charging
// port that is already lit while it charges, and it is lit whether the panel
// is awake, asleep or showing something else -- which a mark on a screen that
// costs 1.7 s to redraw can never be. Two indicators for one fact means one of
// them is wrong at some point, and the one that can be wrong is this one.

// The number, and nothing else at all.
//
// There used to be a drawn cell with a bar in it beside the percentage, which
// is two ways of saying one thing -- and the drawn one is the vaguer of the
// two on 28 pixels of width. There used to be a charging bolt as well; the LED
// by the port says that, always, without a refresh.
int batteryWidth(ToolsCanvas& c, const ToolsHost& host) {
  const int pct = host.batteryPercent();
  if (pct < 0) return 0;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  return c.textWidth(buf, TS_SMALL);
}

void batteryFrame(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black) {
  const int pct = host.batteryPercent();
  if (pct < 0) return;  // no gauge answered: a number would be a lie
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  c.text(right - c.textWidth(buf, TS_SMALL), top - 1, buf, TS_SMALL, black);
}

void batteryFill(ToolsCanvas& c, const ToolsHost& host, int right, int top) {
  (void)c; (void)host; (void)right; (void)top;  // nothing to fill any more
}

// Kept as one call for the harness, which measures what home draws.
void battery(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black) {
  batteryFrame(c, host, right, top, black);
}

// --- the drawer pages --------------------------------------------------------
// One walk of the layout, shared by drawing and by hit-testing, so the two can
// never disagree about where a cell is. Hidden apps are dropped and the rest
// close up; when some are hidden, a "+ add" ghost cell follows the last app --
// the way back to them -- and it walks like any other cell, carrying ADD_IDX.
constexpr uint8_t ADD_IDX = 255;

//   f(item, cx, cy, col, rowTop, rowBottom)
// A guest drawer walks tighter (the dock takes the bottom of the page) and
// never grows the "+ add" cell -- the gear in its header is the way to the
// hidden apps, and dropping the cell is what caps a drawer at three rows.
// topAnchor pins the block under the title instead of centring it, which is
// how the Study drawer makes room for the recently-read strip below.
// Six tiles to a page; a drawer with more pages them behind < > arrows in
// its header corner. The page is clamped here rather than trusted, so a
// stale page (an app hidden while deep in the list) cannot show nothing.
inline constexpr int FOLDER_PER = 9;  // three rows of three

int folderPagesOf(int cells) { return cells <= 0 ? 1 : (cells + FOLDER_PER - 1) / FOLDER_PER; }

int clampFolderPage(int page, int cells) {
  const int last = folderPagesOf(cells) - 1;
  return page < 0 ? 0 : page > last ? last : page;
}

template <typename F>
void walkFolder(int folder, bool guest, bool topAnchor, int page, F f) {
  const Group& grp = GROUPS[folder];
  Item vis[13];
  int n = 0;
  for (int i = 0; i < grp.n; i++)
    if (appvis::visible(grp.items[i].game, grp.items[i].idx)) vis[n++] = grp.items[i];
  if (!guest && n < grp.n) vis[n++] = Item{false, ADD_IDX};
  if (n == 0) return;

  const int first = clampFolderPage(page, n) * FOLDER_PER;
  const int count = n - first > FOLDER_PER ? FOLDER_PER : n - first;

  (void)topAnchor;  // v2 grids always hang from the bar
  for (int i = 0; i < count; i++) {
    const int rowTop = hubui::cellTop(i);
    f(vis[first + i], hubui::cellCx(i), rowTop + TILE / 2, hubui::cellCol(i), rowTop,
      rowTop + ROW_STEP);
  }
}

// How many cells the drawer will lay out, and where the recently-read strip
// would start under them. Shared by drawing and hit-testing, like the walk.
int folderCells(int folder, bool guest) {
  const Group& grp = GROUPS[folder];
  int n = 0;
  for (int i = 0; i < grp.n; i++)
    if (appvis::visible(grp.items[i].game, grp.items[i].idx)) n++;
  if (!guest && n < grp.n) n++;
  return n;
}

int stripTopFor(int cells) { return FOLDER_TOP + ((cells + COLS - 1) / COLS) * ROW_STEP + REC_GAP; }

// The strip fits only when the tiles stop at two rows; the third row (hidden
// apps growing the + add cell) takes the space back.
bool stripFits(int cells) { return cells > 0 && cells <= 2 * COLS; }

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

// The dock: a hairline and three thin marks. On home it sits over a picture,
// so everything is haloed; a guest drawer reuses it unchanged on plain white,
// where the halo simply vanishes. `active` underlines the drawer being looked
// at; home passes -1 and no mark is set apart.
void drawDock(ToolsCanvas& c, int active) {
  // No rule above the dock. The marks are the dock: a line across the picture
  // was drawing a shelf for them to stand on, and the home screen is a
  // photograph with three marks on it, not a bar bolted to the bottom.
  for (int f = 0; f < NGROUPS; f++) {
    const int cx = (SCREEN_W / 6) + f * (SCREEN_W / 3);
    hubmarks::haloed([&](int dx, int dy, bool black) {
      hubmarks::folder(c, f, cx + dx, DOCK_Y + DOCK_H / 2 + 3 + dy, DOCK_ICON, black);
    });
    if (f == active) c.fillRect(cx - 14, SCREEN_H - 8, 28, 3, true);
  }
}

// The recently-read list: title rows, not covers. The dock owns the height
// the 96 px thumbnails wanted, and a title answers "which book" faster at
// arm's length than a stamp-sized cover ever did.
void drawRecentStrip(ToolsCanvas& c, int stripTop, const recents::Entry* rec, int recN) {
  c.textTracked(24, stripTop + 6, "CARRY ON READING", TS_SMALL, true, false, 1);
  c.fillRect(16, stripTop + 32, SCREEN_W - 32, 1, true);
  for (int i = 0; i < recN; i++) {
    const int y = stripTop + REC_HEAD_H + i * REC_ROW_H;
    c.textClipped(24, y + 8, SCREEN_W - 120, rec[i].title, TS_MED, true);
    const char* kind = rec[i].kind == recents::KIND_EPUB ? "epub" : "book";
    c.text(SCREEN_W - 24 - c.textWidth(kind, TS_SMALL), y + 14, kind, TS_SMALL, true);
    if (i + 1 < recN) c.fillRect(24, y + REC_ROW_H - 2, SCREEN_W - 48, 1, true);
  }
}

// The page turn, drawn (and hit) only when a drawer has more than one page
// of tiles (C4: PLAY's twelve). Under the grid, at the same place on every
// page, as two big targets a thumb finds without looking up at the header
// (owner, 25 Nov): three rows of tiles end at 610, the dock starts at 728.
inline constexpr int FPAGE_Y = hubui::FOLDER_TOP + 3 * hubui::ROW_STEP + 12;  // 622
TRect fpagePrevRect(bool) { return TRect{24, FPAGE_Y, 150, 76}; }
TRect fpageNextRect(bool) { return TRect{SCREEN_W - 24 - 150, FPAGE_Y, 150, 76}; }

void drawFolderPager(ToolsCanvas& c, bool guest, int page, int pages) {
  const TRect pv = fpagePrevRect(guest), nx = fpageNextRect(guest);
  // Bare bold arrows, no box (owner): the whole rect is still the target.
  if (page > 0) c.textInBox(pv.x, pv.y, pv.w, pv.h, "<<", TS_HUGE, true, true);
  if (page < pages - 1) c.textInBox(nx.x, nx.y, nx.w, nx.h, ">>", TS_HUGE, true, true);
  char b[8];
  snprintf(b, sizeof(b), "%d / %d", page + 1, pages);
  c.textCentered(SCREEN_W / 2, pv.y + (pv.h - c.textHeight(TS_MED)) / 2, b, TS_MED, true);
}

void renderFolder(ToolsHost& host, ToolsCanvas& c, int folder, int page,
                  const recents::Entry* rec, int recN) {
  const bool guest = host.canExit();
  const Group& grp = GROUPS[folder];

  // The header is the shared v2 bar with the drawer's name. A guest's back
  // corner leaves Toybox altogether and its far corner carries the gear.
  const int cellCount = folderCells(folder, guest);
  const int pages = folderPagesOf(cellCount);
  page = clampFolderPage(page, cellCount);
  // The bar keeps Back and More; the name is the page's heading, large.
  host.topBar("");
  c.textTrackedCentered(SCREEN_W / 2, FOLDER_HEAD_Y, grp.name, TS_HUGE, true, true, 6);
  if (guest)
    decor::gear(c, SCREEN_W - 30, TOPBAR_H / 2, 12, 8, true);
  else
    chrome2::moreDots(c, SCREEN_W - HELP_W / 2, TOPBAR_H / 2);  // More: settings (kit)
  if (pages > 1) drawFolderPager(c, guest, page, pages);

  const bool strip = folder == 2 && !guest && recN > 0 && pages == 1 && stripFits(cellCount);
  walkFolder(folder, guest, strip, page,
             [&](const Item& it, int cx, int, int col, int rowTop, int) {
    const int tx = GRID_X + col * (TILE_W + TILE_GAP);
    const int th = ROW_STEP - TILE_GAP;
    c.drawRect(tx, rowTop, TILE_W, th, 2, true);
    (void)cx;
    const int icx = tx + TILE_W / 2;
    if (it.idx == ADD_IDX && !it.game) {
      c.textCentered(icx, rowTop + th / 2 - 12, "+ add", TS_MED, true);
      return;
    }
    // Kit tile, owner's cut (25 Nov): a big line icon and the name in bold,
    // centred in the tile. No status line -- at one size the bold name and a
    // regular line under it read alike on this panel.
    char label[24];
    sentence(label, sizeof(label), it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx]);
    const int ih = TILE_ICON, lh = c.textHeight(TS_MED);
    const int top = rowTop + (th - (ih + 14 + lh)) / 2;
    if (it.game)
      gicons::draw(c, it.idx, icx, top + ih / 2, ih);
    else
      ticons::draw(c, it.idx, icx, top + ih / 2, ih);
    const TSize sz = c.textWidth(label, TS_MED, true) <= TILE_W - 10 ? TS_MED : TS_SMALL;
    c.textCentered(icx, top + ih + 14 + (lh - c.textHeight(sz)) / 2, label, sz, true, true);
  });
  drawDock(c, folder);
  if (strip) drawRecentStrip(c, stripTopFor(cellCount), rec, recN);
}
}  // namespace

#ifdef TOYBOX_HOST
void hubHostBattery(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black) {
  battery(c, host, right, top, black);
}
#endif

void HubScreen::openFolder(int f) {
  _folder = (int8_t)(f < 0 ? -1 : f >= NGROUPS ? NGROUPS - 1 : f);
  _fpage = 0;  // a drawer opens on its first page
}

void HubScreen::render(ToolsHost& host, ToolsCanvas& c) {
  // A guest has no home page at all: the drawers are its top level, so any
  // path that would have landed home lands on the first drawer instead.
  if (host.canExit() && _folder < 0) _folder = 0;
  if (_folder >= 0) {
    // The Study drawer's recently-read strip. Loaded here, at render, and the
    // count cached so the const hit() agrees with what is on the glass.
    recents::Entry rec[recents::MAX];
    int recN = 0;
    if (_folder == 2 && !host.canExit()) recN = recents::list(host.prefs(), rec);
    _recN = (int8_t)recN;
    renderFolder(host, c, _folder, _fpage, rec, recN);
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

  // The dock: the one part of home that answers touch. No bar behind it --
  // the design took the boxes away -- so everything on it is haloed against
  // the picture.
  drawDock(c, -1);

  // v2 home (kit "Home · Wallpaper"): the clock big in the top-left with the
  // date under it, the battery as a bare number top-right, and the More mark
  // on a white disc beside it -- the one way into settings by touch (hold UP
  // is the other). Everything over the picture is haloed. No wordmark and no
  // hint marks: the kit took them off.
  {
    int hh = 0, mm = 0;
    if (host.clockHHMM(hh, mm)) {
      char clk[8];
      snprintf(clk, sizeof(clk), "%02d:%02d", hh, mm);
      hubmarks::haloed(
          [&](int dx, int dy, bool black) { c.text(20 + dx, 14 + dy, clk, TS_HUGE, black, true); });
      int y = 0, mo = 0, d = 0;
      if (host.clockDate(y, mo, d) && mo >= 1 && mo <= 12) {
        static const char* MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        static const char* WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        // Sakamoto's day of the week.
        static const int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int yy = y - (mo < 3);
        const int wd = (yy + yy / 4 - yy / 100 + yy / 400 + T[mo - 1] + d) % 7;
        char date[24];
        snprintf(date, sizeof(date), "%s %d %s", WD[wd], d, MON[mo - 1]);
        hubmarks::haloed([&](int dx, int dy, bool black) {
          c.text(22 + dx, 14 + c.textHeight(TS_HUGE) + 2 + dy, date, TS_SMALL, black, true);
        });
      }
    }
  }
  // More: three dots on a white disc, no rim.
  c.fillCircle(hubui::MORE_X, hubui::MORE_Y, hubui::MORE_R, false);
  chrome2::moreDots(c, hubui::MORE_X, hubui::MORE_Y);
  // The battery: a bare number, a size up from the old corner, no cell.
  if (host.batteryPercent() >= 0) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", host.batteryPercent());
    const int px = hubui::MORE_X - hubui::MORE_R - 10 - c.textWidth(pct, TS_MED, true);
    const int py = hubui::MORE_Y - c.textHeight(TS_MED) / 2;
    hubmarks::haloed([&](int dx, int dy, bool black) { c.text(px + dx, py + dy, pct, TS_MED, black, true); });
  }
  drawDue(host, c);
}

// v2: a calendar item whose time has come (or is coming, for one set to pop
// up days ahead) sits over home in a banner until it is put off or done.
void HubScreen::drawDue(ToolsHost& host, ToolsCanvas& c) {
  _dueItem = -1;
  _dueBottom = 0;
  int y = 0, mo = 0, d = 0, hh = 0, mm = 0;
  if (!host.clockDate(y, mo, d) || !host.clockHHMM(hh, mm)) return;
  cal::Store& store = calStore();
  cal::load(store);
  int first = -1, count = 0, inDays = -1;
  uint32_t occ = 0;
  for (int i = 0; i < store.n; i++) {
    if (cal::snoozed() & (1ull << i)) continue;
    uint32_t k = 0;
    const int din = cal::dueIn(store.items[i], y, mo, d, hh, mm, k);
    if (din < 0) continue;
    if (first < 0) {
      first = i;
      occ = k;
      inDays = din;
    }
    count++;
  }
  if (first < 0) return;
  _dueItem = (int8_t)first;
  _dueOcc = occ;
  const cal::Item& it = store.items[first];
  char label[48], sub[24];
  char at[8] = "";
  if (!it.allDay) snprintf(at, sizeof(at), "  %02d:%02d", it.hour, it.minute);
  if (inDays == 0)
    snprintf(label, sizeof(label), it.allDay ? "TODAY" : "DUE%s", at);
  else if (inDays == 1)
    snprintf(label, sizeof(label), "TOMORROW%s", at);
  else
    snprintf(label, sizeof(label), "IN %d DAYS%s", inDays, at);
  if (count > 1) snprintf(sub, sizeof(sub), "1 of %d", count);
  host.fadeBehind();
  const int bottom = chrome2::banner(c, hubui::DUE_CY, chrome2::BI_NONE, label, it.title,
                                     count > 1 ? sub : nullptr, hubui::DUE_BTN_H + 16);
  _dueBottom = bottom;
  const TRect l = hubui::dueLater(bottom), r = hubui::dueDone(bottom);
  c.button(l.x, l.y, l.w, l.h, "Remind later", false);
  c.button(r.x, r.y, r.w, r.h, "Done", true);
}

HubScreen::Tap HubScreen::hit(const ToolsHost& host, int x, int y) const {
  Tap t;
  const bool guest = host.canExit();
  const int folder = (guest && _folder < 0) ? 0 : _folder;

  if (folder >= 0) {
    if (tappedBack(x, y)) {
      // The arrow means "up one level": home when there is one, and out of
      // Toybox altogether for a guest, whose drawers are the top.
      t.kind = guest ? Tap::Exit : Tap::Home;
      return t;
    }
    if (guest && inRect(x, y, SCREEN_W - 100, 0, 100, BAR_TOUCH_H)) {
      t.kind = Tap::Settings;  // the gear in the header corner
      return t;
    }
    if (!guest && inRect(x, y, SCREEN_W - HELP_W, 0, HELP_W, BAR_TOUCH_H)) {
      t.kind = Tap::Settings;  // More, top-right: the settings page (kit)
      return t;
    }
    if (y >= DOCK_Y) {
      t.kind = Tap::Folder;
      t.idx = x / (SCREEN_W / 3);
      if (t.idx > 2) t.idx = 2;
      return t;
    }
    // The page arrows, when the drawer has pages to turn.
    const int cellCount = folderCells(folder, guest);
    const int pages = folderPagesOf(cellCount);
    const int page = clampFolderPage(_fpage, cellCount);
    if (pages > 1) {
      if (page > 0 && fpagePrevRect(guest).hit(x, y)) {
        t.kind = Tap::PagePrev;
        return t;
      }
      if (page < pages - 1 && fpageNextRect(guest).hit(x, y)) {
        t.kind = Tap::PageNext;
        return t;
      }
    }
    // The recently-read covers, when the Study drawer is wearing them.
    const bool strip =
        folder == 2 && !guest && _recN > 0 && pages == 1 && stripFits(cellCount);
    if (strip) {
      const int rowTop = stripTopFor(cellCount) + REC_HEAD_H;
      if (y >= rowTop && y < rowTop + _recN * REC_ROW_H) {
        t.kind = Tap::Recent;
        t.idx = (y - rowTop) / REC_ROW_H;
        return t;
      }
    }
    bool found = false;
    Item got{};
    walkFolder(folder, guest, strip, page,
               [&](const Item& it, int cx, int, int col, int rowTop, int rowBottom) {
                 if (found) return;
                 const int left = col * CELL_W;
                 if (x < left || x >= left + CELL_W) return;
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

  // The due popup, when one is up, answers first and takes every other tap.
  if (_dueItem >= 0) {
    if (hubui::dueLater(_dueBottom).hit(x, y)) t.kind = Tap::DueLater;
    else if (hubui::dueDone(_dueBottom).hit(x, y)) t.kind = Tap::DueDone;
    else t.kind = Tap::DueSwallow;
    return t;
  }
  // Home, which only a standalone device has: the dock, and the More disc.
  if (inRect(x, y, hubui::MORE_X - 36, 0, 72, hubui::MORE_Y + 36)) {
    t.kind = Tap::Settings;
    return t;
  }
  if (y >= DOCK_Y) {
    t.kind = Tap::Folder;
    t.idx = x / (SCREEN_W / 3);
    if (t.idx > 2) t.idx = 2;
    return t;
  }
  return t;
}
