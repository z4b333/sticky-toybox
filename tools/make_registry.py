#!/usr/bin/env python3
"""Builds the Seeed Sticky Playground registry entry for this firmware.

Seeed's registry -- github.com/Seeed-Projects/reterminal-sticky-playground-registry
-- is the contribution layer behind the Sticky Playground catalogue. A merged
entry becomes a card on Seeed's site with a browser flashing page beside it,
which is the same job docs/index.html does here, done by them.

There are two ways in. The SOURCE path hands over a buildable tree and lets
their GitHub Actions compile it, and it builds with ESP-IDF and CMake only:
"Projects that need another build system should open an issue first so the
maintainers can add a reproducible CI build adapter." Toybox is PlatformIO and
Arduino, so that path needs their agreement before any pull request.

The FIRMWARE-ONLY path takes the tested binaries with a manifest that records
every offset, byte size and SHA-256, plus an upstream source URL and licence.
That is what this writes, from the same four parts the web installer serves --
so what Seeed would flash is byte-identical to what this project's own page
flashes, which is the only definition of "compatible" worth having.

Run it after tools/make_image.sh:

    python3 tools/make_registry.py [--preview path/to/photo.jpg]

It writes dist/registry/toybox/, which is copied into a clone of the registry
as integrations/toybox/ and validated there with `npm run validate`.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'dist', 'registry', 'toybox')

# The registry flashes one package at fixed offsets, so the four parts the
# installer page writes individually are the four parts named here. Same
# offsets, same files, same order -- see tools/make_image.sh and docs/index.html.
#
# Names are theirs, not ours: the validator wants a bare .bin filename beside
# the manifest, and a reviewer reading someone else's package should not have
# to learn a private naming scheme to see that it is an ordinary ESP32-S3
# layout.
PARTS = [
    ('docs/firmware/toybox-boot.bin', 'bootloader.bin', 0x0),
    ('docs/firmware/toybox-parts.bin', 'partitions.bin', 0x8000),
    ('docs/firmware/toybox-ota0.bin', 'boot_app0.bin', 0xE000),
    ('docs/firmware/toybox-app.bin', 'toybox.bin', 0x10000),
]

SOURCE_URL = 'https://github.com/z4b333/sticky-toybox'


def digest(path):
    data = open(path, 'rb').read()
    return len(data), hashlib.sha256(data).hexdigest(), hashlib.md5(data).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    # Their rule for a community card: "a real Sticky screenshot or photo".
    # This project's own screens are rendered by the test harness on a PC --
    # the firmware's own drawing code, but not a photograph of the device --
    # so a real photo has to be passed in, and the script says so loudly when
    # one is not.
    ap.add_argument('--preview', help='a photograph of the firmware running on a Sticky')
    args = ap.parse_args()

    vpath = os.path.join(ROOT, 'docs/firmware/version.json')
    if not os.path.exists(vpath):
        sys.exit('no docs/firmware/version.json -- run tools/make_image.sh first')
    info = json.load(open(vpath))
    # Their version strings have no leading v, and the directory is named after
    # this exactly: firmware/<version>/manifest.json is checked by string.
    version = info['version'].lstrip('v')

    fw = os.path.join(OUT, 'firmware', version)
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(fw)
    os.makedirs(os.path.join(OUT, 'assets'))

    parts = []
    for src, name, offset in PARTS:
        s = os.path.join(ROOT, src)
        if not os.path.exists(s):
            sys.exit('missing %s -- run tools/make_image.sh first' % src)
        shutil.copy(s, os.path.join(fw, name))
        size, sha, md5 = digest(s)
        parts.append({'path': name, 'offset': offset, 'size': size,
                      'sha256': sha, 'md5': md5})

    # Offsets must not overlap, and the validator checks it. Cheaper to fail
    # here, where the message can say which two.
    ordered = sorted(parts, key=lambda p: p['offset'])
    for a, b in zip(ordered, ordered[1:]):
        if a['offset'] + a['size'] > b['offset']:
            sys.exit('%s runs into %s' % (a['path'], b['path']))

    manifest = {
        'name': 'Toybox',
        'version': version,
        # The module carries octal PSRAM on the same lines QIO would want, so
        # dio is not a preference here -- a QIO header makes the ROM read the
        # bootloader as garbage. See platformio.ini.
        'flashSize': '16MB',
        'flashMode': 'dio',
        'flashFreq': '80m',
        'baudRate': 921600,
        # MUST stay true, and for a reason worth repeating outside this repo:
        # Toybox carries no Improv serial, so esp-web-tools cannot recognise
        # the device as already running this firmware. With prompt_erase false
        # it answers that by silently erasing the whole chip before writing.
        # Seen on real hardware, confirmed from a flash dump.
        'new_install_prompt_erase': True,
        'builds': [{'chipFamily': 'ESP32-S3', 'parts': parts}],
    }
    json.dump(manifest, open(os.path.join(fw, 'manifest.json'), 'w'), indent=2)

    integration = {
        'schemaVersion': 1,
        'id': 'toybox',
        'name': 'Toybox',
        'group': 'community',
        'catalogSection': 'community',
        'mode': 'flash',
        'status': 'beta',
        'summary': 'An EPUB reader, notes you pin to the e-paper, six games and everyday tools.',
        'description': (
            'Toybox turns the reTerminal Sticky into a small e-paper playground: an EPUB '
            'reader that keeps your place on the card, notes you write from your phone and '
            'pin to the screen so they survive the power going off, six games chosen '
            'because they suit a display that redraws in a fifth of a second, and the '
            'everyday tools -- coin, dice, timer, picker, flashcards, recipes. Reading '
            'positions are stored in CrossPoint’s own format, so a card can move '
            'between the two firmwares. English, Thai, Chinese, Japanese, Korean and '
            'Vietnamese.'
        ),
        'author': {'name': 'z4b333', 'url': 'https://github.com/z4b333'},
        'origin': {'name': 'sticky-toybox', 'url': SOURCE_URL},
        'source': {'url': SOURCE_URL, 'license': 'MIT'},
        'support': {'url': SOURCE_URL + '/issues'},
        'documentationUrl': 'https://z4b333.github.io/sticky-toybox/',
        'compatibility': {
            'devices': ['reterminal-sticky'],
            'notes': ('Tested on reTerminal Sticky production hardware. Needs a FAT32 '
                      'microSD card for books, pictures and recipes; notes, settings and '
                      'reading positions work without one.'),
        },
        'assets': {
            'preview': 'assets/preview.png',
            'previewAlt': 'Toybox on a reTerminal Sticky, showing the games drawer',
        },
        'tags': ['ereader', 'notes', 'games', 'epaper', 'offline'],
        'flash': {
            'versions': [{
                'version': version,
                'channel': 'beta',
                'manifestPath': 'firmware/%s/manifest.json' % version,
            }],
            'notes': [
                {'title': 'Device connection',
                 'description': 'Connect reTerminal Sticky with a USB-C data cable and use '
                                'desktop Chrome or Edge. A charging cable will not do.'},
                {'title': 'Installation',
                 'description': 'The package writes the bootloader, partition table, OTA '
                                'data block and application. Leave the erase-device box '
                                'unchecked: unchecked, notes, settings and saved reading '
                                'positions survive a reinstall.'},
                {'title': 'First boot',
                 'description': 'Hold either side button while it powers on to reach the '
                                'service screen, which reports the buses and can correct '
                                'the screen or touch orientation.'},
                {'title': 'Storage',
                 'description': 'On a FAT32 card: books in /books, pictures in '
                                '/wallpapers, recipes in /recipes, and notes and '
                                'flashcard decks in /notes and /decks.'},
            ],
        },
    }
    json.dump(integration, open(os.path.join(OUT, 'integration.json'), 'w'), indent=2)

    preview = os.path.join(OUT, 'assets', 'preview.png')
    if args.preview:
        shutil.copy(args.preview, preview)
        note = 'a photograph of the device'
    else:
        shutil.copy(os.path.join(ROOT, 'docs/shots/hub.png'), preview)
        note = 'A HARNESS RENDER, not a photograph -- replace before submitting'
        print('warning: no --preview given, so assets/preview.png is a harness render.\n'
              '         Seeed asks a community entry for a real screenshot or photo.',
              file=sys.stderr)

    commit = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    open(os.path.join(OUT, 'README.md'), 'w').write(README % {
        'version': version, 'date': info.get('date', ''), 'commit': commit,
        'app': parts[3]['size'], 'sha': parts[3]['sha256'], 'preview': note,
    })

    print('wrote %s' % os.path.relpath(OUT, ROOT))
    print('  version %s, %d parts, %d bytes of application'
          % (version, len(parts), parts[3]['size']))


README = """# Toybox

