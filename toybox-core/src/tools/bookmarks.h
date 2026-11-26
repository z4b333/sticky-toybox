// Places in a book somebody wanted to keep.
//
// On the CARD, not in flash, for the same reason covers are: a card carried to
// another Sticky brings its books, its reading positions and now its marks,
// and a reflash takes none of them away. The file sits in the book's own
// folder (book_store.h), which is named from the book's path, like the cover
// -- so the same caveat: rename the book and the marks are orphaned, exactly
// as its reading position is. The file manager already says so at the moment
// of renaming.
//
// Sixteen per book: this is a shelf for the handful of places worth going back
// to, not a highlighting system. Each carries the words that were on the page
// when it was kept, because "ch 7, page 12" is not a memory of anything -- and
// page 12 stops being page 12 the moment the type changes, where the words do
// not.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "book_store.h"
#include "tools_ui.h"

namespace marks {

// Sixty-four per book (D, 26 Nov: Readest keeps up to 256 a book; this was
// sixteen when marks were a shelf of places rather than highlights).
inline constexpr int MAX = 64;
inline constexpr uint16_t TBK_SPINE = 0xFFFF;  // a .tbk has no chapters
// A highlight that came from Readest has no page in THIS layout until it is
// found; the list then says the chapter alone.
inline constexpr uint16_t NO_PAGE = 0xFFFF;

// spine + offset is an EPUB position (a visible-codepoint offset, CrossPoint's
// counting -- the same Readest's clippings carry, so a pulled highlight lands
// exactly); `page` is what the .tbk reader uses and what an EPUB shows in its
// list, remembering that a page number is only true for the layout that made
// it. `end` is the highlight's end offset when known (pulled ones), else 0.
//
// 96 bytes of label: the phrase as it is matched on the page (lines joined by
// single spaces), cut with "..." when longer.
inline constexpr int LABEL = 96;
inline constexpr int ID = 40;  // Readest's note id; empty for one kept here
enum Src : uint8_t { SRC_HERE = 0, SRC_READEST = 1 };

struct Mark {
  uint16_t spine;
  uint16_t page;
  uint32_t off;
  uint32_t end;
  uint8_t src;
  uint8_t flags;
  char id[ID];
  char label[LABEL];  // the phrase, or empty for a .tbk page
};

inline uint32_t key(const char* file) {
  uint32_t h = 2166136261u;
  for (const char* p = file; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

inline void path(const char* file, char* out, int cap) {
  bstore::file(file, "marks.tbm", out, cap);
}
// Before the per-book folders. Still read when the new file is not there;
// removed by the first save that writes the new one.
inline void oldPath(const char* file, char* out, int cap) {
  snprintf(out, (size_t)cap, "/.toybox/marks/%08lx.tbm", (unsigned long)key(file));
}

// "TBM4", u16 count, u16 tombstones, then MAX entries of 150 bytes, then
// TOMBS ids of 40. Fixed length, so a save is one write and a load is one
// read, and a truncated file is refused rather than guessed at.
//
// Tombstones: Readest highlights removed on this Sticky. They keep a pull
// from bringing the highlight straight back, and are what part B sends to
// Readest as deletions. One book is open at a time, so they live in this
// module between load() and save() (tombs()).
//
// TBM3 (16 x 104) and TBM2 (16 x 48) are still READ; the next save writes TBM4.
inline constexpr int TOMBS = 32;
inline constexpr int ENTRY = 14 + ID + LABEL;  // 150
inline constexpr int HEAD = 8;
inline constexpr int FILE_BYTES = HEAD + MAX * ENTRY + TOMBS * ID;
inline constexpr int OLD_MAX = 16;
inline constexpr int LABEL_V3 = 96, ENTRY_V3 = 8 + LABEL_V3, FILE_BYTES_V3 = 6 + OLD_MAX * ENTRY_V3;
inline constexpr int LABEL_V2 = 40;
inline constexpr int ENTRY_V2 = 8 + LABEL_V2;
inline constexpr int FILE_BYTES_V2 = 6 + OLD_MAX * ENTRY_V2;

struct Tombs {
  int n = 0;
  char id[TOMBS][ID] = {};
};
inline Tombs& tombs() {
  static Tombs t;
  return t;
}
inline bool buried(const char* id) {
  if (!id || !id[0]) return false;
  const Tombs& t = tombs();
  for (int i = 0; i < t.n; i++)
    if (strcmp(t.id[i], id) == 0) return true;
  return false;
}
inline void bury(const char* id) {
  if (!id || !id[0] || buried(id)) return;
  Tombs& t = tombs();
  if (t.n == TOMBS) {  // the oldest goes: it has had the longest to reach Readest
    memmove(t.id[0], t.id[1], (size_t)(TOMBS - 1) * ID);
    t.n--;
  }
  snprintf(t.id[t.n++], ID, "%s", id);
}

inline bool save(ToolsHost& h, const char* file, const Mark* m, int n);

inline uint32_t rd32(const uint8_t* e) {
  return (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
}
inline void wr32(uint8_t* e, uint32_t v) {
  e[0] = (uint8_t)(v & 255);
  e[1] = (uint8_t)((v >> 8) & 255);
  e[2] = (uint8_t)((v >> 16) & 255);
  e[3] = (uint8_t)((v >> 24) & 255);
}

// Moves an old-place file across as it is read, so the old folder empties
// without waiting for the owner to keep another mark.
inline int load(ToolsHost& h, const char* file, Mark* out) {
  tombs() = Tombs{};
  char p[bstore::PATH_CAP];
  path(file, p, sizeof(p));
  // From the heap (PSRAM): 10.9 KB is no stack frame's, nor .bss's, business.
  uint8_t* buf = (uint8_t*)malloc(FILE_BYTES);
  if (!buf) return 0;
  int got = h.sdReadFile(p, buf, FILE_BYTES);
  bool fromOld = false;
  if (got <= 0) {
    oldPath(file, p, sizeof(p));
    got = h.sdReadFile(p, buf, FILE_BYTES);
    fromOld = got > 0;
  }
  int n = 0;
  if (got == FILE_BYTES && memcmp(buf, "TBM4", 4) == 0) {
    n = buf[4] | (buf[5] << 8);
    int t = buf[6] | (buf[7] << 8);
    if (n > MAX) n = MAX;
    if (t > TOMBS) t = TOMBS;
    for (int i = 0; i < n; i++) {
      const uint8_t* e = buf + HEAD + i * ENTRY;
      Mark& o = out[i];
      o.spine = (uint16_t)(e[0] | (e[1] << 8));
      o.page = (uint16_t)(e[2] | (e[3] << 8));
      o.off = rd32(e + 4);
      o.end = rd32(e + 8);
      o.src = e[12];
      o.flags = e[13];
      memcpy(o.id, e + 14, ID);
      o.id[ID - 1] = 0;
      memcpy(o.label, e + 14 + ID, LABEL);
      o.label[LABEL - 1] = 0;
    }
    Tombs& tb = tombs();
    tb.n = t;
    for (int i = 0; i < t; i++) {
      memcpy(tb.id[i], buf + HEAD + MAX * ENTRY + i * ID, ID);
      tb.id[i][ID - 1] = 0;
    }
  } else {
    int entry = 0, labelBytes = 0;
    if (got == FILE_BYTES_V3 && memcmp(buf, "TBM3", 4) == 0) {
      entry = ENTRY_V3;
      labelBytes = LABEL_V3;
    } else if (got == FILE_BYTES_V2 && memcmp(buf, "TBM2", 4) == 0) {
      entry = ENTRY_V2;
      labelBytes = LABEL_V2;
    }
    if (entry) {
      n = buf[4] | (buf[5] << 8);
      if (n > OLD_MAX) n = OLD_MAX;
      for (int i = 0; i < n; i++) {
        const uint8_t* e = buf + 6 + i * entry;
        Mark& o = out[i];
        o = Mark{};
        o.spine = (uint16_t)(e[0] | (e[1] << 8));
        o.page = (uint16_t)(e[2] | (e[3] << 8));
        o.off = rd32(e + 4);
        memcpy(o.label, e + 8, (size_t)labelBytes);
        o.label[labelBytes - 1] = 0;
      }
    }
  }
  free(buf);
  if (fromOld) save(h, file, out, n);  // writes the new place, removes the old
  return n;
}

inline bool save(ToolsHost& h, const char* file, const Mark* m, int n) {
  if (n > MAX) n = MAX;
  uint8_t* buf = (uint8_t*)calloc(1, FILE_BYTES);
  if (!buf) return false;
  memcpy(buf, "TBM4", 4);
  const Tombs& tb = tombs();
  buf[4] = (uint8_t)(n & 255);
  buf[5] = (uint8_t)(n >> 8);
  buf[6] = (uint8_t)(tb.n & 255);
  buf[7] = (uint8_t)(tb.n >> 8);
  for (int i = 0; i < n; i++) {
    uint8_t* e = buf + HEAD + i * ENTRY;
    e[0] = (uint8_t)(m[i].spine & 255);
    e[1] = (uint8_t)(m[i].spine >> 8);
    e[2] = (uint8_t)(m[i].page & 255);
    e[3] = (uint8_t)(m[i].page >> 8);
    wr32(e + 4, m[i].off);
    wr32(e + 8, m[i].end);
    e[12] = m[i].src;
    e[13] = m[i].flags;
    memcpy(e + 14, m[i].id, ID);
    memcpy(e + 14 + ID, m[i].label, LABEL);
  }
  for (int i = 0; i < tb.n; i++) memcpy(buf + HEAD + MAX * ENTRY + i * ID, tb.id[i], ID);
  char p[bstore::PATH_CAP];
  path(file, p, sizeof(p));
  const bool ok = h.sdWriteFileAtomic(p, buf, FILE_BYTES);
  free(buf);
  if (!ok) return false;
  oldPath(file, p, sizeof(p));
  if (h.sdRemove(p)) h.sdRmdir("/.toybox/marks");  // moved; the folder goes when empty
  return true;
}

// Kept in reading order (chapter, then offset), so the list reads like the
// book. Returns the index it landed at, or -1 when it is already there: the
// same Readest id, or -- for one kept here -- the same page (asking twice for
// the same page should not fill the list with duplicates).
inline int add(Mark* m, int& n, const Mark& add) {
  for (int i = 0; i < n; i++) {
    if (add.id[0]) {
      if (strcmp(m[i].id, add.id) == 0) return -1;
    } else if (!m[i].id[0] && m[i].spine == add.spine && m[i].page == add.page &&
               add.page != NO_PAGE) {
      return -1;
    }
  }
  if (n >= MAX) return -1;
  int at = 0;
  while (at < n && (m[at].spine < add.spine || (m[at].spine == add.spine && m[at].off <= add.off)))
    at++;
  for (int i = n; i > at; i--) m[i] = m[i - 1];
  m[at] = add;
  n++;
  return at;
}

inline void remove(Mark* m, int& n, int at) {
  if (at < 0 || at >= n) return;
  for (int i = at; i + 1 < n; i++) m[i] = m[i + 1];
  n--;
}

}  // namespace marks
