#include "gfx.h"

#include <string.h>

#include "board_pins.h"
#include "tools/tiny_fs.h"

// The preview harness can swap in CrossPoint's four UI faces (23/24/29/51 px
// against the 12/16/24/32 these layouts were drawn for) to render every screen
// the way the reader port would. Same tables, same code path -- only the glyphs
// differ, which is exactly what makes the comparison worth anything.
#ifdef TOYBOX_CP_FONTS
#include "fonts_cp.h"
#else
#include "fonts_ui.h"
#endif

#ifdef TOYBOX_HOST
#include <cstdio>
#include <cstring>
// Host-only: every string that runs off the panel is recorded rather than
// silently clipped at the edge. On the device the clip is invisible, which is
// exactly why a caption that overflowed shipped unnoticed.
namespace gfx {
Overflow g_overflow[64];
int g_overflowCount = 0;
const char* g_overflowScreen = "";
char g_overflowScreens[64][40];
void noteOverflow(int x, int w, int scale, const char* s) {
  for (int i = 0; i < g_overflowCount; i++)
    if (strncmp(g_overflow[i].text, s, 63) == 0 && g_overflow[i].x == x) return;
  if (g_overflowCount >= 64) return;
  snprintf(g_overflow[g_overflowCount].text, 64, "%s", s);
  snprintf(g_overflowScreens[g_overflowCount], 40, "%s", g_overflowScreen);
  g_overflow[g_overflowCount].x = x;
  g_overflow[g_overflowCount].width = w;
  g_overflow[g_overflowCount].scale = scale;
  g_overflowCount++;
}
}  // namespace gfx
#endif

#include "fonts_intl.h"
#include "tools/unicode.h"

