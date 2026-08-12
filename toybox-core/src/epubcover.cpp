// The cover pipeline. See tools/epub/epub_cover.h.
//
// All three decode paths hand their output to one streaming builder, which
// never holds more than a band of rows -- see book_thumbs.h for why that
// matters. Nothing here knows what size the pictures come out; that is the
// builder's business.
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

struct Ctx {
  epubc::Book* book;
  bthumb::Builder* out;
  ToolsHost* host;
  const char* file;
  bool sized = false;
};

// --- JPEG, baseline (TJpgDec) -------------------------------------------------

size_t jpegIn(JDEC* jd, uint8_t* buf, size_t n) {
  auto* ctx = (Ctx*)jd->device;
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
  auto* ctx = (Ctx*)jd->device;
  ctx->out->block(rect->left, rect->top, rect->right - rect->left + 1,
                  rect->bottom - rect->top + 1, (const uint8_t*)bitmap);
  return 1;
}

// --- PNG and the progressive DC pass, both row at a time ----------------------

int streamRead(void* pctx, uint8_t* dst, int n) {
  return ((Ctx*)pctx)->book->coverRead(dst, n);
}

bool streamSize(void* uctx, int w, int h) {
  auto* ctx = (Ctx*)uctx;
  // Row-at-a-time decoders may enlarge: the progressive path only ever hands
  // back the image at an eighth of its size, and a cover floating small in
  // the middle of the panel is not what a loading screen is for.
  ctx->sized = ctx->out->begin(*ctx->host, ctx->file, w, h, 6);
  return ctx->sized;
}

void streamRow(void* uctx, int y, const uint8_t* gray, int w) {
  ((Ctx*)uctx)->out->row(y, gray, w);
}

}  // namespace

bool makeThumb(ToolsHost& host, epubc::Book& book, const char* bookFile, bool* transient) {
  if (transient) *transient = false;
  const int type = book.coverType();
  if (type == epubc::Book::COVER_NONE) return false;
  // A cover that will not open is the zip's business, not the heap's.
  if (!book.coverOpen()) return false;

  bthumb::Builder builder;
  Ctx ctx{&book, &builder, &host, bookFile, false};
  bool ok = false;

  if (type == epubc::Book::COVER_JPEG) {
    constexpr size_t WORK = 6500;  // JD_FASTDECODE=1 wants ~3.5 KB; headroom costs little
    void* work = malloc(WORK);
    if (!work) {
      if (transient) *transient = true;
      book.coverClose();
      return false;
    }
    JDEC jd;
    const JRESULT prep = jd_prepare(&jd, jpegIn, work, WORK, &ctx);
    if (prep == JDR_OK) {
      // Baseline: TJpgDec does the whole job. Its own scaler drops anything
      // enormous before it reaches us, which costs nothing and saves time.
      int scale = 0;
      while (scale < 3 && ((jd.width >> scale) > 1600 || (jd.height >> scale) > 2400)) scale++;
      if (builder.begin(host, bookFile, jd.width >> scale, jd.height >> scale)) {
        ok = jd_decomp(&jd, jpegOut, (uint8_t)scale) == JDR_OK;
        ok = ok && builder.finish();
        if (!ok) builder.abort();
      } else if (transient) {
        // The builder's bands are ~46 KB. Failing to get them is a bad
        // moment, not a bad picture.
        *transient = true;
      }
      free(work);
      book.coverClose();
      return ok;
    }
    free(work);
    if (prep != JDR_FMT3) {  // corrupt, not merely progressive
      book.coverClose();
      return false;
    }
    // Progressive: commercial covers usually are. The first scan is the DC
    // coefficients -- the image at 1/8 -- which is still larger than the
    // panel for any real cover.
    book.coverClose();
    if (!book.coverOpen()) return false;
    ejdc::In in{&ctx, streamRead};
    ok = ejdc::decodeGray(in, streamSize, streamRow, &ctx);
  } else {
    epng::In in{&ctx, streamRead};
    ok = epng::decodeGray(in, streamSize, streamRow, &ctx);
  }

  book.coverClose();
  if (ok && ctx.sized) {
    ok = builder.finish();
  } else {
    // ctx.sized false means Builder::begin refused, which on this device is
    // always about memory.
    if (!ctx.sized && transient) *transient = true;
    builder.abort();
  }
  return ok;
}

}  // namespace epubcov
