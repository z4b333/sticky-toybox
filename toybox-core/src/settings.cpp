#include "settings.h"

#include "applist.h"
#include "appvis.h"
#include "record.h"
#include "tools/book_thumbs.h"
#include "tools/flash_qr.h"
#include "tools/lock_image.h"
#include "tools/recents.h"
#include "tools/tool_icons.h"

namespace {

// Two columns of checkboxes, on their own page now, so the rows got their
// height back: 52 px is 5.6 mm -- still under the 7 mm a fingertip wants --
// but the row is 216 px wide and a miss only ticks a neighbouring box, which
// is visible immediately and undone by tapping it again.
constexpr int COL_X[2] = {16, 248};
constexpr int COL_W = 216;
constexpr int ROW_H = 52, BOX = 28;
constexpr int HEAD_H = 26, GROUP_GAP = 14;
constexpr int LIST_TOP = 92;


// Group 0 fills the left column and everything after it stacks down the right.
// Three groups is what there is; a fourth would go on the right too and want
// the row height trimmed.
int columnOf(int g) { return g == 0 ? 0 : 1; }

//   f(item, group, headTop, rowTop, col)
template <typename F>
void walkList(F f) {
  int y[2] = {LIST_TOP, LIST_TOP};
  for (int g = 0; g < applist::NGROUPS; g++) {
    const applist::Group& grp = applist::GROUPS[g];
    const int col = columnOf(g);
    const int headTop = y[col];
    for (int i = 0; i < grp.n; i++)
      f(grp.items[i], grp, headTop, headTop + HEAD_H + i * ROW_H, col);
    y[col] = headTop + HEAD_H + grp.n * ROW_H + GROUP_GAP;
  }
}

void checkbox(ToolsCanvas& c, int x, int y, bool on) {
  if (!on) {
    c.drawRect(x, y, BOX, BOX, 2, true);
    return;
  }
  c.fillRect(x, y, BOX, BOX, true);
  // A tick in the knocked-out white, so a ticked box reads as filled from
  // across the room and as a tick up close.
  c.drawLine(x + 6, y + 14, x + 12, y + 20, 3, false);
  c.drawLine(x + 12, y + 20, x + 22, y + 8, 3, false);
}

// A host with only a switch reports two levels and gets the two words it can
// mean; one with a real volume gets four.
const char* soundLabel(const ToolsHost& host) {
  if (host.soundLevels() <= 2) return host.soundOn() ? "SOUND: ON" : "SOUND: OFF";
  switch (host.soundLevel()) {
    case 0: return "SOUND: MUTE";
    case 1: return "SOUND: LOW";
    case 2: return "SOUND: MEDIUM";
    default: return "SOUND: HIGH";
  }
}

const char* nameOf(const applist::Item& it) {
  return it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx];
}

// Must match the names the games pass to help::suppressed().
constexpr const char* HELP_KEYS[] = {"h_wrd", "h_non", "h_g20", "h_xo", "h_sea", "h_sud"};

}  // namespace

void SettingsScreen::enter() {
  _armed = false;
  _note = nullptr;
  _page = 0;
}

bool SettingsScreen::back() {
  if (_page == 0) return false;
  leaveFiles();  // no-op unless the files page was the one being left
  _page = 0;
  _note = nullptr;
  return true;
}

// Both ways out of the files page go through here: the access point comes
// down, and with it any claim the session still had on the card.
void SettingsScreen::leaveFiles() {
  if (!_filesOk) return;
  _files.stop();
  _filesOk = false;
  _filesSawClient = false;
}

void SettingsScreen::leave() { leaveFiles(); }

bool SettingsScreen::tick(ToolsHost& host) {
  if (_page != 4 || !_filesOk) return false;
  _files.loop();
  // Two things ask for a repaint: the first phone joining (step one becomes
  // step two), and the card being let go after a burst of work (the summary
  // can finally be drawn). Both are impossible to draw any earlier -- the
  // second because the panel's bus was busy.
  if (_files.dirty()) {
    _files.clearDirty();
    return true;
  }
  if (!_filesSawClient && _files.hasClient()) {
    _filesSawClient = true;
    return true;
  }
  return false;
}

