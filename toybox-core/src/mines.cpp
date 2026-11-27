#include "mines.h"

#include <esp_random.h>

#include "record.h"
#include "tools/help.h"

using namespace minesui;

namespace {
int levelKey(char* k, size_t n, const char* pre, int lvl) { return snprintf(k, n, "%s%d", pre, lvl); }

// A flag: a pole and a pennant, drawn in `black` on the square's colour.
void flag(ToolsCanvas& c, const TRect& r, bool black) {
  const int px = r.x + r.w / 2 - 6, top = r.y + 11, bot = r.y + r.h - 11;
  c.fillRect(px, top, 3, bot - top, black);
  decor::triangle(c, px + 3, top, px + 3, top + 16, px + 19, top + 8, black);
  c.fillRect(px - 6, bot - 3, 16, 3, black);
}
// A mine: a ball with four spikes.
void mine(ToolsCanvas& c, const TRect& r, bool black) {
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  c.fillCircle(cx, cy, 10, black);
  c.fillRect(cx - 15, cy - 1, 31, 3, black);
  c.fillRect(cx - 1, cy - 15, 3, 31, black);
}
}  // namespace

void MinesApp::newGame() {
  mns::reset(_b, _level);
  _msg[0] = 0;
  saveState();
}

void MinesApp::saveState() { prefs().putBytes("ms_state", &_b, sizeof(_b)); }

bool MinesApp::loadState() {
  mns::Board s;
  if (prefs().getBytesLength("ms_state") != sizeof(s)) return false;
  if (prefs().getBytes("ms_state", &s, sizeof(s)) != sizeof(s)) return false;
  if (s.level >= mns::LEVELS || mns::over(s)) return false;  // a finished board is done
  _b = s;
  return true;
}

void MinesApp::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _rulesFromMore = false;
  _armedClear = false;
  _level = prefs().getInt("ms_lvl", 0);
  if (_level < 0 || _level >= mns::LEVELS) _level = 0;
  if (!loadState()) newGame();
  _view = help::suppressed(prefs(), "mns") ? View::Board : View::Rules;
}

// Called after every tap that changed the board: the game is over or it is not.
void MinesApp::finish() {
  char k[8];
  if (mns::won(_b)) {
    levelKey(k, sizeof(k), "ms_w", _b.level);
    prefs().putInt(k, prefs().getInt(k, 0) + 1);
    const int streak = prefs().getInt("ms_strk", 0) + 1;
    prefs().putInt("ms_strk", streak);
    if (streak > prefs().getInt("ms_best", 0)) prefs().putInt("ms_best", streak);
    // Every mine gets its flag on a cleared board, so the end looks finished.
    for (int i = 0; i < mns::CELLS; i++)
      if (_b.mine[i]) _b.st[i] = mns::FLAG;
    saveState();
    _view = View::Result;
    host().beep(3);
    host().refresh(true);
    return;
  }
  if (mns::lost(_b)) {
    levelKey(k, sizeof(k), "ms_l", _b.level);
    prefs().putInt(k, prefs().getInt(k, 0) + 1);
    prefs().putInt("ms_strk", 0);
    snprintf(_msg, sizeof(_msg), "a mine - New game to try again");
    saveState();
    host().beep(2);
    host().refresh(true);  // every mine shows at once: a full refresh, no ghosts
    return;
  }
  saveState();
  host().refresh(false);
}

// --- drawing -------------------------------------------------------------------

