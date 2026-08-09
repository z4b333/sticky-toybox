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

namespace lockimg {

inline constexpr const char* PATH = "/lockimg.tbi";
inline constexpr uint32_t MAGIC = 0x31494254;  // 'TBI1', little endian
inline constexpr int W = 480, H = 800;
inline constexpr int STRIDE = W / 8;                          // 60
inline constexpr uint32_t BITS = (uint32_t)STRIDE * H;        // 48000
inline constexpr uint32_t HEADER = 8;
inline constexpr uint32_t FILE_SIZE = HEADER + BITS;

// header: magic, u16 width, u16 height
inline bool have() { return tfs::size(PATH) == FILE_SIZE; }

inline void remove() { tfs::remove(PATH); }

// Drawn through the canvas rather than blitted into the framebuffer, so the
// panel corrections and the rotation apply to it like anything else. Nearly
// 400,000 calls sounds extravagant; it happens once, on the way to power-off,
// beside a refresh that takes 1.7 seconds on its own.
inline bool draw(ToolsCanvas& c) {
  size_t len = 0;
  char* buf = tfs::readAlloc(PATH, len);
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

// A thumbnail for the settings row, so you can tell which picture is loaded
// without powering the device off to look at it.
//
// Each destination pixel takes the majority of the source block it covers
// rather than one sample from it: at this reduction a nearest-neighbour sample
// of a dithered image is a field of noise, because dithering is exactly the
// business of hiding tone in pixel-level variation. Averaging puts the tone
// back. Halftoning the average would be better still and is not worth the code
// for a thumbnail three millimetres wide.
inline bool drawThumb(ToolsCanvas& c, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return false;
  size_t len = 0;
  char* buf = tfs::readAlloc(PATH, len);
  if (!buf) return false;
  if (len != FILE_SIZE) {
    free(buf);
    return false;
  }
  const uint8_t* bits = (const uint8_t*)buf + HEADER;
  for (int ty = 0; ty < h; ty++) {
    const int sy0 = (int)((int64_t)ty * H / h), sy1 = (int)((int64_t)(ty + 1) * H / h);
    for (int tx = 0; tx < w; tx++) {
      const int sx0 = (int)((int64_t)tx * W / w), sx1 = (int)((int64_t)(tx + 1) * W / w);
      int ink = 0, total = 0;
      for (int sy = sy0; sy < sy1; sy++) {
        const uint8_t* row = bits + (size_t)sy * STRIDE;
        for (int sx = sx0; sx < sx1; sx++, total++)
          if (!(row[sx >> 3] & (0x80 >> (sx & 7)))) ink++;
      }
      if (total > 0 && ink * 2 >= total) c.fillRect(x + tx, y + ty, 1, 1, true);
    }
  }
  free(buf);
  return true;
}

}  // namespace lockimg
