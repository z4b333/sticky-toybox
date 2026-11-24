// The picker's list: its limits, and the one text format both ends agree on.
//
// Shared by the tool and by its phone page so the two can never disagree about
// how many items fit or how long a name may be — the page reads these same
// numbers when it warns you.
#pragma once
#include <Arduino.h>

#include "tiny_fs.h"
#include "unicode.h"

namespace plist {

// Owner, 24 Nov: 50 choices a list, paged on the device, and up to 8 named
// lists. A list is a file (/picker/<n>.txt, its name on the first line), not
// NVS: eight full lists are ~24 KB, more than the whole NVS partition.
constexpr int MAX_ITEMS = 50;
constexpr int MAX_CHARS = 20;  // fits a row at TS_MED with the delete button
constexpr int MAX_LISTS = 8;
constexpr int NAME_CHARS = 16;
constexpr int NAME_BYTES = NAME_CHARS * 3;
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
// One line, cleaned: control bytes and emoji dropped, tabs to spaces, ends
// trimmed, cut on a codepoint boundary at `maxChars` / `maxBytes`. Returns
// its length in bytes (0 for a blank line).
inline int cleanLine(const char* p, const char* eol, char* out, int maxChars, int maxBytes) {
  int len = 0, chars = 0;
  for (const char* q = p; q < eol && chars < maxChars;) {
    const char* next = q;
    const uint32_t cp = uni::next(next);
    const int bytes = (int)(next - q);
    if (len + bytes > maxBytes) break;
    if (cp >= 0x80) {
      // Nothing above the BMP has a glyph, so an emoji would draw as blanks.
      // Dropping it is quieter than a row of empty boxes.
      if (cp <= 0xFFFF) {
        for (int k = 0; k < bytes; k++) out[len++] = q[k];
        chars++;
      }
    } else if (cp >= 32 && cp < 127) {
      out[len++] = (char)cp;
      chars++;
    } else if (cp == '\t') {
      out[len++] = ' ';
      chars++;
    }
    q = next;
  }
  out[len] = 0;
  while (len > 0 && out[len - 1] == ' ') out[--len] = 0;
  int lead = 0;
  while (out[lead] == ' ') lead++;
  if (lead) memmove(out, out + lead, (size_t)(len - lead) + 1);
  return len - lead;
}

inline int fromText(const char* text, Item* items) {
  int n = 0;
  const char* p = text;
  while (*p && n < MAX_ITEMS) {
    const char* eol = p;
    while (*eol && *eol != '\n' && *eol != '\r') eol++;
    if (cleanLine(p, eol, items[n], MAX_CHARS, MAX_BYTES) > 0) n++;
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

// --- the lists on the filesystem ----------------------------------------------
// Kept packed: lists 0..n-1 exist, so deleting one moves the later ones down.
using Name = char[NAME_BYTES + 1];

inline void listPath(int i, char* out, size_t n) { snprintf(out, n, "/picker/%d.txt", i); }

inline void cleanName(const char* in, char* out) {
  const char* eol = in;
  while (*eol && *eol != '\n' && *eol != '\r') eol++;
  if (cleanLine(in, eol, out, NAME_CHARS, NAME_BYTES) == 0) strcpy(out, "List");
}

// The list's name and (when `items` is given) its choices. False when the
// list is not there.
inline bool readList(int i, char* name, Item* items, int* count) {
  char path[32];
  listPath(i, path, sizeof(path));
  String body;
  if (!tfs::exists(path) || !tfs::read(path, body)) return false;
  const char* t = body.c_str();
  const char* eol = t;
  while (*eol && *eol != '\n') eol++;
  if (cleanLine(t, eol, name, NAME_CHARS, NAME_BYTES) == 0) strcpy(name, "List");
  if (*eol) eol++;
  if (count) {
    if (items) {
      *count = fromText(eol, items);
    } else {
      int n = 0;  // a count without the room for the items
      for (const char* p = eol; *p && n < MAX_ITEMS;) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        for (const char* q = p; q < e; q++)
          if (*q != ' ' && *q != '\r' && *q != '\t') {
            n++;
            break;
          }
        p = *e ? e + 1 : e;
      }
      *count = n;
    }
  }
  return true;
}

inline bool writeList(int i, const char* name, const Item* items, int count) {
  tfs::ensureDir("/picker");
  char path[32];
  listPath(i, path, sizeof(path));
  String body = name;
  body += '\n';
  const String t = toText(items, count);
  body += t.c_str();
  return tfs::write(path, body.c_str(), body.length());
}

inline int listCount() {
  int n = 0;
  char path[32];
  for (; n < MAX_LISTS; n++) {
    listPath(n, path, sizeof(path));
    if (!tfs::exists(path)) break;
  }
  return n;
}

// Removes list `i` and moves the later ones down a place.
inline void removeList(int i, int n) {
  char from[32], to[32];
  listPath(i, to, sizeof(to));
  tfs::remove(to);
  for (int j = i + 1; j < n; j++) {
    listPath(j, from, sizeof(from));
    listPath(j - 1, to, sizeof(to));
    tfs::rename(from, to);
  }
}

}  // namespace plist
