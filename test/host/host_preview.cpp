// Renders every screen on the host and dumps PGM images, so layout can be
// checked without hardware. It drives the same Toybox object the firmware does,
// through the same StickyHost, so what is rendered here is what the device
// renders. Build: see README (test/host section).
#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "appvis.h"
#include "board_pins.h"
#include "epd.h"
#include "gfx.h"
#include "game2048.h"
#include "service_ui.h"
#include "tour.h"
#include "welcome.h"
#include "sticky_host.h"
#include "toybox.h"
#include "settings.h"
#include "bmp_gray.h"
#include "tools/cpfont.h"
#include "tools/lock_image.h"
#include "tools/lockscreen.h"
#include "nonogram.h"
#include "tools/note_store.h"
#include "tools/tool_book.h"
#include "tools/tool_dice.h"
#include "tools/bookmarks.h"
#include "tools/reader_menu.h"
#include "tools/tool_epub.h"
#include "tools/epub/epubcore.h"
#include "tools/epub/koreader_sdr.h"
#include "tools/recents.h"
#include "tools/book_thumbs.h"
#include "tools/epub/epub_png.h"
#include "tools/epub/epub_jpegdc.h"
#include "fake_epub_cover.inc"
#include "tools/tool_flash.h"
#include "tools/tool_recipe.h"
#include "tools/tool_note.h"
#include "tools/tool_picker.h"
#include "tools/tool_random.h"
#include "tools/tool_timer.h"
#include "tools/tool_sea.h"
#include "tools/tool_sudoku.h"
#include "touch.h"
#include "tools/help.h"
#include "tools/decor.h"
#include "wordle.h"
#include "xo.h"

// --- Mock Epd implementation --------------------------------------------------
Epd epd;
static const char* g_dumpName = "frame";
// Naming the screen also tags any text overflow found while drawing it, so a
// clipped string is reported against the screen it actually belongs to.
// A named screen that never paints means the tap meant to reach it landed on
// empty panel. Without this the run just writes one file fewer and renumbers
// everything after it, which reads like a diff rather than a failure.
static bool g_screenPainted = true;
static void screenPaintCheck() {
  if (g_screenPainted) return;
  printf("PREVIEW FAIL: \"%s\" never painted -- a guard tapped empty panel\n", g_dumpName);
  fflush(stdout);
  abort();
}
static void setScreen(const char* n) {
  screenPaintCheck();
  g_dumpName = n;
  g_screenPainted = false;
  gfx::g_overflowScreen = n;
}
static int g_dumpCounter = 0;
static bool g_dumpEnabled = true;

// Aim at a button by the rect the tool actually uses. Remembered coordinates go
// stale the moment a layout moves, and a guard that silently taps empty panel
// reports the wrong screen rather than a failure.
static void tapRect(const TRect& r);

#ifdef TOYBOX_CP_FONTS
// The CrossPoint pass renders every screen with the reader's own faces, which
// are half again to twice as tall as the ones these layouts were drawn for.
// Several guards below tap at coordinates measured against the Sticky's text, so
// under those faces they land on the wrong line by construction -- that is the
// finding, not a crash. Here a failed guard is counted and the run carries on to
// the end, because the pictures are the point. The real build still aborts.
static int g_softFails = 0;
#define abort() (void)(g_softFails++)
#endif

bool Epd::begin() {
  _fb = (uint8_t*)malloc(EPD_BUF_SIZE);
  _prev = (uint8_t*)malloc(EPD_BUF_SIZE);
  memset(_fb, 0xFF, EPD_BUF_SIZE);
  _panelOk = true;  // the fake panel is always listening
  return true;
}
void Epd::clear(bool white) { memset(_fb, white ? 0xFF : 0x00, EPD_BUF_SIZE); }
// No grey waveform on a PC: the reader falls back to its 1-bit dither, which
// is also the only rendering a .pgm can hold.
bool Epd::displayGrey2bpp(const uint8_t*) { return false; }
void Epd::drawPixel(int x, int y, uint8_t color) {
  // Not a copy of the device mapping -- the same function. See epd.h.
  if (x < 0 || y < 0 || x >= logicalW() || y >= logicalH()) return;
  int px, py;
  epdMapPixel(rotation(), panelFlipX(), panelFlipY(), x, y, px, py);
  uint8_t* p = &_fb[(uint32_t)py * EPD_WB + (px >> 3)];
  const uint8_t mask = 0x80 >> (px & 7);
  if (color)
    *p |= mask;
  else
    *p &= ~mask;
}
void Epd::fillRect(int x, int y, int w, int h, uint8_t color) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) drawPixel(xx, yy, color);
}
void Epd::drawRect(int x, int y, int w, int h, uint8_t color, int t) {
  fillRect(x, y, w, t, color);
  fillRect(x, y + h - t, w, t, color);
  fillRect(x, y, t, h, color);
  fillRect(x + w - t, y, t, h, color);
}
void Epd::drawLine(int x0, int y0, int x1, int y1, uint8_t color, int thickness) {
  const int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  const bool steep = -dy > dx;
  int err = dx + dy;
  if (thickness < 1) thickness = 1;
  const int half = thickness / 2;
  for (;;) {
    if (steep)
      fillRect(x0 - half, y0, thickness, 1, color);
    else
      fillRect(x0, y0 - half, 1, thickness, color);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void Epd::fillCircle(int cx, int cy, int r, uint8_t color) {
  if (r <= 0) return;
  for (int dy = -r; dy <= r; dy++) {
    const int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
  }
}
void Epd::drawCircle(int cx, int cy, int r, uint8_t color, int thickness) {
  if (r <= 0) return;
  if (thickness < 1) thickness = 1;
  const int inner = r - thickness;
  for (int dy = -r; dy <= r; dy++) {
    const int outDx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    if (inner > 0 && abs(dy) < inner) {
      const int inDx = (int)(sqrtf((float)(inner * inner - dy * dy)) + 0.5f);
      fillRect(cx - outDx, cy + dy, outDx - inDx + 1, 1, color);
      fillRect(cx + inDx, cy + dy, outDx - inDx + 1, 1, color);
    } else {
      fillRect(cx - outDx, cy + dy, 2 * outDx + 1, 1, color);
    }
  }
}
// Set by a guard that wants to know the canvas rotation at the FIRST paint of
// a flow -- the loading screen's, in particular, which must stand up even when
// the page it leads to lies down.
static int* g_rotWatch = nullptr;

static void dumpFrame(const uint8_t* fb) {
  if (!g_dumpEnabled) return;
  g_screenPainted = true;
  char name[128];
  snprintf(name, sizeof(name), "preview_%02d_%s.pgm", g_dumpCounter++, g_dumpName);
  FILE* f = fopen(name, "wb");
  // Always dumped in the panel's own frame; the PNG step rotates it upright.
  fprintf(f, "P5\n%d %d\n255\n", PANEL_W, PANEL_H);
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++) {
      const uint8_t bit = fb[(uint32_t)y * EPD_WB + (x >> 3)] & (0x80 >> (x & 7));
      fputc(bit ? 255 : 0, f);
    }
  fclose(f);
  printf("wrote %s\n", name);
}
static int g_paintCount = 0;  // counts refreshes even when dumping is off
// Counted apart, because the whole point of a fast turn is WHICH of these two
// the reader asked for: a full is 1.7 s on the glass and a partial is 0.3 s.
static int g_fullCount = 0, g_partialCount = 0;
void Epd::displayFull() {
  g_paintCount++;
  g_fullCount++;
  if (g_rotWatch) {  // the first paint after a guard armed it, then done
    *g_rotWatch = rotation();
    g_rotWatch = nullptr;
  }
  dumpFrame(_fb);
}
void Epd::displayPartial() {
  g_paintCount++;
  g_partialCount++;
  dumpFrame(_fb);
}
void Epd::deepSleep() {}

// --- Mock buzzer / touch ------------------------------------------------------
namespace buzzer {
static uint8_t g_soundLevel = 3;
void begin() {}
void setLevel(Level lv) { g_soundLevel = (uint8_t)lv; }
Level level() { return (Level)g_soundLevel; }
void setEnabled(bool on) { g_soundLevel = on ? 3 : 0; }
bool enabled() { return g_soundLevel != 0; }
// Counted, so a guard can assert not just that the panel showed the right
// thing but that the device SAID so -- the unwrapping beep, in particular.
int g_confirms = 0;
void tap() {}
void confirm() { g_confirms++; }
void error() {}
void win() {}
}  // namespace buzzer

Touch touch;
bool Touch::begin() { return true; }
void Touch::poll(TouchEvent&) {}
bool Touch::readReg(uint16_t, uint8_t*, uint8_t) { return false; }
void Touch::clearStatus() {}
void Touch::resetWithIntLevel(uint8_t) {}
bool Touch::probe() { return false; }

// --- Glue ---------------------------------------------------------------------
Preferences prefs;

// Stands in for the clock, thermometer and fuel gauge, so the sleeping screens
// render with something in them rather than with every optional part missing.
static void hostLockInfo(lock::Info& i) {
  i.haveClock = true;
  i.hour = 9;
  i.minute = 41;
  i.day = 9;
  i.month = 8;
  i.year = 2026;
  i.haveTemp = true;
  i.tempDeciC = 214;
  i.haveBattery = true;
  i.batteryPct = 84;
}

// The hub is now a dock and three folder pages. The walk mirrors hub.cpp's
// geometry independently -- the applist order, the two-column folder layout,
// the centred block -- so the two can disagree loudly rather than quietly.
static void checkHubRouting(const char* label) {
  struct Want { bool game; int idx; };
  struct Grp { Want items[6]; int n; };
  const Grp ALL[3] = {
      {{{true, 0}, {true, 1}, {true, 2}, {true, 3}, {false, 7}, {false, 8}}, 6},
      {{{false, 0}, {false, 1}, {false, 3}, {false, 4}, {false, 2}, {false, 11}}, 6},
      {{{false, 9}, {false, 10}, {false, 5}, {false, 6}}, 4},
  };
  // The Study drawer wears the recently-read strip once any book has been
  // opened; its tiles are then top-anchored rather than centred.
  recents::Entry rrTmp[recents::MAX];
  const int recentsN = recents::list(stickyHost.prefs(), rrTmp);
  Grp G[3] = {};
  int shown = 0;
  for (int gi = 0; gi < 3; gi++)
    for (int ii = 0; ii < ALL[gi].n; ii++) {
      const Want& w = ALL[gi].items[ii];
      if (!appvis::visible(w.game, w.idx)) continue;
      G[gi].items[G[gi].n++] = w;
      shown++;
    }

  g_dumpEnabled = false;
  toybox.goHub();
  toybox.hostHub().goHome();

  // Home answers nothing but the dock. These points cover the wallpaper, the
  // two hint marks, and the title -- all drawn, none tappable.
  for (auto pt : {std::pair<int,int>{240, 400}, {60, 20}, {hubui::HINT_X, hubui::HINT_GEAR_Y},
                  {hubui::HINT_X, hubui::HINT_RESUME_Y}, {240, hubui::DOCK_Y - 2}}) {
    toybox.onTap(pt.first, pt.second);
    if (toybox.hostInApp() || toybox.hostInSettings() || !toybox.hostHub().atHome()) {
      printf("HUB ROUTING FAIL: home reacted at (%d,%d)\n", pt.first, pt.second);
      abort();
    }
  }

  auto routesTo = [&](int x, int y, const Want& w, const char* what, int gi, int ii) {
    toybox.onTap(x, y);
    const bool ok = toybox.hostInApp() && toybox.hostIsGame() == w.game &&
                    toybox.hostIdx() == w.idx;
    if (!ok) {
      printf("HUB ROUTING FAIL group %d item %d (%s) at (%d,%d)\n", gi, ii, what, x, y);
      abort();
    }
    toybox.goHub();  // back to the folder page the app was opened from
  };

  for (int gi = 0; gi < 3; gi++) {
    // Into the folder through its third of the dock, twice over: the centre
    // of the third and its inner corner, because the whole band is the target.
    toybox.hostHub().goHome();
    toybox.onTap(80 + gi * 160, hubui::DOCK_Y + hubui::DOCK_H / 2);
    if (toybox.hostInApp() || toybox.hostHub().folder() != gi) {
      printf("HUB ROUTING FAIL: dock third %d did not open its folder\n", gi);
      abort();
    }
    toybox.hostHub().goHome();
    toybox.onTap(gi * 160 + 2, EPD_H - 2);
    if (toybox.hostHub().folder() != gi) {
      printf("HUB ROUTING FAIL: dock corner of third %d missed\n", gi);
      abort();
    }
    // The dock rides every drawer now: its thirds hop straight between
    // drawers, no trip home in between.
    toybox.onTap(80 + ((gi + 1) % 3) * 160, hubui::DOCK_Y + hubui::DOCK_H / 2);
    if (toybox.hostInApp() || toybox.hostHub().folder() != (gi + 1) % 3) {
      printf("HUB ROUTING FAIL: the dock did not hop from drawer %d\n", gi);
      abort();
    }
    toybox.onTap(80 + gi * 160, hubui::DOCK_Y + hubui::DOCK_H / 2);  // and back
    // When apps are hidden the drawer grows a "+ add" ghost cell after the
    // last app, and it takes part in the geometry like any other cell.
    const bool hasAdd = G[gi].n < ALL[gi].n;
    const int cells = G[gi].n + (hasAdd ? 1 : 0);
    if (cells == 0) continue;

    // The drawer's grid, mirrored from hub.cpp: two columns, centred block --
    // except Study with recents, whose block pins to the top for the strip.
    const bool strip = gi == 2 && recentsN > 0 && cells <= 4;
    const int rows = (cells + 1) / 2;
    int y0 = strip ? hubui::FOLDER_TOP
                   : hubui::FOLDER_TOP +
                         ((hubui::FOLDER_BOTTOM - hubui::FOLDER_TOP) - rows * hubui::ROW_STEP) / 2;
    if (y0 < hubui::FOLDER_TOP) y0 = hubui::FOLDER_TOP;
    for (int ii = 0; ii < G[gi].n; ii++) {
      const int col = ii % 2, row = ii / 2;
      const int cx = EPD_W / 4 + col * (EPD_W / 2);
      const int rowTop = y0 + row * hubui::ROW_STEP;
      const Want& w = G[gi].items[ii];
      routesTo(cx, rowTop + hubui::TILE / 2, w, "centre", gi, ii);
      routesTo(cx, rowTop + 1, w, "top edge", gi, ii);
      routesTo(cx, rowTop + hubui::ROW_STEP - 2, w, "bottom edge", gi, ii);
      routesTo(col == 0 ? 1 : EPD_W / 2 + 1, rowTop + hubui::TILE / 2, w, "left edge", gi, ii);
      routesTo(col == 0 ? EPD_W / 2 - 2 : EPD_W - 2, rowTop + hubui::TILE / 2, w, "right edge",
               gi, ii);
    }
    if (hasAdd) {
      // The ghost cell itself: it must open settings, and leaving settings
      // must come back to the hub with the drawer still selected.
      const int col = G[gi].n % 2, row = G[gi].n / 2;
      toybox.onTap(EPD_W / 4 + col * (EPD_W / 2),
                   y0 + row * hubui::ROW_STEP + hubui::TILE / 2);
      if (!toybox.hostInSettings()) {
        printf("HUB ROUTING FAIL: + add in drawer %d did not open settings\n", gi);
        abort();
      }
      toybox.onTap(50, 26);  // settings back: out to the hub
      if (toybox.hostHub().folder() != gi) {
        printf("HUB ROUTING FAIL: leaving settings lost drawer %d\n", gi);
        abort();
      }
    }
    // An odd count leaves the last right-hand cell empty; nothing may open.
    if (cells % 2 == 1) {
      toybox.onTap(3 * EPD_W / 4, y0 + (rows - 1) * hubui::ROW_STEP + hubui::TILE / 2);
      if (toybox.hostInApp() || toybox.hostInSettings()) {
        printf("HUB ROUTING FAIL: empty cell in folder %d opened something\n", gi);
        abort();
      }
    }
    // Above and below the block is inert page, not a hidden target.
    if (y0 - 4 > BAR_TOUCH_H) {
      toybox.onTap(EPD_W / 2, y0 - 4);
      if (toybox.hostInApp()) {
        printf("HUB ROUTING FAIL: gap above folder %d grid opened something\n", gi);
        abort();
      }
    }
    const int yEnd = y0 + rows * hubui::ROW_STEP;
    if (yEnd + 4 < EPD_H) {
      toybox.onTap(EPD_W / 2, yEnd + 4);
      if (toybox.hostInApp()) {
        printf("HUB ROUTING FAIL: gap below folder %d grid opened something\n", gi);
        abort();
      }
    }
    // HOME in the corner takes the page back to the picture.
    toybox.onTap(50, 26);
    if (!toybox.hostHub().atHome()) {
      printf("HUB ROUTING FAIL: HOME did not leave folder %d\n", gi);
      abort();
    }
  }
  g_dumpEnabled = true;
  printf("hub routing ok (%s: %d tiles via 3 folders, edges, gaps, home)\n", label, shown);
}

// The corner buttons' touch areas reach below the bar they are drawn in, and
// the back tap is tested before anything a tool owns. A control that creeps up
// into that zone does not become hard to hit -- it stops responding at all, and
// nothing on screen explains why. These are the screens whose first row sits
// closest to the bar; they are the ones that would hit it first.
static_assert(diceui::TYPE_Y0 >= BAR_TOUCH_H, "dice type row is inside the back button");
static_assert(randui::MODE_NUM.y >= BAR_TOUCH_H, "random mode row is inside the back button");
static_assert(timerui::MODE_CD.y >= BAR_TOUCH_H, "timer mode row is inside the back button");
static_assert(nui::BODY.y >= BAR_TOUCH_H, "the note body is inside the back button");

// --- CrossInk .cpfont ---------------------------------------------------------
// The fixture is a real file, made by CrossInk's own converter from DejaVu Sans
// at their size 8, with all four cuts. Hand-writing one would only prove this
// reader agrees with my reading of their format; a file their tool produced
// proves it agrees with their tool.
//
// The path is searched rather than assumed: the documented way to run this is
// from test/host, and the second font pass is run from a scratch directory so
// its .pgm files do not overwrite the first pass's.
static bool readFixture(std::vector<uint8_t>& out) {
  const char* env = getenv("TOYBOX_FIXTURES");
  char envPath[512] = "";
  if (env) snprintf(envPath, sizeof(envPath), "%s/fixture_dejavu_8.cpfont", env);
  const char* tries[] = {"fixture_dejavu_8.cpfont", "test/host/fixture_dejavu_8.cpfont",
                         "../../test/host/fixture_dejavu_8.cpfont", envPath};
  for (const char* path : tries) {
    if (!path[0]) continue;
    FILE* f = fopen(path, "rb");
    if (!f) continue;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    const bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
  }
  return false;
}

static void checkCpFont() {
  std::vector<uint8_t> blob;
  if (!readFixture(blob)) {
    printf("CPFONT FAIL: fixture_dejavu_8.cpfont not found (run from test/host, or set "
           "TOYBOX_FIXTURES)\n");
    abort();
  }
  cpfont::Font font;
  if (!font.open(blob.data(), (uint32_t)blob.size()) || font.styles() != 4) {
    printf("CPFONT FAIL: the fixture would not open (%d styles)\n", font.styles());
    abort();
  }
  // The four cuts, found by name rather than by position.
  for (uint8_t cut : {cpfont::REGULAR, cpfont::BOLD, cpfont::ITALIC, cpfont::BOLD_ITALIC}) {
    if (font.find(cut) < 0) {
      printf("CPFONT FAIL: cut %d missing from a four-cut file\n", cut);
      abort();
    }
  }

  const cpfont::Style& reg = font.style(font.find(cpfont::REGULAR));
  cpfont::Glyph a{}, space{}, bold{}, miss{};
  if (reg.advanceY != 19 || !font.glyph(reg, 'A', a) || a.w != 12 || a.h != 12 ||
      a.advance() != 11 || a.top != 12) {
    printf("CPFONT FAIL: line %u, A is %ux%u adv %d top %d\n", reg.advanceY, a.w, a.h,
           a.advance(), a.top);
    abort();
  }
  // A space has an advance and no bitmap. A reader that assumes every glyph
  // has pixels draws nothing and moves nowhere, and the words run together.
  if (!font.glyph(reg, ' ', space) || space.w != 0 || space.bytes != 0 || space.advance() <= 0) {
    printf("CPFONT FAIL: the space is %ux%u adv %d\n", space.w, space.h, space.advance());
    abort();
  }
  // These packs carry no Thai and no CJK, so the answer has to be an honest
  // "not here" -- which is what lets the baked tables keep drawing Thai under
  // a card font that has never heard of it.
  if (font.glyph(reg, 0x0E01, miss) || font.glyph(reg, 0x4E00, miss)) {
    printf("CPFONT FAIL: a Latin pack claimed Thai or CJK coverage\n");
    abort();
  }

  // The bitmap is a continuous 2-bit stream with no padding at the end of a
  // row, so a row-by-row reader drifts further out of step on every glyph
  // whose width is not a multiple of four. The letter A has ink at its apex
  // and none between its legs: geometry no misaligned reader survives.
  int apex = 0, betweenLegs = 0;
  for (int x = 0; x < a.w; x++)
    if (font.on(reg, a, x, 1)) apex++;
  for (int x = a.w / 2 - 1; x <= a.w / 2 + 1; x++)
    if (font.on(reg, a, x, a.h - 1)) betweenLegs++;
  if (apex < 1 || apex > 4 || betweenLegs != 0) {
    printf("CPFONT FAIL: A has %d px at the apex and %d between the legs\n", apex, betweenLegs);
    abort();
  }

  // Bold is a different bitmap for the same letter, not a copy of it.
  const cpfont::Style& bst = font.style(font.find(cpfont::BOLD));
  int inkReg = 0, inkBold = 0;
  if (!font.glyph(bst, 'A', bold)) {
    printf("CPFONT FAIL: no A in the bold cut\n");
    abort();
  }
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++)
      if (font.on(reg, a, x, y)) inkReg++;
  for (int y = 0; y < bold.h; y++)
    for (int x = 0; x < bold.w; x++)
      if (font.on(bst, bold, x, y)) inkBold++;
  if (inkBold <= inkReg) {
    printf("CPFONT FAIL: bold has %d px of ink against regular's %d\n", inkBold, inkReg);
    abort();
  }

  // Rubbish in, "no" out -- never a half-open font, because half a font is a
  // page of blanks nobody can explain.
  cpfont::Font bad;
  std::vector<uint8_t> wrong(blob);
  wrong[9] = 99;  // a version this does not read
  const bool refused = !bad.open(nullptr, 0) && !bad.open(blob.data(), 8) &&
                       !bad.open(wrong.data(), (uint32_t)wrong.size()) &&
                       !bad.open(blob.data(), 200);  // sections past the end
  if (!refused) {
    printf("CPFONT FAIL: a broken file opened anyway\n");
    abort();
  }
  printf("cpfont ok (4 cuts, %u glyphs, %u px line, Thai and CJK refused honestly)\n",
         reg.glyphCount, reg.advanceY);
}

// The clock every options panel carries in the bar's far corner. It is drawn
// there rather than on the page behind it because a page is not redrawn often
// enough to hold a true time, and both halves of that promise have to be kept:
// it appears when a clock has been set, and NOTHING appears when one has not,
// because an invented time is worse than no time. Measured as ink in the
// corner the "?" would otherwise use, so a string drawn off the end of the bar
// fails here too. Call it with the panel on screen.
static void checkCornerClock(const char* what) {
  const bool wasDumping = g_dumpEnabled;
  g_dumpEnabled = false;
  auto cornerInk = [] {
    int n = 0;
    for (int y = 0; y < TOPBAR_H - 2; y++)
      for (int x = SCREEN_W - BACK_W; x < SCREEN_W; x++) {
        int px, py;
        epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, px, py);
        if ((epd.fb()[(uint32_t)py * EPD_WB + (px >> 3)] & (0x80 >> (px & 7))) == 0) n++;
      }
    return n;
  };
  const int lit = cornerInk();
  sensors::hostSetClock(false);
  stickyHost.refresh(true);
  const int dark = cornerInk();
  sensors::hostSetClock(true);  // back to 09:41 for every shot after this
  stickyHost.refresh(true);
  if (lit < 20 || dark != 0) {
    printf("OPTIONS CLOCK FAIL: %s drew %d px with a clock, %d px without\n", what, lit, dark);
    abort();
  }
  g_dumpEnabled = wasDumping;
  printf("options clock ok (%s: %d px set, none unset)\n", what, lit);
}

// The firmware's end of the clock hook, which on the device writes the RTC.
// Here it records what arrived, so a guard can check the NUMBER rather than
// the screen that follows it: a page that says "the clock is set" while the
// hook never fired is exactly the bug worth catching.
static int64_t g_clockSetMs = 0;
// The device's answer can be no: the chip refuses the write, or takes it and
// does not start keeping time. The page has to be able to say so, so the
// harness can make the hook fail on purpose.
static bool g_clockRefuses = false;
static bool hostSetClockFromPhone(int64_t localEpochMs) {
  g_clockSetMs = localEpochMs;
  if (g_clockRefuses) return false;
  sensors::hostSetClock(true);
  return true;
}

// Tap a tool without emitting a frame for every intermediate state.
static void quietTap(int x, int y) {
  g_dumpEnabled = false;
  toybox.onTap(x, y);
  g_dumpEnabled = true;
}

