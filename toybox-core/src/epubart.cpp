// A book's own illustrations, prepared on the device. See tools/epub/epub_art.h.
#include "tools/epub/epub_art.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools/epub/epub_jpegdc.h"
#include "tools/epub/epub_jpegprog.h"
#include "tools/epub/epub_png.h"

extern "C" {
#include <tjpgd.h>
}

#ifdef EPUBART_DUMP  // a PC bench's hook, never defined in the firmware
void EPUBART_DUMP(const uint8_t* grey, int w, int h);
#endif

namespace epubart {
namespace {

// The decoded picture is held whole, in PSRAM (every allocation here is far
// over the 4 KB line that sends malloc there). 1.5 MP is a 1000x1500 page --
// already twice the panel each way, which is all an area-weighted shrink
// needs. Bigger pictures arrive pre-shrunk: by TJpgDec's own 1/2..1/8 for
// baseline JPEG, by an integer box while streaming for everything else.
constexpr uint32_t SRC_MAX = 1500000u;
// The progressive decoder's coefficient memory (epub_jpegprog.h). 3 MB holds
// a 2000x2000 page at half size; past it the decoder takes a smaller IDCT.
constexpr uint32_t PROG_BUDGET = 3u * 1024u * 1024u;

uint32_t fnv(const char* s, uint32_t h = 2166136261u) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// --- the decoded picture -----------------------------------------------------

struct Src {
  uint8_t* px = nullptr;  // w*h grey, row-major
  int w = 0, h = 0;       // after any integer pre-shrink
  int fullW = 0, fullH = 0;
  int k = 1;              // the pre-shrink: k x k source pixels per cell
  uint16_t* acc = nullptr;
  int accRow = -1, accN = 0;

  ~Src() { release(); }
  void release() {
    free(px);
    free(acc);
    px = nullptr;
    acc = nullptr;
  }

  // `rows` = the decoder hands out whole rows, so it can be box-shrunk while
  // streaming; a block decoder must arrive small enough already.
  bool alloc(int fw, int fh, bool rows) {
    release();
    if (fw < 1 || fh < 1) return false;
    fullW = fw;
    fullH = fh;
    k = 1;
    if (rows)
      while ((uint32_t)((fw + k - 1) / k) * (uint32_t)((fh + k - 1) / k) > SRC_MAX) k++;
    w = (fw + k - 1) / k;
    h = (fh + k - 1) / k;
    if ((uint32_t)w * (uint32_t)h > SRC_MAX) return false;
    px = (uint8_t*)malloc((size_t)w * h);
    if (!px) return false;
    memset(px, 255, (size_t)w * h);
    if (k > 1) {
      acc = (uint16_t*)calloc((size_t)w, sizeof(uint16_t));
      if (!acc) {
        release();
        return false;
      }
    }
    accRow = -1;
    accN = 0;
    return true;
  }

  void flushRow() {
    if (k == 1 || accRow < 0 || accRow >= h || accN == 0) return;
    uint8_t* dst = px + (size_t)accRow * w;
    for (int cx = 0; cx < w; cx++) {
      const int cols = (fullW - cx * k) < k ? (fullW - cx * k) : k;
      const int n = cols * accN;
      dst[cx] = (uint8_t)((acc[cx] + n / 2) / n);
      acc[cx] = 0;
    }
    accN = 0;
  }

  void row(int y, const uint8_t* g, int n) {
    if (!px || y < 0 || y >= fullH) return;
    if (n > fullW) n = fullW;
    if (k == 1) {
      memcpy(px + (size_t)y * w, g, (size_t)n);
      return;
    }
    const int ry = y / k;
    if (ry != accRow) {
      flushRow();
      accRow = ry;
    }
    for (int x = 0; x < n; x++) acc[x / k] = (uint16_t)(acc[x / k] + g[x]);
    accN++;
  }

