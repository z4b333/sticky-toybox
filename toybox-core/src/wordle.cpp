#include "wordle.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"
#include "wordlist.h"

WordleApp wordleApp;

using namespace wordui;

namespace {
const char* ROW1 = "QWERTYUIOP";
const char* ROW2 = "ASDFGHJKL";
const char* ROW3 = "ZXCVBNM";

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

// Days from 1 Jan 2000 to y-m-d (proleptic Gregorian; the RTC never goes
// near the edges of that).
int daysFrom2000(int y, int m, int d) {
  y -= m <= 2;
  const int era = y / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 730425;  // 730425 = days to 2000-01-01 from 0000-03-01
}

// The word of a day: a stride through the list, so consecutive days are far
// apart in it and the order is the same on every device. 7919 is prime and
// does not divide the list's length, so every word comes round once a cycle.
int dailyIndex(int day) {
  long v = ((long)day * 7919L) % WL_ANSWER_COUNT;
  if (v < 0) v += WL_ANSWER_COUNT;
  return (int)v;
}

struct Saved {
  int32_t day;    // the day it is the word of, or -1 for an extra word
  int16_t idx;    // into WL_ANSWERS
  uint8_t row, col, over, won, pad[2];
  char grid[6][5];
  uint8_t score[6][5];
};
}  // namespace

int WordleApp::today() {
  int y = 0, m = 0, d = 0;
  if (!host().clockDate(y, m, d) || y < 2020 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  return daysFrom2000(y, m, d);
}

void WordleApp::begin(int answerIdx) {
  _answerIdx = (int16_t)answerIdx;
  memcpy(_answer, &WL_ANSWERS[answerIdx * 5], 5);
  for (int i = 0; i < 5; i++) _answer[i] = toupper(_answer[i]);
  _answer[5] = 0;
  memset(_grid, 0, sizeof(_grid));
  memset(_score, 0, sizeof(_score));
  memset(_keyState, 0, sizeof(_keyState));
  _row = _col = 0;
  _over = _won = false;
  _msg[0] = 0;
  _view = View::Board;
}

void WordleApp::startDaily(int day) {
  begin(dailyIndex(day));
  _day = day;
  _daily = true;
  prefs().putInt("w_dday", day);
  saveState();
}

void WordleApp::startExtra() {
  // Never today's word again by chance: it would be no game at all.
  const int t = today();
  int idx;
  do {
    idx = (int)(esp_random() % WL_ANSWER_COUNT);
  } while (t >= 0 && idx == dailyIndex(t));
  begin(idx);
  _day = -1;
  _daily = false;
  saveState();
}

void WordleApp::saveState() {
  Saved s;
  memset(&s, 0, sizeof(s));
  s.day = _day;
  s.idx = _answerIdx;
  s.row = (uint8_t)_row;
  s.col = (uint8_t)_col;
  s.over = _over;
  s.won = _won;
  memcpy(s.grid, _grid, sizeof(s.grid));
  memcpy(s.score, _score, sizeof(s.score));
  prefs().putBytes("w_state", &s, sizeof(s));
}

bool WordleApp::loadState() {
  Saved s;
  if (prefs().getBytesLength("w_state") != sizeof(s)) return false;
  if (prefs().getBytes("w_state", &s, sizeof(s)) != sizeof(s)) return false;
  if (s.idx < 0 || s.idx >= WL_ANSWER_COUNT || s.row > 5 || s.col > 5) return false;
  begin(s.idx);
  _day = s.day;
  _daily = s.day >= 0;
  _row = s.row;
  _col = s.col;
  _over = s.over;
  _won = s.won;
  memcpy(_grid, s.grid, sizeof(_grid));
  memcpy(_score, s.score, sizeof(_score));
  for (int r = 0; r < 6; r++)
    for (int i = 0; i < 5; i++) {
      if (!_score[r][i] || !_grid[r][i]) continue;
      const int li = _grid[r][i] - 'A';
      if (li >= 0 && li < 26 && _score[r][i] > _keyState[li]) _keyState[li] = _score[r][i];
    }
  return true;
}

void WordleApp::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _rulesFromMore = false;
  _armedClear = false;
  const bool had = loadState();
  const int t = today();
  if (t >= 0) {
    // Today's word comes first: once a day, whatever was left open before.
    const bool todayPlayed = prefs().getInt("w_dday", -1) == t;
    if (!had || (!todayPlayed && !(_daily && _day == t))) startDaily(t);
  } else if (!had) {
    startExtra();
  }
  _view = help::suppressed(prefs(), "wrd") ? View::Board : View::Rules;
}

