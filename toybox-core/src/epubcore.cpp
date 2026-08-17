// The EPUB core. See epubcore.h for what this is and why it is shaped by
// CrossPoint compatibility. Nothing in this file draws or touches hardware:
// bytes come in through epubc::IO, words come out of Book::next().
#include "tools/epub/epubcore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools/epub/epub_entities.h"
#include <toybox_miniz.h>

namespace epubc {
namespace {

uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t fnv(const char* s, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool wsByte(int b) {
  return b == ' ' || b == '\t' || b == '\r' || b == '\n' || b == '\f' || b == '\v';
}

bool nameIs(const char* name, const char* want) {
  // Case-insensitive, and namespace prefixes ("opf:itemref") are stripped.
  const char* colon = strchr(name, ':');
  if (colon) name = colon + 1;
  for (; *name && *want; name++, want++)
    if (lower(*name) != *want) return false;
  return *name == 0 && *want == 0;
}

// The tags whose text is invisible -- CrossPoint's list, verbatim, because the
// offset count depends on it (VisibleTextUtils::isNonVisibleElement).
bool isNonVisible(const char* name) {
  return nameIs(name, "head") || nameIs(name, "style") || nameIs(name, "script") ||
         nameIs(name, "title") || nameIs(name, "rp");
}

// The inline tags this reader honours. Bold and italic, and nothing else:
// everything further that CSS can say about a span is a rabbit hole with no
// floor.
bool isBoldTag(const char* name) { return nameIs(name, "b") || nameIs(name, "strong"); }

// <cite> joins <i> and <em> because that is what a book uses it for -- the
// name of another book, set in italic by every stylesheet that bothers.
bool isItalTag(const char* name) {
  return nameIs(name, "i") || nameIs(name, "em") || nameIs(name, "cite");
}

// h1..h6, as a level. Headings are block tags too (below), so a heading has
// its own paragraph and can be laid out at its own size without splitting a
// line between two faces.
int headingLevel(const char* name) {
  if (!name || (name[0] != 'h' && name[0] != 'H')) return 0;
  if (name[1] < '1' || name[1] > '6' || name[2] != 0) return 0;
  return name[1] - '0';
}

// Tags that break a paragraph for display. This list is Toybox's own -- it
// affects how text wraps, never how offsets count, so it is free to differ
// from CrossPoint's block model.
bool isBlockTag(const char* name) {
  static const char* const B[] = {
      "p",       "div",     "h1",    "h2",     "h3",     "h4",         "h5",
      "h6",      "li",      "ul",    "ol",     "dl",     "dt",         "dd",
      "tr",      "td",      "th",    "table",  "br",     "hr",         "blockquote",
      "pre",     "section", "article", "aside", "header", "footer",    "figure",
      "figcaption", "nav",  "center"};
  for (const char* b : B)
    if (nameIs(name, b)) return true;
  return false;
}

// Extracts the value of `key` from a raw attribute region (a='x' b="y").
bool attrValue(const char* attrs, const char* key, char* out, int cap) {
  const size_t klen = strlen(key);
  const char* p = attrs;
  while (*p) {
    while (*p && wsByte((uint8_t)*p)) p++;
    if (!*p) break;
    const char* n0 = p;
    while (*p && *p != '=' && !wsByte((uint8_t)*p)) p++;
    const size_t nlen = (size_t)(p - n0);
    while (*p && wsByte((uint8_t)*p)) p++;
    if (*p != '=') {
      if (nlen == 0 && *p) p++;  // stray character; keep moving
      continue;                  // valueless attribute
    }
    p++;
    while (*p && wsByte((uint8_t)*p)) p++;
    char q = 0;
    if (*p == '"' || *p == '\'') q = *p++;
    const char* v0 = p;
    while (*p && (q ? *p != q : !wsByte((uint8_t)*p))) p++;
    const size_t vlen = (size_t)(p - v0);
    if (q && *p == q) p++;
    // name match: case-insensitive, namespace prefix stripped
    const char* nn = n0;
    size_t reallen = nlen;
    const char* colon = (const char*)memchr(n0, ':', nlen);
    if (colon) {
      nn = colon + 1;
      reallen = nlen - (size_t)(nn - n0);
    }
    if (reallen == klen) {
      bool eq = true;
      for (size_t i = 0; i < klen; i++)
        if (lower(nn[i]) != key[i]) {
          eq = false;
          break;
        }
      if (eq) {
        const size_t c = vlen < (size_t)(cap - 1) ? vlen : (size_t)(cap - 1);
        memcpy(out, v0, c);
        out[c] = 0;
        return true;
      }
    }
  }
  return false;
}

// Resolves `href` against the OPF's directory into a zip entry name: strips
// the fragment, URL-decodes %XX, and folds "./" and "../". Zip names carry no
// leading slash.
void resolveHref(const char* dir, const char* href, char* out, int cap) {
  char joined[256];
  if (href[0] == '/') href++;
  snprintf(joined, sizeof(joined), "%s%s", dir, href);
  char decoded[256];
  int d = 0;
  for (const char* p = joined; *p && d < (int)sizeof(decoded) - 1; p++) {
    if (*p == '#') break;
    if (*p == '%' && p[1] && p[2]) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hex(p[1]), lo = hex(p[2]);
      if (hi >= 0 && lo >= 0) {
        decoded[d++] = (char)((hi << 4) | lo);
        p += 2;
        continue;
      }
    }
    decoded[d++] = *p;
  }
  decoded[d] = 0;
  char* segs[24];
  int n = 0;
  for (char* p = strtok(decoded, "/"); p && n < 24; p = strtok(nullptr, "/")) {
    if (strcmp(p, ".") == 0) continue;
    if (strcmp(p, "..") == 0) {
      if (n > 0) n--;
      continue;
    }
    segs[n++] = p;
  }
  int o = 0;
  for (int i = 0; i < n; i++) {
    const int l = (int)strlen(segs[i]);
    if (o + l + 2 >= cap) break;
    if (o) out[o++] = '/';
    memcpy(out + o, segs[i], (size_t)l);
    o += l;
  }
  out[o] = 0;
}

int utf8Encode(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

constexpr uint32_t WIN_SIZE = 32768;  // tinfl's dictionary; power of two

// The window is STATIC, not heap, and this is the third time this device has
// taught the same lesson.
//
// DEFLATE's dictionary is 32 KB by definition -- it cannot be made smaller,
// only found. And on a chip with no PSRAM, running a UI that allocates and
// frees a 48 KB cover buffer on the way into every book, 32 KB contiguous is
// exactly the size that stops being findable. The device reported 79 KB free
// with a largest block of 30 KB: not short of memory, just short of anywhere
// to put this. A book that will not open because the heap is in pieces is a
// worse failure than 32 KB of BSS is a cost.
//
// One reader exists at a time and only one book is open in it, so one window
// is all there ever is. If the CrossPoint port (which has PSRAM configured)
// would rather have this back, it is one #ifdef away -- but it should measure
// before assuming its own heap is any less fragmented than this one.
uint8_t g_window[WIN_SIZE];
bool g_windowTaken = false;

}  // namespace

// --- CrossPoint sidecar ------------------------------------------------------

uint32_t cpHash(const char* s, size_t len) {
  // 32-bit libstdc++ _Hash_bytes: MurmurHashUnaligned2, seed 0xc70f6907.
  // Verified bit-exact against g++ -m32 std::hash<std::string>; the harness
  // pins known vectors so a refactor cannot drift it.
  const uint32_t m = 0x5bd1e995;
  uint32_t h = 0xc70f6907u ^ (uint32_t)len;
  const uint8_t* buf = (const uint8_t*)s;
  while (len >= 4) {
    uint32_t k = le32(buf);
    k *= m;
    k ^= k >> 24;
    k *= m;
    h *= m;
    h ^= k;
    buf += 4;
    len -= 4;
  }
  switch (len) {
    case 3: h ^= (uint32_t)buf[2] << 16; /* fall through */
    case 2: h ^= (uint32_t)buf[1] << 8;  /* fall through */
    case 1:
      h ^= (uint32_t)buf[0];
      h *= m;
  }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

uint64_t fnvHash64(const char* s, size_t len) {
  // FNV-1a 64, matching CrossInk's ZipFile::fnvHash64 exactly: their cache
  // directory name is std::to_string of this value.
  uint64_t hash = 14695981039346656037ull;
  for (size_t i = 0; i < len; i++) {
    hash ^= (uint8_t)s[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

void cacheDir(const char* bookPath, char* out, int cap) {
  snprintf(out, (size_t)cap, "/.crosspoint/epub_%llu",
           (unsigned long long)fnvHash64(bookPath, strlen(bookPath)));
}

void cacheDirLegacy(const char* bookPath, char* out, int cap) {
  snprintf(out, (size_t)cap, "/.crosspoint/epub_%lu",
           (unsigned long)cpHash(bookPath, strlen(bookPath)));
}

int encodeProgress(const Progress& p, uint8_t out[10]) {
  out[0] = p.spine & 0xFF;
  out[1] = (p.spine >> 8) & 0xFF;
  out[2] = p.page & 0xFF;
  out[3] = (p.page >> 8) & 0xFF;
  out[4] = p.pageCount & 0xFF;
  out[5] = (p.pageCount >> 8) & 0xFF;
  if (!p.hasOffset) return 6;
  out[6] = p.offset & 0xFF;
  out[7] = (p.offset >> 8) & 0xFF;
  out[8] = (p.offset >> 16) & 0xFF;
  out[9] = (p.offset >> 24) & 0xFF;
  return 10;
}

bool decodeProgress(const uint8_t* d, int n, Progress& out) {
  if (n != 4 && n != 6 && n != 10) return false;
  out = Progress{};
  out.spine = le16(d);
  out.page = le16(d + 2);
  if (out.page == 0xFFFF) out.page = 0;  // CrossPoint's navigation sentinel, never resume state
  if (n >= 6) out.pageCount = le16(d + 4);
  if (n == 10) {
    out.offset = le32(d + 6);
    out.hasOffset = true;
  }
  return true;
}

// --- the zip directory -------------------------------------------------------

uint32_t Book::spineBytes(int i) const {
  if (!_spine || i < 0 || i >= _spineN) return 0;
  // Chapters the OPF named but the zip never had contribute nothing, which is
  // right: they occupy no reading time either.
  return _spine[i].ok >= 2 ? _spine[i].usize : 0;
}

uint32_t Book::spineTotalBytes() const {
  uint32_t t = 0;
  for (int i = 0; i < _spineN; i++) t += spineBytes(i);
  return t;
}

bool Book::open(IO& io) {
  close();
  _io = &io;
  _err = "";

  // End-of-central-directory: scan back from the file end for its signature.
  const uint32_t fsize = io.size();
  if (fsize < 22) {
    _err = "not a zip";
    return false;
  }
  uint8_t buf[534];
  int cdCount = -1;
  uint32_t cdOfs = 0;
  const uint32_t maxBack = fsize < 66000 ? fsize : 66000;
  uint32_t scanned = 0;
  while (scanned < maxBack && cdCount < 0) {
    // 512-byte steps with a 21-byte overlap so a signature straddling two
    // chunks is still seen whole.
    uint32_t chunk = 533;
    uint32_t end = fsize - scanned;
    uint32_t at = end > chunk ? end - chunk : 0;
    if (at + chunk > fsize) chunk = fsize - at;
    if (io.read(at, buf, chunk) != (int)chunk) break;
    for (int i = (int)chunk - 22; i >= 0; i--) {
      if (buf[i] == 'P' && buf[i + 1] == 'K' && buf[i + 2] == 5 && buf[i + 3] == 6) {
        cdCount = le16(buf + i + 10);
        cdOfs = le32(buf + i + 16);
        break;
      }
    }
    scanned += 512;
  }
  if (cdCount < 0) {
    _err = "no zip directory";
    return false;
  }
  _cdOfs = cdOfs;
  _cdCount = cdCount;

  _spine = (Ent*)calloc(MAX_SPINE, sizeof(Ent));
  if (!_spine) {
    // Named apart from the other one: 6 KB failing and 32 KB failing are
    // different diseases, and "out of memory" alone said neither.
    _err = "no memory for the spine (6 KB)";
    return false;
  }

  char opfPath[192];
  if (!parseContainer(opfPath, sizeof(opfPath))) return false;
  _opfDir[0] = 0;
  const char* slash = strrchr(opfPath, '/');
  if (slash) {
    size_t dl = (size_t)(slash - opfPath) + 1;
    if (dl >= sizeof(_opfDir)) dl = sizeof(_opfDir) - 1;
    memcpy(_opfDir, opfPath, dl);
    _opfDir[dl] = 0;
  }
  if (!parseOpf(opfPath)) return false;
  if (_spineN == 0) {
    _err = "no chapters in the spine";
    return false;
  }
  return true;
}

void Book::close() {
  chapterClose();
  free(_spine);
  _spine = nullptr;
  _spineN = 0;
  free(_inflator);
  _inflator = nullptr;
  if (_window == g_window) g_windowTaken = false;  // the static one goes back
  _window = nullptr;
  free(_inBuf);
  _inBuf = nullptr;
  _io = nullptr;
}

template <typename F>
bool Book::walkCD(F cb) {
  uint32_t at = _cdOfs;
  for (int i = 0; i < _cdCount; i++) {
    uint8_t h[46];
    if (_io->read(at, h, 46) != 46) return false;
    if (le32(h) != 0x02014b50u) return false;
    const uint16_t nameLen = le16(h + 28);
    const uint16_t extraLen = le16(h + 30);
    const uint16_t commentLen = le16(h + 32);
    char name[300];
    const uint16_t keep = nameLen < sizeof(name) - 1 ? nameLen : (uint16_t)(sizeof(name) - 1);
    if (keep && _io->read(at + 46, name, keep) != (int)keep) return false;
    name[keep] = 0;
    if (nameLen == keep) {
      Ent e{};
      e.method = le16(h + 10);
      e.csize = le32(h + 20);
      e.usize = le32(h + 24);
      e.lho = le32(h + 42);
      cb((const char*)name, e);
    }
    at += 46u + nameLen + extraLen + commentLen;
  }
  return true;
}

bool Book::findEntry(const char* wanted, Ent& out) {
  bool found = false;
  walkCD([&](const char* name, const Ent& e) {
    if (!found && strcmp(name, wanted) == 0) {
      out = e;
      found = true;
    }
  });
  return found;
}

bool Book::parseContainer(char* opfPath, int cap) {
  Ent e{};
  if (!findEntry("META-INF/container.xml", e)) {
    _err = "no container.xml";
    return false;
  }
  if (!entryOpen(e)) return false;
  opfPath[0] = 0;
  scanTags([&](const char* name, const char* attrs, bool close) {
    if (!close && nameIs(name, "rootfile") && opfPath[0] == 0) {
      char raw[192];
      if (attrValue(attrs, "full-path", raw, sizeof(raw))) resolveHref("", raw, opfPath, cap);
    }
    return opfPath[0] != 0;
  });
  entryClose();
  if (!opfPath[0]) {
    _err = "no rootfile in container";
    return false;
  }
  return true;
}

bool Book::parseOpf(const char* opfPath) {
  Ent opf{};
  if (!findEntry(opfPath, opf)) {
    _err = "the OPF is missing";
    return false;
  }

  // Pass one: the spine's idrefs, in reading order, stored as id hashes --
  // and the EPUB2 cover declaration, a <meta name="cover" content="id">.
  if (!entryOpen(opf)) return false;
  _spineN = 0;
  uint32_t coverIdHash = 0, ncxIdHash = 0;
  scanTags([&](const char* name, const char* attrs, bool close) {
    if (close) return false;
    if (nameIs(name, "itemref") && _spineN < MAX_SPINE) {
      char idref[128];
      if (attrValue(attrs, "idref", idref, sizeof(idref))) {
        _spine[_spineN].hrefHash = fnv(idref, strlen(idref));
        _spine[_spineN].ok = 0;
        _spineN++;
      }
    } else if (nameIs(name, "spine")) {
      // EPUB2 points at its NCX from here: <spine toc="ncx">.
      char toc[128];
      if (attrValue(attrs, "toc", toc, sizeof(toc))) ncxIdHash = fnv(toc, strlen(toc));
    } else if (nameIs(name, "meta") && coverIdHash == 0) {
      char nm[32], content[128];
      if (attrValue(attrs, "name", nm, sizeof(nm)) && strcmp(nm, "cover") == 0 &&
          attrValue(attrs, "content", content, sizeof(content)))
        coverIdHash = fnv(content, strlen(content));
    }
    return false;
  });
  entryClose();

  // Pass two: manifest items turn id hashes into resolved-path hashes. The
  // cover resolves here too, matched by that meta id or by the EPUB3
  // properties="cover-image" marker, whichever the book uses.
  if (!entryOpen(opf)) return false;
  _coverPathHash = 0;
  _coverType = 0;
  scanTags([&](const char* name, const char* attrs, bool close) {
    if (!close && nameIs(name, "item")) {
      // Static: this lambda is inlined into scanTags, and its locals landed on
      // the loop task's stack every time a book was opened.
      static char id[128], href[192];
      if (attrValue(attrs, "id", id, sizeof(id)) && attrValue(attrs, "href", href, sizeof(href))) {
        const uint32_t idHash = fnv(id, strlen(id));
        static char full[256];
        resolveHref(_opfDir, href, full, sizeof(full));
        const uint32_t pathHash = fnv(full, strlen(full));
        for (int i = 0; i < _spineN; i++)
          if (_spine[i].ok == 0 && _spine[i].hrefHash == idHash) {
            _spine[i].hrefHash = pathHash;
            _spine[i].ok = 1;
          }
        // The contents document, either spelling. EPUB3 marks it in the
        // manifest, EPUB2 names it from the spine; a book carrying both gets
        // the nav one, because it is the one with real titles in it.
        if (idHash == ncxIdHash && ncxIdHash != 0) _ncxPathHash = pathHash;
        {
          static char navProps[128];
          navProps[0] = 0;
          attrValue(attrs, "properties", navProps, sizeof(navProps));
          // "nav" as a whole word: "cover-image" and "scripted" live in the
          // same attribute and a substring test would take half of them.
          for (const char* q = strstr(navProps, "nav"); q; q = strstr(q + 1, "nav")) {
            const bool leftOk = q == navProps || q[-1] == ' ';
            const bool rightOk = q[3] == 0 || q[3] == ' ';
            if (leftOk && rightOk) {
              _navPathHash = pathHash;
              break;
            }
          }
        }
        if (_coverPathHash == 0) {
          static char props[128];
          props[0] = 0;
          attrValue(attrs, "properties", props, sizeof(props));
          const bool isCover =
              (coverIdHash != 0 && idHash == coverIdHash) || strstr(props, "cover-image") != nullptr;
          if (isCover) {
            char mt[64] = "";
            attrValue(attrs, "media-type", mt, sizeof(mt));
            const size_t fl = strlen(full);
            uint8_t type = 0;
            if (strcmp(mt, "image/jpeg") == 0 ||
                (fl > 4 && (strcasecmp(full + fl - 4, ".jpg") == 0 ||
                            (fl > 5 && strcasecmp(full + fl - 5, ".jpeg") == 0))))
              type = COVER_JPEG;
            else if (strcmp(mt, "image/png") == 0 ||
                     (fl > 4 && strcasecmp(full + fl - 4, ".png") == 0))
              type = COVER_PNG;
            if (type) {
              _coverPathHash = pathHash;
              _coverType = type;
            }
          }
        }
      }
    }
    return false;
  });
  entryClose();

  // An <itemref> that no manifest item answers to is dropped, not kept as a
  // blank chapter. CrossPoint only creates a spine entry when the idref
  // resolves, so a slot kept here would shift every chapter after it by one
  // -- and a reading position saved by either firmware would reopen in the
  // wrong chapter on the other. Nothing is lost: the slot has no path behind
  // it and no zip entry to stream, so it could never have been shown.
  {
    int w = 0;
    for (int i = 0; i < _spineN; i++)
      if (_spine[i].ok != 0) _spine[w++] = _spine[i];
    _spineN = w;
  }

  // Pass three: one walk of the zip directory fills in every matched entry.
  _coverOk = false;
  walkCD([&](const char* name, const Ent& e) {
    const uint32_t h = fnv(name, strlen(name));
    for (int i = 0; i < _spineN; i++)
      if (_spine[i].ok == 1 && _spine[i].hrefHash == h) {
        const uint32_t keep = _spine[i].hrefHash;
        _spine[i] = e;
        _spine[i].hrefHash = keep;
        _spine[i].ok = 2;
      }
    if (_coverPathHash != 0 && h == _coverPathHash && !_coverOk) {
      _cover = e;
      _coverOk = true;
    }
    // The contents file, and the folder its own hrefs are relative to.
    const uint8_t kind = (_navPathHash && h == _navPathHash)   ? 2
                         : (_ncxPathHash && h == _ncxPathHash) ? 1
                                                               : 0;
    if (kind > _tocKind) {
      _toc = e;
      _tocKind = kind;
      const char* slash = strrchr(name, '/');
      const int dl = slash ? (int)(slash - name) + 1 : 0;
      if (dl > 0 && dl < (int)sizeof(_tocDir)) {
        memcpy(_tocDir, name, (size_t)dl);
        _tocDir[dl] = 0;
      } else {
        _tocDir[0] = 0;
      }
    }
  });
  if (!_coverOk) _coverType = 0;
  return true;
}

// --- the table of contents ---------------------------------------------------
// One parser for both formats, because both say the same two things in the
// same order: a title, then where it points.
//
//   EPUB3 nav:  <a href="ch1.xhtml">Chapter One</a>
//   EPUB2 ncx:  <navLabel><text>Chapter One</text></navLabel>
//               <content src="ch1.xhtml"/>
//
// So: collect text inside <a> or <text>, and emit when the tag that closes it
// arrives -- </a> for the first, <content src> for the second.
//
// Entries pointing into a chapter that is already listed are dropped. A ToC
// with three headings inside one file would otherwise offer three rows that
// all go to the same page: this reader jumps to chapters, not to fragments,
// and a list that lies about where it lands is worse than a shorter one.
int Book::tocRead(TocEntry* out, int max) {
  if (!_io || _tocKind == 0 || max <= 0) return 0;
  if (!entryOpen(_toc)) return 0;

  int n = 0;
  uint8_t buf[256];
  int have = 0, pos = 0;
  auto nextByte = [&]() -> int {
    if (pos >= have) {
      have = entryRead(buf, sizeof(buf));
      pos = 0;
      if (have <= 0) return -1;
    }
    return buf[pos++];
  };

  char title[96];
  int tl = 0;
  bool collecting = false;
  char href[192] = "";
  char held[96] = "";  // an ncx title, waiting for the <content> that follows

  auto tidy = [&]() {
    // One line, no runs of space, no edges: publishers put newlines and two
    // spaces of indentation inside a <text> element and mean none of it.
    int o = 0;
    bool sp = false;
    for (int i = 0; i < tl; i++) {
      const char ch = wsByte((uint8_t)title[i]) ? ' ' : title[i];
      if (ch == ' ') {
        sp = true;
        continue;
      }
      if (sp && o > 0 && o < (int)sizeof(title) - 1) title[o++] = ' ';
      sp = false;
      if (o < (int)sizeof(title) - 1) title[o++] = ch;
    }
    title[o] = 0;
    tl = o;
  };

  auto emit = [&](const char* label, const char* target) {
    if (n >= max || !label[0] || !target[0]) return;
    char full[256];
    resolveHref(_tocDir, target, full, sizeof(full));
    const uint32_t h = fnv(full, strlen(full));
    for (int i = 0; i < _spineN; i++) {
      if (_spine[i].ok != 2 || _spine[i].hrefHash != h) continue;
      for (int k = 0; k < n; k++)
        if (out[k].spine == (uint16_t)i) return;  // already listed
      snprintf(out[n].title, sizeof(out[n].title), "%s", label);
      out[n].spine = (uint16_t)i;
      n++;
      return;
    }
  };

  int b = nextByte();
  while (b >= 0 && n < max) {
    if (b != '<') {
      if (collecting && tl < (int)sizeof(title) - 1) title[tl++] = (char)b;
      b = nextByte();
      continue;
    }
    // a tag: name, then the rest of it
    char name[32];
    int nl = 0;
    b = nextByte();
    const bool closing = b == '/';
    if (closing) b = nextByte();
    while (b >= 0 && b != '>' && !wsByte(b) && b != '/') {
      if (nl < (int)sizeof(name) - 1) name[nl++] = (char)b;
      b = nextByte();
    }
    name[nl] = 0;
    char attrs[256];
    int al = 0;
    char quote = 0;
    while (b >= 0 && (quote || b != '>')) {
      if (quote) {
        if (b == quote) quote = 0;
      } else if (b == '"' || b == '\'') {
        quote = (char)b;
      }
      if (al < (int)sizeof(attrs) - 1) attrs[al++] = (char)b;
      b = nextByte();
    }
    attrs[al] = 0;
    if (b < 0) break;
    b = nextByte();  // step past '>'

    if (closing) {
      if (nameIs(name, "a") && collecting) {
        collecting = false;
        tidy();
        emit(title, href);
      } else if (nameIs(name, "text") && collecting) {
        collecting = false;
        tidy();
        snprintf(held, sizeof(held), "%s", title);
      }
      continue;
    }
    if (nameIs(name, "a")) {
      href[0] = 0;
      attrValue(attrs, "href", href, sizeof(href));
      collecting = true;
      tl = 0;
    } else if (nameIs(name, "text")) {
      collecting = true;
      tl = 0;
    } else if (nameIs(name, "content")) {
      char src[192] = "";
      attrValue(attrs, "src", src, sizeof(src));
      emit(held, src);
      held[0] = 0;
    } else if (nameIs(name, "navpoint")) {
      held[0] = 0;
    }
  }
  entryClose();
  return n;
}

bool Book::coverOpen() {
  if (!_coverOk || _coverType == 0) return false;
  return entryOpen(_cover);
}

// --- the entry stream --------------------------------------------------------

bool Book::entryOpen(const Ent& e) {
  entryClose();
  if (e.method != 0 && e.method != 8) {
    _err = "unsupported compression";
    return false;
  }
  uint8_t lh[30];
  if (_io->read(e.lho, lh, 30) != 30 || le32(lh) != 0x04034b50u) {
    _err = "bad local header";
    return false;
  }
  // The local header's own name/extra lengths can differ from the central
  // directory's copy; the data starts after the local ones.
  _filePos = e.lho + 30u + le16(lh + 26) + le16(lh + 28);
  _compLeft = e.csize;
  _rawLeft = e.usize;
  _method = e.method;
  _inLen = _inPos = 0;
  _winPos = _winAvail = _winRead = 0;
  if (_method == 8) {
    if (!_inflator) _inflator = malloc(sizeof(tinfl_decompressor));
    if (!_window && !g_windowTaken) {
      _window = g_window;
      g_windowTaken = true;
    }
    if (!_inBuf) _inBuf = (uint8_t*)malloc(2048);
    if (!_inflator || !_window || !_inBuf) {
      // The 32 KB one. DEFLATE's dictionary is 32 KB by definition, so this
      // is the floor -- it cannot be made smaller, only found earlier.
      _err = "no memory to unzip (32 KB block)";
      return false;
    }
    tinfl_init((tinfl_decompressor*)_inflator);
  }
  _entryOpenF = true;
  return true;
}

void Book::entryClose() { _entryOpenF = false; }

int Book::entryRead(uint8_t* dst, int n) {
  if (!_entryOpenF) return -1;
  if (_method == 0) {
    // Stored: the zip is just a container and this is a plain read.
    const uint32_t want = (uint32_t)n < _rawLeft ? (uint32_t)n : _rawLeft;
    if (want == 0) return 0;
    const int got = _io->read(_filePos, dst, want);
    if (got <= 0) return -1;
    _filePos += (uint32_t)got;
    _rawLeft -= (uint32_t)got;
    return got;
  }

  // Deflate, streamed through tinfl with the classic 32 KB wrapping window.
  int out = 0;
  while (out < n) {
    if (_winAvail > 0) {
      uint32_t take = (uint32_t)(n - out) < _winAvail ? (uint32_t)(n - out) : _winAvail;
      const uint32_t untilWrap = WIN_SIZE - _winRead;
      if (take > untilWrap) take = untilWrap;
      memcpy(dst + out, _window + _winRead, take);
      _winRead = (_winRead + take) & (WIN_SIZE - 1);
      _winAvail -= take;
      out += (int)take;
      continue;
    }
    if (_rawLeft == 0) break;
    if (_inPos >= _inLen && _compLeft > 0) {
      const uint32_t want = _compLeft < 2048 ? _compLeft : 2048;
      const int got = _io->read(_filePos, _inBuf, want);
      if (got <= 0) return -1;
      _filePos += (uint32_t)got;
      _compLeft -= (uint32_t)got;
      _inLen = got;
      _inPos = 0;
    }
    size_t inBytes = (size_t)(_inLen - _inPos);
    size_t outBytes = WIN_SIZE - _winPos;
    const tinfl_status st = toybox_tinfl_decompress(
        (tinfl_decompressor*)_inflator, _inBuf + _inPos, &inBytes, _window, _window + _winPos,
        &outBytes, _compLeft > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
    _inPos += (int)inBytes;
    _winAvail += (uint32_t)outBytes;
    _winPos = (_winPos + (uint32_t)outBytes) & (WIN_SIZE - 1);
    if ((uint32_t)outBytes >= _rawLeft)
      _rawLeft = 0;
    else
      _rawLeft -= (uint32_t)outBytes;
    if (st == TINFL_STATUS_DONE) {
      _rawLeft = 0;
      continue;  // drain the backlog on the next spin
    }
    if (st < TINFL_STATUS_DONE) return -1;                        // corrupt stream
    if (outBytes == 0 && inBytes == 0 && _compLeft == 0) break;   // truncated
  }
  return out;
}

// --- the tag-only scanner (container.xml, OPF) -------------------------------

template <typename F>
void Book::scanTags(F cb) {
  uint8_t buf[512];
  int len = 0, pos = 0;
  auto nextByte = [&]() -> int {
    if (pos >= len) {
      len = entryRead(buf, sizeof(buf));
      pos = 0;
      if (len <= 0) return -1;
    }
    return buf[pos++];
  };

  char name[24];
  char attrs[768];
  int b;
  while (true) {
    do {
      b = nextByte();
      if (b < 0) return;
    } while (b != '<');
    b = nextByte();
    if (b < 0) return;
    if (b == '!') {
      const int c1 = nextByte(), c2 = nextByte();
      if (c1 == '-' && c2 == '-') {
        int dash = 0;
        while ((b = nextByte()) >= 0) {
          if (b == '-') dash++;
          else if (b == '>' && dash >= 2) break;
          else dash = 0;
        }
      } else {
        int depth = (c1 == '[') + (c2 == '[') - (c1 == ']') - (c2 == ']');
        if (c1 == '>' || (c2 == '>' && depth <= 0)) continue;
        while ((b = nextByte()) >= 0) {
          if (b == '[') depth++;
          else if (b == ']') depth--;
          else if (b == '>' && depth <= 0) break;
        }
      }
      continue;
    }
    if (b == '?') {
      int last = 0;
      while ((b = nextByte()) >= 0) {
        if (b == '>' && last == '?') break;
        last = b;
      }
      continue;
    }
    bool close = false;
    if (b == '/') {
      close = true;
      b = nextByte();
    }
    int nl = 0;
    while (b >= 0 && b != '>' && !wsByte(b) && b != '/') {
      if (nl < (int)sizeof(name) - 1) name[nl++] = (char)b;
      b = nextByte();
    }
    name[nl] = 0;
    int al = 0;
    char quote = 0;
    while (b >= 0 && (quote || b != '>')) {
      if (quote) {
        if (b == quote) quote = 0;
      } else if (b == '"' || b == '\'') {
        quote = (char)b;
      }
      if (al < (int)sizeof(attrs) - 1) attrs[al++] = (char)b;
      b = nextByte();
    }
    attrs[al] = 0;
    if (b < 0) return;
    if (cb((const char*)name, (const char*)attrs, close)) return;
  }
}

// --- the chapter tokenizer ---------------------------------------------------

bool Book::blobOpen(const char* entryName) {
  if (!_io || !entryName || !*entryName) return false;
  if (!findEntry(entryName, _blob)) return false;
  return entryOpen(_blob);
}

bool Book::chapterOpen(int spineIdx) {
  chapterClose();
  if (!_io || spineIdx < 0 || spineIdx >= _spineN) return false;
  if (_spine[spineIdx].ok != 2) {
    _err = "chapter missing from the zip";
    return false;
  }
  // The chapter's own directory, because an <img src> is relative to the file
  // it appears in and nothing else knew where that file was. One extra walk of
  // the central directory per chapter open, matching the resolved-path hash
  // the OPF pass already stored -- the alternative was keeping every spine
  // entry's name in RAM, which is 300 books' worth of strings for one string
  // at a time.
  _chapDir[0] = 0;
  {
    const uint32_t want = _spine[spineIdx].hrefHash;
    walkCD([&](const char* name, const Ent&) {
      if (_chapDir[0] || fnv(name, strlen(name)) != want) return;
      const char* slash = strrchr(name, '/');
      const int n = slash ? (int)(slash - name) + 1 : 0;
      if (n > 0 && n < (int)sizeof(_chapDir)) {
        memcpy(_chapDir, name, (size_t)n);
        _chapDir[n] = 0;
      }
    });
  }
  if (!entryOpen(_spine[spineIdx])) return false;
  _chapterOpenF = true;
  _insideBody = false;
  _ended = false;
  _nonVisibleDepth = 0;
  _offset = 0;
  _tokLen = _tokPos = 0;
  _pushByte = -1;
  _cpLen = 0;
  _wordLen = 0;
  _wordStart = 0;
  _wordStyle = 0;
  _outStyle = 0;
  _boldDepth = 0;  // a chapter opens in body type, whatever the last one left
  _italDepth = 0;
  _headLevel = 0;
  _outReady = false;
  _wordSinceBreak = false;
  _queued = 0;
  _imgReady = false;
  _imgName[0] = 0;
  return true;
}

void Book::chapterClose() {
  _chapterOpenF = false;
  entryClose();
}

int Book::tokNextByte() {
  if (_pushByte >= 0) {
    const int b = _pushByte;
    _pushByte = -1;
    return b;
  }
  if (_tokPos >= _tokLen) {
    const int got = entryRead(_tokBuf, sizeof(_tokBuf));
    if (got <= 0) return -1;
    _tokLen = got;
    _tokPos = 0;
  }
  return _tokBuf[_tokPos++];
}

void Book::tokFlushWord() {
  if (_wordLen == 0) return;
  memcpy(_outWord, _word, (size_t)_wordLen);
  _outWord[_wordLen] = 0;
  _outStart = _wordStart;
  _outStyle = _wordStyle;
  _outReady = true;
  _wordLen = 0;
  _wordSinceBreak = true;
}

void Book::tokBlockBreak() {
  tokFlushWord();
  if (_wordSinceBreak) {
    _queued = TOK_PARA;
    _wordSinceBreak = false;
  }
}

// One decoded codepoint of chapter text. Counting happens here and only here,
// which is what keeps the offsets CrossPoint-shaped: every codepoint of
// character data inside <body> counts, whitespace included, unless a
// non-visible tag is open.
void Book::tokEmitCp(uint32_t cp, const char* utf8, int len) {
  if (!_insideBody || _nonVisibleDepth > 0) return;
  const uint32_t at = _offset;
  _offset++;
  if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\f' || cp == '\v') {
    tokFlushWord();
    return;
  }
  char sub = 0;
  if (cp == 0xA0) sub = ' ';  // NBSP: unbreakable neighbour glue, drawn as a space
  if (_wordLen + len >= WORD_CAP - 1) {
    // Longer than the buffer: complete what there is; the rest carries on as
    // a fresh word starting at this codepoint. The layout splits long words
    // across lines anyway, so the seam is invisible.
    tokFlushWord();
  }
  if (_wordLen == 0) {
    _wordStart = at;
    _wordStyle = (uint8_t)((_boldDepth ? STYLE_BOLD : 0) | (_italDepth ? STYLE_ITAL : 0) |
                           (_headLevel << 4));
  }
  if (sub) {
    _word[_wordLen++] = sub;
  } else {
    memcpy(_word + _wordLen, utf8, (size_t)len);
    _wordLen += len;
  }
}

// Pumps bytes until the output slot fills or the chapter ends. next() drains.
int Book::tokPump() {
  // _imgReady ends the pump for the same reason a word does. An <img> between
  // two block tags flushes nothing and queues nothing, so without this the
  // loop would run straight past it and the picture would be reported after
  // the word that follows it -- one page late, which is exactly the page it
  // was supposed to open.
  while (!_outReady && !_queued && !_imgReady) {
    int b = tokNextByte();
    if (b < 0) {
      _ended = true;
      tokFlushWord();
      if (!_outReady) return TOK_END;
      _queued = TOK_END;
      break;
    }

    if (b == '<') {
      b = tokNextByte();
      if (b < 0) continue;
      if (b == '!') {
        const int c1 = tokNextByte(), c2 = tokNextByte();
        if (c1 == '-' && c2 == '-') {  // comments are not character data
          int dash = 0;
          while ((b = tokNextByte()) >= 0) {
            if (b == '-') dash++;
            else if (b == '>' && dash >= 2) break;
            else dash = 0;
          }
        } else if (c1 == '[' && c2 == 'C') {  // <![CDATA[ ... ]]> is character data
          for (int k = 0; k < 5; k++) tokNextByte();  // "DATA["
          int r1 = -1, r2 = -1;
          while ((b = tokNextByte()) >= 0) {
            if (r1 == ']' && r2 == ']' && b == '>') break;
            if (r1 >= 0 && _insideBody && _nonVisibleDepth == 0) {
              _cpBuf[0] = (char)r1;
              _cpLen = 1;
              tokEmitCp((uint32_t)(uint8_t)r1, _cpBuf, 1);  // CDATA is almost always ASCII
              _cpLen = 0;
            }
            r1 = r2;
            r2 = b;
          }
        } else {  // DOCTYPE and friends
          int depth = (c1 == '[') + (c2 == '[') - (c1 == ']') - (c2 == ']');
          if (!(c1 == '>' || (c2 == '>' && depth <= 0))) {
            while ((b = tokNextByte()) >= 0) {
              if (b == '[') depth++;
              else if (b == ']') depth--;
              else if (b == '>' && depth <= 0) break;
            }
          }
        }
        continue;
      }
      if (b == '?') {
        int last = 0;
        while ((b = tokNextByte()) >= 0) {
          if (b == '>' && last == '?') break;
          last = b;
        }
        continue;
      }
      bool closeTag = false;
      if (b == '/') {
        closeTag = true;
        b = tokNextByte();
      }
      char name[24];
      int nl = 0;
      while (b >= 0 && b != '>' && !wsByte(b) && b != '/') {
        if (nl < (int)sizeof(name) - 1) name[nl++] = (char)b;
        b = tokNextByte();
      }
      name[nl] = 0;
      bool selfClose = (b == '/');
      // Attributes are normally scanned past and dropped -- nothing above the
      // tokenizer has ever wanted one. An image is the exception, so its
      // attribute region is kept, and only its own.
      const bool wantAttrs = !closeTag && _insideBody && _nonVisibleDepth == 0 &&
                             (nameIs(name, "img") || nameIs(name, "image"));
      char attrs[256];
      int al = 0;
      char quote = 0;
      while (b >= 0 && (quote || b != '>')) {
        if (wantAttrs && al < (int)sizeof(attrs) - 1) attrs[al++] = (char)b;
        if (quote) {
          if (b == quote) quote = 0;
        } else if (b == '"' || b == '\'') {
          quote = (char)b;
          selfClose = false;
        } else if (b == '/') {
          selfClose = true;
        } else if (!wsByte(b) && b != '>') {
          selfClose = false;
        }
        b = tokNextByte();
      }
      if (closeTag) selfClose = false;

      // expat-shaped semantics: a self-closing tag is start then end
      if (closeTag) {
        if (_nonVisibleDepth > 0) {
          _nonVisibleDepth--;
        } else if (nameIs(name, "body")) {
          _insideBody = false;
        }
        if (_insideBody && _nonVisibleDepth == 0) {
          if (isBoldTag(name) && _boldDepth) _boldDepth--;
          if (isItalTag(name) && _italDepth) _italDepth--;
          if (headingLevel(name)) _headLevel = 0;
        }
        if (_insideBody && _nonVisibleDepth == 0 && isBlockTag(name)) tokBlockBreak();
      } else {
        if (nameIs(name, "body")) _insideBody = true;
        const bool skip = _insideBody && (_nonVisibleDepth > 0 || isNonVisible(name));
        if (skip) _nonVisibleDepth++;
        if (_insideBody && _nonVisibleDepth == 0 && isBlockTag(name)) tokBlockBreak();
        if (_insideBody && _nonVisibleDepth == 0 && !selfClose) {
          if (isBoldTag(name) && _boldDepth < 250) _boldDepth++;
          if (isItalTag(name) && _italDepth < 250) _italDepth++;
          if (const int hl = headingLevel(name)) _headLevel = (uint8_t)hl;
        }
        if (selfClose && skip) _nonVisibleDepth--;
        if (wantAttrs) {
          attrs[al] = 0;
          char href[192];
          // SVG's <image> uses xlink:href; attrValue strips the prefix, so one
          // lookup covers both spellings.
          if (attrValue(attrs, "src", href, sizeof(href)) ||
              attrValue(attrs, "href", href, sizeof(href))) {
            resolveHref(_chapDir, href, _imgName, sizeof(_imgName));
            // An image ends the block it sits in, the same as a <br> would --
            // otherwise the words either side of it run together.
            if (_imgName[0]) {
              tokBlockBreak();
              _imgReady = true;
            }
          }
        }
      }
      continue;
    }

    if (b == '&') {
      // Entities, expat-shaped: XML five and numerics expand; known HTML
      // names expand through the vendored table; unknown names pass through
      // literally. All three behaviours match CrossPoint's counting.
      char ent[36];
      int el = 0;
      ent[el++] = '&';
      bool terminated = false;
      while (el < (int)sizeof(ent) - 2) {
        b = tokNextByte();
        if (b < 0) break;
        if (b == ';') {
          ent[el++] = ';';
          terminated = true;
          break;
        }
        if (b == '<' || wsByte(b)) {
          _pushByte = b;
          break;
        }
        ent[el++] = (char)b;
      }
      ent[el] = 0;
      if (terminated && ent[1] == '#') {
        uint32_t cp = 0;
        if (ent[2] == 'x' || ent[2] == 'X') {
          for (const char* p = ent + 3; *p && *p != ';'; p++)
            cp = cp * 16 +
                 (uint32_t)(*p >= 'a' ? *p - 'a' + 10 : *p >= 'A' ? *p - 'A' + 10 : *p - '0');
        } else {
          for (const char* p = ent + 2; *p && *p != ';'; p++) cp = cp * 10 + (uint32_t)(*p - '0');
        }
        char u[4];
        const int ul = utf8Encode(cp, u);
        tokEmitCp(cp, u, ul);
      } else {
        const char* exp = terminated ? epubent::lookup(ent, (size_t)el) : nullptr;
        const char* text = exp ? exp : ent;
        for (const char* p = text; *p;) {
          const char* start = p;
          uint32_t cp = (uint8_t)*p++;
          if (cp >= 0xC0) {
            int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : 1;
            cp &= (uint32_t)(0x3F >> extra);
            while (extra-- > 0 && ((uint8_t)*p & 0xC0) == 0x80)
              cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
          }
          tokEmitCp(cp, start, (int)(p - start));
        }
      }
      continue;
    }

    // plain text byte: assemble UTF-8
    if (!_insideBody || _nonVisibleDepth > 0) continue;
    _cpBuf[_cpLen++] = (char)b;
    const uint8_t leadByte = (uint8_t)_cpBuf[0];
    int need = 1;
    if (leadByte >= 0xF0)
      need = 4;
    else if (leadByte >= 0xE0)
      need = 3;
    else if (leadByte >= 0xC0)
      need = 2;
    if (_cpLen < need && _cpLen < (int)sizeof(_cpBuf)) continue;
    uint32_t cp = leadByte;
    if (need > 1) {
      cp = leadByte & (uint32_t)(0x3F >> (need - 1));
      for (int i = 1; i < need; i++) cp = (cp << 6) | ((uint8_t)_cpBuf[i] & 0x3F);
    }
    tokEmitCp(cp, _cpBuf, _cpLen);
    _cpLen = 0;
  }

  if (_outReady) return TOK_WORD;
  if (_queued) {
    const int q = _queued;
    _queued = 0;
    return q;
  }
  if (_imgReady) {
    _imgReady = false;
    return TOK_IMAGE;  // next() fills the name and the offset
  }
  return TOK_END;
}

int Book::next(char* word, uint32_t& startOff) {
  // An image beats the queue: it was seen at this point in the stream and the
  // page that shows it has to break here.
  if (_imgReady && !_outReady) {
    _imgReady = false;
    startOff = _offset;
    snprintf(word, WORD_CAP, "%s", _imgName);
    return TOK_IMAGE;
  }
  if (!_chapterOpenF) return TOK_ERR;
  if (_outReady) {
    strcpy(word, _outWord);
    startOff = _outStart;
    _outReady = false;
    return TOK_WORD;
  }
  if (_queued) {
    const int q = _queued;
    _queued = 0;
    return q;
  }
  if (_ended) return TOK_END;
  const int r = tokPump();
  if (r == TOK_WORD) {
    strcpy(word, _outWord);
    startOff = _outStart;
    _outReady = false;
  } else if (r == TOK_IMAGE) {
    startOff = _offset;
    snprintf(word, WORD_CAP, "%s", _imgName);
  }
  return r;
}

}  // namespace epubc
