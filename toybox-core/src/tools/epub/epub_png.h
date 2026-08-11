// A small streaming PNG decoder, enough for EPUB cover images.
//
// Built on the same vendored miniz inflate the zip reader uses (PNG's IDAT is
// a zlib stream), following CrossPoint's proof that this is all a cover
// pipeline needs. Supported: 8-bit depth, colour types 0/2/3/4/6, filters
// 0-4, non-interlaced. Everything else returns false and the caller draws the
// plate instead. Output is grayscale rows, alpha composited over white --
// covers become 1-bit thumbnails, so colour would be thrown away anyway.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <toybox_miniz.h>

namespace epng {

// Caps chosen so per-cell accumulation in the caller cannot overflow 16 bits.
inline constexpr int MAX_W = 1400, MAX_H = 2240;

struct In {
  void* ctx;
  int (*read)(void* ctx, uint8_t* dst, int n);  // sequential; <=0 means end
};

using SizeFn = bool (*)(void* uctx, int w, int h);                       // false aborts
using RowFn = void (*)(void* uctx, int y, const uint8_t* gray, int w);

namespace detail {

inline uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Reads exactly n bytes from the source or fails.
inline bool pull(In& in, uint8_t* dst, int n) {
  int got = 0;
  while (got < n) {
    const int r = in.read(in.ctx, dst + got, n - got);
    if (r <= 0) return false;
    got += r;
  }
  return true;
}

inline bool skip(In& in, uint32_t n) {
  uint8_t junk[64];
  while (n) {
    const uint32_t take = n < sizeof(junk) ? n : (uint32_t)sizeof(junk);
    if (!pull(in, junk, (int)take)) return false;
    n -= take;
  }
  return true;
}

inline int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

}  // namespace detail

inline bool decodeGray(In in, SizeFn sizeCb, RowFn rowCb, void* uctx) {
  using namespace detail;
  uint8_t sig[8];
  static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (!pull(in, sig, 8) || memcmp(sig, PNG_SIG, 8) != 0) return false;

  int w = 0, h = 0, colour = -1, channels = 0;
  uint8_t palette[256 * 3];
  bool sawIhdr = false;

  // inflate state, allocated when the first IDAT arrives
  tinfl_decompressor* inf = nullptr;
  uint8_t* window = nullptr;
  uint8_t* rows = nullptr;  // two filtered scanlines + one gray output row
  uint8_t* prev = nullptr;
  uint8_t* cur = nullptr;
  uint8_t* gray = nullptr;
  int rowBytes = 0, rowFill = 0, y = 0;
  uint32_t winPos = 0;

  bool ok = false;
  bool done = false;

  // Feeds decompressed bytes into scanline assembly.
  auto acceptOut = [&](const uint8_t* d, size_t n) {
    while (n && y < h) {
      const int want = rowBytes - rowFill;
      const int take = (int)(n < (size_t)want ? n : (size_t)want);
      memcpy(cur + rowFill, d, (size_t)take);
      rowFill += take;
      d += take;
      n -= (size_t)take;
      if (rowFill < rowBytes) continue;
      rowFill = 0;
      // unfilter in place (cur[0] is the filter byte; pixels start at 1)
      const int bpp = channels;
      uint8_t* px = cur + 1;
      const uint8_t* up = prev + 1;
      switch (cur[0]) {
        case 0: break;
        case 1:
          for (int i = bpp; i < rowBytes - 1; i++) px[i] = (uint8_t)(px[i] + px[i - bpp]);
          break;
        case 2:
          for (int i = 0; i < rowBytes - 1; i++) px[i] = (uint8_t)(px[i] + up[i]);
          break;
        case 3:
          for (int i = 0; i < rowBytes - 1; i++)
            px[i] = (uint8_t)(px[i] + ((up[i] + (i >= bpp ? px[i - bpp] : 0)) >> 1));
          break;
        case 4:
          for (int i = 0; i < rowBytes - 1; i++)
            px[i] = (uint8_t)(px[i] + paeth(i >= bpp ? px[i - bpp] : 0, up[i],
                                            i >= bpp ? up[i - bpp] : 0));
          break;
        default: done = true; return;  // unknown filter: corrupt
      }
      // to grayscale, alpha over white
      for (int x = 0; x < w; x++) {
        const uint8_t* s = px + x * bpp;
        int v = 255;
        switch (colour) {
          case 0: v = s[0]; break;
          case 2: v = (s[0] * 77 + s[1] * 150 + s[2] * 29) >> 8; break;
          case 3: {
            const uint8_t* pe = palette + s[0] * 3;
            v = (pe[0] * 77 + pe[1] * 150 + pe[2] * 29) >> 8;
            break;
          }
          case 4: v = 255 + s[1] * (s[0] - 255) / 255; break;
          case 6: {
            const int l = (s[0] * 77 + s[1] * 150 + s[2] * 29) >> 8;
            v = 255 + s[3] * (l - 255) / 255;
            break;
          }
        }
        gray[x] = (uint8_t)v;
      }
      rowCb(uctx, y, gray, w);
      y++;
      uint8_t* t = prev;
      prev = cur;
      cur = t;
      if (y >= h) done = true;
    }
  };

  uint8_t chunkHead[8];
  uint8_t inBuf[512];
  while (!done && pull(in, chunkHead, 8)) {
    const uint32_t len = be32(chunkHead);
    const bool isIhdr = memcmp(chunkHead + 4, "IHDR", 4) == 0;
    const bool isPlte = memcmp(chunkHead + 4, "PLTE", 4) == 0;
    const bool isIdat = memcmp(chunkHead + 4, "IDAT", 4) == 0;
    const bool isIend = memcmp(chunkHead + 4, "IEND", 4) == 0;

    if (isIhdr) {
      uint8_t ih[13];
      if (len != 13 || !pull(in, ih, 13)) break;
      w = (int)be32(ih);
      h = (int)be32(ih + 4);
      const int depth = ih[8];
      colour = ih[9];
      const int interlace = ih[12];
      static const int8_t CH[7] = {1, -1, 3, 1, 2, -1, 4};
      channels = colour >= 0 && colour <= 6 ? CH[colour] : -1;
      if (depth != 8 || interlace != 0 || channels < 0 || w < 1 || h < 1 || w > MAX_W ||
          h > MAX_H)
        break;
      if (!sizeCb(uctx, w, h)) break;
      sawIhdr = true;
      rowBytes = 1 + w * channels;
      rows = (uint8_t*)malloc((size_t)rowBytes * 2 + (size_t)w);
      if (!rows) break;
      memset(rows, 0, (size_t)rowBytes * 2);
      prev = rows;
      cur = rows + rowBytes;
      gray = rows + (size_t)rowBytes * 2;
      memset(palette, 0, sizeof(palette));
      if (!skip(in, 4)) break;  // crc
      continue;
    }
    if (!sawIhdr) {
      if (!skip(in, len + 4)) break;
      continue;
    }
    if (isPlte) {
      const uint32_t take = len < sizeof(palette) ? len : (uint32_t)sizeof(palette);
      if (!pull(in, palette, (int)take) || !skip(in, len - take + 4)) break;
      continue;
    }
    if (isIend) break;
    if (!isIdat) {
      if (!skip(in, len + 4)) break;
      continue;
    }

    // IDAT: inflate this chunk's payload through the wrapping window.
    if (!inf) {
      inf = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
      window = (uint8_t*)malloc(TINFL_LZ_DICT_SIZE);
      if (!inf || !window) break;
      tinfl_init(inf);
      winPos = 0;
    }
    uint32_t left = len;
    bool bad = false;
    while (left && !done) {
      const uint32_t take = left < sizeof(inBuf) ? left : (uint32_t)sizeof(inBuf);
      if (!pull(in, inBuf, (int)take)) {
        bad = true;
        break;
      }
      left -= take;
      size_t inPos = 0;
      while (inPos < take && !done) {
        size_t inBytes = take - inPos;
        size_t outBytes = TINFL_LZ_DICT_SIZE - winPos;
        const tinfl_status st = toybox_tinfl_decompress(
            inf, inBuf + inPos, &inBytes, window, window + winPos, &outBytes,
            TINFL_FLAG_HAS_MORE_INPUT | TINFL_FLAG_PARSE_ZLIB_HEADER);
        inPos += inBytes;
        acceptOut(window + winPos, outBytes);
        winPos = (winPos + (uint32_t)outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st < TINFL_STATUS_DONE) {
          bad = true;
          break;
        }
        if (st == TINFL_STATUS_DONE) break;
        if (inBytes == 0 && outBytes == 0) break;
      }
      if (bad) break;
    }
    if (bad) break;
    if (!skip(in, 4)) break;  // crc
  }

  ok = sawIhdr && y >= h && h > 0;
  free(rows);
  free(window);
  free(inf);
  return ok;
}

}  // namespace epng
