// Codebreaker (C4). The Sticky hides a row of symbols; you have ten tries to
// find it. After each try: a solid peg for each symbol in the right place, a
// ring for each that is in the code but elsewhere.
//
// Scoring and the hidden code live in code_rules.h (host-tested); the six
// symbols are game_symbols.h's first six.
#pragma once
#include <esp_random.h>

#include "code_rules.h"
#include "game_symbols.h"
#include "kit_game.h"
#include "record.h"

namespace cbui {
inline constexpr int ROW_Y = 52, ROW_H = 44;  // ten rows: 52..492
inline constexpr int SLOT = 44, SLOT_X = 56;
inline constexpr int SYM = 28;                // a symbol in a slot
inline constexpr int PEG_X = 316, PEG_STEP = 26, PEG_R = 9;
inline constexpr int SECRET_Y = ROW_Y + cb::TRIES * ROW_H + 10;  // 502
inline constexpr int PAL_Y = 568, PAL_W = 70, PAL_H = 72;
inline TRect palRect(int k) { return TRect{20 + k * (PAL_W + 4), PAL_Y, PAL_W, PAL_H}; }
inline constexpr TRect DEL{20, 654, 214, 72};
inline constexpr TRect CHECK{246, 654, 214, 72};
inline constexpr TRect NEW_GAME{20, 654, 440, 72};
inline TRect slotRect(int row, int i) { return TRect{SLOT_X + i * SLOT, ROW_Y + row * ROW_H, SLOT, ROW_H}; }
}  // namespace cbui

class CodeTool : public KitGame {
 public:
  const char* title() const override { return "CODEBREAKER"; }

  // Up/Down step the cursor along the row, OK checks a full row.
  bool onButton(SideBtn b) override {
    if (_view != View::Play || _g.done) return false;
    if (b == SideBtn::Ok) {
      check();
      return true;
    }
    _cur = (uint8_t)((_cur + (b == SideBtn::Up ? 1 : _g.slots - 1)) % _g.slots);
    host().beep(0);
    host().refresh(false);
    return true;
  }

#ifdef TOYBOX_HOST
  cb::Game& hostGame() { return _g; }
  void hostLevel(int l) { prefs().putInt("cb_lvl", l); }
#endif

 protected:
  const help::Text& rulesText() const override { return help::CODE; }
  const char* helpKey() const override { return "cb"; }

  void load() override {
    _cur = 0;
    if (prefs().getBytesLength("cb_state") == sizeof(_g) &&
        prefs().getBytes("cb_state", &_g, sizeof(_g)) == sizeof(_g) && _g.level < cb::LEVELS &&
        _g.slots == cb::slotsFor(_g.level) && _g.row <= cb::TRIES && _g.done <= 2) {
      firstEmpty();
      return;
    }
    newGame();
  }
  const char* corner() override { return cb::levelName(_g.level); }

