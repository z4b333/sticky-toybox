// Level (C5): from three accelerometer axes to the angles a spirit level
// shows. No display, no sensor -- the host test feeds it gravity vectors.
//
// Axes as sensors.cpp reads them (+/-2 g, 16384 counts = 1 g). Held upright
// in portrait, gravity points down the panel: ay ~ -1 g (sensors.cpp
// orientation()). Lying face up on a table, gravity is on z.
//
// Two ways to use it, chosen by how it is held:
//  - FLAT on a surface: two angles, how far each panel axis is from level,
//    shown as a bubble in a round vial;
//  - on an EDGE (standing up): one angle, how far that edge is from level,
//    shown as a bar vial along the edge. The edge is whichever of the four
//    the device stands on; the screen turns to match it.
#pragma once
#include <math.h>
#include <stdint.h>

namespace lvl {

enum Mode : uint8_t { FLAT = 0, EDGE = 1 };

struct Reading {
  Mode mode;
  int rot;    // EDGE: which edge is down, as a display rotation 0..3
  int x, y;   // FLAT: tilt along the panel's x and y, tenths of a degree
  int edge;   // EDGE: the edge's tilt from level, tenths of a degree
};

inline int deci(float deg) { return (int)lroundf(deg * 10.0f); }

// Gravity more than 45 degrees out of the panel's plane counts as lying flat.
// `prev` (the last reading's mode, or -1) adds hysteresis: near 45 degrees the
// screen would otherwise flip between the two faces on every sample.
inline Reading read(int ax, int ay, int az, int prev = -1) {
  Reading r{};
  const float fx = (float)ax, fy = (float)ay, fz = (float)az;
  const float inPlane = sqrtf(fx * fx + fy * fy);
  const float k = prev == FLAT ? 0.75f : prev == EDGE ? 1.33f : 1.0f;
  if (fabsf(fz) >= inPlane * k) {
    r.mode = FLAT;
    // Each axis's tilt is its share of gravity against z, signed so that a
    // positive angle means that end of the panel is raised.
    const float az1 = fabsf(fz) < 1.0f ? 1.0f : fabsf(fz);
    r.x = deci(atan2f(-fx, az1) * 57.29578f);
    r.y = deci(atan2f(-fy, az1) * 57.29578f);
    r.rot = -1;
    return r;
  }
  r.mode = EDGE;
  // The direction of gravity in the panel's plane, 0 when it points straight
  // down a portrait panel (ay negative), increasing clockwise.
  float a = atan2f(fx, -fy) * 57.29578f;  // -180..180
  if (a < 0) a += 360.0f;
  // The nearest quarter is the edge it stands on; the rest is the tilt.
  int q = (int)lroundf(a / 90.0f) % 4;
  float off = a - q * 90.0f;
  if (off > 180.0f) off -= 360.0f;
  // Rotation in sensors.cpp's sense: 0 portrait, 1 = 90 cw (ax negative), ...
  static const int ROT[4] = {0, 3, 2, 1};
  r.rot = ROT[q];
  r.edge = deci(off);
  return r;
}

// Within half a degree reads as level: the panel cannot show better, and a
// shelf does not need it.
constexpr int LEVEL_DECI = 5;
inline bool isLevel(const Reading& r, int ox = 0, int oy = 0, int oe = 0) {
  if (r.mode == FLAT) {
    const int dx = r.x - ox, dy = r.y - oy;
    return dx >= -LEVEL_DECI && dx <= LEVEL_DECI && dy >= -LEVEL_DECI && dy <= LEVEL_DECI;
  }
  const int d = r.edge - oe;
  return d >= -LEVEL_DECI && d <= LEVEL_DECI;
}

// How often the screen is redrawn, ms (owner: a setting).
constexpr int RATES = 3;
constexpr int RATE_MS[RATES] = {500, 1000, 2000};
inline const char* rateName(int i) { return i == 0 ? "0.5 s" : i == 1 ? "1 s" : "2 s"; }

}  // namespace lvl
