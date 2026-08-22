#include "sticky_host.h"

#include "cardfonts.h"

#include "sdcard.h"
#include "tools/book_thumbs.h"
#include "tools/epub/epubcore.h"
#include "tools/lock_image.h"

StickyHost stickyHost;

extern Preferences prefs;  // opened in main.cpp

Preferences& StickyHost::prefs() { return ::prefs; }

void StickyHost::beep(uint8_t kind) {
  switch (kind) {
    case 0: buzzer::tap(); break;
    case 1: buzzer::confirm(); break;
    case 2: buzzer::error(); break;
    default: buzzer::win(); break;
  }
}

void StickyHost::setSoundOn(bool on) {
  setSoundLevel(on ? (int)buzzer::Level::High : 0);
}

void StickyHost::setSoundLevel(int lv) {
  if (lv < 0) lv = 0;
  if (lv >= buzzer::LEVEL_COUNT) lv = buzzer::LEVEL_COUNT - 1;
  buzzer::setLevel((buzzer::Level)lv);
  ::prefs.putInt("sound_lv", lv);
  // The old on/off key stays written so a downgrade to a build that only knows
  // about the switch still comes up the way the device is set.
  ::prefs.putBool("sound", lv > 0);
}

int StickyHost::sdWallpapers(char names[][SD_NAME_LEN], int max) {
  static_assert(SD_NAME_LEN == 40, "sdcard::listTbi fills 40-byte names");
  return sdcard::listTbi(names, max);
}

int StickyHost::sdRecipes(char names[][RECIPE_NAME_LEN], int max) {
  static_assert(RECIPE_NAME_LEN == 64, "sdcard::listJson fills 64-byte names");
  return sdcard::listJson(names, max);
}

int StickyHost::sdTextFiles(const char* dir, char names[][RECIPE_NAME_LEN], int max) {
  static_assert(RECIPE_NAME_LEN == 64, "sdcard::listText fills 64-byte names");
  return sdcard::listText(dir, names, max);
}

namespace {
bool isBmpExt(const char* n) {
  const size_t len = strlen(n);
  return len > 4 && strcasecmp(n + len - 4, ".bmp") == 0;
}
}  // namespace

bool StickyHost::sdWallpaperTake(const char* name) {
  // A .bmp -- the format the CrossPoint/CrossInk/Xteink family trades art in
  // -- is dithered on the way through; the home screen stays 1-bit.
  if (isBmpExt(name)) return sdcard::takeBmp(name, wallimg::PATH, 2);
  return sdcard::takeTbi(name, wallimg::PATH);
}

bool StickyHost::sdLockTake(const char* name) {
  // The lock screen is where the panel's four greys are worth having: the
  // picture stands alone for hours. A .bmp becomes the 2bpp grey file; a
  // .tbi stays the finished 1-bit picture it already is. Either way the
  // OTHER file goes: one picture, whichever way it last arrived.
  if (isBmpExt(name)) {
    if (!sdcard::takeBmp(name, lockimg::G2_PATH, 4)) return false;
    tfs::remove(lockimg::PATH);
    return true;
  }
  if (!sdcard::takeTbi(name, lockimg::PATH)) return false;
  tfs::remove(lockimg::G2_PATH);
  return true;
}

// --- covers shared with CrossInk ----------------------------------------------
// CrossInk keeps a finished cover as cover.bmp inside the book's cache
// directory (/.crosspoint/epub_<hash>/) and decodes one only when it is
// missing. Reading theirs and leaving ours means a card moved between the
// two firmwares decodes each cover exactly once, in whichever device the
// book is opened first.

