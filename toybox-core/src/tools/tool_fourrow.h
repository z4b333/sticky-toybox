// Four in a Row (C4). Seven columns, six rows; drop a disc by tapping its
// column, four in a line wins. Against the Sticky at three levels or two
// people passing it round. Undo takes back your move and the Sticky's reply.
//
// The rules and the Sticky's play live in fourrow_rules.h (host-tested).
// You are the solid black disc; the Sticky (or player two) is the ring.
#pragma once
#include <esp_random.h>

#include "fourrow_rules.h"
#include "kit_game.h"
#include "record.h"

namespace c4ui {
inline constexpr int CELL = 66;
inline constexpr int BOARD_W = c4::W * CELL;            // 462
inline constexpr int BOARD_H = c4::H * CELL;            // 396
inline constexpr int BX = (SCREEN_W - BOARD_W) / 2;     // 9
inline constexpr int BY = 136;
inline constexpr int STATUS_Y = 52;
inline constexpr int AIM_Y = 104;                       // the column pointer for the buttons
inline constexpr int STAT_Y = BY + BOARD_H + 18;        // 550
inline TRect statRect(int i) { return TRect{20 + i * 150, STAT_Y, 140, gk::STAT_H}; }
inline constexpr int ROW_Y = STAT_Y + gk::STAT_H + 16;  // 632
inline constexpr TRect UNDO{20, ROW_Y, 128, 60};
inline constexpr TRect CHIP_AI{160, ROW_Y, 160, 60};
inline constexpr TRect CHIP_FRIEND{332, ROW_Y, 128, 60};
// A tap anywhere in a column -- on the board or in the strip above it -- drops.
inline int colAt(int x, int y) {
  if (x < BX || x >= BX + BOARD_W || y < AIM_Y - 8 || y >= BY + BOARD_H) return -1;
  return (x - BX) / CELL;
}
inline TRect cellRect(int col, int row) {  // row 0 is the bottom
  return TRect{BX + col * CELL, BY + (c4::H - 1 - row) * CELL, CELL, CELL};
}
inline TRect colRect(int col) { return TRect{BX + col * CELL, BY, CELL, BOARD_H}; }
}  // namespace c4ui

class FourRowTool : public KitGame {
 public:
  const char* title() const override { return "FOUR IN A ROW"; }

  bool onButton(SideBtn b) override {
    if (_view != View::Play || c4::over(_s.g)) return false;
    if (b == SideBtn::Ok) {
      if (_aim < 0) _aim = 3;
      drop(_aim);
      return true;
    }
    // Up steps right, Down left -- Ships' direction for "next".
    _aim = _aim < 0 ? 3 : (_aim + (b == SideBtn::Up ? 1 : c4::W - 1)) % c4::W;
    host().beep(0);
    host().refresh(false);
    return true;
  }

#ifdef TOYBOX_HOST
  c4::Game& hostGame() { return _s.g; }
  bool hostVsSticky() const { return _s.mode == VS_STICKY; }
  void hostLevel(int l) {
    _s.level = (uint8_t)l;
    prefs().putInt("c4_lvl", l);
  }
#endif

 protected:
  const help::Text& rulesText() const override { return help::FOURROW; }
  const char* helpKey() const override { return "c4"; }

  void load() override {
    _aim = -1;
    _sitA = _sitB = _sitD = 0;
    Saved s;
    if (prefs().getBytesLength("c4_game") == sizeof(s) &&
        prefs().getBytes("c4_game", &s, sizeof(s)) == sizeof(s) && s.level < c4::LEVELS &&
        s.mode <= VS_FRIEND && s.g.moves <= c4::CELLS && !c4::over(s.g)) {
      _s = s;
      return;
    }
    _s.mode = (uint8_t)(geti("c4_mode", VS_STICKY) == VS_FRIEND ? VS_FRIEND : VS_STICKY);
    newGame();
  }

  const char* corner() override {
    return _s.mode == VS_FRIEND ? "2 players" : c4::levelName(_s.level);
  }