// --- the wallpaper page -------------------------------------------------------
// What is behind the home screen, and what the SD card offers to put there.
// The list is .tbi files -- pre-converted on a PC with tools/make_tbi.py --
// and tapping one copies it into the device, so the card can come back out.
void SettingsScreen::enterWall(ToolsHost& host) {
  _wallN = (int8_t)host.sdWallpapers(_wallNames, setui::WALL_MAX);
}

void SettingsScreen::renderWall(ToolsHost& host, ToolsCanvas& c) {
  (void)host;
  using namespace setui;
  drawTopBar(c, "WALLPAPER");

  const TRect rm = wallRemoveRect();
  if (wallimg::have()) {
    c.button(rm.x, rm.y, rm.w, rm.h, "REMOVE THE CURRENT ONE", false, TS_MED);
  } else {
    c.text(rm.x + 4, rm.y + 12, "none set - the home screen is plain", TS_MED, true);
  }

  c.textTracked(16, WALL_Y0 - 40, "ON THE SD CARD", TS_MED, true, false, 1);
  c.fillRect(16, WALL_Y0 - 14, SCREEN_W - 32, 1, true);

  if (_wallN < 0) {
    c.textCentered(SCREEN_W / 2, 320, "no card found", TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, 364, "is one in the slot?", TS_MED, true);
  } else if (_wallN == 0) {
    c.textCentered(SCREEN_W / 2, 320, "no wallpapers on the card", TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, 364, "make .tbi files with tools/make_tbi.py", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 392, "and put them in /wallpapers", TS_SMALL, true);
  } else {
    for (int i = 0; i < _wallN; i++) {
      const TRect r = wallRect(i);
      c.button(r.x, r.y, r.w, r.h, _wallNames[i], false, TS_MED);
    }
  }

  c.textCentered(SCREEN_W / 2, 776,
                 _note ? _note : "a chosen picture is copied in, so the card can come out",
                 TS_SMALL, true);
}

bool SettingsScreen::tapWall(ToolsHost& host, int x, int y) {
  using namespace setui;
  _note = nullptr;
  if (wallimg::have() && wallRemoveRect().hit(x, y)) {
    wallimg::remove();
    _note = "removed - the home screen is plain again";
    host.beep(2);
    return true;
  }
  for (int i = 0; i < _wallN; i++) {
    if (!wallRect(i).hit(x, y)) continue;
    if (host.sdWallpaperTake(_wallNames[i])) {
      _note = "wallpaper set";
      host.beep(1);
    } else {
      _note = "could not read it from the card";
      host.beep(2);
    }
    // The card had the bus and the panel was re-initialised on the way out, so
    // a differential repaint would difference against nothing. Refresh in full
    // here and tell the shell not to repaint again.
    host.refresh(true);
    return false;
  }
  return false;
}

