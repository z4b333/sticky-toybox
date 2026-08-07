// A small Markdown subset, chosen for what actually reads well on e-paper.
//
// Supported:  # ## ###   headings
//             - or *     bullets
//             1.         numbered list
//             - [ ]      checkbox (tappable on the device)
//             >          quote
//             ---        divider
//             **bold**   inline, anywhere
//
// Left out on purpose: italics (the 8x8 font has no italic cut and a sheared
// bitmap looks broken), tables, links and images.
//
// Blocks index into the original text rather than mutating it, so a tapped
// checkbox can be flipped by writing one byte back at `checkOff` and saving the
// buffer unchanged otherwise.
#pragma once
#include "tools_ui.h"

namespace nmd {

enum Type : uint8_t { Blank, Text, H1, H2, H3, Bullet, Numbered, Check, Quote, Rule };

struct Block {
  Type type;
  uint8_t num;        // list number for Numbered
  bool checked;
  uint16_t start;     // offset of the block's *content* in the source
  uint16_t len;
  uint16_t checkOff;  // offset of the char inside [ ] — only valid for Check
};

constexpr int MAX_BLOCKS = 120;
constexpr int LINE_BUF = 200;

inline bool isRule(const char* p, int len) {
  if (len < 3) return false;
  for (int i = 0; i < len; i++)
    if (p[i] != '-' && p[i] != ' ') return false;
  int dashes = 0;
  for (int i = 0; i < len; i++)
    if (p[i] == '-') dashes++;
  return dashes >= 3;
}

// Parses the whole note. `text` is not modified.
inline int parse(const char* text, Block* out, int maxBlocks) {
  int n = 0;
  int listNum = 0;
  const char* base = text;
  const char* p = text;

  while (*p && n < maxBlocks) {
    const char* lineStart = p;
    int len = 0;
    while (p[len] && p[len] != '\n') len++;
    // strip trailing CR and spaces
    int end = len;
    while (end > 0 && (lineStart[end - 1] == '\r' || lineStart[end - 1] == ' ')) end--;

    int i = 0;
    while (i < end && lineStart[i] == ' ') i++;  // leading indent is ignored

    Block b{};
    b.type = Text;
    b.start = (uint16_t)(lineStart - base + i);
    b.len = (uint16_t)(end - i);

    if (end - i == 0) {
      b.type = Blank;
      listNum = 0;
    } else if (isRule(lineStart + i, end - i)) {
      b.type = Rule;
      listNum = 0;
    } else if (lineStart[i] == '#') {
      int h = 0;
      while (i + h < end && lineStart[i + h] == '#') h++;
      if (h <= 3 && i + h < end && lineStart[i + h] == ' ') {
        b.type = (h == 1) ? H1 : (h == 2) ? H2 : H3;
        b.start = (uint16_t)(lineStart - base + i + h + 1);
        b.len = (uint16_t)(end - i - h - 1);
        listNum = 0;
      }
    } else if ((lineStart[i] == '-' || lineStart[i] == '*') && i + 1 < end &&
               lineStart[i + 1] == ' ') {
      // "- [ ] task" is a checkbox; anything else is a plain bullet.
      const int after = i + 2;
      if (after + 2 < end && lineStart[after] == '[' && lineStart[after + 2] == ']') {
        b.type = Check;
        b.checked = (lineStart[after + 1] == 'x' || lineStart[after + 1] == 'X');
        b.checkOff = (uint16_t)(lineStart - base + after + 1);
        int c = after + 3;
        while (c < end && lineStart[c] == ' ') c++;
        b.start = (uint16_t)(lineStart - base + c);
        b.len = (uint16_t)(end - c);
      } else {
        b.type = Bullet;
        b.start = (uint16_t)(lineStart - base + after);
        b.len = (uint16_t)(end - after);
      }
      listNum = 0;
    } else if (lineStart[i] >= '0' && lineStart[i] <= '9') {
      int d = i;
      while (d < end && lineStart[d] >= '0' && lineStart[d] <= '9') d++;
      if (d < end && lineStart[d] == '.' && d + 1 < end && lineStart[d + 1] == ' ') {
        b.type = Numbered;
        b.num = (uint8_t)(++listNum);
        b.start = (uint16_t)(lineStart - base + d + 2);
        b.len = (uint16_t)(end - d - 2);
      }
    } else if (lineStart[i] == '>') {
      b.type = Quote;
      int c = i + 1;
      while (c < end && lineStart[c] == ' ') c++;
      b.start = (uint16_t)(lineStart - base + c);
      b.len = (uint16_t)(end - c);
      listNum = 0;
    }

    out[n++] = b;
    p = lineStart + len;
    if (*p == '\n') p++;
  }
  return n;
}

// --- inline bold + word wrap -------------------------------------------------

// One wrappable unit: an English word, a single han character, a Thai cluster.
// `glue` means it follows its predecessor with no space -- a split inside a
// spaceless run, where the join must not be drawn as a gap.
struct Seg {
  char text[64];
  bool bold;
  bool strike;
  bool glue;
};

// Streams the next segment out of a run of text, tracking **bold** and
// ~~struck~~ spans (markers are removed). Used to be a fixed array of words,
// which put a cap on how much of a block could wrap; a Chinese paragraph is one
// segment per character, and blew straight past it.
struct SegIter {
  const char* src;
  int len;
  int i = 0;
  bool bold = false, strike = false;
  bool pendingGlue = false;

