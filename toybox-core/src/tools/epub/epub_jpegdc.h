// Progressive JPEG, thumbnail-sized: the DC-scan extractor.
//
// tjpgd decodes baseline JPEG only, and full progressive decode wants
// whole-image coefficient memory (about 8 MB for a typical cover) that this
// device does not have. But a progressive file's FIRST scan is the DC
// coefficients -- one value per 8x8 block, streamed in MCU order -- and a
// DC value IS the block's average. Decoding just that scan yields the image
// at 1/8 scale with no per-image memory at all, and 1/8 of any real cover
// is still bigger than the 96x160 thumbnail it is destined for.
//
// Only what a first DC scan can contain is implemented: baseline huffman DC
// tables, interleaved or single-component scans, restart markers, the
// successive-approximation shift. Anything unexpected returns false and the
// caller falls back to the drawn plate.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace ejdc {

struct In {
  void* ctx;
  int (*read)(void* ctx, uint8_t* dst, int n);  // sequential; <=0 means end
};

// size() then row() per 1/8-scale luma row, top to bottom.
using SizeFn = bool (*)(void* uctx, int w, int h);
using RowFn = void (*)(void* uctx, int y, const uint8_t* gray, int w);

namespace detail {

struct Huff {
  // canonical huffman, decoded bit by bit: DC tables are tiny (<=16 symbols)
  uint8_t counts[17] = {};
  uint8_t symbols[16] = {};
  bool ok = false;
};

struct Reader {
  In in;
  uint8_t buf[256];
  int len = 0, pos = 0;
  bool atMarker = false;  // hit an 0xFF <marker> in entropy data
  uint8_t marker = 0;

  int byte() {
    if (pos >= len) {
      len = in.read(in.ctx, buf, sizeof(buf));
      pos = 0;
      if (len <= 0) return -1;
    }
    return buf[pos++];
  }

  uint32_t bitBuf = 0;
  int bitCnt = 0;

  void bitAlign() {
    bitBuf = 0;
    bitCnt = 0;
  }

  int nextBit() {
    if (bitCnt == 0) {
      int b = byte();
      if (b < 0) return -1;
      if (b == 0xFF) {
        const int b2 = byte();
        if (b2 == 0x00) {
          // stuffed FF: data byte
        } else if (b2 >= 0xD0 && b2 <= 0xD7) {
          atMarker = true;
          marker = (uint8_t)b2;
          return -2;  // restart marker reached mid-read
        } else {
          atMarker = true;
          marker = (uint8_t)(b2 < 0 ? 0 : b2);
          return -2;  // end of scan (or corrupt)
        }
      }
      bitBuf = (uint32_t)b;
      bitCnt = 8;
    }
    bitCnt--;
    return (int)((bitBuf >> bitCnt) & 1);
  }

  int decode(const Huff& h) {
    int code = 0;
    int idx = 0;
    for (int l = 1; l <= 16; l++) {
      const int b = nextBit();
      if (b < 0) return -1000 + b;
      code = (code << 1) | b;
      int first = 0;  // first code of this length is built canonically
      // canonical walk: count codes shorter than l
      // (recomputing per step keeps the table tiny; DC tables are 12 entries)
      int firstCode = 0, index = 0;
      for (int k = 1; k < l; k++) {
        firstCode = (firstCode + h.counts[k]) << 1;
        index += h.counts[k];
      }
      (void)first;
      if (h.counts[l] && code - firstCode < h.counts[l]) {
        const int si = index + (code - firstCode);
        return si < 16 ? h.symbols[si] : -1;
      }
    }
    return -1;
  }

  // receive+extend: t extra bits as a signed DC difference
  int receiveExtend(int t) {
    int v = 0;
    for (int i = 0; i < t; i++) {
      const int b = nextBit();
      if (b < 0) return 0;  // truncated: keep going with what we have
      v = (v << 1) | b;
    }
    if (t && v < (1 << (t - 1))) v += 1 - (1 << t);  // negative branch
    return v;
  }
};

}  // namespace detail

