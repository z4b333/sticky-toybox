#pragma once
#include "chrome.h"

class WordleApp : public ToolApp {
 public:
  const char* title() const override { return "WORDLE"; }
  void enter(ToolsHost& h) override;
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;

 private:
  bool _help = false;        // rules card is up
  bool _armedClear = false;  // CLEAR RECORD has been tapped once

  void newGame();
  void drawBoard(ToolsCanvas& c);
  void drawKeyboard(ToolsCanvas& c);
  void submitGuess();
  void keyPressed(char c);

  char _answer[6] = {};
  char _grid[6][5] = {};      // entered letters
  uint8_t _score[6][5] = {};  // 0 pending, 1 absent, 2 present, 3 correct
  uint8_t _keyState[26] = {}; // 0 unknown, 1 absent, 2 present, 3 correct
  int _row = 0, _col = 0;
  bool _over = false, _won = false;
  char _msg[28] = {};
};

extern WordleApp wordleApp;
