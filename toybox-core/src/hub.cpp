#include "hub.h"

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
inline constexpr int FOLDER_PER = 6;

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

  const int step = ROW_STEP;
  const int bottom = FOLDER_BOTTOM;
  const int rows = (count + 1) / 2;
  const int block = rows * step;
  const int avail = bottom - FOLDER_TOP;
  int y0 = topAnchor ? FOLDER_TOP : FOLDER_TOP + (avail - block) / 2;
  if (y0 < FOLDER_TOP) y0 = FOLDER_TOP;

  for (int i = 0; i < count; i++) {
    const int col = i % 2, row = i / 2;
    const int cx = SCREEN_W / 4 + col * (SCREEN_W / 2);
    const int rowTop = y0 + row * step;
    f(vis[first + i], cx, rowTop + TILE / 2, col, rowTop, rowTop + step);
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

int stripTopFor(int cells) { return FOLDER_TOP + ((cells + 1) / 2) * ROW_STEP + REC_GAP; }

// The strip fits only when the tiles stop at two rows; the third row (hidden
// apps growing the + add cell) takes the space back.
bool stripFits(int cells) { return cells > 0 && cells <= 4; }

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

// The page arrows in a drawer's header corner, drawn (and hit) only when the
// drawer has more than one page of tiles. A guest's corner already carries
// the gear, so its pager sits just left of it.
TRect fpagePrevRect(bool guest) { return TRect{SCREEN_W - (guest ? 300 : 190), 4, 56, 56}; }
TRect fpageNextRect(bool guest) { return TRect{SCREEN_W - (guest ? 176 : 66), 4, 56, 56}; }

void drawFolderPager(ToolsCanvas& c, bool guest, int page, int pages) {
  const TRect pv = fpagePrevRect(guest), nx = fpageNextRect(guest);
  if (page > 0) c.text(pv.x + 18, pv.y + 12, "<", TS_LARGE, true, true);
  if (page < pages - 1) c.text(nx.x + 18, nx.y + 12, ">", TS_LARGE, true, true);
  char b[8];
  snprintf(b, sizeof(b), "%d/%d", page + 1, pages);
  c.textCentered((pv.x + nx.x + 56) / 2, 24, b, TS_SMALL, true);
}

void renderFolder(ToolsHost& host, ToolsCanvas& c, int folder, int page,
                  const recents::Entry* rec, int recN) {
  const bool guest = host.canExit();
  const Group& grp = GROUPS[folder];

  // The header, per the design: a back arrow, the count small in the far
  // corner, and the drawer's name big, left-aligned, in sentence case. For a
  // guest the back arrow leaves Toybox altogether, the corner carries the gear
  // (settings has no button hold to reach it by), and the count goes -- a
  // corner does one thing.
  c.drawLine(24, 34, 52, 34, 3, true);
  c.drawLine(24, 34, 36, 24, 3, true);
  c.drawLine(24, 34, 36, 44, 3, true);
  const int cellCount = folderCells(folder, guest);
  const int pages = folderPagesOf(cellCount);
  page = clampFolderPage(page, cellCount);
  if (guest) {
    decor::gear(c, SCREEN_W - 34, 34, 14, 8, true);
  } else if (pages <= 1) {
    const int n = visibleCount(folder);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d apps", n);
    c.text(SCREEN_W - 16 - c.textWidth(buf, TS_SMALL), 24, buf, TS_SMALL, true);
  }
  if (pages > 1) drawFolderPager(c, guest, page, pages);
  char title[16];
  sentence(title, sizeof(title), grp.name);
  c.text(24, 68, title, TS_HUGE, true, true);
  c.fillRect(16, 140, SCREEN_W - 32, 2, true);

  // Where the block of cells starts and ends, for the hairline dividers. The
  // walk is the authority; this only records what it did.
  const bool strip = folder == 2 && !guest && recN > 0 && pages == 1 && stripFits(cellCount);
  int top = SCREEN_H, bottom = 0, cells = 0;
  walkFolder(folder, guest, strip, page,
             [&](const Item& it, int cx, int cy2, int, int rowTop, int rowBottom) {
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
  drawDock(c, folder);
  if (strip) drawRecentStrip(c, stripTopFor(cellCount), rec, recN);
  if (cells == 0) return;

  // Hairline dividers between cells, not boxes around them.
  const int step = ROW_STEP;
  const int rows = (cells + 1) / 2;
  if (cells > 1) c.fillRect(SCREEN_W / 2, top, 1, bottom - top, true);
  for (int r = 1; r < rows; r++)
    c.fillRect(16, top + r * step, SCREEN_W - 32, 1, true);
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

  // The wordmark: each letter in its own small plate, the T and B plates
  // filled. Plates carry their own background, so no halo is needed.
  {
    static const char* L[6] = {"T", "O", "Y", "B", "O", "X"};
    static const bool FILLED[6] = {true, false, false, true, false, false};
    // The plates touch: a wordmark reads as one word, not six tiles.
    for (int i = 0; i < 6; i++) {
      const int bx = 16 + i * 26;
      c.fillRect(bx, 14, 26, 26, FILLED[i]);
      if (!FILLED[i]) c.drawRect(bx, 14, 26, 26, 2, true);
      c.textInBox(bx, 14, 26, 26, L[i], TS_SMALL, !FILLED[i], true);
    }
  }

  // Clock, then the percentage, right-aligned in that order. The clock only
  // exists when an RTC has been set; the loop ticks it with a partial refresh
  // once a minute while home is showing.
  hubmarks::haloed([&](int dx, int dy, bool black) {
    batteryFrame(c, host, SCREEN_W - 14 + dx, 18 + dy, black);
  });
  {
    int hh = 0, mm = 0;
    if (host.clockHHMM(hh, mm)) {
      char clk[8];
      snprintf(clk, sizeof(clk), "%02d:%02d", hh, mm);
      const int pctW = batteryWidth(c, host);
      const int x = SCREEN_W - 14 - pctW - (pctW ? 14 : 0) - c.textWidth(clk, TS_MED);
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
    if (guest && inRect(x, y, SCREEN_W - 100, 0, 100, 60)) {
      t.kind = Tap::Settings;  // the gear in the header corner
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

  // Home, which only a standalone device has. The dock is the only touch
  // surface on it.
  if (y >= DOCK_Y) {
    t.kind = Tap::Folder;
    t.idx = x / (SCREEN_W / 3);
    if (t.idx > 2) t.idx = 2;
    return t;
  }
  return t;
}
