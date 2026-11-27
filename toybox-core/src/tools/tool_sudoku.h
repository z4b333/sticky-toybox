// Sudoku (v2, kit "Sudoku"). Tap a cell, tap a digit.
//
// The grid fills the panel's width: 50 px cells, 5.3 mm, still under a
// fingertip -- but selecting a cell commits nothing and shows at once what
// you picked, so a miss costs one more tap. Everything that commits goes
// through the row of keys under it. Givens are bold and upright, your digits
// slanted, pencil notes small in the cell's corners. Check marks the cells
// that are wrong; level, new puzzle and the rules live in More.
#pragma once
#include <esp_random.h>

#include "decor.h"
#include "game_kit.h"
#include "help.h"
#include "record.h"
#include "sudoku_gen.h"
#include "tools_draw.h"

namespace sdui {
constexpr int CELL = 50;
constexpr int GX = (480 - sud::N * CELL) / 2, GY = 60;  // 15, 60
constexpr int GRID = sud::N * CELL;                      // 450

constexpr int KEY_W = 48, KEY_H = 66, KEY_GAP = 4;
constexpr int KEY_X = (480 - (9 * KEY_W + 8 * KEY_GAP)) / 2;  // 8
constexpr int KEY_Y = GY + GRID + 18;                         // 520

constexpr int BTN_Y = KEY_Y + KEY_H + 14, BTN_H = 64, BTN_W = 140, BTN_GAP = 10;
constexpr int BTN_X = (480 - (3 * BTN_W + 2 * BTN_GAP)) / 2;  // 20
constexpr int MSG_Y = BTN_Y + BTN_H + 22;

inline TRect keyRect(int i) { return TRect{KEY_X + i * (KEY_W + KEY_GAP), KEY_Y, KEY_W, KEY_H}; }
inline TRect btnRect(int i) { return TRect{BTN_X + i * (BTN_W + BTN_GAP), BTN_Y, BTN_W, BTN_H}; }
inline constexpr int B_NOTES = 0, B_ERASE = 1, B_CHECK = 2;
inline TRect cellRect(int k) {
  return TRect{GX + sud::colOf(k) * CELL, GY + sud::rowOf(k) * CELL, CELL, CELL};
}
}  // namespace sdui

class SudokuTool : public ToolApp {
 public:
  const char* title() const override { return "SUDOKU"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _rulesFromMore = false;
    _armedClear = false;
    _view = help::suppressed(prefs(), "sud") ? View::Board : View::Rules;
    _level = (sud::Level)prefs().getInt("sd_lvl", sud::EASY);
    if (_level > sud::HARD) _level = sud::EASY;
    // The saved grid is what carries a half-solved puzzle across leaving and
    // a power cycle; _started only lives as long as this object.
    if (!_started && !loadState()) newPuzzle();
  }

