#!/usr/bin/env python3
"""Writes test/host/fixtures/kosync.epub: the markup KOReader positions have
to survive -- nested divs and sections, inline spans, entities, comments, a
script inside <body>, bare anchors, a namespaced tag, and runs of whitespace
between blocks. Deterministic (fixed timestamps) so the file never churns."""
import sys, zipfile

CH1 = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>One</title><style>p{margin:0}</style></head>
<body>
  <h1>Chapter One</h1>
  <p>It was a <em>bright</em> cold day in April, and the clocks were striking thirteen.</p>
  <div class="a">
    <div class="b">
      <p>Fish &amp; chips&#8212;and <span>a <b>very</b> long</span> walk home.</p>
      <!-- a comment the count must not see -->
      <p><a id="anchor1"/>Anchored text starts here, after an empty anchor.</p>
    </div>
    <section><p>Inside a section: one.</p><p>Inside a section: two.</p></section>
  </div>
  <script>var x = "not text";</script>
  <p>Last line<br/>after a break, then the end.</p>
</body></html>
"""

CH2 = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Two</title></head>
<body>
<epub:switch><p>Switched paragraph.</p></epub:switch>
<p>Second chapter, first paragraph with &#xe9;l&#xe8;ve and &nbsp;space.</p>
<p>Second paragraph <i>italic <b>bold italic</b></i> tail.</p>
<ul><li>one</li><li>two</li><li>three <span>deep</span></li></ul>
</body></html>
"""

OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Kosync Fixture</dc:title>
<dc:identifier id="id">kosync-fixture</dc:identifier><dc:language>en</dc:language></metadata>
<manifest>
<item id="c1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>
<item id="c2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
</manifest>
<spine><itemref idref="c1"/><itemref idref="c2"/></spine>
</package>
"""

CONTAINER = """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""

def add(z, name, data, compress=True):
    zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    zi.compress_type = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    z.writestr(zi, data)

out = sys.argv[1] if len(sys.argv) > 1 else "test/host/fixtures/kosync.epub"
with zipfile.ZipFile(out, "w") as z:
    add(z, "mimetype", "application/epub+zip", compress=False)
    add(z, "META-INF/container.xml", CONTAINER)
    add(z, "OEBPS/content.opf", OPF)
    add(z, "OEBPS/text/ch1.xhtml", CH1)
    add(z, "OEBPS/text/ch2.xhtml", CH2)
print("wrote", out)
