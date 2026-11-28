// Blackjack (C4). One deck against the dealer; Hit, Stand or Double; the
// dealer stands on soft 17; a blackjack pays 3:2. You start with 100 chips and
// bet 5, 10 or 25 a hand. No split -- one hand on the screen at a time.
//
// Owner, 25 Nov ("too fast, and I can't tell when it is over"): after you
// stand, the dealer's hidden card turns and its draws come one at a time, a
// beat apart; a finished hand says so in a black banner, and the buttons
// become one wide Deal again.
//
// The table and its arithmetic live in blackjack_rules.h (host-tested).
#pragma once
#include <esp_random.h>

#include "blackjack_rules.h"
#include "cards.h"
#include "kit_game.h"
#include "record.h"

namespace bjui {
inline constexpr int CARD_W = 100, CARD_H = 140;
inline constexpr int DEALER_LABEL_Y = 54, DEALER_Y = 86;
inline constexpr int YOU_LABEL_Y = 244, YOU_Y = 276;
inline constexpr int MSG_Y = 424;
inline constexpr int STAT_Y = 486;
inline TRect statRect(int i) { return TRect{20 + i * 150, STAT_Y, 140, gk::STAT_H}; }
inline constexpr int BTN_Y = 584, BTN_H = 84;
inline constexpr TRect HIT{20, BTN_Y, 140, BTN_H};
inline constexpr TRect STAND{170, BTN_Y, 140, BTN_H};
inline constexpr TRect DOUBLE{320, BTN_Y, 140, BTN_H};
inline constexpr TRect BET{20, BTN_Y, 140, BTN_H};
inline constexpr TRect DEAL{172, BTN_Y, 288, BTN_H};
inline constexpr TRect RESTART{20, BTN_Y, 440, BTN_H};
inline constexpr int DEALER_BEAT = 35;  // ticks (~50 Hz) between the dealer's cards: 0.7 s
// Where card i of n sits: 110 px apart while they fit, closer once they don't.
inline int cardX(int i, int n) {
  const int step = n <= 1 ? 0 : (440 - CARD_W) / (n - 1) < 110 ? (440 - CARD_W) / (n - 1) : 110;
  return 20 + i * step;
}
}  // namespace bjui

class BlackjackTool : public KitGame {
 public:
  const char* title() const override { return "BLACKJACK"; }

  // Up hits and Down stands while a hand is on; between hands Up/Down move the
  // bet and OK deals.
  bool onButton(SideBtn b) override {
    if (_view != View::Play || _t.phase == bj::DEALER) return false;
    if (_t.phase == bj::PLAYING) {
      if (b == SideBtn::Ok) return false;
      act(b == SideBtn::Up ? 0 : 1);
      return true;
    }
    if (b == SideBtn::Ok) {
      if (broke())
        restart();
      else
        dealHand();
      return true;
    }
    stepBet(b == SideBtn::Up ? 1 : 2);
    return true;
  }

  // The dealer's turn plays out on the clock, one card a beat.
  bool wantsTick() const override { return _t.phase == bj::DEALER; }
  bool napsOk() const override { return true; }
  void tick() override {
    if (_t.phase != bj::DEALER || ++_beat < bjui::DEALER_BEAT) return;
    _beat = 0;
    if (!bj::dealerStep(_t))
      settled();
    else
      save();
    if (_view != View::Play) return;
    if (_t.phase == bj::DONE) host().beep(_t.outcome == bj::LOSE ? 2 : 3);
    host().refresh(false);
  }

#ifdef TOYBOX_HOST
  bj::Table& hostTable() { return _t; }
  void hostDealerDone() {  // the clock, run out at once
    for (int i = 0; i < 40 * bjui::DEALER_BEAT && _t.phase == bj::DEALER; i++) tick();
  }
#endif

 protected:
  const help::Text& rulesText() const override { return help::BLACKJACK; }
  const char* helpKey() const override { return "bj"; }

  void load() override {
    _beat = 0;
    _betIdx = geti("bj_bet", 1);
    if (_betIdx < 0 || _betIdx > 2) _betIdx = 1;
    if (prefs().getBytesLength("bj_tab") == sizeof(_t) &&
        prefs().getBytes("bj_tab", &_t, sizeof(_t)) == sizeof(_t) && _t.deck.pos <= cards::DECK &&
        _t.you.n <= bj::MAX_CARDS && _t.dealer.n <= bj::MAX_CARDS && _t.phase <= bj::DEALER)
      return;
    bj::reset(_t, rnd);
    save();
  }