inline bool decodeGray(In in, SizeFn sizeCb, RowFn rowCb, void* uctx) {
  using namespace detail;
  Reader r;
  r.in = in;

  // SOI
  if (r.byte() != 0xFF || r.byte() != 0xD8) return false;

  Huff dcTab[4];
  uint16_t q0[4] = {8, 8, 8, 8};  // DC quantiser per table; defaulted defensively
  int W = 0, H = 0;
  int compN = 0;
  struct Comp {
    uint8_t id = 0, hs = 1, vs = 1, tq = 0, td = 0;
  } comp[3];
  int restartInterval = 0;
  bool progressive = false;

  // --- markers until the first scan ---------------------------------------
  while (true) {
    int b = r.byte();
    if (b < 0) return false;
    if (b != 0xFF) continue;  // padding/garbage tolerance
    int m = r.byte();
    while (m == 0xFF) m = r.byte();
    if (m < 0) return false;
    if (m == 0xD8) continue;               // stray SOI
    if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;  // no payload
    // every other marker carries a length
    const int lh = r.byte(), ll = r.byte();
    if (lh < 0 || ll < 0) return false;
    int seg = ((lh << 8) | ll) - 2;
    if (seg < 0) return false;

    if (m == 0xDB) {  // DQT: only the DC (first) entry of each table matters
      while (seg > 0) {
        const int pq_tq = r.byte();
        seg--;
        if (pq_tq < 0) return false;
        const int pq = pq_tq >> 4, tq = pq_tq & 15;
        const int n = pq ? 128 : 64;
        for (int i = 0; i < n && seg > 0; i++, seg--) {
          const int v = r.byte();
          if (v < 0) return false;
          if (i == 0 && tq < 4) q0[tq] = (uint16_t)v;
          if (pq && i == 0) {
            const int v2 = r.byte();
            seg--;
            if (v2 < 0) return false;
            if (tq < 4) q0[tq] = (uint16_t)((v << 8) | v2);
          }
        }
      }
      continue;
    }
    if (m == 0xC4) {  // DHT: keep DC tables (class 0)
      while (seg > 0) {
        const int tc_th = r.byte();
        seg--;
        if (tc_th < 0) return false;
        const int tc = tc_th >> 4, th = tc_th & 15;
        uint8_t counts[17] = {};
        int total = 0;
        for (int i = 1; i <= 16; i++) {
          const int v = r.byte();
          seg--;
          if (v < 0) return false;
          counts[i] = (uint8_t)v;
          total += v;
        }
        for (int i = 0; i < total; i++) {
          const int v = r.byte();
          seg--;
          if (v < 0) return false;
          if (tc == 0 && th < 4 && i < 16) dcTab[th].symbols[i] = (uint8_t)v;
        }
        if (tc == 0 && th < 4 && total <= 16) {
          memcpy(dcTab[th].counts, counts, sizeof(counts));
          dcTab[th].ok = true;
        }
      }
      continue;
    }
    if (m == 0xC2 || m == 0xC0 || m == 0xC1) {  // SOF: progressive or baseline
      progressive = (m == 0xC2);
      const int prec = r.byte();
      H = (r.byte() << 8) | r.byte();
      W = (r.byte() << 8) | r.byte();
      compN = r.byte();
      if (prec != 8 || compN < 1 || compN > 3 || W < 8 || H < 8) return false;
      for (int i = 0; i < compN; i++) {
        comp[i].id = (uint8_t)r.byte();
        const int hv = r.byte();
        comp[i].hs = (uint8_t)(hv >> 4);
        comp[i].vs = (uint8_t)(hv & 15);
        comp[i].tq = (uint8_t)r.byte();
        if (comp[i].hs < 1 || comp[i].hs > 2 || comp[i].vs < 1 || comp[i].vs > 2) return false;
      }
      continue;
    }
    if (m == 0xDD) {  // DRI
      restartInterval = (r.byte() << 8) | r.byte();
      continue;
    }
    if (m == 0xDA) {  // SOS: is it a DC scan we can stream?
      const int ns = r.byte();
      if (ns < 1 || ns > 3) return false;
      uint8_t scanComp[3];
      for (int i = 0; i < ns; i++) {
        const uint8_t cs = (uint8_t)r.byte();
        const int tdta = r.byte();
        scanComp[i] = 0xFF;
        for (int k = 0; k < compN; k++)
          if (comp[k].id == cs) {
            comp[k].td = (uint8_t)(tdta >> 4);
            scanComp[i] = (uint8_t)k;
          }
        if (scanComp[i] == 0xFF) return false;
      }
      const int ss = r.byte();
      const int se = r.byte();
      const int ahal = r.byte();
      const int al = ahal & 15;
      if (!progressive) {
        // Baseline files belong to tjpgd; this decoder only backs it up.
        return false;
      }
      if (ss != 0 || se != 0 || (ahal >> 4) != 0) return false;  // not the first DC scan
      if (ns != compN && ns != 1) return false;

      // --- the scan itself ------------------------------------------------
      // Geometry: interleaved scans walk MCUs of hmax x vmax blocks; a
      // single-component DC scan walks that component's blocks directly.
      int hmax = 1, vmax = 1;
      for (int i = 0; i < compN; i++) {
        if (comp[i].hs > hmax) hmax = comp[i].hs;
        if (comp[i].vs > vmax) vmax = comp[i].vs;
      }
      const int lumaW = (W + 7) / 8, lumaH = (H + 7) / 8;  // Y blocks = output pixels
      // The luma DC plane, one byte per block: at most ~64 KB for a 4000-px
      // cover, allocated for the scan and freed by the caller pattern below.
      uint8_t* plane = (uint8_t*)malloc((size_t)lumaW * lumaH);
      if (!plane) return false;
      memset(plane, 255, (size_t)lumaW * lumaH);
      if (!sizeCb(uctx, lumaW, lumaH)) {
        free(plane);
        return false;
      }

      const int yIdx = 0;  // component 0 is Y in every real file
      int pred[3] = {0, 0, 0};
      const uint16_t yq = q0[comp[yIdx].tq];
      bool okScan = true;

      // After each restart interval the entropy stream byte-aligns and an
      // RSTn marker sits in the bytes; consume it or the next decode reads
      // marker bytes as huffman codes.
      auto consumeRst = [&]() -> bool {
        r.bitAlign();
        r.atMarker = false;
        int a = r.byte();
        while (a >= 0 && a != 0xFF) a = r.byte();
        if (a < 0) return false;
        int m2 = r.byte();
        while (m2 == 0xFF) m2 = r.byte();
        return m2 >= 0xD0 && m2 <= 0xD7;
      };

      auto emit = [&](int bx, int by, int dc) {
        if (bx >= lumaW || by >= lumaH) return;
        // dequantised DC / 8 + 128 is the block average
        int v = ((dc << al) * (int)yq) / 8 + 128;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        plane[(size_t)by * lumaW + bx] = (uint8_t)v;
      };

      if (ns == 1) {
        const int ci = scanComp[0];
        // blocks of this component, raster order
        const int cw = (W * comp[ci].hs + 8 * hmax - 1) / (8 * hmax);
        const int ch = (H * comp[ci].vs + 8 * vmax - 1) / (8 * vmax);
        int count = 0;
        for (int by = 0; by < ch && okScan; by++)
          for (int bx = 0; bx < cw && okScan; bx++) {
            const int t = r.decode(dcTab[comp[ci].td]);
            if (t < 0 || t > 15) {
              okScan = r.atMarker;  // clean end-of-scan is fine
              goto scanDone1;
            }
            const int diff = r.receiveExtend(t);
            pred[0] += diff;
            if (ci == yIdx) emit(bx, by, pred[0]);
            if (restartInterval && ++count == restartInterval) {
              count = 0;
              pred[0] = 0;
              if (!consumeRst()) {
                okScan = false;
                goto scanDone1;
              }
            }
          }
      scanDone1:;
      } else {
        const int mcuW = (W + 8 * hmax - 1) / (8 * hmax);
        const int mcuH = (H + 8 * vmax - 1) / (8 * vmax);
        int count = 0;
        for (int my = 0; my < mcuH && okScan; my++) {
          for (int mx = 0; mx < mcuW && okScan; mx++) {
            for (int i = 0; i < ns && okScan; i++) {
              const int ci = scanComp[i];
              for (int b = 0; b < comp[ci].hs * comp[ci].vs; b++) {
                const int t = r.decode(dcTab[comp[ci].td]);
                if (t < 0 || t > 15) {
                  okScan = r.atMarker && my > 0;  // truncated late is tolerable
                  goto scanDone;
                }
                const int diff = r.receiveExtend(t);
                pred[ci] += diff;
                if (ci == yIdx) {
                  const int bx = mx * comp[ci].hs + (b % comp[ci].hs);
                  const int by = my * comp[ci].vs + (b / comp[ci].hs);
                  emit(bx, by, pred[ci]);
                }
              }
            }
            if (restartInterval && ++count == restartInterval &&
                !(my == mcuH - 1 && mx == mcuW - 1)) {
              count = 0;
              pred[0] = pred[1] = pred[2] = 0;
              if (!consumeRst()) {
                okScan = false;
                goto scanDone;
              }
            }
          }
        }
      scanDone:;
      }

      if (okScan) {
        for (int y = 0; y < lumaH; y++) rowCb(uctx, y, plane + (size_t)y * lumaW, lumaW);
      }
      free(plane);
      return okScan;
    }
    // any other marker: skip its payload
    while (seg-- > 0)
      if (r.byte() < 0) return false;
  }
}

}  // namespace ejdc
