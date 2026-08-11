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

// EMPTY_CLOCK used to be 0, and it was the default. It is gone. E-paper holds
// its last image with no power, which is exactly why it makes a poor clock: the
// time is drawn on the way to sleep and is wrong a minute later, then stays
// wrong for however many hours the device sits there. Keeping it would have
// meant waking every minute to redraw, at 1.7 seconds of refresh a time, which
// is not a clock either -- it is a battery with a countdown on it.
//
// The other three keep their stored numbers so a device that has already been
// set keeps its setting. A device still set to 0 falls through to the default.
//
// EMPTY_COVER came last and is the only one that changes by itself: the panel
// wears the cover of whatever you are reading. A device that spends its life
// showing one image should show the image its owner is actually in the middle
// of, and a book cover is the one picture on the device that nobody had to
// choose.
enum Empty : uint8_t {
  EMPTY_PICTURE = 1,
  EMPTY_GOODBYE = 2,
  EMPTY_BLANK = 3,
  EMPTY_COVER = 4,
};
inline constexpr uint8_t EMPTY_FIRST = EMPTY_PICTURE;
inline constexpr uint8_t EMPTY_LAST = EMPTY_COVER;
inline constexpr int EMPTY_COUNT = 4;
enum Wake : uint8_t { WAKE_NOTE = 0, WAKE_HUB = 1 };

// Zero is never. Five minutes suits a magnet on a fridge; a device sitting on a
// desk between turns of a game wants longer, which is the whole reason this is
// a setting.
inline constexpr int SLEEP_MINUTES[] = {0, 1, 5, 15, 30};
inline constexpr int SLEEP_COUNT = (int)(sizeof(SLEEP_MINUTES) / sizeof(SLEEP_MINUTES[0]));

struct Config {
  uint8_t sleepIdx = 2;  // index into SLEEP_MINUTES; 5 minutes
  uint8_t empty = EMPTY_GOODBYE;
  bool showTime = true;
  bool showTemp = true;
  bool showBattery = true;
  bool autoRotate = true;
  // Which way up a pinned note rests when it is not following the device.
  // Chosen at the moment of pinning, where you can see the note the size it
  // will be rather than guessing from a settings row.
  uint8_t pinRotation = 0;
  uint8_t wake = WAKE_NOTE;
};

inline Config load(Preferences& p) {
  Config c;
  c.sleepIdx = (uint8_t)p.getInt("ls_sleep", 2);
  if (c.sleepIdx >= SLEEP_COUNT) c.sleepIdx = 2;
  c.empty = (uint8_t)p.getInt("ls_empty", EMPTY_GOODBYE);
  if (c.empty < EMPTY_FIRST || c.empty > EMPTY_LAST) c.empty = EMPTY_GOODBYE;
  c.pinRotation = (uint8_t)(p.getInt("ls_pinrot", 0) & 3);
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
  p.putInt("ls_pinrot", c.pinRotation);
  p.putInt("ls_wake", c.wake);
}

// The live copy. The settings page writes NVS and updates this in the same
// breath, so a change takes effect on the next paint rather than the next boot,
// and nothing has to thread a Preferences reference through the draw calls.
inline Config g_config;
inline const Config& config() { return g_config; }
inline void setConfig(const Config& c) { g_config = c; }
inline void apply(Preferences& p) { g_config = load(p); }

// Which way up the sleeping note should be drawn. Following the device wins
// when it is switched on and there is an accelerometer to follow; otherwise the
// angle chosen when the note was pinned.
inline uint8_t restRotation(const Config& c, bool haveImu, int fromImu) {
  if (c.autoRotate && haveImu) return (uint8_t)(fromImu & 3);
  return (uint8_t)(c.pinRotation & 3);
}

// Records the angle a note was pinned at. Kept in the live config as well as
// NVS, so the next paint uses it rather than the next boot.
inline void setPinRotation(Preferences& p, uint8_t rot) {
  g_config.pinRotation = (uint8_t)(rot & 3);
  p.putInt("ls_pinrot", g_config.pinRotation);
}

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

}  // namespace lock
