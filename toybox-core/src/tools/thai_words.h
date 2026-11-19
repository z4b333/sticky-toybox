// thai_words.h -- where a line of Thai may break: between words.
//
// Thai writes no spaces between words, and many words have no written vowel
// (คน, ของ, บ่อ), so the letters alone cannot say where one ends: ค|น and
// คน|ห look equally possible. The reader asks a word list instead -- ICU's
// thaidict.txt, 26k words, packed by tools/make_thai_dict.py (thai_dict.inc,
// ~135 KB in flash) -- and splits each Thai run into the fewest words it can.
// A stretch no word covers (a name, a typo) falls back to character clusters,
// so it still breaks, just less well.
//
// Pure: no font, no panel. `breaks()` is what the layout and the parser use.
#pragma once
#include <stdint.h>
#include <string.h>

#include "unicode.h"

namespace thaiwords {

#include "thai_dict.inc"

inline int cmp(const uint8_t* a, int na, const uint8_t* b, int nb) {
  const int c = memcmp(a, b, (size_t)(na < nb ? na : nb));
  return c ? c : na - nb;
}

// Is `key` a word, and does any word start with it? (The second answer is what
// lets a lookup stop growing a candidate early.)
struct Probe {
  bool word, prefix;
};
inline Probe probe(const uint8_t* key, int n) {
  // The last block whose first word is <= key holds key's lower bound, or the
  // next block's first word does.
  int lo = 0, hi = THAI_DICT_BLOCKS - 1, blk = 0;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const uint8_t* p = THAI_DICT_DATA + THAI_DICT_INDEX[mid];
    if (cmp(p + 1, p[0], key, n) <= 0) {
      blk = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  uint8_t w[THAI_DICT_LONGEST + 1];
  const uint8_t* p = THAI_DICT_DATA + THAI_DICT_INDEX[blk];
  int wl = *p++;
  memcpy(w, p, (size_t)wl);
  p += wl;
  int left = THAI_DICT_WORDS - blk * THAI_DICT_BLOCK;
  if (left > THAI_DICT_BLOCK) left = THAI_DICT_BLOCK;
  for (int k = 1;; k++) {
    const int c = cmp(w, wl, key, n);
    if (c == 0) return {true, true};
    if (c > 0) return {false, wl >= n && memcmp(w, key, (size_t)n) == 0};
    if (k >= left) {
      if (blk + 1 >= THAI_DICT_BLOCKS) return {false, false};
      p = THAI_DICT_DATA + THAI_DICT_INDEX[blk + 1];
      wl = *p;
      return {false, wl >= n && memcmp(p + 1, key, (size_t)n) == 0};
    }
    const int pre = *p++, suf = *p++;
    memcpy(w + pre, p, (size_t)suf);
    p += suf;
    wl = pre + suf;
  }
}

inline bool thaiLetter(uint32_t cp) { return cp >= 0x0E01 && cp <= 0x0E5B; }

// One character cluster from i: a leading vowel takes its consonant, and
// marks and following vowels stay with what they follow. The unit an unknown
// stretch breaks into.
inline int cluster(const uint32_t* cps, int n, int i) {
  int j = i + 1;
  if (uni::thaiLeading(cps[i]) && j < n) j++;
  while (j < n && !uni::breakBefore(cps[j - 1], cps[j])) j++;
  return j - i;
}

// Splits n Thai codepoints into the fewest words (a cluster no word covers
// costs as much as several words, so a known word always wins). start[i] is
// set where a piece begins. n is at most MAX_RUN -- a whole reader word (under
// 96 bytes) always fits. The work buffers are static, not on the stack: the
// reader's layout is already deep, and only the UI task ever calls this.
constexpr int MAX_RUN = 96;
inline void segment(const uint32_t* cps, int n, bool* start) {
  static uint8_t key[MAX_RUN];
  for (int i = 0; i < n; i++) key[i] = (uint8_t)(cps[i] - 0x0E00);
  static int16_t cost[MAX_RUN + 1];
  static uint8_t len[MAX_RUN + 1];
  cost[n] = 0;
  for (int i = n - 1; i >= 0; i--) {
    const int cl = cluster(cps, n, i);
    int best = 10 + cost[i + cl], bl = cl;
    for (int L = 1; L <= THAI_DICT_LONGEST && i + L <= n; L++) {
      const Probe pr = probe(key + i, L);
      // A word takes a repetition or abbreviation sign after it with it
      // (จริงๆ, กรุงเทพฯ): neither may start a line. Then it has to end where
      // a line may: not before a mark or a vowel that belongs to its last letter.
      int e = i + L;
      while (e < n && (cps[e] == 0x0E46 || cps[e] == 0x0E2F)) e++;
      if (pr.word && (e == n || uni::breakBefore(cps[e - 1], cps[e])) && 1 + cost[e] < best) {
        best = 1 + cost[e];
        bl = e - i;
      }
      if (!pr.prefix) break;
    }
    cost[i] = (int16_t)best;
    len[i] = (uint8_t)bl;
  }
  for (int i = 0; i < n; i++) start[i] = false;
  for (int i = 0; i < n; i += len[i]) start[i] = true;
}

// ok[b] (b = 0..strlen(s)) is set where a line may break before byte b. Thai
// runs break between the words segment() finds; everything else keeps the
// pairwise rule (uni::breakBefore). s is one word of the reader: under 96 bytes.
inline void breaks(const char* s, uint8_t* ok) {
  static uint32_t cps[MAX_RUN];
  static uint8_t at[MAX_RUN + 1];
  int n = 0;
  at[0] = 0;
  for (const char* p = s; *p && n < MAX_RUN && p - s < 255;) {
    at[n] = (uint8_t)(p - s);
    cps[n++] = uni::next(p);
    at[n] = (uint8_t)(p - s);
  }
  const int len = (int)strlen(s);
  for (int b = 0; b <= len; b++) ok[b] = 0;
  for (int i = 1; i < n; i++) ok[at[i]] = uni::breakBefore(cps[i - 1], cps[i]) ? 1 : 0;
  static bool start[MAX_RUN];
  for (int i = 0; i < n;) {
    if (!thaiLetter(cps[i])) {
      i++;
      continue;
    }
    int j = i;
    while (j < n && thaiLetter(cps[j]) && j - i < MAX_RUN) j++;
    segment(cps + i, j - i, start);
    for (int k = i + 1; k < j; k++) ok[at[k]] = start[k - i] ? 1 : 0;
    i = j;
  }
}

}  // namespace thaiwords
