// Shared UI seam for the utility apps (coin / dice / timer / random / picker).
//
// The tool apps below are compiled UNCHANGED into two different firmwares:
//   * the standalone Toybox firmware (draws through Epd + gfx)
//   * the CrossPoint Reader port    (draws through GfxRenderer)
// Everything device-specific lives behind ToolsCanvas / ToolsHost, so a tool
// app never includes a display header. Both hosts are ESP32-Arduino, so
// Preferences, millis() and esp_random() are used directly.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "unicode.h"

// Text size buckets. Each host maps these onto whatever fonts it has.
enum TSize : uint8_t { TS_SMALL = 0, TS_MED = 1, TS_LARGE = 2, TS_HUGE = 3 };

// The smallest size a string is still readable at, given its script.
//
// Thai floors one step above whatever it was asked for. This is not about the
// line box -- it is about what fills the box. A Thai line has to leave room for
// two storeys of marks above the letters and one below, so the base letters
// occupy maybe half the height that Latin does in the same box, and Thai set at
// the same nominal size reads visibly smaller beside it. Standing it a step
// taller is what makes the two look the same size.
//
// Han and hangul fill their box the way Latin does, so they only need the box
// not to be tiny -- which, since the scale moved up, TS_SMALL no longer is.
inline TSize scriptFloor(const char* s, TSize sz) {
  if (sz >= TS_LARGE || !s) return sz;
  for (const char* p = s; *p;) {
    if (uni::thai(uni::next(p))) return sz < TS_MED ? TS_MED : TS_LARGE;
  }
  return sz;
}

class ToolsCanvas {
 public:
  virtual ~ToolsCanvas() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;

