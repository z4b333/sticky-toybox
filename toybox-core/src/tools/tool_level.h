// Level (C5, UTILITY). A spirit level from the accelerometer.
//
// Lying flat: a round vial and two angles. Standing on an edge: a long vial
// and one angle, with the screen turned so its words read the right way up on
// whichever edge it stands. "Set zero" makes the surface it is on the level
// one (for a case, a sticker, a table you trust more than the chip); the
// zero is kept. How often the screen redraws is a setting (owner, 25 Nov):
// 0.5 s, 1 s or 2 s.
//
// The face is plain on purpose: under partial refresh a hatched or grey area
// keeps a ghost of whatever moved across it (Memory, C4).
//
// The arithmetic is level_calc.h (host-tested).
#pragma once
#include "kit_game.h"
#include "level_calc.h"

namespace lvui {
inline constexpr int BTN_H = 72;
// The two buttons along the foot, wherever the foot is.
inline TRect zeroRect(int w, int h) { return TRect{20, h - 46 - 16 - BTN_H, (w - 52) / 2, BTN_H}; }
inline TRect clearRect(int w, int h) {
  return TRect{32 + (w - 52) / 2, h - 46 - 16 - BTN_H, (w - 52) / 2, BTN_H};
}
inline constexpr int FLAT_SPAN_DECI = 100;  // the round vial's edge is 10 degrees
inline constexpr int EDGE_SPAN_DECI = 50;   // the long vial's ends are 5 degrees
}  // namespace lvui

class LevelTool : public KitGame {
 public:
  ~LevelTool() override {
    if (_host) _host->setCanvasRotation(0);  // apps are portrait
  }
  const char* title() const override { return "LEVEL"; }

  void enter(ToolsHost& h) override {
    _host = &h;
    _beat = 0;
    _have = sample();
    _rot = _have && _r.mode == lvl::EDGE ? _r.rot : 0;
    h.setCanvasRotation(_rot);
    KitGame::enter(h);
  }

  bool wantsTick() const override { return true; }
  bool napsOk() const override { return true; }
  void tick() override {
    if (_view != View::Play) return;
    // The chip is read on every tick (~50 Hz) and smoothed; the glass is
    // only redrawn at the owner's rate.
    _have = sample();
    if (++_beat * 20 < lvl::RATE_MS[_rate]) return;
    _beat = 0;
    const lvl::Reading before = _shown;
    const bool had = _shownHave;
    _shown = _r;
    _shownHave = _have;
    if (!_have) {
      if (had) host().refresh(false);
      return;
    }
    // Standing on another edge: the words turn with it, and a quarter turn
    // changes every pixel, so that one is a full refresh.
    // Laid flat it is looked down on from any side: portrait, as the rest.
    const int want = _r.mode == lvl::EDGE ? _r.rot : 0;
    if (want != _rot) {
      _rot = want;
      host().setCanvasRotation(_rot);
      host().refresh(true);
      return;
    }
    if (had && same(before, _r)) return;  // nothing moved: leave the glass alone
    host().refresh(false);
  }

  bool onButton(SideBtn b) override {
    if (_view != View::Play || b != SideBtn::Ok) return false;
    setZero();
    return true;
  }

#ifdef TOYBOX_HOST
  const lvl::Reading& hostReading() const { return _r; }
  int hostRot() const { return _rot; }
  void hostSample() {  // a second of the hand held still, then the redraw
    for (int i = 0; i < 50; i++) sample();
    _beat = 1000;
    tick();
  }
#endif

 protected:
  const help::Text& rulesText() const override { return help::LEVEL; }
  const char* helpKey() const override { return "lv"; }
  bool wide() const override { return _rot == 1 || _rot == 3; }
  bool isGame() const override { return false; }

  void load() override {
    _rate = geti("lv_rate", 0);
    if (_rate < 0 || _rate >= lvl::RATES) _rate = 0;
    _ox = geti("lv_ox");
    _oy = geti("lv_oy");
    _oe = geti("lv_oe");
  }

