// Cover thumbnails for the recently-read strip.
//
// A .tbk cover is page 0 shrunk 5x5 into a 96x160 one-bit thumbnail and kept
// in internal flash, so the hub can draw it without powering the card. The
// shrink AVERAGES each 5x5 block rather than sampling it -- the pages are
// dithered, and a sampled thumbnail of a dithered page is a field of noise
// (the lock-screen picture taught this once already).
//
// EPUBs get no stored thumbnail: the reader renders no images, so their
// "cover" is a card the hub draws live from the title.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiny_fs.h"

namespace bthumb {

inline constexpr int W = 96, H = 160;      // 480x800 over 5
inline constexpr int BYTES = W * H / 8;    // 1920, packed like every 1-bit image here

inline void path(const char* file, char* out, int cap) {
  uint32_t h = 2166136261u;
  for (const char* p = file; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  snprintf(out, (size_t)cap, "/th_%08lx", (unsigned long)h);
}

inline bool have(const char* file) {
  char p[20];
  path(file, p, sizeof(p));
  return tfs::size(p) == (size_t)BYTES;
}

// `page` is a full .tbk page: 1 bpp MSB-first 1=white, or 2 bpp four-level.
inline void makeAndSave(const char* file, const uint8_t* page, int bpp) {
  uint8_t out[BYTES];
  memset(out, 0xFF, sizeof(out));  // 1 = white, matching the framebuffer
  for (int ty = 0; ty < H; ty++) {
    for (int tx = 0; tx < W; tx++) {
      // Sum darkness over the 5x5 source block, then vote.
      int dark = 0;  // scaled so 1-bit and grey use one threshold
      for (int dy = 0; dy < 5; dy++) {
        const int y = ty * 5 + dy;
        for (int dx = 0; dx < 5; dx++) {
          const int x = tx * 5 + dx;
          if (bpp == 2) {
            const uint32_t i = (uint32_t)y * 480 + x;
            const int lv = (page[i >> 2] >> (6 - 2 * (i & 3))) & 3;  // 0 black .. 3 white
            dark += 3 - lv;
          } else {
            if (!(page[(size_t)y * 60 + (x >> 3)] & (0x80 >> (x & 7)))) dark += 3;
          }
        }
      }
      // 25 samples, 0..75: past half darkness the thumb pixel goes black.
      if (dark >= 38) out[(size_t)ty * (W / 8) + (tx >> 3)] &= ~(0x80 >> (tx & 7));
    }
  }
  char p[20];
  path(file, p, sizeof(p));
  tfs::write(p, (const char*)out, sizeof(out));
}

// Fills `dst` (BYTES) from flash; false when no thumbnail is stored.
inline bool load(const char* file, uint8_t* dst) {
  char p[20];
  path(file, p, sizeof(p));
  size_t len = 0;
  char* buf = tfs::readAlloc(p, len);
  if (!buf) return false;
  const bool ok = len == (size_t)BYTES;
  if (ok) memcpy(dst, buf, BYTES);
  free(buf);
  return ok;
}

}  // namespace bthumb
