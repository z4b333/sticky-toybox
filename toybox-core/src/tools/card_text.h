// Reading notes and decks off the SD card.
//
// Both the notes tool and the flashcards tool offer the same door: a folder on
// the card, a list of the text files in it, and one tap to bring one into the
// device. The screens differ -- a note is one document, a deck is a table --
// but the listing and the reading are the same job twice, so they live here.
//
// Files are written by hand, by a phone, by a spreadsheet, or by the editor on
// the project's own web page (docs/editor.html), which produces exactly the two
// shapes these folders expect.
#pragma once
#include "tools_ui.h"

namespace cardtext {

// The card can hold more than this; the screen cannot. Sixteen rows is already
// more than fits at a readable height, and a folder with fifty files in it is
// asking for a file manager rather than an import list -- which this device has,
// under Settings.
inline constexpr int MAX_FILES = 16;
inline constexpr int NAME_LEN = ToolsHost::RECIPE_NAME_LEN;  // 64, the lister's

inline constexpr const char* NOTES_DIR = "/notes";
inline constexpr const char* DECKS_DIR = "/decks";

// Lists a folder. Returns how many were found, or -1 when no card answered --
// the two are different things to a person standing in front of the screen
// ("there is no card" versus "the card has no notes on it"), so they stay
// different here.
inline int list(ToolsHost& h, const char* dir, char names[][NAME_LEN], int max) {
  if (max > MAX_FILES) max = MAX_FILES;
  return h.sdTextFiles(dir, names, max);
}

// The name without its extension, which is what the note or deck is called once
// it is in. Cut on a byte, not a codepoint: the only thing removed is an ASCII
// extension, and everything before the last dot is copied whole.
inline void stem(const char* file, char* out, int outMax) {
  const char* dot = strrchr(file, '.');
  int n = dot && dot != file ? (int)(dot - file) : (int)strlen(file);
  if (n > outMax - 1) n = outMax - 1;
  memcpy(out, file, (size_t)n);
  out[n] = 0;
}

// Reads a file whole into `buf`, NUL-terminating it. Returns the byte count, or
// -1 when the card would not give it up.
//
// A file longer than the buffer is truncated rather than refused: half a note is
// worth more than none, and the alternative is a screen that says "too long" and
// offers nothing to do about it. The cut is walked back off any partial UTF-8
// sequence, because a note ending in half a Thai character renders as a box that
// nobody put there.
//
// BORROWS THE SD BUS, which re-initialises the panel -- every caller must repaint
// with a full refresh afterwards, not a partial one.
inline int read(ToolsHost& h, const char* dir, const char* file, char* buf, int max) {
  char path[128];
  snprintf(path, sizeof(path), "%s/%s", dir, file);
  int n = h.sdReadWhole(path, buf, max);
  if (n < 0) return -1;
  if (n > max) n = max;
  if (n == max) {  // the buffer filled, so the file may go on past the cut
    int i = n - 1, cont = 0;
    while (i >= 0 && ((uint8_t)buf[i] & 0xC0) == 0x80) {
      i--;
      cont++;
    }
    if (i >= 0) {
      const uint8_t lead = (uint8_t)buf[i];
      const int need = lead < 0xC0 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
      if (cont < need) n = i;  // an unfinished character; drop it whole
    }
  }
  buf[n] = 0;
  return n;
}

}  // namespace cardtext