  virtual void clear() = 0;
  virtual void fillRect(int x, int y, int w, int h, bool black) = 0;
  virtual void drawRect(int x, int y, int w, int h, int thickness, bool black) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, int thickness, bool black) = 0;
  virtual void fillCircle(int cx, int cy, int r, bool black) = 0;
  virtual void drawCircle(int cx, int cy, int r, int thickness, bool black) = 0;

  virtual void text(int x, int y, const char* s, TSize sz, bool black, bool bold = false) = 0;
  virtual int textWidth(const char* s, TSize sz, bool bold = false) const = 0;
  virtual int textHeight(TSize sz) const = 0;

  // Blank space the glyphs leave inside textWidth/textHeight. A bitmap font
  // carries its advance as padding inside the cell -- usually all of it on one
  // side -- so centring on the reported box leaves the ink visibly off centre
  // at large sizes. A host that does not care can leave this at zero.
  virtual void textPad(const char* s, TSize sz, int& left, int& right, int& top,
                       int& bottom) const {
    (void)s;
    (void)sz;
    left = right = top = bottom = 0;
  }

  // --- convenience built on the primitives above ---------------------------
  void textCentered(int cx, int y, const char* s, TSize sz, bool black, bool bold = false) {
    int l, r, t, b;
    textPad(s, sz, l, r, t, b);
    text(cx - textWidth(s, sz, bold) / 2 + (r - l) / 2, y, s, sz, black, bold);
  }
  void textInBox(int x, int y, int w, int h, const char* s, TSize sz, bool black,
                 bool bold = false) {
    int l, r, t, b;
    textPad(s, sz, l, r, t, b);
    textCentered(x + w / 2, y + (h - textHeight(sz)) / 2 + (b - t) / 2, s, sz, black, bold);
  }
  // Filled = black background + white label (primary action).
  // A button is a hairline and a word. The border used to be two pixels, and
  // two pixels on a 235 DPI panel is a heavy black frame around everything --
  // eight of them on a screen and the screen is a grid of boxes rather than a
  // page with things on it. The hub never drew a box in its life and reads as
  // the calmest screen in the firmware; this brings the rest closer to it.
  //
  // `filled` stays for the one action on a screen that is THE action, and it
  // is worth using rarely: on e-paper a filled slab is the loudest mark
  // available and it flashes on every partial refresh.
  void button(int x, int y, int w, int h, const char* label, bool filled,
              TSize sz = TS_MED) {
    if (filled) {
      fillRect(x, y, w, h, true);
      textInBox(x, y, w, h, label, sz, false, true);
    } else {
      fillRect(x, y, w, h, false);
      drawRect(x, y, w, h, 1, true);
      textInBox(x, y, w, h, label, sz, true, false);
    }
  }
  // A row in a list of choices: the label, left-aligned, and a hairline under
  // it. No box. This is the hub's language -- dividers between things rather
  // than frames around them -- and it is what a stack of seven of these should
  // look like on a page that is mostly white.
  //
  // `armed` is for a row that is one tap from doing something irreversible: it
  // gets a hairline all the way round instead of a fill, because a black slab
  // on e-paper is a shout and this only needs to be a raised eyebrow.
  void listRow(int x, int y, int w, int h, const char* label, bool rule = true,
               bool armed = false, TSize sz = TS_MED) {
    if (armed) drawRect(x, y, w, h, 1, true);
    textClipped(x + 12, y + (h - textHeight(sz)) / 2, w - 24, label, sz, true, armed);
    if (rule && !armed) fillRect(x, y + h - 1, w, 1, true);
  }

  // Draws as much of `s` as fits in maxW, ending in "..." when it had to stop.
  //
  // Steps by codepoint, so a clipped Thai or CJK title never ends halfway
  // through a character -- and the ellipsis is three dots rather than U+2026,
  // because the generated faces do not all carry that glyph and a missing one
  // would be worse than no ellipsis at all.
  void textClipped(int x, int y, int maxW, const char* s, TSize sz, bool black,
                   bool bold = false) {
    if (maxW <= 0 || !s || !*s) return;
    if (textWidth(s, sz, bold) <= maxW) {
      text(x, y, s, sz, black, bold);
      return;
    }
    const int dots = textWidth("...", sz, bold);
    char buf[192];
    int n = 0;
    for (const char* p = s; *p;) {
      const char* q = p;
      uni::next(q);
      const int add = (int)(q - p);
      if (n + add > (int)sizeof(buf) - 4) break;
      memcpy(buf + n, p, (size_t)add);
      buf[n + add] = 0;
      if (textWidth(buf, sz, bold) + dots > maxW) break;
      n += add;
      p = q;
    }
    buf[n] = 0;
    memcpy(buf + n, "...", 4);
    text(x, y, buf, sz, black, bold);
  }

  // One glyph, for grids that place characters themselves.
  void textChar(int x, int y, char ch, TSize sz, bool black, bool bold = false) {
    const char s[2] = {ch, 0};
    text(x, y, s, sz, black, bold);
  }

  // Letter-spaced text, for the few titles that want air between the glyphs.
  // Built on text() one character at a time rather than on a spacing argument,
  // so a host whose text engine has no notion of tracking still gets it right.
  // Steps by codepoint, and a combining mark travels with its base -- a Thai
  // deck name in the top bar must not have its tone marks spaced away.
  int textTrackedWidth(const char* s, TSize sz, bool bold, int spacing) const {
    int w = 0;
    char one[16];
    for (const char* p = s; *p;) {
      w += textWidth(cluster(p, one), sz, bold) + spacing;
    }
    return w > 0 ? w - spacing : 0;
  }
  void textTracked(int x, int y, const char* s, TSize sz, bool black, bool bold, int spacing) {
    char one[16];
    for (const char* p = s; *p;) {
      const char* c = cluster(p, one);
      text(x, y, c, sz, black, bold);
      x += textWidth(c, sz, bold) + spacing;
    }
  }
  void textTrackedCentered(int cx, int y, const char* s, TSize sz, bool black, bool bold,
                           int spacing) {
    int l, r, t, b;
    textPad(s, sz, l, r, t, b);
    textTracked(cx - textTrackedWidth(s, sz, bold, spacing) / 2 + (r - l) / 2, y, s, sz, black,
                bold, spacing);
  }

  // Small square stepper button ("-" / "+").
  void stepper(int x, int y, int size, const char* label, bool enabled) {
    drawRect(x, y, size, size, enabled ? 2 : 1, true);
    textInBox(x, y, size, size, label, TS_LARGE, true, enabled);
  }

 private:
  // Copies the next base character plus any combining marks riding it into
  // `out`, advances p, and returns out. 16 bytes holds a base and three marks.
  static const char* cluster(const char*& p, char out[16]) {
    int n = 0;
    const char* start = p;
    uni::next(p);
    while (*p) {
      const char* q = p;
      if (!uni::thaiMark(uni::next(q))) break;
      p = q;
    }
    while (start < p && n < 15) out[n++] = *start++;
    out[n] = 0;
    return out;
  }
};

