// A book's own illustrations, prepared on the device.
//
// Before this, a picture inside an EPUB showed only if something on a PC had
// prepared it (the Toybox slicer's toybox/<path>.bmp, CrossInk's .pxc);
// anything else got the "no picture prepared" plate. Now the device decodes
// the JPEG or PNG itself the first time the page is reached, and keeps the
// result on the card, so every later visit is a file read.
//
// Each part is two pictures, both 480x800 portrait, letterboxed white:
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

// Where one illustration's prepared pictures live on the card. Keyed by the
// book's path, the picture's path inside the zip and its uncompressed size,
// so a book replaced by a different edition does not inherit stale art. The
// .inf file (one byte: the part count) is written last, so its presence
// means every part is there.
void paths(const char* bookFile, const char* entry, uint32_t usize, int part, char* g2, char* b1,
           int cap);
void infPath(const char* bookFile, const char* entry, uint32_t usize, char* inf, int cap);

// Decodes `entry` from the open book and writes every part and the .inf.
// Returns the part count, or 0 when the picture is not one this device can
// decode (not JPEG/PNG, corrupt, too big) or memory/card failed; `transient`
// says which -- a transient failure must not be remembered as "this picture
// cannot be shown".
int prepare(ToolsHost& host, epubc::Book& book, const char* bookFile, const char* entry,
            uint32_t usize, bool* transient = nullptr);

}  // namespace epubart
