// The EPUB reader: real ebooks off the SD card, laid out live with the
// device fonts, one chapter streaming at a time.
//
// The design promise is the reading position. It is stored on the CARD, in
// CrossPoint Reader's own format (/.crosspoint/epub_<hash>/progress.bin, a
// spine index plus a visible-codepoint offset), so the same card moved to a
// CrossPoint device -- or this device reflashed to CrossPoint -- opens the
// same book on the same page. Everything else is deliberately plain: text
// only, no images, no CSS, the layout re-derived on this device every time.
//
// Paging works on a stream. A page is laid out by pulling words until the
// glass is full; turning forward continues the stream, turning back replays
// the chapter to the previous page's recorded start offset (the offsets seen
// so far live in a little per-chapter table). Replay costs one re-inflate of
// the chapter, which disappears inside the panel's own refresh time.
#pragma once
#include "book_thumbs.h"
#include "epub/epub_cover.h"
#include "epub/epubcore.h"
#include "epub/koreader_sdr.h"
#include "recents.h"
#include "shelf.h"
#include "tools_ui.h"

namespace epubui {
inline constexpr int MARGIN = 24;
inline constexpr int TOP = 18;
inline constexpr int BOTTOM = 770;   // the footer band starts below this
inline constexpr int LINE_STEP = 34; // 24 px type with air; ~22 lines a page
inline constexpr int PARA_GAP = 14;
inline constexpr int MAX_LINES = 24;
inline constexpr int TURN_W = 160;   // tap thirds, same as the .tbk reader
// The list itself is shelf.h's: series folders, then books, a page at a time.
inline constexpr int LIST_Y0 = shelf::Y0, LIST_ROW_H = shelf::ROW_H;
inline constexpr int MAX_BOOKS = shelf::MAX_ITEMS;
inline constexpr int MAX_PAGES = 2048;  // per chapter; ~90x any real chapter
}  // namespace epubui

class EpubTool : public ToolApp {
 public:
  const char* title() const override { return "EPUB"; }

  ~EpubTool() override {
    if (_open && _host) closeBook(false);
    free(_lut);
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _screen = Screen::List;
    _note = nullptr;
    snprintf(_dir, sizeof(_dir), "%s", shelf::TOP);
    reload();
    if (!_lut) _lut = (uint32_t*)malloc(sizeof(uint32_t) * epubui::MAX_PAGES);
  }

  void render(ToolsCanvas& c) override {
    if (_screen == Screen::Loading) {
      bthumb::drawLoading(host(), c, _books[_cur].file, _books[_cur].title);
      return;
    }
    if (_screen == Screen::Page) {
      renderPage(c);
      return;
    }
    // Inside a series the bar carries the series name, because that is the
    // only thing on the screen that says where you are.
    host().topBar(inFolder() ? seriesName() : title(), false, inFolder() ? title() : "HUB");
    if (_n < 0) {
      c.textCentered(c.width() / 2, 320, "no card found", TS_LARGE, true);
      c.textCentered(c.width() / 2, 364, "is one in the slot?", TS_MED, true);
      return;
    }
    const int total = items();
    if (total == 0) {
      c.textCentered(c.width() / 2, 300, "no ebooks on the card", TS_LARGE, true);
      c.textCentered(c.width() / 2, 348, "put .epub files in /books", TS_SMALL, true);
      return;
    }
    for (int k = 0; k < shelf::PER_PAGE; k++) {
      const int idx = _lpage * shelf::PER_PAGE + k;
      if (idx >= total) break;
      if (idx < _nf) {
        shelf::drawFolderRow(c, k, _folders[idx].name, _folders[idx].count,
                             shelf::rowSep(k, idx, total));
        continue;
      }
      const int b = idx - _nf;
      const int y = shelf::Y0 + k * shelf::ROW_H;
      c.textClipped(24, y + 10, c.width() - 48, _books[b].title, TS_MED, true, true);
      c.text(24, y + 44, _books[b].cont ? "carries on where it stopped" : "from the start",
             TS_SMALL, true);
      if (shelf::rowSep(k, idx, total))
        c.fillRect(16, y + shelf::ROW_H - 6, c.width() - 32, 1, true);
    }
    shelf::drawPager(c, _lpage, total);
    c.textCentered(c.width() / 2, 770,
                   _note ? _note : "positions are kept on the card, as CrossPoint keeps them",
                   TS_SMALL, true);
  }

