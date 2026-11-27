#include "game2048.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"


G2048App g2048App;

namespace {
int tileValue(uint8_t e) { return e ? (1 << e) : 0; }

// A dashed outline: the tile that has just appeared, and (thinner) an empty
// square. Same stroke weight as a solid border on a new tile so it does not
// read as a smaller tile -- only as a newer one.
void dashedRect(ToolsCanvas& c, int x, int y, int w, int h, int t, int dash, int gap) {
  const int step = dash + gap;
  for (int i = 0; i < w; i += step) {
    const int len = min(dash, w - i);
    c.fillRect(x + i, y, len, t, true);
    c.fillRect(x + i, y + h - t, len, t, true);
  }
  for (int i = 0; i < h; i += step) {
    const int len = min(dash, h - i);
    c.fillRect(x, y + i, t, len, true);
    c.fillRect(x + w - t, y + i, t, len, true);
  }
}
}  // namespace

void G2048App::newGame() {
  memset(_b, 0, sizeof(_b));
  memset(_merged, 0, sizeof(_merged));
  _newCell = -1;
  _score = 0;
  _over = false;
  _canUndo = false;
  _reached2048 = _cheered = false;
  spawn();
  spawn();
}

void G2048App::spawn() {
  int empty[16], n = 0;
  for (int i = 0; i < 16; i++)
    if (!_b[i / 4][i % 4]) empty[n++] = i;
  if (!n) return;
  const int p = empty[esp_random() % n];
  _b[p / 4][p % 4] = (esp_random() % 10 == 0) ? 2 : 1;
  _newCell = (int8_t)p;
}

bool G2048App::move(int dir) {
  uint8_t nb[4][4];
  memcpy(nb, _b, sizeof(nb));
  memset(_merged, 0, sizeof(_merged));
  _newCell = -1;
  int gained = 0;
  bool changed = false;

  // Where a slot in the sliding line actually lives on the board, so a merge
  // can be recorded against the square the player will look at.
  auto cellOf = [&](int line, int i) -> int {
    switch (dir) {
      case 0: return line * 4 + i;
      case 1: return line * 4 + (3 - i);
      case 2: return i * 4 + line;
      default: return (3 - i) * 4 + line;
    }
  };

  auto get = [&](int line, int i) -> uint8_t& {
    switch (dir) {
      case 0: return nb[line][i];          // left
      case 1: return nb[line][3 - i];      // right
      case 2: return nb[i][line];          // up
      default: return nb[3 - i][line];     // down
    }
  };

  for (int line = 0; line < 4; line++) {
    uint8_t vals[4], m = 0;
    for (int i = 0; i < 4; i++)
      if (get(line, i)) vals[m++] = get(line, i);
    uint8_t out[4] = {};
    bool madeHere[4] = {};
    int o = 0;
    for (int i = 0; i < m; i++) {
      if (i + 1 < m && vals[i] == vals[i + 1]) {
        out[o] = vals[i] + 1;
        gained += tileValue(out[o]);
        if (out[o] >= 11) _reached2048 = true;
        madeHere[o] = true;
        o++;
        i++;
      } else {
        out[o++] = vals[i];
      }
    }
    for (int i = 0; i < 4; i++) {
      if (get(line, i) != out[i]) changed = true;
      get(line, i) = out[i];
      if (madeHere[i]) _merged[cellOf(line, i)] = true;
    }
  }

  if (!changed) return false;
  memcpy(_prevB, _b, sizeof(_b));
  _prevScore = _score;
  _canUndo = true;
  memcpy(_b, nb, sizeof(nb));
  _score += gained;
  spawn();
  if (_score > prefs().getInt("t_best", 0)) prefs().putInt("t_best", _score);
  saveState();
  int maxTile = 0;
  for (int i = 0; i < 16; i++) maxTile = max(maxTile, tileValue(_b[i / 4][i % 4]));
  if (maxTile > prefs().getInt("t_tile", 0)) prefs().putInt("t_tile", maxTile);
  if (!anyMoves()) _over = true;
  return true;
}

bool G2048App::anyMoves() const {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      if (!_b[r][c]) return true;
      if (c < 3 && _b[r][c] == _b[r][c + 1]) return true;
      if (r < 3 && _b[r][c] == _b[r + 1][c]) return true;
    }
  return false;
}

