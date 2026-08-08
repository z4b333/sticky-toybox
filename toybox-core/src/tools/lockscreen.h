// The screen that is on the panel when the device is off.
//
// E-paper holds its last image with no power, so this is the screen the device
// spends almost all of its life showing. It is worth settings of its own: how
// long before it appears, what it says when there is no note pinned, and what
// the footer carries.
//
// The core decides what the lock screen looks like; the firmware supplies the
// three things only it can know -- the time, the temperature and the charge --
// through the Info hook. A host with no clock leaves those absent and the
// screens quietly shorten rather than lying.
#pragma once
#include <Preferences.h>
#include <stdio.h>

#include "decor.h"
#include "tools_draw.h"
#include "tools_ui.h"

namespace lock {

enum Empty : uint8_t { EMPTY_CLOCK = 0, EMPTY_GOODBYE = 1, EMPTY_BLANK = 2 };
enum Wake : uint8_t { WAKE_NOTE = 0, WAKE_HUB = 1 };

// Zero is never. Five minutes suits a magnet on a fridge; a device sitting on a
// desk between turns of a game wants longer, which is the whole reason this is
// a setting.
inline constexpr int SLEEP_MINUTES[] = {0, 1, 5, 15, 30};
inline constexpr int SLEEP_COUNT = (int)(sizeof(SLEEP_MINUTES) / sizeof(SLEEP_MINUTES[0]));

struct Config {
  uint8_t sleepIdx = 2;  // index into SLEEP_MINUTES; 5 minutes
  uint8_t empty = EMPTY_CLOCK;
  bool showTime = true;
  bool showTemp = true;
  bool showBattery = true;
  bool autoRotate = true;
  uint8_t wake = WAKE_NOTE;
};

inline Config load(Preferences& p) {
  Config c;
  c.sleepIdx = (uint8_t)p.getInt("ls_sleep", 2);
  if (c.sleepIdx >= SLEEP_COUNT) c.sleepIdx = 2;
  c.empty = (uint8_t)p.getInt("ls_empty", EMPTY_CLOCK);
  if (c.empty > EMPTY_BLANK) c.empty = EMPTY_CLOCK;
  c.showTime = p.getBool("ls_time", true);
  c.showTemp = p.getBool("ls_temp", true);
  c.showBattery = p.getBool("ls_batt", true);
  c.autoRotate = p.getBool("ls_rot", true);
  c.wake = (uint8_t)p.getInt("ls_wake", WAKE_NOTE);
  if (c.wake > WAKE_HUB) c.wake = WAKE_NOTE;
  return c;
}

inline void save(Preferences& p, const Config& c) {
  p.putInt("ls_sleep", c.sleepIdx);
  p.putInt("ls_empty", c.empty);
  p.putBool("ls_time", c.showTime);
  p.putBool("ls_temp", c.showTemp);
  p.putBool("ls_batt", c.showBattery);
  p.putBool("ls_rot", c.autoRotate);
  p.putInt("ls_wake", c.wake);
}

// The live copy. The settings page writes NVS and updates this in the same
// breath, so a change takes effect on the next paint rather than the next boot,
// and nothing has to thread a Preferences reference through the draw calls.
inline Config g_config;
inline const Config& config() { return g_config; }
inline void setConfig(const Config& c) { g_config = c; }
inline void apply(Preferences& p) { g_config = load(p); }

inline uint32_t sleepMs(const Config& c) {
  return (uint32_t)SLEEP_MINUTES[c.sleepIdx] * 60u * 1000u;
}

inline const char* sleepLabel(const Config& c) {
  switch (SLEEP_MINUTES[c.sleepIdx]) {
    case 0: return "never";
    case 1: return "1 minute";
    case 5: return "5 minutes";
    case 15: return "15 minutes";
    default: return "30 minutes";
  }
}

// --- what only the firmware knows --------------------------------------------

struct Info {
  bool haveClock = false;
  int hour = 0, minute = 0, day = 1, month = 1, year = 2026;
  bool haveTemp = false;
  int tempDeciC = 0;
  bool haveBattery = false;
  int batteryPct = -1;
  bool charging = false;
};

using InfoFn = void (*)(Info&);
inline InfoFn g_info = nullptr;
inline void setInfoHook(InfoFn f) { g_info = f; }
inline Info read() {
  Info i;
  if (g_info) g_info(i);
  return i;
}

// --- the pieces both lock screens are made of ---------------------------------

inline int roundedC(int deciC) { return (deciC + (deciC < 0 ? -5 : 5)) / 10; }

// "12:34 · 21°C · 84%", with any part the settings or the hardware leave out
// simply absent. Returns the length written, 0 for nothing at all.
inline int footer(char* out, int cap, const Config& cfg, const Info& in) {
  out[0] = 0;
  int n = 0;
  const auto add = [&](const char* fmt, int v) {
    if (n) n += snprintf(out + n, cap - n, "  ·  ");
    n += snprintf(out + n, cap - n, fmt, v);
  };
  if (cfg.showTime && in.haveClock) {
    if (n) n += snprintf(out + n, cap - n, "  ·  ");
    n += snprintf(out + n, cap - n, "%02d:%02d", in.hour, in.minute);
  }
  // One decimal is more precision than a room deserves; the whole degree reads
  // faster from across the kitchen.
  if (cfg.showTemp && in.haveTemp) add("%d\xc2\xb0" "C", roundedC(in.tempDeciC));
  if (cfg.showBattery && in.haveBattery && in.batteryPct >= 0) add("%d%%", in.batteryPct);
  return n;
}

inline const char* MONTHS[12] = {"January", "February", "March",     "April",   "May",
                                 "June",    "July",     "August",    "September",
                                 "October", "November", "December"};
inline const char* DAYS[7] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                              "Thursday", "Friday", "Saturday"};