  void block(int x0, int y0, int bw, int bh, const uint8_t* g) {
    if (!px || k != 1) return;
    for (int r = 0; r < bh; r++) {
      const int y = y0 + r;
      if (y < 0 || y >= h) continue;
      int n = bw;
      if (x0 + n > w) n = w - x0;
      if (n > 0) memcpy(px + (size_t)y * w + x0, g + (size_t)r * bw, (size_t)n);
    }
  }
};

struct Ctx {
  epubc::Book* book;
  Src* src;
  bool sized = false;
};

int blobRead(void* c, uint8_t* dst, int n) { return ((Ctx*)c)->book->blobRead(dst, n); }

size_t jpegIn(JDEC* jd, uint8_t* buf, size_t n) {
  auto* ctx = (Ctx*)jd->device;
  if (buf) {
    const int got = ctx->book->blobRead(buf, (int)n);
    return got < 0 ? 0 : (size_t)got;
  }
  uint8_t junk[64];
  size_t left = n;
  while (left) {
    const int take = left < sizeof(junk) ? (int)left : (int)sizeof(junk);
    const int got = ctx->book->blobRead(junk, take);
    if (got <= 0) return n - left;
    left -= (size_t)got;
  }
  return n;
}

int jpegOut(JDEC* jd, void* bitmap, JRECT* r) {
  ((Ctx*)jd->device)
      ->src->block(r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1,
                   (const uint8_t*)bitmap);
  return 1;
}

bool rowsSize(void* c, int w, int h) {
  auto* ctx = (Ctx*)c;
  ctx->sized = ctx->src->alloc(w, h, true);
  return ctx->sized;
}
void rowsRow(void* c, int y, const uint8_t* g, int w) { ((Ctx*)c)->src->row(y, g, w); }

// --- resampling ----------------------------------------------------------------

// One line, area-weighted: each output sample is the exact average of the
// stretch of input it covers, partial pixels at either end weighted by how
// much of them falls inside. In 1/256ths of a grey level, so the second pass
// does not round twice.
void areaLine(const uint8_t* in, int inStep, int inN, uint16_t* out, int outStep, int outN) {
  const float r = (float)inN / (float)outN;
  for (int j = 0; j < outN; j++) {
    const float a = j * r, b = (j + 1) * r;
    int i = (int)a;
    float sum = 0;
    float t = a;
    while (t < b - 1e-4f && i < inN) {
      const float end = (float)(i + 1) < b ? (float)(i + 1) : b;
      sum += (end - t) * in[(size_t)i * inStep];
      t = end;
      i++;
    }
    out[(size_t)j * outStep] = (uint16_t)(sum / r * 256.0f + 0.5f);
  }
}

void areaLine16(const uint16_t* in, int inStep, int inN, uint8_t* out, int outStep, int outN) {
  const float r = (float)inN / (float)outN;
  for (int j = 0; j < outN; j++) {
    const float a = j * r, b = (j + 1) * r;
    int i = (int)a;
    float sum = 0;
    float t = a;
    while (t < b - 1e-4f && i < inN) {
      const float end = (float)(i + 1) < b ? (float)(i + 1) : b;
      sum += (end - t) * in[(size_t)i * inStep];
      t = end;
      i++;
    }
    const int v = (int)(sum / r / 256.0f + 0.5f);
    out[(size_t)j * outStep] = (uint8_t)(v > 255 ? 255 : v);
  }
}

// The picture fitted into the panel, keeping its shape, into `g` (W*H, white
// already). Returns false only for want of memory.
bool resample(Src& s, uint8_t* g, int& ox, int& oy, int& ow, int& oh, bool& shrunk) {
  if ((int64_t)s.w * H >= (int64_t)s.h * W) {
    ow = W;
    oh = (int)(((int64_t)s.h * W + s.w / 2) / s.w);
  } else {
    oh = H;
    ow = (int)(((int64_t)s.w * H + s.h / 2) / s.h);
  }
  if (ow < 1) ow = 1;
  if (oh < 1) oh = 1;
  ox = (W - ow) / 2;
  oy = (H - oh) / 2;
  shrunk = ow <= s.w;
  if (shrunk) {
    // Columns first, into a narrow intermediate, then rows.
    uint16_t* mid = (uint16_t*)malloc((size_t)ow * s.h * sizeof(uint16_t));
    if (!mid) return false;
    for (int y = 0; y < s.h; y++) areaLine(s.px + (size_t)y * s.w, 1, s.w, mid + (size_t)y * ow, 1, ow);
    s.release();  // the biggest buffer goes before the last pass needs nothing more
    for (int x = 0; x < ow; x++)
      areaLine16(mid + x, ow, s.h, g + (size_t)oy * W + ox + x, W, oh);
    free(mid);
    return true;
  }
  // Growing: bilinear, sample centres on pixel centres. A small illustration
  // enlarged by nearest-neighbour is a staircase; this is a soft edge, and the
  // unsharp mask afterwards takes some of the softness back.
  const float sx = (float)s.w / ow, sy = (float)s.h / oh;
  for (int y = 0; y < oh; y++) {
    float fy = (y + 0.5f) * sy - 0.5f;
    if (fy < 0) fy = 0;
    int y0 = (int)fy;
    if (y0 > s.h - 1) y0 = s.h - 1;
    const int y1 = y0 + 1 < s.h ? y0 + 1 : y0;
    const float wy = fy - y0;
    const uint8_t* r0 = s.px + (size_t)y0 * s.w;
    const uint8_t* r1 = s.px + (size_t)y1 * s.w;
    uint8_t* dst = g + (size_t)(oy + y) * W + ox;
    for (int x = 0; x < ow; x++) {
      float fx = (x + 0.5f) * sx - 0.5f;
      if (fx < 0) fx = 0;
      int x0 = (int)fx;
      if (x0 > s.w - 1) x0 = s.w - 1;
      const int x1 = x0 + 1 < s.w ? x0 + 1 : x0;
      const float wx = fx - x0;
      const float top = r0[x0] + (r0[x1] - r0[x0]) * wx;
      const float bot = r1[x0] + (r1[x1] - r1[x0]) * wx;
      dst[x] = (uint8_t)(top + (bot - top) * wy + 0.5f);
    }
  }
  s.release();
  return true;
}

// --- tone and edges --------------------------------------------------------------

// Levels from the histogram: a scanned page is rarely black on white -- its
// paper is a light grey and its ink a dark one, and on a panel with four
// greys that wastes one of them. The ends are clipped at 0.3% so a few dust
// specks do not set the black point. Pictures already spanning the range,
// and flat ones (a near-solid plate), are left alone.
void levels(uint8_t* g, int ox, int oy, int ow, int oh) {
  static uint32_t hist[256];
  memset(hist, 0, sizeof(hist));
  for (int y = 0; y < oh; y++) {
    const uint8_t* r = g + (size_t)(oy + y) * W + ox;
    for (int x = 0; x < ow; x++) hist[r[x]]++;
  }
  const uint32_t n = (uint32_t)ow * oh, clip = n * 3 / 1000;
  int lo = 0, hi = 255;
  for (uint32_t a = 0; lo < 255; lo++) {
    a += hist[lo];
    if (a > clip) break;
  }
  for (uint32_t a = 0; hi > 0; hi--) {
    a += hist[hi];
    if (a > clip) break;
  }
  if (hi - lo < 96 || (lo <= 4 && hi >= 251)) return;
  uint8_t lut[256];
  for (int v = 0; v < 256; v++) {
    const int o = (v - lo) * 255 / (hi - lo);
    lut[v] = (uint8_t)(o < 0 ? 0 : o > 255 ? 255 : o);
  }
  for (int y = 0; y < oh; y++) {
    uint8_t* r = g + (size_t)(oy + y) * W + ox;
    for (int x = 0; x < ow; x++) r[x] = lut[r[x]];
  }
}

// Unsharp mask against a 3x3 binomial blur. `amt` in 1/16ths. The blur must
// read the ORIGINAL neighbours, so the row above (already sharpened in place)
// is kept as it was before.
void sharpen(uint8_t* g, int ox, int oy, int ow, int oh, int amt) {
  if (ow < 3 || oh < 3 || amt <= 0) return;
  static uint8_t prev[W], cur[W], next[W];
  memcpy(cur, g + (size_t)oy * W + ox, (size_t)ow);
  memcpy(prev, cur, (size_t)ow);
  for (int y = 0; y < oh; y++) {
    if (y + 1 < oh)
      memcpy(next, g + (size_t)(oy + y + 1) * W + ox, (size_t)ow);
    else
      memcpy(next, cur, (size_t)ow);
    uint8_t* dst = g + (size_t)(oy + y) * W + ox;
    for (int x = 0; x < ow; x++) {
      const int l = x > 0 ? x - 1 : x, r = x + 1 < ow ? x + 1 : x;
      const int blur = (prev[l] + 2 * prev[x] + prev[r] + 2 * cur[l] + 4 * cur[x] + 2 * cur[r] +
                        next[l] + 2 * next[x] + next[r] + 8) >> 4;
      const int v = cur[x] + (((cur[x] - blur) * amt) >> 4);
      dst[x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    memcpy(prev, cur, (size_t)ow);
    memcpy(cur, next, (size_t)ow);
  }
}

// --- dithering -------------------------------------------------------------------

// Serpentine Floyd-Steinberg to `levels` evenly spaced greys (4 here), each
// pixel first passed through `tone`. Rows go out as they finish, packed the
// way the matching panel path reads them. The carried error is clamped:
// unclamped, a long run of near-white builds a debt that pays out as a worm.
bool dither(ToolsHost& host, const uint8_t* g, const uint8_t* tone, int levels, const char* path) {
  if (!host.sdStreamOpen(path)) return false;
  static int16_t err[2][W + 2];
  memset(err, 0, sizeof(err));
  const int bpp = levels == 4 ? 2 : 1;
  const int stride = W * bpp / 8;
  static uint8_t out[10 * (W / 4)];  // ten rows at the larger stride
  int inOut = 0;
  const int step = 255 / (levels - 1);
  bool ok = true;
  for (int y = 0; y < H && ok; y++) {
    int16_t* cur = err[y & 1];
    int16_t* nxt = err[(y + 1) & 1];
    memset(nxt, 0, sizeof(err[0]));
    uint8_t* row = out + (size_t)inOut * stride;
    memset(row, 0xFF, (size_t)stride);  // white, in both packings
    const bool rtl = y & 1;
    for (int i = 0; i < W; i++) {
      const int x = rtl ? W - 1 - i : i;
      int v = tone[g[(size_t)y * W + x]] + cur[x + 1];
      if (v < -64) v = -64;
      if (v > 319) v = 319;
      int q = (v + step / 2) / step;
      if (q < 0) q = 0;
      if (q > levels - 1) q = levels - 1;
      const int e = v - q * step;
      if (bpp == 2) {
        const int shift = 6 - 2 * (x & 3);
        row[x >> 2] = (uint8_t)((row[x >> 2] & ~(3 << shift)) | (q << shift));
      } else if (q == 0) {
        row[x >> 3] &= (uint8_t) ~(0x80 >> (x & 7));
      }
      const int d = rtl ? -1 : 1;
      cur[x + 1 + d] = (int16_t)(cur[x + 1 + d] + e * 7 / 16);
      nxt[x + 1 - d] = (int16_t)(nxt[x + 1 - d] + e * 3 / 16);
      nxt[x + 1] = (int16_t)(nxt[x + 1] + e * 5 / 16);
      nxt[x + 1 + d] = (int16_t)(nxt[x + 1 + d] + e / 16);
    }
    if (++inOut == 10 || y == H - 1) {
      ok = host.sdStreamWrite(out, (uint32_t)(inOut * stride));
      inOut = 0;
    }
  }
  return host.sdStreamClose(ok) && ok;
}

// One bit: Atkinson, not Floyd-Steinberg. It passes on only 6/8 of the
// error, so near-white stays paper and near-black stays ink instead of being
// salted with stray dots -- on line art (which most book illustrations are)
// the outlines and faces come out clean where Floyd-Steinberg leaves a fine
// grain over everything. Compared side by side on real retail art.
bool ditherAtkinson(ToolsHost& host, const uint8_t* g, const uint8_t* tone, const char* path) {
  if (!host.sdStreamOpen(path)) return false;
  static int16_t err[3][W + 4];  // rows y, y+1, y+2; two columns of margin each side
  memset(err, 0, sizeof(err));
  constexpr int STRIDE = W / 8;
  static uint8_t out[10 * STRIDE];
  int inOut = 0;
  bool ok = true;
  for (int y = 0; y < H && ok; y++) {
    int16_t* r0 = err[y % 3];
    int16_t* r1 = err[(y + 1) % 3];
    int16_t* r2 = err[(y + 2) % 3];
    uint8_t* row = out + (size_t)inOut * STRIDE;
    memset(row, 0xFF, STRIDE);
    for (int x = 0; x < W; x++) {
      int v = tone[g[(size_t)y * W + x]] + r0[x + 2];
      const int q = v >= 128 ? 255 : 0;
      if (!q) row[x >> 3] &= (uint8_t) ~(0x80 >> (x & 7));
      const int e = (v - q) / 8;
      r0[x + 3] = (int16_t)(r0[x + 3] + e);
      r0[x + 4] = (int16_t)(r0[x + 4] + e);
      r1[x + 1] = (int16_t)(r1[x + 1] + e);
      r1[x + 2] = (int16_t)(r1[x + 2] + e);
      r1[x + 3] = (int16_t)(r1[x + 3] + e);
      r2[x + 2] = (int16_t)(r2[x + 2] + e);
    }
    memset(r0, 0, sizeof(err[0]));  // becomes row y+3
    if (++inOut == 10 || y == H - 1) {
      ok = host.sdStreamWrite(out, (uint32_t)(inOut * STRIDE));
      inOut = 0;
    }
  }
  return host.sdStreamClose(ok) && ok;
}

}  // namespace

void paths(const char* bookFile, const char* entry, uint32_t usize, char* g2, char* b1, int cap) {
  const uint32_t book = fnv(bookFile);
  const uint32_t pic = fnv(entry) ^ (usize * 2654435761u);
  snprintf(g2, (size_t)cap, "/.toybox/art%d/%08lx/%08lx.g2", PIPELINE, (unsigned long)book,
           (unsigned long)pic);
  snprintf(b1, (size_t)cap, "/.toybox/art%d/%08lx/%08lx.b1", PIPELINE, (unsigned long)book,
           (unsigned long)pic);
}

bool prepare(ToolsHost& host, epubc::Book& book, const char* entry, const char* g2Path,
             const char* b1Path, bool* transient) {
  if (transient) *transient = false;
  uint8_t sig[8] = {};
  if (!book.blobOpen(entry)) return false;
  int got = 0;
  while (got < 8) {
    const int r = book.blobRead(sig + got, 8 - got);
    if (r <= 0) break;
    got += r;
  }
  book.blobClose();
  const bool jpeg = got >= 3 && sig[0] == 0xFF && sig[1] == 0xD8 && sig[2] == 0xFF;
  const bool png = got == 8 && sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G';
  if (!jpeg && !png) return false;  // GIF, SVG, WebP: the plate, honestly

  Src src;
  Ctx ctx{&book, &src, false};
  bool ok = false;
  if (!book.blobOpen(entry)) return false;
  if (jpeg) {
    constexpr size_t WORK = 6500;
    void* work = malloc(WORK);
    if (!work) {
      book.blobClose();
      if (transient) *transient = true;
      return false;
    }
    JDEC jd;
    const JRESULT prep = jd_prepare(&jd, jpegIn, work, WORK, &ctx);
    if (prep == JDR_OK) {
      int scale = 0;
      while (scale < 3 && (uint32_t)(jd.width >> scale) * (uint32_t)(jd.height >> scale) > SRC_MAX)
        scale++;
      if (src.alloc(jd.width >> scale, jd.height >> scale, false)) {
        ok = jd_decomp(&jd, jpegOut, (uint8_t)scale) == JDR_OK;
      } else if (transient) {
        *transient = true;
      }
      free(work);
      book.blobClose();
    } else {
      free(work);
      book.blobClose();
      if (prep != JDR_FMT3) return false;  // corrupt rather than progressive
      // Progressive -- which is most commercial art. Decoded in full for luma,
      // at the smallest IDCT size still as big as the panel will show it.
      if (!book.blobOpen(entry)) return false;
      ejprog::In pin{&ctx, blobRead};
      ok = ejprog::decodeGray(pin, W, H, PROG_BUDGET, rowsSize, rowsRow, &ctx);
      book.blobClose();
      if (!ok) {
        // Something the full decoder does not follow: its first scan alone,
        // the picture at an eighth -- soft, but the picture.
        src.release();
        ctx.sized = false;
        if (!book.blobOpen(entry)) return false;
        ejdc::In in{&ctx, blobRead};
        ok = ejdc::decodeGray(in, rowsSize, rowsRow, &ctx);
        book.blobClose();
      }
      if (!ctx.sized && transient) *transient = true;
    }
  } else {
    epng::In in{&ctx, blobRead};
    ok = epng::decodeGray(in, rowsSize, rowsRow, &ctx);
    book.blobClose();
    if (!ctx.sized && transient) *transient = true;
  }
  if (!ok || !src.px) return false;
  src.flushRow();

  uint8_t* g = (uint8_t*)malloc((size_t)W * H);
  if (!g) {
    if (transient) *transient = true;
    return false;
  }
  memset(g, 255, (size_t)W * H);
  int ox, oy, ow, oh;
  bool shrunk;
  if (!resample(src, g, ox, oy, ow, oh, shrunk)) {
    free(g);
    if (transient) *transient = true;
    return false;
  }
  levels(g, ox, oy, ow, oh);
  // Gentler on an enlarged picture: there is no detail lost to a shrink to
  // bring back, only softness, and sharpening that further shows its halo.
  sharpen(g, ox, oy, ow, oh, shrunk ? 8 : 5);

#ifdef EPUBART_DUMP  // the bench's view of the grey picture before dithering
  EPUBART_DUMP(g, W, H);
#endif
  // Tone curves. Four greys: as the picture says. One bit: deepened a little
  // (gamma 1.15) -- Atkinson drops a quarter of the error, which lightens the
  // light greys, and hair and fabric lines are light greys.
  static uint8_t toneG2[256], toneB1[256];
  for (int v = 0; v < 256; v++) {
    toneG2[v] = (uint8_t)v;
    toneB1[v] = (uint8_t)(255.0f * powf(v / 255.0f, 1.15f) + 0.5f);
  }
  const bool okG2 = dither(host, g, toneG2, 4, g2Path);
  const bool okB1 = ditherAtkinson(host, g, toneB1, b1Path);
  free(g);
  if (!(okG2 && okB1) && transient) *transient = true;  // the card, not the picture
  return okG2 && okB1;
}

}  // namespace epubart
