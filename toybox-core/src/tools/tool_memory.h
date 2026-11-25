// Memory pairs (C4). Turn two cards; a pair stays up, two odd ones go back on
// your next tap. Grids 3x4, 4x4 and 5x5 (the middle square of 5x5 is blank).
//
// The deal and the turning rules live in memory_rules.h (host-tested); the
// faces are game_symbols.h's twelve.
#pragma once
#include <esp_random.h>

#include "game_symbols.h"
#include "kit_game.h"
#include "memory_rules.h"
#include "record.h"

namespace mpui {
inline constexpr int STAT_Y = 54;
inline TRect statRect(int i) { return TRect{20 + i * 150, STAT_Y, 140, gk::STAT_H}; }
inline constexpr int GRID_Y = 140;
inline int cellSize(int size) { return size == 0 ? 120 : size == 1 ? 110 : 88; }
inline int gridX(int size) { return (SCREEN_W - mp::COLS[size] * cellSize(size)) / 2; }
inline TRect cellRect(int size, int k) {
  const int s = cellSize(size);
  return TRect{gridX(size) + (k % mp::COLS[size]) * s, GRID_Y + (k / mp::COLS[size]) * s, s, s};
}
inline int cellAt(int size, int x, int y) {
  for (int k = 0; k < mp::cellsFor(size); k++)
    if (cellRect(size, k).hit(x, y)) return k;
  return -1;
}
}  // namespace mpui

class MemoryTool : public KitGame {
 public:
  const char* title() const override { return "MEMORY"; }

#ifdef TOYBOX_HOST
  mp::Game& hostGame() { return _g; }
  bool hostResult() const { return _result; }
#endif

 protected:
  const help::Text& rulesText() const override { return help::MEMORY; }
  const char* helpKey() const override { return "mp"; }

  void load() override {
    _result = false;
    if (prefs().getBytesLength("mp_state") == sizeof(_g) &&
        prefs().getBytes("mp_state", &_g, sizeof(_g)) == sizeof(_g) && _g.size < mp::SIZES &&
        !mp::won(_g))
      return;
    newGame();
  }
  const char* corner() override { return mp::sizeName(_g.size); }

  int extraRows(gk::Row* rows) override {
    rows[0] = {"New game", nullptr, false};
    rows[1] = {"Grid", mp::sizeName(nextSize()), false};
    return 2;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "New game")) {
      newGame();
      _result = false;
      return true;
    }
    if (!strcmp(what, "Grid")) {
      prefs().putInt("mp_size", (nextSize() + 1) % mp::SIZES);
      if (_g.flips == 0) newGame();  // nothing turned yet: the new grid at once
      return false;
    }
    return false;
  }
  bool hasRecord() override {
    for (const char* k : record::MEMORY)
      if (geti(k)) return true;
    return false;
  }
  void clearRecord() override { record::clear(prefs(), record::MEMORY); }
  void backToPlay() override { host().refresh(true); }

  void renderPlay(ToolsCanvas& c) override {
    using namespace mpui;
    char k[8], v[12];
    key(k, sizeof(k), "mp_b", _g.size);
    const int best = geti(k);
    if (_result) {
      char line[40];
      snprintf(line, sizeof(line), "%d pairs in %d flips", mp::pairsFor(_g.size), _g.flips);
      char f[8], b[8], p[8];
      snprintf(f, sizeof(f), "%d", _g.flips);
      snprintf(b, sizeof(b), "%d", best);
      snprintf(p, sizeof(p), "%d", mp::pairsFor(_g.size) * 2);
      const gk::Stat st[3] = {{f, "FLIPS"}, {b, "BEST"}, {p, "PERFECT"}};
      gk::result(c, "ALL PAIRS", _newBest ? "New best" : "Done", line, st, 3, "Close",
                 "New game");
      gk::foot(c, mp::sizeName(_g.size));
      return;
    }
    gk::statInt(c, statRect(0), "FLIPS", _g.flips);
    snprintf(v, sizeof(v), "%d/%d", _g.found, mp::pairsFor(_g.size));
    gk::stat(c, statRect(1), "PAIRS", v);
    if (best)
      gk::statInt(c, statRect(2), "BEST", best);
    else
      gk::stat(c, statRect(2), "BEST", "-");

    const int s = cellSize(_g.size), sym = s / 2;
    for (int i = 0; i < mp::cellsFor(_g.size); i++) {
      if (mp::isBlank(_g, i)) continue;
      const TRect r = cellRect(_g.size, i);
      const int x = r.x + 4, y = r.y + 4, w = s - 8;
      const int cx = r.x + s / 2, cy = r.y + s / 2;
      if (_g.st[i] == mp::DOWN) {
        // Face down is a plain frame on white (owner, 25 Nov): a hatched back
        // let the symbol that was just there show through a partial refresh.
        tdraw::roundRect(c, x, y, w, w, 8, 2, true);
      } else {
        // Turned now: a heavy frame. Kept: a hairline -- settled, out of the way.
        tdraw::roundRect(c, x, y, w, w, 8, _g.st[i] == mp::UP ? 4 : 1, true);
        gsym::draw(c, _g.sym[i], cx, cy, sym);
      }
    }
    const bool odd = _g.a >= 0 && _g.b >= 0;
    gk::foot(c, mp::won(_g) ? "tap the board for a new game"
                : odd       ? "no pair - tap to turn them back"
                            : "turn two cards");
  }

  void tapPlay(int x, int y) override {
    if (_result) {
      if (gk::RES_RIGHT.hit(x, y)) {
        newGame();
        _result = false;
        host().beep(1);
        host().refresh(true);
      } else if (gk::RES_LEFT.hit(x, y)) {
        _result = false;  // look at the finished board
        host().beep(1);
        host().refresh(true);
      }
      return;
    }
    const int k = mpui::cellAt(_g.size, x, y);
    if (k < 0) return;
    if (mp::won(_g)) {
      newGame();
      host().beep(1);
      host().refresh(true);
      return;
    }
    const mp::Event e = mp::tap(_g, k);
    if (e == mp::NOTHING) return;
    if (mp::won(_g)) {
      finish();
      return;
    }
    save();
    host().beep(e == mp::PAIR ? 1 : e == mp::MISS ? 2 : 0);
    host().refresh(false);
  }

 private:
  mp::Game _g{};
  bool _result = false;
  bool _newBest = false;

  static uint32_t rnd() { return esp_random(); }
  void save() { prefs().putBytes("mp_state", &_g, sizeof(_g)); }
  int nextSize() {
    const int s = geti("mp_size", 1);
    return s >= 0 && s < mp::SIZES ? s : 1;
  }
  void newGame() {
    mp::reset(_g, nextSize(), rnd);
    save();
  }
  void finish() {
    char k[8];
    key(k, sizeof(k), "mp_w", _g.size);
    bump(k);
    key(k, sizeof(k), "mp_b", _g.size);
    const int best = geti(k);
    _newBest = !best || _g.flips < best;
    if (_newBest) prefs().putInt(k, _g.flips);
    prefs().remove("mp_state");
    _result = true;
    host().beep(3);
    host().refresh(true);
  }
};
