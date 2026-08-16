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
  if (host.soundLevels() <= 2) return host.soundOn() ? "Sound: on" : "Sound: off";
  switch (host.soundLevel()) {
    case 0: return "Sound: mute";
    case 1: return "Sound: low";
    case 2: return "Sound: medium";
    default: return "Sound: high";
  }
}

// The lock screen's mode, in the register a value-on-the-right speaks.
const char* emptyLabelSmall(uint8_t e) {
  switch (e) {
    case lock::EMPTY_PICTURE: return "picture";
    case lock::EMPTY_GOODBYE: return "goodbye";
    case lock::EMPTY_COVER: return "cover";
    default: return "blank";
  }
}

const char* nameOf(const applist::Item& it) {
  return it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx];
}

// Little line icons for the settings rows, one per Action, drawn in a ~30 px
// box centred on (cx, cy). Primitives only, like the hub's app icons, so
// both font passes render them identically.
namespace setico {
inline void draw(ToolsCanvas& c, int act, int cx, int cy, bool dark) {
  using namespace setui;
  switch (act) {
    case ACT_WALL:  // a framed picture: mountain and sun
      c.drawRect(cx - 15, cy - 12, 30, 24, 2, dark);
      c.fillCircle(cx + 7, cy - 5, 3, dark);
      c.drawLine(cx - 11, cy + 8, cx - 3, cy - 2, 2, dark);
      c.drawLine(cx - 3, cy - 2, cx + 5, cy + 8, 2, dark);
      break;
    case ACT_LOCK:  // a padlock
      c.drawCircle(cx, cy - 6, 7, 2, dark);
      c.fillRect(cx - 11, cy - 3, 22, 15, dark);
      break;
    case ACT_APPS:  // four tiles
      c.drawRect(cx - 14, cy - 14, 12, 12, 2, dark);
      c.drawRect(cx + 2, cy - 14, 12, 12, 2, dark);
      c.drawRect(cx - 14, cy + 2, 12, 12, 2, dark);
      c.drawRect(cx + 2, cy + 2, 12, 12, 2, dark);
      break;
    case ACT_FILES:  // a phone
      c.drawRect(cx - 9, cy - 14, 18, 28, 2, dark);
      c.fillRect(cx - 4, cy + 8, 8, 2, dark);
      break;
    case ACT_SOUND:  // a speaker and its sound
      c.fillRect(cx - 13, cy - 5, 6, 10, dark);
      c.drawLine(cx - 7, cy - 5, cx - 1, cy - 11, 2, dark);
      c.drawLine(cx - 1, cy - 11, cx - 1, cy + 11, 2, dark);
      c.drawLine(cx - 1, cy + 11, cx - 7, cy + 5, 2, dark);
      c.fillRect(cx + 5, cy - 4, 2, 8, dark);
      c.fillRect(cx + 10, cy - 8, 2, 16, dark);
      break;
    case ACT_CARDS:  // the "?" the how-to cards wear
      c.drawCircle(cx, cy, 14, 2, dark);
      c.textInBox(cx - 10, cy - 11, 20, 22, "?", TS_SMALL, dark, true);
      break;
    default:  // ACT_RESET: a circle coming back around
      c.drawCircle(cx, cy, 11, 2, dark);
      c.drawLine(cx + 6, cy - 12, cx + 13, cy - 13, 2, dark);
      c.drawLine(cx + 13, cy - 13, cx + 10, cy - 6, 2, dark);
      break;
  }
}
}  // namespace setico

// Must match the names the apps pass to help::suppressed(). The flashcards
// and reader cards live here too: the row says "how to play", but what it
// means is "show me the first-time cards again", all of them.
constexpr const char* HELP_KEYS[] = {"h_wrd", "h_non", "h_g20", "h_xo",
                                     "h_sea", "h_sud", "h_fc",  "h_bk", "h_ep"};

}  // namespace

void SettingsScreen::enter() {
  _armed = false;
  _note = nullptr;
  _page = 0;
}

