// Blackjack (C4): one deck, Hit / Stand / Double, the dealer stands on soft
// 17, a blackjack pays 3:2 (rounded up, in your favour), no split.
//
// No display, so the host test can deal thousands of hands and check the
// arithmetic: hand values with soft aces, the dealer's rule, every payout, and
// that chips are never created or lost except by a settled hand.
#pragma once
#include <stdint.h>
#include <string.h>

#include "deck.h"

namespace bj {

constexpr int START_CHIPS = 100;
constexpr int BETS[3] = {5, 10, 25};
constexpr int RESHUFFLE_AT = 15;  // fewer cards than this left: a fresh deck first
constexpr int MAX_CARDS = 11;     // eleven is as many as can make 21

struct Hand {
  uint8_t c[MAX_CARDS];
  uint8_t n;
};

inline int cardPoints(int card) {
  const int r = cards::rankOf(card);
  return r == 0 ? 1 : r >= 9 ? 10 : r + 1;
}
// The best total at or under 21 if there is one. `soft` says an ace is
// counting eleven in it.
inline int value(const Hand& h, bool* soft = nullptr) {
  int v = 0, aces = 0;
  for (int i = 0; i < h.n; i++) {
    v += cardPoints(h.c[i]);
    if (cards::rankOf(h.c[i]) == 0) aces++;
  }
  const bool s = aces > 0 && v + 10 <= 21;
  if (soft) *soft = s;
  return s ? v + 10 : v;
}
inline bool natural(const Hand& h) { return h.n == 2 && value(h) == 21; }
inline bool bust(const Hand& h) { return value(h) > 21; }

// DEALER: you have stood (or doubled, or reached 21); the hidden card is
// face up and the dealer is drawing, one card per dealerStep, so the screen
// can show each draw (owner, 25 Nov: "too fast").
enum : uint8_t { BETTING = 0, PLAYING = 1, DONE = 2, DEALER = 3 };
enum : int8_t { LOSE = -1, PUSH = 0, WIN = 1, BLACKJACK = 2 };

struct Table {
  cards::Deck deck;
  Hand you, dealer;
  int32_t chips;
  int16_t bet;     // staked on the hand in play (doubled after Double)
  uint8_t phase;
  int8_t outcome;  // once DONE
  int16_t paid;    // what came back on the last hand, stake included
};

inline void add(Table& t, Hand& h) {
  if (h.n < MAX_CARDS) h.c[h.n++] = (uint8_t)cards::deal(t.deck);
}

template <typename Rnd>
inline void reset(Table& t, Rnd rnd) {
  memset(&t, 0, sizeof(t));
  cards::shuffle(t.deck, rnd);
  t.chips = START_CHIPS;
  t.phase = BETTING;
}

inline void settle(Table& t, int8_t outcome) {
  t.outcome = outcome;
  t.phase = DONE;
  int back = 0;
  if (outcome == BLACKJACK)
    back = t.bet + (t.bet * 3 + 1) / 2;
  else if (outcome == WIN)
    back = 2 * t.bet;
  else if (outcome == PUSH)
    back = t.bet;
  t.paid = (int16_t)back;
  t.chips += back;
}

inline void compareTotals(Table& t) {
  const int y = value(t.you), d = value(t.dealer);
  if (d > 21 || y > d)
    settle(t, WIN);
  else if (y == d)
    settle(t, PUSH);
  else
    settle(t, LOSE);
}

// The dealer draws to 17 and stands on any 17, soft ones included. One card
// per call; true while there is another to come, false once the hand is
// settled.
inline bool dealerStep(Table& t) {
  if (t.phase != DEALER) return false;
  if (value(t.dealer) < 17) {
    add(t, t.dealer);
    if (value(t.dealer) < 17) return true;
  }
  compareTotals(t);
  return false;
}
inline void finishDealer(Table& t) {
  while (dealerStep(t)) {
  }
}

inline bool canDeal(const Table& t, int bet) {
  return (t.phase == BETTING || t.phase == DONE) && t.chips >= bet;
}

// Stakes `bet` and deals two each. A blackjack on either side ends the hand
// at once (the dealer looks at the hidden card for one).
template <typename Rnd>
inline bool deal(Table& t, int bet, Rnd rnd) {
  if (!canDeal(t, bet)) return false;
  if (cards::left(t.deck) < RESHUFFLE_AT) cards::shuffle(t.deck, rnd);
  t.you.n = t.dealer.n = 0;
  t.chips -= bet;
  t.bet = (int16_t)bet;
  t.paid = 0;
  t.outcome = PUSH;
  t.phase = PLAYING;
  add(t, t.you);
  add(t, t.dealer);
  add(t, t.you);
  add(t, t.dealer);
  const bool y = natural(t.you), d = natural(t.dealer);
  if (y && d)
    settle(t, PUSH);
  else if (y)
    settle(t, BLACKJACK);
  else if (d)
    settle(t, LOSE);
  return true;
}

inline void hit(Table& t) {
  if (t.phase != PLAYING) return;
  add(t, t.you);
  if (bust(t.you))
    settle(t, LOSE);
  else if (value(t.you) == 21)
    t.phase = DEALER;  // nothing better to draw for
}

// Your turn ends; the dealer's starts (dealerStep / finishDealer).
inline void stand(Table& t) {
  if (t.phase == PLAYING) t.phase = DEALER;
}

inline bool canDouble(const Table& t) {
  return t.phase == PLAYING && t.you.n == 2 && t.chips >= t.bet;
}
// Doubles the stake for exactly one more card, then stands.
inline void doubleDown(Table& t) {
  if (!canDouble(t)) return;
  t.chips -= t.bet;
  t.bet = (int16_t)(t.bet * 2);
  add(t, t.you);
  if (bust(t.you))
    settle(t, LOSE);
  else
    t.phase = DEALER;
}

}  // namespace bj
