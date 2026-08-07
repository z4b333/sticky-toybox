// QR rendering onto a ToolsCanvas.
//
// A QR symbol is already a 1-bit bitmap, so e-paper renders it perfectly — no
// greyscale, no anti-aliasing, and it stays readable after a partial refresh.
#pragma once
#include <qrcode.h>

#include "tools_ui.h"

namespace fqr {

// Largest symbol we will attempt. Version 10 holds ~170 alphanumeric chars,
// far more than a WiFi credential or a LAN URL needs.
constexpr uint8_t MAX_VERSION = 10;

// Draws `text` as a QR code centred in a boxSize square at (x, y), including the
// mandatory 4-module quiet zone. Returns the rendered size in pixels, or 0 if
// the payload does not fit.
inline int draw(ToolsCanvas& c, int x, int y, int boxSize, const char* text) {
  // qrcode_getBufferSize(10) == ((57*57)+7)/8 == 407 bytes.
  static uint8_t modules[420];
  QRCode qr;
  uint8_t version = 2;
  for (; version <= MAX_VERSION; version++) {
    if (qrcode_getBufferSize(version) > sizeof(modules)) return 0;
    if (qrcode_initText(&qr, modules, version, ECC_MEDIUM, text) == 0) break;
  }
  if (version > MAX_VERSION) return 0;

  const int n = qr.size;
  const int quiet = 4;
  const int scale = boxSize / (n + 2 * quiet);
  if (scale < 1) return 0;

  // A symbol only ever comes out a whole number of pixels per module, so what
  // gets drawn is boxSize rounded down -- by up to 35 px on a version 5 code.
  // Anchoring at (x, y) and calling the box centred left the symbol visibly off
  // to the left, and shifted it sideways whenever the payload changed version
  // (the wifi code and the link code behind "page didn't open?" differ).
  const int drawn = (n + 2 * quiet) * scale;
  const int pad = (boxSize - drawn) / 2;
  // The whole box is painted white, not just the symbol: a partial refresh
  // leaves ink behind, and a shorter payload draws a smaller code than the one
  // that was on the panel a moment ago.
  c.fillRect(x, y, boxSize, boxSize, false);
  const int x0 = x + pad + quiet * scale;
  const int y0 = y + pad + quiet * scale;
  for (int my = 0; my < n; my++)
    for (int mx = 0; mx < n; mx++)
      if (qrcode_getModule(&qr, mx, my))
        c.fillRect(x0 + mx * scale, y0 + my * scale, scale, scale, true);
  return drawn;
}

}  // namespace fqr
