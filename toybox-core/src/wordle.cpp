#include "wordle.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"
#include "wordlist.h"


WordleApp wordleApp;

namespace {
// --- Layout (portrait 480x800) ----------------------------------------------
// Board at the top, stats in the middle, keyboard along the bottom edge where
// thumbs reach — the shape the game is normally played in.
constexpr int TILE = 56, TGAP = 6;
constexpr int BOARD_W = 5 * TILE + 4 * TGAP;          // 304
constexpr int BOARD_X = (SCREEN_W - BOARD_W) / 2;        // 88
constexpr int BOARD_Y = TOPBAR_H + 14;                // 54

constexpr int PANEL_Y = BOARD_Y + 6 * TILE + 5 * TGAP + 12;  // 432
constexpr int NEW_W = 120, NEW_H = 40;
// Under the streak line, in the band that is only crowded once a game ends --
// so the clear is offered between games, which is when it is wanted.
constexpr int CLEAR_X = 16, CLEAR_Y = 556;
constexpr int NEW_X = SCREEN_W - NEW_W - 16;

constexpr int KEY_W = 44, KEY_H = 52, KGAP = 4;
constexpr int ROW1_Y = 628, ROW2_Y = ROW1_Y + KEY_H + 6, ROW3_Y = ROW2_Y + KEY_H + 6;
const char* ROW1 = "QWERTYUIOP";
const char* ROW2 = "ASDFGHJKL";
const char* ROW3 = "ZXCVBNM";
constexpr int ROW1_X = (SCREEN_W - (10 * KEY_W + 9 * KGAP)) / 2;  // 2
constexpr int ROW2_X = (SCREEN_W - (9 * KEY_W + 8 * KGAP)) / 2;   // 26
constexpr int ENTER_W = 64;
constexpr int ROW3_X = (SCREEN_W - (ENTER_W * 2 + 7 * KEY_W + 8 * KGAP)) / 2;  // 6

bool wordInList(const char* w, const char* list, int count) {
  int lo = 0, hi = count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int c = memcmp(w, &list[mid * 5], 5);
    if (c == 0) return true;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return false;
}
}  // namespace

void WordleApp::newGame() {
  const int idx = esp_random() % WL_ANSWER_COUNT;
  memcpy(_answer, &WL_ANSWERS[idx * 5], 5);
  for (int i = 0; i < 5; i++) _answer[i] = toupper(_answer[i]);
  _answer[5] = 0;
  memset(_grid, 0, sizeof(_grid));
  memset(_score, 0, sizeof(_score));
  memset(_keyState, 0, sizeof(_keyState));
  _row = _col = 0;
  _over = _won = false;
  strcpy(_msg, "guess the word!");
}

void WordleApp::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _armedClear = false;
  _help = !help::suppressed(prefs(), "wrd");
  newGame();
}

void WordleApp::drawBoard(ToolsCanvas& c) {
  for (int r = 0; r < 6; r++) {
    for (int col = 0; col < 5; col++) {
      const int x = BOARD_X + col * (TILE + TGAP);
      const int y = BOARD_Y + r * (TILE + TGAP);
      const uint8_t st = _score[r][col];
      const char ch = _grid[r][col];
      // Letters are centred in the tile by measurement rather than by a fixed
      // offset: glyphs have their own widths now, so W and I do not agree.
      const char one[2] = {ch, 0};
      c.fillRect(x, y, TILE, TILE, false);
      if (st == 3) {  // correct: solid black, white letter
        c.fillRect(x, y, TILE, TILE, true);
        if (ch) c.textInBox(x, y, TILE, TILE, one, TS_HUGE, false, true);
      } else if (st == 2) {  // present: double border, black letter
        c.drawRect(x, y, TILE, TILE, 2, true);
        c.drawRect(x + 5, y + 5, TILE - 10, TILE - 10, 2, true);
        if (ch) c.textInBox(x, y, TILE, TILE, one, TS_LARGE, true);
      } else if (st == 1) {  // absent: thin border, small letter
        c.drawRect(x, y, TILE, TILE, 1, true);
        if (ch) c.textInBox(x, y, TILE, TILE, one, TS_LARGE, true);
      } else {  // pending input
        c.drawRect(x, y, TILE, TILE, ch ? 3 : 1, true);
        if (ch) c.textInBox(x, y, TILE, TILE, one, TS_HUGE, true);
      }
    }
  }
}

