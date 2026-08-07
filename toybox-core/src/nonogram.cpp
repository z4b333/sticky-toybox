#include "nonogram.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"


NonogramApp nonogramApp;

namespace {
// Portrait: puzzle at the top, controls in a two-column block underneath.
struct Btn {
  int x, y, w, h;
};
constexpr Btn BTN_MODE{24, 520, 200, 62};
constexpr Btn BTN_HINT{256, 520, 200, 62};
constexpr Btn BTN_NEW5{24, 596, 200, 54};
constexpr Btn BTN_NEW10{256, 596, 200, 54};
// To the right of the two lines it erases, clear of the SOLVED banner below.
constexpr Btn BTN_CLEAR{SCREEN_W - 16 - record::BTN_W, 664, record::BTN_W, record::BTN_H};
bool inBtn(int px, int py, const Btn& b) { return inRect(px, py, b.x, b.y, b.w, b.h); }
}  // namespace

void NonogramApp::newPuzzle(int n) {
  _n = n;
  nono::generateSolvable(_n, _sol, _clues, [] { return esp_random(); });
  memset(_cell, 0, sizeof(_cell));
  _won = false;
  _fillMode = true;
  _hintsUsed = 0;
  _startMs = millis();

  if (_n == 5) {
    _cellPx = 60;
    _gridX = 150;
    _gridY = 170;
  } else {
    _cellPx = 32;
    _gridX = 132;
    _gridY = 170;
  }
}

bool NonogramApp::hasRecord() {
  for (const char* k : record::NONOGRAM)
    if (prefs().getUInt(k, 0) || prefs().getInt(k, 0)) return true;
  return false;
}

void NonogramApp::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _armedClear = false;
  _help = !help::suppressed(prefs(), "non");
  newPuzzle(_n ? _n : 10);
}

bool NonogramApp::checkWin() {
  uint8_t clue[MAXG], cnt;
  uint8_t line[MAXN];
  for (int r = 0; r < _n; r++) {
    for (int c = 0; c < _n; c++) line[c] = _cell[r][c] == 1;
    nono::cluesOfLine(line, _n, 1, clue, &cnt);
    if (cnt != _clues.rowN[r] || memcmp(clue, _clues.row[r], cnt) != 0) return false;
  }
  for (int c = 0; c < _n; c++) {
    for (int r = 0; r < _n; r++) line[r] = _cell[r][c] == 1;
    nono::cluesOfLine(line, _n, 1, clue, &cnt);
    if (cnt != _clues.colN[c] || memcmp(clue, _clues.col[c], cnt) != 0) return false;
  }
  return true;
}

void NonogramApp::giveHint() {
  // reveal one wrong/missing cell
  int cand[MAXN * MAXN], nc = 0;
  for (int r = 0; r < _n; r++)
    for (int c = 0; c < _n; c++) {
      const bool shouldFill = _sol[r][c] == 1;
      const bool isFill = _cell[r][c] == 1;
      if (shouldFill != isFill) cand[nc++] = r * MAXN + c;
    }
  if (nc == 0) return;
  const int pick = cand[esp_random() % nc];
  _cell[pick / MAXN][pick % MAXN] = _sol[pick / MAXN][pick % MAXN] ? 1 : 2;
  _hintsUsed++;
  host().beep(1);
}