namespace {
// Any BMP on the card becomes this book's cover art: parsed to grey, then
// through the cover builder, which dithers, files both sizes and sweeps the
// stale flash copy -- exactly as if the JPEG decoder had produced these
// rows. One pipeline, whoever decoded; shared by the .cover.bmp sidecar and
// the CrossInk cache.
bool bmpToCover(StickyHost& h, const char* bmpPath, const char* file) {
  uint8_t* gray = (uint8_t*)ps_malloc((size_t)480 * 800);
  if (!gray) return false;
  bool ok = sdcard::readBmpGray(bmpPath, gray);
  if (ok) {
    bthumb::Builder b;
    ok = b.begin(h, file, 480, 800);
    for (int y = 0; ok && y < 800; y++) b.row(y, gray + (size_t)y * 480, 480);
    if (ok) ok = b.finish();
  }
  free(gray);
  return ok;
}
}  // namespace

bool StickyHost::coverFromBmp(const char* file) {
  // "<stem>.cover.bmp" beside the book -- the same address the legacy
  // .cover.tbi answers to, in the one format everything else already uses.
  char p[160];
  const char* dot = strrchr(file, '.');
  const char* slash = strrchr(file, '/');
  if (!dot || (slash && dot < slash)) return false;
  if (snprintf(p, sizeof(p), "%.*s.cover.bmp", (int)(dot - file), file) >= (int)sizeof(p))
    return false;
  if (!sdcard::exists(p)) return false;
  return bmpToCover(*this, p, file);
}

bool StickyHost::crossCoverGrab(const char* file) {
  char p[112];
  {
    char dir[64];
    epubc::cacheDir(file, dir, sizeof(dir));
    snprintf(p, sizeof(p), "%s/cover.bmp", dir);
    if (!sdcard::exists(p)) {
      epubc::cacheDirLegacy(file, dir, sizeof(dir));
      snprintf(p, sizeof(p), "%s/cover.bmp", dir);
      if (!sdcard::exists(p)) return false;
    }
  }
  return bmpToCover(*this, p, file);
}

bool StickyHost::crossCoverPut(const char* file) {
  // Mid-open only: the card is awake because a reader holds it. Never over a
  // cover that already exists -- theirs is as good as ours.
  if (!sdcard::busHeld()) return false;
  char p[112];
  {
    char dir[64];
    epubc::cacheDir(file, dir, sizeof(dir));
    snprintf(p, sizeof(p), "%s/cover.bmp", dir);
  }
  if (sdcard::exists(p)) return true;
  char big[48];
  bthumb::bigPath(file, big, sizeof(big));
  // A 1-bit BMP, the shape their converter writes: 62-byte header (file
  // header, BITMAPINFOHEADER, a black-then-white palette), then bottom-up
  // rows. At 480 wide a row is exactly 60 bytes -- already 4-aligned -- and
  // our bit sense IS the palette's: index 1 is white. So the pixel bytes are
  // the big cover's own, written back to front.
  constexpr uint32_t ROWB = 60, ROWS = 800, HDR = 62;
  constexpr uint32_t BAND_ROWS = 80, BAND = ROWB * BAND_ROWS;
  uint8_t* band = (uint8_t*)malloc(BAND);
  if (!band) return false;
  // Probe the big cover before opening the stream: a book whose full-size
  // cover was never written (or was dropped) should not leave a header-only
  // cover.bmp behind.
  if (sdcard::readSlice(big, 0, band, (int)ROWB) != (int)ROWB) {
    free(band);
    return false;
  }
  bool ok = sdcard::streamOpen(p);
  if (ok) {
    uint8_t h[HDR] = {0};
    h[0] = 'B'; h[1] = 'M';
    const uint32_t fileSize = HDR + ROWB * ROWS;
    h[2] = (uint8_t)fileSize; h[3] = (uint8_t)(fileSize >> 8);
    h[4] = (uint8_t)(fileSize >> 16); h[5] = (uint8_t)(fileSize >> 24);
    h[10] = HDR;              // offBits
    h[14] = 40;               // BITMAPINFOHEADER
    h[18] = (uint8_t)(480 & 255); h[19] = 480 >> 8;   // width
    h[22] = (uint8_t)(ROWS & 255); h[23] = ROWS >> 8;  // height, bottom-up
    h[26] = 1;                // planes
    h[28] = 1;                // bits per pixel
    const uint32_t img = ROWB * ROWS;
    h[34] = (uint8_t)img; h[35] = (uint8_t)(img >> 8); h[36] = (uint8_t)(img >> 16);
    h[46] = 2;                // colours used
    // palette: 0 = black (already zeroed), 1 = white
    h[58] = h[59] = h[60] = 255;
    ok = sdcard::streamWrite(h, HDR);
  }
  for (int b = (int)(ROWS / BAND_ROWS) - 1; ok && b >= 0; b--) {
    ok = sdcard::readSlice(big, (uint32_t)b * BAND, band, (int)BAND) == (int)BAND;
    for (int r = (int)BAND_ROWS - 1; ok && r >= 0; r--)
      ok = sdcard::streamWrite(band + (uint32_t)r * ROWB, ROWB);
  }
  free(band);
  return sdcard::streamClose(ok) && ok;
}