bool SettingsScreen::back() {
  if (_page == 0) return false;
  leaveFiles();  // no-op unless the files page was the one being left
  // The picture list was opened from the lock screen page, so it goes back
  // there rather than all the way out; every other page came from the top.
  _page = _page == 5 ? 1 : 0;
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

// --- the wallpaper and lock-picture pages -------------------------------------
// What is on the device now, shown as the picture itself: a 1:5 miniature of
// the stored file, its source's name beside it, REMOVE underneath. Then what
// the card offers, the chosen row wearing a tick. One layout for both pages,
// because they are the same page pointed at different files.
void SettingsScreen::enterWall(ToolsHost& host) {
  _wallN = (int8_t)host.sdWallpapers(_wallNames, setui::WALL_MAX);
  _wallPage = 0;
}

namespace {
// < PREV   2 / 4   NEXT > under the rows, drawn only once the card offers
// more than one page. An arrow that cannot go anywhere is not drawn at all.
void drawWallPager(ToolsCanvas& c, bool lockPage, int page, int pages) {
  using namespace setui;
  if (pages <= 1) return;
  const TRect pv = wallPagerPrev(lockPage), nx = wallPagerNext(lockPage);
  if (page > 0) c.button(pv.x, pv.y, pv.w, pv.h, "< PREV", false, TS_SMALL);
  if (page < pages - 1) c.button(nx.x, nx.y, nx.w, nx.h, "NEXT >", false, TS_SMALL);
  char b[16];
  snprintf(b, sizeof(b), "%d / %d", page + 1, pages);
  c.textCentered(SCREEN_W / 2, pv.y + 12, b, TS_MED, true);
}
}  // namespace

namespace {
// The stored picture at a fifth: 480x800 sampled to 96x160, greys thresholded
// at the middle. Small, but unmistakably the picture -- which is the whole
// question this block answers.
void drawMiniPreview(ToolsCanvas& c, const char* path, int x, int y) {
  size_t len = 0;
  char* buf = tfs::readAlloc(path, len);
  if (buf) {
    if (len == tbimg::FILE_SIZE) {
      const uint8_t* bits = (const uint8_t*)buf + tbimg::HEADER;
      for (int py = 0; py < setui::CUR_PV_H; py++)
        for (int px = 0; px < setui::CUR_PV_W; px++) {
          const int sx = px * 5 + 2, sy = py * 5 + 2;
          if (!(bits[sy * 60 + (sx >> 3)] & (0x80 >> (sx & 7))))
            c.fillRect(x + px, y + py, 1, 1, true);
        }
    } else if (len == tbg2::FILE_SIZE) {
      const uint8_t* b2 = (const uint8_t*)buf + tbg2::HEADER;
      for (int py = 0; py < setui::CUR_PV_H; py++)
        for (int px = 0; px < setui::CUR_PV_W; px++) {
          const uint32_t i = (uint32_t)(py * 5 + 2) * 480 + (uint32_t)(px * 5 + 2);
          if (((b2[i >> 2] >> (6 - 2 * (i & 3))) & 3) < 2) c.fillRect(x + px, y + py, 1, 1, true);
        }
    }
    free(buf);
  }
  c.drawRect(x - 2, y - 2, setui::CUR_PV_W + 4, setui::CUR_PV_H + 4, 1, true);
}

// A tick beside the list row whose file is the one on the device.
void drawRowTick(ToolsCanvas& c, const TRect& r) {
  const int cx = r.x + r.w - 28, cy = r.y + r.h / 2;
  c.drawCircle(cx, cy, 14, 2, true);
  c.drawLine(cx - 6, cy, cx - 2, cy + 5, 3, true);
  c.drawLine(cx - 2, cy + 5, cx + 7, cy - 5, 3, true);
}

// One page drawn for both destinations: the stored file(s), the pref that
// remembers where the picture came from, and the page's own words.
void renderPicturePage(ToolsHost& host, ToolsCanvas& c, const char* title, bool have,
                       const char* pvPath, const char* pvAlt, const char* srcKey,
                       const char* noneLine, const char* rows[][40], int, int) {
  (void)rows;
  using namespace setui;
  drawTopBar(c, title);
  c.textTracked(16, CUR_HEAD_Y, "CURRENT", TS_SMALL, true, false, 1);
  c.fillRect(16, CUR_HEAD_Y + 22, SCREEN_W - 32, 1, true);
  if (have) {
    // Whichever file exists is the one previewed: the 1-bit picture, or the
    // grey one a card .bmp became.
    drawMiniPreview(c, tfs::exists(pvPath) ? pvPath : pvAlt, 18, CUR_Y);
    char src[40] = "";
    host.prefs().getString(srcKey, src, sizeof(src));
    const int tx = 16 + CUR_PV_W + 20;
    c.textClipped(tx, CUR_Y + 6, SCREEN_W - tx - 16, src[0] ? src : "sent from the phone",
                  TS_MED, true);
    const TRect rm = wallRemoveRect();
    c.button(rm.x, rm.y, rm.w, rm.h, "REMOVE", false, TS_MED);
  } else {
    c.text(20, CUR_Y + 6, noneLine, TS_MED, true);
  }
}
}  // namespace

void SettingsScreen::renderWall(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  renderPicturePage(host, c, "WALLPAPER", wallimg::have(), wallimg::PATH, wallimg::PATH,
                    "wp_src", "none set - the home screen is plain", nullptr, 0, 0);

  c.textTracked(16, WALL_Y0 - 44, "ON THE SD CARD", TS_SMALL, true, false, 1);
  c.fillRect(16, WALL_Y0 - 22, SCREEN_W - 32, 1, true);

  char src[40] = "";
  host.prefs().getString("wp_src", src, sizeof(src));
  if (_wallN < 0) {
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 60, "no card found", TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 104, "is one in the slot?", TS_MED, true);
  } else if (_wallN == 0) {
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 60, "no pictures on the card", TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 104, "put .bmp pictures in the card's", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 132, "root or /wallpapers", TS_SMALL, true);
  } else {
    const int pages = (_wallN + WALL_PER - 1) / WALL_PER;
    const int first = _wallPage * WALL_PER;
    for (int i = 0; first + i < _wallN && i < WALL_PER; i++) {
      const TRect r = wallRect(i);
      c.listRow(r.x, r.y, r.w, r.h, _wallNames[first + i]);
      if (wallimg::have() && strcmp(_wallNames[first + i], src) == 0) drawRowTick(c, r);
    }
    drawWallPager(c, false, _wallPage, pages);
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
    host.prefs().remove("wp_src");
    _note = "removed - the home screen is plain again";
    host.beep(2);
    return true;
  }
  const int pages = _wallN > 0 ? (_wallN + WALL_PER - 1) / WALL_PER : 1;
  if (pages > 1) {
    if (_wallPage > 0 && wallPagerPrev(false).hit(x, y)) {
      _wallPage--;
      host.beep(1);
      return true;
    }
    if (_wallPage < pages - 1 && wallPagerNext(false).hit(x, y)) {
      _wallPage++;
      host.beep(1);
      return true;
    }
  }
  const int first = _wallPage * WALL_PER;
  for (int row = 0; row < WALL_PER && first + row < _wallN; row++) {
    const int i = first + row;
    if (!wallRect(row).hit(x, y)) continue;
    if (host.sdWallpaperTake(_wallNames[i])) {
      host.prefs().putString("wp_src", _wallNames[i]);
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

// --- the lock screen's picture ------------------------------------------------
// The same list as the wallpaper page, because it is the same kind of file, and
// a card that already holds pictures for one of them holds them for the other.
// The card comes first here for the reason it comes first there: a picture that
// is already on the card is one tap away, where the phone route is an access
// point, a browser, a pairing step and a transfer -- worth it once, for a photo
// that only exists on a phone, and worth nothing at all the other times.
//
// So the phone is still reachable, as one line at the foot of the page. It has
// to be: a device with an empty card slot has no other way to get a picture in.
void SettingsScreen::renderLockPic(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  renderPicturePage(host, c, "LOCK PICTURE", lockimg::have(), lockimg::PATH, lockimg::G2_PATH,
                    "lk_src", "none set yet", nullptr, 0, 0);

  c.textTracked(16, WALL_Y0 - 44, "ON THE SD CARD", TS_SMALL, true, false, 1);
  c.fillRect(16, WALL_Y0 - 22, SCREEN_W - 32, 1, true);

  char src[40] = "";
  host.prefs().getString("lk_src", src, sizeof(src));
  if (_wallN < 0) {
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 60, "no card found", TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 104, "is one in the slot?", TS_MED, true);
  } else if (_wallN == 0) {
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 60, "no pictures on the card", TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 104, "put .bmp pictures in the card's", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, WALL_Y0 + 132, "root or /wallpapers", TS_SMALL, true);
  } else {
    // One row fewer than the wallpaper page: the phone line needs its seat.
    const int pages = (_wallN + LOCK_PER - 1) / LOCK_PER;
    const int first = _wallPage * LOCK_PER;
    for (int i = 0; first + i < _wallN && i < LOCK_PER; i++) {
      const TRect r = wallRect(i);
      c.listRow(r.x, r.y, r.w, r.h, _wallNames[first + i]);
      if (lockimg::have() && strcmp(_wallNames[first + i], src) == 0) drawRowTick(c, r);
    }
    drawWallPager(c, true, _wallPage, pages);
  }

  const TRect ph = lockPhoneRect();
  c.fillRect(ph.x, ph.y - 8, ph.w, 1, true);
  c.text(ph.x + 4, ph.y + 8, "or send one from a phone", TS_SMALL, true);

  c.textCentered(SCREEN_W / 2, 776,
                 _note ? _note : "a chosen picture is copied in, so the card can come out",
                 TS_SMALL, true);
}

