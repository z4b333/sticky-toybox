#include "game2048.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"


G2048App g2048App;

namespace {
// Portrait: the 4x4 board fills the width, score and controls sit beneath it.
constexpr int TILE = 100, TGAP = 10;
constexpr int BOARD = 4 * TILE + 5 * TGAP;      // 450
constexpr int BX = (SCREEN_W - BOARD) / 2;         // 15
constexpr int BY = TOPBAR_H + 20;               // 60

struct Btn {
  int x, y, w, h;
};
constexpr Btn BTN_NEW{40, 660, 180, 60};
constexpr Btn BTN_UNDO{260, 660, 180, 60};
// Under the controls, on the same side of the panel as BEST.
constexpr Btn BTN_CLEAR{SCREEN_W - 16 - record::BTN_W, 736, record::BTN_W, record::BTN_H};

int tileValue(uint8_t e) { return e ? (1 << e) : 0; }

// A dashed outline for the tile that has just appeared. Same stroke weight as a
// solid border so it does not read as a smaller tile -- only as a newer one.
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
  _blinkUntil = 0;
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

void G2048App::render(ToolsCanvas& c) {
  host().topBar("2048", true);
  if (_help) {
    help::render(c, help::G2048);
    return;
  }

  // Board frame
  c.drawRect(BX - 4, BY - 4, BOARD + 8, BOARD + 8, 3, true);

  char buf[24];
  for (int r = 0; r < 4; r++) {
    for (int col = 0; col < 4; col++) {
      const int x = BX + TGAP + col * (TILE + TGAP);
      const int y = BY + TGAP + r * (TILE + TGAP);
      const uint8_t e = _b[r][col];
      if (!e) {
        c.drawRect(x, y, TILE, TILE, 1, true);
        continue;
      }
      const int idx = r * 4 + col;
      const bool isNew = (idx == _newCell);
      // While the flash is up, a tile that just merged is drawn in the opposite
      // style to its neighbours. Inverting rather than adding a marker means it
      // reads at a glance whatever its value, and costs no extra space.
      const bool flash = _blinkUntil != 0 && _merged[idx];
      const bool solid = (e >= 7) != flash;  // 128+ is normally solid

      const int v = tileValue(e);
      snprintf(buf, sizeof(buf), "%d", v);
      if (solid) {
        c.fillRect(x, y, TILE, TILE, true);
        const TSize sc = (v >= 1000) ? TS_MED : TS_LARGE;
        c.textCentered(x + TILE / 2, y + (TILE - c.textHeight(sc)) / 2, buf, sc, false);
      } else {
        const int t = 1 + (e > 2) + (e > 4);  // border weight grows with the tile
        if (isNew)
          dashedRect(c, x, y, TILE, TILE, t + 1, 9, 7);
        else
          c.drawRect(x, y, TILE, TILE, t, true);
        const TSize sc = (v >= 100) ? TS_LARGE : TS_HUGE;
        c.textCentered(x + TILE / 2, y + (TILE - c.textHeight(sc)) / 2, buf, sc, true);
      }
    }
  }

  // Panel
  c.text(BX + 6, BY + BOARD + 24, "SCORE", TS_MED, true);
  snprintf(buf, sizeof(buf), "%d", _score);
  c.text(BX + 6, BY + BOARD + 48, buf, TS_HUGE, true);
  c.text(BX + 250, BY + BOARD + 24, "BEST", TS_MED, true);
  snprintf(buf, sizeof(buf), "%d", prefs().getInt("t_best", 0));
  c.text(BX + 250, BY + BOARD + 48, buf, TS_HUGE, true);

  c.textCentered(SCREEN_W / 2, BY + BOARD + 108, "swipe the board to move tiles", TS_MED, true);

  c.button(BTN_NEW.x, BTN_NEW.y, BTN_NEW.w, BTN_NEW.h, "NEW GAME", false, TS_MED);
  c.button(BTN_UNDO.x, BTN_UNDO.y, BTN_UNDO.w, BTN_UNDO.h, "UNDO", false, TS_MED);
  if (prefs().getInt("t_best", 0) > 0)
    c.button(BTN_CLEAR.x, BTN_CLEAR.y, BTN_CLEAR.w, BTN_CLEAR.h, record::label(_armedClear), _armedClear, TS_MED);

  if (_over) {
    decor::banner(c, BX + 40, BY + BOARD / 2 - 40, BOARD - 80, 80, "GAME OVER", TS_HUGE, false);
  } else if (_reached2048 && !_cheered) {
    decor::confetti(c, BX + 20, BY + 40, BOARD - 40, BOARD - 80, 5, 20, true);
    decor::banner(c, BX + 40, BY + BOARD / 2 - 40, BOARD - 80, 80, "2048!", TS_HUGE, true);
  }
}

void G2048App::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _armedClear = false;
  _help = !help::suppressed(prefs(), "g20");
  newGame();
}

