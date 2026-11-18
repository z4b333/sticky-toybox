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

  c.inkBits(b + HEADER, W, H);
  free(buf);
  return true;
}

}  // namespace tbimg

// The lock screen's grey picture: the same 480x800, but 2 bits per pixel in
// the panel's own 2bpp layout (high pair first, 0 = black, 3 = white). It
// exists because the panel CAN show four greys and a photograph is the one
// thing on this device that deserves them. Made from a .bmp on the card --
// the format the CrossPoint/CrossInk/Xteink family trades sleep art in -- and
// painted straight to the panel's grey path, never through the 1-bit canvas.
namespace tbg2 {
inline constexpr uint32_t MAGIC = 0x31474254;  // 'TBG1', little endian
inline constexpr int W = 480, H = 800;
inline constexpr uint32_t BITS = (uint32_t)W * H / 4;  // 96,000
inline constexpr uint32_t HEADER = 8;                  // magic, u16 w, u16 h
inline constexpr uint32_t FILE_SIZE = HEADER + BITS;
inline bool have(const char* path) { return tfs::size(path) == FILE_SIZE; }

// The grey picture laid OVER what is already on the panel, rather than painted
// to the glass in its own right. Four levels come down to one bit here, and
// the rule is a threshold rather than a dither: this goes over a page already
// knocked back to a checkerboard, and a dithered grey interleaved with that
// reads as noise rather than as a picture. So the two darker levels are ink
// and the two lighter ones are nothing, which is what makes a mostly-white
// picture a frame around the page instead of a sheet over it.
inline bool drawOver(ToolsCanvas& c, const char* path) {
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
  const uint8_t* bits = b + HEADER;
  const int stride = W / 4;
  for (int y = 0; y < H; y++) {
    const uint8_t* row = bits + (size_t)y * stride;
    for (int xb = 0; xb < stride; xb++) {
      const uint8_t v = row[xb];
      if (v == 0xFF) continue;  // four white pixels, which is most of most pictures
      for (int k = 0; k < 4; k++)
        if ((uint8_t)((v >> ((3 - k) * 2)) & 3) <= 1) c.fillRect(xb * 4 + k, y, 1, 1, true);
    }
  }
  free(buf);
  return true;
}
}  // namespace tbg2

namespace lockimg {
inline constexpr const char* PATH = "/lockimg.tbi";
inline constexpr const char* G2_PATH = "/lockimg.g2";
inline constexpr uint32_t FILE_SIZE = tbimg::FILE_SIZE;
inline constexpr int W = tbimg::W, H = tbimg::H;
// Two files, one picture: at most one exists at a time -- whichever way a
// picture last arrived (phone sends 1-bit, a card .bmp becomes grey) removes
// the other, so "have" and "remove" speak for both.
inline bool haveG2() { return tbg2::have(G2_PATH); }
inline bool have() { return tbimg::have(PATH) || haveG2(); }
inline void remove() {
  tfs::remove(PATH);
  tfs::remove(G2_PATH);
}
inline bool draw(ToolsCanvas& c) { return tbimg::draw(c, PATH); }
// The same picture, whichever of the two files it is in, laid over what is
// already on the panel. `have()` answers for both, so anything that asks
// "is a picture stored?" and then draws has to be able to draw both -- the
// lock screen's PAGE said yes to the first question and drew nothing.
inline bool drawOver(ToolsCanvas& c) {
  return tbimg::draw(c, PATH) || tbg2::drawOver(c, G2_PATH);
}
}  // namespace lockimg

namespace wallimg {
inline constexpr const char* PATH = "/wallimg.tbi";
inline bool have() { return tbimg::have(PATH); }
inline void remove() { tfs::remove(PATH); }
inline bool draw(ToolsCanvas& c) { return tbimg::draw(c, PATH); }
}  // namespace wallimg
