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
#include "bookmarks.h"
#include "help.h"
#include "lock_image.h"
#include "reader_menu.h"
#include "recents.h"
#include "shelf.h"
#include "tools_ui.h"

namespace epubui {
inline constexpr int MARGIN = 24;
inline constexpr int TOP = 18;
// The footer band is measured from the bottom of whichever canvas is live:
// the page reads at any of the four rotations now, and 480-tall landscape
// needs the band as much as 800-tall portrait does.
inline constexpr int BOTTOM_INSET = 30, FOOT_H = 56;
// The reader's own type, chosen from the panel. Three sizes and three leadings,
// and nothing SMALLER than the body size the whole firmware settled on: 24 px
// is 2.6 mm on this 235 DPI panel, already about seven point, and the one time
// this project shipped smaller text than that nobody could read the screen.
inline constexpr int SIZES = 3, LEADS = 3;
inline TSize sizeAt(int i) { return i <= 0 ? TS_MED : (i == 1 ? TS_LARGE : TS_HUGE); }
inline const char* sizeName(int i) { return i <= 0 ? "normal" : (i == 1 ? "large" : "largest"); }
inline const char* leadName(int i) { return i <= 0 ? "tight" : (i == 1 ? "normal" : "airy"); }
inline int leadAir(int i) { return i <= 0 ? 6 : (i == 1 ? 10 : 18); }
inline constexpr int PARA_GAP = 14;
// Enough for the smallest type at the tightest leading: 752 px of page over a
// 30 px step is 25 lines, and a page that runs out of Line slots stops early
// with a gap at the bottom that nothing explains.
inline constexpr int MAX_LINES = 28;
inline constexpr int TURN_W = 160;   // tap thirds, same as the .tbk reader
// The list itself is shelf.h's: series folders, then books, a page at a time.
inline constexpr int LIST_Y0 = shelf::Y0, LIST_ROW_H = shelf::ROW_H;
inline constexpr int MAX_BOOKS = shelf::MAX_ITEMS;
inline constexpr int MAX_PAGES = 2048;  // per chapter; ~90x any real chapter
// A page-start offset with its top bit set is an illustration's page. The
// offsets are visible-codepoint counts, so the bit is free: a chapter with two
// billion characters in it does not exist. The flag is needed because an <img>
// adds no codepoints, which leaves a picture and the text after it sharing one
// offset -- and a back-turn that only knows the offset would land on the text
// both times and the picture never.
inline constexpr uint32_t PAGE_IMG = 0x80000000u;
inline constexpr uint32_t pageOff(uint32_t v) { return v & ~PAGE_IMG; }
// One band of a .tbi as it is blitted: 16 rows, so a 48 KB picture crosses the
// bus in 50 reads and never needs 48 KB of anything.
inline constexpr int IMG_BAND_ROWS = 16;
inline uint8_t g_imgBand[IMG_BAND_ROWS * (480 / 8)];
// The book's contents, read once per book. Static because it is 3 KB and one
// book is open at a time; sized so that a light novel's chapter list fits and
// a reference work's is truncated rather than refused.
inline constexpr int MAX_TOC = 64;
inline epubc::Book::TocEntry g_toc[MAX_TOC];
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
    _help = !help::suppressed(prefs(), "ep");
    _note = nullptr;
    snprintf(_dir, sizeof(_dir), "%s", shelf::TOP);
    reload();
    _size = (uint8_t)prefs().getUInt("rd_size", 0);
    _lead = (uint8_t)prefs().getUInt("rd_lead", 1);
    _face = (uint8_t)prefs().getUInt("rd_face", 0);
    if (_face >= h.typefaceCount()) _face = 0;
    _rot = (uint8_t)prefs().getUInt("rd_rot", 0);
    if (_rot != 1 && _rot != 3) _rot = 0;
    if (_size >= epubui::SIZES) _size = 0;
    if (_lead >= epubui::LEADS) _lead = 1;
    if (!_lut) _lut = (uint32_t*)malloc(sizeof(uint32_t) * epubui::MAX_PAGES);
  }

  void render(ToolsCanvas& c) override {
    if (_screen == Screen::Loading) {
      bthumb::drawLoading(host(), c, _books[_cur].file, _books[_cur].title, _freshCover);
      return;
    }
    if (_screen == Screen::Page) {
      if (_picking) {
        renderPick(c);
        return;
      }
      if (_menu != rmenu::Page::None) {
        renderMenu(c);
        return;
      }
      renderPage(c);
      return;
    }
    // Inside a series the bar carries the series name, because that is the
    // only thing on the screen that says where you are.
    host().topBar(inFolder() ? seriesName() : title(), true, inFolder() ? title() : "HUB");
    if (_help) {
      help::render(c, help::EPUB, "HOW TO READ");
      return;
    }
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
      if (host().isHelpTap(x, y)) {
        _help = !_help;
        host().beep(1);
        paint();
        return;
      }
      if (_help) {
        const help::Tap t = help::hit(x, y);
        if (t == help::Tap::None) return;
        if (t == help::Tap::Never) help::suppress(prefs(), "ep");
        _help = false;
        host().beep(1);
        paint();
        return;
      }
      const int total = items();
      if (total <= 0) return;
      const int pages = shelf::pageCount(total);
      if (y >= shelf::PAGER_Y && pages > 1) {
        if (_lpage > 0 && shelf::prevRect().hit(x, y)) {
          _lpage--;
          host().beep(0);
          paint();
        } else if (_lpage < pages - 1 && shelf::nextRect().hit(x, y)) {
          _lpage++;
          host().beep(0);
          paint();
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
    if (_picking) {
      pickTap(x, y);
      return;
    }
    if (_menu != rmenu::Page::None) {
      menuTap(x, y);
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

  // DOWN is always forward, matching the .tbk reader. The power button's short
  // press opens the panel -- contents, marks, type, and the way out -- because
  // a hand holding the device by its edge has one control and the book has
  // more than one thing to ask of it. A hold still powers off; that is
  // main.cpp's, not ours.
  bool onButton(SideBtn b) override {
    if (_screen != Screen::Page) return false;
    if (b == SideBtn::Ok) {
      if (_picking) {
        pickCancel();
        return true;
      }
      if (_menu == rmenu::Page::None)
        menuOpen();
      else if (_menu == rmenu::Page::Root)
        menuClose();
      else
        menuBack();
      return true;
    }
    if (_menu != rmenu::Page::None) {
      // In a list, the side buttons page it; on the root they do nothing,
      // because there is nothing under the five buttons to scroll to.
      menuScroll(b == SideBtn::Down ? 1 : -1);
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
  bool hostFreshCover() const { return _freshCover; }
  // The page's words, joined -- so a guard can prove that no word vanishes at
  // a page boundary by reading the same chapter under two layouts.
  int hostPageJoin(char* out, int cap) const {
    int n = 0;
    for (int i = 0; i < _lineN && n < cap - 2; i++) {
      if (n) out[n++] = ' ';
      const int wl = (int)strlen(_lines[i].t);
      const int take = wl < cap - 1 - n ? wl : cap - 1 - n;
      memcpy(out + n, _lines[i].t, (size_t)take);
      n += take;
    }
    out[n] = 0;
    return n;
  }
  void hostSetStyle(uint8_t size) {
    _size = size;
    restyle();
  }
  void hostGoto(int spine, uint32_t off) { gotoPlace(spine, off); }
  int hostSpine() const { return _spine; }
  int hostPage() const { return _page; }
  uint32_t hostPageOffset() const { return curOffset(); }
  const char* hostPageImage() const { return _pageImage; }
  int hostMenu() const { return (int)_menu; }
  bool hostPicking() const { return _picking; }
  const char* hostPhrase() const { return _phrase; }
  int hostLineY(int i) const { return i < _lineN ? _lines[i].y : -1; }
  const char* hostMarkLabel(int i) const { return i < _nmarks ? _marks[i].label : ""; }
  int hostMarkCount() const { return _nmarks; }
  int hostTextSize() const { return _size; }
  int hostTocCount() { return _ntoc; }
  // The contents fallback -- chapters named by their own first words -- only
  // happens in books with no navigation document, and the fixture has one. So
  // the harness throws it away to walk the other path.
  void hostDropToc() { _ntoc = 0; }
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

  // Where the reader is, in the only terms that survive a change of type or a
  // change of firmware: the visible-codepoint offset of the page's first word.
  uint32_t curOffset() const { return _page < _lutN ? epubui::pageOff(_lut[_page]) : 0; }

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
    p.offset = epubui::pageOff(_lut[_page]);
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
    _freshCover = false;
    host().refresh(true);
    const bool hadCover = bthumb::have(_books[i].file);
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
    _menu = rmenu::Page::None;
    _picking = false;
    _ntoc = -1;
    _nmarks = 0;
    recents::note(prefs(), recents::KIND_EPUB, _books[i].file, _books[i].title);

    // The cover thumbnail for the hub's recently-read strip: decoded once on
    // a book's first open (a second or two for a big JPEG), never again --
    // and a cover that will not decode is marked so it is never retried.
    // A cover the owner put beside the book wins over the one inside it: they
    // chose it, on a machine that could do the picture justice.
    if (!bthumb::coverFromSidecar(host(), _books[i].file) &&
        !bthumb::have(_books[i].file) && !bthumb::failed(_books[i].file)) {
      bool transient = false;
      if (!epubcov::makeThumb(host(), _book, _books[i].file, &transient) && !transient)
        bthumb::markFailed(_books[i].file);
    }
    // A cover that just came into existence goes into the plate's frame, so
    // the first open -- the slowest one, behind a JPEG decode -- shows its
    // progress where the empty frame promised it. One partial refresh; later
    // opens lead with the full-bleed cover instead.
    if (!hadCover && bthumb::have(_books[i].file)) {
      _freshCover = true;
      host().refresh(false);
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
    host().setCanvasRotation(_rot);  // gotoPlace lays out at this width
    _rotLaid = _rot;
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
    _menu = rmenu::Page::None;
    host().setCanvasRotation(0);  // the shelf, like everything else, is portrait
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
    _pendImageValid = false;
    _pageImage[0] = 0;
    _streamLost = false;  // the chapter stream is whole again
    _atEnd = false;
    return true;
  }

  // Lays out the next page from the stream into _lines. Returns the number of
  // words placed (0 means the chapter had nothing left).
  static const char* faceName(int f) {
    return f == 1 ? "Literata" : f == 2 ? "Atkinson" : "DejaVu";
  }

  // 0 portrait, 1 and 3 the two landscapes. Which of the two you want depends
  // on which hand holds the device, so both are offered.
  static uint8_t nextRot(uint8_t r) { return r == 0 ? 1 : r == 1 ? 3 : 0; }
  static const char* rotName(uint8_t r) {
    return r == 1 ? "landscape" : r == 3 ? "landscape, flipped" : "portrait";
  }

  // The panel's screens are drawn portrait; the page is drawn at the chosen
  // rotation. The reflow decision compares against the rotation the CURRENT
  // LAYOUT was made at -- not the canvas, which is always portrait while the
  // menu is up. Comparing the canvas let a landscape layout be drawn on a
  // portrait page when the rotation was cycled all the way round with the
  // panel open; the overflow detector caught it before hardware could.
  void applyRot(uint8_t r) {
    host().setCanvasRotation(r);
    if (((_rotLaid ^ r) & 1) != 0 && _open) restyle();
    _rotLaid = r;
  }

  // Sets the reading face for a stretch of measuring or drawing and puts the
  // previous one back, so nesting works and nothing leaks into the UI.
  struct FaceScope {
    ToolsHost& h;
    int prev;
    FaceScope(ToolsHost& hh, int f) : h(hh), prev(hh.typeface()) { h.setTypeface(f); }
    ~FaceScope() { h.setTypeface(prev); }
  };

  int layoutPage() {
    FaceScope fs(host(), _face);
    ToolsCanvas& c = host().canvas();
    const TSize ts = epubui::sizeAt(_size);
    const int step = c.textHeight(ts) + epubui::leadAir(_lead);
    const int lineW = c.width() - 2 * epubui::MARGIN;
    // What is on the glass right now, kept in case this call finds nothing.
    // A chapter announces its end by a layout that places nothing, and until
    // the caller decides where to go next, the page it was called on is still
    // the page being read. Blanking it here is what turned "back one page" at
    // a chapter boundary into a blank sheet -- and an illustration-only
    // chapter, where the second layout always comes back empty, into a page
    // that erased itself the moment it was reached.
    const int keepLines = _lineN;
    char keepImage[sizeof(_pageImage)];
    memcpy(keepImage, _pageImage, sizeof(keepImage));
    _lineN = 0;
    int y = epubui::TOP;
    char cur[256];
    int curLen = 0;
    int curW = 0;
    int placed = 0;
    uint32_t pageStart = 0;
    bool started = false;
    // An illustration gets the whole glass. A picture squeezed between two
    // paragraphs on a 480x800 panel is worth less than the paragraphs it
    // displaced, and this is how the books themselves are laid out.
    _pageImage[0] = 0;
    if (_pendImageValid) {
      snprintf(_pageImage, sizeof(_pageImage), "%s", _pendImage);
      _pendImageValid = false;
      pageStart = _pendImageOff;
      if (_lutN < epubui::MAX_PAGES) _lut[_lutN++] = pageStart | epubui::PAGE_IMG;
      return 1;
    }
    const int spaceW = c.textWidth(" ", ts) > 0 ? c.textWidth(" ", ts) : 8;

    auto flushLine = [&]() -> bool {  // false: the page is full
      if (curLen == 0) return true;
      if (y + step > c.height() - epubui::BOTTOM_INSET || _lineN >= epubui::MAX_LINES) return false;
      memcpy(_lines[_lineN].t, cur, (size_t)curLen);
      _lines[_lineN].t[curLen] = 0;
      _lines[_lineN].y = (short)y;
      _lineN++;
      y += step;
      curLen = 0;
      curW = 0;
      return true;
    };
    auto roomForLine = [&]() {
      return y + step <= c.height() - epubui::BOTTOM_INSET && _lineN < epubui::MAX_LINES;
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
      if (tok == epubc::TOK_IMAGE) {
        if (!started) {
          // A fresh page: this one is the picture.
          snprintf(_pageImage, sizeof(_pageImage), "%s", _book.imageName());
          pageStart = off;
          placed++;
          break;
        }
        // The page already has text, so the picture opens the next one. Note
        // that both pages then start at the same offset -- an image adds no
        // codepoints -- which resume handles the way it handles any tie.
        snprintf(_pendImage, sizeof(_pendImage), "%s", _book.imageName());
        _pendImageOff = off;
        _pendImageValid = true;
        flushLine();
        _atEnd = false;
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
      const int ww = c.textWidth(w, ts);
      if (!started) {
        pageStart = off;
        started = true;
      }
      // `placed` answers "did any word begin landing on this page": the first
      // word of a fresh page always lands (split if huge), so a word carried
      // whole to the next page only happens on a page that already has text.
      placed++;
      const int need = curLen ? curW + spaceW + ww : ww;
      // A line is only ever STARTED if there is room for it to land. Room
      // used to be checked when the line was flushed, a word too late: a page
      // that filled while a line was still being assembled pended the one
      // word in hand and silently dropped every word already gathered into
      // the line. On hardware, mid-novel: "Good. Now that left the other
      // two." lost everything but "two." across a page turn.
      if (!curLen && !roomForLine()) {
        strcpy(_pend, w);
        _pendOff = off;
        _pendValid = true;
        break;
      }
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
      // Does not fit beside the current line. The flush cannot fail any more
      // -- the line's room was reserved when its first word landed -- but the
      // belt stays: if it somehow does, the whole unfinished line would be
      // the loss, and the word in hand is the least of it.
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
          if (c.textWidth(probe, ts) > lineW) break;
          fitBytes += step;
        }
        if (fitBytes == 0) fitBytes = 1;  // a glyph wider than the line still moves on
        memcpy(cur, rest, (size_t)fitBytes);
        curLen = fitBytes;
        memcpy(probe, rest, (size_t)fitBytes);
        probe[fitBytes] = 0;
        curW = c.textWidth(probe, ts);
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
      if (_lutN < epubui::MAX_PAGES)
        _lut[_lutN++] = pageStart | (_pageImage[0] ? epubui::PAGE_IMG : 0u);
      if (_atEnd && !_pendValid) _chapterPages = _lutN;
    } else {
      if (_atEnd) _chapterPages = _lutN;
      _lineN = keepLines;  // nothing new: the page that was showing still is
      memcpy(_pageImage, keepImage, sizeof(_pageImage));
    }
    return placed;
  }

  // Replays the chapter from the top and lays out exactly the page whose
  // start offset is _lut[pageIdx]. Layout is deterministic from the word
  // stream, so the replayed page breaks identically to when it was first
  // seen. Costs one re-inflate of the chapter, invisible next to the panel.
  bool showPageAt(int pageIdx) {
    const uint32_t target = epubui::pageOff(_lut[pageIdx]);
    const bool wantImage = (_lut[pageIdx] & epubui::PAGE_IMG) != 0;
    // Pictures carry no codepoints, so a run of them shares one offset with
    // the text that follows. Counting how many of this offset's pictures are
    // already behind us says which one this page is.
    int skipImages = 0;
    for (int j = 0; j < pageIdx; j++)
      if ((_lut[j] & epubui::PAGE_IMG) && epubui::pageOff(_lut[j]) == target) skipImages++;
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
      if (wantImage && tok == epubc::TOK_IMAGE && off >= target) {
        if (skipImages > 0) {
          skipImages--;
          continue;
        }
        snprintf(_pendImage, sizeof(_pendImage), "%s", _book.imageName());
        _pendImageOff = off;
        _pendImageValid = true;
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
        // Nothing left in this chapter. That is only an EMPTY chapter if it
        // never produced a page at all -- a chapter holding one illustration
        // and no words produces exactly one page and then comes back empty,
        // and reading "empty" as "skip to the next chapter" is what made the
        // cover page, the gallery and the character art unreachable.
        if (_lutN > 0) {
          _page = _lutN - 1;
          return true;
        }
        if (spineIdx + 1 < _book.spineCount()) return gotoPlace(spineIdx + 1, 0);
        _lineN = 0;
        _page = 0;
        return false;
      }
      _page = _lutN - 1;
      if (_lutN >= 2 && epubui::pageOff(_lut[_lutN - 1]) > off) {
        // overshot by one: the page before this one contains `off`
        return showPageAt(_lutN - 2);
      }
      if (_atEnd && !_pendValid) return true;  // the chapter's last page
    }
  }

  // Drawing an illustration reads another entry out of the same zip, and this
  // core keeps one entry open at a time, so the chapter stream is spent by the
  // time the picture is on the glass. Rebuilding it costs one re-inflate --
  // the same one a back-turn already pays -- so it is put off until a turn
  // actually needs the stream, and never paid at all by a reader who looks at
  // a picture and puts the device down.
  void ensureStream() {
    if (!_streamLost) return;
    _streamLost = false;
    if (_page < _lutN) showPageAt(_page);
  }

  void turn(int dir) {
    if (!_open) return;
    ensureStream();
    if (dir > 0) {
      if (_pendValid || !_atEnd) {
        if (layoutPage() > 0) {
          _page = _lutN - 1;
          saveProgress();
          host().beep(0);
          paint();
          return;
        }
      }
      // end of chapter: the next one, if there is one
      if (_spine + 1 < _book.spineCount()) {
        if (gotoPlace(_spine + 1, 0)) {
          saveProgress();
          host().beep(0);
          paint();
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
      paint();
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
    paint();
  }

  // The pre-rendered picture beside an image: same path inside the zip, with a
  // "toybox/" prefix and a .tbi extension, which is what the PC app writes.
  void tbiEntryFor(const char* img, char* out, int cap) {
    char stem[200];
    snprintf(stem, sizeof(stem), "toybox/%s", img);
    char* dot = strrchr(stem, '.');
    const char* slash = strrchr(stem, '/');
    if (dot && (!slash || dot > slash)) *dot = 0;  // ".jpg" goes, a dotted folder stays
    snprintf(out, (size_t)cap, "%s.tbi", stem);
  }

  bool blobReadFull(uint8_t* dst, int n) {
    int got = 0;
    while (got < n) {
      const int r = _book.blobRead(dst + got, n - got);
      if (r <= 0) return false;
      got += r;
    }
    return true;
  }

  // Blits the picture for this page, a band at a time. The whole picture is
  // 48 KB and this device has no 48 KB to spare, so it never exists whole:
  // 16 rows arrive, 16 rows are drawn, and the buffer is reused 50 times.
  bool drawImagePage(ToolsCanvas& c) {
    char entry[224];
    tbiEntryFor(_pageImage, entry, sizeof(entry));
    // Reading another entry spends the chapter stream; ensureStream() rebuilds
    // it when a turn next needs it.
    _book.chapterClose();
    _streamLost = true;
    if (!_book.blobOpen(entry)) return false;
    if (_book.blobSize() != tbimg::FILE_SIZE) {
      _book.blobClose();
      return false;
    }
    uint8_t head[tbimg::HEADER];
    if (!blobReadFull(head, sizeof(head))) {
      _book.blobClose();
      return false;
    }
    const uint32_t magic = (uint32_t)head[0] | ((uint32_t)head[1] << 8) |
                           ((uint32_t)head[2] << 16) | ((uint32_t)head[3] << 24);
    const int w = head[4] | (head[5] << 8), h = head[6] | (head[7] << 8);
    if (magic != tbimg::MAGIC || w != tbimg::W || h != tbimg::H) {
      _book.blobClose();
      return false;  // a .tbi that is not this panel's picture: the plate, not a guess
    }
    for (int y0 = 0; y0 < tbimg::H; y0 += epubui::IMG_BAND_ROWS) {
      const int rows =
          (tbimg::H - y0) < epubui::IMG_BAND_ROWS ? (tbimg::H - y0) : epubui::IMG_BAND_ROWS;
      if (!blobReadFull(epubui::g_imgBand, rows * tbimg::STRIDE)) break;  // half a picture
      for (int r = 0; r < rows; r++) {
        const uint8_t* row = epubui::g_imgBand + (size_t)r * tbimg::STRIDE;
        for (int xb = 0; xb < tbimg::STRIDE; xb++) {
          const uint8_t v = row[xb];
          if (v == 0xFF) continue;  // a run of white, which is most of most art
          for (int k = 0; k < 8; k++)
            if (!(v & (0x80 >> k))) c.fillRect(xb * 8 + k, y0 + r, 1, 1, true);
        }
      }
    }
    _book.blobClose();
    return true;
  }

  // What an illustration looks like when the book carries no picture for it.
  // Named, because "there is a picture here and you cannot see it" is worth
  // more than a blank page, and the name is what the PC app needs to hear.
  void drawImagePlate(ToolsCanvas& c) {
    const int W = c.width();
    c.drawRect(40, 220, W - 80, 360, 2, true);
    c.textCentered(W / 2, 320, "an illustration", TS_LARGE, true);
    c.textCentered(W / 2, 372, "this book has no picture prepared for it", TS_SMALL, true);
    const char* base = strrchr(_pageImage, '/');
    c.textClipped(60, 430, W - 120, base ? base + 1 : _pageImage, TS_SMALL, true);
    c.textCentered(W / 2, 496, "the PC app adds one under toybox/", TS_SMALL, true);
  }

  // A cosmetic repaint: fast unless the owner turned that off. Never used
  // where the panel MUST be clean -- opening or closing a book releases the
  // card bus and re-initialises the controller, and those still ask for full.
  void paint() { host().refreshFast(rmenu::cleanEvery(mode())); }

  rmenu::Refresh mode() { return rmenu::refreshMode(prefs(), true); }

  // --- the panel --------------------------------------------------------------
  // Opened by the power button. Everything in here is about the book you are
  // in the middle of, which is why none of it lives in settings.

  void menuOpen() {
    _menu = rmenu::Page::Root;
    _mpage = 0;
    _nmarks = marks::load(host(), _books[_cur].file, _marks);
    // The panel is a portrait design; the page under it may not be. Stand the
    // canvas up for the menu and put the page's angle back on the way out.
    const bool turned = host().canvasRotation() != 0;
    host().setCanvasRotation(0);
    host().beep(0);
    if (turned)
      host().refresh(true);
    else
      paint();
  }

  void menuClose() {
    _menu = rmenu::Page::None;
    ensureStream();  // the contents list reads other entries out of the zip
    applyRot(_rot);  // the page's rotation, back on (and a reflow if it changed)
    host().beep(0);
    // A quarter turn changes every pixel; the partial path cannot describe it.
    if (host().canvasRotation() != 0)
      host().refresh(true);
    else
      paint();
  }

  void menuBack() {
    _menu = rmenu::Page::Root;
    _mpage = 0;
    host().beep(0);
    paint();
  }

  void menuScroll(int dir) {
    const int total = _menu == rmenu::Page::Contents  ? tocCount()
                      : _menu == rmenu::Page::Marks   ? _nmarks
                                                      : 0;
    const int pages = shelf::pageCount(total);
    const int want = _mpage + dir;
    if (total <= 0 || want < 0 || want >= pages) {
      host().beep(2);
      return;
    }
    _mpage = want;
    host().beep(0);
    paint();
  }

  // The contents, read once per book and kept until it closes. Books with no
  // usable contents fall back to their own chapters, named by the first words
  // in them -- which costs one chapter open per row shown, and only for the
  // rows actually on the screen.
  int tocCount() {
    if (_ntoc < 0) {
      _ntoc = _book.tocRead(epubui::g_toc, epubui::MAX_TOC);
      _streamLost = true;  // reading the contents spent the chapter stream
    }
    return _ntoc > 0 ? _ntoc : _book.spineCount();
  }

  // What to call row `i` of the contents list, and where it goes.
  void tocRow(int i, char* label, int cap, int& spine) {
    if (_ntoc > 0) {
      spine = epubui::g_toc[i].spine;
      snprintf(label, (size_t)cap, "%s", epubui::g_toc[i].title);
      return;
    }
    spine = i;
    // No contents in the book: the chapter says its own name, in its own first
    // words. Only the visible rows are opened, and only far enough to pull a
    // few words out of the first block.
    char taste[64] = "";
    int used = 0;
    if (_book.chapterOpen(i)) {
      char w[epubc::WORD_CAP];
      uint32_t off = 0;
      for (int k = 0; k < 6 && used < (int)sizeof(taste) - 2; k++) {
        const int t = _book.next(w, off);
        if (t == epubc::TOK_END || t == epubc::TOK_ERR) break;
        if (t != epubc::TOK_WORD) continue;
        if (used) taste[used++] = ' ';
        used += snprintf(taste + used, sizeof(taste) - used, "%s", w);
      }
      _streamLost = true;
    }
    if (taste[0])
      snprintf(label, (size_t)cap, "ch %d - %s", i + 1, taste);
    else
      snprintf(label, (size_t)cap, "chapter %d", i + 1);
  }

  void markLabel(const marks::Mark& m, char* out, int cap) {
    if (m.label[0])
      snprintf(out, (size_t)cap, "%s", m.label);
    else
      snprintf(out, (size_t)cap, "chapter %u, page %u", (unsigned)(m.spine + 1),
               (unsigned)(m.page + 1));
  }

  // --- keeping a phrase --------------------------------------------------------
  // A bookmark that says "ch 7, page 12" is a memory of nothing, and page 12
  // stops being page 12 the moment the type changes. So a mark carries the
  // words that were on the page, and the reader points at them: the page stays
  // where it is, the first tap opens the phrase, the second closes it, and a
  // third screen shows what will be kept before it is kept.

  int lineStepNow() {
    ToolsCanvas& c = host().canvas();
    return c.textHeight(epubui::sizeAt(_size)) + epubui::leadAir(_lead);
  }

  // Which word of a line a tap landed on, measured from the same string the
  // page was drawn from. -1 when the tap is past the end of the line.
  int wordAt(const char* line, int tapX) {
    ToolsCanvas& c = host().canvas();
    const TSize ts = epubui::sizeAt(_size);
    char probe[200];
    int i = 0, word = 0;
    while (line[i]) {
      int j = i;
      while (line[j] && line[j] != ' ') j++;
      const int n = j - i > (int)sizeof(probe) - 1 ? (int)sizeof(probe) - 1 : j - i;
      memcpy(probe, line, (size_t)(i + n) > sizeof(probe) - 1 ? sizeof(probe) - 1 : (size_t)(i + n));
      probe[(size_t)(i + n) > sizeof(probe) - 1 ? sizeof(probe) - 1 : (size_t)(i + n)] = 0;
      if (epubui::MARGIN + c.textWidth(probe, ts) > tapX) return word;
      while (line[j] == ' ') j++;
      i = j;
      word++;
    }
    return word > 0 ? word - 1 : -1;
  }

  // The x span of word `w` in `line`, for the underline.
  void wordSpan(const char* line, int w, int& x0, int& x1) {
    FaceScope fs(host(), _face);
    ToolsCanvas& c = host().canvas();
    const TSize ts = epubui::sizeAt(_size);
    char probe[200];
    int i = 0, word = 0;
    x0 = x1 = epubui::MARGIN;
    while (line[i]) {
      int j = i;
      while (line[j] && line[j] != ' ') j++;
      if (word == w) {
        const size_t pre = (size_t)i < sizeof(probe) ? (size_t)i : sizeof(probe) - 1;
        memcpy(probe, line, pre);
        probe[pre] = 0;
        x0 = epubui::MARGIN + c.textWidth(probe, ts);
        const size_t upto = (size_t)j < sizeof(probe) ? (size_t)j : sizeof(probe) - 1;
        memcpy(probe, line, upto);
        probe[upto] = 0;
        x1 = epubui::MARGIN + c.textWidth(probe, ts);
        return;
      }
      while (line[j] == ' ') j++;
      i = j;
      word++;
    }
  }

  // Everything from (l0,w0) to (l1,w1), joined by single spaces and cut to the
  // label's length -- a bookmark is a reminder, not a quotation.
  void buildPhrase() {
    _phrase[0] = 0;
    int used = 0;
    for (int l = _pickL0; l <= _pickL1 && l < _lineN; l++) {
      const char* line = _lines[l].t;
      int i = 0, word = 0;
      while (line[i]) {
        int j = i;
        while (line[j] && line[j] != ' ') j++;
        const bool after = l > _pickL0 || word >= _pickW0;
        const bool before = l < _pickL1 || word <= _pickW1;
        if (after && before) {
          const int room = (int)sizeof(_phrase) - 1 - used;
          if (room <= 1) {
            // Out of label: say so with an ellipsis rather than stopping mid-word.
            if (used > (int)sizeof(_phrase) - 5) used = (int)sizeof(_phrase) - 5;
            snprintf(_phrase + used, 5, "...");
            return;
          }
          if (used) _phrase[used++] = ' ';
          const int n = j - i < room - 1 ? j - i : room - 1;
          memcpy(_phrase + used, line + i, (size_t)n);
          used += n;
          _phrase[used] = 0;
        }
        while (line[j] == ' ') j++;
        i = j;
        word++;
      }
    }
  }

  void pickStart() {
    if (_pageImage[0] || _lineN == 0) {
      // A picture has no words to pick, so the page itself is the mark.
      keepMark("");
      return;
    }
    _menu = rmenu::Page::None;
    _picking = true;
    _pickL0 = _pickL1 = -1;
    _phrase[0] = 0;
    host().beep(0);
    paint();
  }

  void pickCancel() {
    _picking = false;
    _pickL0 = _pickL1 = -1;
    host().beep(2);
    paint();
  }

  void keepMark(const char* label) {
    marks::Mark m{};
    m.spine = (uint16_t)_spine;
    m.page = (uint16_t)_page;
    m.off = curOffset();
    snprintf(m.label, sizeof(m.label), "%s", label && label[0] ? label : "");
    const bool added = marks::add(_marks, _nmarks, m) >= 0;
    if (added) marks::save(host(), _books[_cur].file, _marks, _nmarks);
    _picking = false;
    _pickL0 = _pickL1 = -1;
    _menu = rmenu::Page::None;
    host().beep(added ? 1 : 2);
    paint();
  }

  // A tap while picking. The first lands the opening word, the second closes
  // the phrase and puts the confirmation up.
  void pickTap(int x, int y) {
    // The corner is NOT the way out here: the first line of the page sits at
    // the top of the glass, and a corner that swallowed it would make the
    // first word of a page unkeepable. The band at the bottom -- the one
    // saying what to do -- is the way out, and so is the power button.
    if (y >= host().canvas().height() - epubui::FOOT_H) {
      pickCancel();
      return;
    }
    const int step = lineStepNow();
    int line = -1;
    for (int i = 0; i < _lineN; i++)
      if (y >= _lines[i].y - 6 && y < _lines[i].y + step) {
        line = i;
        break;
      }
    if (line < 0) return;
    const int word = wordAt(_lines[line].t, x);
    if (word < 0) return;
    if (_pickL0 < 0) {
      _pickL0 = line;
      _pickW0 = word;
      host().beep(0);
      host().refresh(false);
      return;
    }
    // Backwards is allowed: people read backwards to find where a thought
    // started, and refusing the tap teaches nothing.
    if (line < _pickL0 || (line == _pickL0 && word < _pickW0)) {
      _pickL1 = _pickL0;
      _pickW1 = _pickW0;
      _pickL0 = line;
      _pickW0 = word;
    } else {
      _pickL1 = line;
      _pickW1 = word;
    }
    buildPhrase();
    _picking = false;
    _menu = rmenu::Page::Keep;
    host().beep(0);
    paint();
  }

  void renderPick(ToolsCanvas& c) {
    FaceScope fs(host(), _face);
    renderPage(c);
    // The chosen words, underlined. Nothing is inverted: on a page of text an
    // inverted word is a hole, and this has to be readable while it is chosen.
    if (_pickL0 >= 0) {
      int x0 = 0, x1 = 0;
      wordSpan(_lines[_pickL0].t, _pickW0, x0, x1);
      const int y = _lines[_pickL0].y + lineStepNow() - 4;
      c.fillRect(x0, y, x1 - x0, 3, true);
    }
    const int fy = c.height() - epubui::FOOT_H;
    c.fillRect(0, fy, c.width(), epubui::FOOT_H, false);
    c.fillRect(0, fy, c.width(), 2, true);
    c.textCentered(c.width() / 2, 752,
                   _pickL0 < 0 ? "tap the first word to keep" : "now tap the last word", TS_MED,
                   true);
    c.textCentered(c.width() / 2, 778, "tap this bar to stop", TS_SMALL, true);
  }

  void renderMenu(ToolsCanvas& c) {
    char buf[64];
    if (_menu == rmenu::Page::Root) {
      rmenu::Item items[6];
      items[0].label = "Contents";
      snprintf(_rootSub[0], sizeof(_rootSub[0]), "chapter %d of %d", _spine + 1,
               _book.spineCount());
      items[0].sub = _rootSub[0];
      items[1].label = "Bookmarks";
      if (_nmarks > 0)
        snprintf(_rootSub[1], sizeof(_rootSub[1]), "%d kept  -  + keeps a phrase", _nmarks);
      else
        snprintf(_rootSub[1], sizeof(_rootSub[1]), "none yet  -  + keeps a phrase");
      items[1].sub = _rootSub[1];
      items[1].plus = true;
      items[2].label = "Text";
      snprintf(_rootSub[2], sizeof(_rootSub[2]), "%s, %s spacing", epubui::sizeName(_size),
               epubui::leadName(_lead));
      items[2].sub = _rootSub[2];
      int n = 3;
      if (host().typefaceCount() > 1) {
        // A row that cycles in place, like the .tbk reader's page turns: the
        // row states the answer, so there is nowhere to go.
        items[n].label = "Typeface";
        snprintf(_rootSub[n], sizeof(_rootSub[n]), "%s  -  the page reflows", faceName(_face));
        items[n].sub = _rootSub[n];
        n++;
      }
      items[n].label = "Rotation";
      snprintf(_rootSub[n], sizeof(_rootSub[n]), "%s  -  lands when the panel closes",
               rotName(_rot));
      items[n].sub = _rootSub[n];
      n++;
      items[n].label = "Close the book";
      items[n].sub = _books[_cur].title;
      rmenu::drawRoot(host(), c, "Options", items, n + 1);
      return;
    }

    if (_menu == rmenu::Page::Keep) {
      host().topBar("KEEP THIS?", false, "BACK");
      c.textCentered(c.width() / 2, 150, "the phrase to keep", TS_SMALL, true);
      // The phrase itself, wrapped a word at a time. Measuring the whole
      // string and cutting it backwards is the obvious way and it is wrong:
      // the cut shortens the string being measured, so the second line is
      // decided from a length that no longer exists.
      {
        char line[marks::LABEL] = "", cand[marks::LABEL + 8];
        int y = 210;
        const char* p = _phrase;
        while (*p && y < 470) {
          const char* e = p;
          while (*e && *e != ' ') e++;
          snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
          if (line[0] && c.textWidth(cand, TS_LARGE) > c.width() - 80) {
            c.textCentered(c.width() / 2, y, line, TS_LARGE, true);
            y += 44;
            line[0] = 0;
            continue;  // the word that did not fit starts the next line
          }
          snprintf(line, sizeof(line), "%s", cand);
          p = e;
          while (*p == ' ') p++;
        }
        if (line[0]) c.textCentered(c.width() / 2, y, line, TS_LARGE, true);
      }
      char where[48];
      snprintf(where, sizeof(where), "chapter %d, page %d", _spine + 1, _page + 1);
      c.textCentered(c.width() / 2, 470, where, TS_SMALL, true);
      // Save is the one this screen exists for, so it is the heavier word --
      // weight rather than a black slab, which on e-paper shouts.
      c.drawRect(40, 560, (c.width() - 100) / 2, 96, 1, true);
      c.textInBox(40, 560, (c.width() - 100) / 2, 96, "Save", TS_LARGE, true, true);
      c.button(c.width() / 2 + 10, 560, (c.width() - 100) / 2, 96, "Cancel", false);
      return;
    }

    if (_menu == rmenu::Page::Text) {
      host().topBar("TEXT", false, "OPTIONS");
      const char* labels[2] = {"Size", "Spacing"};
      const char* values[2] = {epubui::sizeName(_size), epubui::leadName(_lead)};
      for (int r = 0; r < 2; r++) {
        const int y = 110 + r * 120;
        // Circles, matching the + that keeps a bookmark: the same gesture
        // deserves the same mark, and a hairline circle is the quietest
        // control this panel has.
        c.text(28, y, labels[r], TS_SMALL, true);
        const int cy = y + 64;
        c.drawCircle(66, cy, 34, 1, true);
        c.fillRect(66 - 15, cy - 1, 30, 2, true);
        c.drawCircle(c.width() - 66, cy, 34, 1, true);
        c.fillRect(c.width() - 66 - 15, cy - 1, 30, 2, true);
        c.fillRect(c.width() - 66 - 1, cy - 15, 2, 30, true);
        c.textCentered(c.width() / 2, cy - c.textHeight(TS_LARGE) / 2, values[r], TS_LARGE, true);
      }
      // Page turns: three cadences, cycled by tapping the row. A row rather
      // than a stepper because there is no scale here to step along -- the
      // three are named things, and arrows either side of one would suggest a
      // continuum that does not exist. This reader's setting only; the .tbk
      // reader has its own, on its own options panel.
      c.fillRect(24, TURNS_Y - 12, c.width() - 48, 1, true);
      c.text(28, TURNS_Y + 14, "Page turns", TS_SMALL, true);
      c.text(28, TURNS_Y + 40, rmenu::refreshLabel(mode()), TS_LARGE, true);
      c.textClipped(180, TURNS_Y + 48, c.width() - 210, rmenu::refreshSub(mode()), TS_SMALL, true);
      c.fillRect(24, TURNS_Y + 92, c.width() - 48, 1, true);

      // The sample is the point: nobody can picture 32 px with airy leading.
      const TSize ts = epubui::sizeAt(_size);
      const int step = c.textHeight(ts) + epubui::leadAir(_lead);
      static const char* kSample[4] = {"The quick brown fox jumps", "over the lazy dog, and",
                                       "the page turns after about", "this many lines of it."};
      for (int i = 0; i < 4; i++) c.text(28, TURNS_Y + 122 + i * step, kSample[i], ts, true);
      c.textCentered(c.width() / 2, 748, "the page you are on is kept when this changes", TS_SMALL,
                     true);
      return;
    }

    const bool contents = _menu == rmenu::Page::Contents;
    host().topBar(contents ? "Contents" : "Bookmarks", false, "OPTIONS");
    const int total = contents ? tocCount() : _nmarks;
    if (total <= 0) {
      rmenu::drawEmpty(c, "no bookmarks yet", "KEEP THIS PLACE puts one here");
      return;
    }
    for (int k = 0; k < shelf::PER_PAGE; k++) {
      const int idx = _mpage * shelf::PER_PAGE + k;
      if (idx >= total) break;
      if (contents) {
        char label[64];
        int spine = 0;
        tocRow(idx, label, sizeof(label), spine);
        snprintf(buf, sizeof(buf), "chapter %d", spine + 1);
        rmenu::drawRow(c, k, label, buf, shelf::rowSep(k, idx, total), spine == _spine);
      } else {
        char label[48], where[40];
        markLabel(_marks[idx], label, sizeof(label));
        snprintf(where, sizeof(where), "chapter %u, page %u", (unsigned)(_marks[idx].spine + 1),
                 (unsigned)(_marks[idx].page + 1));
        rmenu::drawRow(c, k, label, where, shelf::rowSep(k, idx, total),
                       _marks[idx].spine == (uint16_t)_spine && _marks[idx].page == (uint16_t)_page);
      }
    }
    shelf::drawPager(c, _mpage, total);
    if (!contents)
      c.textCentered(c.width() / 2, 770, "a second tap on the row you are on removes it", TS_SMALL,
                     true);
  }

  void menuTap(int x, int y) {
    if (host().isBackTap(x, y)) {
      if (_menu == rmenu::Page::Root)
        menuClose();
      else
        menuBack();
      return;
    }
    if (_menu == rmenu::Page::Root) {
      const int W = host().canvas().width();
      if (rmenu::hitPlus(x, y, 1, W)) {
        pickStart();
        return;
      }
      const bool hasFace = host().typefaceCount() > 1;
      const int rowFace = hasFace ? 3 : -1;
      const int rowRot = hasFace ? 4 : 3;
      const int rowClose = rowRot + 1;
      const int hit = rmenu::hitRoot(x, y, rowClose + 1, W);
      if (hit == 0) {
        _menu = rmenu::Page::Contents;
        _mpage = pageOfSpine();
        host().beep(0);
        paint();
        return;
      }
      if (hit == 1) {
        _menu = rmenu::Page::Marks;
        _mpage = 0;
        host().beep(0);
        paint();
        return;
      }
      if (hit == 2) {
        _menu = rmenu::Page::Text;
        host().beep(0);
        paint();
        return;
      }
      if (hit == rowFace) {
        // Cycle and reflow from the offset being read: a different face has
        // different widths, so the page boundaries just moved.
        _face = (uint8_t)((_face + 1) % host().typefaceCount());
        prefs().putUInt("rd_face", _face);
        host().beep(0);
        restyle();
        paint();
        return;
      }
      if (hit == rowRot) {
        // The row cycles; the turn itself waits for the panel to close, so
        // the panel is never asked to draw itself sideways.
        _rot = nextRot(_rot);
        prefs().putUInt("rd_rot", _rot);
        host().beep(0);
        paint();
        return;
      }
      if (hit == rowClose) {
        _menu = rmenu::Page::None;
        closeBook(true);
        return;
      }
      return;
    }

    if (_menu == rmenu::Page::Keep) {
      if (y >= 560 && y < 656) {
        if (x < host().canvas().width() / 2)
          keepMark(_phrase);
        else
          pickCancel();
      }
      return;
    }

    if (_menu == rmenu::Page::Text) {
      if (y >= TURNS_Y - 12 && y < TURNS_Y + 92) {
        rmenu::setRefreshMode(prefs(), true, rmenu::nextRefresh(mode()));
        host().beep(0);
        paint();
        return;
      }
      for (int r = 0; r < 2; r++) {
        const int y0 = 110 + r * 120 + 30;
        if (y < y0 || y >= y0 + 68) continue;
        uint8_t& v = r == 0 ? _size : _lead;
        const int lim = r == 0 ? epubui::SIZES : epubui::LEADS;
        int nv = v;
        if (x < 120)
          nv--;
        else if (x >= host().canvas().width() - 120)
          nv++;
        else
          return;
        if (nv < 0 || nv >= lim) {
          host().beep(2);
          return;
        }
        v = (uint8_t)nv;
        prefs().putUInt(r == 0 ? "rd_size" : "rd_lead", v);
        // The page is re-derived from where the reader IS, not from the page
        // number, because the page number means nothing once the type changes.
        restyle();
        host().beep(0);
        paint();
        return;
      }
      return;
    }

    const bool contents = _menu == rmenu::Page::Contents;
    const int total = contents ? tocCount() : _nmarks;
    const int pages = shelf::pageCount(total);
    if (y >= shelf::PAGER_Y && pages > 1) {
      // Same rule as the shelf: a list of rows moving under a fixed frame is
      // the easiest thing on this device to draw partially.
      if (_mpage > 0 && shelf::prevRect().hit(x, y)) {
        _mpage--;
        host().beep(0);
        paint();
      } else if (_mpage < pages - 1 && shelf::nextRect().hit(x, y)) {
        _mpage++;
        host().beep(0);
        paint();
      }
      return;
    }
    const int idx = shelf::hitRow(x, y, total, _mpage);
    if (idx < 0) return;
    if (contents) {
      char label[64];
      int spine = 0;
      tocRow(idx, label, sizeof(label), spine);
      jumpTo(spine, 0);
      return;
    }
    // A bookmark you are already standing on is one you are asking to remove:
    // there is nowhere else for that tap to mean anything.
    if (_marks[idx].spine == _spine && _marks[idx].page == _page) {
      marks::remove(_marks, _nmarks, idx);
      marks::save(host(), _books[_cur].file, _marks, _nmarks);
      host().beep(1);
      paint();
      return;
    }
    jumpTo(_marks[idx].spine, _marks[idx].off);
  }

  static constexpr int TURNS_Y = 356;

  int pageOfSpine() {
    if (_ntoc <= 0) return _spine / shelf::PER_PAGE;
    for (int i = 0; i < _ntoc; i++)
      if (epubui::g_toc[i].spine >= _spine) return i / shelf::PER_PAGE;
    return 0;
  }

  void jumpTo(int spine, uint32_t off) {
    _streamLost = false;  // gotoPlace opens the chapter itself
    if (!gotoPlace(spine, off)) {
      host().beep(2);
      return;
    }
    _menu = rmenu::Page::None;
    saveProgress();
    host().beep(1);
    paint();
  }

  // Type changed: lay the chapter out again and land on the page holding the
  // place we were reading. Nothing about the position is a page number.
  void restyle() {
    const uint32_t off = curOffset();
    _streamLost = false;
    gotoPlace(_spine, off);
  }

  void renderPage(ToolsCanvas& c) {
    FaceScope fs(host(), _face);
    if (_pageImage[0]) {
      if (!drawImagePage(c)) drawImagePlate(c);
      if (_chrome) renderFooter(c);
      return;
    }
    for (int i = 0; i < _lineN; i++)
      c.text(epubui::MARGIN, _lines[i].y, _lines[i].t, epubui::sizeAt(_size), true);
    if (!_chrome) return;
    renderFooter(c);
  }

  void renderFooter(ToolsCanvas& c) {
    // The footer: where you are, on a plate the page shows through around.
    const int fy = c.height() - epubui::FOOT_H;
    c.fillRect(0, fy, c.width(), epubui::FOOT_H, false);
    c.fillRect(0, fy, c.width(), 2, true);
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
    c.textClipped(16, fy + 14, c.width() - 32 - pw - 12, _books[_cur].title, TS_MED, true, true);
    c.text(c.width() - 16 - pw, fy + 14, pos, TS_MED, true);
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
  bool _help = false;       // the HOW TO READ card, once per device
  bool _freshCover = false; // this open just built the cover; show it framed
  char _noteBuf[96] = {};
  char _pageImage[192] = {};       // this page IS this picture, if set
  char _pendImage[192] = {};       // ...and this one opens the next page
  uint32_t _pendImageOff = 0;
  bool _pendImageValid = false;
  bool _streamLost = false;        // a picture was read out of the same zip
  rmenu::Page _menu = rmenu::Page::None;
  // Picking a phrase to keep: -1 nowhere, then the first word, then the last.
  // Line and word indices into _lines, which is what the page IS -- so the
  // pick is re-measured from the same strings the page was drawn from and
  // needs no table of word boxes anywhere.
  int _pickL0 = -1, _pickW0 = 0, _pickL1 = -1, _pickW1 = 0;
  bool _picking = false;
  char _phrase[marks::LABEL] = {};
  int _mpage = 0;                  // which page of the contents or the marks
  int _ntoc = -1;                  // -1 until the book's contents are read
  marks::Mark _marks[marks::MAX];
  int _nmarks = 0;
  char _rootSub[5][48] = {};
  uint8_t _size = 0, _lead = 1;
  uint8_t _face = 0;  // 0 DejaVu, 1 Literata, 2 Atkinson
  uint8_t _rot = 0;      // the page view's rotation; every menu screen is portrait
  uint8_t _rotLaid = 0;  // the rotation the current layout was measured at

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
