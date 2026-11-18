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
  bool noMemory = false;  // the decoder got as far as a size and memory said no
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
  if (!ctx->sized) ctx->noMemory = true;
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

// A crop of the picture (cx, cy, cw, ch in source pixels) fitted into a
// dw x dh box keeping its shape, centred, into `dst` (stride dw, white
// already). The source is left as it was: one decode serves every part.
// False only for want of memory.
bool fit(const Src& s, int cx, int cy, int cw, int ch, uint8_t* dst, int dw, int dh, int& ox,
         int& oy, int& ow, int& oh, bool& shrunk) {
  if ((int64_t)cw * dh >= (int64_t)ch * dw) {
    ow = dw;
    oh = (int)(((int64_t)ch * dw + cw / 2) / cw);
  } else {
    oh = dh;
    ow = (int)(((int64_t)cw * dh + ch / 2) / ch);
  }
  if (ow < 1) ow = 1;
  if (oh < 1) oh = 1;
  ox = (dw - ow) / 2;
  oy = (dh - oh) / 2;
  shrunk = ow <= cw;
  const uint8_t* base = s.px + (size_t)cy * s.w + cx;
  if (shrunk) {
    // Columns first, into a narrow intermediate, then rows.
    uint16_t* mid = (uint16_t*)malloc((size_t)ow * ch * sizeof(uint16_t));
    if (!mid) return false;
    for (int y = 0; y < ch; y++) areaLine(base + (size_t)y * s.w, 1, cw, mid + (size_t)y * ow, 1, ow);
    for (int x = 0; x < ow; x++) areaLine16(mid + x, ow, ch, dst + (size_t)oy * dw + ox + x, dw, oh);
    free(mid);
    return true;
  }
  // Growing: bilinear, sample centres on pixel centres. A small illustration
  // enlarged by nearest-neighbour is a staircase; this is a soft edge, and the
  // unsharp mask afterwards takes some of the softness back.
  const float sx = (float)cw / ow, sy = (float)ch / oh;
  for (int y = 0; y < oh; y++) {
    float fy = (y + 0.5f) * sy - 0.5f;
    if (fy < 0) fy = 0;
    int y0 = (int)fy;
    if (y0 > ch - 1) y0 = ch - 1;
    const int y1 = y0 + 1 < ch ? y0 + 1 : y0;
    const float wy = fy - y0;
    const uint8_t* r0 = base + (size_t)y0 * s.w;
    const uint8_t* r1 = base + (size_t)y1 * s.w;
    uint8_t* d = dst + (size_t)(oy + y) * dw + ox;
    for (int x = 0; x < ow; x++) {
      float fx = (x + 0.5f) * sx - 0.5f;
      if (fx < 0) fx = 0;
      int x0 = (int)fx;
      if (x0 > cw - 1) x0 = cw - 1;
      const int x1 = x0 + 1 < cw ? x0 + 1 : x0;
      const float wx = fx - x0;
      const float top = r0[x0] + (r0[x1] - r0[x0]) * wx;
      const float bot = r1[x0] + (r1[x1] - r1[x0]) * wx;
      d[x] = (uint8_t)(top + (bot - top) * wy + 0.5f);
    }
  }
  return true;
}

// --- tone and edges --------------------------------------------------------------

// Levels from the histogram, once, on the decoded picture -- so every part of
// a spread gets the same tones. A scanned page is rarely black on white: its
// paper is a light grey and its ink a dark one, and on a panel with four
// greys that wastes one of them. The ends are clipped at 0.3% so a few dust
// specks do not set the black point. Pictures already spanning the range,
// and flat ones (a near-solid plate), are left alone.
void levels(Src& s) {
  static uint32_t hist[256];
  memset(hist, 0, sizeof(hist));
  const size_t n = (size_t)s.w * s.h;
  for (size_t i = 0; i < n; i++) hist[s.px[i]]++;
  const uint32_t clip = (uint32_t)(n * 3 / 1000);
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
  for (size_t i = 0; i < n; i++) s.px[i] = lut[s.px[i]];
}

