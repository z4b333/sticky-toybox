// Places in a book somebody wanted to keep.
//
// On the CARD, not in flash, for the same reason covers are: a card carried to
// another Sticky brings its books, its reading positions and now its marks,
// and a reflash takes none of them away. The file is named from a hash of the
// book's absolute path, like the cover cache, which means the same caveat --
// rename the book and the marks are orphaned, exactly as its reading position
// is. The file manager already says so at the moment of renaming.
//
// Sixteen per book: this is a shelf for the handful of places worth going back
// to, not a highlighting system. Each carries the words that were on the page
// when it was kept, because "ch 7, page 12" is not a memory of anything -- and
// page 12 stops being page 12 the moment the type changes, where the words do
// not.
#pragma once
#include <stdint.h>
#include <string.h>

#include "tools_ui.h"

namespace marks {

inline constexpr int MAX = 16;
inline constexpr uint16_t TBK_SPINE = 0xFFFF;  // a .tbk has no chapters

// spine + offset is an EPUB position in CrossPoint's own terms; `page` is what
// the .tbk reader uses and what an EPUB shows in its list, remembering that a
// page number is only true for the layout that made it.
inline constexpr int LABEL = 40;

struct Mark {
  uint16_t spine;
  uint16_t page;
  uint32_t off;
  char label[LABEL];  // the phrase, or empty for a .tbk page
};

inline uint32_t key(const char* file) {
  uint32_t h = 2166136261u;
  for (const char* p = file; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

inline void path(const char* file, char* out, int cap) {
  snprintf(out, (size_t)cap, "/.toybox/marks/%08lx.tbm", (unsigned long)key(file));
}

// "TBM2", u16 count, then MAX entries of 48 bytes. Fixed length, so a save is
// one write and a load is one read, and a truncated file is refused rather
// than guessed at -- there is nothing here worth recovering half of.
inline constexpr int ENTRY = 8 + LABEL;
inline constexpr int FILE_BYTES = 6 + MAX * ENTRY;

inline int load(ToolsHost& h, const char* file, Mark* out) {
  char p[48];
  path(file, p, sizeof(p));
  uint8_t buf[FILE_BYTES];
  if (h.sdReadFile(p, buf, sizeof(buf)) != FILE_BYTES) return 0;
  if (memcmp(buf, "TBM2", 4) != 0) return 0;
  int n = buf[4] | (buf[5] << 8);
  if (n > MAX) n = MAX;
  for (int i = 0; i < n; i++) {
    const uint8_t* e = buf + 6 + i * ENTRY;
    out[i].spine = (uint16_t)(e[0] | (e[1] << 8));
    out[i].page = (uint16_t)(e[2] | (e[3] << 8));
    out[i].off = (uint32_t)e[4] | ((uint32_t)e[5] << 8) | ((uint32_t)e[6] << 16) |
                 ((uint32_t)e[7] << 24);
    memcpy(out[i].label, e + 8, LABEL);
    out[i].label[LABEL - 1] = 0;
  }
  return n;
}

inline bool save(ToolsHost& h, const char* file, const Mark* m, int n) {
  if (n > MAX) n = MAX;
  uint8_t buf[FILE_BYTES];
  memset(buf, 0, sizeof(buf));
  memcpy(buf, "TBM2", 4);
  buf[4] = (uint8_t)(n & 255);
  buf[5] = (uint8_t)(n >> 8);
  for (int i = 0; i < n; i++) {
    uint8_t* e = buf + 6 + i * ENTRY;
    e[0] = (uint8_t)(m[i].spine & 255);
    e[1] = (uint8_t)(m[i].spine >> 8);
    e[2] = (uint8_t)(m[i].page & 255);
    e[3] = (uint8_t)(m[i].page >> 8);
    e[4] = (uint8_t)(m[i].off & 255);
    e[5] = (uint8_t)((m[i].off >> 8) & 255);
    e[6] = (uint8_t)((m[i].off >> 16) & 255);
    e[7] = (uint8_t)((m[i].off >> 24) & 255);
    memcpy(e + 8, m[i].label, LABEL);
  }
  char p[48];
  path(file, p, sizeof(p));
  return h.sdWriteFileAtomic(p, buf, sizeof(buf));
}

// Marks are kept in reading order, so the list reads like the book. Returns
// the index it landed at, or -1 when this place is already kept (asking twice
// for the same page should not fill the shelf with duplicates).
inline int add(Mark* m, int& n, const Mark& add) {
  for (int i = 0; i < n; i++)
    if (m[i].spine == add.spine && m[i].page == add.page) return -1;
  if (n >= MAX) return -1;
  int at = 0;
  while (at < n && (m[at].spine < add.spine ||
                    (m[at].spine == add.spine && m[at].page < add.page)))
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
