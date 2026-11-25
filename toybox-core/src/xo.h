// Noughts and crosses, with the one rule that makes 3x3 worth replaying.
//
// The rules and the search live in xo_rules.h (no hardware, host-testable);
// this class is only the screen around them.
#pragma once
#include "chrome.h"
#include "tools/game_kit.h"
#include "xo_rules.h"

// v2 (kit "XO"): whose move and which mark you are, the open 3x3 grid edge to
// edge, WON / LOST / DRAW, and two chips -- against the Sticky or a friend.
// The Sticky's last move is striped. Level and the 3-marks rule live in More.
namespace xoui {
inline constexpr int CELL = 140;
inline constexpr int BOARD = 3 * CELL;               // 420
inline constexpr int BX = (SCREEN_W - BOARD) / 2;   // 30
inline constexpr int BY = 112;
inline constexpr int STAT_Y = BY + BOARD + 20;      // 552
inline constexpr TRect CHIP_AI{30, STAT_Y + gk::STAT_H + 16, 236, 56};
inline constexpr TRect CHIP_FRIEND{276, STAT_Y + gk::STAT_H + 16, 174, 56};
inline TRect statRect(int i) { return TRect{30 + i * 144, STAT_Y, 132, gk::STAT_H}; }
}  // namespace xoui

class XoApp : public ToolApp {
 public:
  const char* title() const override { return "XO"; }
  void enter(ToolsHost& h) override;
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;
#ifdef TOYBOX_HOST
  int hostCell(int i) const { return _s.cell[i]; }
#endif

  // Rule set and opponent are persisted, so the device comes back how you left it.
  enum Rule : uint8_t { RULE_CLASSIC = 0, RULE_THREE = 1 };
  // Stored in NVS as x_foe; NORMAL came later (C3) and took the next number.
  enum Foe : uint8_t { FOE_EASY = 0, FOE_HARD = 1, FOE_HUMAN = 2, FOE_NORMAL = 3 };

 private:
  enum class View : uint8_t { Board, Menu, Rules };
  View _view = View::Board;
  bool _armedClear = false;  // Clear record has been tapped once (More)
  bool _rulesFromMore = false;  // the card came from More: Back returns to the game
  int menuRows(gk::Row* rows);
  void tapMenu(int x, int y);
  void setFoe(Foe f);
  static const char* levelName(Foe f);

  void newGame();
  void place(int cell);           // apply the side-to-move's mark, then judge
  void recordResult(int result);  // 1 = human won, -1 = lost, 0 = drew
  bool three() const { return _rule == RULE_THREE; }

  xorules::State _s{};
  uint8_t _turn = 1;    // 1 = X to move, 2 = O
  uint8_t _over = 0;    // 0 playing, 1 X won, 2 O won, 3 drawn
  int8_t _lastAi = -1;  // cell the machine just took, for the "it moved here" dot
  int _plies = 0;       // THREE has no natural end; cap the chase
  Rule _rule = RULE_CLASSIC;
  Foe _foe = FOE_HARD;

  // Pass-and-play keeps no permanent record -- two people sharing one device
  // are not a run of form. It does keep a count for as long as they are sitting
  // there, which is the thing they actually argue about.
  int _sitX = 0, _sitO = 0, _sitD = 0;
};

extern XoApp xoApp;