int StickyHost::shelfFolders(ShelfFolder* out, int max, const char* ext) {
  // Same shape on both sides, so one small buffer and a copy; folders are
  // few enough that this stays on the stack.
  sdcard::ShelfFolder fs[16];
  if (max > 16) max = 16;
  const int n = sdcard::shelfFolders(fs, max, ext);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].name, fs[i].name, sizeof(out[i].name) - 1);
    out[i].name[sizeof(out[i].name) - 1] = 0;
    out[i].count = fs[i].count;
  }
  return n;
}

namespace {
// A shelf's worth of metadata is several KB -- too much for the loop task's
// stack, and worse than useless on the heap: allocating and freeing it is
// exactly what fragmented the largest free block, immediately before the book
// reader asked for the one allocation in the firmware that needs a big
// contiguous run. It lives in BSS instead. Only one reader exists at a time,
// and neither of these calls is re-entrant.
constexpr int LIST_MAX = 32;
sdcard::BookMeta g_bookScratch[LIST_MAX];
sdcard::EpubMeta g_epubScratch[LIST_MAX];

// And the phone's file list, for a blunter reason. sdcard::FileEntry is 132
// bytes and MGR_MAX_FILES is 64, so this array is 8,448 bytes -- against a
// loop task stack of 8,192. It used to be a local, which meant the array
// alone overflowed the stack before the rest of the frame was even laid down,
// and the device rebooted the moment a phone loaded the file page and its
// script asked for /ls. Nothing about that failure looked like a stack: the
// WiFi came up, the page arrived, and then the panel restarted.
//
// The rule this one is here to keep: anything above about a kilobyte does not
// go on the stack in this firmware. files_web.h's own list was already static
// for exactly this reason; only the forwarder was not.
sdcard::FileEntry g_mgrScratch[sdcard::MGR_MAX_FILES];
static_assert(sizeof(g_mgrScratch) > 8192,
              "kept as a reminder: this array is bigger than the loop task's stack");
}  // namespace

int StickyHost::bookList(BookInfo* out, int max, const char* dir) {
  if (max < 1) return 0;
  if (max > LIST_MAX) max = LIST_MAX;
  sdcard::BookMeta* metas = g_bookScratch;
  const int n = sdcard::bookList(metas, max, dir);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].file, metas[i].file, sizeof(out[i].file) - 1);
    out[i].file[sizeof(out[i].file) - 1] = 0;
    strncpy(out[i].title, metas[i].title, sizeof(out[i].title) - 1);
    out[i].title[sizeof(out[i].title) - 1] = 0;
    out[i].pages = metas[i].pages;
    out[i].rtl = metas[i].rtl;
    out[i].bpp = metas[i].bpp;
    out[i].cover = metas[i].cover;
  }
  return n;
}

