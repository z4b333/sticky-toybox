#include "sdcard.h"

#include <string.h>

#include "tools/lock_image.h"
#include "tools/tiny_fs.h"

#ifndef TOYBOX_HOST
#include <SD.h>
#include <SPI.h>

#include "board_pins.h"
#include "epd.h"
#endif

namespace sdcard {

#ifdef TOYBOX_HOST

Report probe() {
  Report r;
  r.failedAt = "no card on a PC";
  return r;
}

// The harness gets two invented wallpapers, so the settings page can be
// rendered and its taps walked without a slot to put a card in.
int listTbi(char names[][40], int max) {
  static const char* kFake[2] = {"mountains.tbi", "night-sky.tbi"};
  int n = 0;
  for (; n < 2 && n < max; n++) {
    strncpy(names[n], kFake[n], 39);
    names[n][39] = 0;
  }
  return n;
}

bool takeTbi(const char* name, const char* destPath) {
  (void)name;
  // A real file lands, all white, so the home screen after the harness "copy"
  // draws through the same path a device would.
  static uint8_t img[tbimg::FILE_SIZE];
  img[0] = 'T'; img[1] = 'B'; img[2] = 'I'; img[3] = '1';
  img[4] = tbimg::W & 255; img[5] = tbimg::W >> 8;
  img[6] = tbimg::H & 255; img[7] = tbimg::H >> 8;
  memset(img + tbimg::HEADER, 0xFF, tbimg::BITS);
  return tfs::write(destPath, (const char*)img, sizeof(img));
}

#else

Report probe() {
  Report r;

  // The panel owns this bus. Its CS is left high by every one of its own
  // transactions, so the card is free to talk -- but only if nothing is
  // mid-refresh, because the SSD1677 does not release the bus while it is
  // being written to. Waiting for BUSY first is not politeness, it is the
  // difference between a clean read and two devices shouting.
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  // The card has no power until this line: the slot sits behind a load switch
  // on SD_PWR_EN, and the first hardware probe failed at "mount" for exactly
  // that reason -- right pins, right bus, no volts. 50 ms covers the card's
  // own power-up before it is asked anything.
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);
  delay(50);

  // 10 MHz, matching what the panel is driven at. A card will usually go much
  // faster; the shared traces are what they are, and a first answer of "yes"
  // at a conservative clock is worth more than a maybe at 40.
  if (!SD.begin(PIN_SD_CS, SPI, 10000000)) {
    r.failedAt = "mount";
    return r;
  }
  r.mounted = true;
  r.sizeMb = SD.cardSize() / (1024ULL * 1024ULL);

  File root = SD.open("/");
  if (root && root.isDirectory()) {
    for (File f = root.openNextFile(); f && r.files < 999; f = root.openNextFile()) r.files++;
  }

  // A real read of a real file, timed. The size of a .tbk page, because that is
  // the read this has to be fast enough for: a page turn already costs 1.7 s of
  // panel refresh, so anything under a tenth of that disappears into it.
  {
    File f = root ? SD.open("/") : File();
    (void)f;
  }
  File any;
  {
    File root2 = SD.open("/");
    for (File f = root2.openNextFile(); f; f = root2.openNextFile()) {
      if (!f.isDirectory() && f.size() >= 4096) {
        any = f;
        break;
      }
    }
  }
  if (!any) {
    r.failedAt = "no file over 4 KB to read";
  } else {
    static uint8_t buf[4096];
    const uint32_t want = any.size() > 48000 ? 48000 : any.size();
    const uint32_t t0 = millis();
    uint32_t got = 0;
    while (got < want) {
      const int n = any.read(buf, sizeof(buf));
      if (n <= 0) break;
      got += (uint32_t)n;
    }
    const uint32_t ms = millis() - t0;
    r.readOk = (got == want);
    r.readKbPerSec = ms ? (uint32_t)((uint64_t)got / ms) : 0;  // bytes/ms == KB/s
    if (!r.readOk) r.failedAt = "read";
  }

  SD.end();
  digitalWrite(PIN_SD_CS, HIGH);
  // Powered back down until the next probe. An unpowered card cannot lean on
  // the shared bus, and it costs nothing while nothing is reading.
  digitalWrite(PIN_SD_PWR, LOW);

  // ...and now the question that matters. If the card has left the panel
  // wedged, BUSY will not move for a soft reset, which is exactly the check
  // the driver already does at boot.
  epd.reinit();
  r.panelSurvived = epd.panelAnswered();
  if (!r.panelSurvived) r.failedAt = "the panel stopped answering afterwards";
  if (r.mounted && r.readOk && r.panelSurvived) r.failedAt = "";
  return r;
}

namespace {
// The card is powered for exactly as long as one call runs. Everything that
// borrows the bus goes through this pair, so nobody can forget the volts, the
// chip selects, or the panel afterwards.
bool busClaim() {
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);
  delay(50);
  return SD.begin(PIN_SD_CS, SPI, 10000000);
}

void busRelease() {
  SD.end();
  digitalWrite(PIN_SD_CS, HIGH);
  digitalWrite(PIN_SD_PWR, LOW);
  // The controller's RAM is not trusted after the bus has been shared, so the
  // caller's next refresh must be a full one.
  epd.reinit();
}

bool isTbiName(const char* n) {
  const size_t len = strlen(n);
  return len > 4 && strcasecmp(n + len - 4, ".tbi") == 0;
}

// Root first, then /wallpapers, so a card with a folder keeps its root tidy
// and a card without one still works.
const char* kDirs[2] = {"/", "/wallpapers"};
}  // namespace

int listTbi(char names[][40], int max) {
  if (!busClaim()) {
    busRelease();
    return -1;
  }
  int n = 0;
  for (const char* dir : kDirs) {
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) continue;
    for (File f = d.openNextFile(); f && n < max; f = d.openNextFile()) {
      if (f.isDirectory() || !isTbiName(f.name())) continue;
      // Wrong-sized files are left off the list entirely: a name that can only
      // fail when tapped is worse than no name.
      if (f.size() != tbimg::FILE_SIZE) continue;
      const char* bare = strrchr(f.name(), '/');
      strncpy(names[n], bare ? bare + 1 : f.name(), 39);
      names[n][39] = 0;
      n++;
    }
  }
  busRelease();
  return n;
}

bool takeTbi(const char* name, const char* destPath) {
  if (!busClaim()) {
    busRelease();
    return false;
  }
  bool ok = false;
  for (const char* dir : kDirs) {
    char path[64];
    snprintf(path, sizeof(path), "%s%s%s", dir, dir[1] ? "/" : "", name);
    File src = SD.open(path, FILE_READ);
    if (!src || src.isDirectory()) continue;
    if (src.size() != tbimg::FILE_SIZE) {
      src.close();
      continue;
    }
    tfs::begin();
    File dst = LittleFS.open(destPath, "w");
    if (!dst) {
      src.close();
      break;
    }
    static uint8_t buf[4096];
    uint32_t moved = 0;
    for (;;) {
      const int got = src.read(buf, sizeof(buf));
      if (got <= 0) break;
      if (dst.write(buf, got) != (size_t)got) break;
      moved += (uint32_t)got;
    }
    src.close();
    dst.close();
    ok = (moved == tbimg::FILE_SIZE);
    // Half a picture is worse than none: it would draw as a photo that turns
    // to noise partway down the panel.
    if (!ok) LittleFS.remove(destPath);
    break;
  }
  busRelease();
  return ok;
}

#endif

}  // namespace sdcard