// Unsharp mask against a 3x3 binomial blur. `amt` in 1/16ths. The blur must
// read the ORIGINAL neighbours, so the row above (already sharpened in place)
// is kept as it was before.
void sharpen(uint8_t* g, int stride, int ox, int oy, int ow, int oh, int amt) {
  if (ow < 3 || oh < 3 || amt <= 0 || ow > 800) return;
  static uint8_t prev[800], cur[800], next[800];
  memcpy(cur, g + (size_t)oy * stride + ox, (size_t)ow);
  memcpy(prev, cur, (size_t)ow);
  for (int y = 0; y < oh; y++) {
    if (y + 1 < oh)
      memcpy(next, g + (size_t)(oy + y + 1) * stride + ox, (size_t)ow);
    else
      memcpy(next, cur, (size_t)ow);
    uint8_t* dst = g + (size_t)(oy + y) * stride + ox;
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

namespace {
void stem(const char* bookFile, const char* entry, uint32_t usize, char* out, int cap) {
  const uint32_t book = fnv(bookFile);
  const uint32_t pic = fnv(entry) ^ (usize * 2654435761u);
  snprintf(out, (size_t)cap, "/.toybox/art%d/%08lx/%08lx", PIPELINE, (unsigned long)book,
           (unsigned long)pic);
}

// One part: the 480x800 grey page `g`, dithered both ways and written.
bool writePart(ToolsHost& host, const uint8_t* g, const char* g2Path, const char* b1Path);
}  // namespace

void paths(const char* bookFile, const char* entry, uint32_t usize, int part, char* g2, char* b1,
           int cap) {
  char st[48];
  stem(bookFile, entry, usize, st, sizeof(st));
  snprintf(g2, (size_t)cap, "%s.%d.g2", st, part);
  snprintf(b1, (size_t)cap, "%s.%d.b1", st, part);
}

void infPath(const char* bookFile, const char* entry, uint32_t usize, char* inf, int cap) {
  char st[48];
  stem(bookFile, entry, usize, st, sizeof(st));
  snprintf(inf, (size_t)cap, "%s.inf", st);
}

namespace {
// `entry` names a picture inside the zip; null means the book's declared
// cover. Both stream through the same entry reader, so only opening differs.
bool openPic(epubc::Book& book, const char* entry) {
  return entry ? book.blobOpen(entry) : book.coverOpen();
}

// The whole picture, decoded to grey and levelled, into `src`. False when it
// cannot be (not JPEG/PNG, corrupt, too big for memory); `transient` is set
// only when memory said no, which a later try may not repeat.
bool decodePicture(epubc::Book& book, const char* entry, Src& src, bool* transient) {
  if (transient) *transient = false;
  uint8_t sig[8] = {};
  if (!openPic(book, entry)) return false;
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

  Ctx ctx{&book, &src, false};
  bool ok = false;
  if (!openPic(book, entry)) return false;
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
      if (!openPic(book, entry)) return false;
      ejprog::In pin{&ctx, blobRead};
      ok = ejprog::decodeGray(pin, W, H, PROG_BUDGET, rowsSize, rowsRow, &ctx);
      book.blobClose();
      if (!ok) {
        // Something the full decoder does not follow: its first scan alone,
        // the picture at an eighth -- soft, but the picture.
        src.release();
        ctx.sized = false;
        ctx.noMemory = false;
        if (!openPic(book, entry)) return false;
        ejdc::In in{&ctx, blobRead};
        ok = ejdc::decodeGray(in, rowsSize, rowsRow, &ctx);
        book.blobClose();
      }
      if (ctx.noMemory && transient) *transient = true;
    }
  } else {
    epng::In in{&ctx, blobRead};
    ok = epng::decodeGray(in, rowsSize, rowsRow, &ctx);
    book.blobClose();
    if (ctx.noMemory && transient) *transient = true;
  }
  if (!ok || !src.px) return false;
  src.flushRow();
  levels(src);
  return true;
}
}  // namespace

