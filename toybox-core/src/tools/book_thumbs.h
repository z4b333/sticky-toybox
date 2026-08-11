// Cover art, at two sizes, for two jobs, in two places.
//
// A book keeps a full panel-sized cover (480x800, one bit, dithered) for the
// loading screen it opens behind, and a small one (96x160, thresholded) for
// the hub's recently-read strip. Both are made once, on a book's first open,
// and both come out of a single pass over the decoded image.
//
// The full-size ones live ON THE CARD, beside the books they belong to: they
// are 48 KB each, the card has gigabytes where internal flash has four
// megabytes, and naming them from the book's path means moving the card to
// another Sticky brings the art along with the reading positions. The strip
// thumbnails stay in internal flash, and have to: the hub draws them with no
// card session open, and claiming the SD bus halfway through rendering a
// screen would re-initialise the panel underneath it.
//
// The pass is streamed, and that is the whole trick. Averaging every source
// pixel into its target cell needs a sum and a count PER CELL: at 96x160 that
// is 61 KB, and at panel size it would be 1.5 MB, which is why the loading
// screen used to be a 96x160 image blown up five times into blocks. But both
// image decoders hand their rows out in order, so only a BAND of output rows
// is ever live -- twenty is enough for the tallest JPEG block -- and each row
// can be dithered and written the moment the source has passed it by. The
// cost is constant and small whatever the output size.
//
// Two decisions worth keeping, both settled by looking at real covers:
//   - The full-size version is dithered and NOT contrast-stretched. Dithering
//     carries tone by itself, and the stretch that helps at 96 px crushes the
//     darks out of a face at 480.
//   - The small version is stretched and thresholded, NOT dithered. At 96 px
//     error diffusion turns mid-grey into speckle that swallows titles.
// And one that runs through everything here: shrinking a picture AVERAGES,
// never samples. The lock screen taught that once already.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiny_fs.h"
#include "tools_ui.h"

