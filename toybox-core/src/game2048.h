#pragma once
#include "chrome.h"

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

 private:
#ifdef TOYBOX_HOST
  // The preview harness cannot play its way to a jammed board or to 2048, and
  // an end screen nobody renders is an end screen nobody checks.
 public:
  void hostSetOver(bool reached) {
    _over = !reached;
    _reached2048 = reached;
    _cheered = false;
    host().refresh(true);
  }

 private:
#endif
  bool _help = false;        // rules card is up
  bool _armedClear = false;  // CLEAR RECORD has been tapped once

  void newGame();
  void saveState();
  bool loadState();
  void spawn();
  bool move(int dir);  // 0 left, 1 right, 2 up, 3 down; returns true if changed
  bool anyMoves() const;

  uint8_t _b[4][4] = {};      // exponents (0 = empty, 1 = "2", ...)
  uint8_t _prevB[4][4] = {};  // undo
  int _score = 0, _prevScore = 0;
  bool _canUndo = false;

  // Which tiles to call out on the next paint: the one that just appeared gets
  // a dashed edge, the ones that just merged flash once. Both are cleared as
  // soon as the next move starts.
  int8_t _newCell = -1;
  bool _merged[16] = {};
  bool _over = false;
  bool _reached2048 = false, _cheered = false;
};

extern G2048App g2048App;
