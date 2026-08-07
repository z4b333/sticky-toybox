// Coin flip: 1, 5 or 10 coins at a time, with a persistent heads/tails tally.
#pragma once
#include <esp_random.h>

#include "tools_draw.h"

namespace coinui {
inline constexpr int STAGE_CX = 240;
inline constexpr TRect FLIP1{60, 410, 360, 84};
inline constexpr TRect FLIP5{60, 506, 170, 56};
inline constexpr TRect FLIP10{250, 506, 170, 56};
inline constexpr TRect RESET{60, 734, 360, 42};

inline void btn(ToolsCanvas& c, const TRect& r, const char* label, bool filled,
                TSize sz = TS_MED) {
  c.button(r.x, r.y, r.w, r.h, label, filled, sz);
}
}  // namespace coinui

class CoinTool : public ToolApp {
 public:
  const char* title() const override { return "COIN FLIP"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _n = 0;  // nothing flipped yet this session
    _heads = prefs().getUInt("c_heads", 0);
    _tails = prefs().getUInt("c_tails", 0);
  }

  void render(ToolsCanvas& c) override {
    using namespace coinui;
    host().topBar(title());

    if (_n == 0) {
      c.drawCircle(STAGE_CX, 220, 100, 3, true);
      c.textInBox(STAGE_CX - 100, 120, 200, 200, "?", TS_HUGE, true, true);
      c.textCentered(STAGE_CX, 348, "tap FLIP to start", TS_LARGE, true);
    } else if (_n == 1) {
      drawBigCoin(c, STAGE_CX, 220, 105, _flips[0]);
      c.textCentered(STAGE_CX, 344, _flips[0] ? "HEADS" : "TAILS", TS_HUGE, true, true);
    } else {
      drawCoinGrid(c);
      int h = 0;
      for (int i = 0; i < _n; i++) h += _flips[i] ? 1 : 0;
      char buf[40];
      snprintf(buf, sizeof(buf), "%d heads  /  %d tails", h, _n - h);
      c.textCentered(STAGE_CX, 370, buf, TS_LARGE, true, true);
    }

    // --- controls and tally ---
    btn(c, FLIP1, "FLIP", true, TS_LARGE);
    btn(c, FLIP5, "FLIP x5", false, TS_MED);
    btn(c, FLIP10, "FLIP x10", false, TS_MED);

    c.drawLine(20, 588, c.width() - 20, 588, 1, true);
    c.text(20, 592, "ALL-TIME TALLY", TS_MED, true);

    char buf[40];
    snprintf(buf, sizeof(buf), "heads  %lu", (unsigned long)_heads);
    c.text(20, 620, buf, TS_MED, true);
    snprintf(buf, sizeof(buf), "tails  %lu", (unsigned long)_tails);
    c.text(250, 620, buf, TS_MED, true);

    const uint32_t total = _heads + _tails;
    if (total > 0) {
      tdraw::progressBar(c, 20, 660, c.width() - 40, 24, (int)((_heads * 1000ULL) / total));
      snprintf(buf, sizeof(buf), "%lu%% heads of %lu flips", (unsigned long)(_heads * 100 / total),
               (unsigned long)total);
      c.text(20, 696, buf, TS_MED, true);
    }
    btn(c, RESET, "RESET TALLY", false, TS_MED);
  }
  void onTap(int x, int y) override {
    using namespace coinui;
    if (host().isBackTap(x, y)) {
      host().goHub();
      return;
    }
    if (FLIP1.hit(x, y)) return flip(1);
    if (FLIP5.hit(x, y)) return flip(5);
    if (FLIP10.hit(x, y)) return flip(10);
    if (RESET.hit(x, y)) {
      _heads = _tails = 0;
      prefs().putUInt("c_heads", 0);
      prefs().putUInt("c_tails", 0);
      host().beep(1);
      host().refresh(true);
    }
  }

 private:
  void flip(int n) {
    _n = n;
    for (int i = 0; i < n; i++) {
      const bool headsUp = (esp_random() & 1u) != 0;
      _flips[i] = headsUp;
      if (headsUp)
        _heads++;
      else
        _tails++;
    }
    prefs().putUInt("c_heads", _heads);
    prefs().putUInt("c_tails", _tails);
    host().beep(1);
    // Big solid shapes ghost badly under a differential refresh — repaint fully.
    host().refresh(true);
  }

  // Heads = outlined coin with a black letter; tails = solid coin, white letter.
  static void drawBigCoin(ToolsCanvas& c, int cx, int cy, int r, bool headsUp) {
    if (headsUp) {
      c.fillCircle(cx, cy, r, false);
      c.drawCircle(cx, cy, r, 6, true);
      c.drawCircle(cx, cy, r - 16, 2, true);
      c.textInBox(cx - r, cy - r, 2 * r, 2 * r, "H", TS_HUGE, true, true);
    } else {
      c.fillCircle(cx, cy, r, true);
      c.fillCircle(cx, cy, r - 16, false);
      c.fillCircle(cx, cy, r - 22, true);
      c.textInBox(cx - r, cy - r, 2 * r, 2 * r, "T", TS_HUGE, false, true);
    }
  }

  void drawCoinGrid(ToolsCanvas& c) {
    constexpr int perRow = 4, r = 44, stepX = 108, stepY = 112;
    const int rows = (_n + perRow - 1) / perRow;
    const int y0 = 204 - ((rows - 1) * stepY) / 2;
    for (int i = 0; i < _n; i++) {
      const int col = i % perRow;
      const int row = i / perRow;
      const int inRow = (row == rows - 1) ? (_n - row * perRow) : perRow;
      const int x0 = coinui::STAGE_CX - ((inRow - 1) * stepX) / 2;
      const int cx = x0 + col * stepX;
      const int cy = y0 + row * stepY;
      if (_flips[i]) {
        c.drawCircle(cx, cy, r, 4, true);
        c.textInBox(cx - r, cy - r, 2 * r, 2 * r, "H", TS_LARGE, true, true);
      } else {
        c.fillCircle(cx, cy, r, true);
        c.textInBox(cx - r, cy - r, 2 * r, 2 * r, "T", TS_LARGE, false, true);
      }
    }
  }

  bool _flips[10] = {};
  int _n = 0;
  uint32_t _heads = 0, _tails = 0;
};