  void onTap(int x, int y) override {
    if (_screen == Screen::List) {
      if (host().isBackTap(x, y)) {
        // The back arrow climbs one level at a time: out of the series
        // first, out of the app second. One arrow, no second control.
        if (inFolder()) {
          snprintf(_dir, sizeof(_dir), "%s", shelf::TOP);
          _note = nullptr;
          reload();
          host().refresh(true);
        } else {
          host().goHub();
        }
        return;
      }
      const int total = items();
      if (total <= 0) return;
      const int pages = shelf::pageCount(total);
      if (y >= shelf::PAGER_Y && pages > 1) {
        if (_lpage > 0 && shelf::prevRect().hit(x, y)) {
          _lpage--;
          host().beep(0);
          host().refresh(true);
        } else if (_lpage < pages - 1 && shelf::nextRect().hit(x, y)) {
          _lpage++;
          host().beep(0);
          host().refresh(true);
        }
        return;
      }
      const int idx = shelf::hitRow(x, y, total, _lpage);
      if (idx < 0) return;
      if (idx < _nf) {
        openFolder(idx);
        return;
      }
      openBook(idx - _nf);
      return;
    }
    // The page. Corner out, thirds turn, middle toggles the footer.
    if (x < 110 && y < 50) {
      closeBook(true);
      return;
    }
    const bool left = x < epubui::TURN_W;
    const bool right = x >= host().canvas().width() - epubui::TURN_W;
    if (!left && !right) {
      _chrome = !_chrome;
      host().beep(0);
      host().refresh(false);  // the footer band is the only change
      return;
    }
    turn(right ? 1 : -1);
  }

  // DOWN is always forward, matching the .tbk reader; a short press of the
  // power button (Ok) closes the book, also matching.
  bool onButton(SideBtn b) override {
    if (_screen != Screen::Page) return false;
    if (b == SideBtn::Ok) {
      closeBook(true);
      return true;
    }
    turn(b == SideBtn::Down ? 1 : -1);
    return true;
  }

  // The hub's recently-read covers land here: straight into the named book.
  bool openDirect(const char* file) override {
    char dir[128];
    shelf::dirOf(file, dir, sizeof(dir));
    if (strcmp(dir, _dir) != 0) {
      snprintf(_dir, sizeof(_dir), "%s", dir);
      reload();
    }
    for (int i = 0; i < (_n < 0 ? 0 : _n); i++)
      if (strcmp(_books[i].file, file) == 0) {
        openBook(i);
        return _open;
      }
    return false;
  }

#ifdef TOYBOX_HOST
  int hostScreen() const { return _screen == Screen::Page ? 1 : 0; }
  int hostSpine() const { return _spine; }
  int hostPage() const { return _page; }
  uint32_t hostPageOffset() const { return _page < _lutN ? _lut[_page] : 0; }
  const char* hostLine(int i) const { return i < _lineN ? _lines[i].t : ""; }
  int hostLineCount() const { return _lineN; }
  const char* hostDir() const { return _dir; }
  int hostFolders() const { return _nf; }
  int hostItems() const { return items(); }
  int hostListPage() const { return _lpage; }
#endif

 private:
  enum class Screen : uint8_t { List, Loading, Page };

  // Any failure that might be about memory carries the numbers. Guessing at
  // this cost two round trips already; the allocator knows and it is free to
  // ask.
  const char* withHeap(const char* why) {
    const uint32_t free = host().heapFree(), big = host().heapLargest();
    if (free == 0 && big == 0) return why;  // a host that cannot tell
    snprintf(_noteBuf, sizeof(_noteBuf), "%s - free %lu KB, biggest %lu KB", why,
             (unsigned long)(free / 1024), (unsigned long)(big / 1024));
    return _noteBuf;
  }

  bool inFolder() const { return !shelf::isTop(_dir); }
  const char* seriesName() const {
    const char* s = strrchr(_dir, '/');
    return s ? s + 1 : _dir;
  }
  int items() const { return (_nf < 0 ? 0 : _nf) + (_n < 0 ? 0 : _n); }

