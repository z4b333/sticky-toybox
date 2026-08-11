// The cover pipeline. See tools/epub/epub_cover.h.
#include "tools/epub/epub_cover.h"

#include <stdlib.h>
#include <string.h>

#include "tools/book_thumbs.h"
#include "tools/epub/epub_jpegdc.h"
#include "tools/epub/epub_png.h"

extern "C" {
#include <tjpgd.h>
}

namespace epubcov {
namespace {

// The accumulator: every decoded pixel lands in one target cell; the cell
// averages when decoding ends. Sixteen-bit sums are safe because both
// decoders cap their output around 1400x2240, which keeps any cell under
// ~250 samples of at most 255 each.
struct Acc {
  uint16_t sum[bthumb::W * bthumb::H];
  uint16_t cnt[bthumb::W * bthumb::H];
  int srcW = 0, srcH = 0;   // decoded source size
  int outW = 0, outH = 0;   // the fitted box inside 96x160
  int xOff = 0, yOff = 0;

  void fit(int w, int h) {
    srcW = w;
    srcH = h;
    // largest box with the cover's aspect that fits the thumbnail
    if ((int64_t)w * bthumb::H >= (int64_t)h * bthumb::W) {
      outW = bthumb::W;
      outH = (int)(((int64_t)h * bthumb::W + w / 2) / w);
    } else {
      outH = bthumb::H;
      outW = (int)(((int64_t)w * bthumb::H + h / 2) / h);
    }
    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;
    xOff = (bthumb::W - outW) / 2;
    yOff = (bthumb::H - outH) / 2;
  }

  void add(int x, int y, uint8_t v) {
    const int tx = xOff + (int)((int64_t)x * outW / srcW);
    const int ty = yOff + (int)((int64_t)y * outH / srcH);
    const int i = ty * bthumb::W + tx;
    sum[i] = (uint16_t)(sum[i] + v);
    cnt[i]++;
  }

  void finish(uint8_t* gray) {
    for (int i = 0; i < bthumb::W * bthumb::H; i++)
      gray[i] = cnt[i] ? (uint8_t)(sum[i] / cnt[i]) : 255;  // white matte
  }
};

// --- JPEG ---------------------------------------------------------------------

struct JpegCtx {
  epubc::Book* book;
  Acc* acc;
  int scale;  // 2^scale divisor tjpgd applies before we see pixels
};

size_t jpegIn(JDEC* jd, uint8_t* buf, size_t n) {
  auto* ctx = (JpegCtx*)jd->device;
  if (buf) {
    const int got = ctx->book->coverRead(buf, (int)n);
    return got < 0 ? 0 : (size_t)got;
  }
  // a skip request: pull and drop
  uint8_t junk[64];
  size_t left = n;
  while (left) {
    const int take = left < sizeof(junk) ? (int)left : (int)sizeof(junk);
    const int got = ctx->book->coverRead(junk, take);
    if (got <= 0) return n - left;
    left -= (size_t)got;
  }
  return n;
}

int jpegOut(JDEC* jd, void* bitmap, JRECT* rect) {
  auto* ctx = (JpegCtx*)jd->device;
  const uint8_t* px = (const uint8_t*)bitmap;  // grayscale, JD_FORMAT 2
  for (int y = rect->top; y <= rect->bottom; y++)
    for (int x = rect->left; x <= rect->right; x++) ctx->acc->add(x, y, *px++);
  return 1;
}

// The DC-extractor callbacks, shared shape with the PNG path.
struct DcCtx {
  epubc::Book* book;
  Acc* acc;
};

int dcRead(void* pctx, uint8_t* dst, int n) {
  return ((DcCtx*)pctx)->book->coverRead(dst, n);
}

bool dcSize(void* uctx, int w, int h) {
  ((DcCtx*)uctx)->acc->fit(w, h);
  return true;
}

void dcRow(void* uctx, int y, const uint8_t* gray, int w) {
  Acc* acc = ((DcCtx*)uctx)->acc;
  for (int x = 0; x < w; x++) acc->add(x, y, gray[x]);
}

bool decodeJpeg(epubc::Book& book, Acc& acc) {
  constexpr size_t WORK = 6500;  // JD_FASTDECODE=1 wants ~3.5 KB; headroom costs little
  void* work = malloc(WORK);
  if (!work) return false;
  JpegCtx ctx{&book, &acc, 0};
  JDEC jd;
  const JRESULT prep = jd_prepare(&jd, jpegIn, work, WORK, &ctx);
  if (prep == JDR_OK) {
    // Baseline: tjpgd does the whole job. Scale big covers down before they
    // reach the accumulator: past ~1200 px wide the extra samples add
    // nothing a 96-px thumbnail can keep.
    int scale = 0;
    while (scale < 3 && ((jd.width >> scale) > 1200 || (jd.height >> scale) > 1920)) scale++;
    acc.fit(jd.width >> scale, jd.height >> scale);
    ctx.scale = scale;
    const JRESULT r = jd_decomp(&jd, jpegOut, (uint8_t)scale);
    free(work);
    return r == JDR_OK;
  }
  free(work);
  if (prep != JDR_FMT3) return false;  // corrupt, not merely progressive

  // Progressive (JDR_FMT3): commercial covers often are. Restart the entry
  // and stream just the first scan -- the DC coefficients -- which is the
  // image at 1/8 scale, still bigger than the thumbnail wants.
  book.coverClose();
  if (!book.coverOpen()) return false;
  DcCtx dctx{&book, &acc};
  ejdc::In in{&dctx, dcRead};
  return ejdc::decodeGray(in, dcSize, dcRow, &dctx);
}

// --- PNG ----------------------------------------------------------------------

struct PngCtx {
  epubc::Book* book;
  Acc* acc;
};

int pngRead(void* pctx, uint8_t* dst, int n) {
  return ((PngCtx*)pctx)->book->coverRead(dst, n);
}

bool pngSize(void* uctx, int w, int h) {
  ((PngCtx*)uctx)->acc->fit(w, h);
  return true;
}

void pngRow(void* uctx, int y, const uint8_t* gray, int w) {
  Acc* acc = ((PngCtx*)uctx)->acc;
  for (int x = 0; x < w; x++) acc->add(x, y, gray[x]);
}

}  // namespace

bool makeThumb(epubc::Book& book, const char* bookFile) {
  const int type = book.coverType();
  if (type == epubc::Book::COVER_NONE) return false;
  if (!book.coverOpen()) return false;

  Acc* acc = (Acc*)calloc(1, sizeof(Acc));
  if (!acc) {
    book.coverClose();
    return false;
  }

  bool ok = false;
  if (type == epubc::Book::COVER_JPEG) {
    ok = decodeJpeg(book, *acc);
  } else {
    PngCtx ctx{&book, acc};
    epng::In in{&ctx, pngRead};
    ok = epng::decodeGray(in, pngSize, pngRow, &ctx);
  }
  book.coverClose();

  if (ok) {
    uint8_t* gray = (uint8_t*)malloc(bthumb::W * bthumb::H);
    if (gray) {
      acc->finish(gray);
      bthumb::saveGray(bookFile, gray);
      free(gray);
    } else {
      ok = false;
    }
  }
  free(acc);
  return ok;
}

}  // namespace epubcov
