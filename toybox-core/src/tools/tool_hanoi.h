// Tower of Hanoi (C4), played on its side. Tap a peg to lift its top disc,
// tap another to put it down; a bigger disc never goes on a smaller one. Move
// the stack from the left peg to the right in as few moves as you can.
//
// Landscape (800 x 480): the side that is up comes from the accelerometer
// when the game opens, or the pinned note's side without one, and stays for
// the visit -- a game is not a note to follow the hand. The panel goes back
// to portrait when the game is deleted on the way out to the hub.
//
// The moves and Undo live in hanoi_rules.h (host-tested).
#pragma once
#include "hanoi_rules.h"
#include "kit_game.h"
#include "lockscreen.h"
#include "record.h"

namespace hnui {
inline constexpr int W = 800, H = 480;
inline constexpr int STAT_Y = 52;
inline TRect statRect(int i) { return TRect{20 + i * 150, STAT_Y, 140, gk::STAT_H}; }
inline constexpr TRect UNDO{500, STAT_Y, 136, gk::STAT_H};
inline constexpr TRect RESTART{646, STAT_Y, 134, gk::STAT_H};
inline constexpr int PEG_W = 260;                // each peg's third of the screen
inline constexpr int LIFT_Y = 128;               // where a lifted disc hovers
inline constexpr int BASE_Y = 396;               // the floor
inline constexpr int DISC_H = 30;
inline constexpr int POLE_TOP = BASE_Y - hn::MAX_DISCS * DISC_H - 20;  // 166
inline int pegX(int p) { return 10 + p * PEG_W + PEG_W / 2; }  // centre line
inline int pegAt(int x, int y) {
  if (y < LIFT_Y - 8 || y >= H - 46) return -1;
  const int p = (x - 10) / PEG_W;
  return x >= 10 && p >= 0 && p < hn::PEGS ? p : -1;
}
inline int discW(int size, int discs) {
  return 60 + (size - 1) * (PEG_W - 24 - 60) / (discs > 1 ? discs - 1 : 1);
}
}  // namespace hnui

class HanoiTool : public KitGame {
 public:
  ~HanoiTool() override {
    if (_host) _host->setCanvasRotation(0);  // the rest of the firmware is portrait
  }
  const char* title() const override { return "TOWER OF HANOI"; }

  void enter(ToolsHost& h) override {
    _host = &h;
    // Turn first: the rules card and the first paint are already sideways.
    const int held = h.deviceOrientation();
    const int pin = lock::config().pinRotation & 3;
    _rot = held == 1 || held == 3 ? held : pin == 1 || pin == 3 ? pin : 1;
    h.setCanvasRotation(_rot);
    KitGame::enter(h);
  }

  // Up/Down move the pointer between pegs, OK lifts or places. Which way is
  // "up" follows the side the panel is turned to, so the pointer moves the
  // way the button sits.
  bool onButton(SideBtn b) override {
    if (_view != View::Play) return false;
    if (hn::solved(_g)) {
      if (b != SideBtn::Ok) return false;
      restart();
      return true;
    }
    if (b == SideBtn::Ok) {
      if (_aim < 0) _aim = _g.held >= 0 ? _g.held : 0;
      tapPeg(_aim);
      return true;
    }
    const bool forward = (b == SideBtn::Up) == (_rot == 3);
    _aim = _aim < 0 ? (_g.held >= 0 ? _g.held : 0) : (_aim + (forward ? 1 : hn::PEGS - 1)) % hn::PEGS;
    host().beep(0);
    host().refresh(false);
    return true;
  }

#ifdef TOYBOX_HOST
  hn::Game& hostGame() { return _g; }
  int hostRot() const { return _rot; }
#endif

 protected:
  const help::Text& rulesText() const override { return help::HANOI; }
  const char* helpKey() const override { return "hn"; }
  bool wide() const override { return true; }

  void load() override {
    _aim = -1;
    if (prefs().getBytesLength("hn_state") == sizeof(_g) &&
        prefs().getBytes("hn_state", &_g, sizeof(_g)) == sizeof(_g) &&
        _g.discs >= hn::MIN_DISCS && _g.discs <= hn::MAX_DISCS &&
        _g.n[0] + _g.n[1] + _g.n[2] == _g.discs && !hn::solved(_g)) {
      _g.held = -1;
      return;
    }
    hn::reset(_g, discs());
    save();
  }
  const char* corner() override {
    snprintf(_corner, sizeof(_corner), "%d discs", _g.discs);
    return _corner;
  }

