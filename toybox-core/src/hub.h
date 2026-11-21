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
inline constexpr int HINT_R = 13;  // 26 px across: the smallest size at which
                                   // the gear keeps its hole

// Drawer pages (v2): the shared top bar with the drawer's name, then a grid
// of three columns of outlined tiles, top-anchored, and the dock below.
// Touch is by column slice (a third of the width) and row, so a tap in the
// gutter between two tiles still lands on one of them.
inline constexpr int COLS = 3;
inline constexpr int CELL_W = SCREEN_W / COLS;  // 160, the touch slice
inline constexpr int FOLDER_TOP = TOPBAR_H + 14;  // 54
inline constexpr int FOLDER_BOTTOM = DOCK_Y - 8;
inline constexpr int TILE = 64;            // icon size inside a tile
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
      None, App, Folder, Home, Settings, Exit, Recent, PagePrev, PageNext
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
};
