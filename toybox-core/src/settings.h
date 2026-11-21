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
  ACT_UPDATE,  // "Update & about": the version, updates over Wi-Fi, and reset stats
  ACT_CLOCK,
  ACT_FONT,
  ACT_VOLUME,  // v2: Low / Mid / High beside the Sound switch
  ACT_SETUP,   // v2: run the setup steps again
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
// v2 (kit "Settings"): three tabs, LOOK / CONNECT / DEVICE, under the bar,
// and the rows of the open tab under them. The enum order stays put so a
// caller aiming at a row by name never has to care which tab it sits in.
inline constexpr int TAB_LOOK = 0, TAB_CONNECT = 1, TAB_DEVICE = 2, TAB_COUNT = 3;
//                                      APPS WALL LOCK FILES SOUND UPDATE CLOCK FONT VOLUME SETUP
inline constexpr int TAB_OF[ACT_COUNT] = {2,   0,   0,   1,    2,    2,     1,    0,   2,     2};
inline constexpr int TAB_POS[ACT_COUNT] = {3,  0,   1,   0,    0,    4,     1,    2,   1,     2};
inline constexpr int TAB_Y = TOPBAR_H + 12, TAB_H = 48, TAB_GAP = 8;
inline constexpr int TAB_W = (SCREEN_W - 32 - 2 * TAB_GAP) / TAB_COUNT;
inline constexpr int TAB_ROWS_Y = TAB_Y + TAB_H + 14;
inline TRect tabRect(int t) { return TRect{16 + t * (TAB_W + TAB_GAP), TAB_Y, TAB_W, TAB_H}; }
inline int tabCount(int t) {
  int n = 0;
  for (int i = 0; i < ACT_COUNT; i++) n += TAB_OF[i] == t;
  return n;
}
inline TRect actionRect(int i) {
  return TRect{BTN_X, TAB_ROWS_Y + TAB_POS[i] * BTN_STEP, BTN_W, BTN_H};
}
// The volume row's three segments, right-aligned in the row.
inline constexpr int VOL_SEG_W = 70, VOL_SEG_H = 40;
inline TRect volSeg(int k) {
  const TRect r = actionRect(ACT_VOLUME);
  return TRect{r.x + r.w - 3 * VOL_SEG_W + k * VOL_SEG_W, r.y + (r.h - VOL_SEG_H) / 2, VOL_SEG_W,
               VOL_SEG_H};
}