  int extraRows(gk::Row* rows) override {
    int n = 0;
    rows[n++] = {"New game", nullptr, false};
    rows[n++] = {"Sticky plays", c4::levelName(nextLevel()), false};
    rows[n++] = {"Who starts", whoName(geti("c4_who", 0)), false};
    return n;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "New game")) {
      newGame();
      return true;
    }
    if (!strcmp(what, "Sticky plays")) {
      prefs().putInt("c4_lvl", (nextLevel() + 1) % c4::LEVELS);
      // A board nobody has touched yet takes the new level at once.
      if (untouched()) newGame();
      return false;
    }
    if (!strcmp(what, "Who starts")) {
      prefs().putInt("c4_who", (geti("c4_who", 0) + 1) % 3);
      if (untouched()) newGame();
      return false;
    }
    return false;
  }
  bool hasRecord() override {
    for (const char* k : record::FOURROW)
      if (geti(k)) return true;
    return false;
  }
  void clearRecord() override { record::clear(prefs(), record::FOURROW); }

  void renderPlay(ToolsCanvas& c) override {
    using namespace c4ui;
    const bool friendly = _s.mode == VS_FRIEND;
    const int w = c4::winner(_s.g);
    const char* status;
    if (w >= 0)
      status = friendly ? (w == 0 ? "Black wins" : "White wins") : (w == 0 ? "You won" : "Sticky won");
    else if (c4::full(_s.g))
      status = "Draw";
    else if (friendly)
      status = c4::toMove(_s.g) == 0 ? "Black to move" : "White to move";
    else
      status = "Your move";
    c.text(BX + 8, STATUS_Y, status, TS_LARGE, true, true);
    const char* tag = friendly ? "pass and play" : "you are black";
    const int tw = c.textWidth(tag, TS_SMALL) + 20;
    c.drawRect(BX + BOARD_W - 8 - tw, STATUS_Y + 2, tw, 32, 2, true);
    c.textInBox(BX + BOARD_W - 8 - tw, STATUS_Y + 2, tw, 32, tag, TS_SMALL, true);

    // The pointer the side buttons move.
    if (_aim >= 0 && w < 0) {
      const int cx = BX + _aim * CELL + CELL / 2;
      decor::triangle(c, cx - 14, AIM_Y, cx + 14, AIM_Y, cx, AIM_Y + 18, true);
    }

    // The board: a 3 px frame, hairline slots, discs.
    c.drawRect(BX - 3, BY - 3, BOARD_W + 6, BOARD_H + 6, 3, true);
    int wc[4] = {-1, -1, -1, -1}, wr[4] = {-1, -1, -1, -1};
    const bool line = c4::winLine(_s.g, wc, wr);
    const int last = c4::lastCol(_s.g);
    for (int col = 0; col < c4::W; col++)
      for (int row = 0; row < c4::H; row++) {
        const TRect r = cellRect(col, row);
        const int cx = r.x + CELL / 2, cy = r.y + CELL / 2;
        const int who = c4::at(_s.g, col, row);
        if (who == 0) {
          c.drawCircle(cx, cy, 26, 1, true);
          continue;
        }
        if (who == 1)
          c.fillCircle(cx, cy, 28, true);
        else
          c.drawCircle(cx, cy, 28, 8, true);
        // The newest disc carries a dot in the other colour, so a move and the
        // reply that arrives with it can both be found.
        if (col == last && row == _s.g.height[col] - 1) c.fillCircle(cx, cy, 6, who != 1);
      }
    if (line)
      for (int k = 0; k < 4; k++) {
        const TRect r = cellRect(wc[k], wr[k]);
        c.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, true);
      }

    char k[8];
    if (friendly) {
      gk::statInt(c, statRect(0), "BLACK", _sitA);
      gk::statInt(c, statRect(1), "WHITE", _sitB);
      gk::statInt(c, statRect(2), "DRAW", _sitD);
    } else {
      key(k, sizeof(k), "c4_w", _s.level);
      gk::statInt(c, statRect(0), "WON", geti(k));
      key(k, sizeof(k), "c4_l", _s.level);
      gk::statInt(c, statRect(1), "LOST", geti(k));
      key(k, sizeof(k), "c4_d", _s.level);
      gk::statInt(c, statRect(2), "DRAW", geti(k));
    }
    const bool undoOk = canUndo();
    c.fillRect(UNDO.x, UNDO.y, UNDO.w, UNDO.h, false);
    c.drawRect(UNDO.x, UNDO.y, UNDO.w, UNDO.h, undoOk ? 2 : 1, true);
    c.textInBox(UNDO.x, UNDO.y, UNDO.w, UNDO.h, "Undo", TS_MED, true, undoOk);
    gk::choice(c, CHIP_AI, "vs Sticky", !friendly);
    gk::choice(c, CHIP_FRIEND, "2 players", friendly);
    gk::foot(c, c4::over(_s.g) ? "tap the board to play again"
                : friendly     ? "tap a column - pass the Sticky"
                               : "tap a column to drop");
  }

  void tapPlay(int x, int y) override {
    using namespace c4ui;
    if (UNDO.hit(x, y)) {
      if (!canUndo()) {
        host().beep(2);
        return;
      }
      c4::undo(_s.g);
      if (_s.mode == VS_STICKY)
        while (_s.g.moves > 0 && c4::toMove(_s.g) != 0) c4::undo(_s.g);
      save();
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (CHIP_AI.hit(x, y) || CHIP_FRIEND.hit(x, y)) {
      const uint8_t m = CHIP_AI.hit(x, y) ? VS_STICKY : VS_FRIEND;
      if (m == _s.mode) return;
      _s.mode = m;
      prefs().putInt("c4_mode", m);
      _sitA = _sitB = _sitD = 0;
      newGame();
      host().beep(1);
      host().refresh(false);
      return;
    }
    const int col = colAt(x, y);
    if (col < 0) return;
    if (c4::over(_s.g)) {
      newGame();
      host().beep(1);
      host().refresh(true);
      return;
    }
    _aim = -1;  // a finger is back in charge; the pointer was for the buttons
    drop(col);
  }

 private:
  enum : uint8_t { VS_STICKY = 0, VS_FRIEND = 1 };
  struct Saved {
    c4::Game g;
    uint8_t mode;
    uint8_t level;  // the Sticky's level for THIS game, so its result lands there
  };
  Saved _s{};
  int8_t _aim = -1;
  // Two people sharing the Sticky get a tally for as long as they sit there
  // (XO's rule); nothing of it is kept.
  int _sitA = 0, _sitB = 0, _sitD = 0;

  static const char* whoName(int w) { return w == 1 ? "You" : w == 2 ? "Sticky" : "Take turns"; }
  int nextLevel() {
    const int l = geti("c4_lvl", c4::NORMAL);
    return l >= 0 && l < c4::LEVELS ? l : c4::NORMAL;
  }
  // Nothing but the Sticky's opening disc on the board: a setting may restart it.
  bool untouched() const {
    return _s.g.moves == 0 || (_s.mode == VS_STICKY && _s.g.moves == 1 && _s.g.first == 1);
  }
  bool canUndo() const {
    if (c4::over(_s.g)) return false;  // the result is written; a new game is the way on
    if (_s.mode == VS_FRIEND) return _s.g.moves > 0;
    return _s.g.moves > (_s.g.first == 1 ? 1 : 0);
  }

  void save() { prefs().putBytes("c4_game", &_s, sizeof(_s)); }

  void newGame() {
    _s.level = (uint8_t)nextLevel();
    // Who opens: always you, always the Sticky, or by turns -- "c4_next"
    // remembers whose turn it is to start.
    const int who = geti("c4_who", 0);
    int first = who == 1 ? 0 : who == 2 ? 1 : geti("c4_next", 0) & 1;
    if (who == 0) prefs().putInt("c4_next", first ^ 1);
    c4::reset(_s.g, first);
    _aim = -1;
    if (_s.mode == VS_STICKY && c4::toMove(_s.g) == 1) c4::play(_s.g, stickyMove());
    save();
  }

  int stickyMove() {
    return c4::bestMove(_s.g, _s.level, [] { return (uint32_t)esp_random(); });
  }

  void drop(int col) {
    if (!c4::canPlay(_s.g, col)) {
      host().beep(2);  // a full column says no
      return;
    }
    c4::play(_s.g, col);
    // The Sticky answers inside the same move: one refresh shows both discs.
    if (_s.mode == VS_STICKY && !c4::over(_s.g)) c4::play(_s.g, stickyMove());
    if (c4::over(_s.g)) {
      finish();
      return;
    }
    save();
    host().beep(0);
    host().refresh(false);
  }

  void finish() {
    const int w = c4::winner(_s.g);
    if (_s.mode == VS_FRIEND) {
      (w == 0 ? _sitA : w == 1 ? _sitB : _sitD)++;
    } else {
      char k[8];
      key(k, sizeof(k), w == 0 ? "c4_w" : w == 1 ? "c4_l" : "c4_d", _s.level);
      bump(k);
    }
    // A finished game is not resumed: the next open starts fresh.
    prefs().remove("c4_game");
    host().beep(w == 0 || _s.mode == VS_FRIEND ? 3 : 2);
    host().refresh(true);
  }
};
