#include "gfx.h"

#include <string.h>

#include "board_pins.h"
#include "tools/tiny_fs.h"

#ifndef TOYBOX_HOST
#include <esp_partition.h>
#include <spi_flash_mmap.h>
#endif

// The preview harness can swap in CrossPoint's four UI faces (23/24/29/51 px
// against the 12/16/24/32 these layouts were drawn for) to render every screen
// the way the reader port would. Same tables, same code path -- only the glyphs
// differ, which is exactly what makes the comparison worth anything.
#ifdef TOYBOX_CP_FONTS
#include "fonts_cp.h"
#else
#include "fonts_ui.h"
#include "fonts_read.h"
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
#include "tools/cpfont.h"
#include "tools/unicode.h"

namespace gfx {

namespace {
// Every size is a real face baked at exactly that pixel height, with a real
// bold cut (see tools/make_fonts.py). The 8x8 pixel font this replaced was
// scaled 2x, 3x and 4x, which turned every large letter into a grid of squares
// -- and its bold was the same glyph smeared one pixel sideways.
// Which family answers: 0 is DejaVu (the UI's face, and the default), 1 and 2
// are the two reading faces the EPUB reader can choose. The reader sets this
// around its own text and puts it back; nothing else touches it, so the UI
// never changes clothes by accident. The CrossPoint pass has one stand-in
// family and ignores the knob, faithfully to that firmware.
int g_typeface = 0;

// Italic, set the same way and for the same reason as the typeface: the reader
// turns it on around one run of a line and off again. A signature-wide flag
// would have to be threaded through drawChar, textInk and the font-pack path
// as well, and every caller that never sets it would carry it.
//
// There is no bold-italic face. Bold wins where a book nests the two: it is
// the stronger of the two signals, and a run that is both is nearly always a
// heading or a name rather than a sentence.
bool g_italic = false;

const UiFont* faceFor(int px, bool bold) {
#ifdef UI_HAS_EXTRAS
  if (g_typeface == 1) {
    if (g_italic && !bold) {
      switch (px) {
        case 24: return &FONT_LIT_24_ITAL;
        case 32: return &FONT_LIT_32_ITAL;
        case 44: return &FONT_LIT_44_ITAL;
        default: return &FONT_LIT_18_ITAL;
      }
    }
    switch (px) {
      case 24: return bold ? &FONT_LIT_24_BOLD : &FONT_LIT_24_REG;
      case 32: return bold ? &FONT_LIT_32_BOLD : &FONT_LIT_32_REG;
      case 44: return bold ? &FONT_LIT_44_BOLD : &FONT_LIT_44_REG;
      default: return bold ? &FONT_LIT_18_BOLD : &FONT_LIT_18_REG;
    }
  }
#endif
#ifdef UI_HAS_ITALIC
  if (g_italic && !bold) {
    switch (px) {
      case 24: return &FONT_24_ITAL;
      case 32: return &FONT_32_ITAL;
      case 44: return &FONT_44_ITAL;
      default: return &FONT_18_ITAL;
    }
  }
#endif
  switch (px) {
    case 24: return bold ? &FONT_24_BOLD : &FONT_24_REG;
    case 32: return bold ? &FONT_32_BOLD : &FONT_32_REG;
    case 44: return bold ? &FONT_44_BOLD : &FONT_44_REG;
    default: return bold ? &FONT_18_BOLD : &FONT_18_REG;
  }
}

#ifndef UI_HAS_EXTRAS
// A stand-in face (the CrossPoint pass) carries only ASCII; accents fall
// through to the international tables, as they do on that firmware.
constexpr int UI_EXTRA_COUNT = 0;
const uint16_t UI_EXTRA_CPS[1] PROGMEM = {0};
#endif

// The baked faces carry ASCII at indices 0..94 and the extras after them, in
// UI_EXTRA_CPS order -- which the generator sorts, so this is a binary search.
// -1 means the face does not have it and the caller falls through to the
// international tables.
//
// It used to scan, behind a hand-written "is it roughly in the Latin range"
// guess. The first codepoints added outside that guess -- the vulgar fractions
// a recipe measures flour in -- were refused before the scan ever ran, and drew
// a box with the table sitting there holding them. A search that reads the
// table cannot disagree with the table.
int glyphIndex(uint32_t cp) {
  if (cp >= 32 && cp <= 126) return (int)cp - 32;
  int lo = 0, hi = UI_EXTRA_COUNT - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const uint32_t at = pgm_read_word(&UI_EXTRA_CPS[mid]);
    if (at == cp) return 95 + mid;
    if (at < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

FontGlyph glyphOf(const UiFont* f, uint32_t cp) {
  int idx = glyphIndex(cp);
  if (idx < 0) idx = (int)'?' - 32;
  const FontGlyph* src = &f->glyphs[idx];
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

// Three intl faces cover four sizes. There is no 44 px cut: at 8,290 code
// points a face costs about its height squared, and 44 px would add roughly
// 1.9 MB to a 4 MB app partition to make headlines bigger in Chinese. TS_HUGE
// borrows the 32 px face and centres it in the taller box, which is the same
// thing every typesetter does when the size they want is not in the drawer.
const IntlFace* intlFor(int px, int& yAdjust) {
  yAdjust = 0;
  // Up to 20, not up to 16: the smallest Latin bucket moved to 18 px and the
  // non-Latin faces did not follow it. They are baked in threes (16/24/32),
  // and so are the font packs already installed on people's devices -- a new
  // size here would be a size those packs do not carry, and every Chinese
  // character in small text would fall back to nothing. A 16 px glyph inside
  // an 18 px line reads a shade small; a missing one reads as a hole.
  if (px <= 20) return &INTL_16;
  if (px <= 24) return &INTL_24;
  if (px <= 32) return &INTL_32;
  yAdjust = (px - 32) / 2;
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

// --- a face off the card ------------------------------------------------------
// CrossInk's .cpfont families, chosen by the owner and read straight from the
// SD card (see cardfonts.h). One file per line box: their sizes are points at
// 150 DPI, ours are pixel boxes, so the loader picks whichever of a family's
// files lands closest to each of 18 / 24 / 32 / 44 and hands the bytes here.
//
// A card face answers FIRST, for every codepoint it carries -- it is the face
// the owner asked for, and a face that only got the letters the baked tables
// happened to miss would be no face at all. What it does not carry falls
// through unchanged, which is the whole reason Thai still draws under a
// Latin-only font.
struct CardFace {
  cpfont::Font font;
  uint8_t* blob = nullptr;  // freed when the face is replaced, if owned
  bool owns = false;        // false when a neighbouring box shares these bytes
  int box = 0;              // the Toybox line box this file serves
  bool live = false;
};
constexpr int CARD_BOXES = 4;
constexpr int CARD_BOX_PX[CARD_BOXES] = {18, 24, 32, 44};

// Two sets, because the device has two kinds of text. The UNIVERSAL face is
// the firmware's own -- menus, labels, the hub, every screen that is Toybox
// talking. The CONTENT face belongs to whatever app is open and covers what
// the owner put there: the book, the note, the card, the recipe.
//
// Keeping them apart is what lets somebody read a novel in a serif without
// their settings page changing clothes, and it is why an app can be given a
// face of its own at all. Content wins while an app has it turned on; the
// moment it is off -- which is every screen the firmware draws for itself --
// the universal face answers.
CardFace g_ui[CARD_BOXES];
CardFace g_content[CARD_BOXES];
bool g_contentOn = false;

int cardSlot(int px) {
  for (int i = 0; i < CARD_BOXES; i++)
    if (CARD_BOX_PX[i] == px) return i;
  // Anything between the named sizes takes the largest box that still fits, so
  // an odd request never silently jumps a size.
  int best = 0;
  for (int i = 0; i < CARD_BOXES; i++)
    if (CARD_BOX_PX[i] <= px) best = i;
  return best;
}

// Where the baked faces put their baseline, measured from their own glyphs
// rather than declared. Mixed lines are the point: a card font with no Thai
// draws its Latin and the baked tables draw the Thai, and the two have to sit
// on one baseline or the line visibly staggers. Measured once per face.
struct BaselineCache {
  const UiFont* f = nullptr;
  int baseline = 0;
};
BaselineCache g_baselines[8];

int bakedBaseline(const UiFont* f) {
  for (auto& c : g_baselines)
    if (c.f == f) return c.baseline;
  // The bottom row of ink in a capital H IS the baseline, for every Latin face
  // ever cut: no overshoot, no descender.
  const FontGlyph g = glyphOf(f, 'H');
  const int stride = (g.width + 7) / 8;
  int bottom = -1;
  for (int row = 0; row < f->height; row++) {
    const uint8_t* line = &f->bits[g.offset + row * stride];
    for (int col = 0; col < g.width; col++)
      if (pgm_read_byte(&line[col >> 3]) & (0x80 >> (col & 7))) {
        bottom = row;
        break;
      }
  }
  const int baseline = bottom >= 0 ? bottom + 1 : (f->height * 3) / 4;
  for (auto& c : g_baselines)
    if (!c.f) {
      c.f = f;
      c.baseline = baseline;
      break;
    }
  return baseline;
}

// The cut a run wants, and what to do when the file has not got it. Bold and
// italic both fall back to regular rather than to nothing; bold then gets the
// same double-strike the intl tables get, and italic simply reads upright,
// which is what every e-reader does with a single-cut font.
int cardCut(const cpfont::Font& f, bool bold, bool italic) {
  const uint8_t want = bold ? (italic ? cpfont::BOLD_ITALIC : cpfont::BOLD)
                            : (italic ? cpfont::ITALIC : cpfont::REGULAR);
  int i = f.find(want);
  if (i >= 0) return i;
  if (bold && italic) {
    i = f.find(cpfont::BOLD);
    if (i < 0) i = f.find(cpfont::ITALIC);
    if (i >= 0) return i;
  }
  return f.find(cpfont::REGULAR);
}

struct CardHit {
  const CardFace* face = nullptr;
  const cpfont::Style* style = nullptr;
  cpfont::Glyph glyph;
};

bool cardFind(int px, bool bold, bool italic, uint32_t cp, CardHit& out) {
  const int slot = cardSlot(px);
  const CardFace& c = (g_contentOn && g_content[slot].live) ? g_content[slot] : g_ui[slot];
  if (!c.live) return false;
  const int idx = cardCut(c.font, bold, italic);
  if (idx < 0) return false;
  const cpfont::Style& st = c.font.style(idx);
  if (!c.font.glyph(st, cp, out.glyph)) return false;
  out.face = &c;
  out.style = &st;
  return true;
}

void blitCard(const CardHit& h, int x, int baselineY, uint8_t color) {
  const cpfont::Glyph& g = h.glyph;
  for (int row = 0; row < g.h; row++)
    for (int col = 0; col < g.w; col++)
      if (h.face->font.on(*h.style, g, col, row))
        epd.drawPixel(x + g.left + col, baselineY - g.top + row, color);
}

// Advance of one codepoint past ASCII, packs included.
int intlAdvance(int px, uint32_t cp) {
  int yAdj;
  const Hit h = intlFind(px, yAdj, cp);
  if (h.g) return pgm_read_byte(&h.g->adv);
  return (h.f->box * 3) / 5 + 4;  // the missing-glyph box
}
}  // namespace

// --- the card face, from outside ----------------------------------------------
// gfx does no file work: the card, its directories and its bus belong to the
// layer that owns them (cardfonts.h), which reads a file whole and hands the
// bytes over. Ownership passes here -- the bytes must outlive every glyph
// drawn from them, and a face replaced frees the one it replaced.
bool cardFaceSet(int px, uint8_t* blob, uint32_t len, bool content, bool owns) {
  const int slot = cardSlot(px);
  CardFace& c = (content ? g_content : g_ui)[slot];
  cpfont::Font parsed;
  if (blob && !parsed.open(blob, len)) return false;  // the old face stays put
  if (c.blob && c.owns) free(c.blob);
  c.blob = blob;
  c.owns = owns;
  c.box = CARD_BOX_PX[slot];
  if (!blob) {
    c.live = false;
    c.font = cpfont::Font();
    return true;
  }
  c.font = parsed;
  c.live = true;
  return true;
}

void cardFaceClear(bool content) {
  for (int i = 0; i < CARD_BOXES; i++) cardFaceSet(CARD_BOX_PX[i], nullptr, 0, content, false);
}

bool cardFaceLive(bool content) {
  for (const CardFace& c : (content ? g_content : g_ui))
    if (c.live) return true;
  return false;
}

int cardFaceLine(int px, bool content) {
  const CardFace& c = (content ? g_content : g_ui)[cardSlot(px)];
  return c.live ? c.font.style(0).advanceY : 0;
}

void contentFace(bool on) { g_contentOn = on; }
bool contentFaceOn() { return g_contentOn; }

void setTypeface(int n) { g_typeface = (n < 0 || n > 1) ? 0 : n; }
int typeface() { return g_typeface; }
void setItalic(bool on) { g_italic = on; }
bool italic() { return g_italic; }

void drawChar(int x, int y, char c, int px, uint8_t color) {
  const UiFont* f = faceFor(px, false);
  blit(f, glyphOf(f, c), x, y, color);
}

int drawText(int x, int y, const char* s, int scale, uint8_t color, bool bold, int spacing) {
#ifdef TOYBOX_HOST
  {
    const int w = textWidth(s, scale, bold, spacing);
    // Against the live panel width, not the portrait constant: the pinned note
    // rotates, and text that fits an 800 px landscape is not an overflow.
    if (s[0] && (x < 0 || x + w > epd.logicalW())) noteOverflow(x, w, scale, s);
  }
#endif
  const UiFont* f = faceFor(scale, bold);
  const int baseline = y + bakedBaseline(f);
  int cx = x;
  uint32_t prev = 0;
  for (const char* p = s; *p;) {
    const uint32_t cp = uni::next(p);
    CardHit card;
    if (cardFind(scale, bold, g_italic, cp, card)) {
      blitCard(card, cx, baseline, color);
      // Bold with no bold cut: the same double-strike the intl tables use.
      if (bold && card.style->cut != cpfont::BOLD && card.style->cut != cpfont::BOLD_ITALIC)
        blitCard(card, cx + 1, baseline, color);
      const int adv = card.glyph.advance();
      cx += adv + (adv > 0 ? spacing : 0);
      prev = cp;
      continue;
    }
    if (glyphIndex(cp) >= 0) {
      const FontGlyph g = glyphOf(f, cp);
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
    // Measured through the same door it will be drawn through. A width that
    // consults the baked tables while the drawing consults the card font is
    // how centred text drifts and how a page turn loses its last word.
    CardHit card;
    if (cardFind(scale, bold, g_italic, cp, card)) {
      const int adv = card.glyph.advance();
      w += adv + (adv > 0 ? spacing : 0);
      continue;
    }
    if (glyphIndex(cp) >= 0) {
      w += glyphOf(f, cp).width + spacing;
      continue;
    }
    const int adv = intlAdvance(scale, cp);
    w += adv + (adv > 0 ? spacing : 0);
  }
  if (w > 0) w -= spacing;
  return w < 0 ? 0 : w;
}

int textHeight(int px) {
  // A card face has its own line height, and while an app is drawing the
  // owner's words in one, that is the height its lines have to step by --
  // otherwise a 30 px face is laid out on 24 px lines and every line lands on
  // the one below it.
  //
  // Only while the content face is on. The firmware's own screens keep the
  // baked metrics they were laid out against, so choosing a face for a book
  // cannot move the buttons on a settings page.
  if (g_contentOn) {
    const int slot = cardSlot(px);
    if (g_content[slot].live) return g_content[slot].font.style(0).advanceY;
  }
  return faceFor(px, false)->height;
}

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
  // A card face's glyphs are positioned from a baseline rather than packed
  // into a box, so the box-relative measurement below does not describe them.
  // Reporting no slack is the honest answer: centring falls back to the box,
  // which is where it started before this refinement existed.
  {
    const int slot = cardSlot(scale);
    if ((g_contentOn && g_content[slot].live) || g_ui[slot].live) return;
  }
  const UiFont* f = faceFor(scale, bold);
  const int h = f->height;

  int inkL = 0, inkR = 0, inkT = h, inkB = -1;
  bool any = false;
  int x = 0;
  for (const char* p = s; *p;) {
    const uint32_t cp = uni::next(p);
    if (glyphIndex(cp) < 0) continue;  // intl glyphs fill their boxes anyway
    const FontGlyph g = glyphOf(f, cp);
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

namespace {
int g_loadedFaces = 0;
}
int loadedFaceCount() { return g_loadedFaces; }

int loadFontPacks() {
#ifndef TOYBOX_HOST
  // Raw pack partitions, written directly by the web installer. Memory-mapped,
  // so a pack costs no RAM at all -- the glyph tables read from flash exactly
  // like the baked ones. An empty partition is all 0xFF and fails the magic
  // check, which is the whole "is it installed" protocol.
  static bool partitionsDone = false;
  if (!partitionsDone) {
    partitionsDone = true;
    const char* parts[3] = {"zh_font", "ko_font", "ja_font"};
    for (int i = 0; i < 3; i++) {
      const esp_partition_t* p =
          esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, parts[i]);
      if (!p) continue;
      const void* mapped = nullptr;
      esp_partition_mmap_handle_t h;
      if (esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &mapped, &h) != ESP_OK)
        continue;
      // The handle is deliberately kept for the life of the firmware.
      if (!registerPack((char*)mapped, p->size)) esp_partition_munmap(h);
    }
  }
#endif
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
  g_loadedFaces = g_packFaceCount;
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
