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
#include "tools_ui.h"

namespace bookui {
// The page view has no chrome at all -- a page of a book should be the whole
// glass. Touch is zoned instead: the outer thirds turn, the middle toggles a
// footer with the title and position, and the top-left corner is the way out,
// same place every other screen keeps it.
inline constexpr int TURN_W = 160;
inline constexpr int LIST_Y0 = 64, LIST_ROW_H = 88;
inline constexpr int MAX_BOOKS = 8;
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
    _n = h.bookList(_books, bookui::MAX_BOOKS);
    // Sized for a grey page; a B/W book simply uses the front half.
    if (!_pageBuf) _pageBuf = (uint8_t*)malloc(ToolsHost::BOOK_PAGE_BYTES_GREY);
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
    host().topBar(title());
    if (_n < 0) {
      c.textCentered(c.width() / 2, 320, "no card found", TS_LARGE, true);
      c.textCentered(c.width() / 2, 364, "is one in the slot?", TS_MED, true);
      return;
    }
    if (_n == 0) {
      c.textCentered(c.width() / 2, 300, "no books on the card", TS_LARGE, true);
      c.textCentered(c.width() / 2, 348, "make .tbk files with tools/make_tbk.py", TS_SMALL, true);
      c.textCentered(c.width() / 2, 376, "and put them in /books", TS_SMALL, true);
      return;
    }
    for (int i = 0; i < _n; i++) {
      const int y = bookui::LIST_Y0 + i * bookui::LIST_ROW_H;
      c.text(24, y + 10, _books[i].title, TS_MED, true, true);
      char sub[64];
      const uint32_t at = savedPage(i);
      if (at > 0)
        snprintf(sub, sizeof(sub), "%lu pages  ·  at page %lu", (unsigned long)_books[i].pages,
                 (unsigned long)(at + 1));
      else
        snprintf(sub, sizeof(sub), "%lu pages  ·  new", (unsigned long)_books[i].pages);
      c.text(24, y + 44, sub, TS_SMALL, true);
      c.fillRect(16, y + bookui::LIST_ROW_H - 6, c.width() - 32, 1, true);
    }
    c.textCentered(c.width() / 2, 776, _note ? _note : "the card stays in while you read",
                   TS_SMALL, true);
  }

  void onTap(int x, int y) override {
    if (_screen == Screen::List) {
      if (host().isBackTap(x, y)) {
        host().goHub();
        return;
      }
      if (_n <= 0) return;
      const int i = (y - bookui::LIST_Y0) / bookui::LIST_ROW_H;
      if (y < bookui::LIST_Y0 || i < 0 || i >= _n) return;
      openBook(i);
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
  bool openDirect(const char* file) override {
    for (int i = 0; i < _n; i++)
      if (strcmp(_books[i].file, file) == 0) {
        openBook(i);
        return _open;
      }
    return false;  // card gone or file renamed: the list explains itself
  }

#ifdef TOYBOX_HOST
  int hostScreen() const { return _screen == Screen::Page ? 1 : 0; }
  uint32_t hostPage() const { return _pageNo; }
#endif

 private:
  enum class Screen : uint8_t { List, Loading, Page };

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

  void openBook(int i) {
    if (!_pageBuf) return;
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
    // The cover thumbnail for the hub's recently-read strip: page 0 shrunk
    // into flash, made once while the bus is already up. One extra page read
    // on the first-ever open of a book; nothing on every open after.
    if (!bthumb::have(_books[i].file) && host().bookPage(0, _pageBuf))
      bthumb::makeAndSave(host(), _books[i].file, _pageBuf, _books[i].bpp);
    if (!host().bookPage(_pageNo, _pageBuf)) {
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
    if (_books[_cur].bpp == 2 && !_chrome && host().bookShowGrey(_pageBuf)) return;
    host().refresh(true);
  }

  void leaveBook() {
    host().bookClose();  // powers the card down and re-initialises the panel
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
    if (!host().bookPage((uint32_t)want, _pageBuf)) {
      _note = "the card stopped answering";
      leaveBook();
      return;
    }
    _pageNo = (uint32_t)want;
    char k[12];
    posKey(_cur, k);
    prefs().putUInt(k, _pageNo);
    host().beep(0);
    // Full refresh either way: a page of a book changes every region of the
    // panel, and partials would stack ghosts of the last page under this one.
    showPage();
  }

  void renderPage(ToolsCanvas& c) {
    if (!_pageBuf) return;
    if (_cur >= 0 && _books[_cur].bpp == 2) {
      // The 1-bit stand-in for a grey page: black and white pass through, and
      // the two mids become an ordered 2x2 dither -- three dots in four for
      // dark grey, one in four for light. This is what guests and the preview
      // harness always see, and what the device shows under the footer.
      for (int y = 0; y < 800; y++) {
        for (int x = 0; x < 480; x++) {
          const uint32_t i = (uint32_t)y * 480 + x;
          const uint8_t lv = (_pageBuf[i >> 2] >> (6 - 2 * (i & 3))) & 3;
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
      c.text(16, 758, _books[_cur].title, TS_MED, true, true);
      char pos[24];
      snprintf(pos, sizeof(pos), "%lu / %lu", (unsigned long)(_pageNo + 1),
               (unsigned long)_books[_cur].pages);
      c.text(c.width() - 16 - c.textWidth(pos, TS_MED), 758, pos, TS_MED, true);
    }
  }

  Screen _screen = Screen::List;
  ToolsHost::BookInfo _books[bookui::MAX_BOOKS];
  int _n = -1;
  int _cur = -1;
  uint32_t _pageNo = 0;
  bool _open = false;
  bool _chrome = false;
  uint8_t* _pageBuf = nullptr;
  const char* _note = nullptr;
};