void MinesApp::renderBoard(ToolsCanvas& c) {
  char buf[40];
  const bool lost = mns::lost(_b);
  for (int k = 0; k < mns::CELLS; k++) {
    const TRect r = cellRect(k);
    const uint8_t st = _b.st[k];
    if (lost && _b.mine[k]) {
      if (_b.boom == k + 1) {  // the one that went off: striped, the mine on white
        gk::stripes(c, r.x, r.y, r.w, r.h);
        c.fillCircle(r.x + r.w / 2, r.y + r.h / 2, 17, false);
        mine(c, r, true);
      } else if (st == mns::FLAG) {  // found: keeps its flag
        c.fillRect(r.x, r.y, r.w, r.h, true);
        flag(c, r, false);
      } else {
        c.fillRect(r.x, r.y, r.w, r.h, true);
        mine(c, r, false);
      }
      continue;
    }
    if (st == mns::OPEN) {
      const int n = mns::count(_b, k);
      if (n) {
        const char s[2] = {(char)('0' + n), 0};
        c.textInBox(r.x, r.y, r.w, r.h, s, TS_HUGE, true, true);
      }
      continue;
    }
    c.fillRect(r.x, r.y, r.w, r.h, true);
    if (st == mns::FLAG) {
      flag(c, r, false);
      if (lost) {  // a flag on a safe square: crossed out
        c.drawLine(r.x + 8, r.y + 8, r.x + r.w - 8, r.y + r.h - 8, 3, false);
        c.drawLine(r.x + r.w - 8, r.y + 8, r.x + 8, r.y + r.h - 8, 3, false);
      }
    }
  }
  // The grid over it: white between black squares, black between white ones,
  // a 3 px frame round the whole.
  for (int i = 1; i < mns::N; i++) {
    for (int k = 0; k < mns::N; k++) {
      // Vertical line between column i-1 and i, row k.
      const int a = k * mns::N + i - 1, b = k * mns::N + i;
      const bool dark = _b.st[a] != mns::OPEN && _b.st[b] != mns::OPEN;
      c.fillRect(GX + i * CELL - 1, GY + k * CELL, 2, CELL, !dark);
      const int a2 = (i - 1) * mns::N + k, b2 = i * mns::N + k;
      const bool dark2 = _b.st[a2] != mns::OPEN && _b.st[b2] != mns::OPEN;
      c.fillRect(GX + k * CELL, GY + i * CELL - 1, CELL, 2, !dark2);
    }
  }
  c.drawRect(GX - 3, GY - 3, GRID + 6, GRID + 6, 3, true);

  snprintf(buf, sizeof(buf), "%d", (int)_b.mines - mns::flags(_b));
  gk::stat(c, statRect(0), "MINES LEFT", buf);
  gk::stat(c, statRect(1), "LEVEL", mns::levelName(_b.level));
  char k[8];
  levelKey(k, sizeof(k), "ms_w", _b.level);
  gk::statInt(c, statRect(2), "WON", prefs().getInt(k, 0));
  c.button(NEW.x, NEW.y, NEW.w, NEW.h, "New game", lost, TS_LARGE);
  if (_msg[0]) c.textCentered(c.width() / 2, MSG_Y, _msg, TS_MED, true, true);
  gk::foot(c, lost                ? "every mine is showing"
              : mns::won(_b) ? "cleared - New game for another"
                             : "tap to dig - hold to flag");
}

void MinesApp::renderResult(ToolsCanvas& c) {
  char w[8], l[8], s[8], line[48], k[8];
  levelKey(k, sizeof(k), "ms_w", _b.level);
  snprintf(w, sizeof(w), "%d", prefs().getInt(k, 0));
  levelKey(k, sizeof(k), "ms_l", _b.level);
  snprintf(l, sizeof(l), "%d", prefs().getInt(k, 0));
  snprintf(s, sizeof(s), "%d", prefs().getInt("ms_strk", 0));
  const gk::Stat st[3] = {{w, "won"}, {l, "lost"}, {s, "in a row"}};
  snprintf(line, sizeof(line), "all %d mines found - %s", _b.mines, mns::levelName(_b.level));
  gk::result(c, "CLEARED", "You won", line, st, 3, "Close", "New game");
  gk::foot(c, "won and lost at this level");
}

int MinesApp::menuRows(gk::Row* rows) {
  int n = 0;
  rows[n++] = {"How to play", nullptr, false};
  rows[n++] = {"New game", nullptr, false};
  rows[n++] = {"Level (next game)", mns::levelName(_level), false};
  bool any = false;
  for (int i = 0; i < mns::LEVELS; i++) {
    char k[8];
    levelKey(k, sizeof(k), "ms_w", i);
    if (prefs().getInt(k, 0)) any = true;
    levelKey(k, sizeof(k), "ms_l", i);
    if (prefs().getInt(k, 0)) any = true;
  }
  if (any) rows[n++] = {"Clear record", nullptr, _armedClear};
  return n;
}