  void render(ToolsCanvas& c) override {
    using namespace sdui;
    if (_view == View::Rules) {
      gk::bar(host(), c, title());
      gk::rules(c, help::SUDOKU);
      return;
    }
    if (_view == View::Menu) {
      gk::Row rows[gk::MENU_MAX];
      gk::menu(host(), c, title(), rows, menuRows(rows));
      return;
    }
    char buf[48];
    if (_view == View::Result) {
      gk::bar(host(), c, title());
      char n0[8], n1[8], n2[8];
      snprintf(n0, sizeof(n0), "%d", solvedAt(0));
      snprintf(n1, sizeof(n1), "%d", solvedAt(1));
      snprintf(n2, sizeof(n2), "%d", solvedAt(2));
      const gk::Stat st[3] = {{n0, "easy"}, {n1, "medium"}, {n2, "hard"}};
      snprintf(buf, sizeof(buf), "SUDOKU - %s", sud::levelName(_puzzleLevel));
      gk::result(c, buf, "Solved", "every row, column and box", st, 3, "Close", "New puzzle");
      gk::foot(c, "solved puzzles by level");
      return;
    }

    snprintf(buf, sizeof(buf), "%d left", sud::remaining(_grid));
    gk::bar(host(), c, title(), _solved ? "solved" : buf);

    // --- grid --------------------------------------------------------------
    for (int k = 0; k < sud::CELLS; k++) {
      const TRect r = cellRect(k);
      const bool sel = k == _sel;
      if (sel) c.fillRect(r.x, r.y, CELL, CELL, true);
      if (_grid[k]) {
        const char s[2] = {(char)('0' + _grid[k]), 0};
        // Kit: bold = given, slanted = yours. Same size for both, so a full
        // row reads as one line of digits.
        int l, rr, t, b;
        c.textPad(s, TS_HUGE, l, rr, t, b);
        const int tw = c.textWidth(s, TS_HUGE, _given[k], !_given[k]);
        const int ty = r.y + (CELL - c.textHeight(TS_HUGE)) / 2 + (b - t) / 2;
        c.text(r.x + (CELL - tw) / 2 + (rr - l) / 2, ty, s, TS_HUGE, !sel, _given[k], !_given[k]);
        if (_marked[k] && _grid[k] != _solution[k]) {
          // Check found this one wrong: a stroke through it, corner to corner.
          c.drawLine(r.x + 6, r.y + CELL - 6, r.x + CELL - 6, r.y + 6, 3, !sel);
        }
      } else if (_notes[k]) {
        // Pencil notes: each digit in its own third of the cell, 1 top-left
        // to 9 bottom-right, the way they are written on paper.
        for (int d = 1; d <= 9; d++) {
          if (!(_notes[k] & (1 << d))) continue;
          const char s[2] = {(char)('0' + d), 0};
          const int sx = r.x + ((d - 1) % 3) * (CELL / 3) + CELL / 6;
          const int sy = r.y + ((d - 1) / 3) * (CELL / 3) + 1;
          c.textCentered(sx, sy, s, TS_SMALL, !sel);
        }
      }
    }
    for (int i = 0; i <= sud::N; i++) {
      const int t = (i % 3 == 0) ? 3 : 1;
      c.fillRect(GX + i * CELL - t / 2, GY, t, GRID + 1, true);
      c.fillRect(GX, GY + i * CELL - t / 2, GRID + 1, t, true);
    }

    // --- keys --------------------------------------------------------------
    const int selDigit = (_sel >= 0) ? _grid[_sel] : 0;
    for (int i = 0; i < 9; i++) {
      const TRect r = keyRect(i);
      const char lbl[2] = {(char)('1' + i), 0};
      if (selDigit == i + 1) {  // the digit in the selected cell
        c.fillRect(r.x, r.y, r.w, r.h, true);
        c.textInBox(r.x, r.y, r.w, r.h, lbl, TS_LARGE, false, true);
      } else if (placed(i + 1) >= 9) {  // spent: all nine are out
        c.drawRect(r.x, r.y, r.w, r.h, 1, true);
        c.textInBox(r.x, r.y, r.w, r.h, lbl, TS_LARGE, true);
        c.fillRect(r.x + 10, r.y + r.h / 2 - 1, r.w - 20, 2, true);
      } else {
        c.drawRect(r.x, r.y, r.w, r.h, 2, true);
        c.textInBox(r.x, r.y, r.w, r.h, lbl, TS_LARGE, true, true);
      }
    }
    gk::choice(c, btnRect(B_NOTES), _notesMode ? "Notes on" : "Notes", _notesMode, TS_LARGE);
    const TRect e = btnRect(B_ERASE), k = btnRect(B_CHECK);
    c.button(e.x, e.y, e.w, e.h, "Erase", false, TS_LARGE);
    c.button(k.x, k.y, k.w, k.h, "Check", false, TS_LARGE);

    if (_msg[0]) c.textCentered(c.width() / 2, MSG_Y, _msg, TS_MED, true, true);
    snprintf(buf, sizeof(buf), "%s - bold = given, slanted = yours",
             _puzzleLevel == sud::EASY ? "easy" : _puzzleLevel == sud::MEDIUM ? "medium" : "hard");
    gk::foot(c, buf);
  }