// Sakamoto's method. The RTC counts a date but not a weekday, and a lock screen
// that says only the number is a worse clock than the one on the wall.
inline int weekday(int y, int m, int d) {
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 1 || m > 12) return 0;
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// --- the panel with no note on it ---------------------------------------------

// Drawn edge to edge, no chrome. This is a device that holds an image with the
// power off, so with nothing pinned the best thing it can be is a clock.
inline void drawClock(ToolsCanvas& c, const Config& cfg, const Info& in) {
  const int W = c.width(), H = c.height();
  char buf[64];

  if (!in.haveClock) {
    // No RTC, or one that has never been set. Saying nothing is better than
    // showing 00:00 as though it meant something.
    c.textTrackedCentered(W / 2, H / 2 - 60, "TOYBOX", TS_HUGE, true, true, 6);
    decor::ornament(c, W / 2, H / 2 + 10, 300, true);
    c.textCentered(W / 2, H / 2 + 40, "press power to wake", TS_MED, true);
    return;
  }

  // 130 px digits are as large as four of them plus a colon fit across a 480 px
  // panel, and at 235 DPI that is 14 mm -- readable from the other side of a
  // room, which is where a fridge is read from.
  snprintf(buf, sizeof(buf), "%02d:%02d", in.hour, in.minute);
  tdraw::seg7Centered(c, W / 2, 250, 130, buf, true);

  snprintf(buf, sizeof(buf), "%s %d %s", DAYS[weekday(in.year, in.month, in.day)], in.day,
           MONTHS[(in.month >= 1 && in.month <= 12) ? in.month - 1 : 0]);
  c.textCentered(W / 2, 430, buf, TS_LARGE, true, true);

  // Everything under the rule is optional, and the rule only earns its place if
  // something is left to sit beneath it.
  char line[48];
  Config sub = cfg;
  sub.showTime = false;  // the time is the point of the screen above
  if (footer(line, sizeof(line), sub, in) > 0) {
    decor::ornament(c, W / 2, 494, 300, true);
    c.textCentered(W / 2, 524, line, TS_LARGE, true);
  }

  c.textCentered(W / 2, H - 60, "press power to wake", TS_SMALL, true);
}

}  // namespace lock
