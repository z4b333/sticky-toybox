// The EPUB reader: real ebooks off the SD card, laid out live with the
// device fonts, one chapter streaming at a time.
//
// The design promise is the reading position. It is stored on the CARD, in
// the book's own folder (/.toybox/books/<book>/position.bin: a spine index
// plus a visible-codepoint offset, so it survives any change of font, size or
// rotation), and shared with Readest and KOReader through progress sync
// (kosync.h). Everything else is deliberately plain: no CSS, the layout
// re-derived on this device every time.
//
// Paging works on a stream. A page is laid out by pulling words until the
// glass is full; turning forward continues the stream, turning back replays
// the chapter to the previous page's recorded start offset (the offsets seen
// so far live in a little per-chapter table). Replay costs one re-inflate of
// the chapter, which disappears inside the panel's own refresh time.
#pragma once
#include "book_thumbs.h"
#include "epub/epub_art.h"
#include "epub/epub_cover.h"
#include "epub/epubcore.h"
#include "thai_words.h"
#include "bookmarks.h"
#include "decor.h"
#include "../chrome.h"
#include "flash_qr.h"
#include "help.h"
#include "series.h"
#include "home_net.h"
#include "kosync.h"
#include "readest_hl.h"
#include "lib_web.h"
#include "lock_image.h"
#include "reader_menu.h"
#include "recents.h"
#include "read_order.h"
#include "../home_style.h"
#include "shelf.h"
#include "sync_page.h"
#include "tools_ui.h"

namespace epubui {
inline constexpr int MARGIN = 24;
// v2: the corner marks -- Back (peeled corner) top-left, More top-right --
// are invisible while reading and drawn in full, on white plates, while the
// footer is up. The first book ever opened shows a guide to the tap zones.
inline constexpr int TOP = 18;
inline constexpr int MARK = 36;
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
// v2 fine tuning (kit "Text"): side margins and the gap between paragraphs,
// three steps each, beside size and line spacing.
inline constexpr int MARGINS = 3, PARAS = 3;
inline int marginPx(int i) { return i <= 0 ? 14 : (i == 1 ? 24 : 40); }
inline const char* marginName(int i) { return i <= 0 ? "narrow" : (i == 1 ? "normal" : "wide"); }
inline int paraPx(int i) { return i <= 0 ? 4 : (i == 1 ? PARA_GAP : 28); }
inline const char* paraName(int i) { return i <= 0 ? "small" : (i == 1 ? "normal" : "large"); }
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
  int fontSlot() const override { return ToolsHost::FONT_READER; }
  // A book open at a page, with nothing over it. Not the shelf, not the
  // options panel, not the middle of picking a phrase to keep.
  bool showingOwnPage() const override {
    return _open && _screen == Screen::Page && _menu == rmenu::Page::None && !_picking;
  }

  ~EpubTool() override {
    if (_host) libLeave(false);
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
    _margin = (uint8_t)prefs().getUInt("rd_marg", 1);
    _para = (uint8_t)prefs().getUInt("rd_para", 1);
    if (_margin >= epubui::MARGINS) _margin = 1;
    if (_para >= epubui::PARAS) _para = 1;
    // The face is one choice now, not two. It used to live in "rd_face" as a
    // number 0 or 1; it lives in the Books slot as a NAME, which can equally
    // be a family off the card. A device that was reading in Literata is
    // carried across the first time it opens the reader.
    if (!h.fontFor(ToolsHost::FONT_READER)[0]) {
      const uint32_t old = prefs().getUInt("rd_face", 0);
      if (old == 1 && h.typefaceCount() > 1) h.fontSet(ToolsHost::FONT_READER, h.typefaceName(1));
      prefs().remove("rd_face");
    }
    _face = faceIndexOf(h, h.fontFor(ToolsHost::FONT_READER));
    // The card's families, listed while the bus is still free. Once a book is
    // open the reader owns the card, and asking then answers "no card" to a
    // device with one plainly in it.
    _famN = (int8_t)h.fontFamilies(_famNames, MAX_FAMS);
    _rot = (uint8_t)prefs().getUInt("rd_rot", 0);
    if (_rot != 1 && _rot != 3) _rot = 0;
    if (_size >= epubui::SIZES) _size = 0;
    if (_lead >= epubui::LEADS) _lead = 1;
    if (!_lut) _lut = (uint32_t*)malloc(sizeof(uint32_t) * epubui::MAX_PAGES);
  }

