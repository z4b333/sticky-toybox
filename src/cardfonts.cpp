#include "cardfonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "sdcard.h"
#include "tools/cpfont.h"

#ifndef TOYBOX_HOST
#include <esp_heap_caps.h>
#endif

namespace cardfonts {
namespace {

char g_universal[32] = "";
char g_content[32] = "";

// The boxes Toybox draws in, and the order they are filled. Every one that
// finds no reasonable file is simply left baked, which is why a family that
// only ships small sizes still works for body text and leaves headings alone.
constexpr int BOXES[4] = {18, 24, 32, 44};

// A file's own idea of its line height, from the header and the first style's
// table of contents -- 96 bytes, not the megabyte behind them. Reading every
// size of a family whole just to compare four numbers would be a second and a
// half of card for nothing.
int lineOf(const char* path) {
  uint8_t head[96];
  const int n = sdcard::readFileAt(path, head, sizeof(head));
  if (n < 64) return 0;
  cpfont::Font probe;
  // open() checks the magic and the version, and refuses a file whose sections
  // run past its length -- which every file does here, since only the head was
  // read. So the header fields are read directly instead: this is a peek, not
  // an open.
  (void)probe;
  if (memcmp(head, "CPFONT\0\0", 8) != 0) return 0;
  uint16_t version;
  memcpy(&version, head + 8, 2);
  if (version != cpfont::VERSION) return 0;
  const int styles = head[12];
  if (styles < 1 || styles > cpfont::MAX_STYLES) return 0;
  return head[32 + 12];  // advanceY of the first style, from its TOC entry
}

// Which file belongs in a box. Closest line height wins, and a tie goes to the
// smaller file: text that overruns its box collides with the line beneath it,
// while text a pixel short of it just sits a little airy.
//
// A file more than a third away from the box is not used at all. A family that
// only ships 18 pt would otherwise put a 45 px line into an 18 px box and every
// small caption on the device would eat the one above it.
int pickFor(int box, char files[][48], int n, const int* lines) {
  int best = -1, bestGap = 0;
  for (int i = 0; i < n; i++) {
    if (lines[i] <= 0) continue;
    const int gap = lines[i] > box ? (lines[i] - box) * 2 : (box - lines[i]);
    if (lines[i] > box + box / 3) continue;
    if (best < 0 || gap < bestGap || (gap == bestGap && lines[i] < lines[best])) {
      best = i;
      bestGap = gap;
    }
  }
  (void)files;
  return best;
}

uint8_t* bigAlloc(uint32_t n) {
#ifndef TOYBOX_HOST
  // PSRAM: a family's four sizes are a few megabytes, and the internal heap is
  // where the framebuffer and every app's working memory live.
  if (uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM)) return p;
#endif
  return (uint8_t*)malloc(n);
}

}  // namespace

int families(char names[][32], int max) {
  if (max > MAX_FAMILIES) max = MAX_FAMILIES;
  if (!sdcard::browseOpen()) return -1;
  char found[MAX_FAMILIES][sdcard::FONT_NAME_LEN];
  const int n = sdcard::fontFamilies(found, max);
  for (int i = 0; i < n && i < max; i++) snprintf(names[i], 32, "%s", found[i]);
  sdcard::browseClose();
  return n;
}

namespace {
bool load(const char* family, bool content) {
  if (!family || !family[0]) return false;
  if (!sdcard::browseOpen()) return false;

  // Static, not stack: sixteen names and sixteen paths is 3.6 KB, and the
  // ESP32 task these run on has 8. Nothing here is reentrant -- one font is
  // chosen at a time, by a person, on a screen.
  static char files[MAX_SIZES][sdcard::FONT_FILE_LEN];
  const int n = sdcard::fontFiles(family, files, MAX_SIZES);
  if (n <= 0) {
    sdcard::browseClose();
    return false;
  }

  int lines[MAX_SIZES] = {};
  static char paths[MAX_SIZES][160];
  memset(paths, 0, sizeof(paths));
  for (int i = 0; i < n; i++) {
    if (!sdcard::fontPath(family, files[i], paths[i], sizeof(paths[i]))) continue;
    lines[i] = lineOf(paths[i]);
  }

  // Everything is read before anything is installed. A half-installed family --
  // two boxes from the card, two baked -- is a page in two fonts, and the way
  // it happens is a card pulled out halfway through.
  uint8_t* blobs[4] = {};
  uint32_t sizes[4] = {};
  bool ownsBlob[4] = {};
  bool any = false;
  int loadedFrom[4] = {-1, -1, -1, -1};
  for (int b = 0; b < 4; b++) {
    const int pick = pickFor(BOXES[b], files, n, lines);
    if (pick < 0) continue;
    // The same file often wins two neighbouring boxes -- 8 pt suits both the
    // 18 and the 24 px box on most faces. Read once, shared, freed once.
    for (int prev = 0; prev < b; prev++) {
      if (loadedFrom[prev] == pick && blobs[prev]) {
        blobs[b] = blobs[prev];
        sizes[b] = sizes[prev];
        ownsBlob[b] = false;
        loadedFrom[b] = pick;
        break;
      }
    }
    if (blobs[b]) {
      any = true;
      continue;
    }
    const int size = sdcard::fileSize(paths[pick]);
    if (size <= 0) continue;
    uint8_t* blob = bigAlloc((uint32_t)size);
    if (!blob) continue;  // out of PSRAM: this box stays baked
    const int got = sdcard::readFileAt(paths[pick], blob, size);
    if (got != size) {
      free(blob);
      continue;
    }
    blobs[b] = blob;
    sizes[b] = (uint32_t)size;
    ownsBlob[b] = true;
    loadedFrom[b] = pick;
    any = true;
  }
  sdcard::browseClose();

  if (!any) {
    for (int b = 0; b < 4; b++)
      if (ownsBlob[b]) free(blobs[b]);
    return false;
  }

  // Install, and let gfx refuse anything that is not a font it can read. Only
  // now is the old family let go.
  gfx::cardFaceClear(content);
  for (int b = 0; b < 4; b++) {
    if (!blobs[b]) continue;
    // Whoever read the file owns the bytes; a box that shares them does not
    // free them. One file often wins two neighbouring boxes, and freeing it
    // twice is the kind of crash that only happens on somebody else's card.
    const bool owns = ownsBlob[b];
    if (!gfx::cardFaceSet(BOXES[b], blobs[b], sizes[b], content, owns) && owns) free(blobs[b]);
  }
  if (!gfx::cardFaceLive(content)) return false;
  snprintf(content ? g_content : g_universal, 32, "%s", family);
  return true;
}
}  // namespace

bool useUniversal(const char* family) { return load(family, false); }
bool useContent(const char* family) { return load(family, true); }

void noneUniversal() {
  gfx::cardFaceClear(false);
  g_universal[0] = 0;
}

void noneContent() {
  gfx::cardFaceClear(true);
  g_content[0] = 0;
}

const char* universal() { return g_universal; }
const char* content() { return g_content; }

}  // namespace cardfonts
