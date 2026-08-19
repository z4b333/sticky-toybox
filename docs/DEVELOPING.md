# Developing Toybox

Everything the README used to carry about building the firmware, testing it,
and finding your way around the source.

## Building

The project uses [PlatformIO](https://platformio.org).

```
pio run                    # build
pio run -t upload          # flash over USB-C
pio device monitor -b 115200
```

If you don't want to install a toolchain, use the web flasher, or flash the
prebuilt image directly:

```
esptool --chip esp32s3 write_flash 0x0 docs/firmware/toybox-full.bin
```

`tools/make_image.sh` rebuilds that image. It merges the bootloader, the
partition table and the app into one file, because the web flasher writes a
single file to offset 0. It also writes `docs/firmware/version.json`, which is
what the flasher page prints beside the install button — so what the page says
it is about to write and what the service screen says afterwards can be
compared directly.

### Two things about this board that will waste your day

**The flash mode must be `dio`, not `qio`.** This module is an ESP32-S3**R8**:
its octal PSRAM shares the extra SPI data lines, so a header asking for quad
flash reads makes the ROM load the second-stage bootloader as garbage. The
symptom is a boot loop with `rst:0x7 (TG0WDT_SYS_RST)`, `mode:QIO` and
`ets_loader.c 78`, and nothing on the panel at all — which looks exactly like a
failed flash.

**There are two coordinate spaces and they are both 800 and 480.** `EPD_W` and
`EPD_H` are the logical portrait 480×800 that every layout is written in.
`PANEL_W` and `PANEL_H` are the hardware's 800×480, which is also the shape of
the framebuffer. `epdMapPixel` in `src/epd.h` is the one place they meet. The
controller must only ever be told the panel's dimensions; giving it the
logical ones makes each row 60 bytes wide where the framebuffer supplies 100,
and the image repeats down the panel over a field of noise.

## The partition table

`partitions_toybox.csv`:

| partition | size | contents |
|---|---|---|
| factory (app) | 4 MB | firmware, currently about 3.9 MB — roughly 280 KB spare |
| zh_font | 4.4 MB | optional Chinese font pack |
| ko_font | 2.4 MB | optional Korean font pack |
| ja_font | 0.8 MB | optional Japanese font pack |
| spiffs (LittleFS) | 4.7 MB | notes, decks, the lock screen picture |

Language packs are raw flash partitions written by the installer and
memory-mapped by the firmware, so they cost no RAM. An erased partition fails
its magic check and is skipped. There is no OTA.

## The service screen

Hold either side button while the device powers on. It is driven entirely by
the three physical buttons, so it works with touch completely broken, and it
opens by itself if the touch controller does not answer.

It reports what answered on each bus, the battery voltage, PSRAM, how many
font packs loaded, the build it is running, and the live accelerometer reading.
It can correct the display and touch orientation without rebuilding anything,
and saves what you choose to NVS.

[docs/BRINGUP.md](BRINGUP.md) walks through a first boot in order, and
[checklist.html](checklist.html) is the same thing as a tickable page for a
phone held next to the device.

## Testing

Everything can be tested on a PC. There are two host-side programs in
`test/host/`:

`test_logic.cpp` covers game logic: the nonogram generator and solver, Wordle
scoring, 2048 moves, Sudoku uniqueness, Battleship rules and its network
protocol, and the XO game-tree proof.

`host_preview.cpp` builds the firmware against a fake display, renders every
screen to an image file, and checks the results. It verifies tap routing, the
settings flow, note editing, the pinned screen, multilingual rendering, font
pack loading, the four display rotations, the QR encoder, the side buttons,
and that no text runs off the edge of the panel. On the real device, text past
the edge is clipped silently; here it fails the run instead.

```
cd test/host
g++ -std=gnu++17 -O2 -w -DTOYBOX_HOST -I . -I mock -I ../../src \
  -I ../../toybox-core/src -I ../../lib/QRCode/src -I ../../lib/miniz/src -I ../../lib/tjpgd/src \
  host_preview.cpp ../../lib/QRCode/src/qrcode.c ../../src/gfx.cpp \
  ../../src/fonts_intl.cpp ../../src/sensors.cpp ../../src/sticky_host.cpp ../../src/sdcard.cpp \
  ../../toybox-core/src/toybox.cpp ../../toybox-core/src/hub.cpp ../../toybox-core/src/epubcore.cpp \
  ../../toybox-core/src/epubcover.cpp -x c ../../lib/tjpgd/src/tjpgd.c -x none \
  ../../lib/miniz/src/toybox_miniz_impl.c \
  ../../toybox-core/src/settings.cpp ../../toybox-core/src/wordle.cpp \
  ../../toybox-core/src/nonogram.cpp ../../toybox-core/src/game2048.cpp \
  ../../toybox-core/src/xo.cpp -o preview
./preview
```

## The Seeed Playground registry

Seeed keeps a public contribution repo for firmware that appears in the
reTerminal Sticky Playground catalogue:
<https://github.com/Seeed-Projects/reterminal-sticky-playground-registry>. A
merged entry becomes a card on their site with a browser flashing page beside
it.

They take two kinds of contribution. The SOURCE path hands over a buildable
tree for their GitHub Actions to compile, and their only build adapter is
ESP-IDF with CMake -- "Projects that need another build system should open an
issue first". Toybox is PlatformIO and Arduino, so that path needs their
agreement first. The FIRMWARE-ONLY path takes the tested binaries with a
manifest recording every offset, byte size and SHA-256, plus an upstream source
URL and licence, and that is the one this repo generates:

```
sh tools/make_image.sh
python3 tools/make_registry.py --preview path/to/device-photo.jpg
```

It writes `dist/registry/toybox/` from the same four binaries the installer
page serves, so Seeed's flasher and this project's own write identical bytes.
Pass `--preview`: their community entries want a real screenshot or photo, and
without it the script falls back to a harness render and says so.

To check it the way their reviewers will, clone the registry, drop the entry in
and run their validator:

```
git clone --depth 1 https://github.com/Seeed-Projects/reterminal-sticky-playground-registry reg
cp -R dist/registry/toybox reg/integrations/toybox
cd reg && npm install && npm test && npm run validate
```

Two differences to keep in mind when reading a bug report from someone who
installed from Seeed's page rather than ours: their flasher always writes the
whole four-part package (there is no app-only UPDATE), and it cannot offer the
optional CJK font packs, which stay a `/fonts` install from this project's own
page.

Build with `-DTOYBOX_CP_FONTS` to render every screen with a much taller set of
faces instead. It was written to check the shared code under a second host's
font metrics; that host is gone, but the pass stays as a stress test -- those
faces run half again to twice the height of the Sticky's, and several layout
bugs this month were invisible at the device's own size and obvious under
these. Sample renders are in `docs/screens/`.

A guard worth understanding before you add one: several of the bugs that
reached hardware were invisible to a screenshot. A QR code that encodes
nothing looks like a QR code. A landscape rotation that is half a turn out
fills the panel exactly as well as a correct one. Guards that count ink pass
both. The useful ones check a property — that the chosen QR version actually
holds the payload, that content-up at rotation 1 lands where content-right
does at rotation 0.

## Text sizes

The display is 235 DPI, so pixel sizes map directly to physical sizes:

| size | box | on screen | used for |
|---|---|---|---|
| `TS_HUGE` | 44 px | 4.8 mm | scores and large numbers |
| `TS_LARGE` | 32 px | 3.5 mm | primary buttons, titles |
| `TS_MED` | 24 px | 2.6 mm | body text and captions |
| `TS_SMALL` | 16 px | 1.7 mm | short labels only |

These were 12/16/24/32 until the first person held the device and said the text
was too small to read. They had been chosen by counting pixels on a monitor,
where 16 px is a comfortable size; on a 235 DPI panel it is 1.7 mm, about five
point. Body text is now 2.6 mm, roughly seven and a half point.

Think in millimetres for this device. One millimetre is 9.25 pixels.

Thai is always set one step larger than it was asked for, because its two
storeys of marks leave the letters about half the height Latin gets in the same
box. Chinese, Japanese and Korean are not, since they fill their box. See
[LANGUAGES.md](LANGUAGES.md).

The glyph tables are generated by `tools/make_fonts.py` (ASCII) and
`tools/make_fonts_intl.py` (everything else). Their ink thresholds run in
opposite directions — one tests darkness on white, the other tests coverage —
so 176 in the first and 80 in the second are the same decision, since
255 − 176 = 79.

## Project layout

```
src/            hardware layer: display, touch, buzzer, sensors, power, loop
toybox-core/    all apps and screens, hardware independent
tools/          font generators, the image builder, the Thai rendering study
test/host/      logic tests and the screen preview harness
test/web/       browser test for the phone-side picker page
docs/           documentation, screen renders, and the web installer page
prebuilt/       flashable firmware image
```

`toybox-core/` only talks to two small interfaces — a canvas and a host — so it
can be embedded in other firmware. The CrossPoint Reader port does this in
about 110 lines. See [PORTING.md](PORTING.md).

When you add something to a `ToolsHost`, give it a default implementation built
on what is already there. `soundLevel()` defaults to answering through
`soundOn()`, so a host that only has a switch keeps working and its settings
screen offers the two words a switch can mean.