// --- the files page -----------------------------------------------------------
// Pair, then get out of the way. The file list is on the phone because that is
// where the person is looking -- and because while the card is being written
// to, this panel physically cannot be redrawn.
void SettingsScreen::renderFiles(ToolsHost& host, ToolsCanvas& c) {
  (void)host;
  using namespace setui;
  drawTopBar(c, "FILES");
  char buf[72];

  if (!_filesOk) {
    c.textCentered(SCREEN_W / 2, 320, "could not start wifi", TS_LARGE, true, true);
    c.button(filesDoneRect().x, filesDoneRect().y, filesDoneRect().w, filesDoneRect().h, "BACK",
             true, TS_LARGE);
    return;
  }

  if (!_files.hasClient()) {
    c.textCentered(SCREEN_W / 2, 64, "STEP 1 OF 2", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 96, "join the device's wifi", TS_MED, true);
    const String wifi = _files.wifiPayload();
    fqr::draw(c, FILES_QR_X, FILES_QR_Y, FILES_QR, wifi.c_str());
    c.textCentered(SCREEN_W / 2, 420, "Scan with your phone camera", TS_MED, true, true);
    snprintf(buf, sizeof(buf), "%s   key %s", _files.ssid(), _files.password());
    c.textCentered(SCREEN_W / 2, 456, buf, TS_MED, true);
    c.textCentered(SCREEN_W / 2, 492, "this code joins the wifi, nothing more", TS_SMALL, true);
  } else {
    c.textCentered(SCREEN_W / 2, 64, "STEP 2 OF 2", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 96, "phone joined", TS_MED, true);
    fqr::draw(c, FILES_QR_X, FILES_QR_Y, FILES_QR, _files.url());
    c.textCentered(SCREEN_W / 2, 420, "The file list should have opened.", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 448, "If not, scan this or type it in:", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 484, _files.url(), TS_MED, true, true);
  }

  // What the phone has done so far. Written after the fact, because while it
  // was happening the card had the wire this screen draws on.
  const fweb::Counts& n = _files.counts();
  if (_files.touched()) {
    c.fillRect(16, 546, SCREEN_W - 32, 1, true);
    int y = 562;
    if (n.added) {
      snprintf(buf, sizeof(buf), "%u added   %u MB", (unsigned)n.added,
               (unsigned)(n.bytes / 1024));
      c.textCentered(SCREEN_W / 2, y, buf, TS_MED, true, true);
      y += 32;
    }
    if (n.removed) {
      snprintf(buf, sizeof(buf), "%u deleted", (unsigned)n.removed);
      c.textCentered(SCREEN_W / 2, y, buf, TS_MED, true);
      y += 32;
    }
    if (n.renamed) {
      snprintf(buf, sizeof(buf), "%u renamed", (unsigned)n.renamed);
      c.textCentered(SCREEN_W / 2, y, buf, TS_MED, true);
    }
  } else {
    c.textCentered(SCREEN_W / 2, 578, "nothing sent yet", TS_SMALL, true);
  }

  c.textCentered(SCREEN_W / 2, 664, "the screen waits while files are moving", TS_SMALL, true);
  const TRect d = filesDoneRect();
  c.button(d.x, d.y, d.w, d.h, "DONE", false, TS_LARGE);
}

bool SettingsScreen::tapFiles(ToolsHost& host, int x, int y) {
  if (setui::filesDoneRect().hit(x, y)) {
    leaveFiles();
    _page = 0;
    host.beep(1);
    // The card may have been claimed a moment ago, so the panel starts again
    // from nothing rather than from a difference.
    host.refresh(true);
    return false;
  }
  return false;
}

void SettingsScreen::render(ToolsHost& host, ToolsCanvas& c) {
  if (_page == 1) {
    renderLock(host, c);
    return;
  }
  if (_page == 2) {
    renderWall(host, c);
    return;
  }
  if (_page == 4) {
    renderFiles(host, c);
    return;
  }
  if (_page == 3) {
    renderApps(host, c);
    return;
  }
  drawTopBar(c, "SETTINGS");

  using namespace setui;
  // The pages first, then the things that act right here.
  const TRect sa = actionRect(ACT_APPS), sw = actionRect(ACT_WALL);
  const TRect s1 = actionRect(ACT_LOCK), s0 = actionRect(ACT_SOUND);
  const TRect sf = actionRect(ACT_FILES);
  const TRect s2 = actionRect(ACT_CARDS), s3 = actionRect(ACT_RESET);
  c.button(sa.x, sa.y, sa.w, sa.h, "APPS ON THE HUB...", false, TS_MED);
  c.button(sw.x, sw.y, sw.w, sw.h, "WALLPAPER...", false, TS_MED);
  c.button(s1.x, s1.y, s1.w, s1.h, "LOCK SCREEN...", false, TS_MED);
  c.button(sf.x, sf.y, sf.w, sf.h, "FILES OVER WIFI...", false, TS_MED);
  c.button(s0.x, s0.y, s0.w, s0.h, soundLabel(host), false, TS_MED);
  c.button(s2.x, s2.y, s2.w, s2.h, "SHOW HOW TO PLAY AGAIN", false, TS_MED);
  c.button(s3.x, s3.y, s3.w, s3.h,
           _armed ? "TAP AGAIN TO ERASE SCORES" : "RESET STATS AND TALLIES", _armed, TS_MED);

  c.textCentered(SCREEN_W / 2, 776, _note ? _note : "rows ending in ... open a page of their own",
                 TS_SMALL, true);
}

