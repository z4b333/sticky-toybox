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

// What the loaded content family offers a reader, smallest first: one entry
// per distinct line height, and none of them smaller than the body size the
// firmware settled on. Filled by a content load, so the reader can step
// through a family's real sizes instead of through Toybox's four boxes.
constexpr int BODY_MIN = 24;
int g_sizeN = 0;
int g_sizeLine[MAX_SIZES] = {};
int g_sizePt[MAX_SIZES] = {};
int g_sizeFile[MAX_SIZES] = {};  // which of the family's files each one is
int g_sizeSel = -1;              // the one in the body box, or -1 for automatic

void forgetSizes() {
  g_sizeN = 0;
  g_sizeSel = -1;
}

// The number a size's file is named for: Bitter_12.cpfont is their 12. Read
// off the end of the bare name, because it is the number the person seeing
// their card's contents already knows the file by.
int ptOf(const char* file) {
  const char* dot = strrchr(file, '.');
  int end = dot ? (int)(dot - file) : (int)strlen(file);
  int start = end;
  while (start > 0 && file[start - 1] >= '0' && file[start - 1] <= '9') start--;
  if (start == end) return 0;
  int v = 0;
  for (int i = start; i < end; i++) v = v * 10 + (file[i] - '0');
  return v > 0 && v < 400 ? v : 0;
}

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
  // Whoever opened the browse session owns it. The reader lists the card's
  // families on its way in, while its own shelf session is up, and closing
  // that would take the bus out from under a shelf still listing books.
  const bool mine = !sdcard::browsing();
  if (mine && !sdcard::browseOpen()) return -1;
  char found[MAX_FAMILIES][sdcard::FONT_NAME_LEN];
  const int n = sdcard::fontFamilies(found, max);
  for (int i = 0; i < n && i < max; i++) snprintf(names[i], 32, "%s", found[i]);
  if (mine) sdcard::browseClose();
  return n;
}

namespace {
bool load(const char* family, bool content, int bodyLine) {
  if (!family || !family[0]) return false;
  const bool mine = !sdcard::browsing();
  if (mine && !sdcard::browseOpen()) return false;

  // Static, not stack: sixteen names and sixteen paths is 3.6 KB, and the
  // ESP32 task these run on has 8. Nothing here is reentrant -- one font is
  // chosen at a time, by a person, on a screen.
  static char files[MAX_SIZES][sdcard::FONT_FILE_LEN];
  const int n = sdcard::fontFiles(family, files, MAX_SIZES);
  if (n <= 0) {
    if (mine) sdcard::browseClose();
    return false;
  }

  int lines[MAX_SIZES] = {};
  static char paths[MAX_SIZES][160];
  memset(paths, 0, sizeof(paths));
  for (int i = 0; i < n; i++) {
    if (!sdcard::fontPath(family, files[i], paths[i], sizeof(paths[i]))) continue;
    lines[i] = lineOf(paths[i]);
  }

  // The sizes this family offers a reader, smallest first. Built here because
  // the headers have just been read and reading them again is a second of card
  // for numbers already in hand. An insertion sort over at most sixteen, and
  // a height already in the list is a duplicate cut of the same size -- two
  // files that draw the same line are one choice on a stepper.
  int order[MAX_SIZES], orderN = 0;
  for (int i = 0; i < n; i++) {
    if (lines[i] < BODY_MIN) continue;
    bool dup = false;
    for (int k = 0; k < orderN; k++)
      if (lines[order[k]] == lines[i]) dup = true;
    if (dup) continue;
    int at = orderN;
    while (at > 0 && lines[order[at - 1]] > lines[i]) {
      order[at] = order[at - 1];
      at--;
    }
    order[at] = i;
    orderN++;
  }

  // Which file the body box gets, and which the headings above it. -1 means
  // the automatic choice, which is every app but the reader and every family
  // with nothing to step through.
  int forceBody = -1, forceHead = -1;
  if (content && orderN >= 2) {
    // No size asked for means the body box, which is where the automatic
    // choice would have landed anyway on every family that ships a size near
    // it. Going through the list rather than around it is what lets a screen
    // say which size the words are in without guessing: the answer is always
    // one of the ones on offer.
    const int want = bodyLine > 0 ? bodyLine : BOXES[1];
    int best = 0;
    for (int k = 1; k < orderN; k++) {
      const int gap = lines[order[k]] > want ? lines[order[k]] - want : want - lines[order[k]];
      const int bg =
          lines[order[best]] > want ? lines[order[best]] - want : want - lines[order[best]];
      if (gap < bg) best = k;
    }
    forceBody = order[best];
    // A heading is one step up, and at the top of the family it is the body
    // size in bold -- the same answer the built-in faces give at their largest.
    forceHead = order[best + 1 < orderN ? best + 1 : best];
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
    // Boxes 1 and 2 are the reader's body and its headings. When a size was
    // asked for they are told, not measured; the other two are chosen the
    // automatic way so that the small print inside an app still has a face.
    int pick = pickFor(BOXES[b], files, n, lines);
    if (b == 1 && forceBody >= 0) pick = forceBody;
    if (b == 2 && forceHead >= 0) pick = forceHead;
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
  if (mine) sdcard::browseClose();

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
  if (!gfx::cardFaceLive(content)) {
    if (content) forgetSizes();
    return false;
  }
  snprintf(content ? g_content : g_universal, 32, "%s", family);
  if (content) {
    // Only worth offering when there is a choice: one usable size is not a
    // scale to step along, and the app keeps its own sizes instead.
    g_sizeN = orderN >= 2 ? orderN : 0;
    g_sizeSel = -1;
    for (int k = 0; k < g_sizeN; k++) {
      g_sizeLine[k] = lines[order[k]];
      g_sizePt[k] = ptOf(files[order[k]]);
      g_sizeFile[k] = order[k];
      // Where the stepper stands, including after an automatic load: the file
      // the body box ended up with is one of these, and a reader opening the
      // Text page should see the size it is already reading in.
      if (order[k] == (forceBody >= 0 ? forceBody : loadedFrom[1])) g_sizeSel = k;
    }
  }
  return true;
}
}  // namespace

bool useUniversal(const char* family) { return load(family, false, 0); }
bool useContent(const char* family, int bodyLine) { return load(family, true, bodyLine); }

int contentSizeCount() { return g_sizeN; }
int contentSizeLine(int i) { return (i >= 0 && i < g_sizeN) ? g_sizeLine[i] : 0; }
int contentSizePt(int i) { return (i >= 0 && i < g_sizeN) ? g_sizePt[i] : 0; }
int contentSizeIndex() { return g_sizeSel; }

void noneUniversal() {
  gfx::cardFaceClear(false);
  g_universal[0] = 0;
}

void noneContent() {
  gfx::cardFaceClear(true);
  g_content[0] = 0;
  forgetSizes();
}

const char* universal() { return g_universal; }
const char* content() { return g_content; }

}  // namespace cardfonts
