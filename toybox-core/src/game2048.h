#pragma once
#include "chrome.h"
#include "tools/game_kit.h"

// v2 (kit "2048"): SCORE and BEST TILE on top, the board edge to edge, Undo and
// New game under it, the hint at the foot. The end of a game (and reaching
// 2048) is the shared result screen.
namespace g2048ui {
inline constexpr TRect SCORE{20, 54, 214, gk::STAT_H};
inline constexpr TRect BEST{246, 54, 214, gk::STAT_H};
inline constexpr int FRAME = 3, PAD = 10, GAP = 10;
inline constexpr int BOARD = 440;                                     // edge to edge, less 20
inline constexpr int BX = 20, BY = 136;
inline constexpr int TILE = (BOARD - 2 * FRAME - 2 * PAD - 3 * GAP) / 4;  // 96
inline constexpr TRect UNDO{20, BY + BOARD + 18, 214, 64};
inline constexpr TRect NEW{246, BY + BOARD + 18, 214, 64};
}  // namespace g2048ui

class G2048App : public ToolApp {
 public:
  const char* title() const override { return "2048"; }
  void enter(ToolsHost& h) override;
#ifdef TOYBOX_HOST
  // The resume guard needs to compare boards across a destroy and rebuild.
  const uint8_t* hostBoard() const { return &_b[0][0]; }
#endif
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;
  void onSwipe(int dx, int dy) override;
  // Side buttons slide the board: upper = up, lower = down (kit foot line).
  bool onButton(SideBtn b) override;

 private:
#ifdef TOYBOX_HOST
  // The preview harness cannot play its way to a jammed board or to 2048, and
  // an end screen nobody renders is an end screen nobody checks.
 public:
  void hostSetOver(bool reached) {
    _over = !reached;
    _reached2048 = reached;
    _cheered = false;
    _result = true;
    host().refresh(true);
  }

 private:
#endif
  enum class View : uint8_t { Board, Menu, Rules };
  View _view = View::Board;
  bool _armedClear = false;  // Clear record has been tapped once (More)
  bool _rulesFromMore = false;  // the card came from More: Back returns to the game
  bool _result = false;      // the result screen is up over the board

  void newGame();
  void saveState();
  bool loadState();
  void spawn();
  bool move(int dir);  // 0 left, 1 right, 2 up, 3 down; returns true if changed
  void slide(int dir);
  bool anyMoves() const;
  void renderBoard(ToolsCanvas& c);
  void renderResult(ToolsCanvas& c);
  int menuRows(gk::Row* rows);

  uint8_t _b[4][4] = {};      // exponents (0 = empty, 1 = "2", ...)
  uint8_t _prevB[4][4] = {};  // undo
  int _score = 0, _prevScore = 0;
  bool _canUndo = false;

  // Which tiles to call out on the next paint: the one that just appeared gets
  // a dashed edge, the ones that just merged a corner wedge. Both are cleared
  // as soon as the next move starts.
  int8_t _newCell = -1;
  bool _merged[16] = {};
  bool _over = false;
  bool _reached2048 = false, _cheered = false;
};

extern G2048App g2048App;
