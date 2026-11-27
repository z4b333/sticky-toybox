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
#include <functional>

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
#include "tools/tool_calendar.h"
#include "tools/tool_focus.h"
#include "settings.h"
#include "home_style.h"

// The harness's stand-in for the firmware's ota::Device (src/ota.cpp): scripted
// answers, so every screen of Settings > Update & about can be drawn and every
// button's route checked without a radio.
struct FakeOta : ota::Device {
  ota::Err checkErr = ota::Err::Ok, installErr = ota::Err::Ok;
  ota::Check result;
  ota::Lan lanScript, lanNow;
  int installs = 0, restarts = 0, checks = 0;
  bool lanUp = false;
  char rolled[ota::VER_MAX] = {};
  const char* version() override { return "v2.0.0-beta.1-4-gab89111"; }
  const char* slot() override { return "ota_0"; }
  uint32_t slotBytes() override { return 0x2f0000; }
  ota::Err check(ota::Check& out) override {
    checks++;
    out = result;
    return checkErr;
  }
  ota::Err install(const ota::Check& c, ota::Progress p, void* ctx) override {
    installs++;
    if (p && c.needBase) p(ctx, 1, c.remote.base.bytes, c.remote.base.bytes);
    if (p && c.needApp) {
      p(ctx, 0, c.remote.app.bytes / 2, c.remote.app.bytes);
      p(ctx, 0, c.remote.app.bytes, c.remote.app.bytes);
    }
    return installErr;
  }
  void restart() override { restarts++; }
  bool lanStart() override {
    lanUp = true;
    lanNow = lanScript;
    return lanNow.phase != ota::Lan::Failed;
  }
  void lanLoop() override {}
  void lanStop() override {
    lanUp = false;
    lanNow.phase = ota::Lan::Off;
  }
  const ota::Lan& lan() const override { return lanNow; }
  bool takeRollbackNotice(char* v, size_t n) override {
    if (!rolled[0]) return false;
    snprintf(v, n, "%s", rolled);
    rolled[0] = 0;
    return true;
  }
  // FROM SD CARD: a scripted card.
  ota::Card cardScript;
  ota::Err cardErr = ota::Err::Ok;
  int cardScans = 0, cardInstalls = 0;
  void cardScan(ota::Card& out) override {
    cardScans++;
    out = cardScript;
  }
  ota::Err cardInstall(const ota::Card& c, ota::Progress p, void* ctx) override {
    cardInstalls++;
    for (int i = 1; i <= ota::CARD_FILES; i++) {
      const ota::CardFile& f = c.files[i % ota::CARD_FILES];
      if (f.found && p) p(ctx, (uint8_t)(i % ota::CARD_FILES), f.bytes / 2, f.bytes);
    }
    return cardErr;
  }
};
static FakeOta g_fakeOta;
#include "bmp_gray.h"
#include "cardfonts.h"
#include "tools/cpfont_prep.h"
#include "tools/thai_shape.h"
#include "tools/thai_words.h"
#include "tools/cpfont.h"
#include "tools/lock_image.h"
#include "tools/lockscreen.h"
#include "mines.h"
#include "tools/note_store.h"
#include "tools/tool_dice.h"
#include "tools/tool_coin.h"
#include "tools/bookmarks.h"
#include "tools/book_store.h"
#include "tools/epub/epub_art.h"
#include <string>
#include "tools/reader_menu.h"
#include "tools/tool_epub.h"
#include "tools/epub/epubcore.h"
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
#include "tools/tool_blackjack.h"
#include "tools/tool_code.h"
#include "tools/tool_fourrow.h"
#include "tools/tool_hanoi.h"
#include "tools/tool_hilo.h"
#include "tools/tool_memory.h"
#include "tools/tool_climate.h"
#include "tools/charge_dash.h"
#include "tools/tool_level.h"
#include "touch.h"
#include "tools/help.h"
#include "tools/decor.h"

// Where a book's position lives since v2: its own folder.
static const char* posOf(const char* book) {
  static char p[bstore::PATH_CAP];
  bstore::file(book, "position.bin", p, sizeof(p));
  return p;
}
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
static void tapAct(int act);

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
void Epd::dim(uint8_t level) {
  // A set bit is white, so this takes ink away and leaves the rest as it was.
  // The steps are how much survives: everything, three quarters, a half, a
  // quarter. Which one suits is taste -- a picture wants the page faint behind
  // it, a reader wants to still see what they were reading -- so the lock
  // screen makes it a setting rather than a decision taken here.
  if (!level) return;
  for (int y = 0; y < PANEL_H; y++) {
    uint8_t mask;
    switch (level) {
      case 1: mask = (y & 1) ? 0x22 : 0x88; break;   // a quarter of the ink goes
      case 2: mask = (y & 1) ? 0x55 : 0xAA; break;   // half of it
      default: mask = (y & 1) ? 0xFF : 0x55; break;  // three quarters
    }
    uint8_t* row = &_fb[(uint32_t)y * EPD_WB];
    for (int x = 0; x < EPD_WB; x++) row[x] |= mask;
  }
}
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
// The device's own fast path, line for line, so checkInkBits below tests the
// code that ships against the pixel-at-a-time draw it replaced.
void Epd::inkBits(const uint8_t* bits, int w, int h) {
  if (w > logicalW()) w = logicalW() & ~7;
  if (h > logicalH()) h = logicalH();
  const int stride = w / 8;
  int px0, py0, px1, py1, px2, py2;
  epdMapPixel(rotation(), panelFlipX(), panelFlipY(), 0, 0, px0, py0);
  epdMapPixel(rotation(), panelFlipX(), panelFlipY(), 1, 0, px1, py1);
  epdMapPixel(rotation(), panelFlipX(), panelFlipY(), 0, 1, px2, py2);
  const int dxX = px1 - px0, dyX = py1 - py0;
  const int dxY = px2 - px0, dyY = py2 - py0;
  for (int y = 0; y < h; y++) {
    const uint8_t* row = bits + (size_t)y * stride;
    int px = px0 + dxY * y, py = py0 + dyY * y;
    for (int xb = 0; xb < stride; xb++) {
      const uint8_t v = row[xb];
      if (v == 0xFF) {
        px += 8 * dxX;
        py += 8 * dyX;
        continue;
      }
      for (int k = 0; k < 8; k++) {
        if (!(v & (0x80 >> k))) _fb[(uint32_t)py * EPD_WB + (px >> 3)] &= ~(0x80 >> (px & 7));
        px += dxX;
        py += dyX;
      }
    }
  }
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
  // The loading screen is a partial now (the open ends in a full anyway), so
  // the first-paint watch has to see partials too.
  if (g_rotWatch) {
    *g_rotWatch = rotation();
    g_rotWatch = nullptr;
  }
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
void chime() {}
void chimeSlow() {}
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
  struct Grp { Want items[16]; int n; };
  const Grp ALL[3] = {
      // C5: PLAY is fourteen -- nine on the first page, five on the second
      // (Coin and Dice came over from UTILITY).
      {{{true, 0}, {true, 1}, {true, 2}, {true, 3}, {false, 7}, {false, 8}, {false, 14},
        {false, 15}, {false, 16}, {false, 17}, {false, 18}, {false, 19}, {false, 0},
        {false, 1}},
       14},
      {{{false, 3}, {false, 4}, {false, 2}, {false, 11}, {false, 12}, {false, 13}, {false, 20},
        {false, 21}},
       8},
      {{{false, 10}, {false, 5}, {false, 6}}, 3},  // 9 was the manga reader (removed in v2)
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
      printf("HUB ROUTING FAIL group %d item %d (%s) at (%d,%d) inApp %d game %d idx %d want %d\n", gi, ii, what, x, y, toybox.hostInApp(), toybox.hostIsGame(), toybox.hostIdx(), w.idx);
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

    // The drawer's grid (v2): three columns hanging from the bar, touch by
    // column slice and row -- hubui's cell helpers are the authority.
    // Nine tiles to a page; past that the header's ">" turns to the next
    // (hub.cpp fpageNextRect), and the drawer keeps its page across an app.
    constexpr int PER = 9;
    const int lastPageCells = cells - ((cells - 1) / PER) * PER;
    const int rows = (lastPageCells + hubui::COLS - 1) / hubui::COLS;
    const int y0 = hubui::FOLDER_TOP;
    for (int ii = 0; ii < G[gi].n; ii++) {
      if (ii > 0 && ii % PER == 0) {
        toybox.onTap(EPD_W - 24 - 75, hubui::FOLDER_TOP + 3 * hubui::ROW_STEP + 12 + 38);  // ">"
        if (toybox.hostInApp() || toybox.hostInSettings()) {
          printf("HUB ROUTING FAIL: the page arrow in drawer %d opened something\n", gi);
          abort();
        }
      }
      const int slot = ii % PER;
      const int col = hubui::cellCol(slot);
      const int cx = hubui::cellCx(slot);
      const int rowTop = hubui::cellTop(slot);
      const Want& w = G[gi].items[ii];
      routesTo(cx, rowTop + hubui::ROW_STEP / 2, w, "centre", gi, ii);
      routesTo(cx, rowTop + 1, w, "top edge", gi, ii);
      routesTo(cx, rowTop + hubui::ROW_STEP - 2, w, "bottom edge", gi, ii);
      routesTo(col * hubui::CELL_W + 1, rowTop + hubui::ROW_STEP / 2, w, "left edge", gi, ii);
      routesTo((col + 1) * hubui::CELL_W - 2, rowTop + hubui::ROW_STEP / 2, w, "right edge", gi,
               ii);
    }
    if (hasAdd) {
      // The ghost cell itself: it must open settings, and leaving settings
      // must come back to the hub with the drawer still selected.
      if (G[gi].n > 0 && G[gi].n % PER == 0) toybox.onTap(EPD_W - 24 - 75, hubui::FOLDER_TOP + 3 * hubui::ROW_STEP + 12 + 38);
      toybox.onTap(hubui::cellCx(G[gi].n % PER), hubui::cellTop(G[gi].n % PER) + hubui::ROW_STEP / 2);
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
    // A short last row leaves empty slices; nothing may open there.
    if (cells % hubui::COLS != 0) {
      toybox.onTap(hubui::cellCx(cells % PER), hubui::cellTop(cells % PER) + hubui::ROW_STEP / 2);
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

// Drawing with a card face: the fixture is loaded into the 18 px box and the
// same line is measured and drawn with it and without it. Three things have to
// hold, and each was a way to ship a font that looks installed and is not.
static void checkCardFace() {
  std::vector<uint8_t> blob;
  if (!readFixture(blob)) {
    printf("CARD FONT FAIL: fixture missing\n");
    abort();
  }
  const char* kLine = "Hamburgefonstiv";
  const char* kThai = "\xe0\xb8\xaa\xe0\xb8\xa7\xe0\xb8\xb1\xe0\xb8\xaa\xe0\xb8\x94\xe0\xb8\xb5";
  auto inkOnScreen = [&](const char* text, int px) {
    epd.clear(true);
    gfx::drawText(20, 100, text, px, 0);
    int n = 0;
    for (int y = 90; y < 150; y++)
      for (int x = 0; x < 480; x++) {
        int pxx, pyy;
        epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, pxx, pyy);
        if ((epd.fb()[(uint32_t)pyy * EPD_WB + (pxx >> 3)] & (0x80 >> (pxx & 7))) == 0) n++;
      }
    return n;
  };

  const int bakedInk = inkOnScreen(kLine, 18);
  const int bakedWidth = gfx::textWidth(kLine, 18, false, 0);
  const int bakedThai = inkOnScreen(kThai, 18);

  uint8_t* owned = (uint8_t*)malloc(blob.size());
  memcpy(owned, blob.data(), blob.size());
  if (!gfx::cardFaceSet(18, owned, (uint32_t)blob.size(), false, true) || !gfx::cardFaceLive(false)) {
    printf("CARD FONT FAIL: the fixture would not install\n");
    abort();
  }

  const int cardInk = inkOnScreen(kLine, 18);
  const int cardWidth = gfx::textWidth(kLine, 18, false, 0);
  // 1. It is actually being used. A face that installs and never draws is the
  //    failure mode this whole feature invites.
  if (cardInk == bakedInk || cardWidth == bakedWidth) {
    printf("CARD FONT FAIL: same ink (%d) or same width (%d) as the baked face\n", cardInk,
           cardWidth);
    abort();
  }
  if (cardInk < 100) {
    printf("CARD FONT FAIL: the card face drew %d px -- nothing legible\n", cardInk);
    abort();
  }
  // 2. Thai still draws. This pack has no Thai; the baked tables have to keep
  //    answering underneath, or choosing a font silently empties the language.
  const int cardThai = inkOnScreen(kThai, 18);
  if (cardThai != bakedThai) {
    printf("CARD FONT FAIL: Thai drew %d px under the card face, %d without\n", cardThai,
           bakedThai);
    abort();
  }
  // 3. Measuring and drawing agree. A width taken from the baked tables while
  //    the glyphs come off the card is how centred text drifts and how a page
  //    turn loses its last word.
  epd.clear(true);
  gfx::drawText(20, 100, kLine, 18, 0);
  int rightmost = 0;
  for (int y = 90; y < 150; y++)
    for (int x = 0; x < 480; x++) {
      int pxx, pyy;
      epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, pxx, pyy);
      if ((epd.fb()[(uint32_t)pyy * EPD_WB + (pxx >> 3)] & (0x80 >> (pxx & 7))) == 0 && x > rightmost)
        rightmost = x;
    }
  const int drawnWidth = rightmost - 20 + 1;
  if (drawnWidth > cardWidth || drawnWidth < cardWidth - 6) {
    printf("CARD FONT FAIL: drew %d px wide, measured %d\n", drawnWidth, cardWidth);
    abort();
  }

  // One picture of the two faces on one screen, because "the widths differ" is
  // not the same as "it looks like a font".
  {
    epd.clear(true);
    gfx::cardFaceClear(false);
    gfx::drawText(20, 40, "The baked face, Noto Sans", 18, 0);
    gfx::drawText(20, 70, kLine, 18, 0);
    gfx::drawText(20, 100, kThai, 18, 0);
    uint8_t* again = (uint8_t*)malloc(blob.size());
    memcpy(again, blob.data(), blob.size());
    gfx::cardFaceSet(18, again, (uint32_t)blob.size(), false, true);
    gfx::drawText(20, 160, "The card face, off a .cpfont", 18, 0);
    gfx::drawText(20, 190, kLine, 18, 0);
    gfx::drawText(20, 220, kThai, 18, 0);
    gfx::drawText(20, 250, "0123456789 !?,.;: quick brown fox", 18, 0);
    setScreen("card_font");
    epd.displayFull();
  }

  gfx::cardFaceClear(false);
  if (gfx::cardFaceLive(false) || gfx::textWidth(kLine, 18, false, 0) != bakedWidth) {
    printf("CARD FONT FAIL: clearing the face did not put the baked one back\n");
    abort();
  }
  epd.clear(true);
  printf("card face ok (%d px of ink against the baked %d, %d px wide against %d, Thai intact)\n",
         cardInk, bakedInk, cardWidth, bakedWidth);
}

// Choosing a family off the card: three sizes of one face are planted where
// CrossInk puts them, and the chooser has to put the right file in each box.
// Their sizes are points at 150 DPI and ours are pixel boxes, so this is the
// step where a family gets matched to a device by the numbers each file states
// rather than by the number in its name.
// --- a font is prepared once, and read small ever after ---------------------
// The published packs are generous -- Bitter at 16 pt is 962 KB, four cuts of
// 1,650 glyphs covering Latin, Greek, Cyrillic, Vietnamese, punctuation and a
// few hundred emoji -- and every byte of it crossed the bus each time an app
// opened. So the first use cuts the file down to what this device draws and
// writes the small copy back beside it.
//
// The font here is built rather than planted, because the point is exactly
// which codepoints and cuts survive, and a fixture cannot be asked that
// question unless it carries the ones that should not.
namespace prepfix {

struct Range {
  uint32_t first, last;
};
// Latin capitals (kept), Cyrillic (dropped), Thai (kept), an em dash from the
// punctuation block (kept), a smiling face from the emoji block (dropped), and
// the shifted Thai mark forms a Thai face keeps in the Private Use Area (kept;
// last, so a face can be built without them).
static const Range kRanges[6] = {{0x20, 0x7E},     {0x400, 0x44F},     {0xE01, 0xE5B},
                                 {0x2014, 0x2014}, {0x1F600, 0x1F600}, {0xF700, 0xF71A}};

// Four pixels square, so a glyph is exactly four bytes at 2 bits a pixel, and
// its ink is derived from its codepoint -- which is what makes "the prepared
// copy draws the same glyph" a question with an answer.
static void glyphBits(uint32_t cp, uint8_t* out) {
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)((cp * 7 + i * 31) & 0xFF);
}

static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { memcpy(&v[at], &x, 4); }
static void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { memcpy(&v[at], &x, 2); }

// A .cpfont with all four cuts, three of which should survive.
static std::vector<uint8_t> build(uint8_t advanceY, bool thaiForms = true) {
  const int nRanges = thaiForms ? 6 : 5;
  uint32_t nGlyphs = 0;
  for (int k = 0; k < nRanges; k++) nGlyphs += kRanges[k].last - kRanges[k].first + 1;
  const int kCuts = 4;
  std::vector<uint8_t> v(32 + kCuts * 32, 0);
  memcpy(v.data(), "CPFONT\0\0", 8);
  put16(v, 8, 4);  // version
  v[12] = kCuts;
  for (int c = 0; c < kCuts; c++) {
    const size_t at = v.size();
    // intervals
    for (uint32_t gi = 0, k = 0; k < (uint32_t)nRanges; k++) {
      v.resize(v.size() + 12);
      put32(v, v.size() - 12, kRanges[k].first);
      put32(v, v.size() - 8, kRanges[k].last);
      put32(v, v.size() - 4, gi);
      gi += kRanges[k].last - kRanges[k].first + 1;
    }
    // glyphs
    uint32_t off = 0;
    for (int k = 0; k < nRanges; k++)
      for (uint32_t cp = kRanges[k].first; cp <= kRanges[k].last; cp++) {
        v.resize(v.size() + 16, 0);
        uint8_t* g = &v[v.size() - 16];
        g[0] = 4;  // w
        g[1] = 4;  // h
        put16(v, v.size() - 14, (uint16_t)(64 + c));  // advance16, per cut
        put16(v, v.size() - 12, 0);                   // left
        put16(v, v.size() - 10, 4);                   // top
        put16(v, v.size() - 8, 4);                    // bytes
        put32(v, v.size() - 4, off);
        off += 4;
      }
    // bitmaps
    for (int k = 0; k < nRanges; k++)
      for (uint32_t cp = kRanges[k].first; cp <= kRanges[k].last; cp++) {
        uint8_t b[4];
        glyphBits(cp + c, b);
        v.insert(v.end(), b, b + 4);
      }
    uint8_t* t = &v[32 + (size_t)c * 32];
    t[0] = (uint8_t)c;  // cut id: 0 regular, 1 bold, 2 italic, 3 bold-italic
    put32(v, 32 + (size_t)c * 32 + 4, (uint32_t)nRanges);
    put32(v, 32 + (size_t)c * 32 + 8, nGlyphs);
    t[12] = advanceY;
    put16(v, 32 + (size_t)c * 32 + 13, 20);  // ascender
    put16(v, 32 + (size_t)c * 32 + 15, (uint16_t)(int16_t)-6);
    put32(v, 32 + (size_t)c * 32 + 24, (uint32_t)at);
  }
  return v;
}

}  // namespace prepfix

static void checkPreparedFonts() {
  std::vector<uint8_t> src = prepfix::build(24);
  cpfont::Font f;
  if (!f.open(src.data(), (uint32_t)src.size()) || f.styles() != 4) {
    printf("PREP FAIL: the built font is not one this reads (%d cuts)\n", f.styles());
    abort();
  }

  std::vector<uint8_t> out(src.size());
  const uint32_t n = cpfont::prepare(f, (uint32_t)src.size(), out.data(), (uint32_t)out.size());
  cpfont::Font g;
  if (!n || n >= src.size() || !g.open(out.data(), n)) {
    printf("PREP FAIL: %zu bytes prepared to %u, parses %d\n", src.size(), n,
           (int)g.open(out.data(), n));
    abort();
  }
  // The bold-italic cut goes: gfx never draws it -- bold beats italic there --
  // and it is a quarter of every file.
  if (g.styles() != 3 || g.find(cpfont::BOLD_ITALIC) >= 0) {
    printf("PREP FAIL: %d cuts survived, bold-italic at %d\n", g.styles(),
           g.find(cpfont::BOLD_ITALIC));
    abort();
  }
  // What a book is set in stays; what it is not, goes. A dropped codepoint is
  // not a hole -- the baked tables answer for it, the way they do for Thai
  // under a Latin-only pack. Thai itself, and its shifted mark forms, stay.
  struct {
    uint32_t cp;
    bool keep;
    const char* what;
  } want[6] = {{0x41, true, "capital A"},
               {0x2014, true, "an em dash"},
               {0x410, false, "Cyrillic A"},
               {0x1F600, false, "an emoji"},
               {0xE01, true, "Thai ko kai"},
               {0xF70A, true, "a lowered Thai tone mark"}};
  for (int c = 0; c < g.styles(); c++) {
    const cpfont::Style& gs = g.style(c);
    const cpfont::Style& fs = f.style(f.find(gs.cut));
    if (gs.advanceY != fs.advanceY || gs.ascender != fs.ascender ||
        gs.descender != fs.descender) {
      printf("PREP FAIL: cut %d came back with different metrics\n", gs.cut);
      abort();
    }
    for (auto& w : want) {
      cpfont::Glyph a, b;
      const bool has = g.glyph(gs, w.cp, b);
      if (has != w.keep) {
        printf("PREP FAIL: %s is %s the prepared font\n", w.what, has ? "in" : "not in");
        abort();
      }
      if (!w.keep) continue;
      // And it is the SAME glyph: same metrics, same ink, off a rebuilt offset
      // table. An offset a byte out reads the middle of its neighbour and
      // looks like a font that is subtly, unfixably wrong.
      if (!f.glyph(fs, w.cp, a) || a.w != b.w || a.h != b.h || a.advance16 != b.advance16 ||
          a.bytes != b.bytes) {
        printf("PREP FAIL: %s came back a different shape\n", w.what);
        abort();
      }
      for (int y = 0; y < a.h; y++)
        for (int x = 0; x < a.w; x++)
          if (f.ink(fs, a, x, y) != g.ink(gs, b, x, y)) {
            printf("PREP FAIL: %s differs at %d,%d in cut %d\n", w.what, x, y, gs.cut);
            abort();
          }
    }
  }

  // Now the same thing through the card. The first load reads the whole font
  // and leaves a prepared copy beside it; the second reads only that.
  sdcard::hostPutCardFile("/.fonts/Prepped/Prepped_10.cpfont", src.data(), (int)src.size());
  if (!cardfonts::useUniversal("Prepped")) {
    printf("PREP FAIL: the built family would not load\n");
    abort();
  }
  const int madeSize = sdcard::hostCardFileSize("/.fonts/Prepped/Prepped_10.tbf");
  if (madeSize <= 32 || madeSize >= (int)src.size()) {
    printf("PREP FAIL: the prepared copy is %d bytes beside a %zu byte font\n", madeSize,
           src.size());
    abort();
  }
  // A .tbf is not another size to choose from: the family walk takes .cpfont
  // and nothing else, or every prepared copy would double its family's list.
  char sizes[cardfonts::MAX_SIZES][sdcard::FONT_FILE_LEN];
  if (sdcard::fontFiles("Prepped", sizes, cardfonts::MAX_SIZES) != 1) {
    printf("PREP FAIL: the prepared copy showed up as a size of its own\n");
    abort();
  }

  sdcard::hostResetBytesRead();
  cardfonts::noneUniversal();
  if (!cardfonts::useUniversal("Prepped")) {
    printf("PREP FAIL: the family would not load a second time\n");
    abort();
  }
  const uint32_t again = sdcard::hostBytesRead();
  if (again >= src.size()) {
    printf("PREP FAIL: the second load read %u bytes of a %zu byte font -- not the prepared copy\n",
           again, src.size());
    abort();
  }

  // Replace the font with a different one and the prepared copy stops
  // matching: it is stamped with the length of the file it was made from, so a
  // card whose owner swapped a face does not read yesterday's ghost of it.
  std::vector<uint8_t> other = prepfix::build(26);
  other.push_back(0);  // a different length, which is what the stamp records
  sdcard::hostPutCardFile("/.fonts/Prepped/Prepped_10.cpfont", other.data(), (int)other.size());
  cardfonts::noneUniversal();
  if (!cardfonts::useUniversal("Prepped")) {
    printf("PREP FAIL: the replaced font would not load\n");
    abort();
  }
  if (gfx::cardFaceLine(24, false) != 26) {
    printf("PREP FAIL: the replaced font drew a %d px line, not 26 -- the old copy was reused\n",
           gfx::cardFaceLine(24, false));
    abort();
  }
  cardfonts::noneUniversal();
  printf("prepared fonts ok (%zu -> %u bytes, 4 cuts to 3, second load read %u, stamp rebuilds)\n",
         src.size(), n, again);
}

// --- a card face sits on its own baseline, and a strike goes through it ------
// A card face hangs from a baseline it states; the baked faces are packed into
// their box. Everything used to be drawn against the BAKED baseline, which was
// near enough while a card file only ever stood in for the box it sat in --
// and wrong by twenty pixels the moment a reader could set a book in a face
// half again as tall as its box. The same mistake put the strikethrough on a
// done task under the words instead of through them.
static void checkCardBaseline() {
  std::vector<uint8_t> big;
  {
    // The 18 pt cut: a 44 px line, ascender 35, x-height 20 -- deliberately
    // put in the 18 px box, because that is where the two baselines disagree
    // most and where a bug is unmissable.
    char path[256];
    snprintf(path, sizeof(path), "fixture_family/DejaVuSerif_18.cpfont");
    FILE* f = fopen(path, "rb");
    if (!f) {
      const char* env = getenv("TOYBOX_FIXTURES");
      snprintf(path, sizeof(path), "%s/fixture_family/DejaVuSerif_18.cpfont",
               env ? env : "../../test/host");
      f = fopen(path, "rb");
    }
    if (!f) {
      printf("BASELINE FAIL: fixture missing\n");
      abort();
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    big.resize((size_t)n);
    if (fread(big.data(), 1, (size_t)n, f) != (size_t)n) abort();
    fclose(f);
  }
  uint8_t* owned = (uint8_t*)malloc(big.size());
  memcpy(owned, big.data(), big.size());
  if (!gfx::cardFaceSet(18, owned, (uint32_t)big.size(), true, true)) {
    printf("BASELINE FAIL: the tall face would not install\n");
    abort();
  }
  gfx::contentFace(true);

  const int top = 100;
  epd.clear(true);
  gfx::drawText(20, top, "xxxxx", 18, 0);
  int inkT = -1, inkB = -1;
  for (int y = 0; y < 400; y++)
    for (int x = 0; x < 480; x++) {
      int pxx, pyy;
      epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, pxx, pyy);
      if ((epd.fb()[(uint32_t)pyy * EPD_WB + (pxx >> 3)] & (0x80 >> (pxx & 7))) == 0) {
        if (inkT < 0) inkT = y;
        inkB = y;
      }
    }
  const int line = gfx::textHeight(18);
  if (inkT < 0) {
    printf("BASELINE FAIL: the tall face drew nothing\n");
    abort();
  }
  // 1. The words stay inside the line they were given. Drawn against the baked
  //    18 px box's baseline, a 35 px ascender put this fifteen pixels above the
  //    top of its own line -- on top of the line above it.
  if (inkT < top) {
    printf("BASELINE FAIL: a %d px face drew %d px above its own line\n", line, top - inkT);
    abort();
  }
  if (inkB > top + line) {
    printf("BASELINE FAIL: lowercase ran %d px below a %d px line\n", inkB - top - line, line);
    abort();
  }
  // 2. A line through the words goes through the WORDS. Half the line box is
  //    not the answer for a face that hangs from a baseline: here the box is
  //    44 px and the lowercase sits between 15 and 35.
  const int mid = gfx::textMidline(18);
  const int want = (inkT - top + inkB - top) / 2;
  if (mid < inkT - top || mid > inkB - top || mid < want - 3 || mid > want + 3) {
    printf("BASELINE FAIL: the strike sits at %d, the lowercase between %d and %d\n", mid,
           inkT - top, inkB - top);
    abort();
  }
  gfx::contentFace(false);
  // 3. And the firmware's own faces are untouched: half the box, less one, is
  //    where every screen already draws it.
  const int baked = gfx::textMidline(24);
  if (baked != gfx::textHeight(24) / 2 - 1) {
    printf("BASELINE FAIL: the baked midline moved to %d\n", baked);
    abort();
  }
  gfx::cardFaceClear(true);
  printf("card baseline ok (%d px face inside a %d px line, strike at %d of %d..%d)\n", line, line,
         mid, inkT - top, inkB - top);
}

// --- Thai lines in the reader -------------------------------------------------
// Thai has no spaces between words, so a sentence reaches the reader as one
// long run. It used to be cut into 96-byte pieces with a space put back
// between them -- mid-syllable, sometimes leaving a tone mark to start a line
// -- and a run that did not fit beside a line jumped whole to the next one.
// Now: pieces join with no space, cuts fall only where a line may break, and a
// run fills its line. Read through the real reader, from a real book.
static const char* const kThaiCh2[] = {
    "เรื่องสั้นสำหรับทดสอบ",
    "คืนนั้นฝนตกปรอยๆ ตั้งแต่หัวค่ำ น้องเล็กนั่งอยู่ริมหน้าต่าง ถือเครื่องอ่านหนังสือเครื่องเล็กไว้ในมือ หน้าจอสีเทาอ่อนสะท้อนแสงไฟจากโคมข้างเตียง ตัวหนังสือคมชัดเหมือนพิมพ์ลงบนกระดาษจริงๆ",
    "เธอเลื่อนหน้าไปเรื่อยๆ อ่านเรื่องของนักเดินทางคนหนึ่งที่ข้ามภูเขาฝ้ายขาวไปยังหมู่บ้านริมฝั่งน้ำ ที่นั่นมีชาวบ้านปลูกข้าว ทอผ้า และเลี้ยงปลาในบ่อหลังบ้าน ทุกเช้าจะมีเสียงไก่ขันปลุกให้ตื่นก่อนฟ้าสาง",
    "“พรุ่งนี้ไปตลาดกันไหม” พี่ชายถามจากอีกห้อง น้องเล็กตอบว่า “ไปสิ แต่ขออ่านให้จบบทนี้ก่อนนะ” แล้วก็ก้มหน้าอ่านต่อจนดึก ลืมไปเลยว่าน้ำชาในแก้วเย็นชืดไปนานแล้ว",
    "เมื่อถึงหน้าสุดท้ายของบท เธอปิดเครื่องอย่างระมัดระวัง วางไว้ข้างหมอน แล้วหลับไปพร้อมกับภาพของหมู่บ้านริมน้ำที่ยังลอยอยู่ในความคิด",
    "บรรทัดนี้ยาวเป็นพิเศษเพื่อทดสอบการตัดคำภาษาไทยซึ่งไม่มีช่องว่างระหว่างคำและต้องตัดบรรทัดให้ถูกตำแหน่งโดยไม่แยกสระหรือวรรณยุกต์ออกจากพยัญชนะของมันเลยแม้แต่ตัวเดียว",
};

static std::string noSpaces(const std::string& s) {
  std::string o;
  for (char ch : s)
    if (ch != ' ') o += ch;
  return o;
}

static void checkThaiLines() {
  std::vector<uint8_t> book;
  {
    const char* tries[] = {"fixtures/thai_lines.epub", "test/host/fixtures/thai_lines.epub"};
    for (const char* t : tries) {
      FILE* f = fopen(t, "rb");
      if (!f) continue;
      fseek(f, 0, SEEK_END);
      book.resize((size_t)ftell(f));
      fseek(f, 0, SEEK_SET);
      if (fread(book.data(), 1, book.size(), f) != book.size()) book.clear();
      fclose(f);
      break;
    }
  }
  if (book.empty()) {
    printf("THAI LINES FAIL: fixtures/thai_lines.epub not found (run from test/host)\n");
    abort();
  }
  const char* path = "/books/thai-lines.epub";
  sdcard::hostPutCardFile(path, book.data(), (int)book.size());
  sdcard::hostAddBook(path);
  toybox.goHub();
  toybox.hostHub().goHome();
  toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);  // STUDY
  {
    recents::Entry rr[recents::MAX];
    const bool strip = recents::list(stickyHost.prefs(), rr) > 0;
    (void)strip;
    toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);
  }
  if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "EPUB") != 0) {
    printf("THAI LINES FAIL: the EPUB app did not open\n");
    abort();
  }
  auto* et = static_cast<EpubTool*>(toybox.hostActive());
  int idx = -1;
  for (int i = 0; i < et->hostBookCount(); i++) {
    char f[160];
    et->hostFile(i, f, sizeof(f));
    if (strstr(f, "thai-lines.epub")) idx = i;
  }
  if (idx < 0) {
    printf("THAI LINES FAIL: the planted book is not on the shelf\n");
    abort();
  }
  // The word list itself, on the owner's sentence: every word whole.
  {
    const char* s = "อ่านเรื่องของนักเดินทางคนหนึ่งที่ข้ามภูเขา";
    const char* want = "อ่าน|เรื่อง|ของ|นัก|เดิน|ทาง|คน|หนึ่ง|ที่|ข้าม|ภูเขา";
    uint8_t ok[epubc::WORD_CAP + 1];
    thaiwords::breaks(s, ok);
    std::string got;
    for (int b = 0; s[b]; b++) {
      if (b && ok[b]) got += '|';
      got += s[b];
    }
    if (got != want) {
      printf("THAI LINES FAIL: the words came out as %s\n", got.c_str());
      abort();
    }
  }
  et->hostOpen(idx);
  int sizes = 0, lines = 0;
  for (uint8_t size = 0; size < 3; size++, sizes++) {
    et->hostSetStyle(size);
    et->hostGoto(1, 0);
    std::string all;
    std::vector<size_t> seams;  // where one line ends and the next begins, in `all`
    int spaces = 0;
    for (int page = 0; page < 20; page++) {
      for (int i = 0; i < et->hostLineCount(); i++, lines++) {
        const char* t = et->hostLine(i);
        const char* p = t;
        const uint32_t first = uni::next(p);
        if (uni::thaiMark(first) || uni::thaiNoLineStart(first)) {
          printf("THAI LINES FAIL: size %d, a line starts with U+%04X: '%s'\n", size, first, t);
          abort();
        }
        uint32_t last = 0;
        for (p = t; *p;) last = uni::next(p);
        if (uni::thaiLeading(last)) {
          printf("THAI LINES FAIL: size %d, a line ends on leading vowel U+%04X: '%s'\n", size,
                 last, t);
          abort();
        }
        for (p = t; *p; p++) spaces += *p == ' ';
        if (!all.empty()) seams.push_back(all.size());
        all += t;
      }
      const int sp = et->hostSpine(), pg = et->hostPage();
      toybox.onButton(SideBtn::Down);
      if (et->hostSpine() != sp || et->hostPage() == pg) break;
    }
    // Nothing lost, nothing doubled, and no space the book did not have.
    std::string want;
    int wantSpaces = 0;
    for (const char* para : kThaiCh2) {
      want += para;
      for (const char* p = para; *p; p++) wantSpaces += *p == ' ';
    }
    if (noSpaces(all) != noSpaces(want)) {
      printf("THAI LINES FAIL: size %d, the chapter read back differently:\n  %s\n", size,
             all.c_str());
      abort();
    }
    // And no word torn across two lines -- the ones the owner saw split.
    const char* whole[] = {"คน", "ของ", "บ่อ", "นั่น", "หนึ่ง", "ริม", "ระหว่าง", "หนังสือ",
                           "เครื่อง", "ระมัดระวัง", "ภูเขา", "หมู่บ้าน", "เล็ก", "โคม", "จริงๆ"};
    for (const char* wd : whole)
      for (size_t at = all.find(wd); at != std::string::npos; at = all.find(wd, at + 1))
        for (size_t seam : seams)
          if (seam > at && seam < at + strlen(wd)) {
            printf("THAI LINES FAIL: size %d, '%s' is split across two lines\n", size, wd);
            abort();
          }
    if (spaces > wantSpaces) {
      printf("THAI LINES FAIL: size %d, %d spaces on the page, the book has %d\n", size, spaces,
             wantSpaces);
      abort();
    }
  }
  toybox.goHub();
  sdcard::hostClearBooks();
  printf("thai lines ok (%d sizes, %d lines, words whole, no mark starts a line, no space added)\n", sizes,
         lines);
}

// --- small decorations are not pages ------------------------------------------
// Owner, 24 Nov: a 30x34 scene-break ornament filled a page of its own. The
// fixture has the book's two ornaments (30x34, 58x57), a thin divider rule
// and one real illustration; only the illustration may become a page, and
// every word of the chapter still has to be there.
static void checkDecorations() {
  std::vector<uint8_t> book;
  {
    const char* tries[] = {"fixtures/decor.epub", "test/host/fixtures/decor.epub"};
    for (const char* t : tries) {
      FILE* f = fopen(t, "rb");
      if (!f) continue;
      fseek(f, 0, SEEK_END);
      book.resize((size_t)ftell(f));
      fseek(f, 0, SEEK_SET);
      if (fread(book.data(), 1, book.size(), f) != book.size()) book.clear();
      fclose(f);
      break;
    }
  }
  if (book.empty()) {
    printf("DECOR FAIL: fixtures/decor.epub not found (run from test/host)\n");
    abort();
  }
  if (!epubart::isDecoration(30, 34) || !epubart::isDecoration(58, 57) || !epubart::isDecoration(600, 20) ||
      epubart::isDecoration(1800, 651) || epubart::isDecoration(600, 900) || epubart::isDecoration(120, 90)) {
    printf("DECOR FAIL: the size rule is wrong\n");
    abort();
  }
  const char* path = "/books/decor.epub";
  sdcard::hostPutCardFile(path, book.data(), (int)book.size());
  sdcard::hostAddBook(path);
  toybox.goHub();
  toybox.hostHub().goHome();
  toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);  // STUDY
  toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);
  if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "EPUB") != 0) {
    printf("DECOR FAIL: the EPUB app did not open\n");
    abort();
  }
  auto* et = static_cast<EpubTool*>(toybox.hostActive());
  int idx = -1;
  for (int i = 0; i < et->hostBookCount(); i++) {
    char f[160];
    et->hostFile(i, f, sizeof(f));
    if (strstr(f, "decor.epub")) idx = i;
  }
  if (idx < 0) {
    printf("DECOR FAIL: the planted book is not on the shelf\n");
    abort();
  }
  et->hostOpen(idx);
  if (et->hostArtParts("OEBPS/Images/pic.jpg") != 1) {
    printf("DECOR FAIL: the illustration was not prepared (%d)\n", et->hostArtParts("OEBPS/Images/pic.jpg"));
    abort();
  }
  et->hostGoto(0, 0);
  int picPages = 0, decorPages = 0, words = 0;
  for (int page = 0; page < 40; page++) {
    const char* im = et->hostPageImage();
    if (im && im[0]) {
      if (strstr(im, "pic.jpg")) picPages++;
      else decorPages++;
    }
    for (int i = 0; i < et->hostLineCount(); i++)
      for (const char* p = et->hostLine(i); *p; p++) words += *p == ' ';
    if (page == 1) {
      g_dumpEnabled = true;
      setScreen("tool_epub_decor_gap");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
    }
    const int sp = et->hostSpine(), pg = et->hostPage();
    toybox.onButton(SideBtn::Down);
    if (et->hostSpine() != sp || et->hostPage() == pg) break;
  }
  if (decorPages || picPages != 1) {
    printf("DECOR FAIL: %d ornament pages, %d illustration pages (want 0 and 1)\n", decorPages, picPages);
    abort();
  }
  // A book opened before this change: its index has the ornaments as 1-part
  // pictures and no revision. Opening it again must learn they are
  // decorations from their headers alone, not prepare everything again.
  {
    epubart::Prepared recs[16];
    bool checked = false;
    const int n = epubart::indexRead(stickyHost, path, recs, 16, &checked);
    int dec = 0;
    for (int i = 0; i < n; i++)
      if (recs[i].parts == epubart::DECOR) {
        recs[i].parts = 1;
        dec++;
      }
    if (n != 4 || !checked || dec != 3) {
      printf("DECOR FAIL: index n=%d checked=%d decorations=%d (want 4, 1, 3)\n", n, checked, dec);
      abort();
    }
    epubart::indexWrite(stickyHost, path, recs, n);
    char ip[bstore::PATH_CAP];
    bstore::file(path, "art.idx", ip, sizeof(ip));
    uint8_t raw[8 + 16 * 9];
    const int got = stickyHost.sdReadFile(ip, raw, sizeof(raw));
    raw[5] = 0;  // the old revision
    stickyHost.sdWriteFileAtomic(ip, raw, got);
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);
    toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);
    et = static_cast<EpubTool*>(toybox.hostActive());
    for (int i = 0; i < et->hostBookCount(); i++) {
      char f[160];
      et->hostFile(i, f, sizeof(f));
      if (strstr(f, "decor.epub")) et->hostOpen(i);
    }
    const int nn = epubart::indexRead(stickyHost, path, recs, 16, &checked);
    dec = 0;
    for (int i = 0; i < nn; i++) dec += recs[i].parts == epubart::DECOR;
    if (!checked || dec != 3 || et->hostArtParts("OEBPS/Images/pic.jpg") != 1) {
      printf("DECOR FAIL: an old index was not upgraded (checked=%d decorations=%d)\n", checked, dec);
      abort();
    }
  }
  toybox.goHub();
  sdcard::hostClearBooks();
  // Optional, never committed: a real book named by TOYBOX_DECOR_BOOK, read
  // through its first chapters, reporting every picture page it made.
  if (const char* real = getenv("TOYBOX_DECOR_BOOK")) {
    FILE* f = fopen(real, "rb");
    std::vector<uint8_t> rb;
    if (f) {
      fseek(f, 0, SEEK_END);
      rb.resize((size_t)ftell(f));
      fseek(f, 0, SEEK_SET);
      if (fread(rb.data(), 1, rb.size(), f) != rb.size()) rb.clear();
      fclose(f);
    }
    sdcard::hostPutCardFile("/books/real.epub", rb.data(), (int)rb.size());
    sdcard::hostAddBook("/books/real.epub");
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);
    toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);
    auto* rt = static_cast<EpubTool*>(toybox.hostActive());
    for (int i = 0; i < rt->hostBookCount(); i++) {
      char fn[160];
      rt->hostFile(i, fn, sizeof(fn));
      if (strstr(fn, "real.epub")) rt->hostOpen(i);
    }
    for (int sp = 0; sp < 12; sp++) {
      rt->hostGoto(sp, 0);
      for (int page = 0; page < 80; page++) {
        const char* im = rt->hostPageImage();
        if (im && im[0]) printf("  real: spine %d page %d picture %s\n", sp, rt->hostPage(), im);
        const int s0 = rt->hostSpine(), pg = rt->hostPage();
        toybox.onButton(SideBtn::Down);
        if (rt->hostSpine() != s0 || rt->hostPage() == pg) break;
      }
    }
    toybox.goHub();
    {
      struct RIO : epubc::IO {
        int read(uint32_t pos, void* dst, uint32_t n) override {
          return stickyHost.epubRead(pos, dst, n);
        }
        uint32_t size() override { return stickyHost.epubSize(); }
      } rio;
      const bool fo = stickyHost.epubOpen("/books/real.epub");
      epubc::Book bk;
      const bool bo = fo && bk.open(rio);
      if (!bo) printf("  real: probe open failed (file %d, book %s)\n", fo, bk.error());
      if (bo) {
        const char* names[] = {"OEBPS/Images/Art_sborn.jpg", "OEBPS/Images/Art_sborn1.jpg",
                               "OEBPS/Images/Art_ch1.jpg", "OEBPS/Images/Art_insert1.jpg"};
        for (const char* nm : names) {
          int w = 0, h = 0;
          const bool ok = epubart::probeSize(bk, nm, w, h);
          printf("  real: probe %s %d %dx%d decor=%d\n", nm, ok, w, h, epubart::isDecoration(w, h));
        }
      }
    }
    sdcard::hostClearBooks();
  }
  printf("decorations ok (ornaments and the rule stepped over, the illustration kept)\n");
}

// --- Thai marks in a card face ------------------------------------------------
// A card face with Thai draws its marks in the face's own shifted forms
// (U+F700-F71A) where it carries them: left of a tall consonant's tail, down
// where no upper vowel sits, under ฎ and ฏ, with ญ and ฐ losing their tail
// over a lower vowel. Each fixture glyph has its own ink, so "drew the form"
// is checked by drawing the form's codepoint directly and comparing pixels.
static std::vector<uint8_t> inkOf(const char* s) {
  epd.clear(true);
  gfx::drawText(20, 100, s, 24, 0);
  const uint8_t* fb = epd.fb();
  return std::vector<uint8_t>(fb, fb + (size_t)EPD_WB * 800);
}

static void checkThaiCardMarks() {
  // 1. The rule, without a font.
  struct {
    const char* text;
    uint32_t want[4];
    const char* what;
  } rule[] = {
      {"ป่า", {0x0E1B, 0xF705, 0x0E32}, "tone on bare tall consonant: down and left"},
      {"ก้", {0x0E01, 0xF70B}, "tone on bare consonant: down"},
      {"ที่", {0x0E17, 0x0E35, 0x0E48}, "tone over an upper vowel: as it is"},
      {"ฟื้", {0x0E1F, 0xF704, 0xF714}, "vowel and tone on tall consonant: left"},
      {"น้ำ", {0x0E19, 0x0E49, 0x0E33}, "tone over sara am: high"},
      {"ญุ", {0xF70F, 0x0E38}, "yo ying over a lower vowel: no tail"},
      {"ฐู", {0xF700, 0x0E39}, "tho than over a lower vowel: no tail"},
      {"ฎุ", {0x0E0E, 0xF718}, "lower vowel under do chada: dropped"},
      {"ป a่", {0x0E1B, 0x20, 0x61, 0x0E48}, "a mark after Latin: not stacked"},
  };
  for (auto& r : rule) {
    thaishape::State st;
    uint32_t got[8];
    int n = 0;
    for (const char* p = r.text; *p;) {
      const uint32_t cp = uni::next(p);
      const char* q = p;
      const uint32_t nx = *q ? uni::next(q) : 0;
      uint32_t f[2];
      const int k = thaishape::forms(st, cp, nx, f);
      for (int i = 0; i < k && n < 8; i++) got[n++] = f[i];
    }
    for (int i = 0; i < n; i++)
      if (got[i] != r.want[i]) {
        printf("THAI FAIL: %s -- glyph %d is U+%04X, wanted U+%04X\n", r.what, i, got[i],
               r.want[i]);
        abort();
      }
  }
  thaishape::State st;
  uint32_t f[2];
  thaishape::forms(st, 0x0E1B, 0x0E33, f);
  if (thaishape::forms(st, 0x0E33, 0, f) != 2 || f[0] != 0xF711 || f[1] != 0x0E32) {
    printf("THAI FAIL: sara am on a tall consonant did not split with its ring moved left\n");
    abort();
  }

  // 2. Through the panel, in a face that has the forms.
  std::vector<uint8_t> font = prepfix::build(24, true);
  uint8_t* blob = (uint8_t*)malloc(font.size());
  memcpy(blob, font.data(), font.size());
  if (!gfx::cardFaceSet(24, blob, (uint32_t)font.size(), false, true)) {
    printf("THAI FAIL: the fixture face would not install\n");
    abort();
  }
  struct {
    const char* text;
    const char* asDrawn;
    const char* what;
  } pairs[] = {
      {"ป่า", "ปา", "pa with mai ek"},
      {"ฟื้น", "ฟน", "fuen"},
      {"ก้", "ก", "ko with mai tho"},
      {"ญุ", "ุ", "yo ying with sara u"},
      {"ปำ", "ปา", "pam"},
  };
  for (auto& pr : pairs) {
    const std::vector<uint8_t> a = inkOf(pr.text), b = inkOf(pr.asDrawn);
    if (a != b) {
      printf("THAI FAIL: %s did not draw in the face's shifted forms\n", pr.what);
      abort();
    }
    if (gfx::textWidth(pr.text, 24) != gfx::textWidth(pr.asDrawn, 24)) {
      printf("THAI FAIL: %s measures %d, draws %d wide\n", pr.what, gfx::textWidth(pr.text, 24),
             gfx::textWidth(pr.asDrawn, 24));
      abort();
    }
  }
  gfx::cardFaceClear(false);

  // 3. A face without the forms draws the plain marks -- nothing goes missing.
  font = prepfix::build(24, false);
  blob = (uint8_t*)malloc(font.size());
  memcpy(blob, font.data(), font.size());
  if (!gfx::cardFaceSet(24, blob, (uint32_t)font.size(), false, true)) {
    printf("THAI FAIL: the plain fixture face would not install\n");
    abort();
  }
  if (inkOf("ป่า") == inkOf("ปา")) {
    printf("THAI FAIL: a face without the shifted forms dropped the tone mark\n");
    abort();
  }
  if (inkOf("ปำ") == inkOf("ป")) {
    printf("THAI FAIL: a face without the shifted forms dropped sara am\n");
    abort();
  }
  gfx::cardFaceClear(false);
  printf("thai card marks ok (%zu rules, %zu words through the panel)\n",
         sizeof(rule) / sizeof(rule[0]), sizeof(pairs) / sizeof(pairs[0]));
}

// --- the lock screen that keeps the page you were reading --------------------
// E-paper holds its last frame with no power, so the page is already on the
// glass when the device goes to sleep. EMPTY_PAGE keeps it: nothing is
// cleared, the page is knocked back to a grey texture, and the lock picture
// is laid over the top -- its white is transparent already, because tbimg
// skips runs of white.
//
// The rule is a pure function so it can be asked here without a panel or a
// power button, and every refusal in it is a case where the frame on the glass
// is not a page worth keeping.
// The framebuffer blit (Epd::inkBits) must put every pixel exactly where
// the pixel-at-a-time canvas draw it replaced did, in every rotation and with
// either board flip -- covers, picture pages, manga pages and the lock screen
// all go through it.
static void checkInkBits() {
  const int rot0 = epd.rotation();
  const bool fx0 = epd.panelFlipX(), fy0 = epd.panelFlipY();
  static uint8_t bits[800 * 480 / 8];
  uint32_t r = 12345;
  for (size_t i = 0; i < sizeof(bits); i++) {
    r = r * 1103515245u + 12345u;
    // Mostly white bytes with ink scattered through, like a real page, so
    // the all-white skip is exercised as well as the per-bit path.
    bits[i] = ((r >> 16) % 3) ? 0xFF : (uint8_t)(r >> 8);
  }
  static uint8_t ref[EPD_BUF_SIZE];
  ToolsCanvas& c = stickyHost.canvas();
  for (int rot = 0; rot < 4; rot++)
    for (int f = 0; f < 4; f++) {
      epd.setRotation(rot);
      epd.setPanelFlip(f & 1, f & 2);
      const int w = epd.logicalW(), h = epd.logicalH();
      epd.clear();
      c.ToolsCanvas::inkBits(bits, w, h);  // the default: pixel by pixel
      memcpy(ref, epd.fb(), EPD_BUF_SIZE);
      epd.clear();
      c.inkBits(bits, w, h);
      if (memcmp(ref, epd.fb(), EPD_BUF_SIZE) != 0) {
        printf("INK BITS FAIL: rotation %d flips %d differs from the pixel draw\n", rot, f);
        abort();
      }
    }
  epd.setRotation(rot0);
  epd.setPanelFlip(fx0, fy0);
  epd.clear();
}

// C6: every state of the charging dashboard, both ways up, and the popup.
static void renderChargeDash() {
  const bool wasDump = g_dumpEnabled;
  g_dumpEnabled = true;
  const int rot0 = epd.rotation();
  ToolsCanvas& c = stickyHost.canvas();
  chgdash::Info in;
  in.haveClock = true;
  in.hh = 21; in.mm = 24; in.year = 2026; in.month = 11; in.day = 25;
  in.pct = 64;
  in.haveWx = true;
  in.now = wx::Now{312, 355, 70, 2, 2110};
  snprintf(in.place, sizeof(in.place), "Bangkok");
  in.haveIndoor = true; in.inDeci = 286; in.inRh = 61;
  struct Case { const char* name; int rot; chgdash::Batt b; int pct; bool noPlace; int code; };
  const Case cases[] = {
      {"chg_dash", 0, chgdash::B_CHARGING, 64, false, 2},
      {"chg_dash_held", 0, chgdash::B_HELD, 80, false, 61},
      {"chg_dash_noplace", 0, chgdash::B_CHARGING, 92, true, 2},
      {"chg_dash_land", 1, chgdash::B_CHARGING, 64, false, 2},
      {"chg_dash_land_held", 1, chgdash::B_HELD, 80, false, 0},
      {"chg_dash_land_noplace", 1, chgdash::B_FULL, 100, true, 2},
      {"chg_dash_land_storm", 3, chgdash::B_USB, 97, false, 95},
  };
  for (const Case& k : cases) {
    setScreen(k.name);
    epd.setRotation(k.rot);
    in.batt = k.b; in.pct = k.pct; in.noPlace = k.noPlace; in.now.code = (uint8_t)k.code;
    chgdash::draw(c, in);
    if (c.width() > c.height() != (k.rot & 1)) {
      printf("CHGDASH FAIL: %s drew %dx%d\n", k.name, c.width(), c.height());
      abort();
    }
    epd.displayFull();
  }
  // With a picture beside the weather: a striped 480x800 stand-in.
  {
    static uint8_t img[tbimg::FILE_SIZE];
    const uint32_t magic = tbimg::MAGIC;
    memcpy(img, &magic, 4);
    img[4] = 480 & 0xFF; img[5] = 480 >> 8; img[6] = 800 & 0xFF; img[7] = 800 >> 8;
    for (int y = 0; y < 800; y++)
      for (int xb = 0; xb < 60; xb++) {
        // A dithered photo stand-in: a top-to-bottom gradient with a dark
        // disc, ordered-dithered at full size the way a sent picture is.
        uint8_t v = 0;
        for (int k = 0; k < 8; k++) {
          const int x = xb * 8 + k, dx = x - 240, dy = y - 420;
          int g = 255 - y * 255 / 800;
          if (dx * dx + dy * dy < 150 * 150) g = g / 4;
          static const uint8_t B4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
          if (g > B4[y & 3][x & 3] * 16 + 8) v |= (uint8_t)(0x80 >> k);
        }
        img[tbimg::HEADER + y * 60 + xb] = v;
      }
    tfs::write("/chgtest.tbi", (const char*)img, sizeof(img));
    in.pic = "/chgtest.tbi";
    const Case pc[] = {{"chg_photo", 0, chgdash::B_CHARGING, 64, false, 2},
                       {"chg_photo_land", 1, chgdash::B_HELD, 80, false, 61},
                       {"chg_photo_noplace", 0, chgdash::B_CHARGING, 64, true, 2},
                       {"chg_photo_noborder", 0, chgdash::B_FULL, 100, false, 3}};
    for (const Case& k : pc) {
      setScreen(k.name);
      epd.setRotation(k.rot);
      in.batt = k.b; in.pct = k.pct; in.noPlace = k.noPlace; in.now.code = (uint8_t)k.code;
      in.picBorder = strcmp(k.name, "chg_photo_noborder") != 0;
      chgdash::draw(c, in);
      epd.displayFull();
    }
    in.pic = nullptr;
    in.picBorder = true;
    tfs::remove("/chgtest.tbi");
  }
  // The weather pass in Fahrenheit and below zero must still fit its column.
  setScreen("chg_dash_land_cold");
  epd.setRotation(1);
  in.batt = chgdash::B_CHARGING; in.noPlace = false; in.fahrenheit = true; in.now.t = -183;
  in.now.code = 73;
  chgdash::draw(c, in);
  epd.displayFull();
  setScreen("chg_popup");
  epd.setRotation(0);
  epd.clear();
  for (int k = 0; k < c.height(); k += 7) c.fillRect(0, k, c.width(), 1, true);  // a page behind
  const TRect cr = chgdash::drawPopup(c, 3, 64);
  if (cr.y + cr.h > c.height() || cr.w < 200) {
    printf("CHGDASH FAIL: Cancel off the popup\n");
    abort();
  }
  epd.displayFull();
  epd.setRotation(rot0);
  epd.clear();
  g_dumpEnabled = wasDump;
}

static void checkLockOverPage() {
  lock::Config page;
  page.empty = lock::EMPTY_PAGE;
  struct Case {
    bool pinned, low, showing;
    int rot;
    bool want;
    const char* what;
  };
  static const Case kCases[] = {
      {false, false, true, 0, true, "reading a page"},
      {true, false, true, 0, false, "a note is pinned -- the note owns the panel"},
      {false, true, true, 0, false, "the battery is flat and has to say so"},
      {false, false, false, 0, false, "not on a page -- a shelf or a menu"},
      {false, false, true, 1, true, "reading sideways -- the page stays, the picture upright over it"},
      {false, false, true, 3, true, "reading sideways the other way"},
  };
  for (const Case& k : kCases) {
    if (lock::keepsPage(page, k.pinned, k.low, k.showing, k.rot) != k.want) {
      printf("LOCK PAGE FAIL: %s came out %d\n", k.what, !k.want);
      abort();
    }
  }
  // And no other setting keeps the page. This is the one that decides whether
  // the panel is wiped, so a neighbouring enum value getting it wrong would
  // leave somebody's settings screen on the fridge.
  for (int i = 0; i < lock::EMPTY_COUNT; i++) {
    const uint8_t e = lock::emptyAt(i);
    // The page and the two covers laid over it keep the page (v2).
    if (e == lock::EMPTY_PAGE || e == lock::EMPTY_COVER_ON_PAGE || e == lock::EMPTY_COVER_INK)
      continue;
    lock::Config other;
    other.empty = e;
    if (lock::keepsPage(other, false, false, true, 0)) {
      printf("LOCK PAGE FAIL: mode %d kept the page\n", e);
      abort();
    }
  }
  // BLANK is retired: a device still set to it comes back as GOODBYE rather
  // than as a panel showing nothing, which is what a flat battery looks like.
  if (lock::emptyIndexOf(lock::EMPTY_BLANK) != 1) {
    printf("LOCK PAGE FAIL: a retired setting did not land on GOODBYE\n");
    abort();
  }

  // The dim. On one bit there is no dimming, only a checkerboard: every other
  // pixel to paper, so ink becomes half-tone and paper stays paper.
  auto panelInkNow = [] {
    int n = 0;
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) n += __builtin_popcount((uint8_t)~epd.fb()[i]);
    return n;
  };
  // Four steps, and each one has to leave exactly what it says: everything,
  // three quarters, a half, a quarter. How faint the page should be is taste,
  // so the owner sets it -- but the steps themselves are arithmetic.
  epd.clear(false);  // a panel of solid ink
  const int allInk = panelInkNow();
  if (allInk != (int)EPD_BUF_SIZE * 8) {
    printf("LOCK PAGE FAIL: a black panel is %d px\n", allInk);
    abort();
  }
  static const int kLeft[lock::PAGE_DIM_COUNT] = {4, 3, 2, 1};  // quarters left standing
  for (int d = 0; d < lock::PAGE_DIM_COUNT; d++) {
    epd.clear(false);
    epd.dim((uint8_t)d);
    const int left = panelInkNow();
    if (left * 4 != allInk * kLeft[d]) {
      printf("LOCK PAGE FAIL: dim %d left %d px, wanted %d quarters of %d\n", d, left, kLeft[d],
             allInk);
      abort();
    }
  }
  epd.clear(true);  // ...and paper stays paper, at every step
  for (int d = 0; d < lock::PAGE_DIM_COUNT; d++) epd.dim((uint8_t)d);
  if (panelInkNow() != 0) {
    printf("LOCK PAGE FAIL: dimming an empty panel put %d px on it\n", panelInkNow());
    abort();
  }

  // The whole composite, on a stand-in page: ink where a page has ink, dimmed,
  // with the picture over it. Three things have to be true at once, and each
  // one alone can be had by accident -- a cleared panel would pass "the
  // picture is there", and a panel nobody dimmed would pass "the page is
  // there".
  {
    // A page: horizontal bars where the lines of text would be.
    epd.clear(true);
    ToolsCanvas& c = stickyHost.sharedCanvas();
    epd.setRotation(0);
    for (int y = 40; y < 700; y += 30) c.fillRect(24, y, 432, 18, true);
    const int pageInk = panelInkNow();
    epd.dim(lock::PAGE_GHOST);
    const int dimmed = panelInkNow();
    if (dimmed * 4 != pageInk) {
      printf("LOCK PAGE FAIL: the page dimmed from %d to %d\n", pageInk, dimmed);
      abort();
    }
    // A picture with a frame in it, planted the way one arrives.
    {
      static std::vector<uint8_t> pic(tbimg::FILE_SIZE, 0xFF);
      pic[0] = 'T';
      pic[1] = 'B';
      pic[2] = 'I';
      pic[3] = '1';
      pic[4] = (uint8_t)(tbimg::W & 255);
      pic[5] = (uint8_t)(tbimg::W >> 8);
      pic[6] = (uint8_t)(tbimg::H & 255);
      pic[7] = (uint8_t)(tbimg::H >> 8);
      uint8_t* bits = pic.data() + tbimg::HEADER;
      auto ink = [&](int x, int y) {
        bits[(size_t)y * tbimg::STRIDE + (x >> 3)] &= (uint8_t)~(0x80 >> (x & 7));
      };
      for (int x = 8; x < tbimg::W - 8; x++)
        for (int t = 0; t < 4; t++) {
          ink(x, 8 + t);
          ink(x, tbimg::H - 12 + t);
        }
      for (int y = 8; y < tbimg::H - 8; y++)
        for (int t = 0; t < 4; t++) {
          ink(8 + t, y);
          ink(tbimg::W - 12 + t, y);
        }
      tfs::write(lockimg::PATH, (const char*)pic.data(), pic.size());
    }
    if (!tbimg::draw(c, lockimg::PATH)) {
      printf("LOCK PAGE FAIL: the picture would not draw\n");
      abort();
    }
    const int both = panelInkNow();
    if (both <= dimmed) {
      printf("LOCK PAGE FAIL: the picture added %d px over the page\n", both - dimmed);
      abort();
    }
    // The page is still under it. Measured where the picture is NOT -- inside
    // the frame, away from it -- so a panel the picture had cleared would read
    // as blank here however much ink its own frame carries.
    int inside = 0;
    for (int y = 200; y < 400; y++)
      for (int x = 100; x < 380; x++) {
        int pxx, pyy;
        epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, pxx, pyy);
        if ((epd.fb()[(uint32_t)pyy * EPD_WB + (pxx >> 3)] & (0x80 >> (pxx & 7))) == 0) inside++;
      }
    if (inside < 1000) {
      printf("LOCK PAGE FAIL: only %d px of the page survived under the picture\n", inside);
      abort();
    }
    tfs::remove(lockimg::PATH);

    // ...and again with the picture in the OTHER file. A picture taken from a
    // card .bmp is stored as the grey /lockimg.g2, not the 1-bit .tbi, and
    // lockimg::have() answers yes for either -- so the settings page said "one
    // is stored" while the lock screen drew nothing at all. Reported from
    // hardware as "it just dim page".
    epd.clear(true);
    for (int y = 40; y < 700; y += 30) c.fillRect(24, y, 432, 18, true);
    epd.dim(lock::PAGE_GHOST);
    const int dimmedAgain = panelInkNow();
    {
      static std::vector<uint8_t> grey(tbg2::FILE_SIZE, 0xFF);  // 3 is white
      grey[0] = 'T';
      grey[1] = 'B';
      grey[2] = 'G';
      grey[3] = '1';
      grey[4] = (uint8_t)(tbg2::W & 255);
      grey[5] = (uint8_t)(tbg2::W >> 8);
      grey[6] = (uint8_t)(tbg2::H & 255);
      grey[7] = (uint8_t)(tbg2::H >> 8);
      uint8_t* g2 = grey.data() + tbg2::HEADER;
      auto lvl = [&](int x, int y, uint8_t v) {
        uint8_t& by = g2[(size_t)y * (tbg2::W / 4) + (x >> 2)];
        const int sh = (3 - (x & 3)) * 2;
        by = (uint8_t)((by & ~(3 << sh)) | ((v & 3) << sh));
      };
      for (int x = 8; x < tbg2::W - 8; x++)
        for (int t = 0; t < 4; t++) {
          lvl(x, 8 + t, 0);
          lvl(x, tbg2::H - 12 + t, 0);
        }
      for (int y = 8; y < tbg2::H - 8; y++)
        for (int t = 0; t < 4; t++) {
          lvl(8 + t, y, 0);
          lvl(tbg2::W - 12 + t, y, 0);
        }
      // A band of the lighter grey, which must NOT ink: over a page already
      // knocked back to a checkerboard, a mid grey laid on top is noise.
      for (int y = 300; y < 340; y++)
        for (int x = 40; x < 440; x++) lvl(x, y, 2);
      tfs::write(lockimg::G2_PATH, (const char*)grey.data(), grey.size());
    }
    if (!lockimg::have()) {
      printf("LOCK PAGE FAIL: a grey picture did not count as a picture\n");
      abort();
    }
    if (!lockimg::drawOver(c)) {
      printf("LOCK PAGE FAIL: the grey picture would not draw over the page\n");
      abort();
    }
    const int greyBoth = panelInkNow();
    if (greyBoth <= dimmedAgain) {
      printf("LOCK PAGE FAIL: the grey picture added %d px\n", greyBoth - dimmedAgain);
      abort();
    }
    // The light band left the page as it was, rather than flooding it.
    int band = 0;
    for (int y = 300; y < 340; y++)
      for (int x = 40; x < 440; x++) {
        int pxx, pyy;
        epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, pxx, pyy);
        if ((epd.fb()[(uint32_t)pyy * EPD_WB + (pxx >> 3)] & (0x80 >> (pxx & 7))) == 0) band++;
      }
    if (band > 40 * 400 / 3) {
      printf("LOCK PAGE FAIL: the light grey inked %d px of the page\n", band);
      abort();
    }
    tfs::remove(lockimg::G2_PATH);
    epd.clear(true);
    printf("lock over page ok (page %d px, dimmed %d, picture over it %d, %d still showing; "
           "grey picture %d)\n",
           pageInk, dimmed, both, inside, greyBoth);
  }
}

static void checkCardFamily() {
  static const char* kSizes[3] = {"DejaVuSerif_8.cpfont", "DejaVuSerif_12.cpfont",
                                  "DejaVuSerif_18.cpfont"};
  for (const char* name : kSizes) {
    char path[256];
    snprintf(path, sizeof(path), "fixture_family/%s", name);
    FILE* f = fopen(path, "rb");
    if (!f) {
      const char* env = getenv("TOYBOX_FIXTURES");
      snprintf(path, sizeof(path), "%s/fixture_family/%s", env ? env : "../../test/host", name);
      f = fopen(path, "rb");
    }
    if (!f) {
      printf("CARD FAMILY FAIL: %s missing\n", name);
      abort();
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes((size_t)n);
    if (fread(bytes.data(), 1, (size_t)n, f) != (size_t)n) abort();
    fclose(f);
    char onCard[128];
    snprintf(onCard, sizeof(onCard), "/.fonts/DejaVuSerif/%s", name);
    sdcard::hostPutCardFile(onCard, bytes.data(), (int)bytes.size());
  }
  // A folder with no font in it is not a family: it would be a name in a list
  // that does nothing when tapped.
  sdcard::hostPutCardFile("/.fonts/NotAFamily/readme.txt", "hello", 5);

  // The two layouts a person actually produces, as against the one the
  // documentation describes. Unzipping <Family>.zip inside a folder you made
  // yourself leaves <Family>/<Family>/, and dragging the files out of the zip
  // leaves them loose in the root. Both were "no fonts on the card" on real
  // hardware, with the folders plainly sitting there.
  {
    std::vector<uint8_t> one;
    if (!readFixture(one)) abort();
    sdcard::hostPutCardFile("/.fonts/Nested/Nested/Nested_8.cpfont", one.data(), (int)one.size());
    sdcard::hostPutCardFile("/fonts/Loose_8.cpfont", one.data(), (int)one.size());
  }

  char names[cardfonts::MAX_FAMILIES][32];
  const int n = cardfonts::families(names, cardfonts::MAX_FAMILIES);
  bool found = false, ghost = false, nested = false, loose = false;
  for (int i = 0; i < n; i++) {
    if (strcmp(names[i], "DejaVuSerif") == 0) found = true;
    if (strcmp(names[i], "NotAFamily") == 0) ghost = true;
    if (strcmp(names[i], "Nested") == 0) nested = true;
    if (strcmp(names[i], "Loose") == 0) loose = true;
  }
  if (!found || ghost || !nested || !loose) {
    printf("CARD FAMILY FAIL: %d families -- plain %d, nested %d, loose %d, empty folder %d\n", n,
           found, nested, loose, ghost);
    abort();
  }
  // And each of those shapes has to load, not merely be listed.
  for (const char* fam : {"Nested", "Loose"}) {
    if (!cardfonts::useUniversal(fam)) {
      printf("CARD FAMILY FAIL: %s listed but would not load\n", fam);
      abort();
    }
  }
  cardfonts::noneUniversal();

  if (!cardfonts::useUniversal("DejaVuSerif") || strcmp(cardfonts::universal(), "DejaVuSerif") != 0) {
    printf("CARD FAMILY FAIL: the family would not load\n");
    abort();
  }
  // 8 pt is a 19 px line, 12 pt is 29 and 18 pt is 44. The 18 px box takes the
  // 19; the 24 px box takes it too, because 29 would overrun and airy beats
  // colliding; 32 takes the 29 and 44 takes the 44.
  struct { int box, want; } expect[] = {{18, 19}, {24, 19}, {32, 29}, {44, 44}};
  for (auto& e : expect) {
    const int got = gfx::cardFaceLine(e.box, false);
    if (got != e.want) {
      printf("CARD FAMILY FAIL: the %d px box got a %d px line, wanted %d\n", e.box, got, e.want);
      abort();
    }
  }

  // A family that is not there changes nothing -- the face that was working a
  // moment ago is still the face.
  if (cardfonts::useUniversal("NoSuchFamily") || strcmp(cardfonts::universal(), "DejaVuSerif") != 0) {
    printf("CARD FAMILY FAIL: a missing family disturbed the loaded one\n");
    abort();
  }

  // Two faces at once: the firmware's own, and one app's. The whole point is
  // that they do not leak into each other -- a novel in a serif must not put
  // the settings page in a serif too, and the app's face must vanish the
  // moment the app stops asking for it.
  {
    std::vector<uint8_t> sans;
    if (!readFixture(sans)) abort();
    sdcard::hostPutCardFile("/.fonts/DejaVuSans/DejaVuSans_8.cpfont", sans.data(),
                            (int)sans.size());
    if (!cardfonts::useContent("DejaVuSans") ||
        strcmp(cardfonts::content(), "DejaVuSans") != 0) {
      printf("CARD FAMILY FAIL: the content family would not load\n");
      abort();
    }
    const char* kLine = "Hamburgefonstiv";
    gfx::contentFace(false);
    const int uiWidth = gfx::textWidth(kLine, 18, false, 0);
    gfx::contentFace(true);
    const int appWidth = gfx::textWidth(kLine, 18, false, 0);
    gfx::contentFace(false);
    const int backAgain = gfx::textWidth(kLine, 18, false, 0);
    if (uiWidth == appWidth || backAgain != uiWidth) {
      printf("CARD FAMILY FAIL: ui %d, content %d, back to %d\n", uiWidth, appWidth, backAgain);
      abort();
    }
    // Dropping the app's face leaves the firmware's alone. The other order --
    // leaving an app and finding the menus in the book's font -- is the bug
    // this separation exists to prevent.
    cardfonts::noneContent();
    gfx::contentFace(true);
    const int afterDrop = gfx::textWidth(kLine, 18, false, 0);
    gfx::contentFace(false);
    if (afterDrop != uiWidth || cardfonts::content()[0] || !cardfonts::universal()[0]) {
      printf("CARD FAMILY FAIL: dropping the app face disturbed the firmware's\n");
      abort();
    }
  }

  cardfonts::noneUniversal();
  if (gfx::cardFaceLive(false) || cardfonts::universal()[0]) {
    printf("CARD FAMILY FAIL: the baked faces did not come back\n");
    abort();
  }
  printf("card family ok (three sizes into four boxes, and an app face that stays in its app)\n");
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

static std::string g_syncedBook;  // the book the sync guard synced

int main() {
  // The common CJK sets left the app for the base_font partition (a TFP2
  // pack, tools/tfp2.py). The device maps that partition at boot; here it goes
  // in as a /fonts file before anything draws. Without it every Chinese,
  // Japanese and Korean check below would be measuring the missing-glyph box.
  {
    const char* path = getenv("TOYBOX_BASE_PACK");
    if (!path) path = "../../docs/firmware/base_font.tfp";
    FILE* f = fopen(path, "rb");
    if (!f) {
      printf("BASE FAIL: no %s (python3 tools/tfp2.py base ...)\n", path);
      abort();
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string blob((size_t)n, 0);
    fread(&blob[0], 1, (size_t)n, f);
    fclose(f);
    tfs::hostFs()["/fonts/base_font.tfp"] = blob;
    const int packFaces = gfx::loadFontPacks();
    printf("base pack loaded (%ld KB, %d pack faces)\n", n / 1024, packFaces);
  }
  epd.begin();
  prefs.begin("toybox", false);
  // The reader's first-open guide eats the first touch; every walk below
  // opens books as somebody who has seen it. Its own check clears the flag.
  prefs.putBool("ep_guide", true);
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
  prefs.putInt("ms_w0", 3);
  prefs.putInt("ms_l0", 2);
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
  checkCardFace();
  checkCardFamily();
  checkPreparedFonts();
  checkCardBaseline();
  checkThaiCardMarks();
  checkLockOverPage();
  renderChargeDash();
  checkInkBits();
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
      if (f == 0) {  // C4: PLAY's second page, behind the header's ">"
        setScreen("hub_play_2");
        toybox.onTap(EPD_W - 24 - 75, hubui::FOLDER_TOP + 3 * hubui::ROW_STEP + 12 + 38);
      }
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
    toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);  // the first tile: wordle
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
  tapAct(setui::ACT_APPS);

  // Hide four apps through the screen itself, so the hub below reflows around
  // exactly what a finger would have hidden. STUDY's heading sits at
  // 92 + 26 + 8*52 + 14 = 548 with UTILITY's eight rows above it (v2 added
  // CALENDAR and FOCUS).
  // C5: two pages -- PLAY on the first, UTILITY and STUDY on the second.
  g_dumpEnabled = false;
  (void)setRow;
  tapRect(setui::appsItemRect(0, 0));   // Wordle
  tapRect(setui::appsItemRect(0, 2));   // 2048
  tapRect(setui::appsItemRect(0, 13));  // Dice, now in PLAY (right column)
  g_dumpEnabled = true;
  setScreen("settings_apps_2");
  tapRect(setui::appsNextRect());
  g_dumpEnabled = false;
  tapRect(setui::appsItemRect(2, 2));   // Notes
  toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // back up to the buttons page
  // v2: Sound is a switch, Volume is Low / Mid / High. Off, on (at Mid), a
  // level by its segment, and the screen below drawn muted.
  const int levels = stickyHost.soundLevels();
  tapRect(setui::tabRect(setui::TAB_DEVICE));
  if (appvis::shown() != appvis::COUNT - 1 - 4) {  // no tile for tool 9
    printf("SETTINGS FAIL: taps did not land (%d shown)\n", appvis::shown());
    abort();
  }
  if (stickyHost.soundLevel() == 0) tapAct(setui::ACT_SOUND);
  tapRect(setui::volSeg(2));
  if (stickyHost.soundLevel() != (levels > 3 ? 3 : levels - 1)) {
    printf("SETTINGS FAIL: High landed on %d\n", stickyHost.soundLevel());
    abort();
  }
  tapRect(setui::volSeg(0));
  if (stickyHost.soundLevel() != 1) {
    printf("SETTINGS FAIL: Low landed on %d\n", stickyHost.soundLevel());
    abort();
  }
  tapAct(setui::ACT_SOUND);
  if (stickyHost.soundLevel() != 0) {
    printf("SETTINGS FAIL: the switch did not mute\n");
    abort();
  }
  // C6: battery care is on out of the box, and its switch flips it both ways.
  if (!stickyHost.batteryCare()) {
    printf("SETTINGS FAIL: battery care should default on\n");
    abort();
  }
  tapAct(setui::ACT_CARE);
  if (stickyHost.batteryCare()) {
    printf("SETTINGS FAIL: battery care switch did not turn off\n");
    abort();
  }
  {
    const bool wasDump = g_dumpEnabled;
    g_dumpEnabled = true;
    setScreen("settings_device");
    tapAct(setui::ACT_CARE);
    stickyHost.refresh(true);
    g_dumpEnabled = wasDump;
  }
  if (!stickyHost.batteryCare()) {
    printf("SETTINGS FAIL: battery care switch did not turn back on\n");
    abort();
  }
  // C6: the dashboard row opens its page; the switch there flips both ways,
  // and a picture row is a choice.
  if (!stickyHost.chargeDash() || stickyHost.chargePic() != 1) {
    printf("SETTINGS FAIL: dashboard should default on, with the wallpaper\n");
    abort();
  }
  {
    const bool wasDump = g_dumpEnabled;
    g_dumpEnabled = true;
    setScreen("settings_dashboard");
    tapAct(setui::ACT_DASH);
    tapRect(setui::dashSwitchRow());
    if (stickyHost.chargeDash()) {
      printf("SETTINGS FAIL: dashboard switch did not turn off\n");
      abort();
    }
    tapRect(setui::dashSwitchRow());
    tapRect(setui::dashBorderRow());
    if (stickyHost.chargePicBorder()) {
      printf("SETTINGS FAIL: picture border switch did not turn off\n");
      abort();
    }
    tapRect(setui::dashBorderRow());
    tapRect(setui::dashPicRow(3));
    if (!stickyHost.chargeDash() || stickyHost.chargePic() != 3) {
      printf("SETTINGS FAIL: dashboard page taps did not land\n");
      abort();
    }
    tapRect(setui::dashPicRow(1));
    stickyHost.refresh(true);
    g_dumpEnabled = wasDump;
    toybox.onTap(16, 16);  // the corner: back to the DEVICE tab
  }

  // D: Reading sync page. Every switch flips its pref; with Auto sync off the
  // two "when" rows are dimmed and a tap on them does nothing.
  {
    const bool wasDump = g_dumpEnabled;
    auto& pr = stickyHost.prefs();
    g_dumpEnabled = true;
    setScreen("settings_sync");
    tapAct(setui::ACT_SYNC);
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(setui::syncRow(1));
    if (pr.getBool(rhl::K_HL, true)) {
      printf("SETTINGS FAIL: highlights switch did not turn off\n");
      abort();
    }
    tapRect(setui::syncRow(1));
    tapRect(setui::syncRow(2));
    if (pr.getBool(rhl::K_AUTO, true) || rhl::onOpen(pr) || rhl::onClose(pr)) {
      printf("SETTINGS FAIL: auto sync off should switch off both 'when' rows\n");
      abort();
    }
    tapRect(setui::syncRow(3));
    if (!pr.getBool(rhl::K_OPEN, true)) {
      printf("SETTINGS FAIL: a dimmed row still flipped\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_sync_auto_off");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(setui::syncRow(2));
    tapRect(setui::syncRow(4));
    if (!rhl::onOpen(pr) || rhl::onClose(pr)) {
      printf("SETTINGS FAIL: 'when it closes' did not turn off on its own\n");
      abort();
    }
    tapRect(setui::syncRow(4));
    if (!rhl::hl(pr) || !rhl::autoOn(pr) || !rhl::onOpen(pr) || !rhl::onClose(pr)) {
      printf("SETTINGS FAIL: sync switches not back to on\n");
      abort();
    }
    g_dumpEnabled = wasDump;
    toybox.onTap(16, 16);
  }

  // --- v2 setup steps ---------------------------------------------------------
  // From Settings > DEVICE > Setup: seven steps, each drawn; the How step's
  // three ways, the home tiles, sleep and beep write what they say; back on
  // step 1 returns to the settings root.
  {
    const bool wasDump = g_dumpEnabled;
    g_dumpEnabled = true;
    auto back = [] { toybox.onTap(BACK_W / 2, TOPBAR_H / 2); };
    tapAct(setui::ACT_SETUP);
    setScreen("setup_language");
    stickyHost.refresh(true);
    tapRect(setui::setupNext());
    setScreen("setup_how");
    stickyHost.refresh(true);
    tapRect(setui::setupRow(0));  // On this device
    setScreen("setup_font");
    stickyHost.refresh(true);
    tapRect(setui::setupNext());
    setScreen("setup_home");
    tapRect(setui::setupHomeTile(homestyle::TODAY));
    if (homestyle::get(stickyHost.prefs()) != homestyle::TODAY) {
      printf("SETUP FAIL: the Today tile did not set the home style\n");
      abort();
    }
    stickyHost.refresh(true);
    tapRect(setui::setupHomeTile(homestyle::WALL));
    tapRect(setui::setupNext());
    setScreen("setup_sleep");
    tapRect(setui::setupRow(2));  // 15 min
    if (lock::config().sleepIdx != 3) {
      printf("SETUP FAIL: 15 min wrote sleepIdx %d\n", lock::config().sleepIdx);
      abort();
    }
    tapRect(setui::setupRow(1));  // back to 5 min, the default
    stickyHost.refresh(true);
    tapRect(setui::setupNext());
    setScreen("setup_beep");
    tapRect(setui::setupRow(2));  // Mid
    if (stickyHost.soundLevel() != 2) {
      printf("SETUP FAIL: Mid landed on %d\n", stickyHost.soundLevel());
      abort();
    }
    stickyHost.refresh(true);
    tapRect(setui::setupNext());
    setScreen("setup_wifi");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    for (int i = 0; i < setui::SETUP_STEPS - 1; i++) back();  // to step 1
    back();                                                    // to the settings root
    if (!toybox.hostInSettings() || toybox.hostSettings().inSetup()) {
      printf("SETUP FAIL: back from step 1 did not land on the settings root\n");
      abort();
    }
    // A new device: Done on the last step goes to the hub.
    auto walk = [] {
      tapRect(setui::setupNext());  // language
      tapRect(setui::setupRow(0));  // how: on this device
      for (int i = setui::SU_FONT; i < setui::SU_WIFI; i++) tapRect(setui::setupNext());
    };
    auto atHub = [] { return !toybox.hostInSettings() && !toybox.hostInApp(); };
    toybox.openSetup(true);
    walk();
    tapRect(setui::setupDone());
    if (!atHub()) {
      printf("SETUP FAIL: a new device's setup did not end at the hub\n");
      abort();
    }
    // Skip ends it at once.
    toybox.openSetup(true);
    tapRect(setui::setupNext());
    tapRect(setui::setupRow(2));
    if (!atHub()) {
      printf("SETUP FAIL: Skip did not end the setup at the hub\n");
      abort();
    }
    // With my phone: the clock page, and back from it ends the setup.
    toybox.openSetup(true);
    tapRect(setui::setupNext());
    tapRect(setui::setupRow(1));
    if (!toybox.hostInSettings() || toybox.hostSettings().inSetup()) {
      printf("SETUP FAIL: With my phone did not open the clock page\n");
      abort();
    }
    back();
    if (!atHub()) {
      printf("SETUP FAIL: back from the phone page did not end the setup\n");
      abort();
    }
    toybox.openSettings();
    tapRect(setui::tabRect(setui::TAB_DEVICE));
    tapAct(setui::ACT_SOUND);  // back to muted, the state the walk expects
    if (stickyHost.soundLevel() != 0) tapAct(setui::ACT_SOUND);
    printf("setup steps ok (%d steps, how/home/sleep/beep write, skip and phone end)\n",
           setui::SETUP_STEPS);
    g_dumpEnabled = wasDump;
  }

  // --- v2 home styles: Books, Calendar and Today; Settings > Home screen; the
  // upgrade popup over home. ------------------------------------------------------
  {
    const bool wasDump = g_dumpEnabled;
    Preferences& pr = stickyHost.prefs();
    // A streak: today and the two days before, so the week row has three ink.
    int y = 0, mo = 0, d = 0;
    stickyHost.clockDate(y, mo, d);
    for (int ago = 2; ago >= 0; ago--) {
      int yy, mm, dd;
      cal::fromDayNum(cal::dayNum(y, mo, d) - ago, yy, mm, dd);
      homestyle::noteRead(pr, yy, mm, dd);
    }
    if (homestyle::streak(pr, y, mo, d) != 3 || !homestyle::readOn(pr, y, mo, d, 2) ||
        homestyle::readOn(pr, y, mo, d, 3)) {
      printf("HOME FAIL: streak %d after three days\n", homestyle::streak(pr, y, mo, d));
      abort();
    }
    g_dumpEnabled = true;
    toybox.openSettings();
    tapRect(setui::tabRect(setui::TAB_LOOK));
    setScreen("settings_look_home_row");
    stickyHost.refresh(true);
    tapAct(setui::ACT_HOME);
    setScreen("settings_home_screen");
    stickyHost.refresh(true);
    tapRect(setui::homeStyleTile(homestyle::BOOKS));
    if (homestyle::get(pr) != homestyle::BOOKS) {
      printf("HOME FAIL: the Books tile did not set the style\n");
      abort();
    }
    g_dumpEnabled = false;
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // out of settings, to home
    g_dumpEnabled = true;
    setScreen("home_books");
    stickyHost.refresh(true);
    homestyle::set(pr, homestyle::CALENDAR);
    setScreen("home_calendar");
    stickyHost.refresh(true);
    toybox.onTap(hubui::calNext().x + 20, hubui::calNext().y + 20);
    setScreen("home_calendar_next_month");
    stickyHost.refresh(true);
    toybox.onTap(hubui::calPrev().x + 20, hubui::calPrev().y + 20);
    homestyle::set(pr, homestyle::TODAY);
    setScreen("home_today");
    stickyHost.refresh(true);
    // The upgrade popup: Later hides it; Open setup opens setup and marks it.
    pr.remove("su_seen");
    toybox.offerSetup();
    setScreen("home_upgrade_popup");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(hubui::upLater());
    if (toybox.hostInSettings() || pr.getBool("su_seen", false)) {
      printf("HOME FAIL: Later did not just close the popup\n");
      abort();
    }
    toybox.offerSetup();
    tapRect(hubui::upOpen());
    if (!toybox.hostInSettings() || !toybox.hostSettings().inSetup() || !pr.getBool("su_seen", false)) {
      printf("HOME FAIL: Open setup did not open setup and mark it seen\n");
      abort();
    }
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    toybox.offerSetup();  // seen: no popup any more
    homestyle::set(pr, homestyle::WALL);
    if (!toybox.hostInSettings()) toybox.openSettings();
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    printf("home styles ok (books, calendar, today; settings page; upgrade popup)\n");
    {  // the phone page's "Your Sticky" card
      char j[256];
      cweb::infoJson(j, sizeof(j), "d-test9", 82, "Noto \"Sans\"", "\xE0\xB8\x9A\xE0\xB9\x89\xE0\xB8\xB2\xE0\xB8\x99 5G");
      if (!strstr(j, "\"bat\":82") || !strstr(j, "Noto \\\"Sans\\\"") || !strstr(j, "\"fw\":\"d-test9\"")) {
        printf("PHONE FAIL: info json %s\n", j);
        abort();
      }
      printf("phone page info ok %s\n", j);
    }
    g_dumpEnabled = wasDump;
  }

  // --- v2 calendar: two items typed on the keyboard, the due popup over home
  // (now, and three days ahead), Done and Remind later. -------------------------
  {
    const bool wasDump = g_dumpEnabled;
    tfs::remove(cal::PATH);
    auto typeText = [](CalendarTool* ct, const char* text, osk::State& st) {
      (void)ct;
      for (const char* p = text; *p; p++) {
        bool hit = false;
        for (int attempt = 0; attempt < 2 && !hit; attempt++) {
          for (int row = 0; row < osk::ROWS && !hit; row++)
            for (int col = 0;; col++) {
              TRect r;
              int code;
              if (!osk::keyAt(st, row, col, r, code)) break;
              if (code == (unsigned char)*p || (*p == ' ' && code == osk::K_SPACE)) {
                toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
                osk::tap(st, nullptr, 0, -1, -1);  // no-op, keeps st in step below
                if (*p != ' ' && code >= 'A' && code <= 'Z') st.shift = false;
                else if (*p != ' ') st.shift = false;
                hit = true;
                break;
              }
            }
          if (!hit) {  // the letter is on the other case: shift and retry
            TRect r;
            int code;
            osk::keyAt(st, 2, 0, r, code);
            toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
            st.shift = !st.shift;
          }
        }
        if (!hit) {
          printf("CALENDAR FAIL: no key for '%c'\n", *p);
          abort();
        }
      }
      TRect r;
      int code;
      osk::keyAt(st, 3, 2, r, code);  // Done
      toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
    };
    g_dumpEnabled = true;
    setScreen("tool_calendar");
    toybox.open(false, 12);
    CalendarTool* ct = static_cast<CalendarTool*>(toybox.hostActive());
    // One today at 09:00 (the clock says 09:41): due now.
    tapRect(calui::ADD);
    setScreen("tool_calendar_type");
    osk::State st;
    typeText(ct, "Dentist", st);
    setScreen("tool_calendar_add");
    stickyHost.refresh(true);
    tapRect(calui::timeBox());  // type 09:00
    for (const int k : {numpad::ZERO, 8, numpad::DONE}) tapRect(calui::padKey(k));
    if (ct->hostDraftHour() != 9 || ct->hostDraftMinute() != 0) {
      printf("CALENDAR FAIL: keypad gave %02d:%02d, not 09:00\n", ct->hostDraftHour(),
             ct->hostDraftMinute());
      abort();
    }
    tapRect(calui::SAVE);
    // Cancel keeps nothing.
    {
      const int before = ct->hostCount();
      tapRect(calui::ADD);
      osk::State stc;
      typeText(ct, "Nope", stc);
      tapRect(calui::CANCEL_BTN);
      if (ct->hostCount() != before || ct->hostPage() != 0) {
        printf("CALENDAR FAIL: Cancel kept an item (%d -> %d) or stayed (page %d)\n", before,
               ct->hostCount(), ct->hostPage());
        abort();
      }
    }
    // One three days on, popping up three days ahead.
    tapRect(calui::ADD);
    osk::State st2;
    typeText(ct, "Mum visits", st2);
    for (int k = 0; k < 3; k++) tapRect(calui::aPlus(0, 1));
    for (int k = 0; k < 3; k++) tapRect(calui::aPlus(4, 1));
    tapRect(calui::SAVE);
    // Edit: open Mum, type 18:30 on the keypad, save; it keeps its place.
    {
      tapRect(calui::itemRow(0));  // the selected day (13th) lists Mum
      tapRect(calui::EDIT_BTN);
      tapRect(calui::timeBox());  // the time: type it
      g_dumpEnabled = true;
      setScreen("tool_calendar_time");
      for (const int k : {0, 7, 2, 10}) tapRect(calui::padKey(k));  // 1 8 3 0
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      tapRect(calui::padKey(11));
      tapRect(calui::SAVE);
      g_dumpEnabled = true;
    }
    // An all-day item that repeats every week until a date, set on the
    // repeat page; and a yearly one. Checked against the store's own rules.
    {
      tapRect(calui::ADD);
      osk::State st3;
      typeText(ct, "Gym", st3);
      tapRect(calui::allDayChip());
      tapRect(calui::repeatRow());
      g_dumpEnabled = true;
      setScreen("tool_calendar_repeat");
      tapRect(calui::repOpt(2));  // every week
      tapRect(calui::endSeg(1));  // ends on a date: typed, 2026 / 9 / 13
      setScreen("tool_calendar_until");
      for (const int k : {1, numpad::ZERO, 1, 5, numpad::DONE, 8, numpad::DONE, 0}) tapRect(calui::padKey(k));
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      for (const int k : {2, numpad::DONE}) tapRect(calui::padKey(k));
      if (ct->hostDraftUntil() != 20260913u) {
        printf("CALENDAR FAIL: end date typed as %u\n", (unsigned)ct->hostDraftUntil());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_calendar_repeat2");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      tapRect(calui::REP_DONE);
      g_dumpEnabled = true;
      setScreen("tool_calendar_allday");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      tapRect(calui::SAVE);
      cal::Item gym{};
      for (int i = 0; i < ct->hostCount(); i++)
        if (!strcmp(ct->hostTitle(i), "Gym")) gym = ct->hostItem(i);
      if (!gym.allDay || gym.repeat != cal::REPEAT_WEEKLY || !gym.until ||
          !cal::on(gym, 2026, 8, 20) || !cal::on(gym, 2026, 9, 10) || cal::on(gym, 2026, 9, 17)) {
        printf("CALENDAR FAIL: Gym allDay %d repeat %d until %u\n", gym.allDay, gym.repeat,
               (unsigned)gym.until);
        abort();
      }
      cal::Item bday{};
      bday.year = 2026;
      bday.month = 8;
      bday.day = 20;
      bday.repeat = cal::REPEAT_YEARLY;
      if (!cal::on(bday, 2031, 8, 20) || cal::on(bday, 2031, 8, 21) || cal::on(bday, 2025, 8, 20)) {
        printf("CALENDAR FAIL: a yearly item lands on the wrong days\n");
        abort();
      }
      // Remove Gym again so the counts below hold.
      for (int i = 0; i < ct->hostCount(); i++)
        if (!strcmp(ct->hostTitle(i), "Gym")) ct->hostRemove(i);
      g_dumpEnabled = true;
    }
    // The month's name opens the picker: 2027, March; Today comes back.
    {
      tapRect(calui::TITLE_TAP);
      g_dumpEnabled = true;
      setScreen("tool_calendar_pick");
      tapRect(calui::YEAR_TAP);  // the year: a page of twelve, 2024 - 2035
      setScreen("tool_calendar_years");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      tapRect(calui::monthTile(3));  // 2027
      tapRect(calui::monthTile(2));
      if (ct->hostViewYear() != 2027 || ct->hostViewMonth() != 3) {
        printf("CALENDAR FAIL: the picker went to %d-%d\n", ct->hostViewYear(), ct->hostViewMonth());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_calendar_away");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      toybox.onButton(SideBtn::Up);    // April
      toybox.onButton(SideBtn::Down);
      toybox.onButton(SideBtn::Down);  // February
      if (ct->hostViewMonth() != 2) {
        printf("CALENDAR FAIL: the side buttons landed on month %d\n", ct->hostViewMonth());
        abort();
      }
      tapRect(calui::TODAY);
      if (ct->hostViewYear() != 2026 || ct->hostViewMonth() != 8) {
        printf("CALENDAR FAIL: Today went to %d-%d\n", ct->hostViewYear(), ct->hostViewMonth());
        abort();
      }
      g_dumpEnabled = true;
    }
    if (ct->hostCount() != 2 || strcmp(ct->hostTitle(1), "Mum visits") != 0 ||
        ct->hostHour(1) != 18 || ct->hostMinute(1) != 30) {
      printf("CALENDAR FAIL: the edit gave %s at %02d:%02d\n", ct->hostTitle(1), ct->hostHour(1),
             ct->hostMinute(1));
      abort();
    }
    if (ct->hostCount() != 2 || strcmp(ct->hostTitle(0), "Dentist") != 0 ||
        strcmp(ct->hostTitle(1), "Mum visits") != 0) {
      printf("CALENDAR FAIL: %d items ('%s', '%s')\n", ct->hostCount(),
             ct->hostCount() > 0 ? ct->hostTitle(0) : "", ct->hostCount() > 1 ? ct->hostTitle(1) : "");
      abort();
    }
    setScreen("tool_calendar_month");
    stickyHost.refresh(true);
    // Home: Dentist is due; Done puts it away and Mum (in 3 days) comes up.
    setScreen("hub_due");
    toybox.goHub();
    toybox.hostHub().goHome();
    stickyHost.refresh(true);
    if (toybox.hostHub().dueItem() != 0) {
      printf("CALENDAR FAIL: home shows due item %d, wanted 0\n", toybox.hostHub().dueItem());
      abort();
    }
    toybox.onTap(240, 120);  // anywhere else on home: swallowed
    if (toybox.hostInApp() || toybox.hostInSettings()) {
      printf("CALENDAR FAIL: a tap beside the popup went through it\n");
      abort();
    }
    setScreen("hub_due_ahead");
    tapRect(hubui::dueDone(toybox.hostHub().hostDueBottom()));
    if (toybox.hostHub().dueItem() != 1) {
      printf("CALENDAR FAIL: after Done, home shows %d, wanted 1\n", toybox.hostHub().dueItem());
      abort();
    }
    g_dumpEnabled = false;
    tapRect(hubui::dueLater(toybox.hostHub().hostDueBottom()));
    if (toybox.hostHub().dueItem() != -1) {
      printf("CALENDAR FAIL: Remind later left %d up\n", toybox.hostHub().dueItem());
      abort();
    }
    cal::snoozed() = 0;
    tfs::remove(cal::PATH);
    printf("calendar ok (typed titles, keypad time, edit, due now + 3 days ahead, done, later)\n");

    // --- v2 focus: no countdown, a tap on Stop does nothing, a hold does.
    g_dumpEnabled = true;
    setScreen("tool_focus");
    toybox.open(false, 13);
    FocusTool* ft = static_cast<FocusTool*>(toybox.hostActive());
    // Type 40 on the keypad, save it as a preset: it goes first.
    tapRect(focusui::VALUE);
    setScreen("tool_focus_type");
    for (const int k : {3, numpad::ZERO}) tapRect(calui::padKey(k));  // 4 0
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(calui::padKey(numpad::DONE));
    tapRect(focusui::SAVE_PRESET);
    tapRect(focusui::toneSeg(1));  // Slow
    if (ft->hostTone() != 1) {
      printf("FOCUS FAIL: end tone did not switch to Slow\n");
      abort();
    }
    if (ft->hostMin() != 40 || ft->hostPreset(0) != 40 || ft->hostPreset(1) != 15) {
      printf("FOCUS FAIL: typed %d, presets %d %d\n", ft->hostMin(), ft->hostPreset(0), ft->hostPreset(1));
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_focus_set");
    stickyHost.refresh(true);
    setScreen("tool_focus_run");
    tapRect(focusui::START);
    g_dumpEnabled = false;
    tapRect(focusui::STOP);
    if (ft->hostStage() != 1) {
      printf("FOCUS FAIL: a tap on Stop ended it (stage %d)\n", ft->hostStage());
      abort();
    }
    toybox.onHold(focusui::STOP.x + 20, focusui::STOP.y + 20);
    if (ft->hostStage() != 0) {
      printf("FOCUS FAIL: holding Stop did not end it (stage %d)\n", ft->hostStage());
      abort();
    }
    tapRect(focusui::START);
    ft->hostSkipToSecondThird();
    toybox.tick();  // the dots move on: a quiet repaint
    g_dumpEnabled = true;
    setScreen("tool_focus_run_mid");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onBothHeld();  // both side buttons, held: stops it
    if (ft->hostStage() != 0) {
      printf("FOCUS FAIL: holding both side buttons did not stop it\n");
      abort();
    }
    tapRect(focusui::START);
    ft->hostSkipToEnd();
    g_dumpEnabled = true;
    setScreen("tool_focus_done");
    toybox.tick();
    g_dumpEnabled = false;
    if (ft->hostStage() != 2) {
      printf("FOCUS FAIL: the end did not come (stage %d)\n", ft->hostStage());
      abort();
    }
    toybox.goHub();
    printf("focus ok (tap ignored, hold stops, end beeps)\n");
    g_dumpEnabled = wasDump;
    toybox.openSettings();
  }
  setScreen("settings_edited");
  g_dumpEnabled = true;
  stickyHost.refresh(true);

  // --- Settings > Update & about ---------------------------------------------
  // Every screen of the update page against the fake device: the page, a
  // rollback notice, a check that finds nothing and one that finds a release,
  // the install with its bar, a failed install, low battery, the computer
  // page and its lock-out, and the reset that moved here. Each must route
  // the right taps and fit the panel (the overflow detector watches them all).
  {
    ota::hostDevice() = &g_fakeOta;
    auto& st = toybox.hostSettings();
    auto shot = [](const char* name) {
      g_dumpEnabled = true;
      setScreen(name);
      stickyHost.refresh(true);
      g_dumpEnabled = false;
    };
    auto fail = [](const char* why) {
      printf("UPDATE FAIL: %s\n", why);
      abort();
    };
    g_dumpEnabled = false;
    snprintf(g_fakeOta.rolled, sizeof(g_fakeOta.rolled), "v2.0.1");
    tapAct(setui::ACT_UPDATE);
    if (st.hostPage() != 9) fail("the row did not open the update page");
    shot("update_rolled_back");  // no home wifi saved yet, and the rollback notice once
    // No home wifi: CHECK says what to do instead of trying.
    tapRect(setui::updCheckRect());
    if (st.hostUpStage() != 0 || g_fakeOta.checks) fail("checked with no wifi saved");
    shot("update_no_wifi");

    homenet::saveWifi(stickyHost.prefs(), "\xe0\xb8\x9a\xe0\xb9\x89\xe0\xb8\xb2\xe0\xb8\x99 5G", "password1");
    shot("update_page");

    // Reset stats: two taps, the first only asks.
    tapRect(setui::updResetRect());
    shot("update_reset_armed");
    tapRect(TRect{16, 760, 10, 10});  // anywhere else: disarms
    shot("update_reset_disarmed");

    // Up to date.
    g_fakeOta.result = ota::Check{};
    snprintf(g_fakeOta.result.remote.version, ota::VER_MAX, "v2.0.0-beta.1");
    tapRect(setui::updCheckRect());
    if (st.hostUpStage() != 1) fail("CHECK did not start");
    shot("update_checking");
    toybox.tick();
    if (st.hostUpStage() != 2 || g_fakeOta.checks != 1) fail("the check did not run");
    shot("update_up_to_date");
    tapRect(setui::updInstallRect());
    if (g_fakeOta.installs) fail("INSTALL pressed on an up-to-date page");
    tapRect(setui::updDoneRect());
    if (st.hostUpStage() != 0) fail("DONE did not go back");

    // A check that fails says why.
    g_fakeOta.checkErr = ota::Err::NoAnswer;
    tapRect(setui::updCheckRect());
    toybox.tick();
    shot("update_check_failed");
    tapRect(setui::updDoneRect());
    g_fakeOta.checkErr = ota::Err::Ok;

    // A release, with the fonts.
    ota::Check& r = g_fakeOta.result;
    snprintf(r.remote.version, ota::VER_MAX, "v2.0.0-beta.12");
    snprintf(r.remote.date, sizeof(r.remote.date), "20 Nov 2026");
    r.remote.app.bytes = 2412544;
    r.remote.base.bytes = 1103072;
    r.needApp = r.needBase = true;
    tapRect(setui::updCheckRect());
    toybox.tick();
    shot("update_available");

    // Low battery: no INSTALL to press.
    const int batWas = sensors::batteryPercent();
    const bool chgWas = sensors::charging();
    sensors::hostSetBattery(12, false);
    shot("update_low_battery");
    tapRect(setui::updInstallRect());
    if (g_fakeOta.installs) fail("installed on 12% battery");
    sensors::hostSetBattery(80, false);

    // Install, failing.
    g_fakeOta.installErr = ota::Err::Download;
    tapRect(setui::updInstallRect());
    if (st.hostUpStage() != 3) fail("INSTALL did not start");
    g_dumpEnabled = true;
    setScreen("update_installing");  // painted by the progress callback itself
    toybox.tick();
    g_dumpEnabled = false;
    if (g_fakeOta.installs != 1 || st.hostUpStage() != 4) fail("the install did not run");
    shot("update_failed");
    if (g_fakeOta.restarts) fail("restarted after a failed install");
    tapRect(setui::updDoneRect());

    // Install, working: RESTARTING, then the restart.
    g_fakeOta.installErr = ota::Err::Ok;
    tapRect(setui::updCheckRect());
    toybox.tick();
    tapRect(setui::updInstallRect());
    toybox.tick();
    shot("update_restarting");
    toybox.tick();
    if (g_fakeOta.restarts != 1) fail("no restart after a good install");
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // the real device is gone by now
    tapAct(setui::ACT_UPDATE);

    // From a computer.
    g_fakeOta.lanScript = ota::Lan{};
    g_fakeOta.lanScript.phase = ota::Lan::Waiting;
    snprintf(g_fakeOta.lanScript.url, sizeof(g_fakeOta.lanScript.url), "http://toybox.local");
    snprintf(g_fakeOta.lanScript.ip, sizeof(g_fakeOta.lanScript.ip), "192.168.1.123");
    snprintf(g_fakeOta.lanScript.code, sizeof(g_fakeOta.lanScript.code), "480126");
    tapRect(setui::updLanRect());
    shot("update_lan_joining");
    toybox.tick();
    if (!g_fakeOta.lanUp || st.hostUpStage() != 5) fail("the computer page did not open");
    shot("update_lan_code");
    g_fakeOta.lanNow.phase = ota::Lan::Receiving;
    g_fakeOta.lanNow.done = 1300000;
    g_fakeOta.lanNow.total = 2412800;
    snprintf(g_fakeOta.lanNow.incoming, ota::VER_MAX, "v2.0.0-beta.12");
    toybox.tick();
    shot("update_lan_receiving");
    tapRect(setui::updDoneRect());
    if (!g_fakeOta.lanUp) fail("DONE closed the page mid-upload");
    g_fakeOta.lanNow.phase = ota::Lan::Waiting;
    g_fakeOta.lanNow.err = ota::Err::NotToybox;
    toybox.tick();
    shot("update_lan_refused");
    g_fakeOta.lanNow.phase = ota::Lan::Failed;
    g_fakeOta.lanNow.err = ota::Err::WrongCode;
    toybox.tick();
    shot("update_lan_locked");
    tapRect(setui::updDoneRect());
    if (g_fakeOta.lanUp) fail("DONE left the server running");
    // Back out from the computer page: the server goes with the screen.
    tapRect(setui::updLanRect());
    toybox.tick();
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    if (g_fakeOta.lanUp || st.hostPage() != 0) fail("back left the server running");
    // Never got onto the network.
    tapAct(setui::ACT_UPDATE);
    g_fakeOta.lanScript.phase = ota::Lan::Failed;
    g_fakeOta.lanScript.code[0] = 0;
    g_fakeOta.lanScript.err = ota::Err::WifiNotFound;
    tapRect(setui::updLanRect());
    toybox.tick();
    shot("update_lan_no_wifi");
    tapRect(setui::updDoneRect());
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);

    // From the SD card: no card, nothing there, a file that is not ours, then
    // a release with a pack -- low battery, a failed install, a good one.
    tapAct(setui::ACT_UPDATE);
    g_fakeOta.cardScript = ota::Card{};  // NoCard
    tapRect(setui::updCardRect());
    if (st.hostUpStage() != 7) fail("FROM SD CARD did not start");
    shot("update_card_reading");
    toybox.tick();
    if (st.hostUpStage() != 8 || g_fakeOta.cardScans != 1) fail("the card was not read");
    shot("update_card_none");
    tapRect(TRect{16, 712, SCREEN_W - 32, 56});
    if (st.hostUpStage() != 0) fail("DONE did not leave the card screen");
    g_fakeOta.cardScript.state = ota::Card::Nothing;
    tapRect(setui::updCardRect());
    toybox.tick();
    shot("update_card_empty");
    tapRect(TRect{16, 712, SCREEN_W - 32, 56});
    g_fakeOta.cardScript.state = ota::Card::NotToybox;
    snprintf(g_fakeOta.cardScript.bad, sizeof(g_fakeOta.cardScript.bad), "toybox-app.bin");
    tapRect(setui::updCardRect());
    toybox.tick();
    shot("update_card_not_ours");
    tapRect(setui::updCardGoRect());
    if (g_fakeOta.cardInstalls) fail("installed a refused card");
    tapRect(TRect{16, 712, SCREEN_W - 32, 56});
    {
      ota::Card& k = g_fakeOta.cardScript;
      k = ota::Card{};
      k.state = ota::Card::Found;
      snprintf(k.version, sizeof(k.version), "v2.1.0");
      k.files[0].found = true;
      k.files[0].bytes = 1468006;
      k.files[2].found = true;
      k.files[2].bytes = 1153433;
      k.files[3].found = true;
      k.files[3].bytes = 943718;
    }
    tapRect(setui::updCardRect());
    toybox.tick();
    shot("update_card_found");
    sensors::hostSetBattery(15, false);
    shot("update_card_low_battery");
    tapRect(setui::updCardGoRect());
    if (g_fakeOta.cardInstalls) fail("installed from the card on 15% battery");
    sensors::hostSetBattery(80, false);
    g_fakeOta.cardErr = ota::Err::Download;
    tapRect(setui::updCardGoRect());
    if (st.hostUpStage() != 9) fail("INSTALL from the card did not start");
    g_dumpEnabled = true;
    setScreen("update_card_installing");
    toybox.tick();
    g_dumpEnabled = false;
    if (g_fakeOta.cardInstalls != 1 || st.hostUpStage() != 10) fail("the card install did not run");
    shot("update_card_failed");
    const int restartsWas = g_fakeOta.restarts;
    toybox.tick();
    if (g_fakeOta.restarts != restartsWas) fail("restarted after a failed card install");
    tapRect(setui::updDoneRect());
    if (st.hostUpStage() != 0) fail("DONE did not leave a failed card install");
    g_fakeOta.cardErr = ota::Err::Ok;
    tapRect(setui::updCardRect());
    toybox.tick();
    tapRect(setui::updCardGoRect());
    toybox.tick();
    shot("update_card_done");
    toybox.tick();
    if (g_fakeOta.restarts != restartsWas + 1) fail("no restart after the card install");
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);

    homenet::forgetWifi(stickyHost.prefs());
    sensors::hostSetBattery(batWas, chgWas);
    printf("update page ok (%d checks, %d installs, %d restart)\n", g_fakeOta.checks,
           g_fakeOta.installs, g_fakeOta.restarts);
    g_dumpEnabled = true;
  }

  // The lock screen page, reached by the button on the settings screen.
  setScreen("settings_lock");
  tapAct(setui::ACT_LOCK);
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
      tapRect(setui::wakeChipRect(2));
      if (lock::config().wake != lock::WAKE_HUB) {
        printf("LOCK FAIL: the hub wake chip did not take\n");
        abort();
      }
      tapRect(setui::wakeChipRect(1));
      if (lock::config().wake != lock::WAKE_APP) {
        printf("LOCK FAIL: the last-app wake chip did not take\n");
        abort();
      }
      tapRect(setui::wakeChipRect(0));
      if (lock::config().wake != lock::WAKE_NOTE) {
        printf("LOCK FAIL: the note wake chip did not take\n");
        abort();
      }
      tapRect(setui::wakeChipRect(wasWake == lock::WAKE_HUB ? 2 : wasWake == lock::WAKE_APP ? 1 : 0));
    }

    // Every chip has to be reachable and has to be the one that ends up filled.
    // A chip row where two tap targets overlap looks right and picks the wrong
    // thing, which is the failure a screenshot will not show.
    {
      const uint8_t was = lock::config().empty;
      for (int k = 0; k < lock::EMPTY_COUNT; k++) {
        tapRect(setui::chipRect(k));
        const uint8_t want = lock::emptyAt(k);
        if (lock::config().empty != want) {
          printf("LOCK FAIL: chip %d set the empty screen to %d, wanted %d\n", k,
                 lock::config().empty, want);
          abort();
        }
      }
      tapRect(setui::chipRect(lock::emptyIndexOf(was)));
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
    tapAct(setui::ACT_LOCK);  // ...and back to the lock page

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
    tapAct(setui::ACT_WALL);
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
    tapAct(setui::ACT_WALL);
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
    tapAct(setui::ACT_FILES);
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
    if (!fs.hostRename("/books/one-piece-v1.epub", "one-piece-01.epub")) {
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
      if (strcmp(after[i].path, "/books/one-piece-01.epub") == 0) sawRenamed = true;
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
    tapAct(setui::ACT_FILES);
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

    tapAct(setui::ACT_CLOCK);
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
    tapAct(setui::ACT_CLOCK);
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
    tapAct(setui::ACT_CLOCK);
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

  // --- the clock from the internet, through the home Wi-Fi ------------------
  // The phone's page now sends a POSIX time-zone rule with the time, and can
  // save a home network. With both, the clock page offers FROM THE INTERNET.
  // Guarded: the zone is stored, a short password is refused before it can
  // fail at a router, the button exists only when it can work, the number
  // that reaches the chip is UTC moved into the saved zone, and the radio is
  // switched off on every path -- success and each failure.
  {
    g_dumpEnabled = false;
    char tzBuf[homenet::TZ_MAX];
    homenet::tz(prefs, tzBuf, sizeof(tzBuf));
    if (strcmp(tzBuf, "STD-7") != 0) {
      printf("NET FAIL: the phone's zone was stored as '%s'\n", tzBuf);
      abort();
    }
    // UTC -> wall clock, under rules the phone page produces. Expected
    // values from the IANA database (Python zoneinfo) for the real zones.
    struct { const char* tz; time_t utc; int hh, mm; } cases[] = {
        {"STD-7", 1787300000, 15, 13},                                  // Bangkok
        {"STD-1DST,M3.5.0,M10.5.0/3", 1787300000, 10, 13},               // Berlin, summer
        {"STD-1DST,M3.5.0,M10.5.0/3", 1798761600, 1, 0},                // Berlin, winter
        {"STD5DST,M3.2.0,M11.1.0", 1798761600, 19, 0},                  // New York, the day before
        {"STD-5:30", 1787300000, 13, 43},                                 // Kolkata
        {"STD-10:30DST-11,M10.1.0,M4.1.0", 1798761600, 11, 0},          // Lord Howe, summer
    };
    for (const auto& k : cases) {
      const int64_t ms = homenet::localEpochMs(k.utc, k.tz);
      const int m = (int)((ms / 60000) % (24 * 60));
      if (m != k.hh * 60 + k.mm) {
        printf("NET FAIL: %s at %ld gave %02d:%02d, want %02d:%02d\n", k.tz, (long)k.utc, m / 60,
               m % 60, k.hh, k.mm);
        abort();
      }
    }
    if (homenet::tzLooksValid("STD-7;rm") || homenet::saveTz(prefs, "")) {
      printf("NET FAIL: a junk zone was accepted\n");
      abort();
    }

    toybox.goHub();
    toybox.hostHub().goHome();
    homenet::forgetWifi(prefs);
    toybox.openSettings();
    tapAct(setui::ACT_CLOCK);
    // No home Wi-Fi: the button is not there, and its spot does nothing.
    tapRect(setui::clockNetRect());
    if (toybox.hostSettings().hostNetStage() != 0 || toybox.hostSettings().hostPage() != 6) {
      printf("NET FAIL: the internet button answered with no home wifi saved\n");
      abort();
    }
    auto& cs = toybox.hostSettings().hostClock();
    if (cs.hostSendWifi("Home", "short") || homenet::haveWifi(prefs)) {
      printf("NET FAIL: a 5-character WPA password was stored\n");
      abort();
    }
    if (!cs.hostSendWifi("Somchai 5G", "correct horse") || !homenet::haveWifi(prefs)) {
      printf("NET FAIL: a good home wifi was not stored\n");
      abort();
    }
    for (int i = 0; i < 5 && !cs.wasSet(); i++) toybox.tick();
    g_dumpEnabled = true;
    setScreen("settings_clock_set_wifi");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(setui::clockDoneRect());

    // With both saved, the first screen offers the internet.
    tapAct(setui::ACT_CLOCK);
    g_dumpEnabled = true;
    setScreen("settings_clock_net_offer");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    struct { homenet::Result net; const char* shot; } runs[] = {
        {homenet::Result::Ok, "settings_clock_net_ok"},
        {homenet::Result::NotFound, "settings_clock_net_notfound"},
        {homenet::Result::Refused, "settings_clock_net_refused"},
        {homenet::Result::NoInternet, "settings_clock_net_nointernet"},
    };
    bool first = true;
    for (const auto& r : runs) {
      if (!first) tapAct(setui::ACT_CLOCK);
      first = false;
      homenet::g_hostNet = r.net;
      homenet::g_hostJoins = homenet::g_hostOffs = 0;
      g_clockSetMs = 0;
      if (r.net == homenet::Result::Ok) g_dumpEnabled = true;
      if (r.net == homenet::Result::Ok) setScreen("settings_clock_net_busy");
      tapRect(setui::clockNetRect());
      g_dumpEnabled = false;
      if (toybox.hostSettings().hostNetStage() != 1 || cs.hasClient()) {
        printf("NET FAIL: the button did not start (stage %d) or the AP stayed up\n",
               toybox.hostSettings().hostNetStage());
        abort();
      }
      toybox.tick();
      if (toybox.hostSettings().hostNetStage() != 2 || homenet::g_hostJoins != 1 ||
          homenet::g_hostOffs != 1) {
        printf("NET FAIL (%s): stage %d, joins %d, radio offs %d\n", r.shot,
               toybox.hostSettings().hostNetStage(), homenet::g_hostJoins, homenet::g_hostOffs);
        abort();
      }
      const bool wantSet = r.net == homenet::Result::Ok;
      const int64_t want = homenet::localEpochMs(homenet::g_hostUtc, "STD-7");
      if (wantSet ? g_clockSetMs != want : g_clockSetMs != 0) {
        printf("NET FAIL (%s): the chip got %lld, want %lld\n", r.shot, (long long)g_clockSetMs,
               (long long)(wantSet ? want : 0));
        abort();
      }
      g_dumpEnabled = true;
      setScreen(r.shot);
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      tapRect(setui::clockDoneRect());
      if (toybox.hostSettings().hostPage() != 0 || toybox.hostSettings().hostNetStage() != 0) {
        printf("NET FAIL (%s): DONE did not return to settings\n", r.shot);
        abort();
      }
    }
    // The backup network: tried only when the first is not there or refuses;
    // its name is the one reported; when both fail the radio is still off.
    {
      if (!cs.hostSendWifi("Phone hotspot", "hotspot123", 1) ||
          !homenet::haveWifiSlot(prefs, 1)) {
        printf("NET FAIL: the backup wifi was not stored\n");
        abort();
      }
      homenet::g_hostNet = homenet::Result::NotFound;
      homenet::g_hostNet2 = homenet::Result::Ok;
      homenet::g_hostOffs = 0;
      g_clockSetMs = 0;
      if (homenet::syncClock(prefs) != homenet::Result::Ok || g_clockSetMs == 0 ||
          strcmp(homenet::g_joined, "Phone hotspot") != 0 || homenet::g_hostOffs != 2) {
        printf("NET FAIL: backup: joined '%s', clock %lld, offs %d\n", homenet::g_joined,
               (long long)g_clockSetMs, homenet::g_hostOffs);
        abort();
      }
      homenet::g_hostNet = homenet::Result::Ok;  // the first answers: the backup is not tried
      if (homenet::syncClock(prefs) != homenet::Result::Ok ||
          strcmp(homenet::g_joined, "Somchai 5G") != 0) {
        printf("NET FAIL: with the first network up, '%s' was joined\n", homenet::g_joined);
        abort();
      }
      homenet::g_hostNet = homenet::Result::Refused;
      homenet::g_hostNet2 = homenet::Result::NotFound;
      homenet::g_hostOffs = 0;
      if (homenet::syncClock(prefs) != homenet::Result::NotFound || homenet::g_hostOffs != 2) {
        printf("NET FAIL: both down should report the backup's answer, radio off\n");
        abort();
      }
      // Only a backup saved: it is used, and named.
      homenet::forgetWifiSlot(prefs, 0);
      homenet::g_hostNet = homenet::Result::Ok;
      homenet::g_hostNet2 = homenet::Result::NoWifiSaved;
      char nm[homenet::SSID_MAX];
      homenet::wifiName(prefs, nm, sizeof(nm));
      if (!homenet::haveWifi(prefs) || strcmp(nm, "Phone hotspot") != 0 ||
          homenet::syncClock(prefs) != homenet::Result::Ok) {
        printf("NET FAIL: a backup alone did not work (name '%s')\n", nm);
        abort();
      }
    }
    homenet::g_hostNet = homenet::Result::Ok;
    homenet::g_hostNet2 = homenet::Result::NoWifiSaved;
    homenet::forgetWifi(prefs);
    if (homenet::haveWifi(prefs)) {
      printf("NET FAIL: forgetWifi left a network behind\n");
      abort();
    }
    toybox.goHub();
    toybox.hostHub().goHome();
    sensors::hostSetClock(true);
    g_dumpEnabled = true;
    printf("clock from the internet ok (zone stored, 6 zones converted, 4 network outcomes, backup network, radio off each time)\n");
  }

  {
    g_dumpEnabled = false;
    // --- a cover the owner put beside the book ------------------------------
    // This path shipped in beta.26 with no guard at all and crashed the device
    // on the first book that used it: it ran the finished picture through the
    // cover BUILDER, which meant 46 KB of heap and a 3,968-byte stack frame on
    // an 8 KB task. The assertion that would have caught it is the one below --
    // the cover on the card must be the sidecar's own bytes, unchanged. A
    // builder cannot produce that: it re-dithers, and a re-dither of a dithered
    // picture is never bit-identical.
    {
      const char* kBook = "/books/walden.epub";
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
      char big[bstore::PATH_CAP];
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
    g_dumpEnabled = true;
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
    // Both hash generations of the OLD position folders, pinned against
    // independent references -- still needed to find a pre-v2 position and
    // move it into the book's folder. FNV is the one older Toybox wrote;
    // Murmur the 32-bit one CrossPoint used.
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
      char p[bstore::PATH_CAP];
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
    // EPUB is the drawer's first cell (row 0, left column) since the manga
    // reader left. Two rows of tiles, centred -- or top-anchored when the
    // drawer is wearing the recently-read strip.
    {
      recents::Entry rr[recents::MAX];
      const bool strip = recents::list(stickyHost.prefs(), rr) > 0;
      (void)strip;
    toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);
    }
    if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "EPUB") != 0) {
      printf("EPUB APP FAIL: the STUDY drawer's first cell did not open EPUB\n");
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
    toybox.onTap(help::NEVER_BTN.x + 10, help::NEVER_BTN.y + 10);  // the switch on
    toybox.onTap(help::OK_BTN.x + 10, help::OK_BTN.y + 10);        // then Got it
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

    // Open the one invented book; it starts at the top of chapter one. The
    // very first open ever shows the guide to the tap zones, and the first
    // touch puts it away for good without doing anything else.
    stickyHost.prefs().remove("ep_guide");
    toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
    if (!et->hostGuide()) {
      printf("GUIDE FAIL: the first book did not show the guide\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_guide");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    {
      const int pageWas = et->hostPage();
      toybox.onTap(470, 400);  // would be "next page" -- the guide takes it
      if (et->hostGuide() || et->hostPage() != pageWas ||
          !stickyHost.prefs().getBool("ep_guide", false)) {
        printf("GUIDE FAIL: the first touch did not just put the guide away\n");
        abort();
      }
    }
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
      // The picture is drawn, not merely announced: plate.png is a real JPEG
      // (the fixture's portrait cover), prepared on the device when the book
      // opened. A page that fell back to the plate carries a small fraction
      // of this ink, and a picture read short leaves its bottom rows bare.
      int ink = 0;
      for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) ink += __builtin_popcount((uint8_t)~epd.fb()[i]);
      // The panel's own buffer is 800 across by 480 down, whichever way the
      // canvas is turned; a 240x400 picture fills the whole 480x800 page.
      int inkRows = 0;
      for (int y = 0; y < 480; y++)
        for (int xb = 0; xb < 100; xb++)
          if ((uint8_t)~epd.fb()[(size_t)y * 100 + xb]) {
            inkRows++;
            break;
          }
      if (ink < 40000 || inkRows < 440) {
        printf("EPUB APP FAIL: the illustration drew %d px over %d rows\n", ink, inkRows);
        abort();
      }
      if (!et->hostArtPrepared("OEBPS/images/plate.png") ||
          et->hostArtParts("OEBPS/images/plate.png") != 1) {
        printf("EPUB APP FAIL: the portrait picture is not one prepared page\n");
        abort();
      }

      // What went onto the card, checked against the source itself, not
      // against what the code under test believes: a real share of the page is
      // ink and a real share paper, the 4-grey and 1-bit versions agree cell by
      // cell on where, and the tones are the right way up -- the source's top
      // quarter averages 80 of 255 and its bottom quarter 136, so the top must
      // come out clearly darker. A negative swaps them; a total could not tell
      // (the picture averages almost exactly mid-grey).
      char g2[bstore::PATH_CAP], b1[bstore::PATH_CAP];
      static uint8_t g2b[96000], b1b[48000];
      if (!et->hostArtPaths("OEBPS/images/plate.png", 0, g2, b1, sizeof(g2)) ||
          sdcard::readWhole(g2, g2b, sizeof(g2b)) != 96000 ||
          sdcard::readWhole(b1, b1b, sizeof(b1b)) != 48000) {
        printf("EPUB APP FAIL: prepared art missing or the wrong size (%s, %s)\n", g2, b1);
        abort();
      }
      auto lvl = [&](int x, int y) { return (g2b[y * 120 + x / 4] >> (6 - 2 * (x & 3))) & 3; };
      auto bit = [&](int x, int y) { return (b1b[y * 60 + x / 8] & (0x80 >> (x & 7))) ? 0 : 1; };
      long greySum = 0, top = 0, bot = 0;
      int b1Ink = 0, agree = 0, cells = 0;
      for (int y = 0; y < 800; y += 8)
        for (int x = 0; x < 480; x += 8) {
          int g = 0, k = 0;
          for (int dy = 0; dy < 8; dy++)
            for (int dx = 0; dx < 8; dx++) {
              g += lvl(x + dx, y + dy);
              k += bit(x + dx, y + dy);
            }
          greySum += g;
          b1Ink += k;
          agree += (g < 64 * 3 / 2) == (k > 32);
          cells++;
          if (y < 200) top += g;
          if (y >= 600) bot += g;
        }
      const int meanLevel100 = (int)(greySum * 100 / (480L * 800));  // 0..300
      if (meanLevel100 < 120 || meanLevel100 > 295 || b1Ink < 4000 || b1Ink > 300000 ||
          agree * 100 < cells * 85 || top * 10 > bot * 8) {
        printf("EPUB APP FAIL: device art looks wrong: mean level %d.%02d, %d px ink, "
               "%d/%d cells agree, top %ld bottom %ld\n",
               meanLevel100 / 100, meanLevel100 % 100, b1Ink, agree, cells, top, bot);
        abort();
      }
      printf("device art ok (mean level %d.%02d, %d px ink, %d/%d cells agree)\n",
             meanLevel100 / 100, meanLevel100 % 100, b1Ink, agree, cells);

      // The cover goes through the same pipeline as the pictures. The
      // fixture's cover.jpg and plate.png are the same JPEG, and a portrait
      // picture is one upright part, so the loading-screen cover on the card
      // must be byte for byte the picture page's one-bit version. Anything
      // else means the cover took some other, older road.
      {
        char big[bstore::PATH_CAP];
        static uint8_t cov[48000];
        bthumb::bigPath(et->hostBookFile(), big, sizeof(big));
        if (sdcard::readWhole(big, cov, sizeof(cov)) != 48000 || memcmp(cov, b1b, 48000) != 0) {
          printf("EPUB APP FAIL: the cover (%s) is not the art pipeline's picture\n", big);
          abort();
        }
        printf("epub cover ok (same pipeline as the pictures, byte for byte)\n");
      }

      // The landscape picture (art2.png, 400x240): part 0 turned a quarter
      // clockwise to fill the panel, then upright slices -- 240 tall at the
      // panel's shape is 144 wide, so 400 needs three. The fixture has a dark
      // band along its TOP and a black square near its LEFT end, so turned
      // clockwise the band lands along the panel's RIGHT edge and the square
      // near its TOP; the first slice holds the square, the last does not.
      if (et->hostArtParts("OEBPS/images/art2.png") != 4) {
        printf("EPUB APP FAIL: the landscape picture is %d parts, not 4\n",
               et->hostArtParts("OEBPS/images/art2.png"));
        abort();
      }
      auto loadPart = [&](int part) {
        if (!et->hostArtPaths("OEBPS/images/art2.png", part, g2, b1, sizeof(g2)) ||
            sdcard::readWhole(g2, g2b, sizeof(g2b)) != 96000) {
          printf("EPUB APP FAIL: landscape part %d missing\n", part);
          abort();
        }
      };
      auto darkIn = [&](int x0, int y0, int x1, int y1) {  // mean darkness, 0..3
        long d = 0, n = 0;
        for (int y = y0; y < y1; y++)
          for (int x = x0; x < x1; x++, n++) d += 3 - lvl(x, y);
        return (int)(d * 100 / n);  // x100
      };
      loadPart(0);
      const int bandRight = darkIn(400, 100, 470, 700), bandLeft = darkIn(10, 100, 80, 700);
      const int sqTop = darkIn(150, 0, 330, 200), sqBot = darkIn(150, 600, 330, 800);
      if (bandRight < 200 || bandLeft > 100 || sqTop <= sqBot) {
        printf("EPUB APP FAIL: landscape part 0 is not turned clockwise (band R %d L %d, square "
               "top %d bottom %d)\n", bandRight, bandLeft, sqTop, sqBot);
        abort();
      }
      loadPart(1);
      const int s1top = darkIn(0, 0, 480, 120), s1sq = darkIn(0, 250, 480, 550);
      loadPart(3);
      const int s3sq = darkIn(0, 250, 480, 550);
      if (s1top < 200 || s1sq <= s3sq) {
        printf("EPUB APP FAIL: landscape slices wrong (slice 1 top %d square %d, slice 3 %d)\n",
               s1top, s1sq, s3sq);
        abort();
      }
      printf("landscape art ok (4 parts; turned: band R %d L %d; slices %d > %d)\n", bandRight,
             bandLeft, s1sq, s3sq);

      // A picture that cannot be decoded is not prepared, and is one page.
      if (et->hostArtPrepared("OEBPS/images/missing.png") ||
          et->hostArtParts("OEBPS/images/missing.png") != 1) {
        printf("EPUB APP FAIL: the broken picture was prepared, or is not one page\n");
        abort();
      }
    }

    // The next picture cannot be decoded (only a PNG signature): the reader
    // says so by name rather than showing a blank page.
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);
    if (et->hostPage() != 2 || strcmp(et->hostPageImage(), "OEBPS/images/missing.png") != 0) {
      printf("EPUB APP FAIL: the broken picture is p%d img '%s'\n", et->hostPage(),
             et->hostPageImage());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_art_plate");
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

    // A turn holds its save back until the reader pauses (EpubTool::saveSoon):
    // the reader must be asking for ticks now, and stop once the pause has
    // passed and the place is on the card.
    if (!toybox.wantsTick()) {
      printf("EPUB APP FAIL: a page turn left no save pending\n");
      abort();
    }
    delay(3001);
    toybox.tick();
    if (toybox.wantsTick()) {
      printf("EPUB APP FAIL: the held-back save did not happen after the pause\n");
      abort();
    }

    // What did that write to the card? The position, in the book's own
    // folder -- and nothing in CrossPoint's folders or beside the book.
    {
      uint8_t buf[16];
      char pos[bstore::PATH_CAP];
      bstore::file("/books/wind.epub", "position.bin", pos, sizeof(pos));
      const int n = stickyHost.sdReadFile(pos, buf, 16);
      epubc::Progress p;
      if (n != 10 || !epubc::decodeProgress(buf, n, p) || p.spine != 1 || !p.hasOffset ||
          p.offset != page1Off) {
        printf("EPUB APP FAIL: %s says n=%d spine=%d off=%u, reader is at %u\n", pos, n,
               n == 10 ? p.spine : -1, n == 10 ? p.offset : 0, page1Off);
        abort();
      }
      const char* gone[] = {"/.crosspoint/epub_1175141337288249041/progress.bin",
                            "/.crosspoint/epub_836526750/progress.bin",
                            "/books/wind.sdr/metadata.epub.lua"};
      for (const char* g : gone)
        if (stickyHost.sdReadFile(g, buf, 16) > 0) {
          printf("EPUB APP FAIL: %s was written; v2 keeps nothing there\n", g);
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

    // The More card's tiles (v2): Contents, Highlight, Sync, Rotation,
    // Settings, Exit. From the page, or from the panel (OK closes it first).
    auto epubMore = [&](int tile) {
      if (et->hostMenu() != 0) toybox.onButton(SideBtn::Ok);
      const TRect mc = rmenu::moreCorner(stickyHost.canvas().width());
      toybox.onTap(mc.x + mc.w / 2, mc.y + mc.h / 2);
      const TRect t = rmenu::moreTile(tile, stickyHost.canvas().width(), stickyHost.canvas().height());
      toybox.onTap(t.x + t.w / 2, t.y + t.h / 2);
    };
    // More > Settings (owner, 26 Nov): Text, Highlights, Reading sync, Start
    // again; Text opens the bar at the foot of the page, at a tab.
    auto epubSettings = [&](int tile) {
      if (et->hostMenu() != 0) toybox.onButton(SideBtn::Ok);
      const int W = stickyHost.canvas().width(), H = stickyHost.canvas().height();
      const TRect mc = rmenu::moreCorner(W);
      toybox.onTap(mc.x + mc.w / 2, mc.y + mc.h / 2);
      tapRect(rmenu::moreTile(4, W, H));
      tapRect(rmenu::moreSetTile(tile, W, H));
    };
    auto epubTextTab = [&](int tab) {
      epubSettings(0);
      tapRect(rmenu::textTab(tab, stickyHost.canvas().width(), stickyHost.canvas().height()));
    };
    auto tbSeg = [&](int tab, int k) {
      const int W = stickyHost.canvas().width(), H = stickyHost.canvas().height();
      tapRect(rmenu::textTab(tab, W, H));
      tapRect(rmenu::textSeg(k, W, H));
    };
    // --- the panel behind the power button ----------------------------------
    // OK used to close the book. It now opens the options panel, and closing
    // the book is a row in it.
    g_dumpEnabled = true;
    if (!toybox.onButton(SideBtn::Ok) || et->hostMenu() == 0) {
      printf("EPUB APP FAIL: OK did not open the panel\n");
      abort();
    }
    setScreen("tool_epub_options");  // now the More card (owner, 26 Nov): no panel, no clock on it
    stickyHost.refresh(true);

    // Contents: the book's own nav document, and a jump that lands where the
    // row says -- including the chapter that is nothing but a picture.
    g_dumpEnabled = false;
    epubMore(0);
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
    epubMore(0);  // CONTENTS
    toybox.onTap(240, shelf::Y0 + shelf::ROW_H + 20);   // the long chapter
    if (et->hostSpine() != 1 || et->hostLineCount() < 3) {
      printf("EPUB APP FAIL: no page of words to pick from (s%d, %d lines)\n", et->hostSpine(),
             et->hostLineCount());
      abort();
    }
    epubMore(1);  // Highlight
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
    // v2: Cancel on the Highlight bar leaves highlight mode altogether --
    // no bar left behind on the page.
    {
      epubMore(1);  // Highlight
      toybox.onTap(40, et->hostLineY(0) + 10);
      toybox.onTap(200, et->hostLineY(1) + 10);
      if (et->hostMenu() != (int)rmenu::Page::Keep) {
        printf("HIGHLIGHT FAIL: no Highlight bar to cancel (menu %d)\n", et->hostMenu());
        abort();
      }
      toybox.onTap(360, 600);  // Cancel
      if (et->hostMenu() != 0 || et->hostPicking() || et->hostMarkCount() != 1) {
        printf("HIGHLIGHT FAIL: Cancel left menu %d picking %d marks %d\n", et->hostMenu(),
               (int)et->hostPicking(), et->hostMarkCount());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_highlight_cancelled");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
    }
    // v2: the kept words are underlined on the page they were picked from.
    g_dumpEnabled = true;
    setScreen("tool_epub_highlighted");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    {
      char mp[bstore::PATH_CAP];
      marks::path("/books/wind.epub", mp, sizeof(mp));
      static uint8_t raw[marks::FILE_BYTES];
      if (stickyHost.sdReadFile(mp, raw, sizeof(raw)) != marks::FILE_BYTES ||
          memcmp(raw, "TBM4", 4) != 0 || raw[4] != 1 || raw[marks::HEAD + 14 + marks::ID] == 0) {
        printf("EPUB APP FAIL: no bookmark with a phrase on the card at %s\n", mp);
        abort();
      }
    }
    // CANCEL on the confirmation keeps nothing.
    epubMore(1);  // Highlight
    toybox.onTap(40, et->hostLineY(0) + 10);
    toybox.onTap(200, et->hostLineY(1) + 10);
    toybox.onTap(360, 600);  // CANCEL
    if (et->hostMarkCount() != 1) {
      printf("EPUB APP FAIL: CANCEL kept one anyway (%d marks)\n", et->hostMarkCount());
      abort();
    }
    epubSettings(1);  // HIGHLIGHTS
    if (et->hostMenu() != (int)rmenu::Page::Marks) {
      printf("EPUB APP FAIL: the bookmark list did not open\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_marks");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    // Its x removes it: kept here and never sent, so one tap does.
    tapRect(TRect{480 - 16 - 48, shelf::Y0 + (shelf::ROW_H - 6 - 48) / 2, 48, 48});
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
      char mp[bstore::PATH_CAP];
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
      if (sdcard::hostReadSide(mp, raw, 8) < 8 || memcmp(raw, "TBM4", 4) != 0) {
        printf("MARKS FAIL: the save did not migrate the file to TBM4\n");
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
      printf("marks migration ok (TBM2 read, TBM4 written, long labels whole)\n");
    }

    // Type: bigger text re-derives the page from the offset, so the reader
    // stays where it was reading rather than where the page number was. Done
    // on a page of WORDS -- the jump above left us on a picture, which has no
    // lines to count.
    epubTextTab(1);  // More > Settings > Text, at Size
    if (et->hostMenu() != (int)rmenu::Page::TextBar) {
      printf("EPUB APP FAIL: the text bar did not open\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_text");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    {
      const uint32_t before = et->hostPageOffset();
      const int lines = et->hostLineCount();
      tapRect(rmenu::textPlus(480, 800));  // SIZE +
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
      tapRect(rmenu::textMinus(480, 800));  // SIZE -, back to normal
      // v2: margins and paragraph gap step, and a wider margin fits fewer
      // words on a line, so the page holds no more text than before.
      const uint32_t offBefore = et->hostPageOffset();
      tbSeg(3, 2);  // margins: wide
      tbSeg(4, 2);  // paragraphs: large
      g_dumpEnabled = true;
      setScreen("tool_epub_text_fine");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      if (et->hostPageOffset() > offBefore) {
        printf("EPUB APP FAIL: wider margins moved the reader forward\n");
        abort();
      }
      tbSeg(3, 1);  // back to normal
      tbSeg(4, 1);
    }

    // The contents fallback: a book with no navigation document names its
    // chapters by their own first words.
    et->hostDropToc();
    epubMore(0);
    g_dumpEnabled = true;
    setScreen("tool_epub_contents_plain");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Ok);  // back to the page (it came from More)
    epubTextTab(0);                // the Text bar, at Font

    // The Font row opens a list of every face this device can set a book in --
    // the built-ins and whatever the card offered -- rather than cycling the
    // two that happen to be compiled in. There used to be two controls for
    // this one question, this row and the Books row in settings, and a book
    // could be set to Literata and to Bitter at once with neither screen
    // showing the other's answer.
    //
    // Whichever is chosen, the place being read must not move: a different
    // face measures differently, so the page boundaries shift under the
    // reader's eyes and the offset is what has to stay put.
    {
      const uint32_t offBefore = et->hostPageOffset();
      tapRect(rmenu::textFontMore(480, 800));  // > : the whole list
      if (et->hostMenu() != (int)rmenu::Page::Font) {
        printf("EPUB APP FAIL: the Font row did not open the list (menu %d)\n", et->hostMenu());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_font");
      stickyHost.refresh(true);
      g_dumpEnabled = false;

      // The second built-in, by name.
      toybox.onTap(240, shelf::Y0 + shelf::ROW_H + 20);
      if (strcmp(stickyHost.fontFor(ToolsHost::FONT_READER), stickyHost.typefaceName(1)) != 0) {
        printf("EPUB APP FAIL: choosing the second face stored '%s'\n",
               stickyHost.fontFor(ToolsHost::FONT_READER));
        abort();
      }
      if (et->hostPageOffset() != offBefore) {
        printf("EPUB APP FAIL: changing the face moved the reader (%u -> %u)\n",
               (unsigned)offBefore, (unsigned)et->hostPageOffset());
        abort();
      }
      // ...and it is the same answer settings gives, because it is one
      // setting now rather than two.
      if (strcmp(stickyHost.fontFor(ToolsHost::FONT_READER), "Literata") != 0) {
        printf("EPUB APP FAIL: settings and the reader disagree about the face\n");
        abort();
      }
      // Back to the first, which stores nothing: "" is the face this firmware
      // has always drawn in.
      tapRect(rmenu::textFontMore(480, 800));
      toybox.onTap(240, shelf::Y0 + 20);
      if (stickyHost.fontFor(ToolsHost::FONT_READER)[0] ||
          et->hostPageOffset() != offBefore) {
        printf("EPUB APP FAIL: the first row did not put the built-in face back\n");
        abort();
      }
      printf("book face ok (built-ins and card families in one list, the place stands still)\n");
    }

    // --- a book set in a family off the card walks THAT family's sizes -------
    // The firmware has three named sizes because it has four line boxes. A
    // family is cut at as many sizes as its maker felt like -- eight or ten is
    // ordinary -- and a reader set in one was choosing between "normal",
    // "large" and "largest" out of ten real cuts, three of which the boxes
    // happened to reach. DejaVuSerif on this card draws a 29 px line and a
    // 44 px one above the body floor, and the Size stepper has to walk those.
    //
    // The layout half is the part that bites: a page laid out against the
    // firmware's 24 px box and drawn in a 44 px face is a page whose lines sit
    // on top of each other, and a footer left in the book's face runs a
    // chapter name out of the bottom of the panel.
    {
      const uint32_t offBefore = et->hostPageOffset();
      const int spineBefore = et->hostSpine();
      if (!stickyHost.fontSet(ToolsHost::FONT_READER, "DejaVuSerif") ||
          !stickyHost.fontEnter(ToolsHost::FONT_READER)) {
        printf("READER SIZE FAIL: the card family would not become the book's face\n");
        abort();
      }
      // Two sizes on offer, named by their own files, and the body is in the
      // smaller because that is the one nearest the box the firmware reads in.
      if (stickyHost.faceSizeCount() != 2 || stickyHost.faceSizeLine(0) != 29 ||
          stickyHost.faceSizeLine(1) != 44 || stickyHost.faceSizePt(0) != 12 ||
          stickyHost.faceSizePt(1) != 18 || stickyHost.faceSize() != 0) {
        printf("READER SIZE FAIL: %d sizes -- %d px/%d pt then %d px/%d pt, at %d\n",
               stickyHost.faceSizeCount(), stickyHost.faceSizeLine(0), stickyHost.faceSizePt(0),
               stickyHost.faceSizeLine(1), stickyHost.faceSizePt(1), stickyHost.faceSize());
        abort();
      }
      // The 8 pt file is a 19 px line: below the body size this firmware
      // settled on, so it is not offered at all. A page nobody can read is not
      // a choice.
      if (stickyHost.faceSizeLine(2) != 0) {
        printf("READER SIZE FAIL: a %d px line was offered as a reading size\n",
               stickyHost.faceSizeLine(2));
        abort();
      }
      et->hostSetStyle(0);
      const int linesSmall = et->hostLineCount();

      if (et->hostMenu() != (int)rmenu::Page::TextBar) {
        printf("READER SIZE FAIL: the text bar is not up (menu %d)\n", et->hostMenu());
        abort();
      }
      tapRect(rmenu::textTab(1, 480, 800));  // Size
      g_dumpEnabled = true;
      setScreen("tool_epub_text_card");
      stickyHost.refresh(true);
      g_dumpEnabled = false;

      tapRect(rmenu::textPlus(480, 800));  // SIZE +, to the family's 18 pt
      if (stickyHost.faceSize() != 1 || gfx::cardFaceLine(24, true) != 44) {
        printf("READER SIZE FAIL: stepping up left the size at %d and the body box at %d px\n",
               stickyHost.faceSize(), gfx::cardFaceLine(24, true));
        abort();
      }
      // The page has to have been laid out against the face it is drawn in.
      // Same offset, and fewer lines because each one is half again as tall.
      if (et->hostPageOffset() > offBefore) {
        printf("READER SIZE FAIL: a bigger size moved the reader forward\n");
        abort();
      }
      if (et->hostLineCount() >= linesSmall) {
        printf("READER SIZE FAIL: 44 px lines gave %d of them, 29 px gave %d\n",
               et->hostLineCount(), linesSmall);
        abort();
      }
      // And the lines do not land on each other: every line has to sit at
      // least a 44 px line below the one above it.
      for (int i = 1; i < et->hostLineCount(); i++) {
        const int gap = et->hostLineY(i) - et->hostLineY(i - 1);
        if (gap > 0 && gap < 44) {
          printf("READER SIZE FAIL: line %d sits %d px under line %d, in a 44 px face\n", i, gap,
                 i - 1);
          abort();
        }
      }
      // The top of the family is the top: there is no next size to step to.
      tapRect(rmenu::textPlus(480, 800));
      if (stickyHost.faceSize() != 1) {
        printf("READER SIZE FAIL: stepping past the family's largest gave %d\n",
               stickyHost.faceSize());
        abort();
      }

      // The footer stays in the DEVICE's face. It is drawn in the 24 px box --
      // the very box a chosen size sits in -- so leaving the book's face on
      // would set the chapter name in 44 px inside a 56 px band and run it off
      // the bottom of the panel.
      // (already on the panel: Book settings)
      // What the lock screen asks before it decides to keep the panel as it
      // is. A panel over the page does not count -- leaving a half-open
      // options panel on the fridge for eight hours is not the feature.
      if (toybox.showingOwnPage()) {
        printf("READER SIZE FAIL: the options panel counted as a page\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);  // panel down, onto the page
      if (!toybox.showingOwnPage()) {
        printf("READER SIZE FAIL: a book open at a page did not count as one\n");
        abort();
      }
      toybox.onTap(240, 400);        // the middle: the footer comes up
      stickyHost.refresh(true);
      int first = -1, last = -1;
      for (int y = 800 - epubui::FOOT_H + 8; y < 800; y++)
        for (int x = 0; x < 480; x++) {
          int pxx, pyy;
          epdMapPixel(epd.rotation(), epd.panelFlipX(), epd.panelFlipY(), x, y, pxx, pyy);
          if ((epd.fb()[(uint32_t)pyy * EPD_WB + (pxx >> 3)] & (0x80 >> (pxx & 7))) == 0) {
            if (first < 0) first = y;
            last = y;
          }
        }
      // Measured from under the band's rule, which is ink of its own. The
      // device's face fills eighteen rows here; the book's 44 px one fills
      // thirty-two and would fill more if the band let it.
      if (first < 0 || last - first > 24) {
        printf("READER SIZE FAIL: the footer's ink spans %d rows (%d..%d) -- the book's face\n",
               first < 0 ? -1 : last - first, first, last);
        abort();
      }
      if (gfx::contentFaceOn()) {
        printf("READER SIZE FAIL: the book's face was left on after a page was drawn\n");
        abort();
      }
      toybox.onTap(240, 400);  // the footer back down

      // Put the book back the way the next check expects to find it.
      epubTextTab(1);
      tapRect(rmenu::textMinus(480, 800));  // SIZE -, back to the family's 12 pt
      toybox.onButton(SideBtn::Ok);
      stickyHost.fontSet(ToolsHost::FONT_READER, "");
      stickyHost.fontEnter(ToolsHost::FONT_READER);
      et->hostSetStyle(0);
      et->hostGoto(spineBefore, offBefore);
      toybox.onButton(SideBtn::Ok);  // the panel again, as it was
      printf("reader sizes ok (%d cuts walked, %d px lines laid out, footer in the device face)\n",
             stickyHost.faceSizeCount() ? stickyHost.faceSizeCount() : 2, 44);
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
      const uint32_t offR = et->hostPageOffset();
      const int linesPortrait = et->hostLineCount();
      et->hostSetRot(3);  // RIGHT: landscape (now via More > Rotation and its lock)
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
      toybox.onButton(SideBtn::Ok);  // More: the card sits at the page's own angle
      if (stickyHost.canvasRotation() != 3 || et->hostMenu() != (int)rmenu::Page::More) {
        printf("EPUB APP FAIL: More did not open over the landscape page (rot %d, menu %d)\n",
               stickyHost.canvasRotation(), et->hostMenu());
        abort();
      }
      // The other landscape, one tap, no cycling through portrait to get
      // there; then straight home the same way.
      et->hostSetRot(1);  // LEFT: the other landscape
      toybox.onButton(SideBtn::Ok);
      if (stickyHost.canvasRotation() != 1) {
        printf("EPUB APP FAIL: LEFT did not land the flipped landscape\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      et->hostSetRot(0);  // PORTRAIT
      toybox.onButton(SideBtn::Ok);
      if (stickyHost.canvasRotation() != 0 ||
          stickyHost.prefs().getUInt("rd_rot", 99) != 0) {
        printf("EPUB APP FAIL: PORTRAIT did not stand the page back up\n");
        abort();
      }
      printf("rotation ok (three buttons, turns on close, offset pinned)\n");
      toybox.onButton(SideBtn::Ok);  // panel up for the close row below
    }
    // --- KOReader progress sync, from the panel ---------------------------
    // With a home Wi-Fi and a sync account saved, the panel grows an eighth
    // row. Guarded through a stand-in server: an upload carries an xpointer
    // that reads back to this very page; another reader further on is ASKED
    // about, never jumped to; JUMP lands on its place and STAY does not move
    // or upload; a place this file does not have falls back to the
    // percentage; failures say why; and the radio is off after every run.
    {
      g_dumpEnabled = false;
      toybox.onButton(SideBtn::Ok);  // panel down...
      homenet::saveWifi(stickyHost.prefs(), "Somchai 5G", "correct horse");
      kosync::forgetAccount(stickyHost.prefs());
      kosync::g_server = kosync::HostServer();
      homenet::g_hostNet = homenet::Result::Ok;
      toybox.onButton(SideBtn::Ok);  // ...and up again, now with Sync
      g_dumpEnabled = true;
      setScreen("tool_epub_options_sync");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      const int rowSync = 6;
      const int signY = 560 + 120 + 40;

      // 0. No account: the row signs in with Readest. A code, shown as a QR
      // code, polled while pending; CANCEL and an expired code both leave
      // the radio off and nothing saved; an approval saves the device key as
      // the account (md5 of it, the x-auth-key Readest checks) and goes
      // straight on to the sync.
      {
        homenet::g_hostJoins = homenet::g_hostOffs = 0;
        et->hostSyncFromPanel();
        if (et->hostSyncStage() != 3) {
          printf("SYNC FAIL: with no account the row did not start sign-in (stage %d)\n",
                 et->hostSyncStage());
          abort();
        }
        toybox.tick();  // asks for the code
        if (et->hostSyncStage() != 4 || kosync::g_server.codes != 1 || homenet::g_hostOffs != 0) {
          printf("SYNC FAIL: no code on screen (stage %d, %d codes)\n", et->hostSyncStage(),
                 kosync::g_server.codes);
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_signin");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
        // CANCEL while pending: radio off, nothing saved, back to the options.
        toybox.onTap(240, signY);
        if (et->hostSyncStage() != 0 || homenet::g_hostOffs != 1 ||
            kosync::haveAccount(stickyHost.prefs()) || et->hostMenu() != 0) {
          printf("SYNC FAIL: CANCEL left stage %d, offs %d, menu %d\n", et->hostSyncStage(),
                 homenet::g_hostOffs, et->hostMenu());
          abort();
        }
        // An expired code.
        kosync::g_server.polls = 0;
        kosync::g_server.expire = true;
        et->hostSyncFromPanel();
        for (int i = 0; i < 6 && et->hostSyncStage() != 2; i++) toybox.tick();
        if (et->hostSyncOut() != 3 || kosync::haveAccount(stickyHost.prefs())) {
          printf("SYNC FAIL: an expired code gave out %d\n", et->hostSyncOut());
          abort();
        }
        toybox.onTap(240, 560 + 40);  // back to the book
        toybox.onButton(SideBtn::Ok);
        // Approved after two pending polls.
        kosync::g_server.polls = 0;
        kosync::g_server.expire = false;
        homenet::g_hostOffs = 0;
        et->hostSyncFromPanel();
        for (int i = 0; i < 8 && et->hostSyncStage() != 2; i++) toybox.tick();
        char key[40], want[33];
        stickyHost.prefs().getString(kosync::K_KEY, key, sizeof(key));
        md5t::hex("devkey-123", want);
        if (!kosync::isReadest(stickyHost.prefs()) || strcmp(key, want) ||
            kosync::g_server.polls != 3 || et->hostSyncOut() != 0 || kosync::g_server.puts != 1) {
          printf("SYNC FAIL: approval: readest %d key ok %d polls %d out %d puts %d\n",
                 kosync::isReadest(stickyHost.prefs()), !strcmp(key, want), kosync::g_server.polls,
                 et->hostSyncOut(), kosync::g_server.puts);
          abort();
        }
        if (homenet::g_hostOffs != 2) {  // once after sign-in, once after the sync
          printf("SYNC FAIL: radio offs %d after sign-in + sync\n", homenet::g_hostOffs);
          abort();
        }
        toybox.onTap(240, 560 + 40);  // back to the book
        toybox.onButton(SideBtn::Ok);
        kosync::g_server = kosync::HostServer();
      }
      auto tapSync = [&](const char* busyShot) {
        homenet::g_hostJoins = homenet::g_hostOffs = 0;
        if (busyShot) {
          g_dumpEnabled = true;
          setScreen(busyShot);
        }
        et->hostSyncFromPanel();
        g_dumpEnabled = false;
        if (et->hostMenu() != (int)rmenu::Page::Sync || et->hostSyncStage() != 1) {
          printf("SYNC FAIL: the Sync row did not start (menu %d stage %d)\n", et->hostMenu(),
                 et->hostSyncStage());
          abort();
        }
        toybox.tick();
        if (et->hostSyncStage() != 2 || homenet::g_hostOffs != 1) {
          printf("SYNC FAIL: stage %d after the tick, radio offs %d\n", et->hostSyncStage(),
                 homenet::g_hostOffs);
          abort();
        }
      };
      const int btnY = 560 + 40;

      // 1. Nothing on the server: upload, and the xpointer is this page.
      const int spineHere = et->hostSpine();
      const uint32_t offHere = et->hostPageOffset();
      tapSync("tool_epub_sync_busy");
      if (et->hostSyncOut() != 0 || kosync::g_server.puts != 1) {
        printf("SYNC FAIL: an empty server got out %d, %d puts\n", et->hostSyncOut(),
               kosync::g_server.puts);
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_sync_sent");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      {  // the shelf's sync mark: a "synced" marker in the book's folder
        char mk[bstore::PATH_CAP];
        bstore::file(et->hostCurFile(), "synced", mk, sizeof(mk));
        if (!sdcard::hostCardHasPrefix(mk)) {
          printf("SYNC FAIL: no synced marker at %s after a sync\n", mk);
          abort();
        }
        g_syncedBook = et->hostCurFile();
      }
      kosync::Remote sent;
      kosync::parseRemote(kosync::g_server.lastPut.c_str(), sent);
      int s1 = -1;
      uint32_t o1 = 0;
      if (!et->hostLocate(sent.progress, s1, o1) || s1 != spineHere || o1 < offHere) {
        printf("SYNC FAIL: uploaded '%s' reads back as %d:%u, the page is %d:%u\n", sent.progress,
               s1, o1, spineHere, offHere);
        abort();
      }
      if (fabs(sent.percentage - et->hostBookFraction()) > 1e-6 || strcmp(sent.device, "Toybox")) {
        printf("SYNC FAIL: uploaded %.4f from '%s'\n", sent.percentage, sent.device);
        abort();
      }
      toybox.onTap(240, btnY);  // back to the book
      if (et->hostMenu() != 0 || et->hostSpine() != spineHere) {
        printf("SYNC FAIL: 'Back to the book' left menu %d spine %d\n", et->hostMenu(),
               et->hostSpine());
        abort();
      }

      // 2. Readest is further on: ask. Its place is a real xpointer in
      // chapter 2 of this book.
      char doc[33];
      et->hostDocHash(doc);
      char farXp[240];
      et->hostXp(1, 2500, farXp, sizeof(farXp));
      int sFar = -1;
      uint32_t oFar = 0;
      et->hostLocate(farXp, sFar, oFar);
      auto storeRemote = [&](const char* xp, double pct) {
        char body[600];
        snprintf(body, sizeof(body),
                 "{\"document\":\"%s\",\"progress\":\"%s\",\"percentage\":%.4f,"
                 "\"device\":\"Readest\",\"device_id\":\"readest-1\",\"timestamp\":1787300000}",
                 doc, xp, pct);
        kosync::g_server.rec[doc] = body;
      };
      storeRemote(farXp, 0.93);
      toybox.onButton(SideBtn::Ok);
      const int putsBefore = kosync::g_server.puts;
      tapSync(nullptr);
      if (et->hostSyncOut() != 2) {
        printf("SYNC FAIL: a reader further on gave out %d, not ASK\n", et->hostSyncOut());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_sync_ask");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      // STAY: nothing moves, nothing is sent.
      toybox.onTap(360, btnY);
      if (et->hostMenu() != 0 || et->hostSpine() != spineHere ||
          kosync::g_server.puts != putsBefore) {
        printf("SYNC FAIL: STAY moved to %d or sent (%d puts)\n", et->hostSpine(),
               kosync::g_server.puts - putsBefore);
        abort();
      }
      // JUMP: on the page holding the other reader's place.
      toybox.onButton(SideBtn::Ok);
      tapSync(nullptr);
      toybox.onTap(120, btnY);
      if (et->hostMenu() != 0 || et->hostSpine() != sFar || et->hostPageOffset() > oFar) {
        printf("SYNC FAIL: JUMP landed %d:%u, the place is %d:%u\n", et->hostSpine(),
               et->hostPageOffset(), sFar, oFar);
        abort();
      }

      // 3. A place this file does not have: the percentage decides.
      storeRemote("/body/DocFragment[2]/body/table[9]/tr[1]/text()[1].0", 0.97);
      toybox.onButton(SideBtn::Ok);
      tapSync(nullptr);
      toybox.onTap(120, btnY);
      if (et->hostMenu() != 0 || et->hostSpine() < sFar) {
        printf("SYNC FAIL: the percentage fallback went back to spine %d\n", et->hostSpine());
        abort();
      }

      // 4. In step: their paragraph is on the page shown. Its percentage is
      // set far off on purpose -- the paragraph decides, not the number.
      {
        char hereXp[240];
        et->hostXp(et->hostSpine(), et->hostPageOffset(), hereXp, sizeof(hereXp));
        storeRemote(hereXp, 0.5);
      }
      toybox.onButton(SideBtn::Ok);
      const int putsSame = kosync::g_server.puts;
      tapSync(nullptr);
      if (et->hostSyncOut() != 1 || kosync::g_server.puts != putsSame) {
        printf("SYNC FAIL: the same place gave out %d with %d puts\n", et->hostSyncOut(),
               kosync::g_server.puts - putsSame);
        abort();
      }
      toybox.onTap(240, btnY);

      // 4b. Behind by paragraph, though its percentage claims further on:
      // upload ours, ask nothing.
      {
        char backXp[240];
        et->hostXp(0, 0, backXp, sizeof(backXp));
        storeRemote(backXp, 0.99);
        toybox.onButton(SideBtn::Ok);
        const int putsBack = kosync::g_server.puts;
        tapSync(nullptr);
        if (et->hostSyncOut() != 0 || kosync::g_server.puts != putsBack + 1) {
          printf("SYNC FAIL: a paragraph behind gave out %d with %d puts\n", et->hostSyncOut(),
                 kosync::g_server.puts - putsBack);
          abort();
        }
        toybox.onTap(240, btnY);
      }

      // D. Highlights from Readest come with the place (part A, pull). Two on
      // this very page (made from its own words, so they underline here), one
      // elsewhere, one deleted that was never here; then Readest deletes one,
      // and one removed HERE stays removed however often the pull runs.
      {
        char hdoc[33];
        et->hostDocHash(hdoc);
        const int sp = et->hostSpine();
        const uint32_t po = et->hostPageOffset();
        char w0[64] = "", w1[64] = "";
        {  // the first three words of lines 0 and 2: real words, on this page
          auto firstWords = [](const char* line, char* out, int cap) {
            int n = 0, words = 0;
            for (const char* q = line; *q && n < cap - 1; q++) {
              if (*q == ' ' && ++words == 3) break;
              out[n++] = *q;
            }
            out[n] = 0;
          };
          firstWords(et->hostLineText(0), w0, sizeof(w0));
          firstWords(et->hostLineText(2), w1, sizeof(w1));
        }
        char page1[2048];
        snprintf(page1, sizeof(page1),
                 "{\"document\":\"%s\",\"sync_version\":2,\"cursor\":3,\"more\":true,\"items\":["
                 "{\"id\":\"rd-aaa1111\",\"deleted\":0,\"revision\":1,\"spine\":%d,\"start_offset\":%u,"
                 "\"end_offset\":%u,\"chapter\":\"One\",\"text\":\"%s\",\"created_at\":1787000000},"
                 "{\"id\":\"rd-gone000\",\"deleted\":1,\"revision\":2},"
                 "{\"id\":\"rd-bbb2222\",\"deleted\":0,\"spine\":%d,\"start_offset\":%u,\"end_offset\":%u,"
                 "\"chapter\":\"One\",\"text\":\"%s\\n\",\"created_at\":1787000001}]}",
                 hdoc, sp, po, po + (unsigned)strlen(w0), w0, sp, po + 72,
                 po + 72 + (unsigned)strlen(w1), w1);
        const char* page2 =
            "{\"cursor\":4,\"more\":false,\"items\":[{\"id\":\"rd-ccc3333\",\"deleted\":0,\"spine\":0,"
            "\"start_offset\":0,\"end_offset\":30,\"chapter\":\"\",\"text\":\"a phrase from elsewhere "
            "in the book, \\u201cquoted\\u201d\"}]}";
        rhl::g_hostClips = rhl::HostClips();
        rhl::g_hostClips.pages[hdoc] = {page1, page2};
        const int had = et->hostMarkCount();
        toybox.onButton(SideBtn::Ok);
        tapSync(nullptr);
        if (et->hostHlAdded() != 3 || et->hostHlRemoved() != 0 || et->hostMarkCount() != had + 3 ||
            rhl::g_hostClips.gets != 2) {
          printf("HL FAIL: pull added %d removed %d, %d marks (had %d), %d gets\n", et->hostHlAdded(),
                 et->hostHlRemoved(), et->hostMarkCount(), had, rhl::g_hostClips.gets);
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_highlights");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
        toybox.onTap(240, btnY);  // back to the book: two underlines on the page
        g_dumpEnabled = true;
        setScreen("tool_epub_readest_underlined");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
        // Their own: from Readest, with an id, on this page by offset.
        int onPage = 0, fromReadest = 0;
        for (int i = 0; i < et->hostMarkCount(); i++) {
          fromReadest += et->hostMarkSrc(i) == marks::SRC_READEST;
          onPage += et->hostMarkSrc(i) == marks::SRC_READEST && et->hostMarkHere(i);
        }
        if (fromReadest != 3 || onPage != 2) {
          printf("HL FAIL: %d from Readest, %d found on this page (want 3, 2)\n", fromReadest, onPage);
          abort();
        }
        // The same pull again: nothing new.
        toybox.onButton(SideBtn::Ok);
        tapSync(nullptr);
        if (et->hostHlAdded() != 0 || et->hostMarkCount() != had + 3) {
          printf("HL FAIL: a second pull added %d (%d marks)\n", et->hostHlAdded(), et->hostMarkCount());
          abort();
        }
        toybox.onTap(240, btnY);
        // Readest deletes rd-bbb2222: gone here too.
        page1[0] = 0;
        rhl::g_hostClips.pages[hdoc] = {
            "{\"cursor\":2,\"more\":false,\"items\":[{\"id\":\"rd-bbb2222\",\"deleted\":1}]}"};
        toybox.onButton(SideBtn::Ok);
        tapSync(nullptr);
        if (et->hostHlRemoved() != 1 || et->hostMarkCount() != had + 2) {
          printf("HL FAIL: Readest's deletion removed %d (%d marks)\n", et->hostHlRemoved(),
                 et->hostMarkCount());
          abort();
        }
        toybox.onTap(240, btnY);
        // Removed HERE: buried, so a pull that still lists it does not bring it back.
        {
          int at = -1;
          for (int i = 0; i < et->hostMarkCount(); i++)
            if (!strcmp(et->hostMarkId(i), "rd-aaa1111")) at = i;
          if (at < 0 || !et->hostMarkHere(at)) {
            printf("HL FAIL: rd-aaa1111 not on this page to remove\n");
            abort();
          }
          et->hostRemoveMark(at);
          char again[1024];
          snprintf(again, sizeof(again),
                   "{\"cursor\":1,\"more\":false,\"items\":[{\"id\":\"rd-aaa1111\",\"deleted\":0,"
                   "\"spine\":%d,\"start_offset\":%u,\"end_offset\":%u,\"text\":\"%s\"}]}",
                   sp, po + 1, po + 9, w0);
          rhl::g_hostClips.pages[hdoc] = {again};
          // First with Readest out of reach for the deletion: the burial holds,
          // on the card too, and the pull still leaves it out.
          rhl::g_hostPuts = rhl::HostPuts();
          rhl::g_hostPuts.fail = kosync::Result::NetworkError;
          toybox.onButton(SideBtn::Ok);
          tapSync(nullptr);
          if (et->hostHlAdded() != 0 || et->hostMarkCount() != had + 1 || !marks::buried("rd-aaa1111")) {
            printf("HL FAIL: a removed highlight came back (%d added, %d marks)\n", et->hostHlAdded(),
                   et->hostMarkCount());
            abort();
          }
          et->hostReloadMarks();  // and the burial is on the card, not just in RAM
          if (!marks::buried("rd-aaa1111")) {
            printf("HL FAIL: the burial did not survive a reload\n");
            abort();
          }
          toybox.onTap(240, btnY);
          // Then Readest is told: one deletion sent, and the tombstone goes.
          rhl::g_hostPuts.fail = kosync::Result::Ok;
          toybox.onButton(SideBtn::Ok);
          tapSync(nullptr);
          if (et->hostHlDead() != 1 || rhl::g_hostPuts.bodies.size() != 1 ||
              rhl::g_hostPuts.bodies[0].find("{\"id\":\"rd-aaa1111\",\"deleted\":1}") == std::string::npos ||
              marks::buried("rd-aaa1111") || et->hostMarkCount() != had + 1) {
            printf("HL FAIL: the deletion did not go to Readest (%d dead, %d puts: %s)\n", et->hostHlDead(),
                   (int)rhl::g_hostPuts.bodies.size(),
                   rhl::g_hostPuts.bodies.empty() ? "" : rhl::g_hostPuts.bodies[0].c_str());
            abort();
          }
          // Readest now lists it deleted.
          rhl::g_hostClips.pages[hdoc] = {
              "{\"cursor\":1,\"more\":false,\"items\":[{\"id\":\"rd-aaa1111\",\"deleted\":1}]}"};
          toybox.onTap(240, btnY);
        }
        // Highlights off: the pull does not run.
        stickyHost.prefs().putBool(rhl::K_HL, false);
        const int getsOff = rhl::g_hostClips.gets;
        toybox.onButton(SideBtn::Ok);
        tapSync(nullptr);
        if (rhl::g_hostClips.gets != getsOff) {
          printf("HL FAIL: highlights off still pulled\n");
          abort();
        }
        toybox.onTap(240, btnY);
        stickyHost.prefs().remove(rhl::K_HL);
        rhl::g_hostClips = rhl::HostClips();
        printf("readest highlights ok (pull, again, deleted there, removed here, off)\n");
      }

      // D (owner's photos, 26 Nov): a short Readest highlight is underlined on
      // its own page only, never wherever the same words come again. "1 w"
      // is on most pages of this book.
      {
        const uint32_t po = et->hostPageOffset();
        et->hostAddReadestMark("rd-short", po + 4, po + 7, "1 w");
        stickyHost.refresh(true);
        const int here = et->hostUnderlines();
        toybox.onButton(SideBtn::Up);  // next page
        stickyHost.refresh(true);
        const int there = et->hostUnderlines();
        toybox.onButton(SideBtn::Down);
        et->hostRemoveById("rd-short");
        if (here != 1 || there != 0) {
          printf("HL FAIL: a short highlight underlined %d times here, %d on the next page\n", here, there);
          abort();
        }
        printf("short highlight on its own page only ok\n");
      }

      // Owner, 26 Nov: a highlight's words open its card. One from Readest
      // asks twice to go (it goes on both sides) and is buried; one kept here
      // and never sent goes at one tap. The Highlights list's x does the same.
      {
        const uint32_t po = et->hostPageOffset();
        et->hostAddReadestMark("rd-card", po, po + 17, "w0721 w0722 w0723");
        stickyHost.refresh(true);
        int at = -1;
        for (int i = 0; i < et->hostUlCount(); i++) at = i;  // its stroke(s): the only marks here
        if (at < 0) {
          printf("CARD FAIL: no underline to tap\n");
          abort();
        }
        const TRect u = et->hostUlRect(0);
        toybox.onTap(u.x + u.w / 2, u.y + u.h / 2);
        if (et->hostMenu() != (int)rmenu::Page::HlCard) {
          printf("CARD FAIL: tapping the underline gave menu %d\n", et->hostMenu());
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_hl_card");
        stickyHost.refresh(false);
        g_dumpEnabled = false;
        const TRect rm = et->hostHlRemoveRect();
        tapRect(rm);
        if (!et->hostHlArmed() || et->hostMarkCount() == 0) {
          printf("CARD FAIL: a Readest highlight went at one tap\n");
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_hl_card_armed");
        stickyHost.refresh(false);
        g_dumpEnabled = false;
        const int before = et->hostMarkCount();
        tapRect(et->hostHlRemoveRect());
        if (et->hostMenu() != 0 || et->hostMarkCount() != before - 1 || !marks::buried("rd-card")) {
          printf("CARD FAIL: Tap again left menu %d, %d marks, buried %d\n", et->hostMenu(),
                 et->hostMarkCount(), (int)marks::buried("rd-card"));
          abort();
        }
        // One kept here: one tap.
        et->hostKeepWords(0, 3, 0, 4);
        stickyHost.refresh(true);
        const int kept = et->hostMarkCount();
        const TRect u2 = et->hostUlRect(0);
        toybox.onTap(u2.x + u2.w / 2, u2.y + u2.h / 2);
        tapRect(et->hostHlRemoveRect());
        if (et->hostMarkCount() != kept - 1 || et->hostMenu() != 0) {
          printf("CARD FAIL: one kept here did not go at one tap\n");
          abort();
        }
        // Close leaves it be.
        et->hostKeepWords(0, 3, 0, 4);
        stickyHost.refresh(true);
        const TRect u3 = et->hostUlRect(0);
        toybox.onTap(u3.x + u3.w / 2, u3.y + u3.h / 2);
        const TRect r3 = et->hostHlRemoveRect();
        tapRect(TRect{r3.x + r3.w + 10, r3.y, r3.w, r3.h});  // Close
        if (et->hostMarkCount() != kept || et->hostMenu() != 0) {
          printf("CARD FAIL: Close removed it or stayed up\n");
          abort();
        }
        // The list: x on a Readest one asks, the second x removes it.
        et->hostAddReadestMark("rd-list", po + 30, po + 41, "w0726 w0727");
        epubSettings(1);  // Highlights
        int idx = -1;
        for (int i = 0; i < et->hostMarkCount(); i++)
          if (!strcmp(et->hostMarkId(i), "rd-list")) idx = i;
        const TRect xr{480 - 16 - 48, shelf::Y0 + (idx % shelf::PER_PAGE) * shelf::ROW_H + (shelf::ROW_H - 6 - 48) / 2, 48, 48};
        if (idx >= shelf::PER_PAGE) toybox.onButton(SideBtn::Down);
        tapRect(xr);
        if (et->hostListArmed() != idx) {
          printf("CARD FAIL: the list's x did not ask first (armed %d, idx %d)\n", et->hostListArmed(), idx);
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_marks_armed");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
        const int n0 = et->hostMarkCount();
        tapRect(xr);
        if (et->hostMarkCount() != n0 - 1 || !marks::buried("rd-list")) {
          printf("CARD FAIL: the second x did not remove and bury it\n");
          abort();
        }
        // Reading sync from the book's own settings: the same page as Settings'.
        toybox.onTap(20, 20);  // back to the page
        epubSettings(2);       // Reading sync
        if (et->hostMenu() != (int)rmenu::Page::SyncSet) {
          printf("CARD FAIL: Reading sync row gave menu %d\n", et->hostMenu());
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_reading_sync");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
        tapRect(syncpg::row(1));
        if (stickyHost.prefs().getBool(rhl::K_HL, true)) {
          printf("CARD FAIL: the Highlights switch did not flip from the book\n");
          abort();
        }
        tapRect(syncpg::row(1));
        toybox.onTap(20, 20);  // back: to the page
        if (et->hostMenu() != 0) {
          printf("CARD FAIL: back from Reading sync gave menu %d\n", et->hostMenu());
          abort();
        }
        // Leave the book as the checks after this expect it: nothing of ours.
        for (int i = et->hostMarkCount() - 1; i >= 0; i--)
          if (et->hostMarkSrc(i) == marks::SRC_HERE) et->hostRemoveMark(i);
        marks::clearTombs();
        et->hostSaveMarks();
        // The More card, its Settings side, and the Text bar's tabs (owner, 26 Nov).
        {
          const TRect mc = rmenu::moreCorner(480);
          toybox.onTap(mc.x + mc.w / 2, mc.y + mc.h / 2);
          g_dumpEnabled = true;
          setScreen("tool_epub_more");
          stickyHost.refresh(true);
          tapRect(rmenu::moreTile(4, 480, 800));
          setScreen("tool_epub_more_settings");
          stickyHost.refresh(true);
          g_dumpEnabled = false;
          tapRect(rmenu::moreSetBack(480, 800));  // back to the six
          tapRect(rmenu::moreTile(4, 480, 800));
          tapRect(rmenu::moreSetTile(0, 480, 800));
          if (et->hostMenu() != (int)rmenu::Page::TextBar) {
            printf("CARD FAIL: Text gave menu %d\n", et->hostMenu());
            abort();
          }
          for (int tab : {0, 2, 5}) {
            tapRect(rmenu::textTab(tab, 480, 800));
            g_dumpEnabled = true;
            char nm[40];
            snprintf(nm, sizeof(nm), "tool_epub_textbar_%d", tab);
            setScreen(nm);
            stickyHost.refresh(true);
            g_dumpEnabled = false;
          }
          // Docked: a tap on the page, even just above the bar, changes nothing.
          toybox.onTap(240, 300);
          toybox.onTap(240, rmenu::textBar(480, 800).y - 8);
          if (et->hostMenu() != (int)rmenu::Page::TextBar) {
            printf("CARD FAIL: a tap off the bar closed it (menu %d)\n", et->hostMenu());
            abort();
          }
          tapRect(rmenu::textClose(480, 800));  // its x does
          if (et->hostMenu() != 0) {
            printf("CARD FAIL: the bar's x left menu %d\n", et->hostMenu());
            abort();
          }
        }
        printf("highlight card ok (tap the words, Readest asks twice and is buried, list x, Reading sync)\n");
      }

      // D part B. Highlights kept here go to Readest: exact offsets, the whole
      // phrase (not the 96-byte label), once. Removing a sent one tells Readest;
      // removing an unsent one tells nobody. One kept before part B goes too.
      {
        char hdoc[33];
        et->hostDocHash(hdoc);
        rhl::g_hostClips = rhl::HostClips();
        rhl::g_hostPuts = rhl::HostPuts();
        const uint32_t po = et->hostPageOffset();
        int thai = -1;
        for (int i = 0; i < et->hostLineN(); i++)
          if (strstr(et->hostLineText(i), "\xe0\xb8\xaa\xe0\xb8\xa7")) thai = i;  // "สว"
        if (thai < 0) {
          printf("PUSH FAIL: no Thai line on the page\n");
          abort();
        }
        const int had = et->hostMarkCount();
        et->hostKeepWords(0, 0, 0, 2);  // "w0721 w0722 w0723", the page's first words
        et->hostKeepWords(1, 0, 3, 5);  // three whole lines: longer than a label
        et->hostKeepWords(thai, 0, thai, 1);  // Thai, then CJK
        et->hostAddOldMark("an old one kept before...");
        if (et->hostMarkCount() != had + 4 || et->hostHlWaiting() != 4) {
          printf("PUSH FAIL: kept %d (want 4), %d waiting\n", et->hostMarkCount() - had, et->hostHlWaiting());
          abort();
        }
        int first = -1, lines = -1, th = -1, old = -1;
        for (int i = 0; i < et->hostMarkCount(); i++) {
          const char* l = et->hostMarkLabel(i);
          if (!strcmp(l, "w0721 w0722 w0723")) first = i;
          if (!strncmp(l, "w0727", 5)) lines = i;
          if (strstr(l, "\xe4\xb8\x96")) th = i;  // 世
          if (!strncmp(l, "an old", 6)) old = i;
        }
        if (first < 0 || lines < 0 || th < 0 || old < 0) {
          printf("PUSH FAIL: kept labels not found (%d %d %d %d)\n", first, lines, th, old);
          abort();
        }
        if (et->hostMarkOff(first) != po || et->hostMarkEnd(first) != po + 17 ||
            strncmp(et->hostMarkId(first), "sticky-", 7) != 0) {
          printf("PUSH FAIL: first words at %u..%u (page %u) id %s\n", et->hostMarkOff(first),
                 et->hostMarkEnd(first), po, et->hostMarkId(first));
          abort();
        }
        char longId[marks::ID], thaiId[marks::ID];
        snprintf(longId, sizeof(longId), "%s", et->hostMarkId(lines));
        snprintf(thaiId, sizeof(thaiId), "%s", et->hostMarkId(th));
        {
          const char* lab = et->hostMarkLabel(lines);
          const size_t L = strlen(lab);
          if (L < 3 || strcmp(lab + L - 3, "...") != 0 || !et->hostTextWaiting(longId)) {
            printf("PUSH FAIL: the long one should be cut in its label, whole beside it\n");
            abort();
          }
        }
        // Thai + CJK: the range is the codepoints of the words (one space between).
        if (et->hostMarkEnd(th) - et->hostMarkOff(th) != (uint32_t)rhl::codepoints(et->hostMarkLabel(th))) {
          printf("PUSH FAIL: Thai range %u..%u for %d codepoints\n", et->hostMarkOff(th), et->hostMarkEnd(th),
                 rhl::codepoints(et->hostMarkLabel(th)));
          abort();
        }
        toybox.onButton(SideBtn::Ok);
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_sent");
        tapSync(nullptr);
        g_dumpEnabled = false;
        std::string all;
        for (auto& b : rhl::g_hostPuts.bodies) all += b;
        char want[160];
        snprintf(want, sizeof(want), "\"start_offset\":%u,\"end_offset\":%u,\"text\":\"w0721 w0722 w0723\"", po,
                 po + 17);
        if (et->hostHlSent() != 4 || rhl::g_hostPuts.bodies.size() != 1 || all.find(want) == std::string::npos ||
            all.find("w0727 w0728") == std::string::npos || all.find("w0743 w0744\"") == std::string::npos ||
            all.find("\"text\":\"an old one kept before\"") == std::string::npos ||
            all.find("\xe0\xb8\xaa\xe0\xb8\xa7") == std::string::npos || et->hostHlWaiting() != 0 ||
            et->hostTextWaiting(longId)) {
          printf("PUSH FAIL: sent %d in %d puts, %d still waiting:\n%s\n", et->hostHlSent(),
                 (int)rhl::g_hostPuts.bodies.size(), et->hostHlWaiting(), all.c_str());
          abort();
        }
        toybox.onTap(240, btnY);
        // Again: nothing to send.
        toybox.onButton(SideBtn::Ok);
        tapSync(nullptr);
        if (rhl::g_hostPuts.bodies.size() != 1) {
          printf("PUSH FAIL: a second sync sent again\n");
          abort();
        }
        toybox.onTap(240, btnY);
        // Readest lists them back (its copies): nothing doubles.
        {
          std::string page = "{\"cursor\":9,\"more\":false,\"items\":[";
          page += "{\"id\":\"" + std::string(thaiId) + "\",\"deleted\":0,\"spine\":0,\"text\":\"x\"}]}";
          rhl::g_hostClips.pages[hdoc] = {page};
          const int before = et->hostMarkCount();
          toybox.onButton(SideBtn::Ok);
          tapSync(nullptr);
          if (et->hostMarkCount() != before || et->hostHlAdded() != 0) {
            printf("PUSH FAIL: its own highlight came back as a second one\n");
            abort();
          }
          toybox.onTap(240, btnY);
        }
        // Removing a sent one: Readest hears of it.
        for (int i = 0; i < et->hostMarkCount(); i++)
          if (!strcmp(et->hostMarkId(i), thaiId)) et->hostRemoveMark(i);
        // An unsent one, kept and removed before any sync: nobody hears.
        et->hostKeepWords(4, 0, 4, 1);
        int fresh = -1;
        for (int i = 0; i < et->hostMarkCount(); i++)
          if (!strncmp(et->hostMarkLabel(i), et->hostLineText(4), 5) && !(et->hostMarkFlags(i) & 1)) fresh = i;
        char freshId[marks::ID];
        snprintf(freshId, sizeof(freshId), "%s", fresh >= 0 ? et->hostMarkId(fresh) : "");
        if (fresh >= 0) et->hostRemoveMark(fresh);
        if (fresh < 0 || marks::buried(freshId) || !marks::buried(thaiId) || et->hostHlWaiting() != 1) {
          printf("PUSH FAIL: removal bookkeeping (fresh %d, %d waiting)\n", fresh, et->hostHlWaiting());
          abort();
        }
        rhl::g_hostClips.pages.clear();
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_closing_waiting");
        const int bookIdx = et->hostCurIndex();
        toybox.onTap(40, 20);  // close: the strip counts the one deletion
        g_dumpEnabled = false;
        all.clear();
        for (auto& b : rhl::g_hostPuts.bodies) all += b;
        if (rhl::g_hostPuts.bodies.size() != 2 ||
            rhl::g_hostPuts.bodies[1] != "{\"items\":[{\"id\":\"" + std::string(thaiId) + "\",\"deleted\":1}]}" ||
            all.find(freshId) != std::string::npos) {
          printf("PUSH FAIL: on the way out: %s\n", all.c_str());
          abort();
        }
        et->hostOpen(bookIdx);
        toybox.tick();
        rhl::g_hostPuts = rhl::HostPuts();
        printf("readest push ok (kept, whole text, offsets, Thai, old one, once, deletions, unsent)\n");
      }

      // D. Auto sync: closing the book sends the place (the strip over the
      // page first), opening it checks Readest by itself, sleep sends it too;
      // with Auto sync off none of them touch the radio.
      {
        const int cur = et->hostCurIndex();
        int putsWas = kosync::g_server.puts;
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_closing");
        toybox.onTap(40, 20);  // the corner closes the book
        g_dumpEnabled = false;
        if (kosync::g_server.puts != putsWas + 1 || et->hostCurIndex() != -1) {
          printf("AUTO FAIL: closing sent %d places (book %d)\n", kosync::g_server.puts - putsWas,
                 et->hostCurIndex());
          abort();
        }
        const int getsWas = kosync::g_server.gets;
        et->hostOpen(cur);
        if (et->hostAuto() != 1 || et->hostSyncStage() != 1) {
          printf("AUTO FAIL: opening did not start a sync (auto %d stage %d)\n", et->hostAuto(),
                 et->hostSyncStage());
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_opening");
        stickyHost.refresh(false);
        g_dumpEnabled = false;
        toybox.tick();
        if (et->hostAuto() != 0 || et->hostSyncStage() != 0 || kosync::g_server.gets != getsWas + 1 ||
            et->hostMenu() != 0) {
          printf("AUTO FAIL: after the open's sync: auto %d stage %d gets %d menu %d\n", et->hostAuto(),
                 et->hostSyncStage(), kosync::g_server.gets - getsWas, et->hostMenu());
          abort();
        }
        putsWas = kosync::g_server.puts;
        g_dumpEnabled = true;
        setScreen("tool_epub_sync_sleeping");
        toybox.beforeSleep();
        g_dumpEnabled = false;
        if (kosync::g_server.puts != putsWas + 1) {
          printf("AUTO FAIL: sleep sent %d places\n", kosync::g_server.puts - putsWas);
          abort();
        }
        // Off: the corner closes with no radio, the open does not sync.
        stickyHost.prefs().putBool(rhl::K_AUTO, false);
        homenet::g_hostJoins = 0;
        toybox.onTap(40, 20);
        et->hostOpen(cur);
        toybox.beforeSleep();
        if (homenet::g_hostJoins != 0 || et->hostAuto() != 0 || et->hostSyncStage() != 0) {
          printf("AUTO FAIL: auto sync off still joined %d times\n", homenet::g_hostJoins);
          abort();
        }
        // Only the open: closing is quiet.
        stickyHost.prefs().putBool(rhl::K_AUTO, true);
        stickyHost.prefs().putBool(rhl::K_CLOSE, false);
        putsWas = kosync::g_server.puts;
        toybox.onTap(40, 20);
        if (kosync::g_server.puts != putsWas) {
          printf("AUTO FAIL: 'when it closes' off still sent the place\n");
          abort();
        }
        et->hostOpen(cur);
        if (et->hostAuto() != 1) {
          printf("AUTO FAIL: 'when a book opens' on did not sync\n");
          abort();
        }
        toybox.tick();
        stickyHost.prefs().remove(rhl::K_CLOSE);
        printf("auto sync ok (close, open, sleep, off, open only)\n");
      }

      // 5. Failures say what failed, and the place on the card stays.
      kosync::g_server.fail = kosync::Result::AuthFailed;
      toybox.onButton(SideBtn::Ok);
      tapSync(nullptr);
      if (et->hostSyncOut() != 3) {
        printf("SYNC FAIL: a refused account gave out %d\n", et->hostSyncOut());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_sync_refused");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      toybox.onTap(240, btnY);
      kosync::g_server.fail = kosync::Result::Ok;
      homenet::g_hostNet = homenet::Result::NotFound;
      toybox.onButton(SideBtn::Ok);
      tapSync(nullptr);
      if (et->hostSyncOut() != 3 || kosync::g_server.gets == 0) {
        printf("SYNC FAIL: a missing router gave out %d\n", et->hostSyncOut());
        abort();
      }
      toybox.onTap(240, btnY);
      homenet::g_hostNet = homenet::Result::Ok;

      // Back as the rest of this test found it: no account, the panel up,
      // the first chapter on the page.
      homenet::forgetWifi(stickyHost.prefs());
      kosync::forgetAccount(stickyHost.prefs());
      et->hostGoto(spineHere, offHere);
      toybox.onButton(SideBtn::Ok);
      g_dumpEnabled = false;  // as the rotation checks above left it
      printf("kosync panel ok (upload reads back, ask/stay/jump, percentage fallback, in step, "
             "failures, radio off each run)\n");
    }
    // Row 6 now: "Start again" sits between the rotation buttons and the way
    // out, so a thumb reaching for one cannot land on the other.
    toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book
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
      // Into the book's own folder, over the position it already holds.
      sdcard::hostPlantSide(posOf("/books/wind.epub"), buf, n);
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
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book  // CLOSE THE BOOK
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
      sdcard::hostPlantSide(posOf("/books/wind.epub"), buf, n);
      toybox.open(false, 10);
      auto* et3 = static_cast<EpubTool*>(toybox.hostActive());
      toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
      if (et3->hostScreen() != 1 || et3->hostSpine() != 2 ||
          strcmp(et3->hostPageImage(), "OEBPS/images/art2.png") != 0) {
        printf("EPUB APP FAIL: the picture-only chapter opened as s%d img '%s'\n",
               et3->hostSpine(), et3->hostPageImage());
        abort();
      }
      // A landscape picture is four pages (part 0 turned, three slices) at one
      // offset, and resume takes the LAST page at the saved offset -- the last
      // slice. Back through the parts, each a page...
      for (int part = 3; part > 0; part--) {
        if (et3->hostPageImagePart() != part) {
          printf("EPUB APP FAIL: expected landscape part %d, on part %d (s%d p%d)\n", part,
                 et3->hostPageImagePart(), et3->hostSpine(), et3->hostPage());
          abort();
        }
        toybox.onButton(SideBtn::Up);
      }
      if (et3->hostPageImagePart() != 0 || strcmp(et3->hostPageImage(), "OEBPS/images/art2.png")) {
        printf("EPUB APP FAIL: UP through the landscape parts did not reach part 0\n");
        abort();
      }
      // ...then out of it, into the chapter of words before it...
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
      // Forward through the parts lands on each in turn: the turned whole,
      // then the slices left to right.
      for (int part = 0; part < 4; part++) {
        if (et3->hostPageImagePart() != part || et3->hostSpine() != 2) {
          printf("EPUB APP FAIL: DOWN through the landscape parts: wanted part %d, got %d (s%d)\n",
                 part, et3->hostPageImagePart(), et3->hostSpine());
          abort();
        }
        if (part < 3) toybox.onButton(SideBtn::Down);
      }
      toybox.onButton(SideBtn::Ok);                       // the panel
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book  // CLOSE THE BOOK
      printf("epub picture chapters ok (reachable both ways, never blank)\n");
    }
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("epub reader ok (opens, turns, replays, and trades positions with CrossPoint)\n");
  }

  // --- the recently-read strip -----------------------------------------------
  // The book tests above opened a real book, so the Study drawer now carries
  // its cover. A .tbk entry left by the manga reader (removed in v2) is
  // dropped rather than shown as a cover nothing can open. Tapping a cover
  // reopens that book directly.
  {
    g_dumpEnabled = false;
    recents::note(stickyHost.prefs(), recents::KIND_TBK, "/books/old-manga.tbk", "Old manga");
    recents::note(stickyHost.prefs(), recents::KIND_EPUB, "/books/wind.epub", "wind");
    recents::Entry rec[recents::MAX];
    const int n = recents::list(stickyHost.prefs(), rec);
    if (n != 1 || rec[0].kind != recents::KIND_EPUB || strcmp(rec[0].file, "/books/wind.epub") != 0) {
      printf("RECENTS FAIL: %d entries, front '%s'\n", n, n ? rec[0].file : "");
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

    // The title rows sit under the one tile row; row 0 is the epub.
    const int rowY = hubui::FOLDER_TOP + 1 * hubui::ROW_STEP + hubui::REC_GAP +
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
    toybox.hostHub().goHome();

    // The DOWN hold's new job: carry on READING. With recents present it
    // lands inside the last book at its saved page, not on any list.
    if (!toybox.carryOnReading() || toybox.hostIdx() != 10 ||
        static_cast<EpubTool*>(toybox.hostActive())->hostScreen() != 1) {
      printf("RECENTS FAIL: carry on reading did not land inside the book\n");
      abort();
    }
    toybox.onButton(SideBtn::Ok);
    toybox.goHub();

  // Relocated below the recents guard: both of these open books, and the
  // strip above pins an exact order.
  {
    // --- a pre-v2 position moves into the book's folder ---------------------
    // Before v2 the place lived in /.crosspoint/epub_<murmur32> (CrossPoint's,
    // read first) and epub_<fnv64> (older Toybox), with the chapter note
    // toybox.pos beside the second. A book with nothing in its folder brings
    // the place across once -- opened where it was, the note copied, the old
    // files deleted and the old folders removed when that empties them -- and
    // a CrossPoint cache file sharing the folder is left alone.
    {
      epubc::Progress cp;
      cp.spine = 1;
      cp.page = 1;
      cp.pageCount = 9;
      cp.offset = 2500;
      cp.hasOffset = true;
      uint8_t buf[10];
      const int n = epubc::encodeProgress(cp, buf);
      static const char kMur[] = "/.crosspoint/epub_836526750";
      static const char kFnv[] = "/.crosspoint/epub_1175141337288249041";
      char pth[128];
      sdcard::hostPlantSide(posOf("/books/wind.epub"), buf, 0);  // nothing in the folder
      char place[bstore::PATH_CAP];
      bstore::file("/books/wind.epub", "place.txt", place, sizeof(place));
      sdcard::hostPlantSide(place, buf, 0);
      snprintf(pth, sizeof(pth), "%s/progress.bin", kMur);
      sdcard::hostPlantSide(pth, buf, n);
      snprintf(pth, sizeof(pth), "%s/progress.bin", kFnv);
      epubc::Progress older = cp;
      older.spine = 0;  // the stale copy: CrossPoint's folder must win
      uint8_t ob[10];
      sdcard::hostPlantSide(pth, ob, epubc::encodeProgress(older, ob));
      const char kNote[] = "3\t2\t9\tThe Moved Chapter\n";
      snprintf(pth, sizeof(pth), "%s/toybox.pos", kFnv);
      sdcard::hostPlantSide(pth, kNote, (int)strlen(kNote));
      snprintf(pth, sizeof(pth), "%s/book.bin", kMur);  // CrossPoint's own cache
      sdcard::hostPlantSide(pth, "cp", 2);
      toybox.open(false, 10);
      auto* etm = static_cast<EpubTool*>(toybox.hostActive());
      toybox.onTap(240, epubui::LIST_Y0 + epubui::LIST_ROW_H + 10);
      if (etm->hostScreen() != 1 || etm->hostSpine() != 1 || etm->hostPageOffset() > 2500) {
        printf("EPUB APP FAIL: the old position was not taken (s%d off %u)\n", etm->hostSpine(),
               etm->hostPageOffset());
        abort();
      }
      uint8_t back[64];
      epubc::Progress q;
      int m = sdcard::hostReadSide(posOf("/books/wind.epub"), back, 16);
      if (m != 10 || !epubc::decodeProgress(back, m, q) || q.spine != 1 || q.offset != 2500) {
        printf("EPUB APP FAIL: the position did not move into the book folder (n=%d)\n", m);
        abort();
      }
      m = sdcard::hostReadSide(place, back, sizeof(back));
      if (m != (int)strlen(kNote) || memcmp(back, kNote, (size_t)m) != 0) {
        printf("EPUB APP FAIL: the chapter note did not move (n=%d)\n", m);
        abort();
      }
      const char* gone[] = {"/progress.bin", "/toybox.pos"};
      for (const char* g : gone)
        for (const char* d : {kMur, kFnv}) {
          snprintf(pth, sizeof(pth), "%s%s", d, g);
          if (sdcard::hostReadSide(pth, back, 16) > 0) {
            printf("EPUB APP FAIL: %s survived the move\n", pth);
            abort();
          }
        }
      snprintf(pth, sizeof(pth), "%s/book.bin", kMur);
      if (sdcard::hostReadSide(pth, back, 16) != 2) {
        printf("EPUB APP FAIL: CrossPoint's own cache file was touched\n");
        abort();
      }
      sdcard::hostPlantSide(pth, back, 0);
      toybox.onButton(SideBtn::Ok);
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book  // CLOSE THE BOOK
      printf("position move ok (old folders read once, note moved, old files gone)\n");
    }

    // --- the Readest library ---------------------------------------------
    // Signed in to Readest with a home Wi-Fi saved, the top shelf opens with
    // "From Readest". Guarded: pages of the cloud library; a download lands in
    // /books/Readest under a FAT-safe name and opens; a book already there
    // shows its tick and opens without a download; the phone's search and
    // download requests are carried out; Cancel leaves no file; a failure
    // says so; the radio is off once the screen is left.
    {
      homenet::saveWifi(stickyHost.prefs(), "Somchai 5G", "correct horse");
      kosync::saveAccount(stickyHost.prefs(), kosync::READEST_URL, "reader@example.com", "devkey-123");
      kosync::g_hostLibBooks = 11;
      kosync::g_hostLibFail = kosync::Result::Ok;
      kosync::g_hostBookBytes = 50000;
      homenet::g_hostOffs = 0;
      toybox.open(false, 10);
      auto* el = static_cast<EpubTool*>(toybox.hostActive());
      g_dumpEnabled = true;
      setScreen("tool_epub_shelf_readest");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      toybox.onTap(240, shelf::Y0 + 10);  // row 0: From Readest
      if (el->hostLibStage() != 1) {
        printf("LIB FAIL: the Readest row did not open the library (stage %d)\n", el->hostLibStage());
        abort();
      }
      toybox.tick();
      if (el->hostLibStage() != 2 || el->hostLibCount() != kosync::LIB_PER_PAGE || !el->hostLibMore() ||
          !el->hostLibWifi()) {
        printf("LIB FAIL: page one: stage %d, %d books, more %d\n", el->hostLibStage(),
               el->hostLibCount(), el->hostLibMore());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_readest_list");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      const int btnY = 690 + 35;
      toybox.onTap(400, btnY);  // page 2 >
      toybox.tick();
      if (el->hostLibCount() != 3 || el->hostLibMore()) {
        printf("LIB FAIL: page two had %d books, more %d\n", el->hostLibCount(), el->hostLibMore());
        abort();
      }
      toybox.onTap(60, btnY);  // < page 1
      toybox.tick();
      // Book 03's title carries a slash and quotes: its file name must not.
      kosync::LibBook b3;
      snprintf(b3.title, sizeof(b3.title), "%s", el->hostLibTitle(2));
      char p3[160];
      kosync::libPath(b3, p3, sizeof(p3));
      if (strncmp(p3, "/books/Readest/Book 03_ A_B ", 28) != 0 || strchr(p3 + 15, '"') ||
          strchr(p3 + 15, ':')) {
        printf("LIB FAIL: an unsafe title became '%s'\n", p3);
        abort();
      }
      // Download Book 01: progress, then the book itself.
      toybox.onTap(240, 64 + 10);
      if (el->hostLibStage() != 3) {
        printf("LIB FAIL: tapping a book did not start a download (stage %d)\n", el->hostLibStage());
        abort();
      }
      toybox.tick();
      g_dumpEnabled = true;
      setScreen("tool_epub_readest_download");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      for (int i = 0; i < 40 && el->hostScreen() != 1; i++) toybox.tick();
      if (el->hostScreen() != 1 || !sdcard::hostHasBook("/books/Readest/Book 01.epub") ||
          el->hostLibWifi()) {
        printf("LIB FAIL: the download did not open as a book (screen %d, on card %d, wifi %d)\n",
               el->hostScreen(), sdcard::hostHasBook("/books/Readest/Book 01.epub"), el->hostLibWifi());
        abort();
      }
      if (sdcard::hostCardFileSize("/books/Readest/Book 01.epub.part") > 0) {
        printf("LIB FAIL: the .part file was left behind\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book  // CLOSE (8 rows: Sync is there)
      toybox.onTap(20, 20);  // out of /books/Readest to the top shelf
      // Again: Book 01 is now on the card -- a tick, and a tap opens it.
      toybox.onTap(240, shelf::Y0 + 10);
      toybox.tick();
      if (!el->hostLibLocal(0) || el->hostLibLocal(1)) {
        printf("LIB FAIL: the tick marks are %d %d\n", el->hostLibLocal(0), el->hostLibLocal(1));
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_readest_local");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      toybox.onTap(240, 64 + 10);
      if (el->hostScreen() != 1 || el->hostLibStage() != 0) {
        printf("LIB FAIL: a book already on the card did not open directly\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book
      toybox.onTap(20, 20);

      // The phone: SEARCH puts up the QR; a search from the page filters the
      // list here and publishes it there; a download asked from the page runs.
      toybox.onTap(240, shelf::Y0 + 10);
      toybox.tick();
      toybox.onTap(240, btnY);  // SEARCH
      if (el->hostLibStage() != 5 || !el->hostWeb().running()) {
        printf("LIB FAIL: SEARCH did not put up the phone page (stage %d)\n", el->hostLibStage());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_readest_phone");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      el->hostWeb().hostSearch("Book 1");
      toybox.tick();
      if (el->hostLibStage() != 2 || el->hostLibCount() != 2 ||
          kosync::g_hostLastQuery != "Book 1" || !strstr(el->hostWeb().hostResults().c_str(), "Book 11")) {
        printf("LIB FAIL: the phone's search gave stage %d, %d books, query '%s'\n",
               el->hostLibStage(), el->hostLibCount(), kosync::g_hostLastQuery.c_str());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_readest_search");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      char id10[33];
      md5t::hex("Book 10", id10);
      el->hostWeb().hostDownload(id10);
      toybox.tick();
      if (el->hostLibStage() != 3) {
        printf("LIB FAIL: the phone's download did not start (stage %d)\n", el->hostLibStage());
        abort();
      }
      for (int i = 0; i < 40 && el->hostScreen() != 1; i++) toybox.tick();
      if (el->hostScreen() != 1 || !sdcard::hostHasBook("/books/Readest/Book 10.epub")) {
        printf("LIB FAIL: the phone's download did not open\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book
      toybox.onTap(20, 20);

      // Cancel part-way: no book, no .part.
      kosync::g_hostBookBytes = 400000;
      toybox.onTap(240, shelf::Y0 + 10);
      toybox.tick();
      toybox.onTap(240, 64 + 76 + 10);  // Book 02
      toybox.tick();
      toybox.onTap(240, btnY);  // CANCEL
      if (el->hostLibStage() != 2 || sdcard::hostHasBook("/books/Readest/Book 02.epub") ||
          sdcard::hostCardFileSize("/books/Readest/Book 02.epub.part") > 0) {
        printf("LIB FAIL: cancel left stage %d / a file behind\n", el->hostLibStage());
        abort();
      }
      // A failure says so, and TRY AGAIN tries again.
      kosync::g_hostLibFail = kosync::Result::NetworkError;
      toybox.onTap(400, btnY);  // page 2: fails
      toybox.tick();
      if (el->hostLibStage() != 4) {
        printf("LIB FAIL: a failed list did not show the error (stage %d)\n", el->hostLibStage());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_readest_error");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      kosync::g_hostLibFail = kosync::Result::Ok;
      toybox.onTap(240, btnY);
      toybox.tick();
      if (el->hostLibStage() != 2) {
        printf("LIB FAIL: TRY AGAIN did not recover\n");
        abort();
      }
      const int offsBefore = homenet::g_hostOffs;
      toybox.onTap(20, 20);  // leave: radio off, phone page down
      if (el->hostLibStage() != 0 || el->hostLibWifi() || el->hostWeb().running() ||
          homenet::g_hostOffs != offsBefore + 1) {
        printf("LIB FAIL: leaving left the radio %d / web %d\n", el->hostLibWifi(),
               el->hostWeb().running());
        abort();
      }
      kosync::forgetAccount(stickyHost.prefs());
      homenet::forgetWifi(stickyHost.prefs());
      sdcard::hostClearBooks();
      // The three books' folders (position, place) and the downloads: the fake
      // card has two dozen small-file slots, and later guards need them.
      sdcard::hostCardRemovePrefix("/.toybox/books/Book ");
      sdcard::hostCardRemovePrefix(kosync::LIB_DIR);
      kosync::g_hostBookBytes = 50000;
      toybox.goHub();
      printf("readest library ok (pages, download opens, ticks, phone search + download, cancel, error, radio off)\n");
    }

    // --- series rows: volumes grouped by their names ---------------------
    // Three volumes of one series (spelled three ways, out of order) become
    // one row named after the series; a single "Part 2" and a "1984" stay
    // books. Inside the row: volume order, the volume as the name, and back
    // returns to the level it came from. Closing a book keeps the row open.
    {
      sdcard::hostAddBook("/books/That Time I Got Reincarnated as a Slime - Volume 10.epub");
      sdcard::hostAddBook("/books/That Time I Got Reincarnated as a Slime - Volume 02.epub");
      sdcard::hostAddBook("/books/that time i got reincarnated as a slime vol 1.epub");
      sdcard::hostAddBook("/books/Dune Part 2.epub");
      sdcard::hostAddBook("/books/1984.epub");
      toybox.open(false, 10);  // fresh enter: the shelf re-lists
      auto* es = static_cast<EpubTool*>(toybox.hostActive());
      if (es->hostGroups() != 1 || strcmp(es->hostGroupName(0), "That Time I Got Reincarnated as a Slime") != 0 ||
          es->hostViewCount() != 4) {
        printf("SERIES FAIL: %d groups ('%s'), %d loose books\n", es->hostGroups(),
               es->hostGroupName(0), es->hostViewCount());
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_series_shelf");
      stickyHost.refresh(true);
      const int groupRow = es->hostFolders();  // right after the real folders
      setScreen("tool_epub_series_open");
      toybox.onTap(240, shelf::Y0 + groupRow * shelf::ROW_H + 10);
      g_dumpEnabled = false;
      if (es->hostInGroup() != 0 || es->hostViewCount() != 3) {
        printf("SERIES FAIL: the series row opened as group %d with %d books\n",
               es->hostInGroup(), es->hostViewCount());
        abort();
      }
      char f0[128], f1[128], f2[128];
      es->hostFile(es->hostViewBook(0), f0, sizeof(f0));
      es->hostFile(es->hostViewBook(1), f1, sizeof(f1));
      es->hostFile(es->hostViewBook(2), f2, sizeof(f2));
      if (!strstr(f0, "vol 1") || !strstr(f1, "Volume 02") || !strstr(f2, "Volume 10")) {
        printf("SERIES FAIL: volume order %s | %s | %s\n", f0, f1, f2);
        abort();
      }
      toybox.onTap(240, shelf::Y0 + 1 * shelf::ROW_H + 10);  // Volume 02
      if (es->hostScreen() != 1) {
        printf("SERIES FAIL: a volume in the row did not open\n");
        abort();
      }
      toybox.onButton(SideBtn::Ok);
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book  // CLOSE THE BOOK
      if (es->hostScreen() != 0 || es->hostInGroup() != 0) {
        printf("SERIES FAIL: closing a volume left the row (group %d)\n", es->hostInGroup());
        abort();
      }
      toybox.onTap(20, 20);  // back: out of the row, not out of the app
      if (!toybox.hostInApp() || es->hostInGroup() != -1 || es->hostGroups() != 1) {
        printf("SERIES FAIL: back from the row went to group %d\n", es->hostInGroup());
        abort();
      }
      // Owner, 26 Nov: the shelf puts the book read last first. The last
      // loose book, opened and closed, leads the list after it.
      {
        const int bi = es->hostViewBook(es->hostViewCount() - 1);
        char want[128], first[128], got[128];
        es->hostFile(bi, want, sizeof(want));
        es->hostFile(es->hostViewBook(0), first, sizeof(first));
        if (!strcmp(want, first)) {
          printf("SHELF ORDER FAIL: the last book is already first\n");
          abort();
        }
        es->hostOpen(bi);
        toybox.onTap(40, 20);  // close: the shelf re-lists
        es->hostFile(es->hostViewBook(0), got, sizeof(got));
        if (strcmp(got, want) != 0) {
          printf("SHELF ORDER FAIL: after reading %s the shelf starts with %s\n", want, got);
          abort();
        }
        g_dumpEnabled = true;
        setScreen("tool_epub_shelf_recent_first");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
        printf("shelf order ok (the book read last leads)\n");
      }
      sdcard::hostClearBooks();
      toybox.goHub();
      printf("series rows ok (three spellings grouped, volume order, back to the shelf)\n");
    }

    // --- whole names and the sync mark on the shelf ------------------------
    // A release name longer than the 40-byte title field is drawn whole over
    // two lines; a started book with a "synced" marker shows the mark; one
    // without it does not.
    {
      const char* kLong = "/books/Classroom of the Elite - Volume 01 [Seven Seas][Kobo].epub";
      const char* kPlain = "/books/Plain Book.epub";
      sdcard::hostAddBook(kLong);
      sdcard::hostAddBook(kPlain);
      char p[bstore::PATH_CAP];
      for (const char* f : {kLong, kPlain}) {
        bstore::file(f, "position.bin", p, sizeof(p));
        sdcard::hostPutCardFile(p, "\x01\x00\x00\x00\x00\x00", 6);
      }
      bstore::file(kLong, "synced", p, sizeof(p));
      sdcard::hostPutCardFile(p, "1", 1);
      toybox.open(false, 10);
      auto* es = static_cast<EpubTool*>(toybox.hostActive());
      if (!es->hostBookSynced(kLong) || es->hostBookSynced(kPlain)) {
        printf("SHELF FAIL: synced flags %d %d (want 1 0)\n", es->hostBookSynced(kLong),
               es->hostBookSynced(kPlain));
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_epub_shelf_names_sync");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      sdcard::hostCardRemovePrefix("/.toybox/books/Classroom");
      sdcard::hostCardRemovePrefix("/.toybox/books/Plain");
      sdcard::hostClearBooks();
      toybox.goHub();
      printf("shelf names ok (whole names, sync mark only where synced)\n");
    }

    // --- the CrossInk shelf ---------------------------------------------------
    // /Read (and /epub) appear as folders beside the series, named by their
    // absolute path; opening one lists and opens its books, and the position
    // lands in that book's own folder.
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
      toybox.onButton(SideBtn::Ok);
      toybox.onTap(40, 20);  // the Back corner closes the book  // CLOSE: the save lands
      uint8_t pb[16];
      if (sdcard::hostReadSide(posOf("/Read/novel.epub"), pb, 16) != 10) {
        sdcard::hostDumpSides();
        printf("EPUB APP FAIL: /Read progress not in its book folder\n");
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

    // --- no covers traded with CrossInk any more ---------------------------
    // v2 dropped the CrossPoint cover cache (owner, 18 Nov): opening
    // wind.epub earlier must not have left a cover.bmp in its old folder.
    {
      char dir[64], p[112];
      epubc::cacheDir("/books/wind.epub", dir, sizeof(dir));
      snprintf(p, sizeof(p), "%s/cover.bmp", dir);
      if (sdcard::hostCardFileSize(p) > 0) {
        printf("BMP FAIL: a cover.bmp was left in %s\n", dir);
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
    tapAct(setui::ACT_WALL);
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
      char p[bstore::PATH_CAP];
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

  // --- one folder per book (book_store.h) -----------------------------------
  // Everything the device makes about a book lives in one folder named after
  // it. Asserted: the names (readable, safe for FAT, whole UTF-8 characters,
  // two same-named books apart), the cover decoded rather than taken from
  // CrossPoint's cache, the picture index, pictures from the old layout moved
  // across rather than prepared again, and old bookmarks moved on first read.
  {
    g_dumpEnabled = false;
    char d[bstore::PATH_CAP], d2[bstore::PATH_CAP], nm[bstore::NAME_BYTES + 1];
    bstore::dir("/books/Classroom of the Elite - Volume 01 [Seven Seas][Kobo].epub", d,
                sizeof(d));
    const char* want = "/.toybox/books/Classroom of the Elite - Volume 01 [Seven Seas][Kobo] ~";
    if (strncmp(d, want, strlen(want)) != 0 || strlen(d) != strlen(want) + 4) {
      printf("BOOK FOLDER FAIL: named '%s'\n", d);
      abort();
    }
    bstore::dir("/books/a/Vol 01.epub", d, sizeof(d));
    bstore::dir("/books/b/Vol 01.epub", d2, sizeof(d2));
    if (strcmp(d, d2) == 0) {
      printf("BOOK FOLDER FAIL: two books share '%s'\n", d);
      abort();
    }
    bstore::name("/books/what: a *title*?.epub", nm, sizeof(nm));
    if (strcmp(nm, "what_ a _title__") != 0) {
      printf("BOOK FOLDER FAIL: unsafe name came out '%s'\n", nm);
      abort();
    }
    bstore::name("/books/odd. .epub", nm, sizeof(nm));
    if (strcmp(nm, "odd") != 0) {
      printf("BOOK FOLDER FAIL: trailing dot/space kept: '%s'\n", nm);
      abort();
    }
    {  // "a" + forty Thai letters (3 bytes each): 72 bytes falls mid-letter
      std::string thai = "/books/a";
      for (int i = 0; i < 40; i++) thai += "\xE0\xB8\x81";
      thai += ".epub";
      bstore::name(thai.c_str(), nm, sizeof(nm));
      const int len = (int)strlen(nm);
      if (len != 70 || nm[0] != 'a' || (len - 1) % 3 != 0) {
        printf("BOOK FOLDER FAIL: a long Thai name cut to %d bytes\n", len);
        abort();
      }
    }
    {  // the longest leaf, with the atomic writer's ".tmp", still fits
      std::string longest = "/books/";
      longest.append(200, 'x');
      longest += ".epub";
      char p[bstore::PATH_CAP];
      bstore::file(longest.c_str(), "ffffffff.5.g2", p, sizeof(p));
      if (strlen(p) + 4 >= (size_t)bstore::PATH_CAP) {
        printf("BOOK FOLDER FAIL: a path of %zu bytes\n", strlen(p));
        abort();
      }
    }

    // The cover: decoded from the book. A cover.bmp sitting in the old
    // CrossPoint folder is never read (v2 dropped that cache).
    // The long-named book: no sidecar of the owner's, and opened just above.
    static const char kBook[] =
        "/books/A Book With The Kind Of Very Long Release Filename Publishers Actually Use Vol "
        "01.epub";
    char p[bstore::PATH_CAP];
    bthumb::path(kBook, p, sizeof(p));
    tfs::remove(p);
    strncat(p, "f", sizeof(p) - strlen(p) - 1);
    tfs::remove(p);
    bthumb::bigPath(kBook, p, sizeof(p));
    sdcard::hostCardRemove(p);
    {
      static uint8_t bmp[14 + 40 + 1024 + 240 * 400];  // 8-bit, every pixel black
      const uint32_t off = 14 + 40 + 1024, total = off + 240 * 400;
      memset(bmp, 0, sizeof(bmp));
      bmp[0] = 'B'; bmp[1] = 'M';
      bmp[2] = (uint8_t)total; bmp[3] = (uint8_t)(total >> 8); bmp[4] = (uint8_t)(total >> 16);
      bmp[10] = (uint8_t)off; bmp[11] = (uint8_t)(off >> 8);
      bmp[14] = 40;
      bmp[18] = 240; bmp[22] = (uint8_t)(400 & 255); bmp[23] = (uint8_t)(400 >> 8);
      bmp[26] = 1; bmp[28] = 8;
      for (int i = 0; i < 256; i++) bmp[54 + i * 4] = bmp[55 + i * 4] = bmp[56 + i * 4] = (uint8_t)i;
      char dir[64], cp[112];
      epubc::cacheDir(kBook, dir, sizeof(dir));
      snprintf(cp, sizeof(cp), "%s/cover.bmp", dir);
      sdcard::hostPutCardFile(cp, bmp, (int)total);
    }
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* eb = static_cast<EpubTool*>(toybox.hostActive());
    if (!eb->openDirect(kBook)) {
      printf("BOOK FOLDER FAIL: %s did not open\n", kBook);
      abort();
    }
    {
      char g2[bstore::PATH_CAP], b1[bstore::PATH_CAP];
      static uint8_t cov[48000], pic[48000];
      bthumb::bigPath(kBook, p, sizeof(p));
      if (!eb->hostArtPaths("OEBPS/images/plate.png", 0, g2, b1, sizeof(g2)) ||
          sdcard::readWhole(p, cov, sizeof(cov)) != 48000 ||
          sdcard::readWhole(b1, pic, sizeof(pic)) != 48000 || memcmp(cov, pic, 48000) != 0) {
        printf("BOOK FOLDER FAIL: the cover was not decoded from the book (CrossPoint's taken?) "
               "paths %d cover %d pic %d same %d [%s] [%s]\n",
               eb->hostArtPaths("OEBPS/images/plate.png", 0, g2, b1, sizeof(g2)),
               sdcard::readWhole(p, cov, sizeof(cov)), sdcard::readWhole(b1, pic, sizeof(pic)),
               memcmp(cov, pic, 48000) == 0, p, b1);
        abort();
      }
      bstore::dir(kBook, d, sizeof(d));
      if (strncmp(p, d, strlen(d)) != 0 || strncmp(b1, d, strlen(d)) != 0) {
        printf("BOOK FOLDER FAIL: cover %s / picture %s outside %s\n", p, b1, d);
        abort();
      }
    }
    bstore::file(kBook, "art.idx", p, sizeof(p));
    if (!sdcard::hostCardHasPrefix(p)) {
      printf("BOOK FOLDER FAIL: no picture index at %s\n", p);
      abort();
    }

    // Pictures from the old layout: /.toybox/art2/<hash>/<pic>.<part>.g2|b1
    // and a one-byte .inf. Rewind this book to that state -- plate.png moved
    // there and overwritten with a pattern no decoder makes, missing.png
    // remembered as undecodable, the index gone -- and reopen.
    const uint32_t uPlate = eb->hostArtUsize("OEBPS/images/plate.png");
    const uint32_t uMiss = eb->hostArtUsize("OEBPS/images/missing.png");
    toybox.goHub();
    auto fnv = [](const char* s) { return bstore::key(s); };
    char od[64];
    snprintf(od, sizeof(od), "/.toybox/art%d/%08lx", epubart::PIPELINE, (unsigned long)fnv(kBook));
    const uint32_t pkPlate = fnv("OEBPS/images/plate.png") ^ (uPlate * 2654435761u);
    const uint32_t pkMiss = fnv("OEBPS/images/missing.png") ^ (uMiss * 2654435761u);
    char g2n[bstore::PATH_CAP], b1n[bstore::PATH_CAP], g2o[96], b1o[96], inf[96];
    epubart::paths(kBook, "OEBPS/images/plate.png", uPlate, 0, g2n, b1n, sizeof(g2n));
    snprintf(g2o, sizeof(g2o), "%s/%08lx.0.g2", od, (unsigned long)pkPlate);
    snprintf(b1o, sizeof(b1o), "%s/%08lx.0.b1", od, (unsigned long)pkPlate);
    static uint8_t pat[96000];
    memset(pat, 0x55, sizeof(pat));
    sdcard::hostCardRemove(g2n);
    sdcard::hostCardRemove(b1n);
    sdcard::hostPutCardFile(g2o, pat, 96000);
    sdcard::hostPutCardFile(b1o, pat, 48000);
    const uint8_t one = 1, zero = 0;
    snprintf(inf, sizeof(inf), "%s/%08lx.inf", od, (unsigned long)pkPlate);
    sdcard::hostPutCardFile(inf, &one, 1);
    snprintf(inf, sizeof(inf), "%s/%08lx.inf", od, (unsigned long)pkMiss);
    sdcard::hostPutCardFile(inf, &zero, 1);
    sdcard::hostCardRemove(p);  // art.idx
    toybox.open(false, 10, false);
    eb = static_cast<EpubTool*>(toybox.hostActive());
    if (!eb->openDirect(kBook)) {
      printf("BOOK FOLDER FAIL: %s did not reopen\n", kBook);
      abort();
    }
    {
      static uint8_t got[96000];
      char odSlash[72];
      snprintf(odSlash, sizeof(odSlash), "%s/", od);
      if (sdcard::readWhole(g2n, got, 96000) != 96000 || memcmp(got, pat, 96000) != 0 ||
          sdcard::readWhole(b1n, got, 48000) != 48000 || memcmp(got, pat, 48000) != 0) {
        printf("BOOK FOLDER FAIL: an old picture was prepared again, not moved\n");
        abort();
      }
      if (sdcard::hostCardHasPrefix(odSlash)) {
        printf("BOOK FOLDER FAIL: files left behind in %s\n", od);
        abort();
      }
      if (eb->hostArtPrepared("OEBPS/images/missing.png") ||
          eb->hostArtParts("OEBPS/images/plate.png") != 1 ||
          eb->hostArtParts("OEBPS/images/art2.png") != 4 || !sdcard::hostCardHasPrefix(p)) {
        printf("BOOK FOLDER FAIL: after the move: missing %d, plate %d parts, art2 %d, index %d\n",
               eb->hostArtPrepared("OEBPS/images/missing.png"),
               eb->hostArtParts("OEBPS/images/plate.png"),
               eb->hostArtParts("OEBPS/images/art2.png"), sdcard::hostCardHasPrefix(p));
        abort();
      }
    }
    toybox.goHub();
    // Leave nothing odd for later guards: the patterned picture goes, and the
    // next open of this book prepares it properly.
    sdcard::hostCardRemove(g2n);
    sdcard::hostCardRemove(b1n);
    sdcard::hostCardRemove(p);

    // Bookmarks kept before the folders: read from the old place, written to
    // the new one on that same read, and the old file gone.
    {
      static const char kMarked[] = "/books/marked-before.epub";
      marks::Mark mm[2] = {};
      mm[0].spine = 3;
      mm[0].off = 1234;
      snprintf(mm[0].label, sizeof(mm[0].label), "the phrase that was kept");
      mm[1].spine = 5;
      char np[bstore::PATH_CAP], op[48];
      marks::path(kMarked, np, sizeof(np));
      marks::oldPath(kMarked, op, sizeof(op));
      sdcard::browseOpen();
      marks::save(stickyHost, kMarked, mm, 2);  // into the new place...
      sdcard::hostCardMove(np, op);             // ...then back to where it used to live
      marks::Mark back[marks::MAX];
      const int n = marks::load(stickyHost, kMarked, back);
      const bool newThere = sdcard::hostCardHasPrefix(np), oldThere = sdcard::hostCardHasPrefix(op);
      sdcard::browseClose();
      if (n != 2 || back[0].off != 1234 || strcmp(back[0].label, "the phrase that was kept") != 0 ||
          !newThere || oldThere) {
        printf("BOOK FOLDER FAIL: old marks: %d read, new %d, old %d\n", n, newThere, oldThere);
        abort();
      }
    }
    printf("book folders ok (named, cover decoded first, index, old pictures and marks moved)\n");
    g_dumpEnabled = true;
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
      char big[bstore::PATH_CAP];
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
    // More > Settings > Start again: the bar at the foot asks; its button is
    // the second tap.
    auto openStart = [&]() {
      const TRect mc = rmenu::moreCorner(480);
      toybox.onTap(mc.x + mc.w / 2, mc.y + mc.h / 2);
      tapRect(rmenu::moreTile(4, 480, 800));
      tapRect(rmenu::moreSetTile(3, 480, 800));
    };
    openStart();
    g_dumpEnabled = true;
    setScreen("epub_start_again_armed");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    if (er->hostMenu() != (int)rmenu::Page::StartBar || er->hostSpine() != spineWas) {
      printf("RESET FAIL: the first tap did something (menu %d, s%d)\n", er->hostMenu(),
             er->hostSpine());
      abort();
    }
    // A tap somewhere else takes the question away rather than leaving it
    // armed under a thumb: into Text, back out, and the next tap on the row
    // must only ASK again.
    toybox.onTap(240, 300);  // the page: the bar goes, nothing else
    if (er->hostMenu() != 0 || er->hostSpine() != spineWas) {
      printf("RESET FAIL: the question survived a trip to another screen\n");
      abort();
    }
    openStart();
    tapRect(rmenu::startGo(480, 800));  // and confirms
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
        char path[bstore::PATH_CAP];
        bstore::file(file, "place.txt", path, sizeof(path));
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

    toybox.goHub();
    g_dumpEnabled = true;
  }

  // --- landscape artwork on the panel -----------------------------------------
  // Chapter three's picture is landscape, so its first page is the whole
  // picture turned a quarter clockwise. Checked where it matters, in the
  // panel's own buffer through the panel's own mapping: the fixture's dark
  // band (the picture's TOP) must run down the page's RIGHT edge, and the
  // left edge stay light. Drawn as the one-bit version, since the host has
  // no grey.
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 10, false);
    auto* eb = static_cast<EpubTool*>(toybox.hostActive());
    if (!eb->openDirect("/books/wind.epub")) {
      printf("WIDE ART FAIL: the book did not open\n");
      abort();
    }
    // Walk to chapter three, the picture-only one; it opens on part 0.
    for (int k = 0; k < 30 && eb->hostSpine() != 2; k++) toybox.onButton(SideBtn::Down);
    if (eb->hostSpine() != 2 || strcmp(eb->hostPageImage(), "OEBPS/images/art2.png") != 0 ||
        eb->hostPageImagePart() != 0) {
      printf("WIDE ART FAIL: never reached the landscape picture (s%d img '%s' part %d)\n",
             eb->hostSpine(), eb->hostPageImage(), eb->hostPageImagePart());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_art_wide");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    auto boxInk = [&](int x0, int y0, int x1, int y1) {
      long n = 0, all = 0;
      for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++, all++) {
          int px = 0, py = 0;
          epdMapPixel(0, epd.panelFlipX(), epd.panelFlipY(), x, y, px, py);
          if (!(epd.fb()[(size_t)py * 100 + (px >> 3)] & (0x80 >> (px & 7)))) n++;
        }
      return (int)(n * 100 / all);  // percent inked
    };
    const int right = boxInk(410, 100, 470, 700), left = boxInk(10, 100, 70, 700);
    if (right < 60 || left > 20) {
      printf("WIDE ART FAIL: turned picture: right edge %d%% inked, left %d%%\n", right, left);
      abort();
    }
    // One page on: the first slice, upright, the black square in it.
    toybox.onButton(SideBtn::Down);
    if (eb->hostPageImagePart() != 1) {
      printf("WIDE ART FAIL: DOWN from the turned picture gave part %d\n",
             eb->hostPageImagePart());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_epub_art_slice");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    const int sliceTop = boxInk(0, 0, 480, 120);
    if (sliceTop < 60) {
      printf("WIDE ART FAIL: the first slice's top band is %d%% inked\n", sliceTop);
      abort();
    }
    toybox.goHub();
    printf("wide artwork ok (turned: right %d%% left %d%%; slice top %d%%)\n", right, left,
           sliceTop);
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
    tapRect(help::OK_BTN);  // Got it
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

    // A step longer than the screen goes on over a second one, and none of
    // it is lost (owner: a 400-character step stopped mid-word).
    {
      std::string longStep;
      while (longStep.size() < 900) longStep += "While stirring, slowly pour in half the stock and keep going. ";
      std::string json = std::string("{\"@type\":\"Recipe\",\"name\":\"Country Pork Meatballs with Onion Gravy and a Very Long Name\",\"recipeInstructions\":[\"Short first.\",\"") +
                         longStep + "END.\",\"Last.\"]}";
      g_dumpEnabled = false;
      if (!rt->hostCook(json.c_str()) || (int)strlen(rt->hostRecipe().steps[1]) < 880) {
        printf("RECIPE FAIL: a 900-character step was not kept whole (%d)\n", (int)strlen(rt->hostRecipe().steps[1]));
        abort();
      }
      rt->hostForward();  // to step 2
      g_dumpEnabled = true;
      setScreen("tool_recipe_long_step");
      stickyHost.refresh(true);
      if (!rt->hostMore()) {
        printf("RECIPE FAIL: a long step did not say it goes on\n");
        abort();
      }
      int parts = 1;
      g_dumpEnabled = false;
      while (rt->hostMore() && parts < 8) {
        rt->hostForward();
        stickyHost.refresh(true);
        parts++;
      }
      g_dumpEnabled = true;
      setScreen("tool_recipe_long_step_end");
      stickyHost.refresh(true);
      if (rt->hostStep() != 1 || rt->hostMore()) {
        printf("RECIPE FAIL: long step parts %d, step %d\n", parts, rt->hostStep());
        abort();
      }
      g_dumpEnabled = false;
      rt->hostForward();
      if (rt->hostStep() != 2) {
        printf("RECIPE FAIL: after the long step's last part, forward did not reach step 3\n");
        abort();
      }
      toybox.onTap(50, 26);  // cooking -> the recipe
      toybox.onTap(50, 26);  // -> the list again, as the tests below expect
      g_dumpEnabled = true;
      printf("recipe long step ok (%d screens)\n", parts);
    }

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
      tapRect(help::OK_BTN);  // Got it, the help card again
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
    toybox.onHold(240, rcpui::LIST_Y0 + 30);                          // hold it: Delete
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
    // The top-right corner opens More (v2): a card of six tiles over the
    // page; a tap outside it closes it again.
    {
    const int MW = stickyHost.sharedCanvas().width(), MH = stickyHost.sharedCanvas().height();
    setScreen("tool_epub_more");
    toybox.onTap(MW - 30, 20);
    if (!static_cast<EpubTool*>(toybox.hostActive())->hostMoreOpen()) {
      printf("MORE FAIL: the top-right corner did not open More\n");
      abort();
    }
    g_dumpEnabled = false;
    toybox.onTap(240, 790);  // below the card: closed
    if (static_cast<EpubTool*>(toybox.hostActive())->hostMoreOpen()) {
      printf("MORE FAIL: a tap outside the card did not close it\n");
      abort();
    }
    toybox.onTap(240, 400);  // and the footer away again
    // Settings tile: the book settings list.
    toybox.onTap(MW - 30, 20);
    tapRect(rmenu::moreTile(4, MW, MH));
    g_dumpEnabled = true;
    setScreen("tool_epub_book_settings");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Ok);  // and down onto the page
    // Rotation tile: freed, the page turns with the device by itself (tick),
    // wears TAP TO LOCK, and the plate locks it. More then opens landscape.
    {
      EpubTool* ep = static_cast<EpubTool*>(toybox.hostActive());
      sensors::hostSetImu(true);
      sensors::hostSetOrientation(0);
      toybox.onTap(MW - 30, 20);
      tapRect(rmenu::moreTile(3, MW, MH));
      sensors::hostSetOrientation(1);
      g_dumpEnabled = true;
      setScreen("tool_epub_follow");
      for (int k = 0; k < 3 && ep->hostRot() != 1; k++) {
        g_millis += 500;
        toybox.tick();
      }
      g_dumpEnabled = false;
      if (ep->hostRot() != 1 || !ep->hostFollowing()) {
        printf("ROTATION FAIL: following did not take angle 1 by itself (rot %d)\n", ep->hostRot());
        abort();
      }
      tapRect(rmenu::lockPlate(stickyHost.sharedCanvas().width()));
      if (ep->hostFollowing()) {
        printf("ROTATION FAIL: the plate did not lock\n");
        abort();
      }
      const int LW = stickyHost.sharedCanvas().width(), LH = stickyHost.sharedCanvas().height();
      g_dumpEnabled = true;
      setScreen("tool_epub_more_landscape");
      toybox.onTap(LW - 30, 20);
      g_dumpEnabled = false;
      if (!ep->hostMoreOpen() || stickyHost.canvasRotation() != 1) {
        printf("ROTATION FAIL: More did not open at the landscape angle\n");
        abort();
      }
      // Back to locked portrait for the checks after this one.
      tapRect(rmenu::moreTile(3, LW, LH));  // free again
      sensors::hostSetOrientation(0);
      for (int k = 0; k < 3 && ep->hostRot() != 0; k++) {
        g_millis += 500;
        toybox.tick();
      }
      tapRect(rmenu::lockPlate(stickyHost.sharedCanvas().width()));
      if (ep->hostRot() != 0 || ep->hostFollowing()) {
        printf("ROTATION FAIL: could not get back to locked portrait (rot %d)\n", ep->hostRot());
        abort();
      }
      sensors::hostSetImu(false);
    }
    }
    printf("more popup ok (opens in the middle, closes outside, settings tile, rotation follows)\n");

    const char* pp = posOf(
        "/books/A Book With The Kind Of Very Long Release Filename Publishers Actually Use Vol 01.epub");
    uint8_t buf[16];
    if (stickyHost.sdReadFile(pp, buf, sizeof(buf)) != 10) {
      printf("LONGPATH FAIL: progress not written in the book's folder\n");
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
  // The cards row lives on the apps page now, which is the page about apps.
  tapAct(setui::ACT_APPS);
  tapRect(setui::appsCardsRect());
  toybox.hostSettings().back();
  // Reset lives on the update page now (Settings > Update & about).
  tapAct(setui::ACT_UPDATE);
  tapRect(setui::updResetRect());  // armed again
  tapRect(setui::updResetRect());  // ...and confirmed
  toybox.hostSettings().back();
  // Cleared means gone, not zeroed: a sentinel default proves the key itself
  // was removed, so a fresh device and a reset one read identically.
  if (prefs.getInt("w_games", -1) != -1 || prefs.getUInt("c_heads", 99) != 99 ||
      help::suppressed(prefs, "wrd")) {
    printf("SETTINGS FAIL: reset left something behind\n");
    abort();
  }
  printf("settings ok (hides, reflows, clears scores, restores cards)\n");

  // The font row: the card's families, the firmware's own face as the first
  // row rather than a button off to one side, and the choice actually taking.
  {
    g_dumpEnabled = false;
    toybox.openSettings();
    tapAct(setui::ACT_FONT);
    if (toybox.hostSettings().hostPage() != 7) {
      printf("FONT PAGE FAIL: the row did not open the page\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_font_who");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // The device's own face first: row 0 of the five.
    tapRect(setui::fontWhoRect(ToolsHost::FONT_DEVICE));
    if (toybox.hostSettings().hostPage() != 8) {
      printf("FONT PAGE FAIL: the device row did not open the family list\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_font");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // The built-in faces come first, by name; the default is ticked.
    // Choosing IBM Plex Sans dresses the device at once -- no card involved --
    // and is remembered by name.
    tapRect(setui::fontRect(2));
    {
      char plex[32] = "";
      prefs.getString("font_uni", plex, sizeof(plex));
      if (gfx::typeface() != 2 || strcmp(plex, "IBM Plex Sans") != 0 ||
          gfx::cardFaceLive(false)) {
        printf("FONT PAGE FAIL: IBM Plex Sans did not take (face %d, kept '%s')\n",
               gfx::typeface(), plex);
        abort();
      }
      g_dumpEnabled = true;
      setScreen("settings_font_plex");
      stickyHost.refresh(true);
      g_dumpEnabled = false;
      // v1 called the default DejaVu; a device that chose it by name wakes up
      // in Noto Sans rather than looking for a card family called DejaVu.
      tapRect(setui::fontRect(0));
      if (gfx::typeface() != 0 || stickyHost.fontFor(ToolsHost::FONT_DEVICE)[0]) {
        printf("FONT PAGE FAIL: the default did not come back from Plex\n");
        abort();
      }
    }

    // Then the card's families, after the built-ins.
    tapRect(setui::fontRect(stickyHost.typefaceCount()));
    if (!gfx::cardFaceLive(false) || !stickyHost.fontFor(ToolsHost::FONT_DEVICE)[0]) {
      printf("FONT PAGE FAIL: choosing a family did not take\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("settings_font_chosen");
    stickyHost.refresh(true);
    g_dumpEnabled = false;

    // And it is remembered, which is the difference between a setting and a
    // gesture: the name has to survive in NVS, not just in the running face.
    char saved[32] = "";
    prefs.getString("font_uni", saved, sizeof(saved));
    if (!saved[0] || strcmp(saved, stickyHost.fontFor(ToolsHost::FONT_DEVICE)) != 0) {
      printf("FONT PAGE FAIL: chose %s, remembered '%s'\n",
             stickyHost.fontFor(ToolsHost::FONT_DEVICE), saved);
      abort();
    }

    tapRect(setui::fontRect(0));  // back to the built-in face
    prefs.getString("font_uni", saved, sizeof(saved));
    if (gfx::cardFaceLive(false) || stickyHost.fontFor(ToolsHost::FONT_DEVICE)[0] || saved[0]) {
      printf("FONT PAGE FAIL: the built-in row did not put it back (kept '%s')\n", saved);
      abort();
    }

    // An app's face is a different slot, remembered under its own key, and it
    // must NOT become the device's face -- the whole point of the split.
    toybox.hostSettings().back();  // back to the five rows
    tapRect(setui::fontWhoRect(ToolsHost::FONT_READER));
    tapRect(setui::fontRect(stickyHost.typefaceCount()));
    char forBooks[32] = "";
    prefs.getString("font_ep", forBooks, sizeof(forBooks));
    if (!forBooks[0] || stickyHost.fontFor(ToolsHost::FONT_DEVICE)[0] ||
        strcmp(stickyHost.fontFor(ToolsHost::FONT_READER), forBooks) != 0) {
      printf("FONT PAGE FAIL: books got '%s', device got '%s'\n", forBooks,
             stickyHost.fontFor(ToolsHost::FONT_DEVICE));
      abort();
    }
    // Choosing it does not load it -- an app's face arrives when the app does.
    if (gfx::cardFaceLive(true)) {
      printf("FONT PAGE FAIL: the book face loaded before any book was opened\n");
      abort();
    }
    // ...and the shell loads it when the reader opens, then drops it on the
    // way back to the hub.
    toybox.hostSettings().back();
    toybox.goHub();
    toybox.hostHub().goHome();
    toybox.onTap(80 + 2 * 160, hubui::DOCK_Y + 30);  // the STUDY drawer
    toybox.onTap(hubui::cellCx(0), hubui::cellTop(0) + hubui::ROW_STEP / 2);  // EPUB, first cell
    if (!toybox.hostInApp() || strcmp(toybox.activeTitle(), "EPUB") != 0) {
      printf("FONT PAGE FAIL: could not open the reader to check its face\n");
      abort();
    }
    const bool loaded = gfx::cardFaceLive(true);
    toybox.goHub();
    if (!loaded) {
      printf("FONT PAGE FAIL: opening the reader did not load its face\n");
      abort();
    }
    if (gfx::cardFaceLive(true)) {
      printf("FONT PAGE FAIL: leaving the app kept its face resident\n");
      abort();
    }
    stickyHost.fontSet(ToolsHost::FONT_READER, "");
    toybox.openSettings();
    toybox.goHub();
    toybox.hostHub().goHome();
    g_dumpEnabled = true;
    printf("font page ok (card families listed, chosen, remembered, and given back)\n");
  }

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
  tapRect(help::OK_BTN);  // GOT IT
  auto wordleKey = [&](char ch) { tapRect(wordui::keyRect(ch)); };
  // Today's word is the date's: the same on every open, and a game left
  // half-played is the one that comes back.
  const std::string daily = static_cast<WordleApp*>(toybox.hostActive())->hostAnswer();
  if (!static_cast<WordleApp*>(toybox.hostActive())->hostDaily()) {
    printf("WORDLE FAIL: with a clock it did not open on today's word\n");
    abort();
  }
  for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
  g_dumpEnabled = true;
  setScreen("wordle");
  tapRect(wordui::ENTER);
  g_dumpEnabled = false;
  toybox.goHub();
  toybox.open(true, 0);
  if (daily != static_cast<WordleApp*>(toybox.hostActive())->hostAnswer()) {
    printf("WORDLE FAIL: leaving and coming back changed today's word\n");
    abort();
  }
  tapRect(help::OK_BTN);  // the card again: this visit only
  for (const char* p = "SLO"; *p; p++) wordleKey(*p);
  g_dumpEnabled = true;
  setScreen("wordle_typing");
  wordleKey('T');
  setScreen("wordle_short");
  tapRect(wordui::ENTER);  // four letters: "too short"
  g_dumpEnabled = false;
  tapRect(wordui::DEL);
  for (int i = 0; i < 4; i++) tapRect(wordui::DEL);

  // Play it out so the end screen is rendered rather than assumed. The last
  // ENTER is the frame we want, so everything before it is silent.
  for (int g = 0; g < 4; g++) {
    for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
    tapRect(wordui::ENTER);
  }
  for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
  g_dumpEnabled = true;
  setScreen("wordle_over");
  tapRect(wordui::ENTER);  // the sixth guess ends it: the result screen
  setScreen("wordle_board_over");
  tapRect(gk::RES_LEFT);   // Close: the finished board
  setScreen("wordle_more");
  toybox.onTap(EPD_W - 28, 20);  // More
  g_dumpEnabled = false;
  tapRect(gk::menuRect(1));  // New word
  if (static_cast<WordleApp*>(toybox.hostActive())->hostDaily() ||
      daily == static_cast<WordleApp*>(toybox.hostActive())->hostAnswer()) {
    printf("WORDLE FAIL: New word did not give an extra word\n");
    abort();
  }
  // Solve one for the "solved in" screen: type the answer itself.
  {
    const std::string ans = static_cast<WordleApp*>(toybox.hostActive())->hostAnswer();
    for (const char* p = "CRANE"; *p; p++) wordleKey(*p);
    tapRect(wordui::ENTER);
    for (char ch : ans) wordleKey(ch);
    g_dumpEnabled = true;
    setScreen("wordle_solved");
    tapRect(wordui::ENTER);
  }
  printf("wordle ok (today's word by date, kept on reopen; New word is another; result screen)\n");

  // --- mines ----------------------------------------------------------------
  setScreen("help_mines");
  prefs.remove("ms_state");
  toybox.open(true, 1);
  g_dumpEnabled = false;
  tapRect(help::OK_BTN);
  g_dumpEnabled = true;
  setScreen("mines_new");
  stickyHost.refresh(true);
  {
    auto* m = static_cast<MinesApp*>(toybox.hostActive());
    auto cellTap = [&](int k) { tapRect(minesui::cellRect(k)); };
    g_dumpEnabled = false;
    cellTap(4 * mns::N + 4);  // the middle: always safe, always a patch
    mns::Board& b = m->hostBoard();
    if (b.mine[40] || mns::count(b, 40) != 0 || b.st[40] != mns::OPEN) {
      printf("MINES FAIL: the first tap was not a safe, open patch\n");
      abort();
    }
    // Flag a mine next to the open patch with a hold, the way a finger does.
    int flagged = -1;
    for (int k = 0; k < mns::CELLS && flagged < 0; k++) {
      if (!b.mine[k] || b.st[k] != mns::HIDDEN) continue;
      bool edge = false;
      mns::around(k, [&](int j) { edge |= b.st[j] == mns::OPEN; });
      if (edge) flagged = k;
    }
    if (flagged >= 0) {
      const TRect r = minesui::cellRect(flagged);
      toybox.onHold(r.x + r.w / 2, r.y + r.h / 2);
      if (b.st[flagged] != mns::FLAG) {
        printf("MINES FAIL: a hold did not plant a flag\n");
        abort();
      }
      cellTap(flagged);  // a tap on a flag must not dig it
      if (b.st[flagged] != mns::FLAG || mns::lost(b)) {
        printf("MINES FAIL: a tap dug through a flag\n");
        abort();
      }
    }
    if (toybox.holdMs() == 0 || toybox.holdMs() > 600) {
      printf("MINES FAIL: Mines did not ask for a quick hold (%d ms)\n", toybox.holdMs());
      abort();
    }
    g_dumpEnabled = true;
    setScreen("mines_play");
    stickyHost.refresh(false);
    // Lose on purpose: dig a mine that has no flag.
    g_dumpEnabled = false;
    int boom = -1;
    for (int k = 0; k < mns::CELLS && boom < 0; k++)
      if (b.mine[k] && b.st[k] == mns::HIDDEN) boom = k;
    g_dumpEnabled = true;
    setScreen("mines_lost");
    cellTap(boom);
    if (!mns::lost(b)) {
      printf("MINES FAIL: digging a mine did not end the game\n");
      abort();
    }
    // A new game, then win it by digging every safe square.
    g_dumpEnabled = false;
    tapRect(minesui::NEW);
    cellTap(0);
    for (int k = 0; k < mns::CELLS; k++)
      if (!b.mine[k] && b.st[k] == mns::HIDDEN) cellTap(k);
    g_dumpEnabled = true;
    if (!mns::won(b)) {
      printf("MINES FAIL: every safe square dug, and not won\n");
      abort();
    }
    setScreen("mines_won");
    stickyHost.refresh(true);
    setScreen("mines_board_won");
    tapRect(gk::RES_LEFT);
    setScreen("mines_more");
    toybox.onTap(EPD_W - 28, 20);
    printf("mines ok (first tap safe, hold flags at %d ms, flags guard, lose, win)\n",
           toybox.holdMs());
  }

  setScreen("help_2048");
  toybox.open(true, 2);
  g_dumpEnabled = false;
  tapRect(help::OK_BTN);  // GOT IT
  toybox.onSwipe(-120, 10);
  g_dumpEnabled = true;
  setScreen("g2048_merge");
  toybox.onSwipe(10, -120);  // a wedge marks what merged, the new tile is dashed

  // ...and the next move clears the mark, because it belongs to the move that
  // made it rather than to a timer.
  setScreen("g2048");
  toybox.onSwipe(10, 120);
  setScreen("g2048_button");
  toybox.onButton(SideBtn::Up);  // the upper side button slides the board up

  // A late board, every tile style on it: plain, striped and solid.
  {
    struct Saved {
      uint8_t b[4][4], prev[4][4];
      int32_t score, prevScore;
      uint8_t canUndo, over, reached, pad[2];
    } sv{};
    const uint8_t late[16] = {1, 2, 3, 1, 4, 5, 2, 0, 7, 8, 4, 2, 10, 9, 6, 3};
    memcpy(sv.b, late, 16);
    sv.score = 20116;
    prefs.putBytes("t_state", &sv, sizeof(sv));
    g_dumpEnabled = false;
    toybox.goHub();
    g_dumpEnabled = true;
    help::suppress(prefs, "g20");  // straight to the board
    setScreen("g2048_tiles");
    toybox.open(true, 2);
  }

  setScreen("g2048_win");
  static_cast<G2048App*>(toybox.hostActive())->hostSetOver(true);
  setScreen("g2048_over");
  static_cast<G2048App*>(toybox.hostActive())->hostSetOver(false);
  setScreen("g2048_more");
  g_dumpEnabled = false;
  tapRect(gk::RES_LEFT);
  g_dumpEnabled = true;
  toybox.onTap(EPD_W - 28, 20);
  g_dumpEnabled = false;
  toybox.onTap(BACK_W / 2, TOPBAR_H / 2);  // back from More to the board
  g_dumpEnabled = true;

  // --- XO: vs the Sticky, then the 3-mark rule ------------------------------
  setScreen("help_xo");
  prefs.putInt("x_foe", 1);
  toybox.open(true, 3);
  g_dumpEnabled = false;
  tapRect(help::OK_BTN);  // GOT IT
  auto xoTap = [&](int cell) {
    toybox.onTap(xoui::BX + (cell % 3) * xoui::CELL + xoui::CELL / 2,
                 xoui::BY + (cell / 3) * xoui::CELL + xoui::CELL / 2);
  };
  // The Sticky's replies depend on the dice, so each move goes to the first
  // free square of a list rather than to a fixed one.
  auto xoFree = [&](std::initializer_list<int> order) {
    auto* xo = static_cast<XoApp*>(toybox.hostActive());
    for (int k : order)
      if (!xo->hostCell(k)) return xoTap(k);
  };
  xoTap(4);  // centre; the machine answers in the same frame
  g_dumpEnabled = true;
  setScreen("xo");
  xoFree({0, 2, 6, 8, 1, 3, 5, 7});

  setScreen("xo_three");
  g_dumpEnabled = false;
  toybox.onTap(EPD_W - 28, 20);  // More
  tapRect(gk::menuRect(3));      // Rule -> 3 marks
  toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
  xoFree({4, 0, 8, 2});
  xoFree({0, 8, 2, 6});
  xoFree({8, 2, 6, 1});
  g_dumpEnabled = true;
  xoFree({2, 6, 1, 3, 5, 7});

  setScreen("xo_more");
  toybox.onTap(EPD_W - 28, 20);

  // Pass-and-play, taken to a win, so the result line gets rendered.
  setScreen("xo_won");
  g_dumpEnabled = false;
  tapRect(gk::menuRect(3));  // back to Classic
  toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
  tapRect(xoui::CHIP_FRIEND);  // vs a friend
  xoTap(0);
  xoTap(3);
  xoTap(1);
  xoTap(4);
  g_dumpEnabled = true;
  xoTap(2);  // X takes the top row
  g_dumpEnabled = false;
  tapRect(xoui::CHIP_AI);  // leave it as the record guard below expects
  g_dumpEnabled = true;

  // --- tools -------------------------------------------------------------
  setScreen("tool_coin");
  g_dumpEnabled = false;
  toybox.open(false, 0);
    g_dumpEnabled = true;
  tapRect(coinui::FLIP1);  // Flip

  setScreen("tool_coin_multi");
  tapRect(coinui::FLIP10);  // x10
  {
    // The coin repeats the last kind of flip; the third box opens History.
    CoinTool* coin = static_cast<CoinTool*>(toybox.hostActive());
    g_dumpEnabled = false;
    const int before = coin->hostFlips();
    tapRect(coinui::COIN);
    if (coin->hostBatch() != 10 || coin->hostFlips() != before + 10) {
      printf("COIN FAIL: tapping the coin after x10 flipped %d\n", coin->hostFlips() - before);
      abort();
    }
    tapRect(coinui::FLIP1);
    tapRect(coinui::FLIP5);
    tapRect(coinui::HIST_BOX);
    if (!coin->hostHistory()) {
      printf("COIN FAIL: the history box did not open History\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_coin_history");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onTap(0, 0);  // back to the coin
    g_dumpEnabled = true;
  }

  setScreen("tool_dice");
  g_dumpEnabled = false;
  toybox.open(false, 1);
  tapRect(diceui::typeRect(5));  // D20
  tapRect(diceui::CNT_UP);
  tapRect(diceui::CNT_UP);
  tapRect(diceui::MOD_UP);
  tapRect(diceui::MOD_UP);
  g_dumpEnabled = true;
  toybox.onTap(240, 262 + 38);  // ROLL

  setScreen("tool_dice_d6");
  g_dumpEnabled = false;
  tapRect(diceui::typeRect(1));  // D6
  g_dumpEnabled = true;
  toybox.onTap(240, 262 + 38);

  setScreen("tool_timer");
  g_dumpEnabled = false;
  toybox.open(false, 2);
    quietTap(20 + 112 + 52, 372 + 26);  // 25 min preset (row 1, col 1)
  g_dumpEnabled = true;
  tapRect(timerui::CD_START);

  // The time typed on a keypad, and kept as a preset (owner, 24 Nov).
  {
    g_dumpEnabled = false;
    auto* tt = static_cast<TimerTool*>(toybox.hostActive());
    tapRect(timerui::CD_START);  // pause, so the time can be typed
    auto key = [&](int k) { tapRect(numpad::key(k)); };
    tapRect(timerui::TIME_TAP);
    key(1);  // 2
    key(2);  // 3
    key(numpad::ZERO);
    g_dumpEnabled = true;
    setScreen("tool_timer_keypad");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    key(numpad::DONE);
    if (tt->hostTyping() || tt->hostSetSec() != 150) {
      printf("TIMER FAIL: 2 3 0 set %d s (want 150)\n", tt->hostSetSec());
      abort();
    }
    // 9 0 is a minute and a half; 3 alone is too short and stays on the keypad.
    tapRect(timerui::TIME_TAP);
    key(2);  // 3
    key(numpad::DONE);
    if (!tt->hostTyping() || tt->hostSetSec() != 150) {
      printf("TIMER FAIL: 3 s was taken\n");
      abort();
    }
    key(numpad::CLEAR);
    key(8);  // 9
    key(numpad::ZERO);
    key(numpad::DONE);
    if (tt->hostSetSec() != 90) {
      printf("TIMER FAIL: 9 0 set %d s (want 90)\n", tt->hostSetSec());
      abort();
    }
    // Eight presets: Save waits until one is held and removed.
    tapRect(timerui::SAVE_BTN);
    if (tt->hostPresets() != 8) {
      printf("TIMER FAIL: a ninth preset was saved\n");
      abort();
    }
    {
      const TRect r = timerui::presetRect(7);  // 60 min
      toybox.onHold(r.x + r.w / 2, r.y + r.h / 2);
    }
    g_dumpEnabled = true;
    setScreen("tool_timer_remove");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(timerui::presetRect(7));
    tapRect(timerui::SAVE_BTN);
    if (tt->hostPresets() != 8 || tt->hostPreset(1) != 90 || tt->hostPreset(7) != 30 * 60) {
      printf("TIMER FAIL: presets after remove+save: %d, [1]=%d [7]=%d\n", tt->hostPresets(), tt->hostPreset(1),
             tt->hostPreset(7));
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_timer_saved");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 2);
    tt = static_cast<TimerTool*>(toybox.hostActive());
    if (tt->hostPresets() != 8 || tt->hostPreset(1) != 90 || tt->hostSetSec() != 90) {
      printf("TIMER FAIL: presets not kept across a restart\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("timer keypad ok (typed 2:30 and 1:30, 3 s refused, preset saved in order and kept)\n");
  }

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
  // From typed on the keypad: 5, then To 50; No repeats on.
  {
    stickyHost.refresh(true);  // the fresh screen, presets and all
    g_dumpEnabled = false;
    RandomTool* rnd = static_cast<RandomTool*>(toybox.hostActive());
    tapRect(randui::FROM_BOX);
    tapRect(numpad::key(4));  // 5
    tapRect(numpad::key(numpad::DONE));
    tapRect(randui::TO_BOX);
    tapRect(numpad::key(4));  // 5
    tapRect(numpad::key(numpad::ZERO));
    g_dumpEnabled = true;
    setScreen("tool_random_keypad");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(numpad::key(numpad::DONE));
    tapRect(randui::UNIQUE);
    if (rnd->hostMin() != 5 || rnd->hostMax() != 50 || !rnd->hostUnique()) {
      printf("RANDOM FAIL: typed 5 to 50 with no repeats, got %d to %d (%d)\n", rnd->hostMin(), rnd->hostMax(),
             rnd->hostUnique());
      abort();
    }
    tapRect(randui::DRAW_NUM);
    tapRect(randui::DRAW_NUM);
    g_dumpEnabled = true;
    setScreen("tool_random_norepeat");
  }
  tapRect(randui::DRAW_NUM);  // Draw

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
  PickerTool* pk = static_cast<PickerTool*>(toybox.hostActive());
  tapRect(pickui::LIST_BTN);
  // Types with the shared keyboard: finds each letter's key, shifting first
  // for capitals (the keyboard starts shifted on an empty line).
  auto oskType = [&](const char* text, osk::State& st) {
    for (const char* p = text; *p; p++) {
      bool hit = false;
      for (int attempt = 0; attempt < 2 && !hit; attempt++) {
        for (int row = 0; row < osk::ROWS && !hit; row++)
          for (int col = 0;; col++) {
            TRect r;
            int code;
            if (!osk::keyAt(st, row, col, r, code)) break;
            if (code == (unsigned char)*p || (*p == ' ' && code == osk::K_SPACE)) {
              toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
              if (*p != ' ') st.shift = false;
              hit = true;
              break;
            }
          }
        if (!hit) {
          TRect r;
          int code;
          osk::keyAt(st, 2, 0, r, code);  // shift
          toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
          st.shift = !st.shift;
        }
      }
      if (!hit) {
        printf("PICKER FAIL: no key for '%c'\n", *p);
        abort();
      }
    }
  };
  auto oskDone = [&](const osk::State& st) {
    for (int row = 0; row < osk::ROWS; row++)
      for (int col = 0;; col++) {
        TRect r;
        int code;
        if (!osk::keyAt(st, row, col, r, code)) break;
        if (code == osk::K_DONE) {
          toybox.onTap(r.x + r.w / 2, r.y + r.h / 2);
          return;
        }
      }
  };
  const char* names[3] = {"Alice", "Bob", "Charlie"};
  for (int i = 0; i < 3; i++) {
    tapRect(pickui::ADD_BTN);
    osk::State st;
    st.shift = true;
    oskType(names[i], st);
    if (i == 2) {
      g_dumpEnabled = true;
      stickyHost.refresh(true);  // the keyboard, mid-word
      g_dumpEnabled = false;
    }
    oskDone(st);
  }
  if (pk->hostCount() != 3) {
    printf("PICKER FAIL: typed 3 choices, list has %d\n", pk->hostCount());
    abort();
  }
  g_dumpEnabled = true;
  setScreen("tool_picker_list");
  stickyHost.refresh(true);
  g_dumpEnabled = false;
  toybox.onTap(0, 0);  // back to the reel
  tapRect(pickui::SPIN);
  for (int i = 0; i < 20 && pk->hostRolling(); i++) {
    g_millis += 200;
    toybox.tick();
  }
  if (pk->hostRolling() || pk->hostPicked() < 0) {
    printf("PICKER FAIL: the reel did not stop on a pick\n");
    abort();
  }
  g_dumpEnabled = true;
  setScreen("tool_picker");
  stickyHost.refresh(true);

  // The picker can also take its list from a phone.
  g_dumpEnabled = false;
  tapRect(pickui::LIST_BTN);
  g_dumpEnabled = true;
  setScreen("tool_picker_phone");
  tapRect(pickui::PHONE_BTN);

  setScreen("tool_picker_phone_done");
  g_dumpEnabled = false;
  toybox.tick();
  toybox.tick();
  g_dumpEnabled = true;
  toybox.tick();  // stub reports a list arrived

  setScreen("tool_picker_after");
  tapRect(pickui::DONE_BTN);  // DONE -> back to the list

  // With a home Wi-Fi saved the phone page moves onto it (toybox.local),
  // the Sticky's own access point one tap away as the backup; and when the
  // router does not answer, the backup is what comes up.
  {
    g_dumpEnabled = false;
    homenet::saveWifi(stickyHost.prefs(), "Home-2.4", "secret123");
    homenet::g_hostNet = homenet::Result::Ok;
    tapRect(pickui::PHONE_BTN);
    if (!pk->hostNetHome()) {
      printf("PICKER FAIL: a saved home Wi-Fi did not carry the phone page\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_picker_home");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(pickui::ALT_BTN);  // the backup
    if (pk->hostNetHome() || !pk->hostNetUp()) {
      printf("PICKER FAIL: 'Use the Sticky's own Wi-Fi' did not switch to the access point\n");
      abort();
    }
    tapRect(pickui::DONE_BTN);
    homenet::g_hostNet = homenet::Result::NotFound;  // the router is away
    tapRect(pickui::PHONE_BTN);
    if (pk->hostNetHome() || !pk->hostNetUp()) {
      printf("PICKER FAIL: no fallback to the access point when home did not answer\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_picker_home_fallback");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(pickui::DONE_BTN);
    homenet::forgetWifi(stickyHost.prefs());
    homenet::g_hostNet = homenet::Result::Ok;
    g_dumpEnabled = true;
    printf("home wifi ok (phone page on toybox.local, own access point as backup)\n");
  }

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

  // Named lists, 50 choices a list in pages, and a no-repeats round per list
  // (owner, 24 Nov: "B with named lists").
  {
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 4);
    pk = static_cast<PickerTool*>(toybox.hostActive());
    if (pk->hostLists() != 1 || strcmp(pk->hostListName(0), "My list") != 0) {
      printf("PICKER FAIL: %d lists, the first \"%s\" (want 1, My list)\n", pk->hostLists(), pk->hostListName(0));
      abort();
    }
    auto spin = [&] {
      tapRect(pickui::SPIN);
      for (int i = 0; i < 20 && pk->hostRolling(); i++) {
        g_millis += 200;
        toybox.tick();
      }
    };
    std::string many;
    for (int i = 1; i <= 55; i++) many += "Choice " + std::to_string(i) + "\n";
    pk->hostPhoneSave(many.c_str(), -2, "");
    if (pk->hostCount() != 50) {
      printf("PICKER FAIL: 55 sent, %d kept (want 50)\n", pk->hostCount());
      abort();
    }
    tapRect(pickui::LIST_BTN);
    g_dumpEnabled = true;
    setScreen("tool_picker_page1");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    toybox.onButton(SideBtn::Down);
    toybox.onSwipe(-200, 0);
    if (pk->hostPage() != 2) {
      printf("PICKER FAIL: DOWN and a swipe left went to page %d (want 3)\n", pk->hostPage() + 1);
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_picker_page3");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(pickui::PG_PREV);
    if (pk->hostPage() != 1) {
      printf("PICKER FAIL: < went to page %d (want 2)\n", pk->hostPage() + 1);
      abort();
    }
    // The name in the bar opens the lists; a new one comes onto the screen.
    tapRect(pickui::BAR_TAP);
    tapRect(pickui::NEW_BTN);
    pk->hostTypeName("Lunch");
    oskDone(osk::State{});
    if (pk->hostLists() != 2 || pk->hostCur() != 1 || pk->hostCount() != 0 ||
        strcmp(pk->hostListName(1), "Lunch") != 0) {
      printf("PICKER FAIL: new list: %d lists, on %d, %d choices\n", pk->hostLists(), pk->hostCur(), pk->hostCount());
      abort();
    }
    pk->hostPhoneSave("Pizza\nSushi\nTacos\n", -2, "");
    toybox.onTap(0, 0);  // the reel
    g_dumpEnabled = true;
    setScreen("tool_picker_lunch");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    // A round is its list's own.
    tapRect(pickui::NOREP_BTN);
    spin();
    spin();
    if (pk->hostRemaining() != 1) {
      printf("PICKER FAIL: two no-repeat spins left %d of 3\n", pk->hostRemaining());
      abort();
    }
    tapRect(pickui::BAR_TAP);
    tapRect(pickui::lrowRect(0));
    spin();
    if (pk->hostCur() != 0 || pk->hostRemaining() != 49) {
      printf("PICKER FAIL: the first list's round: on %d, %d left (want 0, 49)\n", pk->hostCur(), pk->hostRemaining());
      abort();
    }
    tapRect(pickui::BAR_TAP);
    tapRect(pickui::lrowRect(1));
    toybox.goHub();
    toybox.open(false, 4);
    pk = static_cast<PickerTool*>(toybox.hostActive());
    if (pk->hostCur() != 1 || pk->hostRemaining() != 1) {
      printf("PICKER FAIL: back on Lunch after a restart: on %d, %d left (want 1, 1)\n", pk->hostCur(),
             pk->hostRemaining());
      abort();
    }
    tapRect(pickui::KEEP_BTN);  // back to any of them
    // Hold a list: Rename and Delete.
    tapRect(pickui::BAR_TAP);
    {
      const TRect r = pickui::lrowRect(1);
      toybox.onHold(r.x + 40, r.y + r.h / 2);
    }
    g_dumpEnabled = true;
    setScreen("tool_picker_lists");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(pickui::lrenRect(1));
    pk->hostTypeName("Dinner");
    oskDone(osk::State{});
    if (strcmp(pk->hostListName(1), "Dinner") != 0 || pk->hostCount() != 3) {
      printf("PICKER FAIL: rename gave \"%s\" with %d choices\n", pk->hostListName(1), pk->hostCount());
      abort();
    }
    {
      const TRect r = pickui::lrowRect(1);
      toybox.onHold(r.x + 40, r.y + r.h / 2);
    }
    tapRect(pickui::ldelRect(1));
    if (pk->hostLists() != 1 || pk->hostCur() != 0 || pk->hostCount() != 50) {
      printf("PICKER FAIL: delete left %d lists, on %d with %d\n", pk->hostLists(), pk->hostCur(), pk->hostCount());
      abort();
    }
    // A new list straight from the phone, by name.
    const auto sv = pk->hostPhoneSave("Chess\nGo\n", -1, "Games");
    const String js = pk->hostListsJson();
    if (sv.list != 1 || sv.n != 2 || pk->hostCur() != 1 || strstr(js.c_str(), "\"name\":\"Games\",\"n\":2") == nullptr) {
      printf("PICKER FAIL: phone new list: went to %d with %d; lists %s\n", sv.list, sv.n, js.c_str());
      abort();
    }
    toybox.onTap(0, 0);
    g_dumpEnabled = true;
    setScreen("tool_picker_lists_after");
    tapRect(pickui::BAR_TAP);
    g_dumpEnabled = false;
    toybox.goHub();
    // The list from before named lists becomes "My list".
    tfs::remove("/picker/0.txt");
    tfs::remove("/picker/1.txt");
    stickyHost.prefs().putString("pk_items", "Old one\nOld two\n");
    toybox.open(false, 4);
    pk = static_cast<PickerTool*>(toybox.hostActive());
    if (pk->hostLists() != 1 || pk->hostCount() != 2 || strcmp(pk->hostListName(0), "My list") != 0 ||
        stickyHost.prefs().isKey("pk_items")) {
      printf("PICKER FAIL: migration: %d lists, %d choices, \"%s\"\n", pk->hostLists(), pk->hostCount(),
             pk->hostListName(0));
      abort();
    }
    toybox.goHub();
    g_dumpEnabled = true;
    printf("picker lists ok (50 a list in pages, named lists, a round per list, old list migrated)\n");
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
  {
    // Through the deck: one put back, the rest known, to the session's end.
    g_dumpEnabled = false;
    tapRect(fcui::AGAIN_BTN);
    for (int i = 0; i < 12; i++) tapRect(fcui::GOOD_BTN);
    g_dumpEnabled = true;
    setScreen("tool_flash_session_done");
    stickyHost.refresh(true);
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
  g_dumpEnabled = false;
  tapRect(fcui::DONE_BTN);
  {
    using namespace fcard;
    DeckInfo d[MAX_DECKS];
    for (int n = listDecks(d, MAX_DECKS); n > 0; n = listDecks(d, MAX_DECKS))
    {
      const TRect r = fcui::rowRect(0, n);
      toybox.onHold(r.x + 40, r.y + r.h / 2);  // hold a deck: it offers Delete
      if (n == 1) {
        g_dumpEnabled = true;
        setScreen("tool_flash_delete");
        stickyHost.refresh(true);
        g_dumpEnabled = false;
      }
      tapRect(fcui::delRect(0, n));
    }
  }
  g_dumpEnabled = true;
  setScreen("tool_flash_empty");
  stickyHost.refresh(true);

  // 300 cards, and the longest faces whole (owner, 24 Nov): a 128-character
  // front and a 256-character back, in English and in Thai, never cut.
  {
    std::string body;
    const char* enF = "Which process in the leaves of green plants turns sunlight, water and carbon dioxide into sugar and oxygen for the whole plant?";
    const char* enB = "Photosynthesis. In the chloroplasts, chlorophyll captures light energy, which splits water and releases oxygen; that energy is then used in the Calvin cycle to fix carbon dioxide into glucose, which the plant stores as starch or burns for growth and repair";
    const char* thF = "กระบวนการใดในใบของพืชสีเขียวที่เปลี่ยนแสงแดด น้ำ และคาร์บอนไดออกไซด์ ให้เป็นน้ำตาลและออกซิเจนสำหรับพืชทั้งต้นได้อย่างไรกันนะครับ";
    const char* thB = "การสังเคราะห์ด้วยแสง ในคลอโรพลาสต์ คลอโรฟิลล์จะดูดกลืนพลังงานแสง ซึ่งใช้แยกน้ำและปล่อยออกซิเจนออกมา จากนั้นพลังงานนั้นถูกใช้ในวัฏจักรคัลวินเพื่อตรึงคาร์บอนไดออกไซด์ให้เป็นกลูโคส ซึ่งพืชเก็บไว้เป็นแป้งหรือใช้เพื่อการเจริญเติบโตและซ่อมแซมเซลล์ต่างๆ ภายในต้น";
    auto chars = [](const char* t) {
      int n = 0;
      for (const char* p = t; *p;) {
        uni::next(p);
        n++;
      }
      return n;
    };
    printf("  flash: en %d/%d chars, th %d/%d chars\n", chars(enF), chars(enB), chars(thF), chars(thB));
    for (int i = 0; i < 300; i++) {
      body += (i % 2) ? thF : enF;
      body += '\t';
      body += (i % 2) ? thB : enB;
      body += '\n';
    }
    char saved[fcard::NAME_LEN + 1];
    const int got = fcard::importDeck("long cards", body.c_str(), saved);
    fcard::Card* deck = (fcard::Card*)malloc(sizeof(fcard::Card) * fcard::MAX_CARDS);
    const int n = fcard::loadDeck("long cards", deck, fcard::MAX_CARDS);
    const bool whole = n == 300 && strcmp(deck[0].front, enF) == 0 && strcmp(deck[0].back, enB) == 0 &&
                       strcmp(deck[1].front, thF) == 0 && strcmp(deck[1].back, thB) == 0;
    free(deck);
    if (got != 300 || !whole) {
      printf("FLASH LONG FAIL: imported %d, loaded %d, whole %d\n", got, n, whole);
      abort();
    }
    g_dumpEnabled = false;
    toybox.goHub();
    toybox.open(false, 5);
    auto* ft = static_cast<FlashTool*>(toybox.hostActive());
    if (ft->hostHelp()) tapRect(help::OK_BTN);
    fcard::DeckInfo d[fcard::MAX_DECKS];
    const int nd = fcard::listDecks(d, fcard::MAX_DECKS);
    int row = -1;
    for (int i = 0; i < nd; i++)
      if (strcmp(d[i].name, "long cards") == 0) row = i;
    tapRect(fcui::rowRect(row, nd));
    if (ft->hostCardCount() != 300) {
      printf("FLASH LONG FAIL: the deck opened with %d cards\n", ft->hostCardCount());
      abort();
    }
    bool seenEn = false, seenTh = false;
    for (int k = 0; k < 8; k++) {
      const bool th = strcmp(ft->hostFace(false), thF) == 0;
      const bool dump = th ? !seenTh : !seenEn;
      g_dumpEnabled = dump;
      if (dump) setScreen(th ? "tool_flash_long_th_front" : "tool_flash_long_en_front");
      stickyHost.refresh(true);
      if (!FlashTool::hostFitAll() || FlashTool::hostFitSize() < TS_MED) {
        printf("FLASH LONG FAIL: %s front cut or too small (size %d)\n", th ? "Thai" : "English",
               FlashTool::hostFitSize());
        abort();
      }
      if (dump) setScreen(th ? "tool_flash_long_th_back" : "tool_flash_long_en_back");
      toybox.onTap(240, 300);  // flip
      if (!FlashTool::hostFitAll() || FlashTool::hostFitSize() < TS_MED) {
        printf("FLASH LONG FAIL: %s back cut or too small (size %d)\n", th ? "Thai" : "English",
               FlashTool::hostFitSize());
        abort();
      }
      (th ? seenTh : seenEn) = true;
      g_dumpEnabled = false;
      tapRect(fcui::GOOD_BTN);
    }
    if (!seenEn || !seenTh) {
      printf("FLASH LONG FAIL: did not meet both kinds of card\n");
      abort();
    }
    toybox.goHub();
    fcard::deleteDeck("long cards");
    g_dumpEnabled = true;
    printf("flash long ok (300 cards, 128/256-character faces whole at TS_MED or larger)\n");
  }

  // --- ships (solo) --------------------------------------------------------
  setScreen("help_ships");
  toybox.open(false, 7);
  setScreen("tool_sea_menu");
  tapRect(help::OK_BTN);  // GOT IT
  {
    auto* sea = static_cast<SeaTool*>(toybox.hostActive());
    setScreen("tool_sea_setup");
    tapRect(seaui::SOLO_ROW);  // Against the Sticky: the empty board, Carrier in hand

    // Place the Carrier by hand: tap a square (striped preview), turn it, put it down.
    setScreen("tool_sea_setup_aim");
    tapRect(seaui::setCell(sea::cellAt(1, 2)));
    setScreen("tool_sea_setup_turn");
    tapRect(seaui::TURN_BTN);
    g_dumpEnabled = false;
    tapRect(seaui::PLACE_BTN);
    if (!sea::placed(sea->hostMine(), 0)) {
      printf("SHIPS FAIL: Place did not put the Carrier down\n");
      abort();
    }
    // Two more by hand, leaving two in hand for the picture.
    tapRect(seaui::setCell(sea::cellAt(4, 0)));
    tapRect(seaui::PLACE_BTN);
    tapRect(seaui::setCell(sea::cellAt(4, 6)));
    tapRect(seaui::PLACE_BTN);
    // A square where the ship would overlap is refused, not placed.
    tapRect(seaui::setCell(sea::cellAt(1, 4)));
    g_dumpEnabled = true;
    setScreen("tool_sea_setup_overlap");
    tapRect(seaui::PLACE_BTN);
    g_dumpEnabled = false;
    tapRect(seaui::SHUFFLE_BTN);  // the rest at random: all five down
    if (!sea::fleetPlaced(sea->hostMine())) {
      printf("SHIPS FAIL: Shuffle did not put the whole fleet down\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_sea_setup_done");
    stickyHost.refresh(false);

    setScreen("tool_sea_play");
    g_dumpEnabled = false;
    tapRect(seaui::PLACE_BTN);  // Start
    // Fire a diagonal (aim, then fire) so the board shows hits and misses.
    for (int k = 0; k < 6; k++) {
      const TRect r = seaui::enemyCell(sea::cellAt(k, k));
      tapRect(r);
      tapRect(r);
    }
    const int shots = sea->hostShots();
    if (shots < 1) {
      printf("SHIPS FAIL: aim-then-tap-again did not fire\n");
      abort();
    }
    g_dumpEnabled = true;
    tapRect(seaui::enemyCell(sea::cellAt(2, 7)));  // leave one square aimed at

    // Wreckage: sink one enemy ship outright and show what that looks like.
    setScreen("tool_sea_sunk");
    sea->hostSinkShip(0);
    stickyHost.refresh(false);

    // Fire at every square in turn; the game ends one way or the other.
    setScreen("tool_sea_over");
    g_dumpEnabled = false;
    for (int k = 0; k < sea::CELLS && sea->hostScreen() == 3; k++) {
      if (sea->hostEnemy().shot[k] != sea::UNSHOT) continue;
      tapRect(seaui::enemyCell(k));
      tapRect(seaui::enemyCell(k));
    }
    g_dumpEnabled = true;
    stickyHost.refresh(true);
    if (sea->hostScreen() != 4) {
      printf("SHIPS FAIL: firing at every square did not end the game\n");
      abort();
    }
    setScreen("tool_sea_more");
    toybox.onTap(EPD_W - 28, 20);
    g_dumpEnabled = false;
    toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
    g_dumpEnabled = true;
    printf("ships ok (placed by hand, overlap refused, shuffle, aim then fire, game over)\n");
  }

  setScreen("tool_sea_pair");
  g_dumpEnabled = false;
  toybox.open(false, 7);
  tapRect(help::OK_BTN);     // reopening brings the rules card back
  tapRect(seaui::DUEL_ROW);  // Two Stickies
  g_dumpEnabled = true;
  tapRect(seaui::HOST_BTN);  // HOST A GAME

  // --- sudoku -------------------------------------------------------------
  setScreen("help_sudoku");
  prefs.remove("sd_state");
  toybox.open(false, 8);
  g_dumpEnabled = false;
  tapRect(help::OK_BTN);  // GOT IT
  {
    auto* sd = static_cast<SudokuTool*>(toybox.hostActive());
    int empty[4], ne = 0;
    for (int k = 0; k < sud::CELLS && ne < 4; k++)
      if (!sd->hostGiven(k)) empty[ne++] = k;
    // A right digit, a wrong one, and pencil notes in a third.
    tapRect(sdui::cellRect(empty[0]));
    tapRect(sdui::keyRect(sd->hostSolution(empty[0]) - 1));
    tapRect(sdui::cellRect(empty[1]));
    tapRect(sdui::keyRect(sd->hostSolution(empty[1]) % 9));  // wrong on purpose
    tapRect(sdui::cellRect(empty[2]));
    tapRect(sdui::btnRect(sdui::B_NOTES));
    tapRect(sdui::keyRect(1));
    tapRect(sdui::keyRect(4));
    tapRect(sdui::keyRect(6));
    tapRect(sdui::btnRect(sdui::B_NOTES));
    tapRect(sdui::cellRect(empty[3]));
    tapRect(sdui::cellRect(empty[3]));  // re-tapping keeps it selected
    if (sd->hostSel() != empty[3]) {
      printf("SUDOKU FAIL: a second tap on the selected cell cleared it\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_sudoku");
    stickyHost.refresh(false);
    setScreen("tool_sudoku_check");
    tapRect(sdui::btnRect(sdui::B_CHECK));
  }

  setScreen("tool_sudoku_solved");
  static_cast<SudokuTool*>(toybox.hostActive())->hostSolve();
  stickyHost.refresh(true);
  setScreen("tool_sudoku_more");
  g_dumpEnabled = false;
  tapRect(gk::RES_LEFT);
  g_dumpEnabled = true;
  toybox.onTap(EPD_W - 28, 20);
  g_dumpEnabled = false;
  toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
  g_dumpEnabled = true;

  // --- C4 games (tool indexes 14..19) -------------------------------------------
  {
    auto more = [] { toybox.onTap(EPD_W - 28, 20); };
    auto back = [] { toybox.onTap(BACK_W / 2, TOPBAR_H / 2); };
    auto inTool = [](int idx) { return toybox.hostInApp() && !toybox.hostIsGame() && toybox.hostIdx() == idx; };

    // ---- Four in a Row ----
    for (const char* k : {"c4_game", "c4_mode", "c4_lvl", "c4_who", "c4_next", "h_c4"}) prefs.remove(k);
    setScreen("help_fourrow");
    toybox.open(false, 14);
    auto* fr = static_cast<FourRowTool*>(toybox.hostActive());
    if (!fr->hostInRules()) {
      printf("FOUR ROW FAIL: the rules card did not open first\n");
      abort();
    }
    setScreen("tool_fourrow");
    tapRect(help::OK_BTN);
    {
      c4::Game& g = fr->hostGame();
      // Take turns: you open the first game.
      if (g.moves != 0 || c4::toMove(g) != 0) {
        printf("FOUR ROW FAIL: the first game did not open with you (moves %d)\n", g.moves);
        abort();
      }
      g_dumpEnabled = false;
      tapRect(c4ui::colRect(3));
      tapRect(c4ui::colRect(2));
      if (g.moves != 4 || c4::toMove(g) != 0) {
        printf("FOUR ROW FAIL: two drops gave %d discs (want 4: each answered)\n", g.moves);
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_fourrow_play");
      tapRect(c4ui::colRect(4));
      // Undo takes back the pair.
      const int before = g.moves;
      setScreen("tool_fourrow_undo");
      tapRect(c4ui::UNDO);
      if (g.moves != before - 2 || c4::toMove(g) != 0) {
        printf("FOUR ROW FAIL: Undo left %d discs (want %d), turn %d\n", g.moves, before - 2,
               c4::toMove(g));
        abort();
      }
      // A win: three of yours on the bottom row with the fourth open.
      g_dumpEnabled = false;
      c4::reset(g, 0);
      for (int col : {0, 6, 1, 6, 2, 5}) c4::play(g, col);
      g_dumpEnabled = true;
      const int wonBefore = prefs.getInt("c4_w1", 0);
      setScreen("tool_fourrow_won");
      tapRect(c4ui::colRect(3));
      if (c4::winner(g) != 0 || prefs.getInt("c4_w1", 0) != wonBefore + 1) {
        printf("FOUR ROW FAIL: the four did not win or was not recorded\n");
        abort();
      }
      // Undo is shut once the result is written.
      tapRect(c4ui::UNDO);
      if (c4::winner(g) != 0) {
        printf("FOUR ROW FAIL: Undo reopened a finished game\n");
        abort();
      }
      // The board starts the next game; the Sticky opens it (take turns).
      setScreen("tool_fourrow_sticky_opens");
      tapRect(c4ui::colRect(0));
      if (g.moves != 1 || g.first != 1) {
        printf("FOUR ROW FAIL: the second game did not open with the Sticky's disc\n");
        abort();
      }
      if (!fr->hostVsSticky()) abort();
      // Its opening disc cannot be undone.
      tapRect(c4ui::UNDO);
      if (g.moves != 1) {
        printf("FOUR ROW FAIL: Undo removed the Sticky's opening disc\n");
        abort();
      }
      // Two players: a full column refuses.
      setScreen("tool_fourrow_2p");
      tapRect(c4ui::CHIP_FRIEND);
      g_dumpEnabled = false;
      for (int i = 0; i < 6; i++) tapRect(c4ui::colRect(0));
      const int full = g.moves;
      tapRect(c4ui::colRect(0));
      if (full != 6 || g.moves != 6) {
        printf("FOUR ROW FAIL: a full column took a disc (%d -> %d)\n", full, g.moves);
        abort();
      }
      // Undo in two-player takes one disc back.
      tapRect(c4ui::UNDO);
      if (g.moves != 5) {
        printf("FOUR ROW FAIL: two-player Undo took %d back\n", 6 - g.moves);
        abort();
      }
      g_dumpEnabled = true;
      setScreen("tool_fourrow_buttons");
      toybox.onButton(SideBtn::Up);   // the pointer appears
      g_dumpEnabled = false;
      toybox.onButton(SideBtn::Ok);   // and drops
      if (g.moves != 6) {
        printf("FOUR ROW FAIL: OK did not drop at the pointer\n");
        abort();
      }
      g_dumpEnabled = true;
    }
    setScreen("tool_fourrow_more");
    more();
    if (!fr->hostInMenu()) abort();
    g_dumpEnabled = false;
    back();
    tapRect(c4ui::CHIP_AI);  // back to the Sticky for the next visit
    back();
    if (toybox.hostInApp()) {
      printf("FOUR ROW FAIL: Back did not leave for the hub\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("four in a row ok (drop and answer, undo pair, win recorded, full column, 2 players)\n");

    // ---- Blackjack ----
    for (const char* k : {"bj_tab", "bj_bet", "h_bj"}) prefs.remove(k);
    setScreen("help_blackjack");
    toybox.open(false, 15);
    auto* bjt = static_cast<BlackjackTool*>(toybox.hostActive());
    setScreen("tool_blackjack");
    tapRect(help::OK_BTN);
    {
      bj::Table& t = bjt->hostTable();
      if (t.chips != bj::START_CHIPS || t.phase != bj::BETTING) {
        printf("BLACKJACK FAIL: a new table has %ld chips, phase %d\n", (long)t.chips, t.phase);
        abort();
      }
      g_dumpEnabled = false;
      // Deal until a hand is in play (a natural settles at once).
      for (int i = 0; i < 20 && t.phase != bj::PLAYING; i++) tapRect(bjui::DEAL);
      g_dumpEnabled = true;
      if (t.phase != bj::PLAYING) {
        printf("BLACKJACK FAIL: twenty deals and never a hand to play\n");
        abort();
      }
      setScreen("tool_blackjack_hand");
      stickyHost.refresh(false);
      const int cardsBefore = t.you.n;
      setScreen("tool_blackjack_hit");
      tapRect(bjui::HIT);
      if (t.you.n != cardsBefore + 1) {
        printf("BLACKJACK FAIL: Hit did not add a card\n");
        abort();
      }
      // The hit may have ended the hand: deal on until there is one to stand on.
      g_dumpEnabled = false;
      for (int i = 0; i < 30 && t.phase != bj::PLAYING; i++) tapRect(bjui::DEAL);
      g_dumpEnabled = true;
      if (t.phase == bj::PLAYING) {
        setScreen("tool_blackjack_dealer");
        tapRect(bjui::STAND);
        if (t.phase != bj::DEALER && t.phase != bj::DONE) abort();
      }
      if (t.phase == bj::DEALER) {
        tapRect(bjui::DEAL);  // nothing answers during the dealer's turn
        if (t.phase != bj::DEALER) {
          printf("BLACKJACK FAIL: a tap cut the dealer's turn short\n");
          abort();
        }
        g_dumpEnabled = false;
        for (int i = 0; i < bjui::DEALER_BEAT - 1; i++) toybox.tick();
        g_dumpEnabled = true;
        setScreen("tool_blackjack_dealer_draw");
        bjt->hostDealerDone();
      }
      if (t.phase != bj::DONE) {
        printf("BLACKJACK FAIL: Stand did not settle the hand\n");
        abort();
      }
      const int won = prefs.getInt("bj_w", 0), lost = prefs.getInt("bj_l", 0),
                push = prefs.getInt("bj_p", 0);
      if (won + lost + push < 1) {
        printf("BLACKJACK FAIL: the settled hand was not recorded\n");
        abort();
      }
      // The bet button walks 5 / 10 / 25.
      tapRect(bjui::BET);
      if (prefs.getInt("bj_bet", 1) != 2) {
        printf("BLACKJACK FAIL: Bet did not move to 25\n");
        abort();
      }
      // Out of chips: the one button left starts again with 100.
      t.chips = 3;
      setScreen("tool_blackjack_broke");
      stickyHost.refresh(false);
      tapRect(bjui::DEAL);  // Deal is gone; the whole row restarts
      if (t.chips != bj::START_CHIPS || t.phase != bj::BETTING) {
        printf("BLACKJACK FAIL: Start again did not refill the chips\n");
        abort();
      }
    }
    setScreen("tool_blackjack_more");
    more();
    g_dumpEnabled = false;
    back();
    back();
    g_dumpEnabled = true;
    printf("blackjack ok (deal, hit, stand, recorded, bet, out of chips)\n");

    // ---- Higher or Lower ----
    for (const char* k : {"hl_state", "h_hl"}) prefs.remove(k);
    setScreen("help_hilo");
    toybox.open(false, 16);
    auto* hl = static_cast<HiLoTool*>(toybox.hostActive());
    setScreen("tool_hilo");
    tapRect(help::OK_BTN);
    {
      // Guess right three times (the next card is known here), then wrong.
      g_dumpEnabled = false;
      int right = 0;
      for (int i = 0; i < 40 && right < 3; i++) {
        const int a = cards::highValue(hl->hostShown()), b = cards::highValue(hl->hostNext());
        if (a == b) {
          tapRect(hlui::HIGHER);  // a push: the run goes on
          continue;
        }
        tapRect(b > a ? hlui::HIGHER : hlui::LOWER);
        right++;
      }
      g_dumpEnabled = true;
      if (hl->hostStreak() != 3 || hl->hostOver()) {
        printf("HILO FAIL: three right guesses gave a streak of %d\n", hl->hostStreak());
        abort();
      }
      setScreen("tool_hilo_right");
      stickyHost.refresh(false);
      g_dumpEnabled = false;
      for (int i = 0; i < 40 && !hl->hostOver(); i++) {
        const int a = cards::highValue(hl->hostShown()), b = cards::highValue(hl->hostNext());
        if (a == b) {
          tapRect(hlui::HIGHER);
          continue;
        }
        g_dumpEnabled = true;
        setScreen("tool_hilo_over");
        tapRect(b > a ? hlui::LOWER : hlui::HIGHER);
      }
      g_dumpEnabled = true;
      if (!hl->hostOver() || prefs.getInt("hl_best", 0) != 3) {
        printf("HILO FAIL: the wrong guess did not end the run (best %d)\n",
               prefs.getInt("hl_best", 0));
        abort();
      }
      setScreen("tool_hilo_again");
      tapRect(hlui::AGAIN);
      if (hl->hostOver() || hl->hostStreak() != 0) abort();
    }
    g_dumpEnabled = false;
    back();
    g_dumpEnabled = true;
    printf("higher or lower ok (right, push, wrong ends the run, best kept)\n");

    // ---- Codebreaker ----
    for (const char* k : {"cb_state", "cb_lvl", "h_cb"}) prefs.remove(k);
    setScreen("help_code");
    toybox.open(false, 17);
    auto* cbt = static_cast<CodeTool*>(toybox.hostActive());
    setScreen("tool_code");
    tapRect(help::OK_BTN);
    {
      cb::Game& g = cbt->hostGame();
      // Two wrong-ish tries, then the code itself.
      g_dumpEnabled = false;
      for (int r = 0; r < 2; r++) {
        for (int i = 0; i < g.slots; i++) {
          int sym = (g.secret[i] + 1 + r + i) % cb::SYMS;
          // Easy refuses a symbol twice in a row: take the next one allowed.
          while (!cb::allowed(g, i, sym)) sym = (sym + 1) % cb::SYMS;
          tapRect(cbui::palRect(sym));
        }
        tapRect(cbui::CHECK);
      }
      if (g.row != 2) {
        printf("CODE FAIL: two checked rows gave row %d\n", g.row);
        abort();
      }
      // A half-filled row: Check refuses.
      tapRect(cbui::palRect(g.secret[0]));
      tapRect(cbui::CHECK);
      if (g.row != 2) abort();
      g_dumpEnabled = true;
      setScreen("tool_code_play");
      stickyHost.refresh(false);
      g_dumpEnabled = false;
      tapRect(cbui::DEL);
      for (int i = 0; i < g.slots; i++) tapRect(cbui::palRect(g.secret[i]));
      g_dumpEnabled = true;
      setScreen("tool_code_won");
      tapRect(cbui::CHECK);
      if (g.done != 1 || prefs.getInt("cb_w0", 0) != 1 || prefs.getInt("cb_b0", 0) != 3) {
        printf("CODE FAIL: the code was not broken in 3 (done %d, best %d)\n", g.done,
               prefs.getInt("cb_b0", 0));
        abort();
      }
      // Hard: five slots.
      g_dumpEnabled = false;
      more();
      tapRect(gk::menuRect(2));  // Level -> Normal
      tapRect(gk::menuRect(2));  // -> Hard
      tapRect(gk::menuRect(1));  // New game
      g_dumpEnabled = true;
      if (g.slots != 5) {
        printf("CODE FAIL: Hard did not hide five\n");
        abort();
      }
      setScreen("tool_code_hard");
      g_dumpEnabled = false;
      for (int i = 0; i < 3; i++) tapRect(cbui::palRect(i));
      g_dumpEnabled = true;
      stickyHost.refresh(false);
    }
    setScreen("tool_code_more");
    more();
    g_dumpEnabled = false;
    back();
    back();
    g_dumpEnabled = true;
    printf("codebreaker ok (scored rows, half row refused, broken in 3, hard has five)\n");

    // ---- Tower of Hanoi (landscape) ----
    for (const char* k : {"hn_state", "hn_n", "h_hn"}) prefs.remove(k);
    setScreen("help_hanoi");
    toybox.open(false, 18);
    auto* hnt = static_cast<HanoiTool*>(toybox.hostActive());
    if (stickyHost.canvasRotation() != 1 && stickyHost.canvasRotation() != 3) {
      printf("HANOI FAIL: opened at rotation %d, not on its side\n", stickyHost.canvasRotation());
      abort();
    }
    setScreen("tool_hanoi");
    tapRect(help::OK_WIDE);
    {
      hn::Game& g = hnt->hostGame();
      auto peg = [](int p) { toybox.onTap(hnui::pegX(p), 300); };
      setScreen("tool_hanoi_lift");
      peg(0);
      if (g.held != 0) {
        printf("HANOI FAIL: a tap on the left peg did not lift its disc\n");
        abort();
      }
      g_dumpEnabled = false;
      peg(2);  // smallest to the goal
      peg(0);
      peg(2);  // the next one onto it: refused
      if (g.moves != 1 || g.n[2] != 1) {
        printf("HANOI FAIL: a bigger disc went on a smaller (moves %d)\n", g.moves);
        abort();
      }
      peg(1);  // put it on the middle instead
      g_dumpEnabled = true;
      setScreen("tool_hanoi_two");
      stickyHost.refresh(false);
      setScreen("tool_hanoi_undo");
      tapRect(hnui::UNDO);
      if (g.moves != 1 || g.n[0] != 3) {
        printf("HANOI FAIL: Undo did not put the disc back\n");
        abort();
      }
      // Solve it the textbook way from the start: 15 moves for four discs.
      // All but the last move off camera; the last one lands on the frame.
      tapRect(hnui::RESTART);
      std::vector<std::pair<int, int>> mv;
      std::function<void(int, int, int, int)> solve = [&](int n, int a, int b, int c) {
        if (!n) return;
        solve(n - 1, a, c, b);
        mv.push_back({a, c});
        solve(n - 1, b, a, c);
      };
      solve(4, 0, 1, 2);
      g_dumpEnabled = false;
      for (size_t i = 0; i + 1 < mv.size(); i++) {
        peg(mv[i].first);
        peg(mv[i].second);
      }
      peg(mv.back().first);
      g_dumpEnabled = true;
      setScreen("tool_hanoi_solved");
      peg(mv.back().second);
      if (!hn::solved(g) || prefs.getInt("hn_b4", 0) != 15) {
        printf("HANOI FAIL: the textbook solution did not solve in 15 (best %d, moves %d)\n",
               prefs.getInt("hn_b4", 0), g.moves);
        abort();
      }
    }
    g_dumpEnabled = true;
    setScreen("tool_hanoi_more");
    toybox.onTap(800 - 28, 20);
    if (!hnt->hostInMenu()) {
      printf("HANOI FAIL: More in the landscape corner did not open\n");
      abort();
    }
    g_dumpEnabled = false;
    back();
    back();
    g_dumpEnabled = true;
    if (toybox.hostInApp() || stickyHost.canvasRotation() != 0) {
      printf("HANOI FAIL: leaving did not stand the panel back up (rotation %d)\n",
             stickyHost.canvasRotation());
      abort();
    }
    printf("hanoi ok (sideways, lift and place, bigger-on-smaller refused, undo, 15 moves)\n");

    // ---- Memory ----
    for (const char* k : {"mp_state", "mp_size", "h_mp"}) prefs.remove(k);
    setScreen("help_memory");
    toybox.open(false, 19);
    auto* mpt = static_cast<MemoryTool*>(toybox.hostActive());
    setScreen("tool_memory");
    tapRect(help::OK_BTN);
    {
      mp::Game& g = mpt->hostGame();
      // Two that do not match.
      int a = 0, b = 1;
      while (g.sym[b] == g.sym[a]) b++;
      g_dumpEnabled = false;
      tapRect(mpui::cellRect(g.size, a));
      g_dumpEnabled = true;
      setScreen("tool_memory_miss");
      tapRect(mpui::cellRect(g.size, b));
      if (g.a != a || g.b != b) abort();
      // The next tap turns them back and turns its own card.
      int c = 2;
      while (c == a || c == b) c++;
      g_dumpEnabled = false;
      tapRect(mpui::cellRect(g.size, c));
      if (g.st[a] != mp::DOWN || g.st[b] != mp::DOWN || g.st[c] != mp::UP) {
        printf("MEMORY FAIL: the odd pair did not go back on the next tap\n");
        abort();
      }
      // Now find every pair: the card left up first, then the rest in order.
      for (int guard = 0; guard < 64 && !mp::won(g); guard++) {
        int k = g.a;
        if (k < 0)
          for (int i = 0; i < mp::cellsFor(g.size) && k < 0; i++)
            if (!mp::isBlank(g, i) && g.st[i] == mp::DOWN) k = i;
        int j = -1;
        for (int i = 0; i < mp::cellsFor(g.size) && j < 0; i++)
          if (i != k && g.sym[i] == g.sym[k]) j = i;
        if (g.st[k] == mp::DOWN) tapRect(mpui::cellRect(g.size, k));
        if (mp::pairsFor(g.size) - g.found == 1) {
          g_dumpEnabled = true;
          setScreen("tool_memory_done");
        }
        tapRect(mpui::cellRect(g.size, j));
      }
      g_dumpEnabled = true;
      if (!mp::won(g) || !mpt->hostResult() || prefs.getInt("mp_w1", 0) != 1) {
        printf("MEMORY FAIL: every pair found but no result (found %d)\n", g.found);
        abort();
      }
      setScreen("tool_memory_board");
      tapRect(gk::RES_LEFT);  // Close: the finished board
      // 5x5, with its blank middle.
      g_dumpEnabled = false;
      toybox.onTap(BACK_W / 2, TOPBAR_H / 2);
      toybox.open(false, 19);
      mpt = static_cast<MemoryTool*>(toybox.hostActive());
      tapRect(help::OK_BTN);  // the card comes back on every open until "don't show"
      more();
      tapRect(gk::menuRect(2));  // Grid -> 5 x 5 (nothing turned: at once)
      g_dumpEnabled = true;
      setScreen("tool_memory_5x5");
      back();
      if (mpt->hostGame().size != 2 || !mp::isBlank(mpt->hostGame(), 12)) {
        printf("MEMORY FAIL: 5 x 5 did not deal with a blank centre (size %d, sym12 %d, flips %d, mp_size %d)\n", mpt->hostGame().size, mpt->hostGame().sym[12], mpt->hostGame().flips, prefs.getInt("mp_size", -1));
        abort();
      }
      g_dumpEnabled = false;
      more();
      tapRect(gk::menuRect(2));  // -> 3 x 4
      g_dumpEnabled = true;
      setScreen("tool_memory_3x4");
      back();
    }
    setScreen("tool_memory_more");
    more();
    g_dumpEnabled = false;
    back();
    back();
    g_dumpEnabled = true;
    if (inTool(19)) abort();
    printf("memory ok (odd pair back on the next tap, all pairs, 5x5 blank, 3x4)\n");
  }


  // --- C5: Level and Climate (tool indexes 20, 21) --------------------------------
  {
    auto more = [] { toybox.onTap(EPD_W - 28, 20); };
    auto back = [] { toybox.onTap(BACK_W / 2, TOPBAR_H / 2); };
    // ---- Level ----
    for (const char* k : {"lv_ox", "lv_oy", "lv_oe", "lv_rate", "h_lv"}) prefs.remove(k);
    sensors::hostSetImu(true);
    sensors::hostSetAccel(0, 0, 16384);  // flat on a table, dead level
    setScreen("help_level");
    toybox.open(false, 20);
    auto* lv = static_cast<LevelTool*>(toybox.hostActive());
    setScreen("tool_level_flat");
    tapRect(help::OK_BTN);
    if (lv->hostReading().mode != lvl::FLAT || !lvl::isLevel(lv->hostReading())) {
      printf("LEVEL FAIL: flat and level did not read as such\n");
      abort();
    }
    sensors::hostSetAccel(900, -600, 16300);  // tilted a few degrees
    setScreen("tool_level_flat_tilt");
    lv->hostSample();
    lv->hostSample();
    if (lvl::isLevel(lv->hostReading())) {
      printf("LEVEL FAIL: a 3 degree tilt read as level\n");
      abort();
    }
    setScreen("tool_level_zero");
    tapRect(lvui::zeroRect(EPD_W, EPD_H));
    if (!prefs.isKey("lv_ox")) {
      printf("LEVEL FAIL: Set zero kept nothing\n");
      abort();
    }
    // Standing on its bottom edge (portrait), a degree off.
    sensors::hostSetAccel(290, -16380, 0);
    setScreen("tool_level_edge");
    lv->hostSample();
    lv->hostSample();
    lv->hostSample();
    if (lv->hostReading().mode != lvl::EDGE || stickyHost.canvasRotation() != 0) {
      printf("LEVEL FAIL: standing upright did not read as an edge (rot %d)\n",
             stickyHost.canvasRotation());
      abort();
    }
    // On a long edge: the screen turns to match.
    sensors::hostSetAccel(-16380, 200, 0);
    setScreen("tool_level_edge_long");
    for (int i = 0; i < 4; i++) lv->hostSample();
    if (stickyHost.canvasRotation() != 1) {
      printf("LEVEL FAIL: on its long edge the screen did not turn (rot %d)\n",
             stickyHost.canvasRotation());
      abort();
    }
    setScreen("tool_level_more");
    toybox.onTap(800 - 28, 20);
    if (!lv->hostInMenu()) {
      printf("LEVEL FAIL: More did not open on its side\n");
      abort();
    }
    g_dumpEnabled = false;
    tapRect(gk::menuRect(1, 800));  // Redraw every -> 1 s
    if (prefs.getInt("lv_rate", 0) != 1) {
      printf("LEVEL FAIL: the redraw setting did not move\n");
      abort();
    }
    back();
    back();
    g_dumpEnabled = true;
    if (toybox.hostInApp() || stickyHost.canvasRotation() != 0) {
      printf("LEVEL FAIL: leaving did not stand the panel back up\n");
      abort();
    }
    sensors::hostSetImu(false);
    g_dumpEnabled = false;
    toybox.open(false, 20);
    g_dumpEnabled = true;
    setScreen("tool_level_none");
    tapRect(help::OK_BTN);
    g_dumpEnabled = false;
    back();
    g_dumpEnabled = true;
    sensors::hostSetAccel(0, -16384, 0);
    printf("level ok (flat, tilt, zero kept, upright edge, long edge turns the screen, rate, no sensor)\n");

    // ---- Climate ----
    const bool hadWifi = homenet::haveWifi(prefs);
    for (const char* k : {"cl_unit", "cl_off", "h_cl", wx::K_CITY, wx::K_GEO, wx::K_NOW})
      prefs.remove(k);
    sensors::hostSetClimate(true, 252, 48);
    sensors::hostSetBatteryTemp(true, 291);
    setScreen("help_climate");
    toybox.open(false, 21);
    auto* cl = static_cast<ClimateTool*>(toybox.hostActive());
    g_dumpEnabled = false;
    tapRect(help::OK_BTN);
    // Half an hour of a warming room.
    for (int i = 0; i < 90; i++) {
      sensors::hostSetClimate(true, 245 + (i * 13) / 90 + (i % 7 == 0 ? 2 : 0), 48 + i / 30);
      cl->hostRead(1);
    }
    g_dumpEnabled = true;
    if (cl->hostPoints() != 31) {  // the reading on opening, then 90 more
      printf("CLIMATE FAIL: 90 readings made %d graph points (want 31)\n", cl->hostPoints());
      abort();
    }
    setScreen("tool_climate");
    stickyHost.refresh(false);
    // Refresh with nowhere to look: it says so.
    if (!hadWifi) homenet::saveWifi(prefs, "HomeNet", "password1");
    setScreen("tool_climate_no_place");
    tapRect(clui::REFRESH);
    if (prefs.isKey(wx::K_NOW)) abort();
    // The place from the phone (owner): More > Outdoor place serves the page
    // on the home Wi-Fi.
    g_dumpEnabled = false;
    more();
    g_dumpEnabled = true;
    setScreen("tool_climate_phone");
    tapRect(gk::menuRect(2));  // Outdoor place
    if (!cl->hostPhone() || !cl->hostPhoneHome()) {
      printf("CLIMATE FAIL: Outdoor place did not open the home Wi-Fi page\n");
      abort();
    }
    setScreen("tool_climate_phone_saved");
    // Picked from the suggestions: the coordinates come with it, so the
    // fetch below must not need a lookup of its own.
    if (!cl->hostPhonePick("Bangkok", 137540, 1005014, "Bangkok, Thailand") || !wx::haveCity(prefs) ||
        !prefs.isKey(wx::K_GEO)) {
      printf("CLIMATE FAIL: the phone's place was not kept\n");
      abort();
    }
    g_dumpEnabled = false;
    tapRect(importui::DONE_BTN);
    g_dumpEnabled = true;
    if (cl->hostPhone()) abort();
    setScreen("tool_climate_outdoor");
    tapRect(clui::REFRESH);
    if (!prefs.isKey(wx::K_NOW) || !prefs.isKey(wx::K_GEO)) {
      printf("CLIMATE FAIL: a fetch kept no reading\n");
      abort();
    }
    // Battery: read the room every 30 s, and the outside every 10 min on
    // its own while Climate is open.
    g_dumpEnabled = false;
    more();
    tapRect(gk::menuRect(1));  // Read the room every -> 30 s
    tapRect(gk::menuRect(3));  // Update outside -> every 10 min
    back();
    if (prefs.getInt("cl_int", 0) != 1 || prefs.getInt("cl_out", 0) != 1) {
      printf("CLIMATE FAIL: the interval rows did not move\n");
      abort();
    }
    {
      const int before = wx::g_hostFetches;
      for (int i = 0; i < 10 * 60 * 50 + 5; i++) toybox.tick();
      if (wx::g_hostFetches != before + 1) {
        printf("CLIMATE FAIL: ten minutes made %d fetches (want 1)\n", wx::g_hostFetches - before);
        abort();
      }
    }
    more();
    tapRect(gk::menuRect(3));  // -> every 30 min
    tapRect(gk::menuRect(3));  // -> manual
    // Fahrenheit, through More.
    tapRect(gk::menuRect(4));  // Units
    back();
    g_dumpEnabled = true;
    setScreen("tool_climate_f");
    stickyHost.refresh(false);
    setScreen("tool_climate_more");
    more();
    g_dumpEnabled = false;
    tapRect(gk::menuRect(4));  // back to C
    back();
    back();
    g_dumpEnabled = true;
    // No sensor at all.
    sensors::hostSetClimate(false);
    g_dumpEnabled = false;
    toybox.open(false, 21);
    g_dumpEnabled = true;
    setScreen("tool_climate_none");
    tapRect(help::OK_BTN);
    g_dumpEnabled = false;
    back();
    g_dumpEnabled = true;
    sensors::hostSetBatteryTemp(false);
    for (const char* k : {wx::K_CITY, wx::K_GEO, wx::K_NOW}) prefs.remove(k);
    if (!hadWifi) homenet::forgetWifi(prefs);
    printf("climate ok (readings, graph points, no place, fetch kept, F, no sensor)\n");
  }

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
    // TAP TO LOCK (owner, 27 Nov): the plate at the top, and a tap anywhere on
    // the note keeps the angle it follows.
    g_dumpEnabled = true;
    setScreen("note_orient_tap_to_lock");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    {
      const int w = stickyHost.canvas().width();
      tapRect(rmenu::lockPlate(w));
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
    tapAct(setui::ACT_LOCK);  // into the lock page
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
    tapRect(setui::chipRect(lock::emptyIndexOf(lock::EMPTY_COVER)));
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

    // v2: the two covers over the page, drawn over a stand-in page of text
    // faded the way the power-off path fades the reader's page.
    for (const uint8_t m : {lock::EMPTY_COVER_ON_PAGE, lock::EMPTY_COVER_INK}) {
      setScreen(m == lock::EMPTY_COVER_INK ? "lockscreen_cover_ink" : "lockscreen_cover_on_page");
      epd.clear();
      ToolsCanvas& lc = stickyHost.sharedCanvas();
      for (int ly = 40, k = 0; ly < 760; ly += 34, k++)
        lc.text(24, ly, k % 3 ? "The rain had not stopped since dusk. She" :
                                "turned another page, and the little", TS_MED, true);
      epd.dim(m == lock::EMPTY_COVER_ON_PAGE && lock::config().pageDim < 2 ? 2
                                                                           : lock::config().pageDim);
      if (!bthumb::drawOverPage(lc, m)) {
        printf("COVER LOCK FAIL: mode %d did not draw over the page\n", m);
        abort();
      }
      epd.displayFull();
    }
    // Owner, 27 Nov: a book read sideways keeps its sideways page, and the
    // cover goes over it upright -- the way power-off now does it.
    {
      setScreen("lockscreen_cover_on_sideways_page");
      epd.setRotation(3);
      epd.clear();
      ToolsCanvas& lc = stickyHost.sharedCanvas();
      for (int ly = 30, k = 0; ly < 440; ly += 34, k++)
        lc.text(24, ly, k % 3 ? "The rain had not stopped since dusk, and she kept on" :
                                "turned another page, and the little lamp went", TS_MED, true);
      epd.dim(2);
      epd.setRotation(0);
      if (!bthumb::drawOverPage(stickyHost.sharedCanvas(), lock::EMPTY_COVER_ON_PAGE)) {
        printf("COVER LOCK FAIL: the cover did not go over a sideways page\n");
        abort();
      }
      epd.displayFull();
    }
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
    tapRect(setui::chipRect(lock::emptyIndexOf(lock::EMPTY_GOODBYE)));
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
    static const char* kKeys[4] = {"h_wrd", "h_mns", "h_g20", "h_xo"};
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
    const TRect q = wordui::keyRect('Q');
    const int KEY_X = q.x + q.w / 2, KEY_Y = q.y + q.h / 2;  // a letter on Wordle's keyboard
    const int OK_X = help::OK_BTN.x + help::OK_BTN.w / 2, OK_Y = help::OK_BTN.y + 20;  // Play
    const int NEVER_X = help::NEVER_BTN.x + 40, NEVER_Y = help::NEVER_BTN.y + 20;     // the switch
    const int HELP_X = EPD_W - 28, HELP_Y = 20;   // More, in the top bar
    const TRect howTo = gk::menuRect(0);          // More > How to play

    prefs.remove("w_state");  // a game in play, so a key tap paints
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
    if (help::suppressed(prefs, "wrd")) {
      printf("HELP FAIL: the switch alone closed the card for good\n");
      abort();
    }
    toybox.onTap(OK_X, OK_Y);
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
    // ...and More > How to play is the way back in.
    toybox.onTap(HELP_X, HELP_Y);
    toybox.onTap(howTo.x + howTo.w / 2, howTo.y + howTo.h / 2);
    before = g_paintCount;
    toybox.onTap(KEY_X, KEY_Y);
    if (g_paintCount != before) {
      printf("HELP FAIL: More > How to play did not raise the card\n");
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
    // XO's Clear record, the last row of its More page.
    const TRect clr = gk::menuRect(4);
    const int CX = clr.x + clr.w / 2, CY = clr.y + clr.h / 2;
    prefs.putBool("h_xo", true);
    prefs.putInt("x_foe", 1);  // a preference, not a score: must survive
    prefs.putInt("x_w", 3);
    prefs.putInt("x_l", 1);
    prefs.putInt("x_d", 2);

    toybox.open(true, 3);
    toybox.onTap(EPD_W - 28, 20);  // More
    g_dumpEnabled = true;
    setScreen("confirm_menu_armed");
    toybox.onTap(CX, CY);  // arms
    g_dumpEnabled = false;
    if (prefs.getInt("x_w", -1) != 3) {
      printf("RECORD FAIL: one tap cleared XO\n");
      abort();
    }
    toybox.onTap(240, 700);  // somewhere else on the page: disarms
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

  // --- v2 Wake to Book: the book read last opens at its place; with none,
  // the caller is told and stays home.
  {
    const bool wasDump = g_dumpEnabled;
    g_dumpEnabled = false;
    recents::Entry rec[recents::MAX];
    const int n = recents::list(prefs, rec);
    toybox.hostGoHome();
    homestyle::set(prefs, homestyle::TODAY);  // the banner over a styled home
    g_dumpEnabled = true;
    setScreen("wake_opening_book");
    const bool opened = toybox.openLastBook();
    g_dumpEnabled = false;
    homestyle::set(prefs, homestyle::WALL);
    if (n > 0 && (!opened || !toybox.hostInApp())) {
      printf("WAKE FAIL: Wake to Book did not open \"%s\"\n", rec[0].title);
      abort();
    }
    if (n == 0 && (opened || toybox.hostInApp())) {
      printf("WAKE FAIL: Wake to Book opened something with nothing read\n");
      abort();
    }
    toybox.goHub();
    printf("wake to book ok (%s)\n", n > 0 ? rec[0].title : "nothing read: home");
    // Wake to Last app: a game reopens itself; a reader goes on into its book.
    toybox.open(true, 3);  // XO
    toybox.hostGoHome();
    if (!toybox.openLastApp() || !toybox.hostInApp() || !toybox.hostIsGame() || toybox.hostIdx() != 3) {
      printf("WAKE FAIL: Wake to Last app did not reopen XO\n");
      abort();
    }
    toybox.goHub();
    printf("wake to last app ok (XO)\n");
    {  // the styled homes with a real cover (made on the book's first open)
      static uint8_t tb[bthumb::BYTES];
      const bool have = n > 0 && bthumb::load(rec[0].file, tb);
      printf("home cover: %s\n", have ? "thumbnail in flash" : "none (stand-in drawn)");
      if (n > 0 && !bthumb::haveCover(bthumb::HOME_SMALL, rec[0].file)) {
        printf("HOME FAIL: no small home cover was made for \"%s\"\n", rec[0].title);
        abort();
      }
      if (n > 0 && !bthumb::haveHome(rec[0].file)) {
        printf("HOME FAIL: no sharp home cover was made for \"%s\"\n", rec[0].title);
        abort();
      }
      // Where they were left, and a third book, as a week of reading would leave them.
      recents::Entry all[recents::MAX];
      int m = recents::list(prefs, all);
      if (m > 0) recents::place(prefs, all[0].file, "Chapter 5: The Lizardmen and the Ogres", 42);
      m = recents::list(prefs, all);
      if (m >= 2) {
        snprintf(all[1].chap, sizeof(all[1].chap), "%s", "Interlude");
        all[1].pct = 78;
        static const char* const more[3] = {"Mushoku Tensei - Volume 04", "Overlord 01", "Spice and Wolf"};
        for (int k = 0; m < recents::MAX && k < 3; k++, m++) {
          all[m] = recents::Entry{};
          all[m].kind = recents::KIND_EPUB;
          snprintf(all[m].file, sizeof(all[m].file), "/books/%s.epub", more[k]);
          snprintf(all[m].title, sizeof(all[m].title), "%s", more[k]);
          all[m].pct = 12;
        }
        recents::save(prefs, all, m);
      }
      recents::list(prefs, all);
      if (all[0].pct != 42 || strcmp(all[0].chap, "Chapter 5: The Lizardmen and the Ogres") != 0) {
        printf("HOME FAIL: the place did not stick (%d, %s)\n", all[0].pct, all[0].chap);
        abort();
      }
      g_dumpEnabled = true;
      toybox.hostGoHome();
      homestyle::set(prefs, homestyle::BOOKS);
      setScreen("home_books_cover");
      stickyHost.refresh(true);
      homestyle::set(prefs, homestyle::TODAY);
      setScreen("home_today_cover");
      stickyHost.refresh(true);
      setScreen("home_after_covers");
      g_dumpEnabled = false;
      homestyle::set(prefs, homestyle::WALL);
    }
    g_dumpEnabled = wasDump;
  }
  // --- v2 Notes on the home Wi-Fi (as the Picker): toybox.local/write first,
  // the Sticky's own access point one tap away.
  {
    homenet::saveWifi(stickyHost.prefs(), "Home-2.4", "secret123");
    homenet::g_hostNet = homenet::Result::Ok;
    toybox.open(false, 6);
    auto* nt = static_cast<NoteTool*>(toybox.hostActive());
    g_dumpEnabled = true;
    setScreen("tool_note_pair_joining");  // the paint made while the radio looks
    nt->openPairing();
    g_dumpEnabled = false;
    if (!nt->hostNetHome()) {
      printf("NOTE FAIL: a saved home Wi-Fi did not carry the notes page\n");
      abort();
    }
    g_dumpEnabled = true;
    setScreen("tool_note_pair_home");
    stickyHost.refresh(true);
    g_dumpEnabled = false;
    tapRect(nui::ALT_BTN);
    if (nt->hostNetHome() || !nt->hostNetUp()) {
      printf("NOTE FAIL: the backup did not switch to the access point\n");
      abort();
    }
    tapRect(nui::DONE_BTN);
    homenet::forgetWifi(stickyHost.prefs());
    toybox.goHub();
    printf("notes home wifi ok (toybox.local/write, own access point as backup)\n");
    g_dumpEnabled = true;
  }

  // Every drawer carries More in its top-right corner, and it opens Settings.
  {
    g_dumpEnabled = false;
    for (int f = 0; f < 3; f++) {
      toybox.goHub();
      toybox.onTap(f * (EPD_W / 3) + EPD_W / 6, EPD_H - 30);  // the dock: drawer f
      toybox.onTap(EPD_W - 28, 20);                            // More
      if (!toybox.hostInSettings()) {
        printf("DRAWER FAIL: More in drawer %d did not open Settings\n", f);
        abort();
      }
    }
    toybox.goHub();
    g_dumpEnabled = true;
    printf("drawer More ok (all three open Settings)\n");
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
          if (strcmp(static_cast<FlashTool*>(toybox.hostActive())->hostDeckName(), "hanzi") == 0) break;
          toybox.onTap(0, 0);  // back to the deck list
        }
      }
      if (strcmp(static_cast<FlashTool*>(toybox.hostActive())->hostDeckName(), "hanzi") != 0) {
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

  // --- damaged packs ----------------------------------------------------------
  // A pack comes from a flash partition or a card: it is data, and a bad one
  // must be refused whole, never half-registered or read past its end. Every
  // cut of the base pack, and a few corrupted block tables, go through the
  // real loader (run this build under -fsanitize=address to see the reads).
  {
    std::string good = tfs::hostFs()["/fonts/base_font.tfp"];
    const int before = gfx::loadedFaceCount();
    int refused = 0, tries = 0;
    auto offer = [&](const std::string& blob) {
      char name[32];
      snprintf(name, sizeof(name), "/fonts/bad%d.tfp", tries++);
      tfs::hostFs()[name] = blob;
      gfx::loadFontPacks();
      tfs::hostFs().erase(name);
      if (gfx::loadedFaceCount() == before) refused++;
    };
    // Cuts: inside the header, the cps, the glyph table, the block tables, the stream.
    for (size_t cut : {size_t(3), size_t(9), size_t(30), size_t(5000), size_t(100000),
                       size_t(110000), good.size() / 2, good.size() - 5})
      offer(good.substr(0, cut));
    // Not a base pack any more (flags 0) but every face truncated: refused.
    // A block claiming more raw bytes than a slot holds: refused.
    {
      std::string b = good;
      const uint32_t count = *(const uint32_t*)&b[12 + 8];
      const uint32_t nblk = *(const uint32_t*)&b[12 + 12];
      size_t at = 12 + 20 + count * 2;
      at = (at + 3) & ~(size_t)3;
      at += (size_t)count * 12;
      uint32_t* zraw = (uint32_t*)&b[at];
      zraw[1] = zraw[0] + 9000;  // > INTL_ZBLOCK_MAX
      b[8] = 0;                  // offer it as a language pack
      offer(b);
      std::string c = good;
      uint32_t* zoff = (uint32_t*)&c[at + (nblk + 1) * 4];
      zoff[1] = 0x7fffffff;      // stream offset far past the end
      c[8] = 0;
      offer(c);
    }
    if (refused != tries) {
      printf("PACK GUARD FAIL: %d of %d damaged packs accepted\n", tries - refused, tries);
      abort();
    }
    printf("damaged packs refused (%d of %d)\n", refused, tries);
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
    tapRect(g2048ui::NEW);
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

  checkThaiLines();
  checkDecorations();
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
// A root settings row lives in one tab (v2): open that tab, then the row.
static void tapAct(int act) {
  tapRect(setui::tabRect(setui::TAB_OF[act]));
  tapRect(setui::actionRect(act));
}
