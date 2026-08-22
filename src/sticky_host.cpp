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

// --- the faces, device and per app --------------------------------------------
// The card work lives in cardfonts.cpp; this is the seam core asks through,
// plus the five NVS keys. A device turned off in a serif comes back in one,
// and so does a book.
namespace {
// Short keys, because NVS names are capped at 15 characters and these five
// have to stay stable: renaming one loses somebody's choice.
const char* kFontKey[ToolsHost::FONT_SLOTS] = {"font_uni", "font_ep", "font_nt", "font_fc",
                                               "font_rc"};
char g_fontName[ToolsHost::FONT_SLOTS][ToolsHost::FONT_FAMILY_LEN] = {};
bool g_fontRead[ToolsHost::FONT_SLOTS] = {};

// Which slot the open app is drawing from, so turning its face on knows which
// baked cut to ask for as well as which card family.
int g_fontSlotOpen = -1;

// The card's families, remembered from the last successful listing. Inside an
// app the card's bus belongs to that app -- a reader has a book open on it --
// and asking again would answer "no card" to a device with a card plainly in
// it. The reader lists them on its way in, while the bus is still free, and
// this is what it reads back from.
char g_famCache[cardfonts::MAX_FAMILIES][ToolsHost::FONT_FAMILY_LEN] = {};
int g_famCached = -1;

// The size an app's words are wanted at, kept beside the family name: a book
// left at the family's 12 pt opens at 12 pt tomorrow. Stored as the line
// height in pixels rather than as a position in a list, because a list moves
// when somebody adds a file to the card and a line height does not.
//
// Separate keys from the family ones, and short: NVS caps a name at fifteen
// characters.
const char* kSizeKey[ToolsHost::FONT_SLOTS] = {"fsz_uni", "fsz_ep", "fsz_nt", "fsz_fc", "fsz_rc"};
int g_fontSize[ToolsHost::FONT_SLOTS] = {};
bool g_sizeRead[ToolsHost::FONT_SLOTS] = {};
// What the resident content family was read with, so that re-entering an app
// with the same face and the same size does not read megabytes again.
int g_contentWant = 0;
}  // namespace

int StickyHost::fontFamilies(char names[][FONT_FAMILY_LEN], int max) {
  const int n = cardfonts::families(names, max);
  if (n >= 0) {
    g_famCached = n < cardfonts::MAX_FAMILIES ? n : cardfonts::MAX_FAMILIES;
    for (int i = 0; i < g_famCached; i++) snprintf(g_famCache[i], FONT_FAMILY_LEN, "%s", names[i]);
    return n;
  }
  // -1 means the card did not answer, which is two different things: there is
  // no card, or somebody else is holding its bus. Only the second deserves the
  // remembered list -- handing back a stale one for a card that has been taken
  // out would offer families that are not there.
  if (sdcard::busHeld() && g_famCached >= 0) {
    const int m = g_famCached < max ? g_famCached : max;
    for (int i = 0; i < m; i++) snprintf(names[i], FONT_FAMILY_LEN, "%s", g_famCache[i]);
    return m;
  }
  return -1;
}

// Which built-in a stored name means, or -1 for a card family. "" is the first
// one, which is the face this firmware has always drawn in.
static int builtInIndex(const StickyHost& h, const char* name) {
  if (!name || !name[0]) return 0;
  for (int i = 0; i < h.typefaceCount(); i++)
    if (strcmp(h.typefaceName(i), name) == 0) return i;
  return -1;
}

const char* StickyHost::fontFor(int slot) const {
  if (slot < 0 || slot >= FONT_SLOTS) return "";
  // Read once and cached: this is asked while drawing a settings row, and NVS
  // reads are not free.
  if (!g_fontRead[slot]) {
    g_fontRead[slot] = true;
    const_cast<StickyHost*>(this)->prefs().getString(kFontKey[slot], g_fontName[slot],
                                                     FONT_FAMILY_LEN);
  }
  return g_fontName[slot];
}