void WordleApp::drawKeyboard(ToolsCanvas& c) {
  auto drawKey = [&](int x, int y, int w, const char* label, uint8_t st) {
    const TSize sc = (strlen(label) > 1) ? TS_SMALL : TS_LARGE;  // ENTER/DEL smaller
    c.fillRect(x, y, w, KEY_H, false);
    if (st == 3) {  // correct
      c.fillRect(x, y, w, KEY_H, true);
      c.textCentered(x + w / 2, y + (KEY_H - c.textHeight(sc)) / 2, label, sc, false);
    } else if (st == 2) {  // present
      c.drawRect(x, y, w, KEY_H, 3, true);
      c.textCentered(x + w / 2, y + (KEY_H - c.textHeight(sc)) / 2, label, sc, true);
    } else if (st == 1) {  // absent: hatched
      c.drawRect(x, y, w, KEY_H, 1, true);
      for (int d = 0; d < w + KEY_H; d += 7) {
        for (int t = 0; t < KEY_H; t++) {
          const int px = x + d - t;
          if (px >= x && px < x + w) c.fillRect(px, y + t, 1, 1, true);
        }
      }
    } else {
      c.drawRect(x, y, w, KEY_H, 2, true);
      c.textCentered(x + w / 2, y + (KEY_H - c.textHeight(sc)) / 2, label, sc, true);
    }
  };

  char lbl[2] = {0, 0};
  for (int i = 0; ROW1[i]; i++) {
    lbl[0] = ROW1[i];
    drawKey(ROW1_X + i * (KEY_W + KGAP), ROW1_Y, KEY_W, lbl, _keyState[ROW1[i] - 'A']);
  }
  for (int i = 0; ROW2[i]; i++) {
    lbl[0] = ROW2[i];
    drawKey(ROW2_X + i * (KEY_W + KGAP), ROW2_Y, KEY_W, lbl, _keyState[ROW2[i] - 'A']);
  }
  drawKey(ROW3_X, ROW3_Y, ENTER_W, "ENTER", 0);
  for (int i = 0; ROW3[i]; i++) {
    lbl[0] = ROW3[i];
    drawKey(ROW3_X + ENTER_W + KGAP + i * (KEY_W + KGAP), ROW3_Y, KEY_W, lbl,
            _keyState[ROW3[i] - 'A']);
  }
  drawKey(ROW3_X + ENTER_W + KGAP + 7 * (KEY_W + KGAP), ROW3_Y, ENTER_W, "<DEL", 0);
}

void WordleApp::render(ToolsCanvas& c) {
  host().topBar("WORDLE", true);
  if (_help) {
    help::render(c, help::WORDLE);
    return;
  }
  drawBoard(c);
  drawKeyboard(c);

  // The band between the board and the keyboard. While a game is running it
  // carries one line -- what just happened -- at a size you can read without
  // leaning in. The record and the guess-distribution chart only appear once
  // the game is over, which is when they are worth looking at; during play they
  // were six rows of 8 px digits competing with the board for attention.
  c.button(NEW_X, PANEL_Y - 4, NEW_W, NEW_H, "NEW", _over, TS_MED);

  char buf[40];
  if (!_over) {
    // The message shares this line with the NEW button, so a long one steps
    // down a size rather than sliding underneath it.
    if (_msg[0]) {
      const int roomForMsg = NEW_X - 32;
      const TSize ms = c.textWidth(_msg, TS_LARGE) <= roomForMsg ? TS_LARGE : TS_MED;
      c.text(16, PANEL_Y + 8 + (ms == TS_LARGE ? 0 : 4), _msg, ms, true);
    }
    snprintf(buf, sizeof(buf), "guess %d of 6", _row + 1);
    c.text(16, PANEL_Y + 52, buf, TS_MED, true);
    snprintf(buf, sizeof(buf), "streak %d", prefs().getInt("w_streak", 0));
    c.text(16, PANEL_Y + 80, buf, TS_MED, true);
    if (prefs().getInt("w_games", 0) > 0)
      c.button(CLEAR_X, CLEAR_Y, record::BTN_W, record::BTN_H, record::label(_armedClear), _armedClear, TS_MED);
    return;
  }

  // The verdict gets a plaque; it has to clear the NEW button, so it stops
  // short of it rather than running the width of the panel.
  decor::banner(c, 16, PANEL_Y - 4, NEW_X - 32, 44, _msg, TS_MED, _won);
  const int games = prefs().getInt("w_games", 0);
  const int wins = prefs().getInt("w_wins", 0);
  snprintf(buf, sizeof(buf), "won %d of %d   streak %d", wins, games,
           prefs().getInt("w_streak", 0));
  c.text(16, PANEL_Y + 40, buf, TS_MED, true);

  int maxd = 1;
  int dist[6];
  for (int i = 0; i < 6; i++) {
    char key[12];
    snprintf(key, sizeof(key), "w_d%d", i + 1);
    dist[i] = prefs().getInt(key, 0);
    maxd = max(maxd, dist[i]);
  }
  for (int i = 0; i < 6; i++) {
    const int y = PANEL_Y + 72 + i * 20;
    snprintf(buf, sizeof(buf), "%d", i + 1);
    c.text(16, y, buf, TS_MED, true);
    const int w = 16 + 300 * dist[i] / maxd;
    c.fillRect(36, y, w, 14, true);
    snprintf(buf, sizeof(buf), "%d", dist[i]);
    c.text(36 + w + 8, y, buf, TS_MED, true);
  }
}

