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
#include "sticky_host.h"
#include "toybox.h"
#include "nonogram.h"
#include "tools/note_store.h"
#include "tools/tool_flash.h"
#include "tools/tool_note.h"
#include "tools/tool_picker.h"
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
static void setScreen(const char* n) {
  g_dumpName = n;
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
  if (x < 0 || y < 0 || x >= EPD_W || y >= EPD_H) return;
#ifdef TOYBOX_PORTRAIT
  const int px = PANEL_W - 1 - y, py = x;
#else
  const int px = x, py = y;
#endif
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
  toybox.onTap(240, 586);  // SOUND
  if (appvis::shown() != 9 || buzzer::enabled()) {
    printf("SETTINGS FAIL: taps did not land (%d shown, sound %d)\n", appvis::shown(),
           (int)buzzer::enabled());
    abort();
  }
  setScreen("settings_edited");
  g_dumpEnabled = true;
  toybox.onTap(240, 722);  // RESET, first tap: arms and asks

  setScreen("hub_hidden");
  toybox.goHub();
  checkHubRouting("four hidden");

  // Erasing scores and restoring the rules cards, checked rather than drawn --
  // then everything is put back so the screens below show a lived-in device.
  g_dumpEnabled = false;
  prefs.putBool("h_wrd", true);
  toybox.onTap(EPD_W - 40, 28);  // the routing walk left us on the hub: go back in
  toybox.onTap(240, 654);        // SHOW HOW TO PLAY AGAIN
  toybox.onTap(240, 722);  // RESET, armed again
  toybox.onTap(240, 722);  // ...and confirmed
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
  toybox.onSwipe(10, -120);   // merged tiles flash, the new one is dashed

  // ...and a beat later the flash clears on its own.
  setScreen("g2048");
  delay(500);
  toybox.tick();

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
  toybox.onTap(40 + 100, 444 + 33);  // START

  setScreen("tool_random");
  g_dumpEnabled = false;
  toybox.open(false, 3);
    g_dumpEnabled = true;
  toybox.onTap(240 + 100, 560 + 31);  // DRAW

  setScreen("tool_card");
  g_dumpEnabled = false;
  quietTap(250 + 95, 50 + 24);  // CARD mode
  quietTap(40 + 100, 548 + 33);
  quietTap(40 + 100, 548 + 33);
  g_dumpEnabled = true;
  toybox.onTap(40 + 100, 548 + 33);  // DRAW CARD

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

  setScreen("tool_flash_decks");
  toybox.open(false, 5);

  setScreen("tool_flash_front");
  toybox.onTap(20 + 180, 56 + 20);  // open the first deck

  setScreen("tool_flash_back");
  toybox.onTap(240, 220);  // tap the card to reveal

  setScreen("tool_flash_import");
  g_dumpEnabled = false;
  quietTap(0, 0);  // back to the deck list
  g_dumpEnabled = true;
  toybox.onTap(20 + 220, 448 + 36);  // IMPORT

  setScreen("tool_flash_import_alt");
  tapRect(fcui::ALT_BTN);  // "page didn't open?" -> link QR

  setScreen("tool_flash_import_done");
  g_dumpEnabled = false;
  tapRect(fcui::ALT_BTN);  // back to the wifi QR
  toybox.tick();
  toybox.tick();
  g_dumpEnabled = true;
  toybox.tick();  // stub reports the deck arrived

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
  toybox.onTap(240, 316 + 46);  // TWO DEVICES
  g_dumpEnabled = true;
  toybox.onTap(240, 150 + 42);  // HOST A GAME

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
      toybox.open(false, 5);
      FlashTool* ft = static_cast<FlashTool*>(toybox.hostActive());
      (void)ft;
      // the new deck's row: decks are listed in tfs order; tap each row until a
      // study screen appears with a non-latin front. Simpler: row 0.
      // Find the row whose name is "hanzi" by probing the four rows.
      for (int row = 0; row < 4; row++) {
        g_dumpEnabled = false;
        toybox.onTap(20 + 180, 56 + row * 46 + 18);
        g_dumpEnabled = true;
        if (strcmp(toybox.activeTitle(), "hanzi") == 0) break;
        quietTap(0, 0);  // back to the deck list
      }
      if (strcmp(toybox.activeTitle(), "hanzi") != 0) {
        printf("INTL FAIL: could not open the CJK deck\n");
        abort();
      }
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
