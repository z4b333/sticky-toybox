#include "xo.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"


using xorules::State;

XoApp xoApp;

namespace {
// Portrait: a big board you can hit with a thumb, controls stacked beneath.
constexpr int CELL = 132;
constexpr int BOARD = 3 * CELL;          // 396
constexpr int BX = (SCREEN_W - BOARD) / 2;  // 42
constexpr int BY = 120;

struct Btn {
  int x, y, w, h;
};
constexpr int FOE_W = 140, FOE_H = 54, FOE_Y = 536;
constexpr int FOE_X0 = (SCREEN_W - 3 * FOE_W - 2 * 15) / 2;  // 15
constexpr Btn BTN_RULE{42, 602, 220, 54};
constexpr Btn BTN_NEW{282, 602, 156, 54};
// Beside the record it erases, on the strip under it.
constexpr Btn BTN_CLEAR{SCREEN_W - 16 - record::BTN_W, 752, record::BTN_W, 44};

// THREE never fills the board, so nothing ends it on its own. Long chases are
// almost always repetitions; call them a draw rather than let one run forever.
constexpr int MAX_PLIES = 60;

Btn foeRect(int i) { return Btn{FOE_X0 + i * (FOE_W + 15), FOE_Y, FOE_W, FOE_H}; }
int cellX(int c) { return BX + (c % 3) * CELL; }
int cellY(int c) { return BY + (c / 3) * CELL; }
}  // namespace

// --- game flow ---------------------------------------------------------------

void XoApp::newGame() {
  xorules::reset(_s);
  _turn = 1;
  _over = 0;
  _lastAi = -1;
  _plies = 0;
}

void XoApp::recordResult(int result) {
  if (_foe == FOE_HUMAN) {
    // Nothing is written to NVS here; the tally lives only until you leave.
    if (result > 0)
      _sitX++;
    else if (result < 0)
      _sitO++;
    else
      _sitD++;
    return;
  }
  int streak = prefs().getInt("x_strk", 0);
  if (result > 0) {
    prefs().putInt("x_w", prefs().getInt("x_w", 0) + 1);
    streak++;
    if (streak > prefs().getInt("x_best", 0)) prefs().putInt("x_best", streak);
  } else if (result < 0) {
    prefs().putInt("x_l", prefs().getInt("x_l", 0) + 1);
    streak = 0;
  } else {
    prefs().putInt("x_d", prefs().getInt("x_d", 0) + 1);
  }
  prefs().putInt("x_strk", streak);
}

void XoApp::place(int cell) {
  xorules::applyMove(_s, _turn, cell, three());
  _plies++;
  const int w = xorules::winner(_s);
  if (w) {
    _over = (uint8_t)w;
    recordResult(w == 1 ? 1 : -1);
  } else if (!three() && xorules::boardFull(_s)) {
    _over = 3;
    recordResult(0);
  } else if (_plies >= MAX_PLIES) {
    _over = 3;
    recordResult(0);
  } else {
    _turn = 3 - _turn;
  }
}

// --- drawing -----------------------------------------------------------------

namespace {
// A mark that is about to lift is drawn hairline instead of solid — same shape,
// visibly on its way out.
void drawX(ToolsCanvas& c, int cell, bool doomed) {
  const int x = cellX(cell), y = cellY(cell);
  const int in = 30, t = doomed ? 3 : 11;
  c.drawLine(x + in, y + in, x + CELL - in, y + CELL - in, t, true);
  c.drawLine(x + CELL - in, y + in, x + in, y + CELL - in, t, true);
}

void drawO(ToolsCanvas& c, int cell, bool doomed) {
  const int x = cellX(cell), y = cellY(cell);
  c.drawCircle(x + CELL / 2, y + CELL / 2, CELL / 2 - 30, doomed ? 3 : 11, true);
}
}  // namespace

