// The "from your phone" page shared by Recipes, Flashcards and Picker, laid
// out from the kit's "Add a recipe" / "Add a deck": a QR with a shadow, one
// bold line, the address in a dotted box, three numbered steps, Done.
//
// On the home Wi-Fi the QR is the page's address (toybox.local/...); on the
// Sticky's own access point it is the network to join, as before. A quiet
// button under the steps switches to the backup.
#pragma once
#include "flash_qr.h"
#include "tools_draw.h"

namespace importui {
inline constexpr int QR_X = 110, QR_Y = 76, QR_SIZE = 260;
inline constexpr TRect ALT_BTN{24, 620, 432, 56};
inline constexpr TRect DONE_BTN{24, 700, 432, 72};

struct Words {
  const char* scanHome;      // "Scan to paste a recipe"
  const char* steps[3];
};

// "Joining Home-2.4..." while the radio looks for the router.
inline void drawJoining(ToolsCanvas& c, const char* wifi) {
  c.textCentered(c.width() / 2, 300, "Joining", TS_MED, true);
  c.textCentered(c.width() / 2, 340, wifi, TS_LARGE, true, true);
  c.textCentered(c.width() / 2, 400, "the home Wi-Fi, up to 15 s", TS_MED, true);
}

inline void drawSteps(ToolsCanvas& c, int y0, const char* const steps[3]) {
  char n[4];
  for (int i = 0; i < 3; i++) {
    const int y = y0 + i * 44;
    c.drawCircle(38, y + 14, 14, 2, true);
    snprintf(n, sizeof(n), "%d", i + 1);
    c.textCentered(38, y + 2, n, TS_MED, true, true);
    c.textClipped(64, y, 456 - 64, steps[i], TS_MED, true);
  }
}

// The QR in a frame with a hard shadow, as the kit draws it.
inline void drawQr(ToolsCanvas& c, const char* payload) {
  const int fx = QR_X - 14, fy = QR_Y - 14, fs = QR_SIZE + 28;
  c.fillRect(fx + 6, fy + 6, fs, fs, true);
  c.fillRect(fx, fy, fs, fs, false);
  c.drawRect(fx, fy, fs, fs, 3, true);
  fqr::draw(c, QR_X, QR_Y, QR_SIZE, payload);
}

// The address in a dotted box, without "http://".
inline void drawAddress(ToolsCanvas& c, int cy, const char* url) {
  const char* shown = strncmp(url, "http://", 7) == 0 ? url + 7 : url;
  const int w = c.textWidth(shown, TS_MED) + 28, h = 40, x = c.width() / 2 - w / 2;
  for (int i = 0; i < w; i += 6) {
    c.fillRect(x + i, cy, 3, 2, true);
    c.fillRect(x + i, cy + h - 2, 3, 2, true);
  }
  for (int i = 0; i < h; i += 6) {
    c.fillRect(x, cy + i, 2, 3, true);
    c.fillRect(x + w - 2, cy + i, 2, 3, true);
  }
  c.textInBox(x, cy, w, h, shown, TS_MED, true);
}

// The whole page. `note` is a line about the backup ("the home Wi-Fi did not
// answer"), or null.
template <class Net>
inline void draw(ToolsCanvas& c, Net& net, bool altQr, const Words& w, const char* note) {
  if (net.home()) {
    drawQr(c, net.homeUrl());
    c.textCentered(c.width() / 2, QR_Y + QR_SIZE + 26, w.scanHome, TS_MED, true, true);
    drawAddress(c, QR_Y + QR_SIZE + 62, net.homeUrl());
    char ip[48];
    snprintf(ip, sizeof(ip), "or %s", strncmp(net.ipUrl(), "http://", 7) == 0 ? net.ipUrl() + 7 : net.ipUrl());
    c.textCentered(c.width() / 2, QR_Y + QR_SIZE + 108, ip, TS_MED, true);
    drawSteps(c, QR_Y + QR_SIZE + 150, w.steps);
    c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "Use the Sticky's own Wi-Fi", false, TS_MED);
  } else {
    char buf[64];
    if (altQr) {
      drawQr(c, net.url());
      c.textCentered(c.width() / 2, QR_Y + QR_SIZE + 26, "Open this on your phone", TS_MED, true, true);
      drawAddress(c, QR_Y + QR_SIZE + 62, net.url());
    } else {
      const String wifi = net.wifiPayload();
      drawQr(c, wifi.c_str());
      c.textCentered(c.width() / 2, QR_Y + QR_SIZE + 26, "Scan to join the Sticky's Wi-Fi", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   key %s", net.ssid(), net.password());
      c.textCentered(c.width() / 2, QR_Y + QR_SIZE + 70, buf, TS_MED, true);
    }
    if (note) c.textCentered(c.width() / 2, QR_Y + QR_SIZE + 108, note, TS_MED, true, true);
    drawSteps(c, QR_Y + QR_SIZE + 150, w.steps);
    c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, altQr ? "Back to the Wi-Fi code" : "Page didn't open?",
             false, TS_MED);
  }
  c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Done", true, TS_LARGE);
}
}  // namespace importui
