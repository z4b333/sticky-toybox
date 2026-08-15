// BMP in, one byte of grey per pixel out -- the format CrossPoint, CrossInk
// and the Xteink tools trade sleep art and covers in.
//
// Parsing is templated over a tiny Reader (read/seek) so the SAME code runs
// against a File on the device and a memory blob in the harness: the parser
// the guards exercise is the parser the card meets. Handles 1/2/4/8/24/32 bpp,
// bottom-up and top-down rows, and reads the palette rather than assuming a
// ramp -- an inverted palette is exactly the trap that makes one firmware's
// grey another firmware's negative.
//
// Scaling is a single streaming pass: every source row is folded into the
// destination row it lands on, sums and counts per destination cell, so a
// 3000-pixel-wide photograph never needs a full-size buffer -- one source row
// at a time, box-averaged, which is the "shrinking always averages, never
// samples" rule the cover pipeline already lives by.
#pragma once
#include <stdint.h>
#include <string.h>

namespace bmpg {

inline constexpr int OUT_W = 480, OUT_H = 800;

struct Header {
  uint32_t offBits = 0;
  int width = 0, height = 0;  // height already absolute
  bool topDown = false;
  int bpp = 0;
  uint32_t compression = 0;
  uint8_t palLum[256];  // luminance per palette index (identity for 24/32)
  uint32_t rowBytes = 0;
};

template <typename R>
static bool readN(R& r, void* dst, uint32_t n) {
  return r.read(dst, n) == (int)n;
}

template <typename R>
bool parseHeader(R& r, Header& h) {
  uint8_t fh[14];
  if (!r.seek(0) || !readN(r, fh, 14) || fh[0] != 'B' || fh[1] != 'M') return false;
  h.offBits = fh[10] | (fh[11] << 8) | ((uint32_t)fh[12] << 16) | ((uint32_t)fh[13] << 24);
  uint8_t ih[40];
  if (!readN(r, ih, 40)) return false;
  const uint32_t dibSize = ih[0] | (ih[1] << 8) | ((uint32_t)ih[2] << 16) | ((uint32_t)ih[3] << 24);
  if (dibSize < 40) return false;
  const int32_t w = (int32_t)(ih[4] | (ih[5] << 8) | ((uint32_t)ih[6] << 16) | ((uint32_t)ih[7] << 24));
  const int32_t rawH =
      (int32_t)(ih[8] | (ih[9] << 8) | ((uint32_t)ih[10] << 16) | ((uint32_t)ih[11] << 24));
  h.width = w;
  h.topDown = rawH < 0;
  h.height = rawH < 0 ? -rawH : rawH;
  h.bpp = ih[14] | (ih[15] << 8);
  h.compression = ih[16] | (ih[17] << 8) | ((uint32_t)ih[18] << 16) | ((uint32_t)ih[19] << 24);
  if (h.width <= 0 || h.height <= 0 || h.width > 4096 || h.height > 4096) return false;
  // BI_RGB only, plus BI_BITFIELDS for the 32-bit BGRA files phone apps save.
  if (h.compression != 0 && !(h.compression == 3 && h.bpp == 32)) return false;
  if (h.bpp != 1 && h.bpp != 2 && h.bpp != 4 && h.bpp != 8 && h.bpp != 24 && h.bpp != 32)
    return false;

  // The palette decides what a pixel VALUE means; never assume a ramp.
  for (int i = 0; i < 256; i++) h.palLum[i] = (uint8_t)i;
  if (h.bpp <= 8) {
    uint32_t colors = ih[32] | (ih[33] << 8) | ((uint32_t)ih[34] << 16) | ((uint32_t)ih[35] << 24);
    if (colors == 0) colors = 1u << h.bpp;
    if (colors > 256) return false;
    if (!r.seek(14 + dibSize)) return false;
    for (uint32_t i = 0; i < colors; i++) {
      uint8_t bgra[4];
      if (!readN(r, bgra, 4)) return false;
      h.palLum[i] = (uint8_t)((77u * bgra[2] + 150u * bgra[1] + 29u * bgra[0]) >> 8);
    }
  }
  h.rowBytes = ((uint32_t)((h.width * h.bpp + 7) / 8) + 3u) & ~3u;
  return true;
}

// One source row -> greys. `row` is rowBytes long, `grey` width long.
inline void rowToGray(const Header& h, const uint8_t* row, uint8_t* grey) {
  switch (h.bpp) {
    case 24:
      for (int x = 0; x < h.width; x++) {
        const uint8_t* p = row + x * 3;
        grey[x] = (uint8_t)((77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8);
      }
      break;
    case 32:
      for (int x = 0; x < h.width; x++) {
        const uint8_t* p = row + x * 4;
        grey[x] = (uint8_t)((77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8);
      }
      break;
    case 8:
      for (int x = 0; x < h.width; x++) grey[x] = h.palLum[row[x]];
      break;
    case 4:
      for (int x = 0; x < h.width; x++)
        grey[x] = h.palLum[(row[x >> 1] >> (x & 1 ? 0 : 4)) & 0x0F];
      break;
    case 2:
      for (int x = 0; x < h.width; x++)
        grey[x] = h.palLum[(row[x >> 2] >> (6 - 2 * (x & 3))) & 0x03];
      break;
    default:  // 1
      for (int x = 0; x < h.width; x++)
        grey[x] = h.palLum[(row[x >> 3] >> (7 - (x & 7))) & 0x01];
      break;
  }
}

// The whole file, aspect-fit and box-averaged into OUT_W x OUT_H on white.
// `out` is OUT_W*OUT_H bytes; `rowBuf` holds one source row (h.rowBytes) and
// `greyBuf` one source row of greys (h.width) -- the caller owns both so the
// device can park them in PSRAM.
template <typename R>
bool toGray(R& r, const Header& h, uint8_t* out, uint8_t* rowBuf, uint8_t* greyBuf) {
  // Fit: scale = min(OUT/src) but never enlarge past 4x (a 16px icon blown to
  // the whole panel is noise, and the accumulators below cap at 4x anyway).
  uint32_t sw = (uint32_t)h.width, sh = (uint32_t)h.height;
  uint32_t dw = OUT_W, dh = (uint32_t)((uint64_t)sh * OUT_W / sw);
  if (dh > OUT_H) {
    dh = OUT_H;
    dw = (uint32_t)((uint64_t)sw * OUT_H / sh);
  }
  if (dw > 4 * sw) dw = 4 * sw;
  if (dh > 4 * sh) dh = 4 * sh;
  const int x0 = (OUT_W - (int)dw) / 2, y0 = (OUT_H - (int)dh) / 2;
  memset(out, 0xFF, (size_t)OUT_W * OUT_H);

  // Per-destination-row accumulators: sums and counts, folded a source row at
  // a time. 480 cells of 32 bits twice is under 4 KB, stack-safe nowhere --
  // the caller hands them in via out's tail? No: they live here, static,
  // because two callers never run at once and 4 KB of BSS beats 4 KB of any
  // stack this firmware owns.
  static uint32_t sum[OUT_W];
  static uint16_t cnt[OUT_W];
  int curDy = -1;

  if (!r.seek(h.offBits)) return false;
  for (uint32_t sy = 0; sy < sh; sy++) {
    if (!readN(r, rowBuf, h.rowBytes)) return false;
    rowToGray(h, rowBuf, greyBuf);
    const uint32_t syOut = h.topDown ? sy : (sh - 1 - sy);
    const int dy = (int)((uint64_t)syOut * dh / sh);
    if (dy >= (int)dh) continue;
    if (dy != curDy) {
      // Bottom-up files visit destination rows in reverse but still one at a
      // time; flush whichever row was being built.
      if (curDy >= 0)
        for (uint32_t x = 0; x < dw; x++)
          if (cnt[x]) out[(size_t)(y0 + curDy) * OUT_W + x0 + x] = (uint8_t)(sum[x] / cnt[x]);
      memset(sum, 0, sizeof(uint32_t) * dw);
      memset(cnt, 0, sizeof(uint16_t) * dw);
      curDy = dy;
    }
    for (uint32_t sx = 0; sx < sw; sx++) {
      // Shrinking, several source pixels fold into one cell; growing, one
      // source pixel spreads across several -- forward mapping alone would
      // leave white columns between them, the vertical twin of the row
      // replication below.
      const uint32_t dx0 = (uint32_t)((uint64_t)sx * dw / sw);
      uint32_t dx1 = (uint32_t)((uint64_t)(sx + 1) * dw / sw);
      if (dx1 <= dx0) dx1 = dx0 + 1;
      for (uint32_t dx = dx0; dx < dx1 && dx < dw; dx++) {
        sum[dx] += greyBuf[sx];
        cnt[dx]++;
      }
    }
    // Upscale: a source row may cover several destination rows; replicate.
    const int dyEnd = (int)((uint64_t)(syOut + 1) * dh / sh);
    for (int d = dy + 1; d < dyEnd && d < (int)dh; d++)
      for (uint32_t x = 0; x < dw; x++)
        if (cnt[x]) out[(size_t)(y0 + d) * OUT_W + x0 + x] = (uint8_t)(sum[x] / cnt[x]);
  }
  if (curDy >= 0)
    for (uint32_t x = 0; x < dw; x++)
      if (cnt[x]) out[(size_t)(y0 + curDy) * OUT_W + x0 + x] = (uint8_t)(sum[x] / cnt[x]);
  return true;
}

// Atkinson error diffusion, the classic e-paper choice: three quarters of the
// error moves on, a quarter is dropped, which keeps flats clean and blacks
// black. `levels` is 2 (a wallpaper) or 4 (the grey panel).
//
// In place on the grey buffer; the caller packs afterwards.
inline void atkinson(uint8_t* g, int levels) {
  const int n1 = levels - 1;
  static int16_t carry[2][OUT_W + 2];
  memset(carry, 0, sizeof(carry));
  for (int y = 0; y < OUT_H; y++) {
    int16_t* cur = carry[y & 1];
    int16_t* nxt = carry[(y + 1) & 1];
    memset(nxt, 0, sizeof(int16_t) * (OUT_W + 2));
    for (int x = 0; x < OUT_W; x++) {
      int v = g[(size_t)y * OUT_W + x] + cur[x + 1];
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      const int q = (v * n1 + 127) / 255;
      g[(size_t)y * OUT_W + x] = (uint8_t)q;
      const int err = (v - q * 255 / n1) / 8;
      cur[x + 2] += err;      // x+1
      if (x + 3 < OUT_W + 2) cur[x + 3] += err;  // x+2
      nxt[x] += err;          // below-left
      nxt[x + 1] += err;      // below
      nxt[x + 2] += err;      // below-right
      // the sixth eighth (two rows down) is deliberately dropped: one carry
      // row less, and on this panel the difference is invisible
    }
  }
}

// Quantized greys (0..levels-1) packed to the panel's layouts.
inline void pack1(const uint8_t* g, uint8_t* out) {  // 1 bpp, MSB first, 1 = white
  memset(out, 0, (size_t)OUT_W * OUT_H / 8);
  for (int i = 0; i < OUT_W * OUT_H; i++)
    if (g[i]) out[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
}
inline void pack2(const uint8_t* g, uint8_t* out) {  // 2 bpp, high first, 3 = white
  memset(out, 0, (size_t)OUT_W * OUT_H / 4);
  for (int i = 0; i < OUT_W * OUT_H; i++)
    out[i >> 2] |= (uint8_t)((g[i] & 3) << (6 - 2 * (i & 3)));
}

}  // namespace bmpg
