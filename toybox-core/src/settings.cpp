#include "settings.h"

#include "applist.h"
#include "appvis.h"
#include "record.h"
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

constexpr int BTN_X = 16, BTN_W = SCREEN_W - 32, BTN_H = 60;
constexpr int SOUND_Y = 556, CARDS_Y = 624, RESET_Y = 692;

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
}

void SettingsScreen::render(ToolsHost& host, ToolsCanvas& c) {
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
    c.text(x + BOX + 12, rowTop + (ROW_H - 16) / 2, nameOf(it), TS_MED, true);
  });

  c.button(BTN_X, SOUND_Y, BTN_W, BTN_H, host.soundOn() ? "SOUND: ON" : "SOUND: OFF", false, TS_MED);
  c.button(BTN_X, CARDS_Y, BTN_W, BTN_H, "SHOW HOW TO PLAY AGAIN", false, TS_MED);
  c.button(BTN_X, RESET_Y, BTN_W, BTN_H, _armed ? "TAP AGAIN TO ERASE SCORES" : "RESET STATS AND TALLIES", _armed, TS_MED);

  c.textCentered(SCREEN_W / 2, 764, _note ? _note : "hiding an app keeps everything saved in it", TS_SMALL, true);
}

bool SettingsScreen::onTap(ToolsHost& host, int x, int y) {
  // Any tap that is not the reset takes the confirm back down, so an armed
  // button never survives long enough to be pressed by accident later.
  const bool wasArmed = _armed;
  _armed = false;
  _note = nullptr;

  if (inRect(x, y, BTN_X, RESET_Y, BTN_W, BTN_H)) {
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

  if (inRect(x, y, BTN_X, SOUND_Y, BTN_W, BTN_H)) {
    host.setSoundOn(!host.soundOn());
    host.beep(1);
    return true;
  }

  if (inRect(x, y, BTN_X, CARDS_Y, BTN_W, BTN_H)) {
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