  // One listing call per level. Folders only exist at the top: a series of a
  // series is a filing system, not a shelf.
  void reload() {
    _nf = inFolder() ? 0 : host().shelfFolders(_folders, shelf::MAX_FOLDERS, ".epub");
    if (_nf < 0) _nf = 0;
    _n = host().epubList(_books, epubui::MAX_BOOKS, _dir);
    _lpage = 0;
  }

  void openFolder(int i) {
    snprintf(_dir, sizeof(_dir), "%s/%s", shelf::TOP, _folders[i].name);
    _note = nullptr;
    reload();
    host().beep(0);
    host().refresh(true);
  }

  struct HostIO : epubc::IO {
    ToolsHost* h = nullptr;
    int read(uint32_t pos, void* dst, uint32_t n) override { return h->epubRead(pos, dst, n); }
    uint32_t size() override { return h->epubSize(); }
  };

  struct Line {
    char t[200];
    short y;  // where layout put it: paragraph gaps make the steps uneven
  };

  void progressPath(char* out, int cap) {
    char dir[96];
    epubc::cacheDir(_books[_cur].file, dir, sizeof(dir));
    snprintf(out, (size_t)cap, "%s/progress.bin", dir);
  }

  void saveProgress() {
    if (_cur < 0 || _page >= _lutN) return;
    epubc::Progress p;
    p.spine = (uint16_t)_spine;
    p.page = (uint16_t)_page;
    p.pageCount = (uint16_t)(_chapterPages > 0 ? _chapterPages : 0);
    p.offset = _lut[_page];
    p.hasOffset = true;
    uint8_t buf[10];
    const int n = epubc::encodeProgress(p, buf);
    char path[128];
    progressPath(path, sizeof(path));
    host().sdWriteFileAtomic(path, buf, n);
    saveKoreader();
  }

  // How far through the whole book we are, as KOReader wants it: 0..1.
  //
  // Chapter boundaries are exact -- the zip said how big every chapter is --
  // and within a chapter we only claim a fraction once the chapter has been
  // read to its end and its page count is therefore known. Until then the
  // answer is the START of the chapter you are in, which is honest and never
  // overshoots. Landing a reader slightly behind costs them a page turn;
  // landing them ahead costs them the ending.
  double bookFraction() const {
    const uint32_t total = _book.spineTotalBytes();
    if (total == 0) return 0;
    uint32_t before = 0;
    for (int i = 0; i < _spine && i < _book.spineCount(); i++) before += _book.spineBytes(i);
    double f = 0;
    if (_chapterPages > 0 && _page > 0) {
      f = (double)_page / (double)_chapterPages;
      if (f > 1) f = 1;
    }
    const double at = (double)before + (double)_book.spineBytes(_spine) * f;
    const double pct = at / (double)total;
    return pct < 0 ? 0 : (pct > 1 ? 1 : pct);
  }

  void saveKoreader() {
    char path[160];
    if (!ksdr::metaPath(_books[_cur].file, path, sizeof(path))) return;
    ksdr::State st;
    st.percent = bookFraction();
    char lua[256];
    const int n = ksdr::render(st, lua, sizeof(lua));
    if (n > 0) host().sdWriteFileAtomic(path, lua, n);
  }

