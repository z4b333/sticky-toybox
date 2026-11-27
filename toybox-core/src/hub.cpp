#include "hub.h"
#include "home_style.h"
#include "tools/big_digits.h"
#include "tools/book_thumbs.h"
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
  Item vis[17];
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

// --- home styles (kit "Home": Book collection, Calendar, Today) ------------------
namespace {
const char* const MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const MONTH[12] = {"January", "February", "March", "April", "May", "June",
                               "July", "August", "September", "October", "November", "December"};
const char* const WD3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const WDAY[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// The line along the top of a styled home: the time and the date on the left
// (or a label, for Today), the battery and More on the right. Plain white
// ground, so no halo.
void homeTop(ToolsHost& host, ToolsCanvas& c, const char* label) {
  int hh = 0, mm = 0, y = 0, mo = 0, d = 0;
  const bool clk = host.clockHHMM(hh, mm), date = host.clockDate(y, mo, d) && mo >= 1 && mo <= 12;
  if (label) {
    c.textTracked(16, 18, label, TS_SMALL, true, true, 3);
  } else if (clk) {
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", hh, mm);
    c.text(16, 8, t, TS_LARGE, true, true);
    if (date) {
      char ds[24];
      snprintf(ds, sizeof(ds), "%s %d %s", WD3[cal::weekday(y, mo, d)], d, MON[mo - 1]);
      c.text(16 + c.textWidth(t, TS_LARGE, true) + 12, 8 + c.textHeight(TS_LARGE) - c.textHeight(TS_SMALL) - 4,
             ds, TS_SMALL, true);
    }
  }
  chrome2::moreDots(c, hubui::MORE_X, hubui::MORE_Y - 8);
  if (host.batteryPercent() >= 0) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", host.batteryPercent());
    c.text(hubui::MORE_X - hubui::MORE_R - 10 - c.textWidth(pct, TS_SMALL), hubui::MORE_Y - 8 - c.textHeight(TS_SMALL) / 2,
           pct, TS_SMALL, true);
  }
}

// A book with no picture of its own: diagonal hatching and its title in a
// white label, the kit's stand-in cover.
void standInCover(ToolsCanvas& c, int x, int y, int w, int h, const char* title) {
  c.drawRect(x, y, w, h, 2, true);
  for (int k = -h; k < w; k += 9) {
    const int x0 = x + (k < 0 ? 0 : k), y0 = y + (k < 0 ? -k : 0);
    int len = w - (x0 - x);
    if (len > h - (y0 - y)) len = h - (y0 - y);
    if (len > 0) c.drawLine(x0, y0, x0 + len - 1, y0 + len - 1, 1, true);
  }
  const int lw = w - 16;
  c.fillRect(x + 6, y + 8, lw + 4, 26, false);
  c.drawRect(x + 6, y + 8, lw + 4, 26, 1, true);
  c.textClipped(x + 10, y + 11, lw - 4, title, TS_SMALL, true, true);
}

// The book's own cover when its small thumbnail is in flash (made on its first
// open), scaled to fit the box and framed; false leaves the stand-in to draw.
bool bookCover(ToolsCanvas& c, const char* file, int x, int y, int w, int h, bool frame = true) {
  static uint8_t bits[bthumb::BYTES];
  if (!file || !file[0] || !bthumb::load(file, bits)) return false;
  // Fit 96x160 into w x h, keeping its shape.
  int dw = w, dh = w * bthumb::H / bthumb::W;
  if (dh > h) {
    dh = h;
    dw = h * bthumb::W / bthumb::H;
  }
  const int ox = x + (w - dw) / 2, oy = y + (h - dh) / 2;
  c.fillRect(ox, oy, dw, dh, false);
  const int stride = bthumb::W / 8;
  for (int ty = 0; ty < dh; ty++) {
    const uint8_t* row = bits + (size_t)(ty * bthumb::H / dh) * stride;
    int run = -1;
    for (int tx = 0; tx <= dw; tx++) {
      bool ink = false;
      if (tx < dw) {
        const int sx = tx * bthumb::W / dw;
        ink = !(row[sx >> 3] & (0x80 >> (sx & 7)));  // 1 = white
      }
      if (ink && run < 0) run = tx;
      if (!ink && run >= 0) {
        c.fillRect(ox + run, oy + ty, tx - run, 1, true);
        run = -1;
      }
    }
  }
  if (frame) c.drawRect(ox, oy, dw, dh, 2, true);
  return true;
}

// The small home cover (104x174) at (x, y): the sharp one when it has been
// made, else the strip thumbnail fitted in, else the striped stand-in.
void smallCover(ToolsCanvas& c, const recents::Entry& e, int x, int y, const char* label) {
  const bthumb::Size& z = bthumb::HOME_SMALL;
  if (uint8_t* bits = (uint8_t*)malloc((size_t)bthumb::bytesOf(z))) {
    const bool got = bthumb::loadCover(z, e.file, bits);
    if (got) bthumb::inkCover(c, z, bits, x, y);
    free(bits);
    if (got) return;
  }
  if (!bookCover(c, e.file, x, y, z.w, z.h, false)) standInCover(c, x, y, z.w, z.h, label);
}

int wrapLines(ToolsCanvas& c, const char* s, TSize ts, bool bold, int maxW, char (*out)[64], int maxLines) {
  int n = 0;
  const char* p = s;
  while (*p && n < maxLines) {
    while (*p == ' ') p++;
    int len = 0, lastSp = -1;
    const char* q = p;
    while (*q && len < 62) {
      out[n][len] = *q;
      out[n][len + 1] = 0;
      if (c.textWidth(out[n], ts, bold) > maxW) break;
      if (*q == ' ') lastSp = len;
      len++;
      q++;
    }
    out[n][len] = 0;
    if (*q && lastSp > 0) {
      out[n][lastSp] = 0;
      q = p + lastSp + 1;
    }
    if (n == maxLines - 1 && *q) {  // the last line says there was more
      const size_t L = strlen(out[n]);
      if (L > 3) strcpy(out[n] + L - 3, "...");
    }
    p = q;
    n++;
  }
  return n;
}

void streakWeek(ToolsHost& host, ToolsCanvas& c, int x, int y, int boxW, int y2, int mo, int d) {
  static const char* const L[7] = {"S", "M", "T", "W", "T", "F", "S"};
  for (int k = 0; k < 7; k++) {
    const int ago = 6 - k;
    const int bx = x + k * (boxW + 6);
    const bool on = homestyle::readOn(host.prefs(), y2, mo, d, ago);
    if (on) c.fillRect(bx, y, boxW, 30, true);
    else c.drawRect(bx, y, boxW, 30, ago == 0 ? 3 : 2, true);
    const int32_t dn = cal::dayNum(y2, mo, d) - ago;
    int yy, mm2, dd;
    cal::fromDayNum(dn, yy, mm2, dd);
    c.textCentered(bx + boxW / 2, y + 36, L[cal::weekday(yy, mm2, dd)], TS_SMALL, true, ago == 0);
  }
}

void renderBooksHome(ToolsHost& host, ToolsCanvas& c, const recents::Entry* rec, int recN) {
  using namespace hubui;
  homeTop(host, c, nullptr);
  const TRect nb = booksNow();
  if (recN > 0) {
    const int cw = BOOKS_COVER_W;
    uint8_t* home = (uint8_t*)malloc(bthumb::HOME_BYTES);  // 8 KB, only while drawing
    if (home && bthumb::loadHome(rec[0].file, home)) {
      // The sharp one, at its own size: made from the full-size cover.
      bthumb::inkCover(c, bthumb::HOME_BIG, home, nb.x + (cw - bthumb::HOME_W) / 2, nb.y + (nb.h - bthumb::HOME_H) / 2);
    } else if (!bookCover(c, rec[0].file, nb.x, nb.y, cw, nb.h, false)) {
      standInCover(c, nb.x, nb.y, cw, nb.h, rec[0].title);
    }
    free(home);
    const int tx = nb.x + cw + 18, tw = nb.w - cw - 18;
    c.textTracked(tx, nb.y + 6, "READING NOW", TS_SMALL, true, false, 2);
    char lines[4][64];
    const int lh = c.textHeight(TS_MED) + 4;
    const int n = wrapLines(c, rec[0].title, TS_MED, true, tw, lines, 4);
    int ty = nb.y + 34;
    for (int i = 0; i < n; i++, ty += lh) c.text(tx, ty, lines[i], TS_MED, true, true);
    // Where it was left: the chapter, and how far through the whole book.
    ty += 14;
    if (rec[0].chap[0]) {
      c.textTracked(tx, ty, "CHAPTER", TS_SMALL, true, false, 2);
      ty += c.textHeight(TS_SMALL) + 4;
      char cl[3][64];
      const int m = wrapLines(c, rec[0].chap, TS_SMALL, false, tw, cl, 3);
      for (int i = 0; i < m; i++, ty += c.textHeight(TS_SMALL) + 2) c.text(tx, ty, cl[i], TS_SMALL, true);
      ty += 10;
    }
    if (rec[0].pct >= 0) {
      char pc[24];
      snprintf(pc, sizeof(pc), "%d%% of the book", rec[0].pct);
      c.text(tx, ty, pc, TS_MED, true, true);
      ty += c.textHeight(TS_MED) + 6;
      c.drawRect(tx, ty, tw, 14, 2, true);
      const int fill = (tw - 6) * rec[0].pct / 100;
      if (fill > 0) c.fillRect(tx + 3, ty + 3, fill, 8, true);
      ty += 24;
    }
    c.text(tx, nb.y + nb.h - c.textHeight(TS_SMALL), "tap to carry on", TS_SMALL, true);
  } else {
    c.textCentered(SCREEN_W / 2, nb.y + 60, "Nothing read yet", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, nb.y + 96, "open Books from the dock below", TS_SMALL, true);
  }
  // The others: up to four covers, no titles, and the way to all of them.
  const TRect al = booksAll();
  c.textTracked(16, al.y + 8, "ALSO READING", TS_SMALL, true, false, 2);
  c.text(al.x + al.w - c.textWidth("all books >", TS_SMALL), al.y + 8, "all books >", TS_SMALL, true, true);
  if (recN <= 1) c.text(16, BOOKS_ALSO_Y + 16, "one book at a time so far", TS_SMALL, true);
  for (int i = 1; i < recN && i <= 4; i++) {
    const TRect g = booksAlso(i - 1);
    smallCover(c, rec[i], g.x, g.y, rec[i].title);
  }
  // The streak.
  int y = 0, mo = 0, d = 0;
  if (host.clockDate(y, mo, d) && y >= 2020) {
    const int sy = BOOKS_STREAK_Y;
    chrome2::dottedRule(c, 16, sy, SCREEN_W - 32);
    c.textTracked(16, sy + 14, "STREAK", TS_SMALL, true, false, 2);
    const int run = homestyle::streak(host.prefs(), y, mo, d);
    char big[16], sub[24];
    snprintf(big, sizeof(big), run == 1 ? "%d day" : "%d days", run);
    snprintf(sub, sizeof(sub), "best %d", homestyle::best(host.prefs()));
    c.text(16, sy + 40, big, TS_LARGE, true, true);
    c.text(16, sy + 40 + c.textHeight(TS_LARGE) + 2, sub, TS_SMALL, true);
    streakWeek(host, c, 196, sy + 44, 32, y, mo, d);
  }
}

void renderCalendarHome(ToolsHost& host, ToolsCanvas& c, int off) {
  using namespace hubui;
  homeTop(host, c, nullptr);
  int y = 0, mo = 0, d = 0;
  if (!host.clockDate(y, mo, d) || y < 2020) {
    c.textCentered(SCREEN_W / 2, 300, "The clock is not set yet", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 336, "Settings > CONNECT > Clock", TS_SMALL, true);
    return;
  }
  int vy = y, vm = mo + off;
  while (vm < 1) { vm += 12; vy--; }
  while (vm > 12) { vm -= 12; vy++; }
  // The month and its arrows.
  char head[24];
  snprintf(head, sizeof(head), "%s %d", MONTH[vm - 1], vy);
  c.textCentered(SCREEN_W / 2, 62, head, TS_MED, true, true);
  const TRect pv = calPrev(), nx = calNext();
  c.drawLine(pv.x + 34, pv.y + 15, pv.x + 26, pv.y + 25, 3, true);
  c.drawLine(pv.x + 26, pv.y + 25, pv.x + 34, pv.y + 35, 3, true);
  c.drawLine(nx.x + 30, nx.y + 15, nx.x + 38, nx.y + 25, 3, true);
  c.drawLine(nx.x + 38, nx.y + 25, nx.x + 30, nx.y + 35, 3, true);
  // Weekday heads, Monday first; the weekend dark.
  static const char* const H[7] = {"M", "T", "W", "T", "F", "S", "S"};
  for (int k = 0; k < 7; k++) {
    const int cx = CAL_X + k * CAL_CW;
    if (k >= 5) c.fillRect(cx, CAL_HEAD_Y, CAL_CW, CAL_HEAD_H, true);
    else c.drawRect(cx, CAL_HEAD_Y, CAL_CW, CAL_HEAD_H, 1, true);
    c.textInBox(cx, CAL_HEAD_Y, CAL_CW, CAL_HEAD_H, H[k], TS_SMALL, k < 5, true);
  }
  static cal::Store store;
  cal::load(store);
  const int lead = (cal::weekday(vy, vm, 1) + 6) % 7;  // Monday = 0
  const int days = cal::daysIn(vy, vm);
  const int rows = (lead + days + 6) / 7;
  for (int r = 0; r < rows; r++)
    for (int k = 0; k < 7; k++) {
      const int cx = CAL_X + k * CAL_CW, cy = CAL_ROW_Y + r * CAL_ROW_H;
      c.drawRect(cx, cy, CAL_CW, CAL_ROW_H, 1, true);
      const int day = r * 7 + k - lead + 1;
      if (day < 1 || day > days) continue;
      const bool today = vy == y && vm == mo && day == d;
      if (today) c.fillRect(cx + 1, cy + 1, CAL_CW - 2, CAL_ROW_H - 2, true);
      char num[4];
      snprintf(num, sizeof(num), "%d", day);
      c.textCentered(cx + CAL_CW / 2, cy + 8, num, TS_MED, !today, today);
      bool any = false;
      for (int i = 0; i < store.n && !any; i++) any = cal::on(store.items[i], vy, vm, day);
      if (any) c.fillCircle(cx + CAL_CW / 2, cy + CAL_ROW_H - 7, 3, !today);
    }
  // Today's list, under the month.
  const int ly = CAL_ROW_Y + rows * CAL_ROW_H + 14;
  char lab[32];
  snprintf(lab, sizeof(lab), "TODAY \xc2\xb7 %s %d", WD3[cal::weekday(y, mo, d)], d);
  for (char* q = lab; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
  c.textTracked(16, ly, lab, TS_SMALL, true, false, 2);
  c.text(SCREEN_W - 16 - c.textWidth("from the Calendar app", TS_SMALL), ly, "from the Calendar app", TS_SMALL, true);
  int idx[8];
  int n = 0;
  for (int i = 0; i < store.n && n < 8; i++)
    if (cal::on(store.items[i], y, mo, d)) idx[n++] = i;
  for (int a = 1; a < n; a++)  // by time, all-day first
    for (int b = a; b > 0; b--) {
      const cal::Item &p = store.items[idx[b - 1]], &q = store.items[idx[b]];
      const int tp = p.allDay ? -1 : p.hour * 60 + p.minute, tq = q.allDay ? -1 : q.hour * 60 + q.minute;
      if (tp <= tq) break;
      const int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t;
    }
  int hh = 0, mm = 0;
  host.clockHHMM(hh, mm);
  const int now = hh * 60 + mm;
  int next = -1;
  for (int i = 0; i < n && next < 0; i++) {
    const cal::Item& it = store.items[idx[i]];
    if (!it.allDay && it.hour * 60 + it.minute >= now) next = i;
  }
  const int rowH = 54;
  int ry = ly + 28;
  if (n == 0) c.text(16, ry + 14, "Nothing today", TS_MED, true);
  for (int i = 0; i < n && ry + rowH <= DOCK_Y - 4; i++, ry += rowH) {
    const cal::Item& it = store.items[idx[i]];
    const bool hi = i == next;
    if (hi) c.fillRect(0, ry, SCREEN_W, rowH, true);
    char t[8];
    if (it.allDay) snprintf(t, sizeof(t), "all day");
    else snprintf(t, sizeof(t), "%02d:%02d", it.hour, it.minute);
    c.text(16, ry + (rowH - c.textHeight(TS_MED)) / 2, t, it.allDay ? TS_SMALL : TS_MED, !hi, true);
    c.textClipped(120, ry + (rowH - c.textHeight(TS_MED)) / 2, SCREEN_W - 136, it.title, TS_MED, !hi);
    if (!hi) c.fillRect(120, ry + rowH - 1, SCREEN_W - 136, 1, true);
  }
}

// The three apps opened last, still on the hub; Timer, Notes and Calendar
// fill in until three have been used. Books (the block above) is left out.
int todayApps(ToolsHost& host, homestyle::App* out) {
  homestyle::App ra[3];
  const int n = homestyle::recentApps(host.prefs(), ra);
  int w = 0;
  auto add = [&](bool g, int i) {
    if (w >= 3 || (!g && i == 10) || !appvis::visible(g, i)) return;
    for (int k = 0; k < w; k++)
      if (out[k].game == g && out[k].idx == i) return;
    out[w++] = homestyle::App{g, (uint8_t)i};
  };
  for (int i = 0; i < n; i++) add(ra[i].game, ra[i].idx);
  add(false, 2);
  add(false, 6);
  add(false, 12);
  return w;
}

void renderTodayHome(ToolsHost& host, ToolsCanvas& c, const recents::Entry* rec, int recN) {
  using namespace hubui;
  homeTop(host, c, "TODAY");
  int hh = 0, mm = 0, y = 0, mo = 0, d = 0;
  // The time in the big digits (the Timer's), the date in words under it.
  if (host.clockHHMM(hh, mm)) {
    char a2[4], b2[4];
    snprintf(a2, sizeof(a2), "%02d", hh);
    snprintf(b2, sizeof(b2), "%02d", mm);
    const int wa = bigdigits::width(a2), wb = bigdigits::width(b2), gap = 34;
    const int x0 = SCREEN_W / 2 - (wa + gap + wb) / 2;
    bigdigits::drawCentered(c, x0 + wa / 2, TODAY_CLOCK_Y, a2, true);
    bigdigits::drawCentered(c, x0 + wa + gap + wb / 2, TODAY_CLOCK_Y, b2, true);
    const int sq = 12, mx = x0 + wa + gap / 2 - sq / 2;
    c.fillRect(mx, TODAY_CLOCK_Y + bigdigits::H * 3 / 10, sq, sq, true);
    c.fillRect(mx, TODAY_CLOCK_Y + bigdigits::H * 7 / 10 - sq, sq, sq, true);
  }
  if (host.clockDate(y, mo, d) && mo >= 1 && mo <= 12) {
    char dl[40];
    snprintf(dl, sizeof(dl), "%s %d %s", WDAY[cal::weekday(y, mo, d)], d, MONTH[mo - 1]);
    c.textCentered(SCREEN_W / 2, TODAY_CLOCK_Y + bigdigits::H + 10, dl, TS_MED, true, true);
  }
  c.fillRect(16, TODAY_RULE_Y, SCREEN_W - 32, 3, true);
  // Four tiles in a row: short labels, the number large, a word under it.
  auto tile = [&](int i, const char* lab, const char* big, const char* sub, bool dark, bool thick) {
    const TRect r = todayTile(i);
    if (dark) c.fillRect(r.x, r.y, r.w, r.h, true);
    else c.drawRect(r.x, r.y, r.w, r.h, thick ? 3 : 2, true);
    c.textTracked(r.x + 8, r.y + 8, lab, TS_SMALL, !dark, false, 1);
    const TSize bs = c.textWidth(big, TS_LARGE, true) <= r.w - 16 ? TS_LARGE : TS_MED;
    c.text(r.x + 8, r.y + 30, big, bs, !dark, true);
    c.textClipped(r.x + 8, r.y + r.h - 6 - c.textHeight(TS_SMALL), r.w - 12, sub, TS_SMALL, !dark);
  };
  char big[24], sub[32];
  int tdc = 0, rh = 0;
  if (host.climate(tdc, rh)) {
    snprintf(big, sizeof(big), "%d\xc2\xb0", (tdc + (tdc >= 0 ? 5 : -5)) / 10);
    snprintf(sub, sizeof(sub), "%d%% humid", rh);
  } else {
    snprintf(big, sizeof(big), "--");
    snprintf(sub, sizeof(sub), "no reading");
  }
  tile(0, "ROOM", big, sub, false, false);
  const int pct = host.batteryPercent();
  if (pct >= 0) snprintf(big, sizeof(big), "%d%%", pct);
  else snprintf(big, sizeof(big), "--");
  tile(1, "BATTERY", big, pct < 0 ? "" : pct <= 20 ? "charge me" : "left", false, false);
  const int run = (y >= 2020) ? homestyle::streak(host.prefs(), y, mo, d) : 0;
  snprintf(big, sizeof(big), "%d", run);
  snprintf(sub, sizeof(sub), run == 1 ? "day, best %d" : "days, best %d", homestyle::best(host.prefs()));
  tile(2, "STREAK", big, sub, true, false);
  snprintf(big, sizeof(big), "%d", (int)host.prefs().getInt("f_min", 25));
  tile(3, "FOCUS", big, "min - start", false, true);
  // The book being read: a block of its own, cover, title, chapter, how far.
  const TRect b = todayBook();
  c.drawRect(b.x, b.y, b.w, b.h, 2, true);
  if (recN > 0) {
    // The small home cover, made at this size (not shrunk from the big one).
    const int cw = bthumb::HOME_SMALL.w, ch = bthumb::HOME_SMALL.h;
    const int cx = b.x + 14, cy = b.y + (b.h - ch) / 2;
    smallCover(c, rec[0], cx, cy, "");
    const int tx = cx + cw + 16, tw = b.x + b.w - 34 - tx;
    int ty = b.y + 16;
    c.textTracked(tx, ty, "CONTINUE READING", TS_SMALL, true, false, 1);
    ty += c.textHeight(TS_SMALL) + 8;
    char lines[4][64];
    const int n = wrapLines(c, rec[0].title, TS_MED, true, tw, lines, 4);
    for (int i = 0; i < n; i++, ty += c.textHeight(TS_MED) + 2) c.text(tx, ty, lines[i], TS_MED, true, true);
    ty += 8;
    if (rec[0].chap[0]) {
      char cl[2][64];
      const int m = wrapLines(c, rec[0].chap, TS_SMALL, false, tw, cl, 2);
      for (int i = 0; i < m; i++, ty += c.textHeight(TS_SMALL) + 2) c.text(tx, ty, cl[i], TS_SMALL, true);
      ty += 8;
    }
    if (rec[0].pct >= 0) {
      char pc[24];
      snprintf(pc, sizeof(pc), "%d%% of the book", rec[0].pct);
      c.text(tx, ty, pc, TS_SMALL, true, true);
      ty += c.textHeight(TS_SMALL) + 4;
      c.drawRect(tx, ty, tw, 12, 2, true);
      const int fill = (tw - 6) * rec[0].pct / 100;
      if (fill > 0) c.fillRect(tx + 3, ty + 3, fill, 6, true);
    }
    c.drawLine(b.x + b.w - 24, b.y + b.h / 2 - 10, b.x + b.w - 14, b.y + b.h / 2, 3, true);
    c.drawLine(b.x + b.w - 14, b.y + b.h / 2, b.x + b.w - 24, b.y + b.h / 2 + 10, 3, true);
  } else {
    c.textCentered(b.x + b.w / 2, b.y + b.h / 2 - 20, "No book open yet", TS_MED, true, true);
    c.textCentered(b.x + b.w / 2, b.y + b.h / 2 + 14, "tap to choose one", TS_SMALL, true);
  }
  // The three apps opened last.
  c.textTracked(16, TODAY_SC_Y + 2, "RECENT APPS", TS_SMALL, true, false, 2);
  chrome2::dottedRule(c, 16, TODAY_SC_Y + 24, SCREEN_W - 32);
  homestyle::App ra[3];
  const int na = todayApps(host, ra);
  for (int i = 0; i < na; i++) {
    const TRect r = todayShortcut(i);
    if (ra[i].game) gicons::draw(c, ra[i].idx, r.x + r.w / 2, r.y + 30, 48);
    else ticons::draw(c, ra[i].idx, r.x + r.w / 2, r.y + 30, 48);
    char label[24];
    sentence(label, sizeof(label), ra[i].game ? gicons::NAMES[ra[i].idx] : ticons::NAMES[ra[i].idx]);
    const TSize sz = c.textWidth(label, TS_MED, true) <= r.w - 8 ? TS_MED : TS_SMALL;
    c.textCentered(r.x + r.w / 2, r.y + 62, label, sz, true, true);
  }
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
    if (recN > 2) recN = 2;  // the drawer strip has room for two
    _recN = (int8_t)recN;
    renderFolder(host, c, _folder, _fpage, rec, recN);
    return;
  }

  // --- home ------------------------------------------------------------------
  _style = homestyle::get(host.prefs());
  if (_style != homestyle::WALL) {
    recents::Entry rec[recents::MAX];
    const int recN = recents::list(host.prefs(), rec);
    _homeRecN = (int8_t)recN;
    if (_style == homestyle::BOOKS) renderBooksHome(host, c, rec, recN);
    else if (_style == homestyle::CALENDAR) renderCalendarHome(host, c, _calOff);
    else renderTodayHome(host, c, rec, recN);
    drawDock(c, -1);
    if (_opening[0]) {
      host.fadeBehind();
      chrome2::working(c, SCREEN_H / 2, "Opening your book", _opening, 0, 0);
    } else if (_upgrade) drawUpgrade(host, c);
    else drawDue(host, c);
    return;
  }
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
  if (_opening[0]) {
    host.fadeBehind();
    chrome2::working(c, SCREEN_H / 2, "Opening your book", _opening, 0, 0);
  } else if (_upgrade) drawUpgrade(host, c);
  else drawDue(host, c);
}

// v2 (kit "Upgrade popup"): a device updated from v1 is told about setup
// once, over home: Later, or Open setup.
void HubScreen::drawUpgrade(ToolsHost& host, ToolsCanvas& c) {
  using namespace hubui;
  host.fadeBehind();
  c.fillRect(0, UP_Y, SCREEN_W, UP_H, false);
  c.fillRect(0, UP_Y, SCREEN_W, 3, true);
  c.fillRect(0, UP_Y + UP_H - 3, SCREEN_W, 3, true);
  int y = UP_Y + 20;
  c.textTracked(24, y, "TOYBOX 2.0", TS_SMALL, true, false, 3);
  y += c.textHeight(TS_SMALL) + 6;
  c.text(24, y, "New: Setup", TS_LARGE, true, true);
  y += c.textHeight(TS_LARGE) + 10;
  static const char* const B[3] = {"New fonts and a cleaner look", "Home screen styles",
                                   "Sync your place with Readest"};
  for (int i = 0; i < 3; i++) {
    c.fillCircle(32, y + c.textHeight(TS_MED) / 2, 3, true);
    c.text(46, y, B[i], TS_MED, true);
    y += c.textHeight(TS_MED) + 6;
  }
  c.text(24, y + 8, "Everything is kept. Takes a minute.", TS_SMALL, true);
  const TRect l = upLater(), o = upOpen();
  c.button(l.x, l.y, l.w, l.h, "Later", false);
  c.button(o.x, o.y, o.w, o.h, "Open setup", true);
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

  // The upgrade popup takes every tap while it is up.
  if (_upgrade) {
    if (hubui::upLater().hit(x, y)) t.kind = Tap::UpLater;
    else if (hubui::upOpen().hit(x, y)) t.kind = Tap::UpOpen;
    else t.kind = Tap::DueSwallow;
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
  // A styled home's own places.
  auto app = [&](int idx) {
    t.kind = Tap::App;
    t.game = false;
    t.idx = idx;
  };
  if (_style == homestyle::BOOKS) {
    if (hubui::booksAll().hit(x, y)) app(10);
    else if (hubui::booksNow().hit(x, y)) {
      if (_homeRecN > 0) { t.kind = Tap::Recent; t.idx = 0; }
      else app(10);
    } else {
      for (int i = 0; i < 4; i++)
        if (hubui::booksAlso(i).hit(x, y) && _homeRecN > i + 1) {
          t.kind = Tap::Recent;
          t.idx = i + 1;
        }
    }
  } else if (_style == homestyle::CALENDAR) {
    if (hubui::calPrev().hit(x, y)) t.kind = Tap::PagePrev;
    else if (hubui::calNext().hit(x, y)) t.kind = Tap::PageNext;
    else if (y >= hubui::CAL_HEAD_Y && y < DOCK_Y) app(12);
  } else if (_style == homestyle::TODAY) {
    if (hubui::todayTile(3).hit(x, y)) app(13);
    else if (hubui::todayBook().hit(x, y)) {
      if (_homeRecN > 0) { t.kind = Tap::Recent; t.idx = 0; }
      else app(10);
    } else {
      homestyle::App ra[3];
      const int na = todayApps(const_cast<ToolsHost&>(host), ra);
      for (int i = 0; i < na; i++)
        if (hubui::todayShortcut(i).hit(x, y)) {
          app(ra[i].idx);
          t.game = ra[i].game;
        }
    }
  }
  return t;
}
