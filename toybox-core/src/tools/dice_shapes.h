// Wireframe polyhedra for the die-type selector: d4, d6, d8, d10, d12, d20.
//
// A row of shapes says which die you are picking faster than a row of "D4 D6
// D8" ever could, and it is the one place in the app where the object being
// chosen has a recognisable form of its own. Line art suits a one-bit panel:
// no fills to lose, no greys to fake.
//
// Every shape is drawn from a ring of vertices plus a few interior edges -- the
// silhouette of the solid seen corner-on, which is how these are always drawn
// on dice packaging. Each takes a `black` flag so a selected key can invert the
// whole thing to white on black.
#pragma once
#include <math.h>

#include "tools_ui.h"

namespace dshape {

constexpr float TAU = 6.2831853f;

// Vertices of a regular n-gon with vertex 0 straight up, going clockwise.
inline void ring(int cx, int cy, int r, int n, float rot, int* xs, int* ys) {
  for (int i = 0; i < n; i++) {
    const float a = rot + (float)i * TAU / (float)n;
    xs[i] = cx + (int)lroundf((float)r * sinf(a));
    ys[i] = cy - (int)lroundf((float)r * cosf(a));
  }
}

inline void closed(ToolsCanvas& c, const int* xs, const int* ys, int n, int t, bool black) {
  for (int i = 0; i < n; i++)
    c.drawLine(xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n], t, black);
}

// --- the six solids ----------------------------------------------------------

// Tetrahedron: a triangle with the near edge running down the middle.
inline void d4(ToolsCanvas& c, int cx, int cy, int r, int t, bool black) {
  int xs[3], ys[3];
  ring(cx, cy, r, 3, 0.0f, xs, ys);
  closed(c, xs, ys, 3, t, black);
  c.drawLine(xs[0], ys[0], (xs[1] + xs[2]) / 2, (ys[1] + ys[2]) / 2, t, black);
}

// Cube seen corner-on: a hexagon with three edges meeting at the near corner.
inline void d6(ToolsCanvas& c, int cx, int cy, int r, int t, bool black) {
  int xs[6], ys[6];
  ring(cx, cy, r, 6, 0.0f, xs, ys);
  closed(c, xs, ys, 6, t, black);
  for (int i = 1; i < 6; i += 2) c.drawLine(cx, cy, xs[i], ys[i], t, black);
}

// Octahedron: the top face as a chord, its ends falling to the bottom vertex.
inline void d8(ToolsCanvas& c, int cx, int cy, int r, int t, bool black) {
  int xs[6], ys[6];
  ring(cx, cy, r, 6, 0.0f, xs, ys);
  closed(c, xs, ys, 6, t, black);
  c.drawLine(xs[5], ys[5], xs[1], ys[1], t, black);
  c.drawLine(xs[5], ys[5], xs[3], ys[3], t, black);
  c.drawLine(xs[1], ys[1], xs[3], ys[3], t, black);
}

// Pentagonal trapezohedron: a kite, apex over a waist, with the near edge
// dropping to the bottom point.
inline void d10(ToolsCanvas& c, int cx, int cy, int r, int t, bool black) {
  const int wx = (r * 19) / 20, wy = r / 10;      // the widest pair
  const int lx = r / 2, ly = (r * 11) / 20;       // the lower pair
  const int xs[6] = {cx, cx + wx, cx + lx, cx, cx - lx, cx - wx};
  const int ys[6] = {cy - r, cy + wy, cy + ly, cy + r, cy + ly, cy + wy};
  closed(c, xs, ys, 6, t, black);
  c.drawLine(xs[0], ys[0], xs[2], ys[2], t, black);
  c.drawLine(xs[0], ys[0], xs[4], ys[4], t, black);
  c.drawLine(cx, cy + ly, cx, cy + r, t, black);
}

// Dodecahedron: a decagon rim around the pentagon facing you, spokes between.
inline void d12(ToolsCanvas& c, int cx, int cy, int r, int t, bool black) {
  int ox[10], oy[10], ix[5], iy[5];
  ring(cx, cy, r, 10, 0.0f, ox, oy);
  ring(cx, cy, (r * 9) / 20, 5, 0.0f, ix, iy);
  closed(c, ox, oy, 10, t, black);
  closed(c, ix, iy, 5, t, black);
  for (int i = 0; i < 5; i++) c.drawLine(ix[i], iy[i], ox[i * 2], oy[i * 2], t, black);
}

// Icosahedron: hexagon rim, the top face as a triangle, spokes to the corners.
inline void d20(ToolsCanvas& c, int cx, int cy, int r, int t, bool black) {
  int ox[6], oy[6], ix[3], iy[3];
  ring(cx, cy, r, 6, 0.0f, ox, oy);
  // The near face is turned 60 degrees against the rim, so each of its corners
  // sits between two rim corners and reaches out to both. Sharing the rotation
  // instead makes one spoke radial and the pattern comes out as a pinwheel.
  ring(cx, cy, (r * 1) / 2, 3, TAU / 6.0f, ix, iy);
  closed(c, ox, oy, 6, t, black);
  closed(c, ix, iy, 3, t, black);
  for (int i = 0; i < 3; i++) {
    c.drawLine(ix[i], iy[i], ox[(2 * i) % 6], oy[(2 * i) % 6], t, black);
    c.drawLine(ix[i], iy[i], ox[(2 * i + 2) % 6], oy[(2 * i + 2) % 6], t, black);
  }
}

// Indexed the same way the dice tool orders its sides: 4, 6, 8, 10, 12, 20.
inline void draw(ToolsCanvas& c, int idx, int cx, int cy, int r, int t, bool black) {
  switch (idx) {
    case 0: d4(c, cx, cy, r, t, black); break;
    case 1: d6(c, cx, cy, r, t, black); break;
    case 2: d8(c, cx, cy, r, t, black); break;
    case 3: d10(c, cx, cy, r, t, black); break;
    case 4: d12(c, cx, cy, r, t, black); break;
    default: d20(c, cx, cy, r, t, black); break;
  }
}

}  // namespace dshape
