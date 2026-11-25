// Climate (C5): what the SHT40's two numbers mean. No display, no sensor, so
// the host test can check the arithmetic against published tables.
//
// Temperatures in tenths of a degree C throughout, humidity in percent.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>

namespace clim {

// Saturation vapour pressure (Magnus, Sonntag 1990 constants), hPa.
inline float es(float tC) { return 6.112f * expf(17.62f * tC / (243.12f + tC)); }

// The Sticky reads warm while it works (radio, refreshes); the owner's offset
// moves the temperature, and the humidity is moved with it -- the same water
// in the air is a different percentage of what warmer or cooler air holds.
inline int correctedRh(int tDeci, int rh, int offsetDeci) {
  if (!offsetDeci) return rh;
  const float t = tDeci / 10.0f, tc = (tDeci + offsetDeci) / 10.0f;
  float r = rh * es(t) / es(tc);
  if (r < 0) r = 0;
  if (r > 100) r = 100;
  return (int)lroundf(r);
}

inline int dewPoint(int tDeci, int rh) {
  if (rh < 1) rh = 1;
  const float t = tDeci / 10.0f;
  const float g = logf(rh / 100.0f) + 17.62f * t / (243.12f + t);
  return (int)lroundf(10.0f * 243.12f * g / (17.62f - g));
}

// "Feels like": the heat index (NWS / Rothfusz) where it applies -- warm and
// humid -- and the air temperature itself everywhere else indoors.
inline int feelsLike(int tDeci, int rh) {
  const float t = tDeci / 10.0f;
  if (t < 26.7f || rh < 40) return tDeci;
  const float f = t * 1.8f + 32.0f, r = (float)rh;
  float hi = -42.379f + 2.04901523f * f + 10.14333127f * r - 0.22475541f * f * r -
             6.83783e-3f * f * f - 5.481717e-2f * r * r + 1.22874e-3f * f * f * r +
             8.5282e-4f * f * r * r - 1.99e-6f * f * f * r * r;
  return (int)lroundf((hi - 32.0f) / 1.8f * 10.0f);
}

// One word for the room.
inline const char* comfort(int tDeci, int rh) {
  if (tDeci >= 300) return "Hot";
  if (tDeci < 160) return "Cold";
  if (rh < 30) return "Dry";
  if (rh > 65) return "Humid";
  if (tDeci >= 280) return "Warm";
  if (tDeci < 190) return "Cool";
  return "Comfortable";
}

inline int toF(int tDeci) { return (int)lroundf(tDeci * 1.8f + 320.0f); }  // tenths of F

// "24.3" / "-0.5" from tenths, for the screen.
inline void fmt1(char* out, int n, int deci) {
  const int a = deci < 0 ? -deci : deci;
  snprintf(out, n, "%s%d.%d", deci < 0 ? "-" : "", a / 10, a % 10);
}

// The owner's offset steps: -3.0 to +1.0 in halves, -1.0 to start.
constexpr int OFFSETS = 9;
constexpr int OFFSET_DECI[OFFSETS] = {-30, -25, -20, -15, -10, -5, 0, 5, 10};
constexpr int OFFSET_DEFAULT = 4;  // -1.0

// How often the room is read (owner, 25 Nov: battery), and how often the
// outside is fetched while Climate is open.
constexpr int READS = 6;
constexpr int READ_S[READS] = {5, 30, 60, 300, 600, 1800};
inline const char* readName(int i) {
  static const char* N[READS] = {"5 s", "30 s", "1 min", "5 min", "10 min", "30 min"};
  return N[i < 0 || i >= READS ? 0 : i];
}
constexpr int OUTS = 3;
constexpr int OUT_MIN[OUTS] = {0, 10, 30};  // 0: only when Refresh is tapped
inline const char* outName(int i) { return i == 1 ? "every 10 min" : i == 2 ? "every 30 min" : "manual"; }

}  // namespace clim
