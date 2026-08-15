# Making books for Toybox

Everything a PC-side converter needs to produce `.tbk` and `.epub` files, and
the pictures that go with them, for the Seeed reTerminal Sticky running Toybox.
Self-contained: byte layouts, naming rules, the rules the firmware enforces,
and what it does with each thing once it has it.

Written against **v1.0.0-beta.32**. Reference implementations live in this repo
and are the tie-breaker if anything here disagrees with the device:

| tool | what it makes |
|---|---|
| `tools/make_tbk.py` | a `.tbk` volume from a CBZ or a folder of images |
| `tools/make_tbi.py` | a `.tbi` picture from any image |
| `tools/add_epub_art.py` | adds pre-rendered artwork to an existing EPUB |
| `test/host/epub_cli` | reports what the device would find in an EPUB |

---

## 0. The panel, in one paragraph

480 × 800 portrait, 235 DPI, one bit per pixel. A full refresh takes about
1.7 s, a four-level grey page about 3 s. There is no colour, no partial refresh
worth using on artwork, and no GPU. **Every pixel the device draws is prepared
on your machine or arrives as a page of a book** — the firmware scales and
dithers only when it has no choice.

One millimetre is 9.25 pixels. Body text on the device is 24 px ≈ 2.6 mm.

---

## 1. Where files go

```
/books/                         the shelf
/books/<name>.tbk               a loose volume
/books/<name>.epub              a loose ebook
/books/<series>/<name>.tbk      a series folder -- ONE level deep, no deeper
/books/<series>/<name>.epub
```

Rules the firmware enforces:

- **Extensions are matched case-insensitively**, `.tbk` and `.epub`.
- **The whole absolute path must be 127 bytes or less.** A longer one is left
  off the shelf rather than shown and broken — a real release filename plus
  `/books/` gets closer to this than you would think.
- Series folders are **one level**. `/books/A/B/x.tbk` is not shown.
- Books loose in the card's root are also listed, as a convenience. Put them
  in `/books`.
- The **title** shown on the shelf is the `.tbk` header's title field, or for
  an EPUB the filename with its extension removed, cut to 40 bytes. The EPUB's
  internal `<dc:title>` is not used — the filename is what the owner sees in
  the file manager, so the two agreeing matters more.

**The path is an identity.** Reading positions, covers and bookmarks are all
keyed off the book's absolute path. Renaming or moving a book orphans them;
the device's own file manager warns about this at the moment of renaming.

---

## 2. `.tbi` — the one picture format

Wallpapers, lock screens, book covers and EPUB artwork are all the same file.

```
offset  type      value
0       char[4]   "TBI1"
4       u16 LE    width  = 480
6       u16 LE    height = 800
8       ...       48,000 bytes of pixels
                  total file size = 48,008 bytes exactly
```

Pixels: row-major, top to bottom, **60 bytes per row**, **most significant bit
is the leftmost pixel**, **1 = white, 0 = black**. This is the framebuffer's own
convention, so the device copies rather than converts.

Anything that is not exactly 48,008 bytes, or whose header is not `TBI1` 480×800,
is **ignored** — the firmware falls back rather than drawing something wrong.

```python
from PIL import Image
W, H = 480, 800

def to_tbi(path, fit=True, dither=True, stretch=False):
    im = Image.open(path).convert("L")
    s = (min if fit else max)(W / im.width, H / im.height)
    nw, nh = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = im.resize((nw, nh), Image.LANCZOS)
    if stretch:
        from PIL import ImageOps
        im = ImageOps.autocontrast(im, cutoff=1)
    canvas = Image.new("L", (W, H), 255)
    canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))
    bw = canvas.convert("1") if dither else canvas.point(lambda v: 255 if v >= 128 else 0).convert("1")
    bits = bw.tobytes()          # Pillow packs MSB first, 1 = white
    assert len(bits) == W * H // 8
    return b"TBI1" + bytes([W & 255, W >> 8, H & 255, H >> 8]) + bits
```

**Fit or fill?** A cover or an illustration is *fitted* and centred on white —
a cover with its title cropped off is worse than one with margins. A wallpaper
is *filled* (cropped), because a letterboxed wallpaper reads as a mistake.

---

## 3. `.tbk` — scans, manga, anything that is pages of pictures

A header and then fixed-size pages. No index: page N starts at
`dataOffset + N * pageBytes`, so the device seeks, reads and blits.

