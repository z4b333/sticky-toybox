# Languages

Notes and flashcards accept whatever a phone keyboard can produce. This
document describes what renders, at what quality, and how the pieces fit.
The UI itself (buttons, titles, help cards) stays English; the multilingual
surface is user content.

## What works out of the box

The firmware bakes 8,290 glyphs at three sizes (16 / 24 / 32 px boxes, about
2.1 MB of flash), covering:

| script | coverage | source face |
|---|---|---|
| Thai | full block (87 codepoints) | Loma (TLWG) |
| Chinese | GB2312 level 1 — the 3,755 most common simplified characters | Noto Sans CJK SC |
| Japanese | all kana + JIS X 0208 level 1 kanji | Noto Sans CJK (SC style for han) |
| Korean | KS X 1001 — the 2,350 syllables of practical Korean + jamo | Noto Sans CJK KR |
| Vietnamese | Latin Extended Additional, precomposed | DejaVu Sans |
| European | Latin-1 Supplement + Latin Extended-A | DejaVu Sans |

Coverage is derived by round-tripping the standard codecs in the generator,
not from hand-maintained lists. Anything outside the baked set renders as a
hollow box — honest about the gap, without derailing the line.

## Font packs: full coverage on demand

`tools/make_font_pack.py` builds `.tfp` packs holding a language's **full**
character set minus what is already baked:

| pack | adds | size |
|---|---|---|
| `zh_full.tfp` | the rest of the CJK Unified Ideographs block (15,967 glyphs/size) | 4.3 MB |
| `ko_full.tfp` | the remaining 8,822 Hangul syllables | 2.3 MB |
| `ja_full.tfp` | the rest of JIS X 0208 (2,732 glyphs/size) | 0.7 MB |

A pack placed in `/fonts/` on the LittleFS partition is loaded whole into
PSRAM at boot (`gfx::loadFontPacks()`) and answers lookups behind the baked
tables. All three packs fit on the filesystem at once. The phone-side install
flow (upload over the pairing portal) is planned but not yet built; until
then packs go on over USB or a filesystem image.

Pack format (little-endian, all sections 4-aligned): `'TFP1'`, face count,
then per face a 16-byte header (box, toneDrop, count, bitsLen), sorted
`uint16` codepoints, 12-byte glyph records matching `IntlGlyph`, and bitmap
bits.

## The text engine

ASCII stays in the original `UiFont` tables (`fonts_ui.h`). Everything past
ASCII goes through `fonts_intl` glyphs, which carry what ASCII never needed:

* a **signed left bearing** — Thai combining marks have advance 0 and a
  negative bearing, so they land back on top of the previous glyph with no
  special case in the draw loop;
* a **real advance** separate from the bitmap width;
* interval-free **sorted-codepoint lookup** (binary search per face).

`gfx::drawText`, `textWidth` and the wrap code all walk UTF-8 codepoints
(`uni::next` in `toybox-core/src/tools/unicode.h`); malformed bytes render as
`?` one byte at a time rather than desynchronising the walk.

**The Thai tone rule** is the one piece of shaping the engine has. A tone mark
is baked at second-storey height (where it sits over an upper vowel, as in
กี่); over a bare consonant (ก่) it must come down. The distance is *measured*
by the generator — it renders one cluster through a real shaping engine and
through the dumb per-glyph path, takes the difference per face and size, and
stores it as `toneDrop`. `tools/thai_proof.py` is the study that validated
this approach against a HarfBuzz reference before any C was written.

**Bold** past ASCII is a one-pixel double-strike; real bold cuts would double
the tables, and at 235 DPI the difference does not earn the flash.

## Line breaking

Thai and Chinese have no spaces between words. The wrap code (segment
iterator in `note_md.h`, and the flashcard `wrap()`) breaks:

* **CJK** — at any character boundary, except before closing punctuation
  (、。！？」 and friends — a light kinsoku rule);
* **Thai** — at cluster boundaries: never before a combining mark, sara am,
  or the repetition signs. This is the standard embedded fallback; true Thai
  segmentation needs a dictionary, and breaks can land mid-word. Readable,
  not perfect;
* **Korean, Latin** — on spaces, as before.

Segments split inside a spaceless run carry a `glue` flag so no phantom space
is drawn or measured at the join.

## Per-script minimum sizes

Readability floors live in `scriptFloor()` (`tools_ui.h`) and apply wherever
user text is drawn — list rows, the top bar, the flashcard size ladder, the
lock-screen note name, and note body lines:

* **Thai floors at `TS_LARGE`** (24 px box). Loma metric-fitted into a 16 px
  box is a 9 pt face — the two mark storeys eat the line — and is not
  readable. Thai never shrinks below this; layouts spend lines instead.
* **Han and hangul floor at `TS_MED`** (16 px box); they hold up there at
  235 DPI but turn to mush in a 12 px line. If real-panel testing shows dense
  syllables muddy at 16 px, moving hangul to `TS_LARGE` is a one-line change.
* Note body lines containing Thai or CJK are promoted one step to `TS_LARGE`
  outright (`bodySize()` in `note_md.h`) — reading is the point there.

Names survive in their own script: `sanitizeName` in both stores passes
UTF-8 through whole (the filesystem only objects to `/` and control bytes)
and truncates on codepoint boundaries.

## Regenerating

```
python3 tools/make_fonts.py       > src/fonts_ui.h     # ASCII faces
python3 tools/make_fonts_intl.py                       # writes src/fonts_intl.{h,cpp}
python3 tools/make_font_pack.py /tmp/packs             # builds the three .tfp packs
python3 tools/thai_proof.py                            # the rendering study, /tmp/thai/
```

The generators need PIL (with FreeType), fontTools, and the source fonts
(DejaVu, TLWG Loma, Noto Sans CJK) installed on the build machine.

## Known limits

* Thai line breaks are cluster-based, not dictionary-based.
* Codepoints above U+FFFF (emoji, rare ideographs) are not covered.
* The CrossPoint Reader port maps text onto that firmware's own fonts, which
  currently have no Thai — Toybox-in-CrossPoint shows boxes for Thai until
  that host gains a Thai-capable face.
* A 12 px line asked to draw CJK borrows the 16 px face nudged up 2 px; rare,
  and only cosmetically imperfect.