// The two buttons down the side of the device, named for what is written on
// them rather than for what any one app does with them.
// Ok is the power button's short press. It reaches an app only as an offer,
// same as the other two: an app that ignores it leaves it meaning nothing.
enum class SideBtn : uint8_t { Up, Down, Ok };

// Everything a tool app needs from the firmware it is embedded in.
class ToolsHost {
 public:
  virtual ~ToolsHost() = default;
  virtual ToolsCanvas& canvas() = 0;
  virtual Preferences& prefs() = 0;
  // Re-render the active tool and push it to the panel. full=true forces an
  // absolute refresh (slower, clears ghosting) — use on screen changes.
  virtual void refresh(bool full = false) = 0;
  // A page turn's refresh: partial, so it costs 0.3 s instead of 1.7 s, with
  // the host promoting itself to a full clean every so often because ghosting
  // accumulates. Use this for anything cosmetic and frequent -- turning a
  // page, paging a list, moving through the panel. Use refresh(true) when the
  // panel MUST be clean: after the SD bus was released (the controller was
  // re-initialised), after a grey page, and on the way out of a book.
  //
  // `cleanEvery` is how many of these the caller wants between full refreshes;
  // 1 means every one of them, which is a full refresh and the honest way to
  // say "best quality" without a second entry point.
  //
  // The default is a plain partial, which is what a host with no ghosting
  // policy of its own should do.
  virtual void refreshFast(int cleanEvery) {
    if (cleanEvery <= 1) {
      refresh(true);
      return;
    }
    refresh(false);
  }
  virtual void beep(uint8_t kind) = 0;  // 0 tap, 1 confirm, 2 reject, 3 alarm
  virtual void goHub() = 0;
  // Opens the notes tool straight into its pairing screen, where the phone's
  // page carries the picture uploader. The settings screen uses this for the
  // lock screen picture rather than growing a second web server: the browser
  // has to do the cropping and dithering either way, and one pairing screen is
  // easier to keep honest than two. A host with no notes tool leaves this be.
  virtual void goPairPicture() {}
  // Turn the whole canvas a quarter at a time, 0..3, the way the sleeping note
  // turns when the device is. An app that asks for this gets a canvas whose
  // width and height have swapped, and taps arrive in the same rotated space,
  // so a screen laid out against width()/height() keeps working. A host with a
  // fixed screen ignores it and reports 0.
  virtual void setCanvasRotation(int r) { (void)r; }
  virtual int canvasRotation() const { return 0; }
  // Which way up the device is being HELD, from the accelerometer, in the same
  // 0..3 space as canvasRotation. -1 means there is no way to know -- no IMU
  // on the board, or a host with no board at all -- and callers fall back to
  // asking with a button.
  virtual int deviceOrientation() { return -1; }
  virtual void topBar(const char* title, bool withHelp = false, const char* backLabel = "HUB") = 0;
  // A tool with no rules card leaves this false and no "?" is drawn.
  virtual bool isHelpTap(int x, int y) const { return false; }
  virtual bool isBackTap(int x, int y) const = 0;
  // Is there anything above Toybox to go back to? The standalone firmware is
  // the whole device and answers no, so its hub is the root. Inside the reader
  // the hub is one activity deep, and the way out belongs in the same corner
  // every app already puts its back button.
  virtual bool canExit() const { return false; }
  virtual void exit() {}