// A half-finished 2048 board is worth keeping: the game is long, and leaving to
// answer the door should not cost it. The board, the score and the undo slot go
// into NVS as one blob whenever the position changes, and come back on the way
// in. NEW GAME still starts from nothing -- restoring is what happens when you
// did not ask for anything.
void G2048App::saveState() {
  struct Saved {
    uint8_t b[4][4], prev[4][4];
    int32_t score, prevScore;
    uint8_t canUndo, over, reached, pad[2];
  } s;
  memcpy(s.b, _b, sizeof(s.b));
  memcpy(s.prev, _prevB, sizeof(s.prev));
  s.score = _score;
  s.prevScore = _prevScore;
  s.canUndo = _canUndo;
  s.over = _over;
  s.reached = _reached2048;
  s.pad[0] = s.pad[1] = 0;
  prefs().putBytes("t_state", &s, sizeof(s));
}

bool G2048App::loadState() {
  struct Saved {
    uint8_t b[4][4], prev[4][4];
    int32_t score, prevScore;
    uint8_t canUndo, over, reached, pad[2];
  } s;
  if (prefs().getBytesLength("t_state") != sizeof(s)) return false;
  if (prefs().getBytes("t_state", &s, sizeof(s)) != sizeof(s)) return false;
  bool any = false;
  for (int r = 0; r < 4; r++)
    for (int col = 0; col < 4; col++)
      if (s.b[r][col]) any = true;
  if (!any) return false;  // an empty board is not worth restoring
  memcpy(_b, s.b, sizeof(_b));
  memcpy(_prevB, s.prev, sizeof(_prevB));
  _score = s.score;
  _prevScore = s.prevScore;
  _canUndo = s.canUndo != 0;
  _over = s.over != 0;
  _reached2048 = s.reached != 0;
  _cheered = _reached2048;  // do not re-celebrate a milestone from last session
  // Nothing gets called out on a restored board: the dashed edge and the merge
  // flash mean "this changed just now", and nothing did.
  _newCell = -1;
  memset(_merged, 0, sizeof(_merged));
  return true;
}


// --- screens -------------------------------------------------------------------

void G2048App::renderBoard(ToolsCanvas& c) {
  using namespace g2048ui;
  int maxTile = 0;
  for (int i = 0; i < 16; i++) maxTile = max(maxTile, tileValue(_b[i / 4][i % 4]));
  gk::statInt(c, SCORE, "SCORE", _score);
  gk::statInt(c, BEST, "BEST TILE", max(maxTile, (int)prefs().getInt("t_tile", 0)));

  c.drawRect(BX, BY, BOARD, BOARD, FRAME, true);
  char buf[12];
  for (int r = 0; r < 4; r++) {
    for (int col = 0; col < 4; col++) {
      const int x = BX + FRAME + PAD + col * (TILE + GAP);
      const int y = BY + FRAME + PAD + r * (TILE + GAP);
      const uint8_t e = _b[r][col];
      if (!e) {
        dashedRect(c, x, y, TILE, TILE, 1, 6, 6);
        continue;
      }
      const int idx = r * 4 + col;
      const int v = tileValue(e);
      snprintf(buf, sizeof(buf), "%d", v);
      // Kit: 2-8 plain white, 16-64 striped with the number on a white patch,
      // 128 and up solid black. Three steps a glance can tell apart.
      const bool solid = e >= 7;
      const bool striped = e >= 4 && e < 7;
      const TSize sz = v >= 1000 ? TS_MED : v >= 100 ? TS_LARGE : TS_HUGE;
      if (solid) {
        c.fillRect(x, y, TILE, TILE, true);
        c.textInBox(x, y, TILE, TILE, buf, sz, false, true);
      } else {
        if (striped) gk::stripes(c, x, y, TILE, TILE);
        if (idx == _newCell)
          dashedRect(c, x, y, TILE, TILE, 3, 9, 7);
        else
          c.drawRect(x, y, TILE, TILE, 2, true);
        if (striped) {
          const int tw = c.textWidth(buf, sz, true) + 14, th = c.textHeight(sz) + 2;
          c.fillRect(x + (TILE - tw) / 2, y + (TILE - th) / 2, tw, th, false);
        }
        c.textInBox(x, y, TILE, TILE, buf, sz, true, true);
      }
      // A wedge in the top-left corner on whatever just merged, in the colour
      // opposite the tile. It stays until the next move rather than blinking:
      // on this panel what happened has to be in how the board is drawn.
      if (_merged[idx]) {
        for (int k = 0; k < 22; k++) c.fillRect(x + 3, y + 3 + k, 22 - k, 1, !solid);
      }
    }
  }

  c.button(UNDO.x, UNDO.y, UNDO.w, UNDO.h, "Undo", false, TS_LARGE);
  c.button(NEW.x, NEW.y, NEW.w, NEW.h, "New game", false, TS_LARGE);
  gk::foot(c, _over ? "no moves left - New game" : "swipe - or UP / DOWN");
}