void NonogramApp::render(ToolsCanvas& c) {
  char buf[40];
  snprintf(buf, sizeof(buf), "NONOGRAM %dx%d", _n, _n);
  host().topBar(buf, true);
  if (_help) {
    help::render(c, help::NONOGRAM);
    return;
  }

  const int gsize = _n * _cellPx;

  // Clues: columns stacked above each column, rows right-aligned to the left
  for (int col = 0; col < _n; col++) {
    const int cx = _gridX + col * _cellPx + _cellPx / 2;
    const int n = _clues.colN[col];
    for (int i = 0; i < n; i++) {
      snprintf(buf, sizeof(buf), "%d", _clues.col[col][i]);
      c.textCentered(cx, _gridY - (n - i) * 20 - 4, buf, TS_MED, true);
    }
  }
  for (int r = 0; r < _n; r++) {
    const int cy = _gridY + r * _cellPx + (_cellPx - 16) / 2;
    int x = _gridX - 8;
    for (int i = _clues.rowN[r] - 1; i >= 0; i--) {
      snprintf(buf, sizeof(buf), "%d", _clues.row[r][i]);
      x -= c.textWidth(buf, TS_MED);
      c.text(x, cy, buf, TS_MED, true);
      x -= 10;
    }
  }

  // Cells
  for (int r = 0; r < _n; r++) {
    for (int col = 0; col < _n; col++) {
      const int x = _gridX + col * _cellPx;
      const int y = _gridY + r * _cellPx;
      if (_cell[r][col] == 1) {
        c.fillRect(x + 2, y + 2, _cellPx - 4, _cellPx - 4, true);
      } else if (_cell[r][col] == 2) {
        const int m = _cellPx / 4;
        const int len = _cellPx - 2 * m;
        for (int t = 0; t < 2; t++)
          for (int d = 0; d < len; d++) {
            c.fillRect(x + m + d, y + m + d + t, 1, 1, true);
            c.fillRect(x + m + len - d, y + m + d + t, 1, 1, true);
          }
      }
    }
  }

  // Grid lines (thick every 5)
  for (int i = 0; i <= _n; i++) {
    const int t = (i % 5 == 0) ? 3 : 1;
    c.fillRect(_gridX + i * _cellPx - t / 2, _gridY, t, gsize, true);
    c.fillRect(_gridX, _gridY + i * _cellPx - t / 2, gsize, t, true);
  }

  // Panel
  c.button(BTN_MODE.x, BTN_MODE.y, BTN_MODE.w, BTN_MODE.h, _fillMode ? "MODE: FILL" : "MODE: X", _fillMode, TS_MED);
  c.button(BTN_HINT.x, BTN_HINT.y, BTN_HINT.w, BTN_HINT.h, "HINT", false, TS_MED);
  c.button(BTN_NEW5.x, BTN_NEW5.y, BTN_NEW5.w, BTN_NEW5.h, "NEW 5x5", false, TS_MED);
  c.button(BTN_NEW10.x, BTN_NEW10.y, BTN_NEW10.w, BTN_NEW10.h, "NEW 10x10", false, TS_MED);

  snprintf(buf, sizeof(buf), "hints used: %d", _hintsUsed);
  c.text(24, 670, buf, TS_MED, true);

  const char* bestKey = (_n == 5) ? "n5_best" : "n10_best";
  const uint32_t best = prefs().getUInt(bestKey, 0);
  if (best) {
    snprintf(buf, sizeof(buf), "best %lus", (unsigned long)best);
    c.text(24, 698, buf, TS_MED, true);
  }
  if (hasRecord())
    c.button(BTN_CLEAR.x, BTN_CLEAR.y, BTN_CLEAR.w, BTN_CLEAR.h, record::label(_armedClear), _armedClear, TS_MED);

  if (_won) {
    snprintf(buf, sizeof(buf), "SOLVED in %lus!", (unsigned long)_elapsed);
    // No rays: at this height they would spray back over the stats line above.
    // The solved picture is the celebration anyway; this only names the time.
    decor::banner(c, 60, 728, SCREEN_W - 120, 56, buf, TS_MED, false);
  }
}

void NonogramApp::onWin() {
  _won = true;
  _elapsed = (millis() - _startMs) / 1000;
  const char* solvedKey = (_n == 5) ? "n5_solved" : "n10_solved";
  const char* bestKey = (_n == 5) ? "n5_best" : "n10_best";
  prefs().putInt(solvedKey, prefs().getInt(solvedKey, 0) + 1);
  const uint32_t best = prefs().getUInt(bestKey, 0);
  if (_hintsUsed == 0 && (best == 0 || _elapsed < best)) prefs().putUInt(bestKey, _elapsed);
  host().beep(3);
  host().refresh(true);
}

void NonogramApp::onTap(int x, int y) {
  if (host().isHelpTap(x, y)) {
    _help = !_help;
    host().beep(1);
    host().refresh(true);
    return;
  }
  if (_help) {
    const help::Tap t = help::hit(x, y);
    if (t == help::Tap::None) return;
    if (t == help::Tap::Never) help::suppress(prefs(), "non");
    _help = false;
    host().beep(1);
    host().refresh(true);
    return;
  }

  if (host().isBackTap(x, y)) {
    host().goHub();
    return;
  }

  const bool wasArmed = _armedClear;
  _armedClear = false;
  if (inBtn(x, y, BTN_CLEAR) && hasRecord()) {
    if (wasArmed) {
      record::clear(prefs(), record::NONOGRAM);
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

  if (inBtn(x, y, BTN_MODE)) {
    _fillMode = !_fillMode;
    host().beep(0);
    host().refresh(false);
    return;
  }
  if (inBtn(x, y, BTN_HINT)) {
    if (!_won) {
      giveHint();
      if (checkWin()) {
        onWin();
      } else {
        host().refresh(false);
      }
    }
    return;
  }
  if (inBtn(x, y, BTN_NEW5)) {
    host().beep(1);
    newPuzzle(5);
    host().refresh(true);
    return;
  }
  if (inBtn(x, y, BTN_NEW10)) {
    host().beep(1);
    newPuzzle(10);
    host().refresh(true);
    return;
  }

  // Grid taps
  const int c = (x - _gridX) / _cellPx;
  const int r = (y - _gridY) / _cellPx;
  if (x >= _gridX && y >= _gridY && c >= 0 && c < _n && r >= 0 && r < _n && !_won) {
    if (_fillMode)
      _cell[r][c] = (_cell[r][c] == 1) ? 0 : 1;
    else
      _cell[r][c] = (_cell[r][c] == 2) ? 0 : 2;
    host().beep(0);
    if (checkWin()) {
      onWin();
    } else {
      host().refresh(false);
    }
  }
}
