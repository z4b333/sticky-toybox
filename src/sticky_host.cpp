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