  // Battery, as a percentage, or -1 when the host has no gauge (or none
  // answered on the bus). The hub is the only screen that shows it; every
  // other screen would rather spend the pixels on its own content.
  virtual int batteryPercent() const { return -1; }
  virtual bool charging() const { return false; }

  // Whether the device beeps at all. It belongs to the host: the reader has its
  // own idea about sound, and the standalone firmware keeps it in its own NVS.
  virtual bool soundOn() const { return true; }
  virtual void setSoundOn(bool on) { (void)on; }
  // How loud, 0 = silent up to 3 = as loud as the hardware goes. A host that
  // only has a switch answers through the switch and loses nothing: the
  // settings screen offers whatever range it is given.
  virtual int soundLevel() const { return soundOn() ? 3 : 0; }
  virtual void setSoundLevel(int lv) { setSoundOn(lv > 0); }
  virtual int soundLevels() const { return 2; }

  // The wall clock, for the hub's corner. False when there is no RTC or it has
  // never been set -- the hub then simply draws no time, which is honest.
  virtual bool clockHHMM(int& hour, int& minute) const {
    (void)hour;
    (void)minute;
    return false;
  }

  // Wallpapers on the SD card, for the settings page. Fills names (bare file
  // names, NUL-terminated, truncated to fit) and returns how many were found;
  // -1 means no card answered. A host with no card slot keeps the default and
  // the settings row explains itself.
  static constexpr int SD_NAME_LEN = 40;
  virtual int sdWallpapers(char names[][SD_NAME_LEN], int max) {
    (void)names;
    (void)max;
    return -1;
  }
  // Copy the named wallpaper from the card into the device, so it survives the
  // card being removed. True when the file arrived whole and valid.
  virtual bool sdWallpaperTake(const char* name) {
    (void)name;
    return false;
  }
  // The same copy, into the lock screen's picture instead. One list feeds both
  // -- a .tbi is a .tbi -- so only the destination differs.
  virtual bool sdLockTake(const char* name) {
    (void)name;
    return false;
  }

  // Books: pre-converted .tbk volumes on the SD card (tools/make_tbk.py).
  // A page is 48,000 bytes in the framebuffer's own layout, so the reader
  // copies rather than decodes. While a book is open the card stays powered
  // and mounted -- that is the whole experiment this API exists to run --
  // and bookClose() must be called before anything else wants the panel,
  // because it re-initialises the controller on the way out.
  struct BookInfo {
    // Absolute, and 128 for the same reason as EpubInfo: a book inside a
    // series folder carries the folder in its path, and two series can both
    // hold a "vol01.tbk".
    char file[128];
    char title[41];
    uint32_t pages = 0;
    bool rtl = false;
    uint8_t bpp = 1;  // 1 = B/W, 2 = four-level grey
    // The file carries a cover made on a PC. Page 0 is only ever the cover by
    // accident -- a trimmed scan starts at the story -- so when a book says it
    // has a real one, that is what the strip and the loading screen show.
    bool cover = false;
  };
  // A series folder under /books, and how many books of ONE kind it holds --
  // the .tbk reader and the EPUB reader each see only their own, so neither
  // offers a door onto an empty room.
  struct ShelfFolder {
    char name[64];
    uint16_t count = 0;
  };
  // `ext` is ".tbk" or ".epub", lowercase with the dot.
  virtual int shelfFolders(ShelfFolder* out, int max, const char* ext) {
    (void)out;
    (void)max;
    (void)ext;
    return 0;  // a host with no folders simply has a flat shelf
  }
  // What the allocator has, so a message about memory can say how much was
  // wanted and how much there was. A number in a failure is the difference
  // between a report somebody can act on and one more round trip. Zero from a
  // host that cannot tell.
  virtual uint32_t heapFree() const { return 0; }
  virtual uint32_t heapLargest() const { return 0; }