// --- drawing -------------------------------------------------------------------

void WordleApp::drawBoard(ToolsCanvas& c) {
  for (int r = 0; r < 6; r++) {
    for (int col = 0; col < 5; col++) {
      const int x = BOARD_X + col * (TILE + TGAP);
      const int y = BOARD_Y + r * (TILE + TGAP);
      const uint8_t st = _score[r][col];
      const char ch = _grid[r][col];
      const char one[2] = {ch, 0};
      if (st == 3) {  // right place: solid black, white letter
        c.fillRect(x, y, TILE, TILE, true);
        c.textInBox(x, y, TILE, TILE, one, TS_HUGE, false, true);
      } else if (st == 2) {  // in the word: striped, the letter on a white patch
        gk::stripes(c, x, y, TILE, TILE);
        c.drawRect(x, y, TILE, TILE, 2, true);
        const int pw = c.textWidth(one, TS_HUGE, true) + 12, ph = c.textHeight(TS_HUGE);
        c.fillRect(x + (TILE - pw) / 2, y + (TILE - ph) / 2, pw, ph, false);
        c.textInBox(x, y, TILE, TILE, one, TS_HUGE, true, true);
      } else if (st == 1) {  // not in it: plain frame
        c.drawRect(x, y, TILE, TILE, 2, true);
        c.textInBox(x, y, TILE, TILE, one, TS_HUGE, true, true);
      } else if (r == _row && !_over) {  // the row being typed
        c.drawRect(x, y, TILE, TILE, ch ? 3 : 2, true);
        if (ch) c.textInBox(x, y, TILE, TILE, one, TS_HUGE, true, true);
      } else {  // rows still to come: dashed, as the kit draws them
        for (int i = 0; i < TILE; i += 10) {
          const int len = i + 6 <= TILE ? 6 : TILE - i;
          c.fillRect(x + i, y, len, 2, true);
          c.fillRect(x + i, y + TILE - 2, len, 2, true);
          c.fillRect(x, y + i, 2, len, true);
          c.fillRect(x + TILE - 2, y + i, 2, len, true);
        }
      }
    }
  }
}

// Under the board: what the marks mean, or what just went wrong.
void WordleApp::drawLegend(ToolsCanvas& c) {
  const int y = LEGEND_Y, h = c.textHeight(TS_SMALL);
  if (_msg[0]) {
    c.textCentered(c.width() / 2, y, _msg, TS_SMALL, true, true);
    return;
  }
  const int sq = 16, sy = y + (h - sq) / 2;
  int x = 30;
  c.fillRect(x, sy, sq, sq, true);
  x += sq + 6;
  c.text(x, y, "right place", TS_SMALL, true);
  x += c.textWidth("right place", TS_SMALL) + 18;
  gk::stripes(c, x, sy, sq, sq);
  c.drawRect(x, sy, sq, sq, 1, true);
  x += sq + 6;
  c.text(x, y, "in the word", TS_SMALL, true);
  x += c.textWidth("in the word", TS_SMALL) + 18;
  c.text(x, y, "struck: not in it", TS_SMALL, true);
}

