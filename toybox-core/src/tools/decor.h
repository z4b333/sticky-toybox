// Decoration: the marks that make a game look like itself.
//
// One bit per pixel and no animation budget worth the name, so everything here
// has to work as a single still frame. That rules out motion blur, glows and
// easing, and leaves the tools an engraver would use -- rays, bursts, ripples,
// scattered debris, a rule with a diamond in it. Woodcut, not cartoon.
//
// Nothing in this file knows about a game. It draws shapes on a canvas, and the
// games decide what they mean.
#pragma once
#include <math.h>

#include "tools_ui.h"

namespace decor {

constexpr float TAU = 6.2831853f;

inline void diamond(ToolsCanvas& c, int cx, int cy, int r, bool black) {
  for (int dy = -r; dy <= r; dy++) {
    const int w = r - (dy < 0 ? -dy : dy);
    c.fillRect(cx - w, cy + dy, 2 * w + 1, 1, black);
  }
}

// Flat-shaded triangle, scanline filled. The one primitive the canvas does not
// already have, and the one every angular shape here is built from.
inline void triangle(ToolsCanvas& c, int x0, int y0, int x1, int y1, int x2, int y2,
                     bool black) {
  int X0 = x0, Y0 = y0, X1 = x1, Y1 = y1, X2 = x2, Y2 = y2, t;
  if (Y1 < Y0) { t = Y0; Y0 = Y1; Y1 = t; t = X0; X0 = X1; X1 = t; }
  if (Y2 < Y0) { t = Y0; Y0 = Y2; Y2 = t; t = X0; X0 = X2; X2 = t; }
  if (Y2 < Y1) { t = Y1; Y1 = Y2; Y2 = t; t = X1; X1 = X2; X2 = t; }
  if (Y2 == Y0) {
    const int lo = X0 < X1 ? (X0 < X2 ? X0 : X2) : (X1 < X2 ? X1 : X2);
    const int hi = X0 > X1 ? (X0 > X2 ? X0 : X2) : (X1 > X2 ? X1 : X2);
    c.fillRect(lo, Y0, hi - lo + 1, 1, black);
    return;
  }
  for (int y = Y0; y <= Y2; y++) {
    const int xLong = X0 + (int)((long)(X2 - X0) * (y - Y0) / (Y2 - Y0));
    const bool upper = y < Y1;
    const int ya = upper ? Y0 : Y1, yb = upper ? Y1 : Y2;
    const int xa = upper ? X0 : X1, xb = upper ? X1 : X2;
    const int xShort = (yb == ya) ? xb : xa + (int)((long)(xb - xa) * (y - ya) / (yb - ya));
    const int lo = xLong < xShort ? xLong : xShort;
    const int hi = xLong < xShort ? xShort : xLong;
    c.fillRect(lo, y, hi - lo + 1, 1, black);
  }
}

// A filled star with `spikes` points, as a fan of triangles from the centre.
// Adjacent triangles share an edge, so the edges come out dead straight and the
// fill has no seams -- interpolating a radius round the circle instead bulges
// every edge outward and the star comes out as a flower.
inline void star(ToolsCanvas& c, int cx, int cy, int outer, int inner, int spikes, float rot,
                 bool black) {
  const int n = 2 * spikes;
  int px[24], py[24];
  if (n > 24) return;
  for (int i = 0; i < n; i++) {
    const float a = rot + (float)i * TAU / (float)n;
    const int rr = (i & 1) ? inner : outer;
    px[i] = cx + (int)lroundf((float)rr * sinf(a));
    py[i] = cy - (int)lroundf((float)rr * cosf(a));
  }
  for (int i = 0; i < n; i++)
    triangle(c, cx, cy, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], black);
}

// The mark left where a shell lands.
//
// Every tutorial on drawing a bang says the same thing: the star has to be
// irregular. Spikes of one length at even angles read as a sparkle or a
// flower -- which is exactly what the first version of this looked like -- so
// both the angle and the length of every point are jittered, and the jitter is
// derived from a seed so a given square always blows up the same way.
inline void blast(ToolsCanvas& c, int cx, int cy, int r, uint32_t seed, bool black) {
  constexpr int SPIKES = 9;
  constexpr int N = 2 * SPIKES;
  int px[N], py[N];
  uint32_t s = seed * 2654435761u + 12345u;
  for (int i = 0; i < N; i++) {
    s = s * 1664525u + 1013904223u;
    const float wobble = ((float)((s >> 8) & 255) / 255.0f - 0.5f) * (TAU / (float)N) * 0.6f;
    const float a = (float)i * TAU / (float)N + wobble;
    // Tips reach 68-99% of the radius, the notches between them come back to
    // about a third. The gap between the two is what makes it read as spiky
    // rather than merely lumpy.
    const int rr = (i & 1) ? (r * 32) / 100 + (int)(((s >> 18) & 7) * r) / 100
                           : (r * 68) / 100 + (int)(((s >> 18) & 31) * r) / 100;
    px[i] = cx + (int)lroundf((float)rr * sinf(a));
    py[i] = cy - (int)lroundf((float)rr * cosf(a));
  }
  for (int i = 0; i < N; i++)
    triangle(c, cx, cy, px[i], py[i], px[(i + 1) % N], py[(i + 1) % N], black);
}

// A miss is a white peg in the board game: an empty hole, nothing more. One
// ring says "fired here, nothing there" without competing with the hits.
inline void peg(ToolsCanvas& c, int cx, int cy, int r, bool black) {
  // Deliberately small and light. A board fills up with misses, and if each one
  // is as loud as a hit the grid turns into wallpaper.
  c.drawCircle(cx, cy, r, 2, black);
}