// The setup steps (v2, kit "Setup"): font, sleep, beep, Wi-Fi. Big outlined
// rows, the chosen one filled, and Next at the foot. Language, "how to set
// up" and the home-screen style join when the firmware has them to offer.
inline constexpr int SETUP_STEPS = 4;
inline constexpr int SU_TITLE_Y = TOPBAR_H + 22, SU_ROW_Y = 168, SU_ROW_H = 76, SU_ROW_STEP = 88;
inline TRect setupRow(int i) { return TRect{16, SU_ROW_Y + i * SU_ROW_STEP, SCREEN_W - 32, SU_ROW_H}; }
inline TRect setupNext() { return TRect{16, SCREEN_H - 84, SCREEN_W - 32, 64}; }
inline TRect setupLater() { return TRect{16, SCREEN_H - 84, (SCREEN_W - 40) / 2, 64}; }
inline TRect setupDone() {
  return TRect{24 + (SCREEN_W - 40) / 2, SCREEN_H - 84, (SCREEN_W - 40) / 2, 64};
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
// Shown only once a home Wi-Fi and a time zone are saved: the clock without a
// phone. Above DONE, under the last line of the pairing text.
inline TRect clockNetRect() { return TRect{16, 620, SCREEN_W - 32, 60}; }

// The update page (Settings > Update & about). The version at the top, two ways
// to update, then "reset stats" -- which moved here from the main page to make
// room, and is still two taps. Result and progress screens put their buttons
// where every other page in settings does: DONE at the bottom, the one thing to
// do above it.
inline TRect updCheckRect() { return TRect{16, 288, SCREEN_W - 32, 60}; }
inline TRect updLanRect() { return TRect{16, 408, SCREEN_W - 32, 60}; }
inline TRect updResetRect() { return TRect{16, 588, SCREEN_W - 32, 60}; }
inline TRect updInstallRect() { return TRect{16, 620, SCREEN_W - 32, 60}; }
inline TRect updDoneRect() { return filesDoneRect(); }
inline constexpr int UPD_QR = 200, UPD_QR_X = (SCREEN_W - UPD_QR) / 2, UPD_QR_Y = 214;
inline constexpr int UPD_BAR_X = 48, UPD_BAR_Y = 340, UPD_BAR_W = SCREEN_W - 96, UPD_BAR_H = 28;

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
// v2 (kit "Lock screen"): the four faces as picture tiles, the picture's own
// row, SLEEP as one row of segments, WAKING as Wake to + Turn with the
// device. The footer toggles are gone -- no clock or temperature on a
// sleeping panel (they would go stale) -- so their rows sit off the glass.
inline constexpr int LOCK_ROW_Y[LR_COUNT] = {462, 92, 352, 576, 648, 900, 900, 900};
inline constexpr int LOCK_ROW_H[LR_COUNT] = {56, 250, 56, 56, 62, 0, 0, 0};
inline TRect lockRect(int i) { return TRect{LOCK_X, LOCK_ROW_Y[i], LOCK_W, LOCK_ROW_H[i]}; }

inline constexpr int CHIP_GAP = 6;
// The empty-panel tiles: two rows of three (v2, six faces).
inline constexpr int LOCK_TILE_COLS = 3, LOCK_TILE_H = 120, LOCK_TILE_GAP = 10;
inline TRect chipRect(int i) {
  const TRect r = lockRect(LR_EMPTY);
  const int w = (r.w - (LOCK_TILE_COLS - 1) * LOCK_TILE_GAP) / LOCK_TILE_COLS;
  return TRect{r.x + (i % LOCK_TILE_COLS) * (w + LOCK_TILE_GAP),
               r.y + (i / LOCK_TILE_COLS) * (LOCK_TILE_H + LOCK_TILE_GAP), w, LOCK_TILE_H};
}
// Sleep timing as one segmented control, right-aligned in its row, offered
// 1 / 5 / 15 / 30 / Never. Indexed by lock::SLEEP_MINUTES (0 = never), so
// "never" sits last on the glass and first in the table.
inline constexpr int SEG_H = 44, SEG_W = 52, SEG_NEVER_W = 84;
inline TRect sleepChipRect(int i) {
  const TRect r = lockRect(LR_SLEEP);
  const int x0 = r.x + r.w - (4 * SEG_W + SEG_NEVER_W);
  const int pos = i == 0 ? 4 : i - 1;
  return TRect{x0 + pos * SEG_W, r.y + (r.h - SEG_H) / 2, pos == 4 ? SEG_NEVER_W : SEG_W, SEG_H};
}
// Where the power button wakes to: the note, or home.
inline constexpr int WAKE_SEG_W = 100;
inline TRect wakeChipRect(int i) {
  const TRect r = lockRect(LR_WAKE);
  const int x0 = r.x + r.w - 2 * WAKE_SEG_W;
  return TRect{x0 + i * WAKE_SEG_W, r.y + (r.h - SEG_H) / 2, WAKE_SEG_W, SEG_H};
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
  bool wantsTick() const { return _page == 4 || _page == 6 || _page == 9; }

  // The setup steps, from the first boot of a new device (fromBoot: the last
  // step and the back corner on the first one lead to the hub) or from
  // Settings > DEVICE > Setup (they lead back to settings).
  void startSetup(bool fromBoot);
  // True once a boot-started setup has been finished or left: the shell
  // goes to the hub instead of the settings root.
  bool setupFinished() {
    const bool f = _setupLeft;
    _setupLeft = false;
    return f;
  }
  bool inSetup() const { return _page >= 10 && _page < 10 + setui::SETUP_STEPS; }
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
  uint8_t hostNetStage() const { return _netStage; }
  uint8_t hostUpStage() const { return _upStage; }
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
  bool netReady(ToolsHost& host);
  void leaveClock();
  void renderUpdate(ToolsHost& host, ToolsCanvas& c);
  bool tapUpdate(ToolsHost& host, int x, int y);
  bool tickUpdate(ToolsHost& host);
  void leaveUpdate(ToolsHost* host);
  static void onProgress(void* ctx, uint8_t stage, uint32_t done, uint32_t total);
  void renderFontWho(ToolsHost& host, ToolsCanvas& c);
  bool tapFontWho(ToolsHost& host, int x, int y);
  void renderFont(ToolsHost& host, ToolsCanvas& c);
  bool tapFont(ToolsHost& host, int x, int y);
  void enterFont(ToolsHost& host);

  // Erasing every score on the device deserves a second tap, not a second
  // screen: the button asks, and any other tap takes the question away.
  bool _armed = false;
  const char* _note = nullptr;
  // The PAGE chip's note is composed rather than picked from a table: it
  // states how far back the page goes, which the chip itself cannot say.
  char _pageNote[96] = {};
  char _coverNote[96] = {};
  // 0 = settings, 1 = lock, 2 = wallpaper, 3 = apps, 4 = files,
  // 5 = the lock screen's picture, off the card, 6 = the clock,
  // 7 = which text to dress, 8 = the families to dress it in,
  // 9 = update & about, 10..13 = the setup steps
  uint8_t _page = 0;
  uint8_t _tab = 0;  // which settings tab is open (v2)
  bool _setupBoot = false;  // the setup steps were started by a new device
  bool _setupLeft = false;  // ...and have just been left: go to the hub
  bool _setupWifi = false;  // the clock page was opened from setup's Wi-Fi step
  void renderSetup(ToolsHost& host, ToolsCanvas& c);
  bool tapSetup(ToolsHost& host, int x, int y);
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
  // The clock from the internet: 0 = not asked, 1 = asked (the "connecting"
  // screen is up; the next tick does the blocking work), 2 = answered.
  uint8_t _netStage = 0;
  homenet::Result _netResult = homenet::Result::Ok;
  char _netName[homenet::SSID_MAX] = {};
  // The update page: 0 = the page, 1 = checking (the next tick does the
  // blocking work), 2 = what the check found, 3 = installing (the tick blocks,
  // repainting the bar as bytes land), 4 = installed or not, 5 = the computer
  // page, 6 = joining the home Wi-Fi for it (the next tick).
  uint8_t _upStage = 0;
  ota::Err _upErr = ota::Err::Ok;
  ota::Check _upCheck;
  uint8_t _upPart = 0;  // which file the bar is filling: 0 app, 1 fonts
  uint32_t _upDone = 0, _upTotal = 0, _upPainted = 0;
  uint8_t _lanSeen = 0xFF;  // the computer page's phase at the last repaint
  uint32_t _lanPainted = 0;
  char _upRolled[ota::VER_MAX] = {};  // a version that would not start, once
  ToolsHost* _upHost = nullptr;       // for the progress callback
};