// The merge flash lives for one beat and then the board settles. Doing it this
// way round -- highlight first, plain a moment later -- keeps the swipe feeling
// immediate; blinking the other way would put an e-ink refresh between the
// gesture and its result.
constexpr uint32_t BLINK_MS = 450;

void G2048App::tick() {
  if (!_blinkUntil || millis() < _blinkUntil) return;
  _blinkUntil = 0;
  host().refresh(false);
}

void G2048App::onSwipe(int dx, int dy) {
  if (_over) return;
  const int dir = (abs(dx) >= abs(dy)) ? (dx > 0 ? 1 : 0) : (dy > 0 ? 3 : 2);
  if (move(dir)) {
    bool anyMerge = false;
    for (int i = 0; i < 16; i++) anyMerge = anyMerge || _merged[i];
    _blinkUntil = anyMerge ? millis() + BLINK_MS : 0;
    if (_reached2048 && !_cheered) {
      host().beep(3);
      host().refresh(false);
      _cheered = true;
      return;
    }
    host().beep(0);
    if (_over) host().beep(2);
    host().refresh(false);
  }
}

void G2048App::onTap(int x, int y) {
  if (host().isHelpTap(x, y)) {
    _help = !_help;
    host().beep(1);
    host().refresh(true);
    return;
  }
  if (_help) {
    const help::Tap t = help::hit(x, y);
    if (t == help::Tap::None) return;
    if (t == help::Tap::Never) help::suppress(prefs(), "g20");
    _help = false;
    host().beep(1);
    host().refresh(true);
    return;
  }

  if (host().isBackTap(x, y)) {
    host().goHub();
    return;
  }

  // BEST is the only thing on this screen that outlives the game, so clearing
  // it is offered here and armed by a first tap.
  const bool wasArmed = _armedClear;
  _armedClear = false;
  if (inRect(x, y, BTN_CLEAR.x, BTN_CLEAR.y, BTN_CLEAR.w, BTN_CLEAR.h) &&
      prefs().getInt("t_best", 0) > 0) {
    if (wasArmed) {
      record::clear(prefs(), record::G2048);
      host().beep(1);
    } else {
      _armedClear = true;
      host().beep(2);
    }
    host().refresh(false);
    return;
  }
  if (wasArmed) {
    host().refresh(false);
  }

  if (inRect(x, y, BTN_NEW.x, BTN_NEW.y, BTN_NEW.w, BTN_NEW.h)) {
    host().beep(1);
    newGame();
    host().refresh(true);
    return;
  }
  if (inRect(x, y, BTN_UNDO.x, BTN_UNDO.y, BTN_UNDO.w, BTN_UNDO.h) && _canUndo) {
    host().beep(1);
    memcpy(_b, _prevB, sizeof(_b));
    _score = _prevScore;
    _canUndo = false;
    _over = false;
    host().refresh(false);
    return;
  }
  // Tap on the 2048 banner dismisses it
  if (_reached2048 && !_cheered) {
    _cheered = true;
    host().refresh(false);
  }
}
