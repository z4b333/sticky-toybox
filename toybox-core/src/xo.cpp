#include "xo.h"

#include "record.h"

#include <esp_random.h>

#include "tools/decor.h"
#include "tools/help.h"


using xorules::State;

XoApp xoApp;

namespace {
using namespace xoui;
// THREE never fills the board, so nothing ends it on its own. Long chases are
// almost always repetitions; call them a draw rather than let one run forever.
constexpr int MAX_PLIES = 60;

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
// A mark that is about to lift is drawn hairline instead of solid -- same
// shape, visibly on its way out.
void drawX(ToolsCanvas& c, int cell, bool doomed) {
  const int x = cellX(cell), y = cellY(cell);
  const int in = 34, t = doomed ? 3 : 10;
  c.drawLine(x + in, y + in, x + CELL - in, y + CELL - in, t, true);
  c.drawLine(x + CELL - in, y + in, x + in, y + CELL - in, t, true);
}

void drawO(ToolsCanvas& c, int cell, bool doomed) {
  const int x = cellX(cell), y = cellY(cell);
  c.drawCircle(x + CELL / 2, y + CELL / 2, CELL / 2 - 36, doomed ? 3 : 10, true);
}
}  // namespace

const char* XoApp::levelName(Foe f) {
  return f == FOE_EASY ? "Easy" : f == FOE_NORMAL ? "Normal" : "Hard";
}

int XoApp::menuRows(gk::Row* rows) {
  int n = 0;
  rows[n++] = {"How to play", nullptr, false};
  rows[n++] = {"New game", nullptr, false};
  rows[n++] = {"Sticky plays", levelName(_foe == FOE_HUMAN ? (Foe)prefs().getInt("x_lvl", FOE_HARD) : _foe), false};
  rows[n++] = {"Rule", _rule == RULE_THREE ? "3 marks" : "Classic", false};
  if (prefs().getInt("x_w", 0) + prefs().getInt("x_l", 0) + prefs().getInt("x_d", 0) > 0)
    rows[n++] = {"Clear record", nullptr, _armedClear};
  return n;
}

void XoApp::render(ToolsCanvas& c) {
  if (_view == View::Rules) {
    gk::bar(host(), c, "XO");
    gk::rules(c, help::XO);
    return;
  }
  if (_view == View::Menu) {
    gk::Row rows[gk::MENU_MAX];
    gk::menu(host(), c, "XO", rows, menuRows(rows));
    return;
  }
  gk::bar(host(), c, "XO", _rule == RULE_THREE ? "3 marks" : nullptr);
  const bool friendly = _foe == FOE_HUMAN;

  // --- whose move -------------------------------------------------------
  const char* status;
  if (_over == 3)
    status = "Draw";
  else if (_over == 1)
    status = friendly ? "X wins" : "You won";
  else if (_over == 2)
    status = friendly ? "O wins" : "Sticky won";
  else if (friendly)
    status = _turn == 1 ? "X to move" : "O to move";
  else
    status = "Your move";
  c.text(BX, 60, status, TS_LARGE, true, true);
  const char* tag = friendly ? "pass and play" : "you are X";
  const int tw = c.textWidth(tag, TS_SMALL) + 20;
  c.drawRect(BX + BOARD - tw, 62, tw, 32, 2, true);
  c.textInBox(BX + BOARD - tw, 62, tw, 32, tag, TS_SMALL, true);

  // --- board: four lines, no frame (kit) -----------------------------------
  // The Sticky's last move is striped under its mark, so a single refresh
  // still reads as a reply.
  if (_lastAi >= 0) gk::stripes(c, cellX(_lastAi) + 4, cellY(_lastAi) + 4, CELL - 8, CELL - 8);
  for (int i = 1; i < 3; i++) {
    c.fillRect(BX + i * CELL - 2, BY, 4, BOARD, true);
    c.fillRect(BX, BY + i * CELL - 2, BOARD, 4, true);
  }
  const int doomX = _over ? -1 : xorules::doomed(_s, 1, three());
  const int doomO = _over ? -1 : xorules::doomed(_s, 2, three());
  for (int cell = 0; cell < 9; cell++) {
    if (_s.cell[cell] && cell == _lastAi) {
      // A white patch behind the mark keeps it solid on the stripes.
      c.fillCircle(cellX(cell) + CELL / 2, cellY(cell) + CELL / 2, CELL / 2 - 22, false);
    }
    if (_s.cell[cell] == 1)
      drawX(c, cell, cell == doomX);
    else if (_s.cell[cell] == 2)
      drawO(c, cell, cell == doomO);
  }
  if (_over && _over != 3) {
    const int l = xorules::winLine(_s);
    if (l >= 0) {
      const int a = xorules::LINES[l][0], b = xorules::LINES[l][2];
      c.drawLine(cellX(a) + CELL / 2, cellY(a) + CELL / 2, cellX(b) + CELL / 2,
                 cellY(b) + CELL / 2, 9, true);
    }
  }

  // --- record -------------------------------------------------------------
  if (friendly) {
    gk::statInt(c, statRect(0), "X", _sitX);
    gk::statInt(c, statRect(1), "O", _sitO);
    gk::statInt(c, statRect(2), "DRAW", _sitD);
  } else {
    gk::statInt(c, statRect(0), "WON", prefs().getInt("x_w", 0));
    gk::statInt(c, statRect(1), "LOST", prefs().getInt("x_l", 0));
    gk::statInt(c, statRect(2), "DRAW", prefs().getInt("x_d", 0));
  }

  // --- who you play ----------------------------------------------------------
  char chip[32];
  snprintf(chip, sizeof(chip), "vs Sticky - %s",
           _foe == FOE_EASY ? "easy" : _foe == FOE_NORMAL ? "normal"
           : _foe == FOE_HARD ? "hard"
           : prefs().getInt("x_lvl", FOE_HARD) == FOE_EASY ? "easy"
           : prefs().getInt("x_lvl", FOE_HARD) == FOE_NORMAL ? "normal" : "hard");
  gk::choice(c, CHIP_AI, chip, !friendly);
  gk::choice(c, CHIP_FRIEND, "vs a friend", friendly);

  char buf[48];
  if (_over)
    gk::foot(c, "");
  else if (friendly)
    gk::foot(c, "");
  else {
    snprintf(buf, sizeof(buf), "streak %d - best %d", prefs().getInt("x_strk", 0),
             prefs().getInt("x_best", 0));
    gk::foot(c, buf);
  }
}

