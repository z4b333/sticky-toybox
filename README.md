# Toybox

Games and small tools for the **Seeed reTerminal Sticky** (ESP32-S3, 3.97"
800×480 monochrome e-paper, GT911 capacitive touch) — the things you reach for
in an idle moment, driven entirely by tapping the screen.

Every screen is laid out **portrait, 480×800** (the device held tall). Rotation
lives in one place, the display driver's `drawPixel()`, so graphics and text
rotate together, and touch coordinates are mapped back to match in `touch.cpp`.

The user-facing content — notes and flashcards — is **multilingual**: English,
Thai, Chinese, Korean, Japanese and Vietnamese render out of the box, with
optional full-coverage font packs for rare characters. See
[docs/LANGUAGES.md](docs/LANGUAGES.md).

*(เอกสารฉบับภาษาไทยเดิมอยู่ที่ [docs/README.th.md](docs/README.th.md))*

## What's on the hub

**Play** — Wordle (1,634 answers / 4,667 accepted guesses; win %, streak and a
guess-distribution chart), Nonogram (5×5 and 10×10, every generated puzzle
verified logic-solvable, timed with best times, HINT reveals one square),
2048 (undo, a dashed border marks the newly spawned tile and a one-shot blink
marks the merge — emphasis first, then stillness, so no e-ink refresh sits
between the swipe and its result), Sudoku (three difficulties, every puzzle
generated with a unique solution, per-difficulty solve counts), Battleship
(8×8, aim then FIRE to commit; a hunt/target AI averaging 40.5 shots of 64, or
**two devices** over ESP-NOW with hidden fleets), and XO (EASY / HARD /
2-player, CLASSIC or **3 MARKS** rules).

**3 MARKS** exists because 3×3 XO is nearly always a draw: each side keeps at
most three marks on the board, and placing a fourth lifts the oldest off (the
one about to leave is drawn faint a turn ahead, so you can plan around it).
The board never fills, there are no board-full draws, and the game becomes a
chase. HARD is negamax with alpha-beta (full-depth in CLASSIC, 6 plies in
3 MARKS); a host-side test walks the complete game tree to confirm it never
loses, moving first or second.

**Decide** — coin flip with an all-time tally, dice (D4–D20 drawn as real
wireframe solids, with count and modifier), random number / card draw, and a
list picker whose list can come from your phone.

**Everyday** — countdown/stopwatch timer, Leitner flashcards imported from
your phone over a QR-joined access point, and notes written or dictated on
your phone, rendered as Markdown with tappable checkboxes and strikethrough.
A note can be **pinned**: it becomes the power-off screen (e-paper keeps its
image with no power — the note stays on the fridge), waking straight back
into it, ticking lines with a tap and locking again with the power button.

Apps can be hidden from the hub in Settings (the gear in the top bar), which
also holds sound on/off, "show how to play again", and stats reset. Hiding an
app keeps everything saved in it.

## Building and flashing

```
pio run                    # build (PlatformIO, pioarduino ESP32 platform)
pio run -t upload          # flash over USB-C
pio device monitor -b 115200
```

A prebuilt image is committed at `prebuilt/toybox_full.bin` for flashing
without a toolchain (app offset 0x10000, partition table
`partitions_toybox.csv`).

The partition map is a single 4 MB app slot plus an **11.9 MB LittleFS**
filesystem — this firmware has no OTA, and the filesystem holds notes,
flashcard decks and downloadable font packs.

First time on real hardware? Follow [docs/BRINGUP.md](docs/BRINGUP.md).

## Testing without the device

Nothing here has to be judged on hardware. Two host-side programs cover logic
and pixels:

**Logic tests** — `test/host/test_logic.cpp`: the nonogram line solver and 100
generated puzzles (all logic-solvable, all matching their solution), Wordle
scoring including repeated letters, 2048 sliding/merging, the picker's list
codec, XO rules and the full game-tree proof for HARD, Battleship rules, AI
and the two-device duel protocol including dropped packets, and the Sudoku
generator (every generated puzzle re-solved to confirm uniqueness).