#ifndef TOYBOX_HOST
namespace {
// The open book, wearing the panel's GreySource interface. The card is
// already powered and mounted for the reading session, so a band is a seek
// and a read -- three passes over 96 KB is about a third of a second against
// a grey refresh that takes three.
struct CardGrey : Epd::GreySource {
  uint32_t page;
  explicit CardGrey(uint32_t p) : page(p) {}
  bool read(uint32_t off, uint8_t* dst, uint32_t n) override {
    return sdcard::bookReadPageSlice(page, off, dst, n);
  }
};
}  // namespace

bool StickyHost::bookShowGreyPaged(uint32_t idx) {
  CardGrey src(idx);
  return epd.displayGrey2bpp(src);
}
#else
bool StickyHost::bookShowGreyPaged(uint32_t idx) {
  (void)idx;
  return false;  // the preview harness has no waveform to drive
}
#endif

bool StickyHost::bookOpen(const char* file) { return sdcard::bookOpen(file); }
bool StickyHost::bookPage(uint32_t idx, uint8_t* dst) { return sdcard::bookReadPage(idx, dst); }
void StickyHost::bookClose() { sdcard::bookClose(); }

int StickyHost::epubList(EpubInfo* out, int max, const char* dir) {
  if (max < 1) return 0;
  if (max > LIST_MAX) max = LIST_MAX;
  sdcard::EpubMeta* metas = g_epubScratch;
  const int n = sdcard::epubList(metas, max, dir);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].file, metas[i].file, sizeof(out[i].file) - 1);
    out[i].file[sizeof(out[i].file) - 1] = 0;
    strncpy(out[i].title, metas[i].title, sizeof(out[i].title) - 1);
    out[i].title[sizeof(out[i].title) - 1] = 0;
    out[i].cont = metas[i].cont;
    strncpy(out[i].place, metas[i].place, sizeof(out[i].place) - 1);
    out[i].place[sizeof(out[i].place) - 1] = 0;
    out[i].page = metas[i].page;
    out[i].pageCount = metas[i].pageCount;
  }
  return n;
}

bool StickyHost::epubOpen(const char* path) { return sdcard::epubOpen(path); }
int StickyHost::epubRead(uint32_t pos, void* dst, uint32_t n) {
  return sdcard::epubRead(pos, dst, n);
}
uint32_t StickyHost::epubSize() { return sdcard::epubSize(); }
void StickyHost::epubClose() { sdcard::epubClose(); }
int StickyHost::sdReadFile(const char* path, void* dst, int max) {
  return sdcard::readFileAt(path, dst, max);
}
bool StickyHost::sdWriteFileAtomic(const char* path, const void* data, int n) {
  return sdcard::writeFileAtomic(path, data, n);
}

int StickyHost::sdMgrList(SdFile* out, int max) {
  sdcard::FileEntry* ents = g_mgrScratch;
  const int cap = max < sdcard::MGR_MAX_FILES ? max : sdcard::MGR_MAX_FILES;
  const int n = sdcard::mgrList(ents, cap);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].path, ents[i].path, sizeof(out[i].path) - 1);
    out[i].path[sizeof(out[i].path) - 1] = 0;
    out[i].size = ents[i].size;
  }
  return n;
}

// --- the reading face ---------------------------------------------------------
// The card work lives in cardfonts.cpp; this is only the seam core asks
// through. The chosen family is remembered in NVS, so a device that is turned
// off in a serif comes back in one.
int StickyHost::fontFamilies(char names[][FONT_FAMILY_LEN], int max) {
  return cardfonts::families(names, max);
}

bool StickyHost::fontUse(const char* family) {
  if (!cardfonts::useUniversal(family)) return false;
  prefs().putString("font_uni", family);
  return true;
}

void StickyHost::fontNone() {
  cardfonts::noneUniversal();
  prefs().remove("font_uni");
}

const char* StickyHost::fontChosen() const { return cardfonts::universal(); }
