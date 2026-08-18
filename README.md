# Toybox

Custom firmware for the [Seeed reTerminal Sticky](https://www.seeedstudio.com)
(ESP32-S3, 3.97" e-paper touch screen, magnetic back). It turns the device
into a small toy box: 6 games, 2 book readers, a recipe reader, everyday
tools, and notes you can pin to the fridge.

**Install it from your browser, no tools needed:
[z4b333.github.io/sticky-toybox](https://z4b333.github.io/sticky-toybox/)**

E-paper keeps its last image with no power. That is the core idea here: a
pinned note stays on the screen after the device turns off, for as long as
the magnet holds. It works like a piece of paper you can edit from your
phone.

Notes, flashcards and lists support English, Thai, Chinese, Korean, Japanese
and Vietnamese.

## Games

- **Wordle** — 1,634 answers, 4,667 accepted guesses. Tracks win rate,
  streak and a guess chart.
- **Nonogram** — 5×5 and 10×10 picture puzzles. Every puzzle is solvable by
  logic alone. Timed, with best times.
- **2048** — swipe to merge, with undo. New and merged tiles are marked on
  the board so you can see what each move changed.
- **Sudoku** — three difficulties. Every puzzle has exactly one solution.
- **Battleship** — against the device, or between two devices over ESP-NOW.
- **XO** — easy, hard, or two players. The optional **3 MARKS** rule keeps
  only three marks per side, so the game never ends in a draw. Hard mode
  never loses (verified by a full game-tree test).

## Reading

- **Books** — `.tbk` volumes on the microSD card, black-and-white or
  four-level grey. Make them on a PC from CBZ files with
  `tools/make_tbk.py`. Manga can read right-to-left. The power button opens
  options: go to page, bookmarks, page-turn speed, close.
- **EPUB** — real ebooks laid out on the device, with three reading
  typefaces, text size and spacing, screen rotation, table of contents, and
  bookmarks that keep a chosen phrase. Illustrations prepared by a PC tool
  show as full pages.
- **Recipes** — reads the standard recipe data (schema.org JSON) that most
  recipe websites embed. Paste a link on the
  [recipe grabber](https://z4b333.github.io/sticky-toybox/recipe.html) to
  get a `.json` file for `/recipes` on the card, or paste the page to the
  device over WiFi. Ingredients have tick boxes; COOK mode shows one step
  per page in large type.
- Your reading position is saved on the card in the same format CrossPoint
  and CrossInk use (a KOReader sidecar is written too), and their `/Read`
  and `/epub` folders are read. Move the card between firmwares and the
  book opens at the same page. Covers are shared the same way.
- Page turns are fast partial refreshes with a full clean every so often —
  fast, normal or best, set per reader.

## Tools

- **Notes** — write or dictate on your phone, read and tick boxes on the
  device. Pairing is two QR codes and the device steps through them itself.
  Pin a note and it becomes the off-screen; the device asks which way up,
  following the accelerometer while you decide. Three text sizes. The phone
  editor shows the 4,000-byte limit as you type. A note can also come off
  the card: put a `.md` or `.txt` file in `/notes` and press CARD.
- **Flashcards** — spaced repetition, imported from your phone or off the
  card (`.tsv`, `.csv` or `.txt` in `/decks`, one card a line). Re-importing
  a deck keeps the boxes of every card whose question is unchanged. The side
  buttons grade cards one-handed.
- Both can be written on a keyboard first: the
  [note and flashcard editor](https://z4b333.github.io/sticky-toybox/editor.html)
  builds the file in the browser and downloads it for the card.
- **List picker** — type a list or send it from your phone, tap to pick one.
- **Coin flip, dice** (D4–D20 with modifiers), **random number, card draw**.
- **Timer and stopwatch** — the countdown can be adjusted while running.

## Everyday behaviour

- 2048, Sudoku and Nonogram save your game. NEW starts fresh.
- The device sleeps after 1–30 idle minutes (your choice), keeping the
  pinned note on screen. A running timer keeps it awake.
- The pinned note rotates with the device, or rests at the angle you pinned
  it. Everything else stays portrait.
- The sleeping screen's footer can show time, temperature and battery. The
  clock is set from your phone when you save a note.
- With nothing pinned, the off-screen shows a goodbye card, a picture, the
  cover of the book you are reading, or nothing.
- Pictures are plain `.bmp` files — one format for wallpaper, lock screen,
  sleep art and book covers. They come from the card or from your phone,
  which shows you the dithered result before sending. A `.bmp` lock picture
  is shown in real four-level grey. A `/sleep.bmp` on the card (or a random
  `.bmp` from `/.sleep`, the matcha-reader convention) is used at power-off.
- Below 3% battery it shuts down cleanly.

**Settings** (hold UP on the home screen for 3 seconds): which apps the hub
shows, wallpaper, lock screen (sleep timing, what an empty screen shows,
footer, wake target), files over WiFi, sound volume, restore the how-to
cards, reset stats.

## Installing

The [installer page](https://z4b333.github.io/sticky-toybox/) works from
Chrome or Edge over a USB-C data cable, and tells you which build it is
writing.

Language packs are optional. Common Chinese, Korean and Japanese characters
work without them; the packs add the rare ones and can be added or removed
any time.

First boot shows a welcome screen naming the running build and asking if
the screen is the right way round. If it is not, hold either side button
while powering on to reach the service screen, which can fix display and
touch orientation. See [docs/BRINGUP.md](docs/BRINGUP.md).

## Known limitations

- If the panel itself is dead, the screen keeps its old image and the
  device looks unflashed. The firmware signals this with six low beeps at
  boot.
- Thai lines can break mid-word (breaking is by character cluster).
- Characters above U+FFFF, including emoji, are not supported.

## Licence

MIT for the project's own code — see [LICENSE](LICENSE). The vendored QR
library and the glyph tables built from third-party fonts have their own
terms, listed in [THIRD-PARTY.md](THIRD-PARTY.md).

## Building and extending

- [docs/DEVELOPING.md](docs/DEVELOPING.md) — building, the PC test harness,
  partitions, board pitfalls.
- [docs/CONVERTER-SPEC.md](docs/CONVERTER-SPEC.md) — every file format the
  device reads and writes, for anyone making books or pictures for it.
- [docs/WALLPAPER-LOCKSCREEN.md](docs/WALLPAPER-LOCKSCREEN.md) — how
  wallpapers, lock pictures and sleep art work.
- [docs/LANGUAGES.md](docs/LANGUAGES.md) — fonts and language packs.
- [docs/PORTING.md](docs/PORTING.md) — `toybox-core/` is
  hardware-independent and can be embedded in other firmware.