  void openBook(int i) {
    _cur = i;
    // The cover as the loading screen, painted before the card is touched. An
    // EPUB spends real seconds opening (zip walk, cover decode on the first
    // open, the chapter replayed to the saved page); they pass behind the
    // book's own face. First-ever open has no cover yet and shows the plate.
    _screen = Screen::Loading;
    host().refresh(true);
    if (!host().epubOpen(_books[i].file)) {
      _cur = -1;
      _screen = Screen::List;
      _note = "could not open it - is the card still in?";
      host().beep(2);
      host().refresh(true);  // the failed open borrowed the bus
      return;
    }
    _io.h = _host;
    if (!_book.open(_io)) {
      _note = withHeap(_book.error());
      host().epubClose();
      _cur = -1;
      _screen = Screen::List;
      host().beep(2);
      host().refresh(true);
      return;
    }
    _open = true;
    recents::note(prefs(), recents::KIND_EPUB, _books[i].file, _books[i].title);

    // The cover thumbnail for the hub's recently-read strip: decoded once on
    // a book's first open (a second or two for a big JPEG), never again --
    // and a cover that will not decode is marked so it is never retried.
    if (!bthumb::have(_books[i].file) && !bthumb::failed(_books[i].file)) {
      if (!epubcov::makeThumb(host(), _book, _books[i].file))
        bthumb::markFailed(_books[i].file);
    }
    // ...and, if the sleeping panel is set to wear a cover, this book's goes
    // into flash now. After the decode above, so the very first open of a
    // book still gets one.
    bthumb::noteForLock(host(), _books[i].file);

    // Where were we? The card remembers, in CrossPoint's format.
    epubc::Progress p;
    uint8_t buf[10];
    char path[128];
    progressPath(path, sizeof(path));
    const int n = host().sdReadFile(path, buf, sizeof(buf));
    int spine = 0;
    uint32_t off = 0;
    if (n > 0 && epubc::decodeProgress(buf, n, p)) {
      spine = p.spine < _book.spineCount() ? p.spine : _book.spineCount() - 1;
      off = p.hasOffset ? p.offset : 0;
    }
    _screen = Screen::Page;
    host().beep(1);
    if (!gotoPlace(spine, off)) {
      // A book whose first chapter will not parse is a book we cannot show.
      _note = withHeap(_book.error()[0] ? _book.error() : "could not read the chapter");
      closeBook(false);
      host().beep(2);
      host().refresh(true);
      return;
    }
    host().refresh(true);
  }

  void closeBook(bool beep) {
    if (_open) {
      saveProgress();
      _book.close();
      host().epubClose();  // powers the card down, re-initialises the panel
    }
    _open = false;
    _cur = -1;
    _screen = Screen::List;
    if (beep) {
      host().beep(0);
      host().refresh(true);  // full: the controller's RAM was just reset
    }
  }

  // --- layout ---------------------------------------------------------------
  // One page: pull words until the glass is full. Every page records its
  // first word's offset in _lut, which is both the back-turn index and the
  // number CrossPoint needs.

  bool chapterStart(int spineIdx) {
    if (!_book.chapterOpen(spineIdx)) return false;
    _spine = spineIdx;
    _lutN = 0;
    _chapterPages = -1;
    _pendValid = false;
    _atEnd = false;
    return true;
  }

