#pragma once
#include "chrome.h"
#include "tools/game_kit.h"

// v2 (kit "Wordle" and "Wordle, solved"). One word a day by default -- the
// same word all day, chosen by the date -- and New word for more. The game in
// hand is kept across leaving and power-off, so today's word cannot be
// re-rolled by walking out of it.
namespace wordui {
inline constexpr int TILE = 66, TGAP = 7;
inline constexpr int BOARD_W = 5 * TILE + 4 * TGAP;          // 358
inline constexpr int BOARD_X = (SCREEN_W - BOARD_W) / 2;     // 61
inline constexpr int BOARD_Y = TOPBAR_H + 12;                // 52
inline constexpr int BOARD_H = 6 * TILE + 5 * TGAP;          // 431
inline constexpr int LEGEND_Y = BOARD_Y + BOARD_H + 14;      // 497
inline constexpr int KEY_W = 44, KEY_H = 54, KGAP = 4, KROW = 60;
inline constexpr int ROW1_Y = 536, ROW2_Y = ROW1_Y + KROW, ROW3_Y = ROW2_Y + KROW;
inline constexpr int ROW1_X = (SCREEN_W - (10 * KEY_W + 9 * KGAP)) / 2;  // 2
inline constexpr int ROW2_X = (SCREEN_W - (9 * KEY_W + 8 * KGAP)) / 2;   // 26
inline constexpr int ENTER_W = 66;
inline constexpr int ROW3_X = (SCREEN_W - (ENTER_W * 2 + 7 * KEY_W + 8 * KGAP)) / 2;  // 4
// Where a letter's key sits, for the harness: -1 when it is not a letter.
inline TRect keyRect(char ch) {
  const char* r1 = "QWERTYUIOP";
  const char* r2 = "ASDFGHJKL";
  const char* r3 = "ZXCVBNM";
  for (int i = 0; r1[i]; i++)
    if (r1[i] == ch) return TRect{ROW1_X + i * (KEY_W + KGAP), ROW1_Y, KEY_W, KEY_H};
  for (int i = 0; r2[i]; i++)
    if (r2[i] == ch) return TRect{ROW2_X + i * (KEY_W + KGAP), ROW2_Y, KEY_W, KEY_H};
  for (int i = 0; r3[i]; i++)
    if (r3[i] == ch)
      return TRect{ROW3_X + ENTER_W + KGAP + i * (KEY_W + KGAP), ROW3_Y, KEY_W, KEY_H};
  return TRect{0, 0, 0, 0};
}
inline constexpr TRect ENTER{ROW3_X, ROW3_Y, ENTER_W, KEY_H};
inline constexpr TRect DEL{ROW3_X + ENTER_W + KGAP + 7 * (KEY_W + KGAP), ROW3_Y, ENTER_W, KEY_H};
inline constexpr TRect BOARD{BOARD_X, BOARD_Y, BOARD_W, BOARD_H};
}  // namespace wordui

class WordleApp : public ToolApp {
 public:
  const char* title() const override { return "WORDLE"; }
  void enter(ToolsHost& h) override;
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;

#ifdef TOYBOX_HOST
  const char* hostAnswer() const { return _answer; }
  bool hostDaily() const { return _daily; }
#endif

 private:
  enum class View : uint8_t { Board, Result, Menu, Rules };
  View _view = View::Board;
  bool _armedClear = false;  // Clear record has been tapped once (More)
  bool _rulesFromMore = false;  // the card came from More: Back returns to the game

  // The day number the clock gives (days since 1 Jan 2000), or -1 without one.
  int today();
  void startDaily(int day);
  void startExtra();
  void begin(int answerIdx);
  void saveState();
  bool loadState();
  void drawBoard(ToolsCanvas& c);
  void drawLegend(ToolsCanvas& c);
  void drawKeyboard(ToolsCanvas& c);
  void renderResult(ToolsCanvas& c);
  int menuRows(gk::Row* rows);
  void tapMenu(int x, int y);
  void submitGuess();
  void keyPressed(char c);

  char _answer[6] = {};
  int16_t _answerIdx = 0;
  int32_t _day = -1;   // the day this game is the word of, or -1 for an extra
  bool _daily = false;
  char _grid[6][5] = {};      // entered letters
  uint8_t _score[6][5] = {};  // 0 pending, 1 absent, 2 present, 3 correct
  uint8_t _keyState[26] = {}; // 0 unknown, 1 absent, 2 present, 3 correct
  int _row = 0, _col = 0;
  bool _over = false, _won = false;
  char _msg[28] = {};
};

extern WordleApp wordleApp;
