# Toybox

Custom firmware for the [Seeed reTerminal Sticky](https://www.seeedstudio.com)
(ESP32-S3, 3.97" 800×480 e-paper display, capacitive touch). It turns the
device into a small touch-operated toy box: six games, a few everyday tools,
and notes you can pin to the screen.

You can flash it from your browser, no tools needed:
**https://z4b333.github.io/sticky-toybox/**

Notes and flashcards support English, Thai, Chinese, Korean, Japanese and
Vietnamese. See [docs/LANGUAGES.md](docs/LANGUAGES.md) for details.

The original Thai readme is at [docs/README.th.md](docs/README.th.md).

## Features

**Games**

- **Wordle** – 1,634 answers, 4,667 accepted guesses. Tracks win rate, streak
  and a guess chart.
- **Nonogram** – 5×5 and 10×10 picture puzzles. Every generated puzzle is
  checked to be solvable by logic alone. Timed, with best times.
- **2048** – swipe to merge, with undo. New tiles get a dashed border and
  merged tiles blink once, so you can see what changed after each move.
- **Sudoku** – three difficulties. Every puzzle is generated with exactly one
  solution.
- **Battleship** – play against the device, or between two devices over
  ESP-NOW. The AI averages 40.5 shots out of 64.
- **XO (tic-tac-toe)** – easy, hard, or two players. The optional **3 MARKS**
  rule keeps only three marks per side on the board. Placing a fourth removes
  your oldest one, so the game never ends in a full-board draw. Hard mode
  never loses. This is verified by a test that searches the entire game tree.

**Tools**

- Coin flip, dice (D4 to D20, with modifiers), random number, card draw.
- A list picker. Type the list on the device or send it from your phone.
- A countdown timer and stopwatch.
- Flashcards with spaced repetition. Decks are imported from your phone.
- Notes. Write or dictate them on your phone, then read and tick checkboxes
  on the device. A note can be pinned so it stays on the screen even when
  the device is off. E-paper keeps its image without power.

The gear icon on the hub opens settings, where you can hide apps, turn sound
on or off, restore the how-to-play cards, and reset stats.

**Device behaviour**

- 2048, Sudoku and Nonogram remember where you were. Leaving to the hub or
  powering off keeps the board; NEW always starts fresh.
- The battery level shows on the hub. Below 3% the device shuts down cleanly
  rather than risking a half-written screen.
- It sleeps by itself after five idle minutes, keeping the pinned note on
  screen. A running timer holds it awake.
- The pinned note follows the accelerometer, so it stays readable whichever
  way the magnet ends up. Everything else stays portrait.
- The sleeping note's footer shows the time and room temperature. The clock
  is set automatically the first time you save a note from your phone, since
  the device has no network time.

## Building

The project uses [PlatformIO](https://platformio.org).

```
pio run                    # build
pio run -t upload          # flash over USB-C
pio device monitor -b 115200
```

If you don't want to install a toolchain, use the web flasher above, or flash
the prebuilt image directly:

```
esptool --chip esp32s3 write_flash 0x0 docs/firmware/toybox-full.bin
```

The partition table (`partitions_toybox.csv`) has one 4 MB app slot, a
partition for each optional language pack, and a 4.7 MB LittleFS filesystem
for notes and decks. There is no OTA.

Before running on real hardware for the first time, read
[docs/BRINGUP.md](docs/BRINGUP.md).

## Testing

Everything can be tested on a PC. There are two host-side programs in
`test/host/`:

`test_logic.cpp` covers game logic: the nonogram generator and solver, Wordle
scoring, 2048 moves, Sudoku uniqueness, Battleship rules and its network
protocol, and the XO game-tree proof.

`host_preview.cpp` builds the firmware against a fake display, renders every
screen to an image file, and checks the results. It verifies tap routing,
the settings flow, note editing, the pinned screen, multilingual rendering,
font pack loading, and that no text runs off the edge of the panel. On the
real device, text past the edge is clipped silently. Here it fails the test
run instead.

```
cd test/host
g++ -std=gnu++17 -O2 -w -DTOYBOX_HOST -I . -I mock -I ../../src \
  -I ../../toybox-core/src -I ../../lib/QRCode/src \
  host_preview.cpp ../../lib/QRCode/src/qrcode.c ../../src/gfx.cpp \
  ../../src/fonts_intl.cpp ../../src/sensors.cpp ../../src/sticky_host.cpp \
  ../../toybox-core/src/toybox.cpp ../../toybox-core/src/hub.cpp \
  ../../toybox-core/src/settings.cpp ../../toybox-core/src/wordle.cpp \
  ../../toybox-core/src/nonogram.cpp ../../toybox-core/src/game2048.cpp \
  ../../toybox-core/src/xo.cpp -o preview
./preview
```

Build with `-DTOYBOX_CP_FONTS` to render all screens with the CrossPoint
Reader's fonts instead. This checks that the shared code still lays out
correctly under a different host's font metrics. Sample renders are in
`docs/screens/`.

## Text sizes

The display is 235 DPI, so pixel sizes map directly to physical sizes:

| size | box | on screen | used for |
|---|---|---|---|
| `TS_HUGE` | 32 px | 3.4 mm | scores and large numbers |
| `TS_LARGE` | 24 px | 2.6 mm | primary buttons, titles |
| `TS_MED` | 16 px | 1.7 mm | body text and captions |
| `TS_SMALL` | 12 px | 1.3 mm | short labels only |

Some scripts have a minimum size for readability: Thai never renders below
24 px, and Chinese, Japanese and Korean never below 16 px. See
[docs/LANGUAGES.md](docs/LANGUAGES.md).

## Project layout

```
src/            hardware layer: display, touch, buzzer, sensors, power, loop
toybox-core/    all apps and screens, hardware independent
tools/          font generators and the Thai rendering study
test/host/      logic tests and the screen preview harness
test/web/       browser test for the phone-side picker page
docs/           documentation, screen renders, and the web flasher page
prebuilt/       flashable firmware image
```

`toybox-core/` only talks to two small interfaces (a canvas and a host), so
it can be embedded in other firmware. The CrossPoint Reader port does this in
about 110 lines. See [docs/PORTING.md](docs/PORTING.md).

## Known limitations

- Nothing has been tested on real hardware yet. Display orientation and touch
  mapping come from community bring-up notes. If the image is mirrored or
  flipped on your device, see the notes in `epd.cpp` and `touch.cpp`.
- Thai line breaking works at character-cluster level, not word level, so a
  line can break in the middle of a word.
- Characters above U+FFFF (such as emoji) are not supported.