void WordleApp::drawKeyboard(ToolsCanvas& c) {
  auto drawKey = [&](int x, int y, int w, const char* label, uint8_t st) {
    const TSize sz = strlen(label) > 1 ? TS_SMALL : TS_LARGE;
    if (st == 3) {  // right place
      c.fillRect(x, y, w, KEY_H, true);
      c.textInBox(x, y, w, KEY_H, label, sz, false, true);
    } else if (st == 2) {  // in the word: a heavy frame
      c.drawRect(x, y, w, KEY_H, 3, true);
      c.textInBox(x, y, w, KEY_H, label, sz, true, true);
    } else if (st == 1) {  // not in it: struck through
      c.drawRect(x, y, w, KEY_H, 1, true);
      c.textInBox(x, y, w, KEY_H, label, sz, true);
      c.fillRect(x + 8, y + KEY_H / 2 - 1, w - 16, 3, true);
    } else {
      c.drawRect(x, y, w, KEY_H, 2, true);
      c.textInBox(x, y, w, KEY_H, label, sz, true, sz == TS_LARGE);
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
  drawKey(ENTER.x, ENTER.y, ENTER.w, "ENTER", 0);
  for (int i = 0; ROW3[i]; i++) {
    lbl[0] = ROW3[i];
    drawKey(ROW3_X + ENTER_W + KGAP + i * (KEY_W + KGAP), ROW3_Y, KEY_W, lbl,
            _keyState[ROW3[i] - 'A']);
  }
  drawKey(DEL.x, DEL.y, DEL.w, "DEL", 0);
}

// Kit "Wordle, solved": the word on black tiles, then how many guesses every
// win has taken, today's row filled.
void WordleApp::renderResult(ToolsCanvas& c) {
  const int w = c.width();
  char buf[40];
  if (_won)
    snprintf(buf, sizeof(buf), "SOLVED IN %d", _row + 1);
  else
    snprintf(buf, sizeof(buf), "THE WORD WAS");
  c.textTrackedCentered(w / 2, 130, buf, TS_SMALL, true, false, 3);
  constexpr int T = 60, G = 8;
  const int x0 = (w - (5 * T + 4 * G)) / 2;
  for (int i = 0; i < 5; i++) {
    const char one[2] = {_answer[i], 0};
    c.fillRect(x0 + i * (T + G), 162, T, T, true);
    c.textInBox(x0 + i * (T + G), 162, T, T, one, TS_HUGE, false, true);
  }

  c.textTrackedCentered(w / 2, 262, "GUESSES", TS_SMALL, true, false, 2);
  const int gw = c.textTrackedWidth("GUESSES", TS_SMALL, false, 2);
  chrome2::dottedRule(c, w / 2 - gw / 2, 262 + c.textHeight(TS_SMALL) + 4, gw);
  int dist[6], maxd = 1;
  for (int i = 0; i < 6; i++) {
    char key[8];
    snprintf(key, sizeof(key), "w_d%d", i + 1);
    dist[i] = prefs().getInt(key, 0);
    if (dist[i] > maxd) maxd = dist[i];
  }
  for (int i = 0; i < 6; i++) {
    const int y = 306 + i * 44;
    snprintf(buf, sizeof(buf), "%d", i + 1);
    c.text(84, y + 4, buf, TS_MED, true);
    const int bw = 40 + 260 * dist[i] / maxd;
    const bool mine = _won && i == _row;
    snprintf(buf, sizeof(buf), "%d", dist[i]);
    if (mine) {
      c.fillRect(116, y, bw, 32, true);
      c.text(116 + bw - 10 - c.textWidth(buf, TS_MED, true), y + 4, buf, TS_MED, false, true);
    } else {
      c.drawRect(116, y, bw, 32, 2, true);
      c.text(116 + bw - 10 - c.textWidth(buf, TS_MED), y + 4, buf, TS_MED, true);
    }
  }
  snprintf(buf, sizeof(buf), "won %d of %d - best streak %d", prefs().getInt("w_wins", 0),
           prefs().getInt("w_games", 0), prefs().getInt("w_max", 0));
  c.textCentered(w / 2, 576, buf, TS_MED, true);
  c.button(gk::RES_LEFT.x, gk::RES_LEFT.y, gk::RES_LEFT.w, gk::RES_LEFT.h, "Close", false,
           TS_LARGE);
  c.button(gk::RES_RIGHT.x, gk::RES_RIGHT.y, gk::RES_RIGHT.w, gk::RES_RIGHT.h, "New word", true,
           TS_LARGE);
  gk::foot(c, "a new word every day - or play more");
}

int WordleApp::menuRows(gk::Row* rows) {
  int n = 0;
  rows[n++] = {"How to play", nullptr, false};
  rows[n++] = {"New word", nullptr, false};
  if (_daily && _day >= 0 && _day != today() && today() >= 0)
    rows[n++] = {"Today's word", nullptr, false};
  if (prefs().getInt("w_games", 0) > 0) rows[n++] = {"Clear record", nullptr, _armedClear};
  return n;
}

void WordleApp::render(ToolsCanvas& c) {
  if (_view == View::Rules) {
    gk::bar(host(), c, "WORDLE");
    gk::rules(c, help::WORDLE);
    return;
  }
  if (_view == View::Menu) {
    gk::Row rows[gk::MENU_MAX];
    gk::menu(host(), c, "WORDLE", rows, menuRows(rows));
    return;
  }
  char corner[16];
  if (_view == View::Result) {
    snprintf(corner, sizeof(corner), "streak %d", prefs().getInt("w_streak", 0));
    gk::bar(host(), c, "WORDLE", corner);
    renderResult(c);
    return;
  }
  snprintf(corner, sizeof(corner), "%d/6", _over ? _row + 1 : _row + 1);
  gk::bar(host(), c, "WORDLE", corner);
  drawBoard(c);
  drawLegend(c);
  drawKeyboard(c);
  char buf[48];
  if (_over)
    gk::foot(c, "tap the board for the result");
  else {
    snprintf(buf, sizeof(buf), "%s - streak %d - best %d", _daily ? "today's word" : "extra word",
             prefs().getInt("w_streak", 0), prefs().getInt("w_max", 0));
    gk::foot(c, buf);
  }
}

// --- playing -------------------------------------------------------------------

void WordleApp::submitGuess() {
  if (_col < 5) {
    strcpy(_msg, "too short");
    host().beep(2);
    host().refresh(false);
    return;
  }
  char guess[6] = {};
  for (int i = 0; i < 5; i++) guess[i] = tolower(_grid[_row][i]);
  if (!wordInList(guess, WL_ALLOWED, WL_ALLOWED_COUNT)) {
    strcpy(_msg, "not in the word list");
    host().beep(2);
    host().refresh(false);
    return;
  }

  // Score: two-pass (right place first, then in the word with a letter budget).
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

  _msg[0] = 0;
  if (allCorrect) {
    _over = _won = true;
    prefs().putInt("w_games", prefs().getInt("w_games", 0) + 1);
    prefs().putInt("w_wins", prefs().getInt("w_wins", 0) + 1);
    const int streak = prefs().getInt("w_streak", 0) + 1;
    prefs().putInt("w_streak", streak);
    if (streak > prefs().getInt("w_max", 0)) prefs().putInt("w_max", streak);
    char key[8];
    snprintf(key, sizeof(key), "w_d%d", _row + 1);
    prefs().putInt(key, prefs().getInt(key, 0) + 1);
    saveState();
    _view = View::Result;
    host().beep(3);
    host().refresh(true);
    return;
  }
  if (_row == 5) {
    _over = true;
    prefs().putInt("w_games", prefs().getInt("w_games", 0) + 1);
    prefs().putInt("w_streak", 0);
    saveState();
    _view = View::Result;
    host().beep(2);
    host().refresh(true);
    return;
  }
  _row++;
  _col = 0;
  saveState();
  host().beep(1);
  host().refresh(false);
}

void WordleApp::keyPressed(char c) {
  if (_over) return;
  _msg[0] = 0;
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

void WordleApp::tapMenu(int x, int y) {
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
  } else if (!strcmp(what, "New word")) {
    startExtra();
  } else if (!strcmp(what, "Today's word")) {
    startDaily(today());
  } else if (!strcmp(what, "Clear record")) {
    if (!wasArmed) {
      _armedClear = true;
      host().beep(2);
      host().refresh(false);
      return;
    }
    record::clear(prefs(), record::WORDLE);
    _view = View::Board;
  }
  host().beep(1);
  host().refresh(true);
}

void WordleApp::onTap(int x, int y) {
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
    if (t == help::Tap::Never) help::suppress(prefs(), "wrd");
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
      startExtra();
      host().refresh(true);
    } else if (gk::RES_LEFT.hit(x, y)) {
      host().beep(1);
      _view = View::Board;
      host().refresh(true);
    }
    return;
  }

  if (_over) {
    if (BOARD.hit(x, y)) {
      host().beep(1);
      _view = View::Result;
      host().refresh(true);
    }
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
    if (ENTER.hit(x, y)) {
      keyPressed('\r');
    } else if (x >= DEL.x) {
      keyPressed('\b');
    } else {
      const int i = (x - ROW3_X - ENTER_W - KGAP) / (KEY_W + KGAP);
      if (i >= 0 && i < 7) keyPressed(ROW3[i]);
    }
  }
}
