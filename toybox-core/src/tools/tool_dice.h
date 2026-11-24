// Tabletop dice roller: D4/D6/D8/D10/D12/D20, 1-6 dice, -10..+10 modifier.
// Natural maximum rolls are highlighted (filled die), natural 1s get a double
// border — the two results a tabletop player actually looks for.
#pragma once
#include <esp_random.h>

#include "dice_shapes.h"
#include "tools_draw.h"

namespace diceui {
// v2 (kit "Dice"): the kind as a row of chips, how many and what to add,
// the dice large, the total under them, earlier rolls as small chips.
inline constexpr int TYPE_W = 70, TYPE_H = 52, TYPE_GAP = 4;
inline constexpr int TYPE_X0 = (480 - (6 * TYPE_W + 5 * TYPE_GAP)) / 2;
inline constexpr int TYPE_Y0 = 52;
inline constexpr int STEP = 52;
inline constexpr TRect CNT_DN{240, 120, STEP, STEP};
inline constexpr TRect CNT_UP{408, 120, STEP, STEP};
inline constexpr TRect MOD_DN{240, 180, STEP, STEP};
inline constexpr TRect MOD_UP{408, 180, STEP, STEP};
inline constexpr TRect BAND{20, 248, 440, 290};
inline constexpr TRect ROLL{20, 724, 440, 64};

inline constexpr int SIDES[6] = {4, 6, 8, 10, 12, 20};

inline TRect typeRect(int i) {
  return TRect{TYPE_X0 + i * (TYPE_W + TYPE_GAP), TYPE_Y0, TYPE_W, TYPE_H};
}
}  // namespace diceui

class DiceTool : public ToolApp {
 public:
  const char* title() const override { return "DICE"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _typeIdx = prefs().getInt("d_type", 1);  // default D6
    _count = prefs().getInt("d_count", 1);
    _mod = prefs().getInt("d_mod", 0);
    if (_typeIdx < 0 || _typeIdx > 5) _typeIdx = 1;
    if (_count < 1 || _count > 6) _count = 1;
    if (_mod < -10 || _mod > 10) _mod = 0;
    _rolled = 0;
    _histN = 0;
  }

  void render(ToolsCanvas& c) override {
    using namespace diceui;
    host().topBar(title());
    const int sides = SIDES[_typeIdx];
    char buf[24];

    // --- the kind of die ---------------------------------------------------
    for (int i = 0; i < 6; i++) {
      const TRect r = typeRect(i);
      const bool on = i == _typeIdx;
      c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
      snprintf(buf, sizeof(buf), "D%d", SIDES[i]);
      c.textInBox(r.x, r.y, r.w, r.h, buf, TS_MED, true, on);
    }
    // --- how many, and what to add ----------------------------------------
    c.text(20, CNT_DN.y + 12, "How many", TS_MED, true);
    c.stepper(CNT_DN.x, CNT_DN.y, STEP, "-", _count > 1);
    snprintf(buf, sizeof(buf), "%d", _count);
    c.textInBox(CNT_DN.x + STEP, CNT_DN.y, CNT_UP.x - CNT_DN.x - STEP, STEP, buf, TS_LARGE, true, true);
    c.stepper(CNT_UP.x, CNT_UP.y, STEP, "+", _count < 6);
    c.text(20, MOD_DN.y + 12, "Add", TS_MED, true);
    c.stepper(MOD_DN.x, MOD_DN.y, STEP, "-", _mod > -10);
    snprintf(buf, sizeof(buf), "%+d", _mod);
    c.textInBox(MOD_DN.x + STEP, MOD_DN.y, MOD_UP.x - MOD_DN.x - STEP, STEP, buf, TS_LARGE, true, true);
    c.stepper(MOD_UP.x, MOD_UP.y, STEP, "+", _mod < 10);

    // --- the dice ----------------------------------------------------------
    // One die drawn large, six drawn small; the band never moves, so the
    // total below it never moves either.
    constexpr int GAP = 12, MAX_CELL = 180;
    const int n = _rolled > 0 ? _rolled : _count;
    const int cols = n < 3 ? n : 3;
    const int rows = (n + 2) / 3;
    int cell = (BAND.w - (cols - 1) * GAP) / cols;
    const int fits = (BAND.h - (rows - 1) * GAP) / rows;
    if (fits < cell) cell = fits;
    if (cell > MAX_CELL) cell = MAX_CELL;
    const int gw = cols * cell + (cols - 1) * GAP, gh = rows * cell + (rows - 1) * GAP;
    const int GX = BAND.x + (BAND.w - gw) / 2, GY = BAND.y + (BAND.h - gh) / 2;
    if (_rolled == 0) {
      for (int i = 0; i < n; i++) {
        const int x = GX + (i % cols) * (cell + GAP), y = GY + (i / cols) * (cell + GAP);
        tdraw::roundRect(c, x, y, cell, cell, cell / 6, 1, true);
      }
      c.textCentered(c.width() / 2, BAND.y + BAND.h / 2 - 14, "tap to roll", TS_LARGE, true);
    }
    for (int i = 0; i < _rolled; i++) {
      const int x = GX + (i % cols) * (cell + GAP), y = GY + (i / cols) * (cell + GAP);
      drawDie(c, x, y, cell, _values[i], sides);
    }

    // --- total, and what came before --------------------------------------
    if (_rolled > 0) {
      c.textTrackedCentered(c.width() / 2, 548, "TOTAL", TS_MED, true, true, 3);
      snprintf(buf, sizeof(buf), "%d", _total);
      c.textCentered(c.width() / 2, 578, buf, TS_HUGE, true, true);
    }
    formula(buf, sizeof(buf));
    c.textCentered(c.width() / 2, 632, buf, TS_MED, true);
    if (_histN > 1) {
      int x = 20;
      c.text(x, 676, "earlier", TS_MED, true);
      x += c.textWidth("earlier", TS_MED) + 12;
      for (int i = 1; i < _histN && x < 440; i++) {
        snprintf(buf, sizeof(buf), "%d", _histTotal[i]);
        const int w = c.textWidth(buf, TS_MED) + 18;
        if (x + w > 460) break;
        c.drawRect(x, 672, w, 36, 1, true);
        c.textInBox(x, 672, w, 36, buf, TS_MED, true);
        x += w + 8;
      }
    }
    c.button(ROLL.x, ROLL.y, ROLL.w, ROLL.h, "Roll", true, TS_LARGE);
  }

