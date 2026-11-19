// One folder per book, for everything the device makes about it.
//
//   /.toybox/books/<book's file name> ~<4 hex>/
//       cover.tbc            the loading-screen cover (book_thumbs.h)
//       art.idx              which pictures are prepared, and how many pages each
//       <8 hex>.<part>.g2    a prepared picture, four greys   (epub_art.h)
//       <8 hex>.<part>.b1    the same, one bit
//       marks.tbm            bookmarks (bookmarks.h)
//       position.bin         where the reader stopped (tool_epub.h)
//       place.txt            the shelf's note of it: chapter name, page
//
// Named after the book so a person looking at the card can tell which folder
// is whose, and clear one book by deleting one folder. The four hex digits
// are a hash of the book's whole path: two "Volume 01.epub" in different
// series folders are two different books and get two folders. Under the
// hidden /.toybox, so neither the shelf nor a phone's file list shows it, and
// it works for a book anywhere on the card, not only under /books.
//
// The reading position joined them in v2. Before, it lived in CrossPoint's
// folders (/.crosspoint/epub_<hash>/progress.bin and toybox.pos) so a card
// could move between the two firmwares; that is no longer a goal (owner,
// 18 Nov 2026), and an old position moves here the first time its book is
// opened.
//
// Before v2 these were spread by kind -- /.toybox/covers(2)/<hash>.tbc,
// /.toybox/art2/<hash>/..., /.toybox/marks/<hash>.tbm -- under names nobody
// could read. Each kind moves itself across the first time its book is
// opened (see the readers); nothing is rebuilt that can be moved.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace bstore {

// Longest path anything below builds, with room for a ".tmp" suffix: 15 for
// the root, 72 + 6 for the folder name, 1 + 14 for the longest leaf.
inline constexpr int PATH_CAP = 128;
inline constexpr int NAME_BYTES = 72;  // bytes of the book's own name kept

inline uint32_t key(const char* file) {
  uint32_t h = 2166136261u;
  for (const char* p = file; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

// The folder's name part: the book's file name without its extension, made
// safe for FAT (no \ / : * ? " < > | or control characters, no trailing dot
// or space), cut to NAME_BYTES bytes without splitting a UTF-8 character.
inline void name(const char* bookFile, char* out, int cap) {
  const char* base = strrchr(bookFile, '/');
  base = base ? base + 1 : bookFile;
  const char* dot = strrchr(base, '.');
  int len = (dot && dot > base) ? (int)(dot - base) : (int)strlen(base);
  int n = 0;
  const int lim = cap - 1 < NAME_BYTES ? cap - 1 : NAME_BYTES;
  for (int i = 0; i < len && n < lim; i++) {
    unsigned char c = (unsigned char)base[i];
    if (c < 0x20 || c == 0x7F || strchr("\\/:*?\"<>|", c)) c = '_';
    out[n++] = (char)c;
  }
  if (len > n) {
    // Cut short: drop a trailing character whose bytes did not all fit.
    int j = n;
    while (j > 0 && ((unsigned char)out[j - 1] & 0xC0) == 0x80) j--;
    if (j > 0) {
      const unsigned char lead = (unsigned char)out[j - 1];
      const int want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      if ((j - 1) + want > n) n = j - 1;
    }
  }
  while (n > 0 && (out[n - 1] == '.' || out[n - 1] == ' ')) n--;
  if (n == 0) n = snprintf(out, (size_t)cap, "book");
  out[n] = 0;
}

inline void dir(const char* bookFile, char* out, int cap) {
  char nm[NAME_BYTES + 1];
  name(bookFile, nm, sizeof(nm));
  snprintf(out, (size_t)cap, "/.toybox/books/%s ~%04lx", nm,
           (unsigned long)(key(bookFile) & 0xFFFFu));
}

inline void file(const char* bookFile, const char* leaf, char* out, int cap) {
  char d[PATH_CAP];
  dir(bookFile, d, sizeof(d));
  snprintf(out, (size_t)cap, "%s/%s", d, leaf);
}

}  // namespace bstore
