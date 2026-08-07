// The picker's list: its limits, and the one text format both ends agree on.
//
// Shared by the tool and by its phone page so the two can never disagree about
// how many items fit or how long a name may be — the page reads these same
// numbers when it warns you.
#pragma once
#include <Arduino.h>

namespace plist {

constexpr int MAX_ITEMS = 10;  // what the list screen can show without scrolling
constexpr int MAX_LEN = 20;    // fits a row at TS_MED with the delete button
constexpr size_t BLOB = (size_t)(MAX_LEN + 1) * MAX_ITEMS + 8;

using Item = char[MAX_LEN + 1];

// The 8x8 font is ASCII 32..126, so anything else would draw as noise. Accented
// Latin is folded to its base letter rather than dropped — a phone keyboard
// produces it constantly, and "Jose" is a far better outcome than "Jos" or a
// row of boxes.
inline char foldLatin1(uint8_t hi) {
  static const char kFold[65] =
      "AAAAAAECEEEEIIIIDNOOOOOxOUUUUYPs"
      "aaaaaaeceeeeiiiidnooooo/ouuuuypy";
  return (hi >= 0x80 && hi <= 0xBF) ? kFold[hi - 0x80] : '?';
}

// One item per line. Blank lines are skipped, ends are trimmed, over-long names
// are cut rather than rejected, and the tail past MAX_ITEMS is dropped.
inline int fromText(const char* text, Item* items) {
  int n = 0;
  const char* p = text;
  while (*p && n < MAX_ITEMS) {
    const char* eol = p;
    while (*eol && *eol != '\n' && *eol != '\r') eol++;

    int len = 0;
    char buf[MAX_LEN + 1];
    for (const char* q = p; q < eol && len < MAX_LEN; q++) {
      const uint8_t ch = (uint8_t)*q;
      if (ch == 0xC3 && q + 1 < eol) {  // two-byte Latin supplement
        buf[len++] = foldLatin1((uint8_t)*++q);
      } else if (ch >= 0x80) {
        // Some other multi-byte sequence: emit one marker, skip its body.
        buf[len++] = '?';
        while (q + 1 < eol && ((uint8_t)q[1] & 0xC0) == 0x80) q++;
      } else if (ch >= 32 && ch < 127) {
        buf[len++] = (char)ch;
      } else if (ch == '\t') {
        buf[len++] = ' ';
      }
    }
    buf[len] = 0;
    while (len > 0 && buf[len - 1] == ' ') buf[--len] = 0;
    int lead = 0;
    while (buf[lead] == ' ') lead++;
    if (buf[lead]) {
      memcpy(items[n], buf + lead, (size_t)(len - lead) + 1);
      n++;
    }

    p = eol;
    while (*p == '\n' || *p == '\r') p++;
  }
  return n;
}

inline String toText(const Item* items, int n) {
  String out;
  out.reserve(BLOB);
  for (int i = 0; i < n; i++) {
    out += items[i];
    out += '\n';
  }
  return out;
}

}  // namespace plist