  int extraRows(gk::Row* rows) override {
    rows[0] = {"Redraw every", lvl::rateName(_rate), false};
    rows[1] = {"Clear zero", hasZero() ? nullptr : "none set", false};
    return 2;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "Redraw every")) {
      _rate = (_rate + 1) % lvl::RATES;
      prefs().putInt("lv_rate", _rate);
      return false;
    }
    if (!strcmp(what, "Clear zero")) {
      clearZero();
      return true;
    }
    return false;
  }

  void renderPlay(ToolsCanvas& c) override {
    using namespace lvui;
    const int w = c.width(), h = c.height();
    if (!_have) {
      c.textCentered(w / 2, h / 2 - 40, "No tilt sensor answered", TS_LARGE, true, true);
      c.textCentered(w / 2, h / 2 + 4, "this Sticky cannot be a level", TS_MED, true);
      gk::foot(c, "");
      return;
    }
    const bool level = lvl::isLevel(_r, _ox, _oy, _oe);
    if (_r.mode == lvl::FLAT)
      renderFlat(c, w, h, level);
    else
      renderEdge(c, w, h, level);
    const TRect z = zeroRect(w, h), cl = clearRect(w, h);
    c.button(z.x, z.y, z.w, z.h, "Set zero", false, TS_LARGE);
    if (hasZero()) c.button(cl.x, cl.y, cl.w, cl.h, "Clear zero", false, TS_LARGE);
    gk::foot(c, "");
  }

  void tapPlay(int x, int y) override {
    const int w = canvas().width(), h = canvas().height();
    if (lvui::zeroRect(w, h).hit(x, y)) {
      setZero();
      return;
    }
    if (hasZero() && lvui::clearRect(w, h).hit(x, y)) {
      clearZero();
      host().beep(1);
      host().refresh(false);
    }
  }

  void backToPlay() override {
    host().setCanvasRotation(_rot);
    host().refresh(true);
  }

 private:
  lvl::Reading _r{}, _shown{};
  bool _have = false, _shownHave = false;
  int _rot = 0, _rate = 0, _beat = 0;
  int _ox = 0, _oy = 0, _oe = 0;
  // A little smoothing: the chip jitters by a few counts at rest.
  int _fx = 0, _fy = 0, _fz = 0;
  bool _primed = false;
  int _primedMode = -1;

  bool hasZero() const { return _ox || _oy || _oe; }

  bool sample() {
    int x, y, z;
    if (!host().accel3(x, y, z)) return false;
    if (!_primed) {
      _fx = x, _fy = y, _fz = z;
      _primed = true;
    } else {
      // ~0.2 s to settle at 50 Hz: steady without lagging the hand.
      _fx += (x - _fx) / 4, _fy += (y - _fy) / 4, _fz += (z - _fz) / 4;
    }
    _r = lvl::read(_fx, _fy, _fz, _primedMode);
    _primedMode = _r.mode;
    return true;
  }
  static bool same(const lvl::Reading& a, const lvl::Reading& b) {
    return a.mode == b.mode && a.x == b.x && a.y == b.y && a.edge == b.edge && a.rot == b.rot;
  }

  void setZero() {
    if (!_have) {
      host().beep(2);
      return;
    }
    if (_r.mode == lvl::FLAT) {
      _ox = _r.x;
      _oy = _r.y;
      prefs().putInt("lv_ox", _ox);
      prefs().putInt("lv_oy", _oy);
    } else {
      _oe = _r.edge;
      prefs().putInt("lv_oe", _oe);
    }
    host().beep(1);
    host().refresh(false);
  }
  void clearZero() {
    _ox = _oy = _oe = 0;
    prefs().remove("lv_ox");
    prefs().remove("lv_oy");
    prefs().remove("lv_oe");
  }

  static void deg(char* out, int n, const char* pre, int deci) {
    const int a = deci < 0 ? -deci : deci;
    snprintf(out, n, "%s%s%d.%d\xc2\xb0", pre, deci < 0 ? "-" : "", a / 10, a % 10);
  }

  void badge(ToolsCanvas& c, int cx, int y, bool level) {
    if (level)
      gk::slab(c, cx, y, "LEVEL", TS_LARGE);
    else
      c.textCentered(cx, y + 8, "not level", TS_LARGE, true);
  }

  void renderFlat(ToolsCanvas& c, int w, int h, bool level) {
    using namespace lvui;
    // The vial: a circle, cross hairs, and the ring that counts as level.
    const int R = w / 2 - 30 < 170 ? w / 2 - 30 : 170;
    const int cx = w / 2, cy = TOPBAR_H + 30 + R;
    c.drawCircle(cx, cy, R, 3, true);
    c.drawLine(cx - R, cy, cx + R, cy, 1, true);
    c.drawLine(cx, cy - R, cx, cy + R, 1, true);
    const int ring = R * lvl::LEVEL_DECI * 3 / FLAT_SPAN_DECI;
    c.drawCircle(cx, cy, ring + 22, 2, true);
    // The bubble floats to the high side.
    int dx = _r.x - _ox, dy = _r.y - _oy;
    int bx = -dx * R / FLAT_SPAN_DECI, by = dy * R / FLAT_SPAN_DECI;
    const int lim = R - 22;
    const float d = sqrtf((float)(bx * bx + by * by));
    if (d > lim) {
      bx = (int)(bx * lim / d);
      by = (int)(by * lim / d);
    }
    c.fillCircle(cx + bx, cy + by, 20, true);
    c.fillCircle(cx + bx - 6, cy + by - 6, 5, false);  // the glint
    // The two angles and the verdict.
    const int sy = cy + R + 24;
    char a[16], b[16];
    deg(a, sizeof(a), "", dx);
    deg(b, sizeof(b), "", dy);
    gk::stat(c, TRect{20, sy, (w - 50) / 2, gk::STAT_H}, "ACROSS", a);
    gk::stat(c, TRect{30 + (w - 50) / 2, sy, (w - 50) / 2, gk::STAT_H}, "ALONG", b);
    badge(c, w / 2, sy + gk::STAT_H + 16, level);
  }

  void renderEdge(ToolsCanvas& c, int w, int h, bool level) {
    using namespace lvui;
    // The long vial across the screen, its middle marked, the bubble in it.
    const int vx = 40, vw = w - 80, vh = 80;
    const int vy = TOPBAR_H + (h - TOPBAR_H - 46 - BTN_H - 16) / 2 - 110;
    tdraw::roundRect(c, vx, vy, vw, vh, vh / 2, 3, true);
    const int mid = vx + vw / 2;
    const int tick = vw * lvl::LEVEL_DECI * 3 / (2 * EDGE_SPAN_DECI) + 30;
    c.fillRect(mid - tick, vy + 6, 2, vh - 12, true);
    c.fillRect(mid + tick - 2, vy + 6, 2, vh - 12, true);
    const int de = _r.edge - _oe;
    int bx = -de * (vw / 2) / EDGE_SPAN_DECI;
    const int lim = vw / 2 - 40;
    if (bx > lim) bx = lim;
    if (bx < -lim) bx = -lim;
    tdraw::fillRound(c, mid + bx - 30, vy + 14, 60, vh - 28, (vh - 28) / 2, true);
    char a[16];
    deg(a, sizeof(a), "", de);
    c.textCentered(w / 2, vy + vh + 24, a, TS_HUGE, true, true);
    badge(c, w / 2, vy + vh + 86, level);
  }
};