bool SettingsScreen::tapLockPic(ToolsHost& host, int x, int y) {
  using namespace setui;
  _note = nullptr;
  if (lockimg::have() && wallRemoveRect().hit(x, y)) {
    lockimg::remove();
    host.prefs().remove("lk_src");
    _note = "removed";
    host.beep(2);
    return true;
  }
  if (lockPhoneRect().hit(x, y)) {
    // Settings has no web server of its own, so this hands over to the notes
    // tool's pairing screen, whose phone page already carries the uploader.
    host.beep(1);
    host.goPairPicture();
    return false;  // the shell repaints when the tool opens
  }
  const int pages = _wallN > 0 ? (_wallN + LOCK_PER - 1) / LOCK_PER : 1;
  if (pages > 1) {
    if (_wallPage > 0 && wallPagerPrev(true).hit(x, y)) {
      _wallPage--;
      host.beep(1);
      return true;
    }
    if (_wallPage < pages - 1 && wallPagerNext(true).hit(x, y)) {
      _wallPage++;
      host.beep(1);
      return true;
    }
  }
  const int first = _wallPage * LOCK_PER;
  for (int row = 0; row < LOCK_PER && first + row < _wallN; row++) {
    const int i = first + row;
    if (!wallRect(row).hit(x, y)) continue;
    if (host.sdLockTake(_wallNames[i])) {
      host.prefs().putString("lk_src", _wallNames[i]);
      _note = "lock picture set";
      host.beep(1);
    } else {
      _note = "could not read it from the card";
      host.beep(2);
    }
    // The card had the bus and the panel was re-initialised on the way out, so
    // a differential repaint would difference against nothing.
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
  if (_page == 5) {
    renderLockPic(host, c);
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
  // Group headings above their first rows, from the same position table the
  // rects come from -- so a row cannot move without its heading following.
  {
    static const char* kHeads[5] = {"LOOK", "APPS", "PHONE", "DEVICE", "EXTRAS"};
    static const int kFirstRow[5] = {0, 2, 3, 4, 5};
    for (int k = 0; k < 5; k++) {
      const int y = ROOT_Y0 + kFirstRow[k] * BTN_STEP + (k + 1) * ROOT_HEAD - 28;
      c.textTracked(BTN_X, y, kHeads[k], TS_SMALL, true, false, 1);
      c.fillRect(BTN_X, y + 22, BTN_W, 1, true);
    }
  }

  // Each row: icon, name, and the CURRENT VALUE on the right -- the page
  // answers before it is asked. A chevron on the rows that open pages.
  char apps[16];
  snprintf(apps, sizeof(apps), "%d shown", appvis::shown());
  const char* sound = soundLabel(host);
  const char* soundV = strchr(sound, ' ') ? strchr(sound, ' ') + 1 : sound;
  struct Row {
    int act;
    const char* name;
    const char* value;
    bool page;  // opens a page of its own
  };
  const Row rows[ACT_COUNT] = {
      {ACT_WALL, "Wallpaper", wallimg::have() ? "set" : "none", true},
      {ACT_LOCK, "Lock screen", emptyLabelSmall(lock::config().empty), true},
      {ACT_APPS, "Apps on the hub", apps, true},
      {ACT_FILES, "Files over WiFi", "", true},
      {ACT_SOUND, "Sound", soundV, false},
      {ACT_CARDS, "Show the how-to cards again", "", false},
      {ACT_RESET, _armed ? "Tap again to erase scores" : "Reset stats and tallies", "", false},
  };
  for (const Row& r : rows) {
    const TRect a = actionRect(r.act);
    const bool inverted = r.act == ACT_RESET && _armed;
    if (inverted) c.fillRect(a.x, a.y, a.w, a.h, true);
    setico::draw(c, r.act, a.x + 22, a.y + a.h / 2, !inverted);
    c.text(a.x + 52, a.y + (a.h - c.textHeight(TS_MED)) / 2, r.name, TS_MED, !inverted);
    int rx = a.x + a.w - 8;
    if (r.page) {
      // The chevron: two strokes, quieter than a glyph and always the same.
      const int cy = a.y + a.h / 2;
      c.drawLine(rx - 14, cy - 9, rx - 5, cy, 2, !inverted);
      c.drawLine(rx - 5, cy, rx - 14, cy + 9, 2, !inverted);
      rx -= 26;
    }
    if (r.value[0]) {
      const int vw = c.textWidth(r.value, TS_MED, true);
      c.text(rx - vw, a.y + (a.h - c.textHeight(TS_MED)) / 2, r.value, TS_MED, !inverted, true);
    }
  }

  c.textCentered(SCREEN_W / 2, 776, _note ? _note : "tap a row to open or change it", TS_SMALL,
                 true);
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
// The four on/off rows.
const char* rowName(int i) {
  switch (i) {
    case setui::LR_ROTATE: return "Turn with the device";
    case setui::LR_TIME: return "Show the time";
    case setui::LR_TEMP: return "Show the temperature";
    default: return "Show the battery";
  }
}
bool rowChecked(int i, const lock::Config& c) {
  switch (i) {
    case setui::LR_ROTATE: return c.autoRotate;
    case setui::LR_TIME: return c.showTime;
    case setui::LR_TEMP: return c.showTemp;
    default: return c.showBattery;
  }
}
bool rowIsCheck(int i) { return i >= setui::LR_ROTATE; }

// A section heading in the root page's register: small tracked caps over a
// hairline, with one quiet line of explanation.
void lockHead(ToolsCanvas& c, int y, const char* name, const char* sub) {
  c.textTracked(setui::LOCK_X, y, name, TS_SMALL, true, false, 1);
  if (sub) {
    const int sw = c.textWidth(sub, TS_SMALL);
    c.text(setui::LOCK_X + setui::LOCK_W - sw, y, sub, TS_SMALL, true);
  }
  c.fillRect(setui::LOCK_X, y + 22, setui::LOCK_W, 1, true);
}
}  // namespace

void SettingsScreen::renderLock(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "LOCK SCREEN");

  // WHEN IT SLEEPS: the five timings as chips, the current one filled.
  lockHead(c, 64, "WHEN IT SLEEPS", "the panel keeps its image");
  static const char* kSleep[lock::SLEEP_COUNT] = {"never", "1 min", "5 min", "15 min", "30 min"};
  for (int k = 0; k < lock::SLEEP_COUNT; k++) {
    const TRect ch = sleepChipRect(k);
    c.button(ch.x, ch.y, ch.w, ch.h, kSleep[k], _lock.sleepIdx == k, TS_SMALL);
  }

  // WHAT IT SHOWS, with no note pinned: the four faces, then the picture's
  // own row -- its state on the left, its actions on the right.
  lockHead(c, 162, "WHAT IT SHOWS", "with no note pinned");
  for (int k = 0; k < lock::EMPTY_COUNT; k++) {
    const uint8_t v = (uint8_t)(lock::EMPTY_FIRST + k);
    const TRect ch = chipRect(k);
    c.button(ch.x, ch.y, ch.w, ch.h, emptyLabel(v), _lock.empty == v, TS_SMALL);
  }
  {
    const TRect r = lockRect(LR_PICTURE);
    c.text(r.x + 4, r.y + 8, "The picture", TS_MED, true);
    c.text(r.x + 4, r.y + 36, lockimg::have() ? "one is stored" : "none chosen yet", TS_SMALL,
           true);
    const TRect sr = sendRect();
    c.button(sr.x, sr.y, sr.w, sr.h, lockimg::have() ? "REPLACE" : "FROM CARD", false, TS_MED);
    if (lockimg::have()) {
      const TRect rm = removeRect();
      c.button(rm.x, rm.y, rm.w, rm.h, "x", false, TS_MED);
    }
  }

  // THE FOOTER LINE: what the sleeping panel's bottom line carries.
  lockHead(c, 324, "THE FOOTER LINE", "under a note or a goodbye");
  for (int i = LR_TIME; i <= LR_BATT; i++) {
    const TRect r = lockRect(i);
    c.text(r.x + 4, r.y + (r.h - c.textHeight(TS_MED)) / 2, rowName(i), TS_MED, true);
    checkbox(c, r.x + r.w - 4 - BOX, r.y + (r.h - BOX) / 2, rowChecked(i, _lock));
  }

  // WAKING: where the power button lands, and whether the note follows the
  // device's angle while it sleeps.
  lockHead(c, 528, "WAKING", "the power button, and the angle");
  {
    const TRect a = wakeChipRect(0), b = wakeChipRect(1);
    c.button(a.x, a.y, a.w, a.h, "WAKE TO THE NOTE", _lock.wake == lock::WAKE_NOTE, TS_SMALL);
    c.button(b.x, b.y, b.w, b.h, "WAKE TO THE HUB", _lock.wake == lock::WAKE_HUB, TS_SMALL);
  }
  {
    const TRect r = lockRect(LR_ROTATE);
    c.text(r.x + 4, r.y + 6, rowName(LR_ROTATE), TS_MED, true);
    // When it is off, say what happens instead -- otherwise the note simply
    // stops turning and it looks like the angle came from nowhere.
    c.text(r.x + 4, r.y + 34,
           _lock.autoRotate ? "only the pinned note turns; apps stay portrait"
                            : "the note rests at the angle it was pinned at",
           TS_SMALL, true);
    checkbox(c, r.x + r.w - 4 - BOX, r.y + (r.h - BOX) / 2, _lock.autoRotate);
  }

  c.textCentered(SCREEN_W / 2, 776, _note ? _note : "everything here takes effect at once",
                 TS_SMALL, true);
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
    // Straight to the card's list. Reading it borrows the display's bus and
    // re-initialises the panel on the way out, so the repaint is full and the
    // shell is told not to do one of its own.
    host.beep(1);
    _note = nullptr;
    enterWall(host);
    _page = 5;
    host.refresh(true);
    return false;
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

  // The sleep and wake chips: a direct choice each, nothing to cycle through.
  for (int k = 0; k < lock::SLEEP_COUNT; k++) {
    if (!sleepChipRect(k).hit(x, y)) continue;
    _lock.sleepIdx = (uint8_t)k;
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    return true;
  }
  for (int k = 0; k < 2; k++) {
    if (!wakeChipRect(k).hit(x, y)) continue;
    _lock.wake = k == 0 ? lock::WAKE_NOTE : lock::WAKE_HUB;
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    return true;
  }

  for (int i = 0; i < LR_COUNT; i++) {
    if (!lockRect(i).hit(x, y)) continue;
    if (!rowIsCheck(i)) return false;  // chips and buttons were handled above
    switch (i) {
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
  if (_page == 5) return tapLockPic(host, x, y);
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