void XoApp::render(ToolsCanvas& c) {
  host().topBar("XO", true);
  if (_help) {
    help::render(c, help::XO);
    return;
  }

  char buf[48];

  // --- status ----------------------------------------------------------
  const char* status;
  if (_over == 3)
    status = "DRAW";
  else if (_over == 1)
    status = (_foe == FOE_HUMAN) ? "X WINS" : "YOU WIN";
  else if (_over == 2)
    status = (_foe == FOE_HUMAN) ? "O WINS" : "MACHINE WINS";
  else if (_foe == FOE_HUMAN)
    status = (_turn == 1) ? "X TO PLAY" : "O TO PLAY";
  else
    status = "YOUR TURN";
  if (_over) {
    // A result is worth a plaque; a turn is not.
    const bool youWon = (_over == 1 && _foe != FOE_HUMAN);
    decor::banner(c, 70, 54, 340, 52, status, TS_LARGE, youWon);
  } else {
    c.textTrackedCentered(SCREEN_W / 2, 66, status, TS_LARGE, true, false, 2);
  }

  // --- board -----------------------------------------------------------
  c.drawRect(BX - 3, BY - 3, BOARD + 6, BOARD + 6, 3, true);
  for (int i = 1; i < 3; i++) {
    c.fillRect(BX + i * CELL - 2, BY + 8, 4, BOARD - 16, true);
    c.fillRect(BX + 8, BY + i * CELL - 2, BOARD - 16, 4, true);
  }

  const int doomX = _over ? -1 : xorules::doomed(_s, 1, three());
  const int doomO = _over ? -1 : xorules::doomed(_s, 2, three());
  for (int cell = 0; cell < 9; cell++) {
    if (_s.cell[cell] == 1)
      drawX(c, cell, cell == doomX);
    else if (_s.cell[cell] == 2)
      drawO(c, cell, cell == doomO);
  }

  // Where the machine just played, so a single refresh still reads as a reply.
  if (_lastAi >= 0 && !_over)
    c.fillCircle(cellX(_lastAi) + CELL - 16, cellY(_lastAi) + 16, 6, true);

  if (_over && _over != 3) {
    const int l = xorules::winLine(_s);
    if (l >= 0) {
      const int a = xorules::LINES[l][0], b = xorules::LINES[l][2];
      c.drawLine(cellX(a) + CELL / 2, cellY(a) + CELL / 2, cellX(b) + CELL / 2,
                   cellY(b) + CELL / 2, 9, true);
    }
  }

  // --- controls --------------------------------------------------------
  // One text size for the whole row. "2 PLAYER" is the longest label, so it
  // sets the size for all three -- a row where one button's text is smaller
  // than its neighbours' reads as a mistake, not as emphasis.
  static const char* kFoeNames[3] = {"EASY", "HARD", "2 PLAYER"};
  for (int i = 0; i < 3; i++) {
    const Btn b = foeRect(i);
    c.button(b.x, b.y, b.w, b.h, kFoeNames[i], i == (int)_foe, TS_MED);
  }
  c.button(BTN_RULE.x, BTN_RULE.y, BTN_RULE.w, BTN_RULE.h, _rule == RULE_THREE ? "3 MARKS" : "CLASSIC", _rule == RULE_THREE, TS_LARGE);
  c.button(BTN_NEW.x, BTN_NEW.y, BTN_NEW.w, BTN_NEW.h, "NEW", false, TS_LARGE);

  // Once it is over, the line under the controls stops explaining the rule and
  // starts saying what to do next -- there is no room for both above the board.
  c.textCentered(SCREEN_W / 2, 668, _over ? "tap the board for a new game"
                        : _rule == RULE_THREE ? "the faint mark lifts next"
                                              : "three in a row wins", TS_MED, true);

  // --- record ----------------------------------------------------------
  if (_foe == FOE_HUMAN) {
    c.text(20, 706, "pass the device each turn", TS_MED, true);
    if (_sitX + _sitO + _sitD > 0) {
      snprintf(buf, sizeof(buf), "this sitting   X %d   O %d   drew %d", _sitX, _sitO, _sitD);
      c.text(20, 732, buf, TS_MED, true);
    }
  } else {
    const int w = prefs().getInt("x_w", 0), l = prefs().getInt("x_l", 0);
    const int d = prefs().getInt("x_d", 0);
    snprintf(buf, sizeof(buf), "won %d   lost %d   drew %d", w, l, d);
    c.text(20, 706, buf, TS_MED, true);
    snprintf(buf, sizeof(buf), "streak %d   best %d", prefs().getInt("x_strk", 0),
             prefs().getInt("x_best", 0));
    c.text(20, 732, buf, TS_MED, true);
    if (w + l + d > 0)
      c.button(BTN_CLEAR.x, BTN_CLEAR.y, BTN_CLEAR.w, BTN_CLEAR.h, record::label(_armedClear), _armedClear, TS_MED);
  }
}