  const char* corner() override {
    snprintf(_corner, sizeof(_corner), "bet %d", between() ? bet() : (int)_t.bet);
    return _corner;
  }

  int extraRows(gk::Row* rows) override {
    snprintf(_betLabel, sizeof(_betLabel), "%d chips", bet());
    rows[0] = {"Bet", _betLabel, false};
    rows[1] = {"Start again with 100", nullptr, false};
    return 2;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "Bet")) {
      _betIdx = (_betIdx + 1) % 3;
      prefs().putInt("bj_bet", _betIdx);
      return false;
    }
    if (!strncmp(what, "Start again", 11)) {
      bj::reset(_t, rnd);
      save();
      return true;
    }
    return false;
  }
  bool hasRecord() override {
    for (const char* k : record::BLACKJACK)
      if (geti(k)) return true;
    return false;
  }
  void clearRecord() override { record::clear(prefs(), record::BLACKJACK); }

  void renderPlay(ToolsCanvas& c) override {
    using namespace bjui;
    const bool hide = _t.phase == bj::PLAYING;  // the dealer's second card is face down
    char buf[48];
    // The dealer.
    c.textTracked(20, DEALER_LABEL_Y, "DEALER", TS_SMALL, true, false, 2);
    if (_t.dealer.n) {
      if (hide) {
        const int up = bj::cardPoints(_t.dealer.c[0]);
        snprintf(buf, sizeof(buf), "%d + ?", up == 1 ? 11 : up);
      } else {
        snprintf(buf, sizeof(buf), "%d", bj::value(_t.dealer));
      }
      c.text(460 - c.textWidth(buf, TS_MED, true), DEALER_LABEL_Y - 4, buf, TS_MED, true, true);
    }
    for (int i = 0; i < _t.dealer.n; i++) {
      const int x = cardX(i, _t.dealer.n);
      if (hide && i == 1)
        cards::back(c, x, DEALER_Y, CARD_W, CARD_H);
      else
        cards::face(c, x, DEALER_Y, CARD_W, CARD_H, _t.dealer.c[i]);
    }
    if (!_t.dealer.n) emptySpot(c, DEALER_Y);
    // You.
    c.textTracked(20, YOU_LABEL_Y, "YOU", TS_SMALL, true, false, 2);
    if (_t.you.n) {
      bool soft = false;
      const int v = bj::value(_t.you, &soft);
      snprintf(buf, sizeof(buf), soft && v < 21 ? "soft %d" : "%d", v);
      c.text(460 - c.textWidth(buf, TS_MED, true), YOU_LABEL_Y - 4, buf, TS_MED, true, true);
    }
    for (int i = 0; i < _t.you.n; i++)
      cards::face(c, cardX(i, _t.you.n), YOU_Y, CARD_W, CARD_H, _t.you.c[i]);
    if (!_t.you.n) emptySpot(c, YOU_Y);

    // How the hand ended: a black banner nobody can miss.
    if (_t.phase == bj::DONE) {
      message(buf, sizeof(buf));
      gk::slab(c, c.width() / 2, MSG_Y, buf, TS_LARGE);
    } else if (_t.phase == bj::DEALER) {
      c.textCentered(c.width() / 2, MSG_Y + 8, "Dealer plays...", TS_LARGE, true, true);
    }

    snprintf(buf, sizeof(buf), "%ld", (long)_t.chips);
    gk::stat(c, statRect(0), "CHIPS", buf);
    gk::statInt(c, statRect(1), "BET", between() ? bet() : _t.bet);
    gk::statInt(c, statRect(2), "HANDS WON", geti("bj_w"));

    if (_t.phase == bj::PLAYING) {
      c.button(HIT.x, HIT.y, HIT.w, HIT.h, "Hit", true, TS_LARGE);
      c.button(STAND.x, STAND.y, STAND.w, STAND.h, "Stand", false, TS_LARGE);
      if (bj::canDouble(_t))
        c.button(DOUBLE.x, DOUBLE.y, DOUBLE.w, DOUBLE.h, "Double", false, TS_LARGE);
      gk::foot(c, "");
      return;
    }
    if (_t.phase == bj::DEALER) {
      gk::foot(c, "wait for the dealer");
      return;
    }
    if (broke()) {
      c.button(RESTART.x, RESTART.y, RESTART.w, RESTART.h, "Start again with 100", true, TS_LARGE);
      gk::foot(c, "out of chips");
      return;
    }
    snprintf(buf, sizeof(buf), "Bet %d", bet());
    gk::choice(c, BET, buf, true, TS_LARGE);
    const bool over = _t.phase == bj::DONE;
    c.button(DEAL.x, DEAL.y, DEAL.w, DEAL.h, over ? "Deal again" : "Deal", true, TS_LARGE);
    gk::foot(c, "");
  }

  void tapPlay(int x, int y) override {
    using namespace bjui;
    if (_t.phase == bj::DEALER) return;  // the dealer's turn: nothing to press
    if (_t.phase == bj::PLAYING) {
      if (HIT.hit(x, y))
        act(0);
      else if (STAND.hit(x, y))
        act(1);
      else if (DOUBLE.hit(x, y) && bj::canDouble(_t))
        act(2);
      return;
    }
    if (broke()) {
      if (RESTART.hit(x, y)) restart();
      return;
    }
    if (BET.hit(x, y)) {
      stepBet(1);
      return;
    }
    if (DEAL.hit(x, y)) dealHand();
  }

 private:
  bj::Table _t{};
  int _betIdx = 1;
  int _beat = 0;
  char _corner[16] = {};
  char _betLabel[16] = {};

  static uint32_t rnd() { return esp_random(); }
  int bet() const { return bj::BETS[_betIdx]; }
  bool between() const { return _t.phase == bj::BETTING || _t.phase == bj::DONE; }
  bool broke() const { return between() && _t.chips < bj::BETS[0]; }
  void save() { prefs().putBytes("bj_tab", &_t, sizeof(_t)); }

  void emptySpot(ToolsCanvas& c, int y) {
    tdraw::roundRect(c, 20, y, bjui::CARD_W, bjui::CARD_H, 10, 1, true);
  }

  void stepBet(int by) {
    _betIdx = (_betIdx + by) % 3;
    prefs().putInt("bj_bet", _betIdx);
    host().beep(0);
    host().refresh(false);
  }

  void restart() {
    bj::reset(_t, rnd);
    save();
    host().beep(1);
    host().refresh(true);
  }

  void message(char* out, size_t n) {
    const int net = _t.paid - _t.bet;
    switch (_t.outcome) {
      case bj::BLACKJACK: snprintf(out, n, "BLACKJACK  +%d", net); break;
      case bj::WIN:
        snprintf(out, n, bj::bust(_t.dealer) ? "DEALER BUSTS  +%d" : "YOU WIN  +%d", net);
        break;
      case bj::PUSH: snprintf(out, n, "PUSH - BET BACK"); break;
      default:
        snprintf(out, n, bj::bust(_t.you)          ? "BUST  -%d"
                         : bj::natural(_t.dealer) ? "DEALER BLACKJACK  -%d"
                                                  : "DEALER WINS  -%d",
                 _t.bet);
        break;
    }
  }

  void dealHand() {
    if (!bj::canDeal(_t, bet())) {
      host().beep(2);
      return;
    }
    bj::deal(_t, bet(), rnd);
    if (_t.phase == bj::DONE)
      settled();
    else
      save();
    host().beep(1);
    // The whole table changes on a deal: clean it.
    host().refresh(true);
  }

  // 0 hit, 1 stand, 2 double
  void act(int what) {
    if (_t.phase != bj::PLAYING) return;
    if (what == 0)
      bj::hit(_t);
    else if (what == 1)
      bj::stand(_t);
    else
      bj::doubleDown(_t);
    _beat = 0;  // the dealer's first draw comes a beat after its card turns
    if (_t.phase == bj::DONE)
      settled();
    else
      save();
    host().beep(_t.phase == bj::DONE ? 2 : 0);
    host().refresh(false);
  }

  void settled() {
    if (_t.outcome == bj::WIN || _t.outcome == bj::BLACKJACK)
      bump("bj_w");
    else if (_t.outcome == bj::LOSE)
      bump("bj_l");
    else
      bump("bj_p");
    if (_t.chips > geti("bj_best", bj::START_CHIPS)) prefs().putInt("bj_best", (int)_t.chips);
    save();
  }
};
