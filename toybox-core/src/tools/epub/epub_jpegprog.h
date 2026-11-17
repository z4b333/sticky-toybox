// Progressive JPEG, decoded properly -- luma only, and only as sharp as the
// panel can use.
//
// Nearly every illustration in a commercial EPUB is a progressive JPEG (all
// nineteen in the first retail book tried). epub_jpegdc.h reads only such a
// file's first scan, the picture at an eighth: fine for a 96x160 thumbnail,
// a blur on a full page. A full progressive decode keeps every coefficient of
// every component until the last scan -- ~8 MB for a 1400x2000 page -- which
// is why it was never done here.
//
// Two things make it fit. Grey only: the chroma components' AC scans are
// skipped without being decoded (their DC is decoded, because the first scan
// interleaves it with luma's). And only the low frequencies are KEPT: a page
// that will be shown at 480x800 needs, of each 8x8 block, only the KxK
// corner a 1/2 or 1/4 IDCT reads. Every coefficient is still DECODED -- the
// bitstream cannot be followed otherwise -- and a 64-bit mask per block
// remembers which ones are nonzero, because the refinement scans need that
// history for the coefficients we did not keep. For 1403x2000 at half size:
// 44,000 blocks x (16 x 2 B + 8 B) = 1.7 MB, in PSRAM.
//
// Implements: SOF2 (progressive, huffman, 8-bit), DC first and refinement
// scans (interleaved or not), AC spectral selection and successive
// approximation including EOB runs, restart intervals, 8- and 16-bit
// quantisation tables. Anything else returns false and the caller falls back.
#pragma once
#include <math.h>
#include <new>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace ejprog {

struct In {
  void* ctx;
  int (*read)(void* ctx, uint8_t* dst, int n);  // sequential; <=0 means end
};

// size(w, h) once, at the reduced size; then row(y, grey, w) top to bottom.
using SizeFn = bool (*)(void* uctx, int w, int h);
using RowFn = void (*)(void* uctx, int y, const uint8_t* gray, int w);

namespace detail {

inline const uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct Huff {
  // Canonical tables: a 9-bit fast lookup, then the long codes by length.
  uint8_t fastLen[512];
  uint8_t fastSym[512];
  int32_t maxcode[18];
  int32_t valptr[17];
  int32_t mincode[17];
  uint8_t vals[256];
  bool ok = false;

  bool build(const uint8_t counts[16], const uint8_t* symbols, int nsym) {
    memset(fastLen, 0, sizeof(fastLen));
    memcpy(vals, symbols, (size_t)nsym);
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
      valptr[l] = k;
      mincode[l] = code;
      for (int i = 0; i < counts[l - 1]; i++, k++, code++) {
        if (l <= 9) {
          const int lo = code << (9 - l), n = 1 << (9 - l);
          for (int j = 0; j < n; j++) {
            fastLen[lo + j] = (uint8_t)l;
            fastSym[lo + j] = symbols[k];
          }
        }
      }
      maxcode[l] = counts[l - 1] ? code - 1 : -1;
      if (code > (1 << l)) return false;  // over-subscribed
      code <<= 1;
    }
    maxcode[17] = 0x7FFFFFFF;
    ok = true;
    return true;
  }
};

struct Bits {
  In in;
  uint8_t buf[2048];
  int len = 0, pos = 0;
  uint32_t acc = 0;  // MSB-aligned
  int cnt = 0;
  bool marker = false;  // entropy data ended at a marker; feed zeros
  uint8_t markerCode = 0;
  bool eof = false;

  int byte() {
    if (pos >= len) {
      len = in.read(in.ctx, buf, sizeof(buf));
      pos = 0;
      if (len <= 0) {
        eof = true;
        return -1;
      }
    }
    return buf[pos++];
  }

