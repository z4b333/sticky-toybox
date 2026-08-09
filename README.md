# Toybox

Custom firmware for the [Seeed reTerminal Sticky](https://www.seeedstudio.com)
— an ESP32-S3 with a 3.97" e-paper screen and a magnet on the back. It turns
the device into a small touch-operated toy box: six games, a handful of
everyday tools, and notes you can pin to the fridge.

**Install it from your browser, no tools needed:
[z4b333.github.io/sticky-toybox](https://z4b333.github.io/sticky-toybox/)**

E-paper keeps its last image with no power at all, which is the idea the whole
thing is built around. A pinned note stays on the screen after the device
switches off, and stays there for as long as the magnet holds. It is a piece of
paper you can edit from your phone.

Notes, flashcards and lists work in English, Thai, Chinese, Korean, Japanese
and Vietnamese. The Thai readme is at [docs/README.th.md](docs/README.th.md).

## Games

- **Wordle** — 1,634 answers, 4,667 accepted guesses. Tracks win rate, streak
  and a guess chart.
- **Nonogram** — 5×5 and 10×10 picture puzzles. Every generated puzzle is
  checked to be solvable by logic alone. Timed, with best times.
- **2048** — swipe to merge, with undo. New tiles get a dashed border and
  merged ones a wedge in the corner, so you can see what changed after each
  move. Both marks last until the next move rather than flashing: on e-paper
  every change costs a visible refresh, so what happened is in how the board is
  drawn rather than in it changing.
- **Sudoku** — three difficulties. Every puzzle is generated with exactly one
  solution.
- **Battleship** — play against the device, or between two devices over
  ESP-NOW. The AI averages 40.5 shots out of 64.
- **XO** — easy, hard, or two players. The optional **3 MARKS** rule keeps only
  three marks per side on the board; placing a fourth removes your oldest, so
  the game never ends in a full-board draw. Hard mode never loses, which is
  verified by a test that searches the entire game tree.

## Tools

- **Notes.** Write or dictate them on your phone, then read them and tick
  checkboxes on the device. Pin one and it becomes the screen the device shows
  when it is off — pinning asks which way up you want it, with the note drawn
  full size so you are choosing the thing you are looking at rather than
  guessing from a settings row.
- **Flashcards** with spaced repetition, imported from your phone. The two side
  buttons grade a card without reaching up to the panel: DOWN reveals the
  answer and then takes it as known, UP sends it back to be asked again.
- **A list picker.** Type the list on the device or send it from your phone; it
  keeps whatever script it was written in.
- **Coin flip, dice** (D4 to D20, with modifiers), **random number, card draw.**
- **A countdown timer and stopwatch.**

## How it behaves

- 2048, Sudoku and Nonogram remember where you were. Leaving to the hub or
  powering off keeps the board; NEW always starts fresh.
- It sleeps by itself after five idle minutes, keeping the pinned note on
  screen. A running timer holds it awake.
- The pinned note follows the accelerometer, so it stays readable whichever way
  the magnet ends up. Turn that off and it rests at the angle you pinned it at
  instead. Everything else stays portrait.
- The sleeping note's footer shows the time, room temperature and battery, each
  of which can be turned off. The clock is set from your phone the first time
  you save a note, since the device has no network time.
- With nothing pinned it shows a goodbye card, a picture, or nothing at all.
  There is deliberately no clock: a panel that holds its last image with no
  power is exactly the wrong place for one. The time would be drawn on the way
  to sleep, wrong a minute later, and wrong for hours after that.
- A picture can be sent from the notes page on your phone. The browser crops,
  greyscales and dithers it, and shows you the result before it goes — a
  photograph at one bit per pixel is either striking or mud, and there is no
  way to tell but to look. What reaches the device is 48 KB of packed bits, so
  nothing on it has to decode anything.
- Below 3% battery it shuts down cleanly rather than risk a half-written
  screen.

The gear icon opens settings: hide apps you don't use, set the beep volume,
restore the how-to-play cards, reset stats. A second page covers the lock
screen — sleep timing, what an empty panel shows, where the power button wakes
to, and whether the note follows the accelerometer.

## Getting it onto a device

The [installer page](https://z4b333.github.io/sticky-toybox/) does everything
from Chrome or Edge over a USB-C data cable. It tells you which build it is
about to write, so you can check afterwards that it landed.

Language packs are optional. Common Chinese characters, Korean and Japanese
already work without them; the packs only add the rare ones, and they can be
added or removed later without touching the firmware.

The first boot after any flash opens with a welcome screen: it names the build
that is running, so you can check it against what the installer said it was
writing, and it asks whether the screen came up the right way round. If it did
not, hold either side button while the device powers on. That reaches the
service screen, which reports what the board answered on each bus and can
correct the display and touch orientation if your unit disagrees with mine.
[docs/BRINGUP.md](docs/BRINGUP.md) walks through it, and
[checklist.html](https://z4b333.github.io/sticky-toybox/checklist.html) is the
same thing as a tickable page for a phone.

## Known limitations

- The board's 8 MB of PSRAM is not configured, so the service screen reports
  none. Nothing currently needs it.
- The microSD slot is not used. It shares the display's SPI bus and has never
  been exercised.
- Thai line breaking works at character-cluster level, not word level, so a
  line can break in the middle of a word.
- Characters above U+FFFF, including emoji, are not supported.

## Licence

MIT for the project's own code — see [LICENSE](LICENSE). The vendored QR code
library and the glyph tables generated from third-party fonts have their own
terms, recorded in [THIRD-PARTY.md](THIRD-PARTY.md), including one question
about the Thai face that is not settled.

## Building it yourself

[docs/DEVELOPING.md](docs/DEVELOPING.md) covers the build, the host-side test
harness that renders every screen on a PC, the partition layout, and the two
things about this board that will otherwise waste your day.

`toybox-core/` is hardware-independent and talks to two small interfaces, so
the apps can be embedded in other firmware — see
[docs/PORTING.md](docs/PORTING.md).