  // Lays out the next page from the stream into _lines. Returns the number of
  // words placed (0 means the chapter had nothing left).
  int layoutPage() {
    ToolsCanvas& c = host().canvas();
    const int lineW = c.width() - 2 * epubui::MARGIN;
    _lineN = 0;
    int y = epubui::TOP;
    char cur[256];
    int curLen = 0;
    int curW = 0;
    int placed = 0;
    uint32_t pageStart = 0;
    bool started = false;
    const int spaceW = c.textWidth(" ", TS_MED) > 0 ? c.textWidth(" ", TS_MED) : 8;

    auto flushLine = [&]() -> bool {  // false: the page is full
      if (curLen == 0) return true;
      if (y + epubui::LINE_STEP > epubui::BOTTOM || _lineN >= epubui::MAX_LINES) return false;
      memcpy(_lines[_lineN].t, cur, (size_t)curLen);
      _lines[_lineN].t[curLen] = 0;
      _lines[_lineN].y = (short)y;
      _lineN++;
      y += epubui::LINE_STEP;
      curLen = 0;
      curW = 0;
      return true;
    };
    auto roomForLine = [&]() {
      return y + epubui::LINE_STEP <= epubui::BOTTOM && _lineN < epubui::MAX_LINES;
    };

    char w[epubc::WORD_CAP];
    uint32_t off = 0;
    while (true) {
      int tok;
      if (_pendValid) {
        strcpy(w, _pend);
        off = _pendOff;
        _pendValid = false;
        tok = epubc::TOK_WORD;
      } else {
        tok = _book.next(w, off);
      }

      if (tok == epubc::TOK_END || tok == epubc::TOK_ERR) {
        _atEnd = true;
        flushLine();
        break;
      }
      if (tok == epubc::TOK_PARA) {
        if (!started) continue;  // no leading gap on a fresh page
        if (!flushLine()) {
          _atEnd = false;
          break;  // full at a paragraph edge; nothing pends
        }
        y += epubui::PARA_GAP;
        continue;
      }

      // a word
      const int ww = c.textWidth(w, TS_MED);
      if (!started) {
        pageStart = off;
        started = true;
      }
      // `placed` answers "did any word begin landing on this page": the first
      // word of a fresh page always lands (split if huge), so a word carried
      // whole to the next page only happens on a page that already has text.
      placed++;
      const int need = curLen ? curW + spaceW + ww : ww;
      if (need <= lineW) {
        if (curLen) {
          cur[curLen++] = ' ';
          curW += spaceW;
        }
        const int wl = (int)strlen(w);
        if (curLen + wl < (int)sizeof(cur)) {
          memcpy(cur + curLen, w, (size_t)wl);
          curLen += wl;
          curW += ww;
        }
        continue;
      }
      // does not fit beside the current line
      if (!flushLine()) {
        strcpy(_pend, w);
        _pendOff = off;
        _pendValid = true;
        break;
      }
      if (ww <= lineW) {
        if (!roomForLine()) {
          strcpy(_pend, w);
          _pendOff = off;
          _pendValid = true;
          break;
        }
        const int wl = (int)strlen(w);
        memcpy(cur, w, (size_t)wl);
        curLen = wl;
        curW = ww;
        continue;
      }
      // Longer than a whole line (Thai and CJK runs, URLs): hard-split at the
      // widest prefix that fits, and keep going with the remainder.
      const char* rest = w;
      while (*rest) {
        if (!roomForLine()) {
          strcpy(_pend, rest);
          _pendOff = off;  // close enough: a back-turn lands at the word's start
          _pendValid = true;
          break;
        }
        int fitBytes = 0;
        char probe[256];
        while (rest[fitBytes]) {
          int step = 1;
          const uint8_t lead = (uint8_t)rest[fitBytes];
          if (lead >= 0xF0) step = 4;
          else if (lead >= 0xE0) step = 3;
          else if (lead >= 0xC0) step = 2;
          memcpy(probe, rest, (size_t)(fitBytes + step));
          probe[fitBytes + step] = 0;
          if (c.textWidth(probe, TS_MED) > lineW) break;
          fitBytes += step;
        }
        if (fitBytes == 0) fitBytes = 1;  // a glyph wider than the line still moves on
        memcpy(cur, rest, (size_t)fitBytes);
        curLen = fitBytes;
        memcpy(probe, rest, (size_t)fitBytes);
        probe[fitBytes] = 0;
        curW = c.textWidth(probe, TS_MED);
        rest += fitBytes;
        if (*rest && !flushLine()) {
          strcpy(_pend, rest);
          _pendOff = off;
          _pendValid = true;
          break;
        }
      }
      if (_pendValid) break;
    }

    if (placed > 0) {
      if (_lutN < epubui::MAX_PAGES) _lut[_lutN++] = pageStart;
      if (_atEnd && !_pendValid) _chapterPages = _lutN;
    } else if (_atEnd) {
      _chapterPages = _lutN;
    }
    return placed;
  }

  // Replays the chapter from the top and lays out exactly the page whose
  // start offset is _lut[pageIdx]. Layout is deterministic from the word
  // stream, so the replayed page breaks identically to when it was first
  // seen. Costs one re-inflate of the chapter, invisible next to the panel.
  bool showPageAt(int pageIdx) {
    const uint32_t target = _lut[pageIdx];
    const int keepPages = _chapterPages;
    if (!chapterStart(_spine)) return false;
    _chapterPages = keepPages;
    char w[epubc::WORD_CAP];
    uint32_t off = 0;
    while (true) {
      const int tok = _book.next(w, off);
      if (tok == epubc::TOK_END || tok == epubc::TOK_ERR) {
        _atEnd = true;
        break;
      }
      if (tok != epubc::TOK_WORD) continue;  // page tops swallow paragraph gaps
      if (off >= target) {
        strcpy(_pend, w);
        _pendOff = off;
        _pendValid = true;
        break;
      }
    }
    _lutN = pageIdx;  // layoutPage re-records this page at its own index
    layoutPage();
    _page = pageIdx;
    return true;
  }

