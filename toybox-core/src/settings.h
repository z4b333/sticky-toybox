// Device settings: which apps the hub shows, sound, and two things you can put
// back the way they were.
#pragma once
#include "chrome.h"
#include "tools/clock_web.h"
#include "tools/files_web.h"
#include "tools/lockscreen.h"

// Geometry the preview harness aims at. Remembered coordinates go stale the
// moment a row moves; these do not.
namespace setui {
// The main page: grouped rows under small tracked headings, an icon on the
// left of each, and the CURRENT VALUE on the right -- the page answers at a
// glance instead of hiding every state behind a tap. Rows that open a page
// carry a chevron; rows that act in place do not. 60 px rows at a 68 px
// step, over the 7 mm a fingertip wants.
// ROOT_HEAD was 36 while there were seven rows. The eighth (the clock) needs
// the height back, and a heading's own ink is 28 px, so the four that came out
// of the four gaps are four the page was not using. The rows themselves keep
// their 60 over a 68 step -- the thing a thumb actually aims at is untouched.
inline constexpr int BTN_X = 16, BTN_W = SCREEN_W - 32, BTN_H = 60, BTN_STEP = 68;
inline constexpr int ROOT_Y0 = 64, ROOT_HEAD = 32;
enum Action : int {
  ACT_APPS,
  ACT_WALL,
  ACT_LOCK,
  ACT_FILES,
  ACT_SOUND,
  ACT_RESET,
  ACT_CLOCK,
  ACT_FONT,
  ACT_COUNT
};
// Visual order groups the rows by what they are about (LOOK first -- the
// rows people come for); the enum order stays put so nothing that aims at a
// row by name has to care where it sits. The clock sits under Files over
// WiFi, because both are the same sentence to the person doing them: get the
// phone out; the font sits under the wallpaper and the lock screen, because
// all three are what the device looks like.
//
// The page holds eight rows and no more. "Show the how-to cards again" left
// it for the apps page, which is where the cards belong: it is a row about
// apps, and it was sitting in EXTRAS because that is where rows go when
// nobody asks where they belong.
inline constexpr int ROOT_POS[ACT_COUNT] = {3, 0, 1, 4, 6, 7, 5, 2};
// How many group headings sit above each visual row.
inline constexpr int ROOT_HEADS[ACT_COUNT] = {1, 1, 1, 2, 3, 3, 4, 5};
inline TRect actionRect(int i) {
  const int r = ROOT_POS[i];
  return TRect{BTN_X, ROOT_Y0 + r * BTN_STEP + ROOT_HEADS[r] * ROOT_HEAD, BTN_W, BTN_H};
}

// The files-over-wifi page. Two pairing steps, then a summary of what the
// phone did -- the file list itself lives on the phone, which is where the
// person is looking, and is the only place it can be shown while the card
// holds the display's bus.
inline constexpr int FILES_QR = 240, FILES_QR_X = (SCREEN_W - FILES_QR) / 2, FILES_QR_Y = 150;
inline TRect filesDoneRect() { return TRect{16, 700, SCREEN_W - 32, 60}; }

// The font page: the families the card offers, one per row, with the one in
// use wearing a tick. The first row is the firmware's own face, so "put it
// back" is a row rather than a separate button -- the same shape as choosing
// any other one.
// The first font page: which text is being dressed. The device's own, then
// one row per app that shows the owner's words.
inline constexpr int FONT_WHO_Y0 = 120, FONT_WHO_H = 76, FONT_WHO_STEP = 84;
inline TRect fontWhoRect(int i) {
  return TRect{16, FONT_WHO_Y0 + i * FONT_WHO_STEP, SCREEN_W - 32, FONT_WHO_H};
}

inline constexpr int FONT_Y0 = 128, FONT_ROW_H = 56, FONT_ROW_STEP = 62, FONT_PER = 8;
inline constexpr int FONT_MAX = 24;
inline TRect fontRect(int i) {
  return TRect{16, FONT_Y0 + i * FONT_ROW_STEP, SCREEN_W - 32, FONT_ROW_H};
}
inline int fontPagerY() { return FONT_Y0 + FONT_PER * FONT_ROW_STEP + 6; }
inline TRect fontPagerPrev() { return TRect{16, fontPagerY(), 130, 44}; }
inline TRect fontPagerNext() { return TRect{SCREEN_W - 16 - 130, fontPagerY(), 130, 44}; }

// The how-to cards, on the apps page now.
inline TRect appsCardsRect() { return TRect{16, 700, SCREEN_W - 32, 56}; }

// The clock page: the same two pairing steps as the files page, at the same
// coordinates, because it is the same act. What differs is the end -- the
// phone sends one number by itself, and the device answers with the time it
// now believes, in digits big enough to check from arm's length.
inline constexpr int CLOCK_BIG_Y = 560;
inline TRect clockDoneRect() { return filesDoneRect(); }

// The wallpaper and lock-picture pages: what is on the device now -- shown
// as the picture itself, a 1:5 miniature, beside its name and its REMOVE --
// then what the card offers, with the chosen row wearing a tick. The old
// layout answered "which one is it?" with nothing at all, and sat its
// remove button on top of the card list's heading.
// Up to 24 names remembered; six rows to a page on the wallpaper page, five
// on the lock page (the phone line needs its seat), with < PREV / NEXT >
// under the rows once there is more than one page.
inline constexpr int WALL_MAX = 24;
inline constexpr int WALL_PER = 6, LOCK_PER = 5;
inline constexpr int CUR_HEAD_Y = 56;                      // "CURRENT" + rule
inline constexpr int CUR_Y = 92;                           // the preview block
inline constexpr int CUR_PV_W = 96, CUR_PV_H = 160;        // 480x800 at 1:5
inline TRect wallPreviewRect() { return TRect{16, CUR_Y, CUR_PV_W, CUR_PV_H}; }
inline TRect wallRemoveRect() { return TRect{SCREEN_W - 16 - 140, CUR_Y + CUR_PV_H - 48, 140, 48}; }
inline constexpr int WALL_Y0 = 316, WALL_ROW_H = 56, WALL_ROW_STEP = 62;
inline TRect wallRect(int i) { return TRect{16, WALL_Y0 + i * WALL_ROW_STEP, SCREEN_W - 32, WALL_ROW_H}; }
// The pager sits where the rows stop: after six on the wallpaper page, after
// five on the lock page.
inline int wallPagerY(bool lockPage) { return WALL_Y0 + (lockPage ? LOCK_PER : WALL_PER) * WALL_ROW_STEP + 6; }
inline TRect wallPagerPrev(bool lockPage) { return TRect{16, wallPagerY(lockPage), 130, 44}; }
inline TRect wallPagerNext(bool lockPage) { return TRect{SCREEN_W - 16 - 130, wallPagerY(lockPage), 130, 44}; }

// The lock screen page, in four sections a person can name -- WHEN IT
// SLEEPS, WHAT IT SHOWS, THE FOOTER LINE, WAKING -- instead of one column of
// eight unlike rows. Everything choice-of-N is a chip row with the current
// one filled (sleep timing and wake target included: a value that cycles on
// a tap hides its other options); everything on/off is a checkbox.
inline constexpr int LOCK_X = 16, LOCK_W = SCREEN_W - 32;
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
// Explicit positions, because the sections interleave the enum's order.
inline constexpr int LOCK_ROW_Y[LR_COUNT] = {100, 198, 252, 564, 614, 360, 410, 460};
inline constexpr int LOCK_ROW_H[LR_COUNT] = {46, 46, 56, 46, 62, 44, 44, 44};
inline TRect lockRect(int i) { return TRect{LOCK_X, LOCK_ROW_Y[i], LOCK_W, LOCK_ROW_H[i]}; }

inline constexpr int CHIP_GAP = 6;
// The empty-panel chips, over their row.
inline TRect chipRect(int i) {
  const TRect r = lockRect(LR_EMPTY);
  const int w = (r.w - (lock::EMPTY_COUNT - 1) * CHIP_GAP) / lock::EMPTY_COUNT;
  return TRect{r.x + i * (w + CHIP_GAP), r.y, w, r.h};
}
// Sleep timing as five chips: never, 1, 5, 15, 30 minutes.
inline TRect sleepChipRect(int i) {
  const TRect r = lockRect(LR_SLEEP);
  const int w = (r.w - (lock::SLEEP_COUNT - 1) * CHIP_GAP) / lock::SLEEP_COUNT;
  return TRect{r.x + i * (w + CHIP_GAP), r.y, w, r.h};
}
// Where the power button wakes to: the note, or the hub.
inline TRect wakeChipRect(int i) {
  const TRect r = lockRect(LR_WAKE);
  const int w = (r.w - CHIP_GAP) / 2;
  return TRect{r.x + i * (w + CHIP_GAP), r.y, w, r.h};
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
  bool wantsTick() const { return _page == 4 || _page == 6; }
  bool tick(ToolsHost& host);

#ifdef TOYBOX_HOST
  void hostOpenLock() { _page = 1; }
  void hostOpenLockPic(ToolsHost& host) {
    enterWall(host);
    _page = 5;
  }
  int hostPage() const { return _page; }
  fweb::FilesServer& hostFiles() { return _files; }
  cweb::ClockServer& hostClock() { return _clock; }
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
  void renderClock(ToolsHost& host, ToolsCanvas& c);
  bool tapClock(ToolsHost& host, int x, int y);
  void leaveClock();
  void renderFontWho(ToolsHost& host, ToolsCanvas& c);
  bool tapFontWho(ToolsHost& host, int x, int y);
  void renderFont(ToolsHost& host, ToolsCanvas& c);
  bool tapFont(ToolsHost& host, int x, int y);
  void enterFont(ToolsHost& host);

  // Erasing every score on the device deserves a second tap, not a second
  // screen: the button asks, and any other tap takes the question away.
  bool _armed = false;
  const char* _note = nullptr;
  char _coverNote[96] = {};
  // 0 = settings, 1 = lock, 2 = wallpaper, 3 = apps, 4 = files,
  // 5 = the lock screen's picture, off the card, 6 = the clock,
  // 7 = which text to dress, 8 = the families to dress it in
  uint8_t _page = 0;
  lock::Config _lock;
  // The card's offerings, read once on entering the page: the card is powered
  // per call, and re-listing on every repaint would strobe it. Pages 2 and 5
  // are never open at the same time, so they share the one buffer.
  int8_t _wallN = -1;
  int8_t _wallPage = 0;  // which page of card pictures is up
  char _wallNames[setui::WALL_MAX][ToolsHost::SD_NAME_LEN] = {};
  // The files page's access point. Held by value, started on entering the
  // page and stopped on every way out of it.
  fweb::FilesServer _files;
  bool _filesOk = false;
  bool _filesSawClient = false;
  // The clock page's access point, on the same terms. Two access points are
  // never up at once: both pages are reached from the root and every way out
  // of one goes through its own stop().
  // The card's font families, read once on entering the page: reading them
  // claims the card's bus and takes the panel with it, and doing that on every
  // repaint would strobe the screen.
  char _fontNames[setui::FONT_MAX][32] = {};
  int8_t _fontN = -1;
  int8_t _fontPage = 0;
  // Which of the five faces the list is choosing for.
  int8_t _fontSlot = 0;
  cweb::ClockServer _clock;
  bool _clockOk = false;
  bool _clockSawClient = false;
  bool _clockSawSet = false;
};
