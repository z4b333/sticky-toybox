#include "settings.h"

#include "applist.h"
#include "appvis.h"
#include "record.h"
#include "tools/lock_image.h"
#include "tools/tool_icons.h"

namespace {

// Two columns of checkboxes. A 52 px row is 5.6 mm -- under the 7 mm a
// fingertip wants -- but the row is 216 px wide and a miss only ticks a
// neighbouring box, which is visible immediately and undone by tapping it
// again. Nothing here is destructive except the reset, and that one is a
// full-width 60 px button with a confirm.
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
  _page = 0;
  _note = nullptr;
  return true;
}

void SettingsScreen::render(ToolsHost& host, ToolsCanvas& c) {
  if (_page == 1) {
    renderLock(host, c);
    return;
  }
  drawTopBar(c, "SETTINGS");

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

  using namespace setui;
  const TRect s0 = actionRect(ACT_SOUND), s1 = actionRect(ACT_LOCK);
  const TRect s2 = actionRect(ACT_CARDS), s3 = actionRect(ACT_RESET);
  c.button(s0.x, s0.y, s0.w, s0.h, host.soundOn() ? "SOUND: ON" : "SOUND: OFF", false, TS_MED);
  c.button(s1.x, s1.y, s1.w, s1.h, "LOCK SCREEN...", false, TS_MED);
  c.button(s2.x, s2.y, s2.w, s2.h, "SHOW HOW TO PLAY AGAIN", false, TS_MED);
  c.button(s3.x, s3.y, s3.w, s3.h,
           _armed ? "TAP AGAIN TO ERASE SCORES" : "RESET STATS AND TALLIES", _armed, TS_MED);

  c.textCentered(SCREEN_W / 2, 776, _note ? _note : "hiding an app keeps everything saved in it",
                 TS_SMALL, true);
}

// --- the lock screen page -----------------------------------------------------
// One row per setting, each showing what it is set to and cycling on a tap.
// Seven rows of two-state and three-state choices do not want seven different
// controls; a row that names the thing and states the answer is enough, and it
// reads as a list of decisions rather than a panel of switches.
namespace {
const char* emptyLabel(uint8_t e) {
  switch (e) {
    case lock::EMPTY_CLOCK: return "CLOCK";
    case lock::EMPTY_PICTURE: return "PICTURE";
    case lock::EMPTY_GOODBYE: return "GOODBYE";
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
    case setui::LR_ROTATE: return "only the pinned note turns; apps stay portrait";
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
        const TRect ch = chipRect(k);
        c.button(ch.x, ch.y, ch.w, ch.h, emptyLabel((uint8_t)k), _lock.empty == k, TS_SMALL);
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
    _lock.empty = (uint8_t)k;
    lock::save(host.prefs(), _lock);
    lock::setConfig(_lock);
    host.beep(0);
    return true;
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

  // Any tap that is not the reset takes the confirm back down, so an armed
  // button never survives long enough to be pressed by accident later.
  const bool wasArmed = _armed;
  _armed = false;
  _note = nullptr;

  using namespace setui;
  if (actionRect(ACT_LOCK).hit(x, y)) {
    _lock = lock::load(host.prefs());
    _page = 1;
    host.beep(1);
    return true;
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
    host.setSoundOn(!host.soundOn());
    host.beep(1);
    return true;
  }

  if (actionRect(ACT_CARDS).hit(x, y)) {
    for (const char* k : HELP_KEYS) host.prefs().remove(k);
    _note = "the rules cards will show again";
    host.beep(1);
    return true;
  }

  bool hit = false;
  walkList([&](const applist::Item& it, const applist::Group&, int, int rowTop, int col) {
    if (hit || !inRect(x, y, COL_X[col], rowTop, COL_W, ROW_H)) return;
    hit = true;
    appvis::toggle(it.game, it.idx);
  });
  if (!hit) return wasArmed;  // only repaint if a question came down

  appvis::save(host.prefs());
  host.beep(0);
  return true;
}
