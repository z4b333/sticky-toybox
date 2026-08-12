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

  // The cover PIPELINE is not run here any more: it needs a ToolsHost (the
  // card, the flash store, the heap figures), which only the preview harness
  // has. `preview` covers it. This tool is about the zip, the word stream and
  // the artwork, which need nothing but the file.

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
    uint32_t words = 0, paras = 0, lastOff = 0, pics = 0, ready = 0;
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
      if (t == epubc::TOK_IMAGE) {
        // An illustration, and whether this book carries the picture the
        // device would actually draw: same entry, "toybox/" prefix, ".tbi".
        pics++;
        char entry[256];
        snprintf(entry, sizeof(entry), "toybox/%s", book.imageName());
        char* dot = strrchr(entry, '.');
        const char* slash = strrchr(entry, '/');
        if (dot && (!slash || dot > slash)) *dot = 0;
        strncat(entry, ".tbi", sizeof(entry) - strlen(entry) - 1);
        // The chapter stream is spent by opening another entry, so this run
        // reopens the chapter afterwards -- exactly as the reader does.
        const uint32_t here = off;
        bool ok = false;
        if (book.blobOpen(entry)) {
          uint8_t head[8];
          ok = book.blobSize() == 48008u && book.blobRead(head, 8) == 8 &&
               memcmp(head, "TBI1", 4) == 0 && head[4] == (480 & 255) && head[5] == (480 >> 8) &&
               head[6] == (800 & 255) && head[7] == (800 >> 8);
          book.blobClose();
        }
        if (ok) ready++;
        printf("       image %-52s %s\n", book.imageName(), ok ? "prepared" : "-- plate");
        // Replay to where we were, the way a turn off a picture does.
        if (!book.chapterOpen(s)) break;
        uint32_t o2 = 0;
        while (o2 < here) {
          const int t2 = book.next(w, o2);
          if (t2 == epubc::TOK_END || t2 == epubc::TOK_ERR) break;
        }
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
    printf("ch %02d: %6u words %5u paras %2u pics (%u prepared) %7.1f ms  %s  first '%s' last "
           "'%s'\n",
           s, words, paras, pics, ready, ms(t0, t1),
           mono ? "offsets ok" : "OFFSETS NOT MONOTONIC", first, last);
  }
  printf("total: %llu words, %.0f ms on this machine\n", (unsigned long long)totalWords,
         totalMs);
  return 0;
}