namespace gfx {

namespace {
// Every size is a real face baked at exactly that pixel height, with a real
// bold cut (see tools/make_fonts.py). The 8x8 pixel font this replaced was
// scaled 2x, 3x and 4x, which turned every large letter into a grid of squares
// -- and its bold was the same glyph smeared one pixel sideways.
const UiFont* faceFor(int px, bool bold) {
  switch (px) {
    case 12: return bold ? &FONT_12_BOLD : &FONT_12_REG;
    case 24: return bold ? &FONT_24_BOLD : &FONT_24_REG;
    case 32: return bold ? &FONT_32_BOLD : &FONT_32_REG;
    default: return bold ? &FONT_16_BOLD : &FONT_16_REG;
  }
}

int glyphIndex(char c) {
  if (c < 32 || c > 126) c = '?';
  return c - 32;
}

FontGlyph glyphOf(const UiFont* f, char c) {
  const FontGlyph* src = &f->glyphs[glyphIndex(c)];
  FontGlyph g;
  g.width = pgm_read_byte(&src->width);
  g.offset = pgm_read_word(&src->offset);
  return g;
}

void blit(const UiFont* f, const FontGlyph& g, int x, int y, uint8_t color) {
  const int stride = (g.width + 7) / 8;
  for (int row = 0; row < f->height; row++) {
    const uint8_t* line = &f->bits[g.offset + row * stride];
    for (int col = 0; col < g.width; col++)
      if (pgm_read_byte(&line[col >> 3]) & (0x80 >> (col & 7)))
        epd.drawPixel(x + col, y + row, color);
  }
}

// --- everything past ASCII ---------------------------------------------------
// The intl faces carry Thai, CJK, Korean, kana and accented Latin with signed
// bearings and real advances (see tools/make_fonts_intl.py). ASCII stays in the
// UiFont tables above; the split is invisible from outside this file.

// No 12 px intl face is baked -- CJK and Thai are not legible at that height.
// TS_SMALL text that carries such characters borrows the 16 px face, nudged up
// two so it centres on the smaller line rather than hanging below it.
const IntlFace* intlFor(int px, int& yAdjust) {
  yAdjust = 0;
  if (px <= 12) {
    yAdjust = -2;
    return &INTL_16;
  }
  if (px <= 16) return &INTL_16;
  if (px <= 24) return &INTL_24;
  return &INTL_32;
}

const IntlGlyph* intlGlyph(const IntlFace* f, uint32_t cp) {
  if (cp > 0xFFFF) return nullptr;
  uint32_t lo = 0, hi = f->count;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    const uint16_t v = pgm_read_word(&f->cps[mid]);
    if (v == cp) return &f->glyphs[mid];
    if (v < cp)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}

// Downloadable pack faces (see tools/make_font_pack.py): the full character
// sets, loaded whole into PSRAM from LittleFS. The baked tables answer first;
// these catch what they miss.
constexpr int MAX_PACK_FACES = 12;
IntlFace g_packFaces[MAX_PACK_FACES];
int g_packFaceCount = 0;

// The face+glyph pair a codepoint resolves to. The face matters as much as the
// glyph: the bitmap bytes live in whichever table owned the hit.
struct Hit {
  const IntlFace* f;
  const IntlGlyph* g;
};

Hit intlFind(int px, int& yAdjust, uint32_t cp) {
  const IntlFace* baked = intlFor(px, yAdjust);
  if (const IntlGlyph* g = intlGlyph(baked, cp)) return {baked, g};
  for (int i = 0; i < g_packFaceCount; i++) {
    if (g_packFaces[i].box != baked->box) continue;
    if (const IntlGlyph* g = intlGlyph(&g_packFaces[i], cp)) return {&g_packFaces[i], g};
  }
  return {baked, nullptr};
}

void blitIntl(const IntlFace* f, const IntlGlyph* g, int x, int y, uint8_t color) {
  const uint8_t w = pgm_read_byte(&g->w), h = pgm_read_byte(&g->h);
  const uint32_t off = pgm_read_dword(&g->off);
  const int stride = (w + 7) / 8;
  for (int row = 0; row < h; row++) {
    const uint8_t* line = &f->bits[off + (uint32_t)row * stride];
    for (int col = 0; col < w; col++)
      if (pgm_read_byte(&line[col >> 3]) & (0x80 >> (col & 7)))
        epd.drawPixel(x + col, y + row, color);
  }
}

// Advance of one codepoint past ASCII, packs included.
int intlAdvance(int px, uint32_t cp) {
  int yAdj;
  const Hit h = intlFind(px, yAdj, cp);
  if (h.g) return pgm_read_byte(&h.g->adv);
  return (h.f->box * 3) / 5 + 4;  // the missing-glyph box
}
}  // namespace

void drawChar(int x, int y, char c, int px, uint8_t color) {
  const UiFont* f = faceFor(px, false);
  blit(f, glyphOf(f, c), x, y, color);
}

int drawText(int x, int y, const char* s, int scale, uint8_t color, bool bold, int spacing) {
#ifdef TOYBOX_HOST
  {
    const int w = textWidth(s, scale, bold, spacing);
    if (s[0] && (x < 0 || x + w > EPD_W)) noteOverflow(x, w, scale, s);
  }
#endif
  const UiFont* f = faceFor(scale, bold);
  int cx = x;
  uint32_t prev = 0;
  for (const char* p = s; *p;) {
    const uint32_t cp = uni::next(p);
    if (cp < 128) {
      const FontGlyph g = glyphOf(f, (char)cp);
      blit(f, g, cx, y, color);
      cx += g.width + spacing;
      prev = cp;
      continue;
    }
    int yAdj;
    const Hit hit = intlFind(scale, yAdj, cp);
    if (!hit.g) {
      // A hollow box the size of a typical glyph: honest about the gap without
      // derailing the line.
      const int bw = intlAdvance(scale, cp) - 4;
      epd.drawRect(cx + 1, y + 2, bw, scale - 4, color, 1);
      cx += bw + 4 + spacing;
      prev = cp;
      continue;
    }
    const IntlFace* F = hit.f;
    const IntlGlyph* g = hit.g;
    int gy = y + yAdj;
    // A tone mark is baked at second-storey height (where it sits over an upper
    // vowel); over a bare consonant it comes down by the measured drop.
    if (uni::thaiTone(cp) && !uni::thaiUpper(prev)) gy += F->toneDrop;
    const int gx = cx + (int8_t)pgm_read_byte(&g->left);
    const int gt = gy + (int8_t)pgm_read_byte(&g->top);
    blitIntl(F, g, gx, gt, color);
    // Bold has no second cut past ASCII (it would double the tables); a one
    // pixel double-strike is the classic fallback and reads fine at 235 DPI.
    if (bold) blitIntl(F, g, gx + 1, gt, color);
    const int adv = pgm_read_byte(&g->adv);
    cx += adv + (adv > 0 ? spacing : 0);  // marks take no advance and no tracking
    prev = cp;
  }
  return cx - x;
}

int textWidth(const char* s, int scale, bool bold, int spacing) {
  const UiFont* f = faceFor(scale, bold);
  int w = 0;
  for (const char* p = s; *p;) {
    const uint32_t cp = uni::next(p);
    if (cp < 128) {
      w += glyphOf(f, (char)cp).width + spacing;
      continue;
    }
    const int adv = intlAdvance(scale, cp);
    w += adv + (adv > 0 ? spacing : 0);
  }
  if (w > 0) w -= spacing;
  return w < 0 ? 0 : w;
}

int textHeight(int px) { return faceFor(px, false)->height; }

// Blank space a string leaves inside the box textWidth/textHeight report.
// Centring on that box alone parks the ink off centre -- most visibly on a
// single big letter, like the H on the coin.
void textInk(const char* s, int scale, bool bold, int spacing, int& left, int& right,
             int& top, int& bottom) {
  left = right = top = bottom = 0;
  if (!s || !s[0]) return;
  // Ink centring exists for big lone ASCII glyphs (the H on the coin). Past
  // ASCII the intl glyphs already fill their boxes, and walking their bitmaps
  // here would cost more than the pixel it would move anything.
  for (const char* p = s; *p; p++)
    if ((uint8_t)*p >= 0x80) return;
  const UiFont* f = faceFor(scale, bold);
  const int h = f->height;

  int inkL = 0, inkR = 0, inkT = h, inkB = -1;
  bool any = false;
  int x = 0;
  for (const char* p = s; *p; p++) {
    const FontGlyph g = glyphOf(f, *p);
    const int adv = g.width;
    const int stride = (adv + 7) / 8;
    int gl = adv, gr = -1, gt = h, gb = -1;
    for (int row = 0; row < h; row++) {
      const uint8_t* line = &f->bits[g.offset + row * stride];
      for (int col = 0; col < adv; col++) {
        if (!(pgm_read_byte(&line[col >> 3]) & (0x80 >> (col & 7)))) continue;
        if (col < gl) gl = col;
        if (col > gr) gr = col;
        if (row < gt) gt = row;
        if (row > gb) gb = row;
      }
    }
    if (gr >= 0) {
      if (!any || x + gl < inkL) inkL = x + gl;
      if (!any || x + gr + 1 > inkR) inkR = x + gr + 1;
      if (gt < inkT) inkT = gt;
      if (gb > inkB) inkB = gb;
      any = true;
    }
    x += adv + spacing;
  }
  if (!any) return;
  left = inkL;
  right = textWidth(s, scale, bold, spacing) - inkR;
  top = inkT;
  bottom = h - 1 - inkB;
}

void drawTextCentered(int cx, int y, const char* s, int scale, uint8_t color, bool bold,
                      int spacing) {
  int l, r, t, b;
  textInk(s, scale, bold, spacing, l, r, t, b);
  drawText(cx - textWidth(s, scale, bold, spacing) / 2 + (r - l) / 2, y, s, scale, color, bold,
           spacing);
}

// --- font packs --------------------------------------------------------------

namespace {
constexpr int MAX_PACKS = 4;
char* g_packBlobs[MAX_PACKS];
char g_packNames[MAX_PACKS][24];

uint32_t rd32(const char* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

// Registers the faces inside one loaded blob. The blob stays allocated for the
// life of the process; faces point straight into it.
bool registerPack(char* blob, size_t len) {
  if (len < 8 || memcmp(blob, "TFP1", 4) != 0) return false;
  const uint32_t faces = rd32(blob + 4);
  size_t at = 8;
  for (uint32_t i = 0; i < faces; i++) {
    if (at + 16 > len || g_packFaceCount >= MAX_PACK_FACES) return false;
    IntlFace f;
    f.box = (uint16_t)rd32(blob + at);
    f.toneDrop = (uint16_t)rd32(blob + at + 4);
    f.count = rd32(blob + at + 8);
    const uint32_t bitsLen = rd32(blob + at + 12);
    at += 16;
    f.cps = (const uint16_t*)(blob + at);
    at += (size_t)f.count * 2;
    at = (at + 3) & ~(size_t)3;
    f.glyphs = (const IntlGlyph*)(blob + at);
    at += (size_t)f.count * sizeof(IntlGlyph);
    f.bits = (const uint8_t*)(blob + at);
    at += bitsLen;
    at = (at + 3) & ~(size_t)3;
    if (at > len) return false;
    g_packFaces[g_packFaceCount++] = f;
  }
  return true;
}
}  // namespace

int loadFontPacks() {
  char names[MAX_PACKS][24];
  const int n = tfs::list("/fonts", ".tfp", &names[0][0], sizeof(names[0]), MAX_PACKS, 23);
  for (int i = 0; i < n; i++) {
    bool have = false;
    for (int j = 0; j < MAX_PACKS; j++)
      if (g_packBlobs[j] && strcmp(g_packNames[j], names[i]) == 0) have = true;
    if (have) continue;
    int slot = -1;
    for (int j = 0; j < MAX_PACKS; j++)
      if (!g_packBlobs[j]) { slot = j; break; }
    if (slot < 0) break;

    char path[48];
    snprintf(path, sizeof(path), "/fonts/%s.tfp", names[i]);
    size_t len = 0;
    char* blob = tfs::readAlloc(path, len);
    if (!blob) continue;
    const int before = g_packFaceCount;
    if (registerPack(blob, len)) {
      g_packBlobs[slot] = blob;
      snprintf(g_packNames[slot], sizeof(g_packNames[slot]), "%s", names[i]);
    } else {
      g_packFaceCount = before;  // half-registered faces would point into freed memory
      free(blob);
    }
  }
  return g_packFaceCount;
}

void drawButton(int x, int y, int w, int h, const char* label, int scale, bool filled) {
  int l, r, t, b;
  textInk(label, scale, filled, 0, l, r, t, b);
  const int ty = y + (h - textHeight(scale)) / 2 + (b - t) / 2;
  if (filled) {
    epd.fillRect(x, y, w, h, 0);
    drawTextCentered(x + w / 2, ty, label, scale, 1, true);
  } else {
    epd.drawRect(x, y, w, h, 0, 2);
    drawTextCentered(x + w / 2, ty, label, scale, 0, false);
  }
}

}  // namespace gfx
