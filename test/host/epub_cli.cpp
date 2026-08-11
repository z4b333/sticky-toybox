// A command-line run of the EPUB core against a real book on disk: the zip,
// the OPF, the word stream, the cover pipeline -- everything except a screen.
// Build (from test/host):
//   g++ -std=gnu++17 -O2 -w -DTOYBOX_HOST -I . -I mock -I ../../src \
//     -I ../../toybox-core/src -I ../../lib/miniz/src -I ../../lib/tjpgd/src \
//     epub_cli.cpp ../../toybox-core/src/epubcore.cpp ../../toybox-core/src/epubcover.cpp \
//     ../../lib/miniz/src/toybox_miniz_impl.c ../../lib/tjpgd/src/tjpgd.c -o epub_cli
//   ./epub_cli book.epub
#include <chrono>
#include <cstdio>
#include <cstring>

#include "tools/book_thumbs.h"
#include "tools/epub/epub_cover.h"
#include "tools/epub/epubcore.h"

namespace {

struct FileIO : epubc::IO {
  FILE* f = nullptr;
  uint32_t sz = 0;
  int read(uint32_t pos, void* dst, uint32_t n) override {
    if (fseek(f, (long)pos, SEEK_SET) != 0) return -1;
    return (int)fread(dst, 1, n, f);
  }
  uint32_t size() override { return sz; }
};

double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: epub_cli <book.epub>\n");
    return 2;
  }
  FileIO io;
  io.f = fopen(argv[1], "rb");
  if (!io.f) {
    printf("cannot open %s\n", argv[1]);
    return 2;
  }
  fseek(io.f, 0, SEEK_END);
  io.sz = (uint32_t)ftell(io.f);

  epubc::Book book;
  auto t0 = std::chrono::steady_clock::now();
  if (!book.open(io)) {
    printf("open FAILED: %s\n", book.error());
    return 1;
  }
  auto t1 = std::chrono::steady_clock::now();
  printf("open ok: %d spine chapters, %.1f ms  (%u bytes)\n", book.spineCount(), ms(t0, t1),
         io.sz);
  printf("cover: type %d (0 none, 1 jpeg, 2 png), %u bytes compressed source\n",
         book.coverType(), book.coverSize());

  // the cover pipeline, into the fake flash store, dumped as a PGM
  t0 = std::chrono::steady_clock::now();
  const bool cov = epubcov::makeThumb(book, "/books/cli.epub");
  t1 = std::chrono::steady_clock::now();
  printf("cover thumb: %s, %.1f ms\n", cov ? "ok" : "FAILED", ms(t0, t1));
  if (cov) {
    uint8_t bits[bthumb::BYTES];
    if (bthumb::load("/books/cli.epub", bits)) {
      FILE* out = fopen("cover_thumb.pgm", "wb");
      fprintf(out, "P5\n%d %d\n255\n", bthumb::W, bthumb::H);
      for (int y = 0; y < bthumb::H; y++)
        for (int x = 0; x < bthumb::W; x++) {
          const uint8_t v =
              (bits[(size_t)y * (bthumb::W / 8) + (x >> 3)] & (0x80 >> (x & 7))) ? 255 : 0;
          fwrite(&v, 1, 1, out);
        }
      fclose(out);
      printf("  wrote cover_thumb.pgm\n");
    }
  }

  // every chapter: word count, offsets monotonic, timing; print a taste
  char w[epubc::WORD_CAP];
  uint32_t off = 0;
  uint64_t totalWords = 0;
  double totalMs = 0;
  for (int s = 0; s < book.spineCount(); s++) {
    if (!book.chapterOpen(s)) {
      printf("ch %02d: OPEN FAILED (%s)\n", s, book.error());
      continue;
    }
    uint32_t words = 0, paras = 0, lastOff = 0;
    bool mono = true;
    char first[epubc::WORD_CAP] = "", last[epubc::WORD_CAP] = "";
    t0 = std::chrono::steady_clock::now();
    for (;;) {
      const int t = book.next(w, off);
      if (t == epubc::TOK_END) break;
      if (t == epubc::TOK_ERR) {
        printf("ch %02d: STREAM ERROR mid-chapter\n", s);
        break;
      }
      if (t == epubc::TOK_PARA) {
        paras++;
        continue;
      }
      if (off < lastOff) mono = false;
      lastOff = off;
      if (first[0] == 0) strcpy(first, w);
      strcpy(last, w);
      words++;
    }
    t1 = std::chrono::steady_clock::now();
    totalWords += words;
    totalMs += ms(t0, t1);
    printf("ch %02d: %6u words %5u paras  %7.1f ms  %s  first '%s' last '%s'\n", s, words,
           paras, ms(t0, t1), mono ? "offsets ok" : "OFFSETS NOT MONOTONIC", first, last);
  }
  printf("total: %llu words, %.0f ms on this machine\n", (unsigned long long)totalWords,
         totalMs);
  return 0;
}
