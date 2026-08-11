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

int StickyHost::bookList(BookInfo* out, int max) {
  sdcard::BookMeta metas[8];
  const int n = sdcard::bookList(metas, max < 8 ? max : 8);
  for (int i = 0; i < (n < 0 ? 0 : n); i++) {
    strncpy(out[i].file, metas[i].file, sizeof(out[i].file) - 1);
    out[i].file[sizeof(out[i].file) - 1] = 0;
    strncpy(out[i].title, metas[i].title, sizeof(out[i].title) - 1);
    out[i].title[sizeof(out[i].title) - 1] = 0;
    out[i].pages = metas[i].pages;
    out[i].rtl = metas[i].rtl;
    out[i].bpp = metas[i].bpp;
  }
  return n;
}

bool StickyHost::bookOpen(const char* file) { return sdcard::bookOpen(file); }
bool StickyHost::bookPage(uint32_t idx, uint8_t* dst) { return sdcard::bookReadPage(idx, dst); }
void StickyHost::bookClose() { sdcard::bookClose(); }

int StickyHost::epubList(EpubInfo* out, int max) {
  sdcard::EpubMeta metas[8];
  const int n = sdcard::epubList(metas, max < 8 ? max : 8);
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