### Header, 64 bytes, little-endian throughout

| offset | type | field | value |
|---|---|---|---|
| 0 | char[4] | magic | `"TBK1"` |
| 4 | u16 | width | **480** |
| 6 | u16 | height | **800** |
| 8 | u8 | bpp | **1** or **2** |
| 9 | u8 | flags | bit 0 = right-to-left, bit 1 = a cover follows the header |
| 10 | u16 | reserved | 0 |
| 12 | u32 | pageCount | > 0 |
| 16 | u32 | pageBytes | `48000 * bpp` |
| 20 | u32 | dataOffset | `64`, or **`48064`** when bit 1 is set |
| 24 | char[40] | title | UTF-8, NUL-padded, not NUL-terminated if exactly 40 |

**Refused outright:** wrong magic; any dimensions other than 480×800; `bpp`
outside {1, 2}; a `pageBytes` that contradicts `bpp`; `pageCount` of zero.

**Two forgiving rules, and they matter:**

- `dataOffset` **below 64 is read as 64**, not refused. Files exist whose
  converter never filled the field in.
- If bit 1 claims a cover but `dataOffset < 48064`, **the cover flag is
  dropped**, because believing it would read page 0 out of the middle of the
  cover. **Set both fields or neither.**

### The embedded cover

48,000 bytes at offset 64 when flags bit 1 is set. Same layout as a 1-bit
page — **one bit even in a 2-bpp book**, because the loading screen and the
hub's strip both draw through the ordinary canvas.

Note this is the raw 48,000 bytes: **no `TBI1` header**. A `.tbi` file is this
block with an 8-byte header in front of it.

### Pages

- **1 bpp**: 48,000 bytes a page. MSB first, 1 = white. Blitted straight to the
  framebuffer, ~1.7 s a turn.
- **2 bpp (grey)**: 96,000 bytes a page, four pixels per byte, high bits first,
  **0 = black, 1 = dark grey, 2 = light grey, 3 = white**. Goes through the
  four-level waveform, ~3 s a turn, and cannot take a partial refresh.

Portrait 480×800, row-major, **not pre-rotated** — the board's own flips are
applied in the firmware.

Sizes to keep in mind: 200 pages at 1 bpp is 9.6 MB; at 2 bpp, 19.2 MB. A
437-page grey volume is 40 MB, which is fine on the card and is exactly the
kind of file that used to expose memory bugs — the reader now streams every
grey page in 40-row bands and never holds one whole.

### Right-to-left

Flags bit 0. The reader swaps its tap zones: in an RTL book the *left* third
turns forward. It changes nothing about the file layout.

### Conversion recipe

```
python tools/make_tbk.py volume1.cbz --rtl --trim --grey \
       --title "One Piece vol 1" --cover front.jpg --out E:/books/
```

`--trim` crops white margins per page before scaling; scans carry generous
borders and removing them puts 10–15 % more resolution into the dialogue.

---

## 4. `.epub` — real ebooks

The device parses the zip in place. `META-INF/container.xml` → the OPF →
the spine, and chapters are streamed and laid out live with the device fonts.
**Nothing is re-flowed on your machine**; the EPUB is read as an EPUB.

### What is read

| part | how it is used |
|---|---|
| `container.xml`, OPF | spine order, manifest, cover declaration |
| spine documents | the text, one chapter at a time |
| `<img>` / `<image>` | an illustration — **its own full page** (§5) |
| EPUB3 `nav` doc, or EPUB2 `.ncx` | the reader's contents list |
| cover image | the loading screen and the hub thumbnail |

### What is ignored, by design

- **All CSS.** Fonts, sizes, colours, margins, alignment: none of it.
- **SVG.** There is no vector renderer and there will not be one.
- Anything inside `<head>`, `<style>`, `<script>`, `<title>` or `<rp>`.
- Fixed-layout / pre-paginated EPUBs. They open, but they are read as text.
- Audio, video, JavaScript, fallbacks, media overlays.

### Text, and one hard compatibility rule

The reading position is stored **on the card in CrossPoint Reader's format**,
as a spine index plus a **visible-codepoint offset**: zero-based Unicode
codepoints of every character-data run inside `<body>`, whitespace text nodes
included, with the non-visible subtrees above excluded, XML and numeric
entities expanded, and unknown named entities passed through literally.