  int extraRows(gk::Row* rows) override {
    rows[0] = {"New game", nullptr, false};
    rows[1] = {"Level", cb::levelName(nextLevel()), false};
    return 2;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "New game")) {
      abandon();
      newGame();
      return true;
    }
    if (!strcmp(what, "Level")) {
      prefs().putInt("cb_lvl", (nextLevel() + 1) % cb::LEVELS);
      // An untouched board takes the new level straight away.
      if (_g.row == 0 && !_g.done && rowEmpty()) newGame();
      return false;
    }
    return false;
  }
  bool hasRecord() override {
    for (const char* k : record::CODE)
      if (geti(k)) return true;
    return false;
  }
  void clearRecord() override { record::clear(prefs(), record::CODE); }

  void renderPlay(ToolsCanvas& c) override {
    using namespace cbui;
    for (int r = 0; r < cb::TRIES; r++) {
      const int y = ROW_Y + r * ROW_H;
      char n[4];
      snprintf(n, sizeof(n), "%d", r + 1);
      const bool live = r == _g.row && !_g.done;
      c.text(SLOT_X - 12 - c.textWidth(n, TS_SMALL, live), y + (ROW_H - c.textHeight(TS_SMALL)) / 2, n,
             TS_SMALL, true, live);
      for (int i = 0; i < _g.slots; i++) {
        const TRect s = slotRect(r, i);
        const int cx = s.x + SLOT / 2, cy = s.y + ROW_H / 2;
        const uint8_t sym = _g.guess[r][i];
        if (r < _g.row || live) {
          c.drawRect(s.x + 3, s.y + 3, SLOT - 6, ROW_H - 6, live && i == _cur ? 3 : 1, true);
          if (sym < cb::SYMS) gsym::draw(c, sym, cx, cy, SYM);
        } else {
          c.fillCircle(cx, cy, 2, true);  // a try still to come
        }
      }
      if (r < _g.row) pegs(c, r, y);
    }
    // The code: hatched until it is found or the tries run out.
    c.textTracked(PEG_X - 4, SECRET_Y + (ROW_H - c.textHeight(TS_SMALL)) / 2,
                  _g.done ? "THE CODE" : "HIDDEN CODE", TS_SMALL, true, false, 2);
    for (int i = 0; i < _g.slots; i++) {
      const TRect s{SLOT_X + i * SLOT, SECRET_Y, SLOT, ROW_H};
      if (_g.done) {
        c.drawRect(s.x + 3, s.y + 3, SLOT - 6, ROW_H - 6, 2, true);
        gsym::draw(c, _g.secret[i], s.x + SLOT / 2, s.y + ROW_H / 2, SYM);
      } else {
        gk::stripes(c, s.x + 3, s.y + 3, SLOT - 6, ROW_H - 6);
      }
    }
    chrome2::dottedRule(c, 20, SECRET_Y + ROW_H + 8, 440);

    if (_g.done) {
      char line[40];
      if (_g.done == 1)
        snprintf(line, sizeof(line), "Broken in %d %s", _g.row, _g.row == 1 ? "try" : "tries");
      else
        snprintf(line, sizeof(line), "Out of tries");
      c.textCentered(c.width() / 2, PAL_Y + 16, line, TS_LARGE, true, true);
      c.button(NEW_GAME.x, NEW_GAME.y, NEW_GAME.w, NEW_GAME.h, "New game", true, TS_LARGE);
      gk::foot(c, recordLine());
      return;
    }
    for (int k = 0; k < cb::SYMS; k++) {
      const TRect p = palRect(k);
      const bool ok = cb::allowed(_g, _cur, k);
      c.fillRect(p.x, p.y, p.w, p.h, false);
      c.drawRect(p.x, p.y, p.w, p.h, ok ? 2 : 1, true);
      if (ok)
        gsym::draw(c, k, p.x + p.w / 2, p.y + p.h / 2, 36);
      else
        chrome2::dottedRule(c, p.x + 12, p.y + p.h / 2, p.w - 24);  // already used (Easy)
    }
    c.button(DEL.x, DEL.y, DEL.w, DEL.h, "Delete", false, TS_LARGE);
    c.button(CHECK.x, CHECK.y, CHECK.w, CHECK.h, "Check", cb::rowFull(_g), TS_LARGE);
    gk::foot(c, recordLine());
  }

  void tapPlay(int x, int y) override {
    using namespace cbui;
    if (_g.done) {
      if (NEW_GAME.hit(x, y)) {
        newGame();
        host().beep(1);
        host().refresh(true);
      }
      return;
    }
    for (int i = 0; i < _g.slots; i++)
      if (slotRect(_g.row, i).hit(x, y)) {
        _cur = (uint8_t)i;
        host().beep(0);
        host().refresh(false);
        return;
      }
    for (int k = 0; k < cb::SYMS; k++)
      if (palRect(k).hit(x, y)) {
        if (!cb::allowed(_g, _cur, k)) {
          host().beep(2);
          return;
        }
        _g.guess[_g.row][_cur] = (uint8_t)k;
        // On to the next empty slot, so four taps fill a row.
        for (int s = 1; s <= _g.slots; s++) {
          const int j = (_cur + s) % _g.slots;
          if (_g.guess[_g.row][j] == cb::EMPTY) {
            _cur = (uint8_t)j;
            break;
          }
        }
        save();
        host().beep(0);
        host().refresh(false);
        return;
      }
    if (DEL.hit(x, y)) {
      if (_g.guess[_g.row][_cur] == cb::EMPTY && _cur > 0) _cur--;
      _g.guess[_g.row][_cur] = cb::EMPTY;
      save();
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (CHECK.hit(x, y)) check();
  }

 private:
  cb::Game _g{};
  uint8_t _cur = 0;
  char _foot[40] = {};

  static uint32_t rnd() { return esp_random(); }
  void save() { prefs().putBytes("cb_state", &_g, sizeof(_g)); }
  int nextLevel() {
    const int l = geti("cb_lvl", 0);
    return l >= 0 && l < cb::LEVELS ? l : 0;
  }
  bool rowEmpty() const {
    for (int i = 0; i < _g.slots; i++)
      if (_g.guess[_g.row][i] != cb::EMPTY) return false;
    return true;
  }
  void firstEmpty() {
    _cur = 0;
    if (_g.row >= cb::TRIES) return;
    for (int i = 0; i < _g.slots; i++)
      if (_g.guess[_g.row][i] == cb::EMPTY) {
        _cur = (uint8_t)i;
        return;
      }
  }

  // Solid pegs first, then rings, in a line beside the try.
  void pegs(ToolsCanvas& c, int r, int y) {
    using namespace cbui;
    const int cy = y + ROW_H / 2;
    int k = 0;
    for (int i = 0; i < _g.exact[r]; i++, k++) c.fillCircle(PEG_X + k * PEG_STEP, cy, PEG_R, true);
    for (int i = 0; i < _g.near[r]; i++, k++) c.drawCircle(PEG_X + k * PEG_STEP, cy, PEG_R, 3, true);
    if (k == 0) c.textTracked(PEG_X - 4, cy - c.textHeight(TS_SMALL) / 2, "none", TS_SMALL, true, false, 1);
  }

  const char* recordLine() {
    char k[8];
    key(k, sizeof(k), "cb_w", _g.level);
    const int w = geti(k);
    key(k, sizeof(k), "cb_g", _g.level);
    const int g = geti(k);
    key(k, sizeof(k), "cb_b", _g.level);
    const int b = geti(k);
    if (!g)
      snprintf(_foot, sizeof(_foot), "fill the row, then Check");
    else if (b)
      snprintf(_foot, sizeof(_foot), "won %d of %d - best %d %s", w, g, b, b == 1 ? "try" : "tries");
    else
      snprintf(_foot, sizeof(_foot), "won %d of %d", w, g);
    return _foot;
  }

  void newGame() {
    cb::reset(_g, nextLevel(), rnd);
    _cur = 0;
    save();
  }

  // Leaving a game with tries in it counts as a game played, not won -- so
  // New game cannot quietly skip a hard code.
  void abandon() {
    if (_g.done || _g.row == 0) return;
    char k[8];
    key(k, sizeof(k), "cb_g", _g.level);
    bump(k);
  }

  void check() {
    if (!cb::rowFull(_g)) {
      host().beep(2);
      return;
    }
    cb::submit(_g);
    if (_g.done) {
      char k[8];
      key(k, sizeof(k), "cb_g", _g.level);
      bump(k);
      if (_g.done == 1) {
        key(k, sizeof(k), "cb_w", _g.level);
        bump(k);
        key(k, sizeof(k), "cb_b", _g.level);
        const int best = geti(k);
        if (!best || _g.row < best) prefs().putInt(k, _g.row);
      }
      save();
      host().beep(_g.done == 1 ? 3 : 2);
      host().refresh(true);
      return;
    }
    firstEmpty();
    save();
    host().beep(1);
    host().refresh(false);
  }
};
