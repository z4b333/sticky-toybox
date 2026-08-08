// Sudoku. Tap a cell, tap a digit.
//
// The grid's 48 px cells are 5.1 mm, under a fingertip -- but selecting a cell
// commits nothing and shows you immediately what you picked, so a miss costs
// one more tap. Every action that does commit goes through the number pad,
// whose keys are 9 mm. The small target is deliberately on the reversible half.
#pragma once
#include <esp_random.h>

#include "decor.h"
#include "help.h"
#include "record.h"
#include "sudoku_gen.h"
#include "tools_draw.h"

namespace sdui {
constexpr int CELL = 48;
constexpr int GX = (480 - sud::N * CELL) / 2, GY = 80;

// The pad lost 8 px a key and the whole stack moved up, to open a strip at the
// bottom for the clear. A key is still 68 px -- 7.3 mm, over the fingertip
// minimum -- and it is the only row on this screen that commits anything.
constexpr int PAD_W = 84, PAD_H = 68, PAD_GAP = 10;
constexpr int PAD_X = (480 - (5 * PAD_W + 4 * PAD_GAP)) / 2;
constexpr int PAD_Y1 = 524, PAD_Y2 = PAD_Y1 + PAD_H + PAD_GAP;

constexpr int BTN_Y = 682, BTN_H = 60, BTN_W = 105, BTN_GAP = 10;
constexpr int BTN_X = (480 - (4 * BTN_W + 3 * BTN_GAP)) / 2;

inline constexpr TRect CLEAR_BTN{480 - 16 - record::BTN_W, 752, record::BTN_W, 44};

inline TRect padRect(int i) {
  return TRect{PAD_X + (i % 5) * (PAD_W + PAD_GAP), (i < 5) ? PAD_Y1 : PAD_Y2, PAD_W, PAD_H};
}
inline TRect btnRect(int i) {
  return TRect{BTN_X + i * (BTN_W + BTN_GAP), BTN_Y, BTN_W, BTN_H};
}
}  // namespace sdui

class SudokuTool : public ToolApp {
 public:
  const char* title() const override { return "SUDOKU"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _armedClear = false;
    _help = !help::suppressed(prefs(), "sud");
    _level = (sud::Level)prefs().getInt("sd_lvl", sud::EASY);
    if (_level > sud::HARD) _level = sud::EASY;
    // _started only survives while the tool object lives, and it is destroyed
    // on the way back to the hub. The saved grid is what carries a half-solved
    // puzzle across that, and across a power cycle.
    if (!_started && !loadState()) newPuzzle();
  }