int prepare(ToolsHost& host, epubc::Book& book, const char* bookFile, const char* entry,
            uint32_t usize, bool* transient) {
  if (transient) *transient = false;
  Src src;
  if (!decodePicture(book, entry, src, transient)) return 0;

  // Landscape art, turned: worth it when turning makes the picture clearly
  // bigger on the panel (a near-square gains nothing and would only make the
  // reader turn the device for no reason).
  const float upright = fminf((float)W / src.w, (float)H / src.h);
  const float turned = fminf((float)H / src.w, (float)W / src.h);
  const bool landscape = turned > upright * 1.15f;
  // Its slices: full height, the panel's shape, evenly overlapped.
  int slices = 0;
  if (landscape) {
    const float sw = src.h * (float)W / H;
    slices = (int)ceilf(src.w / sw - 0.05f);
    if (slices < 2) slices = 2;
    if (slices > MAX_PARTS - 1) slices = MAX_PARTS - 1;
  }
  const int parts = 1 + slices;

  uint8_t* g = (uint8_t*)malloc((size_t)W * H);
  uint8_t* land = landscape ? (uint8_t*)malloc((size_t)W * H) : nullptr;
  if (!g || (landscape && !land)) {
    free(g);
    free(land);
    if (transient) *transient = true;
    return 0;
  }
  bool good = true;
  char g2[64], b1[64];
  for (int part = 0; part < parts && good; part++) {
    memset(g, 255, (size_t)W * H);
    int ox, oy, ow, oh;
    bool shrunk;
    // Gentler on an enlarged picture: there is no detail lost to a shrink to
    // bring back, only softness, and sharpening that further shows its halo.
    if (part == 0 && landscape) {
      // Fitted into an 800x480 landscape page, then turned a quarter clockwise:
      // landscape (lx, ly) lands at portrait (479 - ly, lx).
      memset(land, 255, (size_t)W * H);
      good = fit(src, 0, 0, src.w, src.h, land, H, W, ox, oy, ow, oh, shrunk);
      if (!good) break;
      sharpen(land, H, ox, oy, ow, oh, shrunk ? 8 : 5);
      for (int ly = 0; ly < W; ly++)
        for (int lx = 0; lx < H; lx++) g[(size_t)lx * W + (W - 1 - ly)] = land[(size_t)ly * H + lx];
    } else {
      int cx = 0, cw = src.w;
      if (part > 0) {
        cw = (int)(src.h * (float)W / H + 0.5f);
        if (cw > src.w) cw = src.w;
        cx = slices > 1 ? (int)((int64_t)(src.w - cw) * (part - 1) / (slices - 1)) : 0;
      }
      good = fit(src, cx, 0, cw, src.h, g, W, H, ox, oy, ow, oh, shrunk);
      if (!good) break;
      sharpen(g, W, ox, oy, ow, oh, shrunk ? 8 : 5);
    }
#ifdef EPUBART_DUMP  // the bench's view of the grey picture before dithering
    EPUBART_DUMP(g, W, H);
#endif
    paths(bookFile, entry, usize, part, g2, b1, sizeof(g2));
    good = writePart(host, g, g2, b1);
  }
  free(g);
  free(land);
  src.release();
  if (!good) {
    if (transient) *transient = true;  // memory or the card, not the picture
    return 0;
  }
  // Last: the part count, whose presence says every part is on the card.
  char inf[64];
  infPath(bookFile, entry, usize, inf, sizeof(inf));
  const uint8_t n = (uint8_t)parts;
  if (!host.sdStreamOpen(inf) || !host.sdStreamWrite(&n, 1) || !host.sdStreamClose(true)) {
    if (transient) *transient = true;
    return 0;
  }
  return parts;
}

namespace {
bool writePart(ToolsHost& host, const uint8_t* g, const char* g2Path, const char* b1Path) {
  // Four greys take the picture as it is; one bit goes through writeB1's
  // deeper curve (Atkinson drops a quarter of the error, which lightens the
  // light greys, and hair and fabric lines are light greys).
  static uint8_t toneG2[256];
  static bool made = false;
  if (!made) {
    for (int v = 0; v < 256; v++) toneG2[v] = (uint8_t)v;
    made = true;
  }
  return dither(host, g, toneG2, 4, g2Path) && writeB1(host, g, b1Path);
}
}  // namespace

bool coverGrey(epubc::Book& book, uint8_t* g, bool* transient) {
  if (transient) *transient = false;
  if (book.coverType() == epubc::Book::COVER_NONE) return false;
  Src src;
  if (!decodePicture(book, nullptr, src, transient)) return false;
  memset(g, 255, (size_t)W * H);
  int ox, oy, ow, oh;
  bool shrunk;
  if (!fit(src, 0, 0, src.w, src.h, g, W, H, ox, oy, ow, oh, shrunk)) {
    if (transient) *transient = true;
    return false;
  }
  sharpen(g, W, ox, oy, ow, oh, shrunk ? 8 : 5);
#ifdef EPUBART_DUMP
  EPUBART_DUMP(g, W, H);
#endif
  return true;
}

bool writeB1(ToolsHost& host, const uint8_t* g, const char* path) {
  static uint8_t tone[256];
  static bool made = false;
  if (!made) {
    for (int v = 0; v < 256; v++) tone[v] = (uint8_t)(255.0f * powf(v / 255.0f, 1.15f) + 0.5f);
    made = true;
  }
  return ditherAtkinson(host, g, tone, path);
}

}  // namespace epubart
