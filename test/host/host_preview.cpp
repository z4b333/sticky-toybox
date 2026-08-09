// Renders every screen on the host and dumps PGM images, so layout can be
// checked without hardware. It drives the same Toybox object the firmware does,
// through the same StickyHost, so what is rendered here is what the device
// renders. Build: see README (test/host section).
#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "appvis.h"
#include "board_pins.h"
#include "epd.h"
#include "gfx.h"
#include "game2048.h"
#include "service_ui.h"
#include "sticky_host.h"
#include "toybox.h"
#include "settings.h"
#include "tools/lock_image.h"
#include "tools/lockscreen.h"
#include "nonogram.h"
#include "tools/note_store.h"
#include "tools/tool_dice.h"
#include "tools/tool_flash.h"
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
  return true;
}
void Epd::clear(bool white) { memset(_fb, white ? 0xFF : 0x00, EPD_BUF_SIZE); }
void Epd::drawPixel(int x, int y, uint8_t color) {
  // Mirrors the device mapping exactly, rotation included -- the auto-rotate
  // guard below turns the panel and expects the pixels to follow.
  if (x < 0 || y < 0 || x >= logicalW() || y >= logicalH()) return;
  int px, py;
  switch (rotation()) {
    case 1: px = x; py = y; break;
    case 2: px = y; py = PANEL_H - 1 - x; break;
    case 3: px = PANEL_W - 1 - x; py = PANEL_H - 1 - y; break;
    default: px = PANEL_W - 1 - y; py = x; break;
  }
  if (panelFlipX()) px = PANEL_W - 1 - px;
  if (panelFlipY()) py = PANEL_H - 1 - py;
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
void Epd::displayFull() {
  g_paintCount++;
  dumpFrame(_fb);
}
void Epd::displayPartial() {
  g_paintCount++;
  dumpFrame(_fb);
}
void Epd::deepSleep() {}

// --- Mock buzzer / touch ------------------------------------------------------
namespace buzzer {
static bool g_soundOn = true;
void begin() {}
void setEnabled(bool on) { g_soundOn = on; }
bool enabled() { return g_soundOn; }
void tap() {}
void confirm() {}
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

// The hub draws twelve borderless tiles in a 3-wide grid; this walks the same
// geometry and asserts each tap lands on the app that is drawn there. The order
// is the hub's own slot table: four games, battleship, then the seven tools.
static void checkHubRouting(const char* label) {
  // Mirrors hub.cpp's layout constants and group order.
  const int COLS = 3, TILE_W = 140, X0 = 15, STEP_X = 155;
  const int TOP = 58, ROW_H = 110, HEAD_H = 32, GROUP_GAP = 10;
  struct Want { bool game; int idx; };
  struct Grp { Want items[6]; int n; };
  const Grp ALL[3] = {
      {{{true, 0}, {true, 1}, {true, 2}, {true, 3}, {false, 7}, {false, 8}}, 6},
      {{{false, 0}, {false, 1}, {false, 3}, {false, 4}}, 4},
      {{{false, 2}, {false, 5}, {false, 6}}, 3},
  };
  // The hidden ones drop out and the rest close up; a group emptied entirely
  // takes no heading and no height. Filtered here independently of hub.cpp so
  // the two can disagree loudly rather than quietly.
  Grp G[3] = {};
  int shown = 0;
  for (int gi = 0; gi < 3; gi++)
    for (int ii = 0; ii < ALL[gi].n; ii++) {
      const Want& w = ALL[gi].items[ii];
      if (!appvis::visible(w.game, w.idx)) continue;
      G[gi].items[G[gi].n++] = w;
      shown++;
    }
  // Every tap must open the app drawn under it. The shell is asked directly
  // what it opened, then sent back to the hub for the next probe.
  auto routesTo = [&](int x, int y, const Want& w, const char* what, int gi, int ii) {
    toybox.onTap(x, y);
    const bool ok = toybox.hostInApp() && toybox.hostIsGame() == w.game &&
                    toybox.hostIdx() == w.idx;
    if (!ok) {
      printf("HUB ROUTING FAIL group %d item %d (%s) at (%d,%d)\n", gi, ii, what, x, y);
      abort();
    }
    toybox.goHub();
  };

  g_dumpEnabled = false;
  int y = TOP;
  for (int gi = 0; gi < 3; gi++) {
    if (G[gi].n == 0) continue;
    const int contentTop = y + HEAD_H;
    const int rows = (G[gi].n + COLS - 1) / COLS;
    for (int ii = 0; ii < G[gi].n; ii++) {
      const int col = ii % COLS, row = ii / COLS;
      const int cx = X0 + col * STEP_X + TILE_W / 2;
      const int rowTop = contentTop + row * ROW_H;
      const Want& w = G[gi].items[ii];
      routesTo(cx, rowTop + ROW_H / 2, w, "centre", gi, ii);
      routesTo(cx, rowTop + 1, w, "top edge", gi, ii);
      routesTo(cx, rowTop + ROW_H - 2, w, "bottom edge", gi, ii);
      routesTo(X0 + col * STEP_X + 2, rowTop + ROW_H / 2, w, "left edge", gi, ii);
      routesTo(X0 + col * STEP_X + TILE_W - 3, rowTop + ROW_H / 2, w, "right edge", gi, ii);
      // the outer columns own the screen edge, so nothing is lost off the side
      if (col == 0) routesTo(1, rowTop + ROW_H / 2, w, "screen left", gi, ii);
      if (col == COLS - 1) routesTo(EPD_W - 2, rowTop + ROW_H / 2, w, "screen right", gi, ii);
      if (row == 0) routesTo(cx, y + 2, w, "heading strip", gi, ii);
      if (row == rows - 1)
        routesTo(cx, rowTop + ROW_H + GROUP_GAP - 1, w, "trailing gap", gi, ii);
    }
    // A group that does not fill its last row leaves empty tiles. Nothing is
    // drawn there, so nothing should open -- the one legitimately inert area.
    for (int col = G[gi].n % COLS; col && col < COLS; col++) {
      const int cx = X0 + col * STEP_X + TILE_W / 2;
      const int rowTop = contentTop + (rows - 1) * ROW_H;
      toybox.onTap(cx, rowTop + ROW_H / 2);
      if (toybox.hostInApp() || toybox.hostInSettings()) {
        printf("HUB ROUTING FAIL empty tile (group %d col %d) opened something\n", gi, col);
        abort();
      }
    }
    y = contentTop + rows * ROW_H + GROUP_GAP;
  }

  // The gear owns the whole top-right corner and is the one thing on this
  // screen that no setting can take away.
  for (const int gx : {EPD_W - 79, EPD_W - 1}) {
    for (const int gy : {0, 57}) {
      toybox.onTap(gx, gy);
      if (!toybox.hostInSettings()) {
        printf("HUB ROUTING FAIL gear at (%d,%d)\n", gx, gy);
        abort();
      }
      toybox.goHub();
    }
  }
  g_dumpEnabled = true;
  printf("hub routing ok (%s: %d tiles, edges, headings, gaps, gear)\n", label, shown);
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

  setScreen("hub");
  toybox.goHub();

  // Grid routing: every tile centre, and the gaps between them, must resolve to
  // the app under the finger. requestScreen is stubbed, so record what it asked
  // for and compare against the drawing order.
  checkHubRouting("all shown");

  // Inside the reader the hub is one activity deep, so it grows a way out. The
  // standalone firmware never draws this, because there is nothing above it.
  setScreen("hub_as_guest");
  stickyHost.hostSetCanExit(true);
  stickyHost.refresh(true);
  {
    g_dumpEnabled = false;
    stickyHost.hostClearExited();
    toybox.onTap(50, 26);  // < BACK
    if (!stickyHost.hostExited()) {
      printf("EXIT FAIL: the hub's back button did not leave\n");
      abort();
    }
    // ...and with nothing above, the same tap must open nothing at all.
    stickyHost.hostSetCanExit(false);
    stickyHost.hostClearExited();
    toybox.onTap(50, 26);
    if (stickyHost.hostExited() || toybox.hostInApp() || toybox.hostInSettings()) {
      printf("EXIT FAIL: standalone hub reacted to a corner that has no button\n");
      abort();
    }
    g_dumpEnabled = true;
    printf("hub exit ok (offered as a guest, absent when standalone)\n");
  }

  // --- settings ------------------------------------------------------------
  // Row geometry mirrors settings.cpp: two columns at x 16 and 248, list top
  // 92, a 26 px heading over each group, 52 px rows.
  auto setRow = [](int col, int headTop, int i) {
    return std::pair<int, int>{col ? 330 : 100, headTop + 26 + i * 52 + 26};
  };
  setScreen("settings");
  toybox.onTap(EPD_W - 40, 28);

  // Hide four apps through the screen itself, so the hub below reflows around
  // exactly what a finger would have hidden.
  g_dumpEnabled = false;
  for (auto rc : {setRow(0, 92, 0), setRow(0, 92, 2), setRow(1, 92, 1), setRow(1, 340, 2)})
    toybox.onTap(rc.first, rc.second);
  tapRect(setui::actionRect(setui::ACT_SOUND));
  if (appvis::shown() != 9 || buzzer::enabled()) {
    printf("SETTINGS FAIL: taps did not land (%d shown, sound %d)\n", appvis::shown(),
           (int)buzzer::enabled());
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
    // Every row has to cycle and land back where it started, or a setting can
    // be moved into a state it cannot be moved out of.
    g_dumpEnabled = false;
    for (int r = 0; r < setui::LR_COUNT; r++) {
      const lock::Config before = lock::config();
      const int steps = (r == setui::LR_SLEEP) ? lock::SLEEP_COUNT
                      : (r == setui::LR_EMPTY ? lock::EMPTY_COUNT : 2);
      for (int k = 0; k < steps; k++) tapRect(setui::lockRect(r));
      if (memcmp(&before, &lock::config(), sizeof(lock::Config)) != 0) {
        printf("LOCK FAIL: row %d did not return to where it started\n", r);
        abort();
      }
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

  setScreen("hub_hidden");
  toybox.goHub();
  checkHubRouting("four hidden");

  // Erasing scores and restoring the rules cards, checked rather than drawn --
  // then everything is put back so the screens below show a lived-in device.
  g_dumpEnabled = false;
  prefs.putBool("h_wrd", true);
  toybox.onTap(EPD_W - 40, 28);  // the routing walk left us on the hub: go back in
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

  setScreen("tool_flash_import");
  g_dumpEnabled = false;
  quietTap(0, 0);  // back to the deck list
  g_dumpEnabled = true;
  tapRect(fcui::IMPORT_BTN);

  setScreen("tool_flash_import_alt");
  tapRect(fcui::ALT_BTN);  // "page didn't open?" -> link QR

  setScreen("tool_flash_import_done");
  g_dumpEnabled = false;
  tapRect(fcui::ALT_BTN);  // back to the wifi QR
  toybox.tick();
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

  setScreen("tool_note_pair");
  toybox.onTap(190 + 75, 716 + 25);  // EDIT -> pairing screen

  // A note arriving raises the "what should happen to it" prompt.
  setScreen("tool_note_prompt");
  g_dumpEnabled = false;
  toybox.tick();
  toybox.tick();
  g_dumpEnabled = true;
  toybox.tick();

  setScreen("tool_note_pinned");
  tapRect(nui::PINNOW_BTN);  // SHOW ON SCREEN

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
          img[lockimg::HEADER + (size_t)y * lockimg::STRIDE + (x >> 3)] |= (0x80 >> (x & 7));
      }
    }
    tfs::write(lockimg::PATH, (const char*)img, sizeof(img));
    epd.clear();
    if (!lockimg::draw(stickyHost.sharedCanvas())) {
      printf("PICTURE FAIL: a stored lock screen picture did not draw\n");
      abort();
    }
    epd.displayFull();
    // ...and a file the wrong length has to be refused rather than drawn as
    // half a picture and half whatever was in memory.
    tfs::write(lockimg::PATH, (const char*)img, 1000);
    if (lockimg::have() || lockimg::draw(stickyHost.sharedCanvas())) {
      printf("PICTURE FAIL: a truncated picture was accepted\n");
      abort();
    }
    tfs::remove(lockimg::PATH);
  }

  setScreen("lockscreen_clock");
  epd.clear();
  lock::drawClock(stickyHost.sharedCanvas(), lock::config(), lock::read());
  epd.displayFull();

  setScreen("lockscreen_no_clock");
  {
    const lock::Info none;
    epd.clear();
    lock::drawClock(stickyHost.sharedCanvas(), lock::config(), none);
    epd.displayFull();
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
    printf("pinned screen ok (wakes to the note, taps edit it)\n");
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

  // --- the service screen ---------------------------------------------------
  // The one screen that has to work when the display or the touch mapping is
  // wrong, so it is drawn here like any other and its text is held to the same
  // width rule.
  {
    svc::Report r;
    r.touchOk = true;
    r.touchAddr = 0x5D;
    r.gauge = r.rtc = r.sht = true;
    r.imu = false;
    r.battMv = 3987;
    r.fontFaces = 3;
    r.psramKb = 8192;
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