  void render(ToolsCanvas& c) override {
    using namespace sdui;
    host().topBar(title(), true);
    if (_help) {
      help::render(c, help::SUDOKU);
      return;
    }
    char buf[40];

    // --- status ------------------------------------------------------------
    if (_solved) {
      c.text(GX, 56, "SOLVED", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   %d done", sud::levelName(_level), solvedCount());
      c.text(GX + 140, 56, buf, TS_MED, true);
    } else if (_wrong > 0) {
      snprintf(buf, sizeof(buf), "full, but %d %s wrong", _wrong, _wrong == 1 ? "cell is" : "cells are");
      c.text(GX, 56, buf, TS_MED, true, true);
    } else {
      c.text(GX, 56, sud::levelName(_level), TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%d left", sud::remaining(_grid));
      c.text(GX + 200, 56, buf, TS_MED, true);
    }

    // --- grid --------------------------------------------------------------
    for (int i = 0; i <= sud::N; i++) {
      const int t = (i % 3 == 0) ? 3 : 1;
      c.fillRect(GX + i * CELL - t / 2, GY, t, sud::N * CELL + 1, true);
      c.fillRect(GX, GY + i * CELL - t / 2, sud::N * CELL + 1, t, true);
    }

    for (int k = 0; k < sud::CELLS; k++) {
      if (!_grid[k]) continue;
      const int x = GX + sud::colOf(k) * CELL, y = GY + sud::rowOf(k) * CELL;
      const char s[2] = {(char)('0' + _grid[k]), 0};
      // Givens print large and bold, the player's answers a size smaller. With
      // one bit per pixel there is no grey to fall back on, and bold alone is
      // far too subtle at this size -- size is the difference you can see at a
      // glance, and it mirrors the ink-versus-pencil convention on paper.
      const TSize sz = _given[k] ? TS_HUGE : TS_LARGE;
      const int h = _given[k] ? 32 : 24;
      c.textCentered(x + CELL / 2, y + (CELL - h) / 2, s, sz, true, _given[k]);
    }

    if (_sel >= 0) {
      const int x = GX + sud::colOf(_sel) * CELL, y = GY + sud::rowOf(_sel) * CELL;
      c.drawRect(x + 2, y + 2, CELL - 4, CELL - 4, 4, true);
    }

    // --- number pad --------------------------------------------------------
    // Once the puzzle is out there is nothing left to type, so the pad's space
    // goes to saying so instead of showing ten keys that do nothing.
    if (_solved) {
      decor::confetti(c, 20, PAD_Y1 - 10, 440, 2 * PAD_H + PAD_GAP + 20, 17, 18, true);
      decor::banner(c, 70, PAD_Y1 + 30, 340, 76, "SOLVED", TS_HUGE, true);
      drawLevelRow(c);
      return;
    }
    const bool live = _sel >= 0 && !_given[_sel] && !_solved;
    for (int i = 0; i < 10; i++) {
      const TRect r = padRect(i);
      if (i == 9) {
        c.button(r.x, r.y, r.w, r.h, "CLR", false, TS_MED);
      } else {
        char lbl[2] = {(char)('1' + i), 0};
        // A digit already used nine times has nowhere left to go; showing it
        // spent saves the player counting the board.
        c.button(r.x, r.y, r.w, r.h, lbl, live && placed(i + 1) == 9, TS_HUGE);
      }
    }

    drawLevelRow(c);
  }

  void drawLevelRow(ToolsCanvas& c) {
    using namespace sdui;
    // --- difficulty and new -------------------------------------------------
    for (int i = 0; i < 3; i++) {
      const TRect r = btnRect(i);
      c.button(r.x, r.y, r.w, r.h, sud::levelName((sud::Level)i), i == (int)_level, TS_MED);
    }
    // Outlined, not filled: the filled one on this row is the chosen level, and
    // two solid buttons side by side would read as two selections.
    const TRect nb = btnRect(3);
    c.button(nb.x, nb.y, nb.w, nb.h, "NEW", false, TS_MED);

    if (anySolved())
      c.button(CLEAR_BTN.x, CLEAR_BTN.y, CLEAR_BTN.w, CLEAR_BTN.h, record::label(_armedClear),
               _armedClear, TS_MED);
  }

  void onTap(int x, int y) override {
    using namespace sdui;
    if (host().isBackTap(x, y)) {
      host().goHub();
      return;
    }
    if (host().isHelpTap(x, y)) {
      _help = !_help;
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (_help) {
      const help::Tap t = help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Never) help::suppress(prefs(), "sud");
      _help = false;
      host().beep(1);
      host().refresh(true);
      return;
    }

    const bool wasArmed = _armedClear;
    _armedClear = false;
    if (CLEAR_BTN.hit(x, y) && anySolved()) {
      if (wasArmed) {
        record::clear(prefs(), record::SUDOKU);
        host().beep(1);
      } else {
        _armedClear = true;
        host().beep(2);
      }
      host().refresh(false);
      return;
    }
    if (wasArmed) host().refresh(false);

    // Difficulty picks the level for the next puzzle but never wipes this one;
    // NEW is the only thing that throws work away.
    for (int i = 0; i < 3; i++) {
      if (!btnRect(i).hit(x, y)) continue;
      if ((int)_level == i) return;
      _level = (sud::Level)i;
      prefs().putInt("sd_lvl", i);
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (btnRect(3).hit(x, y)) {
      host().beep(1);
      newPuzzle();
      host().refresh(true);
      return;
    }

    if (x >= GX && x < GX + sud::N * CELL && y >= GY && y < GY + sud::N * CELL) {
      const int k = (y - GY) / CELL * sud::N + (x - GX) / CELL;
      if (_given[k]) {
        host().beep(2);
        return;
      }
      _sel = (_sel == k) ? -1 : k;  // tapping the selected cell again clears it
      host().beep(0);
      host().refresh(false);
      return;
    }

    if (_sel < 0 || _given[_sel] || _solved) return;
    for (int i = 0; i < 10; i++) {
      if (!padRect(i).hit(x, y)) continue;
      const uint8_t v = (i == 9) ? 0 : (uint8_t)(i + 1);
      if (_grid[_sel] == v) return;
      _grid[_sel] = v;
      host().beep(v ? 0 : 1);
      judge();
      saveState();
      host().refresh(_solved);
      return;
    }
  }

 private:
  int placed(int digit) const {
    int n = 0;
    for (int k = 0; k < sud::CELLS; k++)
      if (_grid[k] == digit) n++;
    return n;
  }

  bool anySolved() {
    for (int i = 0; i < sud::LEVELS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "sd_w%d", i);
      if (prefs().getInt(key, 0) > 0) return true;
    }
    return false;
  }

  int solvedCount() {
    char key[8];
    snprintf(key, sizeof(key), "sd_w%d", (int)_level);
    return prefs().getInt(key, 0);
  }

  // With no per-entry error checking, the grid filling up is the only moment
  // the player gets told anything. Saying how many cells are wrong -- but not
  // which -- ends the puzzle honestly without handing back the answer.
  void judge() {
    _wrong = 0;
    _solved = false;
    if (!sud::complete(_grid)) return;
    _wrong = sud::wrongCount(_grid, _solution);
    if (_wrong > 0) return;
    _solved = true;
    char key[8];
    snprintf(key, sizeof(key), "sd_w%d", (int)_level);
    prefs().putInt(key, prefs().getInt(key, 0) + 1);
  }

  void newPuzzle() {
    sud::generate(_grid, _solution, _level, [] { return esp_random(); });
    for (int k = 0; k < sud::CELLS; k++) _given[k] = _grid[k] != 0;
    _sel = -1;
    _wrong = 0;
    _solved = false;
    _started = true;
    saveState();
  }

  // A generated puzzle takes real work to produce and longer still to solve, so
  // it is written out whenever a digit changes. NEW always generates instead.
  void saveState() {
    struct Saved {
      uint8_t grid[sud::CELLS];
      uint8_t solution[sud::CELLS];
      uint8_t given[sud::CELLS];
      uint8_t level, solved, pad[2];
    } s;
    memcpy(s.grid, _grid, sizeof(s.grid));
    memcpy(s.solution, _solution, sizeof(s.solution));
    for (int i = 0; i < sud::CELLS; i++) s.given[i] = _given[i] ? 1 : 0;
    s.level = (uint8_t)_level;
    s.solved = _solved ? 1 : 0;
    s.pad[0] = s.pad[1] = 0;
    prefs().putBytes("sd_state", &s, sizeof(s));
  }

  bool loadState() {
    struct Saved {
      uint8_t grid[sud::CELLS];
      uint8_t solution[sud::CELLS];
      uint8_t given[sud::CELLS];
      uint8_t level, solved, pad[2];
    } s;
    if (prefs().getBytesLength("sd_state") != sizeof(s)) return false;
    if (prefs().getBytes("sd_state", &s, sizeof(s)) != sizeof(s)) return false;
    if (s.solved) return false;  // a finished board is not worth coming back to
    memcpy(_grid, s.grid, sizeof(_grid));
    memcpy(_solution, s.solution, sizeof(_solution));
    for (int i = 0; i < sud::CELLS; i++) _given[i] = s.given[i] != 0;
    _level = (sud::Level)(s.level <= sud::HARD ? s.level : sud::EASY);
    _solved = false;
    _started = true;
    _sel = -1;
    _wrong = 0;
    return true;
  }

  uint8_t _grid[sud::CELLS] = {};
  uint8_t _solution[sud::CELLS] = {};
  bool _given[sud::CELLS] = {};
  int _sel = -1;
  int _wrong = 0;
#ifdef TOYBOX_HOST
 public:
  void hostSolve() {
    memcpy(_grid, _solution, sizeof(_grid));
    _sel = -1;
    judge();
  }

 private:
#endif
  bool _help = false;
  bool _armedClear = false;
  bool _solved = false, _started = false;
  sud::Level _level = sud::EASY;
};
