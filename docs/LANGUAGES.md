# Language support

Notes and flashcards accept any text a phone keyboard can produce. This page
explains what renders, how well, and how the system works. The UI itself
(buttons, titles, help cards) stays in English. Language support applies to
your content.

## Included in the firmware

The UI face comes in two families, chosen on the device (Settings -> Font):
**Noto Sans** (the default) and **IBM Plex Sans**. Each carries its own Latin,
Vietnamese and Thai at the four UI sizes (18, 24, 32 and 44 px), so Thai sits
at the size and on the baseline of the Latin beside it. Chinese, Japanese and
Korean are shared, at three sizes (16, 24 and 32 px):

| script | coverage | font |
|---|---|---|
| Thai | complete (87 characters) | Noto Sans Thai Looped / IBM Plex Sans Thai Looped |
| Vietnamese | complete | Noto Sans / IBM Plex Sans (Noto where Plex has no glyph) |
| European languages | Latin-1 and Latin Extended-A | Noto Sans / IBM Plex Sans |
| Chinese | GB2312 level 1, the 3,755 most common simplified characters | Noto Sans CJK SC |
| Japanese | all kana, plus JIS level 1 kanji | Noto Sans CJK |
| Korean | KS X 1001, the 2,350 syllables used in practice | Noto Sans CJK KR |

All of it is rendered grid-fitted for the 1-bit panel, so equal strokes come
out equal widths. Thai lines break between words, using ICU's Thai word list
(26,383 words, about 135 KB).

These sets cover normal everyday text. A character outside them is drawn as
a small empty box.

## Font packs for full coverage

If you need rare characters, install a font pack. Each pack contains a
language's full character set, minus what the firmware already includes:

| pack | adds | size |
|---|---|---|
| `zh_full.tfp` | 15,967 more Chinese characters | 4.3 MB |
| `ko_full.tfp` | the remaining 8,822 Korean syllables | 2.3 MB |
| `ja_full.tfp` | 2,732 more kanji | 0.7 MB |

The easiest way to install packs is the web flasher: each language is a
checkbox, and any combination can be selected. Each pack has its own flash
partition (see `partitions_toybox.csv`) and is written there directly. The
firmware memory-maps installed packs straight from flash, so they cost no
RAM. An empty partition is simply skipped.

Packs are built with `tools/make_font_pack.py`. As an alternative to the
flasher, a pack file copied to `/fonts/` on the device's filesystem is also
picked up at boot.

## How the text engine works

ASCII text uses the original bitmap font tables. Everything else uses a
second set of tables where each glyph stores its own position offset and
width. This matters for Thai: vowel and tone marks have zero width and a
negative offset, so they draw on top of the previous letter without any
special handling.

All text functions read UTF-8. Bad bytes render as `?` instead of breaking
the rest of the string.

There is one Thai-specific rule. A tone mark sits higher when it rides on an
upper vowel (as in กี่) and lower on a bare consonant (as in ก่). The fonts
are baked at the higher position, and the font generator measures how far
the mark must drop in the second case by comparing against a real text
shaping engine. The result is stored per font size. The study behind this is
`tools/thai_proof.py`.

Bold for non-ASCII text is drawn by printing the glyph twice with a 1 px
offset. Real bold fonts would double the flash cost for little visible gain
at this resolution.

## Line breaking

Thai and Chinese don't use spaces between words, so the normal break-on-space
rule doesn't work. The wrap code breaks:

- Chinese, Japanese and Korean at any character, except before closing
  punctuation like 。、！？
- Thai between words, found with ICU's Thai word list; a stretch the list
  does not know (a name) breaks between letter clusters, never before a vowel
  or tone mark and never after เ แ โ ใ ไ
- everything else on spaces, as usual

## Minimum sizes

Small text that's readable in English can be unreadable in other scripts.
Thai never sits at the smallest size (18 px): a Thai line has to leave room
for two storeys of marks above the letters and one below, which leaves the
letters too small to read in that box. At every other size it matches the
Latin beside it -- the v2 faces are drawn as pairs. (Up to v1.4 Thai went a
step up at every size, because Loma drew a good deal smaller than DejaVu.)

Han and hangul fill their box the way Latin does. They only need the box not to
be tiny — which, since the whole scale moved up, `TS_SMALL` no longer is, so
they are not bumped at all any more.

There is no 44 px international face. At 8,290 code points a face costs roughly
its height squared, and a 44 px cut would add about 1.9 MB to a 4 MB app
partition to make headlines larger in Chinese. `TS_HUGE` borrows the 32 px face
and centres it in the taller line instead.

File names also keep their original script. A note named รายการ stays
รายการ in the list and in the title bar.

## Regenerating the fonts

```
python3 tools/make_fonts.py > src/fonts_ui.h
python3 tools/make_fonts_script.py
python3 tools/make_fonts_intl.py
python3 tools/make_font_pack.py /tmp/packs
```

The scripts need Python with Pillow (with raqm, for the Thai mark
measurements) and fontTools. The Noto, IBM Plex and Literata files are in
`tools/fonts/`; Noto Sans CJK has to be installed
(`/usr/share/fonts/opentype/noto/`).

## Current limitations

- Thai lines break between words the word list knows; an unknown word (a
  name) can still break between syllables.
- With the built-in faces, ญ and ฐ keep their tail over a lower vowel
  (กตัญญู): the tailless form needs shaping the firmware does not do. Card
  fonts that carry the shifted forms (TLWG faces) draw it right.
- No characters above U+FFFF, so no emoji.
- The CrossPoint Reader port uses that firmware's own fonts, which have no
  Thai. Thai text shows as boxes there.