  void onTap(int x, int y) override {
    using namespace diceui;
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    for (int i = 0; i < 6; i++) {
      if (typeRect(i).hit(x, y)) {
        _typeIdx = i;
        prefs().putInt("d_type", i);
        _rolled = 0;
        host().beep(0);
        host().refreshUi();
        return;
      }
    }
    if (CNT_DN.hit(x, y) && _count > 1) return setCount(_count - 1);
    if (CNT_UP.hit(x, y) && _count < 6) return setCount(_count + 1);
    if (MOD_DN.hit(x, y) && _mod > -10) return setMod(_mod - 1);
    if (MOD_UP.hit(x, y) && _mod < 10) return setMod(_mod + 1);
    if (ROLL.hit(x, y) || BAND.hit(x, y)) return roll();
  }

  // DOWN rolls, as the kit's foot says.
  bool onButton(SideBtn b) override {
    if (b != SideBtn::Down) return false;
    roll();
    return true;
  }

 private:
  void setCount(int v) {
    _count = v;
    prefs().putInt("d_count", v);
    _rolled = 0;
    host().beep(0);
    host().refreshUi();
  }
  void setMod(int v) {
    _mod = v;
    prefs().putInt("d_mod", v);
    host().beep(0);
    host().refresh(false);
  }

  void formula(char* out, size_t n) const {
    const int sides = diceui::SIDES[_typeIdx];
    if (_mod == 0)
      snprintf(out, n, "%dd%d", _count, sides);
    else
      snprintf(out, n, "%dd%d%+d", _count, sides, _mod);
  }

  void roll() {
    const int sides = diceui::SIDES[_typeIdx];
    _rolled = _count;
    _total = _mod;
    for (int i = 0; i < _count; i++) {
      _values[i] = (int)(esp_random() % (uint32_t)sides) + 1;
      _total += _values[i];
    }
    // Newest first, oldest falls off the end.
    for (int i = kHist - 1; i > 0; i--) memcpy(_hist[i], _hist[i - 1], sizeof(_hist[0]));
    char f[16];
    formula(f, sizeof(f));
    snprintf(_hist[0], sizeof(_hist[0]), "%s = %d", f, _total);
    for (int i = kHist - 1; i > 0; i--) _histTotal[i] = _histTotal[i - 1];
    _histTotal[0] = _total;
    if (_histN < kHist) _histN++;

    bool crit = false;
    for (int i = 0; i < _count; i++)
      if (_values[i] == sides) crit = true;
    host().beep(crit ? 3 : 1);
    host().refresh(true);
  }

  static void drawDie(ToolsCanvas& c, int x, int y, int size, int value, int sides) {
    // Rounded like a real die rather than a bare square. The corner radius is a
    // sixth of the face, which is about what a moulded plastic die has and is
    // enough to read as "die" at a glance instead of "box with dots in it".
    const int r = size / 6;
    const bool crit = (value == sides);
    if (crit) {
      tdraw::fillRound(c, x, y, size, size, r, true);
    } else {
      tdraw::roundRect(c, x, y, size, size, r, 3, true);
      const int in = size / 16;
      if (value == 1)
        tdraw::roundRect(c, x + in, y + in, size - 2 * in, size - 2 * in, r - in, 1, true);
    }
    if (sides == 6) {
      tdraw::dicePips(c, x, y, size, value, crit);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%d", value);
      const int h = size / 2;
      tdraw::seg7Centered(c, x + size / 2, y + (size - h) / 2, h, buf, !crit);
    }
  }

  static constexpr int kHist = 6;
  int _typeIdx = 1, _count = 1, _mod = 0;
  int _values[6] = {};
  int _rolled = 0, _total = 0;
  char _hist[kHist][20] = {};
  int _histTotal[kHist] = {};
  int _histN = 0;
};
