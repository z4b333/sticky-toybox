// Mines (C3, owner: replaces Nonogram). The Sudoku board's 9 x 9 grid and
// geometry, so the two read as one family. Tap to dig, hold to flag; the
// first tap is always safe and opens a patch; tapping a finished number opens
// round it. Hidden squares are black and dug ones white, the 1-bit way of
// wiping the board clean (E-Ink Arcade's Minesweeper).
//
// The rules live in tools/mines_rules.h (no hardware, host-tested).
#pragma once
#include "chrome.h"
#include "tools/game_kit.h"
#include "tools/mines_rules.h"

namespace minesui {
inline constexpr int CELL = 50;
inline constexpr int GX = (SCREEN_W - mns::N * CELL) / 2, GY = 52;  // Sudoku's
inline constexpr int GRID = mns::N * CELL;                          // 450
inline constexpr int STAT_Y = GY + GRID + 18;                       // 520
inline TRect statRect(int i) { return TRect{20 + i * 150, STAT_Y, 140, gk::STAT_H}; }
inline constexpr TRect NEW{20, STAT_Y + gk::STAT_H + 16, 440, 64};
inline constexpr int MSG_Y = NEW.y + NEW.h + 18;
inline TRect cellRect(int k) {
  return TRect{GX + mns::xOf(k) * CELL, GY + mns::yOf(k) * CELL, CELL, CELL};
}
}  // namespace minesui

class MinesApp : public ToolApp {
 public:
  const char* title() const override { return "MINES"; }
  void enter(ToolsHost& h) override;
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;
  void onHold(int x, int y) override;
  uint16_t holdMs() const override { return 450; }

#ifdef TOYBOX_HOST
  mns::Board& hostBoard() { return _b; }
  void hostShow() { host().refresh(true); }
  void hostResult() {
    _view = View::Result;
    host().refresh(true);
  }
#endif

 private:
  enum class View : uint8_t { Board, Result, Menu, Rules };
  View _view = View::Board;
  bool _armedClear = false;
  bool _rulesFromMore = false;  // the card came from More: Back returns to the game

  void newGame();
  void saveState();
  bool loadState();
  void finish();  // after a tap that may have ended the game
  int menuRows(gk::Row* rows);
  void tapMenu(int x, int y);
  void renderBoard(ToolsCanvas& c);
  void renderResult(ToolsCanvas& c);

  mns::Board _b{};
  int _level = 0;  // the next game's
  char _msg[40] = {};
};
