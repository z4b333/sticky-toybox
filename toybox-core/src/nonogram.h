#pragma once
#include "chrome.h"
#include "nonogram_solver.h"

class NonogramApp : public ToolApp {
 public:
  const char* title() const override { return "NONOGRAM"; }
  void enter(ToolsHost& h) override;
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;

 private:
#ifdef TOYBOX_HOST
 public:
  void hostSetWon(uint32_t secs) {
    _won = true;
    _elapsed = secs;
    host().refresh(true);
  }

 private:
#endif
  bool _help = false;        // rules card is up
  bool _armedClear = false;  // CLEAR RECORD has been tapped once

  // Best times and solved counts, across both sizes.
  bool hasRecord();

  static constexpr int MAXN = nono::MAXN;
  static constexpr int MAXG = nono::MAXG;

  void newPuzzle(int n);
  void saveState();
  bool loadState();
  void layoutFor(int n);
  bool checkWin();
  void giveHint();
  void onWin();

  int _n = 10;                // board size (5 or 10)
  uint8_t _sol[MAXN][MAXN];   // solution 0/1
  uint8_t _cell[MAXN][MAXN];  // player: 0 empty, 1 filled, 2 X
  nono::Clues _clues;
  bool _fillMode = true;
  bool _won = false;
  int _hintsUsed = 0;
  uint32_t _startMs = 0, _elapsed = 0;

  // layout (computed per size)
  int _cellPx = 32, _gridX = 165, _gridY = 145;
};

extern NonogramApp nonogramApp;