  void fill() {
    while (cnt <= 24) {
      int b = 0;
      if (!marker) {
        b = byte();
        if (b < 0) {
          b = 0;
          marker = true;
        } else if (b == 0xFF) {
          int b2 = byte();
          while (b2 == 0xFF) b2 = byte();  // fill bytes
          if (b2 != 0) {
            marker = true;
            markerCode = (uint8_t)(b2 < 0 ? 0 : b2);
            b = 0;
          }
        }
      }
      acc |= (uint32_t)b << (24 - cnt);
      cnt += 8;
    }
  }
  int bit1() {
    if (cnt < 1) fill();
    const int v = (int)(acc >> 31);
    acc <<= 1;
    cnt--;
    return v;
  }
  int bits(int n) {
    if (n == 0) return 0;
    if (cnt < n) fill();
    const int v = (int)(acc >> (32 - n));
    acc <<= n;
    cnt -= n;
    return v;
  }
  int extend(int n) {
    const int v = bits(n);
    return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
  }
  int decode(const Huff& h) {
    if (cnt < 16) fill();
    const int peek = (int)(acc >> 23);
    const int l = h.fastLen[peek];
    if (l) {
      acc <<= l;
      cnt -= l;
      return h.fastSym[peek];
    }
    for (int len2 = 10; len2 <= 16; len2++) {
      const int code = (int)(acc >> (32 - len2));
      if (code <= h.maxcode[len2]) {
        acc <<= len2;
        cnt -= len2;
        const int idx = h.valptr[len2] + code - h.mincode[len2];
        return idx >= 0 && idx < 256 ? h.vals[idx] : 0;
      }
    }
    return -1;  // corrupt
  }
  // Between scans and at restarts: drop the bit buffer and pick up whole bytes.
  void reset() {
    acc = 0;
    cnt = 0;
    marker = false;
  }
};

struct Comp {
  int id, h, v, tq;
  int td = 0, ta = 0;
  int dcPred = 0;
};

struct Dec {
  Bits br;
  uint16_t q[4][64] = {};
  Huff dc[4], ac[4];
  Comp comp[4];
  int ncomp = 0, W = 0, H = 0, hmax = 1, vmax = 1, mcux = 0, mcuy = 0;
  int restart = 0;
  int yIdx = 0;          // which component is luma (the first)
  int bw = 0, bh = 0;    // luma's padded block grid
  int K = 8;             // coefficients kept per side
  int16_t* coef = nullptr;   // bw*bh*K*K
  uint64_t* nz = nullptr;    // bw*bh nonzero masks, natural order
  int eobrun = 0;
  int pending = -1;      // a marker the entropy decoder ran into, for the marker loop
  int8_t keepIdx[64];    // natural index -> slot in the KxK corner, or -1

  ~Dec() {
    free(coef);
    free(nz);
  }

  int u16() {
    const int a = br.byte(), b = br.byte();
    if (a < 0 || b < 0) return -1;
    return (a << 8) | b;
  }
  bool skip(int n) {
    for (int i = 0; i < n; i++)
      if (br.byte() < 0) return false;
    return true;
  }

  // Coefficient access for luma block (bx, by), natural index z.
  bool isNz(int blk, int z) const { return (nz[blk] >> z) & 1; }
  void setVal(int blk, int z, int v) {
    if (v) nz[blk] |= (uint64_t)1 << z;
    const int s = keepIdx[z];
    if (s >= 0) coef[(size_t)blk * K * K + s] = (int16_t)v;
  }
  int getVal(int blk, int z) const {
    const int s = keepIdx[z];
    return s >= 0 ? coef[(size_t)blk * K * K + s] : 0;
  }
  void refine(int blk, int z, int p1, int m1) {
    // a correction bit for an already-nonzero coefficient
    if (br.bit1()) {
      const int s = keepIdx[z];
      if (s >= 0) {
        int16_t& c = coef[(size_t)blk * K * K + s];
        if ((c & p1) == 0) c = (int16_t)(c + (c >= 0 ? p1 : m1));
      }
    }
  }
};

}  // namespace detail

