// The .tbk reader: fixed-size pre-dithered pages off the SD card.
//
// This app is an experiment wearing a UI. Every other use of the card borrows
// the display's SPI bus for a moment and gives it back; a book holds the bus
// for the whole session, with a 48 KB read and a 1.7 s panel refresh
// interleaved on every page turn. Whether that sharing survives hundreds of
// turns is the question the reader plan depends on, and the only place it can
// be answered is here, on hardware.
//
// The pages were dithered on a PC (tools/make_tbk.py), so a turn is seek,
// read, blit: no decoder, no zip, and the panel's own refresh is the floor on
// how fast a page can turn.
#pragma once
#include "book_thumbs.h"
#include "recents.h"
#include "shelf.h"
#include "tools_ui.h"

namespace bookui {
// The page view has no chrome at all -- a page of a book should be the whole
// glass. Touch is zoned instead: the outer thirds turn, the middle toggles a
// footer with the title and position, and the top-left corner is the way out,
// same place every other screen keeps it.
inline constexpr int TURN_W = 160;
// The list itself is shelf.h's: series folders, then books, a page at a time.
inline constexpr int LIST_Y0 = shelf::Y0, LIST_ROW_H = shelf::ROW_H;
inline constexpr int MAX_BOOKS = shelf::MAX_ITEMS;
// A grey page read a band at a time: 480 pixels at two bits is 120 bytes a
// row, and forty rows is 4,800 -- small enough to be a fixed buffer, big
// enough that a page is twenty reads rather than eight hundred.
inline constexpr int GREY_ROW_BYTES = 120, GREY_BAND_ROWS = 40;
// One band, shared by the two places that read one: drawing a grey page
// through the canvas and building a grey book's cover. Neither ever runs
// while the other is, and two copies would be 4.8 KB of BSS spent on saying
// so twice.
inline uint8_t g_greyBand[GREY_BAND_ROWS * GREY_ROW_BYTES];
}  // namespace bookui

class BookTool : public ToolApp {
 public:
  const char* title() const override { return "BOOKS"; }

  ~BookTool() override {
    // Toybox destroys apps on the way out; the card must not stay powered
    // behind our back, and the panel is re-initialised by the close, which is
    // why leaving an app always repaints in full.
    if (_open && _host) host().bookClose();
    free(_pageBuf);
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _screen = Screen::List;
    _open = false;
    _note = nullptr;
    snprintf(_dir, sizeof(_dir), "%s", shelf::TOP);
    reload();
    // The page buffer is NOT taken here. It used to be, at the grey size, for
    // every book on the shelf whether or not one was ever opened -- 96 KB of
    // contiguous heap, which is the largest single allocation the firmware
    // makes, requested straight after a directory listing that had just
    // allocated and freed several KB of its own. On a device with no PSRAM
    // that is how you end up with plenty of free heap and no block big enough
    // to put a page in. It is taken in openBook now, sized to the book.
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
      c.textCentered(c.width() / 2, 300, "no books on the card", TS_LARGE, true);
      c.textCentered(c.width() / 2, 348, "make .tbk files with tools/make_tbk.py", TS_SMALL, true);
      c.textCentered(c.width() / 2, 376, "and put them in /books", TS_SMALL, true);
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
      char sub[64];
      const uint32_t at = savedPage(b);
      if (at > 0)
        snprintf(sub, sizeof(sub), "%lu pages  ·  at page %lu", (unsigned long)_books[b].pages,
                 (unsigned long)(at + 1));
      else
        snprintf(sub, sizeof(sub), "%lu pages  ·  new", (unsigned long)_books[b].pages);
      c.text(24, y + 44, sub, TS_SMALL, true);
      if (shelf::rowSep(k, idx, total))
        c.fillRect(16, y + shelf::ROW_H - 6, c.width() - 32, 1, true);
    }
    shelf::drawPager(c, _lpage, total);
    c.textCentered(c.width() / 2, 770, _note ? _note : "the card stays in while you read",
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

    // The page view. Corner out, thirds turn, middle toggles the footer.
    if (x < 110 && y < 50) {
      leaveBook();
      return;
    }
    const bool leftZone = x < bookui::TURN_W;
    const bool rightZone = x >= host().canvas().width() - bookui::TURN_W;
    if (!leftZone && !rightZone) {
      _chrome = !_chrome;
      host().beep(0);
      // A grey page cannot take a partial band -- toggling chrome swaps the
      // whole page between the grey waveform and the dithered canvas path.
      if (_books[_cur].bpp == 2)
        showPage();
      else
        host().refresh(false);  // the footer band is the only change
      return;
    }
    // In a right-to-left book the "next" page is on the left, which is the
    // entire difference between manga and everything else.
    const bool forward = _books[_cur].rtl ? leftZone : rightZone;
    turn(forward ? 1 : -1);
  }