bool StickyHost::fontSet(int slot, const char* family) {
  if (slot < 0 || slot >= FONT_SLOTS) return false;
  const int built = builtInIndex(*this, family);
  // The first built-in is stored as "", not as its name. One canonical value
  // for "the face this firmware has always drawn in" means a screen asking
  // "is anything chosen here?" gets one answer rather than two spellings of
  // the same one.
  const bool none = built == 0;
  // The device's own face is loaded the moment it is chosen, because the
  // screen the choice was made on is drawn in it. An app's face is only
  // remembered here; it loads when the app opens.
  if (slot == FONT_DEVICE) {
    if (none || built >= 0) {
      cardfonts::noneUniversal();
      gfx::setTypeface(built > 0 ? built : 0);
    } else if (!cardfonts::useUniversal(family)) {
      return false;
    }
  } else if (!none && built < 0) {
    // Checked now rather than at the door of the app: being told "that card
    // has no such family" belongs where the choice is made.
    char names[cardfonts::MAX_FAMILIES][FONT_FAMILY_LEN];
    const int n = cardfonts::families(names, cardfonts::MAX_FAMILIES);
    bool there = false;
    for (int i = 0; i < n; i++)
      if (strcmp(names[i], family) == 0) there = true;
    if (!there) return false;
  }
  snprintf(g_fontName[slot], FONT_FAMILY_LEN, "%s", none ? "" : family);
  g_fontRead[slot] = true;
  if (none)
    prefs().remove(kFontKey[slot]);
  else
    prefs().putString(kFontKey[slot], family);
  return true;
}

bool StickyHost::fontEnter(int slot) {
  g_fontSlotOpen = slot;
  const char* fam = fontFor(slot);
  // A built-in face needs no card at all: it is already in the firmware, and
  // turning the app's face on is a matter of asking for the right cut.
  if (!fam[0] || builtInIndex(*this, fam) >= 0) {
    cardfonts::noneContent();
    return false;
  }
  const int want = fontSizeWanted(slot);
  // Already the resident content family, at the size it is wanted in: opening
  // the same book twice should not read four megabytes twice.
  if (strcmp(cardfonts::content(), fam) == 0 && g_contentWant == want) return true;
  if (!cardfonts::useContent(fam, want)) return false;
  g_contentWant = want;
  return true;
}

void StickyHost::fontLeave() {
  cardfonts::noneContent();
  g_fontSlotOpen = -1;
  g_contentWant = 0;
}

// The stored size for a slot, in pixels of line, or 0 for "whichever of the
// family's sizes lands closest to the firmware's own box".
int StickyHost::fontSizeWanted(int slot) {
  if (slot < 0 || slot >= FONT_SLOTS) return 0;
  if (!g_sizeRead[slot]) {
    g_sizeRead[slot] = true;
    g_fontSize[slot] = (int)prefs().getUInt(kSizeKey[slot], 0);
  }
  return g_fontSize[slot];
}

int StickyHost::faceSizeCount() { return cardfonts::contentSizeCount(); }
int StickyHost::faceSizeLine(int i) { return cardfonts::contentSizeLine(i); }
int StickyHost::faceSizePt(int i) { return cardfonts::contentSizePt(i); }
int StickyHost::faceSize() { return cardfonts::contentSizeIndex(); }

bool StickyHost::faceSizeSet(int i) {
  if (g_fontSlotOpen < 0 || g_fontSlotOpen >= FONT_SLOTS) return false;
  const int line = cardfonts::contentSizeLine(i);
  if (line <= 0) return false;
  const char* fam = fontFor(g_fontSlotOpen);
  if (!fam[0]) return false;
  // The family is read again, because a size is a different set of files. It
  // costs what opening the app costs, and it fails the same way: the face that
  // was working is left alone.
  if (!cardfonts::useContent(fam, line)) return false;
  g_contentWant = line;
  g_fontSize[g_fontSlotOpen] = line;
  g_sizeRead[g_fontSlotOpen] = true;
  prefs().putUInt(kSizeKey[g_fontSlotOpen], (uint32_t)line);
  return true;
}

void StickyHost::fontContent(bool on) {
  gfx::contentFace(on);
  // The baked cut belongs to the same choice as the card family: an app set to
  // Literata is set to Literata whether or not a card is in the slot. Off puts
  // the device's own face back, which is the state every firmware screen draws
  // in.
  if (!on) {
    gfx::setTypeface(builtInIndex(*this, fontFor(FONT_DEVICE)) > 0 ? 1 : 0);
    return;
  }
  const int built = g_fontSlotOpen >= 0 ? builtInIndex(*this, fontFor(g_fontSlotOpen)) : 0;
  gfx::setTypeface(built > 0 ? built : 0);
}
