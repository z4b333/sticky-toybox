// Device settings: which apps the hub shows, sound, and two things you can put
// back the way they were.
#pragma once
#include "chrome.h"
#include "tools/lockscreen.h"

// Geometry the preview harness aims at. Remembered coordinates go stale the
// moment a row moves; these do not.
namespace setui {
inline constexpr int BTN_X = 16, BTN_W = SCREEN_W - 32, BTN_H = 54, BTN_STEP = 62;
inline constexpr int BTN_Y0 = 528;
inline TRect actionRect(int i) { return TRect{BTN_X, BTN_Y0 + i * BTN_STEP, BTN_W, BTN_H}; }
enum Action : int { ACT_SOUND, ACT_LOCK, ACT_CARDS, ACT_RESET, ACT_COUNT };

// The lock screen page. Rows are not all the same height: most are a name, a
// hint and an answer, but the one that chooses what an empty panel shows needs
// a row of four choices under it, and the three footer switches need less than
// any of them. A table of heights is easier to keep honest than a constant
// everything has to be talked out of.
inline constexpr int LOCK_Y0 = 92, LOCK_X = 16, LOCK_W = SCREEN_W - 32;
enum LockRow : int {
  LR_SLEEP,
  LR_EMPTY,
  LR_PICTURE,  // sits under the row that can ask for a picture
  LR_WAKE,
  LR_ROTATE,
  LR_TIME,
  LR_TEMP,
  LR_BATT,
  LR_COUNT
};
inline constexpr int LOCK_HEIGHTS[LR_COUNT] = {76, 128, 76, 76, 76, 62, 62, 62};
inline TRect lockRect(int i) {
  int y = LOCK_Y0;
  for (int k = 0; k < i; k++) y += LOCK_HEIGHTS[k];
  return TRect{LOCK_X, y, LOCK_W, LOCK_HEIGHTS[i] - 8};
}

// The things an empty panel can show, as chips under their heading. A value
// that cycles on a tap hides its other options; chips with the current one
// filled say what the choices are and which one is on, in the space the cycling
// answer was using anyway.
inline constexpr int CHIP_H = 46, CHIP_GAP = 6, CHIP_N = lock::EMPTY_COUNT;
inline TRect chipRect(int i) {
  const TRect r = lockRect(LR_EMPTY);
  const int w = (r.w - (CHIP_N - 1) * CHIP_GAP) / CHIP_N;
  return TRect{r.x + i * (w + CHIP_GAP), r.y + r.h - CHIP_H, w, CHIP_H};
}

// The picture row's actions, right-aligned: send one, and throw the stored one
// away. REMOVE only exists when there is something to remove.
inline constexpr int SEND_W = 150, ACT_H2 = 46, REM_W = 46;
inline TRect sendRect() {
  const TRect r = lockRect(LR_PICTURE);
  return TRect{r.x + r.w - SEND_W, r.y + (r.h - ACT_H2) / 2, SEND_W, ACT_H2};
}
inline TRect removeRect() {
  const TRect s = sendRect();
  return TRect{s.x - REM_W - 8, s.y, REM_W, ACT_H2};
}
}  // namespace setui

class SettingsScreen {
 public:
  void enter();  // called each time it is opened, to clear any armed button
  void render(ToolsHost& host, ToolsCanvas& c);
  // Returns true if the tap changed something and the screen needs repainting;
  // leaving is handled by the shell, which owns the back button.
  bool onTap(ToolsHost& host, int x, int y);
  // The shell offers the back tap here first. True means it was consumed going
  // up a page rather than out of settings altogether.
  bool back();

#ifdef TOYBOX_HOST
  void hostOpenLock() { _page = 1; }
  int hostPage() const { return _page; }
#endif

 private:
  void renderLock(ToolsHost& host, ToolsCanvas& c);
  bool tapLock(ToolsHost& host, int x, int y);

  // Erasing every score on the device deserves a second tap, not a second
  // screen: the button asks, and any other tap takes the question away.
  bool _armed = false;
  const char* _note = nullptr;
  uint8_t _page = 0;  // 0 = settings, 1 = lock screen
  lock::Config _lock;
};
