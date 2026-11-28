#include "settings.h"
#include "home_style.h"
#include "tools/sync_page.h"

#include "applist.h"
#include "appvis.h"
#include "record.h"
#include "tools/book_thumbs.h"
#include "tools/charge_dash.h"
#include "tools/flash_qr.h"
#include "tools/home_net.h"
#include "tools/kosync.h"
#include "tools/readest_hl.h"
#include "tools/lock_image.h"
#include "tools/recents.h"
#include "tools/tool_icons.h"

namespace {

// Two columns of checkboxes, on two pages (owner, C5): PLAY's fourteen on the
// first, seven a column; UTILITY and STUDY side by side on the second. The
// rows keep their 52 px (5.6 mm) -- splitting the list is what bought them
// back after C4 had squeezed them to 46.
constexpr int COL_X[2] = {16, 248};
constexpr int COL_W = 216;
constexpr int ROW_H = 52, BOX = 28;
constexpr int HEAD_H = 26;
constexpr int LIST_TOP = 92;
constexpr int APPS_PAGES = 2;
int g_appsPage = 0;

// Which page a group is listed on, and which column it starts in.
int pageOf(int g) { return g == 0 ? 0 : 1; }
int columnOf(int g) { return g == 2 ? 1 : 0; }

//   f(item, group, headTop, rowTop, col, firstOfColumn)
template <typename F>
void walkList(int page, F f) {
  for (int g = 0; g < applist::NGROUPS; g++) {
    if (pageOf(g) != page) continue;
    const applist::Group& grp = applist::GROUPS[g];
    // A group too long for one column (PLAY) carries on at the top of the next.
    const int perCol = grp.n > 8 ? (grp.n + 1) / 2 : grp.n;
    for (int i = 0; i < grp.n; i++) {
      const int col = columnOf(g) + i / perCol, k = i % perCol;
      f(grp.items[i], grp, LIST_TOP, LIST_TOP + HEAD_H + k * ROW_H, col, k == 0);
    }
  }
}

}  // namespace

namespace setui {
TRect appsItemRect(int group, int i) {
  int k = i;  // its place among everything listed on that page
  for (int g = 0; g < group; g++)
    if (pageOf(g) == pageOf(group)) k += applist::GROUPS[g].n;
  TRect r{0, 0, 0, 0};
  int n = 0;
  walkList(pageOf(group),
           [&](const applist::Item&, const applist::Group&, int, int rowTop, int col, bool) {
             if (n++ == k) r = TRect{COL_X[col], rowTop, COL_W, ROW_H};
           });
  return r;
}
int appsPageOf(int group) { return pageOf(group); }
}  // namespace setui

namespace {

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

// The lock screen's mode, in the register a value-on-the-right speaks.
const char* emptyLabelSmall(uint8_t e) {
  switch (e) {
    case lock::EMPTY_PICTURE: return "my picture";
    case lock::EMPTY_COVER: return "full cover";
    case lock::EMPTY_COVER_ON_PAGE: return "cover on page";
    case lock::EMPTY_COVER_INK: return "see-through";
    case lock::EMPTY_PAGE: return "last page";
    default: return "goodbye";
  }
}

const char* nameOf(const applist::Item& it) {
  return it.game ? gicons::NAMES[it.idx] : ticons::NAMES[it.idx];
}

// "v2.0.0-beta.3-7-gab89111" -> "v2.0.0-beta.3+7": what a row has room for.
void shortVersion(const char* v, char* out, size_t n) {
  snprintf(out, n, "%s", v ? v : "");
  const ota::Ver p = ota::parse(v);
  if (!p.ok || !p.past) return;
  char* g = strstr(out, "-g");
  if (!g) return;
  *g = 0;
  char* dash = strrchr(out, '-');
  if (dash) *dash = '+';
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
    case ACT_SYNC:  // two arrows chasing round
      c.drawCircle(cx, cy, 11, 2, dark);
      c.fillRect(cx - 14, cy - 2, 6, 4, !dark);
      c.fillRect(cx + 8, cy - 2, 6, 4, !dark);
      c.drawLine(cx + 11, cy - 1, cx + 7, cy - 6, 2, dark);
      c.drawLine(cx + 11, cy - 1, cx + 15, cy - 6, 2, dark);
      c.drawLine(cx - 11, cy + 1, cx - 7, cy + 6, 2, dark);
      c.drawLine(cx - 11, cy + 1, cx - 15, cy + 6, 2, dark);
      break;
    case ACT_CLOCK:  // a clock face, hands at ten past ten
      c.drawCircle(cx, cy, 14, 2, dark);
      c.drawLine(cx, cy, cx, cy - 8, 2, dark);
      c.drawLine(cx, cy, cx + 6, cy + 3, 2, dark);
      break;
    case ACT_HOME:  // a house
      c.drawLine(cx - 13, cy - 1, cx, cy - 13, 2, dark);
      c.drawLine(cx, cy - 13, cx + 13, cy - 1, 2, dark);
      c.drawRect(cx - 10, cy - 2, 20, 15, 2, dark);
      c.fillRect(cx - 3, cy + 5, 6, 8, dark);
      break;
    case ACT_SETUP:  // a wand: a stick and a small star
      c.drawLine(cx - 12, cy + 12, cx + 4, cy - 4, 3, dark);
      decor::star(c, cx + 8, cy - 8, 8, 3, 4, 0.f, dark);
      break;
    case ACT_VOLUME:  // four rising bars
      for (int k = 0; k < 4; k++) c.fillRect(cx - 13 + k * 7, cy + 8 - (k + 1) * 5, 4, (k + 1) * 5, dark);
      break;
    case ACT_FONT:  // a letter, which is the only honest icon for a typeface
      c.textInBox(cx - 14, cy - 15, 28, 30, "Aa", TS_MED, dark, true);
      break;
    case ACT_CARE:  // a battery, most of the way full
      c.drawRect(cx - 14, cy - 8, 26, 16, 2, dark);
      c.fillRect(cx + 12, cy - 4, 3, 8, dark);
      c.fillRect(cx - 10, cy - 4, 15, 8, dark);
      break;
    case ACT_DASH:  // a small clock face over a plug's two pins
      c.drawRect(cx - 14, cy - 12, 28, 20, 2, dark);
      c.drawLine(cx, cy - 6, cx, cy - 1, 2, dark);
      c.drawLine(cx, cy - 1, cx + 5, cy + 2, 2, dark);
      c.fillRect(cx - 6, cy + 8, 3, 6, dark);
      c.fillRect(cx + 4, cy + 8, 3, 6, dark);
      break;
    case ACT_SLEEP:  // a crescent moon
      c.fillCircle(cx, cy, 12, dark);
      c.fillCircle(cx + 7, cy - 5, 10, !dark);
      break;
    case ACT_UPDATE:  // an arrow coming down into a tray
      c.drawLine(cx, cy - 14, cx, cy + 4, 2, dark);
      c.drawLine(cx - 7, cy - 3, cx, cy + 4, 2, dark);
      c.drawLine(cx + 7, cy - 3, cx, cy + 4, 2, dark);
      c.drawLine(cx - 13, cy + 4, cx - 13, cy + 12, 2, dark);
      c.drawLine(cx - 13, cy + 12, cx + 13, cy + 12, 2, dark);
      c.drawLine(cx + 13, cy + 12, cx + 13, cy + 4, 2, dark);
      break;
    default:  // reset stats (now on the update page): a circle coming back around
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
// Every rules card's "don't show again". C3 renamed Nonogram's to Mines'
// ("h_mns"), and C4 adds the six new games.
constexpr const char* HELP_KEYS[] = {"h_wrd", "h_mns", "h_g20", "h_xo", "h_sea", "h_sud",
                                     "h_fc",  "h_bk",  "h_ep",  "h_rcp", "h_c4", "h_bj",
                                     "h_hl",  "h_cb",  "h_hn",  "h_mp",  "h_lv", "h_cl",
                                     "h_nt",  "h_fo"};

void lightSwitch(ToolsCanvas& c, int x, int y, bool on) { syncpg::lightSwitch(c, x, y, on); }

const char* dashPicName(int p) {
  static const char* N[4] = {"No picture", "Home wallpaper", "Lock-screen picture",
                             "Recent book cover"};
  return N[p < 0 || p > 3 ? 0 : p];
}
const char* dashPicShort(int p) {
  static const char* N[4] = {"No picture", "Wallpaper", "Lock picture", "Book cover"};
  return N[p < 0 || p > 3 ? 0 : p];
}
const char* dashPicPath(int p) {
  return p == 1 ? wallimg::PATH : p == 2 ? lockimg::PATH : p == 3 ? bthumb::LOCK_PATH : nullptr;
}

}  // namespace

void SettingsScreen::enter() {
  _armed = false;
  _note = nullptr;
  _page = 0;
}

bool SettingsScreen::back() {
  if (_page == 0) return false;
  if (inSetup()) {
    if (_page > 10) {
      _page--;
      return true;
    }
    if (_setupBoot) {
      _setupBoot = false;
      _page = 0;
      return false;  // the shell takes a new device to the hub
    }
    _page = 0;
    return true;
  }
  if (_page == 6 && _setupWifi) {
    // The clock page, opened from the setup's Wi-Fi step, goes back to it --
    // or, chosen as "With my phone", that was the setup: it is over.
    leaveClock();
    _netStage = 0;
    _setupWifi = false;
    if (_setupPhone) {
      _setupPhone = false;
      if (_setupBoot) {
        _setupBoot = false;
        _page = 0;
        return false;  // the shell takes a new device to the hub
      }
      _page = 0;
      return true;
    }
    _page = 10 + setui::SU_WIFI;
    return true;
  }
  leaveFiles();  // no-op unless the files page was the one being left
  leaveClock();  // ditto for the clock page's access point
  _netStage = 0;
  if (_page == 9) {
    // Nothing to leave while the device is busy writing; the shell's back
    // tap cannot even arrive then (the tick blocks). The computer page is
    // the one that can be open, and it closes with the screen.
    leaveUpdate(_upHost);
    _upStage = 0;
    _armed = false;
  }
  // The picture list was opened from the lock screen page, so it goes back
  // there rather than all the way out; every other page came from the top.
  _page = _page == 5 ? 1 : (_page == 8 ? 7 : 0);
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

void SettingsScreen::leave() {
  leaveFiles();
  leaveClock();
  leaveUpdate(_upHost);
}

bool SettingsScreen::tick(ToolsHost& host) {
  if (_page == 9) return tickUpdate(host);
  if (_page == 6) {
    if (_netStage == 1) {
      // The "connecting" screen went up with the tap; this is the wait it
      // promised. The portal is already down -- one radio job at a time.
      _netResult = homenet::syncClock(host.prefs());
      if (homenet::g_joined[0])  // the backup may have answered instead
        snprintf(_netName, sizeof(_netName), "%s", homenet::g_joined);
      _netStage = 2;
      host.beep(_netResult == homenet::Result::Ok ? 1 : 2);
      return true;
    }
    if (!_clockOk) return false;
    _clock.loop();
    // Two repaints, both of which the screen learns about rather than causes:
    // a phone joining (step one becomes step two), and the time arriving.
    if (!_clockSawSet && (_clock.wasSet() || _clock.wasRefused())) {
      _clockSawSet = true;
      return true;
    }
    if (!_clockSawClient && _clock.hasClient()) {
      _clockSawClient = true;
      return true;
    }
    return false;
  }
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
  if (page > 0) c.button(pv.x, pv.y, pv.w, pv.h, "< Prev", false, TS_SMALL);
  if (page < pages - 1) c.button(nx.x, nx.y, nx.w, nx.h, "Next >", false, TS_SMALL);
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
    c.button(rm.x, rm.y, rm.w, rm.h, "Remove", false, TS_MED);
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

  if (_note) c.textCentered(SCREEN_W / 2, 776, _note, TS_SMALL, true);
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

  if (_note) c.textCentered(SCREEN_W / 2, 776, _note, TS_SMALL, true);
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
    c.button(filesDoneRect().x, filesDoneRect().y, filesDoneRect().w, filesDoneRect().h, "Back",
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

  const TRect d = filesDoneRect();
  c.button(d.x, d.y, d.w, d.h, "Done", true, TS_LARGE);
}

// --- the font page ------------------------------------------------------------
// The families the card offers, one per row, the one in use wearing a tick.
// The firmware's own face is the first row rather than a separate button: "put
// it back" is the same kind of choice as "use that one", and a REMOVE off to
// one side would say otherwise.
void SettingsScreen::enterFont(ToolsHost& host) {
  _fontN = (int8_t)host.fontFamilies(_fontNames, setui::FONT_MAX);
  _fontPage = 0;
}

// Which text is being dressed. Five faces: the one the firmware talks in, and
// one for each app that shows the owner's own words. An app's face covers the
// book, the note, the card, the recipe -- never the menus around them, so
// leaving an app never leaves the firmware wearing its clothes.
namespace {
struct FontWho {
  int slot;
  const char* name;
  const char* what;
};
const FontWho kFontWho[ToolsHost::FONT_SLOTS] = {
    {ToolsHost::FONT_DEVICE, "The device", "menus, labels, every screen Toybox draws"},
    {ToolsHost::FONT_READER, "Books", "the page you read, not the shelf"},
    {ToolsHost::FONT_NOTES, "Notes", "what you wrote, not the list"},
    {ToolsHost::FONT_CARDS, "Flashcards", "what is on the card"},
    {ToolsHost::FONT_RECIPES, "Recipes", "the step you are cooking"},
};
}  // namespace

void SettingsScreen::renderFontWho(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "FONT");
  c.textCentered(SCREEN_W / 2, 68, "Anything not chosen is the built-in face,", TS_SMALL, true);
  c.textCentered(SCREEN_W / 2, 94, "which also fills in Thai and Chinese underneath.", TS_SMALL,
                 true);

  for (int i = 0; i < ToolsHost::FONT_SLOTS; i++) {
    const TRect r = fontWhoRect(i);
    const char* fam = host.fontFor(kFontWho[i].slot);
    c.text(r.x + 12, r.y + 6, kFontWho[i].name, TS_MED, true);
    c.textClipped(r.x + 12, r.y + 40, r.w - 160, kFontWho[i].what, TS_SMALL, true);
    const char* value = fam[0] ? fam : host.typefaceName(0);
    const int vw = c.textWidth(value, TS_MED, true);
    c.text(r.x + r.w - 34 - vw, r.y + 6, value, TS_MED, true, true);
    const int cy = r.y + 18;
    c.drawLine(r.x + r.w - 22, cy - 8, r.x + r.w - 14, cy, 2, true);
    c.drawLine(r.x + r.w - 14, cy, r.x + r.w - 22, cy + 8, 2, true);
    if (i < ToolsHost::FONT_SLOTS - 1) c.fillRect(r.x, r.y + r.h, r.w, 1, true);
  }
  // Said here, once, under all five rows: it is a consequence of choosing a
  // card face rather than a hazard of it, and it applies to whichever row was
  // just tapped. A warning on the picker itself would arrive after the
  // decision and read as a hazard.
  c.fillRect(16, 700, SCREEN_W - 32, 1, true);
  c.textCentered(SCREEN_W / 2, 716, "A face off the card is megabytes, read each time", TS_SMALL,
                 true);
  c.textCentered(SCREEN_W / 2, 744, "it is needed - so the app it dresses opens slower.", TS_SMALL,
                 true);
  c.textCentered(SCREEN_W / 2, 776, "an app's face never dresses the menus around it", TS_SMALL,
                 true);
}

bool SettingsScreen::tapFontWho(ToolsHost& host, int x, int y) {
  using namespace setui;
  for (int i = 0; i < ToolsHost::FONT_SLOTS; i++) {
    if (!fontWhoRect(i).hit(x, y)) continue;
    host.beep(1);
    _fontSlot = (int8_t)kFontWho[i].slot;
    enterFont(host);
    _page = 8;
    host.refresh(true);  // reading the card took the panel's bus with it
    return false;
  }
  return false;
}

void SettingsScreen::renderFont(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "FONT", false, "BACK");
  const FontWho& who = kFontWho[_fontSlot < ToolsHost::FONT_SLOTS ? _fontSlot : 0];
  c.textTracked(16, 56, who.name, TS_SMALL, true, false, 1);
  c.fillRect(16, 78, SCREEN_W - 32, 1, true);
  c.textCentered(SCREEN_W / 2, 92, who.what, TS_SMALL, true);

  // The built-in faces first, by name -- they are always there, card or not
  // -- then whatever the card has. The first built-in is the default, stored
  // as "", so it is the one ticked when nothing has been chosen.
  const int builtIns = host.typefaceCount();
  const int cardN = _fontN > 0 ? _fontN : 0;
  const int total = builtIns + cardN;
  const int pages = (total + FONT_PER - 1) / FONT_PER;
  const char* chosen = host.fontFor(_fontSlot);
  for (int k = 0; k < FONT_PER; k++) {
    const int idx = _fontPage * FONT_PER + k;
    if (idx >= total) break;
    const TRect r = fontRect(k);
    const bool builtIn = idx < builtIns;
    const char* name = builtIn ? host.typefaceName(idx) : _fontNames[idx - builtIns];
    const bool on = idx == 0 ? !chosen[0] : strcmp(chosen, name) == 0;
    c.text(r.x + 12, r.y + (r.h - c.textHeight(TS_MED)) / 2, name, TS_MED, true, on);
    if (on) {
      // The same tick the app list uses, so "this is the one" reads the same
      // way on both pages.
      const int cy = r.y + r.h / 2;
      c.drawLine(r.x + r.w - 44, cy, r.x + r.w - 34, cy + 10, 3, true);
      c.drawLine(r.x + r.w - 34, cy + 10, r.x + r.w - 14, cy - 10, 3, true);
    }
    if (k < FONT_PER - 1 && idx < total - 1) c.fillRect(r.x, r.y + r.h - 1, r.w, 1, true);
  }

  if (pages > 1) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d / %d", _fontPage + 1, pages);
    c.textCentered(SCREEN_W / 2, fontPagerY() + 10, buf, TS_SMALL, true);
    if (_fontPage > 0) {
      const TRect p = fontPagerPrev();
      c.button(p.x, p.y, p.w, p.h, "< Prev", false, TS_SMALL);
    }
    if (_fontPage < pages - 1) {
      const TRect n = fontPagerNext();
      c.button(n.x, n.y, n.w, n.h, "Next >", false, TS_SMALL);
    }
  }

