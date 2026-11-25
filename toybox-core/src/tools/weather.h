// The weather outside, from Open-Meteo (C5 Climate; C6's charging dashboard
// reads the same).
//
// The place is a city name the owner types on the phone page (clock_web.h,
// "Weather place"). The first fetch looks it up once with Open-Meteo's
// geocoding API and keeps the coordinates; every fetch after asks only for
// the current weather there. No account, no key.
//
// Fetched only when asked (Climate's Refresh): the home Wi-Fi is joined for
// the one request and switched off again (home_net.h's rule). The last
// answer is kept in NVS with its time, so the screen has something to show
// the next time it opens.
//
// The parsing is plain text search on the two small replies, kept free of
// Arduino so the host test can run it against saved replies.
#pragma once
#include <Preferences.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "home_net.h"

#ifndef TOYBOX_HOST
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#endif

namespace wx {

inline constexpr const char* K_CITY = "wx_city";
inline constexpr const char* K_GEO = "wx_geo";
inline constexpr const char* K_NOW = "wx_now";
inline constexpr size_t CITY_MAX = 48;

struct Geo {
  int32_t lat, lon;  // degrees * 10000
  char city[CITY_MAX];   // the name as typed, so a new name looks up again
  char found[CITY_MAX];  // the name Open-Meteo matched, to show
};
struct Now {
  int16_t t;      // tenths of a degree C
  int16_t feels;  // tenths of a degree C
  uint8_t rh;
  uint8_t code;   // WMO weather code
  int16_t hhmm;   // local time of the reading, 1805 = 18:05; -1 unknown
};

// --- the place -----------------------------------------------------------------
inline bool haveCity(Preferences& p) { return p.isKey(K_CITY); }
inline void city(Preferences& p, char* out, size_t n) {
  out[0] = 0;
  if (p.isKey(K_CITY)) p.getString(K_CITY, out, n);
}
// A new place forgets the old coordinates and the old reading.
inline bool saveCity(Preferences& p, const char* name) {
  if (!name) return false;
  while (*name == ' ') name++;
  const size_t n = strlen(name);
  if (!n || n >= CITY_MAX) return false;
  p.putString(K_CITY, name);
  p.remove(K_GEO);
  p.remove(K_NOW);
  return true;
}
// A place picked from the phone page's suggestions (C5): the phone looked it
// up already, so its coordinates come with it and the Sticky never has to.
// `label` is what the screen shows ("Springfield, Illinois, United States"
// is cut to fit).
inline bool savePlace(Preferences& p, const char* name, int32_t lat, int32_t lon,
                      const char* label) {
  if (!saveCity(p, name)) return false;
  if (lat < -900000 || lat > 900000 || lon < -1800000 || lon > 1800000) return true;
  Geo g{};
  g.lat = lat;
  g.lon = lon;
  p.getString(K_CITY, g.city, sizeof(g.city));
  snprintf(g.found, sizeof(g.found), "%s", label && label[0] ? label : g.city);
  p.putBytes(K_GEO, &g, sizeof(g));
  return true;
}
inline bool lastNow(Preferences& p, Now& n) {
  return p.getBytesLength(K_NOW) == sizeof(n) && p.getBytes(K_NOW, &n, sizeof(n)) == sizeof(n);
}
inline bool geo(Preferences& p, Geo& g) {
  return p.getBytesLength(K_GEO) == sizeof(g) && p.getBytes(K_GEO, &g, sizeof(g)) == sizeof(g);
}

// --- what a code means -------------------------------------------------------------
// WMO weather interpretation codes, as Open-Meteo documents them.
inline const char* codeWord(int c) {
  if (c == 0) return "Clear";
  if (c == 1) return "Mostly clear";
  if (c == 2) return "Partly cloudy";
  if (c == 3) return "Cloudy";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c == 85 || c == 86) return "Snow showers";
  if (c >= 95) return "Thunderstorm";
  return "-";
}

// --- reading the replies ---------------------------------------------------------
// The number after "key": inside [from, ...). False when the key is absent or
// is followed by something that is not a number (the units block has strings).
inline bool numberAfter(const char* from, const char* key, float& out) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* k = strstr(from, pat);
  if (!k) return false;
  k += strlen(pat);
  while (*k == ' ') k++;
  char* end = nullptr;
  out = strtof(k, &end);
  return end && end != k;
}
inline bool stringAfter(const char* from, const char* key, char* out, size_t n) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* k = strstr(from, pat);
  if (!k) return false;
  k += strlen(pat);
  size_t i = 0;
  while (*k && *k != '"' && i + 1 < n) out[i++] = *k++;
  out[i] = 0;
  return i > 0;
}

// The geocoding reply: the first of "results".
inline bool parseGeo(const char* json, Geo& g) {
  const char* r = strstr(json, "\"results\"");
  if (!r) return false;
  float lat, lon;
  if (!numberAfter(r, "latitude", lat) || !numberAfter(r, "longitude", lon)) return false;
  g.lat = (int32_t)lroundf(lat * 10000.0f);
  g.lon = (int32_t)lroundf(lon * 10000.0f);
  if (!stringAfter(r, "name", g.found, sizeof(g.found))) g.found[0] = 0;
  return true;
}