int main() {
  epd.begin();
  prefs.begin("toybox", false);
  toybox.begin(stickyHost);
  lock::apply(prefs);
  lock::setInfoHook(hostLockInfo);
  clockset::hook(hostSetClockFromPhone);
  prefs.putInt("w_games", 12);
  prefs.putInt("w_wins", 10);
  prefs.putInt("w_streak", 4);
  prefs.putInt("w_max", 7);
  prefs.putInt("w_d3", 4);
  prefs.putInt("w_d4", 5);
  prefs.putInt("w_d5", 1);
  prefs.putInt("n5_solved", 3);
  prefs.putInt("n10_solved", 2);
  prefs.putUInt("n10_best", 412);
  prefs.putInt("t_best", 20116);
  prefs.putInt("t_tile", 1024);
  prefs.putUInt("c_heads", 63);
  prefs.putUInt("c_tails", 57);
  prefs.putInt("x_w", 3);
  prefs.putInt("x_l", 1);
  prefs.putInt("x_d", 2);
  prefs.putInt("x_strk", 2);
  prefs.putInt("x_best", 4);
  prefs.putInt("sd_w0", 3);
  prefs.putInt("bs_w", 2);
  prefs.putInt("bs_l", 1);

  {  // TEMP decor sheet
    ToolsCanvas& c = stickyHost.sharedCanvas();
    epd.clear();
    decor::blast(c, 90, 90, 60, 3, true);
    decor::peg(c, 240, 90, 28, true);
    decor::debris(c, 390, 90, 60, 7, true);
    decor::star(c, 90, 240, 60, 24, 5, 0.0f, true);
    decor::rays(c, 240, 240, 20, 60, 12, 0.0f, true);
    decor::diamond(c, 390, 240, 30, true);
    decor::ornament(c, 240, 330, 300, true);
    decor::confetti(c, 20, 360, 440, 120, 3, 18, true);
    decor::banner(c, 60, 520, 360, 80, "YOU WIN", TS_HUGE, true);
    decor::banner(c, 60, 660, 360, 70, "GAME OVER", TS_LARGE, false);
    setScreen("decor");
    epd.displayFull();
  }

  sensors::hostSetClock(true);  // 09:41, the hour every product shot keeps
  setScreen("hub");
  toybox.goHub();

  // Grid routing: the dock, every folder tile, and the gaps, must resolve to
  // the thing under the finger. requestScreen is stubbed, so record what it
  // asked for and compare against the drawing order.
  checkCpFont();
  checkHubRouting("all shown");

  // The three folder pages, drawn as a finger would reach them.
  {
    static const char* kFolderShots[3] = {"hub_play", "hub_decide", "hub_study"};
    for (int f = 0; f < 3; f++) {
      g_dumpEnabled = false;
      toybox.hostHub().goHome();
      g_dumpEnabled = true;
      setScreen(kFolderShots[f]);
      toybox.onTap(80 + f * 160, hubui::DOCK_Y + hubui::DOCK_H / 2);
    }
    g_dumpEnabled = false;
    toybox.onTap(50, 26);  // back to the picture
    g_dumpEnabled = true;
  }

  // Carry on. Opening an app leaves a trail in NVS; resumeLast follows it,
  // refuses a hidden app, and reports nothing to follow when there is nothing.
  {
    g_dumpEnabled = false;
    toybox.onTap(80, hubui::DOCK_Y + 30);  // PLAY
    toybox.onTap(EPD_W / 4, hubui::FOLDER_TOP +
                     ((hubui::FOLDER_BOTTOM - hubui::FOLDER_TOP) - 3 * hubui::ROW_STEP) / 2 +
                     hubui::TILE / 2);  // the first tile: wordle
    if (!toybox.hostInApp() || !toybox.hostIsGame() || toybox.hostIdx() != 0) {
      printf("RESUME FAIL: could not open wordle to leave a trail\n");
      abort();
    }
    toybox.goHub();
    toybox.hostHub().goHome();
    if (!toybox.resumeLast() || !toybox.hostInApp() || !toybox.hostIsGame() ||
        toybox.hostIdx() != 0) {
      printf("RESUME FAIL: the DOWN hold did not reopen wordle\n");
      abort();
    }
    toybox.goHub();
    if (toybox.hostHub().folder() != 0) {
      printf("RESUME FAIL: resuming did not land the hub on the app's folder\n");
      abort();
    }
    appvis::set(true, 0, false);  // hide it: the trail must go cold
    if (toybox.resumeLast()) {
      printf("RESUME FAIL: resumed an app that settings says is hidden\n");
      abort();
    }
    appvis::set(true, 0, true);
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("resume trail ok (follows, refuses hidden)\n");
  }

  // As a guest the hub has no home page at all: the drawers are the top level,
  // wearing the dock, with the back arrow leaving Toybox and the gear in the
  // header corner. The standalone home never draws any of that.
  setScreen("hub_as_guest");
  stickyHost.hostSetCanExit(true);
  stickyHost.refresh(true);
  {
    g_dumpEnabled = false;
    // The dock on a guest drawer switches drawers in place.
    toybox.onTap(EPD_W / 2, hubui::DOCK_Y + 30);  // middle third: UTILITY
    if (toybox.hostHub().folder() != 1 || toybox.hostHub().atHome()) {
      printf("EXIT FAIL: the guest dock did not switch to the second drawer\n");
      abort();
    }
    stickyHost.hostClearExited();
    toybox.onTap(50, 26);  // the back arrow: out of Toybox altogether
    if (!stickyHost.hostExited()) {
      printf("EXIT FAIL: the guest back arrow did not leave\n");
      abort();
    }
    // ...standalone, the same tap on home must open nothing at all.
    stickyHost.hostSetCanExit(false);
    toybox.hostHub().goHome();
    stickyHost.hostClearExited();
    toybox.onTap(50, 26);
    if (stickyHost.hostExited() || toybox.hostInApp() || toybox.hostInSettings()) {
      printf("EXIT FAIL: standalone hub reacted to a corner that has no button\n");
      abort();
    }
    // The gear in the guest header corner opens settings, because a guest host
    // may have no side buttons to hold.
    stickyHost.hostSetCanExit(true);
    toybox.onTap(EPD_W - 45, 30);
    if (!toybox.hostInSettings()) {
      printf("EXIT FAIL: the guest gear did not open settings\n");
      abort();
    }
    toybox.goHub();
    stickyHost.hostSetCanExit(false);
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("hub exit ok (guest drawers with dock, absent when standalone)\n");
  }

  // --- settings ------------------------------------------------------------
  // Row geometry mirrors settings.cpp: two columns at x 16 and 248, list top
  // 92, a 26 px heading over each group, 52 px rows -- back to 52 since the
  // checkboxes moved onto their own page and stopped sharing the height.
  auto setRow = [](int col, int headTop, int i) {
    return std::pair<int, int>{col ? 330 : 100, headTop + 26 + i * 52 + 26};
  };
  setScreen("settings");
  toybox.openSettings();

  // The checkbox list lives behind APPS ON THE HUB... now.
  setScreen("settings_apps");
  tapRect(setui::actionRect(setui::ACT_APPS));

  // Hide four apps through the screen itself, so the hub below reflows around
  // exactly what a finger would have hidden. STUDY's heading sits at
  // 92 + 26 + 6*52 + 14 = 444 with UTILITY's six rows above it.
  g_dumpEnabled = false;
  for (auto rc : {setRow(0, 92, 0), setRow(0, 92, 2), setRow(1, 92, 1), setRow(1, 444, 2)})
    toybox.onTap(rc.first, rc.second);
  toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // back up to the buttons page
  // Sound steps down a level on each tap and wraps at the bottom, so tapping it
  // once from the top has to land on the level below the top, and tapping it
  // as many times as there are levels has to come back to where it started.
  const int levels = stickyHost.soundLevels();
  const int soundWas = stickyHost.soundLevel();
  tapRect(setui::actionRect(setui::ACT_SOUND));
  if (appvis::shown() != 12 || stickyHost.soundLevel() != soundWas - 1) {
    printf("SETTINGS FAIL: taps did not land (%d shown, sound %d)\n", appvis::shown(),
           stickyHost.soundLevel());
    abort();
  }
  for (int i = 1; i < levels; i++) tapRect(setui::actionRect(setui::ACT_SOUND));
  if (stickyHost.soundLevel() != soundWas) {
    printf("SETTINGS FAIL: %d taps on sound landed on %d, not %d\n", levels,
           stickyHost.soundLevel(), soundWas);
    abort();
  }
  // ...and the screen below is drawn with it muted, which is the state the
  // walk used to leave it in.
  for (int i = 0; i < levels - 1; i++) tapRect(setui::actionRect(setui::ACT_SOUND));
  if (stickyHost.soundLevel() != 0) {
    printf("SETTINGS FAIL: could not reach mute\n");
    abort();
  }
  setScreen("settings_edited");
  g_dumpEnabled = true;
  tapRect(setui::actionRect(setui::ACT_RESET));  // first tap: arms and asks

  // The lock screen page, reached by the button on the settings screen.
  setScreen("settings_lock");
  tapRect(setui::actionRect(setui::ACT_LOCK));
  if (!toybox.hostInSettings()) {
    printf("LOCK FAIL: the lock screen page did not open\n");
    abort();
  }
  {
    // The four on/off rows have to toggle and land back where they started,
    // or a setting can be moved into a state it cannot be moved out of.
    g_dumpEnabled = false;
    for (int r = setui::LR_ROTATE; r <= setui::LR_BATT; r++) {
      const lock::Config before = lock::config();
      for (int k = 0; k < 2; k++) tapRect(setui::lockRect(r));
      if (memcmp(&before, &lock::config(), sizeof(lock::Config)) != 0) {
        printf("LOCK FAIL: row %d did not return to where it started\n", r);
        abort();
      }
    }
    // Sleep timing and the wake target are chips now: every one reachable,
    // every one the one that ends up chosen.
    {
      const uint8_t wasIdx = lock::config().sleepIdx;
      for (int k = 0; k < lock::SLEEP_COUNT; k++) {
        tapRect(setui::sleepChipRect(k));
        if (lock::config().sleepIdx != k) {
          printf("LOCK FAIL: sleep chip %d chose %d\n", k, lock::config().sleepIdx);
          abort();
        }
      }
      tapRect(setui::sleepChipRect(wasIdx));
      const uint8_t wasWake = lock::config().wake;
      tapRect(setui::wakeChipRect(1));
      if (lock::config().wake != lock::WAKE_HUB) {
        printf("LOCK FAIL: the hub wake chip did not take\n");
        abort();
      }
      tapRect(setui::wakeChipRect(0));
      if (lock::config().wake != lock::WAKE_NOTE) {
        printf("LOCK FAIL: the note wake chip did not take\n");
        abort();
      }
      tapRect(setui::wakeChipRect(wasWake == lock::WAKE_HUB ? 1 : 0));
    }

    // Every chip has to be reachable and has to be the one that ends up filled.
    // A chip row where two tap targets overlap looks right and picks the wrong
    // thing, which is the failure a screenshot will not show.
    {
      const uint8_t was = lock::config().empty;
      for (int k = 0; k < lock::EMPTY_COUNT; k++) {
        tapRect(setui::chipRect(k));
        const uint8_t want = (uint8_t)(lock::EMPTY_FIRST + k);
        if (lock::config().empty != want) {
          printf("LOCK FAIL: chip %d set the empty screen to %d, wanted %d\n", k,
                 lock::config().empty, want);
          abort();
        }
      }
      tapRect(setui::chipRect(was - lock::EMPTY_FIRST));
    }

    // The picture row is the one that goes somewhere: to the card's list of
    // pictures, which is where a picture comes from now. The phone route still
    // exists at the foot of that page -- a device with an empty slot has no
    // other way in -- and it still hands over to the notes tool's pairing
    // screen, because that is where the uploader lives. Settings has no web
    // server and should not grow one.
    tapRect(setui::sendRect());
    if (!toybox.hostInSettings() || toybox.hostSettings().hostPage() != 5) {
      printf("LOCK FAIL: the picture row did not open the card's list\n");
      abort();
    }
    if (lockimg::have()) {
      printf("LOCK FAIL: a lock picture existed before one was chosen\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_lock_from_card");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();
    g_dumpEnabled = false;
    tapRect(setui::wallRect(0));  // "mountains.tbi"
    if (!lockimg::have()) {
      printf("LOCK FAIL: choosing a file did not store a lock picture\n");
      abort();
    }
    // The page now says WHICH picture: the miniature is up and the chosen
    // row wears the tick (the pref carries the name the tick matches).
    {
      char src[40] = "";
      stickyHost.prefs().getString("lk_src", src, sizeof(src));
      if (strcmp(src, "mountains.tbi") != 0) {
        printf("LOCK FAIL: the chosen name was not remembered ('%s')\n", src);
        abort();
      }
    }
    g_dumpEnabled = true;
    setScreen("settings_lock_chosen");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();
    g_dumpEnabled = false;
    // And the wallpaper is untouched by it: one list, two destinations, and
    // the failure worth guarding against is the two of them being one file.
    if (wallimg::have()) {
      printf("LOCK FAIL: choosing a lock picture wrote the wallpaper too\n");
      abort();
    }
    tapRect(setui::wallRemoveRect());
    if (lockimg::have()) {
      printf("LOCK FAIL: REMOVE left the lock picture behind\n");
      abort();
    }
    tapRect(setui::lockPhoneRect());
    if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "NOTES") != 0) {
      printf("LOCK FAIL: the phone line did not open the notes pairing\n");
      abort();
    }
    toybox.goHub();
    toybox.openSettings();  // back into settings...
    tapRect(setui::actionRect(setui::ACT_LOCK));  // ...and back to the lock page

    // Back from the picture list goes up to the lock page, not out of it: it
    // was opened from a row there, and landing on the settings root would lose
    // the place the person was standing in.
    tapRect(setui::sendRect());
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    if (!toybox.hostInSettings() || toybox.hostSettings().hostPage() != 1) {
      printf("LOCK FAIL: back from the picture list did not return to the lock page\n");
      abort();
    }

    // ...and back goes up one page rather than out of settings altogether.
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    if (!toybox.hostInSettings()) {
      printf("LOCK FAIL: back left settings instead of returning to it\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("lock screen ok (%d rows cycle, back goes up one page)\n", setui::LR_COUNT);
  }

  // --- the wallpaper page ---------------------------------------------------
  // Settings > WALLPAPER lists the card's .tbi files (the host build invents
  // two), copying one in must leave a valid wallpaper on the device, and
  // REMOVE must take it away again.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.hostHub().goHome();  // the hold that opens settings only fires here
    toybox.openSettings();
    g_dumpEnabled = true;
    setScreen("settings_wallpaper");
    tapRect(setui::actionRect(setui::ACT_WALL));
    g_dumpEnabled = false;
    if (wallimg::have()) {
      printf("WALLPAPER FAIL: a wallpaper existed before one was chosen\n");
      abort();
    }
    tapRect(setui::wallRect(0));  // "mountains.tbi"
    if (!wallimg::have()) {
      printf("WALLPAPER FAIL: choosing a file did not store a wallpaper\n");
      abort();
    }
    // The chosen picture must actually reach the home screen.
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // out of the wallpaper page
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // out of settings
    if (!toybox.atHubHome()) {
      printf("WALLPAPER FAIL: backing out of settings did not land home\n");
      abort();
    }
    toybox.openSettings();
    tapRect(setui::actionRect(setui::ACT_WALL));
    tapRect(setui::wallRemoveRect());
    if (wallimg::have()) {
      printf("WALLPAPER FAIL: REMOVE left the wallpaper behind\n");
      abort();
    }
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    g_dumpEnabled = true;
    printf("wallpaper page ok (list, choose, remove, home shows it)\n");
  }

  // --- files over wifi -------------------------------------------------------
  // The settings page that hands the card to a phone. What matters here is not
  // the pairing -- that is the same portal every other screen uses -- but the
  // bus discipline: the card is claimed lazily, held across a burst of work,
  // and handed back before anything repaints. And the path rules, which are
  // the only thing standing between a phone and the rest of the card.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.openSettings();
    tapRect(setui::actionRect(setui::ACT_FILES));
    if (toybox.hostSettings().hostPage() != 4) {
      printf("FILES FAIL: the row did not open the files page\n");
      abort();
    }
    auto& fs = toybox.hostSettings().hostFiles();
    g_dumpEnabled = true;
    setScreen("settings_files");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // A phone joins: the page advances by itself, exactly like notes pairing.
    if (!toybox.wantsTick()) {
      printf("FILES FAIL: settings did not ask for ticks while serving\n");
      abort();
    }
    toybox.tick();
    if (!fs.hasClient()) {
      printf("FILES FAIL: no phone joined\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_files_joined");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // What the card looked like before the phone touched it.
    ToolsHost::SdFile before[48];
    const int nBefore = fs.hostList(before, 48);
    if (nBefore < 2) {
      printf("FILES FAIL: the invented card listed %d files\n", nBefore);
      abort();
    }

    // A real send, through the same host calls an upload handler makes.
    if (!fs.hostUpload("books", "walden.epub", 812000)) {
      printf("FILES FAIL: upload refused\n");
      abort();
    }
    if (!fs.hostRename("/books/one-piece-v1.tbk", "one-piece-01.tbk")) {
      printf("FILES FAIL: rename refused\n");
      abort();
    }
    if (!fs.hostDelete("/wallpapers/mountains.tbi")) {
      printf("FILES FAIL: delete refused\n");
      abort();
    }

    // The path rules. A phone may name a file, never a place: anything that
    // could climb out of the three known folders has to be refused.
    if (fs.hostUpload("books", "../../secret.bin", 10) ||
        fs.hostUpload("books", "sub/dir.epub", 10) || fs.hostUpload("etc", "x.epub", 10) ||
        fs.hostDelete("/books/../wind.epub") || fs.hostDelete("/nvs/key") ||
        fs.hostRename("/books/wind.epub", "../out.epub")) {
      printf("FILES FAIL: a path a phone should never reach was accepted\n");
      abort();
    }
    // ...and a rename onto a name already taken must not eat the other book.
    if (fs.hostRename("/books/wind.epub", "walden.epub")) {
      printf("FILES FAIL: rename overwrote an existing file\n");
      abort();
    }

    ToolsHost::SdFile after[48];
    const int nAfter = fs.hostList(after, 48);
    bool sawNew = false, sawRenamed = false, sawDeleted = true;
    for (int i = 0; i < nAfter; i++) {
      if (strcmp(after[i].path, "/books/walden.epub") == 0 && after[i].size == 812000)
        sawNew = true;
      if (strcmp(after[i].path, "/books/one-piece-01.tbk") == 0) sawRenamed = true;
      if (strcmp(after[i].path, "/wallpapers/mountains.tbi") == 0) sawDeleted = false;
    }
    if (nAfter != nBefore || !sawNew || !sawRenamed || !sawDeleted) {
      printf("FILES FAIL: card is %d files, new %d renamed %d deleted %d\n", nAfter, sawNew,
             sawRenamed, sawDeleted);
      abort();
    }

    // The bus is still held -- a burst is one claim -- and nothing may have
    // repainted while it was.
    if (!fs.holdingBus()) {
      printf("FILES FAIL: the card was let go mid-burst\n");
      abort();
    }
    // Quiet for long enough, and it hands the card back and asks to repaint.
    bool released = false;
    for (int i = 0; i < 8 && !released; i++) {
      if (toybox.hostSettings().tick(stickyHost)) released = true;
    }
    if (!released || fs.holdingBus()) {
      printf("FILES FAIL: the card was never handed back\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_files_done");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // DONE leaves, and the access point must not outlive the screen.
    tapRect(setui::filesDoneRect());
    if (toybox.hostSettings().hostPage() != 0 || fs.hasClient()) {
      printf("FILES FAIL: DONE did not close the session\n");
      abort();
    }
    // ...and so must leaving settings by any other door.
    tapRect(setui::actionRect(setui::ACT_FILES));
    toybox.goHub();
    if (toybox.hostSettings().hostFiles().hasClient()) {
      printf("FILES FAIL: the access point outlived settings\n");
      abort();
    }
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("files over wifi ok (one claim per burst, handed back, paths refused)\n");
  }

  // --- setting the clock from a phone ---------------------------------------
  // The device has no network time. Until now the only way to set its clock
  // was to send it a NOTE -- the notes page posts the phone's local time
  // alongside the text -- which nobody could be expected to guess. This is the
  // same handover as a page of its own, so the guard is that the row exists,
  // says what the device believes before it is tapped, and that the number
  // reaches the firmware's hook rather than merely lighting up a screen.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.hostHub().goHome();

    // A device that has never been told the time says so, in the row.
    sensors::hostSetClock(false);
    g_clockSetMs = 0;
    toybox.openSettings();
    g_dumpEnabled = true;
    setScreen("settings_clock_unset");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    tapRect(setui::actionRect(setui::ACT_CLOCK));
    if (toybox.hostSettings().hostPage() != 6) {
      printf("CLOCK FAIL: the row did not open the clock page\n");
      abort();
    }
    auto& cs = toybox.hostSettings().hostClock();
    g_dumpEnabled = true;
    setScreen("settings_clock");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    if (!toybox.wantsTick()) {
      printf("CLOCK FAIL: settings did not ask for ticks while serving\n");
      abort();
    }
    toybox.tick();  // a phone joins: step one becomes step two
    if (!cs.hasClient()) {
      printf("CLOCK FAIL: no phone joined\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_clock_joined");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // The phone's page sends by itself. Nothing is pressed here on purpose:
    // if this ever needs a tap, this guard stops passing.
    for (int i = 0; i < 4 && !cs.wasSet(); i++) toybox.tick();
    if (!cs.wasSet() || g_clockSetMs == 0) {
      printf("CLOCK FAIL: the page reached %s, the hook got %lld\n",
             cs.wasSet() ? "set" : "unset", (long long)g_clockSetMs);
      abort();
    }
    // The number is a local wall-clock in milliseconds, not seconds and not
    // UTC-with-an-offset-still-to-apply: 09:41 on the phone must arrive as
    // 09:41. Checked against the epoch rather than the RTC, because the host
    // has no RTC to read it back from.
    const int64_t mins = (g_clockSetMs / 60000) % (24 * 60);
    if (mins != 9 * 60 + 41) {
      printf("CLOCK FAIL: the phone's 09:41 arrived as %02d:%02d\n", (int)(mins / 60),
             (int)(mins % 60));
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_clock_set");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // DONE leaves, and the access point must not outlive the screen -- the
    // same rule the files page has, and the same way of getting it wrong.
    tapRect(setui::clockDoneRect());
    if (toybox.hostSettings().hostPage() != 0 || cs.hasClient()) {
      printf("CLOCK FAIL: DONE did not close the session\n");
      abort();
    }

    // And the answer the device gave for a year without anyone seeing it: the
    // phone sends, the chip does not keep it, and the page used to say "THE
    // CLOCK IS SET" over an empty space where the digits belong. It must say
    // what happened instead.
    g_clockRefuses = true;
    sensors::hostSetClock(false);
    g_clockSetMs = 0;
    tapRect(setui::actionRect(setui::ACT_CLOCK));
    for (int i = 0; i < 5 && !toybox.hostSettings().hostClock().wasRefused(); i++) toybox.tick();
    auto& cs2 = toybox.hostSettings().hostClock();
    if (!cs2.wasRefused() || cs2.wasSet() || g_clockSetMs == 0) {
      printf("CLOCK FAIL: a refused write reported set=%d refused=%d hook=%lld\n",
             cs2.wasSet() ? 1 : 0, cs2.wasRefused() ? 1 : 0, (long long)g_clockSetMs);
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_clock_refused");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(setui::clockDoneRect());
    g_clockRefuses = false;
    tapRect(setui::actionRect(setui::ACT_CLOCK));
    toybox.goHub();
    if (toybox.hostSettings().hostClock().hasClient()) {
      printf("CLOCK FAIL: the access point outlived settings\n");
      abort();
    }
    toybox.hostHub().goHome();
    sensors::hostSetClock(true);  // back to 09:41 for every shot after this
    g_dumpEnabled = true;
    printf("clock from phone ok (row reads it, page sets it, ap closes)\n");
  }

  // --- the book reader ------------------------------------------------------
  // The .tbk reader against the host's two invented volumes: the list, a page,
  // the turn zones (including the right-to-left swap), the side buttons, the
  // ends of the book, and the position surviving a trip out and back in.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);  // STUDY
    {
      // BOOKS is the drawer's first cell.
      const int rows = (4 + 1) / 2;
      int y0 = hubui::FOLDER_TOP +
               ((hubui::FOLDER_BOTTOM - hubui::FOLDER_TOP) - rows * hubui::ROW_STEP) / 2;
      g_dumpEnabled = true;
      setScreen("tool_books_list");
      toybox.onTap(EPD_W / 4, y0 + hubui::TILE / 2);
    }
    BookTool* bt = static_cast<BookTool*>(toybox.hostActive());
    if (!toybox.hostInApp() || toybox.hostIdx() != 9 || bt->hostScreen() != 0) {
      printf("BOOK FAIL: the STUDY drawer's first cell did not open the list\n");
      abort();
    }

    // First open shows the HOW TO READ card -- the page view has no chrome,
    // so nothing else ever says what the power button does. GOT IT dismisses
    // it for this run without suppressing it for the next.
    setScreen("tool_books_help");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();
    toybox.onTap(help::OK_BTN.x + 10, help::OK_BTN.y + 10);
    if (help::suppressed(stickyHost.prefs(), "bk")) {
      printf("BOOK FAIL: GOT IT suppressed the card for ever\n");
      abort();
    }

    // The top shelf is a series folder and the two loose books; the folder
    // opens onto its own list, which needs two pages.
    if (bt->hostFolders() != 1 || bt->hostItems() != 3) {
      printf("BOOK FAIL: the top shelf is %d folders and %d rows\n", bt->hostFolders(),
             bt->hostItems());
      abort();
    }
    setScreen("tool_books_series");
    toybox.onTap(240, shelf::Y0 + 10);  // the series row
    if (strcmp(bt->hostDir(), "/books/One Piece") != 0 || bt->hostFolders() != 0 ||
        bt->hostItems() != 9) {
      printf("BOOK FAIL: the series opened as '%s' with %d rows\n", bt->hostDir(),
             bt->hostItems());
      abort();
    }
    // MORE goes to the second page, BACK returns; nine books over seven rows.
    setScreen("tool_books_series_p2");
    toybox.onTap(EPD_W - 16 - shelf::PAGER_W / 2, shelf::PAGER_Y + shelf::PAGER_H / 2);
    if (bt->hostListPage() != 1) {
      printf("BOOK FAIL: MORE did not turn the list page\n");
      abort();
    }
    g_dumpEnabled = false;
    toybox.onTap(16 + shelf::PAGER_W / 2, shelf::PAGER_Y + shelf::PAGER_H / 2);
    if (bt->hostListPage() != 0) {
      printf("BOOK FAIL: BACK did not return to the first list page\n");
      abort();
    }

    // Open the manga (right-to-left, 12 pages) and read a few pages.
    g_dumpEnabled = true;
    setScreen("tool_books_page");
    toybox.onTap(240, bookui::LIST_Y0 + 10);
    if (bt->hostScreen() != 1 || bt->hostPage() != 0) {
      printf("BOOK FAIL: opening a book did not land on its first page\n");
      abort();
    }
    // A first open builds the cover mid-open but no longer repaints for it:
    // the loading face stays as painted and the page is the next thing the
    // panel shows. The cover must still exist afterwards.
    if (!bthumb::have("/books/One Piece/one-piece-v1.tbk")) {
      printf("BOOK FAIL: the first open did not build the cover\n");
      abort();
    }
    // A 1-bit page still arrives whole -- 48 KB is a block this device can
    // find, and the blit wants it contiguous.
    if (bt->hostPageBufBytes() != 48000) {
      printf("BOOK FAIL: a 1-bit book holds %u bytes, not one page\n",
             (unsigned)bt->hostPageBufBytes());
      abort();
    }
    g_dumpEnabled = false;

    // This volume carries a cover made on a PC, and that is what the card's
    // full-size cover must be -- byte for byte, because a 480x800 one-bit
    // source neither scales nor dithers on the way through. Page 0 would
    // differ in every band, which is exactly the bug this guards.
    {
      uint8_t* want = (uint8_t*)malloc(48000);
      uint8_t* got = (uint8_t*)malloc(48000);
      char bp[48];
      bthumb::bigPath("/books/One Piece/one-piece-v1.tbk", bp, sizeof(bp));
      if (!want || !got || !stickyHost.bookCover(want)) {
        printf("TBKCOVER FAIL: the book did not hand back its embedded cover\n");
        abort();
      }
      if (stickyHost.sdReadWhole(bp, got, 48000) != 48000 || memcmp(want, got, 48000) != 0) {
        printf("TBKCOVER FAIL: the stored cover is not the embedded one\n");
        abort();
      }
      // ...and a book with no embedded cover still gets one from page 0.
      uint8_t* p0 = (uint8_t*)malloc(48000);
      char wp[48];
      bthumb::bigPath("/books/walden.tbk", wp, sizeof(wp));
      if (p0 && stickyHost.sdReadWhole(wp, p0, 48000) == 48000 &&
          memcmp(want, p0, 48000) == 0) {
        printf("TBKCOVER FAIL: a book with no cover borrowed another book's\n");
        abort();
      }
      free(p0);
      free(want);
      free(got);
    }

    // A build that fails must leave nothing behind: no thumbnail standing in
    // front of a cover that was never written, and no permanent marker either.
    // Nothing in this reader decodes, so a failure is the card or the heap
    // having a bad moment, and the right answer is to try again next time.
    {
      const char* victim = "/books/walden.tbk";
      char sp[24];
      bthumb::path(victim, sp, sizeof(sp));
      tfs::remove(sp);
      uint8_t* page = (uint8_t*)malloc(48000);
      memset(page, 0xFF, 48000);
      sdcard::hostFailNextStreamClose();
      if (bthumb::makeAndSave(stickyHost, victim, page, 1)) {
        printf("TBKCOVER FAIL: a failed stream close was reported as success\n");
        abort();
      }
      if (bthumb::have(victim)) {
        printf("TBKCOVER FAIL: a thumbnail survived a cover that was never written\n");
        abort();
      }
      if (bthumb::failed(victim)) {
        printf("TBKCOVER FAIL: a passing failure was marked permanent\n");
        abort();
      }
      // And the retry works, which is the whole point of not marking it.
      if (!bthumb::makeAndSave(stickyHost, victim, page, 1) || !bthumb::have(victim)) {
        printf("TBKCOVER FAIL: the retry did not make the cover\n");
        abort();
      }
      free(page);
    }
    // RTL: forward is the LEFT third; the right third must go nowhere at the
    // cover.
    toybox.onTap(EPD_W - 20, 400);
    if (bt->hostPage() != 0) {
      printf("BOOK FAIL: an rtl book paged forward off the right edge\n");
      abort();
    }
    toybox.onTap(20, 400);   // forward
    toybox.onTap(20, 400);   // forward
    if (bt->hostPage() != 2) {
      printf("BOOK FAIL: two left taps should stand on page 3 of a manga\n");
      abort();
    }
    toybox.onButton(SideBtn::Down);  // DOWN is always forward
    if (bt->hostPage() != 3) {
      printf("BOOK FAIL: DOWN did not page forward\n");
      abort();
    }
    toybox.onButton(SideBtn::Up);
    if (bt->hostPage() != 2) {
      printf("BOOK FAIL: UP did not page back\n");
      abort();
    }
    // The middle toggles the footer chrome; capture it once.
    g_dumpEnabled = true;
    setScreen("tool_books_footer");
    toybox.onTap(240, 400);
    g_dumpEnabled = false;
    toybox.onTap(240, 400);  // and away again

    // Leave through the corner, reopen: the book must remember page 3.
    toybox.onTap(20, 20);
    if (bt->hostScreen() != 0) {
      printf("BOOK FAIL: the corner did not leave the page view\n");
      abort();
    }
    toybox.onTap(240, bookui::LIST_Y0 + 10);
    if (bt->hostPage() != 2) {
      printf("BOOK FAIL: the book forgot its page across a close\n");
      abort();
    }
    toybox.onTap(20, 20);
    // The back arrow on a series list climbs out of the folder rather than
    // out of the app.
    toybox.onTap(20, 20);
    if (!toybox.hostInApp() || strcmp(bt->hostDir(), "/books") != 0) {
      printf("BOOK FAIL: back did not climb out of the series\n");
      abort();
    }

    // The grey volume: the host build has no grey waveform, so opening it must
    // land on the 1-bit fallback dither -- which is also the render captured.
    g_dumpEnabled = true;
    setScreen("tool_books_grey");
    toybox.onTap(240, bookui::LIST_Y0 + 2 * bookui::LIST_ROW_H + 10);
    if (bt->hostScreen() != 1) {
      printf("BOOK FAIL: the grey book did not open\n");
      abort();
    }
    // And it holds NOTHING. A grey page is 96,000 bytes; asking for that in
    // one run is what stopped every grey book on the owner's card from
    // opening at all, so the page goes to the panel -- and to the canvas --
    // a band at a time and is never assembled. If this ever goes non-zero
    // again, grey books are one fragmented heap away from being unopenable.
    if (bt->hostPageBufBytes() != 0) {
      printf("BOOK FAIL: a grey book allocated %u bytes of page buffer\n",
             (unsigned)bt->hostPageBufBytes());
      abort();
    }
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);
    if (bt->hostPage() != 1) {
      printf("BOOK FAIL: the grey book did not turn\n");
      abort();
    }
    // --- the panel behind the power button ----------------------------------
    // The same panel the EPUB reader carries, minus the two things a .tbk has
    // no opinion about: no chapters to list, no type to set.
    g_dumpEnabled = true;
    if (!toybox.onButton(SideBtn::Ok) || bt->hostMenu() == 0) {
      printf("BOOK FAIL: OK did not open the panel\n");
      abort();
    }
    setScreen("tool_books_options");
    stickyHost.refresh(true);
    checkCornerClock("tbk");

    // Keep this page, from the + on the bookmarks row, and prove it reached
    // the card rather than a variable.
    g_dumpEnabled = false;
    toybox.onTap(rmenu::plusRect(1, 480).x + 40, rmenu::plusRect(1, 480).y + 40);
    if (bt->hostMarkCount() != 1) {
      printf("BOOK FAIL: keeping a page gave %d marks\n", bt->hostMarkCount());
      abort();
    }
    {
      char mp[48];
      marks::path("/books/grey-test.tbk", mp, sizeof(mp));
      uint8_t raw[marks::FILE_BYTES];
      if (stickyHost.sdReadFile(mp, raw, sizeof(raw)) != marks::FILE_BYTES ||
          memcmp(raw, "TBM3", 4) != 0 || raw[4] != 1) {
        printf("BOOK FAIL: no bookmark file on the card at %s\n", mp);
        abort();
      }
    }

    // The jump dial: fifties, tens and ones, and the side buttons do ones.
    toybox.onTap(240, rmenu::rootRect(0, 480).y + 40);
    if (bt->hostMenu() != (int)rmenu::Page::Contents) {
      printf("BOOK FAIL: the jump screen did not open\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_books_jump");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    {
      // The keypad. The grey book is three pages, so a 9 is a typo and must be
      // refused rather than clamped -- clamping a mis-key lands you somewhere
      // you did not ask for and looks like the device deciding for itself.
      auto key = [&](int i) {
        const TRect r = BookTool::hostKeyRect(i, 480);
        toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
      };
      const uint32_t before = bt->hostDialled();
      key(8);  // "9", a typo in a three-page book
      if (bt->hostDialled() != before) {
        printf("BOOK FAIL: 9 was accepted in a 3-page book (%lu -> %lu)\n",
               (unsigned long)before, (unsigned long)bt->hostDialled());
        abort();
      }
      key(2);  // "3", the last page
      if (bt->hostDialled() != 2) {
        printf("BOOK FAIL: keying 3 dialled %lu\n", (unsigned long)bt->hostDialled());
        abort();
      }
      key(11);  // BACK, clearing it
      key(1);   // "2"
      if (bt->hostDialled() != 1) {
        printf("BOOK FAIL: BACK then 2 dialled %lu\n", (unsigned long)bt->hostDialled());
        abort();
      }
      toybox.onTap(240, 680);  // GO
      if (bt->hostMenu() != 0 || bt->hostPage() != 1) {
        printf("BOOK FAIL: GO landed on page %lu (menu %d)\n", (unsigned long)bt->hostPage(),
               bt->hostMenu());
        abort();
      }
    }

    // Page turns is a row that cycles in place: three taps must come back to
    // where it started, and none of them may leave the panel.
    toybox.onButton(SideBtn::Ok);
    {
      const rmenu::Refresh was = rmenu::refreshMode(stickyHost.prefs(), false);
      for (int i = 0; i < 3; i++) {
        toybox.onTap(240, rmenu::rootRect(2, 480).y + 40);
        if (bt->hostMenu() == 0) {
          printf("BOOK FAIL: the page turns row left the panel\n");
          abort();
        }
      }
      if (rmenu::refreshMode(stickyHost.prefs(), false) != was) {
        printf("BOOK FAIL: three taps on page turns did not come back to %s\n",
               rmenu::refreshLabel(was));
        abort();
      }
    }

    // And the way out is the row under it -- row 4 now, with "Start again"
    // between the settings and the exit.
    toybox.onTap(240, rmenu::rootRect(4, 480).y + 40);
    if (bt->hostScreen() != 0) {
      printf("BOOK FAIL: CLOSE THE BOOK did not close it\n");
      abort();
    }

    // Now REOPEN it. A mark that is only in RAM looks identical to a mark on
    // the card until the book is closed, which is how a bookmark that was
    // never written once got all the way to hardware: the panel said "1 kept",
    // the beep sounded, and the card had nothing on it.
    toybox.onTap(240, bookui::LIST_Y0 + 2 * bookui::LIST_ROW_H + 10);
    if (bt->hostScreen() != 1) {
      printf("BOOK FAIL: the book did not reopen\n");
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    if (bt->hostMarkCount() != 1) {
      printf("BOOK FAIL: the kept page did not survive closing the book (%d marks)\n",
             bt->hostMarkCount());
      abort();
    }
    toybox.onTap(240, rmenu::rootRect(4, 480).y + 40);  // close it again
    printf("tbk panel ok (keypad refuses a typo, marks survive the book closing)\n");
    // ...and means nothing on the list, so main.cpp falls through to its own
    // uses of the button.
    if (toybox.onButton(SideBtn::Ok)) {
      printf("BOOK FAIL: OK was swallowed on the book list\n");
      abort();
    }
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("book reader ok (list, rtl turns, buttons, ends, position, grey fallback)\n");
    printf("tbk covers ok (embedded beats page 0, a failed build retries cleanly)\n");

    // --- a cover the owner put beside the book ------------------------------
    // This path shipped in beta.26 with no guard at all and crashed the device
    // on the first book that used it: it ran the finished picture through the
    // cover BUILDER, which meant 46 KB of heap and a 3,968-byte stack frame on
    // an 8 KB task. The assertion that would have caught it is the one below --
    // the cover on the card must be the sidecar's own bytes, unchanged. A
    // builder cannot produce that: it re-dithers, and a re-dither of a dithered
    // picture is never bit-identical.
    {
      const char* kBook = "/books/walden.tbk";
      uint8_t side[tbimg::FILE_SIZE];
      auto paint = [&](int seed) {
        side[0] = 'T'; side[1] = 'B'; side[2] = 'I'; side[3] = '1';
        side[4] = 480 & 255; side[5] = 480 >> 8;
        side[6] = 800 & 255; side[7] = 800 >> 8;
        uint8_t* bits = side + tbimg::HEADER;
        memset(bits, 0xFF, tbimg::BITS);
        // A wedge, so the thumbnail has both tones and the stretch has work.
        for (int y = 0; y < 800; y++)
          for (int xb = 0; xb < 60; xb++)
            if (xb < (y + seed * 40) * 60 / 800) bits[(size_t)y * 60 + xb] = 0x00;
      };
      paint(0);
      sdcard::hostPutCardFile("/books/walden.cover.tbi", side, sizeof(side));

      if (!bthumb::coverFromSidecar(stickyHost, kBook)) {
        printf("SIDECAR FAIL: a sidecar beside the book was not picked up\n");
        abort();
      }
      char big[48];
      bthumb::bigPath(kBook, big, sizeof(big));
      if (sdcard::hostCardFileSize(big) != bthumb::BIG_BYTES) {
        printf("SIDECAR FAIL: the cover on the card is %d bytes\n",
               sdcard::hostCardFileSize(big));
        abort();
      }
      uint8_t got[600];
      for (int off = 0; off < bthumb::BIG_BYTES; off += (int)sizeof(got)) {
        if (stickyHost.sdReadSlice(big, (uint32_t)off, got, sizeof(got)) != (int)sizeof(got) ||
            memcmp(got, side + tbimg::HEADER + off, sizeof(got)) != 0) {
          {
            int k = 0;
            while (k < (int)sizeof(got) && got[k] == side[tbimg::HEADER + off + k]) k++;
            printf("SIDECAR FAIL: differs at byte %d (+%d): card %02x want %02x\n", off, k,
                   got[k], side[tbimg::HEADER + off + k]);
          }
          abort();
        }
      }
      if (!bthumb::have(kBook)) {
        printf("SIDECAR FAIL: no strip thumbnail was written\n");
        abort();
      }
      // Replacing the file on the card must show up: the owner iterating on a
      // cover should not have to delete a cache they cannot see.
      paint(1);
      sdcard::hostPutCardFile("/books/walden.cover.tbi", side, sizeof(side));
      bthumb::coverFromSidecar(stickyHost, kBook);
      if (stickyHost.sdReadSlice(big, 0, got, sizeof(got)) != (int)sizeof(got) ||
          memcmp(got, side + tbimg::HEADER, sizeof(got)) != 0) {
        printf("SIDECAR FAIL: a replaced sidecar was not picked up\n");
        abort();
      }
      printf("sidecar covers ok (copied byte for byte, replaced when it changes)\n");
    }
  }

  // --- the .tbk header ---------------------------------------------------------
  // The rules a PC-side converter has to match, tested on bytes rather than
  // through a card. dataOffset was in the format from the start but assumed
  // rather than read until covers arrived, so its edge cases are the ones a
  // file in the wild is most likely to land on.
  {
    g_dumpEnabled = false;
    auto header = [](uint8_t bpp, uint8_t flags, uint32_t pages, uint32_t dataOff,
                     uint8_t* h) {
      memset(h, 0, 64);
      memcpy(h, "TBK1", 4);
      h[4] = 480 & 255; h[5] = 480 >> 8;
      h[6] = 800 & 255; h[7] = 800 >> 8;
      h[8] = bpp;
      h[9] = flags;
      h[12] = (uint8_t)(pages & 255); h[13] = (uint8_t)(pages >> 8);
      const uint32_t pb = 48000u * bpp;
      h[16] = (uint8_t)(pb & 255); h[17] = (uint8_t)((pb >> 8) & 255);
      h[18] = (uint8_t)((pb >> 16) & 255); h[19] = (uint8_t)((pb >> 24) & 255);
      h[20] = (uint8_t)(dataOff & 255); h[21] = (uint8_t)((dataOff >> 8) & 255);
      h[22] = (uint8_t)((dataOff >> 16) & 255); h[23] = (uint8_t)((dataOff >> 24) & 255);
      strcpy((char*)h + 24, "A Title");
    };
    uint8_t h[64];
    sdcard::BookMeta m;

    // The ordinary file every converter has written until now.
    header(1, 0, 12, 64, h);
    if (!sdcard::parseTbkBytes(h, m) || m.cover || m.dataOffset != 64 || m.rtl) {
      printf("TBKHDR FAIL: a plain header parsed as cover=%d off=%u\n", (int)m.cover,
             (unsigned)m.dataOffset);
      abort();
    }
    // One with a cover: flag set, pages pushed past it.
    header(1, 2, 12, 64 + 48000, h);
    if (!sdcard::parseTbkBytes(h, m) || !m.cover || m.dataOffset != 64 + 48000) {
      printf("TBKHDR FAIL: a cover header parsed as cover=%d off=%u\n", (int)m.cover,
             (unsigned)m.dataOffset);
      abort();
    }
    // Both flags together: rtl and a cover are independent bits.
    header(1, 1 | 2, 12, 64 + 48000, h);
    if (!sdcard::parseTbkBytes(h, m) || !m.cover || !m.rtl) {
      printf("TBKHDR FAIL: rtl and cover did not survive together\n");
      abort();
    }
    // Claims a cover but leaves no room for one: the flag is dropped rather
    // than believed, because believing it reads page 0 out of the middle of
    // the cover.
    header(1, 2, 12, 64, h);
    if (!sdcard::parseTbkBytes(h, m) || m.cover || m.dataOffset != 64) {
      printf("TBKHDR FAIL: an impossible cover claim was believed\n");
      abort();
    }
    // A converter that never filled the field in. Treated as 64, not refused.
    header(1, 0, 12, 0, h);
    if (!sdcard::parseTbkBytes(h, m) || m.dataOffset != 64) {
      printf("TBKHDR FAIL: a zero dataOffset was not read as 64\n");
      abort();
    }
    // Still refused: wrong magic, a page size that contradicts the depth, and
    // a book with no pages.
    header(1, 0, 12, 64, h);
    h[0] = 'X';
    if (sdcard::parseTbkBytes(h, m)) { printf("TBKHDR FAIL: bad magic accepted\n"); abort(); }
    header(2, 0, 12, 64, h);
    h[16] = 0x80; h[17] = 0xBB;  // 48,000 on a 2-bpp file, which needs 96,000
    if (sdcard::parseTbkBytes(h, m)) { printf("TBKHDR FAIL: a lying page size was accepted\n"); abort(); }
    header(1, 0, 0, 64, h);
    if (sdcard::parseTbkBytes(h, m)) { printf("TBKHDR FAIL: an empty book was accepted\n"); abort(); }
    g_dumpEnabled = true;
    printf("tbk header ok (dataOffset honoured, cover flag checked against it)\n");
  }

  // --- the EPUB core: CrossPoint parity --------------------------------------
  // The hash and the visible-codepoint counting are the two things that make a
  // reading position portable between firmwares. Both are pinned to exact
  // values here: the hash against numbers taken from a real 32-bit libstdc++
  // (g++ -m32, the same _Hash_bytes the ESP32 toolchains carry), the counting
  // against hand-derived offsets in the invented book's first chapter.
  {
    struct HV { const char* s; uint32_t h; };
    // std::hash<std::string> observed under g++ -m32:
    static const HV VECS[] = {{"/books/x.epub", 1473393747u},
                              {"/Alice in Wonderland.epub", 1076398746u},
                              {"/books/One Piece vol 1.epub", 2644013571u},
                              {"", 3990065800u},
                              {"a", 2167009006u},
                              {"/books/wind.epub", 836526750u}};
    for (const HV& v : VECS) {
      if (epubc::cpHash(v.s, strlen(v.s)) != v.h) {
        printf("EPUB FAIL: cpHash(\"%s\") = %u, CrossPoint would use %u\n", v.s,
               epubc::cpHash(v.s, strlen(v.s)), v.h);
        abort();
      }
    }
    // The KOReader sidecar's path derivation, which is a different rule from
    // CrossPoint's hash: strip the last suffix, add .sdr, and keep the suffix
    // in the file name exactly as the card spells it.
    {
      struct MP { const char* book; const char* want; };
      static const MP CASES[] = {
          {"/books/wind.epub", "/books/wind.sdr/metadata.epub.lua"},
          {"/books/Uketsu/strange-houses.epub",
           "/books/Uketsu/strange-houses.sdr/metadata.epub.lua"},
          // Case is carried through: KOReader derives its name from the same
          // string, so lower-casing here is the one way to disagree.
          {"/books/Loud.EPUB", "/books/Loud.sdr/metadata.EPUB.lua"},
          // Dots in the title must not be mistaken for the suffix.
          {"/books/Vol.1 - Title.epub", "/books/Vol.1 - Title.sdr/metadata.epub.lua"},
      };
      char got[160];
      for (const MP& c : CASES) {
        if (!ksdr::metaPath(c.book, got, sizeof(got)) || strcmp(got, c.want) != 0) {
          printf("KSDR FAIL: %s -> %s, wanted %s\n", c.book, got, c.want);
          abort();
        }
      }
      // No suffix means no name KOReader would ever look for.
      if (ksdr::metaPath("/books/noext", got, sizeof(got))) {
        printf("KSDR FAIL: a suffixless book got a sidecar path\n");
        abort();
      }
      // The percentage is clamped, and a clamped value still has to be Lua a
      // dofile() will accept -- no "nan", no exponent.
      ksdr::State st;
      st.percent = 1.7;
      char lua[256];
      if (ksdr::render(st, lua, sizeof(lua)) <= 0 || !strstr(lua, "= 1.000000,") ||
          strstr(lua, "e-") || strstr(lua, "nan")) {
        printf("KSDR FAIL: an out-of-range percentage rendered as:\n%s\n", lua);
        abort();
      }
      st.percent = 0.0 / 0.0;  // NaN: would render as "nan" and break dofile
      if (ksdr::render(st, lua, sizeof(lua)) <= 0 || strstr(lua, "nan")) {
        printf("KSDR FAIL: NaN reached the sidecar\n");
        abort();
      }
    }

    // Both hash generations, pinned against independent references. The FNV
    // vector is CrossInk's current directory name (fnvHash64, decimal); the
    // Murmur one is the 32-bit libstdc++ std::hash older CrossPoint used, kept
    // for reading old cards.
    char dir[96];
    epubc::cacheDir("/books/wind.epub", dir, sizeof(dir));
    if (strcmp(dir, "/.crosspoint/epub_1175141337288249041") != 0) {
      printf("EPUB FAIL: FNV cache dir is %s\n", dir);
      abort();
    }
    if (epubc::fnvHash64("abc", 3) != 16654208175385433931ull) {
      printf("EPUB FAIL: fnvHash64 drifted\n");
      abort();
    }
    epubc::cacheDirLegacy("/books/wind.epub", dir, sizeof(dir));
    if (strcmp(dir, "/.crosspoint/epub_836526750") != 0) {
      printf("EPUB FAIL: legacy cache dir is %s\n", dir);
      abort();
    }

    // progress.bin round-trip, all three sizes CrossPoint writes.
    {
      epubc::Progress p;
      uint8_t b10[10] = {3, 0, 7, 0, 20, 0, 0x39, 0x30, 0, 0};  // spine 3, page 7/20, off 12345
      if (!epubc::decodeProgress(b10, 10, p) || p.spine != 3 || p.page != 7 || p.pageCount != 20 ||
          !p.hasOffset || p.offset != 12345) {
        printf("EPUB FAIL: 10-byte progress decoded wrong\n");
        abort();
      }
      uint8_t out[10];
      if (epubc::encodeProgress(p, out) != 10 || memcmp(out, b10, 10) != 0) {
        printf("EPUB FAIL: progress did not round-trip\n");
        abort();
      }
      uint8_t b6[6] = {1, 0, 0xFF, 0xFF, 9, 0};  // the last-page sentinel must clear
      if (!epubc::decodeProgress(b6, 6, p) || p.spine != 1 || p.page != 0 || p.hasOffset) {
        printf("EPUB FAIL: 6-byte progress decoded wrong\n");
        abort();
      }
    }

    // The word stream, against the invented book. Chapter one is stored (its
    // bytes are the fixture); chapter two is a real deflate blob, so tinfl
    // streams compressed data even on this build.
    struct MemIO : epubc::IO {
      int read(uint32_t pos, void* dst, uint32_t n) override {
        return stickyHost.epubRead(pos, dst, n);
      }
      uint32_t size() override { return stickyHost.epubSize(); }
    } mio;
    if (!stickyHost.epubOpen("/books/wind.epub")) {
      printf("EPUB FAIL: the invented epub did not open\n");
      abort();
    }
    epubc::Book book;
    if (!book.open(mio) || book.spineCount() != 3) {
      printf("EPUB FAIL: open: %s (spine %d)\n", book.error(), book.spineCount());
      abort();
    }
    struct Tok { int t; const char* w; uint32_t off; };
    // Hand-derived from kFakeCh1 in sdcard.cpp: offsets count every codepoint
    // inside <body> (newlines between tags included), &nbsp; glues "two three"
    // into one token, &#233; and &amp; decode, head/style/title text does not
    // count at all.
    static const Tok WANT[] = {{epubc::TOK_WORD, "One", 1},   {epubc::TOK_WORD, "two three", 5},
                               {epubc::TOK_PARA, "", 0},      {epubc::TOK_WORD, "caf\xC3\xA9", 15},
                               {epubc::TOK_WORD, "&", 20},    {epubc::TOK_WORD, "more", 22},
                               {epubc::TOK_PARA, "", 0},
                               // The picture, and then "ende" STILL at 27: an
                               // <img> adds no codepoints, which is the whole
                               // reason artwork could be added to this reader
                               // without moving anyone's CrossPoint bookmark.
                               {epubc::TOK_IMAGE, "OEBPS/images/plate.png", 27},
                               {epubc::TOK_IMAGE, "OEBPS/images/missing.png", 27},
                               {epubc::TOK_WORD, "ende", 27},
                               {epubc::TOK_PARA, "", 0},      {epubc::TOK_END, "", 0}};
    if (!book.chapterOpen(0)) {
      printf("EPUB FAIL: chapter one did not open\n");
      abort();
    }
    char w[epubc::WORD_CAP];
    uint32_t off = 0;
    for (const Tok& want : WANT) {
      const int t = book.next(w, off);
      if (t == epubc::TOK_IMAGE &&
          (strcmp(book.imageName(), want.w) != 0 || off != want.off || want.t != t)) {
        printf("EPUB FAIL: image '%s'@%u, expected '%s'@%u\n", book.imageName(), off, want.w,
               want.off);
        abort();
      }
      if (t != want.t || (t == epubc::TOK_WORD && (strcmp(w, want.w) != 0 || off != want.off))) {
        printf("EPUB FAIL: expected %d '%s'@%u, got %d '%s'@%u\n", want.t, want.w, want.off, t,
               w, off);
        abort();
      }
    }
    // The contents, as the panel's jump list will show them: an EPUB3 nav
    // document, hrefs resolved to spine indices, a fragment (#top) ignored,
    // and the whitespace a publisher leaves inside an <a> folded away.
    {
      epubc::Book::TocEntry toc[8];
      const int n = book.tocRead(toc, 8);
      const struct { const char* title; int spine; } WANT_TOC[] = {
          {"One two three", 0}, {"The long one", 1}, {"A plate", 2}};
      if (n != 3) {
        printf("TOC FAIL: %d entries, wanted 3\n", n);
        abort();
      }
      for (int i = 0; i < 3; i++)
        if (strcmp(toc[i].title, WANT_TOC[i].title) != 0 || toc[i].spine != WANT_TOC[i].spine) {
          printf("TOC FAIL: entry %d is '%s' -> ch %d\n", i, toc[i].title, toc[i].spine);
          abort();
        }
      printf("epub contents ok (nav parsed, fragments dropped, titles tidied)\n");
    }

    // Chapter two through tinfl: 800 numbered words plus a five-word coda,
    // first word at offset 1 (the newline after <body> is offset 0).
    if (!book.chapterOpen(1)) {
      printf("EPUB FAIL: chapter two did not open\n");
      abort();
    }
    int words = 0;
    bool sawFirst = false;
    char last[epubc::WORD_CAP] = "";
    for (;;) {
      const int t = book.next(w, off);
      if (t == epubc::TOK_END) break;
      if (t == epubc::TOK_ERR) {
        printf("EPUB FAIL: chapter two errored mid-stream\n");
        abort();
      }
      if (t != epubc::TOK_WORD) continue;
      if (words == 0) sawFirst = (strcmp(w, "w0001") == 0 && off == 1);
      words++;
      strcpy(last, w);
    }
    if (words != 805 || !sawFirst || strcmp(last, "line") != 0) {
      printf("EPUB FAIL: chapter two gave %d words, last '%s'\n", words, last);
      abort();
    }
    // The cover: the fake OPF declares it EPUB2-style, and the entry is a
    // real baseline JPEG the tjpgd path must find and type correctly.
    if (book.coverType() != epubc::Book::COVER_JPEG || book.coverSize() == 0) {
      printf("EPUB FAIL: cover not found (type %d)\n", book.coverType());
      abort();
    }
    book.close();
    stickyHost.epubClose();
    printf("epub core ok (hash pinned, offsets exact, tinfl streams, cover found)\n");
    printf("koreader sidecar ok (paths derived, percentages clamped and finite)\n");
  }

  // --- the cover decoders ----------------------------------------------------
  // Each decode path against a known image: the PNG decoder (both colour
  // shapes), and the progressive-JPEG DC extractor, whose whole job is
  // commercial covers tjpgd refuses. The baseline-JPEG path is proven by the
  // reader test below, which must leave a stored thumbnail behind.
  {
    struct Mem {
      const uint8_t* d;
      uint32_t len, pos;
    };
    auto memRead = [](void* ctx, uint8_t* dst, int n) -> int {
      Mem* m = (Mem*)ctx;
      uint32_t take = m->len - m->pos;
      if ((uint32_t)n < take) take = (uint32_t)n;
      memcpy(dst, m->d + m->pos, take);
      m->pos += take;
      return (int)take;
    };
    struct Got {
      int w = 0, h = 0;
      uint8_t px[64 * 64] = {};
      static bool size(void* u, int w, int h) {
        ((Got*)u)->w = w;
        ((Got*)u)->h = h;
        return true;
      }
      static void row(void* u, int y, const uint8_t* g, int w) {
        Got* s = (Got*)u;
        if (y < 64 && w <= 64) memcpy(s->px + y * 64, g, (size_t)w);
      }
    };

    // PNG, truecolour and palette: left half white, right half black.
    for (const auto* blob : {&kTestPngRgb[0], &kTestPngPal[0]}) {
      const bool pal = blob == &kTestPngPal[0];
      Mem m{blob, pal ? (uint32_t)sizeof(kTestPngPal) : (uint32_t)sizeof(kTestPngRgb), 0};
      Got got;
      epng::In in{&m, memRead};
      if (!epng::decodeGray(in, Got::size, Got::row, &got) || got.w != 40 || got.h != 30 ||
          got.px[15 * 64 + 5] < 200 || got.px[15 * 64 + 35] > 50) {
        printf("EPUB COVER FAIL: png %s decoded wrong (%dx%d, l=%d r=%d)\n",
               pal ? "palette" : "rgb", got.w, got.h, got.px[15 * 64 + 5],
               got.px[15 * 64 + 35]);
        abort();
      }
    }

    // Progressive JPEG: the DC scan is the image at 1/8. Left half dark
    // (~60), right half light (~200), bright disc in the middle.
    {
      Mem m{kTestJpegProg, (uint32_t)sizeof(kTestJpegProg), 0};
      Got got;
      ejdc::In in{&m, memRead};
      if (!ejdc::decodeGray(in, Got::size, Got::row, &got) || got.w != 40 || got.h != 60) {
        printf("EPUB COVER FAIL: progressive DC scan (%dx%d)\n", got.w, got.h);
        abort();
      }
      const int l = got.px[10 * 64 + 5], r = got.px[10 * 64 + 35], c = got.px[30 * 64 + 20];
      if (l < 30 || l > 100 || r < 160 || r > 240 || c < 200) {
        printf("EPUB COVER FAIL: DC tones l=%d r=%d disc=%d\n", l, r, c);
        abort();
      }
    }
    printf("epub covers ok (png rgb+palette, progressive DC extractor)\n");
  }

  // --- the streaming cover builder -------------------------------------------
  // The thing that lets a cover be panel-sized at all: rows go in, two
  // pictures come out, and the memory in between is a band rather than the
  // whole image. Both directions matter -- covers arrive larger than the
  // panel (baseline JPEG) and much smaller (the progressive DC pass, which
  // only ever yields an eighth).
  {
    // The card holds the full-size covers, so the builder needs a session
    // holding the bus -- which on a real device is the book being opened.
    stickyHost.sdMgrOpen();
    auto feed = [](const char* file, int w, int h, int maxUp) {
      bthumb::Builder b;
      if (!b.begin(stickyHost, file, w, h, maxUp)) return false;
      uint8_t* line = (uint8_t*)malloc((size_t)w);
      for (int y = 0; y < h; y++) {
        // A frame around a diagonal: ink at the edges and on a line, so a
        // squashed or torn output is visible as a wrong count.
        for (int x = 0; x < w; x++) {
          const bool edge = x < 2 || y < 2 || x >= w - 2 || y >= h - 2;
          const bool diag = (x * h / w) == y;
          line[x] = (edge || diag) ? 0 : 255;
        }
        b.row(y, line, w);
      }
      free(line);
      return b.finish();
    };

    // Shrinking: twice the panel in each direction.
    if (!feed("/books/down.tbk", 960, 1600, 1) || !bthumb::haveBig(stickyHost, "/books/down.tbk") ||
        !bthumb::have("/books/down.tbk")) {
      printf("COVER BUILD FAIL: a shrunk cover did not come out whole\n");
      abort();
    }
    // Growing: a DC-sized image, which must fill the panel rather than float
    // small in the middle of it.
    if (!feed("/books/up.tbk", 176, 250, 6) || !bthumb::haveBig(stickyHost, "/books/up.tbk")) {
      printf("COVER BUILD FAIL: an enlarged cover did not come out whole\n");
      abort();
    }
    {
      // The enlarged one must actually reach the panel's edges: a frame two
      // pixels thick, spread 2.7x, has to leave ink on the outer columns.
      char p[48];
      bthumb::bigPath("/books/up.tbk", p, sizeof(p));
      uint8_t* buf = (uint8_t*)malloc(bthumb::BIG_BYTES);
      const int len = stickyHost.sdReadWhole(p, buf, bthumb::BIG_BYTES);
      if (len != bthumb::BIG_BYTES) {
        printf("COVER BUILD FAIL: enlarged cover is %d bytes\n", len);
        abort();
      }
      int inkRows = 0;
      for (int y = 0; y < bthumb::BIG_H; y++) {
        const uint8_t* row = buf + (size_t)y * (bthumb::BIG_W / 8);
        for (int x = 0; x < bthumb::BIG_W; x++)
          if (!(row[x >> 3] & (0x80 >> (x & 7)))) {
            inkRows++;
            break;
          }
      }
      free(buf);
      // 176x250 grown to fill 480 wide is 682 rows of picture in an 800-row
      // panel; every one of them carries at least the frame's edge.
      if (inkRows < 600) {
        printf("COVER BUILD FAIL: enlarged cover only reached %d rows\n", inkRows);
        abort();
      }
    }

    // The card has room for every book, so nothing is ever evicted -- the
    // old ten-cover cap was there because internal flash did not.
    for (int i = 0; i < 14; i++) {
      char name[32];
      snprintf(name, sizeof(name), "/books/many%02d.tbk", i);
      if (!feed(name, 240, 400, 1)) {
        printf("COVER BUILD FAIL: cover %d of the long run\n", i);
        abort();
      }
    }
    if (!bthumb::haveBig(stickyHost, "/books/down.tbk") ||
        !bthumb::haveBig(stickyHost, "/books/many00.tbk") ||
        !bthumb::haveBig(stickyHost, "/books/many13.tbk")) {
      printf("COVER BUILD FAIL: a cover went missing from the card\n");
      abort();
    }
    // The strip thumbnails stay in internal flash: the hub draws them with no
    // card session, and must never need one.
    if (!bthumb::have("/books/down.tbk")) {
      printf("COVER BUILD FAIL: the strip thumbnail is not in flash\n");
      abort();
    }
    stickyHost.sdMgrClose();
    // ...and with the card handed back, reading a cover claims the bus by
    // itself, which is what the loading screen relies on.
    if (!bthumb::haveBig(stickyHost, "/books/down.tbk")) {
      printf("COVER BUILD FAIL: a cover could not be read with no session open\n");
      abort();
    }
    printf("cover builder ok (shrinks, grows to fill, lives on the card)\n");
  }

  // --- the EPUB reader app ---------------------------------------------------
  // Open through the hub, read, turn, close; then the CrossPoint round-trip:
  // the position a turn writes must be the position a fresh open lands on,
  // through the same file a CrossPoint device would read.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);  // the STUDY drawer
    // EPUB is the drawer's second cell (row 0, right column). The book test
    // already put entries in recents, so Study is wearing the strip and its
    // tiles are top-anchored.
    toybox.onTap(3 * EPD_W / 4, hubui::FOLDER_TOP + hubui::TILE / 2);
    if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "EPUB") != 0) {
      printf("EPUB APP FAIL: the STUDY drawer's second cell did not open EPUB\n");
      abort();
    }
    auto* et = static_cast<EpubTool*>(toybox.hostActive());
    g_dumpEnabled = true;

    // The reader's first-open card, and DON'T SHOW AGAIN honoured: this one is
    // suppressed for good, which is what the settings row exists to undo.
    setScreen("tool_epub_help");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();
    toybox.onTap(help::NEVER_BTN.x + 10, help::NEVER_BTN.y + 10);
    if (!help::suppressed(stickyHost.prefs(), "ep")) {
      printf("EPUB APP FAIL: DON'T SHOW AGAIN did not stick\n");
      abort();
    }

    setScreen("tool_epub_list");
    stickyHost.refresh(true);

    // The EPUB shelf has its own folder -- one series, holding one book --
    // and the .tbk series is not on it, because each reader sees only its
    // own kind. Walk in and back out without opening anything.
    if (et->hostFolders() != 1 || et->hostItems() != 3) {
      printf("EPUB APP FAIL: the shelf is %d folders and %d rows\n", et->hostFolders(),
             et->hostItems());
      abort();
    }
    setScreen("tool_epub_series");
    toybox.onTap(240, shelf::Y0 + 10);
    if (strcmp(et->hostDir(), "/books/Uketsu") != 0 || et->hostItems() != 1) {
      printf("EPUB APP FAIL: the series opened as '%s' with %d rows\n", et->hostDir(),
             et->hostItems());
      abort();
    }
    g_dumpEnabled = false;
    toybox.onTap(20, 20);
    if (strcmp(et->hostDir(), "/books") != 0) {
      printf("EPUB APP FAIL: back did not climb out of the series\n");
      abort();
    }

    // Open the one invented book; it starts at the top of chapter one.
    toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
    if (et->hostScreen() != 1 || et->hostSpine() != 0 || et->hostPage() != 0) {
      printf("EPUB APP FAIL: the book did not open at the start (s%d p%d)\n", et->hostSpine(),
             et->hostPage());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_page");
    stickyHost.refresh(true);

    // Forward: chapter one's text is one page, and the illustration at the end
    // of it is the second -- a picture gets the whole glass rather than a
    // corner of a page of text.
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);
    if (et->hostSpine() != 0 || et->hostPage() != 1 ||
        strcmp(et->hostPageImage(), "OEBPS/images/plate.png") != 0) {
      printf("EPUB APP FAIL: DOWN did not land on the illustration (s%d p%d img '%s')\n",
             et->hostSpine(), et->hostPage(), et->hostPageImage());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_art");
    stickyHost.refresh(true);
    {
      // The picture is drawn, not merely announced: the fixture's .tbi is a
      // frame, both diagonals and a blob, so a page that fell back to the
      // "no picture prepared" plate carries a small fraction of this ink --
      // and a picture read a band short carries most of it, which is why the
      // guard also asks that the bottom rows are inked.
      int ink = 0;
      for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) ink += __builtin_popcount((uint8_t)~epd.fb()[i]);
      // The panel's own buffer is 800 across by 480 down, whichever way the
      // canvas is turned; the picture fills all but the 20-pixel margin its
      // frame leaves, so it inks about 440 of those 480 lines.
      int inkRows = 0;
      for (int y = 0; y < 480; y++)
        for (int xb = 0; xb < 100; xb++)
          if ((uint8_t)~epd.fb()[(size_t)y * 100 + xb]) {
            inkRows++;
            break;
          }
      if (ink < 9000 || inkRows < 400) {
        printf("EPUB APP FAIL: the illustration drew %d px over %d rows\n", ink, inkRows);
        abort();
      }
    }

    // The next picture is one the book carries no .tbi for, so the reader says
    // so by name rather than showing a blank page. Rendered here because a
    // plate is text, and text is what runs off the edge of a 480-pixel panel.
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);
    if (et->hostPage() != 2 || strcmp(et->hostPageImage(), "OEBPS/images/missing.png") != 0) {
      printf("EPUB APP FAIL: the unprepared picture is p%d img '%s'\n", et->hostPage(),
             et->hostPageImage());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_art_missing");
    stickyHost.refresh(true);

    // Back one: the two pictures share an offset with each other and with the
    // word after them, so only the count of pictures already passed says which
    // page this is. Get that wrong and the drawn one is unreachable.
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Up);
    if (et->hostPage() != 1 || strcmp(et->hostPageImage(), "OEBPS/images/plate.png") != 0) {
      printf("EPUB APP FAIL: UP between two pictures landed on p%d img '%s'\n", et->hostPage(),
             et->hostPageImage());
      abort();
    }
    toybox.onButton(SideBtn::Down);  // back onto the unprepared one

    // The text resumes after the picture. This is the turn that has to rebuild
    // the chapter stream the picture spent reading itself out of the same zip.
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);
    if (et->hostSpine() != 0 || et->hostPage() != 3 || et->hostPageImage()[0] ||
        strcmp(et->hostLine(0), "ende") != 0) {
      printf("EPUB APP FAIL: the text did not resume after the picture (s%d p%d '%s')\n",
             et->hostSpine(), et->hostPage(), et->hostLine(0));
      abort();
    }
    // And UP finds the picture again. A page is replayed from its recorded
    // start offset, and an <img> shares its offset with the word after it, so
    // without the flag in that table this turn would land on "ende" twice and
    // the picture would be unreachable backwards.
    toybox.onButton(SideBtn::Up);
    if (et->hostPage() != 2 || strcmp(et->hostPageImage(), "OEBPS/images/missing.png") != 0) {
      printf("EPUB APP FAIL: UP did not find the illustration again (p%d img '%s')\n",
             et->hostPage(), et->hostPageImage());
      abort();
    }
    toybox.onButton(SideBtn::Down);  // back onto "ende"

    // And on again into chapter two.
    toybox.onButton(SideBtn::Down);
    if (et->hostSpine() != 1 || et->hostPage() != 0) {
      printf("EPUB APP FAIL: DOWN did not cross into chapter two (s%d p%d)\n", et->hostSpine(),
             et->hostPage());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_ch2");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);  // page 2 of chapter two
    if (et->hostSpine() != 1 || et->hostPage() != 1) {
      printf("EPUB APP FAIL: DOWN did not turn within chapter two\n");
      abort();
    }
    const uint32_t page1Off = et->hostPageOffset();

    // What did that write to the card? Exactly what CrossPoint reads.
    {
      uint8_t buf[16];
      const int n = stickyHost.sdReadFile("/.crosspoint/epub_1175141337288249041/progress.bin", buf, 16);
      epubc::Progress p;
      if (n != 10 || !epubc::decodeProgress(buf, n, p) || p.spine != 1 || !p.hasOffset ||
          p.offset != page1Off) {
        printf("EPUB APP FAIL: progress.bin says n=%d spine=%d off=%u, reader is at %u\n", n,
               n == 10 ? p.spine : -1, n == 10 ? p.offset : 0, page1Off);
        abort();
      }
    }

    // And beside the book, KOReader's own sidecar: a card carried to a
    // KOReader device should open near here without a sync server.
    {
      char mp[160];
      if (!ksdr::metaPath("/books/wind.epub", mp, sizeof(mp)) ||
          strcmp(mp, "/books/wind.sdr/metadata.epub.lua") != 0) {
        printf("KSDR FAIL: metaPath gave '%s'\n", mp);
        abort();
      }
      char lua[512];
      const int n = stickyHost.sdReadFile(mp, lua, sizeof(lua) - 1);
      if (n <= 0) {
        printf("KSDR FAIL: no sidecar written beside the book\n");
        abort();
      }
      lua[n] = 0;
      // Reading chapter two of two: past the start of the book, short of its
      // end. The exact number is the first chapter's share of the bytes, and
      // pinning it would pin the fixture's whitespace, so the guard is the
      // shape -- which is what a wrong percentage gets wrong.
      const char* key = strstr(lua, "[\"last_percent\"] = ");
      if (!key) {
        printf("KSDR FAIL: no last_percent in:\n%s\n", lua);
        abort();
      }
      const double pct = atof(key + strlen("[\"last_percent\"] = "));
      if (!(pct > 0.0 && pct < 1.0)) {
        printf("KSDR FAIL: mid-book percentage came out %f\n", pct);
        abort();
      }
      // percent_finished drives KOReader's footer and must agree with the
      // position it actually restores from.
      const char* disp = strstr(lua, "[\"percent_finished\"] = ");
      if (!disp || atof(disp + strlen("[\"percent_finished\"] = ")) != pct) {
        printf("KSDR FAIL: the two percentages disagree\n");
        abort();
      }
    }

    // UP goes back to page 1 of the chapter, and the replayed page starts
    // where it started the first time.
    toybox.onButton(SideBtn::Up);
    if (et->hostSpine() != 1 || et->hostPage() != 0 || et->hostPageOffset() != 1) {
      printf("EPUB APP FAIL: UP did not replay page one (p%d off %u)\n", et->hostPage(),
             et->hostPageOffset());
      abort();
    }
    // ...and back across the chapter boundary to chapter one's last page.
    toybox.onButton(SideBtn::Up);
    if (et->hostSpine() != 0) {
      printf("EPUB APP FAIL: UP did not cross back into chapter one\n");
      abort();
    }

    // --- page turns ----------------------------------------------------------
    // A page turn is a PARTIAL refresh -- 0.3 s against 1.7 s -- with the host
    // promoting itself to a clean full one every so often. The cadence IS the
    // feature: too rare and the ghosts pile up, too often and it is not fast
    // any more. So each of the three settings is walked and counted, because a
    // setting whose name is the only thing that changes is worse than no
    // setting at all.
    {
      g_dumpEnabled = false;
      struct Case {
        rmenu::Refresh mode;
        int turns, fulls, partials;
      };
      static const Case kCases[3] = {
          {rmenu::Refresh::Fast, 16, 1, 15},
          {rmenu::Refresh::Normal, 8, 1, 7},
          {rmenu::Refresh::Best, 3, 3, 0},
      };
      // The cadences, counted at the host. Driving these through sixteen real
      // page turns would need a book long enough to survive them, and a turn
      // that falls off the end of one repaints nothing -- which reads as a
      // cadence failure and is not one.
      for (const Case& t : kCases) {
        stickyHost.resetFastCount();
        const int f0 = g_fullCount, p0 = g_partialCount;
        for (int i = 0; i < t.turns; i++) stickyHost.refreshFast(rmenu::cleanEvery(t.mode));
        const int fulls = g_fullCount - f0, partials = g_partialCount - p0;
        if (fulls != t.fulls || partials != t.partials) {
          printf("EPUB APP FAIL: %s, %d turns cost %d full and %d partial, wanted %d and %d\n",
                 rmenu::refreshLabel(t.mode), t.turns, fulls, partials, t.fulls, t.partials);
          abort();
        }
      }

      // And the reader is actually wired to it: eight real turns on the
      // middle setting, which is the one a book in this harness is long
      // enough to survive.
      rmenu::setRefreshMode(stickyHost.prefs(), true, rmenu::Refresh::Normal);
      stickyHost.resetFastCount();
      const int f0 = g_fullCount, p0 = g_partialCount;
      for (int i = 0; i < 8; i++) toybox.onButton(SideBtn::Down);
      if (g_fullCount - f0 != 1 || g_partialCount - p0 != 7) {
        printf("EPUB APP FAIL: eight turns cost %d full and %d partial refreshes\n",
               g_fullCount - f0, g_partialCount - p0);
        abort();
      }

      // The two readers keep their own answer. One preference for both would
      // pass every count above and still be the wrong feature.
      rmenu::setRefreshMode(stickyHost.prefs(), true, rmenu::Refresh::Fast);
      rmenu::setRefreshMode(stickyHost.prefs(), false, rmenu::Refresh::Best);
      if (rmenu::refreshMode(stickyHost.prefs(), true) != rmenu::Refresh::Fast ||
          rmenu::refreshMode(stickyHost.prefs(), false) != rmenu::Refresh::Best) {
        printf("EPUB APP FAIL: the two readers share one refresh setting\n");
        abort();
      }
      stickyHost.resetFastCount();
      printf("page turns ok (three cadences count out, and the readers keep their own)\n");
    }

    // --- the panel behind the power button ----------------------------------
    // OK used to close the book. It now opens the options panel, and closing
    // the book is a row in it.
    g_dumpEnabled = true;
    if (!toybox.onButton(SideBtn::Ok) || et->hostMenu() == 0) {
      printf("EPUB APP FAIL: OK did not open the panel\n");
      abort();
    }
    setScreen("tool_epub_options");
    stickyHost.refresh(true);

    checkCornerClock("epub");

    // Contents: the book's own nav document, and a jump that lands where the
    // row says -- including the chapter that is nothing but a picture.
    g_dumpEnabled = false;
    toybox.onTap(240, rmenu::rootRect(0, 480).y + 40);
    if (et->hostMenu() != (int)rmenu::Page::Contents || et->hostTocCount() != 3) {
      printf("EPUB APP FAIL: contents shows menu %d with %d entries\n", et->hostMenu(),
             et->hostTocCount());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_contents");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(240, shelf::Y0 + 2 * shelf::ROW_H + 20);  // the third chapter
    if (et->hostMenu() != 0 || et->hostSpine() != 2 ||
        strcmp(et->hostPageImage(), "OEBPS/images/art2.png") != 0) {
      printf("EPUB APP FAIL: the contents jump landed on s%d img '%s'\n", et->hostSpine(),
             et->hostPageImage());
      abort();
    }

    // A phrase kept, on the card, where a reflash cannot reach it. The pick
    // needs words on the screen, so go to the chapter of them first.
    toybox.onButton(SideBtn::Ok);
    toybox.onTap(240, rmenu::rootRect(0, 480).y + 40);  // CONTENTS
    toybox.onTap(240, shelf::Y0 + shelf::ROW_H + 20);   // the long chapter
    if (et->hostSpine() != 1 || et->hostLineCount() < 3) {
      printf("EPUB APP FAIL: no page of words to pick from (s%d, %d lines)\n", et->hostSpine(),
             et->hostLineCount());
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    toybox.onTap(rmenu::plusRect(1, 480).x + 40, rmenu::plusRect(1, 480).y + 40);
    if (!et->hostPicking()) {
      printf("EPUB APP FAIL: + did not start a phrase pick\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_pick");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(40, et->hostLineY(0) + 10);   // the first word of the page
    if (!et->hostPicking()) {
      printf("EPUB APP FAIL: the first word ended the pick\n");
      abort();
    }
    toybox.onTap(200, et->hostLineY(1) + 10);  // ...and a word on the next line
    if (et->hostMenu() != (int)rmenu::Page::Keep || !et->hostPhrase()[0]) {
      printf("EPUB APP FAIL: the pick gave menu %d phrase '%s'\n", et->hostMenu(),
             et->hostPhrase());
      abort();
    }
    // The phrase must be the words themselves, in order, starting with the
    // first word of the page -- not a page number wearing a costume.
    if (strncmp(et->hostPhrase(), et->hostLine(0), 5) != 0) {
      printf("EPUB APP FAIL: phrase '%s' does not start the line '%s'\n", et->hostPhrase(),
             et->hostLine(0));
      abort();
    }
    // ...and it must actually run ONTO the second line, since that is where
    // the closing tap was. A phrase that stops at the line break would be a
    // line, which is not what was asked for.
    {
      char firstOfNext[16] = "";
      snprintf(firstOfNext, sizeof(firstOfNext), "%.5s", et->hostLine(1));
      // ...unless the label ran out first and said so, which is the honest
      // answer for a phrase longer than a bookmark: the label bytes, then an ellipsis.
      const char* ph = et->hostPhrase();
      const size_t pl = strlen(ph);
      const bool cut = pl >= 3 && strcmp(ph + pl - 3, "...") == 0;
      if (!strstr(ph, firstOfNext) && !cut) {
        printf("EPUB APP FAIL: phrase '%s' never reached line two ('%s')\n", ph, firstOfNext);
        abort();
      }
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_keep");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(120, 600);  // SAVE
    if (et->hostMarkCount() != 1 || !et->hostMarkLabel(0)[0]) {
      printf("EPUB APP FAIL: saving gave %d marks, label '%s'\n", et->hostMarkCount(),
             et->hostMarkLabel(0));
      abort();
    }
    {
      char mp[48];
      marks::path("/books/wind.epub", mp, sizeof(mp));
      uint8_t raw[marks::FILE_BYTES];
      if (stickyHost.sdReadFile(mp, raw, sizeof(raw)) != marks::FILE_BYTES ||
          memcmp(raw, "TBM3", 4) != 0 || raw[4] != 1 || raw[14] == 0) {
        printf("EPUB APP FAIL: no bookmark with a phrase on the card at %s\n", mp);
        abort();
      }
    }
    // CANCEL on the confirmation keeps nothing.
    toybox.onButton(SideBtn::Ok);
    toybox.onTap(rmenu::plusRect(1, 480).x + 40, rmenu::plusRect(1, 480).y + 40);
    toybox.onTap(40, et->hostLineY(0) + 10);
    toybox.onTap(200, et->hostLineY(1) + 10);
    toybox.onTap(360, 600);  // CANCEL
    if (et->hostMarkCount() != 1) {
      printf("EPUB APP FAIL: CANCEL kept one anyway (%d marks)\n", et->hostMarkCount());
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    toybox.onTap(240, rmenu::rootRect(1, 480).y + 40);  // BOOKMARKS
    if (et->hostMenu() != (int)rmenu::Page::Marks) {
      printf("EPUB APP FAIL: the bookmark list did not open\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_marks");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    // Standing on it already, so this tap removes it.
    toybox.onTap(240, shelf::Y0 + 20);
    if (et->hostMarkCount() != 0) {
      printf("EPUB APP FAIL: the second tap did not remove the bookmark\n");
      abort();
    }

    // --- marks kept before the labels grew --------------------------------
    // A TBM2 file (40-byte labels) planted where a book's marks live: load
    // reads it whole, the next save writes TBM3, and a label longer than
    // TBM2 could ever hold survives a round trip uncut. Run here because the
    // open book holds the bus, which the side-file calls faithfully require.
    {
      uint8_t old[marks::FILE_BYTES_V2];
      memset(old, 0, sizeof(old));
      memcpy(old, "TBM2", 4);
      old[4] = 1;
      uint8_t* e = old + 6;
      e[0] = 2;                   // spine 2
      e[2] = 5;                   // page 5
      e[4] = 0x39; e[5] = 0x30;   // off 12345
      snprintf((char*)e + 8, marks::LABEL_V2, "kept before the labels grew");
      char mp[48];
      marks::path("/books/old-marks.epub", mp, sizeof(mp));
      sdcard::hostPlantSide(mp, old, (int)sizeof(old));
      static marks::Mark mm[marks::MAX];
      const int nOld = marks::load(stickyHost, "/books/old-marks.epub", mm);
      if (nOld != 1 || mm[0].spine != 2 || mm[0].page != 5 || mm[0].off != 12345 ||
          strcmp(mm[0].label, "kept before the labels grew") != 0) {
        printf("MARKS FAIL: the TBM2 file did not read back (n=%d label '%s')\n", nOld,
               nOld ? mm[0].label : "");
        abort();
      }
      if (!marks::save(stickyHost, "/books/old-marks.epub", mm, 1)) {
        printf("MARKS FAIL: saving the migrated marks failed\n");
        abort();
      }
      uint8_t raw[8];
      if (sdcard::hostReadSide(mp, raw, 8) < 8 || memcmp(raw, "TBM3", 4) != 0) {
        printf("MARKS FAIL: the save did not migrate the file to TBM3\n");
        abort();
      }
      snprintf(mm[0].label, sizeof(mm[0].label),
               "a phrase well past forty bytes, the length that used to cut sentences in half");
      marks::save(stickyHost, "/books/old-marks.epub", mm, 1);
      static marks::Mark two[marks::MAX];
      if (marks::load(stickyHost, "/books/old-marks.epub", two) != 1 ||
          strcmp(two[0].label, mm[0].label) != 0) {
        printf("MARKS FAIL: a long label did not survive the round trip\n");
        abort();
      }
      printf("marks migration ok (TBM2 read, TBM3 written, long labels whole)\n");
    }

    // Type: bigger text re-derives the page from the offset, so the reader
    // stays where it was reading rather than where the page number was. Done
    // on a page of WORDS -- the jump above left us on a picture, which has no
    // lines to count.
    toybox.onButton(SideBtn::Ok);  // the panel again
    toybox.onTap(240, rmenu::rootRect(2, 480).y + 40);
    if (et->hostMenu() != (int)rmenu::Page::Text) {
      printf("EPUB APP FAIL: the text page did not open\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_text");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    {
      const uint32_t before = et->hostPageOffset();
      const int lines = et->hostLineCount();
      toybox.onTap(440, 110 + 34 + 20);  // SIZE +
      if (et->hostTextSize() != 1) {
        printf("EPUB APP FAIL: the size did not change (%d)\n", et->hostTextSize());
        abort();
      }
      if (et->hostPageOffset() > before) {
        printf("EPUB APP FAIL: bigger type moved the reader forward (%u -> %u)\n", before,
               et->hostPageOffset());
        abort();
      }
      if (et->hostLineCount() >= lines) {
        printf("EPUB APP FAIL: bigger type gave %d lines, was %d\n", et->hostLineCount(), lines);
        abort();
      }
      toybox.onTap(40, 110 + 34 + 20);  // SIZE -, back to normal
    }

    // The contents fallback: a book with no navigation document names its
    // chapters by their own first words.
    et->hostDropToc();
    toybox.onButton(SideBtn::Ok);
    toybox.onTap(240, rmenu::rootRect(0, 480).y + 40);
    g_dumpEnabled = true;
    setScreen("tool_epub_contents_plain");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Ok);  // back to the root

    // The typeface row cycles in place: a tap per face comes back to DejaVu,
    // the preference sticks, and the page never leaves the panel. In between,
    // a different face must actually measure differently -- that is the whole
    // point -- so the second face's line count is allowed to differ but the
    // reading offset must not move. Counted from the host rather than written
    // down here: dropping Atkinson took the count from three to two, and a
    // literal 3 would have walked one face past the end.
    {
      const uint32_t offBefore = et->hostPageOffset();
      for (int i = 0; i < stickyHost.typefaceCount(); i++) {
        toybox.onTap(240, rmenu::rootRect(3, 480).y + 40);
        if (et->hostMenu() == 0) {
          printf("EPUB APP FAIL: the typeface row left the panel\n");
          abort();
        }
        if (et->hostPageOffset() != offBefore) {
          printf("EPUB APP FAIL: changing typeface moved the reader (%u -> %u)\n",
                 (unsigned)offBefore, (unsigned)et->hostPageOffset());
          abort();
        }
      }
      if (stickyHost.prefs().getUInt("rd_face", 99) != 0) {
        printf("EPUB APP FAIL: a full cycle of typeface taps did not come back to DejaVu\n");
        abort();
      }
      printf("typeface ok (three faces cycle, the place read from stands still)\n");
    }

    // --- no word may vanish at a page boundary ------------------------------
    // The same stretch of chapter is read twice, once at largest and once at
    // normal, and the words joined across pages must be identical: the layout
    // may break lines wherever it likes, but it may not eat. On hardware a
    // page that filled mid-line dropped every gathered word but the last --
    // "Good. Now that left the other two." arrived as "two."
    toybox.onButton(SideBtn::Ok);  // panel down, back onto the page
    {
      const int spine0 = et->hostSpine();
      const uint32_t off0 = et->hostPageOffset();
      static char walkA[8192], walkB[8192], page[2048];
      auto walk = [&](char* out, size_t cap) {
        size_t n = 0;
        uint32_t prevOff = 0xFFFFFFFFu;
        int prevLen = -1;
        for (int i = 0; i < 64; i++) {
          if (et->hostSpine() != spine0) break;
          const int pl = et->hostPageJoin(page, sizeof(page));
          if (et->hostPageOffset() == prevOff && pl == prevLen) break;  // end of book
          prevOff = et->hostPageOffset();
          prevLen = pl;
          if (n && pl && n + 1 < cap) out[n++] = ' ';
          size_t take = (size_t)pl;
          if (take > cap - 1 - n) take = cap - 1 - n;
          memcpy(out + n, page, take);
          n += take;
          toybox.onButton(SideBtn::Down);
        }
        out[n] = 0;
      };
      et->hostSetStyle(2);  // largest: a page boundary every few sentences
      walk(walkA, sizeof(walkA));
      et->hostGoto(spine0, off0);
      et->hostSetStyle(0);  // normal
      walk(walkB, sizeof(walkB));
      if (strcmp(walkA, walkB) != 0) {
        int i = 0;
        while (walkA[i] && walkA[i] == walkB[i]) i++;
        const int from = i > 40 ? i - 40 : 0;
        printf("EPUB APP FAIL: the chapter reads differently by size at byte %d\n"
               "  largest: ...%.90s\n  normal:  ...%.90s\n",
               i, walkA + from, walkB + from);
        abort();
      }
      et->hostGoto(spine0, off0);
      printf("page boundaries ok (chapter reads identically at largest and normal)\n");
    }
    toybox.onButton(SideBtn::Ok);  // the panel again, for the close row below

    // And the way out is a row, not the button.
    // Rotation is three buttons in the row: LEFT (1), PORTRAIT (0), RIGHT
    // (3), one tap each -- and the turn still lands when the panel closes.
    // The senses were confirmed on glass and swapped once.
    {
      const int rotY = rmenu::rootRect(4, 480).y + 79;  // through the buttons
      const uint32_t offR = et->hostPageOffset();
      const int linesPortrait = et->hostLineCount();
      toybox.onTap(380, rotY);  // RIGHT: landscape
      if (stickyHost.canvasRotation() != 0) {
        printf("EPUB APP FAIL: the panel turned before it closed\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);  // close the panel; the page turns now
      if (stickyHost.canvasRotation() != 3) {
        printf("EPUB APP FAIL: closing the panel did not turn the page\n");
        abort();
      }
      if (et->hostPageOffset() != offR) {
        printf("EPUB APP FAIL: rotation moved the reader (%u -> %u)\n", (unsigned)offR,
               (unsigned)et->hostPageOffset());
        abort();
      }
      if (et->hostLineCount() >= linesPortrait) {
        printf("EPUB APP FAIL: landscape laid out %d lines, portrait %d -- no reflow?\n",
               et->hostLineCount(), linesPortrait);
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_landscape");
      epd.clear();
      toybox.render(stickyHost.sharedCanvas());
      epd.displayFull();
      g_dumpEnabled = false;
      toybox.onButton(SideBtn::Ok);  // the panel stands the canvas up again
      if (stickyHost.canvasRotation() != 0) {
        printf("EPUB APP FAIL: the panel opened sideways\n");
        abort();
      }
      // The other landscape, one tap, no cycling through portrait to get
      // there; then straight home the same way.
      toybox.onTap(98, rotY);  // LEFT: the other landscape
      toybox.onButton(SideBtn::Ok);
      if (stickyHost.canvasRotation() != 1) {
        printf("EPUB APP FAIL: LEFT did not land the flipped landscape\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(239, rotY);  // PORTRAIT
      toybox.onButton(SideBtn::Ok);
      if (stickyHost.canvasRotation() != 0 ||
          stickyHost.prefs().getUInt("rd_rot", 99) != 0) {
        printf("EPUB APP FAIL: PORTRAIT did not stand the page back up\n");
        abort();
      }
      printf("rotation ok (three buttons, turns on close, offset pinned)\n");
      toybox.onButton(SideBtn::Ok);  // panel up for the close row below
    }
    // Row 6 now: "Start again" sits between the rotation buttons and the way
    // out, so a thumb reaching for one cannot land on the other.
    toybox.onTap(240, rmenu::rootRect(6, 480).y + 40);
    if (et->hostScreen() != 0) {
      printf("EPUB APP FAIL: CLOSE THE BOOK did not close it\n");
      abort();
    }
    printf("epub panel ok (contents jump, marks on the card, type re-derives)\n");
    toybox.goHub();

    // The CrossPoint round-trip: a position written by "another firmware"
    // (spine 1, mid-chapter offset 2500) must open on the page containing it.
    {
      epubc::Progress cp;
      cp.spine = 1;
      cp.page = 3;       // deliberately wrong for our pagination: the offset wins
      cp.pageCount = 9;
      cp.offset = 2500;
      cp.hasOffset = true;
      uint8_t buf[10];
      const int n = epubc::encodeProgress(cp, buf);
      // No session is open here, and the card calls now refuse that -- as the
      // device does. This is the harness standing in for another firmware
      // having left the file behind, so it plants it directly.
      //
      // Into CROSSPOINT's directory: the 32-bit hash of the book's path, which
      // is where that firmware keeps its cache and its position, and the only
      // place it looks (confirmed against a real card). Toybox's own 64-bit
      // directory already holds a position by now -- which is exactly the
      // situation the owner hit, and the reason CrossPoint's file has to win.
      sdcard::hostPlantSide("/.crosspoint/epub_836526750/progress.bin", buf, n);
      toybox.open(false, 10);  // EPUB, fresh
      auto* et2 = static_cast<EpubTool*>(toybox.hostActive());
      toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
      if (et2->hostScreen() != 1 || et2->hostSpine() != 1) {
        printf("EPUB APP FAIL: the planted position did not open chapter two\n");
        abort();
      }
      const uint32_t start = et2->hostPageOffset();
      if (start > 2500) {
        printf("EPUB APP FAIL: landed at %u, past the planted 2500\n", start);
        abort();
      }
      // the next page must start beyond the planted offset
      toybox.onButton(SideBtn::Down);
      if (et2->hostPageOffset() <= 2500) {
        printf("EPUB APP FAIL: page after the planted one starts at %u\n",
               et2->hostPageOffset());
        abort();
      }
      toybox.onButton(SideBtn::Ok);                       // the panel
      toybox.onTap(240, rmenu::rootRect(6, 480).y + 40);  // CLOSE THE BOOK
    }


    // --- a chapter that is one picture and no words -------------------------
    // A cover page, a colour gallery, a character-art plate: real books are
    // full of chapters with an <img> and nothing else. Such a chapter lays out
    // exactly one page and then comes back empty, and the reader used to read
    // that emptiness as "an empty chapter, move on" -- so the page was skipped
    // on the way in and unreachable on the way back, and the layout that found
    // nothing had already blanked the page that was showing.
    {
      epubc::Progress cp;
      cp.spine = 2;
      cp.page = 0;
      cp.pageCount = 1;
      cp.offset = 0;
      cp.hasOffset = true;
      uint8_t buf[10];
      const int n = epubc::encodeProgress(cp, buf);
      sdcard::hostPlantSide("/.crosspoint/epub_836526750/progress.bin", buf, n);
      toybox.open(false, 10);
      auto* et3 = static_cast<EpubTool*>(toybox.hostActive());
      toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
      if (et3->hostScreen() != 1 || et3->hostSpine() != 2 ||
          strcmp(et3->hostPageImage(), "OEBPS/images/art2.png") != 0) {
        printf("EPUB APP FAIL: the picture-only chapter opened as s%d img '%s'\n",
               et3->hostSpine(), et3->hostPageImage());
        abort();
      }
      // Back out of it, into the chapter of words before it...
      toybox.onButton(SideBtn::Up);
      if (et3->hostSpine() != 1 || et3->hostPageImage()[0] || et3->hostLineCount() == 0) {
        printf("EPUB APP FAIL: UP out of the picture chapter gave s%d, %d lines, img '%s'\n",
               et3->hostSpine(), et3->hostLineCount(), et3->hostPageImage());
        abort();
      }
      // ...and forward into it again, which is the turn that used to arrive on
      // a blank sheet.
      toybox.onButton(SideBtn::Down);
      if (et3->hostSpine() != 2 ||
          strcmp(et3->hostPageImage(), "OEBPS/images/art2.png") != 0) {
        printf("EPUB APP FAIL: DOWN back into the picture chapter gave s%d img '%s'\n",
               et3->hostSpine(), et3->hostPageImage());
        abort();
      }
      toybox.onButton(SideBtn::Ok);                       // the panel
      toybox.onTap(240, rmenu::rootRect(6, 480).y + 40);  // CLOSE THE BOOK
      printf("epub picture chapters ok (reachable both ways, never blank)\n");
    }
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("epub reader ok (opens, turns, replays, and trades positions with CrossPoint)\n");
  }

  // --- the recently-read strip -----------------------------------------------
  // The book tests above opened real books, so the Study drawer now carries
  // covers: the epub most recently, a .tbk (with a stored page-0 thumbnail)
  // before it. Tapping a cover reopens that book directly.
  {
    g_dumpEnabled = false;
    recents::Entry rec[recents::MAX];
    const int n = recents::list(stickyHost.prefs(), rec);
    if (n != 2 || rec[0].kind != recents::KIND_EPUB || strcmp(rec[0].file, "/books/wind.epub") != 0 ||
        rec[1].kind != recents::KIND_TBK) {
      printf("RECENTS FAIL: %d entries, front '%s'\n", n, n ? rec[0].file : "");
      abort();
    }
    if (!bthumb::have(rec[1].file)) {
      printf("RECENTS FAIL: no thumbnail stored for %s\n", rec[1].file);
      abort();
    }
    // ...and the epub's cover decoded into the same store when it was opened.
    if (!bthumb::have("/books/wind.epub") || bthumb::failed("/books/wind.epub")) {
      printf("RECENTS FAIL: the epub cover thumbnail was not made\n");
      abort();
    }
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);  // the STUDY drawer
    g_dumpEnabled = true;
    setScreen("hub_study_recent");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // The title rows sit under the two tile rows; row 0 is the epub.
    const int rowY = hubui::FOLDER_TOP + 2 * hubui::ROW_STEP + hubui::REC_GAP +
                     hubui::REC_HEAD_H;
    stickyHost.hostResetPaints();
    toybox.onTap(EPD_W / 4, rowY + 10);
    if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "EPUB") != 0 ||
        static_cast<EpubTool*>(toybox.hostActive())->hostScreen() != 1) {
      printf("RECENTS FAIL: the epub cover did not reopen the book\n");
      abort();
    }
    // ...and straight to the book: the loading face and the first page, two
    // paints. Three would mean the shelf list flashed on the way, the screen
    // this shortcut exists to skip.
    if (stickyHost.hostPaints() > 2) {
      printf("RECENTS FAIL: the cover tap cost %d paints - did the shelf show?\n",
             stickyHost.hostPaints());
      abort();
    }
    toybox.onButton(SideBtn::Ok);  // close the book
    toybox.goHub();
    toybox.onTap(EPD_W / 2, rowY + hubui::REC_ROW_H + 10);  // row 1: the .tbk
    if (!toybox.hostInApp() || toybox.hostIdx() != 9 ||
        static_cast<BookTool*>(toybox.hostActive())->hostScreen() != 1) {
      printf("RECENTS FAIL: the tbk cover did not reopen the book\n");
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    toybox.goHub();
    toybox.hostHub().goHome();

    // The DOWN hold's new job: carry on READING. With recents present it
    // lands inside the last book at its saved page, not on any list. The
    // last book opened was the .tbk cover a moment ago, which is now the
    // front of recents -- exactly the point.
    if (!toybox.carryOnReading() || toybox.hostIdx() != 9 ||
        static_cast<BookTool*>(toybox.hostActive())->hostScreen() != 1) {
      printf("RECENTS FAIL: carry on reading did not land inside the book\n");
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    toybox.goHub();

  // Relocated below the recents guard: both of these open books, and the
  // strip above pins an exact order.
  {
    // --- a card from an older firmware migrates its position ----------------
    // Older CrossPoint (and Toybox until now) kept the cache under the Murmur
    // hash. When the FNV directory has nothing, the Murmur one is read -- and
    // the next save writes FNV, so the position has moved house.
    {
      epubc::Progress cp;
      cp.spine = 1;
      cp.page = 1;
      cp.pageCount = 9;
      cp.offset = 2500;
      cp.hasOffset = true;
      uint8_t buf[10];
      const int n = epubc::encodeProgress(cp, buf);
      sdcard::hostPlantSide("/.crosspoint/epub_1175141337288249041/progress.bin", buf, 0);
      sdcard::hostPlantSide("/.crosspoint/epub_836526750/progress.bin", buf, n);
      toybox.open(false, 10);
      auto* etm = static_cast<EpubTool*>(toybox.hostActive());
      toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
      if (etm->hostScreen() != 1 || etm->hostSpine() != 1) {
        printf("EPUB APP FAIL: the legacy-hash position was not read\n");
        abort();
      }
      toybox.onButton(SideBtn::Down);  // a turn: the save lands in the FNV dir
      uint8_t back[16];
      epubc::Progress q;
      const int m =
          sdcard::hostReadSide("/.crosspoint/epub_1175141337288249041/progress.bin", back, 16);
      if (m != 10 || !epubc::decodeProgress(back, m, q) || q.spine != 1) {
        printf("EPUB APP FAIL: the migrated position did not land in the FNV dir (n=%d)\n", m);
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(240, rmenu::rootRect(6, 480).y + 40);  // CLOSE THE BOOK
      printf("hash migration ok (reads Murmur, writes FNV)\n");
    }

    // --- the CrossInk shelf ---------------------------------------------------
    // /Read (and /epub) appear as folders beside the series, named by their
    // absolute path; opening one lists and opens its books, and the position
    // lands under the FNV hash of the /Read path, where CrossInk will look.
    {
      sdcard::hostSetCrossRoots(true);
      toybox.open(false, 10);  // fresh enter: the shelf re-lists
      auto* etr = static_cast<EpubTool*>(toybox.hostActive());
      if (etr->hostFolders() != 2) {
        printf("EPUB APP FAIL: /Read did not shelve (%d folders)\n", etr->hostFolders());
        abort();
      }
      toybox.onTap(240, shelf::Y0 + shelf::ROW_H + 10);  // the second folder: /Read
      if (strcmp(etr->hostDir(), "/Read") != 0 || etr->hostItems() != 1) {
        printf("EPUB APP FAIL: /Read opened as '%s' with %d rows\n", etr->hostDir(),
               etr->hostItems());
        abort();
      }
      toybox.onTap(240, shelf::Y0 + 10);  // the one book
      if (etr->hostScreen() != 1) {
        printf("EPUB APP FAIL: the /Read book did not open\n");
        abort();
      }
      toybox.onButton(SideBtn::Down);              // a turn
      toybox.onButton(SideBtn::Ok);                // the panel
      toybox.onTap(240, rmenu::rootRect(6, 480).y + 40);  // CLOSE: the save lands
      uint8_t pb[16];
      if (sdcard::hostReadSide("/.crosspoint/epub_16433272010175318797/progress.bin", pb, 16) !=
          10) {
        sdcard::hostDumpSides();
        printf("EPUB APP FAIL: /Read progress not under the FNV of its own path\n");
        abort();
      }
      toybox.onTap(20, 20);                               // back out of /Read
      if (strcmp(etr->hostDir(), "/books") != 0) {
        printf("EPUB APP FAIL: back from /Read did not land on the top shelf\n");
        abort();
      }
      sdcard::hostSetCrossRoots(false);
      printf("crossink shelf ok (/Read lists, opens, and keeps its place)\n");
    }
  }
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("recents ok (covers reopen, thumbnail stored, DOWN carries on reading)\n");
  }

  // --- BMP pictures ----------------------------------------------------------
  // The format the CrossPoint/CrossInk/Xteink family trades sleep art and
  // covers in. Every byte parsed here goes through src/bmp_gray.h -- the
  // parser the device ships -- against BMPs built the way real writers build
  // them: bottom-up and top-down, palettes that mean what they say (including
  // one that lies backwards), 8 and 24 and 4 bits per pixel.
  {
    g_dumpEnabled = false;
    static uint8_t gray[480 * 800];

    // A 240x400 8bpp quadrant card: black / white over grey 85 / grey 170.
    // 240x400 doubles to exactly 480x800 -- the whole panel covered, every
    // quadrant where arithmetic says, and the upscale replication exercised
    // on the way (the 4x enlargement cap stays clear at 2x).
    auto quad8 = [](bool invertPalette) {
      static uint8_t bmp[14 + 40 + 1024 + 240 * 400];
      const int W = 240, H = 400;
      const uint32_t off = 14 + 40 + 1024, img = (uint32_t)W * H, total = off + img;
      memset(bmp, 0, sizeof(bmp));
      bmp[0] = 'B'; bmp[1] = 'M';
      bmp[2] = (uint8_t)total; bmp[3] = (uint8_t)(total >> 8); bmp[4] = (uint8_t)(total >> 16);
      bmp[10] = (uint8_t)off; bmp[11] = (uint8_t)(off >> 8);
      bmp[14] = 40;
      bmp[18] = (uint8_t)(W & 255); bmp[19] = (uint8_t)(W >> 8);
      bmp[22] = (uint8_t)(H & 255); bmp[23] = (uint8_t)(H >> 8);  // positive: bottom-up
      bmp[26] = 1; bmp[28] = 8;
      for (int i = 0; i < 256; i++) {  // palette: BGRA, honest or inverted
        const uint8_t v = invertPalette ? (uint8_t)(255 - i) : (uint8_t)i;
        bmp[54 + i * 4 + 0] = bmp[54 + i * 4 + 1] = bmp[54 + i * 4 + 2] = v;
      }
      for (int y = 0; y < H; y++) {      // stored bottom row first
        const int srcY = H - 1 - y;      // the picture's own row
        for (int x = 0; x < W; x++) {
          uint8_t v;
          if (srcY < H / 2) v = x < W / 2 ? 0 : 255;
          else v = x < W / 2 ? 85 : 170;
          bmp[off + (uint32_t)y * W + x] = v;
        }
      }
      return std::make_pair((const uint8_t*)bmp, total);
    };

    {
      auto [b, n] = quad8(false);
      sdcard::hostPutCardFile("/wallpapers/photo.bmp", b, (int)n);
      if (!sdcard::readBmpGray("/wallpapers/photo.bmp", gray)) {
        printf("BMP FAIL: the 8bpp quadrant card did not parse\n");
        abort();
      }
      const int tl = gray[100 * 480 + 100], tr = gray[100 * 480 + 400];
      const int bl = gray[700 * 480 + 100], br = gray[700 * 480 + 400];
      if (tl != 0 || tr != 255 || bl != 85 || br != 170) {
        printf("BMP FAIL: quadrants read %d %d %d %d, want 0 255 85 170\n", tl, tr, bl, br);
        abort();
      }
    }
    {
      // The same picture with a backwards palette: the VALUES are identical,
      // the palette says they mean the opposite, and the palette must win.
      auto [b, n] = quad8(true);
      sdcard::hostPutCardFile("/inverted.bmp", b, (int)n);
      if (!sdcard::readBmpGray("/inverted.bmp", gray) || gray[100 * 480 + 100] != 255 ||
          gray[100 * 480 + 400] != 0) {
        printf("BMP FAIL: an inverted palette was not honoured\n");
        abort();
      }
    }
    {
      // 24bpp, top-down (negative height), pure-colour quadrants: the
      // BGR-order luminance weights, and the row direction flag.
      const int W = 96, H = 160;
      const uint32_t off = 14 + 40, img = (uint32_t)W * 3 * H, total = off + img;
      static uint8_t bmp[14 + 40 + 96 * 3 * 160];
      memset(bmp, 0, sizeof(bmp));
      bmp[0] = 'B'; bmp[1] = 'M';
      bmp[10] = (uint8_t)off;
      bmp[14] = 40;
      bmp[18] = W;
      const int32_t nh = -H;  // top-down
      memcpy(bmp + 22, &nh, 4);
      bmp[26] = 1; bmp[28] = 24;
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          uint8_t* p = bmp + off + ((uint32_t)y * W + x) * 3;  // stored top first
          if (y < H / 2) {
            if (x < W / 2) p[2] = 255;       // red
            else p[1] = 255;                 // green
          } else {
            if (x < W / 2) p[0] = 255;       // blue
            else p[0] = p[1] = p[2] = 255;   // white
          }
        }
      sdcard::hostPutCardFile("/rgb.bmp", bmp, (int)total);
      if (!sdcard::readBmpGray("/rgb.bmp", gray)) {
        printf("BMP FAIL: the 24bpp top-down card did not parse\n");
        abort();
      }
      const int r = gray[100 * 480 + 100], g = gray[100 * 480 + 400];
      const int bu = gray[700 * 480 + 100], w = gray[700 * 480 + 400];
      if (r != (77 * 255) >> 8 || g != (150 * 255) >> 8 || bu != (29 * 255) >> 8 || w != 255) {
        printf("BMP FAIL: 24bpp luminance read %d %d %d %d\n", r, g, bu, w);
        abort();
      }
    }
    {
      // 4bpp with a two-colour palette, and the small-image cap: an 8x8 may
      // grow at most 4x, so it lands as a 32x32 patch on white.
      const int W = 8, H = 8;
      const uint32_t off = 14 + 40 + 64, total = off + 4 * H;  // rowBytes(8px,4bpp)=4->pad 4
      static uint8_t bmp[14 + 40 + 64 + 4 * 8];
      memset(bmp, 0, sizeof(bmp));
      bmp[0] = 'B'; bmp[1] = 'M';
      bmp[10] = (uint8_t)off;
      bmp[14] = 40;
      bmp[18] = W; bmp[22] = H;
      bmp[26] = 1; bmp[28] = 4;
      bmp[46] = 2;                                   // two palette entries
      bmp[54 + 4] = bmp[54 + 5] = bmp[54 + 6] = 255; // index 1 = white
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
          if (x < W / 2)  // left half index 1 (white), right half 0 (black)
            bmp[off + (uint32_t)y * 4 + (x >> 1)] |= (uint8_t)(1 << (x & 1 ? 0 : 4));
      sdcard::hostPutCardFile("/tiny.bmp", bmp, (int)total);
      if (!sdcard::readBmpGray("/tiny.bmp", gray)) {
        printf("BMP FAIL: the 4bpp card did not parse\n");
        abort();
      }
      const int x0 = (480 - 32) / 2, y0 = (800 - 32) / 2;
      if (gray[0] != 255 || gray[(y0 + 8) * 480 + x0 + 8] != 255 ||
          gray[(y0 + 8) * 480 + x0 + 24] != 0) {
        printf("BMP FAIL: 4bpp halves read %d %d (margin %d)\n",
               gray[(y0 + 8) * 480 + x0 + 8], gray[(y0 + 8) * 480 + x0 + 24], gray[0]);
        abort();
      }
    }
    {
      // Atkinson on a flat middle grey: to 2 levels, roughly half the dots
      // set; to 4, everything within a level of the middle pair. Property
      // checks, because dithering's whole job is to have no exact answer.
      memset(gray, 128, sizeof(gray));
      bmpg::atkinson(gray, 2);
      uint32_t ones = 0;
      for (uint32_t i = 0; i < sizeof(gray); i++) {
        if (gray[i] > 1) {
          printf("BMP FAIL: 2-level dither produced value %d\n", gray[i]);
          abort();
        }
        ones += gray[i];
      }
      const double frac = (double)ones / sizeof(gray);
      if (frac < 0.45 || frac < 128.0 / 255 - 0.05 || frac > 128.0 / 255 + 0.05) {
        printf("BMP FAIL: 2-level dither of grey 128 set %.3f of dots\n", frac);
        abort();
      }
      memset(gray, 128, sizeof(gray));
      bmpg::atkinson(gray, 4);
      for (uint32_t i = 0; i < sizeof(gray); i++)
        if (gray[i] < 1 || gray[i] > 2) {
          printf("BMP FAIL: 4-level dither of grey 128 left level %d\n", gray[i]);
          abort();
        }
    }
    printf("bmp parse ok (palettes, row orders, depths, dither)\n");

    // --- and the ways a .bmp gets used ---------------------------------------
    // The wallpaper list offers it; taking it as wallpaper makes the 1-bit
    // .tbi; taking it as the lock picture makes the 2bpp grey file -- and
    // each choice removes the other lock file, so exactly one picture rules.
    {
      char names[8][40];
      const int n = sdcard::listTbi(names, 8);
      bool listed = false;
      for (int i = 0; i < n; i++) listed = listed || strcmp(names[i], "photo.bmp") == 0;
      if (!listed) {
        printf("BMP FAIL: photo.bmp not on the wallpaper list (%d names)\n", n);
        abort();
      }
      if (!stickyHost.sdWallpaperTake("photo.bmp") ||
          tfs::size(wallimg::PATH) != tbimg::FILE_SIZE) {
        printf("BMP FAIL: photo.bmp did not become the wallpaper\n");
        abort();
      }
      // The top-left quadrant was black; dithered black stays black.
      {
        size_t len = 0;
        char* buf = tfs::readAlloc(wallimg::PATH, len);
        const uint8_t* bits = (const uint8_t*)buf + tbimg::HEADER;
        int blacks = 0;
        for (int y = 100; y < 110; y++)
          for (int xb = 0; xb < 20; xb++)
            for (int k = 0; k < 8; k++)
              if (!(bits[y * 60 + xb] & (0x80 >> k))) blacks++;
        free(buf);
        if (blacks < 1500) {  // of 1600
          printf("BMP FAIL: the wallpaper's black quadrant has %d black dots\n", blacks);
          abort();
        }
      }
      if (!stickyHost.sdLockTake("photo.bmp") || tfs::size(lockimg::G2_PATH) != tbg2::FILE_SIZE ||
          !lockimg::haveG2() || !lockimg::have()) {
        printf("BMP FAIL: photo.bmp did not become the grey lock picture\n");
        abort();
      }
      // The grey file really has four levels: quadrant means of the packed
      // 2bpp, in the panel's own order.
      {
        size_t len = 0;
        char* buf = tfs::readAlloc(lockimg::G2_PATH, len);
        const uint8_t* bits = (const uint8_t*)buf + tbg2::HEADER;
        auto meanAt = [&](int yb, int xb) {
          double s = 0;
          int c = 0;
          for (int y = yb; y < yb + 40; y++)
            for (int x = xb; x < xb + 40; x++) {
              const uint32_t i = (uint32_t)y * 480 + x;
              s += (bits[i >> 2] >> (6 - 2 * (i & 3))) & 3;
              c++;
            }
          return s / c;
        };
        const double tl = meanAt(80, 80), tr = meanAt(80, 360);
        const double bl = meanAt(680, 80), br = meanAt(680, 360);
        free(buf);
        if (tl > 0.05 || tr < 2.95 || bl < 0.8 || bl > 1.2 || br < 1.8 || br > 2.2) {
          printf("BMP FAIL: grey lock quadrants mean %.2f %.2f %.2f %.2f\n", tl, tr, bl, br);
          abort();
        }
      }
      // A .tbi taken afterwards removes the grey file; the .bmp taken again
      // removes the .tbi. One picture, whichever way it last arrived.
      if (!stickyHost.sdLockTake("mountains.tbi") || tfs::exists(lockimg::G2_PATH) ||
          !tfs::exists(lockimg::PATH) || !lockimg::have()) {
        printf("BMP FAIL: taking a .tbi did not retire the grey lock file\n");
        abort();
      }
      if (!stickyHost.sdLockTake("photo.bmp") || tfs::exists(lockimg::PATH) ||
          !lockimg::haveG2()) {
        printf("BMP FAIL: taking the .bmp back did not retire the .tbi\n");
        abort();
      }
      lockimg::remove();
      if (lockimg::have()) {
        printf("BMP FAIL: remove did not clear both lock files\n");
        abort();
      }
      wallimg::remove();
    }

    // Sleep art: /sleep.bmp wins; otherwise the hidden folder offers a face.
    {
      auto [b, n] = quad8(false);
      sdcard::hostPutCardFile("/.sleep/night.bmp", b, (int)n);
      if (!sdcard::sleepArtGray(gray) || gray[100 * 480 + 100] != 0 ||
          gray[100 * 480 + 400] != 255) {
        printf("BMP FAIL: /.sleep art was not picked up\n");
        abort();
      }
      auto [b2, n2] = quad8(true);
      sdcard::hostPutCardFile("/sleep.bmp", b2, (int)n2);
      if (!sdcard::sleepArtGray(gray) || gray[100 * 480 + 100] != 255) {
        printf("BMP FAIL: /sleep.bmp did not win over the folder\n");
        abort();
      }
    }
    printf("bmp art ok (list, wallpaper, grey lock, sleep pick)\n");

    // --- covers traded with CrossInk -----------------------------------------
    // Put: opening wind.epub earlier left OUR cover as their cover.bmp, in
    // their cache dir, in their format -- prove it parses back to the very
    // pixels of the big cover it came from.
    {
      char dir[64], p[112];
      epubc::cacheDir("/books/wind.epub", dir, sizeof(dir));
      snprintf(p, sizeof(p), "%s/cover.bmp", dir);
      const int sz = sdcard::hostCardFileSize(p);
      if (sz != 62 + 60 * 800) {
        printf("BMP FAIL: cover.bmp for wind.epub is %d bytes (want 48062)\n", sz);
        abort();
      }
      if (!sdcard::readBmpGray(p, gray)) {
        printf("BMP FAIL: our own cover.bmp did not parse\n");
        abort();
      }
      char big[48];
      bthumb::bigPath("/books/wind.epub", big, sizeof(big));
      static uint8_t tbc[48000];
      if (sdcard::readWhole(big, tbc, sizeof(tbc)) != (int)sizeof(tbc)) {
        printf("BMP FAIL: wind's big cover went missing\n");
        abort();
      }
      for (int i = 0; i < 480 * 800; i += 997) {
        const int want = (tbc[i >> 3] & (0x80 >> (i & 7))) ? 255 : 0;
        if (gray[i] != want) {
          printf("BMP FAIL: cover.bmp pixel %d is %d, the cover says %d\n", i, gray[i], want);
          abort();
        }
      }
    }
    // Grab: a cover.bmp another firmware left becomes our cover art without
    // any decode -- for a book that does not even exist, which is the proof
    // no decoder was involved.
    {
      auto [b, n] = quad8(false);
      char dir[64], p[112];
      epubc::cacheDir("/books/fake-cross.epub", dir, sizeof(dir));
      snprintf(p, sizeof(p), "%s/cover.bmp", dir);
      sdcard::hostPutCardFile(p, b, (int)n);
      if (!stickyHost.crossCoverGrab("/books/fake-cross.epub") ||
          !bthumb::have("/books/fake-cross.epub")) {
        printf("BMP FAIL: a waiting cover.bmp was not grabbed\n");
        abort();
      }
      static uint8_t small[bthumb::BYTES];
      if (!bthumb::load("/books/fake-cross.epub", small)) {
        printf("BMP FAIL: the grabbed cover made no thumbnail\n");
        abort();
      }
      // Top-left of the quadrant card is black, top-right white; the strip
      // thumbnail (96x160) must agree. Sampled clear of the halfway seam:
      // bytes 0-3 are columns 0-31 (deep in the black half), bytes 8-11
      // columns 64-95 (deep in the white).
      int tlBlack = 0, trBlack = 0;
      for (int y = 20; y < 40; y++)
        for (int x = 0; x < 12; x++) {
          if (x >= 4 && x < 8) continue;
          for (int k = 0; k < 8; k++) {
            const bool black = !(small[y * 12 + x] & (0x80 >> k));
            if (x < 4) tlBlack += black; else trBlack += black;
          }
        }
      if (tlBlack < 500 || trBlack > 140) {
        printf("BMP FAIL: grabbed thumbnail halves %d/%d black\n", tlBlack, trBlack);
        abort();
      }
      // ...and the legacy Murmur directory is honoured when FNV has nothing.
      epubc::cacheDirLegacy("/books/old-cross.epub", dir, sizeof(dir));
      snprintf(p, sizeof(p), "%s/cover.bmp", dir);
      sdcard::hostPutCardFile(p, b, (int)n);
      if (!stickyHost.crossCoverGrab("/books/old-cross.epub")) {
        printf("BMP FAIL: a legacy-dir cover.bmp was not grabbed\n");
        abort();
      }
    }
    // Sidecar: a user drops <book>.cover.bmp beside the book -- the one-format
    // route -- and it becomes that book's cover with no decoder involved.
    {
      auto [b, n] = quad8(false);
      sdcard::hostPutCardFile("/books/side.cover.bmp", b, (int)n);
      if (!stickyHost.coverFromBmp("/books/side.epub") ||
          !bthumb::have("/books/side.epub")) {
        printf("BMP FAIL: a .cover.bmp sidecar was not taken\n");
        abort();
      }
      static uint8_t small[bthumb::BYTES];
      if (!bthumb::load("/books/side.epub", small)) {
        printf("BMP FAIL: the sidecar cover made no thumbnail\n");
        abort();
      }
      int tlBlack = 0, trBlack = 0;
      for (int y = 20; y < 40; y++)
        for (int x = 0; x < 12; x++) {
          if (x >= 4 && x < 8) continue;
          for (int k = 0; k < 8; k++) {
            const bool black = !(small[y * 12 + x] & (0x80 >> k));
            if (x < 4) tlBlack += black; else trBlack += black;
          }
        }
      if (tlBlack < 500 || trBlack > 140) {
        printf("BMP FAIL: sidecar thumbnail halves %d/%d black\n", tlBlack, trBlack);
        abort();
      }
      // No sidecar, no claim: a book without one must say so.
      if (stickyHost.coverFromBmp("/books/bare.epub")) {
        printf("BMP FAIL: coverFromBmp invented a sidecar\n");
        abort();
      }
    }
    printf("crossink covers ok (ours put back, theirs grabbed, both dirs, sidecar bmp)\n");
    g_dumpEnabled = true;
  }

  // --- picture list paging ----------------------------------------------------
  // The card now holds more pictures than one page shows (the two .tbi fakes
  // plus every .bmp planted above), so the wallpaper page grows < PREV /
  // NEXT >, and a choice made on page two lands on the right file.
  {
    g_dumpEnabled = false;
    static char names[setui::WALL_MAX][ToolsHost::SD_NAME_LEN];
    const int n = sdcard::listTbi(names, setui::WALL_MAX);
    if (n <= setui::WALL_PER) {
      printf("PAGING FAIL: expected more than one page of pictures, got %d\n", n);
      abort();
    }
    const int pages = (n + setui::WALL_PER - 1) / setui::WALL_PER;
    toybox.goHub();
    toybox.openSettings();
    tapRect(setui::actionRect(setui::ACT_WALL));
    tapRect(setui::wallPagerNext(false));  // page two
    tapRect(setui::wallRect(0));           // its first row
    char src[40] = "";
    stickyHost.prefs().getString("wp_src", src, sizeof(src));
    if (strcmp(src, names[setui::WALL_PER]) != 0) {
      printf("PAGING FAIL: page 2 row 0 chose '%s', the list says '%s'\n", src,
             names[setui::WALL_PER]);
      abort();
    }
    // PREV returns; a second PREV is off the end and must go nowhere.
    tapRect(setui::wallPagerPrev(false));
    tapRect(setui::wallPagerPrev(false));
    tapRect(setui::wallRect(0));
    stickyHost.prefs().getString("wp_src", src, sizeof(src));
    if (strcmp(src, names[0]) != 0) {
      printf("PAGING FAIL: back on page 1, row 0 chose '%s' not '%s'\n", src, names[0]);
      abort();
    }
    // Clean up: later guards expect no wallpaper set.
    wallimg::remove();
    stickyHost.prefs().remove("wp_src");
    toybox.goHub();
    printf("picture paging ok (%d names, %d pages, both directions land)\n", n, pages);
    g_dumpEnabled = true;
  }

  // --- first open leads with the sidecar cover --------------------------------
  // A never-opened book with a .cover.bmp beside it: the FIRST paint of its
  // open must already be the cover, not the titled plate. (Beta.43 built the
  // cover after the loading paint, so a book's very first open showed the
  // plate even when the owner had supplied the picture.) The sidecar is
  // all-black on purpose: the loudest possible difference from the plate's
  // white field, measurable straight off the dumped frame.
  {
    g_dumpEnabled = false;
    static uint8_t bmp[14 + 40 + 1024 + 240 * 400];
    {
      const int W = 240, H = 400;
      const uint32_t off = 14 + 40 + 1024, img = (uint32_t)W * H, total = off + img;
      memset(bmp, 0, sizeof(bmp));
      bmp[0] = 'B'; bmp[1] = 'M';
      bmp[2] = (uint8_t)total; bmp[3] = (uint8_t)(total >> 8); bmp[4] = (uint8_t)(total >> 16);
      bmp[10] = (uint8_t)off; bmp[11] = (uint8_t)(off >> 8);
      bmp[14] = 40;
      bmp[18] = (uint8_t)(W & 255); bmp[19] = (uint8_t)(W >> 8);
      bmp[22] = (uint8_t)(H & 255); bmp[23] = (uint8_t)(H >> 8);
      bmp[26] = 1; bmp[28] = 8;
      // The palette stays all zeroes: every pixel is black.
    }
    sdcard::hostPutCardFile("/books/Uketsu/strange-houses.cover.bmp", bmp, (int)sizeof(bmp));
    if (bthumb::have("/books/Uketsu/strange-houses.epub")) {
      printf("SIDECAR-OPEN FAIL: the book already had a cover; this guard needs a first open\n");
      abort();
    }
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* eso = static_cast<EpubTool*>(toybox.hostActive());
    const int frame0 = g_dumpCounter;
    g_dumpEnabled = true;
    setScreen("sidecar_first_open");
    if (!eso->openDirect("/books/Uketsu/strange-houses.epub")) {
      printf("SIDECAR-OPEN FAIL: the book did not open\n");
      abort();
    }
    g_dumpEnabled = false;
    char fname[128];
    snprintf(fname, sizeof(fname), "preview_%02d_sidecar_first_open.pgm", frame0);
    FILE* fr = fopen(fname, "rb");
    if (!fr) {
      printf("SIDECAR-OPEN FAIL: no frame was painted (%s)\n", fname);
      abort();
    }
    char head[64];
    (void)!fgets(head, sizeof(head), fr);
    (void)!fgets(head, sizeof(head), fr);
    (void)!fgets(head, sizeof(head), fr);
    long dark = 0, totalPx = 0;
    for (int ch; (ch = fgetc(fr)) != EOF; totalPx++)
      if (ch < 128) dark++;
    fclose(fr);
    if (totalPx < 480 * 800 || dark * 2 < totalPx) {
      printf("SIDECAR-OPEN FAIL: first paint was %ld/%ld dark -- the plate, not the cover\n",
             dark, totalPx);
      abort();
    }
    toybox.goHub();
    printf("sidecar first open ok (first paint %ld%% dark = the cover)\n",
           dark * 100 / totalPx);
    g_dumpEnabled = true;
  }

  // --- unwrapping a book with no sidecar --------------------------------------
  // The other first open: no cover beside the book, so one has to be decoded
  // out of it. That open leads with the plate -- which now SAYS what the wait
  // is for -- and ends with the finished cover, full size, and a beep. The
  // sequence is what is asserted: plate first (mostly white, framed), cover
  // second (the fixture's face), one confirm beep between them.
  {
    g_dumpEnabled = false;
    // The long-named book: no sidecar, and no CrossInk cover.bmp planted for
    // it by an earlier guard, so its cover can only come from the decoder --
    // which is the path this guard is about.
    static const char kUnwrap[] =
        "/books/A Book With The Kind Of Very Long Release Filename Publishers Actually Use Vol "
        "01.epub";
    {  // put the book back to never-opened: no thumbnail, no big face
      char p[48];
      bthumb::path(kUnwrap, p, sizeof(p));
      tfs::remove(p);
      bthumb::bigPath(kUnwrap, p, sizeof(p));
      tfs::remove(p);
    }
    if (bthumb::have(kUnwrap)) {
      printf("UNWRAP FAIL: the book still had a cover; this guard needs a first open\n");
      abort();
    }
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* eu = static_cast<EpubTool*>(toybox.hostActive());
    const int frame0 = g_dumpCounter;
    const int beeps0 = buzzer::g_confirms;
    g_dumpEnabled = true;
    setScreen("unwrap");
    if (!eu->openDirect(kUnwrap)) {
      printf("UNWRAP FAIL: the book did not open\n");
      abort();
    }
    g_dumpEnabled = false;
    const int frames = g_dumpCounter - frame0;
    if (frames < 2) {
      printf("UNWRAP FAIL: %d paint(s) -- the cover never landed after the plate\n", frames);
      abort();
    }
    if (buzzer::g_confirms - beeps0 < 1) {
      printf("UNWRAP FAIL: the finished cover did not beep\n");
      abort();
    }
    if (!bthumb::have(kUnwrap)) {
      printf("UNWRAP FAIL: the open built no cover at all\n");
      abort();
    }
    // Which paint is which. The plate is words on white -- a few percent ink
    // at most, now that it draws no frame -- and a cover is a picture, which
    // is a different order of magnitude. Both tests together: a pale cover
    // could pass the first on its own, and a dark plate could pass neither.
    auto panelInk = [](int idx) {
      char fname[128];
      snprintf(fname, sizeof(fname), "preview_%02d_unwrap.pgm", idx);
      FILE* fp = fopen(fname, "rb");
      if (!fp) {
        printf("UNWRAP FAIL: no frame %s\n", fname);
        abort();
      }
      char head[64];
      for (int k = 0; k < 3; k++) (void)!fgets(head, sizeof(head), fp);
      long ink = 0, n = 0;
      for (int ch; (ch = fgetc(fp)) != EOF; n++)
        if (ch < 128) ink++;
      fclose(fp);
      if (n < PANEL_W * PANEL_H) {
        printf("UNWRAP FAIL: frame %s was short (%ld)\n", fname, n);
        abort();
      }
      return ink;
    };
    const long area = (long)PANEL_W * PANEL_H;
    const long plateInk = panelInk(frame0), coverInk = panelInk(frame0 + 1);
    if (plateInk * 12 > area) {
      printf("UNWRAP FAIL: the first paint inked %ld of %ld -- a picture, not the plate\n",
             plateInk, area);
      abort();
    }
    if (coverInk <= plateInk * 4) {
      printf("UNWRAP FAIL: the second paint added no cover (%ld vs %ld of %ld)\n", coverInk,
             plateInk, area);
      abort();
    }
    toybox.goHub();
    printf("unwrapping ok (empty plate %ld, cover %ld of %ld, %d paints, beeped)\n", plateInk,
           coverInk, area, frames);
    // The plate again with a short name, which takes the biggest face rather
    // than the step down a long one falls back to. Drawn directly because
    // every book in the fixture that would show it has a cover already; the
    // point is the overflow detector seeing 44 px type on a 480 px panel.
    g_dumpEnabled = true;
    setScreen("unwrap_short_name");
    epd.clear();
    bthumb::drawPlate(stickyHost.sharedCanvas(), "/books/wind.epub", "Strange Houses");
    epd.displayFull();
  }

  // --- the loading screen is portrait, whatever the page's angle -------------
  // A cover is a portrait object: laid into a landscape canvas it is cropped,
  // not turned, which on glass was a cover lying on its side with its bottom
  // half missing. So the plate and the cover paint standing up even when the
  // reader is set to a landscape page -- the reading rotation goes on at the
  // page, and only there.
  {
    g_dumpEnabled = false;
    stickyHost.prefs().putUInt("rd_rot", 1);  // read sideways
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* er = static_cast<EpubTool*>(toybox.hostActive());
    stickyHost.setCanvasRotation(1);  // and leave the canvas lying down beforehand
    int rotAtLoading = -1;
    g_rotWatch = &rotAtLoading;       // sampled on the first paint of the open
    if (!er->openDirect("/books/wind.epub")) {
      printf("PORTRAIT FAIL: the book did not open\n");
      abort();
    }
    g_rotWatch = nullptr;
    if (rotAtLoading != 0) {
      printf("PORTRAIT FAIL: the loading screen painted at rotation %d\n", rotAtLoading);
      abort();
    }
    if (stickyHost.canvasRotation() != 1) {
      printf("PORTRAIT FAIL: the page did not take the reading rotation (%d)\n",
             stickyHost.canvasRotation());
      abort();
    }
    toybox.goHub();
    stickyHost.setCanvasRotation(0);
    stickyHost.prefs().putUInt("rd_rot", 0);
    printf("loading portrait ok (plate and cover stand up, the page lies down)\n");
    g_dumpEnabled = true;
  }

  // --- an illustration stands up, mid-book, in a sideways reader ------------
  // The one the owner actually hit: reading in landscape, the book's own
  // artwork (its cover, as page one of many EPUBs) was blitted 480x800 into
  // an 800x480 canvas -- cropped to a square, not turned. The page must stand
  // the panel up for a picture and lie it back down for the text after it,
  // without re-laying the text out.
  {
    g_dumpEnabled = false;
    stickyHost.prefs().putUInt("rd_rot", 1);
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* ea = static_cast<EpubTool*>(toybox.hostActive());
    if (!ea->openDirect("/books/wind.epub")) {
      printf("ART ROT FAIL: the book did not open\n");
      abort();
    }
    // The book resumes wherever an earlier guard left it, so walk rather than
    // assume: to a text page first (which must lie down)...
    for (int k = 0; k < 12 && ea->hostPageImage()[0]; k++) toybox.onButton(SideBtn::Down);
    if (ea->hostPageImage()[0] || stickyHost.canvasRotation() != 1) {
      printf("ART ROT FAIL: no sideways text page found (img '%s' rot %d)\n",
             ea->hostPageImage(), stickyHost.canvasRotation());
      abort();
    }
    const int linesSideways = ea->hostLineCount();
    const int spineBefore = ea->hostSpine(), pageBefore = ea->hostPage();
    // ...then on until the illustration turns up.
    for (int k = 0; k < 12 && !ea->hostPageImage()[0]; k++) toybox.onButton(SideBtn::Down);
    if (!ea->hostPageImage()[0]) {
      printf("ART ROT FAIL: paging forward never reached the illustration\n");
      abort();
    }
    if (stickyHost.canvasRotation() != 0) {
      printf("ART ROT FAIL: the picture painted at rotation %d -- cropped, not turned\n",
             stickyHost.canvasRotation());
      abort();
    }
    // ...and back to text the other way (the fixture ends on artwork, so
    // forward has nowhere to go). Consecutive pictures must stay standing on
    // the way, never flapping the panel between them.
    for (int k = 0; k < 12 && ea->hostPageImage()[0]; k++) {
      toybox.onButton(SideBtn::Up);
      if (ea->hostPageImage()[0] && stickyHost.canvasRotation() != 0) {
        printf("ART ROT FAIL: a second picture in a row lay down (%d)\n",
               stickyHost.canvasRotation());
        abort();
      }
    }
    if (ea->hostPageImage()[0]) {
      printf("ART ROT FAIL: never got back to text (s%d p%d img '%s')\n", ea->hostSpine(),
             ea->hostPage(), ea->hostPageImage());
      abort();
    }
    if (stickyHost.canvasRotation() != 1) {
      printf("ART ROT FAIL: the text after a picture did not lie back down (%d)\n",
             stickyHost.canvasRotation());
      abort();
    }
    // ...and the text was never re-measured at the picture's rotation. Walk
    // back to the very page we started on and compare: the same page laid out
    // at a different width is a different number of lines, which is exactly
    // what a reader sees as the text reflowing under them.
    for (int k = 0; k < 12 && (ea->hostSpine() != spineBefore || ea->hostPage() != pageBefore);
         k++)
      toybox.onButton(SideBtn::Up);
    if (ea->hostSpine() != spineBefore || ea->hostPage() != pageBefore) {
      printf("ART ROT FAIL: could not get back to s%d p%d (at s%d p%d)\n", spineBefore,
             pageBefore, ea->hostSpine(), ea->hostPage());
      abort();
    }
    if (ea->hostLineCount() != linesSideways) {
      printf("ART ROT FAIL: s%d p%d now lays out %d lines, was %d -- measured at the "
             "picture's width\n",
             spineBefore, pageBefore, ea->hostLineCount(), linesSideways);
      abort();
    }
    toybox.goHub();
    stickyHost.setCanvasRotation(0);
    stickyHost.prefs().putUInt("rd_rot", 0);
    printf("art rotation ok (picture stands up, text lies back down, no relayout)\n");
    g_dumpEnabled = true;
  }

  // --- headings and bold ----------------------------------------------------
  // Chapter one opens with an <h2> and carries a <b> inside its second
  // paragraph. The heading must be ONE line, marked as a heading and set
  // larger than the body; the bold phrase must be marked bold where it
  // starts and not before it. Both are read off the laid-out lines rather
  // than the pixels, because "which bytes are bold" is the thing that was
  // wrong on glass -- the styling is per byte, and an off-by-one there shows
  // up as a bold space or a plain first letter.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* es = static_cast<EpubTool*>(toybox.hostActive());
    if (!es->openDirect("/books/wind.epub")) {
      printf("STYLE FAIL: the book did not open\n");
      abort();
    }
    es->hostGoto(0, 0);  // the top of chapter one
    if (strcmp(es->hostLine(0), "One two three") != 0) {
      printf("STYLE FAIL: line 0 is '%s', wanted the whole heading\n", es->hostLine(0));
      abort();
    }
    if (es->hostLineHead(0) == 0) {
      printf("STYLE FAIL: the heading line is not marked as one\n");
      abort();
    }
    if (es->hostLineY(1) - es->hostLineY(0) <= 0) {
      printf("STYLE FAIL: the heading left no room for the line under it\n");
      abort();
    }
    // "cafe & more": the bold opens at the ampersand, four bytes in (the e
    // is two bytes), and everything before it is body weight.
    const char* l1 = es->hostLine(1);
    const char* amp = strchr(l1, '&');
    if (!amp) {
      printf("STYLE FAIL: line 1 is '%s', wanted the bold phrase\n", l1);
      abort();
    }
    const int at = (int)(amp - l1);
    // The separating space is left to the run it precedes -- it carries no
    // ink either way, and letting it join the bold phrase keeps that phrase
    // one run, which is what the layout measured. So the check starts one
    // byte before the ampersand.
    for (int i = 0; i < at - 1; i++)
      if (es->hostLineBoldAt(1, i)) {
        printf("STYLE FAIL: byte %d of '%s' is bold before the <b> opens\n", i, l1);
        abort();
      }
    for (int i = at; l1[i]; i++)
      if (!es->hostLineBoldAt(1, i)) {
        printf("STYLE FAIL: byte %d of '%s' lost its bold\n", i, l1);
        abort();
      }
    // ...and the italic, which is a different face rather than a sheared one:
    // "ende" is inside an <em>, every byte of it is marked, and the italic
    // measures narrower than the roman. That last check is the one that would
    // catch the tables being wired to the roman by mistake -- a fallback the
    // bake script warns about but the firmware could not otherwise notice.
    es->hostGoto(0, 27);  // "ende", the paragraph after the illustrations
    const char* le = nullptr;
    int lineIdx = -1;
    for (int i = 0; i < es->hostLineCount(); i++)
      if (strstr(es->hostLine(i), "ende")) { le = es->hostLine(i); lineIdx = i; }
    if (!le) {
      printf("STYLE FAIL: no line holding the italic word\n");
      abort();
    }
    for (int i = 0; le[i]; i++)
      if (!es->hostLineItalAt(lineIdx, i)) {
        printf("STYLE FAIL: byte %d of '%s' lost its italic\n", i, le);
        abort();
      }
    {
      // Measured under LITERATA, not the default face. DejaVu's oblique is
      // drawn to the roman's own advance widths -- that is how the family is
      // designed -- so a width test there proves nothing either way. Literata
      // draws a true italic with its own narrower fit, so if the bake ever
      // falls back to the roman (make_fonts_read.py warns, and warnings get
      // scrolled past) these two numbers become equal and this fails.
#ifndef TOYBOX_CP_FONTS
      gfx::setTypeface(1);
      const int roman = stickyHost.canvas().textWidth("ende", TS_MED, false, false);
      const int ital = stickyHost.canvas().textWidth("ende", TS_MED, false, true);
      gfx::setTypeface(0);
      if (roman == ital) {
        printf("STYLE FAIL: Literata's italic measures the same as its roman (%d px)"
               " - the italic tables are the roman face\n", roman);
        abort();
      }
#endif  // the CrossPoint stand-in family has one weight and no italic
    }
    toybox.goHub();
    printf("headings, bold and italic ok (h2 whole and large, <b> and <em> exact"
           " to the byte, italic a face of its own)\n");
    g_dumpEnabled = true;
  }

  // --- a card that has been to CrossPoint and back ----------------------------
  // The bug the owner found by doing it: read a book in Toybox, flash
  // CrossPoint, read further, flash back -- and Toybox opened at the place it
  // had last seen, while CrossPoint had opened at page one. Two halves of one
  // wrong preference: Toybox wrote its position under a 64-bit hash of the
  // path and read that first, and CrossPoint keeps its whole cache (and its
  // progress.bin) under a 32-bit hash and looks nowhere else.
  //
  // So: CrossPoint's directory is read FIRST, and every save writes both.
  // Both halves are checked here, because fixing one without the other still
  // leaves a reader losing their place.
  {
    g_dumpEnabled = false;
    static const char kCross[] = "/.crosspoint/epub_836526750/progress.bin";      // 32-bit
    static const char kOurs[] = "/.crosspoint/epub_1175141337288249041/progress.bin";  // 64-bit
    // Toybox's own file says chapter one; CrossPoint's says chapter two,
    // which is where the reader actually stopped.
    {
      epubc::Progress mine{};
      mine.spine = 0; mine.page = 0; mine.pageCount = 1; mine.offset = 0; mine.hasOffset = true;
      uint8_t b[10];
      sdcard::hostPlantSide(kOurs, b, epubc::encodeProgress(mine, b));
      epubc::Progress theirs{};
      theirs.spine = 1; theirs.page = 0; theirs.pageCount = 9; theirs.offset = 2500;
      theirs.hasOffset = true;
      uint8_t c[10];
      sdcard::hostPlantSide(kCross, c, epubc::encodeProgress(theirs, c));
    }
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* ec = static_cast<EpubTool*>(toybox.hostActive());
    if (!ec->openDirect("/books/wind.epub")) {
      printf("CROSSPOINT FAIL: the book did not open\n");
      abort();
    }
    if (ec->hostSpine() != 1) {
      printf("CROSSPOINT FAIL: opened chapter %d -- ours won over CrossPoint's\n",
             ec->hostSpine());
      abort();
    }
    // ...and reading on writes BOTH, so flashing back the other way finds it.
    toybox.onButton(SideBtn::Down);
    const int spineNow = ec->hostSpine();
    const uint32_t offNow = ec->hostPageOffset();
    toybox.goHub();  // closing the book saves
    uint8_t back[10];
    if (sdcard::hostReadSide(kCross, back, sizeof(back)) != 10) {
      printf("CROSSPOINT FAIL: nothing was written where CrossPoint reads\n");
      abort();
    }
    epubc::Progress got{};
    if (!epubc::decodeProgress(back, 10, got) || got.spine != spineNow ||
        got.offset != offNow) {
      printf("CROSSPOINT FAIL: their file says s%u off %u, the reader was at s%d off %u\n",
             got.spine, got.offset, spineNow, offNow);
      abort();
    }
    if (sdcard::hostReadSide(kOurs, back, sizeof(back)) != 10) {
      printf("CROSSPOINT FAIL: our own directory stopped being written\n");
      abort();
    }
    printf("crosspoint round trip ok (their place wins, both files kept current)\n");
    g_dumpEnabled = true;
  }

  // --- a cover from an older firmware is rebuilt ------------------------------
  // Books covered before full-size covers moved to the card have the strip
  // thumbnail and nothing else, and the loading screen answers that by
  // blowing the 96x160 up five times into blocks. The builder used to look at
  // the thumbnail alone, see one, and skip -- so that blocky enlargement was
  // what those books showed for ever. Half a cover is not a cover.
  {
    g_dumpEnabled = false;
    const char* kOld = "/books/wind.epub";
    // Leave the book exactly as an old firmware would have: thumbnail yes,
    // full-size no.
    {
      static uint8_t small[bthumb::BYTES];
      if (!bthumb::load(kOld, small)) {
        printf("REBUILD FAIL: no thumbnail to start from\n");
        abort();
      }
      char big[48];
      bthumb::bigPath(kOld, big, sizeof(big));
      sdcard::hostPutCardFile(big, "", 0);  // truncate the full-size cover away
    }
    if (!bthumb::have(kOld) || bthumb::haveBig(stickyHost, kOld)) {
      printf("REBUILD FAIL: could not stage a half-covered book\n");
      abort();
    }
    if (bthumb::complete(stickyHost, kOld)) {
      printf("REBUILD FAIL: half a cover counted as complete\n");
      abort();
    }
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* eo = static_cast<EpubTool*>(toybox.hostActive());
    if (!eo->openDirect(kOld)) {
      printf("REBUILD FAIL: the book did not open\n");
      abort();
    }
    if (!bthumb::haveBig(stickyHost, kOld)) {
      printf("REBUILD FAIL: opening it did not rebuild the full-size cover\n");
      abort();
    }
    toybox.goHub();
    printf("stale cover ok (thumbnail without a full cover is rebuilt on open)\n");
    g_dumpEnabled = true;
  }

  // --- the shelf holds the card ----------------------------------------------
  // Browsing used to power the card up and put it down again for every
  // listing, and a release re-initialises the panel, so moving one level cost
  // a FULL refresh: about two seconds to open a folder. The shelf now holds
  // one session for as long as it is on screen, which makes those moves
  // partial -- and that is what this guard measures, because "feels faster"
  // is not a thing a test can see.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 10);  // the EPUB shelf, painted
    if (!sdcard::hostBrowsing()) {
      printf("BROWSE FAIL: the shelf did not take the card\n");
      abort();
    }
    auto* eb2 = static_cast<EpubTool*>(toybox.hostActive());
    const int f0 = g_fullCount;
    // Into the series folder (row 0 of the top shelf) and back out again.
    toybox.onTap(240, shelf::Y0 + 10);
    if (!eb2->hostInFolder()) {
      printf("BROWSE FAIL: the folder did not open\n");
      abort();
    }
    toybox.onTap(20, 20);  // back to the top shelf
    if (eb2->hostInFolder()) {
      printf("BROWSE FAIL: back did not leave the folder\n");
      abort();
    }
    if (g_fullCount != f0) {
      printf("BROWSE FAIL: a folder round trip cost %d full refresh(es)\n", g_fullCount - f0);
      abort();
    }
    // ...and the card is put down when the app is, so the hub never draws
    // with a powered card behind it.
    toybox.goHub();
    if (sdcard::hostBrowsing()) {
      printf("BROWSE FAIL: leaving the shelf left the card up\n");
      abort();
    }
    printf("shelf session ok (folder in and out, no full refresh, card released)\n");
    g_dumpEnabled = true;
  }

  // --- start again, per book -------------------------------------------------
  // Two taps on one row, and this book forgets where it was. The first tap
  // only asks: a row that erased a reading position on one tap would be the
  // most expensive misfire on the device. Checked on the EPUB reader, whose
  // position lives on the card, and on the .tbk reader, whose position is a
  // setting -- and bookmarks must survive both, being a different promise.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* er = static_cast<EpubTool*>(toybox.hostActive());
    if (!er->openDirect("/books/wind.epub")) {
      printf("RESET FAIL: the book did not open\n");
      abort();
    }
    // Get somewhere worth losing.
    for (int k = 0; k < 6; k++) toybox.onButton(SideBtn::Down);
    const int spineWas = er->hostSpine();
    const uint32_t offWas = er->hostPageOffset();
    if (spineWas == 0 && er->hostPage() == 0) {
      printf("RESET FAIL: could not get away from the start\n");
      abort();
    }
    static marks::Mark mbuf[marks::MAX];
    const int marksWere = marks::load(stickyHost, "/books/wind.epub", mbuf);
    toybox.onButton(SideBtn::Ok);  // the panel
    const int W = 480, rowReset = stickyHost.typefaceCount() > 1 ? 5 : 4;
    g_dumpEnabled = true;
    setScreen("epub_start_again_armed");
    toybox.onTap(240, rmenu::rootRect(rowReset, W).y + 40);  // asks
    g_dumpEnabled = false;
    if (er->hostMenu() == 0 || er->hostSpine() != spineWas) {
      printf("RESET FAIL: the first tap did something (menu %d, s%d)\n", er->hostMenu(),
             er->hostSpine());
      abort();
    }
    // A tap somewhere else takes the question away rather than leaving it
    // armed under a thumb: into Text, back out, and the next tap on the row
    // must only ASK again.
    toybox.onTap(240, rmenu::rootRect(2, W).y + 40);  // Text
    toybox.onTap(20, 20);                             // back to the root
    toybox.onTap(240, rmenu::rootRect(rowReset, W).y + 40);  // asks (not confirms)
    if (er->hostSpine() != spineWas) {
      printf("RESET FAIL: the question survived a trip to another screen\n");
      abort();
    }
    toybox.onTap(240, rmenu::rootRect(rowReset, W).y + 40);  // and confirms
    if (er->hostMenu() != 0) {
      printf("RESET FAIL: the panel stayed up after starting again\n");
      abort();
    }
    // The first page of the first chapter -- by page number, not offset: a
    // chapter's first page starts at its first visible codepoint, which is
    // not necessarily zero.
    if (er->hostSpine() != 0 || er->hostPage() != 0) {
      printf("RESET FAIL: still at s%d p%d\n", er->hostSpine(), er->hostPage());
      abort();
    }
    if (marks::load(stickyHost, "/books/wind.epub", mbuf) != marksWere) {
      printf("RESET FAIL: starting again took the bookmarks with it\n");
      abort();
    }
    // ...and the card was told, so closing and reopening still starts at the
    // beginning rather than resurrecting the old place.
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* er2 = static_cast<EpubTool*>(toybox.hostActive());
    er2->openDirect("/books/wind.epub");
    if (er2->hostSpine() != 0 || er2->hostPage() != 0) {
      printf("RESET FAIL: reopening went back to s%d p%d\n", er2->hostSpine(),
             er2->hostPage());
      abort();
    }
    toybox.goHub();
    printf("epub start again ok (asks first, forgets the place, keeps the marks)\n");

    // The shelf row has to say what the card says. It is listed once, on the
    // way into the app -- the "carries on" check is an SD.exists per book --
    // so a book read and backed out of used to keep the label it was listed
    // with. The row said "from the start" under a book that resumed perfectly
    // when tapped, which is the shelf disagreeing with itself.
    {
      toybox.open(false, 10, false);
      auto* es = static_cast<EpubTool*>(toybox.hostActive());
      if (es->hostBookCount() < 1) {
        printf("SHELF FAIL: the shelf listed nothing\n");
        abort();
      }
      // Both books on this card have been read by the guards above, so the
      // row is forced back to what a never-opened book shows. That is the
      // state the bug lived in: listed once on entry, never updated after.
      const int row = 0;
      es->hostSetCont(row, false);
      char file[128];
      es->hostFile(row, file, sizeof(file));
      if (!es->openDirect(file)) {
        printf("SHELF FAIL: could not open \"%s\" from row %d\n", file, row);
        abort();
      }
      for (int k = 0; k < 4; k++) toybox.onButton(SideBtn::Down);
      g_dumpEnabled = true;
      setScreen("tool_epub_list_place");
      toybox.onTap(20, 20);  // back to the shelf, which saves the place
      g_dumpEnabled = false;
      if (!es->hostCont(row)) {
        printf("SHELF FAIL: row %d still says \"from the start\" after reading it\n", row);
        abort();
      }
      {
        char place[64];
        es->hostPlace(row, place, sizeof(place));
        if (!place[0]) {
          printf("SHELF FAIL: row %d carries no chapter name\n", row);
          abort();
        }
      }
      toybox.goHub();
      printf("shelf label ok (a book just read no longer claims to be unstarted)\n");

      // The other half of the same line: a book whose contents could not be
      // read at all. The reader falls back to the chapter's own first words in
      // its contents list, and the shelf has to say the same thing rather than
      // "chapter 14" beside a list reading "ch 14 - Chapter 7: Friends".
      {
        toybox.open(false, 10, false);
        auto* eb = static_cast<EpubTool*>(toybox.hostActive());
        eb->hostSetCont(0, false);
        char file[128];
        eb->hostFile(0, file, sizeof(file));
        eb->openDirect(file);
        eb->hostForgetToc();  // as if the NCX had resolved to nothing
        for (int k = 0; k < 3; k++) toybox.onButton(SideBtn::Down);
        toybox.onTap(20, 20);  // back to the shelf: the note is written here
        char place[64];
        eb->hostPlace(0, place, sizeof(place));
        if (!place[0] || strncmp(place, "chapter ", 8) == 0) {
          printf("SHELF FAIL: no-contents book fell back to \"%s\"\n", place);
          abort();
        }
        toybox.goHub();
        printf("shelf fallback ok (a book with no contents still names its chapter)\n");
      }

      // ...and the same line after the app has been left and re-entered, which
      // is the path that reads the note back off the CARD rather than off the
      // row it was just written to. A chapter name as long as the field allows,
      // planted directly, because the harness's own books are called things
      // like "The long one" and would have fitted inside a buffer half the
      // size -- which is how "Chapter 7: Friends" reached hardware as
      // "Chapter 7: Frie".
      {
        toybox.open(false, 10, false);
        auto* ec = static_cast<EpubTool*>(toybox.hostActive());
        char file[128];
        ec->hostFile(0, file, sizeof(file));
        char dir[96], path[128];
        epubc::cacheDir(file, dir, sizeof(dir));
        snprintf(path, sizeof(path), "%s/toybox.pos", dir);
        // 40 bytes, the longest a chapter name may be.
        const char* kLong = "Chapter 12: The Longest Name In This Boo";
        char note[96];
        const int nn = snprintf(note, sizeof(note), "3\t7\t19\t%s\n", kLong);
        sdcard::hostPlantSide(path, note, nn);
        toybox.goHub();
        toybox.open(false, 10, false);  // lists the shelf again, from the card
        auto* ec2 = static_cast<EpubTool*>(toybox.hostActive());
        char got[64];
        ec2->hostPlace(0, got, sizeof(got));
        if (strcmp(got, kLong) != 0) {
          printf("SHELF FAIL: read back \"%s\", planted \"%s\"\n", got, kLong);
          abort();
        }
        toybox.goHub();
        printf("shelf note ok (a full-length chapter name survives the card)\n");
      }

      // The mapping the shelf line depends on, against a contents table shaped
      // like the ones that broke it on hardware: chapters in spine order, and
      // then -- at the END of the list, where these releases put them -- the
      // colour inserts and the illustration gallery, pointing back at files
      // near the front of the book.
      //
      // Scanning for the LAST row at or before the position walks past every
      // chapter and lands on one of those, which is how a book open at chapter
      // one came to be labelled "chapter 37".
      {
        epubc::Book::TocEntry toc[6] = {};
        snprintf(toc[0].title, sizeof(toc[0].title), "Cover");
        toc[0].spine = 0;
        snprintf(toc[1].title, sizeof(toc[1].title), "Chapter 1: Maomao");
        toc[1].spine = 3;
        snprintf(toc[2].title, sizeof(toc[2].title), "Chapter 2: The Two Consorts");
        toc[2].spine = 5;
        snprintf(toc[3].title, sizeof(toc[3].title), "Chapter 3: Jinshi");
        toc[3].spine = 9;
        snprintf(toc[4].title, sizeof(toc[4].title), "Colour inserts");
        toc[4].spine = 1;  // back-pointing, and listed last
        snprintf(toc[5].title, sizeof(toc[5].title), "Afterword");
        toc[5].spine = 20;
        struct { int spine; const char* want; } cases[] = {
            {0, "Cover"},
            {2, "Colour inserts"},           // genuinely the nearest row before it
            {3, "Chapter 1: Maomao"},        // the one that read "chapter 37"
            {4, "Chapter 1: Maomao"},        // still inside chapter one
            {5, "Chapter 2: The Two Consorts"},
            {12, "Chapter 3: Jinshi"},
            {25, "Afterword"},
        };
        for (const auto& c : cases) {
          const int row = epubui::tocRowForSpine(toc, 6, c.spine);
          if (row < 0 || strcmp(toc[row].title, c.want) != 0) {
            printf("CHAPTER FAIL: spine %d gave \"%s\", wanted \"%s\"\n", c.spine,
                   row < 0 ? "(none)" : toc[row].title, c.want);
            abort();
          }
        }
        // A book with no contents at all: nothing to point at, and the caller
        // falls back to the spine number.
        if (epubui::tocRowForSpine(toc, 0, 4) != -1) {
          printf("CHAPTER FAIL: an empty contents list should offer no row\n");
          abort();
        }
        printf("chapter names ok (a back-pointing insert does not become the chapter)\n");
      }
    }

    // The same row in the .tbk reader, where a position is a setting rather
    // than a file on the card. Row 3 there: Go to page, Bookmarks, Page
    // turns, Start again, Close.
    toybox.open(false, 9, false);
    auto* br = static_cast<BookTool*>(toybox.hostActive());
    // Straight to a book: the top of this shelf is a series folder, so a tap
    // at the first row would open the folder rather than anything to read.
    if (!br->openDirect("/books/One Piece/one-piece-v1.tbk")) {
      printf("RESET FAIL: the .tbk book did not open\n");
      abort();
    }
    for (int k = 0; k < 3; k++) toybox.onButton(SideBtn::Down);
    if (br->hostPage() == 0) {
      printf("RESET FAIL: the .tbk reader would not leave page one\n");
      abort();
    }
    toybox.onButton(SideBtn::Ok);                     // the panel
    toybox.onTap(240, rmenu::rootRect(3, W).y + 40);  // asks
    if (br->hostPage() == 0) {
      printf("RESET FAIL: the .tbk first tap reset it\n");
      abort();
    }
    toybox.onTap(240, rmenu::rootRect(3, W).y + 40);  // confirms
    if (br->hostMenu() != 0 || br->hostPage() != 0) {
      printf("RESET FAIL: the .tbk book is at page %lu (menu %d)\n",
             (unsigned long)br->hostPage(), br->hostMenu());
      abort();
    }
    // Closing and reopening still starts at page one.
    toybox.onButton(SideBtn::Ok);
    toybox.onTap(240, rmenu::rootRect(4, W).y + 40);  // close
    br->openDirect("/books/One Piece/one-piece-v1.tbk");  // and open it again
    if (br->hostPage() != 0) {
      printf("RESET FAIL: the .tbk book reopened at page %lu\n", (unsigned long)br->hostPage());
      abort();
    }
    toybox.goHub();
    printf("tbk start again ok (asks first, page one, and it sticks)\n");
    g_dumpEnabled = true;
  }

  // --- artwork prepared as .bmp ---------------------------------------------
  // The one format, inside the book too: chapter three's illustration is a
  // 1-bit 480x800 BMP (chapter one's is still a .tbi, so both routes stay
  // walked). The fixture builds the BMP from the .tbi's own bits, so "did it
  // draw" is the same question, with the same answer: a frame, two diagonals
  // and a blob. A BMP read as if it were top-down would put the picture on
  // the panel upside down -- which the blob, sitting above the middle, is
  // there to catch.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* eb = static_cast<EpubTool*>(toybox.hostActive());
    if (!eb->openDirect("/books/wind.epub")) {
      printf("BMP ART FAIL: the book did not open\n");
      abort();
    }
    // Walk to chapter three, which is the picture-only one.
    for (int k = 0; k < 30 && eb->hostSpine() != 2; k++) toybox.onButton(SideBtn::Down);
    if (eb->hostSpine() != 2 || strcmp(eb->hostPageImage(), "OEBPS/images/art2.png") != 0) {
      printf("BMP ART FAIL: never reached the .bmp illustration (s%d img '%s')\n",
             eb->hostSpine(), eb->hostPageImage());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_art_bmp");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    int ink = 0;
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) ink += __builtin_popcount((uint8_t)~epd.fb()[i]);
    int inkRows = 0;
    for (int y = 0; y < 480; y++)
      for (int xb = 0; xb < 100; xb++)
        if ((uint8_t)~epd.fb()[(size_t)y * 100 + xb]) {
          inkRows++;
          break;
        }
    if (ink < 9000 || inkRows < 400) {
      printf("BMP ART FAIL: the .bmp illustration drew %d px over %d rows\n", ink, inkRows);
      abort();
    }
    // Right way up. A BMP stores its rows bottom-first, so reading one as if
    // it were top-down flips the picture -- and the fixture's frame, X and
    // centred blob are symmetric enough that a flip would look identical.
    // The BMP therefore carries a solid mark near its TOP (rows 60-100), and
    // the mirror of that band must be empty.
    auto bandInk = [&](int y0, int y1) {
      long n = 0;
      for (int y = y0; y < y1; y++)
        for (int x = 40; x < 120; x++) {
          int px = 0, py = 0;
          epdMapPixel(0, epd.panelFlipX(), epd.panelFlipY(), x, y, px, py);
          if (!(epd.fb()[(size_t)py * 100 + (px >> 3)] & (0x80 >> (px & 7)))) n++;
        }
      return n;
    };
    const long topBand = bandInk(60, 100), bottomBand = bandInk(700, 740);
    if (topBand < 2000 || bottomBand > topBand / 4) {
      printf("BMP ART FAIL: mark %ld at the top, %ld at the bottom -- rows upside down\n",
             topBand, bottomBand);
      abort();
    }
    toybox.goHub();
    printf("bmp artwork ok (%d px, %d rows, right way up)\n", ink, inkRows);
    g_dumpEnabled = true;
  }

  // --- recipes ---------------------------------------------------------------
  // The parser first, on JSON shaped like the internet actually serves it:
  // @graph with an Article in the way, @type as an array, HowToStep objects,
  // a HowToSection, entities, escapes, HTML tags, ISO durations.
  {
    g_dumpEnabled = false;
    static rcp::Recipe r;
    static const char kPage[] =
        "{\"@context\":\"https://schema.org\",\"@graph\":[{\"@type\":\"Article\",\"name\":\"Blog"
        "\"},"
        "{\"@type\":[\"Recipe\"],\"name\":\"Tarte \\u00e0 l'oignon &amp; thyme\","
        "\"recipeYield\":[\"6\",\"6 slices\"],\"totalTime\":\"PT1H30M\","
        "\"recipeIngredient\":[\"2 <b>large</b> onions\",\"1 sheet puff pastry\","
        "\"2 tbsp cr\\u00e8me fra\\u00eeche\"],"
        "\"recipeInstructions\":[{\"@type\":\"HowToStep\",\"text\":\"Slice the onions &amp; cook "
        "slowly.\"},"
        "{\"@type\":\"HowToSection\",\"name\":\"Assembly\",\"itemListElement\":["
        "{\"@type\":\"HowToStep\",\"text\":\"Roll out the pastry.\"},"
        "{\"@type\":\"HowToStep\",\"text\":\"Top &#38; bake 25 minutes.\"}]}],"
        "\"author\":{\"@type\":\"Person\",\"name\":\"Someone\"}}]}";
    if (!rcp::parse(kPage, strlen(kPage), r)) {
      printf("RECIPE FAIL: the @graph page did not parse\n");
      abort();
    }
    if (strcmp(r.name, "Tarte \xc3\xa0 l'oignon & thyme") != 0 ||
        strcmp(r.yield, "6 slices") != 0 || r.totalMin != 90 || r.nIng != 3 || r.nSteps != 3) {
      printf("RECIPE FAIL: got '%s' yield '%s' %u min, %d ing %d steps\n", r.name, r.yield,
             (unsigned)r.totalMin, r.nIng, r.nSteps);
      abort();
    }
    if (strcmp(r.ing[0], "2 large onions") != 0 ||
        strcmp(r.ing[2], "2 tbsp cr\xc3\xa8me fra\xc3\xae" "che") != 0 ||
        strcmp(r.steps[0], "Slice the onions & cook slowly.") != 0 ||
        strcmp(r.steps[2], "Top & bake 25 minutes.") != 0) {
      printf("RECIPE FAIL: text mangled: '%s' / '%s' / '%s'\n", r.ing[0], r.steps[0], r.steps[2]);
      abort();
    }
    // The plain shapes: bare object, string-array steps, numeric yield.
    static const char kPlain[] =
        "{\"@type\":\"Recipe\",\"name\":\"Toast\",\"recipeIngredient\":[\"bread\"],"
        "\"recipeInstructions\":[\"Toast the bread.\",\"Butter it.\"],"
        "\"recipeYield\":4,\"prepTime\":\"PT5M\"}";
    if (!rcp::parse(kPlain, strlen(kPlain), r) || r.nSteps != 2 || r.prepMin != 5 ||
        strcmp(r.yield, "4") != 0) {
      printf("RECIPE FAIL: the plain recipe misread (%d steps, %u min, '%s')\n", r.nSteps,
             (unsigned)r.prepMin, r.yield);
      abort();
    }
    char head[48];
    rcp::headline(r, head, sizeof(head));
    if (strcmp(head, "serves 4  \xc2\xb7  5 min") != 0) {
      printf("RECIPE FAIL: headline says '%s'\n", head);
      abort();
    }
    static const char kNot[] = "{\"@type\":\"Article\",\"name\":\"not food\"}";
    if (rcp::parse(kNot, strlen(kNot), r)) {
      printf("RECIPE FAIL: an Article passed as a recipe\n");
      abort();
    }
    printf("recipe parse ok (graph, sections, entities, durations)\n");

    // The store: same name replaces, different name takes a new slot.
    static const char kDal[] =
        "{\"@type\":\"Recipe\",\"name\":\"Weeknight dal\",\"recipeIngredient\":[\"1 cup red "
        "lentils\",\"2 cloves garlic\"],\"recipeInstructions\":[\"Rinse the lentils.\",\"Fry the "
        "garlic.\",\"Simmer 20 minutes.\"],\"recipeYield\":\"4\"}";
    {
      char nm[rcp::NAME_LEN];
      const int a = rstore::save(kDal, strlen(kDal), nm, sizeof(nm));
      const int b = rstore::save(kDal, strlen(kDal), nm, sizeof(nm));
      int slots[rstore::SLOTS];
      static char names[rstore::SLOTS][rcp::NAME_LEN];
      if (a < 0 || a != b || rstore::list(slots, names, rstore::SLOTS) != 1 ||
          strcmp(names[0], "Weeknight dal") != 0) {
        printf("RECIPE FAIL: the store did not replace by name (%d vs %d)\n", a, b);
        abort();
      }
      rstore::remove(a);
      if (rstore::list(slots, names, rstore::SLOTS) != 0) {
        printf("RECIPE FAIL: a removed recipe still lists\n");
        abort();
      }
    }

    // A real recipe off a real site, for the screens rather than the parser:
    // seven ingredients long enough to wrap, twelve steps long enough to page,
    // and fractions and footnote marks that send the renderer to the
    // international face. The dal above is two lines and proves nothing about
    // a layout.
    static const char kPizza[] =
        "{\"@type\":\"Recipe\",\"name\":\"The Best Pizza Dough Recipe\",\"recipeYield\":[\"12\",\"12 "
        "servings (makes one 10-12\\\" pizza)\"],\"totalTime\":\"PT60M\",\"recipeIngredient\":[\"2-2 "
        "⅓ cups all-purpose flour OR bread flour¹ (divided (250-295g))\",\"1 packet instant yeast² (("
        "2 ¼ teaspoon))\",\"1 ½ teaspoons sugar\",\"¾ teaspoon salt\",\"⅛-¼ teaspoon garlic powder an"
        "d/or dried basil leaves (optional)\",\"2 Tablespoons olive oil (+ additional )\",\"¾ cup  wa"
        "rm water³ ((175ml))\"],\"recipeInstructions\":[{\"@type\":\"HowToStep\",\"text\":\"Combine 1"
        " cup (125g) of flour, instant yeast, sugar, and salt in a large bowl. If desired, add garlic"
        " powder and dried basil at this point as well.\",\"name\":\"Combine 1 cup (125g) of flour, i"
        "nstant yeast, sugar, and salt in a large bowl. If desired, add garlic powder and dried basil"
        " at this point as well.\"},{\"@type\":\"HowToStep\",\"text\":\"Add olive oil and warm water "
        "and use a wooden spoon to stir well very well.\",\"name\":\"Add olive oil and warm water and"
        " use a wooden spoon to stir well very well.\"},{\"@type\":\"HowToStep\",\"text\":\"Gradually"
        " add another 1 cup (125g) of flour. Add any additional flour as needed (I&#039;ve found that"
        " sometimes I need as much as an additional ⅓ cup), stirring until the dough is forming into "
        "a cohesive, elastic ball and is beginning to pull away from the sides of the bowl (see video"
        " above recipe for visual cue). The dough will still be slightly sticky but still should be m"
        "anageable with your hands.\",\"name\":\"Gradually add another 1 cup (125g) of flour. Add any"
        " additional flour as needed (I&#039;ve found that sometimes I need as much as an additional "
        "⅓ cup), stirring until the dough is forming into a cohesive, elastic ball and is beginning t"
        "o pull away from the sides of the bowl (see video above recipe for visual cue). The dough wi"
        "ll still be slightly sticky but still should be manageable with your hands.\"},{\"@type\":\""
        "HowToStep\",\"text\":\"Drizzle a separate, large, clean bowl generously with olive oil and u"
        "se a pastry brush to brush up the sides of the bowl.\",\"name\":\"Drizzle a separate, large,"
        " clean bowl generously with olive oil and use a pastry brush to brush up the sides of the bo"
        "wl.\"},{\"@type\":\"HowToStep\",\"text\":\"Lightly dust your hands with flour and form your "
        "pizza dough into a round ball and transfer to your olive oil-brushed bowl. Use your hands to"
        " roll the pizza dough along the inside of the bowl until it is coated in olive oil, then cov"
        "er the bowl tightly with plastic wrap and place it in a warm place.\",\"name\":\"Lightly dus"
        "t your hands with flour and form your pizza dough into a round ball and transfer to your oli"
        "ve oil-brushed bowl. Use your hands to roll the pizza dough along the inside of the bowl unt"
        "il it is coated in olive oil, then cover the bowl tightly with plastic wrap and place it in "
        "a warm place.\"},{\"@type\":\"HowToStep\",\"text\":\"Allow dough to rise for 30 minutes or u"
        "ntil doubled in size. If you intend to bake this dough into a pizza, I also recommend prehea"
        "ting your oven to 425F (215C) at this point so that it will have reached temperature once yo"
        "ur pizza is ready to bake.\",\"name\":\"Allow dough to rise for 30 minutes or until doubled "
        "in size. If you intend to bake this dough into a pizza, I also recommend preheating your ove"
        "n to 425F (215C) at this point so that it will have reached temperature once your pizza is r"
        "eady to bake.\"},{\"@type\":\"HowToStep\",\"text\":\"Once the dough has risen, use your hand"
        "s to gently deflate it and transfer to a lightly floured surface and knead briefly until smo"
        "oth (about 3-5 times).&nbsp;\",\"name\":\"Once the dough has risen, use your hands to gently"
        " deflate it and transfer to a lightly floured surface and knead briefly until smooth (about "
        "3-5 times).&nbsp;\"},{\"@type\":\"HowToStep\",\"text\":\"Use either your hands or a rolling "
        "pin to work the dough into 12\\\" circle.\",\"name\":\"Use either your hands or a rolling pi"
        "n to work the dough into 12\\\" circle.\"},{\"@type\":\"HowToStep\",\"text\":\"Transfer doug"
        "h to a parchment paper lined pizza pan and either pinch the edges or fold them over to form "
        "a crust.\",\"name\":\"Transfer dough to a parchment paper lined pizza pan and either pinch t"
        "he edges or fold them over to form a crust.\"},{\"@type\":\"HowToStep\",\"text\":\"Drizzle a"
        "dditional olive oil (about a Tablespoon) over the top of the pizza and use your pastry brush"
        " to brush the entire surface of the pizza (including the crust) with olive oil.&nbsp;\",\"na"
        "me\":\"Drizzle additional olive oil (about a Tablespoon) over the top of the pizza and use y"
        "our pastry brush to brush the entire surface of the pizza (including the crust) with olive o"
        "il.&nbsp;\"},{\"@type\":\"HowToStep\",\"text\":\"Use a fork to poke holes all over the cente"
        "r of the pizza to keep the dough from bubbling up in the oven.\",\"name\":\"Use a fork to po"
        "ke holes all over the center of the pizza to keep the dough from bubbling up in the oven.\"}"
        ",{\"@type\":\"HowToStep\",\"text\":\"Add desired toppings (see the notes for a link to my fa"
        "vorite, 5-minute pizza sauce recipe!) and bake in a 425F (215C) preheated oven for 13-15 min"
        "utes or until toppings are golden brown. Slice and serve.\",\"name\":\"Add desired toppings "
        "(see the notes for a link to my favorite, 5-minute pizza sauce recipe!) and bake in a 425F ("
        "215C) preheated oven for 13-15 minutes or until toppings are golden brown. Slice and serve."
        "\"}]}";

    // The app itself, over a card recipe and a phone one.
    sdcard::hostPutCardFile("/recipes/tarte.json", kPage, (int)strlen(kPage));
    toybox.open(false, 11);
    g_dumpEnabled = true;
    setScreen("tool_recipe_help");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    // The way out works even with the help card up -- on glass this tap
    // rebooted the device once; at minimum it must leave cleanly.
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    if (toybox.hostInApp()) {
      printf("RECIPE FAIL: HUB did not leave from under the help card\n");
      abort();
    }
    toybox.open(false, 11);  // fresh entry: the card is up again
    auto* rt = static_cast<RecipeTool*>(toybox.hostActive());
    toybox.onTap(240, 650);  // GOT IT
    if (rt->hostCount() != 1) {
      printf("RECIPE FAIL: the card recipe did not list (%d)\n", rt->hostCount());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_recipe_list");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(240, rcpui::LIST_Y0 + 20);  // the one row
    if (rt->hostScreen() != 1 || rt->hostRecipe().nIng != 3) {
      printf("RECIPE FAIL: the card recipe did not open (screen %d)\n", rt->hostScreen());
      abort();
    }
    toybox.onTap(100, rt->hostIngTop() + rt->hostIngRowH() / 2);  // tick the onions
    if (!rt->hostTicked(0)) {
      printf("RECIPE FAIL: the tick did not take\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_recipe_view");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(240, rt->hostCookBtn().y + 30);
    if (rt->hostScreen() != 2 || rt->hostStep() != 0) {
      printf("RECIPE FAIL: COOK did not open step one\n");
      abort();
    }
    toybox.onTap(400, 750);  // the NEXT zone
    toybox.onButton(SideBtn::Down);
    if (rt->hostStep() != 2) {
      printf("RECIPE FAIL: stepping landed on %d, not 2\n", rt->hostStep());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_recipe_cook");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(400, 750);  // past the last step: done, back to the recipe
    if (rt->hostScreen() != 1) {
      printf("RECIPE FAIL: finishing did not return to the recipe\n");
      abort();
    }
    toybox.onTap(50, 26);  // back to the list

    // The phone route, through the same store the real page posts to.
    toybox.onTap(240, rcpui::PHONE_BTN.y + 20);
    if (rt->hostScreen() != 3) {
      printf("RECIPE FAIL: the phone button did not open pairing\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_recipe_phone");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    if (!rt->hostNet().hostPost(kDal)) {
      printf("RECIPE FAIL: the posted recipe was refused\n");
      abort();
    }
    toybox.onTap(rcpui::DONE_BTN.x + 30, rcpui::DONE_BTN.y + 30);
    if (rt->hostCount() != 2) {
      printf("RECIPE FAIL: the phone recipe did not join the list (%d)\n", rt->hostCount());
      abort();
    }
    toybox.onTap(240, rcpui::LIST_Y0 + 20);  // flash recipes list first
    if (rt->hostScreen() != 1 || rt->hostRecipe().nSteps != 3) {
      printf("RECIPE FAIL: the phone recipe did not open\n");
      abort();
    }

    // --- the options panel ---------------------------------------------------
    // Size and rotation, and the thing they are really testing: that the
    // ingredient list is measured rather than written down. A turned panel is
    // 480 px tall where the old layout started its rows at 228 and ran six of
    // 66 -- 624 px of list into 480 px of screen. The overflow detector cannot
    // see that (the rows are drawn, just off the bottom), so the count is
    // checked here and the paint is left to the detector for the rest.
    {
      // On the pizza: seven ingredients and twelve steps, so the pager and
      // the wrapping are both in play. It lands on the card here rather than
      // at the top of this guard so that every check above still runs against
      // the one recipe it was written for. Found by opening rows rather than
      // by index -- the list is flash-first and its order is not this guard's
      // business.
      sdcard::hostPutCardFile("/recipes/pizza.json", kPizza, (int)strlen(kPizza));
      toybox.open(false, 11);  // a fresh entry re-reads /recipes
      rt = static_cast<RecipeTool*>(toybox.hostActive());
      toybox.onTap(240, 650);  // GOT IT, the help card again
      bool found = false;
      for (int k = 0; k < rcpui::LIST_PER && !found; k++) {
        toybox.onTap(240, rcpui::LIST_Y0 + k * rcpui::LIST_ROW + 20);
        if (rt->hostScreen() == 1 && rt->hostRecipe().nIng == 7) found = true;
        else if (rt->hostScreen() == 1) toybox.onTap(50, 26);
      }
      if (!found) {
        printf("RECIPE FAIL: the pizza did not open off the card\n");
        abort();
      }
      if (rt->hostRecipe().nSteps != 12) {
        printf("RECIPE FAIL: the pizza has %d steps, wanted 12\n",
               (int)rt->hostRecipe().nSteps);
        abort();
      }
      const int perPortrait = rt->hostIngPer();
      const int rowPortrait = rt->hostIngRowH();
      if (rt->hostRecipe().nIng <= perPortrait) {
        printf("RECIPE FAIL: seven ingredients fit on one page (%d), so the pager"
               " is untested\n", perPortrait);
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      if (!rt->hostMenu()) {
        printf("RECIPE FAIL: OK did not open the options panel\n");
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_recipe_options");
      stickyHost.refresh(true);
      checkCornerClock("recipe");
      g_dumpEnabled = false;

      // Bigger type, taller rows, and never more of them. The height is the
      // strict test: under a font family whose medium and large are close --
      // CrossPoint's are 24 and 29 px -- a taller row can still divide into
      // the same count, and asserting fewer rows there is asserting something
      // about the font rather than about this code.
      toybox.onTap(480 - 66, RecipeTool::hostStepperY(0) + 64);  // the + of the size stepper
      if (rt->hostSize() != 1) {
        printf("RECIPE FAIL: the size row did not step (%d)\n", rt->hostSize());
        abort();
      }
      toybox.onButton(SideBtn::Ok);  // close, back to the recipe
      const int perLarge = rt->hostIngPer(), rowLarge = rt->hostIngRowH();
      if (rowLarge <= rowPortrait || perLarge > perPortrait) {
        printf("RECIPE FAIL: large type gives rows of %d (was %d), %d per page (was %d)\n",
               rowLarge, rowPortrait, perLarge, perPortrait);
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_recipe_view_large");
      stickyHost.refresh(true);
      g_dumpEnabled = false;

      // Line spacing: three steps back round to where it started, and the
      // rows breathe on the way. Same measured property as the size -- the
      // row height has to follow it, or "airy" is a word with no effect.
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(480 - 66, RecipeTool::hostStepperY(1) + 64);  // spacing +: normal -> airy
      if (rt->hostLead() != 2) {
        printf("RECIPE FAIL: the spacing row did not step (%d)\n", rt->hostLead());
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      const int rowAiry = rt->hostIngRowH();
      if (rowAiry <= rowLarge) {
        printf("RECIPE FAIL: airy rows are %d, normal ones %d\n", rowAiry, rowLarge);
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(66, RecipeTool::hostStepperY(1) + 64);  // spacing -: airy -> normal
      if (rt->hostLead() != 1) {
        printf("RECIPE FAIL: the spacing did not cycle back (%d)\n", rt->hostLead());
        abort();
      }

      // ...and turned. Every row, and the COOK button, must still be on the
      // panel: that is the whole of what the old constants got wrong.
      {  // the LEFT button of the three, the same control the readers have
        const TRect b = rt->hostRotRect(0);
        toybox.onTap(b.x + b.w / 2, b.y + b.h / 2);
      }
      if (rt->hostRot() != 1) {
        printf("RECIPE FAIL: the rotation buttons did not turn it (%d)\n", rt->hostRot());
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      if (stickyHost.canvas().width() <= stickyHost.canvas().height()) {
        printf("RECIPE FAIL: the recipe page did not turn (%d x %d)\n",
               stickyHost.canvas().width(), stickyHost.canvas().height());
        abort();
      }
      const int per = rt->hostIngPer();
      const int lastRow = rt->hostIngTop() + per * rt->hostIngRowH();
      if (per < 1 || lastRow > stickyHost.canvas().height()) {
        printf("RECIPE FAIL: %d turned rows end at %d on a %d-tall panel\n", per, lastRow,
               stickyHost.canvas().height());
        abort();
      }
      const TRect cb = rt->hostCookBtn();
      if (cb.y + cb.h > stickyHost.canvas().height() || cb.x + cb.w > stickyHost.canvas().width()) {
        printf("RECIPE FAIL: the COOK button is off a turned panel\n");
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_recipe_view_turned");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      toybox.onTap(cb.x + 30, cb.y + 30);  // and the steps, turned and large
      if (rt->hostScreen() != 2) {
        printf("RECIPE FAIL: COOK did not open on a turned panel\n");
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_recipe_cook_turned");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      toybox.onTap(50, 26);  // back to the recipe

      // Put it back, and check the panel goes portrait with it: the list and
      // the pairing screen are portrait designs and have no landscape.
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(66, RecipeTool::hostStepperY(0) + 64);  // size -, back to normal
      {  // and PORTRAIT, the middle of the three
        const TRect b = rt->hostRotRect(1);
        toybox.onTap(b.x + b.w / 2, b.y + b.h / 2);
      }
      if (rt->hostSize() != 0 || rt->hostRot() != 0) {
        printf("RECIPE FAIL: the panel did not cycle back (size %d, rot %d)\n", rt->hostSize(),
               rt->hostRot());
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      printf("recipe options ok (size, spacing and rotation measured, not written down)\n");
    }

    toybox.onTap(50, 26);  // back to the list
    toybox.onTap(rcpui::delRect(0).x + 20, rcpui::delRect(0).y + 20);  // delete it
    if (rt->hostCount() != 2) {  // the two on the card, which no x can remove
      printf("RECIPE FAIL: delete left %d recipes\n", rt->hostCount());
      abort();
    }
    toybox.goHub();
    printf("recipes ok (card list, ticks, step pages, phone import)\n");
    g_dumpEnabled = true;
  }

  // --- long book paths -------------------------------------------------------
  // The second invented epub's path is 93 bytes -- longer than the 64-byte
  // buffers that silently truncated a real book's name on a real card. It has
  // to list, open, read, and write its progress under the right hash.
  {
    g_dumpEnabled = false;
    toybox.open(false, 10);
    auto* et = static_cast<EpubTool*>(toybox.hostActive());
    toybox.onTap(240, epubui::LIST_Y0 + 2 * epubui::LIST_ROW_H + 10);  // folder, wind, then it
    if (et->hostScreen() != 1 || et->hostSpine() != 0) {
      printf("LONGPATH FAIL: the long-named book did not open\n");
      abort();
    }
    toybox.onButton(SideBtn::Down);  // a turn, so progress lands on the card

    // The footer, on the book with the punishing name. This is the render the
    // overflow guard needed and never had: every other fixture is called
    // "wind" or "Walden", so a title long enough to run over the page number
    // -- which is what a real release filename does -- was never drawn here at
    // all. The bug reached hardware because the harness only owned short books.
    g_dumpEnabled = true;
    setScreen("tool_epub_footer_long");
    toybox.onTap(240, 400);  // the middle toggles the footer
    g_dumpEnabled = false;
    toybox.onTap(240, 400);  // and away again

    char dir[96];
    epubc::cacheDir(
        "/books/A Book With The Kind Of Very Long Release Filename Publishers Actually Use Vol 01.epub",
        dir, sizeof(dir));
    char pp[128];
    snprintf(pp, sizeof(pp), "%s/progress.bin", dir);
    uint8_t buf[16];
    if (stickyHost.sdReadFile(pp, buf, sizeof(buf)) != 10) {
      printf("LONGPATH FAIL: progress not written under the full-path hash\n");
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("long paths ok (93-byte name lists, opens, and keeps its position)\n");
  }

  // The battery icon, which had never been rendered here at all: it is drawn
  // only when a gauge answers, and the harness answers with no gauge, so it
  // went to hardware unlooked at.
  {
    g_dumpEnabled = false;
    auto blackAt = [](int lx, int ly) {
      int px, py;
      epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), lx, ly, px, py);
      return (epd.fb()[(uint32_t)py * EPD_WB + (px >> 3)] & (0x80 >> (px & 7))) == 0;
    };
    // The battery is a NUMBER now, not a drawn cell with a bar in it. The old
    // guard measured the bar against the percentage; there is no bar, so this
    // measures the two things the number still has to get right: it is there
    // at all when the gauge answered, it is not there when the gauge did not
    // (an invented number is worse than no number), and charging adds a mark
    // to the left of it rather than changing the number.
    const int RIGHT = 200, TOP = 100;
    auto inkNear = [&](int x0, int x1) {
      int n = 0;
      for (int y = TOP - 6; y < TOP + 22; y++)
        for (int x = x0; x < x1; x++)
          if (blackAt(x, y)) n++;
      return n;
    };
    sensors::hostSetBattery(-1, false);
    epd.clear();
    hubHostBattery(stickyHost.sharedCanvas(), stickyHost, RIGHT, TOP, true);
    if (inkNear(RIGHT - 90, RIGHT + 4) != 0) {
      printf("BATTERY FAIL: something was drawn with no gauge answering\n");
      abort();
    }
    sensors::hostSetBattery(89, false);
    epd.clear();
    hubHostBattery(stickyHost.sharedCanvas(), stickyHost, RIGHT, TOP, true);
    const int plain = inkNear(RIGHT - 90, RIGHT + 4);
    if (plain < 40) {
      printf("BATTERY FAIL: the percentage drew %d px\n", plain);
      abort();
    }
    // Charging draws NOTHING extra: the board's own LED by the port says that,
    // lit whether the panel is awake or asleep, which no mark on a 1.7-second
    // screen can match. Two indicators for one fact means one of them is
    // eventually wrong.
    sensors::hostSetBattery(89, true);
    epd.clear();
    hubHostBattery(stickyHost.sharedCanvas(), stickyHost, RIGHT, TOP, true);
    const int charging = inkNear(RIGHT - 90, RIGHT + 4);
    if (charging != plain) {
      printf("BATTERY FAIL: charging drew %d px where not charging drew %d\n", charging, plain);
      abort();
    }
    sensors::hostSetBattery(89, false);
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    setScreen("hub_battery");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();
    g_dumpEnabled = false;
    sensors::hostSetBattery(-1, false);
    toybox.goHub();
    g_dumpEnabled = true;
    printf("battery ok (a number only, and charging is the LED's job)\n");
  }

  setScreen("hub_hidden");
  toybox.goHub();
  toybox.hostHub().goHome();
  checkHubRouting("four hidden");

  // Erasing scores and restoring the rules cards, checked rather than drawn --
  // then everything is put back so the screens below show a lived-in device.
  g_dumpEnabled = false;
  prefs.putBool("h_wrd", true);
  toybox.openSettings();  // the routing walk left us on the hub: go back in
  tapRect(setui::actionRect(setui::ACT_CARDS));
  tapRect(setui::actionRect(setui::ACT_RESET));  // armed again
  tapRect(setui::actionRect(setui::ACT_RESET));  // ...and confirmed
  // Cleared means gone, not zeroed: a sentinel default proves the key itself
  // was removed, so a fresh device and a reset one read identically.
  if (prefs.getInt("w_games", -1) != -1 || prefs.getUInt("c_heads", 99) != 99 ||
      help::suppressed(prefs, "wrd")) {
    printf("SETTINGS FAIL: reset left something behind\n");
    abort();
  }
  printf("settings ok (hides, reflows, clears scores, restores cards)\n");

  appvis::g_mask = appvis::ALL;
  buzzer::setEnabled(true);
  prefs.putInt("w_games", 12);
  prefs.putInt("w_wins", 10);
  prefs.putInt("w_streak", 4);
  prefs.putInt("w_max", 7);
  prefs.putInt("t_best", 20116);
  prefs.putInt("t_tile", 1024);
  prefs.putUInt("n10_best", 412);
  prefs.putUInt("c_heads", 63);
  prefs.putUInt("c_tails", 57);
  prefs.putInt("x_w", 3);
  prefs.putInt("x_l", 1);
  prefs.putInt("x_d", 2);
  prefs.putInt("x_strk", 2);
  prefs.putInt("x_best", 4);
  prefs.putInt("sd_w0", 3);
  prefs.putInt("bs_w", 2);
  prefs.putInt("bs_l", 1);
  g_dumpEnabled = true;

  // --- wordle (portrait keyboard: KEY_W 44, KGAP 4, rows 628/686/744) ------
  // Every game now opens on its rules card, so each flow dismisses it first.
  // Routing and settings above opened every app many times over, and each open
  // draws random numbers. Reseed so the puzzles, fleets and rolls in the images
  // below are the same on every run.
  hostReseed(42);

  // The settings guard above erases every score, including the guess
  // distribution. Put one back so the end-of-game screen renders its chart:
  // with nothing to chart it now draws nothing at all, which is the point of
  // the change but not the picture worth keeping.
  prefs.putInt("w_d3", 4);
  prefs.putInt("w_d4", 5);
  prefs.putInt("w_d5", 1);

  setScreen("help_wordle");
  toybox.open(true, 0);
  g_dumpEnabled = false;
  toybox.onTap(240, 653);  // GOT IT
  auto wordleKey = [&](char ch) {
    const char* r1 = "QWERTYUIOP";
    const char* r2 = "ASDFGHJKL";
    const char* r3 = "ZXCVBNM";
    for (int i = 0; r1[i]; i++)
      if (r1[i] == ch) return toybox.onTap(2 + i * 48 + 22, 628 + 26);
    for (int i = 0; r2[i]; i++)
      if (r2[i] == ch) return toybox.onTap(26 + i * 48 + 22, 686 + 26);
    for (int i = 0; r3[i]; i++)
      if (r3[i] == ch) return toybox.onTap(6 + 64 + 4 + i * 48 + 22, 744 + 26);
  };
  for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
  g_dumpEnabled = true;
  setScreen("wordle");
  toybox.onTap(6 + 32, 744 + 26);  // ENTER

  // Play it out so the end-of-game plaque is rendered rather than assumed. The
  // last ENTER is the frame we want, so everything before it is silent.
  g_dumpEnabled = false;
  for (int g = 0; g < 4; g++) {
    for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
    toybox.onTap(6 + 32, 744 + 26);
  }
  for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
  g_dumpEnabled = true;
  setScreen("wordle_over");
  toybox.onTap(6 + 32, 744 + 26);  // ENTER: the sixth guess ends it

  setScreen("help_nonogram");
  toybox.open(true, 1);
  setScreen("nonogram");
  toybox.onTap(240, 653);  // GOT IT, which repaints the board

  setScreen("nonogram_solved");
  static_cast<NonogramApp*>(toybox.hostActive())->hostSetWon(412);

  setScreen("help_2048");
  toybox.open(true, 2);
  g_dumpEnabled = false;
  toybox.onTap(240, 653);  // GOT IT
  toybox.onSwipe(-120, 10);
  g_dumpEnabled = true;
  setScreen("g2048_merge");
  toybox.onSwipe(10, -120);  // a wedge marks what merged, the new tile is dashed

  // ...and the next move clears the mark, because it belongs to the move that
  // made it rather than to a timer.
  setScreen("g2048");
  toybox.onSwipe(10, 120);

  setScreen("g2048_win");
  static_cast<G2048App*>(toybox.hostActive())->hostSetOver(true);
  setScreen("g2048_over");
  static_cast<G2048App*>(toybox.hostActive())->hostSetOver(false);

  // --- XO: classic vs HARD, then the 3-mark variant -----------------------
  setScreen("help_xo");
  toybox.open(true, 3);
  g_dumpEnabled = false;
  toybox.onTap(240, 653);  // GOT IT
  auto xoTap = [&](int cell) { toybox.onTap(42 + (cell % 3) * 132 + 66, 120 + (cell / 3) * 132 + 66); };
  xoTap(4);  // centre; the machine answers in the same frame
  g_dumpEnabled = true;
  setScreen("xo");
  xoTap(0);

  setScreen("xo_three");
  g_dumpEnabled = false;
  toybox.onTap(42 + 110, 602 + 27);  // rule toggle -> 3 MARKS
  xoTap(4);
  xoTap(0);
  xoTap(8);
  g_dumpEnabled = true;
  xoTap(2);

  // Pass-and-play, taken to a win, so the result plaque gets rendered.
  setScreen("xo_won");
  g_dumpEnabled = false;
  toybox.onTap(42 + 110, 602 + 27);   // back to CLASSIC
  toybox.onTap(15 + 2 * 155 + 70, 536 + 27);  // 2 PLAYER
  xoTap(0);
  xoTap(3);
  xoTap(1);
  xoTap(4);
  g_dumpEnabled = true;
  xoTap(2);  // X takes the top row

  // --- tools -------------------------------------------------------------
  setScreen("tool_coin");
  g_dumpEnabled = false;
  toybox.open(false, 0);
    g_dumpEnabled = true;
  toybox.onTap(240, 410 + 42);  // FLIP

  setScreen("tool_coin_multi");
  toybox.onTap(250 + 85, 506 + 28);  // FLIP x10

  setScreen("tool_dice");
  g_dumpEnabled = false;
  toybox.open(false, 1);
    quietTap(40 + 5 * 80, 50 + 44);  // D20, last of the six wireframes
  quietTap(380 + 20, 166 + 20);          // count +
  quietTap(380 + 20, 166 + 20);
  quietTap(380 + 20, 212 + 20);  // mod +
  quietTap(380 + 20, 212 + 20);
  g_dumpEnabled = true;
  toybox.onTap(240, 262 + 38);  // ROLL

  setScreen("tool_dice_d6");
  g_dumpEnabled = false;
  quietTap(40 + 1 * 80, 50 + 44);  // D6
  g_dumpEnabled = true;
  toybox.onTap(240, 262 + 38);

  setScreen("tool_timer");
  g_dumpEnabled = false;
  toybox.open(false, 2);
    quietTap(20 + 112 + 52, 372 + 26);  // 25 min preset (row 1, col 1)
  g_dumpEnabled = true;
  tapRect(timerui::CD_START);

  // The other half of the same tool. It had never been rendered here, so the
  // lap list below the clock had never been looked at at all.
  setScreen("tool_timer_stopwatch");
  g_dumpEnabled = false;
  tapRect(timerui::MODE_SW);
  tapRect(timerui::SW_START);
  for (int k = 0; k < 3; k++) {
    delay(37500);  // the mock clock only moves when something asks it to
    tapRect(timerui::SW_LAP);
  }
  delay(12000);
  g_dumpEnabled = true;
  stickyHost.refresh(false);  // the stopwatch repaints on its own every 10s

  setScreen("tool_random");
  g_dumpEnabled = false;
  toybox.open(false, 3);
    g_dumpEnabled = true;
  toybox.onTap(240 + 100, 560 + 31);  // DRAW

  setScreen("tool_card");
  g_dumpEnabled = false;
  quietTap(randui::MODE_CARD.x + randui::MODE_CARD.w / 2,
           randui::MODE_CARD.y + randui::MODE_CARD.h / 2);  // CARD mode
  quietTap(randui::DRAW_CARD.x + randui::DRAW_CARD.w / 2,
           randui::DRAW_CARD.y + randui::DRAW_CARD.h / 2);
  quietTap(randui::DRAW_CARD.x + randui::DRAW_CARD.w / 2,
           randui::DRAW_CARD.y + randui::DRAW_CARD.h / 2);
  g_dumpEnabled = true;
  tapRect(randui::DRAW_CARD);  // DRAW

  // Picker: type items on the portrait keyboard (KEY_W 44, rows 340/404/468).
  setScreen("tool_picker_kb");
  g_dumpEnabled = false;
  toybox.open(false, 4);
    auto typeItem = [&](const char* s) {
    quietTap(20 + 107, 452 + 25);  // ADD ITEM
    for (const char* p = s; *p; p++) {
      const char* r1 = "QWERTYUIOP";
      const char* r2 = "ASDFGHJKL";
      const char* r3 = "ZXCVBNM";
      int kx = -1, ky = -1;
      if (*p == ' ') { quietTap(160 + 80, 540 + 28); continue; }
      for (int i = 0; r1[i]; i++)
        if (r1[i] == *p) kx = 2 + i * 48 + 22, ky = 340 + 28;
      for (int i = 0; r2[i]; i++)
        if (r2[i] == *p) kx = 26 + i * 48 + 22, ky = 404 + 28;
      for (int i = 0; r3[i]; i++)
        if (r3[i] == *p) kx = 74 + i * 48 + 22, ky = 468 + 28;
      if (kx >= 0) quietTap(kx, ky);
    }
  };
  const int PK_DEL_X = 330 + 65, PK_DEL_Y = 540 + 28;
  const int PK_OK_X = 20 + 220, PK_OK_Y = 540 + 72 + 32;
  typeItem("ALICE");
  g_dumpEnabled = true;
  toybox.onTap(2 + 22, 340 + 28);  // one more key, to capture the keyboard
  g_dumpEnabled = false;
  quietTap(PK_DEL_X, PK_DEL_Y);  // DEL the stray key
  quietTap(PK_OK_X, PK_OK_Y);    // OK
  typeItem("BOB");
  quietTap(PK_OK_X, PK_OK_Y);
  typeItem("CHARLIE");
  quietTap(PK_OK_X, PK_OK_Y);
  g_dumpEnabled = true;
  setScreen("tool_picker");
  toybox.onTap(20 + 220, 676 + 35);  // PICK ONE

  // The picker can also take its list from a phone.
  setScreen("tool_picker_phone");
  toybox.onTap(245 + 107, 444 + 23);  // FROM PHONE

  setScreen("tool_picker_phone_done");
  g_dumpEnabled = false;
  toybox.tick();
  toybox.tick();
  g_dumpEnabled = true;
  toybox.tick();  // stub reports a list arrived

  setScreen("tool_picker_after");
  tapRect(pickui::DONE_BTN);  // DONE -> back to the list

  {
    // What arrives from the phone has to arrive whole. This used to fold every
    // non-ASCII codepoint to a question mark, a rule left over from an 8x8 font
    // that has not been in the firmware for a long time, and a Thai list came
    // out as six rows of ??????.
    plist::Item items[plist::MAX_ITEMS] = {};
    const int n = plist::fromText("กะเพรา\nJosé\n\u007f\nend\n", items);
    if (n != 3 || strcmp(items[0], "กะเพรา") != 0 || strcmp(items[1], "José") != 0 ||
        strcmp(items[2], "end") != 0) {
      printf("PICKER FAIL: got %d items, first \"%s\" second \"%s\"\n", n, items[0], items[1]);
      abort();
    }
    // ...and the cut at the length limit falls between characters, never
    // inside one, or the panel gets a half-written glyph.
    char longThai[256] = {};
    for (int i = 0; i < 30; i++) strcat(longThai, "ก");
    plist::fromText(longThai, items);
    if (uni::count(items[0]) != plist::MAX_CHARS) {
      printf("PICKER FAIL: a long Thai item cut to %d characters, wanted %d\n",
             uni::count(items[0]), plist::MAX_CHARS);
      abort();
    }
    for (const char* q = items[0]; *q;) {
      const char* r = q;
      if (uni::next(r) == 0xFFFD || r > items[0] + plist::MAX_BYTES) {
        printf("PICKER FAIL: the cut landed inside a character\n");
        abort();
      }
      q = r;
    }
    printf("picker text ok (UTF-8 survives, the cut falls between characters)\n");
  }

  // --- flashcards --------------------------------------------------------
  {
    using namespace fcard;
    char saved[NAME_LEN + 1];
    importDeck("french starter",
               "bonjour\thello\nmerci\tthank you\ns'il vous plait\tplease\n"
               "au revoir\tgoodbye\noui\tyes\nnon\tno\nl'eau\twater\nle pain\tbread\n",
               saved);
    importDeck("chem exam",
               "Avogadro constant | 6.022 x 10^23 particles per mole\n"
               "molarity | moles of solute per litre of solution\n"
               "catalyst | speeds a reaction without being consumed\n"
               "isotope | same element, different neutron count\n",
               saved);
    importDeck("thai phrases", "sawasdee - hello\nkhop khun - thank you\n", saved);
    // Pretend some cards are already mastered so the bars are not all empty.
    Card* deck = (Card*)malloc(sizeof(Card) * MAX_CARDS);
    const int n = loadDeck("french starter", deck, MAX_CARDS);
    for (int i = 0; i < n; i += 2) deck[i].box = MAX_BOX;
    saveBoxes("french starter", deck, n);
    free(deck);
  }

  setScreen("help_flashcards");
  toybox.open(false, 5);

  setScreen("tool_flash_decks");
  tapRect(help::OK_BTN);

  setScreen("tool_flash_front");
  tapRect(fcui::rowRect(0, 3));  // open the first deck

  setScreen("tool_flash_back");
  toybox.onTap(240, 220);  // tap the card to reveal

  {
    // The side buttons have to reach the same three actions the panel does, and
    // reach nothing at all anywhere else. A button that quietly graded a card
    // from the deck list would be the worst kind of bug: silent, and it moves
    // something you cannot see.
    g_dumpEnabled = false;
    const int paintsBefore = g_paintCount;
    if (!toybox.onButton(SideBtn::Down)) {  // the card is revealed: GOT IT
      printf("BUTTON FAIL: DOWN did nothing on a revealed card\n");
      abort();
    }
    if (toybox.onButton(SideBtn::Up)) {  // now on the front of the next card
      printf("BUTTON FAIL: UP graded a card that had not been revealed\n");
      abort();
    }
    if (!toybox.onButton(SideBtn::Down)) {  // reveal it
      printf("BUTTON FAIL: DOWN did not reveal the next card\n");
      abort();
    }
    if (!toybox.onButton(SideBtn::Up)) {  // AGAIN
      printf("BUTTON FAIL: UP did not send a revealed card back\n");
      abort();
    }
    if (g_paintCount <= paintsBefore) {
      printf("BUTTON FAIL: three button presses and nothing repainted\n");
      abort();
    }
    quietTap(0, 0);  // back to the deck list...
    if (toybox.onButton(SideBtn::Down) || toybox.onButton(SideBtn::Up)) {
      printf("BUTTON FAIL: a side button did something on the deck list\n");
      abort();
    }
    toybox.goHub();
    if (toybox.onButton(SideBtn::Down)) {
      printf("BUTTON FAIL: a side button did something on the hub\n");
      abort();
    }
    // ...and back to where the walk was, so the screens below are unchanged.
    // Opening the tool raises its rules card again -- dismissed, not
    // suppressed, is the state the walk left it in.
    toybox.open(false, 5);
    tapRect(help::OK_BTN);
    tapRect(fcui::rowRect(0, 3));
    g_dumpEnabled = true;
    printf("side buttons ok (grade a card, and nothing anywhere else)\n");
  }

  setScreen("tool_flash_import");
  g_dumpEnabled = false;
  quietTap(0, 0);  // back to the deck list
  g_dumpEnabled = true;
  tapRect(fcui::IMPORT_BTN);

  // A phone joining the access point moves this screen on by itself, the way
  // the notes tool's pairing screen has since it was built. It did not, and the
  // difference was invisible to a screenshot: both screens are a QR code.
  setScreen("tool_flash_import_alt");
  {
    const int before = g_paintCount;
    toybox.tick();  // the stub's first poll: a phone is on the AP
    if (g_paintCount <= before) {
      printf("IMPORT FAIL: a phone joined and the screen did not move on\n");
      abort();
    }
  }

  setScreen("tool_flash_import_done");
  g_dumpEnabled = false;
  toybox.tick();
  g_dumpEnabled = true;
  toybox.tick();  // stub reports the deck arrived

  // Deleting the last deck is the only way to see an empty list: the sample
  // deck is recreated on enter(), not on every paint. It had never been drawn.
  setScreen("tool_flash_empty");
  g_dumpEnabled = false;
  tapRect(fcui::DONE_BTN);
  {
    using namespace fcard;
    DeckInfo d[MAX_DECKS];
    for (int n = listDecks(d, MAX_DECKS); n > 0; n = listDecks(d, MAX_DECKS))
      tapRect(fcui::delRect(0, n));
  }
  g_dumpEnabled = true;
  stickyHost.refresh(true);

  // --- battleship (solo) --------------------------------------------------
  setScreen("help_ships");
  toybox.open(false, 7);
  setScreen("tool_sea_menu");
  toybox.onTap(240, 653);  // GOT IT

  setScreen("tool_sea_setup");
  toybox.onTap(240, 200 + 46);  // PLAY THE DEVICE

  setScreen("tool_sea_play");
  g_dumpEnabled = false;
  toybox.onTap(250 + 95, 552 + 34);  // READY
  // Fire a diagonal so the board shows a mix of hits and misses.
  for (int k = 0; k < 6; k++) {
    const int cx = 40 + k * 50 + 25, cy = 100 + k * 50 + 25;
    toybox.onTap(cx, cy);          // select
    toybox.onTap(240, 512 + 30);   // FIRE
  }
  toybox.onTap(40 + 25, 100 + 3 * 50 + 25);  // leave one square selected
  g_dumpEnabled = true;
  toybox.onTap(40 + 2 * 50 + 25, 100 + 5 * 50 + 25);

  // Wreckage: sink one enemy ship outright and show what that looks like.
  setScreen("tool_sea_sunk");
  static_cast<SeaTool*>(toybox.hostActive())->hostSinkShip(0);
  stickyHost.refresh(false);

  // Fire at every square in turn; the game ends one way or the other, and the
  // end screen is what we came for.
  setScreen("tool_sea_over");
  g_dumpEnabled = false;
  for (int k = 0; k < sea::CELLS; k++) {
    toybox.onTap(40 + sea::xOf(k) * 50 + 25, 100 + sea::yOf(k) * 50 + 25);
    toybox.onTap(240, 512 + 30);
    // Halfway down the board something has always gone under by now: catch the
    // wreckage marks before the game finishes.
  }
  g_dumpEnabled = true;
  stickyHost.refresh(true);

  setScreen("tool_sea_pair");
  g_dumpEnabled = false;
  toybox.open(false, 7);
  tapRect(help::OK_BTN);     // reopening brings the rules card back
  tapRect(seaui::DUEL_BTN);  // TWO DEVICES
  g_dumpEnabled = true;
  tapRect(seaui::HOST_BTN);  // HOST A GAME

  // --- sudoku -------------------------------------------------------------
  // Which cells are givens depends on the generated puzzle, so rather than
  // guess coordinates, sweep a row and a column: taps on givens are refused and
  // whichever empty cell was touched last stays selected.
  setScreen("help_sudoku");
  toybox.open(false, 8);
  g_dumpEnabled = false;
  toybox.onTap(240, 653);  // GOT IT
  auto sudCell = [](int r, int c) { toybox.onTap(24 + c * 48 + 24, 80 + r * 48 + 24); };
  auto sudKey = [](int i) { toybox.onTap(10 + (i % 5) * 94 + 42, (i < 5 ? 524 : 602) + 34); };
  for (int c = 0; c < 9; c++) {  // some of these land, some are refused
    sudCell(0, c);
    sudKey(c % 9);
  }
  for (int r = 1; r < 5; r++) {
    sudCell(r, 4);
    sudKey((r + 2) % 9);
  }
  for (int r = 0; r < 9; r++) sudCell(r, 2);  // leave one empty cell selected
  g_dumpEnabled = true;
  setScreen("tool_sudoku");
  sudKey(4);  // enter a digit into it

  setScreen("tool_sudoku_solved");
  static_cast<SudokuTool*>(toybox.hostActive())->hostSolve();
  stickyHost.refresh(true);

  // --- notes -------------------------------------------------------------
  {
    static const char kNote[] =
        "# Shopping\n"
        "- [ ] milk, the **big** carton\n"
        "- [x] bread\n"
        "- [ ] eggs\n"
        "- [ ] coffee beans\n"
        "\n"
        "## Before Friday\n"
        "1. book the dentist\n"
        "2. return the library books\n"
        "\n"
        "---\n"
        "> back Tuesday, feed the cat\n";
    note::save("shopping", kNote, strlen(kNote));
    static const char kNote2[] = "# Wifi\n- [ ] call the engineer\n";
    note::save("house", kNote2, strlen(kNote2));
  }

  setScreen("tool_note_list");
  toybox.open(false, 6);

  setScreen("tool_note_view");
  toybox.onTap(20 + 180, 56 + 46 + 20);  // open the richer note

  // Aim at the lines the tool itself laid out rather than at remembered
  // coordinates: the bands move with the text metrics, and this same guard runs
  // against CrossPoint's much taller faces.
  NoteTool* nt = static_cast<NoteTool*>(toybox.hostActive());
  const int checkY = nt->hostLineY(nmd::Check);
  const int numY = nt->hostLineY(nmd::Numbered);

  setScreen("tool_note_ticked");
  toybox.onTap(60, checkY);  // tap a checkbox row: it ticks and strikes through

  // Lines that are not checkboxes cross out instead, and it is the note's own
  // Markdown that changes -- check the ~~ actually landed in the file.
  setScreen("tool_note_crossed");
  toybox.onTap(60, numY);  // a numbered line under "Before Friday"
  {
    String body;
    note::load("shopping", body);
    if (!strstr(body.c_str(), "~~")) {
      printf("NOTE FAIL: crossing a plain line did not reach the text\n");
      abort();
    }
    // ...and tapping it again takes the marks back out.
    g_dumpEnabled = false;
    toybox.onTap(60, numY);
    note::load("shopping", body);
    if (strstr(body.c_str(), "~~")) {
      printf("NOTE FAIL: the crossing would not come off again\n");
      abort();
    }
    toybox.onTap(60, numY);  // leave it struck for the screenshots below
    g_dumpEnabled = true;
    printf("note taps ok (ticks, crosses, uncrosses, all in the markdown)\n");
  }

  // The text size cycles in place -- normal, large, largest, normal -- and is
  // remembered. The overflow detector runs on the two big dumps, which is the
  // guard that matters: largest is 40 px in a 440 px column, the layout most
  // likely to push a long word off the glass.
  {
    setScreen("tool_note_large");
    toybox.onTap(nui::SIZE_BTN.x + 10, nui::SIZE_BTN.y + 10);
    if (nmd::g_body != TS_LARGE || stickyHost.prefs().getUInt("nt_size", 99) != 1) {
      printf("NOTE FAIL: one tap on the size button did not land on large\n");
      abort();
    }
    setScreen("tool_note_largest");
    toybox.onTap(nui::SIZE_BTN.x + 10, nui::SIZE_BTN.y + 10);
    g_dumpEnabled = false;
    toybox.onTap(nui::SIZE_BTN.x + 10, nui::SIZE_BTN.y + 10);
    if (nmd::g_body != TS_MED || stickyHost.prefs().getUInt("nt_size", 99) != 0) {
      printf("NOTE FAIL: three taps did not come back to normal\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("note size ok (three steps, remembered, back to start)\n");
  }

  setScreen("tool_note_pair");
  toybox.onTap(190 + 75, 716 + 25);  // EDIT -> pairing screen

  // A note arriving raises the "what should happen to it" prompt.
  // A phone joining the access point moves the screen on by itself: step one is
  // a picture of a thing already done. The device can see this, so it does not
  // ask -- there used to be a button reading PAGE DIDN'T OPEN? under a QR code
  // that only ever joins wifi, which promised something the code does not do.
  setScreen("tool_note_pair_joined");
  toybox.tick();
  if (strstr(toybox.activeTitle(), "NOTES") == nullptr) {
    printf("PAIR FAIL: the tool left its pairing screen\n");
    abort();
  }
  {
    // The step-two screen has to be a different picture from step one, or the
    // advance happened in a variable and nowhere else.
    int ink = 0;
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) ink += __builtin_popcount((uint8_t)~epd.fb()[i]);
    if (ink < 2000) {
      printf("PAIR FAIL: step two drew almost nothing (%d px)\n", ink);
      abort();
    }
    printf("pairing ok (advances by itself when a phone joins)\n");
  }

  setScreen("tool_note_prompt");
  g_dumpEnabled = false;
  toybox.tick();
  g_dumpEnabled = true;
  toybox.tick();

  // SHOW ON SCREEN now goes through the which-way-up step rather than straight
  // to the confirmation. The note is drawn edge to edge at the angle being
  // tried, so what you are looking at is the thing you are choosing.
  setScreen("tool_note_orient");
  tapRect(nui::PINNOW_BTN);

  {
    // TURN has to move the canvas, and the buttons have to follow it into
    // landscape -- they are placed against width() and height(), which swap.
    // A step whose own way out was off the panel at two of the four angles
    // would be a trap you could only escape by pulling the power.
    if (stickyHost.canvasRotation() != 0) {
      printf("ORIENT FAIL: opened at rotation %d, not 0\n", stickyHost.canvasRotation());
      abort();
    }
    for (int r = 1; r <= 3; r++) {
      const int w = stickyHost.canvas().width(), h = stickyHost.canvas().height();
      tapRect(nui::turnRect(w, h));
      if (stickyHost.canvasRotation() != r) {
        printf("ORIENT FAIL: TURN went from %d to %d\n", r - 1, stickyHost.canvasRotation());
        abort();
      }
    }
    setScreen("tool_note_orient_turned");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();

    // Keeping it saves the angle and puts the panel back to portrait, because
    // every other screen in the firmware is drawn portrait.
    const int w = stickyHost.canvas().width(), h = stickyHost.canvas().height();
    tapRect(nui::keepRect(w, h));
    if (stickyHost.canvasRotation() != 0) {
      printf("ORIENT FAIL: left the panel at rotation %d\n", stickyHost.canvasRotation());
      abort();
    }
    if (lock::config().pinRotation != 3) {
      printf("ORIENT FAIL: saved %d, chose 3\n", lock::config().pinRotation);
      abort();
    }
    // ...and that is the angle the sleeping note is drawn at, once the note is
    // no longer following the device.
    lock::Config cfg = lock::config();
    cfg.autoRotate = false;
    lock::setConfig(cfg);
    if (lock::restRotation(lock::config(), true, 1) != 3) {
      printf("ORIENT FAIL: a note that does not follow the device ignored its angle\n");
      abort();
    }
    cfg.autoRotate = true;
    lock::setConfig(cfg);
    if (lock::restRotation(lock::config(), true, 1) != 1) {
      printf("ORIENT FAIL: a note that follows the device ignored the device\n");
      abort();
    }
    lock::setPinRotation(prefs, 0);
    printf("orient ok (turns, saves, and rests at what it saved)\n");
  }

  // The same step on a device WITH an accelerometer: the angle follows the
  // hand, not a button. The button-path walk above is the fallback for boards
  // whose IMU never answered.
  {
    g_dumpEnabled = false;
    sensors::hostSetImu(true);
    sensors::hostSetOrientation(1);
    char wasPinned[note::NAME_LEN + 1];
    const bool hadPin = note::getPinned(wasPinned);
    // The last guard left the tool on the pairing screen; walk to a note's
    // view and pin from there, which is the other door into this step.
    toybox.onTap(nui::DONE_BTN.x + 10, nui::DONE_BTN.y + 10);  // out of pairing
    toybox.onTap(20 + 180, 56 + 46 + 20);                      // open the richer note
    NoteTool* ntool = static_cast<NoteTool*>(toybox.hostActive());
    toybox.onTap(nui::PIN_BTN.x + 10, nui::PIN_BTN.y + 10);  // PIN -- or UNPIN if it was pinned
    if (stickyHost.canvasRotation() != 1)
      toybox.onTap(nui::PIN_BTN.x + 10, nui::PIN_BTN.y + 10);  // then PIN
    if (stickyHost.canvasRotation() != 1) {
      printf("ORIENT IMU FAIL: opened at %d, the device is held at 1\n",
             stickyHost.canvasRotation());
      abort();
    }
    if (!ntool->wantsTick()) {
      printf("ORIENT IMU FAIL: the step is not asking for ticks\n");
      abort();
    }
    // Turning the device turns the note -- through the same tick the shell
    // gives every app, throttled inside the tool.
    sensors::hostSetOrientation(2);
    for (int i = 0; i < 10; i++) toybox.tick();
    if (stickyHost.canvasRotation() != 2) {
      printf("ORIENT IMU FAIL: the note did not follow the device to 2\n");
      abort();
    }
    // The size button rides this screen too: three taps cycle back to where
    // they started, and none of them disturb the angle being chosen.
    for (int i = 0; i < 3; i++) {
      const int w = stickyHost.canvas().width(), h = stickyHost.canvas().height();
      tapRect(nui::orientSizeRect(w, h));
    }
    if (stickyHost.prefs().getUInt("nt_size", 99) != 0 || stickyHost.canvasRotation() != 2) {
      printf("ORIENT IMU FAIL: the size button cycled to %u and rotation %d\n",
             stickyHost.prefs().getUInt("nt_size", 99), stickyHost.canvasRotation());
      abort();
    }
    // The TURN button is gone: a tap where it was does nothing.
    {
      const int w = stickyHost.canvas().width(), h = stickyHost.canvas().height();
      tapRect(nui::turnRect(w, h));
      if (stickyHost.canvasRotation() != 2) {
        printf("ORIENT IMU FAIL: the retired TURN button still turns\n");
        abort();
      }
      // The confirmation stays a button: KEEP saves the followed angle.
      tapRect(nui::keepRect(w, h));
    }
    if (lock::config().pinRotation != 2 || stickyHost.canvasRotation() != 0) {
      printf("ORIENT IMU FAIL: kept %d at rotation %d\n", lock::config().pinRotation,
             stickyHost.canvasRotation());
      abort();
    }
    lock::setPinRotation(prefs, 0);
    sensors::hostSetImu(false);
    sensors::hostSetOrientation(0);
    // Put the world back: the walk pinned this note to open the step, and the
    // guards below are about whatever was pinned before it.
    if (hadPin)
      note::setPinned(wasPinned);
    else
      note::setPinned("");
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // out of the view, back to the list
    g_dumpEnabled = true;
    printf("orient imu ok (follows the hand, TURN retired, KEEP still decides)\n");
  }

  setScreen("tool_note_pinned");
  epd.clear();
  toybox.render(stickyHost.sharedCanvas());
  epd.displayFull();

  // A word with no spaces in it, longer than any line -- a URL, a key, a fist
  // on the phone keyboard -- must break by codepoint rather than walk off the
  // glass. Rendered at largest, which is where it walked furthest, and left
  // to the overflow detector, which is exactly the failure it exists to see.
  {
    char wasPinned[note::NAME_LEN + 1];
    const bool hadPin = note::getPinned(wasPinned);
    static const char kWall[] =
        "# Wall\nmdghyfghjughjuyghuyffui7trghu7yghiuuyy\n"
        "longwordwithoutanyspacesatallthatkeepsgoingandgoingandgoing\n";
    note::save("wall", kWall, strlen(kWall));
    note::setPinned("wall");
    nmd::setBody(TS_HUGE);
    setScreen("lockscreen_wall");
    epd.clear();
    if (!drawPinnedFullScreen(stickyHost.sharedCanvas())) {
      printf("NOTE FAIL: the wall note did not draw\n");
      abort();
    }
    epd.displayFull();
    nmd::setBody(TS_MED);
    if (hadPin)
      note::setPinned(wasPinned);
    else
      note::setPinned("");
    printf("long-word wrap ok (a spaceless wall stays on the glass at largest)\n");
  }

  // What the panel keeps once the device powers down.
  // Asleep: the note and nothing else. Awake: the same note, with the footer
  // saying what a finger and the button will do.
  // The panel with no note on it, which is what most devices show most of the
  // time. Both halves: with a clock set, and with an RTC that has never been.
  // A picture on the lock screen. The phone does the dithering; this stands in
  // for what it would send, so the blit and the file format are exercised.
  setScreen("lockscreen_picture");
  {
    static uint8_t img[lockimg::FILE_SIZE];
    img[0] = 'T'; img[1] = 'B'; img[2] = 'I'; img[3] = '1';
    img[4] = lockimg::W & 255; img[5] = lockimg::W >> 8;
    img[6] = lockimg::H & 255; img[7] = lockimg::H >> 8;
    // An ordered-dither sphere: enough grey to prove the blit draws tone rather
    // than blocks, and cheap enough to write here.
    static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6},
                                    {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int y = 0; y < lockimg::H; y++) {
      for (int x = 0; x < lockimg::W; x++) {
        const float dx = (x - 240) / 210.0f, dy = (y - 400) / 210.0f;
        const float r2 = dx * dx + dy * dy;
        float v = r2 > 1.0f ? 0.92f : 0.15f + 0.8f * (1.0f - r2) * (1.0f - r2);
        if (r2 > 1.0f && ((x / 40 + y / 40) & 1)) v = 0.80f;
        const int level = (int)(v * 16.0f);
        if (level > bayer[y & 3][x & 3])
          img[tbimg::HEADER + (size_t)y * tbimg::STRIDE + (x >> 3)] |= (0x80 >> (x & 7));
      }
    }
    tfs::write(lockimg::PATH, (const char*)img, sizeof(img));
    epd.clear();
    if (!lockimg::draw(stickyHost.sharedCanvas())) {
      printf("PICTURE FAIL: a stored lock screen picture did not draw\n");
      abort();
    }
    epd.displayFull();

    // The settings row's thumbnail, with something in it. Drawn here rather
    // than beside the other settings screens because that is where a picture
    // exists to draw -- and a thumbnail that renders as an empty frame when a
    // picture is stored would otherwise look exactly like no picture at all.
    g_dumpEnabled = false;  // the walk in refreshes on every step
    toybox.goHub();
    toybox.openSettings();
    tapRect(setui::actionRect(setui::ACT_LOCK));  // into the lock page
    g_dumpEnabled = true;
    setScreen("settings_lock_picture");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();

    // The fourth thing an empty panel can show: the cover of the book being
    // read. We are already standing on the lock page, and the readers above
    // have left a real decoded cover on the fake card, so the whole path is
    // exercisable from here -- chip, copy, and the picture it produces.
    if (bthumb::haveLock()) {
      printf("COVER LOCK FAIL: a cover was stashed before anyone asked for one\n");
      abort();
    }
    // Nothing is copied while the setting is something else: 48 KB of flash on
    // every book open would be a cost paid by people who never chose this.
    if (bthumb::noteForLock(stickyHost, "/books/wind.epub")) {
      printf("COVER LOCK FAIL: a cover was copied with the setting off\n");
      abort();
    }
    g_dumpEnabled = false;
    tapRect(setui::chipRect(lock::EMPTY_COVER - lock::EMPTY_FIRST));
    if (lock::config().empty != lock::EMPTY_COVER) {
      printf("COVER LOCK FAIL: the chip did not select the cover\n");
      abort();
    }
    // Choosing it takes the copy there and then, rather than leaving the panel
    // unchanged until the next book is opened.
    if (!bthumb::haveLock()) {
      printf("COVER LOCK FAIL: choosing the chip did not take a copy\n");
      abort();
    }
    if (tfs::size(bthumb::LOCK_PATH) != tbimg::FILE_SIZE) {
      printf("COVER LOCK FAIL: the copy is %d bytes, not a picture\n",
             (int)tfs::size(bthumb::LOCK_PATH));
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_lock_cover");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();

    // And what the sleeping panel then shows. main.cpp's power-off path is not
    // in this build, so the draw is called the way it calls it.
    setScreen("lockscreen_cover");
    epd.clear();
    if (!tbimg::draw(stickyHost.sharedCanvas(), bthumb::LOCK_PATH)) {
      printf("COVER LOCK FAIL: the stashed cover did not draw\n");
      abort();
    }
    epd.displayFull();
    g_dumpEnabled = false;

    // With the setting on, a book open copies its own cover over the top...
    if (!bthumb::noteForLock(stickyHost, "/books/wind.epub")) {
      printf("COVER LOCK FAIL: an open did not refresh the cover\n");
      abort();
    }
    // ...and a book whose cover was never built leaves the old one alone
    // rather than writing a broken file.
    if (bthumb::stashForLock(stickyHost, "/books/no-such-book.epub")) {
      printf("COVER LOCK FAIL: a book with no cover produced one\n");
      abort();
    }
    if (!bthumb::haveLock()) {
      printf("COVER LOCK FAIL: a failed copy destroyed the good one\n");
      abort();
    }
    // Put the setting back so the screens below are the ordinary ones.
    tapRect(setui::chipRect(lock::EMPTY_GOODBYE - lock::EMPTY_FIRST));
    g_dumpEnabled = true;
    printf("cover lock screen ok (opt-in, copied on choosing, survives a bad book)\n");

    // The same bits as the home wallpaper, drawn under the dock and the
    // overlays: the one screen where the halo treatment meets a real picture.
    tfs::write(wallimg::PATH, (const char*)img, sizeof(img));
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    setScreen("hub_wallpaper");
    epd.clear();
    toybox.render(stickyHost.sharedCanvas());
    epd.displayFull();
    tfs::remove(wallimg::PATH);

    // ...and a file the wrong length has to be refused rather than drawn as
    // half a picture and half whatever was in memory.
    tfs::write(lockimg::PATH, (const char*)img, 1000);
    if (lockimg::have() || lockimg::draw(stickyHost.sharedCanvas())) {
      printf("PICTURE FAIL: a truncated picture was accepted\n");
      abort();
    }
    tfs::remove(lockimg::PATH);
  }

  setScreen("lockscreen");
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas());
  epd.displayFull();

  setScreen("pinned_live");
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
  epd.displayFull();

  // A tap on the woken note edits it in place, with no app open at all.
  {
    String before, after;
    note::load("shopping", before);
    if (!tapPinnedFullScreen(stickyHost.sharedCanvas(), 60, 100)) {
      printf("PINNED FAIL: a tap on the woken note did nothing\n");
      abort();
    }
    note::load("shopping", after);
    if (strcmp(before.c_str(), after.c_str()) == 0) {
      printf("PINNED FAIL: the note came back unchanged\n");
      abort();
    }
    setScreen("pinned_ticked");
    epd.clear();
    drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
    epd.displayFull();

    // The two buttons on the woken note must not be inside the text. A tap
    // meant for UNPIN that ticks a line instead is worse than a tap that does
    // nothing, because it edits the note on its way past.
    for (const TRect& b : {PINNED_HUB, PINNED_UNPIN}) {
      String was;
      note::load("shopping", was);
      tapPinnedFullScreen(stickyHost.sharedCanvas(), b.x + b.w / 2, b.y + b.h / 2);
      String now;
      note::load("shopping", now);
      if (strcmp(was.c_str(), now.c_str()) != 0) {
        printf("PINNED FAIL: a button on the woken note edited the note under it\n");
        abort();
      }
    }
    if (PINNED_HUB.hit(PINNED_UNPIN.x + 1, PINNED_UNPIN.y + 1)) {
      printf("PINNED FAIL: HUB and UNPIN overlap\n");
      abort();
    }
    // ...and neither does the hint text beside them. Two things drawn over each
    // other are legible as neither, and nothing about the render says so.
    {
      ToolsCanvas& c = stickyHost.sharedCanvas();
      const int right = PINNED_UNPIN.x + PINNED_UNPIN.w;
      for (const char* t : {"tap a line to tick", "power puts it back"}) {
        const int left = c.width() - 34 - c.textWidth(t, TS_SMALL);
        if (left < right + 8) {
          printf("PINNED FAIL: \"%s\" starts at %d, over a button ending at %d\n", t, left,
                 right);
          abort();
        }
      }
    }
    printf("pinned screen ok (wakes to the note, taps edit it, buttons do not)\n");
  }

  g_dumpEnabled = false;
  tapRect(nui::DONE_BTN);  // DONE -> back to the list
  setScreen("tool_note_list_pinned");
  g_dumpEnabled = true;
  stickyHost.refresh(false);


  // Tools are heap-allocated on select and freed on release, so open/close every
  // one of them several times over: under -fsanitize=address this is what catches
  // a use-after-free or a double free in that path. Imports and the power-off
  // renderer allocate too, so exercise those as well.
  g_dumpEnabled = false;
  for (int round = 0; round < 3; round++) {
    for (int t = 0; t < 9; t++) {
      toybox.open(false, t);
      toybox.tick();
      toybox.onTap(240, 400);
      toybox.goHub();
    }
    char saved[fcard::NAME_LEN + 1];
    fcard::importDeck("stress", "a\tb\nc\td\ne\tf\n", saved);
    drawPinnedFullScreen(stickyHost.sharedCanvas());
  }
  toybox.goHub();
  toybox.goHub();  // going home twice must be harmless
  g_dumpEnabled = true;
  printf("tool lifecycle ok (%d opens)\n", 3 * 9);

  // The corner buttons are drawn inside a 40px bar but answer to 50, and the
  // extra ten pixels are the whole point of the change -- a guard that only
  // tapped the middle of the bar would pass with them removed.
  g_dumpEnabled = false;
  for (int t = 0; t < 9; t++) {
    for (const int y : {2, TOPBAR_H - 1, TOPBAR_H, BAR_TOUCH_H - 1}) {
      toybox.open(false, t);
      toybox.onTap(BACK_W / 2, y);
      if (toybox.hostInApp()) {
        printf("BACK FAIL: tool %d ignored a tap at y=%d\n", t, y);
        abort();
      }
    }
    // ...and the row below it still belongs to the tool.
    toybox.open(false, t);
    toybox.onTap(BACK_W / 2, BAR_TOUCH_H);
    if (!toybox.hostInApp()) {
      printf("BACK FAIL: tool %d lost the row under the bar (y=%d)\n", t, BAR_TOUCH_H);
      abort();
    }
    toybox.goHub();
  }

  // ...and the rules card no longer traps you inside a game. It used to sit in
  // front of "< HUB", so the only way out of a game you had just opened was to
  // find and press a button on the card first.
  for (int g = 0; g < 4; g++) {
    static const char* kKeys[4] = {"h_wrd", "h_non", "h_g20", "h_xo"};
    prefs.remove(kKeys[g]);  // put the card back up
    toybox.open(true, g);
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    if (toybox.hostInApp()) {
      printf("BACK FAIL: game %d's rules card swallowed the back tap\n", g);
      abort();
    }
    help::suppress(prefs, kKeys[g] + 2);  // leave it as the guards below expect
  }
  g_dumpEnabled = true;
  printf("back button ok (below the bar on 9 tools, past the rules card on 4 games)\n");

  // The rules card is only safe to dismiss for ever because the "?" brings it
  // back. Check the whole loop: it blocks the game underneath, it stays gone
  // once refused, and the "?" raises it again.
  {
    g_dumpEnabled = false;
    const int KEY_X = 2 + 22, KEY_Y = 628 + 26;   // a letter on Wordle's keyboard
    const int OK_X = 240, OK_Y = 653;             // GOT IT
    const int NEVER_X = 240, NEVER_Y = 730;       // DON'T SHOW AGAIN
    const int HELP_X = EPD_W - 28, HELP_Y = 20;   // the ? in the top bar

    prefs.putBool("h_wrd", false);
    toybox.open(true, 0);
    int before = g_paintCount;
    toybox.onTap(KEY_X, KEY_Y);
    if (g_paintCount != before) {
      printf("HELP FAIL: a game tap got through the card\n");
      abort();
    }
    toybox.onTap(OK_X, OK_Y);
    before = g_paintCount;
    toybox.onTap(KEY_X, KEY_Y);
    if (g_paintCount == before) {
      printf("HELP FAIL: GOT IT did not release the game\n");
      abort();
    }
    // GOT IT is for this visit only.
    toybox.open(true, 0);
    before = g_paintCount;
    toybox.onTap(KEY_X, KEY_Y);
    if (g_paintCount != before) {
      printf("HELP FAIL: GOT IT silently became permanent\n");
      abort();
    }
    toybox.onTap(NEVER_X, NEVER_Y);
    if (!help::suppressed(prefs, "wrd")) {
      printf("HELP FAIL: DON'T SHOW AGAIN was not recorded\n");
      abort();
    }
    toybox.open(true, 0);
    before = g_paintCount;
    toybox.onTap(KEY_X, KEY_Y);
    if (g_paintCount == before) {
      printf("HELP FAIL: the card came back after being refused\n");
      abort();
    }
    // ...and the ? is the way back in.
    toybox.onTap(HELP_X, HELP_Y);
    before = g_paintCount;
    toybox.onTap(KEY_X, KEY_Y);
    if (g_paintCount != before) {
      printf("HELP FAIL: ? did not raise the card\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("help gate ok (blocks, dismisses, persists, reopens)\n");
  }

  // Every app clears its own record from its own screen, and none of them does
  // it on one tap. XO stands in for the pattern: the arming tap must change
  // nothing, and walking away from an armed button must disarm it.
  {
    g_dumpEnabled = false;
    const int CX = EPD_W - 16 - 214 + 107, CY = 752 + 22;  // XO's CLEAR RECORD
    prefs.putBool("h_xo", true);
    prefs.putInt("x_foe", 1);  // a preference, not a score: must survive
    prefs.putInt("x_w", 3);
    prefs.putInt("x_l", 1);
    prefs.putInt("x_d", 2);

    toybox.open(true, 3);
    toybox.onTap(CX, CY);  // arms
    if (prefs.getInt("x_w", -1) != 3) {
      printf("RECORD FAIL: one tap cleared XO\n");
      abort();
    }
    toybox.onTap(240, 66);  // somewhere else: disarms
    toybox.onTap(CX, CY);   // arms again, does not confirm the stale one
    if (prefs.getInt("x_w", -1) != 3) {
      printf("RECORD FAIL: an armed clear survived a tap elsewhere\n");
      abort();
    }
    toybox.onTap(CX, CY);  // confirms
    if (prefs.getInt("x_w", -1) != -1 || prefs.getInt("x_strk", -1) != -1) {
      printf("RECORD FAIL: XO record outlived its clear\n");
      abort();
    }
    // ...and it took nothing else with it.
    if (prefs.getInt("x_foe", -1) == -1 || prefs.getUInt("c_heads", 0) == 0) {
      printf("RECORD FAIL: clearing XO reached beyond XO\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("record clears ok (two taps, disarms, app-local)\n");
  }

  // --- importing from the card ---------------------------------------------
  // A note and a deck can arrive on the SD card instead of over wifi: /notes
  // holds .md or .txt, /decks holds .tsv, .csv or .txt. The editor on the
  // project's own page writes exactly these two shapes, and so does a text
  // editor, a phone, or a spreadsheet.
  {
    // Planted beside two files that must NOT be offered: a picture, and a note
    // one folder down. The lister takes four extensions and no subfolders, and
    // both rules are easier to break than to notice.
    static const char kCardNote[] =
        "# Picnic\n"
        "- [ ] blanket\n"
        "- [x] flask\n"
        "- [ ] \xe0\xb8\x82\xe0\xb9\x89\xe0\xb8\xb2\xe0\xb8\xa7\n"  // Thai: rice
        "\n"
        "> written on a laptop, carried on the card\n";
    sdcard::hostPutCardFile("/notes/picnic.md", kCardNote, (int)strlen(kCardNote));
    sdcard::hostPutCardFile("/notes/holiday.png", "\x89PNG", 4);
    sdcard::hostPutCardFile("/notes/old/last-year.md", "# old\n", 6);

    note::Info shelf[note::MAX_NOTES];
    const int before = note::list(shelf, note::MAX_NOTES);
    if (before >= note::MAX_NOTES) {
      printf("CARD FAIL: the note shelf was already full before the import guard\n");
      abort();
    }

    g_dumpEnabled = false;
    toybox.open(false, 6);  // notes
    g_dumpEnabled = true;
    setScreen("tool_note_card");
    tapRect(nui::CARD_BTN);
    if (strcmp(toybox.activeTitle(), "FROM THE CARD") != 0) {
      printf("CARD FAIL: CARD did not open the card list (\"%s\")\n", toybox.activeTitle());
      abort();
    }

    // Row 0 is the only row: if the picture or the subfolder had been listed,
    // the map's own order ("/notes/holiday.png" first) would put one of them
    // here and the import below would fail on its contents.
    setScreen("tool_note_card_done");
    tapRect(nui::cardRow(0));
    String got;
    if (!note::load("picnic", got)) {
      printf("CARD FAIL: tapping the file did not save a note called picnic\n");
      abort();
    }
    if (strcmp(got.c_str(), kCardNote) != 0) {
      printf("CARD FAIL: the note that arrived is not the file that was on the card\n");
      abort();
    }
    if (note::list(shelf, note::MAX_NOTES) != before + 1) {
      printf("CARD FAIL: the notes list did not gain exactly one note\n");
      abort();
    }
    // Importing the same file again replaces that note rather than adding a
    // second one -- this is how a note edited on a laptop comes back.
    g_dumpEnabled = false;
    tapRect(nui::cardRow(0));
    if (note::list(shelf, note::MAX_NOTES) != before + 1) {
      printf("CARD FAIL: re-importing the same file made a second note\n");
      abort();
    }
    tapRect(nui::CARD_DONE);
    if (strcmp(toybox.activeTitle(), "NOTES") != 0) {
      printf("CARD FAIL: DONE did not go back to the note list\n");
      abort();
    }
    quietTap(0, 0);  // out of notes
    g_dumpEnabled = true;
    printf("note card import ok (one .md in, picture and subfolder ignored)\n");
  }

  {
    // Decks: a comma file with a comma INSIDE a quoted answer, which is what a
    // spreadsheet exports and what the quote-aware splitter is for.
    static const char kCardDeck[] =
        "front,back\n"
        "\xe9\xa3\x9f\xe3\xb9\x99\xe3\x82\x8b,\"to eat, to have a meal\"\n"
        "\xe6\xb0\xb4,water\n"
        "\xe5\xb1\xb1,mountain\n";
    sdcard::hostPutCardFile("/decks/kanji n5.csv", kCardDeck, (int)strlen(kCardDeck));
    sdcard::hostPutCardFile("/decks/verbs.tsv", "aller\tto go\nfaire\tto do\n", 25);
    sdcard::hostPutCardFile("/decks/cover.bmp", "BM", 2);

    g_dumpEnabled = false;
    toybox.open(false, 5);  // flashcards
    tapRect(help::OK_BTN);
    g_dumpEnabled = true;
    setScreen("tool_flash_card");
    tapRect(fcui::CARD_BTN);
    if (strcmp(toybox.activeTitle(), "FROM THE CARD") != 0) {
      printf("CARD FAIL: CARD did not open the deck card list (\"%s\")\n",
             toybox.activeTitle());
      abort();
    }

    // The map orders "/decks/cover.bmp" first, so row 0 being the .csv is the
    // proof that the .bmp was filtered out.
    setScreen("tool_flash_card_done");
    tapRect(fcui::cardRow(0));
    {
      using namespace fcard;
      Card* deck = (Card*)malloc(sizeof(Card) * MAX_CARDS);
      const int n = loadDeck("kanji n5", deck, MAX_CARDS);
      if (n != 3) {
        printf("CARD FAIL: the deck came in with %d cards, not 3\n", n);
        abort();
      }
      // The header line is not a card, and the comma inside the quotes is not
      // a column break.
      if (strcmp(deck[0].back, "to eat, to have a meal") != 0) {
        printf("CARD FAIL: the quoted answer arrived as \"%s\"\n", deck[0].back);
        abort();
      }
      // Leitner progress survives a re-import of the same file, which is the
      // whole reason someone would edit a deck on a laptop and drop it back.
      deck[1].box = MAX_BOX;
      saveBoxes("kanji n5", deck, n);
      g_dumpEnabled = false;
      tapRect(fcui::cardRow(0));
      const int again = loadDeck("kanji n5", deck, MAX_CARDS);
      if (again != 3 || deck[1].box != MAX_BOX) {
        printf("CARD FAIL: re-import lost the box levels (%d cards, box %d)\n", again,
               again > 1 ? deck[1].box : -1);
        abort();
      }
      free(deck);
    }
    tapRect(fcui::CARD_DONE);
    if (strcmp(toybox.activeTitle(), "FLASHCARDS") != 0) {
      printf("CARD FAIL: DONE did not go back to the deck list\n");
      abort();
    }
    quietTap(0, 0);
    g_dumpEnabled = true;
    printf("deck card import ok (a spreadsheet .csv in, boxes kept on re-import)\n");
  }

  // --- languages beyond ASCII ---------------------------------------------
  // A note and a flashcard in the five content languages, drawn through the
  // real pipeline. The width checks catch a broken UTF-8 walk (a byte-wise
  // width triples every CJK string); the note render catches wrap regressions
  // (a spaceless paragraph used to be one unbreakable word).
  {
    const char* zh = "\xe4\xb9\xb0\xe7\x89\x9b\xe5\xa5\xb6";                     // 买牛奶
    const char* th = "\xe0\xb8\x8b\xe0\xb8\xb7\xe0\xb9\x89\xe0\xb8\xad\xe0\xb8\x99\xe0\xb8\xa1";  // ซื้อนม
    const char* ko = "\xec\x9a\xb0\xec\x9c\xa0 \xec\x82\xac\xea\xb8\xb0";        // 우유 사기
    for (const char* s : {zh, th, ko}) {
      const int w = gfx::textWidth(s, 24, false, 0);
      const int perCp = w * 3 / (int)strlen(s);  // 3 bytes per cp in all three
      if (perCp < 5 || perCp > 40) {  // Thai marks advance 0, dragging the mean down
        printf("INTL FAIL: \"%s\" measures %dpx -- the UTF-8 walk is off\n", s, w);
        abort();
      }
    }

    static const char kIntl[] =
        "# \xe0\xb8\xa3\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa3\n"  // รายการ
        "- [ ] \xe0\xb8\x99\xe0\xb8\xa1\xe0\xb8\x81\xe0\xb8\xa5\xe0\xb9\x88\xe0\xb8\xad\xe0\xb8\x87\xe0\xb9\x83\xe0\xb8\xab\xe0\xb8\x8d\xe0\xb9\x88\n"
        "- [ ] \xe4\xb9\xb0\xe7\x89\x9b\xe5\xa5\xb6\xe5\x92\x8c\xe9\xb8\xa1\xe8\x9b\x8b\xe8\xbf\x98\xe6\x9c\x89\xe5\x92\x96\xe5\x95\xa1\xe8\xb1\x86\xe4\xb8\x80\xe5\x8c\x85\n"
        "- [ ] \xec\x9a\xb0\xec\x9c\xa0\xec\x99\x80 \xea\xb3\x84\xeb\x9e\x80 \xec\x82\xac\xea\xb8\xb0\n"
        "- [ ] gi\xe1\xba\xb7t qu\xe1\xba\xa7n \xc3\xa1o\n"
        "\n"
        "\xe0\xb8\xa7\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\x99\xe0\xb8\xb5\xe0\xb9\x89\xe0\xb8\xad\xe0\xb8\xb2\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa8\xe0\xb8\x94\xe0\xb8\xb5\xe0\xb8\xa1\xe0\xb8\xb2\xe0\xb8\x81"
        "\xe0\xb9\x84\xe0\xb8\x9b\xe0\xb9\x80\xe0\xb8\x94\xe0\xb8\xb4\xe0\xb8\x99\xe0\xb9\x80\xe0\xb8\xa5\xe0\xb9\x88\xe0\xb8\x99\xe0\xb8\x81\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\x99\xe0\xb8\xb0\n";
    note::save("intl", kIntl, strlen(kIntl));

    setScreen("tool_note_intl");
    toybox.open(false, 6);
    // Open the intl note by its list row: the list is alphabetical, and
    // "intl" sits between "house" and "shopping".
    toybox.onTap(20 + 180, 56 + 46 + 20);
    NoteTool* nt = static_cast<NoteTool*>(toybox.hostActive());
    if (strcmp(toybox.activeTitle(), "intl") != 0) {
      printf("INTL FAIL: opened \"%s\" instead of the intl note\n", toybox.activeTitle());
      abort();
    }
    (void)nt;

    // The spaceless Thai sentence and the long Chinese line must have wrapped:
    // every drawn row has to start inside the body area, and the panel already
    // guards overflow on the right through the overflow log.

    // Flashcards: a Chinese front. Rebuild the sample deck with CJK content.
    {
      static const char kDeck[] =
          "\xe7\x8c\xab\tcat \xe0\xb9\x81\xe0\xb8\xa1\xe0\xb8\xa7\n"
          "\xe7\x89\x9b\xe5\xa5\xb6\tmilk\n";
      char savedName[fcard::NAME_LEN + 1];
      fcard::importDeck("hanzi", kDeck, savedName);
      g_dumpEnabled = false;
      quietTap(0, 0);  // out of notes, back to hub
      g_dumpEnabled = true;
      setScreen("tool_flash_intl_front");
      g_dumpEnabled = false;
      toybox.open(false, 5);
      tapRect(help::OK_BTN);  // every open raises the rules card
      // Decks are listed in filesystem order, so find "hanzi" by opening rows
      // until the title matches. rowRect knows how tall a row is at this count.
      {
        using namespace fcard;
        DeckInfo d[MAX_DECKS];
        const int n = listDecks(d, MAX_DECKS);
        for (int row = 0; row < n; row++) {
          tapRect(fcui::rowRect(row, n));
          if (strcmp(toybox.activeTitle(), "hanzi") == 0) break;
          toybox.onTap(0, 0);  // back to the deck list
        }
      }
      if (strcmp(toybox.activeTitle(), "hanzi") != 0) {
        printf("INTL FAIL: could not open the CJK deck\n");
        abort();
      }
      g_dumpEnabled = true;
      stickyHost.refresh(true);
      setScreen("tool_flash_intl_back");
      toybox.onTap(240, 300);  // flip
    }
    printf("intl ok (widths sane, note and deck render)\n");

    // Names survive in their own script now: a Thai-named note must keep its
    // name through sanitizeName and render its list row at the readable floor.
    {
      char clean[note::NAME_LEN + 1];
      note::sanitizeName("\xe0\xb8\xa3\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa3", clean);
      if (strstr(clean, "_")) {
        printf("NAME FAIL: a Thai name was sanitised to \"%s\"\n", clean);
        abort();
      }
      note::save(clean, "# \xe0\xb8\xa3\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa3\n- [ ] \xe0\xb8\x99\xe0\xb8\xa1\n", 40);
      g_dumpEnabled = false;
      quietTap(0, 0);
      g_dumpEnabled = true;
      setScreen("tool_note_list_thai");
      toybox.open(false, 6);
      printf("thai name ok (\"%s\" survives, floors in the list)\n", clean);
      g_dumpEnabled = false;
      quietTap(0, 0);
      g_dumpEnabled = true;
    }
  }

  // --- font packs -----------------------------------------------------------
  // Install the Korean full pack the way the device would see it (a .tfp in
  // /fonts) and check a syllable OUTSIDE KS X 1001 gains a real glyph:
  // before the pack it draws the missing-glyph box and measures like one.
  {
    const char* rare = "\xeb\xb7\x81";  // 뷁 -- famously not in KS X 1001
    const int before = gfx::textWidth(rare, 24, false, 0);

    FILE* f = fopen("/tmp/packs/ko_full.tfp", "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      const long n = ftell(f);
      fseek(f, 0, SEEK_SET);
      std::string blob((size_t)n, 0);
      fread(&blob[0], 1, (size_t)n, f);
      fclose(f);
      tfs::hostFs()["/fonts/ko_full.tfp"] = blob;

      const int faces = gfx::loadFontPacks();
      const int after = gfx::textWidth(rare, 24, false, 0);
      const int boxW = (24 * 3) / 5 + 4;  // what the missing-glyph box measures
      if (faces < 3 || before != boxW || after == before) {
        printf("PACK FAIL: faces=%d width %d -> %d (box %d)\n", faces, before, after, boxW);
        abort();
      }
      printf("font pack ok (%d faces; %s %dpx -> %dpx)\n", faces, rare, before, after);
    } else {
      printf("font pack SKIPPED (no /tmp/packs/ko_full.tfp -- run make_font_pack.py)\n");
    }
  }

  // --- accented Latin in the baked faces ------------------------------------
  // An e-acute inside 44 px text used to drop to the 32 px international face,
  // centred in the taller box -- "resume" read as a ransom note, and the
  // largest size (the one the owner reads at) was the worst hit. The baked
  // DejaVu faces carry the accents at every size now. The detector is ink
  // position: E and E-acute must start their ink within a couple of rows of
  // each other at 44 px; the old fallback started six rows down before the
  // accent was even considered.
#ifndef TOYBOX_CP_FONTS
  // Skipped under CrossPoint's faces: that firmware really does fall back to
  // the international tables for accents, and this guard is about ours.
  {
    int l, r, tE, b, tEa;
    gfx::textInk("E", TS_HUGE, false, 0, l, r, tE, b);
    gfx::textInk("\xC3\x89", TS_HUGE, false, 0, l, r, tEa, b);  // É
    if (tEa > tE) {
      printf("FONT FAIL: 44 px E-acute starts its ink at row %d, E at %d -- fallback?\n",
             tEa, tE);
      abort();
    }
    // And the advance matches its unaccented sibling, which the intl face's
    // metrics never quite did.
    for (int sz = TS_SMALL; sz <= TS_HUGE; sz++) {
      const int we = gfx::textWidth("e", sz, false, 0);
      const int wa = gfx::textWidth("\xC3\xA9", sz, false, 0);  // é
      if (wa < we - 2 || wa > we + 2) {
        printf("FONT FAIL: size %d e is %d px wide, e-acute %d\n", sz, we, wa);
        abort();
      }
    }
    setScreen("fonts_accents");
    epd.clear();
    gfx::drawText(10, 40, "resume r\xC3\xA9sum\xC3\xA9", TS_HUGE, 0, false);
    gfx::drawText(10, 100, "\xC3\x89l\xC3\xA8ve na\xC3\xAFve gar\xC3\xA7on", TS_HUGE, 0, true);
    gfx::drawText(10, 170, "caf\xC3\xA9 \xC5\x92uvre \xC5\xA0kola \xC5\xBDivot s\xC3\xB8ster",
                  TS_LARGE, 0, false);
    gfx::drawText(10, 220, "\xC3\xBC\x62\x65r ni\xC3\xB1o fian\xC3\xA7\x61ille a\xC3\xA7\x61\xC3\xAD",
                  TS_MED, 0, false);
    epd.displayFull();
    printf("accents ok (44 px E-acute leads with its accent, widths match)\n");
  }
#endif

  // --- resume, rotation, battery -------------------------------------------
  // Three device-shaped behaviours that only the host can exercise cheaply.
  {
    // Resume: play a few moves, leave to the hub (which destroys the app),
    // come back, and the board has to be the one we left. The help gate test
    // above leaves the how-to-play card armed, and a card swallows the taps
    // this guard is trying to make.
    help::suppress(prefs, "g20");
    setScreen("g2048_resumed");  // this open() paints, so give the frame its own name
    toybox.open(true, 2);        // 2048
    g_dumpEnabled = false;
    toybox.onSwipe(-1, 0);
    toybox.onSwipe(0, -1);
    toybox.onSwipe(1, 0);
    const int scoreBefore = prefs.getInt("t_best", 0);
    uint8_t before[16];
    memcpy(before, static_cast<G2048App*>(toybox.hostActive())->hostBoard(), 16);
    toybox.goHub();
    toybox.open(true, 2);
    const uint8_t* after = static_cast<G2048App*>(toybox.hostActive())->hostBoard();
    if (memcmp(before, after, 16) != 0) {
      printf("RESUME FAIL: the 2048 board did not come back\n");
      abort();
    }
    // ...and NEW GAME still means new.
    toybox.onTap(40 + 90, 660 + 30);
    const uint8_t* fresh = static_cast<G2048App*>(toybox.hostActive())->hostBoard();
    int tiles = 0;
    for (int i = 0; i < 16; i++)
      if (fresh[i]) tiles++;
    if (tiles > 2) {
      printf("RESUME FAIL: NEW GAME resumed instead of starting over\n");
      abort();
    }
    toybox.goHub();
    g_dumpEnabled = true;
    (void)scoreBefore;
    printf("resume ok (2048 board survives a trip to the hub, NEW still resets)\n");
  }

  {
    // Auto-rotate: the pinned note is the one screen that turns. Render it in
    // each of the four orientations and confirm the panel actually fills --
    // a wrong transform clips to a corner or draws nothing at all.
    note::setPinned("shopping");
    for (int rot = 0; rot < 4; rot++) {
      epd.setRotation(rot);
      epd.clear();
      char name[8];
      snprintf(name, sizeof(name), "rot%d", rot);
      setScreen(name);
      if (!drawPinnedFullScreen(stickyHost.sharedCanvas(), true)) {
        printf("ROTATE FAIL: nothing drawn at rotation %d\n", rot);
        abort();
      }
      epd.displayFull();
      // Count ink: a broken transform lands most pixels outside the clip.
      int ink = 0;
      for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) ink += __builtin_popcount((uint8_t)~epd.fb()[i]);
      if (ink < 2000) {
        printf("ROTATE FAIL: rotation %d drew only %d px\n", rot, ink);
        abort();
      }
    }
    epd.setRotation(0);
    printf("rotate ok (pinned note fills the panel at all four angles)\n");
  }

  {
    // Filling the panel is not the same as facing the right way. Rotations 1
    // and 3 were transposed for the whole life of this project: both landscapes
    // came out 180 degrees from where they belonged, portrait was untouched,
    // and the guard above passed every time because ink is ink.
    //
    // What actually defines the rotations is where the content's up direction
    // ends up on the device. Rotation 1 means the device has been turned a
    // quarter turn anticlockwise, so the image turns a quarter clockwise to stay
    // upright -- and content-up then lies along the device's right-hand side,
    // which at rotation 0 is where content-right lies. That single equality
    // fixes the handedness; the rest follows by quarter turns.
    g_dumpEnabled = false;
    auto dir = [](int rot, bool wantUp) {
      // Panel-space direction of the content's up (or right) axis, as a unit
      // step. Two points through the real transform, so this measures the code
      // rather than restating it.
      epd.setRotation(rot);
      const int w = epd.logicalW(), h = epd.logicalH();
      auto panel = [&](int x, int y) {
        epd.clear(false);  // all black
        epd.drawPixel(x, y, 1);  // one white pixel
        for (int py = 0; py < PANEL_H; py++)
          for (int px = 0; px < PANEL_W; px++)
            if (epd.fb()[(uint32_t)py * EPD_WB + (px >> 3)] & (0x80 >> (px & 7)))
              return std::make_pair(px, py);
        return std::make_pair(-1, -1);
      };
      // "up" is from the bottom-left corner to the top-left; "right" is from
      // the top-left to the top-right.
      const auto a = wantUp ? panel(0, h - 1) : panel(0, 0);
      const auto b = wantUp ? panel(0, 0) : panel(w - 1, 0);
      const int dx = b.first - a.first, dy = b.second - a.second;
      return std::make_pair(dx == 0 ? 0 : (dx > 0 ? 1 : -1), dy == 0 ? 0 : (dy > 0 ? 1 : -1));
    };

    const auto deviceRight = dir(0, false);  // content-right at rotation 0
    const auto deviceUp = dir(0, true);      // content-up at rotation 0
    const std::pair<int, int> deviceLeft{-deviceRight.first, -deviceRight.second};
    const std::pair<int, int> deviceDown{-deviceUp.first, -deviceUp.second};

    struct { int rot; std::pair<int, int> want; const char* what; } expect[] = {
      {1, deviceRight, "the device's right (quarter turn cw)"},
      {2, deviceDown, "the device's bottom (half turn)"},
      {3, deviceLeft, "the device's left (quarter turn ccw)"},
    };
    for (const auto& e : expect) {
      const auto got = dir(e.rot, true);
      if (got != e.want) {
        printf("ROTATE FAIL: at rotation %d the content points to (%d,%d) in panel space,\n"
               "             but it has to point to %s, which is (%d,%d)\n",
               e.rot, got.first, got.second, e.what, e.want.first, e.want.second);
        abort();
      }
    }
    epd.setRotation(0);
    epd.clear();
    g_dumpEnabled = true;
    printf("rotate ok (each quarter turn faces the way its number says)\n");
  }

  // --- the welcome screen ---------------------------------------------------
  // Both halves: a device that has just been flashed for the first time, and
  // one that has just been updated. They differ by one line, and that line is
  // the difference between a greeting and amnesia.
  {
    setScreen("welcome");
    epd.setRotation(0);
    epd.clear();
    welcome::render(stickyHost.sharedCanvas(), false);
    epd.displayFull();

    setScreen("welcome_updated");
    epd.clear();
    welcome::render(stickyHost.sharedCanvas(), true);
    epd.displayFull();

    // The four GETTING STARTED cards that follow it. Rendered here because
    // they are the one place the dock marks are explained, and a card whose
    // text has drifted off the panel would teach nothing.
    for (int card = 0; card < tour::CARDS; card++) {
      char shot[16];
      snprintf(shot, sizeof(shot), "tour_%d", card + 1);
      setScreen(shot);
      epd.clear();
      tour::render(stickyHost.sharedCanvas(), card);
      epd.displayFull();
    }

    // It is shown when the stored version is not this one, and not shown after
    // it has been marked. A welcome that came back every boot would be the kind
    // of bug nobody reports and everybody resents.
    g_dumpEnabled = false;
    prefs.remove("welcome");
    if (!welcome::pending(prefs)) {
      printf("WELCOME FAIL: a device that has never seen it is not offered it\n");
      abort();
    }
    welcome::markSeen(prefs);
    if (welcome::pending(prefs)) {
      printf("WELCOME FAIL: it comes back after being seen\n");
      abort();
    }
    prefs.putString("welcome", "v0.0-something-else");
    if (!welcome::pending(prefs)) {
      printf("WELCOME FAIL: an updated device is not offered it\n");
      abort();
    }
    welcome::markSeen(prefs);
    g_dumpEnabled = true;
    printf("welcome ok (once per version, and only once)\n");
  }

  // --- the service screen ---------------------------------------------------
  // The one screen that has to work when the display or the touch mapping is
  // wrong, so it is drawn here like any other and its text is held to the same
  // width rule.
  {
    svc::Report r;
    r.panelOk = true;
    r.touchOk = true;
    r.touchAddr = 0x5D;
    r.gauge = r.rtc = r.sht = true;
    r.imu = false;
    r.battMv = 3987;
    r.fontFaces = 3;
    r.psramKb = 8192;
    // Real figures from the device, so the overflow guard sees the widest
    // line this block can produce rather than a comfortable one.
    r.heapKb = 215;
    r.blockKb = 151;
    r.psramFreeKb = 8000;
    r.version = "toybox  Aug  8 2026  11:04:22";
    const svc::Config cfg;

    setScreen("service");
    epd.setRotation(0);
    epd.clear();
    svc::render(stickyHost.sharedCanvas(), r, cfg, 0, false, 0, 0);
    epd.displayFull();

    setScreen("service_touch_test");
    epd.clear();
    svc::render(stickyHost.sharedCanvas(), r, cfg, svc::ROW_TEST, true, 300, 470);
    epd.displayFull();

    // The SD row, in the three states it has. Nothing on the device uses a card
    // yet; this screen exists to answer whether one can be used at all, and a
    // result nobody can read is not an answer.
    setScreen("service_patterns");
    epd.clear();
    svc::render(stickyHost.sharedCanvas(), r, cfg, svc::ROW_PATTERN, false, 0, 0);
    epd.displayFull();
    setScreen("service_sd");
    epd.clear();
    svc::render(stickyHost.sharedCanvas(), r, cfg, svc::ROW_SD, false, 0, 0);
    epd.displayFull();

    svc::Report sdr = r;
    sdr.sdTried = true;
    sdr.sdMounted = true;
    sdr.sdSizeMb = 15193;
    sdr.sdFiles = 12;
    sdr.sdKbPerSec = 480;
    sdr.sdPanelOk = true;
    setScreen("service_sd_ok");
    epd.clear();
    svc::render(stickyHost.sharedCanvas(), sdr, cfg, svc::ROW_SD, false, 0, 0);
    epd.displayFull();

    sdr.sdMounted = false;
    sdr.sdFailedAt = "mount";
    setScreen("service_sd_fail");
    epd.clear();
    svc::render(stickyHost.sharedCanvas(), sdr, cfg, svc::ROW_SD, false, 0, 0);
    epd.displayFull();
  }

  // The corrections that screen writes have to actually move pixels, one axis
  // each. A flip that quietly does nothing would look like a wrong panel.
  {
    auto black = [](int px, int py) {
      return (epd.fb()[(uint32_t)py * EPD_WB + (px >> 3)] & (0x80 >> (px & 7))) == 0;
    };
    g_dumpEnabled = false;
    epd.setRotation(0);
    for (int m = 0; m < 4; m++) {
      epd.setPanelFlip(m & 1, m & 2);
      epd.clear();
      epd.drawPixel(0, 0, 0);  // logical top-left, black
      int px = PANEL_W - 1, py = 0;  // where rotation 0 puts it, before flips
      if (m & 1) px = PANEL_W - 1 - px;
      if (m & 2) py = PANEL_H - 1 - py;
      if (!black(px, py)) {
        printf("FLIP FAIL: flipX=%d flipY=%d did not reach panel (%d,%d)\n", m & 1, (m & 2) >> 1,
               px, py);
        abort();
      }
    }
    epd.setPanelFlip(false, false);
    g_dumpEnabled = true;
    printf("panel flips ok (all four corners reachable from the service screen)\n");
  }

  // A QR code that is wrong is indistinguishable, to the eye, from one that is
  // right: both are a square of speckle. The vendored encoder used to draw a
  // symbol for any payload at any version and report success, so the pairing
  // code -- 37 bytes asked to fit version 2's 28 -- came out unreadable and
  // nobody could see it. This is the check that noticing does not depend on
  // someone standing in front of the panel with a phone.
  {
    g_dumpEnabled = false;
    static uint8_t qrbuf[420];
    QRCode qr;
    auto fits = [&](uint8_t v, const char* t) {
      return qrcode_initText(&qr, qrbuf, v, ECC_MEDIUM, t) == 0;
    };

    // The payloads the pairing screens actually draw, at their longest: the
    // SSID and key are fixed-width, so this is the wifi string byte for byte.
    struct { const char* what; const char* text; } cases[] = {
      {"wifi", "WIFI:T:WPA;S:TOYBOX-4F2A;P:58204617;;"},
      {"url", "http://192.168.4.1"},
    };

    for (const auto& c : cases) {
      const int drawn = fqr::draw(stickyHost.sharedCanvas(), nui::QR_X, nui::QR_Y,
                                  nui::QR_SIZE, c.text);
      if (drawn == 0) {
        printf("QR FAIL: the %s payload does not fit any version we will draw\n", c.what);
        abort();
      }
      // Which version the encoder settles on is checked against the spec rather
      // than against itself: byte-mode capacity at ECC level M, straight out of
      // the QR standard. Asking the library whether it agrees with the library
      // is how a broken one passes.
      static const int CAP_M[10] = {14, 26, 42, 62, 84, 106, 122, 152, 180, 213};
      const int len = (int)strlen(c.text);
      int want = 1;
      while (want <= 10 && CAP_M[want - 1] < len) want++;

      uint8_t v = 1;
      for (; v <= fqr::MAX_VERSION; v++)
        if (fits(v, c.text)) break;
      if (v != want) {
        printf("QR FAIL: %s is %d bytes -- needs v%d, encoder accepted v%d\n", c.what, len,
               want, v);
        abort();
      }
      // 5 px is 0.54 mm at 235 DPI. Below that a phone camera at arm's length
      // is reading noise, valid symbol or not.
      const int n = v * 4 + 17;
      const int scale = nui::QR_SIZE / (n + 8);
      if (scale < 5) {
        printf("QR FAIL: %s is v%d, %d px per module -- too fine to scan\n", c.what, v, scale);
        abort();
      }
      printf("qr %s ok (v%d, %d px per module, %.2f mm)\n", c.what, v, scale,
             scale * 25.4 / 235.0);
    }

    // And the refusal itself, directly: version 2 holds 28 bytes, so 37 must
    // come back as a failure rather than as a drawn square of nonsense. If this
    // line ever passes a re-vendored copy of the library, the check upstream
    // has been lost again.
    if (fits(2, cases[0].text)) {
      printf("QR FAIL: the encoder accepted 37 bytes into version 2\n");
      abort();
    }
    epd.clear();
    g_dumpEnabled = true;
  }

  screenPaintCheck();  // the last screen has nothing after it to catch it

  if (gfx::g_overflowCount) {
    printf("\n%d TEXT OVERFLOW(S) -- these are clipped at the panel edge:\n",
           gfx::g_overflowCount);
    for (int i = 0; i < gfx::g_overflowCount; i++)
      printf("  [%s] x=%d w=%d scale=%d  \"%s\"\n", gfx::g_overflowScreens[i],
             gfx::g_overflow[i].x, gfx::g_overflow[i].width, gfx::g_overflow[i].scale,
             gfx::g_overflow[i].text);
  } else {
    printf("no text overflows\n");
  }

#ifdef TOYBOX_CP_FONTS
  printf("%d guard%s failed under CrossPoint's fonts\n", g_softFails,
         g_softFails == 1 ? "" : "s");
#endif

  printf("done\n");
  return 0;
}

static void tapRect(const TRect& r) { toybox.onTap(r.x + r.w / 2, r.y + r.h / 2); }