  void render(ToolsCanvas& c) override {
    if (_screen == Screen::Loading) {
      bthumb::drawLoading(host(), c, _books[_cur].file, _books[_cur].title);
      if (_artTotal > 0) {
        // The first open's wait, said plainly and counted, on a white band
        // over the cover so it reads whatever the cover is.
        const int W = c.width();
        c.fillRect(24, 640, W - 48, 112, false);
        c.drawRect(24, 640, W - 48, 112, 2, true);
        char line[48];
        snprintf(line, sizeof(line), "preparing artwork %d of %d", _artDone + 1, _artTotal);
        c.textCentered(W / 2, 662, line, TS_MED, true);
        c.textCentered(W / 2, 712, "first open only - later opens are quick", TS_SMALL, true);
      }
      return;
    }
    if (_screen == Screen::Library) {
      renderLibrary(c);
      return;
    }
    if (_screen == Screen::Page) {
      if (_picking) {
        renderPick(c);
        return;
      }
      if (_menu == rmenu::Page::Keep) {
        renderKeep(c);
        return;
      }
      if (_menu != rmenu::Page::None) {
        renderMenu(c);
        return;
      }
      renderPage(c);
      if (_auto == AUTO_OPEN)
        drawAutoStrip(c, "SYNCING", "Checking Readest", "your place and highlights");
      else if (_auto != AUTO_NONE) {
        char sub[80] = "no Wi-Fi: it goes on anyway";
        const int w = rhl::hl(prefs()) ? hlWaiting() : 0;
        if (w) snprintf(sub, sizeof(sub), "and %d highlight%s \xc2\xb7 no Wi-Fi: it goes on anyway", w, w == 1 ? "" : "s");
        drawAutoStrip(c, _auto == AUTO_SLEEP ? "SYNCING \xc2\xb7 THEN SLEEP" : "SYNCING \xc2\xb7 CLOSING",
                      "Sending your place to Readest", sub);
      }
      return;
    }
    // Inside a series the bar carries the series name, because that is the
    // only thing on the screen that says where you are.
    if (_inGroup >= 0)
      host().topBar(_groups[_inGroup].name, true, inFolder() ? seriesName() : title());
    else
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
      const Row r = rowAt(idx);
      if (r.kind == Row::Library) {
        shelf::drawBookRow(c, k, "From Readest", "your Readest library - download a book",
                           shelf::rowSep(k, idx, total));
        continue;
      }
      if (r.kind == Row::Folder) {
        const char* nm = _folders[r.i].name;
        shelf::drawFolderRow(c, k, nm[0] == '/' ? nm + 1 : nm, _folders[r.i].count,
                             shelf::rowSep(k, idx, total));
        continue;
      }
      if (r.kind == Row::Group) {
        shelf::drawFolderRow(c, k, _groups[r.i].name, _groups[r.i].count,
                             shelf::rowSep(k, idx, total));
        continue;
      }
      const int b = r.i;
      char sub[ToolsHost::PLACE_LEN + 24];
      // Inside a series the volume is the name worth reading: "Volume 02",
      // not forty bytes of the series title cut off before the number.
      // The whole name, from the file: the title field keeps only 40 bytes,
      // which cut "Classroom of the Elite - Volume 01 [Seven Seas]" at
      // "[Seve". The row wraps it over two lines and ends it with "..." only
      // when even that is not enough.
      char full[128];
      const char* bare = strrchr(_books[b].file, '/');
      snprintf(full, sizeof(full), "%s", bare ? bare + 1 : _books[b].file);
      if (char* dot = strrchr(full, '.')) *dot = 0;
      const char* name = full[0] ? full : _books[b].title;
      series::Parsed sp;
      if (_inGroup >= 0 && series::parse(_books[b].file, sp)) name = sp.label;
      shelf::drawBookRow(c, k, name, placeLine(_books[b], sub, sizeof(sub)),
                         shelf::rowSep(k, idx, total), _books[b].synced);
    }
    shelf::drawPager(c, _lpage, total);
    c.textCentered(c.width() / 2, 770,
                   _note ? _note : "positions are kept on the card, with each book",
                   TS_SMALL, true);
  }

  void onTap(int x, int y) override {
    if (_screen == Screen::Library) {
      libTap(x, y);
      return;
    }
    if (_screen == Screen::List) {
      if (host().isBackTap(x, y)) {
        // The back arrow climbs one level at a time: out of the series
        // first, out of the app second. One arrow, no second control.
        host().beep(1);
        if (_inGroup >= 0) {  // out of the series row, back to its level
          _inGroup = -1;
          buildView();
          _lpage = _groupPage;
          host().refresh(!_browsing);
        } else if (inFolder()) {
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
        if (t == help::Tap::Toggle) {
          host().beep(0);
          paint();
          return;
        }
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
      const Row r = rowAt(idx);
      if (r.kind == Row::Library) {
        libEnter();
        return;
      }
      if (r.kind == Row::Folder) {
        openFolder(r.i);
        return;
      }
      if (r.kind == Row::Group) {
        _groupPage = _lpage;
        _inGroup = r.i;
        buildView();
        _lpage = 0;
        host().beep(0);
        host().refresh(!_browsing);
        return;
      }
      openBook(r.i);
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
    if (_guide) {
      _guide = false;
      prefs().putBool("ep_guide", true);
      host().beep(1);
      host().refresh(true);
      return;
    }
    // The page (v2). While it follows the device, the plate at the top locks
    // it, and any other tap first takes the device's angle.
    if (_rotFollow) {
      if (rmenu::lockPlate(host().canvas().width()).hit(x, y)) {
        _rotFollow = false;
        host().beep(1);
        host().refresh(false);
        return;
      }
      if (followDevice()) return;
    }
    // Top-left closes, top-right opens More (v2), thirds turn, and the
    // middle toggles the footer.
    if (x < 110 && y < 50) {
      closeBook(true);
      return;
    }
    if (rmenu::moreCorner(host().canvas().width()).hit(x, y)) {
      moreOpen();
      return;
    }
    // A highlight's own words open its card (owner, 26 Nov), wherever on the
    // page they are: the words are what was aimed at, not the turn zone.
    for (int i = 0; i < _ulN; i++) {
      const UlRect& u = _ul[i];
      if (x >= u.x - 6 && x < u.x + u.w + 6 && y >= u.y - 8 && y < u.y + u.h + 8 && u.mark < _nmarks) {
        hlCardOpen(u.mark);
        return;
      }
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
        moreOpen();
      else if (_menu == rmenu::Page::HlCard || _menu == rmenu::Page::More ||
               _menu == rmenu::Page::TextBar || _menu == rmenu::Page::StartBar)
        menuClose();
      else
        menuBack();
      return true;
    }
    if (_menu == rmenu::Page::HlCard || _menu == rmenu::Page::TextBar ||
        _menu == rmenu::Page::StartBar) {  // the bar or card goes; the button turns as ever
      _menu = rmenu::Page::None;
      turn(b == SideBtn::Down ? 1 : -1);
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
  bool hostMoreOpen() const { return _menu == rmenu::Page::More; }
  int hostRot() const { return _rot; }
  bool hostFollowing() const { return _rotFollow; }
  bool hostGuide() const { return _guide; }
  // What the shelf row for book i currently claims: true means it draws
  // "carries on where it stopped".
  int hostBookCount() const { return _n < 0 ? 0 : _n; }
  // Forces a row back to "from the start", so a guard can stand where a
  // freshly-listed shelf stands without deleting anything off the card.
  void hostSetCont(int i, bool on) {
    if (i >= 0 && i < (_n < 0 ? 0 : _n)) _books[i].cont = on;
  }
  // Pretends the contents could not be read, so a guard can walk the path
  // books with a broken NCX actually take.
  void hostForgetToc() { _ntoc = 0; }
  void hostPlace(int i, char* out, int cap) const {
    snprintf(out, (size_t)cap, "%s", (i >= 0 && i < (_n < 0 ? 0 : _n)) ? _books[i].place : "");
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
  bool hostXp(int spine, uint32_t off, char* out, int cap) {
    const bool r = _book.xpointerFor(spine, off, out, cap);
    _streamLost = true;
    return r;
  }
  bool hostLocate(const char* xp, int& spine, uint32_t& off) {
    const bool r = _book.offsetFor(xp, spine, off);
    _streamLost = true;
    return r;
  }
  bool hostDocHash(char out[33]) { return kosync::docHash(_io, out); }
  int hostSyncStage() const { return _syncStage; }
  int hostLibStage() const { return _libStage; }
  int hostLibCount() const { return _libN; }
  bool hostLibLocal(int i) const { return i >= 0 && i < _libN && _libLocal[i]; }
  const char* hostLibTitle(int i) const { return i >= 0 && i < _libN ? _lib[i].title : ""; }
  bool hostLibMore() const { return _libMore; }
  libweb::Server& hostWeb() { return _web; }
  bool hostLibWifi() const { return _libWifi; }
  void hostSyncTick() { tick(); }
  int hostSyncOut() const { return _syncOut; }
  const char* hostCurFile() const { return _cur >= 0 ? _books[_cur].file : ""; }
  bool hostBookSynced(const char* file) const {
    for (int i = 0; i < (_n < 0 ? 0 : _n); i++)
      if (!strcmp(_books[i].file, file)) return _books[i].synced;
    return false;
  }
  double hostBookFraction() const { return bookFraction(); }
  const char* hostPageImage() const { return _pageImage; }
  int hostPageImagePart() const { return _pageImagePart; }
  int hostArtParts(const char* entry) const { return artParts(entry); }
  uint32_t hostArtUsize(const char* entry) const {
    const ArtRec* r = artFor(entry);
    return r ? r->usize : 0;
  }
  bool hostArtPrepared(const char* entry) const {
    const ArtRec* r = artFor(entry);
    return r && r->parts;
  }
  // Card paths of one part of a prepared picture, for a guard.
  bool hostArtPaths(const char* entry, int part, char* g2, char* b1, int cap) const {
    const ArtRec* r = artFor(entry);
    if (!r || !r->parts || part >= r->parts) return false;
    epubart::paths(_books[_cur].file, entry, r->usize, part, g2, b1, cap);
    return true;
  }
  const char* hostBookFile() const { return _cur >= 0 ? _books[_cur].file : ""; }
  int hostMenu() const { return (int)_menu; }
  bool hostPicking() const { return _picking; }
  const char* hostPhrase() const { return _phrase; }
  int hostLineY(int i) const { return i < _lineN ? _lines[i].y : -1; }
  const char* hostMarkLabel(int i) const { return i < _nmarks ? _marks[i].label : ""; }
  int hostMarkCount() const { return _nmarks; }
  const char* hostLineText(int i) const { return i < _lineN ? _lines[i].t : ""; }
  int hostMarkSrc(int i) const { return i < _nmarks ? _marks[i].src : -1; }
  const char* hostMarkId(int i) const { return i < _nmarks ? _marks[i].id : ""; }
  bool hostMarkHere(int i) const { return i < _nmarks && markHere(i); }
  int hostHlAdded() const { return _hlRes.added; }
  int hostHlRemoved() const { return _hlRes.removed; }
  int hostHlSent() const { return _hlSent; }
  int hostUnderlines() const { return _ulCount; }
  // The old Rotation row's three buttons, for the harness: the angle the
  // page takes when the panel closes.
  void hostSetRot(int r) {
    _rot = (uint8_t)r;
    prefs().putUInt("rd_rot", _rot);
  }
  // The old panel's Sync row, for the harness: the sync page, Back to the panel.
  void hostSyncFromPanel() {
    _fromMore = false;
    _menu = rmenu::Page::Sync;
    _syncStage = kosync::haveAccount(prefs()) ? 1 : 3;
    paint();
  }
  int hostUlCount() const { return _ulN; }
  TRect hostUlRect(int i) const { return TRect{_ul[i].x, _ul[i].y, _ul[i].w, _ul[i].h}; }
  int hostHlCard() const { return _hlCard; }
  bool hostHlArmed() const { return _hlArmed; }
  int hostListArmed() const { return _listArmed; }
  void hostSaveMarks() { marks::save(host(), _books[_cur].file, _marks, _nmarks); }
  TRect hostHlRemoveRect() {
    static char q[3][160];
    int n = 0;
    return hlRemoveRect(hlCardRect(host().canvas(), n, q));
  }
  void hostAddReadestMark(const char* id, uint32_t off, uint32_t end, const char* label) {
    marks::Mark m{};
    m.spine = (uint16_t)_spine;
    m.page = marks::NO_PAGE;
    m.off = off;
    m.end = end;
    m.src = marks::SRC_READEST;
    snprintf(m.id, sizeof(m.id), "%s", id);
    snprintf(m.label, sizeof(m.label), "%s", label);
    marks::add(_marks, _nmarks, m);
    marks::save(host(), _books[_cur].file, _marks, _nmarks);
  }
  void hostRemoveById(const char* id) {
    for (int i = 0; i < _nmarks; i++)
      if (!strcmp(_marks[i].id, id)) {
        marks::remove(_marks, _nmarks, i);
        marks::save(host(), _books[_cur].file, _marks, _nmarks);
        return;
      }
  }
  int hostHlDead() const { return _hlDead; }
  int hostHlWaiting() const { return hlWaiting(); }
  uint32_t hostMarkOff(int i) const { return i < _nmarks ? _marks[i].off : 0; }
  uint32_t hostMarkEnd(int i) const { return i < _nmarks ? _marks[i].end : 0; }
  int hostMarkFlags(int i) const { return i < _nmarks ? _marks[i].flags : -1; }
  // A highlight as part A and before kept it: no id, no range.
  void hostAddOldMark(const char* label) {
    marks::Mark m{};
    m.spine = (uint16_t)_spine;
    m.page = (uint16_t)_page;
    m.off = curOffset();
    snprintf(m.label, sizeof(m.label), "%s", label);
    marks::add(_marks, _nmarks, m);
    marks::save(host(), _books[_cur].file, _marks, _nmarks);
  }
  bool hostTextWaiting(const char* id) {
    char t[64];
    return marks::loadText(host(), _books[_cur].file, id, t, sizeof(t));
  }
  int hostLineN() const { return _lineN; }
  // Picks words (line, word)..(line, word) as two taps would, then keeps them.
  void hostKeepWords(int l0, int w0, int l1, int w1) {
    _pickL0 = l0;
    _pickW0 = w0;
    _pickL1 = l1;
    _pickW1 = w1;
    buildPhrase();
    keepMark(_phrase);
  }
  int hostAuto() const { return _auto; }
  void hostReloadMarks() { _nmarks = marks::load(host(), _books[_cur].file, _marks); }
  // The list's second tap on the row you are on, without the list.
  void hostRemoveMark(int at) {
    dropMark(at);
  }
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
  void hostOpen(int i) { openBook(i); }
  int hostCurIndex() const { return _cur; }
  const char* hostDir() const { return _dir; }
  int hostFolders() const { return _nf; }
  int hostGroups() const { return _ng; }
  int hostInGroup() const { return _inGroup; }
  const char* hostGroupName(int g) const { return g >= 0 && g < _ng ? _groups[g].name : ""; }
  int hostViewBook(int i) const { return i >= 0 && i < _nv ? _view[i] : -1; }
  int hostViewCount() const { return _nv; }
  int hostItems() const { return items(); }
  int hostListPage() const { return _lpage; }
#endif

 private:
  enum class Screen : uint8_t { List, Loading, Page, Library };

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

  // D: is an automatic sync possible at all? Readest signed in, a home Wi-Fi.
  bool autoReady() {
    return _host && kosync::isReadest(prefs()) && homenet::haveWifi(prefs());
  }
  // The kit's banner strip across the page while an automatic sync runs.
  void drawAutoStrip(ToolsCanvas& c, const char* label, const char* title, const char* sub) {
    const int W = c.width(), H = c.height();
    const int h = 150, y = (H - h) / 2;
    c.fillRect(0, y, W, h, false);
    c.fillRect(0, y, W, 3, true);
    c.fillRect(0, y + h - 3, W, 3, true);
    c.textTracked(28, y + 24, label, TS_SMALL, true, true, 3);
    c.text(28, y + 24 + c.textHeight(TS_SMALL) + 6, title, TS_LARGE, true, true);
    c.text(28, y + 24 + c.textHeight(TS_SMALL) + 12 + c.textHeight(TS_LARGE), sub, TS_SMALL, true);
  }
  // Is mark i on the page being read? A highlight from Readest has no page in
  // this layout, so its offset decides.
  bool markHere(int i) const {
    const marks::Mark& m = _marks[i];
    if (m.spine != (uint16_t)_spine) return false;
    if (m.page != marks::NO_PAGE) return m.page == (uint16_t)_page;
    if (_page >= _lutN) return false;
    const uint32_t a = epubui::pageOff(_lut[_page]);
    const uint32_t z = _page + 1 < _lutN ? epubui::pageOff(_lut[_page + 1]) : 0xFFFFFFFFu;
    return m.off >= a && m.off < z;
  }
  // D: the sync on the way out -- the book closing, or the Sticky going to
  // sleep. The strip over the page, then the place sent; nothing asked.
  void syncOnTheWayOut(bool sleeping) {
    if (!_open || _cur < 0 || !autoReady() || !rhl::onClose(prefs())) return;
    _menu = rmenu::Page::None;
    _picking = false;
    _auto = sleeping ? AUTO_SLEEP : AUTO_CLOSE;
    host().setCanvasRotation(_rot);
    host().refresh(false);  // the page, with the strip across it
    runSync(true);
    _auto = AUTO_NONE;
    char line[64];
    snprintf(line, sizeof(line), "sync: on the way out (%s) done", sleeping ? "sleep" : "close");
    host().perfLog(line);
  }
  void beforeSleep() override { syncOnTheWayOut(true); }

  bool inFolder() const { return !shelf::isTop(_dir); }
  const char* seriesName() const {
    const char* s = strrchr(_dir, '/');
    return s ? s + 1 : _dir;
  }
  // What a level of the shelf shows, in order: real folders, then series
  // rows (books grouped by their names, series.h), then the books that
  // belong to no series. Inside a series row: its books, in volume order.
  int items() const {
    if (_n < 0) return 0;
    if (_inGroup >= 0) return _nv;
    return libRow() + (_nf < 0 ? 0 : _nf) + _ng + _nv;
  }
  // The Readest library's door: first row of the top shelf, once signed in
  // to Readest and a home Wi-Fi is saved.
  int libRow() const {
    return (!inFolder() && _inGroup < 0 && _host && kosync::isReadest(_host->prefs()) &&
            homenet::haveWifi(_host->prefs()))
               ? 1
               : 0;
  }
  struct Row {
    enum Kind : uint8_t { Folder, Group, Book, Library } kind;
    int i;
  };
  Row rowAt(int idx) const {
    if (_inGroup >= 0) return {Row::Book, _view[idx]};
    if (libRow()) {
      if (idx == 0) return {Row::Library, 0};
      idx--;
    }
    if (idx < _nf) return {Row::Folder, idx};
    if (idx < _nf + _ng) return {Row::Group, idx - _nf};
    return {Row::Book, _view[idx - _nf - _ng]};
  }

  // One listing call per level. Folders only exist at the top: a series of a
  // series is a filing system, not a shelf. Series rows exist at any level.
  void reload() {
    _nf = inFolder() ? 0 : host().shelfFolders(_folders, shelf::MAX_FOLDERS, ".epub");
    if (_nf < 0) _nf = 0;
    _n = host().epubList(_books, epubui::MAX_BOOKS, _dir);
    _lpage = 0;
    regroup();
  }

  // Which books are volumes of one series. A group needs two members; the
  // series row is named as the first file spells it. The open series row
  // survives a re-list (closing a book re-lists) when it is still there.
  void regroup() {
    char keep[series::NAME];
    snprintf(keep, sizeof(keep), "%s", _inGroup >= 0 ? _groups[_inGroup].name : "");
    _ng = 0;
    _inGroup = -1;
    static series::Parsed parsed[epubui::MAX_BOOKS];  // static: the loop task's stack
    bool has[epubui::MAX_BOOKS];
    const int n = _n < 0 ? 0 : _n;
    for (int i = 0; i < n; i++) {
      has[i] = series::parse(_books[i].file, parsed[i]);
      _bookGroup[i] = -1;
    }
    for (int i = 0; i < n; i++) {
      if (!has[i] || _bookGroup[i] >= 0) continue;
      int members = 0;
      for (int j = i; j < n; j++)
        if (has[j] && _bookGroup[j] < 0 && series::sameKey(parsed[i].key, parsed[j].key)) members++;
      if (members < 2 || _ng >= shelf::MAX_FOLDERS) continue;
      Group& g = _groups[_ng];
      snprintf(g.name, sizeof(g.name), "%s", parsed[i].key);
      g.count = 0;
      for (int j = i; j < n; j++)
        if (has[j] && _bookGroup[j] < 0 && series::sameKey(parsed[i].key, parsed[j].key)) {
          _bookGroup[j] = (int8_t)_ng;
          _volume[j] = parsed[j].volume;
          g.count++;
        }
      _ng++;
    }
    // Series rows in name order, like folders on a shelf.
    for (int a = 1; a < _ng; a++)
      for (int b = a; b > 0 && strcasecmp(_groups[b - 1].name, _groups[b].name) > 0; b--) {
        const Group t = _groups[b];
        _groups[b] = _groups[b - 1];
        _groups[b - 1] = t;
        for (int k = 0; k < n; k++)
          if (_bookGroup[k] == b) _bookGroup[k] = -2;
        for (int k = 0; k < n; k++)
          if (_bookGroup[k] == b - 1) _bookGroup[k] = (int8_t)b;
        for (int k = 0; k < n; k++)
          if (_bookGroup[k] == -2) _bookGroup[k] = (int8_t)(b - 1);
      }
    for (int g = 0; keep[0] && g < _ng; g++)
      if (!strcmp(_groups[g].name, keep)) _inGroup = g;
    buildView();
  }

  // The books listed at this level: ungrouped ones most recently read first
  // (owner, 26 Nov), the never-read after them in card order; or -- inside a
  // series row -- that row's books by volume.
  void buildView() {
    _nv = 0;
    const int n = _n < 0 ? 0 : _n;
    for (int i = 0; i < n; i++)
      if (_bookGroup[i] == (_inGroup >= 0 ? _inGroup : -1)) _view[_nv++] = (int16_t)i;
    if (_inGroup < 0 && _nv > 1) {
      const readorder::Table& t = readorder::current(host());
      static uint32_t rk[epubui::MAX_BOOKS];
      for (int k = 0; k < n; k++) rk[k] = readorder::rank(t, _books[k].file);
      for (int a = 1; a < _nv; a++)  // stable: equal ranks keep card order
        for (int b = a; b > 0 && rk[_view[b - 1]] < rk[_view[b]]; b--) {
          const int16_t tmp = _view[b];
          _view[b] = _view[b - 1];
          _view[b - 1] = tmp;
        }
    }
    if (_inGroup >= 0)
      for (int a = 1; a < _nv; a++)
        for (int b = a; b > 0 && _volume[_view[b - 1]] > _volume[_view[b]]; b--) {
          const int16_t t = _view[b];
          _view[b] = _view[b - 1];
          _view[b - 1] = t;
        }
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
    _inGroup = -1;
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

  // Where this book's position lives: its own folder, beside its cover,
  // pictures and bookmarks (book_store.h).
  void progressPath(char* out, int cap) { bstore::file(_books[_cur].file, "position.bin", out, cap); }

  // Before v2 the position lived in CrossPoint's folders -- written to both
  // /.crosspoint/epub_<murmur32> (CrossPoint's own) and epub_<fnv64> (older
  // Toybox), with the shelf's chapter note (toybox.pos) beside the second.
  // Toybox no longer shares a card with CrossPoint (owner, 18 Nov), so a book
  // opened for the first time on this build moves its place across once:
  // read (CrossPoint's folder first -- it was the fresher of the two), write
  // here, then delete the old files and the folders if that leaves them
  // empty. CrossPoint's own cache files, if any, are never touched.
  // Returns the bytes read into `buf`, or 0.
  int adoptOldPosition(uint8_t* buf, int cap) {
    char murmur[96], fnv[96], path[128];
    epubc::cacheDirLegacy(_books[_cur].file, murmur, sizeof(murmur));
    epubc::cacheDir(_books[_cur].file, fnv, sizeof(fnv));
    snprintf(path, sizeof(path), "%s/progress.bin", murmur);
    int n = host().sdReadFile(path, buf, cap);
    if (n <= 0) {
      snprintf(path, sizeof(path), "%s/progress.bin", fnv);
      n = host().sdReadFile(path, buf, cap);
    }
    if (n <= 0) return 0;
    char to[bstore::PATH_CAP];
    progressPath(to, sizeof(to));
    if (!host().sdWriteFileAtomic(to, buf, n)) return n;  // keep the old ones: nothing lost
    char note[16 + ToolsHost::PLACE_LEN];
    snprintf(path, sizeof(path), "%s/toybox.pos", fnv);
    const int m = host().sdReadFile(path, note, sizeof(note));
    if (m > 0) {
      bstore::file(_books[_cur].file, "place.txt", to, sizeof(to));
      host().sdWriteFileAtomic(to, note, m);
    }
    snprintf(path, sizeof(path), "%s/progress.bin", murmur);
    host().sdRemove(path);
    snprintf(path, sizeof(path), "%s/progress.bin", fnv);
    host().sdRemove(path);
    snprintf(path, sizeof(path), "%s/toybox.pos", fnv);
    host().sdRemove(path);
    host().sdRmdir(murmur);  // empty only
    host().sdRmdir(fnv);
    host().sdRmdir("/.crosspoint");
    host().perfLog("position: moved from .crosspoint into the book folder");
    return n;
  }

  // A page turn does not save on the spot: the place goes to the card once
  // the reader pauses (SAVE_DELAY_MS after the last turn), or sooner when the
  // book closes, a menu opens, the app is left or the device powers off --
  // flush() and every direct saveProgress() call clear the pending one. Three
  // atomic writes per turn were ~420 ms of a ~1.15 s turn, measured on the
  // device; a fast reader now pays for one save per pause instead.
  static constexpr uint32_t SAVE_DELAY_MS = 3000;
  void saveSoon() {
    _saveDue = host().nowMs() + SAVE_DELAY_MS;
    if (_saveDue == 0) _saveDue = 1;  // 0 means "nothing pending"
  }
  bool wantsTick() const override {
    return _saveDue != 0 || _syncStage == 1 || _syncStage == 3 || _syncStage == 4 ||
           _screen == Screen::Library || following();
  }
  // v2: while the page follows the device it watches the angle by itself,
  // and the host may nap between looks.
  bool following() const {
    return _rotFollow && _open && _screen == Screen::Page && _menu == rmenu::Page::None &&
           !_picking;
  }
  bool napsOk() const override {
    return following() && _saveDue == 0 && _syncStage == 0;
  }
  void tick() override {
    if (following() && host().nowMs() - _lastLook >= 400) {
      _lastLook = host().nowMs();
      followDevice();
    }
    if (_screen == Screen::Library) {
      libTick();
      return;
    }
    if (_syncStage == 3 || _syncStage == 4) {
      if (signInStep()) paint();
      return;
    }
    if (_syncStage == 1) {
      // The "syncing" screen went up with the tap; this is the wait it
      // promised, blocking, with every step on a deadline.
      runSync();
      _syncStage = 2;
      if (_auto == AUTO_OPEN) {
        _auto = AUTO_NONE;
        // Only a question needs the Sync page; otherwise straight back to the
        // page, now with Readest's highlights underlined on it.
        if (_syncOut == SO_ASK) {
          _menu = rmenu::Page::Sync;
          host().setCanvasRotation(0);
          host().refresh(true);
          return;
        }
        _syncStage = 0;
        host().refresh(false);
        return;
      }
      paint();
      return;
    }
    if (_saveDue && (int32_t)(host().nowMs() - _saveDue) >= 0) {
      const uint32_t t0 = host().nowMs();
      saveProgress();
      char line[48];
      snprintf(line, sizeof(line), "save (deferred) %lu ms", (unsigned long)(host().nowMs() - t0));
      host().perfLog(line);
    }
  }
  void flush() override {
    if (_saveDue) saveProgress();
  }

  void saveProgress() {
    _saveDue = 0;
    if (_cur < 0 || _page >= _lutN) return;
    epubc::Progress p;
    p.spine = (uint16_t)_spine;
    p.page = (uint16_t)_page;
    p.pageCount = (uint16_t)(_chapterPages > 0 ? _chapterPages : 0);
    p.offset = epubui::pageOff(_lut[_page]);
    p.hasOffset = true;
    uint8_t buf[10];
    const int n = epubc::encodeProgress(p, buf);
    char path[bstore::PATH_CAP];
    progressPath(path, sizeof(path));
    host().sdWriteFileAtomic(path, buf, n);
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

  // How far through the whole book we are, 0..1 (what sync sends beside the
  // xpointer).
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

  void openBook(int i) {
    _cur = i;
    const uint32_t tOpen0 = host().nowMs();
    {  // which book the timings below belong to
      char nm[bstore::NAME_BYTES + 1], line[bstore::NAME_BYTES + 12];
      bstore::name(_books[i].file, nm, sizeof(nm));
      snprintf(line, sizeof(line), "open: \"%s\"", nm);
      host().perfLog(line);
    }
    // The owner's own cover first, BEFORE the loading paint: a sidecar put
    // beside the book (.cover.tbi or .cover.bmp) costs no decode and beats
    // anything decoded here, so even a first open leads with that face. (The
    // sidecar check runs every open -- that is how a replaced one is noticed.)
    if (!bthumb::coverFromSidecar(host(), _books[i].file) &&
        !bthumb::complete(host(), _books[i].file) && !bthumb::failed(_books[i].file)) {
      host().coverFromBmp(_books[i].file);
    }
    // The cover as the loading screen. An EPUB spends real seconds opening
    // (zip walk, cover decode on the first open, the chapter replayed to the
    // saved page); they pass behind the book's own face. Only a first open
    // with no sidecar still shows the plate.
    //
    // Portrait, whatever the page's own angle is: a cover is a portrait
    // object -- turned on its side it is cropped, not rotated -- and the
    // plate is a portrait design like every other screen that is not the
    // page itself. The reading rotation goes on at the Page transition.
    host().setCanvasRotation(0);
    _screen = Screen::Loading;
    // Partial, not full: this screen is up for well under a second on any
    // book opened before, and a full refresh here was 1.74 s of an open that
    // already ends in one (the first page, below). The page's full refresh
    // cleans whatever ghost the cover leaves.
    host().refresh(false);
    const uint32_t tPaint = host().nowMs();
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
    const uint32_t tZip = host().nowMs();  // zip directory + container + OPF
    _menu = rmenu::Page::None;
    _picking = false;
    _ntoc = -1;
    _nmarks = 0;
    recents::note(prefs(), recents::KIND_EPUB, _books[i].file, _books[i].title);
    readorder::note(host(), _books[i].file);  // the shelf puts it first next time
    {  // the home screen's reading streak counts days with a book opened
      int sy = 0, sm = 0, sd = 0;
      if (host().clockDate(sy, sm, sd) && sy >= 2020) homestyle::noteRead(prefs(), sy, sm, sd);
    }

    // The decode: every book with no cover of the owner's (tried above,
    // before the loading paint) pays for it -- once, and a cover that will
    // not decode (a GIF or WebP, say) is marked so it is not retried; the
    // plain plate stands in for it.
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

    // Every picture in the book, prepared now if it was not before -- the
    // loading screen counts them off. Before the layout, which asks how many
    // pages each picture is.
    prepareBookArt();

    // Where were we? The book's folder remembers; a book last read before
    // v2 brings its place across from the old folders, once.
    const uint32_t tCover = host().nowMs();
    epubc::Progress p;
    uint8_t buf[10];
    char path[bstore::PATH_CAP];
    progressPath(path, sizeof(path));
    int n = host().sdReadFile(path, buf, sizeof(buf));
    if (n <= 0) n = adoptOldPosition(buf, sizeof(buf));
    int spine = 0;
    uint32_t off = 0;
    if (n > 0 && epubc::decodeProgress(buf, n, p)) {
      spine = p.spine < _book.spineCount() ? p.spine : _book.spineCount() - 1;
      off = p.hasOffset ? p.offset : 0;
    }
    _screen = Screen::Page;
    _guide = !prefs().getBool("ep_guide", false);  // v2: once, the first book ever
    _nmarks = marks::load(host(), _books[_cur].file, _marks);  // highlights to underline
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
    {
      char line[112];
      snprintf(line, sizeof(line), "open: screen %lu ms, book %lu ms, cover %lu ms, place %lu ms (%d pages)",
               (unsigned long)(tPaint - tOpen0), (unsigned long)(tZip - tPaint),
               (unsigned long)(tCover - tZip), (unsigned long)(host().nowMs() - tCover), _lutN);
      host().perfLog(line);
    }
    // A book whose first page is its cover art opens portrait, whatever the
    // reading rotation: see syncPageRot -- and in four greys, like every
    // picture page with nothing over it.
    syncPageRot();
    if (!showDeviceArtGrey()) host().refresh(true);
    // D: the automatic sync on opening -- the strip goes up over the first
    // page, and the next tick does the blocking work.
    if (autoReady() && rhl::onOpen(prefs())) {
      _auto = AUTO_OPEN;
      _syncStage = 1;
      paint();
    }
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
    // No note, no line: a position with no chapter note (one written before
    // the note existed) says nothing until the book has been closed here once.
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
    // The open chapter already worked this out, whichever way it had to.
    if (spine == _chapNameSpine && _chapName[0]) {
      snprintf(out, (size_t)cap, "%s", _chapName);
      return;
    }
    tocCount();  // reads the contents once, while the card is still up
    const int row = epubui::tocRowForSpine(epubui::g_toc, _ntoc > 0 ? _ntoc : 0, spine);
    if (row >= 0) {
      snprintf(out, (size_t)cap, "%s", epubui::g_toc[row].title);
      return;
    }
    // No contents: the same first words the contents list is showing for this
    // chapter, so the shelf and the list say the same thing about the same
    // book. Without this the shelf fell back to "chapter 14" while the list
    // beside it read "ch 14 - Chapter 7: Friends" -- and the book itself calls
    // that Chapter 7.
    char taste[64] = "";
    if (spineTaste(spine, taste, sizeof(taste)))
      snprintf(out, (size_t)cap, "%s", taste);
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
    char path[bstore::PATH_CAP];
    bstore::file(_books[_cur].file, "place.txt", path, sizeof(path));
    host().sdWriteFileAtomic(path, body, n);
    // ...and the row this book came from, so the shelf is right the moment it
    // is drawn rather than the next time the app is entered.
    snprintf(_books[_cur].place, sizeof(_books[_cur].place), "%s", name);
    _books[_cur].page = (uint16_t)(_page + 1);
    _books[_cur].pageCount = (uint16_t)(_chapterPages > 0 ? _chapterPages : 0);
  }

  void closeBook(bool beep) {
    if (beep) syncOnTheWayOut(false);  // the owner closed it: not a failed open
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
    // The book just read goes to the top of its shelf (owner, 26 Nov), and
    // the shelf shows the top, where it is.
    buildView();
    if (_inGroup < 0) _lpage = 0;
    if (beep) {
      host().beep(0);
      host().refresh(true);  // full: the controller's RAM was just reset
    }
  }

  // --- layout ---------------------------------------------------------------
  // One page: pull words until the glass is full. Every page records its
  // first word's offset in _lut, which is both the back-turn index and the
  // number the saved position holds.

  bool chapterStart(int spineIdx) {
    if (!_book.chapterOpen(spineIdx)) return false;
    _spine = spineIdx;
    // What to call this chapter, for the footer and for the note the shelf
    // reads. From the contents when the book has any; otherwise the first
    // page's first line fills it in during layout, which costs nothing --
    // opening the chapter again mid-read to fetch its first words would spend
    // the stream the page is being laid out from.
    _chapName[0] = 0;
    _chapNameSpine = spineIdx;
    if (_ntoc > 0) {
      const int row = epubui::tocRowForSpine(epubui::g_toc, _ntoc, spineIdx);
      if (row >= 0) snprintf(_chapName, sizeof(_chapName), "%s", epubui::g_toc[row].title);
    }
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

  // A stored face name means one of the built-ins, or a family on the card --
  // in which case the baked cut underneath it is the plain one, since the card
  // face answers first for everything it carries.
  static uint8_t faceIndexOf(ToolsHost& h, const char* name) {
    if (!name || !name[0] || strcmp(name, "DejaVu") == 0) return 0;  // v1's name for 0
    for (int i = 0; i < h.typefaceCount(); i++)
      if (strcmp(h.typefaceName(i), name) == 0) return (uint8_t)i;
    return 0;
  }
  // What to show on the Font row: the name of whatever is set, which is a
  // built-in's name or a family's.
  // Every face this device can set a book in: the built-ins first, because
  // they are always there, then whatever the card offered when the reader
  // opened.
  int faceCount() { return host().typefaceCount() + (_famN > 0 ? _famN : 0); }
  const char* faceAt(int i) {
    if (i < host().typefaceCount()) return host().typefaceName(i);
    return _famNames[i - host().typefaceCount()];
  }

  const char* faceLabel() {
    const char* f = host().fontFor(ToolsHost::FONT_READER);
    return f[0] ? f : host().typefaceName(0);
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

  // --- what the book is set in, and how big ------------------------------------
  // A face off the card is cut at eight or ten sizes; the firmware has four
  // boxes and the reader reached three of them. So when the book's face is a
  // family with sizes to choose from, the Size stepper walks the family's own
  // and the page is always drawn in the body box -- the box is just where the
  // chosen file was put. With a built-in face there is nothing to walk and the
  // three named sizes stay exactly as they were.
  //
  // The whole difference is confined to these four calls. Everything that lays
  // out or draws a page asks them rather than epubui::sizeAt, so a size that
  // came off the card and one that came out of the firmware are the same thing
  // by the time the page is measured.
  bool cardSizes() { return host().faceSizeCount() >= 2; }
  int cardSizeAt() {
    const int i = host().faceSize();
    return i < 0 ? 0 : i;
  }
  TSize bodyTS() { return cardSizes() ? TS_MED : epubui::sizeAt(_size); }
  TSize headTS() { return cardSizes() ? TS_LARGE : epubui::headSize(_size); }
  TSize tsFor(int head) { return head ? headTS() : bodyTS(); }
  // What to call the size on a panel. A family's file says what it is -- their
  // 12, at 150 DPI -- and that is the number somebody who put it on the card
  // knows it by; a file with no number in its name is named for the line it
  // draws instead.
  const char* sizeLabel() {
    if (!cardSizes()) return epubui::sizeName(_size);
    const int i = cardSizeAt();
    const int pt = host().faceSizePt(i);
    if (pt > 0)
      snprintf(_szLabel, sizeof(_szLabel), "%d pt", pt);
    else
      snprintf(_szLabel, sizeof(_szLabel), "%d px line", host().faceSizeLine(i));
    return _szLabel;
  }

  int layoutPage() {
    LayoutRot lr(host(), _rot);
    FaceScope fs(host(), _face);
    // Measured in the face the page will be DRAWN in. Laying a book out in the
    // device's face and then drawing it in the book's is two different pages:
    // the widths that decided where every line broke are not the widths the
    // panel gets. It went unnoticed while a card face was only ever a
    // near-enough substitute for the box it sat in; it stops being subtle the
    // moment somebody sets a book in a face half again as tall.
    ContentFace cf(host());
    ToolsCanvas& c = host().canvas();
    const TSize ts = bodyTS();
    const int lineW = c.width() - 2 * epubui::marginPx(_margin);
    // A line's height follows its own type: a heading line is taller than the
    // body around it, so the step cannot be one number for the page any more.
    auto stepFor = [&](int head) {
      return c.textHeight(tsFor(head)) + epubui::leadAir(_lead);
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
    const int keepAnc = _ancN;
    _ancN = 0;
    char keepImage[sizeof(_pageImage)];
    memcpy(keepImage, _pageImage, sizeof(keepImage));
    const uint8_t keepPart = _pageImagePart;
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
    _pageImagePart = 0;
    if (_pendImageValid) {
      snprintf(_pageImage, sizeof(_pageImage), "%s", _pendImage);
      _pageImagePart = _pendImagePart;
      _pendImageValid = false;
      pageStart = _pendImageOff;
      queueNextPart(pageStart);
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
      if (tok == epubc::TOK_IMAGE && artDecor(_book.imageName())) {
        // A scene-break ornament: the space of a paragraph, not a page.
        if (!started) continue;
        if (!flushLine()) {
          _atEnd = false;
          break;
        }
        y += epubui::paraPx(_para) * 2;
        continue;
      }
      if (tok == epubc::TOK_IMAGE) {
        if (!started) {
          // A fresh page: this one is the picture (its first part).
          snprintf(_pageImage, sizeof(_pageImage), "%s", _book.imageName());
          _pageImagePart = 0;
          pageStart = off;
          queueNextPart(off);
          placed++;
          break;
        }
        // The page already has text, so the picture opens the next one. Note
        // that both pages then start at the same offset -- an image adds no
        // codepoints -- which resume handles the way it handles any tie.
        snprintf(_pendImage, sizeof(_pendImage), "%s", _book.imageName());
        _pendImagePart = 0;
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
        y += epubui::paraPx(_para);
        continue;
      }

      // a word, measured in the face it will be drawn in
      const TSize wts = tsFor(wHead);
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
      // Records which bytes of `cur` this word occupies, so the line can be
      // drawn in runs later.
      auto markBold = [&](int from, int len) {
        if (!wBold && !wItal) return;
        for (int i = from; i < from + len && i < (int)sizeof(curBold) * 8; i++) {
          if (wBold) curBold[i >> 3] |= (uint8_t)(1 << (i & 7));
          if (wItal) curItal[i >> 3] |= (uint8_t)(1 << (i & 7));
        }
      };
      // Codepoints between the word's start and `p`: a word carried to the
      // next page part-way through resumes at its own offset, not the word's.
      auto offAt = [&](const char* p) {
        uint32_t n = 0;
        for (const char* q = w; q < p;) {
          uni::next(q);
          n++;
        }
        return off + n;
      };
      // Puts `n` bytes of `s` on the line, `sep` px of space in front of them.
      auto append = [&](const char* s, int n, int sep, int width) {
        if (!curLen) curHead = wHead;
        if (curLen && sep) {
          markBold(curLen, 1);
          cur[curLen++] = ' ';
          curW += sep;
        }
        if (curLen + n < (int)sizeof(cur)) {
          // D part B: where these bytes are in the book, so a highlight
          // picked off this line knows its exact offsets.
          if (_ancN < MAX_ANC && curLen < 256)
            _anc[_ancN++] = Anchor{(uint8_t)_lineN, (uint8_t)curLen, offAt(s)};
          markBold(curLen, n);
          memcpy(cur + curLen, s, (size_t)n);
          curLen += n;
          curW += width;
        }
      };
      auto widthOf = [&](const char* s, int n) {
        char probe[epubc::WORD_CAP];
        memcpy(probe, s, (size_t)n);
        probe[n] = 0;
        return c.textWidth(probe, wts, wBold, wItal);
      };
      // The longest prefix of `s` that fits `room` and ends where a line may
      // break: between Thai words (thai_words.h), between CJK characters.
      // 0: none does.
      auto fitAtBreak = [&](const char* s, int room, int bytes) {
        uint8_t ok[epubc::WORD_CAP + 1];
        thaiwords::breaks(s, ok);
        int best = 0;
        const int len = (int)strlen(s);
        for (int n = 1; n < len; n++) {
          if (!ok[n]) continue;
          if (n > bytes || widthOf(s, n) > room) break;
          best = n;
        }
        return best;
      };
      auto pend = [&](const char* rest) {
        strcpy(_pend, rest);
        _pendOff = offAt(rest);
        _pendStyle = st;
        _pendValid = true;
      };
      // A line is only ever STARTED if there is room for it to land. Room
      // used to be checked when the line was flushed, a word too late: a page
      // that filled while a line was still being assembled pended the one
      // word in hand and silently dropped every word already gathered into
      // the line. On hardware, mid-novel: "Good. Now that left the other
      // two." lost everything but "two." across a page turn.
      if (!curLen && !roomForLine(wHead)) {
        pend(w);
        break;
      }
      // A glued word continues the one before it with no space: a Thai or CJK
      // run the parser had to cut. Anything that does not fit beside the line
      // is split where a line may break, so a spaceless run fills its lines
      // instead of jumping whole to the next one; a Latin word has no such
      // place and moves to the next line as it always has.
      // A line is bounded twice: by the panel's width and by the bytes a Line
      // holds. Thai is three bytes a letter, so at the smallest size a full
      // line of it is more bytes than width -- and overran the buffer.
      const int lineCap = (int)sizeof(_lines[0].t) - 1;
      const char* rest = w;
      bool glue = (st & epubc::Book::STYLE_GLUE) != 0;
      while (*rest) {
        const int sep = (curLen && !glue) ? wSpaceW : 0;
        const int rw = rest == w ? ww : c.textWidth(rest, wts, wBold, wItal);
        const int bytesLeft = lineCap - curLen - (sep ? 1 : 0);
        const int rl = (int)strlen(rest);
        if (curW + sep + rw <= lineW && rl <= bytesLeft) {
          append(rest, rl, sep, rw);
          break;
        }
        const int room = lineW - curW - sep;
        int n = room > 0 && bytesLeft > 0 ? fitAtBreak(rest, room, bytesLeft) : 0;
        if (n > 0) {
          append(rest, n, sep, widthOf(rest, n));
          rest += n;
        } else if (!curLen) {
          // Longer than a whole line with no break inside (a URL): the widest
          // prefix that fits, codepoint by codepoint.
          int fit = 0;
          for (const char* p = rest; *p;) {
            const char* at = p;
            uni::next(p);
            const int k = (int)(p - rest);
            if (k > lineCap || widthOf(rest, k) > lineW) break;
            fit = k;
            (void)at;
          }
          if (fit == 0) {  // a glyph wider than the line still moves on
            const char* p = rest;
            uni::next(p);
            fit = (int)(p - rest);
          }
          append(rest, fit, 0, widthOf(rest, fit));
          rest += fit;
        }
        // The line is done; the rest starts the next one.
        if (!flushLine()) {
          pend(rest);
          break;
        }
        glue = true;
        if (*rest && !roomForLine(wHead)) {
          pend(rest);
          break;
        }
      }
      if (_pendValid) break;
    }

    if (placed > 0) {
      // Before the page is recorded: _lutN is still zero on a chapter's first
      // page, which is the one whose first line is the heading.
      noteChapterNameFromPage();
      if (_lutN < epubui::MAX_PAGES)
        _lut[_lutN++] = pageStart | (_pageImage[0] ? epubui::PAGE_IMG : 0u);
      if (_atEnd && !_pendValid) _chapterPages = _lutN;
    } else {
      if (_atEnd) _chapterPages = _lutN;
      _lineN = keepLines;  // nothing new: the page that was showing still is
      _ancN = keepAnc;  // (nothing was appended, so the anchors are still its)
      memcpy(_pageImage, keepImage, sizeof(_pageImage));
      _pageImagePart = keepPart;
    }
    return placed;
  }

  // After a picture page: if the picture has another part, it opens the next
  // page, at the same offset -- the same mechanism as a picture that follows
  // text, so every replay breaks the pages identically.
  void queueNextPart(uint32_t off) {
    if (_pageImagePart + 1 < artParts(_pageImage)) {
      snprintf(_pendImage, sizeof(_pendImage), "%s", _pageImage);
      _pendImagePart = (uint8_t)(_pageImagePart + 1);
      _pendImageOff = off;
      _pendImageValid = true;
    }
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
      if (wantImage && tok == epubc::TOK_IMAGE && off >= target && !artDecor(_book.imageName())) {
        // A landscape picture is several pages: step over whole pictures,
        // then land on the right part of this one.
        const int parts = artParts(_book.imageName());
        if (skipImages >= parts) {
          skipImages -= parts;
          continue;
        }
        snprintf(_pendImage, sizeof(_pendImage), "%s", _book.imageName());
        _pendImagePart = (uint8_t)skipImages;
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
  // last page that starts at or before it.
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

  // Where a turn's time goes, one console line per turn: finding the page
  // (layout forward, or a chapter replay backward), then saving the position.
  // The host's own "paint" line follows with the render and the panel time.
  void perfTurn(const char* how, uint32_t t0, uint32_t tPage, uint32_t tSave) {
    char line[96];
    snprintf(line, sizeof(line), "turn %s: page %lu ms, save %lu ms", how,
             (unsigned long)(tPage - t0), (unsigned long)(tSave - tPage));
    host().perfLog(line);
  }

  void turn(int dir) {
    if (!_open) return;
    const uint32_t t0 = host().nowMs();
    ensureStream();
    if (dir > 0) {
      if (_pendValid || !_atEnd) {
        if (layoutPage() > 0) {
          _page = _lutN - 1;
          const uint32_t tPage = host().nowMs();
          saveSoon();
          perfTurn("next", t0, tPage, host().nowMs());
          host().beep(0);
          paint();
          return;
        }
      }
      // end of chapter: the next one, if there is one
      if (_spine + 1 < _book.spineCount()) {
        if (gotoPlace(_spine + 1, 0)) {
          // Forward into a chapter starts at its FIRST page. gotoPlace takes
          // the last page at an offset (the resume rule), and a chapter that
          // opens with pictures has several pages at offset 0 -- a landscape
          // picture's parts, or a picture and the text after it.
          if (_page != 0) showPageAt(0);
          const uint32_t tPage = host().nowMs();
          saveSoon();
          perfTurn("next chapter", t0, tPage, host().nowMs());
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
      const uint32_t tPage = host().nowMs();
      saveSoon();
      perfTurn("back", t0, tPage, host().nowMs());
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
    const uint32_t tPage = host().nowMs();
    saveSoon();
    perfTurn("back chapter", t0, tPage, host().nowMs());
    host().beep(0);
    paint();
  }

  // --- a book's illustrations (epub_art.h) -----------------------------------
  // Every picture in the book is prepared on the device, all of them on the
  // book's first open, behind the loading screen; the pictures a PC tool may
  // have left in the book (the slicer's toybox/ .bmp, CrossInk's .pxc) are
  // no longer read -- one decoder, one look, and the original is always in
  // the book beside them. A landscape picture is several pages (part 0 turned
  // to fill the panel, then upright slices), which the layout asks for here.
  struct ArtRec {
    uint32_t key;    // bthumb::key of the picture's zip path
    uint32_t usize;  // its uncompressed size, part of the card key
    uint8_t parts;   // 0 = not prepared (cannot be decoded, or not yet)
    bool known;      // settled: goes in the book's index (a transient miss does not)
  };
  static constexpr int ART_MAX = 256;
  ArtRec* _art = nullptr;  // PSRAM, ART_MAX entries
  int _artN = 0;
  int _artDone = 0, _artTotal = 0;  // the loading screen's progress line

  const ArtRec* artFor(const char* name) const {
    const uint32_t k = bthumb::key(name);
    for (int i = 0; i < _artN; i++)
      if (_art[i].key == k) return &_art[i];
    return nullptr;
  }
  // How many pages a picture is: its parts once prepared, one page (the
  // plate) when it could not be.
  int artParts(const char* name) const {
    const ArtRec* r = artFor(name);
    return r && r->parts && r->parts != epubart::DECOR ? r->parts : 1;
  }
  // A small ornament: stepped over, with a paragraph's space in its place.
  bool artDecor(const char* name) const {
    const ArtRec* r = artFor(name);
    return r && r->parts == epubart::DECOR;
  }

  // On every open: which of the book's pictures are on the card already (one
  // index read for the whole book), and on a first open -- or after a
  // pipeline change -- the rest prepared now, the loading screen counting
  // them off. Slow once, by design: the owner would rather wait at the door
  // than at every picture. Pictures prepared before the per-book folders are
  // moved across, not prepared again.
  void prepareBookArt() {
    if (!_art) _art = (ArtRec*)malloc(sizeof(ArtRec) * ART_MAX);
    _artN = 0;
    if (!_art) return;
    struct Names {
      EpubTool* self;
      char (*name)[192];
      int n;
    } names{this, (char(*)[192])malloc((size_t)ART_MAX * 192), 0};
    if (!names.name) return;
    _book.listImages(
        [](void* ctx, const char* nm, uint32_t usize) {
          Names& N = *(Names*)ctx;
          EpubTool& T = *N.self;
          if (T._artN >= ART_MAX || strlen(nm) >= 192) return;
          T._art[T._artN] = {bthumb::key(nm), usize, 0, false};
          snprintf(N.name[T._artN], 192, "%s", nm);
          T._artN++;
        },
        &names);
    if (_artN == 0) {
      free(names.name);
      return;
    }
    const char* file = _books[_cur].file;
    const uint32_t tArt0 = host().nowMs();
    int missing = 0, nMoved = 0;
    bool moved = false;
    {
      epubart::Prepared* idx =
          (epubart::Prepared*)malloc(sizeof(epubart::Prepared) * epubart::INDEX_MAX);
      bool decorChecked = false;
      const int ni =
          idx ? epubart::indexRead(host(), file, idx, epubart::INDEX_MAX, &decorChecked) : -1;
      for (int i = 0; i < _artN; i++) {
        for (int j = 0; j < ni; j++)
          if (idx[j].key == _art[i].key && idx[j].usize == _art[i].usize &&
              idx[j].parts <= epubart::DECOR) {
            _art[i].parts = idx[j].parts;
            _art[i].known = true;
            break;
          }
        if (_art[i].known && _art[i].parts == 1 && ni >= 0 && !decorChecked) {
          // Prepared before ornaments were recognised: its header says
          // whether it is one; the picture already made simply goes unused.
          int w = 0, h = 0;
          if (epubart::probeSize(_book, names.name[i], w, h) && epubart::isDecoration(w, h)) {
            _art[i].parts = (uint8_t)epubart::DECOR;
          }
          moved = true;  // rewrite the index at the new revision
        }
        if (_art[i].known) continue;
        const int old = epubart::adoptOld(host(), file, _art[i].key, _art[i].usize);
        if (old >= 0) {
          _art[i].parts = (uint8_t)old;
          _art[i].known = true;
          moved = true;
          nMoved++;
        } else {
          missing++;
        }
      }
      free(idx);
    }
    if (moved) {
      epubart::dropOldDir(host(), file);
      writeArtIndex();
    }
    if (missing) {
      _artTotal = missing;
      _artDone = 0;
      for (int i = 0; i < _artN; i++) {
        if (_art[i].known) continue;
        host().refresh(false);  // "preparing artwork 3 of 19"
        bool transient = false;
        _art[i].parts =
            (uint8_t)epubart::prepare(host(), _book, file, names.name[i], _art[i].usize, &transient);
        // A picture that cannot be decoded is remembered as 0 parts, so a GIF
        // is not tried again on every open; a bad moment (memory, the card) is
        // not, and the next open tries again.
        _art[i].known = _art[i].parts > 0 || !transient;
        // After each one, so a power-off halfway through keeps what is done.
        if (_art[i].known) writeArtIndex();
        _artDone++;
      }
      _artTotal = 0;
    }
    // One line per open: how many were already in the index, moved from the
    // old layout, and prepared now -- and what that cost.
    char line[96];
    snprintf(line, sizeof(line), "art: %d pictures - %d ready, %d moved, %d prepared in %lu ms", _artN,
             _artN - nMoved - missing, nMoved, missing, (unsigned long)(host().nowMs() - tArt0));
    host().perfLog(line);
    free(names.name);
  }

  void writeArtIndex() {
    epubart::Prepared* recs =
        (epubart::Prepared*)malloc(sizeof(epubart::Prepared) * epubart::INDEX_MAX);
    if (!recs) return;
    int n = 0;
    for (int i = 0; i < _artN && n < epubart::INDEX_MAX; i++)
      if (_art[i].known) recs[n++] = {_art[i].key, _art[i].usize, _art[i].parts};
    epubart::indexWrite(host(), _books[_cur].file, recs, n);
    free(recs);
  }

  // This page's picture, as card paths: false when it was not prepared.
  bool artPathsHere(char* g2, char* b1, int cap) {
    const ArtRec* r = artFor(_pageImage);
    if (!r || !r->parts) return false;
    epubart::paths(_books[_cur].file, _pageImage, r->usize, _pageImagePart, g2, b1, cap);
    return true;
  }

  // The one-bit picture through the canvas: read whole (one card open, into
  // PSRAM), then only the ink is drawn.
  bool drawImagePage(ToolsCanvas& c) {
    char g2[bstore::PATH_CAP], b1[bstore::PATH_CAP];
    if (!artPathsHere(g2, b1, sizeof(g2))) return false;
    _book.chapterClose();
    _streamLost = true;
    uint8_t* bits = (uint8_t*)malloc(epubart::B1_BYTES);
    if (!bits) return false;
    const bool ok = host().sdReadSlice(b1, 0, bits, (int)epubart::B1_BYTES) == (int)epubart::B1_BYTES;
    if (ok) c.inkBits(bits, epubart::W, epubart::H);
    free(bits);
    return ok;
  }

  // The four-grey picture, straight through the panel's grey waveform, when
  // nothing is drawn over the page. False (and the canvas path) on a host
  // with no grey, or when the file or the memory is not there.
  bool showDeviceArtGrey() {
    if (!_pageImage[0] || _chrome || _menu != rmenu::Page::None || _screen != Screen::Page)
      return false;
    char g2[bstore::PATH_CAP], b1[bstore::PATH_CAP];
    if (!artPathsHere(g2, b1, sizeof(g2))) return false;
    uint8_t* g = (uint8_t*)malloc(epubart::G2_BYTES);
    if (!g) return false;
    bool ok = host().sdReadSlice(g2, 0, g, (int)epubart::G2_BYTES) == (int)epubart::G2_BYTES;
    const uint32_t t0 = host().nowMs();
    ok = ok && host().bookShowGrey(g);
    free(g);
    if (ok) {
      char line[48];
      snprintf(line, sizeof(line), "paint grey art %lu ms", (unsigned long)(host().nowMs() - t0));
      host().perfLog(line);
    }
    return ok;
  }

  // What an illustration looks like when it could not be decoded (a GIF, an
  // SVG, a damaged file). Named, because "there is a picture here and you
  // cannot see it" is worth more than a blank page.
  void drawImagePlate(ToolsCanvas& c) {
    const int W = c.width();
    c.drawRect(40, 220, W - 80, 360, 2, true);
    c.textCentered(W / 2, 320, "an illustration", TS_LARGE, true);
    c.textCentered(W / 2, 372, "a picture this device cannot show", TS_SMALL, true);
    const char* base = strrchr(_pageImage, '/');
    c.textClipped(60, 430, W - 120, base ? base + 1 : _pageImage, TS_SMALL, true);
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
    const bool turned = syncPageRot();
    // An illustration the device prepared, with nothing over it: four greys.
    // The grey waveform is a full sequence anyway, and the driver promotes the
    // next ordinary paint to a full clean by itself.
    if (showDeviceArtGrey()) return;
    if (turned) {
      host().refresh(true);
      return;
    }
    host().refreshFast(rmenu::cleanEvery(mode()));
  }

  rmenu::Refresh mode() { return rmenu::refreshMode(prefs(), true); }

  // --- the panel --------------------------------------------------------------
  // Opened by the power button. Everything in here is about the book you are
  // in the middle of, which is why none of it lives in settings.

  // --- the Readest library ------------------------------------------------
  // The books in the owner's Readest cloud, a page at a time, most recently
  // read first; tap one to download it into /books/Readest (a ✓ marks the
  // ones already there, and tapping those opens them). SEARCH puts a QR code
  // up: the phone, on the same home Wi-Fi, opens a page that searches and can
  // ask for a download (lib_web.h). The radio stays up while this screen is
  // open -- each page would otherwise cost a join -- and goes off on the way
  // out, whichever way out.
  enum : uint8_t { LIB_LOADING = 1, LIB_LIST, LIB_DOWNLOAD, LIB_ERROR, LIB_PHONE };
  static constexpr int LIB_ROW_Y0 = 64, LIB_ROW_H = 76, LIB_BTN_Y = 690, LIB_BTN_H = 70;

  void libEnter() {
    _screen = Screen::Library;
    _libPage = 1;
    _libQuery[0] = 0;
    _libStage = LIB_LOADING;
    host().beep(0);
    paint();  // "loading" up before the wait
  }

  // Out of the library: the phone page down, the radio off, back to the
  // shelf (re-listed: there may be new books). `paintShelf` false when the
  // app itself is going.
  void libLeave(bool paintShelf) {
    if (_libDl) {
      _dl.cancel();
      _libDl = false;
    }
    _web.stop();
    if (_libWifi) homenet::off();
    _libWifi = false;
    if (_screen != Screen::Library) return;
    _screen = Screen::List;
    _libStage = 0;
    if (!paintShelf) return;
    reload();
    host().refresh(!_browsing);
  }

  bool libRadio() {
    if (_libWifi) return true;
    const homenet::Result j = homenet::connect(prefs());
    if (j != homenet::Result::Ok) {
      snprintf(_libMsg, sizeof(_libMsg), "%s", homenet::describe(j));
      return false;
    }
    _libWifi = true;
    return true;
  }

  void libMarkLocal() {
    for (int i = 0; i < _libN; i++) {
      char path[160];
      kosync::libPath(_lib[i], path, sizeof(path));
      _libLocal[i] = host().sdReadSlice(path, 0, path, 1) == 1;
    }
  }

  // The list again, from Readest.
  void libFetch() {
    if (!libRadio()) {
      _libStage = LIB_ERROR;
      return;
    }
    bool more = false;
    int n = 0;
    const kosync::Result r =
        kosync::listLibrary(prefs(), _libPage, _libQuery, _lib, kosync::LIB_PER_PAGE, n, more);
    if (r != kosync::Result::Ok) {
      snprintf(_libMsg, sizeof(_libMsg), "%s", kosync::describe(r));
      _libStage = LIB_ERROR;
      libPublish("");
      return;
    }
    _libN = n;
    _libMore = more;
    libMarkLocal();
    _libStage = (_web.running() && _libShowQr) ? LIB_PHONE : LIB_LIST;
    libPublish("");
  }

  // What the phone page shows: this page of results, which are on the card.
  void libPublish(const char* busy) {
    if (!_web.running()) return;
    String j = "{\"v\":";
    j += (int)(_web.version() + 1);
    j += ",\"q\":\"";
    auto esc = [&](const char* t) {
      for (; *t; t++) {
        if (*t == '"' || *t == '\\') j += '\\';
        if ((unsigned char)*t >= 0x20) j += *t;
      }
    };
    esc(_libQuery);
    j += "\",\"busy\":\"";
    esc(busy);
    j += "\",\"items\":[";
    for (int i = 0; i < _libN; i++) {
      if (i) j += ",";
      j += "{\"id\":\"";
      esc(_lib[i].id);
      j += "\",\"title\":\"";
      esc(_lib[i].title);
      j += "\",\"author\":\"";
      esc(_lib[i].author);
      j += "\",\"local\":";
      j += _libLocal[i] ? "true}" : "false}";
    }
    j += "]}";
    _web.publish(j);
  }

  // The bytes to the card: a .part file, renamed when whole, so a cut-off
  // download never looks like a book.
  struct LibSink : kosync::Sink {
    ToolsHost* h = nullptr;
    char part[168] = {}, final_[160] = {};
    bool open() override { return h->sdStreamOpen(part); }
    bool write(const uint8_t* d, uint32_t n) override { return h->sdStreamWrite(d, n); }
    bool close(bool keep) override {
      if (!h->sdStreamClose(keep)) return false;
      if (!keep) return true;
      h->sdRemove(final_);  // a stale copy of the same title
      return h->sdMove(part, final_);
    }
    bool progress(uint32_t, uint32_t) override { return true; }
  };

  void libStartDownload(int i) {
    if (!libRadio()) {
      _libStage = LIB_ERROR;
      return;
    }
    _libSink.h = _host;
    kosync::libPath(_lib[i], _libSink.final_, sizeof(_libSink.final_));
    snprintf(_libSink.part, sizeof(_libSink.part), "%s.part", _libSink.final_);
    _libDlIdx = i;
    const kosync::Result r = _dl.begin(prefs(), _lib[i], _libSink);
    if (r != kosync::Result::Ok) {
      snprintf(_libMsg, sizeof(_libMsg), "%s", kosync::describe(r));
      _libStage = LIB_ERROR;
      return;
    }
    _libDl = true;
    _libStage = LIB_DOWNLOAD;
    _libPainted = 0;
    char line[140];
    snprintf(line, sizeof(line), "library: downloading \"%s\" (%lu bytes)", _lib[i].title,
             (unsigned long)_dl.total);
    host().perfLog(line);
    libPublish("Downloading on the Sticky...");
  }

  void libTick() {
    _web.loop();
    char q[libweb::QUERY_MAX], id[libweb::ID_MAX];
    if (_libStage != LIB_DOWNLOAD && _web.takeQuery(q, sizeof(q))) {
      snprintf(_libQuery, sizeof(_libQuery), "%s", q);
      _libPage = 1;
      _libShowQr = false;  // the results go on the panel; the phone has them too
      libFetch();
      paint();
      return;
    }
    if (_libStage != LIB_DOWNLOAD && _web.takeDownload(id, sizeof(id))) {
      for (int i = 0; i < _libN; i++)
        if (!strcmp(_lib[i].id, id) && !_libLocal[i]) {
          libStartDownload(i);
          paint();
          return;
        }
    }
    if (_libStage == LIB_LOADING) {
      libFetch();
      paint();
      return;
    }
    if (_libStage == LIB_DOWNLOAD && _libDl) {
      kosync::Result r;
      if (!_dl.step(r)) {
        // A partial repaint every tenth of the book: the bar moves, and the
        // panel is not refreshed faster than it can draw.
        const uint32_t tenth = _dl.total ? (uint32_t)((uint64_t)_dl.got * 10 / _dl.total) : 0;
        if (tenth != _libPainted) {
          _libPainted = tenth;
          host().refresh(false);
        }
        return;
      }
      _libDl = false;
      char line[96];
      snprintf(line, sizeof(line), "library: download %s, %lu bytes", kosync::describe(r),
               (unsigned long)_dl.got);
      host().perfLog(line);
      if (r != kosync::Result::Ok) {
        snprintf(_libMsg, sizeof(_libMsg), "the download stopped: %s", kosync::describe(r));
        _libStage = LIB_ERROR;
        libPublish("The download stopped.");
        paint();
        return;
      }
      // Downloaded: straight into the book, which is what the tap was for.
      // Readest's position for it is one Sync position away.
      char path[160];
      snprintf(path, sizeof(path), "%s", _libSink.final_);
      host().beep(1);
      libPublish("");
      libLeave(false);
      snprintf(_dir, sizeof(_dir), "%s", kosync::LIB_DIR);
      reload();
      if (!openDirect(path)) host().refresh(true);
    }
  }

  void libTap(int x, int y) {
    const int W = host().canvas().width();
    if (host().isBackTap(x, y)) {
      host().beep(1);
      if (_libStage == LIB_PHONE && _libN > 0) {  // the QR away, the list back
        _libShowQr = false;
        _libStage = LIB_LIST;
        paint();
        return;
      }
      libLeave(true);
      return;
    }
    const bool inBtn = y >= LIB_BTN_Y && y < LIB_BTN_Y + LIB_BTN_H;
    switch (_libStage) {
      case LIB_DOWNLOAD:
        if (inBtn) {  // CANCEL
          _dl.cancel();
          _libDl = false;
          host().beep(2);
          _libStage = LIB_LIST;
          libPublish("");
          paint();
        }
        return;
      case LIB_ERROR:
        if (inBtn) {  // TRY AGAIN
          host().beep(0);
          _libStage = LIB_LOADING;
          paint();
        }
        return;
      case LIB_PHONE:
        if (inBtn) {  // DONE
          host().beep(0);
          _libShowQr = false;
          _libStage = LIB_LIST;
          paint();
        }
        return;
      case LIB_LIST: {
        if (inBtn) {
          const int third = (W - 32) / 3;
          if (x < 16 + third) {  // < PAGE
            if (_libPage > 1) {
              _libPage--;
              _libStage = LIB_LOADING;
              host().beep(0);
              paint();
            }
          } else if (x < 16 + 2 * third) {  // SEARCH (or clear it)
            host().beep(0);
            if (_libQuery[0]) {
              _libQuery[0] = 0;
              _libPage = 1;
              _libStage = LIB_LOADING;
            } else if (_web.start()) {
              _libShowQr = true;
              _libStage = LIB_PHONE;
              libPublish("");
            }
            paint();
          } else if (_libMore) {  // PAGE >
            _libPage++;
            _libStage = LIB_LOADING;
            host().beep(0);
            paint();
          }
          return;
        }
        const int row = (y - LIB_ROW_Y0) / LIB_ROW_H;
        if (y < LIB_ROW_Y0 || row < 0 || row >= _libN) return;
        if (_libLocal[row]) {  // on the card already: read it
          char path[160];
          kosync::libPath(_lib[row], path, sizeof(path));
          host().beep(1);
          libLeave(false);
          snprintf(_dir, sizeof(_dir), "%s", kosync::LIB_DIR);
          reload();
          if (!openDirect(path)) host().refresh(true);
          return;
        }
        host().beep(1);
        libStartDownload(row);
        paint();
        return;
      }
      default:
        return;
    }
  }

  void renderLibrary(ToolsCanvas& c) {
    const int W = c.width();
    host().topBar(_libQuery[0] ? _libQuery : "Readest", false, "EPUB");
    char buf[96];
    switch (_libStage) {
      case LIB_LOADING:
        c.textCentered(W / 2, 300, "READEST", TS_MED, true, true);
        c.textCentered(W / 2, 350, _libWifi ? "asking for your books" : "joining the wifi", TS_MED, true);
        return;
      case LIB_ERROR:
        c.textCentered(W / 2, 260, "NOT NOW", TS_MED, true, true);
        c.textCentered(W / 2, 320, _libMsg, TS_MED, true);
        c.button(16, LIB_BTN_Y, W - 32, LIB_BTN_H, "Try again", false);
        return;
      case LIB_DOWNLOAD: {
        const kosync::LibBook& b = _lib[_libDlIdx];
        c.textCentered(W / 2, 200, "DOWNLOADING", TS_MED, true, true);
        c.textCentered(W / 2, 260, b.title, TS_MED, true);
        const int bx = 40, by = 340, bw = W - 80, bh = 36;
        c.drawRect(bx, by, bw, bh, 2, true);
        const uint32_t got = _dl.got, total = _dl.total;
        if (total) c.fillRect(bx + 4, by + 4, (int)((uint64_t)(bw - 8) * got / total), bh - 8, true);
        // KB below a megabyte: a small book would otherwise read "0.0 of 0.0".
        const bool mb = (total ? total : got) >= 1048576;
        const double unit = mb ? 1048576.0 : 1024.0;
        const char* u = mb ? "MB" : "KB";
        if (total)
          snprintf(buf, sizeof(buf), mb ? "%.1f of %.1f %s" : "%.0f of %.0f %s", got / unit,
                   total / unit, u);
        else
          snprintf(buf, sizeof(buf), mb ? "%.1f %s" : "%.0f %s", got / unit, u);
        c.textCentered(W / 2, 400, buf, TS_SMALL, true);
        c.textCentered(W / 2, 450, "into /books/Readest", TS_SMALL, true);
        c.button(16, LIB_BTN_Y, W - 32, LIB_BTN_H, "Cancel", false);
        return;
      }
      case LIB_PHONE:
        c.textCentered(W / 2, 90, "SEARCH FROM YOUR PHONE", TS_MED, true, true);
        fqr::draw(c, (W - 240) / 2, 130, 240, _web.url());
        c.textCentered(W / 2, 400, "scan with a phone on the same wifi", TS_MED, true);
        c.textCentered(W / 2, 436, _web.url(), TS_SMALL, true);
        c.textCentered(W / 2, 480, "results show here and on the phone;", TS_SMALL, true);
        c.textCentered(W / 2, 506, "the phone can ask for a download too", TS_SMALL, true);
        c.button(16, LIB_BTN_Y, W - 32, LIB_BTN_H, "Done", false);
        return;
      default:
        break;
    }
    if (_libN == 0)
      c.textCentered(W / 2, 300, _libQuery[0] ? "nothing matches" : "no books in your Readest cloud",
                     TS_MED, true);
    for (int i = 0; i < _libN; i++) {
      const int y = LIB_ROW_Y0 + i * LIB_ROW_H;
      const int textW = W - 32 - (_libLocal[i] ? 44 : 0);
      c.textClipped(16, y + 10, textW, _lib[i].title, TS_MED, true, true);
      c.textClipped(16, y + 44, textW, _lib[i].author[0] ? _lib[i].author : "-", TS_SMALL, true);
      if (_libLocal[i]) {  // on the card: a tick in a circle
        const int cx = W - 34, cy = y + LIB_ROW_H / 2;
        c.drawCircle(cx, cy, 16, 2, true);
        c.drawLine(cx - 8, cy, cx - 2, cy + 7, 3, true);
        c.drawLine(cx - 2, cy + 7, cx + 9, cy - 7, 3, true);
      }
      if (i + 1 < _libN) c.fillRect(16, y + LIB_ROW_H - 1, W - 32, 1, true);
    }
    const int third = (W - 32) / 3;
    // Only the pager buttons that go somewhere are drawn.
    snprintf(buf, sizeof(buf), "< %d", _libPage - 1);
    if (_libPage > 1) c.button(16, LIB_BTN_Y, third - 8, LIB_BTN_H, buf, false);
    c.button(16 + third, LIB_BTN_Y, third - 8, LIB_BTN_H, _libQuery[0] ? "Clear" : "Search", false);
    snprintf(buf, sizeof(buf), "%d >", _libPage + 1);
    if (_libMore) c.button(16 + 2 * third, LIB_BTN_Y, third - 8, LIB_BTN_H, buf, false);
  }

  // --- KOReader progress sync ---------------------------------------------
  // One row on the panel, offered once a home Wi-Fi and a sync account are
  // both saved (the phone's clock page takes both). The rules for what to do
  // with the answer live in kosync::decide().
  static constexpr int SYNC_BTN_Y = 560, SYNC_BTN_H = 96;
  enum : uint8_t { SO_SENT, SO_SAME, SO_ASK, SO_FAIL, SO_HLONLY };

  // A home Wi-Fi is the one thing needed; with no account yet, the row
  // signs in with Readest first.
  bool syncReady() { return homenet::haveWifi(prefs()); }

  void syncFail(const char* why) {
    _syncOut = SO_FAIL;
    snprintf(_syncMsg, sizeof(_syncMsg), "%s", why);
    char line[96];
    snprintf(line, sizeof(line), "sync: result failed - %s", why);
    host().perfLog(line);
  }

  // Leaving the sign-in screen any way at all switches the radio off: it is
  // the only state in which the radio stays up between ticks.
  void syncAbort() {
    if (_syncStage == 4) homenet::off();
    _syncStage = 0;
  }

  // Sign-in with Readest (stage 3 = asking for a code, 4 = showing it and
  // polling). Returns true when the screen must change.
  bool signInStep() {
    if (_syncStage == 3) {
      const homenet::Result j = homenet::connect(prefs());
      if (j != homenet::Result::Ok) {
        syncFail(homenet::describe(j));
        _syncStage = 2;
        return true;
      }
      const kosync::Result r = kosync::requestCode(_code);
      if (r != kosync::Result::Ok) {
        homenet::off();
        syncFail("Readest did not answer");
        _syncStage = 2;
        return true;
      }
      _codeT0 = _lastPoll = host().nowMs();
      _syncStage = 4;
      host().perfLog("sync: readest sign-in code shown");
      return true;
    }
    // stage 4: poll at the server's pace
    const uint32_t now = host().nowMs();
#ifndef TOYBOX_HOST
    if (now - _lastPoll < _code.intervalS * 1000u) return false;
#endif
    _lastPoll = now;
    const kosync::Poll p = kosync::pollCode(prefs(), _code);
    if (p == kosync::Poll::Pending && now - _codeT0 < _code.expiresS * 1000u) return false;
    homenet::off();
    if (p == kosync::Poll::Approved) {
      host().perfLog("sync: readest sign-in approved");
      host().beep(1);
      _syncStage = 1;  // straight on to the sync it was opened for
      return true;
    }
    syncFail(p == kosync::Poll::Error ? "Readest refused the sign-in" : "the code expired - try again");
    _syncStage = 2;
    return true;
  }

  // The shelf's sync mark: a one-byte "synced" in the book's folder, left
  // once a sync has reached the server (whatever it decided). Never removed;
  // a book that synced once is a book the owner reads in two places.
  void markSynced() {
    if (_cur < 0) return;
    if (!_books[_cur].synced) {
      char path[bstore::PATH_CAP];
      bstore::file(_books[_cur].file, "synced", path, sizeof(path));
      host().sdWriteFileAtomic(path, "1", 1);
    }
    _books[_cur].synced = true;
  }

  // D (26 Nov): highlights from Readest, pulled while the radio is up for the
  // place. `closing` is the sync on the way out (the book closing, or the
  // Sticky going to sleep): it sends the place and asks nothing.
  void pullHighlights(const char* doc) {
    _hlTried = true;
    _hlRes = rhl::Merged{};
    _hlErr = kosync::Result::Ok;
    rhl::Clip* clips = (rhl::Clip*)calloc(rhl::PULL_MAX, sizeof(rhl::Clip));
    if (!clips) {
      _hlErr = kosync::Result::LowMemory;
      return;
    }
    int k = 0;
    _hlErr = rhl::pull(prefs(), doc, clips, rhl::PULL_MAX, k);
    if (_hlErr == kosync::Result::Ok) {
      _hlRes = rhl::merge(_marks, _nmarks, clips, k);
      if (_hlRes.added || _hlRes.removed) marks::save(host(), _books[_cur].file, _marks, _nmarks);
    }
    free(clips);
    char line[96];
    snprintf(line, sizeof(line), "sync: highlights %s, %d from Readest, %d new, %d removed%s",
             kosync::describe(_hlErr), k, _hlRes.added, _hlRes.removed, _hlRes.full ? ", list full" : "");
    host().perfLog(line);
  }

  // D part B: deletions, then highlights kept here, to Readest. One kept
  // before part B has no id and no range: it gets an id now, and its label for
  // text (Readest finds the words near where the page began).
  void pushHighlights(const char* doc) {
    _hlSent = _hlDead = 0;
    _hlPushErr = kosync::Result::Ok;
    const char* file = _books[_cur].file;
    bool changed = false;
    for (int i = 0; i < _nmarks; i++)
      if (_marks[i].src == marks::SRC_HERE && _marks[i].label[0] && !_marks[i].id[0]) {
        rhl::newId(_marks[i].id, sizeof(_marks[i].id));
        changed = true;
      }
    if (changed) marks::save(host(), file, _marks, _nmarks);
    rhl::Out* outs = (rhl::Out*)calloc(rhl::PUSH_BATCH, sizeof(rhl::Out));
    char* texts = (char*)malloc((size_t)rhl::PUSH_BATCH * (marks::TEXT_MAX + 1));
    if (!outs || !texts) {
      free(outs);
      free(texts);
      _hlPushErr = kosync::Result::LowMemory;
      return;
    }
    int next = 0;
    bool deadSent = false;
    while (true) {
      int idx[rhl::PUSH_BATCH];
      int na = 0;
      for (; next < _nmarks && na < rhl::PUSH_BATCH; next++) {
        const marks::Mark& m = _marks[next];
        if (m.src != marks::SRC_HERE || !m.label[0] || (m.flags & marks::FLAG_SENT)) continue;
        char* t = texts + (size_t)na * (marks::TEXT_MAX + 1);
        if (!marks::loadText(host(), file, m.id, t, marks::TEXT_MAX + 1)) {
          snprintf(t, marks::TEXT_MAX + 1, "%s", m.label);
          const size_t L = strlen(t);
          if (L > 3 && strcmp(t + L - 3, "...") == 0) t[L - 3] = 0;  // the label's cut, not the book's
        }
        rhl::Out& o = outs[na];
        snprintf(o.id, sizeof(o.id), "%s", m.id);
        o.spine = m.spine;
        o.start = m.off;
        o.end = m.end > m.off ? m.end : m.off + (uint32_t)rhl::codepoints(t);
        o.text = t;
        idx[na++] = next;
      }
      const int nd = deadSent ? 0 : marks::tombs().n;
      if (!na && !nd) break;
      String b;
      rhl::body(b, marks::tombs().id, nd, outs, na);
      const kosync::Result r = rhl::put(prefs(), doc, b);
      if (r != kosync::Result::Ok) {
        _hlPushErr = r;
        break;
      }
      if (nd) {
        _hlDead = nd;
        marks::clearTombs();
        deadSent = true;
      }
      for (int k = 0; k < na; k++) _marks[idx[k]].flags |= marks::FLAG_SENT;
      _hlSent += na;
    }
    free(outs);
    free(texts);
    if (_hlSent || _hlDead) {
      marks::save(host(), file, _marks, _nmarks);
      if (_hlSent) marks::keepTexts(host(), file, _marks, _nmarks);
    }
    char line[96];
    snprintf(line, sizeof(line), "sync: highlights out %s, %d sent, %d deletions",
             kosync::describe(_hlPushErr), _hlSent, _hlDead);
    host().perfLog(line);
  }

  void runSync(bool closing = false) {
    _hlTried = false;
    if (_saveDue) saveProgress();  // the card first: it is the record that matters
    _syncPct = bookFraction();
    char doc[33];
    if (!kosync::docHash(_io, doc)) {
      syncFail("could not read the book");
      return;
    }
    static char xp[kosync::XP_MAX];  // static: the loop task's stack is small
    const bool haveXp = _book.xpointerFor(_spine, curOffset(), xp, sizeof(xp));
    _streamLost = true;  // the locator replayed the chapter through the same core
    if (!haveXp) snprintf(xp, sizeof(xp), "/body/DocFragment[%d]/body", _spine + 1);
    static char line[kosync::XP_MAX + 64];  // static: whole xpointers, off the stack
    snprintf(line, sizeof(line), "sync: book %s, here %d:%lu = %.4f", doc, _spine,
             (unsigned long)curOffset(), _syncPct);
    host().perfLog(line);
    snprintf(line, sizeof(line), "sync: here %s", xp);
    host().perfLog(line);

    const homenet::Result j = homenet::connect(prefs());
    if (j != homenet::Result::Ok) {  // the radio is already off on this path
      syncFail(homenet::describe(j));
      return;
    }
    if (rhl::hl(prefs()) && kosync::isReadest(prefs())) {
      if (!closing) {
        pullHighlights(doc);
      } else {
        _hlTried = true;
        _hlRes = rhl::Merged{};
        _hlErr = kosync::Result::Ok;
      }
      pushHighlights(doc);
    }
    if (!rhl::pos(prefs())) {  // the place is not synced: highlights were the errand
      homenet::off();
      markSynced();
      _syncOut = SO_HLONLY;
      return;
    }
    char me[33];
    kosync::deviceId(me);
    kosync::Remote& r = _remote;  // a member, not 360 bytes of stack
    const kosync::Result g = kosync::get(prefs(), doc, r);
    if (g != kosync::Result::Ok && g != kosync::Result::NoRecord) {
      homenet::off();
      syncFail(kosync::describe(g));
      return;
    }
    kosync::Decision d = kosync::decide(_syncPct, r, me);
    if (r.have) {
      snprintf(line, sizeof(line), "sync: %s at %.4f %s", r.device[0] ? r.device : "remote",
               r.percentage, r.progress);
      host().perfLog(line);
    }
    // Another reader's record: decide by its paragraph when this file has it.
    // (The radio stays up for this second or so; an upload may follow.)
    if (r.have && strcmp(r.deviceId, me) != 0) {
      int sp = 0;
      uint32_t o = 0;
      if (_book.offsetFor(r.progress, sp, o)) {
        const uint32_t start = curOffset();
        // The next page's start; when it is not laid out yet, only the start counts.
        const uint32_t end = _page + 1 < _lutN ? epubui::pageOff(_lut[_page + 1]) : start + 1;
        d = kosync::decideAt(sp, o, _spine, start, end);
        snprintf(line, sizeof(line), "sync: theirs is %d:%lu, this page %d:%lu..%lu", sp,
                 (unsigned long)o, _spine, (unsigned long)start, (unsigned long)end);
      } else {
        snprintf(line, sizeof(line), "sync: theirs is not in this file - by percentage");
      }
      _streamLost = true;
      host().perfLog(line);
    }
    switch (d) {
      case kosync::Decision::Same:
        homenet::off();
        markSynced();
        host().perfLog("sync: result in step");
        _syncOut = SO_SAME;
        snprintf(_syncMsg, sizeof(_syncMsg), "%s", r.device[0] ? r.device : "the other reader");
        return;
      case kosync::Decision::AskJump:
        homenet::off();
        markSynced();
        if (closing) {  // on the way out nobody is there to ask; the further place stands
          host().perfLog("sync: result further on elsewhere - left as it is");
          _syncOut = SO_SAME;
          return;
        }
        host().perfLog("sync: result further on elsewhere - asking");
        _syncOut = SO_ASK;
        snprintf(_syncMsg, sizeof(_syncMsg), "%s", r.device[0] ? r.device : "another reader");
        return;
      case kosync::Decision::Upload: {
        const kosync::Result p = kosync::put(prefs(), doc, xp, _syncPct);
        homenet::off();
        if (p != kosync::Result::Ok) {
          syncFail(kosync::describe(p));
          return;
        }
        markSynced();
        host().perfLog("sync: result sent");
        _syncOut = SO_SENT;
        return;
      }
    }
  }

  // The other reader's place, in this file: its xpointer when the path is
  // here, its percentage when it is not (a different edition, or markup the
  // other reader's engine rebuilt).
  void syncJump() {
    int spine = 0;
    uint32_t off = 0;
    bool ok = _book.offsetFor(_remote.progress, spine, off);
    if (!ok) ok = kosync::placeFraction(_book, _remote.percentage, spine, off);
    _streamLost = true;
    char line[96];
    snprintf(line, sizeof(line), "sync: jump to %d:%lu (%s)", spine, (unsigned long)off,
             ok ? "found" : "not found");
    host().perfLog(line);
    _syncStage = 0;
    if (!ok) {
      host().beep(2);
      menuClose();
      return;
    }
    applyRot(_rot);
    jumpTo(spine, off);
  }

  // The highlights half of a sync, in one line; false when it did not run.
  bool hlLine(char* buf, size_t n) {
    if (!_hlTried) return false;
    if (_hlErr != kosync::Result::Ok) {
      snprintf(buf, n, "highlights: %s", kosync::describe(_hlErr));
      return true;
    }
    if (_hlPushErr != kosync::Result::Ok) {
      snprintf(buf, n, "highlights not sent: %s", kosync::describe(_hlPushErr));
      return true;
    }
    const int got = _hlRes.added, gone = _hlRes.removed + _hlDead, sent = _hlSent;
    const int kinds = (got > 0) + (gone > 0) + (sent > 0);
    if (kinds == 0) {
      snprintf(buf, n, "highlights in step with Readest");
    } else if (kinds == 1 && got) {
      snprintf(buf, n, "%d new highlight%s from Readest", got, got == 1 ? "" : "s");
    } else if (kinds == 1 && sent) {
      snprintf(buf, n, "%d highlight%s sent to Readest", sent, sent == 1 ? "" : "s");
    } else if (kinds == 1) {
      snprintf(buf, n, "%d highlight%s removed on both", gone, gone == 1 ? "" : "s");
    } else {
      buf[0] = 0;
      auto part = [&](int v, const char* what) {
        if (!v) return;
        const size_t L = strlen(buf);
        snprintf(buf + L, n - L, "%s%d %s", L ? " \xc2\xb7 " : "", v, what);
      };
      part(got, "new");
      part(gone, "removed");
      part(sent, "sent");
    }
    if (_hlRes.full) snprintf(buf + strlen(buf), n - strlen(buf), " (list full)");
    return true;
  }

  void renderSync(ToolsCanvas& c) {
    host().topBar("SYNC", false, "OPTIONS");
    const int W = c.width();
    char buf[96];
    if (_syncStage == 3) {
      c.textCentered(W / 2, 260, "SIGN IN WITH READEST", TS_MED, true, true);
      c.textCentered(W / 2, 320, "asking Readest for a code", TS_MED, true);
      return;
    }
    if (_syncStage == 4) {
      c.textCentered(W / 2, 100, "SIGN IN WITH READEST", TS_MED, true, true);
      fqr::draw(c, (W - 240) / 2, 140, 240, _code.uriFull);
      c.textCentered(W / 2, 400, "scan with a phone signed in", TS_MED, true);
      c.textCentered(W / 2, 430, "to Readest, and approve", TS_MED, true);
      snprintf(buf, sizeof(buf), "or open %s", _code.uri);
      c.textCentered(W / 2, 480, buf, TS_SMALL, true);
      c.textCentered(W / 2, 520, _code.user, TS_LARGE, true, true);
      c.textCentered(W / 2, 590, "waiting for the approval...", TS_SMALL, true);
      c.button(40, SYNC_BTN_Y + 120, W - 80, 80, "Cancel", false);
      return;
    }
    if (_syncStage == 1) {
      char wifi[homenet::SSID_MAX];
      homenet::wifiName(prefs(), wifi, sizeof(wifi));
      c.textCentered(W / 2, 260, "SYNCING", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "through %s", wifi);
      c.textCentered(W / 2, 320, buf, TS_MED, true);
      c.textCentered(W / 2, 380, "this takes up to half a minute", TS_SMALL, true);
      return;
    }
    if (hlLine(buf, sizeof(buf))) {
      const TSize ts = c.textWidth(buf, TS_MED, true) <= W - 32 ? TS_MED : TS_SMALL;  // errors run long
      c.textCentered(W / 2, _syncOut == SO_ASK ? 410 : 420, buf, ts, true, true);
    }
    switch (_syncOut) {
      case SO_HLONLY:
        c.textCentered(W / 2, 200, "HIGHLIGHTS", TS_MED, true, true);
        c.textCentered(W / 2, 260, _hlErr == kosync::Result::Ok ? "synced with Readest" : "not synced",
                       TS_LARGE, true);
        c.textCentered(W / 2, 330, "the place is not synced: Settings > Reading sync", TS_SMALL, true);
        break;
      case SO_SENT:
        c.textCentered(W / 2, 200, "SENT", TS_MED, true, true);
        snprintf(buf, sizeof(buf), "%d%% through the book", (int)(_syncPct * 100 + 0.5));
        c.textCentered(W / 2, 260, buf, TS_LARGE, true);
        c.textCentered(W / 2, 330, "Readest and KOReader can", TS_MED, true);
        c.textCentered(W / 2, 362, "pick up from this page", TS_MED, true);
        break;
      case SO_SAME:
        c.textCentered(W / 2, 200, "IN STEP", TS_MED, true, true);
        c.textCentered(W / 2, 260, "already at the same place", TS_LARGE, true);
        snprintf(buf, sizeof(buf), "as %s", _syncMsg);
        c.textCentered(W / 2, 330, buf, TS_MED, true);
        break;
      case SO_ASK:
        c.textCentered(W / 2, 170, "FURTHER ON ELSEWHERE", TS_MED, true, true);
        c.textCentered(W / 2, 230, _syncMsg, TS_LARGE, true);
        snprintf(buf, sizeof(buf), "is at %d%%", (int)(_remote.percentage * 100 + 0.5));
        c.textCentered(W / 2, 290, buf, TS_LARGE, true);
        snprintf(buf, sizeof(buf), "this device is at %d%%", (int)(_syncPct * 100 + 0.5));
        c.textCentered(W / 2, 360, buf, TS_MED, true);
        c.textCentered(W / 2, 470, "jump there?", TS_MED, true);
        c.drawRect(40, SYNC_BTN_Y, (W - 100) / 2, SYNC_BTN_H, 1, true);
        c.textInBox(40, SYNC_BTN_Y, (W - 100) / 2, SYNC_BTN_H, "Jump there", TS_LARGE, true, true);
        c.button(W / 2 + 10, SYNC_BTN_Y, (W - 100) / 2, SYNC_BTN_H, "Stay here", false);
        return;
      default:
        c.textCentered(W / 2, 200, "NOT SYNCED", TS_MED, true, true);
        c.textCentered(W / 2, 260, _syncMsg, TS_LARGE, true);
        c.textCentered(W / 2, 330, "your place on this device is safe", TS_SMALL, true);
        break;
    }
    c.textCentered(W / 2, 470, "the wifi is off again", TS_SMALL, true);
    if (kosync::haveAccount(prefs())) {
      char name[kosync::USER_MAX];
      kosync::user(prefs(), name, sizeof(name));
      snprintf(buf, sizeof(buf), "%s: %s", kosync::isReadest(prefs()) ? "Readest" : "account", name);
      c.textCentered(W / 2, 500, buf, TS_SMALL, true);
    }
    c.button(40, SYNC_BTN_Y, W - 80, SYNC_BTN_H, "Back to the book", false);
  }

  // --- v2: the More popup and the rotation that follows the device ----------
  void moreOpen() {
    _menu = rmenu::Page::More;
    _moreSet = false;
    _mpage = 0;
    _resetArmed = false;
    _nmarks = marks::load(host(), _books[_cur].file, _marks);
    // The card sits over the page at the page's own angle, so a landscape
    // reader gets a landscape card.
    host().beep(0);
    paint();
  }

  // Takes the device's angle if it has changed. True when the page was
  // re-laid (the tap that asked is used up by the turn).
  bool followDevice() {
    const int o = host().deviceOrientation();
    if (o < 0 || o == _rot) return false;
    _rot = (uint8_t)o;
    prefs().putUInt("rd_rot", _rot);
    applyRot(_rot);
    syncPageRot();
    host().beep(0);
    host().refresh(true);
    return true;
  }

  void renderMore(ToolsCanvas& c) {
    const int W = c.width(), H = c.height();
    renderPage(c);
    const TRect k = rmenu::moreCard(W, H);
    c.fillRect(k.x + 8, k.y + 8, k.w, k.h, true);  // the card's solid edge
    c.fillRect(k.x, k.y, k.w, k.h, false);
    c.drawRect(k.x, k.y, k.w, k.h, 3, true);
    // The book, and where in it.
    // Where in the book, and the battery (owner, 21 Nov): the card is the one
    // place mid-book that is drawn fresh, so the number on it is true.
    char pg[20], pos[40];
    if (_chapterPages > 0)
      snprintf(pg, sizeof(pg), "p %d/%d", _page + 1, _chapterPages);
    else
      snprintf(pg, sizeof(pg), "p %d", _page + 1);
    const int bat = host().batteryPercent();
    if (bat >= 0)
      snprintf(pos, sizeof(pos), "Battery %d%% \xc2\xb7 %s", bat, pg);
    else
      snprintf(pos, sizeof(pos), "%s", pg);
    // The title gets the whole width and up to two lines (owner, 22 Nov);
    // battery and page moved to the foot of the card.
    if (_moreSet) {
      renderMoreSet(c, k, pos);
      return;
    }
    titleTwoLines(c, k.x + 18, k.y + 14, k.w - 36, _books[_cur].title);
    c.fillRect(k.x + 16, k.y + 80, k.w - 32, 1, true);

    const char* syncSub = !syncReady()                     ? "no Wi-Fi saved"
                          : !kosync::haveAccount(prefs()) ? "sign in"
                          : kosync::isReadest(prefs())    ? "Readest"
                                                          : "KOReader";
    const char* lab[6] = {"Contents", "Highlight", "Sync", "Rotation", "Settings", "Exit"};
    const char* sub[6] = {"chapters", "add highlight", syncSub,
                          _rotFollow ? "follows" : "locked",
                          "text, sync, start", "close the book"};
    for (int i = 0; i < 6; i++) {
      const TRect t = rmenu::moreTile(i, W, H);
      const bool on = i == 3 && _rotFollow;
      if (on) c.fillRect(t.x, t.y, t.w, t.h, true);
      else c.drawRect(t.x, t.y, t.w, t.h, 2, true);
      moreIcon(c, i, t.x + t.w / 2, t.y + 34, !on);
      c.textCentered(t.x + t.w / 2, t.y + 60, lab[i], TS_MED, !on, true);
      c.textCentered(t.x + t.w / 2, t.y + 92, sub[i], TS_SMALL, !on);
    }
    moreFoot(c, k, pos);
  }

  // The card's foot: battery and page in bold (owner, 26 Nov: thin text was
  // hard to read on glass), and how to leave.
  void moreFoot(ToolsCanvas& c, const TRect& k, const char* pos) {
    const int fy = k.y + k.h - 44;
    c.text(k.x + 18, fy, pos, TS_MED, true, true);
    const char* hint = "tap outside to close";
    c.text(k.x + k.w - 18 - c.textWidth(hint, TS_SMALL), fy + 4, hint, TS_SMALL, true);
  }

  // More > Settings: Text, Highlights, Reading sync, Start again.
  void renderMoreSet(ToolsCanvas& c, const TRect& k, const char* pos) {
    const int W = c.width(), H = c.height();
    // A back arrow, then the title.
    c.drawLine(k.x + 30, k.y + 40, k.x + 40, k.y + 30, 3, true);
    c.drawLine(k.x + 30, k.y + 40, k.x + 40, k.y + 50, 3, true);
    c.textTracked(k.x + 62, k.y + 26, "SETTINGS", TS_MED, true, true, 4);
    c.fillRect(k.x + 16, k.y + 80, k.w - 32, 1, true);
    char textSub[48], markSub[24];
    snprintf(textSub, sizeof(textSub), "%s \xc2\xb7 %s", faceLabel(), sizeLabel());
    snprintf(markSub, sizeof(markSub), _nmarks ? "%d kept" : "none yet", _nmarks);
    Preferences& p = prefs();
    const char* syncSub = !kosync::haveAccount(p) ? "off"
                          : kosync::isReadest(p)  ? (rhl::autoOn(p) ? "Readest \xc2\xb7 auto" : "Readest")
                                                  : "KOReader";
    const char* lab[4] = {"Text", "Highlights", "Reading sync", "Start again"};
    const char* sub[4] = {textSub, markSub, syncSub, "page 1"};
    for (int i = 0; i < 4; i++) {
      const TRect t = rmenu::moreSetTile(i, W, H);
      c.drawRect(t.x, t.y, t.w, t.h, 2, true);
      const int cx = t.x + t.w / 2, cy = t.y + 34;
      switch (i) {
        case 0:
          c.textCentered(cx, cy - c.textHeight(TS_MED) / 2, "Aa", TS_MED, true, true);
          break;
        case 1:
          for (int r = 0; r < 3; r++) {
            c.fillRect(cx - 16, cy - 10 + r * 9, 4, 3, true);
            c.fillRect(cx - 8, cy - 10 + r * 9, r == 2 ? 16 : 24, 3, true);
          }
          break;
        case 2:
          moreIcon(c, 2, cx, cy, true);
          break;
        default:  // start again: a bar and a triangle pointing back to it
          c.fillRect(cx - 12, cy - 12, 3, 24, true);
          decor::triangle(c, cx + 12, cy - 12, cx + 12, cy + 12, cx - 6, cy, true);
          break;
      }
      c.textCentered(cx, t.y + 60, lab[i], TS_MED, true, true);
      c.textClipped(cx - c.textWidth(sub[i], TS_SMALL) / 2 < t.x + 6 ? t.x + 6 : cx - c.textWidth(sub[i], TS_SMALL) / 2,
                    t.y + 92, t.w - 12, sub[i], TS_SMALL, true);
    }
    moreFoot(c, k, pos);
  }

  // A title on up to two lines: the first breaks at the last space that fits
  // (or mid-word, by codepoint, for scripts with no spaces); the rest is
  // clipped with dots on the second.
  static void titleTwoLines(ToolsCanvas& c, int x, int y, int maxW, const char* s) {
    if (!s || !*s) return;
    if (c.textWidth(s, TS_MED, true) <= maxW) {
      c.text(x, y + 16, s, TS_MED, true, true);  // one line, centred in the two
      return;
    }
    char buf[192];
    int n = 0, lastSpace = -1;
    const char* p = s;
    while (*p) {
      const char* q = p;
      uni::next(q);
      const int add = (int)(q - p);
      if (n + add >= (int)sizeof(buf)) break;
      memcpy(buf + n, p, (size_t)add);
      buf[n + add] = 0;
      if (c.textWidth(buf, TS_MED, true) > maxW) break;
      if (*p == ' ') lastSpace = n;
      n += add;
      p = q;
    }
    int cut = n;
    if (*p && lastSpace > 0) cut = lastSpace;  // break at a word, not in one
    buf[cut] = 0;
    c.text(x, y, buf, TS_MED, true, true);
    const char* rest = s + cut;
    while (*rest == ' ') rest++;
    c.textClipped(x, y + 32, maxW, rest, TS_MED, true, true);
  }

  // Small line icons for the More tiles, in a ~32 px box.
  static void moreIcon(ToolsCanvas& c, int i, int cx, int cy, bool dark) {
    switch (i) {
      case 0:  // contents: lines
        for (int k = 0; k < 4; k++) c.fillRect(cx - 14, cy - 12 + k * 8, k % 2 ? 20 : 28, 3, dark);
        break;
      case 1:  // bookmark ribbon
        c.fillRect(cx - 10, cy - 16, 20, 26, dark);
        decor::triangle(c, cx - 10, cy + 16, cx + 10, cy + 16, cx, cy + 6, !dark);
        c.fillRect(cx - 10, cy + 10, 20, 6, dark);
        decor::triangle(c, cx - 10, cy + 16, cx + 9, cy + 16, cx, cy + 7, !dark);
        break;
      case 2:  // sync: two arcs with heads
        c.drawCircle(cx, cy, 13, 3, dark);
        c.fillRect(cx - 16, cy - 2, 32, 4, !dark);
        decor::triangle(c, cx + 9, cy - 4, cx + 17, cy - 4, cx + 13, cy + 4, dark);
        decor::triangle(c, cx - 17, cy + 4, cx - 9, cy + 4, cx - 13, cy - 4, dark);
        break;
      case 3:  // rotation: a turned letter A
        c.drawLine(cx - 12, cy + 10, cx, cy - 14, 3, dark);
        c.drawLine(cx, cy - 14, cx + 12, cy + 10, 3, dark);
        c.drawLine(cx - 6, cy + 1, cx + 6, cy + 1, 3, dark);
        break;
      case 4:  // settings: a gear
        decor::gear(c, cx, cy, 13, 8, dark);
        c.fillCircle(cx, cy, 4, !dark);
        break;
      default:  // exit: an arrow out of a door
        c.drawRect(cx - 14, cy - 14, 16, 28, 3, dark);
        c.drawLine(cx - 4, cy, cx + 16, cy, 3, dark);
        c.drawLine(cx + 9, cy - 7, cx + 16, cy, 3, dark);
        c.drawLine(cx + 9, cy + 7, cx + 16, cy, 3, dark);
        break;
    }
  }

  void moreTap(int x, int y) {
    const int W = host().canvas().width(), H = host().canvas().height();
    if (!rmenu::moreCard(W, H).hit(x, y)) {
      menuClose();
      return;
    }
    // Every page behind the card is a portrait one; stand up for it.
    // Returns whether it had to turn, which makes the repaint a full one.
    const auto standUp = [&] {
      if (host().canvasRotation() == 0) return false;
      host().setCanvasRotation(0);
      return true;
    };
    const auto show = [&](bool turned) {
      if (turned) host().refresh(true);
      else paint();
    };
    if (_moreSet) {
      moreSetTap(x, y, standUp, show);
      return;
    }
    switch (rmenu::hitMoreTile(x, y, W, H)) {
      case 0: {
        const bool t = standUp();
        _fromMore = true;
        _menu = rmenu::Page::Contents;
        _mpage = pageOfSpine();
        host().beep(0);
        show(t);
        return;
      }
      case 1:
        // Choosing the phrase happens on the page itself, at its angle.
        _fromMore = true;
        _menu = rmenu::Page::None;
        pickStart();
        return;
      case 2:
        if (!syncReady()) {
          host().beep(2);
          return;
        }
        {
          const bool t = standUp();
          _fromMore = true;
          _menu = rmenu::Page::Sync;
          _syncStage = kosync::haveAccount(prefs()) ? 1 : 3;
          host().beep(1);
          show(t);
        }
        return;
      case 3:
        // Free: back to the page, which now turns with the device by itself
        // and wears TAP TO LOCK until locked again.
        _rotFollow = !_rotFollow;
        host().beep(1);
        menuClose();
        return;
      case 4:
        _moreSet = true;  // the same card, its Settings side
        host().beep(0);
        paint();
        return;
      case 5:
        _menu = rmenu::Page::None;
        closeBook(true);
        return;
      default:
        return;
    }
  }

  template <class Up, class Show>
  void moreSetTap(int x, int y, Up& standUp, Show& show) {
    const int W = host().canvas().width(), H = host().canvas().height();
    if (rmenu::moreSetBack(W, H).hit(x, y)) {
      _moreSet = false;
      host().beep(0);
      paint();
      return;
    }
    int i = -1;
    for (int k = 0; k < 4; k++)
      if (rmenu::moreSetTile(k, W, H).hit(x, y)) i = k;
    switch (i) {
      case 0:  // the Text bar, over the page at its own angle
        _menu = rmenu::Page::TextBar;
        _moreSet = false;
        host().beep(0);
        paint();
        return;
      case 1: {
        const bool t = standUp();
        _fromMore = true;
        _moreSet = false;
        _menu = rmenu::Page::Marks;
        _mpage = 0;
        _listArmed = -1;
        host().beep(0);
        show(t);
        return;
      }
      case 2: {
        const bool t = standUp();
        _fromMore = true;
        _moreSet = false;
        _menu = rmenu::Page::SyncSet;
        _spArmed = false;
        _spNote = nullptr;
        host().beep(0);
        show(t);
        return;
      }
      case 3:
        _menu = rmenu::Page::StartBar;
        _moreSet = false;
        host().beep(0);
        paint();
        return;
      default:
        return;
    }
  }

  // --- the Text bar (owner, 26 Nov) ------------------------------------------
  // Six icon tabs and the chosen tab's control, at the foot of the page; the
  // page above is the preview, re-laid at once on every change.
  static void tabIcon(ToolsCanvas& c, int i, int cx, int cy) {
    switch (i) {
      case 0:  // font
        c.textCentered(cx, cy - c.textHeight(TS_MED) / 2, "Aa", TS_MED, true, true);
        break;
      case 1: {  // size: a small A and a big A
        const int sw = c.textWidth("A", TS_SMALL, true), bw = c.textWidth("A", TS_MED, true);
        c.text(cx - (sw + bw + 2) / 2, cy + c.textHeight(TS_MED) / 2 - c.textHeight(TS_SMALL) - 2, "A", TS_SMALL, true, true);
        c.text(cx - (sw + bw + 2) / 2 + sw + 2, cy - c.textHeight(TS_MED) / 2, "A", TS_MED, true, true);
        break;
      }
      case 2:  // line space: lines, and an arrow up and down beside them
        for (int k = 0; k < 3; k++) c.fillRect(cx - 4, cy - 10 + k * 9, 18, 3, true);
        c.fillRect(cx - 13, cy - 12, 3, 24, true);
        decor::triangle(c, cx - 17, cy - 8, cx - 6, cy - 8, cx - 12, cy - 15, true);
        decor::triangle(c, cx - 17, cy + 8, cx - 6, cy + 8, cx - 12, cy + 15, true);
        break;
      case 3:  // margins: two walls and the text between
        c.fillRect(cx - 16, cy - 14, 3, 28, true);
        c.fillRect(cx + 13, cy - 14, 3, 28, true);
        for (int k = 0; k < 4; k++) c.fillRect(cx - 8, cy - 10 + k * 7, k == 3 ? 10 : 16, 2, true);
        break;
      case 4:  // paragraphs: two blocks with a gap
        c.fillRect(cx - 14, cy - 13, 28, 3, true);
        c.fillRect(cx - 14, cy - 7, 20, 3, true);
        c.fillRect(cx - 14, cy + 4, 28, 3, true);
        c.fillRect(cx - 14, cy + 10, 18, 3, true);
        break;
      default:  // page turns: a page with a bolt
        c.drawRect(cx - 11, cy - 14, 22, 28, 2, true);
        c.drawLine(cx + 2, cy - 9, cx - 4, cy + 1, 2, true);
        c.drawLine(cx - 4, cy + 1, cx + 4, cy - 1, 2, true);
        c.drawLine(cx + 4, cy - 1, cx - 2, cy + 9, 2, true);
        break;
    }
  }

  void renderTextBar(ToolsCanvas& c) {
    renderPage(c);
    const int W = c.width(), H = c.height();
    const TRect b = rmenu::textBar(W, H);
    c.fillRect(b.x + 6, b.y + 6, b.w, b.h, true);
    c.fillRect(b.x, b.y, b.w, b.h, false);
    c.drawRect(b.x, b.y, b.w, b.h, 3, true);
    static const char* const NAMES[6][2] = {{"FONT", ""},   {"SIZE", ""},      {"LINE", "SPACE"},
                                            {"MARGINS", ""}, {"PARA-", "GRAPHS"}, {"PAGE", "TURNS"}};
    const int ry = rmenu::textRowY(W, H);
    const int lh = c.textHeight(TS_SMALL);
    if (NAMES[_textTab][1][0]) {
      c.textTracked(b.x + 12, ry + 26 - lh, NAMES[_textTab][0], TS_SMALL, true, true, 2);
      c.textTracked(b.x + 12, ry + 26, NAMES[_textTab][1], TS_SMALL, true, true, 2);
    } else {
      c.textTracked(b.x + 12, ry + 26 - lh / 2, NAMES[_textTab][0], TS_SMALL, true, true, 2);
    }
    auto seg3 = [&](const char* const* names, int sel) {
      for (int k = 0; k < 3; k++) {
        const TRect t = rmenu::textSeg(k, W, H);
        if (k == sel) c.fillRect(t.x, t.y, t.w, t.h, true);
        else c.drawRect(t.x, t.y, t.w, t.h, 2, true);
        c.textInBox(t.x, t.y, t.w, t.h, names[k], TS_SMALL, k != sel, k == sel);
      }
    };
    static const char* const L[3] = {"tight", "normal", "airy"};
    static const char* const M[3] = {"narrow", "normal", "wide"};
    static const char* const P[3] = {"small", "normal", "large"};
    static const char* const T[3] = {"fast", "normal", "best"};
    switch (_textTab) {
      case 0: {  // as many faces as fit, the one in use outlined; > for the whole list
        _fontChipN = 0;
        const int x0 = rmenu::textCtlX0(W, H), x1 = rmenu::textFontMore(W, H).x - 6;
        int x = x0;
        const int total = faceCount();
        const char* cur = faceLabel();
        for (int i = 0; i < total && _fontChipN < 4; i++) {
          const char* name = faceAt(i);
          const int w = c.textWidth(name, TS_SMALL, true) + 20;
          if (x + w > x1) break;
          const bool on = strcmp(name, cur) == 0;
          c.drawRect(x, ry + 4, w, 46, on ? 3 : 1, true);
          c.textInBox(x, ry + 4, w, 46, name, TS_SMALL, true, on);
          _fontChips[_fontChipN] = i;
          _fontChipX[_fontChipN] = (short)x;
          _fontChipW[_fontChipN] = (short)w;
          _fontChipN++;
          x += w + 6;
        }
        const TRect m = rmenu::textFontMore(W, H);
        c.drawLine(m.x + 14, m.y + 13, m.x + 24, m.y + 23, 3, true);
        c.drawLine(m.x + 24, m.y + 23, m.x + 14, m.y + 33, 3, true);
        break;
      }
      case 1: {
        const TRect mi = rmenu::textMinus(W, H), pl = rmenu::textPlus(W, H);
        c.drawRect(mi.x, mi.y, mi.w, mi.h, 2, true);
        c.fillRect(mi.x + 15, mi.y + mi.h / 2 - 1, mi.w - 30, 3, true);
        c.drawRect(pl.x, pl.y, pl.w, pl.h, 2, true);
        c.fillRect(pl.x + 15, pl.y + pl.h / 2 - 1, pl.w - 30, 3, true);
        c.fillRect(pl.x + pl.w / 2 - 1, pl.y + 11, 3, pl.h - 22, true);
        c.textInBox(mi.x + mi.w, mi.y, pl.x - mi.x - mi.w, mi.h, sizeLabel(), TS_MED, true, true);
        break;
      }
      case 2: seg3(L, _lead); break;
      case 3: seg3(M, _margin); break;
      case 4: seg3(P, _para); break;
      default: seg3(T, (int)mode()); break;
    }
    c.fillRect(b.x + 10, b.y + 63, b.w - 20, 1, true);
    for (int i = 0; i < 6; i++) {
      const TRect t = rmenu::textTab(i, W, H);
      if (i == _textTab) c.drawRect(t.x, t.y, t.w, t.h, 3, true);
      tabIcon(c, i, t.x + t.w / 2, t.y + t.h / 2);
    }
    {  // the close, set apart by a rule
      const TRect x = rmenu::textClose(W, H);
      c.fillRect(x.x - 8, x.y + 6, 2, x.h - 12, true);
      c.drawRect(x.x, x.y, x.w, x.h, 2, true);
      drawCross(c, x.x + x.w / 2, x.y + x.h / 2, 9, true);
    }
  }

  void textBarTap(int x, int y) {
    const int W = host().canvas().width(), H = host().canvas().height();
    // Docked (owner, 26 Nov): the page around the bar takes no taps, so a
    // missed control is just a missed control. x, OK or a side button closes.
    if (!rmenu::textBar(W, H).hit(x, y)) return;
    if (rmenu::textClose(W, H).hit(x, y)) {
      menuClose();
      return;
    }
    for (int i = 0; i < 6; i++)
      if (rmenu::textTab(i, W, H).hit(x, y)) {
        if (i != _textTab) {
          _textTab = (uint8_t)i;
          host().beep(0);
          paint();
        }
        return;
      }
    auto segHit = [&]() {
      for (int k = 0; k < 3; k++)
        if (rmenu::textSeg(k, W, H).hit(x, y)) return k;
      return -1;
    };
    switch (_textTab) {
      case 0: {
        if (rmenu::textFontMore(W, H).hit(x, y)) {  // the whole list, as before
          host().setCanvasRotation(0);
          _menu = rmenu::Page::Font;
          _mpage = 0;
          host().beep(0);
          host().refresh(true);
          return;
        }
        for (int i = 0; i < _fontChipN; i++)
          if (x >= _fontChipX[i] && x < _fontChipX[i] + _fontChipW[i]) {
            chooseFace(_fontChips[i]);
            paint();
            return;
          }
        return;
      }
      case 1:
        if (rmenu::textMinus(W, H).hit(x, y)) stepSize(-1);
        else if (rmenu::textPlus(W, H).hit(x, y)) stepSize(1);
        return;
      default: {
        const int k = segHit();
        if (k < 0) return;
        if (_textTab == 5) {
          if (k != (int)mode()) rmenu::setRefreshMode(prefs(), true, (rmenu::Refresh)k);
          host().beep(0);
          paint();
          return;
        }
        uint8_t* v = _textTab == 2 ? &_lead : _textTab == 3 ? &_margin : &_para;
        const char* key = _textTab == 2 ? "rd_lead" : _textTab == 3 ? "rd_marg" : "rd_para";
        if (k == *v) return;
        *v = (uint8_t)k;
        prefs().putUInt(key, *v);
        restyle();
        host().beep(0);
        paint();
        return;
      }
    }
  }

  // A face, by its place in the list: stored as a name, applied at once, and
  // the page reflowed from the offset being read.
  void chooseFace(int idx) {
    host().beep(1);
    if (host().fontSet(ToolsHost::FONT_READER, faceAt(idx))) {
      host().fontEnter(ToolsHost::FONT_READER);
      _face = faceIndexOf(host(), host().fontFor(ToolsHost::FONT_READER));
      restyle();
    }
  }

  // Back to the Text bar over the page, at the page's angle.
  void toTextBar() {
    _menu = rmenu::Page::TextBar;
    _textTab = 0;
    ensureStream();
    applyRot(_rot);
    syncPageRot();
    if (host().canvasRotation() != 0) host().refresh(true);
    else paint();
  }

  void renderStartBar(ToolsCanvas& c) {
    renderPage(c);
    const int W = c.width(), H = c.height();
    const TRect b = rmenu::startBar(W, H);
    c.fillRect(b.x + 6, b.y + 6, b.w, b.h, true);
    c.fillRect(b.x, b.y, b.w, b.h, false);
    c.drawRect(b.x, b.y, b.w, b.h, 3, true);
    const int lh = c.textHeight(TS_SMALL);
    c.textTracked(b.x + 12, b.y + b.h / 2 - lh, "START", TS_SMALL, true, true, 2);
    c.textTracked(b.x + 12, b.y + b.h / 2, "AGAIN", TS_SMALL, true, true, 2);
    c.text(b.x + 100, b.y + (b.h - lh) / 2, "back to page 1", TS_SMALL, true);
    const TRect g = rmenu::startGo(W, H);
    c.fillRect(g.x, g.y, g.w, g.h, true);
    c.textInBox(g.x, g.y, g.w, g.h, "Tap again", TS_MED, false, true);
  }

  void startBarTap(int x, int y) {
    const int W = host().canvas().width(), H = host().canvas().height();
    if (rmenu::startGo(W, H).hit(x, y)) {
      startAgain();
      return;
    }
    if (!rmenu::startBar(W, H).hit(x, y)) menuClose();
  }

  // Forget where this book was being read, and go back to its first page.
  //
  // Nothing is deleted: the position is SET to the beginning and saved the
  // ordinary way, which leaves a valid record on the card instead of a hole
  // that the move from the old folders would fill with an older position. Bookmarks are left alone --
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
    if (_menu == rmenu::Page::Sync && (_syncStage == 1 || _syncStage == 3)) return;
    if (_menu == rmenu::Page::Font) {  // the list came from the Text bar
      toTextBar();
      return;
    }
    // Every page is reached from More now (owner, 26 Nov): back is back to
    // the page it was over.
    syncAbort();
    _fromMore = false;
    menuClose();
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
  // A chapter's own first words, for books whose contents could not be read --
  // and there are plenty: an NCX whose hrefs resolve to nothing leaves the
  // reader with a spine and no names at all. Six words is enough to carry a
  // heading, because a heading is what the first words of a chapter usually
  // are: this is what turns "chapter 14" into "Chapter 7: Friends".
  //
  // Costs one chapter open, so it is called for rows on screen and once on the
  // way out of a book, never for a whole shelf.
  bool spineTaste(int spine, char* out, int cap) {
    out[0] = 0;
    if (!_book.chapterOpen(spine)) return false;
    int used = 0;
    char w[epubc::WORD_CAP];
    uint32_t off = 0;
    for (int k = 0; k < 6 && used < cap - 2; k++) {
      const int t = _book.next(w, off);
      if (t == epubc::TOK_END || t == epubc::TOK_ERR) break;
      if (t != epubc::TOK_WORD) continue;
      if (used) out[used++] = ' ';
      used += snprintf(out + used, (size_t)(cap - used), "%s", w);
    }
    _streamLost = true;
    return out[0] != 0;
  }

  void tocRow(int i, char* label, int cap, int& spine) {
    if (_ntoc > 0) {
      spine = epubui::g_toc[i].spine;
      snprintf(label, (size_t)cap, "%s", epubui::g_toc[i].title);
      return;
    }
    spine = i;
    char taste[64] = "";
    if (spineTaste(i, taste, sizeof(taste)))
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
    ContentFace cf(host());
    ToolsCanvas& c = host().canvas();
    return c.textHeight(bodyTS()) + epubui::leadAir(_lead);
  }

  // Which word of a line a tap landed on, measured from the same string the
  // page was drawn from. -1 when the tap is past the end of the line.
  int wordAt(const char* line, int tapX) {
    // In the book's face, because it is the book's words being measured: an
    // underline drawn from the device's widths lands under the wrong word.
    ContentFace cf(host());
    ToolsCanvas& c = host().canvas();
    const TSize ts = bodyTS();
    char probe[200];
    int i = 0, word = 0;
    while (line[i]) {
      int j = i;
      while (line[j] && line[j] != ' ') j++;
      const int n = j - i > (int)sizeof(probe) - 1 ? (int)sizeof(probe) - 1 : j - i;
      memcpy(probe, line, (size_t)(i + n) > sizeof(probe) - 1 ? sizeof(probe) - 1 : (size_t)(i + n));
      probe[(size_t)(i + n) > sizeof(probe) - 1 ? sizeof(probe) - 1 : (size_t)(i + n)] = 0;
      if (epubui::marginPx(_margin) + c.textWidth(probe, ts) > tapX) return word;
      while (line[j] == ' ') j++;
      i = j;
      word++;
    }
    return word > 0 ? word - 1 : -1;
  }

  // The x span of word `w` in `line`, for the underline.
  void wordSpan(const char* line, int w, int& x0, int& x1) {
    FaceScope fs(host(), _face);
    ContentFace cf(host());
    ToolsCanvas& c = host().canvas();
    const TSize ts = bodyTS();
    char probe[200];
    int i = 0, word = 0;
    x0 = x1 = epubui::marginPx(_margin);
    while (line[i]) {
      int j = i;
      while (line[j] && line[j] != ' ') j++;
      if (word == w) {
        const size_t pre = (size_t)i < sizeof(probe) ? (size_t)i : sizeof(probe) - 1;
        memcpy(probe, line, pre);
        probe[pre] = 0;
        x0 = epubui::marginPx(_margin) + c.textWidth(probe, ts);
        const size_t upto = (size_t)j < sizeof(probe) ? (size_t)j : sizeof(probe) - 1;
        memcpy(probe, line, upto);
        probe[upto] = 0;
        x1 = epubui::marginPx(_margin) + c.textWidth(probe, ts);
        return;
      }
      while (line[j] == ' ') j++;
      i = j;
      word++;
    }
  }

  // Everything from (l0,w0) to (l1,w1), joined by single spaces and cut to the
  // label's length -- a bookmark is a reminder, not a quotation.
  void buildPhrase() { phraseInto(_phrase, sizeof(_phrase)); }
  void phraseInto(char* out, size_t cap) {
    out[0] = 0;
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
          const int room = (int)cap - 1 - used;
          if (room <= 1) {
            // Out of label: say so with an ellipsis rather than stopping mid-word.
            if (used > (int)cap - 5) used = (int)cap - 5;
            snprintf(out + used, 5, "...");
            return;
          }
          if (used) out[used++] = ' ';
          const int n = j - i < room - 1 ? j - i : room - 1;
          memcpy(out + used, line + i, (size_t)n);
          used += n;
          out[used] = 0;
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
    // Leaves the Highlight / Cancel bar too (it lives on the Keep state):
    // without this the bar stayed up after Cancel, and with no words chosen
    // it moved to the top of the page.
    _picking = false;
    _pickL0 = _pickL1 = -1;
    if (_menu == rmenu::Page::Keep) _menu = rmenu::Page::None;
    host().beep(2);
    host().refresh(false);
  }

  // One Readest knows of is buried: a pull must not bring it back, and the
  // next sync tells Readest (deleted on one side, gone on both -- owner). One
  // it never saw just goes, with its waiting text.
  void dropMark(int at) {
    const bool known = marks::known(_marks[at]);
    const bool hadText = _marks[at].id[0] && !known;
    if (known) marks::bury(_marks[at].id);
    marks::remove(_marks, _nmarks, at);
    marks::save(host(), _books[_cur].file, _marks, _nmarks);
    if (hadText) marks::keepTexts(host(), _books[_cur].file, _marks, _nmarks);
  }

  // Highlights kept here that Readest does not have yet, and deletions it has
  // not heard of: what a sync on the way out would carry.
  int hlWaiting() const {
    int n = marks::tombs().n;
    for (int i = 0; i < _nmarks; i++)
      n += _marks[i].src == marks::SRC_HERE && _marks[i].label[0] && !(_marks[i].flags & marks::FLAG_SENT);
    return n;
  }

  // The book's offset of byte `pos` of line `l`; false off the anchors.
  bool offsetAt(int l, int pos, uint32_t& out) const {
    int best = -1;
    for (int i = 0; i < _ancN; i++)
      if (_anc[i].line == l && _anc[i].pos <= pos) best = i;
    if (best < 0) return false;
    uint32_t n = 0;
    for (const char* q = _lines[l].t + _anc[best].pos; q < _lines[l].t + pos && *q;) {
      uni::next(q);
      n++;
    }
    out = _anc[best].off + n;
    return true;
  }

  // The picked words' [start, end) in the book.
  bool pickRange(uint32_t& a, uint32_t& z) const {
    if (_pickL0 < 0 || _pickL1 < 0 || _pickL1 >= _lineN) return false;
    int a0, a1, z0, z1;
    if (!wordBytes(_lines[_pickL0].t, _pickW0, a0, a1) || !wordBytes(_lines[_pickL1].t, _pickW1, z0, z1))
      return false;
    // The last codepoint's first byte (Thai and CJK are three bytes each).
    int last = z1 - 1;
    while (last > z0 && (((uint8_t)_lines[_pickL1].t[last]) & 0xC0) == 0x80) last--;
    if (!offsetAt(_pickL0, a0, a) || !offsetAt(_pickL1, last, z)) return false;
    // z is where the last codepoint starts; one past it ends the range.
    z += 1;
    return z > a;
  }

  void keepMark(const char* label) {
    marks::Mark m{};
    m.spine = (uint16_t)_spine;
    m.page = (uint16_t)_page;
    m.off = curOffset();
    snprintf(m.label, sizeof(m.label), "%s", label && label[0] ? label : "");
    static char whole[marks::TEXT_MAX + 1];  // static: the loop task's stack is small
    whole[0] = 0;
    if (m.label[0]) {
      // D part B: a highlight gets the id Readest will know it by, its exact
      // place, and the whole phrase -- the label is only what fits.
      rhl::newId(m.id, sizeof(m.id));
      uint32_t a = 0, z = 0;
      if (pickRange(a, z)) {
        m.off = a;
        m.end = z;
      }
      phraseInto(whole, sizeof(whole));
    }
    const bool added = marks::add(_marks, _nmarks, m) >= 0;
    if (added) {
      if (whole[0]) marks::keepTexts(host(), _books[_cur].file, _marks, _nmarks, m.id, whole);
      marks::save(host(), _books[_cur].file, _marks, _nmarks);
    }
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

  // --- v2 highlight: the chosen words shown inverted on the page, and the
  // action bar in whichever half of the glass they are not in. ---------------
  TRect keepBar() {
    ToolsCanvas& c = host().canvas();
    const int H = c.height(), W = c.width();
    int midY = H;
    if (_pickL0 >= 0 && _pickL1 >= 0 && _pickL1 < _lineN)
      midY = (_lines[_pickL0].y + _lines[_pickL1].y) / 2;
    const bool selTop = midY < H / 2;
    return TRect{24, selTop ? H - 240 : 120, W - 48, 96};
  }

  // Byte range of word w in line.
  static bool wordBytes(const char* line, int w, int& b0, int& b1) {
    int i = 0, word = 0;
    while (line[i]) {
      int j = i;
      while (line[j] && line[j] != ' ') j++;
      if (word == w) {
        b0 = i;
        b1 = j;
        return true;
      }
      while (line[j] == ' ') j++;
      i = j;
      word++;
    }
    return false;
  }

  void drawSelection(ToolsCanvas& c) {
    if (_pickL0 < 0 || _pickL1 < 0) return;
    FaceScope fs(host(), _face);
    ContentFace cf(host());
    const TSize ts = bodyTS();
    const int step = lineStepNow();
    for (int l = _pickL0; l <= _pickL1 && l < _lineN; l++) {
      const char* line = _lines[l].t;
      int nWords = 0;
      for (int b0, b1; wordBytes(line, nWords, b0, b1);) nWords++;
      if (nWords == 0) continue;
      const int wa = l == _pickL0 ? _pickW0 : 0;
      const int wb = l == _pickL1 ? _pickW1 : nWords - 1;
      int a0, a1, z0, z1;
      if (!wordBytes(line, wa, a0, a1) || !wordBytes(line, wb, z0, z1)) continue;
      int x0, xa, x1, xz;
      wordSpan(line, wa, x0, xa);
      wordSpan(line, wb, xz, x1);
      c.fillRect(x0 - 3, _lines[l].y - 2, x1 - x0 + 6, step - 2, true);
      char seg[201];
      const int n = z1 - a0 < 200 ? z1 - a0 : 200;
      memcpy(seg, line + a0, (size_t)n);
      seg[n] = 0;
      c.text(x0, _lines[l].y, seg, ts, false);
    }
  }

  void renderKeep(ToolsCanvas& c) {
    renderPage(c);
    drawSelection(c);
    const TRect b = keepBar();
    c.fillRect(b.x + 6, b.y + 6, b.w, b.h, true);  // a solid edge, like More
    c.fillRect(b.x, b.y, b.w, b.h, false);
    c.drawRect(b.x, b.y, b.w, b.h, 3, true);
    c.fillRect(b.x, b.y, b.w / 2, b.h, true);
    c.textInBox(b.x, b.y, b.w / 2, b.h, "Highlight", TS_LARGE, false, true);
    c.textInBox(b.x + b.w / 2, b.y, b.w / 2, b.h, "Cancel", TS_LARGE, true);
    // What the picker said, on the label at the top.
    drawPickLabel(c, "HIGHLIGHT THESE WORDS?", "or tap another word to start again");
  }

  // The picker's label, in the footer band at the foot of the page -- the
  // top would cover the first line, whose words have to stay pickable.
  void drawPickLabel(ToolsCanvas& c, const char* what, const char* sub) {
    const int fy = c.height() - epubui::FOOT_H;
    c.fillRect(0, fy, c.width(), epubui::FOOT_H, true);
    c.textTrackedCentered(c.width() / 2, fy + 6, what, TS_SMALL, false, true, 1);
    if (sub) c.textCentered(c.width() / 2, fy + 30, sub, TS_SMALL, false);
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
    drawPickLabel(c, _pickL0 < 0 ? "HIGHLIGHT  1 / 2  TAP THE FIRST WORD"
                                 : "HIGHLIGHT  2 / 2  TAP THE LAST WORD",
                  "tap this bar to stop");
  }

  void renderMenu(ToolsCanvas& c) {
    char buf[64];
    if (_menu == rmenu::Page::More) {
      renderMore(c);
      return;
    }
    if (_menu == rmenu::Page::HlCard) {
      renderHlCard(c);
      return;
    }
    if (_menu == rmenu::Page::TextBar) {
      renderTextBar(c);
      return;
    }
    if (_menu == rmenu::Page::StartBar) {
      renderStartBar(c);
      return;
    }
    if (_menu == rmenu::Page::SyncSet) {
      host().topBar("READING SYNC", false, "OPTIONS");
      syncpg::render(c, prefs(), _spArmed, _spNote);
      return;
    }
    if (_menu == rmenu::Page::Sync) {
      renderSync(c);
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

    if (_menu == rmenu::Page::Font) {
      host().topBar("FONT", false, "OPTIONS");
      c.textCentered(c.width() / 2, 56, "what this book is set in", TS_SMALL, true);
      const int total = faceCount();
      for (int k = 0; k < shelf::PER_PAGE; k++) {
        const int idx = _mpage * shelf::PER_PAGE + k;
        if (idx >= total) break;
        const char* name = faceAt(idx);
        const bool on = strcmp(name, faceLabel()) == 0;
        // A built-in is drawn in itself -- the fairest test of a face is the
        // face. A family off the card is drawn in the reading face instead:
        // showing it would mean reading megabytes to set one line, at the
        // moment somebody is only browsing a list.
        if (idx < host().typefaceCount()) {
          FaceScope fs(host(), idx);
          rmenu::drawRow(c, k, name, "built in", shelf::rowSep(k, idx, total), on);
        } else {
          rmenu::drawRow(c, k, name, "on the card", shelf::rowSep(k, idx, total), on);
        }
      }
      shelf::drawPager(c, _mpage, total);
      c.textCentered(c.width() / 2, 770,
                     _famN < 0 ? "no card, so only the built-in faces"
                               : "a card face takes a moment to open a book",
                     TS_SMALL, true);
      return;
    }

    const bool contents = _menu == rmenu::Page::Contents;
    host().topBar(contents ? "CONTENTS" : "HIGHLIGHTS", false, "OPTIONS");
    const int total = contents ? tocCount() : _nmarks;
    if (total <= 0) {
      rmenu::drawEmpty(c, "no highlights yet", "More > Highlight, then two words");
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
        char label[marks::LABEL + 8], where[96];
        markLabel(_marks[idx], label, sizeof(label));
        markWhere(idx, where, sizeof(where), true);
        // Two lines for the phrase: it is a sentence somebody chose, and the
        // half past the clip was usually the half they chose it for. The x
        // at the right removes it (owner, 26 Nov).
        rmenu::drawRowWrap(c, k, label, where, shelf::rowSep(k, idx, total), markHere(idx), 64);
        const TRect xr = markX(k);
        const bool armed = _listArmed == idx;
        if (armed) c.fillRect(xr.x, xr.y, xr.w, xr.h, true);
        else c.drawRect(xr.x, xr.y, xr.w, xr.h, 2, true);
        drawCross(c, xr.x + xr.w / 2, xr.y + xr.h / 2, 9, !armed);
      }
    }
    shelf::drawPager(c, _mpage, total);
    if (!contents)
      c.textCentered(c.width() / 2, 770,
                     _listArmed >= 0 ? "tap x again: removed in Readest too" : "tap a highlight to go to its page",
                     TS_SMALL, true);
  }

  // The x on Highlights row `slot`.
  static TRect markX(int slot) {
    const int y = shelf::Y0 + slot * shelf::ROW_H;
    return TRect{480 - 16 - 48, y + (shelf::ROW_H - 6 - 48) / 2, 48, 48};
  }
  static void drawCross(ToolsCanvas& c, int cx, int cy, int r, bool black) {
    for (int t = -1; t <= 1; t++)
      for (int i = -r; i <= r; i++) {
        c.fillRect(cx + i + t, cy + i, 1, 1, black);
        c.fillRect(cx + i + t, cy - i, 1, 1, black);
      }
  }

  // "Chapter 1: The Start of the Mayhem · p 3 · Readest", in the book's own
  // chapter name (owner, 26 Nov), the page when it is known.
  void markWhere(int i, char* out, int cap, bool withSrc) {
    const marks::Mark& m = _marks[i];
    char ch[48];
    chapterName(m.spine, ch, sizeof(ch));
    int pg = -1;
    if (m.page != marks::NO_PAGE) pg = m.page;
    else if (markHere(i)) pg = _page;
    const char* from = m.src == marks::SRC_READEST ? "Readest" : "this Sticky";
    int n = snprintf(out, (size_t)cap, "%s", ch);
    if (pg >= 0 && n < cap) n += snprintf(out + n, (size_t)(cap - n), " \xc2\xb7 p %d", pg + 1);
    if (withSrc && n < cap) snprintf(out + n, (size_t)(cap - n), " \xc2\xb7 %s", from);
  }

  void menuTap(int x, int y) {
    if (_menu == rmenu::Page::More) {
      moreTap(x, y);
      return;
    }
    if (_menu == rmenu::Page::HlCard) {
      hlCardTap(x, y);
      return;
    }
    if (_menu == rmenu::Page::TextBar) {
      textBarTap(x, y);
      return;
    }
    if (_menu == rmenu::Page::StartBar) {
      startBarTap(x, y);
      return;
    }
    if (host().isBackTap(x, y)) {
      menuBack();
      return;
    }
    if (_menu == rmenu::Page::SyncSet) {
      if (syncpg::tap(host(), x, y, _spArmed, _spNote)) paint();
      return;
    }

    if (_menu == rmenu::Page::Sync) {
      if (_syncStage == 4) {
        if (y >= SYNC_BTN_Y + 120 && y < SYNC_BTN_Y + 120 + 80) {  // CANCEL
          syncAbort();
          menuBack();
        }
        return;
      }
      if (_syncStage != 2) return;  // nothing to press while it works
      const int W = host().canvas().width();
      const bool left = x < W / 2;
      if (y >= SYNC_BTN_Y && y < SYNC_BTN_Y + SYNC_BTN_H) {
        if (_syncOut == SO_ASK && left) {
          syncJump();
          return;
        }
        // STAY HERE, or the one button every other answer has.
        _syncStage = 0;
        menuClose();
      }
      return;
    }

    if (_menu == rmenu::Page::Keep) {
      const TRect bar = keepBar();
      if (bar.hit(x, y)) {
        if (x < bar.x + bar.w / 2)
          keepMark(_phrase);
        else
          pickCancel();
        return;
      }
      // v2: a third tap on the words starts a new selection there.
      const int step = lineStepNow();
      for (int i = 0; i < _lineN; i++) {
        if (y < _lines[i].y - 6 || y >= _lines[i].y + step) continue;
        const int word = wordAt(_lines[i].t, x);
        if (word < 0) return;
        _menu = rmenu::Page::None;
        _picking = true;
        _pickL0 = i;
        _pickW0 = word;
        _pickL1 = -1;
        host().beep(0);
        host().refresh(false);
        return;
      }
      return;
    }

    if (_menu == rmenu::Page::Font) {
      const int total = faceCount();
      const int pages = shelf::pageCount(total);
      if (y >= shelf::PAGER_Y && pages > 1) {
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
      chooseFace(idx);
      toTextBar();
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
    // The x removes; one Readest knows asks twice, since it goes on both sides.
    if (markX(idx - _mpage * shelf::PER_PAGE).hit(x, y)) {
      if (marks::known(_marks[idx]) && _listArmed != idx) {
        _listArmed = idx;
        host().beep(2);
        paint();
        return;
      }
      _listArmed = -1;
      dropMark(idx);
      if (_mpage > 0 && _mpage * shelf::PER_PAGE >= _nmarks) _mpage--;
      host().beep(1);
      paint();
      return;
    }
    _listArmed = -1;
    jumpTo(_marks[idx].spine, _marks[idx].off);
  }


  // --- Book settings (owner, 26 Nov) ----------------------------------------
  // Words broken into lines of at most `maxW`: at spaces, or by codepoint for
  // a word (or a script) with none. Returns the lines written.
  static int wrapText(ToolsCanvas& c, const char* s, TSize ts, bool bold, int maxW, char (*out)[160], int maxLines) {
    int n = 0;
    out[0][0] = 0;
    const char* p = s;
    while (*p && n < maxLines) {
      while (*p == ' ') p++;
      if (!*p) break;
      int len = 0, lastSpace = -1;
      const char* q = p;
      char* o = out[n];
      while (*q) {
        const char* r = q;
        uni::next(r);
        const int add = (int)(r - q);
        if (len + add >= 159) break;
        memcpy(o + len, q, (size_t)add);
        o[len + add] = 0;
        if (c.textWidth(o, ts, bold) > maxW) {
          o[len] = 0;
          break;
        }
        if (*q == ' ') lastSpace = len;
        len += add;
        q = r;
      }
      if (*q && lastSpace > 0) {  // break at the last space that fitted
        o[lastSpace] = 0;
        q = p + lastSpace + 1;
      } else if (len == 0) {  // not even one codepoint fits: take it anyway
        const char* r = p;
        uni::next(r);
        memcpy(o, p, (size_t)(r - p));
        o[r - p] = 0;
        q = r;
      }
      p = q;
      n++;
      if (n < maxLines) out[n][0] = 0;
    }
    return n;
  }

  // Size, one step: a family off the card walks its own sizes (each step
  // reads the files for it); a built-in face walks the firmware's three.
  void stepSize(int step) {
    if (cardSizes()) {
      const int nv = cardSizeAt() + step;
      if (nv < 0 || nv >= host().faceSizeCount() || !host().faceSizeSet(nv)) {
        host().beep(2);
        return;
      }
    } else {
      const int nv = (int)_size + step;
      if (nv < 0 || nv >= epubui::SIZES) {
        host().beep(2);
        return;
      }
      _size = (uint8_t)nv;
      prefs().putUInt("rd_size", _size);
    }
    // The page is re-derived from where the reader IS, not from the page
    // number, because the page number means nothing once the type changes.
    restyle();
    host().beep(0);
    paint();
  }

  // --- a highlight's card (owner, 26 Nov) ------------------------------------
  // Tapped on the page: the page faded, the card over it at the page's own
  // angle -- the words, where they are, where they came from, Remove / Close.
  void hlCardOpen(int k) {
    _hlCard = k;
    _hlArmed = false;
    _menu = rmenu::Page::HlCard;
    host().beep(0);
    paint();
  }

  TRect hlCardRect(ToolsCanvas& c, int& quoteN, char (*quote)[160]) {
    const int W = c.width(), H = c.height();
    const int cw = W - 48;
    quoteN = wrapText(c, _marks[_hlCard].label, TS_MED, false, cw - 2 * 18 - 16, quote, 3);
    if (quoteN < 1) quoteN = 1;
    const int ch = 16 + 26 + 10 + quoteN * (c.textHeight(TS_MED) + 6) + 8 + 26 + 14 + 58 + 18;
    // Under the words when there is room, else over them.
    int wy = H / 2, wb = H / 2;
    for (int i = 0; i < _ulN; i++)
      if (_ul[i].mark == _hlCard) {
        wy = _ul[i].y;
        wb = _ul[i].y + _ul[i].h;
        break;
      }
    int y = wb + 24;
    if (y + ch > H - 16) y = wy - 24 - ch;
    if (y < 16) y = (H - ch) / 2;
    return TRect{24, y, cw, ch};
  }
  TRect hlRemoveRect(const TRect& k) { return TRect{k.x + 18, k.y + k.h - 18 - 58, (k.w - 36 - 10) / 2, 58}; }
  TRect hlCloseRect(const TRect& k) {
    const TRect r = hlRemoveRect(k);
    return TRect{r.x + r.w + 10, r.y, r.w, r.h};
  }

  void renderHlCard(ToolsCanvas& c) {
    renderPage(c);
    if (_hlCard < 0 || _hlCard >= _nmarks) return;
    host().fadeBehind();
    static char quote[3][160];
    int qn = 0;
    const TRect k = hlCardRect(c, qn, quote);
    c.fillRect(k.x + 8, k.y + 8, k.w, k.h, true);
    c.fillRect(k.x, k.y, k.w, k.h, false);
    c.drawRect(k.x, k.y, k.w, k.h, 3, true);
    const marks::Mark& m = _marks[_hlCard];
    int y = k.y + 16;
    c.textTracked(k.x + 18, y + 2, "HIGHLIGHT", TS_SMALL, true, true, 3);
    {  // where it came from, a tag at the top right
      const char* src = m.src == marks::SRC_READEST ? "Readest" : "this Sticky";
      const int tw = c.textWidth(src, TS_SMALL) + 20, th = c.textHeight(TS_SMALL) + 6;
      c.drawRect(k.x + k.w - 18 - tw, y - 2, tw, th, 2, true);
      c.textInBox(k.x + k.w - 18 - tw, y - 2, tw, th, src, TS_SMALL, true);
    }
    y += 36;
    const int lh = c.textHeight(TS_MED) + 6;
    c.fillRect(k.x + 18, y, 4, qn * lh - 6, true);
    for (int i = 0; i < qn; i++) c.text(k.x + 18 + 16, y + i * lh, quote[i], TS_MED, true);
    y += qn * lh + 8;
    char where[96];
    markWhere(_hlCard, where, sizeof(where), false);
    c.textClipped(k.x + 18, y, k.w - 36, where, TS_SMALL, true);
    const TRect rm = hlRemoveRect(k), cl = hlCloseRect(k);
    c.fillRect(rm.x, rm.y, rm.w, rm.h, true);
    c.textInBox(rm.x, rm.y, rm.w, rm.h, _hlArmed ? "Tap again" : "Remove", TS_MED, false, true);
    c.drawRect(cl.x, cl.y, cl.w, cl.h, 2, true);
    c.textInBox(cl.x, cl.y, cl.w, cl.h, "Close", TS_MED, true);
  }

  void hlCardTap(int x, int y) {
    if (_hlCard < 0 || _hlCard >= _nmarks) {
      menuClose();
      return;
    }
    ToolsCanvas& c = host().canvas();
    static char quote[3][160];
    int qn = 0;
    const TRect k = hlCardRect(c, qn, quote);
    if (hlRemoveRect(k).hit(x, y)) {
      // One Readest knows goes on both sides: that asks twice.
      if (marks::known(_marks[_hlCard]) && !_hlArmed) {
        _hlArmed = true;
        host().beep(2);
        paint();
        return;
      }
      dropMark(_hlCard);
      _hlCard = -1;
      _menu = rmenu::Page::None;
      host().beep(1);
      host().refresh(false);
      return;
    }
    // Close, or anywhere off the card: back to the page.
    _hlCard = -1;
    menuClose();
  }

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
    // The book's own face, if one was chosen for the reader. Only the page:
    // the footer under it is the firmware saying where you are, not the book
    // speaking, and it stays in the device's face.
    if (_pageImage[0]) {
      {
        ContentFace cf(host());
        if (!drawImagePage(c)) drawImagePlate(c);
      }
      if (_chrome) renderFooter(c);
      return;
    }
    ContentFace cf(host());
    // Each line in its own type, and in runs where the STYLE changes inside
    // it: the parser said which bytes were inside a <b> or <strong> and which
    // inside an <i>, <em> or <cite>, and a heading line carries its level.
    // Drawn left to right, each run measured as it is placed, which is the
    // same arithmetic the layout did.
    for (int i = 0; i < _lineN; i++) {
      const Line& ln = _lines[i];
      const TSize lts = tsFor(ln.head);
      const int n = (int)strlen(ln.t);
      int x = epubui::marginPx(_margin);
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
    drawHighlights(c);
    drawPageMarks(c);
    if (_rotFollow) drawLockPlate(c);
    if (_guide) {
      cf.off();
      drawGuide(c);
      return;
    }
    if (!_chrome) return;

    // Out of the book's face first. The footer is the firmware saying where
    // you are, not the book speaking -- and it is drawn in the 24 px box,
    // which is the very box a family's chosen size sits in, so leaving the
    // face on would set the chapter name and the page number in whatever the
    // book was set in and run them out of a 56 px band.
    cf.off();
    renderFooter(c);
  }

  // The first line of a chapter's first page is its heading, in every book
  // that prints one -- which is why the contents list falls back to a chapter's
  // first words and gets "Chapter 7: Friends" out of a book with no contents at
  // all. Taken from the laid-out line rather than from a second pass over the
  // text: it is already here, already trimmed, and already whole.
  void noteChapterNameFromPage() {
    if (_chapName[0] || _lutN != 0 || _lineN <= 0 || _spine != _chapNameSpine) return;
    const char* first = _lines[0].t;
    if (!first[0]) return;
    int take = (int)strlen(first);
    if (take > (int)sizeof(_chapName) - 1) take = (int)sizeof(_chapName) - 1;
    while (take > 0 && ((uint8_t)first[take] & 0xC0) == 0x80) take--;
    memcpy(_chapName, first, (size_t)take);
    _chapName[take] = 0;
  }

  // Kept highlights, underlined where their words are on this page (v2).
  // Found by their text, the way the label was made: the page's lines joined
  // by single spaces, so a phrase that ran across a line break still matches.
  void drawHighlights(ToolsCanvas& c) {
    _ulCount = 0;
    _ulN = 0;
    if (_nmarks <= 0 || _lineN <= 0 || _picking) return;
    bool any = false;
    for (int k = 0; k < _nmarks; k++) any |= _marks[k].label[0] && _marks[k].spine == (uint16_t)_spine;
    if (!any) return;
    const size_t cap = (size_t)_lineN * 201 + 1;
    char* joined = (char*)malloc(cap);
    int* starts = (int*)malloc(sizeof(int) * (size_t)_lineN);
    if (!joined || !starts) {
      free(joined);
      free(starts);
      return;
    }
    int n = 0;
    for (int l = 0; l < _lineN; l++) {
      if (l) joined[n++] = ' ';
      starts[l] = n;
      const int len = (int)strlen(_lines[l].t);
      memcpy(joined + n, _lines[l].t, (size_t)len);
      n += len;
    }
    joined[n] = 0;
    ContentFace cf(host());
    const TSize ts = bodyTS();
    const int mx = epubui::marginPx(_margin);
    _ulN = 0;
    int curMark = -1;
    auto under = [&](int l, int ca, int cz) {
      _ulCount++;
      char pre[201];
      memcpy(pre, _lines[l].t, (size_t)ca);
      pre[ca] = 0;
      const int x0 = mx + c.textWidth(pre, ts);
      memcpy(pre, _lines[l].t, (size_t)cz);
      pre[cz] = 0;
      const int x1 = mx + c.textWidth(pre, ts);
      c.fillRect(x0, _lines[l].y + c.textHeight(ts) - 1, x1 - x0, 2, true);
      if (_ulN < MAX_UL && curMark >= 0)
        _ul[_ulN++] = UlRect{(short)x0, (short)_lines[l].y, (short)(x1 - x0), (short)(c.textHeight(ts) + 2),
                             (uint8_t)curMark};
    };
    for (int k = 0; k < _nmarks; k++) {
      const marks::Mark& m = _marks[k];
      if (!m.label[0] || m.spine != (uint16_t)_spine) continue;
      curMark = k;
      // D: one with a range is underlined by it -- all of it, where a label is
      // only its first 96 bytes. The text match is for those kept before.
      if (m.end > m.off && _ancN > 0) {
        int segL[epubui::MAX_LINES], segA[epubui::MAX_LINES], segZ[epubui::MAX_LINES], ns = 0;
        uint32_t firstAt = 0;  // the offset the underline starts at on this page
        for (int l = 0; l < _lineN; l++) {
          int ca = -1, cz = -1;
          const char* t = _lines[l].t;
          int ai = -1;
          uint32_t at = 0;
          for (const char* q = t; *q;) {
            const int i = (int)(q - t);
            // The anchor this byte runs on: the latest at or before it.
            for (int j = ai + 1; j < _ancN; j++) {
              if (_anc[j].line != l || _anc[j].pos > i) continue;
              if (_anc[j].pos == i) {
                ai = j;
                at = _anc[j].off;
              }
            }
            const char* was = q;
            uni::next(q);
            if (ai >= 0 && *was != ' ' && at >= m.off && at < m.end) {
              if (ca < 0 && ns == 0) firstAt = at;
              if (ca < 0) ca = i;
              cz = (int)(q - t);
            }
            if (ai >= 0) at++;
          }
          if (ca >= 0 && ns < epubui::MAX_LINES) {
            segL[ns] = l;
            segA[ns] = ca;
            segZ[ns++] = cz;
          }
        }
        // Trusted only when the words there are the label's: offsets counted
        // differently somewhere would otherwise underline the wrong words.
        // Compared without spaces, as Readest compares them.
        // None of its range on this page: it is elsewhere -- never matched
        // by its words here (a one-word "The" would underline every "The").
        if (ns == 0) continue;
        // Begun on an earlier page: the label's words are over there.
        bool same = true;
        if (firstAt == m.off) {
          const char* lab = m.label;
          int s0 = 0, i = segA[0];
          while (same && *lab) {
            if (*lab == ' ' || (lab[0] == '.' && !strcmp(lab, "..."))) {
              if (*lab != ' ') break;
              lab++;
              continue;
            }
            while (s0 < ns && i >= segZ[s0]) {
              if (++s0 < ns) i = segA[s0];
            }
            if (s0 >= ns) break;  // the page ends first: the rest is overleaf
            const char ch = _lines[segL[s0]].t[i++];
            if (ch == ' ') continue;
            if (ch != *lab++) same = false;
          }
        }
        if (same) {
          for (int j = 0; j < ns; j++) under(segL[j], segA[j], segZ[j]);
          continue;
        }
      }
      // By its words: only on the page it belongs to.
      if (!markHere(k)) continue;
      char want[marks::LABEL];
      snprintf(want, sizeof(want), "%s", m.label);
      size_t wl = strlen(want);
      if (wl >= 3 && strcmp(want + wl - 3, "...") == 0) want[wl -= 3] = 0;
      if (wl < 3) continue;
      const char* hit = strstr(joined, want);
      if (!hit) continue;
      const int a = (int)(hit - joined), z = a + (int)wl;
      for (int l = 0; l < _lineN; l++) {
        const int ls = starts[l], le = ls + (int)strlen(_lines[l].t);
        if (z <= ls || a >= le) continue;
        under(l, (a > ls ? a : ls) - ls, (z < le ? z : le) - ls);
      }
    }
    free(joined);
    free(starts);
  }

  // The page's two corner marks (v2), shown only with the footer up, each on
  // a white plate so they read over the words.
  void drawPageMarks(ToolsCanvas& c) {
    if (!_chrome) return;
    const int W = c.width(), M = epubui::MARK;
    c.fillRect(0, 0, M + 6, M + 6, false);
    chrome2::peel(c, M);
    c.fillCircle(W - M / 2 - 4, M / 2 + 2, M / 2 + 4, false);
    chrome2::moreDots(c, W - M / 2 - 4, M / 2 + 2);
  }

  // The first-open guide (v2, owner: "clean and easy to understand"): the
  // page faded; two dashed lines split it into its three tap zones, each
  // named once in the middle of its column; the two corners pointed at with
  // their own marks; one line at the foot says how to begin. Any touch puts
  // it away for good.
  void drawGuide(ToolsCanvas& c) {
    const int W = c.width(), H = c.height(), T = epubui::TURN_W, M = epubui::MARK;
    host().fadeBehind();
    // A white plate with a hairline, for every word the guide says.
    auto plate = [&](int cx, int y, const char* a, const char* b, TSize sz) {
      const int aw = c.textWidth(a, sz, true), bw = b ? c.textWidth(b, TS_SMALL) : 0;
      const int pw = (aw > bw ? aw : bw) + 24;
      const int ph = c.textHeight(sz) + (b ? c.textHeight(TS_SMALL) + 6 : 0) + 16;
      c.fillRect(cx - pw / 2, y, pw, ph, false);
      c.drawRect(cx - pw / 2, y, pw, ph, 2, true);
      c.textCentered(cx, y + 8, a, sz, true, true);
      if (b) c.textCentered(cx, y + 8 + c.textHeight(sz) + 4, b, TS_SMALL, true);
    };
    // The three columns.
    const int top = 96, bot = H - 120;
    for (const int x : {T, W - T})
      for (int y = top; y < bot; y += 14) c.fillRect(x - 1, y, 3, 8, true);
    const int mid = (top + bot) / 2 - 30;
    plate(T / 2, mid, "<", "previous", TS_LARGE);
    plate(W - T / 2, mid, ">", "next", TS_LARGE);
    plate(W / 2, mid, "footer", "show / hide", TS_MED);
    // The corners: their marks, and what they do.
    c.fillRect(0, 0, M + 6, M + 6, false);
    chrome2::peel(c, M);
    c.fillCircle(W - M / 2 - 4, M / 2 + 2, M / 2 + 4, false);
    chrome2::moreDots(c, W - M / 2 - 4, M / 2 + 2);
    plate(M + 74, 8, "close book", nullptr, TS_SMALL);
    plate(W - M - 52, 8, "more", nullptr, TS_SMALL);
    // The heading and the way in.
    plate(W / 2, top - 40 + 72, "HOW TO READ", nullptr, TS_MED);
    const int by = H - 96;
    c.fillRect(24, by, W - 48, 64, true);
    c.textCentered(W / 2, by + 8, "tap anywhere to start", TS_MED, false, true);
    c.textCentered(W / 2, by + 38, "the side buttons turn pages too", TS_SMALL, false);
  }

  // While the page follows the device: a plate at the top that locks it.
  void drawLockPlate(ToolsCanvas& c) {
    const TRect r = rmenu::lockPlate(c.width());
    c.fillRect(r.x, r.y, r.w, r.h, true);
    c.textTracked(r.x + (r.w - c.textTrackedWidth("TAP TO LOCK", TS_SMALL, true, 2)) / 2,
                  r.y + (r.h - c.textHeight(TS_SMALL)) / 2, "TAP TO LOCK", TS_SMALL, false, true, 2);
  }

  void renderFooter(ToolsCanvas& c) {
    // The footer: where you are, on a plate the page shows through around.
    const int fy = c.height() - epubui::FOOT_H;
    c.fillRect(0, fy, c.width(), epubui::FOOT_H, false);
    c.fillRect(0, fy, c.width(), 2, true);
    // Where you are is measured FIRST, because it is the part that must not be
    // covered: a title runs to whatever length a publisher felt like, and the
    // one on this card ran straight through the page number.
    char pos[24];
    if (_chapterPages > 0)
      snprintf(pos, sizeof(pos), "p %d/%d", _page + 1, _chapterPages);
    else
      snprintf(pos, sizeof(pos), "p %d", _page + 1);
    const int pw = c.textWidth(pos, TS_MED);
    // The chapter, by name. It used to be "ch 14/21" -- a SPINE index and a
    // spine count, so a book open at Chapter 7 read "ch 14/21" while the panel
    // beside it said "Chapter 7: Friends". The book's own title takes the space
    // only when the chapter has no name to give, which is a book with neither
    // contents nor a heading on its first page.
    const char* left = _chapName[0] ? _chapName : _books[_cur].title;
    c.textClipped(16, fy + 14, c.width() - 32 - pw - 12, left, TS_MED, true, true);
    c.text(c.width() - 16 - pw, fy + 14, pos, TS_MED, true);
  }

  Screen _screen = Screen::List;
  char _dir[128] = "/books";
  // Series rows at this level (series.h) and which books they hold.
  struct Group {
    char name[series::NAME];
    uint16_t count;
  };
  Group _groups[shelf::MAX_FOLDERS];
  int _ng = 0;
  int _inGroup = -1;    // the series row open, or -1
  int _groupPage = 0;   // the page that row was on, to return to
  int8_t _bookGroup[epubui::MAX_BOOKS] = {};
  int32_t _volume[epubui::MAX_BOOKS] = {};
  int16_t _view[epubui::MAX_BOOKS] = {};
  int _nv = 0;
  ToolsHost::ShelfFolder _folders[shelf::MAX_FOLDERS];
  int _nf = 0;
  int _lpage = 0;
  ToolsHost::EpubInfo _books[epubui::MAX_BOOKS];
  int _n = -1;
  int _cur = -1;
  bool _open = false;
  uint32_t _saveDue = 0;  // nowMs() the held-back place is written at; 0 = none
  bool _chrome = false;
  const char* _note = nullptr;
  bool _help = false;       // the HOW TO READ card, once per device
  char _noteBuf[96] = {};
  char _pageImage[192] = {};       // this page IS this picture, if set
  uint8_t _pageImagePart = 0;      // which part of it (landscape art is several)
  char _pendImage[192] = {};       // ...and this one opens the next page
  uint8_t _pendImagePart = 0;
  uint32_t _pendImageOff = 0;
  bool _pendImageValid = false;
  // The Readest library screen.
  kosync::LibBook _lib[kosync::LIB_PER_PAGE];
  bool _libLocal[kosync::LIB_PER_PAGE] = {};
  int _libN = 0, _libPage = 1, _libDlIdx = 0;
  bool _libMore = false, _libWifi = false, _libDl = false, _libShowQr = false;
  uint8_t _libStage = 0;
  uint32_t _libPainted = 0;
  char _libQuery[libweb::QUERY_MAX] = {};
  char _libMsg[64] = {};
  kosync::Download _dl;
  LibSink _libSink;
  libweb::Server _web;
  uint8_t _syncStage = 0;  // 0 none, 1 working (screen up), 2 answered,
                           // 3 asking Readest for a code, 4 showing it / polling
  kosync::DeviceCode _code;
  uint32_t _codeT0 = 0, _lastPoll = 0;
  uint8_t _syncOut = 0;
  double _syncPct = 0;
  char _syncMsg[64] = {};
  kosync::Remote _remote;
  bool _streamLost = false;        // a picture was read out of the same zip
  // The open chapter's name, and which spine item it belongs to. Filled when a
  // chapter opens (from the contents) or when its first page lands (from the
  // heading), and spent by the footer, the options panel and the shelf note.
  char _chapName[41] = {};
  int _chapNameSpine = -1;
  rmenu::Page _menu = rmenu::Page::None;
  // Picking a phrase to keep: -1 nowhere, then the first word, then the last.
  // Line and word indices into _lines, which is what the page IS -- so the
  // pick is re-measured from the same strings the page was drawn from and
  // needs no table of word boxes anywhere.
  int _pickL0 = -1, _pickW0 = 0, _pickL1 = -1, _pickW1 = 0;
  bool _picking = false;
  char _phrase[marks::LABEL] = {};
  // Where each run of bytes on the page's lines starts in the book: line,
  // byte in the line, codepoint offset. A run never holds a space of the
  // book's own, so the codepoints from an anchor on are the book's too.
  struct Anchor {
    uint8_t line, pos;
    uint32_t off;
  };
  static constexpr int MAX_ANC = 400;
  Anchor _anc[MAX_ANC] = {};
  int _ancN = 0;
  int _ulCount = 0;  // underline strokes in the last paint (for the harness)
  // Where the last paint drew each underline, and whose it was: a tap on one
  // opens its card (owner, 26 Nov).
  struct UlRect {
    short x, y, w, h;
    uint8_t mark;
  };
  static constexpr int MAX_UL = 48;
  UlRect _ul[MAX_UL] = {};
  int _ulN = 0;
  int _hlCard = -1;       // the mark the card is about
  bool _hlArmed = false;  // its Remove asked once (one Readest knows)
  int _listArmed = -1;    // the Highlights row whose x asked once
  bool _moreSet = false;  // the More card shows its Settings side
  uint8_t _textTab = 1;   // the Text bar's tab: font, size, line, margins, paragraphs, turns
  int _fontChips[4] = {}; // face indexes behind the bar's font chips, and their x spans
  short _fontChipX[4] = {}, _fontChipW[4] = {};
  int _fontChipN = 0;
  bool _spArmed = false;  // Reading sync: Sign out asked once
  const char* _spNote = nullptr;
  int _hlSent = 0, _hlDead = 0;  // part B: what the last sync sent
  kosync::Result _hlPushErr = kosync::Result::Ok;
  int _mpage = 0;                  // which page of the contents or the marks
  int _ntoc = -1;                  // -1 until the book's contents are read
  marks::Mark _marks[marks::MAX];
  int _nmarks = 0;
  // D: the highlights half of the last sync, and an automatic sync running.
  bool _hlTried = false;
  rhl::Merged _hlRes;
  kosync::Result _hlErr = kosync::Result::Ok;
  enum : uint8_t { AUTO_NONE, AUTO_OPEN, AUTO_CLOSE, AUTO_SLEEP };
  uint8_t _auto = AUTO_NONE;
  char _rootSub[5][48] = {};
  uint8_t _size = 0, _lead = 1;
  uint8_t _margin = 1, _para = 1;  // v2: side margins, paragraph gap
  char _szLabel[16] = "";  // "12 pt", when the size came off the card
  uint8_t _face = 0;  // which built-in cut sits under whatever is chosen
  // The card's families, read once on the way in (see enter): inside a book
  // the card's bus belongs to the book.
  static constexpr int MAX_FAMS = 16;
  char _famNames[MAX_FAMS][ToolsHost::FONT_FAMILY_LEN] = {};
  int8_t _famN = -1;
  bool _browsing = false;    // this app owns the card's shelf session
  bool _resetArmed = false;  // "Start again" asked once and waiting on a second tap
  uint8_t _rot = 0;      // the page view's rotation; every menu screen is portrait
  bool _rotFollow = false;  // v2: the page follows the device's angle until locked
  uint32_t _lastLook = 0;   // when the angle was last looked at
  bool _guide = false;      // the first-open guide to the tap zones is up
  bool _fromMore = false;   // the open menu page was reached from the More popup
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