  int extraRows(gk::Row* rows) override {
    snprintf(_discLabel, sizeof(_discLabel), "%d", discs());
    rows[0] = {"Start again", nullptr, false};
    rows[1] = {"Discs", _discLabel, false};
    return 2;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "Start again")) {
      hn::reset(_g, discs());
      save();
      return true;
    }
    if (!strcmp(what, "Discs")) {
      int d = discs() + 1;
      if (d > hn::MAX_DISCS) d = hn::MIN_DISCS;
      prefs().putInt("hn_n", d);
      if (_g.moves == 0) {  // nothing moved yet: the new stack at once
        hn::reset(_g, d);
        save();
      }
      return false;
    }
    return false;
  }
  bool hasRecord() override {
    for (const char* k : record::HANOI)
      if (geti(k)) return true;
    return false;
  }
  void clearRecord() override { record::clear(prefs(), record::HANOI); }

  void renderPlay(ToolsCanvas& c) override {
    using namespace hnui;
    char k[8];
    key(k, sizeof(k), "hn_b", _g.discs);
    const int best = geti(k);
    gk::statInt(c, statRect(0), "MOVES", _g.moves);
    gk::statInt(c, statRect(1), "FEWEST", hn::minMoves(_g.discs));
    if (best)
      gk::statInt(c, statRect(2), "YOUR BEST", best);
    else
      gk::stat(c, statRect(2), "YOUR BEST", "-");
    const bool done = hn::solved(_g);
    if (!done) {
      const bool can = _g.nh > 0;
      c.fillRect(UNDO.x, UNDO.y, UNDO.w, UNDO.h, false);
      c.drawRect(UNDO.x, UNDO.y, UNDO.w, UNDO.h, can ? 2 : 1, true);
      c.textInBox(UNDO.x, UNDO.y, UNDO.w, UNDO.h, "Undo", TS_LARGE, true, can);
      c.button(RESTART.x, RESTART.y, RESTART.w, RESTART.h, "Restart", false, TS_LARGE);
    }

    // The floor, the poles, the discs.
    c.fillRect(10, BASE_Y, W - 20, 4, true);
    for (int p = 0; p < hn::PEGS; p++) {
      const int cx = pegX(p);
      c.fillRect(cx - 3, POLE_TOP, 6, BASE_Y - POLE_TOP, true);
      for (int i = 0; i < _g.n[p]; i++) {
        if (p == _g.held && i == _g.n[p] - 1) continue;  // it is in the air
        disc(c, cx, BASE_Y - (i + 1) * DISC_H, _g.peg[p][i]);
      }
      if (p == 2) c.textTracked(cx + PEG_W / 2 - 60, BASE_Y + 10, "GOAL", TS_SMALL, true, false, 2);
    }
    if (_g.held >= 0) disc(c, pegX(_g.held), LIFT_Y, hn::top(_g, _g.held), true);
    if (_aim >= 0 && !done) {
      const int cx = pegX(_aim), y = BASE_Y + 10;
      decor::triangle(c, cx - 14, y + 16, cx + 14, y + 16, cx, y, true);
    }
    if (done) {
      char line[48];
      const int fewest = hn::minMoves(_g.discs);
      if (_g.moves == fewest)
        snprintf(line, sizeof(line), "Solved in %d - the fewest possible", _g.moves);
      else
        snprintf(line, sizeof(line), "Solved in %d moves", _g.moves);
      c.textCentered(W / 2, LIFT_Y, line, TS_LARGE, true, true);
      gk::foot(c, "tap or OK to play again");
      return;
    }
    gk::foot(c, _g.held >= 0 ? "tap a peg to put it down" : "tap a peg to lift its top disc");
  }

  void tapPlay(int x, int y) override {
    using namespace hnui;
    if (hn::solved(_g)) {
      if (y > STAT_Y + gk::STAT_H) restart();
      return;
    }
    if (UNDO.hit(x, y)) {
      if (!hn::undo(_g)) {
        host().beep(2);
        return;
      }
      save();
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (RESTART.hit(x, y)) {
      restart();
      return;
    }
    const int p = pegAt(x, y);
    if (p < 0) return;
    _aim = -1;
    tapPeg(p);
  }

 private:
  hn::Game _g{};
  int _rot = 1;
  int8_t _aim = -1;
  char _corner[16] = {};
  char _discLabel[8] = {};

  int discs() {
    const int d = geti("hn_n", hn::DEFAULT_DISCS);
    return d >= hn::MIN_DISCS && d <= hn::MAX_DISCS ? d : hn::DEFAULT_DISCS;
  }
  void save() { prefs().putBytes("hn_state", &_g, sizeof(_g)); }

  // A disc: a rounded bar, solid. The lifted one is drawn hollow with a heavy
  // edge, so it reads as "in your hand" rather than as a disc on a peg.
  void disc(ToolsCanvas& c, int cx, int y, int size, bool lifted = false) {
    const int w = hnui::discW(size, _g.discs);
    if (lifted) {
      tdraw::fillRound(c, cx - w / 2, y, w, hnui::DISC_H - 4, 8, false);
      tdraw::roundRect(c, cx - w / 2, y, w, hnui::DISC_H - 4, 8, 4, true);
    } else {
      tdraw::fillRound(c, cx - w / 2, y + 2, w, hnui::DISC_H - 4, 8, true);
    }
  }

  void restart() {
    hn::reset(_g, discs());
    _aim = -1;
    save();
    host().beep(1);
    host().refresh(true);
  }

  void tapPeg(int p) {
    const int before = _g.moves;
    if (!hn::tap(_g, p)) {
      host().beep(2);  // a bigger disc on a smaller, or nothing to lift
      return;
    }
    if (hn::solved(_g)) {
      char k[8];
      key(k, sizeof(k), "hn_b", _g.discs);
      const int best = geti(k);
      if (!best || _g.moves < best) prefs().putInt(k, _g.moves);
      bump("hn_won");
      prefs().remove("hn_state");
      host().beep(3);
      host().refresh(true);
      return;
    }
    if (_g.moves != before) save();
    host().beep(0);
    host().refresh(false);
  }
};