**Screen previews** — `test/host/host_preview.cpp` builds the firmware against
a mock panel, drives the same `Toybox` object through the same host seam,
renders **every screen to an image**, and runs guards over the results: hub
tap routing under both visibility masks, the settings flow, note taps landing
in the Markdown, the pinned lock screen, tool lifecycle under repeated
open/close, help-card gating, record clears, script width sanity, multilingual
note and flashcard rendering, Thai name survival, font-pack installation, and
a **text-overflow detector** — on the device, text past the panel edge is
clipped silently; here it fails the run, named by screen.

```
cd test/host
g++ -std=gnu++17 -O2 -w -DTOYBOX_HOST -I . -I mock -I ../../src \
  -I ../../toybox-core/src -I ../../lib/QRCode/src \
  host_preview.cpp ../../lib/QRCode/src/qrcode.c ../../src/gfx.cpp \
  ../../src/fonts_intl.cpp ../../src/sticky_host.cpp \
  ../../toybox-core/src/toybox.cpp ../../toybox-core/src/hub.cpp \
  ../../toybox-core/src/settings.cpp ../../toybox-core/src/wordle.cpp \
  ../../toybox-core/src/nonogram.cpp ../../toybox-core/src/game2048.cpp \
  ../../toybox-core/src/xo.cpp -o preview
./preview        # writes preview_NN_<screen>.pgm + prints guard results
```

Adding `-DTOYBOX_CP_FONTS` re-renders every screen with the CrossPoint
Reader's UI faces (up to twice as tall) to prove the shared core survives a
different host's metrics. Reference renders live in `docs/screens/`. The
phone-side picker page has its own Chromium-driven test in `test/web/`.

## Text sizes

The panel is 235 DPI, so pixel sizes translate directly to physical size:

| | box | real size | used for |
|---|---|---|---|
| `TS_HUGE` | 32 px | 3.4 mm | scores, dice totals, headline numbers |
| `TS_LARGE` | 24 px | 2.6 mm | primary buttons, screen titles |
| `TS_MED` | 16 px | 1.7 mm | body text, captions, secondary buttons |
| `TS_SMALL` | 12 px | 1.3 mm | one-word labels and footnotes only |

ASCII draws from proportional DejaVu faces baked at each box, with real bold
cuts rather than a smeared regular. Non-Latin scripts have **per-script
minimum sizes** — Thai never renders below `TS_LARGE`, han and hangul never
below `TS_MED` — enforced by `scriptFloor()` wherever user text is drawn.
The full text-engine story is in [docs/LANGUAGES.md](docs/LANGUAGES.md).

## Repository layout

```
src/               the firmware shell: panel driver, touch, buzzer, power
                   latch, the loop, and the StickyHost/StickyCanvas seam
src/fonts_ui.h     generated ASCII faces (DejaVu, 4 sizes, regular + bold)
src/fonts_intl.*   generated international faces (6 scripts, 3 sizes)
toybox-core/       everything above the seam: hub, settings, all 13 apps —
                   consumed as a PlatformIO library, and built unchanged by
                   the CrossPoint Reader port
tools/             font generators (make_fonts.py, make_fonts_intl.py,
                   make_font_pack.py) and the Thai rendering study
                   (thai_proof.py) that shaped the text engine
test/host/         logic tests + the preview harness and its guards
test/web/          Chromium test for the phone-side picker page
docs/              documentation and reference screen renders
prebuilt/          flashable firmware image from the current source
```

## Porting

`toybox-core/` draws through two small interfaces — `ToolsCanvas` (pixels and
text) and `ToolsHost` (prefs, refresh, beeper, navigation) — and includes no
display or input headers. The CrossPoint Reader port implements that seam over
its `GfxRenderer` in about 110 lines and builds this directory unchanged. To
put Toybox inside another firmware, see [docs/PORTING.md](docs/PORTING.md).

## Hardware caveat

Panel scan direction and touch orientation follow community bring-up data,
not yet verified on this project's own hardware. If the image comes up
mirrored or upside down on a real device, adjust the scan direction in
`epd.cpp` (the TB bits of command 0x01) and the flips in `touch.cpp` —
and see [docs/BRINGUP.md](docs/BRINGUP.md) for the full first-boot checklist.
