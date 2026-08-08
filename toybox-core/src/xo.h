// Noughts and crosses, with the one rule that makes 3x3 worth replaying.
//
// The rules and the search live in xo_rules.h (no hardware, host-testable);
// this class is only the screen around them.
#pragma once
#include "chrome.h"
#include "xo_rules.h"

class XoApp : public ToolApp {
 public:
  const char* title() const override { return "XO"; }
  void enter(ToolsHost& h) override;
  void render(ToolsCanvas& c) override;
  void onTap(int x, int y) override;

  // Rule set and opponent are persisted, so the device comes back how you left it.
  enum Rule : uint8_t { RULE_CLASSIC = 0, RULE_THREE = 1 };
  enum Foe : uint8_t { FOE_EASY = 0, FOE_HARD = 1, FOE_HUMAN = 2 };

 private:
  bool _help = false;        // rules card is up
  bool _armedClear = false;  // CLEAR RECORD has been tapped once

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
