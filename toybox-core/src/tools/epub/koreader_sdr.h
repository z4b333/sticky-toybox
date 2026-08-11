// KOReader's own sidecar, written beside the book so a card carried to a
// KOReader device opens near the right place.
//
// KOReader keeps a per-book settings file at <book without suffix>.sdr/
// metadata.<suffix>.lua -- a Lua table it loads with dofile(). It restores a
// position from "last_xpointer" first and falls back to "last_percent"; the
// third field it reads, "percent_finished", is display only (the footer and
// the library's progress bar).
//
// We cannot write an xpointer. An xpointer is a path into crengine's DOM
// ("/body/DocFragment[7]/body/div/p[12]/text().0") and this reader has no DOM
// -- it streams words and counts codepoints. So we write the percentage, and
// the handoff is deliberately one-way: KOReader takes our percentage once,
// then saves an xpointer and DELETES last_percent, which is exactly right.
// From then on KOReader has a better position than we could have given it,
// and Toybox goes on reading its own CrossPoint sidecar, which is exact.
//
// So: this file is how a book leaves for KOReader, not a channel between the
// two. Nothing here is ever read back.
#pragma once
#include <stdio.h>
#include <string.h>

namespace ksdr {

// "/books/Foo.epub" -> "/books/Foo.sdr/metadata.epub.lua".
//
// The suffix is carried through exactly as it appears, because KOReader
// derives its filename from the same string on the same card: a book stored
// as "Foo.EPUB" gets "metadata.EPUB.lua" from both of us, and lower-casing it
// here would be the one thing that made them disagree.
inline bool metaPath(const char* bookPath, char* out, int cap) {
  if (!bookPath || !*bookPath) return false;
  const char* slash = strrchr(bookPath, '/');
  const char* base = slash ? slash + 1 : bookPath;
  const char* dot = strrchr(base, '.');
  if (!dot || !dot[1]) return false;  // no suffix: no name KOReader would look for
  const int stem = (int)(dot - bookPath);
  const int n = snprintf(out, (size_t)cap, "%.*s.sdr/metadata%s.lua", stem, bookPath, dot);
  return n > 0 && n < cap;
}

// The table itself. Deliberately three fields and no more.
//
// Everything else KOReader stores describes a rendering this device did not
// do -- fonts, margins, page count, the css it used -- and a number invented
// for those keys would be a lie that KOReader believes. doc_pages is the
// tempting one and the worst: ours counts pages of a 480x800 panel at our
// font, and KOReader would show it in the footer beside its own, different,
// page numbers. Omitted fields KOReader simply fills in for itself.
struct State {
  double percent = 0;  // 0..1 through the whole book
};

// Renders the Lua and returns its length, or 0 if it would not fit.
//
// "%.6f" is six digits of a fraction of a book -- a few words. It is also
// plain decimal with no exponent, which matters: Lua would read 3.5e-07 back
// correctly, but a sidecar is the kind of file people open in a text editor
// when something has gone wrong, and it should read as a number.
inline int render(const State& s, char* out, int cap) {
  double p = s.percent;
  if (!(p >= 0)) p = 0;  // also catches NaN, which would render as "nan" and fail dofile
  if (p > 1) p = 1;
  const int n = snprintf(out, (size_t)cap,
                         "-- KOReader sidecar written by Toybox\n"
                         "return {\n"
                         "    [\"last_percent\"] = %.6f,\n"
                         "    [\"percent_finished\"] = %.6f,\n"
                         "    [\"summary\"] = {\n"
                         "        [\"status\"] = \"reading\",\n"
                         "    },\n"
                         "}\n",
                         p, p);
  return (n > 0 && n < cap) ? n : 0;
}

}  // namespace ksdr
