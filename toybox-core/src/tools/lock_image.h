// A picture for the sleeping panel.
//
// The phone does all the work. Its browser already decodes whatever the camera
// produced -- JPEG, PNG, and the HEIC that every iPhone photo actually is --
// and a canvas can scale, crop, greyscale and dither it far better than this
// chip can. More to the point, the phone can show the dithered result before it
// is sent: one-bit e-paper turns a badly chosen photograph into mud, and the
// only way to know is to look. Here the loop would be upload, refresh, squint,
// repeat, at 1.7 seconds a go.
//
// So what arrives is already the finished thing: 480x800 packed one bit per
// pixel, MSB first, 1 = white, exactly the framebuffer's own convention. 48 KB
// over a SoftAP instead of a three megabyte photograph, and drawing it is a
// loop rather than a decoder.
#pragma once
#include "tiny_fs.h"
#include "tools_ui.h"

// Two pictures share one format and one loader: the lock screen's and the home
// screen's wallpaper. They differ only in path, so everything below takes the
// path and the two namespaces at the bottom are just names for the two files.
namespace tbimg {

inline constexpr uint32_t MAGIC = 0x31494254;  // 'TBI1', little endian
inline constexpr int W = 480, H = 800;
inline constexpr int STRIDE = W / 8;                          // 60
inline constexpr uint32_t BITS = (uint32_t)STRIDE * H;        // 48000
inline constexpr uint32_t HEADER = 8;
inline constexpr uint32_t FILE_SIZE = HEADER + BITS;

// header: magic, u16 width, u16 height
inline bool have(const char* path) { return tfs::size(path) == FILE_SIZE; }

// Drawn through the canvas rather than blitted into the framebuffer, so the
// panel corrections and the rotation apply to it like anything else. Nearly
// 400,000 calls sounds extravagant; on the lock screen it happens once, on the
// way to power-off, beside a refresh that takes 1.7 seconds on its own, and on
// the home screen once per visit beside the same full refresh.
inline bool draw(ToolsCanvas& c, const char* path) {
  size_t len = 0;
  char* buf = tfs::readAlloc(path, len);
  if (!buf) return false;
  if (len != FILE_SIZE) {
    free(buf);
    return false;
  }
  const uint8_t* b = (const uint8_t*)buf;
  const uint32_t magic = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
                         ((uint32_t)b[3] << 24);
  const int w = b[4] | (b[5] << 8), h = b[6] | (b[7] << 8);
  if (magic != MAGIC || w != W || h != H) {
    free(buf);
    return false;
  }

  // Runs of white are skipped a byte at a time, which is most of a photograph's
  // sky and most of a line drawing's page.
  const uint8_t* bits = b + HEADER;
  for (int y = 0; y < H; y++) {
    const uint8_t* row = bits + (size_t)y * STRIDE;
    for (int xb = 0; xb < STRIDE; xb++) {
      const uint8_t v = row[xb];
      if (v == 0xFF) continue;
      for (int k = 0; k < 8; k++)
        if (!(v & (0x80 >> k))) c.fillRect(xb * 8 + k, y, 1, 1, true);
    }
  }
  free(buf);
  return true;
}

}  // namespace tbimg

namespace lockimg {
inline constexpr const char* PATH = "/lockimg.tbi";
inline constexpr uint32_t FILE_SIZE = tbimg::FILE_SIZE;
inline constexpr int W = tbimg::W, H = tbimg::H;
inline bool have() { return tbimg::have(PATH); }
inline void remove() { tfs::remove(PATH); }
inline bool draw(ToolsCanvas& c) { return tbimg::draw(c, PATH); }
}  // namespace lockimg

namespace wallimg {
inline constexpr const char* PATH = "/wallimg.tbi";
inline bool have() { return tbimg::have(PATH); }
inline void remove() { tfs::remove(PATH); }
inline bool draw(ToolsCanvas& c) { return tbimg::draw(c, PATH); }
}  // namespace wallimg
