# Language support

Notes and flashcards accept any text a phone keyboard can produce. This page
explains what renders, how well, and how the system works. The UI itself
(buttons, titles, help cards) stays in English. Language support applies to
your content.

## Included in the firmware

The firmware ships with 8,290 glyphs at three sizes (16, 24 and 32 px),
about 2.1 MB of flash:

| script | coverage | font |
|---|---|---|
| Thai | complete (87 characters) | Loma |
| Chinese | GB2312 level 1, the 3,755 most common simplified characters | Noto Sans CJK SC |
| Japanese | all kana, plus JIS level 1 kanji | Noto Sans CJK |
| Korean | KS X 1001, the 2,350 syllables used in practice | Noto Sans CJK KR |
| Vietnamese | complete | DejaVu Sans |
| European languages | Latin-1 and Latin Extended-A | DejaVu Sans |

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
- Thai between letter clusters, but never before a vowel or tone mark
- everything else on spaces, as usual

Thai breaking is approximate. Proper Thai word breaking needs a dictionary,
which the firmware doesn't carry, so a line can break mid-word.

## Minimum sizes

Small text that's readable in English can be unreadable in other scripts.
Thai floors one step above whatever size it was asked for, and it is worth
saying why, because it is not the obvious reason. A Thai line has to leave room
for two storeys of marks above the letters and one below. The letters therefore
occupy roughly half the height Latin does in the same line box, so Thai set at
the same nominal size reads visibly smaller beside it. Standing it a step taller
is what makes the two look like the same size.

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
python3 tools/make_fonts_intl.py
python3 tools/make_font_pack.py /tmp/packs
```

The scripts need Python with Pillow and fontTools, plus the DejaVu, Loma and
Noto Sans CJK fonts installed.

## Current limitations

- Thai line breaks can fall mid-word (see above).
- No characters above U+FFFF, so no emoji.
- The CrossPoint Reader port uses that firmware's own fonts, which have no
  Thai. Thai text shows as boxes there.
