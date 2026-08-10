// The drawn marks the home screen and the first-boot tour share: the three
// folder icons, the two hold hints, and the power symbol. One copy, because
// the tour's whole job is to teach the exact glyphs the home screen uses, and
// two copies would drift apart the first time one of them was tuned.
#pragma once
#include <math.h>

#include "tools/decor.h"
#include "tools/tools_draw.h"
#include "tools/tools_ui.h"

namespace hubmarks {

// Everything drawn over the wallpaper is white with a black edge: the only
// treatment that survives both a bright sky and a black mountain, because it
// carries its own contrast either way. The edge is the same drawing repeated
// at twelve offsets in black, then once in white on top -- crude, but it runs
// once per visit beside a 1.7 s refresh, and it needs no plate.
template <typename F>
inline void haloed(F draw) {
  static constexpr int8_t OFF[][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2},  {-2, -1}, {-2, 1},
                                      {2, -1}, {2, 1}, {-1, -2}, {1, -2}, {-1, 2},  {1, 2}};
  for (const auto& o : OFF) draw(o[0], o[1], true);
  draw(0, 0, false);
}

// The dock marks follow the interface's "minimal pass": thin strokes, no
// filled bodies beyond a pip or a ball, the least ink that still names the
// folder.

// A joystick: ball, stick, flat base line.
inline void play(ToolsCanvas& c, int cx, int cy, int s, bool black) {
  c.fillCircle(cx, cy - (s * 26) / 100, (s * 17) / 100, black);
  c.drawLine(cx, cy - (s * 12) / 100, cx, cy + (s * 26) / 100, 3, black);
  c.drawLine(cx - (s * 30) / 100, cy + (s * 28) / 100, cx + (s * 30) / 100,
             cy + (s * 28) / 100, 3, black);
}

// A die showing five, thin-walled.
inline void decide(ToolsCanvas& c, int cx, int cy, int s, bool black) {
  const int r = (s * 42) / 100;
  tdraw::roundRect(c, cx - r, cy - r, 2 * r, 2 * r, (s * 14) / 100, 2, black);
  static constexpr int8_t P[][2] = {{-20, -20}, {20, -20}, {0, 0}, {-20, 20}, {20, 20}};
  for (const auto& p : P)
    c.fillCircle(cx + (p[0] * s) / 100, cy + (p[1] * s) / 100, (s * 7) / 100, black);
}

// An open book: two thin pages either side of a spine gap, a line of text on
// each.
inline void study(ToolsCanvas& c, int cx, int cy, int s, bool black) {
  const int w = (s * 36) / 100, h = (s * 42) / 100, gap = (s * 6) / 100;
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    const int x0 = sgn < 0 ? cx - gap - w : cx + gap;
    c.drawRect(x0, cy - h / 2 - (s * 4) / 100, w, h, 2, black);
    c.drawLine(x0 + (w * 20) / 100, cy - (s * 8) / 100, x0 + (w * 80) / 100,
               cy - (s * 8) / 100, 2, black);
    c.drawLine(x0 + (w * 20) / 100, cy + (s * 2) / 100, x0 + (w * 80) / 100,
               cy + (s * 2) / 100, 2, black);
  }
  c.drawLine(cx, cy - h / 2 - (s * 4) / 100, cx, cy + h / 2 - (s * 4) / 100, 2, black);
}

inline void folder(ToolsCanvas& c, int f, int cx, int cy, int s, bool black) {
  switch (f) {
    case 0: play(c, cx, cy, s, black); break;
    case 1: decide(c, cx, cy, s, black); break;
    default: study(c, cx, cy, s, black); break;
  }
}

// An arrow doubling back on itself: go on with what you were doing. Nearly a
// full circle with the head on the open end, travelling backwards, which is
// the difference between this and a play triangle.
inline void resume(ToolsCanvas& c, int cx, int cy, int r, bool black) {
  constexpr float DEG = 6.2831853f / 360.0f;
  const float a0 = -40.0f * DEG, a1 = 250.0f * DEG;
  constexpr int STEPS = 18;
  int px = 0, py = 0;
  for (int i = 0; i <= STEPS; i++) {
    const float a = a0 + (a1 - a0) * (float)i / STEPS;
    const int x = cx + (int)lroundf(cosf(a) * (float)(r - 2));
    const int y = cy + (int)lroundf(sinf(a) * (float)(r - 2));
    if (i) c.drawLine(px, py, x, y, 3, black);
    px = x;
    py = y;
  }
  // The head sits on the open end, pointing along the direction of travel.
  const int hx = cx + (int)lroundf(cosf(a0) * (float)(r - 2));
  const int hy = cy + (int)lroundf(sinf(a0) * (float)(r - 2));
  const float dx = sinf(a0), dy = -cosf(a0);  // tangent, from a1 towards a0
  const float ex = -dy, ey = dx;              // perpendicular
  const float h = (float)r * 0.66f;
  decor::triangle(c, (int)lroundf(hx + dx * h), (int)lroundf(hy + dy * h),
                  (int)lroundf(hx + ex * h * 0.62f - dx * h * 0.25f),
                  (int)lroundf(hy + ey * h * 0.62f - dy * h * 0.25f),
                  (int)lroundf(hx - ex * h * 0.62f - dx * h * 0.25f),
                  (int)lroundf(hy - ey * h * 0.62f - dy * h * 0.25f), black);
}

// The universal power mark: a ring with a notch at the top and a bar through
// the notch. Drawn as segments so the notch is a real gap, not an erasure that
// would punch a hole in whatever sits behind it.
inline void power(ToolsCanvas& c, int cx, int cy, int r, bool black) {
  constexpr float DEG = 6.2831853f / 360.0f;
  const float a0 = -60.0f * DEG, a1 = 240.0f * DEG;
  constexpr int STEPS = 18;
  int px = 0, py = 0;
  for (int i = 0; i <= STEPS; i++) {
    const float a = a0 + (a1 - a0) * (float)i / STEPS;
    const int x = cx + (int)lroundf(cosf(a) * (float)(r - 2));
    const int y = cy + (int)lroundf(sinf(a) * (float)(r - 2));
    if (i) c.drawLine(px, py, x, y, 3, black);
    px = x;
    py = y;
  }
  c.drawLine(cx, cy - r - 2, cx, cy - r / 4, 3, black);
}

}  // namespace hubmarks
