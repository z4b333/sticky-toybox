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
inline constexpr int SIZES = 3, LEADS = rmenu::LEADS;
inline TSize sizeAt(int i) { return i <= 0 ? TS_MED : (i == 1 ? TS_LARGE : TS_HUGE); }
inline const char* sizeName(int i) { return i <= 0 ? "normal" : (i == 1 ? "large" : "largest"); }
// Line spacing lives in reader_menu.h now: the recipe app offers the same
// three, and a setting that reads "airy" on one screen and something else on
// another is two settings.
inline const char* leadName(int i) { return rmenu::leadName(i); }
inline int leadAir(int i) { return rmenu::leadAir(i); }
// A heading is set one size up from the body and bold. One step, not six:
// h1 through h6 all mean "this is a heading" to a reader holding a phone-sized
// panel, and a six-level hierarchy would spend the panel's whole size range on
// distinctions nothing on the page makes use of. The largest body size has
// nowhere to go, so its headings say so with weight alone.
inline TSize headSize(int bodyIdx) {
  return bodyIdx <= 0 ? TS_LARGE : TS_HUGE;
}
inline TSize sizeFor(int bodyIdx, int head) { return head ? headSize(bodyIdx) : sizeAt(bodyIdx); }
// The air above a heading, so it belongs to the text it introduces rather
// than floating between two paragraphs.
inline constexpr int HEAD_GAP = 10;
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

// Which row of the contents a spine item sits in, or -1 when there are none.
//
// The rule is "the row with the LARGEST start at or before this spine", not
// "the last row at or before it", and the difference is the whole bug. A
// contents list is not in spine order: these releases put the colour inserts
// and the illustration gallery at the END of the list, pointing back at files
// near the front of the book. Scanning for the last match therefore walked
// past every real chapter and landed on entry 36 -- "chapter 37" under a book
// open at chapter one.
//
// Anything before the first named row belongs to that row: front matter is
// where a book starts.
inline int tocRowForSpine(const epubc::Book::TocEntry* toc, int n, int spine) {
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (toc[i].spine > spine) continue;
    if (best < 0 || toc[i].spine > toc[best].spine) best = i;
  }
  if (best < 0 && n > 0) best = 0;  // ahead of the first row: it is still row one
  return best;
}
}  // namespace epubui

class EpubTool : public ToolApp {
 public:
  const char* title() const override { return "EPUB"; }

  ~EpubTool() override {
    if (_open && _host) closeBook(false);
    // The shelf's own card session, if it is still up. Nothing above this app
    // may inherit a powered card: the hub draws with no session at all.
    if (_host) host().sdBrowseClose();
    _browsing = false;
    free(_lut);
  }

