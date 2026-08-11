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
inline constexpr int HINT_R = 13;  // 26 px across: the smallest size at which
                                   // the gear keeps its hole

// Drawer pages: two columns of hairline-divided cells under a big title.
inline constexpr int FOLDER_TOP = 164;     // below the title and its rule
inline constexpr int FOLDER_BOTTOM = 796;
inline constexpr int TILE = 104;           // icon size
inline constexpr int ROW_STEP = 196;       // >= 190 px cells, per the design

// As a guest inside another firmware there is no home page at all -- the
// drawers are the top level, so they carry the dock themselves and the rows
// tighten to fit above it. Three rows of 180 in the 556 px between the title
// rule and the dock; the "+ add" ghost cell is dropped (the gear is right
// there in the header), which is what keeps a drawer to three rows at most.
inline constexpr int GUEST_ROW_STEP = 180;
inline constexpr int GUEST_FOLDER_BOTTOM = DOCK_Y - 8;

// The recently-read strip on the Study drawer: the last two books as covers,
// under the app tiles. Only drawn standalone with the tiles in two rows or
// fewer -- three rows (hidden apps growing the + add cell) leave no room, and
// the guest drawers give the space to the dock instead.
inline constexpr int REC_THUMB_W = 96, REC_THUMB_H = 160;  // a .tbk page over 5
inline constexpr int REC_HEAD_H = 44;                      // heading + rule
inline constexpr int REC_GAP = 4;                          // tiles to heading
}  // namespace hubui

#ifdef TOYBOX_HOST
// The battery cell, plain and unhaloed, so the harness can measure the bar
// against the number without the halo's black edges confusing the count.
void hubHostBattery(ToolsCanvas& c, const ToolsHost& host, int right, int top, bool black);
#endif

class HubScreen {
 public:
  struct Tap {
    enum Kind : uint8_t { None, App, Folder, Home, Settings, Exit, Recent } kind = None;
    // Settings is also the answer for the drawers' "+ add" cell, which exists
    // to bring hidden apps back.
    bool game = false;
    int idx = 0;  // app index for App, folder index for Folder, slot for Recent
  };

  void render(ToolsHost& host, ToolsCanvas& c);
  Tap hit(const ToolsHost& host, int x, int y) const;

  // Which page is up. Home is folder -1; the folders are applist group order:
  // 0 = PLAY, 1 = DECIDE, 2 = STUDY.
  bool atHome() const { return _folder < 0; }
  void goHome() { _folder = -1; }
  void openFolder(int f);
  int folder() const { return _folder; }

 private:
  int8_t _folder = -1;
  // The recently-read entries, cached at render time so the const hit() can
  // agree with what was drawn without touching NVS.
  int8_t _recN = 0;
};
