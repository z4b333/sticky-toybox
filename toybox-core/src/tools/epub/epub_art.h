// A book's own illustrations, prepared on the device.
//
// Before this, a picture inside an EPUB showed only if something on a PC had
// prepared it (the Toybox slicer's toybox/<path>.bmp, CrossInk's .pxc);
// anything else got the "no picture prepared" plate. Now the device decodes
// the JPEG or PNG itself the first time the page is reached, and keeps the
// result on the card, so every later visit is a file read.
//
// Each part is two pictures, both 480x800 portrait, letterboxed white, in the
// book's own folder (book_store.h):
//   .g2  96,000 B  four-level grey, 2 bpp, high pixel first, 0 = black ..
//                  3 = white -- the .tbk grey page format, shown through the
//                  panel's grey waveform when the page has no chrome over it
//   .b1  48,000 B  one bit, MSB first, 1 = white -- the framebuffer's own
//                  convention, drawn through the canvas everywhere else
//
// Quality is the point of this file, so the steps are the careful ones:
//   decode -> whole picture in PSRAM (pre-shrunk by an integer box only when
//   it would not fit) -> area-weighted shrink, or bilinear growth -> levels
//   from the histogram (scans are rarely true black on true white) -> a light
//   unsharp mask (line art survives the shrink) -> serpentine Floyd-Steinberg,
//   separately for each output, each with its own tone curve.
#pragma once
#include "../tools_ui.h"
#include "epubcore.h"

namespace epubart {

inline constexpr int W = 480, H = 800;
inline constexpr uint32_t G2_BYTES = W * H / 4;  // 96,000
inline constexpr uint32_t B1_BYTES = W * H / 8;  // 48,000

// Bump when the pipeline changes how a picture looks: the folder name carries
// it, so a better pipeline is never hidden behind an older cached picture.
inline constexpr int PIPELINE = 2;

// A picture becomes one page, or for landscape art several: part 0 is the
// whole picture turned a quarter clockwise to fill the portrait panel (its
// top along the panel's right edge), then parts 1..n are upright slices of
// it, left to right, each filling the panel -- a spread read the way the
// panel is held, at a size where its detail survives.
inline constexpr int MAX_PARTS = 6;

// Where one illustration's prepared pictures live: the book's own folder
// (book_store.h), named from the picture's path inside the zip and its
// uncompressed size, so a book replaced by a different edition does not
// inherit stale art.
void paths(const char* bookFile, const char* entry, uint32_t usize, int part, char* g2, char* b1,
           int cap);

// Which of a book's pictures are prepared: the folder's art.idx, read once
// per open. `key` is the FNV-1a of the picture's zip path (bstore::key);
// `parts` 0 means it cannot be decoded here -- remembered, so a GIF is not
// tried again on every open. A transient failure is never recorded.
struct Prepared {
  uint32_t key;
  uint32_t usize;
  uint8_t parts;
};
inline constexpr int INDEX_MAX = 256;
// The records, or -1 when there is no index (or one from another pipeline,
// whose pictures look different and are prepared again).
int indexRead(ToolsHost& host, const char* bookFile, Prepared* out, int max);
bool indexWrite(ToolsHost& host, const char* bookFile, const Prepared* recs, int n);

// Pictures prepared before the per-book folders, under /.toybox/art<N>/:
// moves one picture's parts into the book's folder and returns its part
// count (0 = remembered as undecodable), or -1 when there is nothing old to
// move or the move failed -- prepare it then. dropOldDir removes the old
// folder once it is empty.
int adoptOld(ToolsHost& host, const char* bookFile, uint32_t key, uint32_t usize);
void dropOldDir(ToolsHost& host, const char* bookFile);

// Decodes `entry` from the open book and writes every part. Returns the part
// count, or 0 when the picture is not one this device can decode (not
// JPEG/PNG, corrupt, too big) or memory/card failed; `transient` says which
// -- a transient failure must not be remembered as "this picture cannot be
// shown". Recording the result in the index is the caller's job.
int prepare(ToolsHost& host, epubc::Book& book, const char* bookFile, const char* entry,
            uint32_t usize, bool* transient = nullptr);

// The book's declared cover, decoded the way its illustrations are (full
// progressive decode, levels, area-weighted fit, unsharp mask) and fitted
// upright into the 480x800 grey page `g`, letterboxed white. False as for
// prepare(), with `transient` meaning memory rather than the picture.
bool coverGrey(epubc::Book& book, uint8_t* g, bool* transient = nullptr);

// A grey page to one bit at `path` on the card, the way a picture page's .b1
// is made: Atkinson over a gamma 1.15 curve. On a real retail cover this kept
// faces, hair lines and the small credit text that Floyd-Steinberg turned to
// grain (compared side by side on the bench).
bool writeB1(ToolsHost& host, const uint8_t* g, const char* path);

}  // namespace epubart
