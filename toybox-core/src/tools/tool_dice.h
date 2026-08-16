// Tabletop dice roller: D4/D6/D8/D10/D12/D20, 1-6 dice, -10..+10 modifier.
// Natural maximum rolls are highlighted (filled die), natural 1s get a double
// border — the two results a tabletop player actually looks for.
#pragma once
#include <esp_random.h>

#include "dice_shapes.h"
#include "tools_draw.h"

namespace diceui {
// Portrait: controls at the top where the thumb is, results filling the page.
// The six die types sit in one row as wireframe solids rather than two rows of
// text -- a shape is recognised before a label is read, and one row costs less
// height than the two it replaces.
inline constexpr int TYPE_W = 76, TYPE_H = 88, TYPE_GAP = 4;
inline constexpr int TYPE_X0 = (480 - (6 * TYPE_W + 5 * TYPE_GAP)) / 2;
inline constexpr int TYPE_Y0 = 50;
inline constexpr int STEP = 40;
inline constexpr TRect ROLL{60, 262, 360, 76};
inline constexpr TRect CNT_DN{240, 166, STEP, STEP};
inline constexpr TRect CNT_UP{380, 166, STEP, STEP};
inline constexpr TRect MOD_DN{240, 212, STEP, STEP};
inline constexpr TRect MOD_UP{380, 212, STEP, STEP};

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

    // --- dice faces --------------------------------------------------------
    // The faces get whatever room is left between the formula and the total,
    // so one die is drawn large and six are drawn small rather than one die
    // sitting in the corner of a grid built for six. The band never moves, so
    // the total below it never moves either.
    constexpr int BAND_Y = 372, BAND_H = 248, GAP = 10, MAX_CELL = 200;
    const int n = _rolled > 0 ? _rolled : _count;
    const int cols = n < 3 ? n : 3;
    const int rows = (n + 2) / 3;
    int cell = (440 - (cols - 1) * GAP) / cols;
    const int fits = (BAND_H - (rows - 1) * GAP) / rows;
    if (fits < cell) cell = fits;
    if (cell > MAX_CELL) cell = MAX_CELL;
    const int gw = cols * cell + (cols - 1) * GAP;
    const int gh = rows * cell + (rows - 1) * GAP;
    const int GX = (480 - gw) / 2;
    const int GY = BAND_Y + (BAND_H - gh) / 2;

    if (_rolled == 0) {
      c.drawRect(GX, GY, gw, gh, 1, true);
      c.textInBox(GX, GY, gw, gh, "tap ROLL", TS_LARGE, true);
    }
    for (int i = 0; i < _rolled; i++) {
      const int x = GX + (i % cols) * (cell + GAP);
      const int y = GY + (i / cols) * (cell + GAP);
      drawDie(c, x, y, cell, _values[i], sides);
    }

    // --- total -------------------------------------------------------------
    if (_rolled > 0) {
      c.text(24, 630, "TOTAL", TS_MED, true);
      char buf[12];
      snprintf(buf, sizeof(buf), "%d", _total);
      tdraw::seg7Text(c, 24, 658, 92, buf, true);
    }

    // --- roll history ------------------------------------------------------
    if (_histN > 0) {
      c.text(250, 630, "RECENT", TS_MED, true);
      for (int i = 0; i < _histN && i < 5; i++)
        c.text(250, 660 + i * 24, _hist[i], TS_MED, true);
    }

    // --- panel: die type ---------------------------------------------------
    // Drawn by hand rather than through c.button(): the selected key inverts,
    // and the wireframe has to invert with it or it would vanish into the fill.
    char lbl[8];
    for (int i = 0; i < 6; i++) {
      const TRect r = typeRect(i);
      const bool on = (i == _typeIdx);
      if (on)
        tdraw::fillRound(c, r.x, r.y, r.w, r.h, 8, true);
      else
        tdraw::roundRect(c, r.x, r.y, r.w, r.h, 8, 2, true);
      dshape::draw(c, i, r.x + r.w / 2, r.y + 30, 22, 2, !on);
      snprintf(lbl, sizeof(lbl), "D%d", SIDES[i]);
      c.textCentered(r.x + r.w / 2, r.y + 60, lbl, TS_MED, !on);
    }

    // --- panel: count & modifier ------------------------------------------
    char buf[16];
    c.text(24, 176, "DICE", TS_MED, true);
    c.stepper(CNT_DN.x, CNT_DN.y, STEP, "-", _count > 1);
    snprintf(buf, sizeof(buf), "%d", _count);
    c.textInBox(CNT_DN.x + STEP, CNT_DN.y, CNT_UP.x - CNT_DN.x - STEP, STEP, buf, TS_LARGE,
                true, true);
    c.stepper(CNT_UP.x, CNT_UP.y, STEP, "+", _count < 6);

    c.text(24, 222, "MOD", TS_MED, true);
    c.stepper(MOD_DN.x, MOD_DN.y, STEP, "-", _mod > -10);
    snprintf(buf, sizeof(buf), "%+d", _mod);
    c.textInBox(MOD_DN.x + STEP, MOD_DN.y, MOD_UP.x - MOD_DN.x - STEP, STEP, buf, TS_LARGE,
                true, true);
    c.stepper(MOD_UP.x, MOD_UP.y, STEP, "+", _mod < 10);

    c.button(ROLL.x, ROLL.y, ROLL.w, ROLL.h, "ROLL", true, TS_HUGE);
    formula(buf, sizeof(buf));
    c.textCentered(c.width() / 2, 348, buf, TS_LARGE, true, true);
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
    if (ROLL.hit(x, y)) return roll();
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
  int _histN = 0;
};