// --- the apps page ------------------------------------------------------------
// Which apps the hub's folders show, as the checkbox list the main settings
// page used to carry. On its own page both it and the buttons stopped elbowing
// each other for height.
void SettingsScreen::renderApps(ToolsHost& host, ToolsCanvas& c) {
  (void)host;
  drawTopBar(c, "APPS");

  c.textTracked(16, 56, "SHOW ON THE HUB", TS_MED, true, false, 1);

  walkList([&c](const applist::Item& it, const applist::Group& grp, int headTop, int rowTop,
                int col) {
    const int x = COL_X[col];
    if (rowTop == headTop + HEAD_H) {
      // Same weight and rule as the hub's group headings, so the checkbox list
      // reads as the hub in another form rather than as a separate inventory.
      c.textTracked(x, headTop, grp.name, TS_MED, true, false, 1);
      c.fillRect(x, headTop + 20, COL_W, 1, true);
    }
    checkbox(c, x, rowTop + (ROW_H - BOX) / 2, appvis::visible(it.game, it.idx));
    c.text(x + BOX + 12, rowTop + (ROW_H - c.textHeight(TS_MED)) / 2, nameOf(it), TS_MED, true);
  });

  c.textCentered(SCREEN_W / 2, 776, "hiding an app keeps everything saved in it", TS_SMALL, true);
}

bool SettingsScreen::tapApps(ToolsHost& host, int x, int y) {
  bool hit = false;
  walkList([&](const applist::Item& it, const applist::Group&, int, int rowTop, int col) {
    if (hit || !inRect(x, y, COL_X[col], rowTop, COL_W, ROW_H)) return;
    hit = true;
    appvis::toggle(it.game, it.idx);
  });
  if (!hit) return false;
  appvis::save(host.prefs());
  host.beep(0);
  return true;
}

// --- the lock screen page -----------------------------------------------------
// One row per setting, each showing what it is set to and cycling on a tap.
// Seven rows of two-state and three-state choices do not want seven different
// controls; a row that names the thing and states the answer is enough, and it
// reads as a list of decisions rather than a panel of switches.
namespace {
const char* emptyLabel(uint8_t e) {
  switch (e) {
    case lock::EMPTY_PICTURE: return "PICTURE";
    case lock::EMPTY_GOODBYE: return "GOODBYE";
    case lock::EMPTY_COVER: return "COVER";
    default: return "BLANK";
  }
}
const char* rowName(int i) {
  switch (i) {
    case setui::LR_SLEEP: return "SLEEP AFTER";
    case setui::LR_EMPTY: return "WITH NO NOTE PINNED";
    case setui::LR_PICTURE: return "THE PICTURE";
    case setui::LR_WAKE: return "WAKE TO";
    case setui::LR_ROTATE: return "TURN WITH THE DEVICE";
    case setui::LR_TIME: return "SHOW THE TIME";
    case setui::LR_TEMP: return "SHOW THE TEMPERATURE";
    default: return "SHOW THE BATTERY";
  }
}
const char* rowHint(int i) {
  switch (i) {
    case setui::LR_SLEEP: return "the panel keeps its image with the power off";
    case setui::LR_EMPTY: return "what is on the panel when nothing is pinned";
    case setui::LR_PICTURE:
      // Short, because the buttons start where a longer line would run on.
      // What the picture is for is said by the chip above it.
      return lockimg::have() ? "one is stored" : "none sent yet";
    case setui::LR_WAKE: return "where the power button takes you";
    case setui::LR_ROTATE:
      // When it is off, say what happens instead -- otherwise the note simply
      // stops turning and it looks like the angle came from nowhere.
      return lock::config().autoRotate ? "only the pinned note turns; apps stay portrait"
                                       : "the note rests at the angle it was pinned at";
    default: return nullptr;
  }
}
// Only for the rows that carry a value. The four yes/no rows answer nullptr and
// draw a checkbox instead: "yes" and "no" set in 32 px bold made four rows shout
// their state at you, and a tick is the same answer in a form the eye can skip.
const char* rowValue(int i, const lock::Config& c) {
  switch (i) {
    case setui::LR_SLEEP: return lock::sleepLabel(c);
    case setui::LR_WAKE: return c.wake == lock::WAKE_HUB ? "the hub" : "the note";
    default: return nullptr;
  }
}
// The other four.
bool rowChecked(int i, const lock::Config& c) {
  switch (i) {
    case setui::LR_ROTATE: return c.autoRotate;
    case setui::LR_TIME: return c.showTime;
    case setui::LR_TEMP: return c.showTemp;
    default: return c.showBattery;
  }
}
bool rowIsCheck(int i) { return i >= setui::LR_ROTATE; }
}  // namespace

