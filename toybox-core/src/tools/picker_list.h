// The picker's list: its limits, and the one text format both ends agree on.
//
// Shared by the tool and by its phone page so the two can never disagree about
// how many items fit or how long a name may be — the page reads these same
// numbers when it warns you.
#pragma once
#include <Arduino.h>

#include "unicode.h"

namespace plist {

constexpr int MAX_ITEMS = 10;  // what the list screen can show without scrolling
constexpr int MAX_CHARS = 20;  // fits a row at TS_MED with the delete button
// Storage is in bytes, and a character is not a byte. Everything the firmware
// renders lives in the BMP, so three bytes is the worst case in UTF-8 — a Thai
// or Chinese list of twenty characters needs sixty of them.
constexpr int MAX_BYTES = MAX_CHARS * 3;
constexpr size_t BLOB = (size_t)(MAX_BYTES + 1) * MAX_ITEMS + 8;

using Item = char[MAX_BYTES + 1];

// One item per line. Blank lines are skipped, ends are trimmed, over-long names
// are cut rather than rejected, and the tail past MAX_ITEMS is dropped.
//
// UTF-8 passes through whole. It used to be folded down to ASCII — accented
// Latin to its base letter, everything else to a question mark — because the
// device drew with an 8x8 font that had nothing else in it. That font has been
// gone a long time; notes and flashcards have rendered Thai and Chinese ever
// since, and a Thai picker list still arrived on the panel as six rows of
// ??????. The cut at MAX_CHARS falls on a codepoint boundary, never inside one.
inline int fromText(const char* text, Item* items) {
  int n = 0;
  const char* p = text;
  while (*p && n < MAX_ITEMS) {
    const char* eol = p;
    while (*eol && *eol != '\n' && *eol != '\r') eol++;

    int len = 0, chars = 0;
    char buf[MAX_BYTES + 1];
    for (const char* q = p; q < eol && chars < MAX_CHARS;) {
      const char* next = q;
      const uint32_t cp = uni::next(next);
      const int bytes = (int)(next - q);
      if (len + bytes > MAX_BYTES) break;
      if (cp >= 0x80) {
        // Nothing above the BMP has a glyph, so an emoji would draw as blanks.
        // Dropping it is quieter than a row of empty boxes.
        if (cp <= 0xFFFF) {
          for (int k = 0; k < bytes; k++) buf[len++] = q[k];
          chars++;
        }
      } else if (cp >= 32 && cp < 127) {
        buf[len++] = (char)cp;
        chars++;
      } else if (cp == '\t') {
        buf[len++] = ' ';
        chars++;
      }
      q = next;
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