void XoApp::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _armedClear = false;
  _sitX = _sitO = _sitD = 0;
  _help = !help::suppressed(prefs(), "xo");
  _rule = (Rule)prefs().getInt("x_rule", RULE_CLASSIC);
  _foe = (Foe)prefs().getInt("x_foe", FOE_HARD);
  if (_rule > RULE_THREE) _rule = RULE_CLASSIC;
  if (_foe > FOE_HUMAN) _foe = FOE_HARD;
  newGame();
}

void XoApp::onTap(int x, int y) {
  // Back comes before the rules card, not after it. A card that swallows
  // "< HUB" leaves the only way out of the app behind a button you have to
  // find first, and the sudoku and battleship screens never did that.
  if (host().isBackTap(x, y)) {
    host().beep(1);
    host().goHub();
    return;
  }

  if (host().isHelpTap(x, y)) {
    _help = !_help;
    host().beep(1);
    host().refreshUi();
    return;
  }
  if (_help) {
    const help::Tap t = help::hit(x, y);
    if (t == help::Tap::None) return;
    if (t == help::Tap::Never) help::suppress(prefs(), "xo");
    _help = false;
    host().beep(1);
    host().refreshUi();
    return;
  }

  // The record's own clear: armed by the first tap, and disarmed by any tap
  // that is not the second one, so it cannot lie in wait.
  const bool wasArmed = _armedClear;
  _armedClear = false;
  if (inRect(x, y, BTN_CLEAR.x, BTN_CLEAR.y, BTN_CLEAR.w, BTN_CLEAR.h) && _foe != FOE_HUMAN) {
    if (wasArmed) {
      record::clear(prefs(), record::XO);
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

  for (int i = 0; i < 3; i++) {
    const Btn b = foeRect(i);
    if (inRect(x, y, b.x, b.y, b.w, b.h)) {
      _foe = (Foe)i;
      prefs().putInt("x_foe", i);
      host().beep(1);
      newGame();
      host().refresh(true);
      return;
    }
  }
  if (inRect(x, y, BTN_RULE.x, BTN_RULE.y, BTN_RULE.w, BTN_RULE.h)) {
    _rule = (_rule == RULE_THREE) ? RULE_CLASSIC : RULE_THREE;
    prefs().putInt("x_rule", (int)_rule);
    host().beep(1);
    newGame();
    host().refresh(true);
    return;
  }
  if (inRect(x, y, BTN_NEW.x, BTN_NEW.y, BTN_NEW.w, BTN_NEW.h)) {
    host().beep(1);
    newGame();
    host().refresh(true);
    return;
  }

  if (!inRect(x, y, BX, BY, BOARD, BOARD)) return;

  if (_over) {  // the finished board is itself the "play again" button
    host().beep(1);
    newGame();
    host().refresh(true);
    return;
  }

  const int c = ((y - BY) / CELL) * 3 + (x - BX) / CELL;
  if (c < 0 || c > 8 || _s.cell[c]) {
    host().beep(2);
    return;
  }

  // A mark lifting off is a black-to-white change; DU ghosts badly on those, so
  // those frames get a full refresh and ordinary placements stay fast.
  const bool lifts = xorules::doomed(_s, _turn, three()) >= 0;
  _lastAi = -1;
  place(c);
  host().beep(0);

  // The machine answers inside the same frame — one e-ink refresh per turn,
  // not two.
  bool aiLifts = false;
  if (!_over && _foe != FOE_HUMAN && _turn == 2) {
    aiLifts = xorules::doomed(_s, 2, three()) >= 0;
    const int m = (_foe == FOE_EASY)
                      ? xorules::easyMove(_s, 2, three(), esp_random())
                      : xorules::bestMove(_s, 2, three(), esp_random());
    if (m >= 0) {
      _lastAi = (int8_t)m;
      place(m);
    }
  }

  if (_over == 1)
    host().beep(3);
  else if (_over)
    host().beep(1);

  if (lifts || aiLifts || _over)
    host().refresh(true);
  else
    host().refresh(false);
}
