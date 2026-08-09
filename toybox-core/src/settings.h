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

// The lock screen page: one row per setting, each cycling through its choices.
// 80 rather than 88 because the picture row made eight of them; at 88 the last
// one would have sat under the caption along the bottom.
inline constexpr int LOCK_Y0 = 92, LOCK_H = 80, LOCK_X = 16, LOCK_W = SCREEN_W - 32;
inline TRect lockRect(int i) { return TRect{LOCK_X, LOCK_Y0 + i * LOCK_H, LOCK_W, LOCK_H - 8}; }
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
// The picture row carries a thumbnail and its own delete key, so it is the one
// row with parts rather than a single tap target. The thumbnail keeps the
// panel's 3:5 proportion; anything else would misrepresent the picture.
inline constexpr int THUMB_H = LOCK_H - 20;
inline constexpr int THUMB_W = THUMB_H * 3 / 5;
inline TRect thumbRect() {
  const TRect r = lockRect(LR_PICTURE);
  return TRect{r.x + r.w - THUMB_W - 4, r.y + (r.h - THUMB_H) / 2, THUMB_W, THUMB_H};
}
inline TRect picDelRect() {
  const TRect t = thumbRect();
  return TRect{t.x - 52, t.y + (t.h - 40) / 2, 40, 40};
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