// Bits thrown clear of a wreck. Deterministic -- the same cell always throws
// its debris the same way, so a redraw does not reshuffle the screen.
inline void debris(ToolsCanvas& c, int cx, int cy, int r, uint32_t seed, bool black) {
  uint32_t s = seed * 2654435761u + 1u;
  for (int i = 0; i < 7; i++) {
    s = s * 1664525u + 1013904223u;
    const float a = (float)(s >> 8 & 1023) * TAU / 1024.0f;
    const int d = (r * 6) / 10 + (int)((s >> 20 & 7) * r) / 12;
    const int size = 2 + (int)(s >> 18 & 1);
    c.fillRect(cx + (int)lroundf(d * sinf(a)) - size / 2,
               cy - (int)lroundf(d * cosf(a)) - size / 2, size, size, black);
  }
}

// Light coming off something. Alternating long and short spokes read as rays
// even when only a wedge of them is on screen.
inline void rays(ToolsCanvas& c, int cx, int cy, int from, int to, int n, float rot, bool black) {
  for (int i = 0; i < n; i++) {
    const float a = rot + (float)i * TAU / (float)n;
    const int len = (i & 1) ? (from + (to - from) / 2) : to;
    c.drawLine(cx + (int)lroundf(from * sinf(a)), cy - (int)lroundf(from * cosf(a)),
               cx + (int)lroundf(len * sinf(a)), cy - (int)lroundf(len * cosf(a)), 2, black);
  }
}

// A cog: a ring of square teeth with a hole punched through it.
//
// Built as one filled polygon rather than a circle with spokes poked out of it.
// The spoke version reads as a sun or an asterisk at this size, because thin
// radial lines against a thick rim lose their shape the moment the panel
// rounds them off. Square teeth on a solid body survive.
inline void gear(ToolsCanvas& c, int cx, int cy, int r, int teeth, bool black) {
  constexpr int MAXV = 48;
  const int n = teeth * 4;
  if (n > MAXV || teeth < 3) return;
  int px[MAXV], py[MAXV];

  const float pitch = TAU / (float)teeth;
  const float half = pitch * 0.26f;   // tooth is a little over half the pitch
  const int rin = (r * 74) / 100;     // body of the cog, under the teeth
  int k = 0;
  for (int i = 0; i < teeth; i++) {
    const float a = (float)i * pitch;
    const float ang[4] = {a - half, a + half, a + half, a + pitch - half};
    const int rad[4] = {r, r, rin, rin};
    for (int j = 0; j < 4; j++) {
      px[k] = cx + (int)lroundf((float)rad[j] * sinf(ang[j]));
      py[k] = cy - (int)lroundf((float)rad[j] * cosf(ang[j]));
      k++;
    }
  }
  for (int i = 0; i < n; i++)
    triangle(c, cx, cy, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], black);

  // The hole is what makes it a cog rather than a saw blade.
  c.fillCircle(cx, cy, (r * 34) / 100, !black);
}

// A rule with a diamond in the middle: the full stop of this whole vocabulary.
inline void ornament(ToolsCanvas& c, int cx, int y, int w, bool black) {
  const int arm = (w - 26) / 2;
  if (arm > 6) {
    c.fillRect(cx - w / 2, y, arm, 2, black);
    c.fillRect(cx + w / 2 - arm, y, arm, 2, black);
  }
  diamond(c, cx, y + 1, 5, black);
  c.fillRect(cx - 12, y + 1, 3, 1, black);
  c.fillRect(cx + 10, y + 1, 3, 1, black);
}

// Small marks thrown over an area to say something good just happened. Mixed
// shapes rather than one repeated -- a field of identical dots reads as a
// texture or a fault, not as celebration.
inline void confetti(ToolsCanvas& c, int x, int y, int w, int h, uint32_t seed, int n,
                     bool black) {
  uint32_t s = seed * 2246822519u + 374761393u;
  for (int i = 0; i < n; i++) {
    s = s * 1664525u + 1013904223u;
    const int px = x + (int)((s >> 8) % (uint32_t)(w > 1 ? w : 1));
    s = s * 1664525u + 1013904223u;
    const int py = y + (int)((s >> 8) % (uint32_t)(h > 1 ? h : 1));
    switch ((s >> 4) & 3) {
      case 0:
        diamond(c, px, py, 3, black);
        break;
      case 1:  // a plus
        c.fillRect(px - 4, py - 1, 9, 3, black);
        c.fillRect(px - 1, py - 4, 3, 9, black);
        break;
      case 2:
        c.fillRect(px - 2, py - 2, 5, 5, black);
        break;
      default:
        star(c, px, py, 6, 2, 4, 0.0f, black);
        break;
    }
  }
}

// The end-of-game announcement: a solid plaque with rays coming off both ends
// and a hairline inside it, so the text sits on something rather than floating.
inline void banner(ToolsCanvas& c, int x, int y, int w, int h, const char* text, TSize sz,
                   bool celebrate) {
  if (celebrate) {
    rays(c, x, y + h / 2, h / 2 + 6, h / 2 + 30, 9, TAU / 2.0f, true);
    rays(c, x + w, y + h / 2, h / 2 + 6, h / 2 + 30, 9, 0.0f, true);
  }
  c.fillRect(x, y, w, h, true);
  c.drawRect(x + 5, y + 5, w - 10, h - 10, 1, false);
  c.textInBox(x, y, w, h, text, sz, false, true);
}

}  // namespace decor
