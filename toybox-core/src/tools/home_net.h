// The home Wi-Fi, and the one thing this firmware does with it so far: ask
// the internet what time it is.
//
// Everything else that touches the radio here is the device's OWN access
// point (portal.h) or ESP-NOW (link.h) -- networks with nobody else on them.
// This is the first time the device joins somebody's router, so the rules are
// written down once, here:
//
//  - The network is remembered in NVS (`hn_ssid`, `hn_pass`) and given by a
//    phone through the clock page; nothing is typed on the panel.
//  - The radio is up only while a job runs and is switched OFF afterwards,
//    whatever happened. A radio left on flattens the battery in days.
//  - Every wait has a deadline. A router that never answers costs the person
//    a screen that says so, never a device that hangs.
//
// The time zone (`tz`) is a POSIX rule string, e.g. "STD-7" for Bangkok or
// "STD-1DST,M3.5.0,M10.5.0/3" for Berlin. The phone's page builds it from the
// phone's own clock (clock_web.h), so there is no zone table in the firmware.
// It is needed because the clock chip keeps wall-clock digits (clock_set.h)
// while the internet hands out UTC.
//
// KOReader sync will join through connect()/off() below; nothing else should
// call WiFi.begin().
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clock_set.h"

#ifndef TOYBOX_HOST
#include <WiFi.h>
#include <esp_sntp.h>
#endif

namespace homenet {

inline constexpr const char* K_SSID = "hn_ssid";
inline constexpr const char* K_PASS = "hn_pass";
inline constexpr const char* K_TZ = "tz";

// 32 bytes of SSID and 63 of WPA2 passphrase, per the standard, plus the end.
inline constexpr size_t SSID_MAX = 33, PASS_MAX = 64, TZ_MAX = 64;

// --- what is remembered ----------------------------------------------------
// isKey first, everywhere: reading a key that is not there prints an NVS
// error line, and "no home Wi-Fi yet" is the normal state, not an error.

inline bool haveWifi(Preferences& p) { return p.isKey(K_SSID); }

inline void wifiName(Preferences& p, char* out, size_t n) {
  out[0] = 0;
  if (p.isKey(K_SSID)) p.getString(K_SSID, out, n);
}

// An empty name is not a network; an open network has an empty password.
inline bool saveWifi(Preferences& p, const char* ssid, const char* pass) {
  if (!ssid || !ssid[0] || strlen(ssid) >= SSID_MAX) return false;
  if (!pass) pass = "";
  if (strlen(pass) >= PASS_MAX) return false;
  // WPA2 refuses 1..7 characters; storing one would only fail later, at the
  // router, where the reason is much harder to show.
  if (pass[0] && strlen(pass) < 8) return false;
  p.putString(K_SSID, ssid);
  p.putString(K_PASS, pass);
  return true;
}

inline void forgetWifi(Preferences& p) {
  if (p.isKey(K_SSID)) p.remove(K_SSID);
  if (p.isKey(K_PASS)) p.remove(K_PASS);
}

inline bool haveTz(Preferences& p) { return p.isKey(K_TZ); }

// Only the characters a POSIX TZ rule is made of; anything else is refused
// rather than handed to tzset, whose behaviour on junk differs between libcs.
inline bool tzLooksValid(const char* tz) {
  if (!tz || !tz[0] || strlen(tz) >= TZ_MAX) return false;
  for (const char* s = tz; *s; s++) {
    const char ch = *s;
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == ':' ||
                    ch == ',' || ch == '.' || ch == '/' || ch == '<' || ch == '>';
    if (!ok) return false;
  }
  return true;
}

inline bool saveTz(Preferences& p, const char* tz) {
  if (!tzLooksValid(tz)) return false;
  p.putString(K_TZ, tz);
  return true;
}

inline void tz(Preferences& p, char* out, size_t n) {
  out[0] = 0;
  if (p.isKey(K_TZ)) p.getString(K_TZ, out, n);
}

// --- UTC -> the digits the clock chip keeps ---------------------------------

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). Here
// because newlib on the device has no timegm().
inline int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// A UTC instant, as milliseconds since 1970 SHIFTED into local wall-clock
// time under the POSIX rule `tzRule` -- the same number the phone's page
// sends (clock_web.h), so both roads into clockset::apply() carry one unit.
inline int64_t localEpochMs(time_t utc, const char* tzRule) {
  setenv("TZ", tzRule, 1);
  tzset();
  struct tm lt;
  localtime_r(&utc, &lt);
  const int64_t days = daysFromCivil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1,
                                     (unsigned)lt.tm_mday);
  return ((days * 24 + lt.tm_hour) * 60 + lt.tm_min) * 60000LL + lt.tm_sec * 1000LL;
}

