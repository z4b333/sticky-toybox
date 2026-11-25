// The 52-card deck behind Blackjack and Higher or Lower (C4), with no
// drawing in it so the host logic test can deal from it. A card is 0..51:
// suit * 13 + rank, rank 0 the ace (tools_draw.h's suit order: spade, heart,
// diamond, club). cards.h draws them.
#pragma once
#include <stdint.h>
#include <string.h>

namespace cards {

constexpr int DECK = 52;
inline int suitOf(int card) { return card / 13; }
inline int rankOf(int card) { return card % 13; }  // 0 ace .. 12 king
inline bool hollow(int card) { return suitOf(card) == 1 || suitOf(card) == 2; }

struct Deck {
  uint8_t c[DECK];
  uint8_t pos;  // cards dealt so far
};

template <typename Rnd>
inline void shuffle(Deck& d, Rnd rnd) {
  for (int i = 0; i < DECK; i++) d.c[i] = (uint8_t)i;
  for (int i = DECK - 1; i > 0; i--) {
    const int j = (int)(rnd() % (uint32_t)(i + 1));
    const uint8_t t = d.c[i];
    d.c[i] = d.c[j];
    d.c[j] = t;
  }
  d.pos = 0;
}
inline int left(const Deck& d) { return DECK - d.pos; }
inline int deal(Deck& d) { return d.pos < DECK ? d.c[d.pos++] : -1; }

// Higher or Lower counts the ace high.
inline int highValue(int card) { return rankOf(card) == 0 ? 14 : rankOf(card) + 1; }
// +1 the guess was right, -1 wrong, 0 the same rank (a push).
inline int judge(int shown, int next, bool higher) {
  const int a = highValue(shown), b = highValue(next);
  if (a == b) return 0;
  return (b > a) == higher ? 1 : -1;
}

}  // namespace cards
