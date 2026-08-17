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

#include "lock_image.h"
#include "lockscreen.h"
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

// --- the lock screen's copy ---------------------------------------------------
// The sleeping panel can wear the cover of the book being read, and it must be
// able to do that with the card out: power-off is not a moment to be powering
// the SD up and re-initialising the panel. So the cover is copied into flash
// the moment a book opens, in the picture format the lock screen already
// knows (tbimg), and power-off is the same flash read it always was.
//
// One file, overwritten. Keeping a copy per book would be 48 KB each for
// covers only ever shown one at a time.
inline constexpr const char* LOCK_PATH = "/lockcover.tbi";
inline constexpr const char* LOCK_TMP = "/lockcover.tmp";
inline constexpr uint32_t BAND_BYTES = 4800;  // 80 rows of the big cover
inline constexpr int BAND_ROW_BYTES = BIG_W / 8;  // 60, one row of a 1-bit cover

inline bool haveLock() { return tbimg::have(LOCK_PATH); }

// Copies this book's full-size cover off the card into that file. False when
// the book has no cover yet (the very first open, before the builder has run)
// or the card is gone -- and the lock screen falls back to GOODBYE, which is
// what it does for a picture that was never sent.
inline bool stashForLock(ToolsHost& h, const char* file) {
  char p[48];
  bigPath(file, p, sizeof(p));
  // 48,008 bytes, and not one of them in a buffer of its own. This used to
  // malloc the whole picture to hand to tfs::write, which is a transient
  // allocation the same size as the one that fragments this device's heap
  // into a state where books stop opening. Read a band, write a band.
  uint8_t* buf = (uint8_t*)malloc(BAND_BYTES);
  if (!buf) return false;
  // Into a temporary name, renamed over on success. Writing straight to the
  // real one truncates it the moment we start, so a copy that then failed --
  // a card pulled mid-read, say -- would take the perfectly good cover from
  // the last book with it, and the sleeping panel would fall back to GOODBYE
  // for no reason the owner could see.
  bool ok = tfs::appendOpen(LOCK_TMP);
  if (ok) {
    uint8_t hdr[tbimg::HEADER];
    hdr[0] = 'T'; hdr[1] = 'B'; hdr[2] = 'I'; hdr[3] = '1';
    hdr[4] = (uint8_t)(BIG_W & 255); hdr[5] = (uint8_t)(BIG_W >> 8);
    hdr[6] = (uint8_t)(BIG_H & 255); hdr[7] = (uint8_t)(BIG_H >> 8);
    ok = tfs::appendChunk(hdr, sizeof(hdr));
  }
  for (uint32_t off = 0; ok && off < (uint32_t)BIG_BYTES; off += BAND_BYTES) {
    const uint32_t n = (uint32_t)BIG_BYTES - off < BAND_BYTES ? (uint32_t)BIG_BYTES - off
                                                              : BAND_BYTES;
    ok = h.sdReadSlice(p, off, buf, n) == (int)n && tfs::appendChunk(buf, n);
  }
  tfs::appendClose();
  free(buf);
  if (ok)
    ok = tfs::rename(LOCK_TMP, LOCK_PATH);
  else
    tfs::remove(LOCK_TMP);  // half a picture is not a lock screen
  return ok;
}

// What a reader calls when it opens a book: nothing at all unless the sleeping
// panel is actually set to show covers, because 48 KB of flash written on
// every open would be a cost paid by everyone for a setting most people leave
// alone.
inline bool noteForLock(ToolsHost& h, const char* file) {
  if (lock::load(h.prefs()).empty != lock::EMPTY_COVER) return false;
  return stashForLock(h, file);
}