// --- doing it ----------------------------------------------------------------

enum class Result : uint8_t {
  Ok,
  NoWifiSaved,   // nothing to join -- send it from the phone first
  NoTzSaved,     // the zone comes with the phone's time; send that once
  NotFound,      // the router did not answer to that name
  Refused,       // it answered and turned us away: almost always the password
  NoInternet,    // joined, but no time server replied
  ClockRefused,  // the time came; the clock chip would not keep it
};

inline const char* describe(Result r) {
  switch (r) {
    case Result::Ok: return "the clock is set";
    case Result::NoWifiSaved: return "no home wifi saved";
    case Result::NoTzSaved: return "no time zone saved";
    case Result::NotFound: return "home wifi not found";
    case Result::Refused: return "wifi password refused";
    case Result::NoInternet: return "no internet on that wifi";
    case Result::ClockRefused: return "the clock chip did not keep it";
  }
  return "";
}

inline constexpr uint32_t JOIN_MS = 15000, TIME_MS = 10000;

#ifdef TOYBOX_HOST

// The harness decides what the network does; the rest of the path -- the
// saved keys, the zone, the arithmetic, the clock hook -- is the real code.
inline Result g_hostNet = Result::Ok;
inline time_t g_hostUtc = 1787300000;  // 2026-08-21 08:13 UTC
inline int g_hostJoins = 0, g_hostOffs = 0;

inline Result connect(Preferences& p, uint32_t = JOIN_MS) {
  g_hostJoins++;
  if (!haveWifi(p)) return Result::NoWifiSaved;
  if (g_hostNet == Result::NotFound || g_hostNet == Result::Refused) {
    g_hostOffs++;  // the device switches the radio off on this path too
    return g_hostNet;
  }
  return Result::Ok;
}
inline void off() { g_hostOffs++; }
inline bool fetchUtc(time_t& utc, uint32_t = TIME_MS) {
  if (g_hostNet == Result::NoInternet) return false;
  utc = g_hostUtc;
  return true;
}

#else

// Joins the saved network. On anything but Ok the radio is already off.
inline Result connect(Preferences& p, uint32_t timeoutMs = JOIN_MS) {
  if (!haveWifi(p)) return Result::NoWifiSaved;
  char ssid[SSID_MAX], pass[PASS_MAX];
  p.getString(K_SSID, ssid, sizeof(ssid));
  pass[0] = 0;
  if (p.isKey(K_PASS)) p.getString(K_PASS, pass, sizeof(pass));

  WiFi.persistent(false);  // ours are in NVS already; no second copy in the driver's
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass[0] ? pass : nullptr);
  const uint32_t t0 = millis();
  wl_status_t st = WiFi.status();
  while (st != WL_CONNECTED && millis() - t0 < timeoutMs) {
    if (st == WL_CONNECT_FAILED) break;
    delay(100);
    st = WiFi.status();
  }
  if (st == WL_CONNECTED) return Result::Ok;
  const Result r = (st == WL_NO_SSID_AVAIL) ? Result::NotFound : Result::Refused;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return r;
}

inline void off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);  // the radio is the biggest draw on the board
}

// Asks the pool for UTC. Anything before 2025 is the chip's own uninitialised
// clock, not an answer.
inline bool fetchUtc(time_t& utc, uint32_t timeoutMs = TIME_MS) {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  const uint32_t t0 = millis();
  bool ok = false;
  while (millis() - t0 < timeoutMs) {
    utc = time(nullptr);
    if (utc > 1735689600) {  // 2025-01-01
      ok = true;
      break;
    }
    delay(100);
  }
  esp_sntp_stop();
  return ok;
}

#endif

// Join, ask, write the chip, radio off. Blocks for up to JOIN_MS + TIME_MS;
// the caller paints a "connecting" screen first.
inline Result syncClock(Preferences& p) {
  if (!haveWifi(p)) return Result::NoWifiSaved;
  char rule[TZ_MAX];
  tz(p, rule, sizeof(rule));
  if (!rule[0]) return Result::NoTzSaved;

  const Result j = connect(p);
  if (j != Result::Ok) return j;
  time_t utc = 0;
  const bool got = fetchUtc(utc);
  off();
  if (!got) return Result::NoInternet;
  return clockset::apply(localEpochMs(utc, rule)) ? Result::Ok : Result::ClockRefused;
}

}  // namespace homenet