  if (_fontN <= 0) {
    // Below the built-in rows, which are always there.
    const int y0 = fontRect(builtIns).y + 24;
    c.textCentered(SCREEN_W / 2, y0, _fontN < 0 ? "no card found" : "no fonts on the card",
                   TS_MED, true);
    c.textCentered(SCREEN_W / 2, y0 + 44, "A family is a folder of .cpfont files:", TS_SMALL,
                   true);
    c.textCentered(SCREEN_W / 2, y0 + 76, "/.fonts/Bitter/Bitter_12.cpfont", TS_SMALL, true,
                   true);
    c.textCentered(SCREEN_W / 2, y0 + 116, "/fonts works too, and so do the files", TS_SMALL,
                   true);
    c.textCentered(SCREEN_W / 2, y0 + 148, "lying loose in either folder.", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, y0 + 196, "github.com/uxjulia/crossink-fonts", TS_SMALL, true,
                   true);
  }
}

bool SettingsScreen::tapFont(ToolsHost& host, int x, int y) {
  using namespace setui;
  const int builtIns = host.typefaceCount();
  const int total = builtIns + (_fontN > 0 ? _fontN : 0);
  const int pages = (total + FONT_PER - 1) / FONT_PER;
  if (pages > 1 && y >= fontPagerY()) {
    if (_fontPage > 0 && fontPagerPrev().hit(x, y)) {
      _fontPage--;
      host.beep(0);
      return true;
    }
    if (_fontPage < pages - 1 && fontPagerNext().hit(x, y)) {
      _fontPage++;
      host.beep(0);
      return true;
    }
    return false;
  }
  for (int k = 0; k < FONT_PER; k++) {
    const int idx = _fontPage * FONT_PER + k;
    if (idx >= total) break;
    if (!fontRect(k).hit(x, y)) continue;
    host.beep(1);
    const char* pick = idx == 0          ? ""
                       : idx < builtIns ? host.typefaceName(idx)
                                        : _fontNames[idx - builtIns];
    if (!host.fontSet(_fontSlot, pick)) {
      _note = "that family could not be read";
      return true;
    }
    // Every glyph on the panel just changed, and reading the card took the
    // panel's bus with it. Nothing about this screen can be drawn as a
    // difference from what was there.
    host.refresh(true);
    return false;
  }
  return false;
}

// --- the clock page -----------------------------------------------------------
// The same two pairing steps as the files page, because it is the same act,
// and then a third state the files page has no equivalent of: the phone's page
// sends the time by itself on load, so the device can say what it now believes
// without anybody pressing anything.
void SettingsScreen::renderClock(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "CLOCK");
  char buf[72];

  if (_netStage == 1) {
    c.textCentered(SCREEN_W / 2, 260, "CONNECTING", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 320, _netName, TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, 400, "asking the internet for the time", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 436, "this takes up to half a minute", TS_SMALL, true);
    return;
  }
  if (_netStage == 2) {
    const bool ok = _netResult == homenet::Result::Ok;
    c.textCentered(SCREEN_W / 2, 120, ok ? "THE CLOCK IS SET" : "NOT SET", TS_MED, true, true);
    int hh = 0, mm = 0;
    if (ok && host.clockHHMM(hh, mm)) {
      snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
      c.textCentered(SCREEN_W / 2, 220, buf, TS_HUGE, true, true);
      snprintf(buf, sizeof(buf), "from the internet, through %s", _netName);
      c.textCentered(SCREEN_W / 2, 340, buf, TS_SMALL, true);
    } else if (!ok) {
      c.textCentered(SCREEN_W / 2, 220, homenet::describe(_netResult), TS_LARGE, true);
      const char* hint = "";
      switch (_netResult) {
        case homenet::Result::NotFound: hint = "is the router on and in range?"; break;
        case homenet::Result::Refused: hint = "save the wifi again from the phone"; break;
        case homenet::Result::NoInternet: hint = "the wifi works; the internet did not"; break;
        case homenet::Result::ClockRefused: hint = "restart the device and try again"; break;
        default: hint = "set it from the phone once first"; break;
      }
      c.textCentered(SCREEN_W / 2, 300, hint, TS_MED, true);
    }
    c.textCentered(SCREEN_W / 2, 440, "the wifi is off again", TS_SMALL, true);
    const TRect d = clockDoneRect();
    c.button(d.x, d.y, d.w, d.h, "Done", true, TS_LARGE);
    return;
  }

  if (!_clockOk) {
    c.textCentered(SCREEN_W / 2, 320, "could not start wifi", TS_LARGE, true, true);
    const TRect d = clockDoneRect();
    c.button(d.x, d.y, d.w, d.h, "Back", true, TS_LARGE);
    return;
  }

  if (_clock.wasRefused()) {
    // The phone did its half. Saying "waiting for the phone" here would send
    // somebody back to a QR code that was never the problem.
    c.textCentered(SCREEN_W / 2, 120, "THE PHONE SENT THE TIME", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 200, "the clock has not started yet", TS_LARGE, true);
    c.fillRect(48, 260, SCREEN_W - 96, 1, true);
    c.textCentered(SCREEN_W / 2, 300, "The time was written to the clock chip,", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 328, "which has not begun keeping it. That", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 356, "happens on a device just flashed, or", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 384, "one whose backup cell ran flat.", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 440, "Restart the device. The time is usually", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 468, "there afterwards, and right -- the chip", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 496, "kept what it was given.", TS_SMALL, true);
  } else if (_clock.wasSet()) {
    c.textCentered(SCREEN_W / 2, 120, "THE CLOCK IS SET", TS_MED, true, true);
    int hh = 0, mm = 0;
    if (host.clockHHMM(hh, mm)) {
      snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
      c.textCentered(SCREEN_W / 2, 220, buf, TS_HUGE, true, true);
    }
    c.textCentered(SCREEN_W / 2, 340, "from the phone that just joined", TS_MED, true);
    c.fillRect(48, 400, SCREEN_W - 96, 1, true);
    if (_clock.wifiSaved()) {
      char name[homenet::SSID_MAX];
      homenet::wifiName(host.prefs(), name, sizeof(name));
      snprintf(buf, sizeof(buf), "home wifi saved: %s", name);
      c.textCentered(SCREEN_W / 2, 430, buf, TS_SMALL, true);
      c.textCentered(SCREEN_W / 2, 458, "next time, the clock can come", TS_SMALL, true);
      c.textCentered(SCREEN_W / 2, 486, "from the internet instead", TS_SMALL, true);
    } else {
      c.textCentered(SCREEN_W / 2, 430, "The time zone came with it. Somewhere", TS_SMALL, true);
      c.textCentered(SCREEN_W / 2, 458, "else one day? Send again, or save a", TS_SMALL, true);
      c.textCentered(SCREEN_W / 2, 486, "home wifi on the phone's page.", TS_SMALL, true);
    }
    c.textCentered(SCREEN_W / 2, CLOCK_BIG_Y, "it keeps time on its own from now on,", TS_SMALL,
                   true);
    c.textCentered(SCREEN_W / 2, CLOCK_BIG_Y + 28, "through reboots and reflashes", TS_SMALL, true);
  } else if (!_clock.hasClient()) {
    c.textCentered(SCREEN_W / 2, 64, "STEP 1 OF 2", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 96, "join the device's wifi", TS_MED, true);
    const String wifi = _clock.wifiPayload();
    fqr::draw(c, FILES_QR_X, FILES_QR_Y, FILES_QR, wifi.c_str());
    c.textCentered(SCREEN_W / 2, 420, "Scan with your phone camera", TS_MED, true, true);
    snprintf(buf, sizeof(buf), "%s   key %s", _clock.ssid(), _clock.password());
    c.textCentered(SCREEN_W / 2, 456, buf, TS_MED, true);
    c.textCentered(SCREEN_W / 2, CLOCK_BIG_Y, "the phone sends the time by itself", TS_SMALL, true);
    if (netReady(host)) {
      const TRect n = clockNetRect();
      c.button(n.x, n.y, n.w, n.h, "From the internet", false, TS_MED);
    }
  } else {
    c.textCentered(SCREEN_W / 2, 64, "STEP 2 OF 2", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 96, "phone joined", TS_MED, true);
    fqr::draw(c, FILES_QR_X, FILES_QR_Y, FILES_QR, _clock.url());
    c.textCentered(SCREEN_W / 2, 420, "The page should have opened by itself", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 448, "and sent the time. If not, scan this:", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 484, _clock.url(), TS_MED, true, true);
  }

  const TRect d = clockDoneRect();
  c.button(d.x, d.y, d.w, d.h, "Done", true, TS_LARGE);
}