inline bool have(const char* file) {
  char p[20];
  path(file, p, sizeof(p));
  return tfs::size(p) == (size_t)BYTES;
}
// A book's cover is only DONE when both halves are there: the strip
// thumbnail in flash and the full-size picture on the card. Asking only about
// the thumbnail is how a book covered by an older firmware -- back when the
// loading screen was the 96x160 blown up five times, and no full-size cover
// was kept at all -- kept showing that blocky enlargement for ever: the
// builder looked, saw a thumbnail, and skipped. It rebuilds now, once, on the
// next open.
inline bool haveBig(ToolsHost& h, const char* file);
inline bool complete(ToolsHost& h, const char* file) {
  return have(file) && haveBig(h, file);
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
// Throws away every "this cover will not decode" marker.
//
// Called once when the firmware version changes, because a marker is only as
// trustworthy as the build that wrote it -- and builds of this firmware have
// marked covers permanently failed for reasons that were really a heap in
// pieces. A book whose cover was condemned by a bug should get its day back
// after the bug is fixed, and the cost of being wrong is one cover rebuilt.
inline int sweepFailed() {
  constexpr int MAX = 64, LEN = 24;
  static char names[MAX][LEN + 1];
  const int n = tfs::list("", "f", &names[0][0], LEN + 1, MAX, LEN);
  int gone = 0;
  for (int i = 0; i < n; i++) {
    // Only ours: "th_" + 8 hex + the "f" that list() stripped.
    if (strncmp(names[i], "th_", 3) != 0) continue;
    char p[32];
    snprintf(p, sizeof(p), "/%sf", names[i]);
    if (tfs::remove(p)) gone++;
  }
  return gone;
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
    // Static, not local: 1 KB of histogram and 2 KB of output on the loop
    // task's stack is most of a crash, and only one cover is ever built at a
    // time. See SET_LOOP_TASK_STACK_SIZE in main.cpp for the other half.
    static uint32_t hist[256];
    memset(hist, 0, sizeof(hist));
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
    static uint8_t out[BYTES];
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
// --- a cover the owner supplied ----------------------------------------------
// "<book stem>.cover.tbi", beside the book on the card, in the same 480x800
// one-bit format as a wallpaper or a lock screen picture.
//
// This beats everything else, for both kinds of book. A desktop has the whole
// image, a real dithering library and no 150 KB ceiling; the device has a
// streaming decoder and a band of RAM. Line art in particular comes out badly
// here -- Floyd-Steinberg is a photographic algorithm and it turns a flat grey
// background into a field of worms -- and no amount of cleverness on this chip
// beats somebody choosing the treatment on a monitor.
//
// It also makes a book open faster, because there is nothing to decode.
inline bool sidecarPath(const char* file, char* out, int cap) {
  const char* dot = strrchr(file, '.');
  const char* slash = strrchr(file, '/');
  if (!dot || (slash && dot < slash)) return false;
  const int stem = (int)(dot - file);
  return snprintf(out, (size_t)cap, "%.*s.cover.tbi", stem, file) < cap;
}

// Builds the cover from the sidecar when there is one and the stored copy is
// not already it. False means no sidecar -- decode the book's own cover
// instead. True means the cover is now current, whether it was rebuilt or was
// already right.
//
// The freshness check is 64 bytes from each: replacing the .tbi on the card
// should show up on the device, and re-reading two small chunks per open is
// cheaper than either rebuilding blindly or never noticing.
// Forty rows of the sidecar at a time -- eight thumbnail rows exactly, so an
// output row never straddles two reads. Static, not a local: this runs inside
// openBook on the loop task's 8 KB stack, and a 2,400-byte array there is how
// the first version of this function crashed the device.
inline uint8_t g_scBand[BAND_ROW_BYTES * 40];
inline constexpr int SC_ROWS = 40;

// One row of the strip thumbnail from one band: each output pixel is the mean
// of a 5x5 block of the sidecar's bits, which is the same "average, never
// sample" rule the builder follows when it shrinks a decoded cover.
inline void scSmallRow(const uint8_t* band, int r0, uint8_t* out96) {
  for (int x = 0; x < W; x++) {
    int sum = 0;
    for (int dy = 0; dy < SCALE; dy++) {
      const uint8_t* src = band + (size_t)(r0 + dy) * BAND_ROW_BYTES;
      for (int dx = 0; dx < SCALE; dx++) {
        const int sx = x * SCALE + dx;
        if (src[sx >> 3] & (0x80 >> (sx & 7))) sum += 255;  // 1 = white
      }
    }
    out96[x] = (uint8_t)(sum / (SCALE * SCALE));
  }
}

inline bool coverFromSidecar(ToolsHost& h, const char* file) {
  char sc[160];
  if (!sidecarPath(file, sc, sizeof(sc))) return false;
  uint8_t head[64];
  if (h.sdReadSlice(sc, tbimg::HEADER, head, sizeof(head)) != (int)sizeof(head)) return false;

  // Is the cover on the card already this picture? Sampled at four places
  // rather than one, and none of them the top: almost every cover starts with
  // a band of white margin, so two completely different covers agree on their
  // first 64 bytes far more often than not -- which made "replace the .tbi and
  // reopen" quietly do nothing. These four are a fifth, a half, three quarters
  // and near the end of the picture, where the ink is.
  char big[48];
  bigPath(file, big, sizeof(big));
  {
    static const uint32_t kProbe[4] = {9600, 24000, 36000, 45600};
    bool same = have(file);
    for (int i = 0; same && i < 4; i++) {
      uint8_t a[64], b[64];
      same = h.sdReadSlice(sc, tbimg::HEADER + kProbe[i], a, sizeof(a)) == (int)sizeof(a) &&
             h.sdReadSlice(big, kProbe[i], b, sizeof(b)) == (int)sizeof(b) &&
             memcmp(a, b, sizeof(a)) == 0;
    }
    if (same) return true;  // already the cover on the card
  }

  // The builder is deliberately NOT used here. A sidecar is already the
  // finished picture -- 480x800, one bit, the framebuffer's own convention --
  // so there is nothing to scale and nothing to dither, and running it through
  // the builder meant 46 KB of heap, a re-dither of an image the owner already
  // dithered on a real machine, and a stack frame that overflowed the task.
  // Copy the pixels; average the thumbnail out of them on the way past.
  {
    // This book's cover from before covers lived on the card, swept on the
    // open that was rebuilding it anyway.
    char stale[20];
    staleFlashPath(file, stale, sizeof(stale));
    if (tfs::exists(stale)) tfs::remove(stale);
  }
  if (!h.sdStreamOpen(big)) return true;  // a bad moment, not a bad cover
  uint8_t row96[W];
  static uint32_t hist[256];
  memset(hist, 0, sizeof(hist));
  for (int y0 = 0; y0 < BIG_H; y0 += SC_ROWS) {
    const uint32_t n = (uint32_t)SC_ROWS * BAND_ROW_BYTES;
    if (h.sdReadSlice(sc, tbimg::HEADER + (uint32_t)y0 * BAND_ROW_BYTES, g_scBand, (int)n) !=
            (int)n ||
        !h.sdStreamWrite(g_scBand, n)) {
      h.sdStreamClose(false);
      return true;  // a short or unreadable sidecar: try again next open
    }
    for (int r = 0; r < SC_ROWS; r += SCALE) {
      scSmallRow(g_scBand, r, row96);
      for (int x = 0; x < W; x++) hist[row96[x]]++;
    }
  }
  if (!h.sdStreamClose(true)) return true;

  // The strip thumbnail: stretched and thresholded, not dithered, exactly as
  // the builder does it -- at 96 px a dither is speckle and a threshold is a
  // woodcut. The cut needs the whole picture's histogram, which is why the
  // bands are read a second time rather than kept: 48 KB of card beats 15 KB
  // of RAM held for the life of the firmware.
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
    hi = 255;  // a flat picture: leave it alone
  }
  const int cut = lo + (hi - lo) / 2;

  static uint8_t out[BYTES];
  memset(out, 0xFF, sizeof(out));
  int sy = 0;
  for (int y0 = 0; y0 < BIG_H; y0 += SC_ROWS) {
    const uint32_t n = (uint32_t)SC_ROWS * BAND_ROW_BYTES;
    if (h.sdReadSlice(sc, tbimg::HEADER + (uint32_t)y0 * BAND_ROW_BYTES, g_scBand, (int)n) !=
        (int)n)
      return true;  // the big one is written; the strip can wait for next open
    for (int r = 0; r < SC_ROWS && sy < H; r += SCALE, sy++) {
      scSmallRow(g_scBand, r, row96);
      for (int x = 0; x < W; x++)
        if (row96[x] < cut) out[(size_t)sy * (W / 8) + (x >> 3)] &= (uint8_t)~(0x80 >> (x & 7));
    }
  }
  char smallPath[20];
  path(file, smallPath, sizeof(smallPath));
  tfs::write(smallPath, (const char*)out, sizeof(out));
  return true;
}

// True when both pictures were written. The caller is expected to look: a
// cover that did not get made leaves `have()` false, and the next open of the
// book tries again.
//
// Deliberately no failure marker here, unlike the EPUB side. There the marker
// exists because a cover that will not DECODE will never decode -- it is a
// property of the file, and retrying it costs seconds on every open forever.
// A .tbk cover decodes nothing: it is already the framebuffer's own bytes, so
// every way this can fail (no heap for the bands, a card that stopped
// answering, a full card) is a passing condition. Marking those permanently
// would deny a book its cover for the rest of its life because the device was
// briefly short of memory once.
inline bool makeAndSave(ToolsHost& h, const char* file, const uint8_t* page, int bpp) {
  Builder b;
  if (!b.begin(h, file, 480, 800)) return false;
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
  // finish() writes the small picture only if the big one closed, so a failure
  // here leaves neither -- not a thumbnail with no cover behind it.
  return b.finish();
}

// Breaks `s` into lines that fit `maxW`, whole words wherever it can, and
// says whether it ran out of lines before it ran out of text. Measuring is
// the canvas's job, so this works for any face the caller has set.
inline int wrapLines(ToolsCanvas& c, const char* s, TSize sz, bool bold, int maxW,
                     char out[][64], int maxLines, bool* truncated) {
  *truncated = false;
  int n = 0;
  while (*s && n < maxLines) {
    if (c.textWidth(s, sz, bold) <= maxW) {  // the rest fits on this line
      snprintf(out[n++], 64, "%s", s);
      return n;
    }
    char buf[64];
    int fit = 0, brk = 0;
    for (int i = 0; s[i] && i < (int)sizeof(buf) - 1; i++) {
      buf[i] = s[i];
      buf[i + 1] = 0;
      if (c.textWidth(buf, sz, bold) > maxW) break;
      fit = i + 1;
      if (s[i] == ' ') brk = i;
    }
    const int take = brk > 0 ? brk : (fit > 0 ? fit : 1);  // a huge word breaks mid-word
    memcpy(out[n], s, (size_t)take);
    out[n][take] = 0;
    n++;
    s += take;
    while (*s == ' ') s++;
  }
  *truncated = (*s != 0);
  return n;
}

// The plate a first open shows. Nothing but the book's name, as large as the
// name allows, and one line saying what the wait is for.
//
// It used to draw an empty frame here -- a picture-shaped hole where the
// cover was about to go. On glass that read as a screen that had failed to
// load, which is the opposite of what it was for: this is the slowest open a
// book ever has (the cover is being decoded out of the file) and the screen's
// whole job is to say so calmly. The cover arrives full-bleed a moment later,
// with a beep; see the readers' openBook.
//
// "opening the book" is the wording for a book whose cover will never come
// (the decode was tried once and marked failed): promising an unwrapping that
// cannot happen would be a lie the second time round.
inline void drawPlate(ToolsCanvas& c, const char* file, const char* title) {
  const int maxW = c.width() - 48;
  char cap[64];
  snprintf(cap, sizeof(cap), "%s", title ? title : "");

  // As large as it fits: the biggest face if the name lands in two lines,
  // one step down if it needs more. A name is what the person is waiting to
  // see confirmed, so it gets the room.
  char lines[4][64];
  bool over = false;
  TSize sz = TS_HUGE;
  int n = wrapLines(c, cap, sz, true, maxW, lines, 2, &over);
  if (over) {
    sz = TS_LARGE;
    n = wrapLines(c, cap, sz, true, maxW, lines, 4, &over);
  }

  // The block sits a little above the middle, where a title belongs on a
  // portrait page -- optically centred rather than arithmetically.
  const int step = c.textHeight(sz) + 14;
  const int subH = c.textHeight(TS_SMALL);
  const int blockH = n * step + 22 + subH;
  int y = 360 - blockH / 2;
  if (y < 120) y = 120;
  for (int i = 0; i < n; i++, y += step) {
    if (i == n - 1 && over)  // only a name too long for four lines is cut
      c.textClipped(24, y, maxW, lines[i], sz, true, true);
    else
      c.textCentered(c.width() / 2, y, lines[i], sz, true, true);
  }
  c.textCentered(c.width() / 2, y + 22,
                 failed(file) ? "opening the book" : "unwrapping the new book", TS_SMALL, true);
}

// The loading screen a book opens behind. Best available: the full-size
// cover blitted straight to the panel, the small one blown up if the big has
// been dropped to make room, and the titled plate above for a book being
// opened for the very first time -- the one open where the cover does not
// exist yet, because this is when it gets made.
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

  drawPlate(c, file, title);
}

}  // namespace bthumb