void SettingsScreen::renderLock(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "LOCK SCREEN");
  c.textTracked(16, 56, "WHEN THE DEVICE IS ASLEEP", TS_MED, true, false, 1);

  for (int i = 0; i < LR_COUNT; i++) {
    const TRect r = lockRect(i);
    // The three footer rows are one thought, so they get a rule above them
    // rather than a heading -- they say what the line along the bottom of the
    // sleeping note carries, and that is obvious once they are together.
    if (i == LR_TIME) c.drawLine(r.x, r.y - 6, r.x + r.w, r.y - 6, 1, true);

    const char* hint = rowHint(i);
    const int nameY = hint ? r.y + 8 : r.y + (r.h - c.textHeight(TS_MED)) / 2;
    c.text(r.x + 4, nameY, rowName(i), TS_MED, true);
    if (hint) c.text(r.x + 4, nameY + c.textHeight(TS_MED) + 6, hint, TS_SMALL, true);

    if (rowIsCheck(i)) {
      checkbox(c, r.x + r.w - 4 - BOX, r.y + (r.h - BOX) / 2, rowChecked(i, _lock));
    } else if (i == LR_EMPTY) {
      // Four chips, the chosen one filled. Which of the four is on is the thing
      // the row exists to say, and a filled chip says it from further away than
      // a word does.
      for (int k = 0; k < lock::EMPTY_COUNT; k++) {
        const uint8_t v = (uint8_t)(lock::EMPTY_FIRST + k);
        const TRect ch = chipRect(k);
        c.button(ch.x, ch.y, ch.w, ch.h, emptyLabel(v), _lock.empty == v, TS_SMALL);
      }
    } else if (i == LR_PICTURE) {
      const TRect sr = sendRect();
      c.button(sr.x, sr.y, sr.w, sr.h, lockimg::have() ? "REPLACE" : "SEND ONE", false,
               TS_MED);
      if (lockimg::have()) {
        const TRect rm = removeRect();
        c.button(rm.x, rm.y, rm.w, rm.h, "x", false, TS_MED);
      }
    } else {
      const char* v = rowValue(i, _lock);
      const int vw = c.textWidth(v, TS_LARGE, true);
      c.text(r.x + r.w - 4 - vw, r.y + (r.h - c.textHeight(TS_LARGE)) / 2, v, TS_LARGE, true,
             true);
    }
  }

  c.textCentered(SCREEN_W / 2, 776, _note ? _note : "tap a row to change it", TS_SMALL, true);
}