void G2048App::renderResult(ToolsCanvas& c) {
  char score[16], tile[16], best[16];
  int maxTile = 0;
  for (int i = 0; i < 16; i++) maxTile = max(maxTile, tileValue(_b[i / 4][i % 4]));
  snprintf(score, sizeof(score), "%d", _score);
  snprintf(tile, sizeof(tile), "%d", maxTile);
  snprintf(best, sizeof(best), "%d", prefs().getInt("t_best", 0));
  const gk::Stat st[3] = {{score, "score"}, {tile, "best tile"}, {best, "best score"}};
  if (_over)
    gk::result(c, "GAME OVER", "No moves left", "the board is full", st, 3, "Close", "New game");
  else
    gk::result(c, "YOU MADE IT", "2048!", "keep going for a bigger tile", st, 3, "Keep going",
               "New game");
}

int G2048App::menuRows(gk::Row* rows) {
  int n = 0;
  rows[n++] = {"How to play", nullptr, false};
  rows[n++] = {"New game", nullptr, false};
  if (prefs().getInt("t_best", 0) > 0) rows[n++] = {"Clear record", nullptr, _armedClear};
  return n;
}

void G2048App::render(ToolsCanvas& c) {
  if (_view == View::Rules) {
    gk::bar(host(), c, "2048");
    gk::rules(c, help::G2048);
    return;
  }
  if (_view == View::Menu) {
    gk::Row rows[gk::MENU_MAX];
    gk::menu(host(), c, "2048", rows, menuRows(rows));
    return;
  }
  gk::bar(host(), c, "2048");
  if (_result) {
    renderResult(c);
    return;
  }
  renderBoard(c);
}

void G2048App::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _rulesFromMore = false;
  _armedClear = false;
  _result = false;
  _view = help::suppressed(prefs(), "g20") ? View::Board : View::Rules;
  if (!loadState()) newGame();
}

void G2048App::slide(int dir) {
  if (_over || _result || _view != View::Board) return;
  if (!move(dir)) return;
  if (_reached2048 && !_cheered) {
    _cheered = true;
    _result = true;
    host().beep(3);
    host().refresh(true);
    return;
  }
  host().beep(0);
  if (_over) {
    host().beep(2);
    _result = true;
    host().refresh(true);
    return;
  }
  host().refresh(false);
}

void G2048App::onSwipe(int dx, int dy) {
  slide((abs(dx) >= abs(dy)) ? (dx > 0 ? 1 : 0) : (dy > 0 ? 3 : 2));
}

bool G2048App::onButton(SideBtn b) {
  if (b == SideBtn::Ok || _view != View::Board || _result) return false;
  slide(b == SideBtn::Up ? 2 : 3);
  return true;
}

void G2048App::onTap(int x, int y) {
  using namespace g2048ui;
  // Back comes before everything: a card or a menu that swallows it leaves the
  // only way out of the app behind a button you have to find first.
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
    if (t == help::Tap::Never) help::suppress(prefs(), "g20");
    _view = View::Board;
    _rulesFromMore = false;
    host().beep(1);
    host().refreshUi();
    return;
  }
  if (_view == View::Menu) {
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
      saveState();
      _result = false;
      _view = View::Board;
    } else if (!strcmp(what, "Clear record")) {
      if (!wasArmed) {
        _armedClear = true;
        host().beep(2);
        host().refresh(false);
        return;
      }
      record::clear(prefs(), record::G2048);
      _view = View::Board;
    }
    host().beep(1);
    host().refreshUi();
    return;
  }
  if (gk::isMore(host(), x, y)) {
    _view = View::Menu;
    _armedClear = false;
    host().beep(1);
    host().refreshUi();
    return;
  }

  if (_result) {
    if (gk::RES_RIGHT.hit(x, y)) {
      host().beep(1);
      newGame();
      saveState();  // the fresh board is now the one to come back to
      _result = false;
      host().refresh(true);
    } else if (gk::RES_LEFT.hit(x, y)) {
      host().beep(1);
      _result = false;
      host().refresh(true);
    }
    return;
  }

  if (NEW.hit(x, y)) {
    host().beep(1);
    newGame();
    saveState();
    host().refresh(true);
    return;
  }
  if (UNDO.hit(x, y)) {
    if (!_canUndo) {
      host().beep(2);
      return;
    }
    host().beep(1);
    memcpy(_b, _prevB, sizeof(_b));
    _score = _prevScore;
    _canUndo = false;
    _over = false;
    _newCell = -1;
    memset(_merged, 0, sizeof(_merged));
    saveState();
    host().refresh(false);
  }
}