An e-paper playground for the reTerminal Sticky: an EPUB reader, notes you write
from your phone and pin to the screen, six games, and the everyday tools.

## What it does

- **Reader.** EPUBs off the card, with the book's own contents list, adjustable
  type, three line spacings and three screen rotations. Reading positions are
  written in CrossPoint's format as well as its own, so a card carries its
  places between the two firmwares.
- **Comics.** A `.tbk` page format prepared on a PC by
  [Toybox Slicer](https://github.com/z4b333/Toybox-slicer), which re-cuts a
  webtoon strip at blank gutters so a page never breaks through a face.
- **Notes.** The device serves a small editor to a phone over its own access
  point, or reads a Markdown file off the card. A pinned note stays on the panel
  with the power off.
- **Games.** Wordle, Sudoku, Nonogram, 2048, Ships and XO. Boards and streaks
  are saved as you play.
- **Tools.** Coin, dice, timer and stopwatch, random number, card draw, a picker
  that chooses from a list, flashcards, and recipes read from `/recipes`.
- **Languages.** English, Thai, Chinese, Japanese, Korean and Vietnamese, with
  optional font packs for full CJK coverage installed separately from the
  project's own page.

## Controls

Touch throughout. Two side buttons page lists, turn book pages and step through
recipe steps; the OK button opens the options panel in the readers and the
recipe screens. Holding either side button at power-on reaches the service
screen.

## Package origin

Built from [sticky-toybox](https://github.com/z4b333/sticky-toybox) at
`%(commit)s`, tagged `v%(version)s`, dated %(date)s. The four binaries here are
the same files the project's own web installer serves at
<https://z4b333.github.io/sticky-toybox/> -- produced by `tools/make_image.sh`
and packaged for this registry by `tools/make_registry.py`, so the two installers
write identical bytes.

Application: %(app)d bytes, SHA-256 `%(sha)s`.

Built with PlatformIO and the Arduino ESP32 framework rather than ESP-IDF, which
is why this is a firmware-only contribution rather than a source build.

## Storage

A FAT32 microSD card holds books (`/books`), pictures (`/wallpapers`), recipes
(`/recipes`), and optionally notes (`/notes`, as `.md` or `.txt`) and flashcard
decks (`/decks`, as `.tsv`, `.csv` or `.txt`) to import. Notes, settings, saved
games and reading positions live in the device's own flash and need no card.

## Physical device test

Preview image: %(preview)s.

Tested on reTerminal Sticky production hardware:

- [ ] Power-on and first boot
- [ ] Service screen: buses reported, orientation correct
- [ ] Hub, drawers and every app opens
- [ ] Touch and both side buttons
- [ ] Reader: opens a book, turns pages, keeps its place across a reboot
- [ ] Notes: written from a phone, pinned, still on the panel with power off
- [ ] Reboot with saved state intact
"""


if __name__ == '__main__':
    main()