// The forecast reply's "current" block (not "current_units", which comes
// first and holds the same keys with strings).
inline bool parseNow(const char* json, Now& n) {
  const char* c = strstr(json, "\"current\":{");
  if (!c) return false;
  float t, rh, feels, code;
  if (!numberAfter(c, "temperature_2m", t) || !numberAfter(c, "relative_humidity_2m", rh) ||
      !numberAfter(c, "weather_code", code))
    return false;
  if (!numberAfter(c, "apparent_temperature", feels)) feels = t;
  n.t = (int16_t)lroundf(t * 10.0f);
  n.feels = (int16_t)lroundf(feels * 10.0f);
  n.rh = (uint8_t)(rh < 0 ? 0 : rh > 100 ? 100 : lroundf(rh));
  n.code = (uint8_t)code;
  n.hhmm = -1;
  char when[24];
  // "2026-11-25T18:00": local time at the place (timezone=auto).
  if (stringAfter(c, "time", when, sizeof(when))) {
    const char* tpos = strchr(when, 'T');
    if (tpos && strlen(tpos) >= 6) n.hhmm = (int16_t)(atoi(tpos + 1) * 100 + atoi(tpos + 4));
  }
  return true;
}

inline void urlEncode(const char* in, char* out, size_t n) {
  static const char* DIGITS = "0123456789ABCDEF";  // (not HEX: an Arduino macro)
  size_t o = 0;
  for (const unsigned char* s = (const unsigned char*)in; *s && o + 4 < n; s++) {
    if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
        *s == '-' || *s == '_' || *s == '.') {
      out[o++] = (char)*s;
    } else {
      out[o++] = '%';
      out[o++] = DIGITS[*s >> 4];
      out[o++] = DIGITS[*s & 15];
    }
  }
  out[o] = 0;
}

// --- fetching ----------------------------------------------------------------------
enum class Result : uint8_t { Ok, NoWifiSaved, NoCity, NoNetwork, UnknownCity, NoAnswer };
inline const char* describe(Result r) {
  switch (r) {
    case Result::Ok: return "updated";
    case Result::NoWifiSaved: return "save your home Wi-Fi first (Settings > Clock)";
    case Result::NoCity: return "set a place first: More > Outdoor place";
    case Result::NoNetwork: return "the home Wi-Fi did not answer";
    case Result::UnknownCity: return "that place was not found - check the name";
    default: return "the weather service did not answer";
  }
}

#ifdef TOYBOX_HOST
inline Result g_hostResult = Result::Ok;
inline Now g_hostNow{312, 355, 70, 2, 1805};
inline int g_hostFetches = 0;
inline Result fetch(Preferences& p, Now& out) {
  g_hostFetches++;
  if (!homenet::haveWifi(p)) return Result::NoWifiSaved;
  if (!haveCity(p)) return Result::NoCity;
  if (g_hostResult != Result::Ok) return g_hostResult;
  Geo g{};
  if (!geo(p, g)) {
    city(p, g.city, sizeof(g.city));
    g.lat = 137563;
    g.lon = 1005018;
    snprintf(g.found, sizeof(g.found), "%s", g.city);
    p.putBytes(K_GEO, &g, sizeof(g));
  }
  out = g_hostNow;
  p.putBytes(K_NOW, &out, sizeof(out));
  return Result::Ok;
}
#else
// GET over HTTPS into `body` (small replies only). False on any failure.
inline bool get(const char* url, String& body) {
  NetworkClientSecure sec;
  sec.setInsecure();  // public data, only shown; nothing is sent but a place
  HTTPClient http;
  http.setTimeout(12000);
  http.setConnectTimeout(12000);
  if (!http.begin(sec, url)) return false;
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return body.length() > 0 && body.length() < 16000;
}

inline Result fetch(Preferences& p, Now& out) {
  if (!homenet::haveWifi(p)) return Result::NoWifiSaved;
  if (!haveCity(p)) return Result::NoCity;
  if (homenet::connect(p) != homenet::Result::Ok) {
    homenet::off();
    return Result::NoNetwork;
  }
  Result r = Result::Ok;
  Geo g{};
  char name[CITY_MAX];
  city(p, name, sizeof(name));
  String body;
  if (!geo(p, g) || strcmp(g.city, name) != 0) {
    char enc[CITY_MAX * 3 + 1], url[256];
    urlEncode(name, enc, sizeof(enc));
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&format=json", enc);
    if (!get(url, body)) {
      r = Result::NoAnswer;
    } else if (!parseGeo(body.c_str(), g)) {
      r = Result::UnknownCity;
    } else {
      snprintf(g.city, sizeof(g.city), "%s", name);
      p.putBytes(K_GEO, &g, sizeof(g));
    }
  }
  if (r == Result::Ok) {
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
             "&timezone=auto",
             g.lat / 10000.0, g.lon / 10000.0);
    body = String();
    if (!get(url, body) || !parseNow(body.c_str(), out))
      r = Result::NoAnswer;
    else
      p.putBytes(K_NOW, &out, sizeof(out));
  }
  homenet::off();
  return r;
}
#endif

}  // namespace wx