namespace bthumb {

inline constexpr int W = 96, H = 160;                 // the strip thumbnail
inline constexpr int BYTES = W * H / 8;               // 1,920
inline constexpr int BIG_W = 480, BIG_H = 800;        // the loading screen
inline constexpr int BIG_BYTES = BIG_W * BIG_H / 8;   // 48,000
inline constexpr int SCALE = BIG_W / W;               // 5, both ways

inline uint32_t key(const char* file) {
  uint32_t h = 2166136261u;
  for (const char* p = file; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

inline void path(const char* file, char* out, int cap) {
  snprintf(out, (size_t)cap, "/th_%08lx", (unsigned long)key(file));
}
// On the card, in a folder of our own. Hidden, so the phone's file list and
// the readers' book lists both walk straight past it.
inline void bigPath(const char* file, char* out, int cap) {
  snprintf(out, (size_t)cap, "/.toybox/covers/%08lx.tbc", (unsigned long)key(file));
}
// Where the full-size covers used to live, before they moved to the card.
// Swept as books are reopened rather than in a migration nobody would test.
inline void staleFlashPath(const char* file, char* out, int cap) {
  snprintf(out, (size_t)cap, "/cv_%08lx", (unsigned long)key(file));
}

inline bool have(const char* file) {
  char p[20];
  path(file, p, sizeof(p));
  return tfs::size(p) == (size_t)BYTES;
}
inline bool haveBig(ToolsHost& h, const char* file) {
  char p[48];
  bigPath(file, p, sizeof(p));
  uint8_t probe[4];
  return h.sdReadWhole(p, probe, sizeof(probe)) == (int)sizeof(probe);
}

// Books whose cover would not decode get a marker instead of a retry on
// every open (papyrix's .thumb.failed idea, kept).
inline void markFailed(const char* file) {
  char p[24];
  path(file, p, sizeof(p));
  strncat(p, "f", sizeof(p) - strlen(p) - 1);
  tfs::write(p, "x", 1);
}
inline bool failed(const char* file) {
  char p[24];
  path(file, p, sizeof(p));
  strncat(p, "f", sizeof(p) - strlen(p) - 1);
  return tfs::exists(p);
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

// --- the streaming builder ----------------------------------------------------
// Feed it the decoded image in whatever order the decoder produces, as long
// as blocks arrive top to bottom: rows for PNG and the progressive DC pass,
// 8- or 16-pixel-tall bands for baseline JPEG.
class Builder {
 public:
  // Twenty output rows: one JPEG block row is at most sixteen source rows,
  // and the fit never upscales, so it can never span more than that.
  static constexpr int BAND = 20;

  ~Builder() { freeAll(); }

  // `maxUp` is how much the picture may be enlarged to fill the panel. A
  // decoder that hands out whole rows can be enlarged freely, because one
  // source row is spread and then done with; one that hands out blocks
  // (baseline JPEG) must not be, because a sixteen-row block would then span
  // more output rows than the band holds. Real covers are bigger than the
  // panel anyway -- it is the progressive path, which only ever yields the
  // image at an eighth, that needs this.
  bool begin(ToolsHost& h, const char* file, int srcW, int srcH, int maxUp = 1) {
    freeAll();
    _host = &h;
    if (srcW < 1 || srcH < 1) return false;
    _srcW = srcW;
    _srcH = srcH;
    if ((int64_t)srcW * BIG_H >= (int64_t)srcH * BIG_W) {
      _outW = BIG_W;
      _outH = (int)(((int64_t)srcH * BIG_W + srcW / 2) / srcW);
    } else {
      _outH = BIG_H;
      _outW = (int)(((int64_t)srcW * BIG_H + srcH / 2) / srcH);
    }
    if (_outW > srcW * maxUp || _outH > srcH * maxUp) {
      _outW = srcW * maxUp;
      _outH = srcH * maxUp;
      if (_outW > BIG_W) _outW = BIG_W;
      if (_outH > BIG_H) _outH = BIG_H;
    }
    if (_outW < 1) _outW = 1;
    if (_outH < 1) _outH = 1;
    _up = _outH > srcH;
    _xOff = (BIG_W - _outW) / 2;
    _yOff = (BIG_H - _outH) / 2;

    _sum = (uint16_t*)calloc((size_t)BAND * BIG_W, sizeof(uint16_t));
    _cnt = (uint8_t*)calloc((size_t)BAND * BIG_W, 1);
    _err = (int16_t*)calloc(2 * (BIG_W + 2), sizeof(int16_t));
    _small = (uint8_t*)malloc((size_t)W * H);
    if (!_sum || !_cnt || !_err || !_small) {
      freeAll();
      return false;
    }
    memset(_small, 255, (size_t)W * H);
    bigPath(file, _bigPath, sizeof(_bigPath));
    path(file, _smallPath, sizeof(_smallPath));
    // Sweep this book's old flash cover, from before they moved to the card.
    // Done here rather than as a migration pass, so it costs one remove on
    // the open that was rebuilding the cover anyway.
    {
      char stale[20];
      staleFlashPath(file, stale, sizeof(stale));
      if (tfs::exists(stale)) tfs::remove(stale);
      if (tfs::exists("/cv_index")) tfs::remove("/cv_index");
    }
    // The card holds the picture, so this only works while something already
    // has the bus -- which, on a book's first open, something does.
    if (!_host->sdStreamOpen(_bigPath)) {
      freeAll();
      return false;
    }
    _nextOut = 0;
    _smallRow = 0;
    memset(_smallSum, 0, sizeof(_smallSum));
    memset(_smallCnt, 0, sizeof(_smallCnt));
    _live = true;
    return true;
  }

  // A block of decoded grayscale, `w` wide and `h` tall, starting at source
  // pixel (x0, y0), row-major.
  void block(int x0, int y0, int w, int h, const uint8_t* gray) {
    if (!_live) return;
    // Every output row above this block's first is finished: blocks only ever
    // move down the image, so nothing can add to them again.
    const int firstOut = _up ? upLo(y0, _outH, _srcH, _yOff) : mapY(y0);
    while (_nextOut < firstOut && _nextOut < BIG_H) emitRow(_nextOut++);
    for (int r = 0; r < h; r++) {
      const int sy = y0 + r;
      if (sy >= _srcH) break;
      // Shrinking, a source row lands on one output row and is averaged in
      // with its neighbours. Growing, it is spread across the several output
      // rows it covers -- forward mapping alone would leave white gaps
      // between them.
      const int tFirst = _up ? upLo(sy, _outH, _srcH, _yOff) : mapY(sy);
      const int tLast = _up ? upHi(sy, _outH, _srcH, _yOff) : tFirst;
      const uint8_t* src = gray + (size_t)r * w;
      for (int ty = tFirst; ty <= tLast; ty++) {
        if (ty < _nextOut || ty >= _nextOut + BAND || ty >= BIG_H) continue;
        uint16_t* sum = _sum + (size_t)(ty % BAND) * BIG_W;
        uint8_t* cnt = _cnt + (size_t)(ty % BAND) * BIG_W;
        for (int i = 0; i < w; i++) {
          const int sx = x0 + i;
          if (sx >= _srcW) break;
          const int xFirst = _up ? upLo(sx, _outW, _srcW, _xOff)
                                 : _xOff + (int)((int64_t)sx * _outW / _srcW);
          const int xLast = _up ? upHi(sx, _outW, _srcW, _xOff) : xFirst;
          for (int tx = xFirst; tx <= xLast; tx++) {
            if (tx < 0 || tx >= BIG_W) continue;
            if (cnt[tx] < 255) {
              sum[tx] = (uint16_t)(sum[tx] + src[i]);
              cnt[tx]++;
            }
          }
        }
      }
    }
  }

  void row(int y, const uint8_t* gray, int w) { block(0, y, w, 1, gray); }

  // Writes both pictures and records the big one in the keep list.
  bool finish() {
    if (!_live) return false;
    while (_nextOut < BIG_H) emitRow(_nextOut++);
    const bool ok = _host->sdStreamClose(true);
    if (ok) saveSmall();
    _live = false;
    freeAll();
    return ok;
  }

  void abort() {
    if (!_live) return;
    _host->sdStreamClose(false);
    _live = false;
    freeAll();
  }

 private:
  int mapY(int sy) const {
    if (sy < 0) sy = 0;
    if (sy >= _srcH) sy = _srcH - 1;
    return _yOff + (int)((int64_t)sy * _outH / _srcH);
  }
  // The span of output rows (or columns) that source line `s` covers when the
  // picture is being enlarged: the inverse of the shrinking map.
  static int upLo(int s, int out, int src, int off) {
    return off + (int)(((int64_t)s * out + src - 1) / src);
  }
  static int upHi(int s, int out, int src, int off) {
    return off + (int)((((int64_t)(s + 1) * out + src - 1) / src)) - 1;
  }

  // One finished output row: averaged, dithered into the big picture, and
  // folded into the small one on the way past.
  void emitRow(int t) {
    uint8_t line[BIG_W];
    if (t >= _yOff && t < _yOff + _outH) {
      const uint16_t* sum = _sum + (size_t)(t % BAND) * BIG_W;
      const uint8_t* cnt = _cnt + (size_t)(t % BAND) * BIG_W;
      for (int x = 0; x < BIG_W; x++) line[x] = cnt[x] ? (uint8_t)(sum[x] / cnt[x]) : 255;
    } else {
      memset(line, 255, sizeof(line));  // the letterbox
    }

    // The small picture wants the grey, before any dithering.
    for (int x = 0; x < BIG_W; x++) {
      _smallSum[x / SCALE] += line[x];
      _smallCnt[x / SCALE]++;
    }
    if ((t % SCALE) == SCALE - 1 && _smallRow < H) {
      uint8_t* dst = _small + (size_t)_smallRow * W;
      for (int x = 0; x < W; x++) dst[x] = _smallCnt[x] ? (uint8_t)(_smallSum[x] / _smallCnt[x]) : 255;
      memset(_smallSum, 0, sizeof(_smallSum));
      memset(_smallCnt, 0, sizeof(_smallCnt));
      _smallRow++;
    }

    // Floyd-Steinberg, one row of carry. No contrast stretch: at this size
    // the dither keeps the tone, and stretching only crushes the darks.
    int16_t* cur = _err + (size_t)(t & 1) * (BIG_W + 2);
    int16_t* nxt = _err + (size_t)((t + 1) & 1) * (BIG_W + 2);
    memset(nxt, 0, sizeof(int16_t) * (BIG_W + 2));
    uint8_t packed[BIG_W / 8];
    memset(packed, 0xFF, sizeof(packed));
    for (int x = 0; x < BIG_W; x++) {
      const int v = line[x] + cur[x + 1];
      const int on = v >= 128 ? 255 : 0;
      if (!on) packed[x >> 3] &= ~(0x80 >> (x & 7));
      const int e = v - on;
      cur[x + 2] += (int16_t)((e * 7) / 16);
      nxt[x] += (int16_t)((e * 3) / 16);
      nxt[x + 1] += (int16_t)((e * 5) / 16);
      nxt[x + 2] += (int16_t)(e / 16);
    }
    _host->sdStreamWrite(packed, sizeof(packed));

    memset(_sum + (size_t)(t % BAND) * BIG_W, 0, sizeof(uint16_t) * BIG_W);
    memset(_cnt + (size_t)(t % BAND) * BIG_W, 0, BIG_W);
  }

  // The strip thumbnail: stretched and thresholded, because at 96 px a
  // dither is speckle and a threshold is a woodcut.
  void saveSmall() {
    uint32_t hist[256] = {};
    for (int i = 0; i < W * H; i++) hist[_small[i]]++;
    const uint32_t clip = (uint32_t)(W * H) / 50;  // 2%
    int lo = 0, hi = 255;
    for (uint32_t acc = 0; lo < 255; lo++) {
      acc += hist[lo];
      if (acc > clip) break;
    }
    for (uint32_t acc = 0; hi > 0; hi--) {
      acc += hist[hi];
      if (acc > clip) break;
    }
    if (hi - lo < 32) {
      lo = 0;
      hi = 255;  // a flat image: leave it alone
    }
    const int cut = lo + (hi - lo) / 2;
    uint8_t out[BYTES];
    memset(out, 0xFF, sizeof(out));
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        if (_small[(size_t)y * W + x] < cut)
          out[(size_t)y * (W / 8) + (x >> 3)] &= ~(0x80 >> (x & 7));
    tfs::write(_smallPath, (const char*)out, sizeof(out));
  }

  void freeAll() {
    free(_sum);
    free(_cnt);
    free(_err);
    free(_small);
    _sum = nullptr;
    _cnt = nullptr;
    _err = nullptr;
    _small = nullptr;
  }

  ToolsHost* _host = nullptr;
  uint16_t* _sum = nullptr;
  uint8_t* _cnt = nullptr;
  int16_t* _err = nullptr;
  uint8_t* _small = nullptr;
  uint32_t _smallSum[W] = {};
  uint16_t _smallCnt[W] = {};
  int _srcW = 0, _srcH = 0, _outW = 0, _outH = 0, _xOff = 0, _yOff = 0;
  int _nextOut = 0, _smallRow = 0;
  bool _up = false;
  char _bigPath[48] = {}, _smallPath[20] = {};
  bool _live = false;
};

// A .tbk cover: the book's first page, which is already exactly panel-sized,
// so this is the same pipeline with nothing to scale.
inline void makeAndSave(ToolsHost& h, const char* file, const uint8_t* page, int bpp) {
  Builder b;
  if (!b.begin(h, file, 480, 800)) return;
  uint8_t line[480];
  for (int y = 0; y < 800; y++) {
    for (int x = 0; x < 480; x++) {
      if (bpp == 2) {
        const uint32_t i = (uint32_t)y * 480 + x;
        const int lv = (page[i >> 2] >> (6 - 2 * (i & 3))) & 3;  // 0 black .. 3 white
        line[x] = (uint8_t)(lv * 85);
      } else {
        line[x] = (page[(size_t)y * 60 + (x >> 3)] & (0x80 >> (x & 7))) ? 255 : 0;
      }
    }
    b.row(y, line, 480);
  }
  b.finish();
}

// The loading screen a book opens behind. Best available: the full-size
// cover blitted straight to the panel, the small one blown up if the big has
// been dropped to make room, and a titled plate for a book being opened for
// the very first time -- which is the one open where the cover does not exist
// yet, because this is when it gets made.
inline void drawLoading(ToolsHost& h, ToolsCanvas& c, const char* file, const char* title) {
  // Off the card, borrowing the bus for the read if the reader has not taken
  // it yet. That costs about 150 ms and leaves the panel re-initialised,
  // which is free here: this is drawn as part of a full refresh anyway.
  char p[48];
  bigPath(file, p, sizeof(p));
  if (uint8_t* buf = (uint8_t*)malloc(BIG_BYTES)) {
    const bool got = h.sdReadWhole(p, buf, BIG_BYTES) == BIG_BYTES;
    if (got) {
      for (int y = 0; y < BIG_H; y++) {
        const uint8_t* row = buf + (size_t)y * (BIG_W / 8);
        for (int x = 0; x < BIG_W; x++)
          if (!(row[x >> 3] & (0x80 >> (x & 7)))) c.fillRect(x, y, 1, 1, true);
      }
      free(buf);
      return;
    }
    free(buf);
  }

  static uint8_t small[BYTES];
  if (load(file, small)) {
    for (int y = 0; y < H; y++) {
      const uint8_t* row = small + (size_t)y * (W / 8);
      for (int x = 0; x < W; x++)
        if (!(row[x >> 3] & (0x80 >> (x & 7)))) c.fillRect(x * SCALE, y * SCALE, SCALE, SCALE, true);
    }
    return;
  }

  c.drawRect(144, 200, 192, 300, 2, true);
  c.drawRect(152, 208, 176, 284, 1, true);  // a plate with a rule inside: a cover-to-be
  c.textCentered(c.width() / 2, 560, title, TS_LARGE, true, true);
  c.textCentered(c.width() / 2, 620, "opening the book", TS_SMALL, true);
}

}  // namespace bthumb