  SegIter(const char* s, int l) : src(s), len(l) {}

  bool next(Seg& out) {
    // A space between segments both separates them and resets the glue.
    while (i < len && src[i] == ' ') {
      i++;
      pendingGlue = false;
    }
    if (i >= len) return false;
    out.bold = bold;
    out.strike = strike;
    out.glue = pendingGlue;
    pendingGlue = false;
    int w = 0;
    uint32_t prev = 0;
    while (i < len && src[i] != ' ') {
      if (src[i] == '*' && i + 1 < len && src[i + 1] == '*') {
        bold = !bold;
        i += 2;
        if (w == 0) out.bold = bold;  // marker at the start: whole word takes it
        continue;
      }
      if (src[i] == '~' && i + 1 < len && src[i + 1] == '~') {
        strike = !strike;
        i += 2;
        if (w == 0) out.strike = strike;
        continue;
      }
      const char* p = src + i;
      const uint32_t cp = uni::next(p);
      const int n = (int)(p - (src + i));
      // Split before a legal break inside a spaceless run, and hard-split when
      // the buffer is full (at a codepoint boundary, never inside one).
      if (w > 0 && (uni::breakBefore(prev, cp) || w + n > (int)sizeof(out.text) - 1)) {
        pendingGlue = true;
        break;
      }
      for (int k = 0; k < n && i < len; k++) out.text[w++] = src[i++];
      prev = cp;
    }
    out.text[w] = 0;
    return w > 0 ? true : next(out);
  }
};

// Draws wrapped rich text. Returns the y after the last line drawn; if it would
// pass `maxY` nothing further is drawn and `overflowed` is set.
inline int drawRich(ToolsCanvas& c, int x, int y, int maxW, const char* src, int len,
                    TSize sz, bool forceBold, int maxY, bool* overflowed,
                    bool forceStrike = false) {
  SegIter it(src, len);
  Seg seg;
  const int lh = c.textHeight(sz) + 6;
  const int spaceW = c.textWidth(" ", sz);
  int cx = x;
  bool lineStarted = false;

  while (it.next(seg)) {
    const bool bold = forceBold || seg.bold;
    const int ww = c.textWidth(seg.text, sz, bold);
    const int lead = (lineStarted && !seg.glue) ? spaceW : 0;
    if (lineStarted && cx + lead + ww > x + maxW) {
      y += lh;
      cx = x;
      lineStarted = false;
    }
    if (y + c.textHeight(sz) > maxY) {
      *overflowed = true;
      return y;
    }
    if (lineStarted && !seg.glue) cx += spaceW;
    c.text(cx, y, seg.text, sz, true, bold);
    // The line goes through the middle of the glyphs, not under them: struck
    // out has to be unmistakable from arm's length on the fridge.
    if (forceStrike || seg.strike)
      c.fillRect(cx - 1, y + c.textHeight(sz) / 2 - 1, ww + 2, 2, true);
    cx += ww;
    lineStarted = true;
  }
  return y + lh;
}

inline bool tappable(Type t) {
  return t == Check || t == Bullet || t == Numbered || t == Text;
}

// Applies a tap to a line by editing the note's own Markdown. A checkbox flips
// its [ ]; any other line is wrapped in ~~...~~, or unwrapped if it already is.
//
// Nothing is stored beside the note. The crossing IS the text, so it survives a
// round trip through the phone, it is visible when the file is read anywhere
// else, and there is no second copy of "what is done" to fall out of step --
// which is the promise the top of note_store.h makes.
//
// Returns the note's new length, or -1 if the tap did nothing.
inline int applyTap(char* buf, int len, int cap, const Block& b) {
  if (b.type == Check) {
    if (b.checkOff >= (uint16_t)len) return -1;
    buf[b.checkOff] = b.checked ? ' ' : 'x';
    return len;
  }
  if (!tappable(b.type) || b.len == 0) return -1;

  const int s = b.start, e = b.start + b.len;
  if (e > len) return -1;
  const bool struck = b.len >= 4 && buf[s] == '~' && buf[s + 1] == '~' &&
                      buf[e - 2] == '~' && buf[e - 1] == '~';
  if (struck) {
    memmove(buf + e - 2, buf + e, len - e);
    len -= 2;
    memmove(buf + s, buf + s + 2, len - s - 2);
    len -= 2;
    return len;
  }
  if (len + 4 > cap) return -1;  // no room to mark it: leave the note alone
  memmove(buf + e + 2, buf + e, len - e);
  buf[e] = '~';
  buf[e + 1] = '~';
  len += 2;
  memmove(buf + s + 2, buf + s, len - s);
  buf[s] = '~';
  buf[s + 1] = '~';
  len += 2;
  return len;
}

// --- block rendering ---------------------------------------------------------

// A tappable line. Checkboxes flip their box; every other kind of line gets
// struck through instead. One struct for both, because to a finger they are the
// same gesture and the renderer is the only thing that knows the difference.
struct CheckHit {
  TRect box;
  int block;
};

// Records the band a line occupies. The whole row is the target, including the
// blank to the right of a short line -- aiming at the words themselves would be
// a 4 mm target on a 235 DPI panel.
inline void noteHit(CheckHit* hits, int* hitCount, int maxHits, const TRect& area, int top,
                    int bottom, int block) {
  if (!hits || !hitCount || *hitCount >= maxHits) return;
  // Rows nearly touch, so the bands must not overlap or the wrong line takes
  // the tap. Two pixels of gap, and the full width to aim at.
  hits[*hitCount].box = TRect{area.x - 6, top - 2, area.w + 12, bottom - top - 2};
  hits[*hitCount].block = block;
  (*hitCount)++;
}

inline TSize sizeOf(Type t) {
  switch (t) {
    case H1: return TS_HUGE;
    case H2: return TS_LARGE;
    case H3: return TS_MED;
    default: return TS_MED;
  }
}

// Thai at the body size is Loma at 9 pt -- the two mark storeys eat the box and
// what is left is squint material (measured in tools/thai_proof.py). Body lines
// that carry Thai or CJK step up one size instead; pure-Latin lines keep the
// tighter line the layout was drawn for.
inline TSize bodySize(const char* src, const Block& b, TSize base) {
  if (base != TS_MED) return base;
  const char* p = src + 0;
  const char* end = p + b.len;
  while (p < end) {
    const uint32_t cp = uni::next(p);
    if (uni::thai(cp) || uni::cjk(cp)) return TS_LARGE;
  }
  return base;
}

// Renders blocks from `from` until the area is full. Returns the index of the
// first block that did not fit (== count when everything was drawn), so the
// caller can use it as the next page's starting point.
inline int render(ToolsCanvas& c, const char* src, const Block* blocks, int count, int from,
                  const TRect& area, CheckHit* hits, int maxHits, int* hitCount) {
  int y = area.y;
  const int maxY = area.y + area.h;
  if (hitCount) *hitCount = 0;
  char buf[LINE_BUF];

  for (int i = from; i < count; i++) {
    const Block& b = blocks[i];
    bool over = false;

    switch (b.type) {
      case Blank:
        y += 12;
        break;

      case Rule:
        if (y + 12 > maxY) return i;
        c.drawLine(area.x, y + 5, area.x + area.w, y + 5, 2, true);
        y += 16;
        break;

      case H1:
      case H2:
      case H3: {
        if (i > from) y += 8;  // breathing room above a heading
        const TSize sz = sizeOf(b.type);
        if (y + c.textHeight(sz) > maxY) return i;
        y = drawRich(c, area.x, y, area.w, src + b.start, b.len, sz, true, maxY, &over);
        if (over) return i;
        if (b.type == H1) {
          if (y + 4 <= maxY) c.drawLine(area.x, y - 2, area.x + area.w, y - 2, 2, true);
          y += 8;
        }
        break;
      }

      case Bullet: {
        if (y + c.textHeight(TS_MED) > maxY) return i;
        const int rowTop = y;
        c.fillCircle(area.x + 8, y + c.textHeight(TS_MED) / 2, 4, true);
        y = drawRich(c, area.x + 26, y, area.w - 26, src + b.start, b.len,
                     bodySize(src + b.start, b, TS_MED), false,
                     maxY, &over);
        if (over) return i;
        noteHit(hits, hitCount, maxHits, area, rowTop, y, i);
        break;
      }

      case Numbered: {
        if (y + c.textHeight(TS_MED) > maxY) return i;
        snprintf(buf, sizeof(buf), "%d.", b.num);
        const int numTop = y;
        c.text(area.x, y, buf, TS_MED, true, true);
        y = drawRich(c, area.x + 44, y, area.w - 44, src + b.start, b.len,
                     bodySize(src + b.start, b, TS_MED), false,
                     maxY, &over);
        if (over) return i;
        noteHit(hits, hitCount, maxHits, area, numTop, y, i);
        break;
      }

      case Check: {
        const int boxSize = 22;
        if (y + boxSize > maxY) return i;
        const int by = y + (c.textHeight(TS_MED) - boxSize) / 2;
        c.drawRect(area.x, by, boxSize, boxSize, 2, true);
        if (b.checked) {
          // a tick, drawn as two strokes so it reads at this size
          c.drawLine(area.x + 5, by + 11, area.x + 9, by + 16, 3, true);
          c.drawLine(area.x + 9, by + 16, area.x + 17, by + 5, 3, true);
        }
        if (hits && hitCount && *hitCount < maxHits) {
          // Generous hit box: the row, not just the square.
          hits[*hitCount].box = TRect{area.x - 6, by - 6, area.w, boxSize + 12};
          hits[*hitCount].block = i;
          (*hitCount)++;
        }
        const int textY = y;
        y = drawRich(c, area.x + 36, textY, area.w - 36, src + b.start, b.len,
                     bodySize(src + b.start, b, TS_MED), false,
                     maxY, &over, b.checked);
        if (over) return i;
        y += 5;  // keep the boxes from stacking into a solid column
        break;
      }

      case Quote: {
        if (y + c.textHeight(TS_MED) > maxY) return i;
        const int qy = y;
        y = drawRich(c, area.x + 22, y, area.w - 22, src + b.start, b.len,
                     bodySize(src + b.start, b, TS_MED), false, maxY,
                     &over);
        c.fillRect(area.x, qy, 4, y - qy - 4, true);
        if (over) return i;
        break;
      }

      default: {
        if (y + c.textHeight(TS_MED) > maxY) return i;
        const int rowTop = y;
        y = drawRich(c, area.x, y, area.w, src + b.start, b.len,
                     bodySize(src + b.start, b, TS_MED), false, maxY, &over);
        if (over) return i;
        if (b.type == Text) noteHit(hits, hitCount, maxHits, area, rowTop, y, i);
        break;
      }
    }
  }
  return count;
}

}  // namespace nmd