  static constexpr uint32_t BOOK_PAGE_BYTES = 48000;
  // `dir` is "/books" for the top level (which also picks up books loose in
  // the card's root), or "/books/<series>" inside a folder.
  virtual int bookList(BookInfo* out, int max, const char* dir) {
    (void)out;
    (void)max;
    (void)dir;
    return -1;  // no card, or no host support
  }
  virtual bool bookOpen(const char* file) {
    (void)file;
    return false;
  }
  virtual bool bookPage(uint32_t idx, uint8_t* dst) {
    (void)idx;
    (void)dst;
    return false;
  }
  // The embedded cover: 48,000 bytes, 480x800 one bit, the framebuffer's own
  // convention. Only valid while the book is open, and only when BookInfo::
  // cover said there is one.
  virtual bool bookCover(uint8_t* dst) {
    (void)dst;
    return false;
  }
  virtual void bookClose() {}
  // Show a 2-bit page in true four-level grey, bypassing the canvas: grey is
  // a hardware waveform, not a drawing primitive. False means the host cannot
  // (guest hosts, the preview harness), and the reader dithers the page down
  // to 1-bit instead -- readable everywhere, grey where the glass allows it.
  static constexpr uint32_t BOOK_PAGE_BYTES_GREY = 96000;
  virtual bool bookShowGrey(const uint8_t* packed2bpp) {
    (void)packed2bpp;
    return false;
  }
  // The same, straight off the card: the host streams the page through the
  // waveform a band at a time and the reader never holds one. A grey page is
  // 96,000 bytes, and 96 KB of contiguous heap is not something this device
  // reliably has once the UI has been running -- which is why every grey book
  // refused to open before this existed.
  virtual bool bookShowGreyPaged(uint32_t idx) {
    (void)idx;
    return false;
  }
  // A slice of a page, for drawing a grey one through the canvas when the
  // waveform cannot be used (the footer is up, or the host has no grey).
  virtual bool bookPageSlice(uint32_t idx, uint32_t off, uint8_t* dst, uint32_t n) {
    (void)idx;
    (void)off;
    (void)dst;
    (void)n;
    return false;
  }

  // EPUBs. The same session shape as .tbk books -- an open EPUB holds the SD
  // bus until epubClose(), which re-initialises the panel -- but the host
  // only moves bytes; the parsing lives in epubc (tools/epub/epubcore.h).
  // file is the ABSOLUTE card path, because CrossPoint hashes that exact
  // string to find its progress directory.
  struct EpubInfo {
    char file[128];  // absolute card path; real release filenames run long
    char title[41];
    bool cont = false;  // a reading position already exists on the card
  };
  virtual int epubList(EpubInfo* out, int max, const char* dir) {
    (void)out;
    (void)max;
    (void)dir;
    return -1;
  }
  virtual bool epubOpen(const char* path) {
    (void)path;
    return false;
  }
  virtual int epubRead(uint32_t pos, void* dst, uint32_t n) {
    (void)pos;
    (void)dst;
    (void)n;
    return -1;
  }
  virtual uint32_t epubSize() { return 0; }
  virtual void epubClose() {}
  // Sidecar files beside the book (CrossPoint's progress.bin). Only valid
  // while an EPUB session holds the bus.
  virtual int sdReadFile(const char* path, void* dst, int max) {
    (void)path;
    (void)dst;
    (void)max;
    return -1;
  }
  virtual bool sdWriteFileAtomic(const char* path, const void* data, int n) {
    (void)path;
    (void)data;
    (void)n;
    return false;
  }

