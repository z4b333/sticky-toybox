#include "sticky_host.h"

#include "sdcard.h"
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

bool StickyHost::sdWallpaperTake(const char* name) {
  return sdcard::takeTbi(name, wallimg::PATH);
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