bool SettingsScreen::tapLock(ToolsHost& host, int x, int y) {
  using namespace setui;
  // The parts inside rows come first: a row-sized hit test would swallow every
  // one of them.
  if (lockimg::have() && removeRect().hit(x, y)) {
    lockimg::remove();
    host.beep(2);
    return true;
  }
  if (sendRect().hit(x, y)) {
    // Settings has no web server of its own, so this hands over to the notes
    // tool's pairing screen, whose phone page already carries the uploader.
    host.beep(1);
    host.goPairPicture();
    return false;  // the shell repaints when the tool opens
  }
  for (int k = 0; k < lock::EMPTY_COUNT; k++) {
    if (!chipRect(k).hit(x, y)) continue;
    _lock.empty = (uint8_t)(lock::EMPTY_FIRST + k);
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    if (_lock.empty != lock::EMPTY_COVER) {
      _note = nullptr;
      return true;
    }
    // Take the copy now rather than at the next book open, so choosing this
    // does something today. It reads the card, which re-initialises the panel
    // on the way out -- hence the full repaint here instead of the partial the
    // shell would otherwise do.
    recents::Entry rec[recents::MAX];
    const int n = recents::list(host.prefs(), rec);
    if (n > 0 && bthumb::stashForLock(host, rec[0].file)) {
      _note = nullptr;
    } else if (n <= 0) {
      _note = "no cover yet - open a book and it will appear here";
    } else {
      // A book HAS been read, so the copy failing is about memory or the
      // card, not about having nothing to copy. Say which.
      snprintf(_coverNote, sizeof(_coverNote),
               "could not copy the cover - free %lu KB, biggest %lu KB",
               (unsigned long)(host.heapFree() / 1024),
               (unsigned long)(host.heapLargest() / 1024));
      _note = _coverNote;
    }
    host.refresh(true);
    return false;
  }

  for (int i = 0; i < LR_COUNT; i++) {
    if (!lockRect(i).hit(x, y)) continue;
    if (i == LR_EMPTY || i == LR_PICTURE) return false;  // handled above, by part
    switch (i) {
      case LR_SLEEP: _lock.sleepIdx = (_lock.sleepIdx + 1) % lock::SLEEP_COUNT; break;
      case LR_WAKE: _lock.wake = _lock.wake == lock::WAKE_HUB ? lock::WAKE_NOTE : lock::WAKE_HUB; break;
      case LR_ROTATE: _lock.autoRotate = !_lock.autoRotate; break;
      case LR_TIME: _lock.showTime = !_lock.showTime; break;
      case LR_TEMP: _lock.showTemp = !_lock.showTemp; break;
      default: _lock.showBattery = !_lock.showBattery; break;
    }
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);  // takes effect on the next paint, not the next boot
    host.beep(0);
    return true;
  }
  return false;
}

bool SettingsScreen::onTap(ToolsHost& host, int x, int y) {
  if (_page == 1) return tapLock(host, x, y);
  if (_page == 2) return tapWall(host, x, y);
  if (_page == 3) return tapApps(host, x, y);
  if (_page == 4) return tapFiles(host, x, y);

  // Any tap that is not the reset takes the confirm back down, so an armed
  // button never survives long enough to be pressed by accident later.
  const bool wasArmed = _armed;
  _armed = false;
  _note = nullptr;

  using namespace setui;
  if (actionRect(ACT_APPS).hit(x, y)) {
    _page = 3;
    host.beep(1);
    return true;
  }

  if (actionRect(ACT_LOCK).hit(x, y)) {
    _lock = lock::load(host.prefs());
    _page = 1;
    host.beep(1);
    return true;
  }

  if (actionRect(ACT_WALL).hit(x, y)) {
    host.beep(1);
    enterWall(host);  // powers the card once; the page then repaints from RAM
    _page = 2;
    // The list read borrowed the display's bus and re-initialised the panel,
    // so the repaint has to be full. Done here rather than by the shell,
    // which would have done a partial.
    host.refresh(true);
    return false;
  }

  if (actionRect(ACT_FILES).hit(x, y)) {
    host.beep(1);
    _filesOk = _files.start(host);
    _filesSawClient = false;
    _page = 4;
    // Full, not differential: a QR code with the last screen ghosted through
    // it is a QR code a camera may refuse, and this page is mostly QR.
    host.refresh(true);
    return false;
  }

  if (actionRect(ACT_RESET).hit(x, y)) {
    if (!wasArmed) {
      _armed = true;
      host.beep(2);
    } else {
      record::clearAll(host.prefs());
      _note = "scores and tallies cleared";
      host.beep(1);
    }
    return true;
  }

  if (actionRect(ACT_SOUND).hit(x, y)) {
    // Down through the levels and round again, loudest first, so a device
    // whose owner wants it quiet gets there in one tap rather than three.
    const int n = host.soundLevels();
    host.setSoundLevel((host.soundLevel() + n - 1) % n);
    host.beep(0);  // at the level just chosen, which is the only useful preview
    host.beep(1);
    return true;
  }

  if (actionRect(ACT_CARDS).hit(x, y)) {
    for (const char* k : HELP_KEYS) host.prefs().remove(k);
    _note = "the rules cards will show again";
    host.beep(1);
    return true;
  }

  return wasArmed;  // only repaint if a question came down
}