  // Managing the card from a phone. A session claims the SD bus and holds it
  // across a burst of activity, so nothing may repaint the panel between
  // sdMgrOpen() and sdMgrClose() -- and the paint after the close must be a
  // full one, because closing re-initialises the controller.
  struct SdFile {
    char path[128];
    uint32_t size = 0;
  };
  virtual bool sdMgrOpen() { return false; }
  virtual void sdMgrClose() {}
  virtual int sdMgrList(SdFile* out, int max) {
    (void)out;
    (void)max;
    return -1;
  }
  virtual bool sdMgrDelete(const char* path) {
    (void)path;
    return false;
  }
  virtual bool sdMgrRename(const char* path, const char* bareName) {
    (void)path;
    (void)bareName;
    return false;
  }
  virtual bool sdMgrWriteOpen(const char* dir, const char* bareName) {
    (void)dir;
    (void)bareName;
    return false;
  }
  virtual bool sdMgrWriteChunk(const uint8_t* data, uint32_t n) {
    (void)data;
    (void)n;
    return false;
  }
  virtual bool sdMgrWriteClose(bool keep) {
    (void)keep;
    return false;
  }
  virtual uint32_t sdMgrFreeMb() { return 0; }

  // Files on the card addressed by path, for things the device makes rather
  // than the owner: cover art. Writing streams, because a panel-sized cover
  // is 48 KB and the heap it would have to be assembled in is already
  // carrying a zip window and an image decoder.
  virtual bool sdStreamOpen(const char* path) {
    (void)path;
    return false;
  }
  virtual bool sdStreamWrite(const uint8_t* data, uint32_t n) {
    (void)data;
    (void)n;
    return false;
  }
  virtual bool sdStreamClose(bool keep) {
    (void)keep;
    return false;
  }
  // Reads a whole file, borrowing the bus if nothing else holds it -- in
  // which case the panel is re-initialised and the next paint must be full.
  // Part of a file, from an offset. Same bus discipline as sdReadWhole -- it
  // borrows the bus when nobody holds it -- but it lets a caller work through
  // something large without a buffer the size of the whole thing.
  virtual int sdReadSlice(const char* path, uint32_t off, void* dst, int n) {
    (void)path;
    (void)off;
    (void)dst;
    (void)n;
    return -1;
  }
  virtual int sdReadWhole(const char* path, void* dst, int max) {
    (void)path;
    (void)dst;
    (void)max;
    return -1;
  }

  // Y coordinate where a tool's own content may start (below the top bar).
  virtual int contentTop() const = 0;
};

class ToolApp {
 public:
  virtual ~ToolApp() = default;
  virtual const char* title() const = 0;
  // Called every time the tool is opened from the hub.
  virtual void enter(ToolsHost& h) { _host = &h; }
  virtual void render(ToolsCanvas& c) = 0;
  virtual void onTap(int x, int y) = 0;
  virtual void onSwipe(int dx, int dy) { (void)dx; (void)dy; }
  virtual void tick() {}                       // ~50 Hz, for running timers
  virtual bool wantsTick() const { return false; }
  // The two side buttons, for an app that has something for them to do. Only
  // flashcards does: a hand holding the device can grade a card without the
  // other hand coming up to the panel. Everything else leaves these alone, and
  // an app that does not answer true lets the button mean nothing rather than
  // something it did not intend.
  virtual bool onButton(SideBtn b) {
    (void)b;
    return false;
  }
  // Open somewhere other than the front page. Only the notes tool answers, and
  // only for pairing; everything else opens where it always does.
  virtual bool openPairing() { return false; }
  // Open straight into a named item -- the readers use this for the hub's
  // recently-read covers. Called after enter(); false means the item was not
  // found (card gone, file renamed) and the app stays on its list.
  virtual bool openDirect(const char* file) {
    (void)file;
    return false;
  }

 protected:
  ToolsHost* _host = nullptr;
  ToolsHost& host() { return *_host; }
  ToolsCanvas& canvas() { return _host->canvas(); }
  Preferences& prefs() { return _host->prefs(); }
};

// Rect hit-test shared by every tool.
inline bool tHit(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

struct TRect {
  int x, y, w, h;
  bool hit(int px, int py) const { return tHit(px, py, x, y, w, h); }
};
