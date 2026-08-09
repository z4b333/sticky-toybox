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
  char seen[40] = {};
  p.getString("welcome", seen, sizeof(seen));
  return strncmp(seen, version(), sizeof(seen) - 1) != 0;
}

inline void markSeen(Preferences& p) { p.putString("welcome", version()); }

// The one button, low enough for a thumb holding the device.
inline constexpr TRect BEGIN_BTN{60, 640, 360, 84};

inline void render(ToolsCanvas& c, bool updated) {
  const int W = c.width();
  char buf[80];

  decor::ornament(c, W / 2, 96, 300, true);
  c.textTrackedCentered(W / 2, 140, "TOYBOX", TS_HUGE, true, true, 8);
  // "Hello" on a device that has been flashed twenty times reads as amnesia,
  // so it says which of the two things just happened.
  c.textCentered(W / 2, 200, updated ? "updated and ready" : "hello", TS_LARGE, true);

  snprintf(buf, sizeof(buf), "%s  ·  %s", version(), built());
  c.textCentered(W / 2, 244, buf, TS_MED, true);
  decor::ornament(c, W / 2, 292, 300, true);

  // The part that earns the screen. Phrased as something to check rather than
  // something to know: a new owner cannot be told their panel is wrong, but
  // they can be asked whether this text reads normally.
  c.textCentered(W / 2, 352, "Does this read the right way up,", TS_MED, true);
  c.textCentered(W / 2, 384, "with TOYBOX at the top?", TS_MED, true);

  c.textCentered(W / 2, 440, "If it is upside down or mirrored,", TS_MED, true);
  c.textCentered(W / 2, 472, "hold either side button while the", TS_MED, true);
  c.textCentered(W / 2, 504, "device powers on, and correct it", TS_MED, true);
  c.textCentered(W / 2, 536, "on the screen that appears.", TS_MED, true);

  c.button(BEGIN_BTN.x, BEGIN_BTN.y, BEGIN_BTN.w, BEGIN_BTN.h, "BEGIN", true, TS_LARGE);
  c.textCentered(W / 2, 752, "a side button works too", TS_SMALL, true);
}

}  // namespace welcome