void XoApp::enter(ToolsHost& h) {
  ToolApp::enter(h);
  _rulesFromMore = false;
  _armedClear = false;
  _sitX = _sitO = _sitD = 0;
  _view = help::suppressed(prefs(), "xo") ? View::Board : View::Rules;
  _rule = (Rule)prefs().getInt("x_rule", RULE_CLASSIC);
  _foe = (Foe)prefs().getInt("x_foe", FOE_HARD);
  if (_rule > RULE_THREE) _rule = RULE_CLASSIC;
  if (_foe > FOE_NORMAL) _foe = FOE_HARD;
  newGame();
}

void XoApp::setFoe(Foe f) {
  _foe = f;
  prefs().putInt("x_foe", (int)f);
  // The level the Sticky last played at, so "vs Sticky" goes back to it.
  if (f != FOE_HUMAN) prefs().putInt("x_lvl", (int)f);
  newGame();
}

void XoApp::tapMenu(int x, int y) {
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
  } else if (!strcmp(what, "Sticky plays")) {
    // Easy -> Normal -> Hard -> Easy. Choosing a level also means playing
    // the Sticky.
    const Foe now = _foe == FOE_HUMAN ? (Foe)prefs().getInt("x_lvl", FOE_HARD) : _foe;
    setFoe(now == FOE_EASY ? FOE_NORMAL : now == FOE_NORMAL ? FOE_HARD : FOE_EASY);
  } else if (!strcmp(what, "Rule")) {
    _rule = (_rule == RULE_THREE) ? RULE_CLASSIC : RULE_THREE;
    prefs().putInt("x_rule", (int)_rule);
    newGame();
  } else if (!strcmp(what, "Clear record")) {
    if (!wasArmed) {
      _armedClear = true;
      host().beep(2);
      host().refresh(false);
      return;
    }
    record::clear(prefs(), record::XO);
    _view = View::Board;
  }
  host().beep(1);
  host().refreshUi();
}

void XoApp::onTap(int x, int y) {
  // Back comes before everything else: out of a card or menu to the board,
  // out of the board to the hub.
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
    if (t == help::Tap::Never) help::suppress(prefs(), "xo");
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

  if (CHIP_AI.hit(x, y)) {
    if (_foe != FOE_HUMAN) return;
    host().beep(1);
    setFoe((Foe)prefs().getInt("x_lvl", FOE_HARD));
    host().refresh(true);
    return;
  }
  if (CHIP_FRIEND.hit(x, y)) {
    if (_foe == FOE_HUMAN) return;
    host().beep(1);
    setFoe(FOE_HUMAN);
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

  // The machine answers inside the same frame -- one e-ink refresh per turn,
  // not two.
  bool aiLifts = false;
  if (!_over && _foe != FOE_HUMAN && _turn == 2) {
    aiLifts = xorules::doomed(_s, 2, three()) >= 0;
    const int m = _foe == FOE_EASY     ? xorules::easyMove(_s, 2, three(), esp_random())
                  : _foe == FOE_NORMAL ? xorules::normalMove(_s, 2, three(), esp_random())
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
