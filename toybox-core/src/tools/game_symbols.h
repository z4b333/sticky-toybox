// Twelve plain symbols for the C4 games that need tokens with no colour:
// Codebreaker uses the first six, Memory all twelve.
//
// Each is drawn in a `size` box round (cx, cy). Shapes that read apart at a
// glance even at Codebreaker's 34 px: solid against outline, round against
// angular, never two that differ only by a detail.
#pragma once
#include "decor.h"
#include "tool_icons.h"
#include "tools_draw.h"

namespace gsym {

constexpr int COUNT = 12;
constexpr int CODE_COUNT = 6;  // disc, ring, square, triangle, diamond, plus

inline void draw(ToolsCanvas& c, int idx, int cx, int cy, int size, bool ink = true) {
  const int h = size / 2;
  const int t = size / 7 < 3 ? 3 : size / 7;  // the stroke of the outline shapes
  switch (idx) {
    case 0:  // disc
      c.fillCircle(cx, cy, h, ink);
      break;
    case 1:  // ring
      c.drawCircle(cx, cy, h - 1, t, ink);
      break;
    case 2:  // square
      c.fillRect(cx - h + 2, cy - h + 2, size - 4, size - 4, ink);
      break;
    case 3:  // triangle, point up
      decor::triangle(c, cx, cy - h, cx - h, cy + h - 2, cx + h, cy + h - 2, ink);
      break;
    case 4:  // diamond, outlined
      tdraw::diamond(c, cx, cy, size + 2, ink);
      tdraw::diamond(c, cx, cy, size + 2 - 3 * t, !ink);
      break;
    case 5: {  // plus
      const int w = size / 3;
      c.fillRect(cx - w / 2, cy - h, w, size, ink);
      c.fillRect(cx - h, cy - w / 2, size, w, ink);
      break;
    }
    case 6:  // star
      decor::star(c, cx, cy + size / 20, h + 2, h / 2 - 1, 5, -1.5708f, ink);
      break;
    case 7:  // heart
      tdraw::heart(c, cx, cy, size, ink);
      break;
    case 8: {  // cross
      icons::Pen p(c, cx, cy, size, 26, ink);
      p.t = t + 1;
      p.line(4, 4, 22, 22);
      p.line(22, 4, 4, 22);
      break;
    }
    case 9:  // moon: a disc with a bite taken out of it
      c.fillCircle(cx, cy, h, ink);
      c.fillCircle(cx + h / 2 + 2, cy - h / 3, h - 2, !ink);
      break;
    case 10: {  // bolt
      const int x0 = cx - h, y0 = cy - h, s = size;
      decor::triangle(c, x0 + s * 11 / 20, y0, x0 + s * 3 / 20, y0 + s * 12 / 20, x0 + s * 11 / 20,
                      y0 + s * 12 / 20, ink);
      decor::triangle(c, x0 + s * 9 / 20, y0 + s * 8 / 20, x0 + s * 17 / 20, y0 + s * 8 / 20,
                      x0 + s * 9 / 20, y0 + s, ink);
      c.fillRect(x0 + s * 9 / 20, y0 + s * 8 / 20, s * 2 / 20 + 1, s * 4 / 20 + 1, ink);
      break;
    }
    default:  // 11: square outline with a dot -- a frame
      c.drawRect(cx - h + 1, cy - h + 1, size - 2, size - 2, t, ink);
      c.fillCircle(cx, cy, size / 8 + 1, ink);
      break;
  }
}

}  // namespace gsym