// `boxW` x `boxH` is the box the picture will be fitted into, keeping its
// shape: the decoder picks the largest IDCT reduction that stays at least as
// big as that fit, so nothing the panel could show is thrown away.
// `budget` caps the coefficient memory; past it, false (try another route).
inline bool decodeGray(In in, int boxW, int boxH, uint32_t budget, SizeFn sizeCb, RowFn rowCb,
                       void* uctx) {
  using namespace detail;
  Dec* dp = (Dec*)calloc(1, sizeof(Dec));
  if (!dp) return false;
  new (dp) Dec();
  Dec& d = *dp;
  struct Free {
    Dec* p;
    ~Free() {
      p->~Dec();
      free(p);
    }
  } guard{dp};
  d.br.in = in;

  if (d.br.byte() != 0xFF || d.br.byte() != 0xD8) return false;
  bool frame = false, done = false;
  while (!done) {
    // the next marker: one the last scan already ran into, or the next 0xFF xx
    int m;
    if (d.pending >= 0) {
      m = d.pending;
      d.pending = -1;
    } else {
      int b = d.br.byte();
      while (b >= 0 && b != 0xFF) b = d.br.byte();
      while (b == 0xFF) b = d.br.byte();
      if (b < 0) break;
      m = b;
    }
    if (m == 0) continue;                        // a stuffed byte, not a marker
    if (m == 0xD9) break;                        // EOI
    if (m >= 0xD0 && m <= 0xD7) continue;        // stray RST
    const int len = d.u16();
    if (len < 2) return false;
    if (m == 0xDB) {  // DQT
      int left = len - 2;
      while (left > 0) {
        const int pq = d.br.byte();
        const int prec = pq >> 4, tq = pq & 3;
        left--;
        for (int i = 0; i < 64; i++) {
          const int v = prec ? d.u16() : d.br.byte();
          d.q[tq][kZigzag[i]] = (uint16_t)(v < 0 ? 0 : v);
        }
        left -= prec ? 128 : 64;
      }
    } else if (m == 0xC4) {  // DHT
      int left = len - 2;
      while (left > 0) {
        const int tc = d.br.byte();
        uint8_t counts[16];
        int n = 0;
        for (int i = 0; i < 16; i++) {
          counts[i] = (uint8_t)d.br.byte();
          n += counts[i];
        }
        if (n > 256) return false;
        uint8_t sym[256];
        for (int i = 0; i < n; i++) sym[i] = (uint8_t)d.br.byte();
        Huff& h = (tc >> 4) ? d.ac[tc & 3] : d.dc[tc & 3];
        if (!h.build(counts, sym, n)) return false;
        left -= 17 + n;
      }
    } else if (m == 0xDD) {  // DRI
      d.restart = d.u16();
    } else if (m == 0xC2) {  // SOF2: progressive, huffman
      if (d.br.byte() != 8) return false;
      d.H = d.u16();
      d.W = d.u16();
      d.ncomp = d.br.byte();
      if (d.W <= 0 || d.H <= 0 || d.ncomp < 1 || d.ncomp > 4) return false;
      for (int i = 0; i < d.ncomp; i++) {
        d.comp[i].id = d.br.byte();
        const int hv = d.br.byte();
        d.comp[i].h = hv >> 4;
        d.comp[i].v = hv & 15;
        d.comp[i].tq = d.br.byte() & 3;
        if (d.comp[i].h < 1 || d.comp[i].v < 1) return false;
        if (d.comp[i].h > d.hmax) d.hmax = d.comp[i].h;
        if (d.comp[i].v > d.vmax) d.vmax = d.comp[i].v;
      }
      d.mcux = (d.W + 8 * d.hmax - 1) / (8 * d.hmax);
      d.mcuy = (d.H + 8 * d.vmax - 1) / (8 * d.vmax);
      d.bw = d.mcux * d.comp[0].h;
      d.bh = d.mcuy * d.comp[0].v;
      // The reduction: 8 = full size, 4 = half, 2 = quarter, 1 = an eighth.
      int fitW, fitH;
      if ((int64_t)d.W * boxH >= (int64_t)d.H * boxW) {
        fitW = boxW;
        fitH = (int)((int64_t)d.H * boxW / d.W);
      } else {
        fitH = boxH;
        fitW = (int)((int64_t)d.W * boxH / d.H);
      }
      int N = 8;
      while (N > 1 && (int64_t)d.W * (N / 2) / 8 >= fitW && (int64_t)d.H * (N / 2) / 8 >= fitH) N /= 2;
      const uint32_t blocks = (uint32_t)d.bw * (uint32_t)d.bh;
      while (N > 1 && blocks * (uint32_t)(N * N * 2 + 8) > budget) N /= 2;
      if (blocks * (uint32_t)(N * N * 2 + 8) > budget) return false;
      d.K = N;
      for (int z = 0; z < 64; z++) {
        const int u = z & 7, v = z >> 3;
        d.keepIdx[z] = (int8_t)((u < N && v < N) ? v * N + u : -1);
      }
      d.coef = (int16_t*)calloc((size_t)blocks * N * N, sizeof(int16_t));
      d.nz = (uint64_t*)calloc(blocks, sizeof(uint64_t));
      if (!d.coef || !d.nz) return false;
      frame = true;
    } else if (m == 0xC0 || m == 0xC1 || m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) {
      return false;  // baseline or a coding this file does not do
    } else if (m == 0xDA) {  // SOS
      if (!frame) return false;
      const int ns = d.br.byte();
      if (ns < 1 || ns > 4) return false;
      int sc[4];
      for (int i = 0; i < ns; i++) {
        const int cid = d.br.byte(), t = d.br.byte();
        sc[i] = -1;
        for (int c = 0; c < d.ncomp; c++)
          if (d.comp[c].id == cid) sc[i] = c;
        if (sc[i] < 0) return false;
        d.comp[sc[i]].td = t >> 4;
        d.comp[sc[i]].ta = t & 15;
      }
      const int ss = d.br.byte(), se = d.br.byte(), a = d.br.byte();
      const int ah = a >> 4, al = a & 15;
      if (ss < 0 || se > 63 || ss > se) return false;
      d.br.reset();
      for (int c = 0; c < d.ncomp; c++) d.comp[c].dcPred = 0;
      d.eobrun = 0;

      if (ns == 1 && sc[0] != d.yIdx) {
        // A chroma-only scan: not decoded at all. Walk the bytes to the next
        // marker that is not a restart and hand it to the marker loop.
        for (;;) {
          int x = d.br.byte();
          if (x < 0) return false;
          if (x != 0xFF) continue;
          int y = d.br.byte();
          while (y == 0xFF) y = d.br.byte();
          if (y < 0) return false;
          if (y == 0 || (y >= 0xD0 && y <= 0xD7)) continue;
          d.pending = y;
          break;
        }
        continue;
      }

      const int p1 = 1 << al, m1 = -(1 << al);
      int mcuCount = 0;
      auto doRestart = [&]() {
        // Past the RSTn: the bit reader may already have stopped at it, or may
        // still be short of it with padding bits left over.
        if (!d.br.marker) {
          for (;;) {
            int x = d.br.byte();
            if (x < 0) break;
            if (x != 0xFF) continue;
            int y = d.br.byte();
            while (y == 0xFF) y = d.br.byte();
            if (y != 0) break;
          }
        }
        d.br.reset();
        for (int c = 0; c < d.ncomp; c++) d.comp[c].dcPred = 0;
        d.eobrun = 0;
      };
      auto dcBlock = [&](int c, int blk) {
        Comp& cp = d.comp[c];
        if (ah == 0) {
          const int t = d.br.decode(d.dc[cp.td]);
          if (t < 0 || t > 16) return false;
          const int diff = t ? d.br.extend(t) : 0;
          cp.dcPred += diff;
          if (blk >= 0) d.setVal(blk, 0, cp.dcPred * (1 << al));
        } else {
          const int bitv = d.br.bit1();
          if (blk >= 0 && bitv) d.setVal(blk, 0, d.getVal(blk, 0) | p1);
        }
        return true;
      };
      auto acFirst = [&](int blk) {
        if (d.eobrun > 0) {
          d.eobrun--;
          return true;
        }
        const Huff& h = d.ac[d.comp[d.yIdx].ta];
        for (int k = ss; k <= se; k++) {
          const int rs = d.br.decode(h);
          if (rs < 0) return false;
          const int r = rs >> 4, s = rs & 15;
          if (s == 0) {
            if (r < 15) {
              d.eobrun = (1 << r) - 1;
              if (r) d.eobrun += d.br.bits(r);
              break;
            }
            k += 15;
          } else {
            k += r;
            if (k > 63) return false;
            d.setVal(blk, kZigzag[k], d.br.extend(s) * (1 << al));
          }
        }
        return true;
      };
      auto acRefine = [&](int blk) {
        int k = ss;
        if (d.eobrun <= 0) {
          const Huff& h = d.ac[d.comp[d.yIdx].ta];
          for (; k <= se; k++) {
            const int rs = d.br.decode(h);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15;
            if (s) {
              s = d.br.bit1() ? p1 : m1;
            } else if (r != 15) {
              d.eobrun = 1 << r;
              if (r) d.eobrun += d.br.bits(r);
              break;
            }
            while (k <= se) {
              const int z = kZigzag[k];
              if (d.isNz(blk, z)) {
                d.refine(blk, z, p1, m1);
              } else {
                if (r == 0) break;
                r--;
              }
              k++;
            }
            if (s && k <= se) d.setVal(blk, kZigzag[k], s);
          }
        }
        if (d.eobrun > 0) {
          for (; k <= se; k++) {
            const int z = kZigzag[k];
            if (d.isNz(blk, z)) d.refine(blk, z, p1, m1);
          }
          d.eobrun--;
        }
        return true;
      };

      if (ns == 1) {
        // Non-interleaved: the component's own block grid, unpadded.
        const int c = sc[0];
        const Comp& cp = d.comp[c];
        const int cw = (d.W * cp.h + d.hmax - 1) / d.hmax, ch = (d.H * cp.v + d.vmax - 1) / d.vmax;
        const int nbx = (cw + 7) / 8, nby = (ch + 7) / 8;
        const bool luma = c == d.yIdx;
        for (int by = 0; by < nby; by++)
          for (int bx = 0; bx < nbx; bx++) {
            if (d.restart && mcuCount && mcuCount % d.restart == 0) doRestart();
            mcuCount++;
            const int blk = luma ? by * d.bw + bx : -1;
            bool ok;
            if (ss == 0)
              ok = dcBlock(c, blk);
            else
              ok = luma && (ah == 0 ? acFirst(blk) : acRefine(blk));
            if (!ok) return false;
          }
      } else {
        if (ss != 0) return false;  // interleaved scans are DC only
        for (int my = 0; my < d.mcuy; my++)
          for (int mx = 0; mx < d.mcux; mx++) {
            if (d.restart && mcuCount && mcuCount % d.restart == 0) doRestart();
            mcuCount++;
            for (int i = 0; i < ns; i++) {
              const int c = sc[i];
              const Comp& cp = d.comp[c];
              for (int v = 0; v < cp.v; v++)
                for (int h = 0; h < cp.h; h++) {
                  const int blk =
                      c == d.yIdx ? (my * cp.v + v) * d.bw + (mx * cp.h + h) : -1;
                  if (!dcBlock(c, blk)) return false;
                }
            }
          }
      }
      // Back to byte level: the marker the bit reader stopped at is the next
      // segment's, and goes to the marker loop.
      if (d.br.marker && d.br.markerCode) d.pending = d.br.markerCode;
      d.br.reset();
    } else {
      if (!d.skip(len - 2)) return false;  // APPn, COM, anything else
    }
  }
  if (!frame) return false;

  // The reduced IDCT: the KxK corner of each block, sampled at the centres of
  // a KxK grid of cells. C(0) = 1/sqrt2.
  const int N = d.K;
  float cosT[8][8];
  for (int i = 0; i < N; i++)
    for (int u = 0; u < N; u++) {
      const float t = (i + 0.5f) * 8.0f / N - 0.5f;
      cosT[i][u] = cosf((2.0f * t + 1.0f) * u * 3.14159265f / 16.0f) * (u ? 1.0f : 0.70710678f);
    }
  const int outW = (int)(((int64_t)d.W * N + 7) / 8), outH = (int)(((int64_t)d.H * N + 7) / 8);
  if (!sizeCb(uctx, outW, outH)) return false;
  uint8_t* rows = (uint8_t*)malloc((size_t)outW * N);
  if (!rows) return false;
  const uint16_t* q = d.q[d.comp[d.yIdx].tq];
  float F[64], tmp[64];
  for (int by = 0; by * N < outH; by++) {
    memset(rows, 128, (size_t)outW * N);
    for (int bx = 0; bx * N < outW; bx++) {
      const int blk = by * d.bw + bx;
      const int16_t* cf = d.coef + (size_t)blk * N * N;
      for (int v = 0; v < N; v++)
        for (int u = 0; u < N; u++) F[v * N + u] = (float)cf[v * N + u] * q[v * 8 + u];
      for (int v = 0; v < N; v++)  // rows of coefficients -> columns of samples
        for (int i = 0; i < N; i++) {
          float s = 0;
          for (int u = 0; u < N; u++) s += F[v * N + u] * cosT[i][u];
          tmp[v * N + i] = s;
        }
      for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
          float s = 0;
          for (int v = 0; v < N; v++) s += tmp[v * N + i] * cosT[j][v];
          // 1/4 for the 2-D transform; with C(0) = 1/sqrt2 the DC term comes
          // out as the block's mean, at any N, since this samples the same
          // continuous function at fewer points.
          int val = (int)(s * 0.25f + 128.5f);
          const int x = bx * N + i;
          if (x < outW) rows[(size_t)j * outW + x] = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);
        }
    }
    for (int j = 0; j < N && by * N + j < outH; j++) rowCb(uctx, by * N + j, rows + (size_t)j * outW, outW);
  }
  free(rows);
  return true;
}

}  // namespace ejprog
