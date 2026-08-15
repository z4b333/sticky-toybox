# Toybox

Custom firmware for the [Seeed reTerminal Sticky](https://www.seeedstudio.com)
— an ESP32-S3 with a 3.97" e-paper screen and a magnet on the back. It turns
the device into a small touch-operated toy box: six games, two book readers,
a handful of everyday tools, and notes you can pin to the fridge.

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

## Reading

- **Books** — pre-converted `.tbk` volumes off the microSD card, in 1-bit or
  four-level grey. Made on a PC from your own CBZs with `tools/make_tbk.py`;
  manga read right-to-left when marked. A page fills the glass: the edges
  turn, the middle shows where you are, and the power button opens options —
  go to a page by number, bookmarks, page-turn speed, close.
- **EPUB** — real ebooks, laid out live with the device fonts, with
  illustrations a PC app prepared shown as whole pages. Your place is kept on
  the card in the CrossPoint/CrossInk family's own format — same hash, same
  bytes, and their `/Read` and `/epub` shelves are listed too — so the same
  card moved between firmwares opens the same book at the same page (a
  KOReader sidecar is written too). Covers are traded the same way: one left
  by CrossInk is used without a decode, and ours is left where CrossInk
  looks. Options cover the table of contents, bookmarks down to a chosen
  phrase, text size and spacing.
- Both readers list series folders under `/books`, remember your place, paint
  the cover while a book opens, and can leave the current book's cover on the
  lock screen. Page turns are a 0.3 s partial refresh with a cleaning full
  every so often — fast, normal or best, chosen per reader, because text and
  artwork ghost differently.

## Tools

- **Notes.** Write or dictate them on your phone, then read them and tick
  checkboxes on the device. Pairing is two steps and the device moves between
  them itself: scan the first code to join its wifi, and the moment your phone
  is on it, the screen changes to the link. Pin one and it becomes the screen the device shows
  when it is off — pinning asks which way up you want it, with the note drawn
  full size so you are choosing the thing you are looking at rather than
  guessing from a settings row — and it follows the accelerometer while the
  question is open, so turning the device is the control. Note text comes in
  three sizes, cycled on the note itself, and the phone editor shows the
  device's 4,000-byte limit instead of letting a long note be trimmed in
  silence.
- **Flashcards** with spaced repetition, imported from your phone. The two side
  buttons grade a card without reaching up to the panel: DOWN reveals the
  answer and then takes it as known, UP sends it back to be asked again.
- **A list picker.** Type the list on the device or send it from your phone; it
  keeps whatever script it was written in.
- **Coin flip, dice** (D4 to D20, with modifiers), **random number, card draw.**
- **A countdown timer and stopwatch.** The countdown adjusts by a minute, half
  a minute or five seconds, while it is running as well as before it starts.

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
- With nothing pinned it shows a goodbye card, a picture, the cover of the
  book you are reading, or nothing at all.
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

Settings (hold UP on the home screen for three seconds) covers: which apps
the hub shows, the wallpaper and the lock screen picture — chosen from
`.tbi` or plain `.bmp` files on the card, converted and copied in so the
card can come out; a `.bmp` lock picture is kept in the panel's real
four-level grey and drawn with nothing else over it. A `/sleep.bmp` (or a
random face from `/.sleep`, matcha-reader's convention) on the card wins at
power-off. Also: files over WiFi, beep volume, restoring the first-time
cards, and resetting stats. The
lock screen page also holds sleep timing, what an empty panel shows, where
the power button wakes to, and whether the note follows the accelerometer.

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

- If the panel never answers, the firmware runs perfectly and you see nothing:
  e-paper holds its last image, so a device with an unresponsive display looks
  like a failed flash. It now says so with six low notes at boot and a line on
  the service screen, which is the best it can do — the one output that cannot
  report a dead display is the display.
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