  // Opens spine chapter `spineIdx` and shows the page containing `off` -- the
  // last page that starts at or before it, which is CrossPoint's own rule.
  bool gotoPlace(int spineIdx, uint32_t off) {
    if (!chapterStart(spineIdx)) return false;
    while (true) {
      const int placed = layoutPage();
      if (placed == 0) {
        // an empty chapter: step forward if there is anywhere to go
        if (spineIdx + 1 < _book.spineCount()) return gotoPlace(spineIdx + 1, 0);
        _lineN = 0;
        _page = 0;
        return _lutN > 0;
      }
      _page = _lutN - 1;
      if (_lutN >= 2 && _lut[_lutN - 1] > off) {
        // overshot by one: the page before this one contains `off`
        return showPageAt(_lutN - 2);
      }
      if (_atEnd && !_pendValid) return true;  // the chapter's last page
    }
  }

  void turn(int dir) {
    if (!_open) return;
    if (dir > 0) {
      if (_pendValid || !_atEnd) {
        if (layoutPage() > 0) {
          _page = _lutN - 1;
          saveProgress();
          host().beep(0);
          host().refresh(true);
          return;
        }
      }
      // end of chapter: the next one, if there is one
      if (_spine + 1 < _book.spineCount()) {
        if (gotoPlace(_spine + 1, 0)) {
          saveProgress();
          host().beep(0);
          host().refresh(true);
          return;
        }
      }
      host().beep(2);  // the back cover
      return;
    }
    // backwards
    if (_page > 0) {
      if (!showPageAt(_page - 1)) return;
      saveProgress();
      host().beep(0);
      host().refresh(true);
      return;
    }
    // first page of the chapter: the previous chapter's last page
    if (_spine == 0) {
      host().beep(2);  // the cover
      return;
    }
    if (!chapterStart(_spine - 1)) return;
    while (layoutPage() > 0 && !(_atEnd && !_pendValid)) {
    }
    _page = _lutN > 0 ? _lutN - 1 : 0;
    saveProgress();
    host().beep(0);
    host().refresh(true);
  }

  void renderPage(ToolsCanvas& c) {
    for (int i = 0; i < _lineN; i++)
      c.text(epubui::MARGIN, _lines[i].y, _lines[i].t, TS_MED, true);
    if (!_chrome) return;
    // The footer: where you are, on a plate the page shows through around.
    c.fillRect(0, 744, c.width(), 56, false);
    c.fillRect(0, 744, c.width(), 2, true);
    // Where you are is measured FIRST, because it is the part that must not be
    // covered: a title runs to whatever length a publisher felt like, and the
    // one on this card ran straight through the page number.
    char pos[40];
    if (_chapterPages > 0)
      snprintf(pos, sizeof(pos), "ch %d/%d · p %d/%d", _spine + 1, _book.spineCount(), _page + 1,
               _chapterPages);
    else
      snprintf(pos, sizeof(pos), "ch %d/%d · p %d", _spine + 1, _book.spineCount(), _page + 1);
    const int pw = c.textWidth(pos, TS_MED);
    c.textClipped(16, 758, c.width() - 32 - pw - 12, _books[_cur].title, TS_MED, true, true);
    c.text(c.width() - 16 - pw, 758, pos, TS_MED, true);
  }

  Screen _screen = Screen::List;
  char _dir[128] = "/books";
  ToolsHost::ShelfFolder _folders[shelf::MAX_FOLDERS];
  int _nf = 0;
  int _lpage = 0;
  ToolsHost::EpubInfo _books[epubui::MAX_BOOKS];
  int _n = -1;
  int _cur = -1;
  bool _open = false;
  bool _chrome = false;
  const char* _note = nullptr;
  char _noteBuf[96] = {};

  HostIO _io;
  epubc::Book _book;
  int _spine = 0;
  int _page = 0;
  uint32_t* _lut = nullptr;  // page start offsets discovered in this chapter
  int _lutN = 0;
  int _chapterPages = -1;    // known once the chapter has been read to its end
  bool _atEnd = false;
  char _pend[epubc::WORD_CAP];
  uint32_t _pendOff = 0;
  bool _pendValid = false;
  Line _lines[epubui::MAX_LINES];
  int _lineN = 0;
};
