// The screen a freshly flashed device shows before anything else.
//
// It exists for one reason that is worth more than the greeting: on a board
// nobody has checked, the display can come up mirrored or upside down, and a
// new owner has no way to know that is fixable. They would conclude the
// firmware is broken and stop. So the first thing the device ever says is what
// the screen should look like and which buttons to hold if it does not.
//
// The version is here for the same reason it is on the service screen and on
// the installer page: three places saying the same string is how you tell that
// the thing you flashed is the thing that is running. That is also why this
// comes back after every update rather than only once -- the boot after a flash
// is exactly the moment you want the build number, and a welcome that appears
// then is proof the write took.
#pragma once
#include <Preferences.h>

#include "tools/decor.h"
#include "tools/tools_ui.h"

namespace welcome {

// The build string, as the service screen and version.json spell it.
inline const char* version() {
#ifdef TB_VERSION
  return TB_VERSION;
#else
  return "development build";
#endif
}

inline const char* built() {
#ifdef TB_DATE
  return TB_DATE;
#else
  return __DATE__;
#endif
}

// Shown when the stored string is not the one this firmware carries: a new
// device has nothing stored, and an updated one has the version before it.
inline bool pending(Preferences& p) {
  // isKey first: reading a key that is not there logs an NVS error, and on the
  // one boot where this matters most -- the first -- it is guaranteed not to be
  // there. A red line that means "this device is new" is a bad red line.
  if (!p.isKey("welcome")) return true;
  char seen[40] = {};
  p.getString("welcome", seen, sizeof(seen));
  return strncmp(seen, version(), sizeof(seen) - 1) != 0;
}

inline void markSeen(Preferences& p) { p.putString("welcome", version()); }

// Dismissed by any tap or button; the rectangle stays for older callers.
inline constexpr TRect BEGIN_BTN{0, 0, 480, 800};

// The TB basket with its sparkles (kit "SysWelcome").
inline void logo(ToolsCanvas& c, int x, int y) {
  // the basket: a rim, then a body that narrows to the foot
  c.fillRect(x, y + 14, 56, 6, true);
  for (int r = 0; r < 34; r++) {
    const int in = 4 + r / 6;
    c.fillRect(x + in, y + 20 + r, 56 - 2 * in, 1, true);
  }
  c.text(x + 10, y + 24, "TB", TS_MED, false, true);
  auto star = [&](int cx, int cy, int s) {
    c.drawLine(cx - s, cy, cx + s, cy, 2, true);
    c.drawLine(cx, cy - s, cx, cy + s, 2, true);
  };
  star(x + 28, y - 2, 8);
  star(x + 12, y + 6, 5);
  star(x + 46, y + 6, 5);
}

inline void render(ToolsCanvas& c, bool updated) {
  const int W = c.width();
  char buf[80];

  logo(c, 118, 34);
  c.textTracked(220, 30, "TOYBOX", TS_SMALL, true, true, 4);
  // "Hello" on a device that has been flashed twenty times reads as amnesia,
  // so it says which of the two things just happened.
  c.text(220, 52, updated ? "Updated" : "Hello", TS_LARGE, true, true);
  const int vy = 52 + c.textHeight(TS_LARGE) + 2;
  snprintf(buf, sizeof(buf), "%s \xc2\xb7 build %s", version(), built());
  if (c.textWidth(buf, TS_SMALL) <= W - 236) {
    c.text(220, vy, buf, TS_SMALL, true);
  } else {  // a long version name: two lines rather than off the edge
    c.textClipped(220, vy, W - 236, version(), TS_SMALL, true);
    snprintf(buf, sizeof(buf), "build %s", built());
    c.text(220, vy + c.textHeight(TS_SMALL) + 2, buf, TS_SMALL, true);
  }

  // The check: a drawn device the right way up, for the owner to compare.
  const int dx = 138, dy = 150, dw = 204, dh = 244;
  c.drawRect(dx, dy, dw, dh, 3, true);
  for (int k = 0; k < 3; k++) c.fillRect(dx + dw + 2, dy + 62 + k * 48, 12, 34, true);
  c.drawLine(dx + 16, dy + 20, dx + 23, dy + 27, 2, true);
  c.drawLine(dx + 16, dy + 20, dx + 22, dy + 20, 2, true);
  c.drawLine(dx + 16, dy + 20, dx + 16, dy + 26, 2, true);
  c.text(dx + 30, dy + 16, "this corner", TS_SMALL, true, true);
  c.textCentered(dx + dw / 2, dy + 96, "Aa", TS_HUGE, true, true);
  c.textCentered(dx + dw / 2, dy + 146, "reads left to right", TS_SMALL, true);

  c.textTracked(24, 426, "DOES IT LOOK LIKE THIS?", TS_SMALL, true, false, 2);
  for (int x = 24; x < W - 24; x += 4) c.fillRect(x, 446, 2, 2, true);
  c.text(24, 470, "Yes \xe2\x80\x94 tap anywhere", TS_MED, true);
  c.text(24, 470 + c.textHeight(TS_MED) + 2, "or press any button to start", TS_SMALL, true);
  c.fillRect(24, 520, W - 48, 1, true);
  c.text(24, 543, "Mirrored or upside down?", TS_MED, true);
  c.text(24, 543 + c.textHeight(TS_MED) + 2, "hold UP while switching on to fix it", TS_SMALL, true);
  c.fillRect(24, 593, W - 48, 1, true);

  for (int x = 0; x < W; x += 4) c.fillRect(x, 756, 2, 1, true);
  c.textCentered(W / 2, 770, "the version shows here after every update", TS_SMALL, true);
}

}  // namespace welcome
