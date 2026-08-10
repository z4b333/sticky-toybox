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

}  // namespace sdcard