void MinesApp::render(ToolsCanvas& c) {
  if (_view == View::Rules) {
    gk::bar(host(), c, title());
    gk::rules(c, help::MINES);
    return;
  }
  if (_view == View::Menu) {
    gk::Row rows[gk::MENU_MAX];
    gk::menu(host(), c, title(), rows, menuRows(rows));
    return;
  }
  gk::bar(host(), c, title(), mns::lost(_b) ? "boom" : mns::won(_b) ? "cleared" : nullptr);
  if (_view == View::Result) {
    renderResult(c);
    return;
  }
  renderBoard(c);
}

// --- touch ---------------------------------------------------------------------

void MinesApp::tapMenu(int x, int y) {
  gk::Row rows[gk::MENU_MAX];
  const int n = menuRows(rows);
  const int i = gk::menuHit(x, y, n);
  const bool wasArmed = _armedClear;
  _armedClear = false;
  if (i < 0) {
    if (wasArmed) host().refresh(false);
    return;
  }
  const char* what = rows[i].label;
  if (!strcmp(what, "How to play")) {
    _view = View::Rules;
    _rulesFromMore = true;
  } else if (!strcmp(what, "New game")) {
    newGame();
    _view = View::Board;
  } else if (!strncmp(what, "Level", 5)) {
    _level = (_level + 1) % mns::LEVELS;
    prefs().putInt("ms_lvl", _level);
    // An untouched board takes the new level at once; one in play keeps its own.
    if (!_b.laid) newGame();
  } else if (!strcmp(what, "Clear record")) {
    if (!wasArmed) {
      _armedClear = true;
      host().beep(2);
      host().refresh(false);
      return;
    }
    record::clear(prefs(), record::MINES);
    _view = View::Board;
  }
  host().beep(1);
  host().refreshUi();
}

void MinesApp::onTap(int x, int y) {
  if (host().isBackTap(x, y)) {
    host().beep(1);
    if (_view == View::Menu || (_view == View::Rules && _rulesFromMore)) {
      _view = View::Board;
      _armedClear = false;
      host().refreshUi();
      return;
    }
    host().goHub();
    return;
  }
  if (_view == View::Rules) {
    const help::Tap t = help::hit(x, y);
    if (t == help::Tap::None) return;
    if (t == help::Tap::Toggle) {
      host().beep(0);
      host().refreshUi();
      return;
    }
    if (t == help::Tap::Never) help::suppress(prefs(), "mns");
    _view = View::Board;
    _rulesFromMore = false;
    host().beep(1);
    host().refreshUi();
    return;
  }
  if (_view == View::Menu) return tapMenu(x, y);
  if (gk::isMore(host(), x, y)) {
    _view = View::Menu;
    _armedClear = false;
    host().beep(1);
    host().refreshUi();
    return;
  }
  if (_view == View::Result) {
    if (gk::RES_RIGHT.hit(x, y)) {
      host().beep(1);
      newGame();
      _view = View::Board;
      host().refresh(true);
    } else if (gk::RES_LEFT.hit(x, y)) {
      host().beep(1);
      _view = View::Board;
      host().refresh(true);
    }
    return;
  }

  if (NEW.hit(x, y)) {
    host().beep(1);
    newGame();
    host().refresh(true);
    return;
  }
  if (x < GX || x >= GX + GRID || y < GY || y >= GY + GRID) return;
  if (mns::over(_b)) {
    host().beep(2);
    return;
  }
  const int k = (y - GY) / CELL * mns::N + (x - GX) / CELL;
  if (_b.st[k] == mns::FLAG) {  // a flag protects its square from a stray tap
    host().beep(2);
    return;
  }
  uint8_t before[mns::CELLS];
  memcpy(before, _b.st, sizeof(before));
  mns::tap(_b, k, [] { return esp_random(); });
  if (!memcmp(before, _b.st, sizeof(before))) return;  // nothing to dig there
  host().beep(0);
  finish();
}

void MinesApp::onHold(int x, int y) {
  if (_view != View::Board || mns::over(_b)) return;
  if (x < GX || x >= GX + GRID || y < GY || y >= GY + GRID) return;
  const int k = (y - GY) / CELL * mns::N + (x - GX) / CELL;
  if (!mns::toggleFlag(_b, k)) return;
  host().beep(1);
  saveState();
  host().refresh(false);
}