  bool enterTouchesCard() const override { return true; }  // the shelf lists the card
  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _screen = Screen::List;
    _help = !help::suppressed(prefs(), "ep");
    _note = nullptr;
    snprintf(_dir, sizeof(_dir), "%s", shelf::TOP);
    // Hold the card for the whole time a shelf is on screen. Every listing
    // inside it then borrows this session instead of powering the card up and
    // putting it down again -- and, because nothing releases, moving between
    // folders is a partial refresh rather than the full one a release forces.
    _browsing = h.sdBrowseOpen();
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
      bthumb::drawLoading(host(), c, _books[_cur].file, _books[_cur].title);
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
        const char* nm = _folders[idx].name;
        shelf::drawFolderRow(c, k, nm[0] == '/' ? nm + 1 : nm, _folders[idx].count,
                             shelf::rowSep(k, idx, total));
        continue;
      }
      const int b = idx - _nf;
      char sub[ToolsHost::PLACE_LEN + 24];
      shelf::drawBookRow(c, k, _books[b].title, placeLine(_books[b], sub, sizeof(sub)),
                         shelf::rowSep(k, idx, total));
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
        host().beep(1);
        if (inFolder()) {
          snprintf(_dir, sizeof(_dir), "%s", shelf::TOP);
          _note = nullptr;
          reload();
          host().refresh(!_browsing);  // partial while the shelf holds the card
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
  // What the shelf row for book i currently claims: true means it draws
  // "carries on where it stopped".
  int hostBookCount() const { return _n < 0 ? 0 : _n; }
  // Forces a row back to "from the start", so a guard can stand where a
  // freshly-listed shelf stands without deleting anything off the card.
  void hostSetCont(int i, bool on) {
    if (i >= 0 && i < (_n < 0 ? 0 : _n)) _books[i].cont = on;
  }
  bool hostCont(int i) const { return i >= 0 && i < (_n < 0 ? 0 : _n) && _books[i].cont; }
  void hostFile(int i, char* out, int cap) const {
    snprintf(out, (size_t)cap, "%s", (i >= 0 && i < (_n < 0 ? 0 : _n)) ? _books[i].file : "");
  }
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
  bool hostInFolder() const { return inFolder(); }
  const char* hostLine(int i) const { return i < _lineN ? _lines[i].t : ""; }
  int hostLineHead(int i) const { return i < _lineN ? _lines[i].head : 0; }
  bool hostLineBoldAt(int i, int at) const { return i < _lineN && _lines[i].boldAt(at); }
  bool hostLineItalAt(int i, int at) const { return i < _lineN && _lines[i].italAt(at); }
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
    // A folder named with a leading slash IS its path: /Read and /epub, the
    // roots CrossPoint and CrossInk keep their books in, shelved beside the
    // series folders under /books.
    if (_folders[i].name[0] == '/')
      snprintf(_dir, sizeof(_dir), "%s", _folders[i].name);
    else
      snprintf(_dir, sizeof(_dir), "%s/%s", shelf::TOP, _folders[i].name);
    _note = nullptr;
    reload();
    host().beep(0);
    // A list replacing a list is text on white, the kindest thing a partial
    // can draw -- legal here only because the shelf session means no release
    // re-initialised the panel underneath it.
    host().refresh(!_browsing);
  }

  struct HostIO : epubc::IO {
    ToolsHost* h = nullptr;
    int read(uint32_t pos, void* dst, uint32_t n) override { return h->epubRead(pos, dst, n); }
    uint32_t size() override { return h->epubSize(); }
  };

  struct Line {
    char t[200];
    short y;     // where layout put it: paragraph gaps make the steps uneven
    uint8_t head;  // 0 body, else the heading level it belongs to
    // Which bytes are bold, one bit each, so a line can change weight
    // mid-sentence without carrying a second copy of itself. 200 bytes of
    // text is 25 bytes of flags; runs would be smaller only for lines that
    // do not exist in real books.
    uint8_t bold[25];
    // ...and which are italic, on the same terms. Two bitmaps rather than one
    // two-bit field: the run-splitter reads them independently, and a line is
    // far more often all-roman than mixed.
    uint8_t ital[25];
    bool boldAt(int i) const { return (bold[i >> 3] >> (i & 7)) & 1; }
    bool italAt(int i) const { return (ital[i >> 3] >> (i & 7)) & 1; }
    void setBold(int i) { bold[i >> 3] |= (uint8_t)(1 << (i & 7)); }
  };

  // Two directories hold a position for the same book, and both matter.
  //
  // `crossPoint` is the 32-bit-hash one -- confirmed against a real
  // CrossPoint card: it keeps its whole parsed cache there (book.bin,
  // sections/, html/) and its progress.bin beside it, and it looks nowhere
  // else. The 64-bit FNV one is Toybox's own, and older builds of this
  // firmware read only that. So: read CrossPoint's first, write both.
  void progressPath(char* out, int cap, bool crossPoint = false) {
    char dir[96];
    if (crossPoint)
      epubc::cacheDirLegacy(_books[_cur].file, dir, sizeof(dir));
    else
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
    // BOTH directories, every time. CrossPoint keeps its cache under a
    // 32-bit hash of the book's path and reads its position from there; older
    // Toybox builds read the 64-bit one. Ten bytes twice is nothing, and it
    // means whichever firmware the card is carried to finds the place.
    char path[128];
    progressPath(path, sizeof(path), true);  // CrossPoint's
    host().sdWriteFileAtomic(path, buf, n);
    progressPath(path, sizeof(path));  // ours
    host().sdWriteFileAtomic(path, buf, n);
    saveKoreader();
  }

  // How long a chapter is, in minutes, from the size the zip's central
  // directory already reported -- so the contents list costs no reading at
  // all to draw. Markup bytes divided by 8.5 is close to a word count across
  // the books this has been checked against, and 220 words a minute is an
  // unhurried reader. Both are estimates and the label says so by being
  // round: nobody wants "17.4 min".
  void chapterLength(int spine, char* out, int cap) const {
    const uint32_t bytes = _book.spineBytes(spine);
    const int words = (int)((double)bytes / 8.5);
    const int mins = (int)((double)words / 220.0 + 0.5);
    if (mins < 1)
      snprintf(out, (size_t)cap, "under a minute");
    else
      snprintf(out, (size_t)cap, "%d min", mins);
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
    // The cheap cover sources first, BEFORE the loading paint: a sidecar the
    // owner put beside the book, or a cover CrossInk already decoded. Neither
    // costs a decode, so even a book's very first open can lead with its own
    // face instead of the titled plate. (The sidecar check runs every open
    // anyway -- that is how a replaced sidecar is noticed.)
    if (!bthumb::coverFromSidecar(host(), _books[i].file) &&
        !bthumb::complete(host(), _books[i].file) && !bthumb::failed(_books[i].file)) {
      if (!host().coverFromBmp(_books[i].file)) host().crossCoverGrab(_books[i].file);
    }
    // The cover as the loading screen. An EPUB spends real seconds opening
    // (zip walk, cover decode on the first open, the chapter replayed to the
    // saved page); they pass behind the book's own face. Only a first open
    // with no sidecar and no CrossInk cache still shows the plate.
    //
    // Portrait, whatever the page's own angle is: a cover is a portrait
    // object -- turned on its side it is cropped, not rotated -- and the
    // plate is a portrait design like every other screen that is not the
    // page itself. The reading rotation goes on at the Page transition.
    host().setCanvasRotation(0);
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
    _menu = rmenu::Page::None;
    _picking = false;
    _ntoc = -1;
    _nmarks = 0;
    recents::note(prefs(), recents::KIND_EPUB, _books[i].file, _books[i].title);

    // The decode fallback: only a book with no cover from any cheap source
    // (tried above, before the loading paint) pays for the JPEG decode --
    // once, and a cover that will not decode is marked so it is not retried.
    bool unwrapped = false;
    if (!bthumb::complete(host(), _books[i].file) && !bthumb::failed(_books[i].file)) {
      bool transient = false;
      if (!epubcov::makeThumb(host(), _book, _books[i].file, &transient) && !transient)
        bthumb::markFailed(_books[i].file);
      unwrapped = bthumb::complete(host(), _books[i].file);
    }
    // The unwrapping finishes where it promised: the cover that was just
    // decoded, full size, and a beep to say the wait is over. Only on the one
    // open that had something to wait for -- every later open already led
    // with this face, and repainting it there would read as a stutter. The
    // screen is still Loading, so the paint IS the cover.
    if (unwrapped) {
      host().refresh(true);
      host().beep(1);
    }
    // ...and, if the sleeping panel is set to wear a cover, this book's goes
    // into flash now. After the decode above, so the very first open of a
    // book still gets one.
    bthumb::noteForLock(host(), _books[i].file);
    // The favour returned: leave the finished cover where CrossInk looks for
    // one, so ITS first open of this book skips the decode too. One exists()
    // check when the file is already there.
    if (bthumb::complete(host(), _books[i].file)) host().crossCoverPut(_books[i].file);

    // Where were we? The card remembers, in CrossPoint's format.
    // CrossPoint's directory FIRST. It is the shared one -- that firmware
    // writes its position there and nowhere else -- so it is also the one
    // that is fresh when a card has just come back from it. Reading ours
    // first is what made a book flashed to CrossPoint and back resume at the
    // place Toybox last saw rather than the place the reader actually
    // stopped, while CrossPoint, which never looks at ours, opened the same
    // book at page one. Both halves of that were one wrong preference.
    epubc::Progress p;
    uint8_t buf[10];
    char path[128];
    progressPath(path, sizeof(path), true);
    int n = host().sdReadFile(path, buf, sizeof(buf));
    if (n <= 0) {
      progressPath(path, sizeof(path));  // ours, for a card no CrossPoint has touched
      n = host().sdReadFile(path, buf, sizeof(buf));
    }
    int spine = 0;
    uint32_t off = 0;
    if (n > 0 && epubc::decodeProgress(buf, n, p)) {
      spine = p.spine < _book.spineCount() ? p.spine : _book.spineCount() - 1;
      off = p.hasOffset ? p.offset : 0;
    }
    _screen = Screen::Page;
    host().setCanvasRotation(_rot);  // gotoPlace lays out at this width
    _rotLaid = _rot;
    if (!unwrapped) host().beep(1);  // an unwrapped open already sounded, at the cover
    if (!gotoPlace(spine, off)) {
      // A book whose first chapter will not parse is a book we cannot show.
      _note = withHeap(_book.error()[0] ? _book.error() : "could not read the chapter");
      closeBook(false);
      host().beep(2);
      host().refresh(true);
      return;
    }
    // A book whose first page is its cover art opens portrait, whatever the
    // reading rotation: see syncPageRot.
    syncPageRot();
    host().refresh(true);
  }

  // The second line of a shelf row: where the reader stopped, or nothing at
  // all. A book nobody has opened says nothing -- "from the start" was a label
  // on every row of a new shelf, which is a lot of ink to tell you that a list
  // of books is a list of books. What a started book says comes out of the
  // ten-byte progress file the list already read: which chapter, which page of
  // it, and how many that chapter holds when whatever wrote the file said so.
  //
  // Chapters are counted from the spine, the same way the reader's own footer
  // and contents list count them, so the number here is the number there.
  static const char* placeLine(const ToolsHost::EpubInfo& b, char* buf, int cap) {
    // The chapter's OWN NAME, as its contents row spells it -- not a number.
    // A number cannot be right here: the contents list of a real release opens
    // with "Cover", so its second row is the book's Chapter 1, and counting
    // rows would print "chapter 2" over a page headed Chapter 1. The name is
    // the one thing that is true by construction, because it is the same
    // string the contents screen shows.
    //
    // No sidecar, no line: a position left by CrossPoint says nothing until
    // the book has been closed here once.
    if (!b.cont || !b.place[0]) return "";
    if (b.pageCount > 0)
      snprintf(buf, (size_t)cap, "%s - page %u of %u", b.place, (unsigned)b.page,
               (unsigned)b.pageCount);
    else
      snprintf(buf, (size_t)cap, "%s - page %u", b.place, (unsigned)b.page);
    return buf;
  }

  // Which chapter of the CONTENTS a spine item belongs to, 1-based: the last
  // entry that starts at or before it. Books whose contents could not be read
  // fall back to the spine, which is also what the contents list itself falls
  // back to -- there, a chapter really is a spine item.
  // What this position is called, in the book's own words. Falls back to the
  // spine when a book has no contents at all -- which is what the contents
  // screen itself falls back to, so the two still say the same thing.
  void chapterName(int spine, char* out, int cap) {
    tocCount();  // reads the contents once, while the card is still up
    const int row = epubui::tocRowForSpine(epubui::g_toc, _ntoc > 0 ? _ntoc : 0, spine);
    if (row >= 0)
      snprintf(out, (size_t)cap, "%s", epubui::g_toc[row].title);
    else
      snprintf(out, (size_t)cap, "chapter %d", spine + 1);
  }

  // The note the shelf reads: chapter, page, and how many pages that chapter
  // holds, in the book's own numbering. Written on the way out, once, while
  // the contents are still in hand and the card is still awake.
  void savePlaceNote() {
    if (_cur < 0 || _page >= _lutN) return;
    char name[ToolsHost::PLACE_LEN + 1];
    chapterName(_spine, name, sizeof(name));
    // Tab-separated and versioned, because the first shape of this file held a
    // chapter NUMBER and a device may still be carrying one: a reader that
    // cannot say what version it is looking at has to either guess or lie.
    // Anything that is not a 3 is ignored and the row stays blank until the
    // book is closed here again.
    char body[16 + ToolsHost::PLACE_LEN];
    const int n = snprintf(body, sizeof(body), "3\t%d\t%d\t%s\n", _page + 1,
                           _chapterPages > 0 ? _chapterPages : 0, name);
    char dir[96], path[128];
    epubc::cacheDir(_books[_cur].file, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/toybox.pos", dir);
    host().sdWriteFileAtomic(path, body, n);
    // ...and the row this book came from, so the shelf is right the moment it
    // is drawn rather than the next time the app is entered.
    snprintf(_books[_cur].place, sizeof(_books[_cur].place), "%s", name);
    _books[_cur].page = (uint16_t)(_page + 1);
    _books[_cur].pageCount = (uint16_t)(_chapterPages > 0 ? _chapterPages : 0);
  }

  void closeBook(bool beep) {
    _menu = rmenu::Page::None;
    host().setCanvasRotation(0);  // the shelf, like everything else, is portrait
    if (_open) {
      // Did this session actually leave a position on the card? Same condition
      // saveProgress uses to decide whether there is anything to write.
      const bool placed = _cur >= 0 && _page < _lutN;
      saveProgress();
      // ...and if it did, the row this book came from now says so. The shelf is
      // listed once, on the way into the app, because the check is an SD.exists
      // per book; so a book read and backed out of kept the label it had when
      // the app opened -- "from the start", under a book that resumes perfectly
      // the moment you tap it. The list was right about the card and wrong
      // about the last thirty seconds.
      if (placed) {
        _books[_cur].cont = true;
        savePlaceNote();
      }
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
  static const char* faceName(int f) { return f == 1 ? "Literata" : "DejaVu"; }

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

  // The line breaker measures against the canvas, so the canvas has to be at
  // the READING rotation while it works -- even when the panel is standing up
  // for a picture page (syncPageRot) or for the menu. Put back on the way out,
  // so whoever is drawing keeps the screen they set up.
  struct LayoutRot {
    ToolsHost& h;
    const int was;
    LayoutRot(ToolsHost& host, int want) : h(host), was(host.canvasRotation()) {
      if (was != want) h.setCanvasRotation(want);
    }
    ~LayoutRot() {
      if (h.canvasRotation() != was) h.setCanvasRotation(was);
    }
  };

  int layoutPage() {
    LayoutRot lr(host(), _rot);
    FaceScope fs(host(), _face);
    ToolsCanvas& c = host().canvas();
    const TSize ts = epubui::sizeAt(_size);
    const int lineW = c.width() - 2 * epubui::MARGIN;
    // A line's height follows its own type: a heading line is taller than the
    // body around it, so the step cannot be one number for the page any more.
    auto stepFor = [&](int head) {
      return c.textHeight(epubui::sizeFor(_size, head)) + epubui::leadAir(_lead);
    };
    const int step = stepFor(0);
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
    // The style of the line being assembled: its heading level (the whole
    // line shares one, because a heading is its own block) and which of its
    // bytes are bold.
    int curHead = 0;
    uint8_t curBold[25] = {};
    uint8_t curItal[25] = {};
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
      const int h = stepFor(curHead);
      if (y + h > c.height() - epubui::BOTTOM_INSET || _lineN >= epubui::MAX_LINES) return false;
      memcpy(_lines[_lineN].t, cur, (size_t)curLen);
      _lines[_lineN].t[curLen] = 0;
      _lines[_lineN].y = (short)y;
      _lines[_lineN].head = (uint8_t)curHead;
      memcpy(_lines[_lineN].bold, curBold, sizeof(curBold));
      memcpy(_lines[_lineN].ital, curItal, sizeof(curItal));
      _lineN++;
      y += h;
      curLen = 0;
      curW = 0;
      curHead = 0;
      memset(curBold, 0, sizeof(curBold));
      memset(curItal, 0, sizeof(curItal));
      return true;
    };
    auto roomForLine = [&](int head) {
      return y + stepFor(head) <= c.height() - epubui::BOTTOM_INSET &&
             _lineN < epubui::MAX_LINES;
    };

    char w[epubc::WORD_CAP];
    uint32_t off = 0;
    while (true) {
      int tok;
      uint8_t st = 0;
      if (_pendValid) {
        strcpy(w, _pend);
        off = _pendOff;
        st = _pendStyle;  // a word carried to the next page keeps its markup
        _pendValid = false;
        tok = epubc::TOK_WORD;
      } else {
        tok = _book.next(w, off);
        st = _book.wordStyle();
      }
      const int wHead = st >> 4;
      const bool wBold = (st & epubc::Book::STYLE_BOLD) != 0;
      // Bold wins where a book nests the two: there is no bold-italic face,
      // and gfx would ignore the italic anyway. Deciding it here keeps the
      // measured width and the drawn width the same thing.
      const bool wItal = !wBold && (st & epubc::Book::STYLE_ITAL) != 0;

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

      // a word, measured in the face it will be drawn in
      const TSize wts = epubui::sizeFor(_size, wHead);
      const int ww = c.textWidth(w, wts, wBold, wItal);
      // A heading never shares a line with body text: they are different
      // blocks, so a change of level flushes what is in hand.
      if (curLen && wHead != curHead) {
        if (!flushLine()) {
          strcpy(_pend, w);
          _pendOff = off;
          _pendStyle = st;
          _pendValid = true;
          break;
        }
        if (wHead) y += epubui::HEAD_GAP;
      }
      // The space in front of a word belongs to that word's run: measured in
      // its face and weight, and marked with it, so a bold phrase is one run
      // rather than three and the width the layout counted is the width the
      // drawing spends.
      const int wSpaceW =
          c.textWidth(" ", wts, wBold, wItal) > 0 ? c.textWidth(" ", wts, wBold, wItal) : spaceW;
      if (!started) {
        pageStart = off;
        started = true;
      }
      // `placed` answers "did any word begin landing on this page": the first
      // word of a fresh page always lands (split if huge), so a word carried
      // whole to the next page only happens on a page that already has text.
      placed++;
      const int need = curLen ? curW + wSpaceW + ww : ww;
      // Records which bytes of `cur` this word occupies, so the line can be
      // drawn in runs later.
      auto markBold = [&](int from, int len) {
        if (!wBold && !wItal) return;
        for (int i = from; i < from + len && i < (int)sizeof(curBold) * 8; i++) {
          if (wBold) curBold[i >> 3] |= (uint8_t)(1 << (i & 7));
          if (wItal) curItal[i >> 3] |= (uint8_t)(1 << (i & 7));
        }
      };
      // A line is only ever STARTED if there is room for it to land. Room
      // used to be checked when the line was flushed, a word too late: a page
      // that filled while a line was still being assembled pended the one
      // word in hand and silently dropped every word already gathered into
      // the line. On hardware, mid-novel: "Good. Now that left the other
      // two." lost everything but "two." across a page turn.
      if (!curLen && !roomForLine(wHead)) {
        strcpy(_pend, w);
        _pendOff = off;
        _pendStyle = st;
        _pendValid = true;
        break;
      }
      if (!curLen) curHead = wHead;
      if (need <= lineW) {
        if (curLen) {
          markBold(curLen, 1);
          cur[curLen++] = ' ';
          curW += wSpaceW;
        }
        const int wl = (int)strlen(w);
        if (curLen + wl < (int)sizeof(cur)) {
          markBold(curLen, wl);
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
        _pendStyle = st;
        _pendValid = true;
        break;
      }
      if (ww <= lineW) {
        if (!roomForLine(wHead)) {
          strcpy(_pend, w);
          _pendOff = off;
          _pendStyle = st;
          _pendValid = true;
          break;
        }
        const int wl = (int)strlen(w);
        curHead = wHead;
        markBold(0, wl);
        memcpy(cur, w, (size_t)wl);
        curLen = wl;
        curW = ww;
        continue;
      }
      // Longer than a whole line (Thai and CJK runs, URLs): hard-split at the
      // widest prefix that fits, and keep going with the remainder.
      const char* rest = w;
      curHead = wHead;
      while (*rest) {
        if (!roomForLine(wHead)) {
          strcpy(_pend, rest);
          _pendOff = off;  // close enough: a back-turn lands at the word's start
          _pendStyle = st;
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
          if (c.textWidth(probe, wts, wBold, wItal) > lineW) break;
          fitBytes += step;
        }
        if (fitBytes == 0) fitBytes = 1;  // a glyph wider than the line still moves on
        memcpy(cur, rest, (size_t)fitBytes);
        curLen = fitBytes;
        markBold(0, fitBytes);
        memcpy(probe, rest, (size_t)fitBytes);
        probe[fitBytes] = 0;
        curW = c.textWidth(probe, wts, wBold, wItal);
        rest += fitBytes;
        if (*rest && !flushLine()) {
          strcpy(_pend, rest);
          _pendOff = off;
          _pendStyle = st;
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
        // ...with its markup. Forgetting it here is what made the first word
        // of a resumed page forget it was inside a heading, which split the
        // heading's own first word off as a line of body text.
        _pendStyle = _book.wordStyle();
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
    // The layout runs to the END of the chapter, not just far enough to find
    // the page wanted. How many pages a chapter has is a fact about the panel
    // and the type, not about the file, so laying it out is the only way to
    // learn it -- and the footer has to be able to say "p 3 of 12" the moment
    // a chapter opens, not only after somebody has read to the end of it. One
    // extra pass over a chapter's words, against a full refresh that costs
    // 1.7 s regardless. Turning back INTO a chapter already worked this way.
    int target = -1;
    while (true) {
      const int placed = layoutPage();
      if (placed == 0) {
        // Nothing left in this chapter. That is only an EMPTY chapter if it
        // never produced a page at all -- a chapter holding one illustration
        // and no words produces exactly one page and then comes back empty,
        // and reading "empty" as "skip to the next chapter" is what made the
        // cover page, the gallery and the character art unreachable.
        if (_lutN > 0) break;
        if (spineIdx + 1 < _book.spineCount()) return gotoPlace(spineIdx + 1, 0);
        _lineN = 0;
        _page = 0;
        return false;
      }
      // The first page that starts PAST the mark means the one before it holds
      // it. Noted rather than jumped to, so the count can finish.
      if (target < 0 && _lutN >= 2 && epubui::pageOff(_lut[_lutN - 1]) > off) target = _lutN - 2;
      if (_atEnd && !_pendValid) break;  // the chapter's last page
      if (_lutN >= epubui::MAX_PAGES) break;  // a chapter longer than the table
    }
    if (_chapterPages <= 0) _chapterPages = _lutN;
    _page = _lutN - 1;
    // A mark past the last page start belongs on the last page, which is the
    // one the layout has just left on the screen: no replay needed.
    if (target < 0 || target == _page) return true;
    return showPageAt(target);
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
  // "toybox/" prefix and the extension swapped. `.bmp` is what a converter
  // should write now -- one picture format for the whole device, and a file
  // anything can open -- and `.tbi` is what the older ones wrote.
  void artEntryFor(const char* img, const char* ext, char* out, int cap) {
    char stem[200];
    snprintf(stem, sizeof(stem), "toybox/%s", img);
    char* dot = strrchr(stem, '.');
    const char* slash = strrchr(stem, '/');
    if (dot && (!slash || dot > slash)) *dot = 0;  // ".jpg" goes, a dotted folder stays
    snprintf(out, (size_t)cap, "%s%s", stem, ext);
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
  // A prepared .bmp inside the book. Streamed, never buffered: a BMP written
  // for this panel -- 1 bpp, 480 wide, 800 tall -- is the same 48 KB of bits
  // a .tbi is, in a wrapper the rest of the world can open, and its rows can
  // go straight to the glass as they arrive. Bottom-up storage (the usual
  // way) is not a problem for a one-pass read: the first row off the file is
  // simply the last row of the picture, and it is drawn there.
  //
  // Anything else -- another size, another depth, a top-down file -- is left
  // to the caller's .tbi fallback rather than decoded here. The converter is
  // told exactly what to write (docs/CONVERTER-SPEC.md); a book that ignores
  // that gets the honest "no picture prepared" plate instead of a slow decode
  // in the middle of a page turn.
  bool drawBmpPage(ToolsCanvas& c, const char* entry) {
    if (!_book.blobOpen(entry)) return false;
    uint8_t h[62];  // file header + DIB header + two palette entries
    if (!blobReadFull(h, sizeof(h))) {
      _book.blobClose();
      return false;
    }
    auto u16 = [&](int i) { return (uint32_t)h[i] | ((uint32_t)h[i + 1] << 8); };
    auto u32 = [&](int i) {
      return u16(i) | ((uint32_t)h[i + 2] << 16) | ((uint32_t)h[i + 3] << 24);
    };
    const uint32_t offBits = u32(10), dib = u32(14), comp = u32(30);
    const int32_t w = (int32_t)u32(18), rawH = (int32_t)u32(22);
    const uint32_t bpp = u16(28);
    const bool bottomUp = rawH > 0;
    const int32_t hh = bottomUp ? rawH : -rawH;
    if (dib < 40 || comp != 0 || bpp != 1 || w != tbimg::W || hh != tbimg::H ||
        offBits != sizeof(h)) {
      _book.blobClose();
      return false;
    }
    // Which bit is ink. The palette is read, never assumed: a BMP whose first
    // entry is white is a picture in negative, and plenty of tools write one.
    const bool zeroIsInk = (h[54] + h[55] + h[56]) < (h[58] + h[59] + h[60]);
    for (int r = 0; r < tbimg::H; r++) {
      if (!blobReadFull(epubui::g_imgBand, tbimg::STRIDE)) break;  // half a picture
      const int y = bottomUp ? tbimg::H - 1 - r : r;
      for (int xb = 0; xb < tbimg::STRIDE; xb++) {
        uint8_t v = epubui::g_imgBand[xb];
        if (!zeroIsInk) v = (uint8_t)~v;
        if (v == 0xFF) continue;  // a run of white, which is most of most art
        for (int k = 0; k < 8; k++)
          if (!(v & (0x80 >> k))) c.fillRect(xb * 8 + k, y, 1, 1, true);
      }
    }
    _book.blobClose();
    return true;
  }

  bool drawImagePage(ToolsCanvas& c) {
    char entry[224];
    // Reading another entry spends the chapter stream; ensureStream() rebuilds
    // it when a turn next needs it.
    _book.chapterClose();
    _streamLost = true;
    // The one format first, the old one after it: a book carrying both is a
    // book being converted, and the new picture is the one to believe.
    artEntryFor(_pageImage, ".bmp", entry, sizeof(entry));
    if (drawBmpPage(c, entry)) return true;
    artEntryFor(_pageImage, ".tbi", entry, sizeof(entry));
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
  // An illustration is a portrait object -- exactly 480x800, prepared for
  // this panel -- so a landscape canvas does not turn it, it CROPS it: on
  // glass, a cover lying on its side with its bottom half missing. The panel
  // therefore stands up for a picture page and lies back down for text.
  //
  // The LAYOUT is never re-measured for this. That is the whole trick: a
  // picture page has no lines to lay out, and the text pages either side keep
  // the measurements they already have at the reading rotation, so pagination
  // does not shift under the reader and no restyle is paid for. Only the
  // drawing (and the touch map that follows it) turns.
  //
  // Returns true if the panel turned, which needs a full refresh -- a quarter
  // turn changes every pixel and a partial cannot describe it.
  bool syncPageRot() {
    // The menu and the picker are portrait designs of their own, already
    // stood up by menuOpen; only the page itself answers to this.
    if (_screen != Screen::Page || _menu != rmenu::Page::None || _picking) return false;
    const int want = _pageImage[0] ? 0 : _rot;
    if (host().canvasRotation() == want) return false;
    host().setCanvasRotation(want);
    return true;
  }

  void paint() {
    if (syncPageRot()) {
      host().refresh(true);
      return;
    }
    host().refreshFast(rmenu::cleanEvery(mode()));
  }

  rmenu::Refresh mode() { return rmenu::refreshMode(prefs(), true); }

  // --- the panel --------------------------------------------------------------
  // Opened by the power button. Everything in here is about the book you are
  // in the middle of, which is why none of it lives in settings.

  void menuOpen() {
    _menu = rmenu::Page::Root;
    _mpage = 0;
    _resetArmed = false;  // a question does not survive the screen that asked it
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

  // Forget where this book was being read, and go back to its first page.
  //
  // Nothing is deleted: the position is SET to the beginning and saved the
  // ordinary way, which leaves a valid record on the card (and a 0% KOReader
  // sidecar beside it) instead of a hole that the legacy-directory fallback
  // would happily fill with an older position. Bookmarks are left alone --
  // they are phrases the reader chose to keep, not a place it happened to
  // stop, and losing them here would be a surprise nobody asked for.
  void startAgain() {
    _menu = rmenu::Page::None;
    ensureStream();
    applyRot(_rot);
    _books[_cur].cont = false;  // the shelf says "from the start" again
    if (!gotoPlace(0, 0)) {
      _note = "could not go back to the start";
      host().beep(2);
      host().refresh(true);
      return;
    }
    saveProgress();  // the card now says: the beginning
    syncPageRot();
    host().beep(1);
    host().refresh(true);
  }

  void menuClose() {
    _menu = rmenu::Page::None;
    ensureStream();  // the contents list reads other entries out of the zip
    applyRot(_rot);  // the page's rotation, back on (and a reflow if it changed)
    // ...unless the page under the menu is a picture, which is portrait
    // whatever the text around it does. The layout above stays measured at
    // the reading rotation either way.
    syncPageRot();
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
      rmenu::Item items[7];
      items[0].label = "Contents";
      // The chapter's name when the contents have already been read, and the
      // spine numbers when they have not: this line is drawn on the way INTO
      // the panel, and reading a book's contents to label a row is a cost the
      // person has not asked for yet. The numbers here were always the spine's
      // -- "chapter 4 of 57" over a page headed Chapter 1 -- which is the same
      // thing that made the shelf wrong; naming it is the honest fix, and the
      // count goes because a spine count is not a chapter count either.
      if (_ntoc > 0) {
        const int row = epubui::tocRowForSpine(epubui::g_toc, _ntoc, _spine);
        if (row >= 0)
          snprintf(_rootSub[0], sizeof(_rootSub[0]), "%s", epubui::g_toc[row].title);
        else
          snprintf(_rootSub[0], sizeof(_rootSub[0]), "chapter %d", _spine + 1);
      } else {
        snprintf(_rootSub[0], sizeof(_rootSub[0]), "chapter %d of %d", _spine + 1,
                 _book.spineCount());
      }
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
      const int rowFace = host().typefaceCount() > 1 ? n : -1;
      if (rowFace >= 0) {
        // A row that cycles in place -- but its answer is SHOWN, not named:
        // the line under the label is the book's own words drawn in the face,
        // so each tap is a live specimen rather than a name to imagine.
        items[n].label = "Typeface";
        items[n].sub = "";
        n++;
      }
      const int rowRot = n;
      items[n].label = "Rotation";
      items[n].sub = "";  // three buttons, drawn below
      n++;
      // Forgetting where you were is a per-book thing, so it lives with the
      // book rather than in settings: the panel is already the screen about
      // THIS book. Two taps, like every other irreversible row on the device.
      const int rowReset = n;
      items[n].label = "Start again";
      items[n].sub = _resetArmed ? "tap again to forget this book's place"
                                 : (_books[_cur].cont ? "back to the first page" : "already at the start");
      n++;
      items[n].label = "Close the book";
      items[n].sub = _books[_cur].title;
      rmenu::drawRoot(host(), c, "Options", items, n + 1);
      if (_resetArmed) {  // the armed row inverts, as the reset in settings does
        const TRect r = rmenu::rootRect(rowReset, c.width());
        c.fillRect(r.x, r.y + 1, r.w, r.h - 2, true);
        c.text(r.x + 8, r.y + 20, "Start again", TS_LARGE, false);
        c.text(r.x + 8, r.y + 62, "tap again to forget this book's place", TS_SMALL, false);
      }
      if (rowFace >= 0) {
        const TRect r = rmenu::rootRect(rowFace, c.width());
        char sample[120];
        // The face's name, then the page being read -- the words already on
        // the reader's mind are the fairest test of a font.
        snprintf(sample, sizeof(sample), "%s - %s", faceName(_face),
                 _lineN > 0 && _lines[0].t[0] ? _lines[0].t : "The quick brown fox");
        FaceScope fs(host(), _face);
        c.textClipped(r.x + 8, r.y + 58, r.w - 16, sample, TS_MED, true);
      }
      // The turn itself still waits for the panel to close, so the panel is
      // never asked to draw itself sideways.
      rmenu::drawRotRow(c, rowRot, _rot);
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
      // The steppers live in reader_menu.h now; the recipe app offers the
      // same two and should not have to draw its own circles.
      for (int r = 0; r < 2; r++)
        rmenu::drawStepper(c, 110 + r * rmenu::STEP_H, labels[r], values[r]);
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
        // How LONG it is, not which chapter it is. A book that carries its own
        // contents has already numbered its chapters, and the spine index
        // counts different things -- the cover, the copyright page, the
        // inserts -- so printing it put "chapter 4" under a row the book
        // itself calls "Chapter 1: Maomao". Two numbering systems on one row,
        // and the one the reader trusts is the book's. Length is the fact the
        // book has not already given, and the one that decides whether to
        // start a chapter now.
        char len[24];
        chapterLength(spine, len, sizeof(len));
        if (idx == 0)
          snprintf(buf, sizeof(buf), "the start \xc2\xb7 %s", len);
        else
          snprintf(buf, sizeof(buf), "%s", len);
        rmenu::drawRow(c, k, label, buf, shelf::rowSep(k, idx, total), spine == _spine);
      } else {
        char label[marks::LABEL + 8], where[40];
        markLabel(_marks[idx], label, sizeof(label));
        snprintf(where, sizeof(where), "chapter %u, page %u", (unsigned)(_marks[idx].spine + 1),
                 (unsigned)(_marks[idx].page + 1));
        // Two lines for the phrase: it is a sentence somebody chose, and the
        // half past the clip was usually the half they chose it for.
        rmenu::drawRowWrap(c, k, label, where, shelf::rowSep(k, idx, total),
                           _marks[idx].spine == (uint16_t)_spine &&
                               _marks[idx].page == (uint16_t)_page);
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
      const int rowReset = rowRot + 1;
      const int rowClose = rowReset + 1;
      const int hit = rmenu::hitRoot(x, y, rowClose + 1, W);
      // Any tap that is not the armed row itself disarms it -- the same rule
      // the settings reset follows, so a question never outlives the screen.
      const bool wasArmed = _resetArmed;
      if (wasArmed && hit != rowReset) _resetArmed = false;
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
      if (hit == rowReset) {
        if (!wasArmed) {
          _resetArmed = true;
          host().beep(2);
          paint();
          return;
        }
        _resetArmed = false;
        startAgain();
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
        const int want = rmenu::hitRot(x, y, rowRot, W);
        if (want < 0 || want == _rot) return;  // already there: nothing to say
        _rot = (uint8_t)want;
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
        const int step = rmenu::hitStepper(x, y, 110 + r * rmenu::STEP_H,
                                           host().canvas().width());
        if (step == 0) continue;
        uint8_t& v = r == 0 ? _size : _lead;
        const int lim = r == 0 ? epubui::SIZES : epubui::LEADS;
        const int nv = (int)v + step;
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
    // Each line in its own type, and in runs where the STYLE changes inside
    // it: the parser said which bytes were inside a <b> or <strong> and which
    // inside an <i>, <em> or <cite>, and a heading line carries its level.
    // Drawn left to right, each run measured as it is placed, which is the
    // same arithmetic the layout did.
    for (int i = 0; i < _lineN; i++) {
      const Line& ln = _lines[i];
      const TSize lts = epubui::sizeFor(_size, ln.head);
      const int n = (int)strlen(ln.t);
      int x = epubui::MARGIN;
      // A heading is bold throughout, and bold beats italic (see gfx.h), so
      // both questions are answered per byte and a run is a stretch where
      // neither answer changes.
      auto boldOf = [&](int i2) { return ln.boldAt(i2) || ln.head != 0; };
      auto italOf = [&](int i2) { return ln.italAt(i2) && !boldOf(i2); };
      for (int a = 0; a < n;) {
        const bool bold = boldOf(a), ital = italOf(a);
        int b = a + 1;
        while (b < n && boldOf(b) == bold && italOf(b) == ital) b++;
        char seg[201];
        const int len = b - a;
        memcpy(seg, ln.t + a, (size_t)len);
        seg[len] = 0;
        c.text(x, ln.y, seg, lts, true, bold, ital);
        x += c.textWidth(seg, lts, bold, ital);
        a = b;
      }
    }
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
  bool _browsing = false;    // this app owns the card's shelf session
  bool _resetArmed = false;  // "Start again" asked once and waiting on a second tap
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
  uint8_t _pendStyle = 0;  // the markup the carried word was wearing
  bool _pendValid = false;
  Line _lines[epubui::MAX_LINES];
  int _lineN = 0;
};