bool SettingsScreen::netReady(ToolsHost& host) {
  return homenet::haveWifi(host.prefs()) && homenet::haveTz(host.prefs());
}

bool SettingsScreen::tapClock(ToolsHost& host, int x, int y) {
  if (_netStage == 0 && _clockOk && !_clock.hasClient() && !_clock.wasSet() &&
      !_clock.wasRefused() && netReady(host) && setui::clockNetRect().hit(x, y)) {
    host.beep(1);
    leaveClock();  // the device's own access point down before joining the router
    homenet::wifiName(host.prefs(), _netName, sizeof(_netName));
    _netStage = 1;
    host.refresh(true);  // the QR code goes; the next tick does the waiting
    return false;
  }
  if (_netStage == 1) return false;  // nothing to press while it connects
  if (setui::clockDoneRect().hit(x, y)) {
    _netStage = 0;
    leaveClock();
    _page = 0;
    host.beep(1);
    host.refresh(true);  // the page was mostly QR; a difference of one is worse
    return false;
  }
  return false;
}

// Both ways out of the clock page go through here: the access point comes down
// with the screen that raised it, never after.
void SettingsScreen::leaveClock() {
  if (!_clockOk) return;
  _clock.stop();
  _clockOk = false;
  _clockSawClient = false;
  _clockSawSet = false;
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
  if (inSetup()) {
    renderSetup(host, c);
    return;
  }
  if (_page == 1 || _page == 23) {
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
  if (_page == 6) {
    renderClock(host, c);
    return;
  }
  if (_page == 7) {
    renderFontWho(host, c);
    return;
  }
  if (_page == 9) {
    renderUpdate(host, c);
    return;
  }
  if (_page == 8) {
    renderFont(host, c);
    return;
  }
  if (_page == 3) {
    renderApps(host, c);
    return;
  }
  if (_page == 20) {
    renderDash(host, c);
    return;
  }
  if (_page == 21) {
    renderSync(host, c);
    return;
  }
  if (_page == 22) {
    renderHomeStyle(host, c);
    return;
  }
  drawTopBar(c, "SETTINGS");

  using namespace setui;
  // The tabs: the open one filled, each with how many rows it holds.
  {
    static const char* kTabs[TAB_COUNT] = {"LOOK", "CONNECT", "DEVICE"};
    for (int t = 0; t < TAB_COUNT; t++) {
      const TRect r = tabRect(t);
      const bool on = t == _tab;
      if (on) c.fillRect(r.x, r.y, r.w, r.h, true);
      else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
      char lab[16];
      snprintf(lab, sizeof(lab), "%s %d", kTabs[t], tabCount(t));
      const int tw = c.textTrackedWidth(lab, TS_SMALL, true, 1);
      c.textTracked(r.x + (r.w - tw) / 2, r.y + (r.h - c.textHeight(TS_SMALL)) / 2, lab, TS_SMALL,
                    !on, true, 1);
    }
  }

  // Each row: icon, name, and the CURRENT VALUE on the right -- the page
  // answers before it is asked. A chevron on the rows that open pages.
  char apps[16];
  snprintf(apps, sizeof(apps), "%d shown", appvis::shown());
  // The clock row answers the question it exists for before it is tapped: the
  // time the device believes, or that it has never been told one.
  char clockV[12];
  {
    int hh = 0, mm = 0;
    if (host.clockHHMM(hh, mm))
      snprintf(clockV, sizeof(clockV), "%02d:%02d", hh, mm);
    else
      snprintf(clockV, sizeof(clockV), "not set");
  }
  // The version, as far as it fits beside the row's name: a development
  // build's "-7-gab89111" tail is for the service screen, not this row.
  char upV[24] = "";
  if (ota::Device* d = host.otaDevice()) shortVersion(d->version(), upV, sizeof(upV));
  char sleepV[12];
  {
    const int m = lock::SLEEP_MINUTES[lock::config().sleepIdx];
    if (m) snprintf(sleepV, sizeof(sleepV), "%d min", m);
    else snprintf(sleepV, sizeof(sleepV), "never");
  }
  struct Row {
    int act;
    const char* name;
    const char* value;
    bool page;  // opens a page of its own
  };
  const Row rows[ACT_COUNT] = {
      {ACT_HOME, "Home screen", homestyle::NAMES[homestyle::get(host.prefs())], true},
      {ACT_WALL, "Wallpaper", wallimg::have() ? "set" : "none", true},
      {ACT_LOCK, "Lock screen", emptyLabelSmall(lock::config().empty), true},
      {ACT_FONT, "Font", host.fontFor(ToolsHost::FONT_DEVICE)[0]
                             ? host.fontFor(ToolsHost::FONT_DEVICE)
                             : host.typefaceName(0),
       true},
      {ACT_APPS, "Apps on the hub", apps, true},
      {ACT_FILES, "Add over Wi-Fi", "", true},
      {ACT_CLOCK, "Clock", clockV, true},
      {ACT_SYNC, "Reading sync",
       !kosync::haveAccount(host.prefs()) ? "off" : kosync::isReadest(host.prefs()) ? "Readest" : "KOReader",
       true},
      {ACT_SOUND, "Sound", "", false},
      {ACT_VOLUME, "Volume", "", false},
      {ACT_SETUP, "Setup", "run again", true},
      {ACT_UPDATE, "Update & about", upV, true},
      {ACT_CARE, "Battery care 80%", "", false},
      {ACT_SLEEP, "Sleep & wake", sleepV, true},
      {ACT_DASH, "Charging dashboard", host.chargeDash() ? dashPicShort(host.chargePic()) : "Off",
       true},
  };
  for (const Row& r : rows) {
    if (TAB_OF[r.act] != _tab) continue;
    const TRect a = actionRect(r.act);
    c.fillRect(a.x, a.y + a.h + (BTN_STEP - a.h) / 2, a.w, 1, true);  // the row's hairline
    if (r.act == ACT_CARE) {
      lightSwitch(c, a.x + a.w - SWITCH_W - 4, a.y + (a.h - SWITCH_H) / 2, host.batteryCare());
    } else if (r.act == ACT_SOUND) {
      // d18: Sound and Volume are one row -- Off / Low / Mid / High.
      static const char* kVol[4] = {"Off", "Low", "Mid", "High"};
      const int lv = host.soundLevel();  // 0 = off, 1..3
      for (int k = 0; k < 4; k++) {
        const TRect g = soundSeg(k);
        const bool sel = lv == k;
        if (sel) c.fillRect(g.x, g.y, g.w, g.h, true);
        else c.drawRect(g.x, g.y, g.w, g.h, 2, true);
        c.textInBox(g.x, g.y, g.w, g.h, kVol[k], TS_SMALL, !sel, true);
      }
    }
    const bool inverted = false;
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

  // The version, quietly at the foot of every tab (kit), so "which one am I
  // running?" is answered without opening Update & about.
  if (upV[0]) {
    char ver[40];
    snprintf(ver, sizeof(ver), "Toybox %s", upV);
    c.textCentered(SCREEN_W / 2, ROOT_VER_Y, ver, TS_SMALL, true);
  }
  if (_note) c.textCentered(SCREEN_W / 2, 776, _note, TS_SMALL, true);
}

// --- the apps page ------------------------------------------------------------
// Which apps the hub's folders show, as the checkbox list the main settings
// page used to carry. On its own page both it and the buttons stopped elbowing
// each other for height.
void SettingsScreen::renderApps(ToolsHost& host, ToolsCanvas& c) {
  (void)host;
  drawTopBar(c, "APPS");

  c.textTracked(16, 56, "SHOW ON THE HUB", TS_MED, true, false, 1);

  walkList(g_appsPage, [&c](const applist::Item& it, const applist::Group& grp, int headTop,
                            int rowTop, int col, bool first) {
    const int x = COL_X[col];
    if (first) {
      // Same weight and rule as the hub's group headings, so the checkbox list
      // reads as the hub in another form rather than as a separate inventory.
      c.textTracked(x, headTop, grp.name, TS_MED, true, false, 1);
      c.fillRect(x, headTop + 20, COL_W, 1, true);
    }
    checkbox(c, x, rowTop + (ROW_H - BOX) / 2, appvis::visible(it.game, it.idx));
    c.text(x + BOX + 12, rowTop + (ROW_H - c.textHeight(TS_MED)) / 2, nameOf(it), TS_MED, true);
  });

  // The page turn: the drawers' bold arrows, the page between them.
  const TRect pv = setui::appsPrevRect(), nx = setui::appsNextRect();
  if (g_appsPage > 0) c.textInBox(pv.x, pv.y, pv.w, pv.h, "<<", TS_HUGE, true, true);
  if (g_appsPage < APPS_PAGES - 1) c.textInBox(nx.x, nx.y, nx.w, nx.h, ">>", TS_HUGE, true, true);
  char pg[8];
  snprintf(pg, sizeof(pg), "%d / %d", g_appsPage + 1, APPS_PAGES);
  c.textCentered(SCREEN_W / 2, pv.y + (pv.h - c.textHeight(TS_MED)) / 2, pg, TS_MED, true);

  // The cards row came off the settings page: it is a row about apps, and this
  // is the page about apps.
  // d18: and Reset stats, which came here from Update & about -- both are
  // about the apps, side by side. The reset asks once more before it erases.
  const TRect r = setui::appsCardsRect();
  c.button(r.x, r.y, r.w, r.h, "How-to cards", false, TS_MED);
  const TRect z = setui::appsResetRect();
  if (_armed) {
    c.fillRect(z.x, z.y, z.w, z.h, true);
    c.textInBox(z.x, z.y, z.w, z.h, "Tap to erase", TS_MED, false, true);
  } else {
    c.button(z.x, z.y, z.w, z.h, "Reset scores", false, TS_MED);
  }
  if (_note) c.textCentered(SCREEN_W / 2, 770, _note, TS_SMALL, true);
}

// --- Reading sync (D) ----------------------------------------------------------------
// The page itself is shared with the reader's Book settings (sync_page.h).
void SettingsScreen::renderSync(ToolsHost& host, ToolsCanvas& c) {
  drawTopBar(c, "READING SYNC");
  syncpg::render(c, host.prefs(), _armed, _note);
}

bool SettingsScreen::tapSync(ToolsHost& host, int x, int y) {
  return syncpg::tap(host, x, y, _armed, _note);
}

// --- the charging dashboard page (C6) ---------------------------------------------
void SettingsScreen::renderDash(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "DASHBOARD");
  const TRect sw = dashSwitchRow();
  c.text(sw.x + 14, sw.y + (sw.h - c.textHeight(TS_MED)) / 2, "Show when plugged in", TS_MED, true);
  lightSwitch(c, sw.x + sw.w - SWITCH_W - 4, sw.y + (sw.h - SWITCH_H) / 2, host.chargeDash());
  c.fillRect(sw.x, sw.y + sw.h + 2, sw.w, 1, true);
  const TRect br = dashBorderRow();
  c.text(br.x + 14, br.y + (br.h - c.textHeight(TS_MED)) / 2, "Picture border", TS_MED, true);
  lightSwitch(c, br.x + br.w - SWITCH_W - 4, br.y + (br.h - SWITCH_H) / 2, host.chargePicBorder());
  c.fillRect(br.x, br.y + br.h + 2, br.w, 1, true);
  c.textTracked(BTN_X, DASH_LABEL_Y, "PICTURE BESIDE THE WEATHER", TS_SMALL, true, false, 3);
  c.fillRect(BTN_X, DASH_PIC_Y - 4, BTN_W, 2, true);
  const int chosen = host.chargePic();
  for (int i = 0; i < DASH_PICS; i++) {
    const TRect r = dashPicRow(i);
    const bool sel = i == chosen;
    if (sel) c.fillRect(r.x, r.y, r.w, r.h, true);
    // The thumbnail: the picture itself at a tenth (48x80), or a dashed
    // frame where there is none.
    const int tx = r.x + 14, ty = r.y + (r.h - 80) / 2;
    const char* path = dashPicPath(i);
    const bool grey = i == 2 && !tbimg::have(lockimg::PATH) && lockimg::haveG2();
    if (grey) path = lockimg::G2_PATH;
    if (!path || !chgdash::drawSmooth(c, path, grey, tx, ty, 1, 10)) {
      if (sel) c.fillRect(tx, ty, 48, 80, false);
      for (int k = 0; k < 80; k += 8) {
        c.fillRect(tx, ty + k, 2, 4, true);
        c.fillRect(tx + 46, ty + k, 2, 4, true);
      }
      for (int k = 0; k < 48; k += 8) {
        c.fillRect(tx + k, ty, 4, 2, true);
        c.fillRect(tx + k, ty + 78, 4, 2, true);
      }
    } else {
      c.drawRect(tx - 1, ty - 1, 50, 82, 1, !sel);
    }
    c.text(tx + 48 + 18, r.y + (r.h - c.textHeight(TS_MED)) / 2, dashPicName(i), TS_MED, !sel);
    if (sel) {
      const int w = c.textTrackedWidth("CHOSEN", TS_SMALL, false, 3);
      c.textTracked(r.x + r.w - 14 - w, r.y + (r.h - c.textHeight(TS_SMALL)) / 2, "CHOSEN", TS_SMALL,
                    false, false, 3);
    }
    c.fillRect(r.x, r.y + r.h - 1, r.w, 1, true);
  }
  c.text(BTN_X, DASH_PIC_Y + DASH_PICS * DASH_PIC_H + 16, "No picture yet: weather only.", TS_SMALL,
         true);
}

bool SettingsScreen::tapDash(ToolsHost& host, int x, int y) {
  using namespace setui;
  if (dashSwitchRow().hit(x, y)) {
    host.setChargeDash(!host.chargeDash());
    host.beep(1);
    return true;
  }
  if (dashBorderRow().hit(x, y)) {
    host.setChargePicBorder(!host.chargePicBorder());
    host.beep(1);
    return true;
  }
  for (int i = 0; i < DASH_PICS; i++) {
    if (!dashPicRow(i).hit(x, y)) continue;
    if (host.chargePic() == i) return false;
    host.setChargePic(i);
    host.beep(0);
    return true;
  }
  return false;
}

bool SettingsScreen::tapApps(ToolsHost& host, int x, int y) {
  const bool wasArmed = _armed;
  _armed = false;
  _note = nullptr;
  if (setui::appsResetRect().hit(x, y)) {
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
  if (setui::appsCardsRect().hit(x, y)) {
    for (const char* k : HELP_KEYS) host.prefs().remove(k);
    _note = "the how-to cards will show again";
    host.beep(1);
    return true;
  }
  if (g_appsPage > 0 && setui::appsPrevRect().hit(x, y)) {
    g_appsPage--;
    host.beep(0);
    return true;
  }
  if (g_appsPage < APPS_PAGES - 1 && setui::appsNextRect().hit(x, y)) {
    g_appsPage++;
    host.beep(0);
    return true;
  }
  bool hit = false;
  walkList(g_appsPage, [&](const applist::Item& it, const applist::Group&, int, int rowTop, int col,
                           bool) {
    if (hit || !inRect(x, y, COL_X[col], rowTop, COL_W, ROW_H)) return;
    hit = true;
    appvis::toggle(it.game, it.idx);
  });
  if (!hit) return wasArmed;  // repaint only if a question came down
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
    case lock::EMPTY_PICTURE: return "MY PICTURE";
    case lock::EMPTY_COVER: return "FULL COVER";
    case lock::EMPTY_COVER_ON_PAGE: return "COVER ON PAGE";
    case lock::EMPTY_COVER_INK: return "SEE-THROUGH";
    case lock::EMPTY_PAGE: return "LAST PAGE";
    default: return "GOODBYE";
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
// A section heading in the root page's register: small tracked caps over a
// hairline, with one quiet line of explanation.
void lockHead(ToolsCanvas& c, int y, const char* name, const char* sub) {
  c.textTracked(setui::LOCK_X, y, name, TS_SMALL, true, false, 1);
  if (sub) {
    const int sw = c.textWidth(sub, TS_SMALL);
    c.text(setui::LOCK_X + setui::LOCK_W - sw, y, sub, TS_SMALL, true);
  }
  chrome2::dottedRule(c, setui::LOCK_X, y + 24, setui::LOCK_W);
}
}  // namespace

namespace {
// A tile's little picture of what the sleeping panel will show.
void lockThumb(ToolsCanvas& c, uint8_t v, int x, int y, int w, int h) {
  c.fillRect(x, y, w, h, false);
  c.drawRect(x, y, w, h, 2, true);
  const int ix = x + 8, iw = w - 16;
  switch (v) {
    case lock::EMPTY_PICTURE:  // a dotted field: a picture
      for (int yy = y + 6; yy < y + h - 6; yy += 6)
        for (int xx = x + 6 + ((yy / 6) & 1) * 3; xx < x + w - 6; xx += 6) c.fillRect(xx, yy, 2, 2, true);
      break;
    case lock::EMPTY_COVER:  // faint lines behind a hatched cover
      for (int yy = y + 10; yy < y + h - 8; yy += 8) c.fillRect(ix, yy, iw - (yy % 3) * 8, 1, true);
      c.fillRect(x + w / 2 - 24, y + 18, 48, h - 36, false);
      c.drawRect(x + w / 2 - 24, y + 18, 48, h - 36, 2, true);
      for (int d = 0; d < 48 + h - 36; d += 7)
        c.drawLine(x + w / 2 - 24 + (d < 48 ? d : 48), y + 18 + (d < 48 ? 0 : d - 48),
                   x + w / 2 - 24 + (d < h - 36 ? 0 : d - (h - 36)),
                   y + 18 + (d < h - 36 ? d : h - 36), 1, true);
      break;
    case lock::EMPTY_COVER_ON_PAGE:  // a small framed cover over a page of text
    case lock::EMPTY_COVER_INK: {  // the cover's ink straight on the page
      for (int yy = y + 10, k = 0; yy < y + h - 8; yy += 8, k++)
        if (k % 2 == 0 || v == lock::EMPTY_COVER_INK) c.fillRect(ix, yy, iw - ((k * 7) % 4) * 6, 1, true);
      if (v == lock::EMPTY_COVER_ON_PAGE) {
        const int cw = w / 2, ch = h / 2, cx0 = x + (w - cw) / 2, cy0 = y + (h - ch) / 2;
        c.fillRect(cx0 + 3, cy0 + 3, cw, ch, true);
        c.fillRect(cx0, cy0, cw, ch, false);
        c.drawRect(cx0, cy0, cw, ch, 2, true);
        for (int d = 4; d < cw + ch; d += 6)
          c.drawLine(cx0 + (d < cw ? d : cw - 1), cy0 + (d < cw ? 0 : d - cw),
                     cx0 + (d < ch ? 0 : d - ch), cy0 + (d < ch ? d : ch - 1), 1, true);
      } else {
        const int cw = w * 2 / 3, ch = h - 24, cx0 = x + (w - cw) / 2, cy0 = y + 12;
        for (int d = 0; d < cw + ch; d += 9)
          c.drawLine(cx0 + (d < cw ? d : cw - 1), cy0 + (d < cw ? 0 : d - cw),
                     cx0 + (d < ch ? 0 : d - ch), cy0 + (d < ch ? d : ch - 1), 2, true);
      }
      break;
    }
    case lock::EMPTY_PAGE:  // a page of text
      for (int yy = y + 12, k = 0; yy < y + h - 10; yy += 9, k++)
        c.fillRect(ix, yy, iw - ((k * 7) % 4) * 6, 3, true);
      break;
    default: {  // goodbye: the box with TB
      const int bw = 44, bh = 32, bx = x + (w - bw) / 2, by = y + h / 2 - 10;
      c.fillRect(bx, by, bw, bh, true);
      c.textInBox(bx, by, bw, bh, "TB", TS_SMALL, false, true);
      decor::star(c, bx + bw / 2, by - 12, 7, 2, 4, 0.f, true);
      decor::star(c, bx + 6, by - 6, 4, 1, 4, 0.f, true);
      decor::star(c, bx + bw - 6, by - 6, 4, 1, 4, 0.f, true);
      break;
    }
  }
}
// One segment of a segmented control: filled when chosen.
void segment(ToolsCanvas& c, const TRect& r, const char* label, bool on, bool lastEdge) {
  (void)lastEdge;
  if (on) c.fillRect(r.x, r.y, r.w, r.h, true);
  else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
  c.textInBox(r.x, r.y, r.w, r.h, label, TS_SMALL, !on, true);
}
}  // namespace

void SettingsScreen::renderLock(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  (void)host;
  if (_page == 23) {
    renderSleep(c);
    return;
  }
  drawTopBar(c, "LOCK SCREEN");

  // WHEN NOTHING IS PINNED: the four faces as tiles, each a small picture of
  // what the sleeping panel shows, then the picture's own row.
  lockHead(c, 58, "WHEN NOTHING IS PINNED", nullptr);
  for (int k = 0; k < lock::EMPTY_COUNT; k++) {
    const uint8_t v = lock::emptyAt(k);
    const TRect t = chipRect(k);
    const bool on = _lock.empty == v;
    const int th = t.h - 34;
    lockThumb(c, v, t.x, t.y, t.w, th);
    if (on) c.drawRect(t.x - 2, t.y - 2, t.w + 4, th + 4, 2, true);
    const char* lab = emptyLabel(v);
    char name[16];
    // The chips spoke in caps; the tiles speak in words.
    snprintf(name, sizeof(name), "%c%s", lab[0], lab + 1);
    for (char* q = name + 1; *q; q++)
      if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
    const int lw = c.textWidth(name, TS_SMALL, true);
    const int ly = t.y + th + 8;
    if (on) c.fillRect(t.x + (t.w - lw) / 2 - 6, ly - 2, lw + 12, c.textHeight(TS_SMALL) + 4, true);
    c.text(t.x + (t.w - lw) / 2, ly, name, TS_SMALL, !on, true);
  }
  {
    const TRect r = lockRect(LR_PICTURE);
    c.text(r.x + 4, r.y + 4, "The picture", TS_MED, true);
    const char* kind = !lockimg::have()      ? "none chosen yet"
                       : lockimg::haveG2()   ? "stored, in grey"
                                             : "stored, black and white";
    c.text(r.x + 4, r.y + 32, kind, TS_SMALL, true);
    const TRect sr = sendRect();
    c.button(sr.x, sr.y, sr.w, sr.h, lockimg::have() ? "Replace" : "From card", false, TS_MED);
    if (lockimg::have()) {
      const TRect rm = removeRect();
      c.button(rm.x, rm.y, rm.w, rm.h, "x", false, TS_MED);
    }
  }

  if (_note) c.textCentered(SCREEN_W / 2, 776, _note, TS_SMALL, true);
}

// d18: Settings > DEVICE > Sleep & wake. The two sections that crowded the
// lock screen page, on a page of their own.
void SettingsScreen::renderSleep(ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "SLEEP & WAKE");
  // SLEEP: one row, five segments.
  lockHead(c, 58, "SLEEP", nullptr);
  {
    const TRect r = lockRect(LR_SLEEP);
    c.text(r.x + 4, r.y + 2, "Sleep after", TS_MED, true);
    c.text(r.x + 4, r.y + 30, "minutes, no taps", TS_SMALL, true);
    static const char* kSleep[lock::SLEEP_COUNT] = {"Never", "1", "5", "15", "30"};
    for (int k = 0; k < lock::SLEEP_COUNT; k++)
      segment(c, sleepChipRect(k), kSleep[k], _lock.sleepIdx == k, false);
  }

  // WAKING: where the power button lands, and whether the note follows the
  // device's angle while it sleeps.
  lockHead(c, 172, "WAKING", nullptr);
  {
    const TRect r = lockRect(LR_WAKE);
    c.text(r.x + 4, r.y + (r.h - c.textHeight(TS_MED)) / 2, "Wake to", TS_MED, true);
    segment(c, wakeChipRect(0), "Note", _lock.wake == lock::WAKE_NOTE, false);
    segment(c, wakeChipRect(1), "Last app", _lock.wake == lock::WAKE_APP, false);
    segment(c, wakeChipRect(2), "Home", _lock.wake == lock::WAKE_HUB, false);
  }
  {
    const TRect r = lockRect(LR_ROTATE);
    c.text(r.x + 4, r.y + 4, rowName(LR_ROTATE), TS_MED, true);
    c.text(r.x + 4, r.y + 32,
           _lock.autoRotate ? "the pinned note turns; apps stay portrait"
                            : "the note rests at the angle it was pinned at",
           TS_SMALL, true);
    const bool on = _lock.autoRotate;
    const int tw = 64, th = 32, tx = r.x + r.w - tw - 4, ty = r.y + (r.h - th) / 2;
    if (on) c.fillRect(tx, ty, tw, th, true);
    else c.drawRect(tx, ty, tw, th, 2, true);
    c.fillCircle(on ? tx + tw - th / 2 : tx + th / 2, ty + th / 2, th / 2 - 6, !on);
  }

  if (_note) c.textCentered(SCREEN_W / 2, 776, _note, TS_SMALL, true);
}

bool SettingsScreen::tapLock(ToolsHost& host, int x, int y) {
  using namespace setui;
  // The parts inside rows come first: a row-sized hit test would swallow every
  // one of them.
  if (_page == 23) return tapSleep(host, x, y);
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
    const uint8_t was = _lock.empty;
    _lock.empty = lock::emptyAt(k);
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    if (_lock.empty == lock::EMPTY_PAGE) {
      // Tapping PAGE again steps how far back the page goes. How faint it
      // should be is taste rather than correctness -- a picture wants the page
      // barely there, a reader wants to still see what they were reading --
      // and putting it behind a second tap on the chip that already says PAGE
      // costs no room on a page that has none, the way the reader's own page
      // turns are cycled by tapping their row.
      if (was == lock::EMPTY_PAGE)
        _lock.pageDim = (uint8_t)((_lock.pageDim + 1) % lock::PAGE_DIM_COUNT);
      lock::save(host.prefs(), _lock);
      lock::setConfig(_lock);
      snprintf(_pageNote, sizeof(_pageNote), "the page %s  -  %s",
               lock::pageDimName(_lock.pageDim),
               lockimg::have() ? "tap Last page to change" : "no picture stored yet");
      _note = _pageNote;
      return true;
    }
    if (!lock::usesCover(_lock.empty)) {
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

  return false;
}

bool SettingsScreen::tapSleep(ToolsHost& host, int x, int y) {
  using namespace setui;
  // The sleep and wake chips: a direct choice each, nothing to cycle through.
  for (int k = 0; k < lock::SLEEP_COUNT; k++) {
    if (!sleepChipRect(k).hit(x, y)) continue;
    _lock.sleepIdx = (uint8_t)k;
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    return true;
  }
  for (int k = 0; k < 3; k++) {
    if (!wakeChipRect(k).hit(x, y)) continue;
    _lock.wake = k == 0 ? lock::WAKE_NOTE : k == 1 ? lock::WAKE_APP : lock::WAKE_HUB;
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    return true;
  }

  // The one on/off row left on the glass. (The footer toggles' rows sit off
  // it; nothing can hit them.)
  if (lockRect(LR_ROTATE).hit(x, y)) {
    _lock.autoRotate = !_lock.autoRotate;
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);  // takes effect on the next paint, not the next boot
    host.beep(0);
    return true;
  }
  return false;
}

bool SettingsScreen::onTap(ToolsHost& host, int x, int y) {
  if (inSetup()) return tapSetup(host, x, y);
  if (_page == 1 || _page == 23) return tapLock(host, x, y);
  if (_page == 2) return tapWall(host, x, y);
  if (_page == 5) return tapLockPic(host, x, y);
  if (_page == 3) return tapApps(host, x, y);
  if (_page == 20) return tapDash(host, x, y);
  if (_page == 21) return tapSync(host, x, y);
  if (_page == 22) return tapHomeStyle(host, x, y);
  if (_page == 4) return tapFiles(host, x, y);
  if (_page == 6) return tapClock(host, x, y);
  if (_page == 7) return tapFontWho(host, x, y);
  if (_page == 8) return tapFont(host, x, y);
  if (_page == 9) return tapUpdate(host, x, y);

  // Any tap that is not the reset takes the confirm back down, so an armed
  // button never survives long enough to be pressed by accident later.
  const bool wasArmed = _armed;
  _armed = false;
  _note = nullptr;

  using namespace setui;
  for (int t = 0; t < TAB_COUNT; t++) {
    if (tabRect(t).hit(x, y)) {
      if (_tab == t) return wasArmed;
      _tab = (uint8_t)t;
      host.beep(0);
      return true;
    }
  }
  // Only the open tab's rows answer.
  auto rowHit = [&](int act) { return TAB_OF[act] == _tab && actionRect(act).hit(x, y); };
  if (rowHit(ACT_APPS)) {
    _page = 3;
    g_appsPage = 0;  // the list opens on PLAY
    host.beep(1);
    return true;
  }

  if (rowHit(ACT_LOCK)) {
    _lock = lock::load(host.prefs());
    _page = 1;
    host.beep(1);
    return true;
  }
  if (rowHit(ACT_SLEEP)) {
    _lock = lock::load(host.prefs());
    _page = 23;
    host.beep(1);
    return true;
  }

  if (rowHit(ACT_WALL)) {
    host.beep(1);
    enterWall(host);  // powers the card once; the page then repaints from RAM
    _page = 2;
    // The list read borrowed the display's bus and re-initialised the panel,
    // so the repaint has to be full. Done here rather than by the shell,
    // which would have done a partial.
    host.refresh(true);
    return false;
  }

  if (rowHit(ACT_FILES)) {
    host.beep(1);
    _filesOk = _files.start(host);
    _filesSawClient = false;
    _page = 4;
    // Full, not differential: a QR code with the last screen ghosted through
    // it is a QR code a camera may refuse, and this page is mostly QR.
    host.refresh(true);
    return false;
  }

  if (rowHit(ACT_FONT)) {
    host.beep(1);
    _page = 7;
    // Reading the card takes the panel's bus with it, so what comes back has
    // to be drawn from nothing rather than from a difference.
    host.refresh(true);
    return false;
  }

  if (rowHit(ACT_CLOCK)) {
    host.beep(1);
    clockInfo(host);
    _clockOk = _clock.start(host.prefs());
    _netStage = 0;
    _clockSawClient = false;
    _clockSawSet = false;
    _page = 6;
    // Full, for the same reason the files page is: a QR code with the last
    // screen ghosted through it is one a camera may refuse.
    host.refresh(true);
    return false;
  }

  if (rowHit(ACT_UPDATE)) {
    host.beep(1);
    _page = 9;
    _upStage = 0;
    _upErr = ota::Err::Ok;
    _upRolled[0] = 0;
    if (ota::Device* d = host.otaDevice()) d->takeRollbackNotice(_upRolled, sizeof(_upRolled));
    host.refresh(true);
    return false;
  }

  if (rowHit(ACT_SETUP)) {
    host.beep(1);
    startSetup(false);
    return true;
  }
  if (TAB_OF[ACT_SOUND] == _tab) {
    // Off / Low / Mid / High, a direct choice each.
    for (int k = 0; k < 4; k++) {
      if (!soundSeg(k).hit(x, y)) continue;
      const int n = host.soundLevels();
      host.setSoundLevel(k < n ? k : n - 1);
      host.beep(0);  // at the level just chosen: the only useful preview
      return true;
    }
  }
  if (rowHit(ACT_CARE)) {
    // C6: stop charging at 80 %, start again below 75 %. Only while awake;
    // the note says so, because a Sticky left off on the cable fills up.
    host.setBatteryCare(!host.batteryCare());
    host.beep(1);
    return true;
  }
  if (rowHit(ACT_HOME)) {
    _page = 22;
    host.beep(1);
    return true;
  }
  if (rowHit(ACT_SYNC)) {
    _page = 21;
    _armed = false;
    host.beep(1);
    return true;
  }
  if (rowHit(ACT_DASH)) {
    _page = 20;
    host.beep(1);
    return true;
  }

  return wasArmed;  // only repaint if a question came down
}

// --- update & about -------------------------------------------------------------
// The version this Sticky runs, the two ways to a newer one, and the reset that
// moved here from the main page. The work itself is ota::Device's; this page
// only says what is happening, in the order it happens. Nothing here can touch
// notes or settings, and the page says so where people look before they press.

namespace {
const char* upHint(ota::Err e) {
  switch (e) {
    case ota::Err::NoWifiSaved: return "save a home wifi first: Settings > Clock";
    case ota::Err::WifiNotFound: return "is the router on and in range?";
    case ota::Err::WifiRefused: return "save the wifi again from the phone";
    case ota::Err::NoAnswer: return "the wifi works; the internet did not";
    case ota::Err::NoUpdateFile: return "try again later, or update from the website";
    case ota::Err::Download: return "move closer to the router and try again";
    case ota::Err::Mismatch: return "a damaged download; try again";
    case ota::Err::LowBattery: return "charge it, or plug it in, and try again";
    case ota::Err::LowMemory: return "restart the device and try again";
    default: return "update from the website instead";
  }
}

// A bar in the device's own language: an outlined box filling with ink.
void upBar(ToolsCanvas& c, uint32_t done, uint32_t total) {
  using namespace setui;
  c.drawRect(UPD_BAR_X, UPD_BAR_Y, UPD_BAR_W, UPD_BAR_H, 2, true);
  const int inner = UPD_BAR_W - 8;
  const int w = total ? (int)((uint64_t)inner * (done > total ? total : done) / total) : 0;
  if (w > 0) c.fillRect(UPD_BAR_X + 4, UPD_BAR_Y + 4, w, UPD_BAR_H - 8, true);
}

void upBytes(ToolsCanvas& c, int y, uint32_t done, uint32_t total) {
  char a[16], b[16], line[48];
  ota::sizeText(done, a, sizeof(a));
  if (total) {
    ota::sizeText(total, b, sizeof(b));
    snprintf(line, sizeof(line), "%s of %s", a, b);
  } else {
    snprintf(line, sizeof(line), "%s", a);
  }
  c.textCentered(SCREEN_W / 2, y, line, TS_SMALL, true);
}
}  // namespace

namespace {
// An SD card, 48x60 at (x, y): a cut corner and four contacts.
void sdIcon(ToolsCanvas& c, int x, int y) {
  c.drawLine(x, y, x + 34, y, 3, true);
  c.drawLine(x + 34, y, x + 48, y + 14, 3, true);
  c.drawLine(x + 48, y + 14, x + 48, y + 60, 3, true);
  c.drawLine(x + 48, y + 60, x, y + 60, 3, true);
  c.drawLine(x, y + 60, x, y, 3, true);
  for (int k = 0; k < 4; k++) c.fillRect(x + 8 + k * 8, y + 6, 4, 12, true);
}
// The order the card's files go in (packs, then the firmware) and where one
// stands in it, 1-based, for "STEP 2 OF 3".
int cardStep(const ota::Card& k, int idx) {
  int n = 0;
  for (int i = 1; i <= ota::CARD_FILES; i++) {
    const int j = i % ota::CARD_FILES;
    if (!k.files[j].found) continue;
    n++;
    if (j == idx) return n;
  }
  return n;
}
}  // namespace

void SettingsScreen::renderUpdate(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  _upHost = &host;
  ota::Device* d = host.otaDevice();
  drawTopBar(c, "UPDATE");
  char buf[96];

  // v2 banners: the page under them, faded, and a strip across the middle.
  auto overPage = [&](chrome2::BannerIcon icon, const char* label, const char* title,
                      const char* sub) {
    const uint8_t was = _upStage;
    _upStage = 0;
    c.clear();
    renderUpdate(host, c);
    _upStage = was;
    host.fadeBehind();
    chrome2::banner(c, SCREEN_H / 2, icon, label, title, sub);
  };
  if (_upStage == 1) {
    char lab[48];
    snprintf(lab, sizeof(lab), "CHECKING  %s", _netName);
    overPage(chrome2::BI_WORKING, lab, "Asking the update site", "Up to half a minute.");
    return;
  }

  if (_upStage == 2) {
    const ota::Remote& r = _upCheck.remote;
    const TRect dn = updDoneRect();
    if (_upErr != ota::Err::Ok) {
      c.textCentered(SCREEN_W / 2, 120, "NOT CHECKED", TS_MED, true, true);
      c.textCentered(SCREEN_W / 2, 220, ota::describe(_upErr), TS_LARGE, true);
      c.textCentered(SCREEN_W / 2, 300, upHint(_upErr), TS_MED, true);
      c.textCentered(SCREEN_W / 2, 440, "the wifi is off again", TS_SMALL, true);
      c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
      return;
    }
    if (!_upCheck.needApp && !_upCheck.needBase) {
      c.textCentered(SCREEN_W / 2, 120, "UP TO DATE", TS_MED, true, true);
      c.textCentered(SCREEN_W / 2, 220, d ? d->version() : r.version, TS_LARGE, true, true);
      snprintf(buf, sizeof(buf), "the newest on the site is %s", r.version);
      c.textCentered(SCREEN_W / 2, 300, buf, TS_SMALL, true);
      c.textCentered(SCREEN_W / 2, 440, "the wifi is off again", TS_SMALL, true);
      c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
      return;
    }
    c.textCentered(SCREEN_W / 2, 100, _upCheck.needApp ? "NEW VERSION" : "FONTS NEED REPAIR", TS_MED,
                   true, true);
    c.textCentered(SCREEN_W / 2, 170, _upCheck.needApp ? r.version : "CJK base set", TS_HUGE,
                   true, true);
    if (r.date[0]) c.textCentered(SCREEN_W / 2, 250, r.date, TS_SMALL, true);
    {
      uint32_t bytes = 0;
      if (_upCheck.needApp) bytes += r.app.bytes;
      if (_upCheck.needBase) bytes += r.base.bytes;
      char sz[16];
      ota::sizeText(bytes, sz, sizeof(sz));
      snprintf(buf, sizeof(buf), "%s to download%s", sz,
               _upCheck.needApp && _upCheck.needBase ? ", fonts included" : "");
      c.textCentered(SCREEN_W / 2, 290, buf, TS_MED, true);
    }
    snprintf(buf, sizeof(buf), "now: %s", d ? d->version() : "");
    c.textCentered(SCREEN_W / 2, 336, buf, TS_SMALL, true);
    c.fillRect(48, 392, SCREEN_W - 96, 1, true);
    c.textCentered(SCREEN_W / 2, 420, "about a minute, over the home wifi", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 448, "notes, books and settings stay", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 476, "if it will not start, it goes back by itself", TS_SMALL, true);
    const int bat = host.batteryPercent();
    const bool low = !host.charging() && bat >= 0 && bat < r.minBattery;
    const TRect in = updInstallRect();
    if (low) {
      snprintf(buf, sizeof(buf), "battery %d%%: charge to %d%% or plug in", bat, r.minBattery);
      c.textCentered(SCREEN_W / 2, in.y + 16, buf, TS_MED, true, true);
    } else {
      c.button(in.x, in.y, in.w, in.h, "Install", true, TS_LARGE);
    }
    c.button(dn.x, dn.y, dn.w, dn.h, "Not now", false, TS_LARGE);
    return;
  }

  if (_upStage == 3) {
    c.textCentered(SCREEN_W / 2, 120, "UPDATING", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 190, _upCheck.remote.version, TS_LARGE, true, true);
    c.textCentered(SCREEN_W / 2, 296, _upPart ? "the fonts" : "the firmware", TS_MED, true);
    upBar(c, _upDone, _upTotal);
    upBytes(c, UPD_BAR_Y + UPD_BAR_H + 16, _upDone, _upTotal);
    c.textCentered(SCREEN_W / 2, 470, "keep it near the router", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 506, "notes and settings are not touched", TS_SMALL, true);
    return;
  }

  if (_upStage == 4) {
    if (_upErr == ota::Err::Ok) {
      snprintf(buf, sizeof(buf), "into %s", _upCheck.remote.version);
      overPage(chrome2::BI_DOWNLOAD, "RESTARTING", buf, nullptr);
      return;
    }
    c.textCentered(SCREEN_W / 2, 120, "NOT UPDATED", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 220, ota::describe(_upErr), TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, 300, upHint(_upErr), TS_MED, true);
    c.textCentered(SCREEN_W / 2, 380, "this version keeps running", TS_SMALL, true);
    const TRect dn = updDoneRect();
    c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
    return;
  }

  // --- FROM SD CARD --------------------------------------------------------------
  if (_upStage == 7) {
    overPage(chrome2::BI_WORKING, "FROM SD CARD", "Reading the card", "A second or two.");
    return;
  }
  if (_upStage == 8) {
    const ota::Card& k = _card;
    if (k.state != ota::Card::Found) {
      const char* t = "Nothing to install";
      const char* l1 = "Copy toybox-app.bin, and any packs,";
      const char* l2 = "to toybox/update on the card.";
      const char* note = "Files from the release page, as they are.";
      const char* lab = "FROM SD CARD";
      if (k.state == ota::Card::NoCard) {
        t = "No card in the slot";
        l1 = "Put the card in, then try again.";
        l2 = nullptr;
        note = "The card is read only when you ask.";
      } else if (k.state == ota::Card::NotToybox || k.state == ota::Card::TooBig) {
        lab = "NOT INSTALLED";
        t = k.state == ota::Card::NotToybox ? "Not a Toybox file" : "Too big for its place";
        snprintf(buf, sizeof(buf), "%s on the card is not", k.bad);
        l1 = buf;
        l2 = k.state == ota::Card::NotToybox ? "made for the Sticky." : "from a release for the Sticky.";
        note = "Nothing was changed.";
      }
      sdIcon(c, SCREEN_W / 2 - 24, 196);
      c.textTrackedCentered(SCREEN_W / 2, 282, lab, TS_SMALL, true, true, 4);
      c.textCentered(SCREEN_W / 2, 312, t, TS_LARGE, true, true);
      c.textCentered(SCREEN_W / 2, 372, l1, TS_MED, true);
      if (l2) c.textCentered(SCREEN_W / 2, 404, l2, TS_MED, true);
      chrome2::dottedRule(c, 16, 668, SCREEN_W - 32);
      c.textCentered(SCREEN_W / 2, 676, note, TS_SMALL, true);
      const TRect dn = TRect{16, 712, SCREEN_W - 32, 56};
      c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
      return;
    }
    sdIcon(c, SCREEN_W / 2 - 24, 72);
    c.textTrackedCentered(SCREEN_W / 2, 150, "ON THE CARD", TS_SMALL, true, true, 4);
    if (k.hasApp()) c.textCentered(SCREEN_W / 2, 178, k.version, TS_HUGE, true, true);
    else c.textCentered(SCREEN_W / 2, 188, "Language packs", TS_LARGE, true, true);
    snprintf(buf, sizeof(buf), "now: %s", d ? d->version() : "");
    c.textCentered(SCREEN_W / 2, 246, buf, TS_SMALL, true);
    c.textTracked(16, 286, "WILL INSTALL", TS_SMALL, true, false, 1);
    chrome2::dottedRule(c, 16, 308, SCREEN_W - 32);
    int y = 316;
    for (int i = 1; i <= ota::CARD_FILES; i++) {
      const ota::CardFile& f = k.files[i % ota::CARD_FILES];
      if (!f.found) continue;
      c.text(16, y + 4, f.title, TS_MED, true, true);
      c.text(16, y + 4 + c.textHeight(TS_MED), f.file, TS_SMALL, true);
      char sz[16];
      ota::sizeText(f.bytes, sz, sizeof(sz));
      c.text(SCREEN_W - 16 - c.textWidth(sz, TS_MED), y + 4, sz, TS_MED, true);
      y += 56;
      c.fillRect(16, y - 2, SCREEN_W - 32, 1, true);
    }
    chrome2::dottedRule(c, 16, 618, SCREEN_W - 32);
    c.textCentered(SCREEN_W / 2, 628, "About a minute. Notes, books and settings stay.", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 654, "If it won't start, it goes back.", TS_SMALL, true);
    const TRect no = updCardNoRect(), go = updCardGoRect();
    c.button(no.x, no.y, no.w, no.h, "Not now", false, TS_MED);
    const int bat = host.batteryPercent();
    if (!host.charging() && bat >= 0 && bat < ota::DEFAULT_MIN_BATTERY) {
      for (int k2 = 0; k2 < go.w; k2 += 10) {
        c.fillRect(go.x + k2, go.y, 6, 2, true);
        c.fillRect(go.x + k2, go.y + go.h - 2, 6, 2, true);
      }
      for (int k2 = 0; k2 < go.h; k2 += 10) {
        c.fillRect(go.x, go.y + k2, 2, 6, true);
        c.fillRect(go.x + go.w - 2, go.y + k2, 2, 6, true);
      }
      snprintf(buf, sizeof(buf), "Battery %d%%: charge to", bat);
      c.textCentered(go.x + go.w / 2, go.y + 8, buf, TS_SMALL, true, true);
      snprintf(buf, sizeof(buf), "%d%% or plug in", ota::DEFAULT_MIN_BATTERY);
      c.textCentered(go.x + go.w / 2, go.y + 8 + c.textHeight(TS_SMALL) + 2, buf, TS_SMALL, true, true);
    } else {
      c.button(go.x, go.y, go.w, go.h, "Install", true, TS_MED);
    }
    return;
  }
  if (_upStage == 9) {
    const int n = _card.count(), at = cardStep(_card, _upPart);
    snprintf(buf, sizeof(buf), "FROM THE CARD \xc2\xb7 STEP %d OF %d", at, n);
    c.textTrackedCentered(SCREEN_W / 2, 120, buf, TS_SMALL, true, true, 3);
    c.textCentered(SCREEN_W / 2, 176, _card.hasApp() ? _card.version : "Language packs", TS_LARGE,
                   true, true);
    c.textCentered(SCREEN_W / 2, 296, _card.files[_upPart % ota::CARD_FILES].title, TS_MED, true);
    upBar(c, _upDone, _upTotal);
    upBytes(c, UPD_BAR_Y + UPD_BAR_H + 16, _upDone, _upTotal);
    c.textCentered(SCREEN_W / 2, 470, "Leave the card in.", TS_MED, true);
    c.textCentered(SCREEN_W / 2, 506, "notes and settings are not touched", TS_SMALL, true);
    return;
  }
  if (_upStage == 10) {
    if (_upErr == ota::Err::Ok) {
      if (_card.hasApp()) snprintf(buf, sizeof(buf), "into %s", _card.version);
      else snprintf(buf, sizeof(buf), "with the new packs");
      overPage(chrome2::BI_DOWNLOAD, "INSTALLED \xc2\xb7 RESTARTING", buf,
               "The files on the card are now .done.");
      return;
    }
    c.textCentered(SCREEN_W / 2, 120, "NOT UPDATED", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 220, ota::describe(_upErr), TS_LARGE, true);
    c.textCentered(SCREEN_W / 2, 300,
                   _upErr == ota::Err::Download ? "is the card still in? try again" : upHint(_upErr),
                   TS_MED, true);
    c.textCentered(SCREEN_W / 2, 380, "this version keeps running", TS_SMALL, true);
    const TRect dn = updDoneRect();
    c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
    return;
  }

  if (_upStage == 5 || _upStage == 6) {
    const ota::Lan* l = d ? &d->lan() : nullptr;
    const TRect dn = updDoneRect();
    if (_upStage == 6 || !l || l->phase == ota::Lan::Joining) {
      c.textCentered(SCREEN_W / 2, 260, "CONNECTING", TS_MED, true, true);
      c.textCentered(SCREEN_W / 2, 320, _netName, TS_LARGE, true);
      c.textCentered(SCREEN_W / 2, 400, "joining the home wifi", TS_MED, true);
      return;
    }
    switch (l->phase) {
      case ota::Lan::Receiving:
      case ota::Lan::Verifying:
        c.textCentered(SCREEN_W / 2, 120,
                       l->phase == ota::Lan::Receiving ? "RECEIVING" : "CHECKING THE FILE", TS_MED,
                       true, true);
        c.textCentered(SCREEN_W / 2, 190, l->incoming[0] ? l->incoming : "from the computer",
                       TS_LARGE, true, true);
        upBar(c, l->done, l->total);
        upBytes(c, UPD_BAR_Y + UPD_BAR_H + 16, l->done, l->total);
        c.textCentered(SCREEN_W / 2, 470, "keep it near the router", TS_MED, true);
        return;
      case ota::Lan::Done:
        snprintf(buf, sizeof(buf), "into %s", l->incoming[0] ? l->incoming : "the new build");
        overPage(chrome2::BI_DOWNLOAD, "RESTARTING", buf, nullptr);
        return;
      case ota::Lan::Failed:
        if (l->err != ota::Err::Ok && !l->code[0]) {  // never got on the network
          c.textCentered(SCREEN_W / 2, 120, "NOT CONNECTED", TS_MED, true, true);
          c.textCentered(SCREEN_W / 2, 220, ota::describe(l->err), TS_LARGE, true);
          c.textCentered(SCREEN_W / 2, 300, upHint(l->err), TS_MED, true);
          c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
          return;
        }
        break;  // locked out: the code screen says so below
      default:
        break;
    }
    c.textCentered(SCREEN_W / 2, 64, "FROM A COMPUTER", TS_MED, true, true);
    c.textCentered(SCREEN_W / 2, 100, "on the same wifi, open", TS_SMALL, true);
    c.textCentered(SCREEN_W / 2, 132, "toybox.local", TS_LARGE, true, true);
    snprintf(buf, sizeof(buf), "or http://%s", l->ip);
    c.textCentered(SCREEN_W / 2, 176, buf, TS_SMALL, true);
    snprintf(buf, sizeof(buf), "http://%s", l->ip);
    fqr::draw(c, UPD_QR_X, UPD_QR_Y, UPD_QR, buf);
    c.textCentered(SCREEN_W / 2, 432, "then type this code", TS_SMALL, true);
    c.textTrackedCentered(SCREEN_W / 2, 466, l->code, TS_HUGE, true, true, 8);
    if (l->phase == ota::Lan::Failed) {
      c.textCentered(SCREEN_W / 2, 560, "too many wrong codes", TS_MED, true, true);
      c.textCentered(SCREEN_W / 2, 596, "press DONE and open the page again", TS_SMALL, true);
    } else if (l->err != ota::Err::Ok) {
      snprintf(buf, sizeof(buf), "last try: %s", ota::describe(l->err));
      c.textCentered(SCREEN_W / 2, 560, buf, TS_MED, true, true);
      c.textCentered(SCREEN_W / 2, 596, "the page can send again", TS_SMALL, true);
    }
    c.button(dn.x, dn.y, dn.w, dn.h, "Done", true, TS_LARGE);
    return;
  }

  // The page.
  c.textTracked(16, 64, "THIS STICKY", TS_SMALL, true, false, 1);
  c.fillRect(16, 86, SCREEN_W - 32, 1, true);
  c.text(16, 100, d ? d->version() : "", TS_LARGE, true, true);
  if (d) {
    char sz[16];
    ota::sizeText(d->slotBytes(), sz, sizeof(sz));
    snprintf(buf, sizeof(buf), "running from %s, %s update slots", d->slot(), sz);
    c.text(16, 146, buf, TS_SMALL, true);
  } else {
    c.text(16, 146, "no update slots on this install:", TS_SMALL, true);
    c.text(16, 172, "use UPDATE on the website once", TS_SMALL, true);
  }
  if (_upRolled[0]) {
    c.fillRect(16, 190, SCREEN_W - 32, 64, true);
    snprintf(buf, sizeof(buf), "%s did not start", _upRolled);
    c.textCentered(SCREEN_W / 2, 196, buf, TS_MED, false, true);
    c.textCentered(SCREEN_W / 2, 228, "so this version came back", TS_SMALL, false);
  }
  c.textTracked(16, 262, "UPDATE", TS_SMALL, true, false, 1);
  c.fillRect(16, 284 - 0, SCREEN_W - 32, 1, true);
  const bool wifi = homenet::haveWifi(host.prefs());
  if (d) {
    const TRect ck = updCheckRect(), ln = updLanRect();
    // d18: three buttons and nothing under them -- each one's own page says
    // what it does. Reset stats moved to Settings > DEVICE > Apps on the hub.
    (void)wifi;
    c.button(ck.x, ck.y, ck.w, ck.h, "Check for update", true, TS_MED);
    c.button(ln.x, ln.y, ln.w, ln.h, "From a computer", false, TS_MED);
    const TRect cd = updCardRect();
    c.button(cd.x, cd.y, cd.w, cd.h, "From SD card", false, TS_MED);
  }
  if (_note) c.textCentered(SCREEN_W / 2, 776, _note, TS_SMALL, true);
}

bool SettingsScreen::tapUpdate(ToolsHost& host, int x, int y) {
  using namespace setui;
  ota::Device* d = host.otaDevice();
  if (_upStage == 1 || _upStage == 3 || _upStage == 6) return false;  // busy
  if (_upStage == 2) {
    const bool offer = _upErr == ota::Err::Ok && (_upCheck.needApp || _upCheck.needBase);
    const int bat = host.batteryPercent();
    const bool low = !host.charging() && bat >= 0 && bat < _upCheck.remote.minBattery;
    if (offer && !low && d && updInstallRect().hit(x, y)) {
      host.beep(1);
      _upStage = 3;
      _upPart = _upCheck.needBase ? 1 : 0;
      _upDone = 0;
      _upTotal = _upPart ? _upCheck.remote.base.bytes : _upCheck.remote.app.bytes;
      _upPainted = 0;
      host.refresh(true);  // the next tick does the work, painting as it goes
      return false;
    }
    if (updDoneRect().hit(x, y)) {
      host.beep(1);
      _upStage = 0;
      host.refresh(true);
      return false;
    }
    return false;
  }
  if (_upStage == 4) {
    if (_upErr != ota::Err::Ok && updDoneRect().hit(x, y)) {
      host.beep(1);
      _upStage = 0;
      host.refresh(true);
    }
    return false;
  }
  if (_upStage == 7 || _upStage == 9) return false;  // busy with the card
  if (_upStage == 8) {
    if (_card.state != ota::Card::Found) {
      if (TRect{16, 712, SCREEN_W - 32, 56}.hit(x, y)) {
        host.beep(1);
        _upStage = 0;
        host.refresh(true);
      }
      return false;
    }
    if (updCardNoRect().hit(x, y)) {
      host.beep(1);
      _upStage = 0;
      host.refresh(true);
      return false;
    }
    const int bat = host.batteryPercent();
    const bool low = !host.charging() && bat >= 0 && bat < ota::DEFAULT_MIN_BATTERY;
    if (!low && d && updCardGoRect().hit(x, y)) {
      host.beep(1);
      _upStage = 9;
      for (int i = 1; i <= ota::CARD_FILES; i++)  // the first file it will write
        if (_card.files[i % ota::CARD_FILES].found) {
          _upPart = (uint8_t)(i % ota::CARD_FILES);
          break;
        }
      _upDone = 0;
      _upTotal = _card.files[_upPart].bytes;
      _upPainted = 0;
      host.refresh(true);
    }
    return false;
  }
  if (_upStage == 10) {
    if (_upErr != ota::Err::Ok && updDoneRect().hit(x, y)) {
      host.beep(1);
      _upStage = 0;
      host.refresh(true);
    }
    return false;
  }
  if (_upStage == 5) {
    const ota::Lan::Phase ph = d ? d->lan().phase : ota::Lan::Off;
    if (ph == ota::Lan::Receiving || ph == ota::Lan::Verifying || ph == ota::Lan::Done) return false;
    if (updDoneRect().hit(x, y)) {
      host.beep(1);
      leaveUpdate(&host);
      _upStage = 0;
      host.refresh(true);
    }
    return false;
  }

  // The page.
  const bool wasArmed = _armed;
  _armed = false;
  _note = nullptr;
  if (!d) return wasArmed;
  if (updCheckRect().hit(x, y)) {
    if (!homenet::haveWifi(host.prefs())) {
      host.beep(2);
      _note = "save a home wifi from the phone first";
      return true;
    }
    host.beep(1);
    homenet::wifiName(host.prefs(), _netName, sizeof(_netName));
    _upRolled[0] = 0;
    _upStage = 1;
    host.refresh(true);
    return false;
  }
  if (updCardRect().hit(x, y)) {
    host.beep(1);
    _upRolled[0] = 0;
    _card = ota::Card{};
    _upStage = 7;
    host.refresh(true);
    return false;
  }
  if (updLanRect().hit(x, y)) {
    if (!homenet::haveWifi(host.prefs())) {
      host.beep(2);
      _note = "save a home wifi from the phone first";
      return true;
    }
    host.beep(1);
    homenet::wifiName(host.prefs(), _netName, sizeof(_netName));
    _upRolled[0] = 0;
    _upStage = 6;
    _lanSeen = 0xFF;
    host.refresh(true);
    return false;
  }
  return wasArmed;
}

void SettingsScreen::onProgress(void* ctx, uint8_t stage, uint32_t done, uint32_t total) {
  SettingsScreen* s = (SettingsScreen*)ctx;
  ToolsHost* host = s->_upHost;
  s->_upPart = stage;
  s->_upDone = done;
  s->_upTotal = total;
  if (!host) return;
  // A partial refresh is 0.3 s; one every ~6 % keeps the bar honest without
  // the panel becoming the slow part of the download.
  const uint32_t step = total / 16 + 1;
  if (done == total || done >= s->_upPainted + step || done < s->_upPainted) {
    s->_upPainted = done;
    host->refresh(false);
  }
}

bool SettingsScreen::tickUpdate(ToolsHost& host) {
  _upHost = &host;
  ota::Device* d = host.otaDevice();
  if (!d) return false;
  if (_upStage == 1) {
    _upErr = d->check(_upCheck);
    if (homenet::g_joined[0]) snprintf(_netName, sizeof(_netName), "%s", homenet::g_joined);
    _upStage = 2;
    host.beep(_upErr == ota::Err::Ok ? 1 : 2);
    return true;
  }
  if (_upStage == 3) {
    _upErr = d->install(_upCheck, &SettingsScreen::onProgress, this);
    _upStage = 4;
    host.beep(_upErr == ota::Err::Ok ? 1 : 2);
    return true;
  }
  if (_upStage == 4) {
    if (_upErr == ota::Err::Ok) d->restart();  // "RESTARTING" is on the panel by now
    return false;
  }
  if (_upStage == 7) {
    d->cardScan(_card);
    _upStage = 8;
    host.beep(_card.state == ota::Card::Found ? 1 : 2);
    return true;  // the card let go of the bus: the repaint is a full one
  }
  if (_upStage == 9) {
    _upErr = d->cardInstall(_card, &SettingsScreen::onProgress, this);
    _upStage = 10;
    host.beep(_upErr == ota::Err::Ok ? 1 : 2);
    return true;
  }
  if (_upStage == 10) {
    if (_upErr == ota::Err::Ok) d->restart();
    return false;
  }
  if (_upStage == 6) {
    d->lanStart();
    _upStage = 5;
    _lanSeen = 0xFF;
    return true;
  }
  if (_upStage == 5) {
    d->lanLoop();
    const ota::Lan& l = d->lan();
    if (l.phase != _lanSeen) {
      _lanSeen = l.phase;
      _lanPainted = l.done;
      if (l.phase == ota::Lan::Done) host.beep(1);
      return true;
    }
    if (l.phase == ota::Lan::Receiving && l.done >= _lanPainted + (l.total / 16 + 1)) {
      _lanPainted = l.done;
      host.refresh(false);
    }
    return false;
  }
  return false;
}

void SettingsScreen::leaveUpdate(ToolsHost* host) {
  if (!host) return;
  if (ota::Device* d = host->otaDevice()) {
    if (_upStage == 5 || _upStage == 6) d->lanStop();
  }
}

// --- the setup steps (v2) -------------------------------------------------------
namespace {
// One big choice row: outlined, or filled with a tick when chosen. The sub
// line is optional.
void setupRowDraw(ToolsCanvas& c, const TRect& r, const char* label, const char* sub, bool on) {
  if (on) c.fillRect(r.x, r.y, r.w, r.h, true);
  else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
  const int lh = c.textHeight(TS_MED), sh = c.textHeight(TS_SMALL);
  const int block = sub ? lh + 2 + sh : lh;
  const int ty = r.y + (r.h - block) / 2;
  c.text(r.x + 20, ty, label, TS_MED, !on, true);
  if (sub) c.text(r.x + 20, ty + lh + 2, sub, TS_SMALL, !on);
  if (on) {
    const int cx = r.x + r.w - 34, cy = r.y + r.h / 2;
    c.drawLine(cx - 10, cy, cx - 3, cy + 7, 3, false);
    c.drawLine(cx - 3, cy + 7, cx + 10, cy - 7, 3, false);
  }
}
// Setup's sleep rows, in the order they are offered, as lock::SLEEP_MINUTES
// indexes: 1, 5, 15, 30 minutes, then never.
constexpr uint8_t kSetupSleep[5] = {1, 2, 3, 4, 0};
// The built-in faces setup offers for the device's own words.
constexpr int kSetupFace[2] = {0, 2};  // Noto Sans, IBM Plex Sans
}  // namespace

// The phone page's "Your Sticky" card: version, battery, font, home Wi-Fi.
void SettingsScreen::clockInfo(ToolsHost& host) {
  ota::Device* d = host.otaDevice();
  const char* font = host.fontFor(ToolsHost::FONT_DEVICE);
  char wifi[homenet::SSID_MAX] = {};
  if (homenet::haveWifi(host.prefs())) homenet::wifiName(host.prefs(), wifi, sizeof(wifi));
  _clock.setInfo(d ? d->version() : "", host.batteryPercent(), font && font[0] ? font : host.typefaceName(0),
                 wifi);
}

void SettingsScreen::startSetup(bool fromBoot) {
  _armed = false;
  _note = nullptr;
  _setupBoot = fromBoot;
  _setupLeft = false;
  _setupWifi = false;
  _setupPhone = false;
  _lock = lock::config();
  _page = 10;
}

namespace {
// A setup choice with a small drawn mark at its left: the How step's rows.
void setupChoiceDraw(ToolsCanvas& c, const TRect& r, int icon, const char* label, const char* sub, bool on) {
  if (on) c.fillRect(r.x, r.y, r.w, r.h, true);
  else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
  const bool ink = !on;
  const int ix = r.x + 34, iy = r.y + r.h / 2;
  switch (icon) {
    case 0:  // a device
      c.drawRect(ix - 11, iy - 16, 22, 32, 2, ink);
      for (int k = 0; k < 3; k++) c.fillRect(ix - 6, iy - 8 + k * 6, 12, 2, ink);
      break;
    case 1:  // a code to scan
      c.drawRect(ix - 15, iy - 15, 12, 12, 2, ink);
      c.drawRect(ix + 3, iy - 15, 12, 12, 2, ink);
      c.drawRect(ix - 15, iy + 3, 12, 12, 2, ink);
      c.fillRect(ix + 4, iy + 4, 5, 5, ink);
      c.fillRect(ix + 10, iy + 10, 5, 5, ink);
      break;
    default:  // an arrow on
      c.drawLine(ix - 14, iy, ix + 12, iy, 3, ink);
      c.drawLine(ix + 4, iy - 8, ix + 12, iy, 3, ink);
      c.drawLine(ix + 4, iy + 8, ix + 12, iy, 3, ink);
      break;
  }
  const int lh = c.textHeight(TS_MED), sh = c.textHeight(TS_SMALL);
  const int ty = r.y + (r.h - (lh + 2 + sh)) / 2;
  c.text(r.x + 70, ty, label, TS_MED, ink, true);
  c.text(r.x + 70, ty + lh + 2, sub, TS_SMALL, ink);
}
}  // namespace

void SettingsScreen::renderSetup(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  const int step = _page - 10;
  char corner[12];
  snprintf(corner, sizeof(corner), "%d / %d", step + 1, SETUP_STEPS);
  drawTopBar(c, "SETUP", false, "", corner);

  static const char* kTitle[SETUP_STEPS] = {"Language", "How do you want to set up?", "Font",
                                            "Home screen", "Sleep after", "Beep level", "Wi-Fi"};
  static const char* kSub[SETUP_STEPS] = {
      "For menus and help. Books keep their own.",
      "You can change it all later.",
      "For menus. Books can have their own.",
      "What you see when it wakes.",
      "No taps for this long, it sleeps.",
      "Tap one to hear it.",
      "For the clock, sync and updates. Optional."};
  const TSize tts = c.textWidth(kTitle[step], TS_LARGE, true) <= SCREEN_W - 32 ? TS_LARGE : TS_MED;
  c.text(16, SU_TITLE_Y, kTitle[step], tts, true, true);
  c.text(16, SU_TITLE_Y + c.textHeight(TS_LARGE) + 6, kSub[step], TS_SMALL, true);

  const char* hint = nullptr;
  if (step == SU_LANG) {
    // Only English is in the firmware for now: the one honest row.
    setupRowDraw(c, setupRow(0), "English", "the menus and help", true);
    hint = "More languages come with updates";
  } else if (step == SU_HOW) {
    setupChoiceDraw(c, setupRow(0), 0, "On this device", "One step at a time. About a minute.", true);
    setupChoiceDraw(c, setupRow(1), 1, "With my phone", "Scan a code; Wi-Fi and the clock there.", false);
    setupChoiceDraw(c, setupRow(2), 2, "Skip", "Start with the defaults.", false);
    hint = "tap a choice";
  } else if (step == SU_FONT) {
    const char* cur = host.fontFor(ToolsHost::FONT_DEVICE);
    for (int i = 0; i < 2; i++) {
      const char* name = host.typefaceName(kSetupFace[i]);
      const bool on = i == 0 ? !cur[0] || strcmp(cur, name) == 0 : strcmp(cur, name) == 0;
      const TRect r = setupRow(i);
      // Each row is set in the face it names, so the choice is made by eye.
      const int was = host.typeface();
      host.setTypeface(kSetupFace[i]);
      setupRowDraw(c, r, name, "Aa Bb 123  \xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x84", on);
      host.setTypeface(was);
    }
    hint = "More faces in Settings > LOOK > Font";
  } else if (step == SU_HOME) {
    const uint8_t cur = homestyle::get(host.prefs());
    for (int i = 0; i < homestyle::COUNT; i++) {
      const TRect t = setupHomeTile(i);
      const bool on = cur == i;
      if (on) c.fillRect(t.x, t.y, t.w, t.h, true);
      else c.drawRect(t.x, t.y, t.w, t.h, 2, true);
      const int tw = 86, th = 112;
      homestyle::thumb(c, (uint8_t)i, t.x + (t.w - tw) / 2, t.y + 12, tw, th, !on);
      c.textCentered(t.x + t.w / 2, t.y + t.h - 30, homestyle::NAMES[i], TS_SMALL, !on, true);
    }
    hint = "Or later: Settings > LOOK > Home screen";
  } else if (step == SU_SLEEP) {
    static const char* kLab[5] = {"1 min", "5 min", "15 min", "30 min", "Never"};
    static const char* kNote[5] = {"Longest battery", "Recommended", nullptr, nullptr,
                                   "Uses the most battery"};
    for (int i = 0; i < 5; i++)
      setupRowDraw(c, setupRow(i), kLab[i], kNote[i], _lock.sleepIdx == kSetupSleep[i]);
    hint = "The last screen stays on while it sleeps";
  } else if (step == SU_BEEP) {
    static const char* kLab[4] = {"Off", "Low", "Mid", "High"};
    static const char* kNote[4] = {"No beeps at all", nullptr, "Default", nullptr};
    const int lv = host.soundLevel();
    for (int i = 0; i < 4; i++) setupRowDraw(c, setupRow(i), kLab[i], kNote[i], lv == i);
    hint = "Timers and games use this too";
  } else {
    char name[homenet::SSID_MAX] = {};
    const bool have = homenet::haveWifi(host.prefs());
    if (have) homenet::wifiName(host.prefs(), name, sizeof(name));
    setupRowDraw(c, setupRow(0), have ? name : "No home Wi-Fi yet", have ? "saved" : nullptr,
                 have);
    setupRowDraw(c, setupRow(1), have ? "Change it with my phone" : "Set it up with my phone",
                 "Scan a code, type it there", false);
    hint = "Or later: Settings > CONNECT > Clock";
  }
  if (hint) c.textCentered(SCREEN_W / 2, setupNext().y - 34, hint, TS_SMALL, true);
  if (step == SU_WIFI) {
    c.button(setupLater().x, setupLater().y, setupLater().w, setupLater().h, "Later", false);
    c.button(setupDone().x, setupDone().y, setupDone().w, setupDone().h, "Done", true);
  } else if (step != SU_HOW) {
    const TRect n = setupNext();
    c.button(n.x, n.y, n.w, n.h, "Next", true);
  }
}

bool SettingsScreen::tapSetup(ToolsHost& host, int x, int y) {
  using namespace setui;
  const int step = _page - 10;
  auto finish = [&] {
    host.beep(1);
    if (_setupBoot) {
      _setupBoot = false;
      _setupLeft = true;  // the shell takes it from here, to the hub
      _page = 0;
      return true;
    }
    _page = 0;
    return true;
  };
  auto openPhone = [&] {
    host.beep(1);
    _setupWifi = true;
    clockInfo(host);
    _clockOk = _clock.start(host.prefs());
    _netStage = 0;
    _clockSawClient = false;
    _clockSawSet = false;
    _page = 6;
    return true;
  };
  if (step == SU_WIFI) {
    if (setupLater().hit(x, y) || setupDone().hit(x, y)) return finish();
    if (setupRow(1).hit(x, y)) return openPhone();
    return false;
  }
  if (step == SU_HOW) {
    if (setupRow(0).hit(x, y)) {
      host.beep(1);
      _page++;
      return true;
    }
    if (setupRow(1).hit(x, y)) {
      _setupPhone = true;  // the phone page is the whole setup
      return openPhone();
    }
    if (setupRow(2).hit(x, y)) return finish();
    return false;
  }
  if (setupNext().hit(x, y)) {
    host.beep(1);
    _page++;
    return true;
  }
  if (step == SU_FONT) {
    for (int i = 0; i < 2; i++) {
      if (!setupRow(i).hit(x, y)) continue;
      host.beep(0);
      host.fontSet(ToolsHost::FONT_DEVICE, i == 0 ? "" : host.typefaceName(kSetupFace[i]));
      host.refresh(true);  // every glyph on the panel just changed
      return false;
    }
  } else if (step == SU_HOME) {
    for (int i = 0; i < homestyle::COUNT; i++) {
      if (!setupHomeTile(i).hit(x, y)) continue;
      homestyle::set(host.prefs(), (uint8_t)i);
      host.beep(0);
      return true;
    }
  } else if (step == SU_SLEEP) {
    for (int i = 0; i < 5; i++) {
      if (!setupRow(i).hit(x, y)) continue;
      _lock.sleepIdx = kSetupSleep[i];
      lock::save(host.prefs(), _lock);
      lock::setConfig(_lock);
      host.beep(0);
      return true;
    }
  } else if (step == SU_BEEP) {
    for (int i = 0; i < 4; i++) {
      if (!setupRow(i).hit(x, y)) continue;
      const int n = host.soundLevels();
      host.setSoundLevel(i < n ? i : n - 1);
      host.beep(0);  // at the level just chosen
      return true;
    }
  }
  return false;
}

// --- Settings > LOOK > Home screen (v2) -------------------------------------------
void SettingsScreen::renderHomeStyle(ToolsHost& host, ToolsCanvas& c) {
  using namespace setui;
  drawTopBar(c, "HOME SCREEN");
  const uint8_t cur = homestyle::get(host.prefs());
  for (int i = 0; i < homestyle::COUNT; i++) {
    const TRect t = homeStyleTile(i);
    homestyle::thumb(c, (uint8_t)i, t.x, t.y, t.w, t.h, true);
    if (cur == i) c.drawRect(t.x - 4, t.y - 4, t.w + 8, t.h + 8, 3, true);
    const char* name = i == homestyle::BOOKS ? "Books" : homestyle::NAMES[i];
    const int ly = t.y + t.h + 12;
    if (cur == i) {
      const int lw = c.textWidth(name, TS_SMALL, true) + 30;
      c.fillRect(t.x + (t.w - lw) / 2, ly - 4, lw, c.textHeight(TS_SMALL) + 8, true);
      const int tx = t.x + (t.w - lw) / 2 + 8, cy = ly + c.textHeight(TS_SMALL) / 2;
      c.drawLine(tx, cy, tx + 4, cy + 4, 2, false);
      c.drawLine(tx + 4, cy + 4, tx + 11, cy - 4, 2, false);
      c.text(tx + 16, ly, name, TS_SMALL, false, true);
    } else {
      c.textCentered(t.x + t.w / 2, ly, name, TS_SMALL, true, true);
    }
  }
  {  // the dashed promise
    const TRect t = homeStyleTile(homestyle::COUNT);
    for (int k = 0; k < t.w; k += 10) {
      c.fillRect(t.x + k, t.y, 5, 2, true);
      c.fillRect(t.x + k, t.y + t.h - 2, 5, 2, true);
    }
    for (int k = 0; k < t.h; k += 10) {
      c.fillRect(t.x, t.y + k, 2, 5, true);
      c.fillRect(t.x + t.w - 2, t.y + k, 2, 5, true);
    }
    c.textCentered(t.x + t.w / 2, t.y + t.h / 2 - 30, "More styles", TS_SMALL, true);
    c.textCentered(t.x + t.w / 2, t.y + t.h / 2 - 8, "come with", TS_SMALL, true);
    c.textCentered(t.x + t.w / 2, t.y + t.h / 2 + 14, "updates", TS_SMALL, true);
  }
}

bool SettingsScreen::tapHomeStyle(ToolsHost& host, int x, int y) {
  using namespace setui;
  for (int i = 0; i < homestyle::COUNT; i++) {
    const TRect t = homeStyleTile(i);
    if (x >= t.x - 8 && x < t.x + t.w + 8 && y >= t.y - 8 && y < t.y + t.h + 40) {
      if (homestyle::get(host.prefs()) == i) return false;
      homestyle::set(host.prefs(), (uint8_t)i);
      host.beep(1);
      return true;
    }
  }
  return false;
}
