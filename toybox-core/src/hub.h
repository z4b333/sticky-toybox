// The home screen and the three folder pages behind it.
//
// Home is a picture with a dock: the owner's wallpaper full-bleed, three
// drawn folder marks along the bottom, and nothing else that answers touch.
// Tapping a mark opens that folder as a full page of big app tiles. The
// title and battery float over the picture in white with a black edge, and
// two small marks on the right margin sit level with the physical buttons
// they explain.
#pragma once
#include "chrome.h"

// Geometry the preview harness asserts against, kept out of the .cpp so the
// assertions and the drawing cannot quietly disagree.
namespace hubui {
inline constexpr int DOCK_H = 72;                 // the bar along the bottom
inline constexpr int DOCK_Y = SCREEN_H - DOCK_H;  // 728
inline constexpr int DOCK_ICON = 44;

// The two hint marks, level with the physical UP and DOWN buttons on the
// right edge of the case. Measured off the vendor drawing: the button centres
// sit at roughly 24% and 35% of the glass, which is 192 and 282 px of 800.
inline constexpr int HINT_X = SCREEN_W - 22;
inline constexpr int HINT_GEAR_Y = 192;
inline constexpr int HINT_RESUME_Y = 282;
// v2 home: the More disc in the top-right corner (opens settings).
inline constexpr int MORE_X = SCREEN_W - 34;
inline constexpr int MORE_Y = 34;
inline constexpr int MORE_R = 22;
inline constexpr int HINT_R = 13;
// v2: the due popup's two buttons, under its words.
inline constexpr int DUE_CY = SCREEN_H / 2 - 40;
inline constexpr int DUE_BTN_H = 60;
inline TRect dueLater(int bottom) { return TRect{28, bottom - 28 - DUE_BTN_H, (SCREEN_W - 70) / 2, DUE_BTN_H}; }
inline TRect dueDone(int bottom) {
  return TRect{42 + (SCREEN_W - 70) / 2, bottom - 28 - DUE_BTN_H, (SCREEN_W - 70) / 2, DUE_BTN_H};
}  // 26 px across: the smallest size at which
                                   // the gear keeps its hole

// Drawer pages (v2): the shared top bar with the drawer's name, then a grid
// of three columns of outlined tiles, top-anchored, and the dock below.
// Touch is by column slice (a third of the width) and row, so a tap in the
// gutter between two tiles still lands on one of them.
inline constexpr int COLS = 3;
inline constexpr int CELL_W = SCREEN_W / COLS;  // 160, the touch slice
// The drawer's name stands large under the bar (owner, 25 Nov), and the grid
// hangs below it: three rows end at 610, the page turn under them at 622,
// the dock at 728.
inline constexpr int FOLDER_HEAD_Y = TOPBAR_H + 14;  // 54
inline constexpr int FOLDER_TOP = TOPBAR_H + 78;     // 118
inline constexpr int FOLDER_BOTTOM = DOCK_Y - 8;
inline constexpr int TILE = 64;            // icon size inside a tile (pre-C3)
inline constexpr int TILE_ICON = 68;       // v2 line icons (kit: 44; owner: bigger)
inline constexpr int ROW_STEP = 164;       // tile height plus the gap under it
inline constexpr int TILE_GAP = 8;
inline constexpr int GRID_X = 16;          // the outlined tiles' left edge
inline constexpr int TILE_W = (SCREEN_W - 2 * GRID_X - (COLS - 1) * TILE_GAP) / COLS;  // 144
// Where tile i (0-based, on the page) sits.
inline int cellCol(int i) { return i % COLS; }
inline int cellRow(int i) { return i / COLS; }
inline int cellCx(int i) { return cellCol(i) * CELL_W + CELL_W / 2; }
inline int cellTop(int i) { return FOLDER_TOP + cellRow(i) * ROW_STEP; }

// As a guest inside another firmware there is no home page at all -- the
// drawers are the top level, so they carry the dock themselves and the rows
// tighten to fit above it. Three rows of 180 in the 556 px between the title
// rule and the dock; the "+ add" ghost cell is dropped (the gear is right
// there in the header), which is what keeps a drawer to three rows at most.

// The recently-read strip on the Study drawer: the last two books as covers,
// under the app tiles. Only drawn standalone with the tiles in two rows or
// fewer -- three rows (hidden apps growing the + add cell) leave no room, and
// the guest drawers give the space to the dock instead.
inline constexpr int REC_ROW_H = 48;   // a recently-read title row
inline constexpr int REC_HEAD_H = 44;  // heading + rule
inline constexpr int REC_GAP = 4;      // tiles to heading

// --- home styles (kit "Home") -------------------------------------------------
// Book collection (kit HomeBooksFw, owner 27 Nov): the book being read as a
// 168x280 cover, no frame, its place beside it; three more under it at the
// shared 132x220 size; the streak a black band across the panel.
inline constexpr int BOOKS_COVER_W = 168;
inline TRect booksNow() { return TRect{16, 64, SCREEN_W - 32, 280}; }
inline constexpr int BOOKS_TEXT_X = 202, BOOKS_TEXT_W = SCREEN_W - 16 - BOOKS_TEXT_X;  // 262
inline constexpr int BOOKS_ALSO_HEAD_Y = 358, BOOKS_ALSO_Y = 390, BOOKS_GRID_W = 132, BOOKS_GRID_H = 220;
inline constexpr int BOOKS_ALSO_N = 3;
inline TRect booksAlso(int i = 0) { return TRect{16 + i * 158, BOOKS_ALSO_Y, BOOKS_GRID_W, BOOKS_GRID_H}; }
inline TRect booksAll() { return TRect{SCREEN_W - 150, BOOKS_ALSO_HEAD_Y - 8, 134, 36}; }
inline constexpr int BOOKS_STREAK_Y = 622, BOOKS_STREAK_H = 100;
// The Study drawer's covers (kit DrawerStudyFw): three under one row of tiles.
inline constexpr int REC_COVER_HEAD = 18, REC_COVER_RULE = 44, REC_COVER_TOP = 58;
inline TRect studyCover(int i, int stripTop) { return TRect{16 + i * 158, stripTop + REC_COVER_TOP, 132, 220}; }
// Calendar: the month, then today's list.
inline TRect calPrev() { return TRect{8, 50, 64, 50}; }
inline TRect calNext() { return TRect{SCREEN_W - 72, 50, 64, 50}; }
inline constexpr int CAL_X = 16, CAL_CW = (SCREEN_W - 32) / 7, CAL_HEAD_Y = 100, CAL_HEAD_H = 30;
inline constexpr int CAL_ROW_Y = CAL_HEAD_Y + CAL_HEAD_H, CAL_ROW_H = 44;
// Today: four tiles, the book, three shortcuts (kit HomeTodayFw, owner 27 Nov).
// The clock in the mid digits, the date larger under it, no rule; four tiles
// in one row, the numbers centred.
inline constexpr int TODAY_CLOCK_Y = 58, TODAY_DATE_Y = 168;
inline TRect todayTile(int i) { return TRect{16 + i * 113, 224, 107, 96}; }
inline TRect todayBook() { return TRect{16, 332, SCREEN_W - 32, 246}; }
inline constexpr int TODAY_SC_Y = 588;
inline TRect todayShortcut(int i) { return TRect{16 + i * 150, TODAY_SC_Y + 32, 148, 104}; }
// The upgrade popup's two buttons.
inline constexpr int UP_Y = 196, UP_H = 330;
inline TRect upLater() { return TRect{24, UP_Y + UP_H - 24 - 60, (SCREEN_W - 60) / 2, 60}; }
inline TRect upOpen() { return TRect{36 + (SCREEN_W - 60) / 2, UP_Y + UP_H - 24 - 60, (SCREEN_W - 60) / 2, 60}; }
}  // namespace hubui

