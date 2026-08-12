#!/usr/bin/env python3
"""Put pre-rendered artwork into an EPUB, for the Toybox reader.

The reader shows an illustration as a whole page, but it does not decode the
book's JPEGs: it looks for a picture already in its own format, at the image's
path with a "toybox/" prefix and a ".tbi" extension.

    OEBPS/Images/insert-01.jpg          <- the original, untouched
    toybox/OEBPS/Images/insert-01.tbi   <- what the device draws

Nothing else in the book changes. The XHTML keeps its ordinary <img>, the
original image stays where it is, and every other reader ignores the extra
entry -- a zip may carry files the OPF manifest never mentions. The book stays
a valid EPUB.

Why here and not on the device: a desktop has the whole image, a real dithering
library and no memory ceiling, where the device has a streaming decoder and a
band of RAM. It also opens faster, because there is nothing to decode.

Usage:
    python add_epub_art.py book.epub                     # -> book.toybox.epub
    python add_epub_art.py book.epub --dither none       # threshold, for line art
    python add_epub_art.py book.epub --skip logo.jpg     # leave one unprepared
    python add_epub_art.py book.epub --min-px 40000      # ignore small decorations
    python add_epub_art.py book.epub --cover-sidecar     # also write <book>.cover.tbi

The .tbi entries are STORED, not deflated: they are already one bit a pixel and
compress by a few percent, and a stored entry means the device seeks straight
to the pixels instead of inflating 48 KB to find them.
"""
import argparse
import io
import os
import posixpath
import re
import sys
import zipfile

try:
    from PIL import Image, ImageOps
except ImportError:
    sys.exit("needs Pillow:  pip install Pillow")

W, H = 480, 800
MAGIC = b"TBI1"
DOC_EXT = (".xhtml", ".html", ".htm")
IMG_EXT = (".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp")


def resolve(base_dir: str, href: str) -> str:
    """Where an <img src> points, the way the firmware resolves it."""
    href = href.split("#", 1)[0].split("?", 1)[0]
    if href.startswith("/"):
        href = href[1:]
    return posixpath.normpath(posixpath.join(base_dir, href))


def find_images(z: zipfile.ZipFile) -> list:
    """Every image referenced by an <img>/<image>, in document order, deduped."""
    seen, out = set(), []
    names = set(z.namelist())
    for doc in z.namelist():
        if not doc.lower().endswith(DOC_EXT):
            continue
        try:
            text = z.read(doc).decode("utf-8", "replace")
        except Exception:
            continue
        base = posixpath.dirname(doc)
        for href in re.findall(r"<(?:img|image)[^>]*?(?:src|xlink:href|href)=\"([^\"]+)\"",
                               text, re.I):
            path = resolve(base, href)
            if path in names and path not in seen and path.lower().endswith(IMG_EXT):
                seen.add(path)
                out.append(path)
    return out


def to_tbi(data: bytes, dither: str, stretch: bool) -> bytes:
    """One picture, 480x800, aspect-fitted on white, packed 1 bpp MSB first."""
    im = Image.open(io.BytesIO(data)).convert("L")
    s = min(W / im.width, H / im.height)
    nw, nh = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = im.resize((nw, nh), Image.LANCZOS)
    if stretch:
        # Line art and screentone usually gain from this; a photograph does not.
        im = ImageOps.autocontrast(im, cutoff=1)
    canvas = Image.new("L", (W, H), 255)
    canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))
    bw = canvas.convert("1") if dither == "fs" else canvas.point(
        lambda v: 255 if v >= 128 else 0).convert("1")
    bits = bw.tobytes()  # Pillow packs MSB first, 1 = white -- the same convention
    assert len(bits) == W * H // 8, len(bits)
    return MAGIC + bytes([W & 255, W >> 8, H & 255, H >> 8]) + bits


def main() -> None:
    ap = argparse.ArgumentParser(description="Add Toybox artwork to an EPUB.")
    ap.add_argument("book")
    ap.add_argument("--out", help="output path (default: <book>.toybox.epub)")
    ap.add_argument("--dither", choices=["fs", "none"], default="fs",
                    help="fs = Floyd-Steinberg (photographic art); none = threshold")
    ap.add_argument("--stretch", action="store_true",
                    help="contrast-stretch first: helps line art, flattens photographs")
    ap.add_argument("--skip", action="append", default=[],
                    help="substring of an entry to leave unprepared (repeatable)")
    ap.add_argument("--min-px", type=int, default=0,
                    help="ignore images smaller than this many pixels (logos, rules)")
    ap.add_argument("--cover-sidecar", action="store_true",
                    help="also write <book>.cover.tbi beside the output")
    args = ap.parse_args()

    out = args.out or re.sub(r"\.epub$", "", args.book, flags=re.I) + ".toybox.epub"
    src = zipfile.ZipFile(args.book)
    images = find_images(src)
    if not images:
        sys.exit("no <img> in this book -- nothing to prepare")

    prepared, skipped = {}, []
    for path in images:
        if any(s.lower() in path.lower() for s in args.skip):
            skipped.append((path, "asked to skip"))
            continue
        raw = src.read(path)
        if args.min_px:
            with Image.open(io.BytesIO(raw)) as probe:
                if probe.width * probe.height < args.min_px:
                    skipped.append((path, f"{probe.width}x{probe.height}, too small"))
                    continue
        prepared["toybox/" + posixpath.splitext(path)[0] + ".tbi"] = to_tbi(
            raw, args.dither, args.stretch)
        print(f"  prepared  {path}")
    for path, why in skipped:
        print(f"  left      {path}  ({why})")

    # mimetype must stay first and stored, or the file stops being an EPUB.
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            data = src.read(item.filename)
            # A fresh ZipInfo, not the source's: writestr() rewrites the one it
            # is handed (header offset and all), and the source's own directory
            # is made of those objects -- reuse them and the input zip becomes
            # unreadable halfway through the copy.
            info = zipfile.ZipInfo(item.filename, date_time=item.date_time)
            info.external_attr = item.external_attr
            info.internal_attr = item.internal_attr
            info.create_system = item.create_system
            stored = item.filename == "mimetype" or item.compress_type == zipfile.ZIP_STORED
            dst.writestr(info, data,
                         compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
        for name, data in prepared.items():
            dst.writestr(name, data, compress_type=zipfile.ZIP_STORED)

    print(f"\n{out}: {len(prepared)} pictures added, {len(skipped)} left to the plate")

    if args.cover_sidecar:
        # The book's own cover, as a file beside it: this beats anything the
        # device can decode, for both .epub and .tbk.
        cover = next((p for p in src.namelist()
                      if p.lower().endswith(IMG_EXT) and "cover" in p.lower()), None)
        if not cover:
            print("no cover image found in the zip -- sidecar skipped")
            return
        side = re.sub(r"\.epub$", "", out, flags=re.I) + ".cover.tbi"
        with open(side, "wb") as f:
            f.write(to_tbi(src.read(cover), "fs", False))
        print(f"{side}: from {cover}")


if __name__ == "__main__":
    main()
