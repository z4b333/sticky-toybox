// Higher or Lower (C4). One card is showing; say whether the next is higher
// or lower. Aces are high, the same rank is a push (the run goes on), and the
// first wrong guess ends the run. The deck is reshuffled when it runs out.
//
// Shares the deck and the card faces with Blackjack (cards.h); the one rule
// that is its own, cards::judge, is host-tested there.
#pragma once
#include <esp_random.h>

#include "cards.h"
#include "kit_game.h"
#include "record.h"

namespace hlui {
inline constexpr int STAT_Y = 54;
inline TRect statRect(int i) { return TRect{20 + i * 150, STAT_Y, 140, gk::STAT_H}; }
inline constexpr TRect PREV{20, 196, 120, 168};    // the card you guessed from
inline constexpr TRect CARD{176, 140, 284, 380};   // the card showing now
inline constexpr int MSG_Y = 538;
inline constexpr TRect HIGHER{20, 584, 214, 120};
inline constexpr TRect LOWER{246, 584, 214, 120};
inline constexpr TRect AGAIN{20, 584, 440, 120};
}  // namespace hlui

class HiLoTool : public KitGame {
 public:
  const char* title() const override { return "HIGHER OR LOWER"; }

  bool onButton(SideBtn b) override {
    if (_view != View::Play) return false;
    if (_s.over) {
      if (b != SideBtn::Ok) return false;
      again();
      return true;
    }
    if (b == SideBtn::Ok) return false;
    guess(b == SideBtn::Up);
    return true;
  }

#ifdef TOYBOX_HOST
  int hostShown() const { return _s.shown; }
  int hostNext() const { return _s.d.pos < cards::DECK ? _s.d.c[_s.d.pos] : -1; }
  bool hostOver() const { return _s.over; }
  int hostStreak() const { return _s.streak; }
  void hostGuess(bool higher) { guess(higher); }
#endif

 protected:
  const help::Text& rulesText() const override { return help::HILO; }
  const char* helpKey() const override { return "hl"; }

  void load() override {
    if (prefs().getBytesLength("hl_state") == sizeof(_s) &&
        prefs().getBytes("hl_state", &_s, sizeof(_s)) == sizeof(_s) && _s.d.pos >= 1 &&
        _s.d.pos <= cards::DECK && _s.shown < cards::DECK)
      return;
    start();
  }

  int extraRows(gk::Row* rows) override {
    rows[0] = {"New run", nullptr, false};
    return 1;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "New run")) {
      // A run abandoned with a streak on it still counts as one.
      if (!_s.over && _s.streak > 0) bump("hl_runs");
      start();
      return true;
    }
    return false;
  }
  bool hasRecord() override {
    for (const char* k : record::HILO)
      if (geti(k)) return true;
    return false;
  }
  void clearRecord() override { record::clear(prefs(), record::HILO); }

  void renderPlay(ToolsCanvas& c) override {
    using namespace hlui;
    gk::statInt(c, statRect(0), "STREAK", _s.streak);
    gk::statInt(c, statRect(1), "BEST", geti("hl_best"));
    gk::statInt(c, statRect(2), "CARDS LEFT", cards::left(_s.d));
    if (_s.prev < cards::DECK) {
      c.textTracked(PREV.x, PREV.y - 28, "WAS", TS_SMALL, true, false, 2);
      cards::face(c, PREV.x, PREV.y, PREV.w, PREV.h, _s.prev, TS_LARGE);
    }
    cards::face(c, CARD.x, CARD.y, CARD.w, CARD.h, _s.shown, TS_HUGE);
    const char* msg = _s.last > 0    ? "Right!"
                      : _s.last < 0 ? "Wrong - the run is over"
                      : _s.prev < cards::DECK ? "Same rank - a push"
                                              : "Higher or lower?";
    c.textCentered(c.width() / 2, MSG_Y, msg, TS_LARGE, true, true);
    if (_s.over) {
      c.button(AGAIN.x, AGAIN.y, AGAIN.w, AGAIN.h, "Play again", true, TS_HUGE);
      gk::foot(c, "");
      return;
    }
    c.button(HIGHER.x, HIGHER.y, HIGHER.w, HIGHER.h, "Higher", false, TS_HUGE);
    c.button(LOWER.x, LOWER.y, LOWER.w, LOWER.h, "Lower", false, TS_HUGE);
    arrow(c, HIGHER, true);
    arrow(c, LOWER, false);
    gk::foot(c, "");
  }

  void tapPlay(int x, int y) override {
    using namespace hlui;
    if (_s.over) {
      if (AGAIN.hit(x, y)) again();
      return;
    }
    if (HIGHER.hit(x, y))
      guess(true);
    else if (LOWER.hit(x, y))
      guess(false);
  }

 private:
  struct State {
    cards::Deck d;
    uint8_t shown;  // the card face up now
    uint8_t prev;   // the one before it, 0xFF at the start of a run
    int16_t streak;
    int8_t last;    // +1 right, -1 wrong, 0 push or nothing yet
    uint8_t over;
  };
  State _s{};

  static uint32_t rnd() { return esp_random(); }
  void save() { prefs().putBytes("hl_state", &_s, sizeof(_s)); }

  // A small triangle under the word, pointing the way the button means.
  static void arrow(ToolsCanvas& c, const TRect& r, bool up) {
    const int cx = r.x + r.w / 2, y = r.y + r.h - 26;
    if (up)
      decor::triangle(c, cx - 14, y + 12, cx + 14, y + 12, cx, y - 2, true);
    else
      decor::triangle(c, cx - 14, y - 2, cx + 14, y - 2, cx, y + 12, true);
  }

  void start() {
    cards::shuffle(_s.d, rnd);
    _s.shown = (uint8_t)cards::deal(_s.d);
    _s.prev = 0xFF;
    _s.streak = 0;
    _s.last = 0;
    _s.over = 0;
    save();
  }

  void again() {
    // The deck carries on: the next run starts from the card that ended this one.
    _s.prev = 0xFF;
    _s.streak = 0;
    _s.last = 0;
    _s.over = 0;
    save();
    host().beep(1);
    host().refresh(true);
  }

  void guess(bool higher) {
    if (_s.over) return;
    if (cards::left(_s.d) == 0) cards::shuffle(_s.d, rnd);
    const int next = cards::deal(_s.d);
    const int j = cards::judge(_s.shown, next, higher);
    _s.prev = _s.shown;
    _s.shown = (uint8_t)next;
    _s.last = (int8_t)j;
    if (j > 0) {
      _s.streak++;
      if (_s.streak > geti("hl_best")) prefs().putInt("hl_best", _s.streak);
      host().beep(0);
    } else if (j < 0) {
      _s.over = 1;
      bump("hl_runs");
      host().beep(2);
    } else {
      host().beep(0);
    }
    save();
    host().refresh(false);
  }
};
