// The last books opened, for the Study drawer's recently-read strip.
//
// Two entries in NVS as one blob: a reader notes a book every time one is
// opened, the hub lists them under the Study tiles, and tapping one reopens
// the book directly at its saved position. The file field is the absolute
// .epub path the reader lists books by, so handing it back to its
// openDirect() always means the same book. (KIND_TBK entries are left over
// from the manga reader, removed in v2; list() drops them.)
#pragma once
#include <string.h>

#include "tools_ui.h"

namespace recents {

// Three (v2, owner 27 Nov): the Books home shows the one being read and two
// more. The Study drawer's strip still shows two.
inline constexpr int MAX = 3;
inline constexpr uint8_t KIND_TBK = 0, KIND_EPUB = 1;

struct Entry {
  uint8_t kind = KIND_TBK;
  char file[128] = "";  // must hold whatever the readers list books by
  char title[61] = "";  // 60 characters (owner, 27 Nov; was 40)
  char chap[41] = "";   // where it was left: the chapter's name
  int8_t pct = -1;      // ...and how far through the whole book, -1 unknown
};

// The NVS key carries a version because the entry grows: an old-layout blob
// must read as what it was, not as garbage titles. "recents2" (file 128,
// title 41) is read once and carried over.
struct EntryV2 {
  uint8_t kind;
  char file[128];
  char title[41];
};
inline int list(Preferences& p, Entry* out) {
  int w = 0;
  if (p.isKey("recents3")) {
    Entry e[MAX];
    const size_t got = p.getBytes("recents3", e, sizeof(e));
    const int n = (int)(got / sizeof(Entry));
    for (int i = 0; i < n && i < MAX; i++)
      if (e[i].kind == KIND_EPUB) out[w++] = e[i];
    return w;
  }
  if (p.isKey("recents2")) {
    EntryV2 e[2];
    const size_t got = p.getBytes("recents2", e, sizeof(e));
    const int n = (int)(got / sizeof(EntryV2));
    for (int i = 0; i < n && i < 2; i++) {
      if (e[i].kind != KIND_EPUB) continue;
      out[w] = Entry{};
      out[w].kind = e[i].kind;
      memcpy(out[w].file, e[i].file, sizeof(e[i].file));
      out[w].file[sizeof(out[w].file) - 1] = 0;
      strncpy(out[w].title, e[i].title, sizeof(out[w].title) - 1);
      w++;
    }
  }
  return w;
}

inline void save(Preferences& p, const Entry* e, int n) {
  p.putBytes("recents3", e, sizeof(Entry) * (size_t)n);
  if (p.isKey("recents2")) p.remove("recents2");
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
    if (old[i].kind == kind && strcmp(old[i].file, file) == 0) {  // moved to the front
      snprintf(now[0].chap, sizeof(now[0].chap), "%s", old[i].chap);
      now[0].pct = old[i].pct;
      continue;
    }
    now[w++] = old[i];
  }
  save(p, now, w);
}

// Where a book was left, for the Books home. Written only when it changed,
// so closing a book twice at the same place costs no flash.
inline void place(Preferences& p, const char* file, const char* chap, int pct) {
  Entry e[MAX];
  const int n = list(p, e);
  for (int i = 0; i < n; i++) {
    if (strcmp(e[i].file, file) != 0) continue;
    if (e[i].pct == pct && strncmp(e[i].chap, chap, sizeof(e[i].chap) - 1) == 0) return;
    snprintf(e[i].chap, sizeof(e[i].chap), "%s", chap);
    e[i].pct = (int8_t)pct;
    save(p, e, n);
    return;
  }
}

}  // namespace recents
