// Device settings: which apps the hub shows, sound, and two things you can put
// back the way they were.
#pragma once
#include "chrome.h"
#include "tools/files_web.h"
#include "tools/lockscreen.h"

// Geometry the preview harness aims at. Remembered coordinates go stale the
// moment a row moves; these do not.
namespace setui {
// The main page is buttons and nothing else -- the app checkboxes moved to
// their own page, which gave everything back its breathing room: 62 px
// buttons at a 76 px step, comfortably over the 7 mm a fingertip wants and
// still clear of the footer with seven of them. The four that end in "..."
// open pages; the other three act right here.
inline constexpr int BTN_X = 16, BTN_W = SCREEN_W - 32, BTN_H = 62, BTN_STEP = 76;
inline constexpr int BTN_Y0 = 116;
inline TRect actionRect(int i) { return TRect{BTN_X, BTN_Y0 + i * BTN_STEP, BTN_W, BTN_H}; }
enum Action : int {
  ACT_APPS,
  ACT_WALL,
  ACT_LOCK,
  ACT_FILES,
  ACT_SOUND,
  ACT_CARDS,
  ACT_RESET,
  ACT_COUNT
};

// The files-over-wifi page. Two pairing steps, then a summary of what the
// phone did -- the file list itself lives on the phone, which is where the
// person is looking, and is the only place it can be shown while the card
// holds the display's bus.
inline constexpr int FILES_QR = 240, FILES_QR_X = (SCREEN_W - FILES_QR) / 2, FILES_QR_Y = 150;
inline TRect filesDoneRect() { return TRect{16, 700, SCREEN_W - 32, 60}; }

// The wallpaper page: what is on the device now, then what the card offers.
inline constexpr int WALL_MAX = 8;
inline constexpr int WALL_Y0 = 172, WALL_ROW_H = 56, WALL_ROW_STEP = 62;
inline TRect wallRect(int i) { return TRect{16, WALL_Y0 + i * WALL_ROW_STEP, SCREEN_W - 32, WALL_ROW_H}; }
inline TRect wallRemoveRect() { return TRect{16, 84, SCREEN_W - 32, 50}; }

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

// The picture row's actions, right-aligned: choose one from the card, and
// throw the stored one away. REMOVE only exists when there is something to
// remove.
inline constexpr int SEND_W = 190, ACT_H2 = 46, REM_W = 46;
inline TRect sendRect() {
  const TRect r = lockRect(LR_PICTURE);
  return TRect{r.x + r.w - SEND_W, r.y + (r.h - ACT_H2) / 2, SEND_W, ACT_H2};
}
inline TRect removeRect() {
  const TRect s = sendRect();
  return TRect{s.x - REM_W - 8, s.y, REM_W, ACT_H2};
}

// The lock picture page: the same list as the wallpaper page, writing to the
// other file. The phone route lives on as one small line at the foot of it --
// the only way in for a device with nothing in the card slot, and no louder
// than that case deserves.
inline TRect lockPhoneRect() { return TRect{16, 726, SCREEN_W - 32, 40}; }
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
  // True while on the one page whose leaving can release the card's bus (and
  // with it re-initialise the panel). The shell asks before calling back(), so
  // it knows whether the repaint after it must be a full one.
  bool onBusPage() const { return _page == 4; }
  // Leaving settings altogether, however that happens: the files page owns a
  // running access point and a possibly-claimed SD bus, and neither may
  // outlive the screen that started them.
  void leave();

  // The files page runs a web server, so settings needs loop time -- the only
  // page here that does. Returns true when the screen wants repainting after
  // the tick (the card was let go and the summary has changed).
  bool wantsTick() const { return _page == 4; }
  bool tick(ToolsHost& host);

#ifdef TOYBOX_HOST
  void hostOpenLock() { _page = 1; }
  void hostOpenLockPic(ToolsHost& host) {
    enterWall(host);
    _page = 5;
  }
  int hostPage() const { return _page; }
  fweb::FilesServer& hostFiles() { return _files; }
#endif

 private:
  void renderLock(ToolsHost& host, ToolsCanvas& c);
  bool tapLock(ToolsHost& host, int x, int y);
  void renderWall(ToolsHost& host, ToolsCanvas& c);
  bool tapWall(ToolsHost& host, int x, int y);
  void enterWall(ToolsHost& host);
  void renderLockPic(ToolsHost& host, ToolsCanvas& c);
  bool tapLockPic(ToolsHost& host, int x, int y);
  void renderApps(ToolsHost& host, ToolsCanvas& c);
  bool tapApps(ToolsHost& host, int x, int y);
  void renderFiles(ToolsHost& host, ToolsCanvas& c);
  bool tapFiles(ToolsHost& host, int x, int y);
  void leaveFiles();

  // Erasing every score on the device deserves a second tap, not a second
  // screen: the button asks, and any other tap takes the question away.
  bool _armed = false;
  const char* _note = nullptr;
  char _coverNote[96] = {};
  // 0 = settings, 1 = lock, 2 = wallpaper, 3 = apps, 4 = files,
  // 5 = the lock screen's picture, off the card
  uint8_t _page = 0;
  lock::Config _lock;
  // The card's offerings, read once on entering the page: the card is powered
  // per call, and re-listing on every repaint would strobe it. Pages 2 and 5
  // are never open at the same time, so they share the one buffer.
  int8_t _wallN = -1;
  char _wallNames[setui::WALL_MAX][ToolsHost::SD_NAME_LEN] = {};
  // The files page's access point. Held by value, started on entering the
  // page and stopped on every way out of it.
  fweb::FilesServer _files;
  bool _filesOk = false;
  bool _filesSawClient = false;
};
