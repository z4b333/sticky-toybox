// microSD, for the one question that decides whether this device can ever be a
// reader: does the card work while sharing the display's SPI bus?
//
// It is the least-verified thing in the project and the only one a book format
// depends on. A .tbk page is 48,000 bytes and internal flash holds about a
// hundred of them, so a volume has to come off a card or not at all.
//
// Nothing in the firmware uses this yet. It exists so the service screen can
// ask, and so the answer is a line on a screen rather than an opinion.
#pragma once
#include <Arduino.h>

namespace sdcard {

struct Report {
  bool mounted = false;
  uint64_t sizeMb = 0;
  int files = 0;         // entries in the root, capped at what we bother counting
  bool readOk = false;   // a real read of a real file came back
  uint32_t readKbPerSec = 0;
  bool panelSurvived = false;  // the panel still answered afterwards
  const char* failedAt = "not tried";
};

// Mounts, measures, reads, and then checks the panel is still there.
//
// The last step is the point. Two devices on one bus fail in a way that looks
// like neither of them is broken: the card reads perfectly, the panel stops
// answering, and the next refresh silently does nothing. Asking the panel
// afterwards is the difference between "SD works" and "SD works and costs you
// the screen".
Report probe();

// Wallpapers. Pre-converted 480x800 .tbi files, made on a PC with
// tools/make_tbi.py, sitting in the card's root or in /wallpapers. The card is
// powered for exactly as long as each call runs and the panel is re-initialised
// afterwards, so the caller must follow either one with a full refresh.
//
// list fills bare file names and returns the count, or -1 when no card
// mounted. take copies one of those files into LittleFS at destPath and
// validates the size on the way; a wrong-sized file is refused, not truncated.
int listTbi(char names[][40], int max);
bool takeTbi(const char* name, const char* destPath);

// Books. A .tbk (tools/make_tbk.py) is a 64-byte header and then fixed-size
// pages in the framebuffer's own layout; page N is a seek and a 48,000-byte
// read, nothing else. Unlike everything above, an open book HOLDS the bus:
// the card stays powered and mounted between page turns, with panel refreshes
// interleaved -- which is exactly the sharing experiment the reader exists to
// run. bookClose() powers the card down and re-initialises the panel, so the
// next paint after it must be full.
struct BookMeta {
  char file[40];
  char title[41];
  uint32_t pages = 0;
  bool rtl = false;
  uint8_t bpp = 1;       // 1 = B/W (48,000-byte pages), 2 = grey (96,000)
};
int bookList(BookMeta* out, int max);  // -1: no card
// The module remembers the open book's page size (48,000 or 96,000 bytes),
// so a read is just an index and a buffer big enough for either.
bool bookOpen(const char* file);
bool bookReadPage(uint32_t idx, uint8_t* dst);
void bookClose();

// EPUBs. Same bus discipline as .tbk books: listing borrows the bus for one
// call, an open EPUB holds it for the whole reading session, and epubClose()
// powers the card down and re-initialises the panel. The file field is the
// book's ABSOLUTE card path ("/books/x.epub") because that exact string is
// what CrossPoint hashes to find its progress directory -- see epubcore.h.
struct EpubMeta {
  // 128, not 64: real release filenames run long ("Classroom of the Elite
  // Volume 01 Seven Seas..." is 60 bytes WITH the /books/ prefix), and a
  // truncated path fails to open with no visible reason.
  char file[128];
  char title[41];
  bool cont = false;  // a CrossPoint progress file exists for it
};
int epubList(EpubMeta* out, int max);  // -1: no card
bool epubOpen(const char* path);
int epubRead(uint32_t pos, void* dst, uint32_t n);
uint32_t epubSize();
void epubClose();

// Small sidecar files (CrossPoint's progress.bin), valid only while an EPUB
// session holds the bus. writeFileAtomic makes the directories, writes a
// .tmp, and renames over -- an interrupted write leaves the old file whole.
int readFileAt(const char* path, void* dst, int max);
bool writeFileAtomic(const char* path, const void* data, int n);

// Is any session -- a book, an epub, the phone -- currently holding the bus?
bool busHeld();

// A file on the card written a piece at a time, for anything too big to
// assemble in RAM first. Only valid while something already holds the bus;
// the cover builder streams into this while a book is being opened.
bool streamOpen(const char* path);
bool streamWrite(const uint8_t* data, uint32_t n);
bool streamClose(bool keep);

// Reads a whole file. If nothing holds the bus this claims it for the read
// and gives it back -- which re-initialises the panel, so the caller's next
// paint must be a full one. Returns bytes read, or -1.
int readWhole(const char* path, void* dst, int max);

// --- managing the card from a phone -----------------------------------------
// The card is the only place books live, and until now the only way to put one
// there was to take it out. This is the same bus discipline as a reading
// session -- claim, work, release, panel re-initialised on the way out -- but
// driven by HTTP handlers instead of taps, so the claim is held across a whole
// burst of phone activity rather than per file.
//
// Every path here is absolute and comes back out of mgrList exactly as the
// phone must send it in again. Writes take a folder and a BARE name, which the
// device joins itself: a phone cannot talk its way out of /books that way.
struct FileEntry {
  char path[128];
  uint32_t size = 0;
};
inline constexpr int MGR_MAX_FILES = 64;

bool mgrOpen();   // claim the bus for a session; false when no card mounts
void mgrClose();  // release it -- the caller's next paint must be full
bool mgrHolding();
int mgrList(FileEntry* out, int max);  // -1 when the session is not open
bool mgrDelete(const char* path);
bool mgrRename(const char* path, const char* bareName);
// Streaming upload: open, feed chunks as they arrive off the socket, close.
// mgrWriteClose(false) throws the partial file away, which is what a dropped
// connection deserves -- half a book is worse than no book.
bool mgrWriteOpen(const char* dir, const char* bareName);
bool mgrWriteChunk(const uint8_t* data, uint32_t n);
bool mgrWriteClose(bool keep);
uint32_t mgrFreeMb();

}  // namespace sdcard
