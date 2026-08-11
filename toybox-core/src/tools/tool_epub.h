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
#include "recents.h"
#include "tools_ui.h"

namespace epubui {
inline constexpr int MARGIN = 24;
inline constexpr int TOP = 18;
inline constexpr int BOTTOM = 770;   // the footer band starts below this
inline constexpr int LINE_STEP = 34; // 24 px type with air; ~22 lines a page
inline constexpr int PARA_GAP = 14;
inline constexpr int MAX_LINES = 24;
inline constexpr int TURN_W = 160;   // tap thirds, same as the .tbk reader
inline constexpr int LIST_Y0 = 64, LIST_ROW_H = 88;
inline constexpr int MAX_BOOKS = 8;
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
    _n = h.epubList(_books, epubui::MAX_BOOKS);
    if (!_lut) _lut = (uint32_t*)malloc(sizeof(uint32_t) * epubui::MAX_PAGES);
  }

  void render(ToolsCanvas& c) override {
    if (_screen == Screen::Loading) {
      bthumb::drawLoading(c, _books[_cur].file, _books[_cur].title);
      return;
    }
    if (_screen == Screen::Page) {
      renderPage(c);
      return;
    }
    host().topBar(title());
    if (_n < 0) {
      c.textCentered(c.width() / 2, 320, "no card found", TS_LARGE, true);
      c.textCentered(c.width() / 2, 364, "is one in the slot?", TS_MED, true);
      return;
    }
    if (_n == 0) {
      c.textCentered(c.width() / 2, 300, "no ebooks on the card", TS_LARGE, true);
      c.textCentered(c.width() / 2, 348, "put .epub files in /books", TS_SMALL, true);
      return;
    }
    for (int i = 0; i < _n; i++) {
      const int y = epubui::LIST_Y0 + i * epubui::LIST_ROW_H;
      c.text(24, y + 10, _books[i].title, TS_MED, true, true);
      c.text(24, y + 44, _books[i].cont ? "carries on where it stopped" : "from the start",
             TS_SMALL, true);
      c.fillRect(16, y + epubui::LIST_ROW_H - 6, c.width() - 32, 1, true);
    }
    c.textCentered(c.width() / 2, 748, _note ? _note : "the position is kept on the card itself,",
                   TS_SMALL, true);
    if (!_note)
      c.textCentered(c.width() / 2, 772, "so CrossPoint firmware picks up the same page",
                     TS_SMALL, true);
  }

  void onTap(int x, int y) override {
    if (_screen == Screen::List) {
      if (host().isBackTap(x, y)) {
        host().goHub();
        return;
      }
      if (_n <= 0) return;
      const int i = (y - epubui::LIST_Y0) / epubui::LIST_ROW_H;
      if (y < epubui::LIST_Y0 || i < 0 || i >= _n) return;
      openBook(i);
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
    for (int i = 0; i < _n; i++)
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
#endif

 private:
  enum class Screen : uint8_t { List, Loading, Page };

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
      _note = _book.error();
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
      if (!epubcov::makeThumb(_book, _books[i].file)) bthumb::markFailed(_books[i].file);
    }

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
      _note = _book.error()[0] ? _book.error() : "could not read the chapter";
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
    c.text(16, 758, _books[_cur].title, TS_MED, true, true);
    char pos[40];
    if (_chapterPages > 0)
      snprintf(pos, sizeof(pos), "ch %d/%d · p %d/%d", _spine + 1, _book.spineCount(), _page + 1,
               _chapterPages);
    else
      snprintf(pos, sizeof(pos), "ch %d/%d · p %d", _spine + 1, _book.spineCount(), _page + 1);
    c.text(c.width() - 16 - c.textWidth(pos, TS_MED), 758, pos, TS_MED, true);
  }

  Screen _screen = Screen::List;
  ToolsHost::EpubInfo _books[epubui::MAX_BOOKS];
  int _n = -1;
  int _cur = -1;
  bool _open = false;
  bool _chrome = false;
  const char* _note = nullptr;

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