**Consequence for a converter: do not rewrite the text of a chapter.** Adding
or removing a single character inside `<body>` — an inserted space, a
normalised entity, a re-indented paragraph — moves every bookmark after it.
Adding *markup* is free; adding *characters* is not. This is why the artwork
route in §5 adds a zip entry rather than touching the XHTML.

### Contents

Read once when the panel's CONTENTS is opened, from whichever exists:

- **EPUB3**: the manifest item whose `properties` contains `nav` as a whole
  word, then every `<a href>` in it, in document order.
- **EPUB2**: the file named by `<spine toc="...">`, then each
  `<navLabel><text>` paired with the `<content src>` that follows it.

Both spellings are accepted and **nav wins** when a book carries both. Each
entry's href is resolved (fragments dropped, `%XX` decoded, `./` and `../`
folded) and matched to a spine index; **entries landing in a chapter already
listed are dropped**, because the reader jumps to chapters, not to fragments.
Titles are trimmed to one line and cut to 43 bytes. At most 64 entries.

A book with no usable contents falls back to naming each chapter by its own
first words.

### The cover

Found the two standard ways: EPUB2 `<meta name="cover" content="id">`, or an
EPUB3 manifest item whose `properties` contains `cover-image`. Baseline JPEG
and PNG decode at full resolution; **progressive JPEG decodes at one eighth**,
which is the next section and is the thing worth acting on.

It is decoded **once**, on the book's first open, and cached. It costs a second
or two, and the dithering is the device's own — see §6 for the better route.

### Progressive JPEG covers lose seven eighths of their detail

This is the single strongest reason to ship a sidecar, and it is invisible
until you look at the result.

The device's baseline-JPEG decoder (TJpgDec) **refuses progressive JPEGs**.
Rather than showing nothing, the firmware falls back to its own extractor,
which reads only the **DC coefficients** of the first scan — one value per
8 × 8 block. That is the image at **1/8 scale**, and the cover builder then
enlarges it to fill 480 × 800.

Measured on a real book (*Classroom of the Elite* vol 1, Seven Seas):

| | |
|---|---|
| cover in the EPUB | progressive JPEG, 1404 × 2000 |
| what the device decodes | **175 × 250** |
| what it then draws | that, enlarged **2.7×**, dithered |

Big shapes survive. Type does not: the author and illustrator credits under
the title are legible from a PC-made `.tbi` and illegible from the device's
own build of the same file.

**Commercial ebook covers are usually progressive** — it is what a web-oriented
export pipeline produces — so this is the common case, not the corner case. In
that same book, the cover *and* all ten interior illustrations are progressive;
the only baseline JPEG in the file is the publisher's logo. Assume the art you
care about is the art the device would see least of.

What a converter should do:

- **Detect it and act on it.** Walk the JPEG markers: `FFC0`/`FFC1` is
  baseline, **`FFC2` is progressive**. Skip `FFD8`, and skip `FFD0`–`FFD7`;
  every other marker carries a big-endian u16 length you can jump over.
- If the cover is progressive — or simply always — **write the `.cover.tbi`
  sidecar** (§6). It costs 48 KB beside a book that is already megabytes.
- The same applies to any **artwork** inside the book: a progressive insert
  with no `toybox/` counterpart would be decoded the same way. Pre-render it.

```python
def jpeg_is_progressive(data: bytes) -> bool:
    i = 2
    while i < len(data) - 1:
        if data[i] != 0xFF:
            i += 1
            continue
        m = data[i + 1]
        if m in (0xC0, 0xC1):
            return False
        if m == 0xC2:
            return True
        if m == 0xD8 or 0xD0 <= m <= 0xD7:
            i += 2
            continue
        i += 2 + ((data[i + 2] << 8) | data[i + 3])
    return False
```

---

## 5. Artwork inside an EPUB

Light novels carry character art and story plates, and the reader shows them.
**The desktop makes the picture; the device blits it.**

### What to add

For every image the device should display, add **one extra zip entry**, at the
original entry's path with a `toybox/` prefix and a `.tbi` extension:

```
OEBPS/Images/insert-01.jpg          <- the original, left exactly as it is
toybox/OEBPS/Images/insert-01.tbi   <- what the device draws
```

- The path is the **resolved zip entry name** of the original, not the `src`
  attribute. `<img src="../Images/x.jpg">` inside `OEBPS/Text/ch1.xhtml`
  resolves to `OEBPS/Images/x.jpg`, so the artwork entry is
  `toybox/OEBPS/Images/x.tbi`.
