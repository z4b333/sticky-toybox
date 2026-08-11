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

namespace {
// Two invented volumes. Page k is a frame, a diagonal, and k+1 tally bars --
// cheap to draw into a raw buffer, and visibly different page to page, which
// is what the harness needs to prove a turn actually turned.
void fakePage(uint32_t idx, uint8_t* dst) {
  memset(dst, 0xFF, 48000);
  auto setBlack = [&](int x, int y) {
    if (x < 0 || x >= 480 || y < 0 || y >= 800) return;
    dst[(size_t)y * 60 + (x >> 3)] &= ~(0x80 >> (x & 7));
  };
  for (int x = 8; x < 472; x++) { setBlack(x, 8); setBlack(x, 791); }
  for (int y = 8; y < 792; y++) { setBlack(8, y); setBlack(471, y); }
  for (int t = 0; t < 700; t++)
    for (int w = 0; w < 3; w++) setBlack(40 + (t * 400) / 700 + w, 60 + t);
  for (uint32_t k = 0; k <= idx && k < 12; k++)
    for (int y = 40; y < 100; y++)
      for (int x = 0; x < 24; x++) setBlack(40 + (int)k * 36 + x, y);
}
uint32_t g_fakeOpenPages = 0;
}  // namespace

namespace {
// A grey fake: four vertical bands at the four levels, plus idx+1 black tally
// bars, so the fallback dither and a page turn are both visible in a render.
void fakePageGrey(uint32_t idx, uint8_t* dst) {
  for (uint32_t i = 0; i < 96000; i++) dst[i] = 0;
  for (int y = 0; y < 800; y++) {
    for (int x = 0; x < 480; x++) {
      const uint32_t i = (uint32_t)y * 480 + x;
      uint8_t lv = (uint8_t)(x / 120);  // 0,1,2,3 left to right
      if (y > 60 && y < 120 && (x / 36) < (int)(idx + 1) && (x / 36) % 2 == 0) lv = 0;
      dst[i >> 2] |= lv << (6 - 2 * (i & 3));
    }
  }
}
uint8_t g_fakeBpp = 1;
}  // namespace

int bookList(BookMeta* out, int max) {
  static const BookMeta kFake[3] = {{"one-piece-v1.tbk", "One Piece vol 1", 12, true, 1},
                                    {"walden.tbk", "Walden", 8, false, 1},
                                    {"grey-test.tbk", "Grey test card", 3, false, 2}};
  int n = 0;
  for (; n < 3 && n < max; n++) out[n] = kFake[n];
  return n;
}

bool bookOpen(const char* file) {
  g_fakeBpp = strstr(file, "grey") ? 2 : 1;
  g_fakeOpenPages = strstr(file, "walden") ? 8 : (g_fakeBpp == 2 ? 3 : 12);
  return true;
}

bool bookReadPage(uint32_t idx, uint8_t* dst) {
  if (idx >= g_fakeOpenPages) return false;
  if (g_fakeBpp == 2)
    fakePageGrey(idx, dst);
  else
    fakePage(idx, dst);
  return true;
}

void bookClose() { g_fakeOpenPages = 0; }

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

namespace {
// The open book: its file handle, and whether the bus is currently claimed.
// One at a time is all the UI can show, so one is all this holds.
File g_book;
bool g_bookBusUp = false;
uint32_t g_bookPageBytes = 48000;

constexpr uint32_t TBK_HEADER = 64;

const char* kBookDirs[2] = {"/books", "/"};

bool parseTbkHeader(File& f, BookMeta& out) {
  uint8_t h[TBK_HEADER];
  if (f.read(h, sizeof(h)) != sizeof(h)) return false;
  if (memcmp(h, "TBK1", 4) != 0) return false;
  const int w = h[4] | (h[5] << 8), ht = h[6] | (h[7] << 8);
  const int bpp = h[8];
  if (w != 480 || ht != 800 || (bpp != 1 && bpp != 2)) return false;
  const uint32_t pageBytes = (uint32_t)h[16] | ((uint32_t)h[17] << 8) |
                             ((uint32_t)h[18] << 16) | ((uint32_t)h[19] << 24);
  if (pageBytes != 48000u * (uint32_t)bpp) return false;  // header lying about itself
  out.bpp = (uint8_t)bpp;
  out.rtl = (h[9] & 1) != 0;
  out.pages = (uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) |
              ((uint32_t)h[15] << 24);
  memcpy(out.title, h + 24, 40);
  out.title[40] = 0;
  if (!out.title[0]) strncpy(out.title, "untitled", sizeof(out.title) - 1);
  return out.pages > 0;
}
}  // namespace

int bookList(BookMeta* out, int max) {
  if (!busClaim()) {
    busRelease();
    return -1;
  }
  int n = 0;
  for (const char* dir : kBookDirs) {
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) continue;
    for (File f = d.openNextFile(); f && n < max; f = d.openNextFile()) {
      if (f.isDirectory()) continue;
      const char* nm = f.name();
      const size_t len = strlen(nm);
      if (len < 5 || strcasecmp(nm + len - 4, ".tbk") != 0) continue;
      BookMeta m{};
      if (!parseTbkHeader(f, m)) continue;
      const char* bare = strrchr(nm, '/');
      strncpy(m.file, bare ? bare + 1 : nm, sizeof(m.file) - 1);
      out[n++] = m;
    }
  }
  busRelease();
  return n;
}

bool bookOpen(const char* file) {
  bookClose();
  if (!busClaim()) {
    busRelease();
    return false;
  }
  g_bookBusUp = true;
  for (const char* dir : kBookDirs) {
    char path[64];
    snprintf(path, sizeof(path), "%s%s%s", dir, dir[1] ? "/" : "", file);
    g_book = SD.open(path, FILE_READ);
    if (g_book && !g_book.isDirectory()) {
      BookMeta m{};
      if (parseTbkHeader(g_book, m)) {
        g_bookPageBytes = 48000u * m.bpp;
        return true;  // bus stays up: reading now
      }
      g_book.close();
    }
  }
  bookClose();
  return false;
}

bool bookReadPage(uint32_t idx, uint8_t* dst) {
  if (!g_book) return false;
  if (!g_book.seek(TBK_HEADER + (uint64_t)idx * g_bookPageBytes)) return false;
  uint32_t got = 0;
  while (got < g_bookPageBytes) {
    const int n = g_book.read(dst + got, g_bookPageBytes - got);
    if (n <= 0) break;
    got += (uint32_t)n;
  }
  return got == g_bookPageBytes;
}

void bookClose() {
  if (g_book) g_book.close();
  if (g_bookBusUp) {
    busRelease();
    g_bookBusUp = false;
  }
}

#endif

}  // namespace sdcard
