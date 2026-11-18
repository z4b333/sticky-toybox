#!/usr/bin/env python3
"""An independent reading of the xpointer rules, for checking the firmware's.

For every drawn character of every chapter of an EPUB, prints
  <spine> <visible offset> <xpointer>
using Python's own HTML parser, not the firmware's tokenizer. The rules are
CrossPoint's (which Readest and KOReader read back correctly):
  - offsets count every codepoint of body text outside head/style/script/
    title/rp, whitespace included;
  - an element's index is its 1-based position among same-named siblings,
    always written;
  - text()[k] counts the runs of text directly inside an element, a run
    starting at the first character after any tag;
  - whitespace is never an anchor.
Comments and CDATA do not split a run (the firmware's tokenizer never sees
them as tags)."""
import sys, zipfile, re
from html.parser import HTMLParser

NONVIS = {"head", "style", "script", "title", "rp"}

def local(name):
    return name.split(":")[-1].lower()

class Ref(HTMLParser):
    def __init__(self, spine):
        super().__init__(convert_charrefs=True)
        self.spine = spine
        self.inbody = False
        self.nv = 0
        self.stack = []  # [name, idx, textN, counts]
        self.pending = True
        self.count = 0
        self.start = 0
        self.rows = []

    def handle_starttag(self, tag, attrs):
        tag = local(tag)
        if tag == "body":
            self.inbody = True
            self.stack = [["body", 1, 0, {}]]
            self.pending = True
            return
        if not self.inbody:
            return
        self.pending = True
        parent = self.stack[-1]
        parent[3][tag] = parent[3].get(tag, 0) + 1
        self.stack.append([tag, parent[3][tag], 0, {}])
        if self.nv or tag in NONVIS:
            self.nv += 1

    def handle_endtag(self, tag):
        tag = local(tag)
        if tag == "body":
            self.inbody = False
            return
        if not self.inbody:
            return
        self.pending = True
        for d in range(len(self.stack) - 1, 0, -1):
            if self.stack[d][0] == tag:
                del self.stack[d:]
                break
        if self.nv:
            self.nv -= 1

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if local(tag) != "body":
            self.handle_endtag(tag)

    def handle_data(self, data):
        if not self.inbody or self.nv:
            return
        for ch in data:
            if self.pending:
                self.stack[-1][2] += 1
                self.start = self.count
                self.pending = False
            if not ch.isspace() or ch == "\xa0":
                path = "/body/DocFragment[%d]/body" % (self.spine + 1)
                for s in self.stack[1:]:
                    path += "/%s[%d]" % (s[0], s[1])
                path += "/text()[%d].%d" % (self.stack[-1][2], self.count - self.start)
                self.rows.append((self.spine, self.count, path))
            self.count += 1

def spine_files(z):
    cont = z.read("META-INF/container.xml").decode()
    opf = re.search(r'full-path="([^"]+)"', cont).group(1)
    base = opf.rsplit("/", 1)[0] + "/" if "/" in opf else ""
    o = z.read(opf).decode()
    items = {}
    for tag in re.findall(r'<item\b[^>]*>', o):
        i = re.search(r'\bid="([^"]+)"', tag)
        h = re.search(r'\bhref="([^"]+)"', tag)
        if i and h:
            items[i.group(1)] = h.group(1)
    return [base + items[i] for i in re.findall(r'<itemref[^>]*idref="([^"]+)"', o)]

z = zipfile.ZipFile(sys.argv[1])
for i, f in enumerate(spine_files(z)):
    r = Ref(i)
    r.feed(z.read(f).decode("utf-8"))
    for row in r.rows:
        print("%d %d %s" % row)
