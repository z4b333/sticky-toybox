// The last books opened, for the Study drawer's recently-read strip.
//
// Two entries in NVS as one blob: a reader notes a book every time one is
// opened, the hub lists them under the Study tiles, and tapping one reopens
// the book directly at its saved position. The file field is whatever the
// owning reader lists books by -- a bare .tbk name, an absolute .epub path --
// so handing it back to the same reader's openDirect() always means the same
// book.
#pragma once
#include <string.h>

#include "tools_ui.h"

namespace recents {

inline constexpr int MAX = 2;
inline constexpr uint8_t KIND_TBK = 0, KIND_EPUB = 1;

struct Entry {
  uint8_t kind = KIND_TBK;
  char file[128] = "";  // must hold whatever the readers list books by
  char title[41] = "";
};

// The NVS key carries a 2 because the entry grew (file 64 -> 128 bytes) and
// an old-layout blob must read as empty rather than as garbage titles.
inline int list(Preferences& p, Entry* out) {
  Entry e[MAX];
  const size_t got = p.getBytes("recents2", e, sizeof(e));
  const int n = (int)(got / sizeof(Entry));
  for (int i = 0; i < n && i < MAX; i++) out[i] = e[i];
  return n > MAX ? MAX : n;
}

inline void note(Preferences& p, uint8_t kind, const char* file, const char* title) {
  Entry old[MAX];
  const int n = list(p, old);
  Entry now[MAX] = {};
  now[0].kind = kind;
  strncpy(now[0].file, file, sizeof(now[0].file) - 1);
  strncpy(now[0].title, title, sizeof(now[0].title) - 1);
  int w = 1;
  for (int i = 0; i < n && w < MAX; i++) {
    if (old[i].kind == kind && strcmp(old[i].file, file) == 0) continue;  // moved to the front
    now[w++] = old[i];
  }
  p.putBytes("recents2", now, sizeof(Entry) * (size_t)w);
}

}  // namespace recents