#ifdef TOYBOX_HOST
// The battery cell, plain and unhaloed, so the harness can measure the bar
// against the number without the halo's black edges confusing the count.
void hubHostBattery(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black);
#endif

class HubScreen {
 public:
  struct Tap {
    enum Kind : uint8_t {
      None, App, Folder, Home, Settings, Exit, Recent, PagePrev, PageNext,
      DueLater, DueDone, DueSwallow,  // v2: the calendar's due popup over home
      Repaint,                        // home styles: something on home changed (a month turned)
      UpLater, UpOpen                 // v2: the "New: Setup" popup for an updated device
    } kind = None;
    // Settings is also the answer for the drawers' "+ add" cell, which exists
    // to bring hidden apps back.
    bool game = false;
    int idx = 0;  // app index for App, folder index for Folder, slot for Recent
  };

  void render(ToolsHost& host, ToolsCanvas& c);
  Tap hit(const ToolsHost& host, int x, int y) const;

  // Which page is up. Home is folder -1; the folders are applist group order:
  // 0 = PLAY, 1 = UTILITY, 2 = STUDY.
  bool atHome() const { return _folder < 0; }
  void goHome() {
    _folder = -1;
    _fpage = 0;
  }
  void openFolder(int f);
  int folder() const { return _folder; }
  // A drawer with more than six visible tiles pages them; the arrows live in
  // the header corner. Render clamps, so stepping past the end is harmless.
  void folderPageStep(int d) { _fpage = (int8_t)(_fpage + d < 0 ? 0 : _fpage + d); }
  int folderPage() const { return _fpage; }

 private:
  int8_t _folder = -1;
  int8_t _fpage = 0;
  // The recently-read entries, cached at render time so the const hit() can
  // agree with what was drawn without touching NVS.
  int8_t _recN = 0;
  // v2: the calendar item popping up over home, found at render so the const
  // hit() agrees with what is on the glass. -1 = none.
  int8_t _dueItem = -1;
  uint32_t _dueOcc = 0;
  int _dueBottom = 0;
  void drawDue(ToolsHost& host, ToolsCanvas& c);
  char _opening[48] = "";
  bool _upgrade = false;
  int8_t _calOff = 0;
  uint8_t _style = 0;     // the home style drawn last, for hit()
  int8_t _homeRecN = 0;   // recently-read books home showed
  void drawUpgrade(ToolsHost& host, ToolsCanvas& c);

 public:
  int dueItem() const { return _dueItem; }
  // v2: the upgrade popup ("New: Setup") over home, for a device updated from
  // v1. The shell raises it once a boot; Later and Open setup take it down.
  // Wake to Book: "Opening <title>" over home while the card is found, so a
  // slow (or absent) card is a message rather than a frozen splash.
  void showOpening(const char* title) {
    if (!title) {
      _opening[0] = 0;
      return;
    }
    snprintf(_opening, sizeof(_opening), "%s", title);
  }
  void showUpgrade(bool on) { _upgrade = on; }
  bool upgradeUp() const { return _upgrade; }
  // Calendar home: months away from this one.
  void stepMonth(int d) { _calOff = (int8_t)(_calOff + d); }
  int hostCalOff() const { return _calOff; }
  uint32_t dueOccurrence() const { return _dueOcc; }
  int hostDueBottom() const { return _dueBottom; }

 private:
};