- Only the **last** extension is replaced: `a.b.jpg` → `toybox/a.b.tbi`.
- The file is a full `.tbi` **with** its 8-byte header — 48,008 bytes.
- **Store them uncompressed (zip method 0)** if your library allows it. They
  are already one bit per pixel and compress by a few percent, and a stored
  entry lets the device seek straight to the pixels instead of inflating 48 KB
  to find them. Deflated entries work; they are just slower.
- **Nothing else in the book changes.** The XHTML keeps its ordinary `<img>`,
  the original image stays where it is, and every other reader ignores an entry
  the manifest never mentions. **The book stays a valid EPUB.**

### How the device shows them

- **An illustration gets its own page** — the whole glass, with the text
  resuming on the next turn. So fit the artwork into 480×800 on white, exactly
  as for a cover.
- **Every `<img>` in the body gets a page**, including a 3 KB publisher logo.
  If a book has decorative images you would rather not see as full pages, the
  place to remove them is your app — the firmware cannot tell a plate from a
  rule.
- **Without a `toybox/` entry the page names the image** ("this book has no
  picture prepared for it" plus the filename). On-device decoding of the
  original is planned; when it lands, a `toybox/` entry will still win.
- Bookmarks are unaffected: an `<img>` carries no character data, so artwork
  does not move a single reading position.

---

## 6. Covers — three routes, one winner

In priority order, highest first:

**1. A sidecar beside the book — works for BOTH formats, and is the recommended route.**

```
/books/Uketsu/strange-houses.epub
/books/Uketsu/strange-houses.cover.tbi     <- same stem, ".cover.tbi"

/books/One Piece/vol01.tbk
/books/One Piece/vol01.cover.tbi
```

The stem is the path with its **last** extension removed. The file is an
ordinary `.tbi` (§2). Replace it and the device picks the change up on the
book's next open — it compares four 64-byte samples taken from the middle of
the picture, so there is no cache to clear.

**2. Inside the `.tbk`** — the embedded cover of §3. Travels with the file,
survives a rename, needs no second file. Ignored if a sidecar exists.

**3. Decoded from the book** — an EPUB's declared cover, or a `.tbk`'s page 0.
Page 0 is a fallback rather than a rule: a trimmed scan starts at the story.

**Why the sidecar wins:** your machine has the whole image, a real dithering
library, and no memory ceiling; the device has a streaming decoder and a band
of RAM. Line art especially suffers on-device — Floyd–Steinberg is a
photographic algorithm and turns a flat grey background into a field of worms.
A sidecar also makes the book open faster, because there is nothing to decode.

And for a **progressive** JPEG cover it is not a matter of taste: the device
sees that cover at 1/8 scale and enlarges it (§4). A 1404 × 2000 cover reaches
the panel from a 175 × 250 decode. Most commercial ebook covers are
progressive. **If your app writes one file beside a book, make it this one.**

### What the device does with a cover

| where | size | treatment |
|---|---|---|
| loading screen | 480×800 | drawn as-is, full size |
| hub "recently read" strip | 96×160 | averaged down, stretched 2–98 %, thresholded |
| lock screen (if set to COVER) | 480×800 | copied to internal flash on book open |

All built once, on the book's first open, then cached on the card at
`/.toybox/covers/<hash>.tbc`.

---

## 7. Choosing the treatment

The device applies **no** contrast stretch to a full-size picture and dithers
with Floyd–Steinberg when it has to do the work itself. On a desktop you can do
better, and the choice depends on the art:

| kind of art | scale | tone | dither |
|---|---|---|---|
| photographic cover, painted illustration | Lanczos, fit | none | Floyd–Steinberg |
| line art, ink, flat character art | Lanczos, fit | stretch 1–99 % | FS, or a plain threshold |
| screentoned manga panel | Lanczos, fit | mild stretch | FS — but check for moiré against the screentone |
| text-heavy plate (title pages, credits) | Lanczos, fit | stretch | **threshold**, never dither |

Two rules that are not negotiable, both learned on real covers:

- **Shrinking must average, never sample.** A dithered image carries its tone
  in pixel-level variation; point-sampling it produces noise.
- **Never dither twice.** If you dither to 1-bit and something later scales it,
  the result is mud. Scale first, dither last.

Show the result before you write it. The device cannot.

---

## 8. What the DEVICE writes — do not create these, do not clobber them

| path | what it is |
|---|---|
| `/.crosspoint/epub_<hash64>/progress.bin` | reading position, CrossPoint's format. `<hash64>` is FNV-1a **64** of the absolute path, decimal — the hash current CrossInk uses. A legacy directory named by 32-bit libstdc++ `std::hash` is read once for migration, never written |
| `/.crosspoint/epub_<hash64>/cover.bmp` | the finished cover, left where CrossInk looks for one: 480×800, **1-bit BMP**, 62-byte header, black-then-white palette, bottom-up, 48,062 bytes. Written only if absent; a cover.bmp found here (either hash dir) is **read** instead of decoding the EPUB's cover |
| `<book>.sdr/metadata.epub.lua` | a KOReader sidecar, written for cards carried to KOReader. One-way; never read back |
| `/.toybox/covers/<hash>.tbc` | the built 480×800 cover. `<hash>` is FNV-1a 32 of the absolute path |
| `/.toybox/marks/<hash>.tbm` | bookmarks, 774 bytes, magic `TBM2` |
| `/.toybox/` | the device's own folder — hidden from the shelf and the file manager |

Books are also listed from `/Read` and `/epub` — CrossInk's shelf roots —
beside `/books`, so a card set up for either firmware reads in both.

**Recipes** (nothing to convert): the device reads schema.org/Recipe JSON-LD
from `.json` files in `/recipes` — the `<script type="application/ld+json">`
block of a recipe page, saved verbatim, works as-is (`@graph`, HowToStep and
HowToSection forms, ISO-8601 durations, entities and `\u` escapes are all
handled on device). Keep files under 256 KB and names under 63 bytes.

**Do not write a cover into `/.toybox/covers/`.** The firmware decides whether
a book has a cover by looking at a thumbnail in its *internal flash*, which a
PC cannot write; it would see none, rebuild, and overwrite your file. Covers
from a PC go beside the book (§6) or inside the `.tbk`.

A converter may safely **delete** a `.tbc` or a `.tbm` belonging to a book it is
replacing; both are rebuilt or simply lost, and neither breaks anything.

---

## 9. Checklist before shipping a file

**`.tbk`**

- [ ] magic `TBK1`, 480×800, `bpp` ∈ {1, 2}, `pageCount` > 0
- [ ] `pageBytes == 48000 * bpp`
- [ ] `dataOffset` is 64, or 48064 **and** flags bit 1 set
- [ ] file size == `dataOffset + pageCount * pageBytes` exactly
- [ ] title fits 40 bytes of UTF-8 **without splitting a character**
- [ ] pages are not pre-rotated, MSB first, 1 = white (1 bpp) / 3 = white (2 bpp)

**`.epub`**

- [ ] the original opens in another reader unchanged (nothing in `<body>` edited)
- [ ] every `toybox/…` entry is exactly 48,008 bytes and starts `TBI1` 480×800
- [ ] artwork paths are the **resolved** entry names, prefixed and re-extensioned
- [ ] `mimetype` is still the first entry and still stored
- [ ] `toybox/` entries are stored (method 0) where possible

**Either**

- [ ] full path on the card ≤ 127 bytes
- [ ] sidecar `.cover.tbi` (if any) shares the book's stem exactly and is 48,008 bytes
- [ ] any EPUB whose cover is a **progressive** JPEG ships a `.cover.tbi` (§4)

### Testing without a device

```
cd test/host
g++ -std=gnu++17 -O2 -w -DTOYBOX_HOST -I . -I mock -I ../../src \
  -I ../../toybox-core/src -I ../../lib/miniz/src \
  epub_cli.cpp ../../toybox-core/src/epubcore.cpp \
  ../../lib/miniz/src/toybox_miniz_impl.c -o epub_cli
./epub_cli "your book.epub"
```

That runs the firmware's own EPUB core against the file and prints the spine,
the contents it found, every image, and whether each one has artwork the device
can draw:

```
open ok: 20 spine chapters, 0.2 ms  (20236379 bytes)
contents: 19 entries
   ch 06  Chapter 1: The Structure of Japanese Societ
       image OEBPS/Images/insert-01.jpg          prepared
       image OEBPS/Images/logo.jpg               -- plate
ch 07:  10446 words 408 paras  2 pics (2 prepared)  offsets ok
```

`prepared` means the device will draw your picture. `-- plate` means it will
show the filename instead.