  // The side buttons page too: DOWN is always forward, whatever the reading
  // direction, matching the flashcards' "DOWN proceeds". The power button's
  // short press closes the book -- the one physical way out, for a hand
  // holding the device by its edge with no thumb free for the corner.
  bool onButton(SideBtn b) override {
    if (_screen != Screen::Page) return false;
    if (b == SideBtn::Ok) {
      leaveBook();
      return true;
    }
    turn(b == SideBtn::Down ? 1 : -1);
    return true;
  }

  // The hub's recently-read covers land here: straight into the named book.
  // The book may live in a series the list is not currently showing, so walk
  // to its folder first -- and if it is gone, the reader is left standing in
  // that folder, which is the most useful place to be looking.
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
    return false;  // card gone or file renamed: the list explains itself
  }

#ifdef TOYBOX_HOST
  int hostScreen() const { return _screen == Screen::Page ? 1 : 0; }
  uint32_t hostPage() const { return _pageNo; }
  const char* hostDir() const { return _dir; }
  int hostFolders() const { return _nf; }
  int hostItems() const { return items(); }
  int hostListPage() const { return _lpage; }
  uint32_t hostPageBufBytes() const { return _pageBufBytes; }
#endif

 private:
  enum class Screen : uint8_t { List, Loading, Page };

  // Grown, never shrunk, while a book is open; released the moment one is
  // closed. Sized in whole pages so a grey book and a B/W book can follow one
  // another without a reallocation each time.
  // A grey book asks for nothing. Its page goes to the panel a band at a time
  // straight off the card, and the canvas fallback reads bands too, so the
  // 96,000 bytes never exist in one place. A 1-bit page still arrives whole:
  // 48 KB is a block this device can find, and the blit wants it contiguous.
  static uint32_t pageBufNeed(uint8_t bpp) {
    return bpp == 2 ? 0u : ToolsHost::BOOK_PAGE_BYTES;
  }
  bool ensurePageBuf(uint8_t bpp) {
    const uint32_t need = pageBufNeed(bpp);
    if (need == 0) {
      freePageBuf();
      return true;
    }
    if (_pageBuf && _pageBufBytes >= need) return true;
    freePageBuf();
    _pageBuf = (uint8_t*)malloc(need);
    _pageBufBytes = _pageBuf ? need : 0;
    return _pageBuf != nullptr;
  }
  void freePageBuf() {
    free(_pageBuf);
    _pageBuf = nullptr;
    _pageBufBytes = 0;
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
    _nf = inFolder() ? 0 : host().shelfFolders(_folders, shelf::MAX_FOLDERS, ".tbk");
    if (_nf < 0) _nf = 0;
    _n = host().bookList(_books, bookui::MAX_BOOKS, _dir);
    _lpage = 0;
  }

  void openFolder(int i) {
    snprintf(_dir, sizeof(_dir), "%s/%s", shelf::TOP, _folders[i].name);
    _note = nullptr;
    reload();
    host().beep(0);
    host().refresh(true);
  }

  // Position keys are 4-byte FNV hashes of the file name: "b" + 8 hex chars
  // fits NVS's 15-char key limit with room to spare, and survives renames of
  // nothing but itself, which is the deal position-by-filename always was.
  void posKey(int i, char* out) const {
    uint32_t h = 2166136261u;
    for (const char* p = _books[i].file; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    snprintf(out, 12, "b%08lx", (unsigned long)h);
  }

  uint32_t savedPage(int i) {
    char k[12];
    posKey(i, k);
    const uint32_t at = prefs().getUInt(k, 0);
    return at < _books[i].pages ? at : 0;
  }

  // A 1-bit page is pulled into the buffer the blit needs. A grey page is not
  // pulled anywhere -- showPage streams it -- so all this does is confirm the
  // card will still answer for it, which is what the old read was really
  // telling us when it failed.
  // Page 0 of a grey book, banded into the thumbnail builder. The builder
  // takes rows, which is exactly what a band is a stack of, so nothing here
  // needs the page whole either.
  bool makeGreyCover(const char* file) {
    bthumb::Builder b;
    if (!b.begin(host(), file, 480, 800)) return false;
    uint8_t* band = bookui::g_greyBand;
    uint8_t line[480];
    for (int y0 = 0; y0 < 800; y0 += bookui::GREY_BAND_ROWS) {
      const int rows = (800 - y0) < bookui::GREY_BAND_ROWS ? (800 - y0) : bookui::GREY_BAND_ROWS;
      if (!host().bookPageSlice(0, (uint32_t)y0 * bookui::GREY_ROW_BYTES, band,
                                (uint32_t)rows * bookui::GREY_ROW_BYTES)) {
        b.abort();
        return false;
      }
      for (int r = 0; r < rows; r++) {
        const uint8_t* src = band + (size_t)r * bookui::GREY_ROW_BYTES;
        for (int x = 0; x < 480; x++)
          line[x] = (uint8_t)(((src[x >> 2] >> (6 - 2 * (x & 3))) & 3) * 85);
        b.row(y0 + r, line, 480);
      }
    }
    return b.finish();
  }

  bool readCurrentPage() {
    if (_cur < 0) return false;
    if (_books[_cur].bpp == 2) {
      uint8_t probe[4];
      return host().bookPageSlice(_pageNo, 0, probe, sizeof(probe));
    }
    return _pageBuf && host().bookPage(_pageNo, _pageBuf);
  }

  void openBook(int i) {
    // A buffer for THIS book: 48 KB for one bit, 96 KB for grey. Asking for
    // the grey size always was asking for twice what most books need.
    if (!ensurePageBuf(_books[i].bpp)) {
      // And when it cannot be had, say so WITH THE NUMBERS. This used to be a
      // bare `return`, then a sentence with no figures in it -- which read
      // like a diagnosis and was only a guess. What the allocator was asked
      // for, and what it had at that instant, is the whole of the evidence,
      // and it is free to print.
      snprintf(_noteBuf, sizeof(_noteBuf), "wanted %lu KB - free %lu KB, biggest block %lu KB",
               (unsigned long)(pageBufNeed(_books[i].bpp) / 1024),
               (unsigned long)(host().heapFree() / 1024),
               (unsigned long)(host().heapLargest() / 1024));
      _note = _noteBuf;
      host().beep(2);
      host().refresh(true);
      return;
    }
    // The cover as the loading screen: painted before the card is touched,
    // so the open happens behind the book's own face rather than a stale list.
    _cur = i;
    _screen = Screen::Loading;
    host().refresh(true);
    if (!host().bookOpen(_books[i].file)) {
      _note = "could not open it - is the card still in?";
      _screen = Screen::List;
      host().beep(2);
      host().refresh(true);  // the failed open borrowed the bus
      return;
    }
    _cur = i;
    _open = true;
    _pageNo = savedPage(i);
    _chrome = false;
    recents::note(prefs(), recents::KIND_TBK, _books[i].file, _books[i].title);
    // The cover thumbnail for the hub's recently-read strip, made once while
    // the bus is already up: the file's own cover if the converter put one
    // there, and page 0 otherwise. Page 0 is the fallback rather than the
    // rule because it is only the cover by accident -- a trimmed scan starts
    // at the story, and a grey test card starts at a test card.
    // A build that fails leaves have() false and is tried again next time,
    // which is right for this reader: nothing here decodes, so every failure
    // is a passing one. See makeAndSave.
    if (!bthumb::have(_books[i].file)) {
      bool made = false;
      if (_books[i].cover) {
        // The embedded cover is 48,000 bytes of one bit and wants a buffer
        // for exactly one call. A grey book has none, so borrow one and hand
        // it straight back rather than carrying it all session.
        uint8_t* tmp = _pageBuf ? _pageBuf : (uint8_t*)malloc(ToolsHost::BOOK_PAGE_BYTES);
        if (tmp && host().bookCover(tmp))
          made = bthumb::makeAndSave(host(), _books[i].file, tmp, 1);
        if (tmp && tmp != _pageBuf) free(tmp);
      }
      if (!made) {
        made = _books[i].bpp == 2 ? makeGreyCover(_books[i].file)
                                  : (_pageBuf && host().bookPage(0, _pageBuf) &&
                                     bthumb::makeAndSave(host(), _books[i].file, _pageBuf, 1));
      }
      if (!made) _note = "the cover could not be made - it will try again";
    }
    // ...and, if the sleeping panel is set to wear a cover, this book's goes
    // into flash now. After the builder above, so the very first open of a
    // book still gets one.
    bthumb::noteForLock(host(), _books[i].file);
    if (!readCurrentPage()) {
      leaveBook();
      return;
    }
    _screen = Screen::Page;
    host().beep(1);
    showPage();
  }

  // One place decides how a page reaches the glass. A 2-bit page goes through
  // the host's grey waveform when it can -- unless the footer is up, because
  // chrome has to be drawn over the page and only the canvas can do that. A
  // host with no grey (guests, the harness) falls through to the canvas path,
  // where render() dithers the page down to 1-bit.
  void showPage() {
    if (_books[_cur].bpp == 2 && !_chrome) {
      // Streamed off the card first. bookShowGrey (the whole page in one
      // buffer) is kept for a host that only has that, and is only reachable
      // when a buffer exists -- which for a grey book it now never does.
      if (host().bookShowGreyPaged(_pageNo)) return;
      if (_pageBuf && host().bookShowGrey(_pageBuf)) return;
    }
    host().refresh(true);
  }

  void leaveBook() {
    host().bookClose();  // powers the card down and re-initialises the panel
    freePageBuf();       // the shelf does not need 48 KB to draw a list
    _open = false;
    _screen = Screen::List;
    host().beep(0);
    host().refresh(true);  // full: the controller's RAM was just reset
  }

  void turn(int dir) {
    if (_cur < 0) return;
    const int64_t want = (int64_t)_pageNo + dir;
    if (want < 0 || want >= (int64_t)_books[_cur].pages) {
      host().beep(2);  // the cover and the back cover are where turning stops
      return;
    }
    const uint32_t was = _pageNo;
    _pageNo = (uint32_t)want;
    if (!readCurrentPage()) {
      _pageNo = was;
      _note = "the card stopped answering";
      leaveBook();
      return;
    }
    char k[12];
    posKey(_cur, k);
    prefs().putUInt(k, _pageNo);
    host().beep(0);
    // Full refresh either way: a page of a book changes every region of the
    // panel, and partials would stack ghosts of the last page under this one.
    showPage();
  }

  void renderPage(ToolsCanvas& c) {
    // A grey book has no buffer at all; it draws from the card below.
    if (!_pageBuf && !(_cur >= 0 && _books[_cur].bpp == 2)) return;
    if (_cur >= 0 && _books[_cur].bpp == 2) {
      // The 1-bit stand-in for a grey page: black and white pass through, and
      // the two mids become an ordered 2x2 dither -- three dots in four for
      // dark grey, one in four for light. This is what guests and the preview
      // harness always see, and what the device shows under the footer.
      //
      // Read a band at a time, like the waveform path: a grey page is 96,000
      // bytes and this reader no longer holds one.
      uint8_t* band = bookui::g_greyBand;
      for (int y0 = 0; y0 < 800; y0 += bookui::GREY_BAND_ROWS) {
        const int rows = (800 - y0) < bookui::GREY_BAND_ROWS ? (800 - y0) : bookui::GREY_BAND_ROWS;
        if (!host().bookPageSlice(_pageNo, (uint32_t)y0 * bookui::GREY_ROW_BYTES, band,
                                  (uint32_t)rows * bookui::GREY_ROW_BYTES))
          break;
        for (int r = 0; r < rows; r++) {
          const uint8_t* row = band + (size_t)r * bookui::GREY_ROW_BYTES;
          const int y = y0 + r;
          for (int x = 0; x < 480; x++) {
            const uint8_t lv = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
            bool black;
            switch (lv) {
              case 0: black = true; break;
              case 1: black = !((x & 1) && (y & 1)); break;
              case 2: black = !(x & 1) && !(y & 1); break;
              default: black = false; break;
            }
            if (black) c.fillRect(x, y, 1, 1, true);
          }
        }
      }
    } else {
    // The blit: same convention as the framebuffer, so this is a copy through
    // the canvas -- rotation and the board flips still apply. Runs of white
    // skip a byte at a time, which is most of any page.
    for (int y = 0; y < 800; y++) {
      const uint8_t* row = _pageBuf + (size_t)y * 60;
      for (int xb = 0; xb < 60; xb++) {
        const uint8_t v = row[xb];
        if (v == 0xFF) continue;
        for (int k = 0; k < 8; k++)
          if (!(v & (0x80 >> k))) c.fillRect(xb * 8 + k, y, 1, 1, true);
      }
    }
    }
    if (_chrome) {
      // The footer: title and where you are, on a plate the page shows through
      // around. Tapping the middle again takes it away.
      c.fillRect(0, 744, c.width(), 56, false);
      c.fillRect(0, 744, c.width(), 2, true);
      char pos[24];
      snprintf(pos, sizeof(pos), "%lu / %lu", (unsigned long)(_pageNo + 1),
               (unsigned long)_books[_cur].pages);
      const int pw = c.textWidth(pos, TS_MED);
      c.textClipped(16, 758, c.width() - 32 - pw - 12, _books[_cur].title, TS_MED, true, true);
      c.text(c.width() - 16 - pw, 758, pos, TS_MED, true);
    }
  }

  Screen _screen = Screen::List;
  char _dir[128] = "/books";
  ToolsHost::ShelfFolder _folders[shelf::MAX_FOLDERS];
  int _nf = 0;
  int _lpage = 0;
  ToolsHost::BookInfo _books[bookui::MAX_BOOKS];
  int _n = -1;
  int _cur = -1;
  uint32_t _pageNo = 0;
  bool _open = false;
  bool _chrome = false;
  uint8_t* _pageBuf = nullptr;
  uint32_t _pageBufBytes = 0;
  const char* _note = nullptr;
  char _noteBuf[80] = {};
};