  void onTap(int x, int y) override {
    using namespace sdui;
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
      if (t == help::Tap::Never) help::suppress(prefs(), "sud");
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
        newPuzzle();
        _view = View::Board;
        host().refresh(true);
      } else if (gk::RES_LEFT.hit(x, y)) {
        host().beep(1);
        _view = View::Board;
        host().refresh(true);
      }
      return;
    }

    if (x >= GX && x < GX + GRID && y >= GY && y < GY + GRID) {
      const int k = (y - GY) / CELL * sud::N + (x - GX) / CELL;
      if (_given[k]) {
        host().beep(2);
        return;
      }
      // Re-tapping the selected cell keeps it: on a slow panel a second tap
      // from doubt is normal, and deselecting would make the next key do
      // nothing.
      if (_sel == k) return;
      _sel = k;
      _msg[0] = 0;
      host().beep(0);
      host().refresh(false);
      return;
    }

    if (btnRect(B_NOTES).hit(x, y)) {
      _notesMode = !_notesMode;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (btnRect(B_CHECK).hit(x, y)) {
      check();
      return;
    }
    if (_solved) return;
    if (btnRect(B_ERASE).hit(x, y)) {
      if (_sel < 0 || _given[_sel] || (!_grid[_sel] && !_notes[_sel])) {
        host().beep(2);
        return;
      }
      _grid[_sel] = 0;
      _notes[_sel] = 0;
      _marked[_sel] = false;
      _msg[0] = 0;
      host().beep(1);
      saveState();
      host().refresh(false);
      return;
    }
    for (int i = 0; i < 9; i++) {
      if (!keyRect(i).hit(x, y)) continue;
      if (_sel < 0 || _given[_sel]) {
        host().beep(2);
        return;
      }
      const uint8_t v = (uint8_t)(i + 1);
      _msg[0] = 0;
      if (_notesMode) {
        if (_grid[_sel]) {  // notes are for empty cells
          host().beep(2);
          return;
        }
        _notes[_sel] ^= (uint16_t)(1 << v);
        host().beep(0);
        saveState();
        host().refresh(false);
        return;
      }
      if (_grid[_sel] == v) return;
      _grid[_sel] = v;
      _notes[_sel] = 0;
      _marked[_sel] = false;
      clearNotesAround(_sel, v);
      host().beep(0);
      judge();
      saveState();
      if (_solved) {
        _view = View::Result;
        host().beep(3);
        host().refresh(true);
        return;
      }
      host().refresh(false);
      return;
    }
  }

 private:
  enum class View : uint8_t { Board, Result, Menu, Rules };

  int menuRows(gk::Row* rows) {
    int n = 0;
    rows[n++] = {"How to play", nullptr, false};
    rows[n++] = {"New puzzle", nullptr, false};
    rows[n++] = {"Level (next puzzle)", _level == sud::EASY ? "Easy" : _level == sud::MEDIUM ? "Medium" : "Hard", false};
    if (anySolved()) rows[n++] = {"Clear record", nullptr, _armedClear};
    return n;
  }

  void tapMenu(int x, int y) {
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
    } else if (!strcmp(what, "New puzzle")) {
      newPuzzle();
      _view = View::Board;
    } else if (!strncmp(what, "Level", 5)) {
      // The level picks the next puzzle; it never throws this one away.
      _level = (sud::Level)(((int)_level + 1) % sud::LEVELS);
      prefs().putInt("sd_lvl", (int)_level);
    } else if (!strcmp(what, "Clear record")) {
      if (!wasArmed) {
        _armedClear = true;
        host().beep(2);
        host().refresh(false);
        return;
      }
      record::clear(prefs(), record::SUDOKU);
      _view = View::Board;
    }
    host().beep(1);
    host().refreshUi();
  }

  int placed(int digit) const {
    int n = 0;
    for (int k = 0; k < sud::CELLS; k++)
      if (_grid[k] == digit) n++;
    return n;
  }

  // A digit written in a cell can no longer be a note anywhere it sees.
  void clearNotesAround(int k, uint8_t v) {
    const uint16_t bit = (uint16_t)(1 << v);
    for (int j = 0; j < sud::CELLS; j++) {
      if (sud::rowOf(j) == sud::rowOf(k) || sud::colOf(j) == sud::colOf(k) ||
          sud::boxOf(j) == sud::boxOf(k))
        _notes[j] &= (uint16_t)~bit;
    }
  }

  // Check: every digit of yours that disagrees with the answer gets struck
  // through, and the count is said under the buttons. It shows mistakes, never
  // the right digit.
  void check() {
    int wrong = 0, mine = 0;
    for (int k = 0; k < sud::CELLS; k++) {
      _marked[k] = false;
      if (_given[k] || !_grid[k]) continue;
      mine++;
      if (_grid[k] != _solution[k]) {
        _marked[k] = true;
        wrong++;
      }
    }
    if (mine == 0)
      snprintf(_msg, sizeof(_msg), "nothing of yours to check yet");
    else if (wrong == 0)
      snprintf(_msg, sizeof(_msg), "all %d of yours are right", mine);
    else
      snprintf(_msg, sizeof(_msg), "%d %s wrong - struck through", wrong,
               wrong == 1 ? "is" : "are");
    host().beep(wrong ? 2 : 1);
    host().refresh(false);
  }

  bool anySolved() {
    for (int i = 0; i < sud::LEVELS; i++)
      if (solvedAt(i) > 0) return true;
    return false;
  }
  int solvedAt(int lvl) {
    char key[8];
    snprintf(key, sizeof(key), "sd_w%d", lvl);
    return prefs().getInt(key, 0);
  }

  // The grid filling up is the moment to say whether it is right; how many
  // cells are wrong -- but not which -- unless Check is asked.
  void judge() {
    _solved = false;
    if (!sud::complete(_grid)) return;
    const int wrong = sud::wrongCount(_grid, _solution);
    if (wrong > 0) {
      snprintf(_msg, sizeof(_msg), "full, but %d %s wrong", wrong, wrong == 1 ? "cell is" : "cells are");
      return;
    }
    _solved = true;
    _sel = -1;
    char key[8];
    snprintf(key, sizeof(key), "sd_w%d", (int)_puzzleLevel);
    prefs().putInt(key, prefs().getInt(key, 0) + 1);
  }

  void newPuzzle() {
    _puzzleLevel = _level;
    sud::generate(_grid, _solution, _level, [] { return esp_random(); });
    for (int k = 0; k < sud::CELLS; k++) _given[k] = _grid[k] != 0;
    memset(_notes, 0, sizeof(_notes));
    memset(_marked, 0, sizeof(_marked));
    _sel = -1;
    _msg[0] = 0;
    _solved = false;
    _started = true;
    saveState();
  }

  // A generated puzzle takes real work to produce and longer to solve, so it
  // is written out whenever anything on it changes.
  struct SavedV1 {  // up to C2: no notes
    uint8_t grid[sud::CELLS];
    uint8_t solution[sud::CELLS];
    uint8_t given[sud::CELLS];
    uint8_t level, solved, pad[2];
  };
  struct Saved {
    SavedV1 base;
    uint16_t notes[sud::CELLS];
  };
  void saveState() {
    Saved s;
    memcpy(s.base.grid, _grid, sizeof(s.base.grid));
    memcpy(s.base.solution, _solution, sizeof(s.base.solution));
    for (int i = 0; i < sud::CELLS; i++) s.base.given[i] = _given[i] ? 1 : 0;
    s.base.level = (uint8_t)_puzzleLevel;
    s.base.solved = _solved ? 1 : 0;
    s.base.pad[0] = s.base.pad[1] = 0;
    memcpy(s.notes, _notes, sizeof(s.notes));
    prefs().putBytes("sd_state", &s, sizeof(s));
  }

  bool loadState() {
    Saved s;
    memset(&s, 0, sizeof(s));
    const size_t len = prefs().getBytesLength("sd_state");
    if (len == sizeof(Saved)) {
      if (prefs().getBytes("sd_state", &s, sizeof(s)) != sizeof(s)) return false;
    } else if (len == sizeof(SavedV1)) {  // a puzzle from before notes existed
      if (prefs().getBytes("sd_state", &s.base, sizeof(s.base)) != sizeof(s.base)) return false;
    } else {
      return false;
    }
    if (s.base.solved) return false;  // a finished board is not worth coming back to
    memcpy(_grid, s.base.grid, sizeof(_grid));
    memcpy(_solution, s.base.solution, sizeof(_solution));
    for (int i = 0; i < sud::CELLS; i++) _given[i] = s.base.given[i] != 0;
    memcpy(_notes, s.notes, sizeof(_notes));
    _puzzleLevel = (sud::Level)(s.base.level <= sud::HARD ? s.base.level : sud::EASY);
    memset(_marked, 0, sizeof(_marked));
    _solved = false;
    _started = true;
    _sel = -1;
    _msg[0] = 0;
    return true;
  }

  uint8_t _grid[sud::CELLS] = {};
  uint8_t _solution[sud::CELLS] = {};
  bool _given[sud::CELLS] = {};
  uint16_t _notes[sud::CELLS] = {};  // bit d = pencil note d
  bool _marked[sud::CELLS] = {};     // struck by the last Check
  int _sel = -1;
  char _msg[40] = {};
  bool _notesMode = false;
#ifdef TOYBOX_HOST
 public:
  void hostSolve() {
    memcpy(_grid, _solution, sizeof(_grid));
    _sel = -1;
    judge();
    if (_solved) _view = View::Result;
  }
  void hostNotes() {  // a few pencil notes in the first empty cells
    int done = 0;
    for (int k = 0; k < sud::CELLS && done < 3; k++)
      if (!_grid[k]) _notes[k] = (uint16_t)((done == 0 ? 0x3FE : 0) | (1 << (1 + done)) | (1 << 7)), done++;
  }
  int hostSel() const { return _sel; }
  bool hostGiven(int k) const { return _given[k]; }
  int hostSolution(int k) const { return _solution[k]; }

 private:
#endif
  View _view = View::Board;
  bool _armedClear = false;
  bool _solved = false, _started = false;
  bool _rulesFromMore = false;  // the card came from More: Back returns to the game
  sud::Level _level = sud::EASY;        // the next puzzle's
  sud::Level _puzzleLevel = sud::EASY;  // the one on the board
};
