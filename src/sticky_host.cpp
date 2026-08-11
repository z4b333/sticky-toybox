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

int StickyHost::bookList(BookInfo* out, int max, const char* dir) {
  // A shelf's worth of metadata is several KB, which is more than the loop
  // task's stack should be asked to hold; it lives on the heap for the one
  // call and goes straight back.
  if (max < 1) return 0;
  sdcard::BookMeta* metas = (sdcard::BookMeta*)malloc(sizeof(sdcard::BookMeta) * (size_t)max);
  if (!metas) return -1;
  const int n = sdcard::bookList(metas, max, dir);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].file, metas[i].file, sizeof(out[i].file) - 1);
    out[i].file[sizeof(out[i].file) - 1] = 0;
    strncpy(out[i].title, metas[i].title, sizeof(out[i].title) - 1);
    out[i].title[sizeof(out[i].title) - 1] = 0;
    out[i].pages = metas[i].pages;
    out[i].rtl = metas[i].rtl;
    out[i].bpp = metas[i].bpp;
  }
  free(metas);
  return n;
}

bool StickyHost::bookOpen(const char* file) { return sdcard::bookOpen(file); }
bool StickyHost::bookPage(uint32_t idx, uint8_t* dst) { return sdcard::bookReadPage(idx, dst); }
void StickyHost::bookClose() { sdcard::bookClose(); }

int StickyHost::epubList(EpubInfo* out, int max, const char* dir) {
  if (max < 1) return 0;
  sdcard::EpubMeta* metas = (sdcard::EpubMeta*)malloc(sizeof(sdcard::EpubMeta) * (size_t)max);
  if (!metas) return -1;
  const int n = sdcard::epubList(metas, max, dir);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].file, metas[i].file, sizeof(out[i].file) - 1);
    out[i].file[sizeof(out[i].file) - 1] = 0;
    strncpy(out[i].title, metas[i].title, sizeof(out[i].title) - 1);
    out[i].title[sizeof(out[i].title) - 1] = 0;
    out[i].cont = metas[i].cont;
  }
  free(metas);
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
  sdcard::FileEntry ents[sdcard::MGR_MAX_FILES];
  const int cap = max < sdcard::MGR_MAX_FILES ? max : sdcard::MGR_MAX_FILES;
  const int n = sdcard::mgrList(ents, cap);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].path, ents[i].path, sizeof(out[i].path) - 1);
    out[i].path[sizeof(out[i].path) - 1] = 0;
    out[i].size = ents[i].size;
  }
  return n;
}
