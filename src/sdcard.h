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
};
int bookList(BookMeta* out, int max);  // -1: no card
bool bookOpen(const char* file);
bool bookReadPage(uint32_t idx, uint8_t* dst48k);
void bookClose();

}  // namespace sdcard