void WordleApp::submitGuess() {
  if (_col < 5) {
    strcpy(_msg, "too short!");
    host().beep(2);
    host().refresh(false);
    return;
  }
  char guess[6] = {};
  for (int i = 0; i < 5; i++) guess[i] = tolower(_grid[_row][i]);
  if (!wordInList(guess, WL_ALLOWED, WL_ALLOWED_COUNT)) {
    strcpy(_msg, "not in word list");
    host().beep(2);
    host().refresh(false);
    return;
  }

  // Score: two-pass (correct first, then present with letter budget)
  int counts[26] = {};
  for (int i = 0; i < 5; i++) counts[_answer[i] - 'A']++;
  for (int i = 0; i < 5; i++) {
    if (_grid[_row][i] == _answer[i]) {
      _score[_row][i] = 3;
      counts[_answer[i] - 'A']--;
    }
  }
  for (int i = 0; i < 5; i++) {
    if (_score[_row][i] == 3) continue;
    const int li = _grid[_row][i] - 'A';
    if (counts[li] > 0) {
      _score[_row][i] = 2;
      counts[li]--;
    } else {
      _score[_row][i] = 1;
    }
  }
  for (int i = 0; i < 5; i++) {
    const int li = _grid[_row][i] - 'A';
    if (_score[_row][i] > _keyState[li]) _keyState[li] = _score[_row][i];
  }

  bool allCorrect = true;
  for (int i = 0; i < 5; i++)
    if (_score[_row][i] != 3) allCorrect = false;

  if (allCorrect) {
    _over = _won = true;
    const char* praise[6] = {"GENIUS!",  "MAGNIFICENT!", "IMPRESSIVE!",
                             "SPLENDID!", "GREAT!",       "PHEW!"};
    strcpy(_msg, praise[_row]);
    // stats
    prefs().putInt("w_games", prefs().getInt("w_games", 0) + 1);
    prefs().putInt("w_wins", prefs().getInt("w_wins", 0) + 1);
    const int streak = prefs().getInt("w_streak", 0) + 1;
    prefs().putInt("w_streak", streak);
    if (streak > prefs().getInt("w_max", 0)) prefs().putInt("w_max", streak);
    char key[8];
    snprintf(key, sizeof(key), "w_d%d", _row + 1);
    prefs().putInt(key, prefs().getInt(key, 0) + 1);
    host().beep(3);
  } else if (_row == 5) {
    _over = true;
    snprintf(_msg, sizeof(_msg), "it was: %s", _answer);
    prefs().putInt("w_games", prefs().getInt("w_games", 0) + 1);
    prefs().putInt("w_streak", 0);
    host().beep(2);
  } else {
    _row++;
    _col = 0;
    strcpy(_msg, "");
    host().beep(1);
  }
  host().refresh(false);
}

void WordleApp::keyPressed(char c) {
  if (_over) return;
  if (c == '\r') {
    submitGuess();
    return;
  }
  if (c == '\b') {
    if (_col > 0) {
      _col--;
      _grid[_row][_col] = 0;
      host().beep(0);
      host().refresh(false);
    }
    return;
  }
  if (_col < 5) {
    _grid[_row][_col++] = c;
    host().beep(0);
    host().refresh(false);
  }
}

void WordleApp::onTap(int x, int y) {
  if (host().isHelpTap(x, y)) {
    _help = !_help;
    host().beep(1);
    host().refresh(true);
    return;
  }
  if (_help) {
    const help::Tap t = help::hit(x, y);
    if (t == help::Tap::None) return;
    if (t == help::Tap::Never) help::suppress(prefs(), "wrd");
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
  if (!_over && prefs().getInt("w_games", 0) > 0 &&
      inRect(x, y, CLEAR_X, CLEAR_Y, record::BTN_W, record::BTN_H)) {
    if (wasArmed) {
      record::clear(prefs(), record::WORDLE);
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

  if (inRect(x, y, NEW_X, PANEL_Y - 4, NEW_W, NEW_H)) {
    host().beep(1);
    newGame();
    host().refresh(true);  // fresh board: clean the ghosting too
    return;
  }
  // Keyboard rows
  if (y >= ROW1_Y && y < ROW1_Y + KEY_H) {
    const int i = (x - ROW1_X) / (KEY_W + KGAP);
    if (i >= 0 && i < 10 && x >= ROW1_X) keyPressed(ROW1[i]);
  } else if (y >= ROW2_Y && y < ROW2_Y + KEY_H) {
    const int i = (x - ROW2_X) / (KEY_W + KGAP);
    if (i >= 0 && i < 9 && x >= ROW2_X) keyPressed(ROW2[i]);
  } else if (y >= ROW3_Y && y < ROW3_Y + KEY_H) {
    if (x >= ROW3_X && x < ROW3_X + ENTER_W) {
      keyPressed('\r');
    } else if (x >= ROW3_X + ENTER_W + KGAP + 7 * (KEY_W + KGAP)) {
      keyPressed('\b');
    } else {
      const int i = (x - ROW3_X - ENTER_W - KGAP) / (KEY_W + KGAP);
      if (i >= 0 && i < 7) keyPressed(ROW3[i]);
    }
  }
}
