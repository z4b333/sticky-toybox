// When each book was last opened, for the shelf's order (owner, 26 Nov:
// "every shelf" -- most recently read first, the rest as before).
//
// One small file for the whole card rather than one per book, so a shelf of
// fifty is one read: "TBR1", u32 next number, u16 count, then count x
// {u32 fnv(path), u32 number}. Bigger number = read more recently. The 256
// most recent are kept; a book fallen off that end sorts with the unread.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tools_ui.h"

namespace readorder {

inline constexpr const char* PATH = "/.toybox/read_order.bin";
inline constexpr int MAX = 256;
struct Entry {
  uint32_t key, n;
};
struct Table {
  uint32_t next = 1;
  int count = 0;
  Entry e[MAX];
};

inline uint32_t keyOf(const char* path) {
  uint32_t h = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)path; *p; p++) h = (h ^ *p) * 16777619u;
  return h ? h : 1;
}

inline bool load(ToolsHost& h, Table& t) {
  t.next = 1;
  t.count = 0;
  uint8_t* buf = (uint8_t*)malloc(10 + MAX * 8);  // only while reading: RAM is short
  if (!buf) return false;
  const int got = h.sdReadFile(PATH, buf, 10 + MAX * 8);
  bool ok = got >= 10 && memcmp(buf, "TBR1", 4) == 0;
  if (ok) {
    memcpy(&t.next, buf + 4, 4);
    uint16_t c;
    memcpy(&c, buf + 8, 2);
    ok = c <= MAX && 10 + c * 8 <= got;
    if (ok) {
      t.count = c;
      memcpy(t.e, buf + 10, (size_t)c * 8);
    }
  }
  free(buf);
  return ok;
}

inline bool save(ToolsHost& h, const Table& t) {
  uint8_t* buf = (uint8_t*)malloc(10 + MAX * 8);
  if (!buf) return false;
  memcpy(buf, "TBR1", 4);
  memcpy(buf + 4, &t.next, 4);
  const uint16_t c = (uint16_t)t.count;
  memcpy(buf + 8, &c, 2);
  memcpy(buf + 10, t.e, (size_t)t.count * 8);
  const bool ok = h.sdWriteFileAtomic(PATH, buf, 10 + t.count * 8);
  free(buf);
  return ok;
}

// The table in memory: the shelf re-lists at moments the card is not powered
// (closing a book, say), so it sorts by this copy, read from the card once a
// session lets it and kept current by note().
struct Live {
  Table t;
  bool loaded = false;
};
inline Live& live() {
  static Live l;
  return l;
}
inline const Table& current(ToolsHost& h) {
  Live& l = live();
  if (!l.loaded && load(h, l.t)) l.loaded = true;
  return l.t;
}

// The book at `path` was just opened (the reader holds the card then).
inline void note(ToolsHost& h, const char* path) {
  Live& l = live();
  if (!l.loaded) l.loaded = load(h, l.t) || true;  // none on the card yet: start one
  Table& t = l.t;
  const uint32_t k = keyOf(path);
  int at = -1, oldest = 0;
  for (int i = 0; i < t.count; i++) {
    if (t.e[i].key == k) at = i;
    if (t.e[i].n < t.e[oldest].n) oldest = i;
  }
  if (at < 0) at = t.count < MAX ? t.count++ : oldest;
  t.e[at] = Entry{k, t.next++};
  save(h, t);
}

// How recently: 0 for never (or long ago), else bigger is more recent.
inline uint32_t rank(const Table& t, const char* path) {
  const uint32_t k = keyOf(path);
  for (int i = 0; i < t.count; i++)
    if (t.e[i].key == k) return t.e[i].n;
  return 0;
}

}  // namespace readorder
