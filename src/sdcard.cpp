#include "sdcard.h"

#include <stdlib.h>
#include <string.h>

#ifdef TOYBOX_HOST
#include <map>
#include <string>
#endif

#include "tools/epub/epubcore.h"
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

// --- the invented EPUB -------------------------------------------------------
// A real zip, assembled in memory: chapter one stored, so the offset guard can
// count its codepoints by hand, and chapter two deflated (a blob compressed at
// build time), so tinfl streams real compressed data in the harness too.
namespace {

#include "fake_epub_ch2.inc"
#include "fake_epub_cover.inc"

// Chapter one is the offset-parity fixture: entities the XML layer expands
// (&#233;), one only the HTML table knows (&nbsp;), one expat predefines
// (&amp;), skipped head content, and whitespace text nodes that all count.
// The harness's expected numbers are hand-derived from THIS string; touching
// it means re-deriving them.
static const char kFakeCh1[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
    "<head><title>Chapter one</title>\n"
    "<style>p { margin: 0; }</style>\n"
    "</head>\n"
    "<body>\n"
    "<p>One two&nbsp;three</p>\n"
    "<p>caf&#233; &amp; more</p>\n"
    "<p>ende</p>\n"
    "</body>\n"
    "</html>\n";

static const char kFakeContainer[] =
    "<?xml version=\"1.0\"?>\n"
    "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
    "  <rootfiles>\n"
    "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
    "  </rootfiles>\n"
    "</container>\n";

static const char kFakeOpf[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\">\n"
    "  <metadata><dc:title xmlns:dc=\"http://purl.org/dc/elements/1.1/\">Wind</dc:title>\n"
    "    <meta name=\"cover\" content=\"cov\"/>\n"
    "  </metadata>\n"
    "  <manifest>\n"
    "    <item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
    "    <item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
    "    <item id=\"cov\" href=\"cover.jpg\" media-type=\"image/jpeg\"/>\n"
    "    <item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n"
    "  </manifest>\n"
    "  <spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine>\n"
    "</package>\n";

uint8_t* g_fakeEpub = nullptr;
uint32_t g_fakeEpubLen = 0;
bool g_fakeEpubOpen = false;

void put16(uint8_t* p, uint32_t v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
void put32(uint8_t* p, uint32_t v) {
  p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255;
}

void buildFakeEpub() {
  if (g_fakeEpub) return;
  struct E {
    const char* name;
    const uint8_t* data;
    uint32_t csize, usize;
    uint16_t method;
    uint32_t lho;
  };
  E ents[5] = {
      {"META-INF/container.xml", (const uint8_t*)kFakeContainer, (uint32_t)strlen(kFakeContainer),
       (uint32_t)strlen(kFakeContainer), 0, 0},
      {"OEBPS/content.opf", (const uint8_t*)kFakeOpf, (uint32_t)strlen(kFakeOpf),
       (uint32_t)strlen(kFakeOpf), 0, 0},
      {"OEBPS/ch1.xhtml", (const uint8_t*)kFakeCh1, (uint32_t)strlen(kFakeCh1),
       (uint32_t)strlen(kFakeCh1), 0, 0},
      {"OEBPS/ch2.xhtml", kFakeCh2Deflate, (uint32_t)sizeof(kFakeCh2Deflate), kFakeCh2Raw, 8, 0},
      {"OEBPS/cover.jpg", kFakeCoverJpeg, (uint32_t)sizeof(kFakeCoverJpeg),
       (uint32_t)sizeof(kFakeCoverJpeg), 0, 0},
  };
  uint32_t total = 22;
  for (const E& e : ents) total += 30 + 46 + 2 * (uint32_t)strlen(e.name) + e.csize;
  g_fakeEpub = (uint8_t*)malloc(total);
  uint8_t* p = g_fakeEpub;
  for (E& e : ents) {
    e.lho = (uint32_t)(p - g_fakeEpub);
    const uint32_t nl = (uint32_t)strlen(e.name);
    memset(p, 0, 30);
    put32(p, 0x04034b50u);
    put16(p + 8, e.method);
    put32(p + 18, e.csize);
    put32(p + 22, e.usize);
    put16(p + 26, nl);
    memcpy(p + 30, e.name, nl);
    memcpy(p + 30 + nl, e.data, e.csize);
    p += 30 + nl + e.csize;
  }
  const uint32_t cdOfs = (uint32_t)(p - g_fakeEpub);
  for (const E& e : ents) {
    const uint32_t nl = (uint32_t)strlen(e.name);
    memset(p, 0, 46);
    put32(p, 0x02014b50u);
    put16(p + 10, e.method);
    put32(p + 20, e.csize);
    put32(p + 24, e.usize);
    put16(p + 28, nl);
    put32(p + 42, e.lho);
    memcpy(p + 46, e.name, nl);
    p += 46 + nl;
  }
  const uint32_t cdSize = (uint32_t)(p - g_fakeEpub) - cdOfs;
  memset(p, 0, 22);
  put32(p, 0x06054b50u);
  put16(p + 8, 5);
  put16(p + 10, 5);
  put32(p + 12, cdSize);
  put32(p + 16, cdOfs);
  p += 22;
  g_fakeEpubLen = (uint32_t)(p - g_fakeEpub);
}

// The sidecar store: enough of a filesystem for progress files. The harness
// reads it back through readFileAt to check what a CrossPoint device would
// find on the card.
struct FakeSide {
  char path[120];
  uint8_t data[32];
  int n = 0;
};
FakeSide g_side[4];
}  // namespace

// The second invented book exists to exercise long paths: real release
// filenames pass 64 bytes with the /books/ prefix, which is exactly how a
// book on a real card silently failed to open (see EpubMeta::file).
static const char kFakeLongPath[] =
    "/books/A Book With The Kind Of Very Long Release Filename Publishers Actually Use Vol 01.epub";

int epubList(EpubMeta* out, int max) {
  int n = 0;
  const char* files[2] = {"/books/wind.epub", kFakeLongPath};
  const char* titles[2] = {"wind", "the long-named book"};
  for (int i = 0; i < 2 && n < max; i++) {
    EpubMeta m{};
    strncpy(m.file, files[i], sizeof(m.file) - 1);
    strncpy(m.title, titles[i], sizeof(m.title) - 1);
    char cache[96];
    epubc::cacheDir(m.file, cache, sizeof(cache));
    strncat(cache, "/progress.bin", sizeof(cache) - strlen(cache) - 1);
    m.cont = false;
    for (const FakeSide& s : g_side)
      if (s.n > 0 && strcmp(s.path, cache) == 0) m.cont = true;
    out[n++] = m;
  }
  return n;
}

bool epubOpen(const char* path) {
  if (strcmp(path, "/books/wind.epub") != 0 && strcmp(path, kFakeLongPath) != 0) return false;
  buildFakeEpub();
  g_fakeEpubOpen = true;
  return true;
}

int epubRead(uint32_t pos, void* dst, uint32_t n) {
  if (!g_fakeEpubOpen || pos > g_fakeEpubLen) return -1;
  uint32_t take = g_fakeEpubLen - pos;
  if (n < take) take = n;
  memcpy(dst, g_fakeEpub + pos, take);
  return (int)take;
}

uint32_t epubSize() { return g_fakeEpubOpen ? g_fakeEpubLen : 0; }

void epubClose() { g_fakeEpubOpen = false; }

int readFileAt(const char* path, void* dst, int max) {
  for (const FakeSide& s : g_side)
    if (s.n > 0 && strcmp(s.path, path) == 0) {
      const int n = s.n < max ? s.n : max;
      memcpy(dst, s.data, (size_t)n);
      return n;
    }
  return -1;
}

bool writeFileAtomic(const char* path, const void* data, int n) {
  if (n > (int)sizeof(g_side[0].data)) return false;
  FakeSide* slot = nullptr;
  for (FakeSide& s : g_side)
    if (s.n > 0 && strcmp(s.path, path) == 0) slot = &s;
  if (!slot)
    for (FakeSide& s : g_side)
      if (s.n == 0 && !slot) slot = &s;
  if (!slot) return false;
  strncpy(slot->path, path, sizeof(slot->path) - 1);
  memcpy(slot->data, data, (size_t)n);
  slot->n = n;
  return true;
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

// The invented card, for the file-manager harness: a handful of files the
// phone can list, rename, delete and add to, with the same path rules the
// device enforces so the guards are testing the real decisions.
namespace {
struct FakeFile {
  char path[128];
  uint32_t size;
  bool used;
};
FakeFile g_card[16] = {
    {"/books/wind.epub", 19443403, true},
    {"/books/one-piece-v1.tbk", 640064, true},
    {"/wallpapers/mountains.tbi", 48008, true},
};
bool g_mgrUp = false;
int g_writeSlot = -1;
uint32_t g_written = 0;

bool safeName(const char* n) {
  if (!n || !*n || *n == '.') return false;
  if (strlen(n) > 90) return false;
  for (const char* p = n; *p; p++)
    if (*p == '/' || *p == '\\' || *p < 32 || *p == 127) return false;
  return strstr(n, "..") == nullptr;
}
bool safePath(const char* p) {
  if (!p || p[0] != '/' || strlen(p) >= 128) return false;
  if (strstr(p, "..")) return false;
  const char* slash = strrchr(p, '/');
  if (!slash || !safeName(slash + 1)) return false;
  const size_t dirLen = (size_t)(slash - p);
  if (dirLen == 0) return true;
  return (dirLen == 6 && strncmp(p, "/books", 6) == 0) ||
         (dirLen == 11 && strncmp(p, "/wallpapers", 11) == 0);
}
const char* dirFor(const char* key) {
  if (strcmp(key, "books") == 0) return "/books";
  if (strcmp(key, "wallpapers") == 0) return "/wallpapers";
  if (strcmp(key, "root") == 0) return "/";
  return nullptr;
}
}  // namespace

bool mgrOpen() {
  g_mgrUp = true;
  return true;
}
void mgrClose() {
  if (g_writeSlot >= 0) {
    g_card[g_writeSlot].used = false;  // a session cut mid-file
    g_writeSlot = -1;
  }
  g_mgrUp = false;
}
bool mgrHolding() { return g_mgrUp; }

int mgrList(FileEntry* out, int max) {
  if (!g_mgrUp) return -1;
  int n = 0;
  for (const FakeFile& f : g_card) {
    if (!f.used || n >= max) continue;
    FileEntry e{};
    strncpy(e.path, f.path, sizeof(e.path) - 1);
    e.size = f.size;
    out[n++] = e;
  }
  return n;
}

bool mgrDelete(const char* path) {
  if (!g_mgrUp || !safePath(path)) return false;
  for (FakeFile& f : g_card)
    if (f.used && strcmp(f.path, path) == 0) {
      f.used = false;
      return true;
    }
  return false;
}

bool mgrRename(const char* path, const char* bareName) {
  if (!g_mgrUp || !safePath(path) || !safeName(bareName)) return false;
  const char* slash = strrchr(path, '/');
  char dest[128];
  if (snprintf(dest, sizeof(dest), "%.*s/%s", (int)(slash - path), path, bareName) >=
      (int)sizeof(dest))
    return false;
  for (const FakeFile& f : g_card)
    if (f.used && strcmp(f.path, dest) == 0) return false;
  for (FakeFile& f : g_card)
    if (f.used && strcmp(f.path, path) == 0) {
      strncpy(f.path, dest, sizeof(f.path) - 1);
      return true;
    }
  return false;
}

bool mgrWriteOpen(const char* dir, const char* bareName) {
  if (!g_mgrUp) return false;
  const char* folder = dirFor(dir);
  if (!folder || !safeName(bareName)) return false;
  char full[128];
  if (snprintf(full, sizeof(full), "%s%s%s", folder, folder[1] ? "/" : "", bareName) >=
      (int)sizeof(full))
    return false;
  for (int i = 0; i < 16; i++)
    if (!g_card[i].used) {
      strncpy(g_card[i].path, full, sizeof(g_card[i].path) - 1);
      g_card[i].size = 0;
      g_card[i].used = true;
      g_writeSlot = i;
      g_written = 0;
      return true;
    }
  return false;
}

bool mgrWriteChunk(const uint8_t*, uint32_t n) {
  if (g_writeSlot < 0) return false;
  g_written += n;
  return true;
}

bool mgrWriteClose(bool keep) {
  if (g_writeSlot < 0) return false;
  if (keep)
    g_card[g_writeSlot].size = g_written;
  else
    g_card[g_writeSlot].used = false;
  g_writeSlot = -1;
  return keep;
}

uint32_t mgrFreeMb() { return g_mgrUp ? 121000 : 0; }

// The card's own little filesystem, for everything written by path rather
// than by name: cover art, and whatever else learns to live out here.
namespace {
std::map<std::string, std::string>& fakeCard() {
  static std::map<std::string, std::string> fs;
  return fs;
}
std::string g_streamPath;
std::string g_streamBuf;
bool g_streaming = false;
}  // namespace

bool busHeld() { return g_mgrUp || g_fakeEpubOpen || g_fakeOpenPages > 0; }

bool streamOpen(const char* path) {
  g_streamPath = path;
  g_streamBuf.clear();
  g_streaming = true;
  return true;
}
bool streamWrite(const uint8_t* data, uint32_t n) {
  if (!g_streaming) return false;
  g_streamBuf.append((const char*)data, n);
  return true;
}
bool streamClose(bool keep) {
  if (!g_streaming) return false;
  if (keep) fakeCard()[g_streamPath] = g_streamBuf;
  g_streaming = false;
  g_streamBuf.clear();
  return keep;
}
int readWhole(const char* path, void* dst, int max) {
  auto it = fakeCard().find(path);
  if (it == fakeCard().end()) return -1;
  const int n = (int)it->second.size() < max ? (int)it->second.size() : max;
  memcpy(dst, it->second.data(), (size_t)n);
  return n;
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

namespace {
File g_epub;
bool g_epubBusUp = false;

bool isEpubName(const char* n) {
  const size_t len = strlen(n);
  return len > 5 && strcasecmp(n + len - 5, ".epub") == 0;
}

// mkdir every parent of `path` ("/a/b/c.bin" -> /a, /a/b). FAT mkdir on an
// existing directory is a cheap no-op-with-false, so no exists() dance.
void makeParents(const char* path) {
  char tmp[128];
  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  for (char* p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = 0;
    SD.mkdir(tmp);
    *p = '/';
  }
}
}  // namespace

int epubList(EpubMeta* out, int max) {
  if (!busClaim()) {
    busRelease();
    return -1;
  }
  int n = 0;
  for (const char* dir : kBookDirs) {
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) continue;
    for (File f = d.openNextFile(); f && n < max; f = d.openNextFile()) {
      if (f.isDirectory() || !isEpubName(f.name())) continue;
      EpubMeta m{};
      const char* bare = strrchr(f.name(), '/');
      bare = bare ? bare + 1 : f.name();
      const int need = snprintf(m.file, sizeof(m.file), "%s%s%s", dir, dir[1] ? "/" : "", bare);
      // A truncated path could never open (and would hash to the wrong
      // progress directory); leaving the book off the list beats listing a
      // name that only fails when tapped.
      if (need >= (int)sizeof(m.file)) continue;
      strncpy(m.title, bare, sizeof(m.title) - 1);
      char* dot = strrchr(m.title, '.');
      if (dot) *dot = 0;
      // Whether CrossPoint (or a previous Toybox session) left a position.
      char cache[96];
      epubc::cacheDir(m.file, cache, sizeof(cache));
      strncat(cache, "/progress.bin", sizeof(cache) - strlen(cache) - 1);
      m.cont = SD.exists(cache);
      out[n++] = m;
    }
  }
  busRelease();
  return n;
}

bool epubOpen(const char* path) {
  epubClose();
  if (!busClaim()) {
    busRelease();
    return false;
  }
  g_epubBusUp = true;
  g_epub = SD.open(path, FILE_READ);
  if (!g_epub || g_epub.isDirectory()) {
    epubClose();
    return false;
  }
  return true;  // the bus stays up: reading now
}

int epubRead(uint32_t pos, void* dst, uint32_t n) {
  if (!g_epub) return -1;
  if (!g_epub.seek(pos)) return -1;
  uint32_t got = 0;
  while (got < n) {
    const int r = g_epub.read((uint8_t*)dst + got, n - got);
    if (r <= 0) break;
    got += (uint32_t)r;
  }
  return (int)got;
}

uint32_t epubSize() { return g_epub ? g_epub.size() : 0; }

void epubClose() {
  if (g_epub) g_epub.close();
  if (g_epubBusUp) {
    busRelease();
    g_epubBusUp = false;
  }
}

int readFileAt(const char* path, void* dst, int max) {
  if (!g_epubBusUp) return -1;
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) return -1;
  const int n = f.read((uint8_t*)dst, max);
  f.close();
  return n;
}

// --- managing the card from a phone -----------------------------------------

namespace {
bool g_mgrUp = false;
File g_mgrWrite;
char g_mgrWritePath[128] = "";

// The folders a phone may write into, and the only ones it can name. "books"
// and "wallpapers" are where the readers and the wallpaper picker look;
// "root" is the card's top level, for anything else.
const char* mgrDirFor(const char* key) {
  if (strcmp(key, "books") == 0) return "/books";
  if (strcmp(key, "wallpapers") == 0) return "/wallpapers";
  if (strcmp(key, "root") == 0) return "/";
  return nullptr;
}

// A name the device is willing to build a path out of. No separators, no
// parent hops, nothing hidden, and short enough that the joined path still
// fits every buffer that will carry it.
bool mgrSafeName(const char* n) {
  if (!n || !*n || *n == '.') return false;
  if (strlen(n) > 90) return false;
  for (const char* p = n; *p; p++)
    if (*p == '/' || *p == '\\' || *p < 32 || *p == 127) return false;
  return strstr(n, "..") == nullptr;
}

// ...and a path the device is willing to touch: one it could itself have
// produced, which means inside one of the three known folders.
bool mgrSafePath(const char* p) {
  if (!p || p[0] != '/' || strlen(p) >= 128) return false;
  if (strstr(p, "..")) return false;
  const char* slash = strrchr(p, '/');
  if (!slash) return false;
  if (!mgrSafeName(slash + 1)) return false;
  const size_t dirLen = (size_t)(slash - p);
  if (dirLen == 0) return true;  // the card's root
  return (dirLen == 6 && strncmp(p, "/books", 6) == 0) ||
         (dirLen == 11 && strncmp(p, "/wallpapers", 11) == 0);
}
}  // namespace

bool busHeld() { return g_epubBusUp || g_bookBusUp || g_mgrUp; }

namespace {
File g_stream;
char g_streamPath[160] = "";
}  // namespace

bool streamOpen(const char* path) {
  if (!busHeld() || !path || path[0] != '/') return false;
  if (g_stream) g_stream.close();
  makeParents(path);
  strncpy(g_streamPath, path, sizeof(g_streamPath) - 1);
  g_streamPath[sizeof(g_streamPath) - 1] = 0;
  g_stream = SD.open(g_streamPath, FILE_WRITE);
  return (bool)g_stream;
}

bool streamWrite(const uint8_t* data, uint32_t n) {
  if (!g_stream) return false;
  return g_stream.write(data, n) == n;
}

bool streamClose(bool keep) {
  if (!g_stream) return false;
  g_stream.close();
  if (!keep && g_streamPath[0]) SD.remove(g_streamPath);
  g_streamPath[0] = 0;
  return keep;
}

int readWhole(const char* path, void* dst, int max) {
  // Borrow the bus only if nobody else has it. Claiming costs the card's
  // power-up and a controller reset -- about 150 ms, which disappears into
  // the refresh that follows.
  const bool mine = !busHeld();
  if (mine && !busClaim()) {
    busRelease();
    return -1;
  }
  int got = -1;
  File f = SD.open(path, FILE_READ);
  if (f && !f.isDirectory()) {
    got = f.read((uint8_t*)dst, max);
    f.close();
  }
  if (mine) busRelease();
  return got;
}

bool mgrOpen() {
  if (g_mgrUp) return true;
  if (!busClaim()) {
    busRelease();
    return false;
  }
  g_mgrUp = true;
  return true;
}

void mgrClose() {
  if (g_mgrWrite) {
    g_mgrWrite.close();
    if (g_mgrWritePath[0]) SD.remove(g_mgrWritePath);  // a session cut mid-file
    g_mgrWritePath[0] = 0;
  }
  if (!g_mgrUp) return;
  busRelease();
  g_mgrUp = false;
}

bool mgrHolding() { return g_mgrUp; }

int mgrList(FileEntry* out, int max) {
  if (!g_mgrUp) return -1;
  static const char* kDirs[3] = {"/books", "/wallpapers", "/"};
  int n = 0;
  for (const char* dir : kDirs) {
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) continue;
    for (File f = d.openNextFile(); f && n < max; f = d.openNextFile()) {
      if (f.isDirectory()) continue;
      const char* nm = f.name();
      const char* bare = strrchr(nm, '/');
      bare = bare ? bare + 1 : nm;
      if (bare[0] == '.') continue;  // dotfiles, and the CrossPoint cache
      FileEntry e{};
      if (snprintf(e.path, sizeof(e.path), "%s%s%s", dir, dir[1] ? "/" : "", bare) >=
          (int)sizeof(e.path))
        continue;
      e.size = f.size();
      out[n++] = e;
    }
  }
  return n;
}

bool mgrDelete(const char* path) {
  if (!g_mgrUp || !mgrSafePath(path)) return false;
  return SD.remove(path);
}

bool mgrRename(const char* path, const char* bareName) {
  if (!g_mgrUp || !mgrSafePath(path) || !mgrSafeName(bareName)) return false;
  char dest[128];
  const char* slash = strrchr(path, '/');
  const int dirLen = (int)(slash - path);
  if (snprintf(dest, sizeof(dest), "%.*s/%s", dirLen, path, bareName) >= (int)sizeof(dest))
    return false;
  if (strcmp(dest, path) == 0) return true;
  if (SD.exists(dest)) return false;  // never silently eat an existing book
  return SD.rename(path, dest);
}

bool mgrWriteOpen(const char* dir, const char* bareName) {
  if (!g_mgrUp) return false;
  const char* folder = mgrDirFor(dir);
  if (!folder || !mgrSafeName(bareName)) return false;
  if (g_mgrWrite) g_mgrWrite.close();
  if (folder[1]) SD.mkdir(folder);
  if (snprintf(g_mgrWritePath, sizeof(g_mgrWritePath), "%s%s%s", folder, folder[1] ? "/" : "",
               bareName) >= (int)sizeof(g_mgrWritePath)) {
    g_mgrWritePath[0] = 0;
    return false;
  }
  g_mgrWrite = SD.open(g_mgrWritePath, FILE_WRITE);
  if (!g_mgrWrite) {
    g_mgrWritePath[0] = 0;
    return false;
  }
  return true;
}

bool mgrWriteChunk(const uint8_t* data, uint32_t n) {
  if (!g_mgrWrite) return false;
  return g_mgrWrite.write(data, n) == n;
}

bool mgrWriteClose(bool keep) {
  if (!g_mgrWrite) return false;
  g_mgrWrite.close();
  bool ok = true;
  if (!keep && g_mgrWritePath[0]) {
    SD.remove(g_mgrWritePath);
    ok = false;
  }
  g_mgrWritePath[0] = 0;
  return ok;
}

uint32_t mgrFreeMb() {
  if (!g_mgrUp) return 0;
  const uint64_t total = SD.totalBytes(), used = SD.usedBytes();
  if (total <= used) return 0;
  return (uint32_t)((total - used) / (1024ULL * 1024ULL));
}

bool writeFileAtomic(const char* path, const void* data, int n) {
  if (!g_epubBusUp) return false;
  makeParents(path);
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  const bool wrote = f.write((const uint8_t*)data, n) == (size_t)n;
  f.close();
  if (!wrote) {
    SD.remove(tmp);
    return false;
  }
  SD.remove(path);  // FAT rename does not overwrite
  return SD.rename(tmp, path);
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
