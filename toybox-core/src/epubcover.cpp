// The cover pipeline. See tools/epub/epub_cover.h.
//
// Since v2 a cover is decoded exactly the way the book's illustrations are
// (epub_art.h): the whole picture, progressive JPEGs included, levelled,
// fitted by area, sharpened, then Atkinson to one bit. Before, a progressive
// cover -- which is most retail covers -- only ever yielded its first scan,
// the picture at an eighth, blown back up and Floyd-Steinberg dithered: a
// face became a smudge and the credit text became grain.
#include "tools/epub/epub_cover.h"

#include <stdlib.h>
#include <string.h>

#include "tools/book_thumbs.h"
#include "tools/epub/epub_art.h"

namespace epubcov {

bool makeThumb(ToolsHost& host, epubc::Book& book, const char* bookFile, bool* transient) {
  if (transient) *transient = false;
  if (book.coverType() == epubc::Book::COVER_NONE) return false;

  uint8_t* g = (uint8_t*)malloc((size_t)epubart::W * epubart::H);  // 384 KB, PSRAM
  if (!g) {
    if (transient) *transient = true;
    return false;
  }
  bool memory = false;
  bool ok = epubart::coverGrey(book, g, &memory);
  if (ok) {
    bthumb::sweepStale(bookFile);
    char big[bstore::PATH_CAP];
    bthumb::bigPath(bookFile, big, sizeof(big));
    // The loading-screen cover first: the strip thumbnail is only written
    // after it, so "have the small one" still implies the big one is there.
    ok = epubart::writeB1(host, g, big);
    if (ok) {
      // The strip thumbnail, averaged 5x5 out of the same grey page -- the
      // panel-sized page is exactly five times the thumbnail each way.
      static_assert(epubart::W == bthumb::W * bthumb::SCALE, "thumbnail scale");
      static_assert(epubart::H == bthumb::H * bthumb::SCALE, "thumbnail scale");
      uint8_t* small = (uint8_t*)malloc((size_t)bthumb::W * bthumb::H);
      if (small) {
        constexpr int S = bthumb::SCALE;
        for (int y = 0; y < bthumb::H; y++)
          for (int x = 0; x < bthumb::W; x++) {
            int sum = 0;
            for (int dy = 0; dy < S; dy++) {
              const uint8_t* r = g + (size_t)(y * S + dy) * epubart::W + x * S;
              for (int dx = 0; dx < S; dx++) sum += r[dx];
            }
            small[(size_t)y * bthumb::W + x] = (uint8_t)(sum / (S * S));
          }
        char sp[20];
        bthumb::path(bookFile, sp, sizeof(sp));
        ok = bthumb::saveSmallGrey(small, sp);
        free(small);
        if (ok) bthumb::sweepOldCovers(host, bookFile);
      } else {
        ok = false;
      }
    }
    // The picture decoded; what failed was the card or the heap.
    if (!ok) memory = true;
  }
  free(g);
  if (transient) *transient = memory;
  return ok;
}

}  // namespace epubcov
