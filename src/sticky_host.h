// The standalone firmware's half of the seam: it draws with Epd + gfx, keeps
// its settings in its own NVS namespace, and beeps with the on-board buzzer.
// Everything above this file is shared with the CrossPoint port.
#pragma once
#include <Preferences.h>

#include "board_pins.h"
#include "epd.h"

#include "buzzer.h"
#include "chrome.h"
#include "gfx.h"
#include "sdcard.h"
#include "sensors.h"
#include "touch.h"
#include "toybox.h"

class StickyCanvas : public ToolsCanvas {
 public:
  // Asked of the panel rather than fixed, so the pinned note reflows into the
  // full landscape when the device is turned on its side. Every app screen
  // runs at rotation 0, where these are the familiar 480x800.
  int width() const override { return epd.logicalW(); }
  int height() const override { return epd.logicalH(); }
  void clear() override { epd.clear(); }
  void fillRect(int x, int y, int w, int h, bool black) override {
    epd.fillRect(x, y, w, h, black ? 0 : 1);
  }
  void drawRect(int x, int y, int w, int h, int t, bool black) override {
    epd.drawRect(x, y, w, h, black ? 0 : 1, t);
  }
  void drawLine(int x0, int y0, int x1, int y1, int t, bool black) override {
    epd.drawLine(x0, y0, x1, y1, black ? 0 : 1, t);
  }
  void fillCircle(int cx, int cy, int r, bool black) override {
    epd.fillCircle(cx, cy, r, black ? 0 : 1);
  }
  void drawCircle(int cx, int cy, int r, int t, bool black) override {
    epd.drawCircle(cx, cy, r, black ? 0 : 1, t);
  }
  void text(int x, int y, const char* s, TSize sz, bool black, bool bold) override {
    gfx::drawText(x, y, s, scaleOf(sz), black ? 0 : 1, bold);
  }
  int textWidth(const char* s, TSize sz, bool bold) const override {
    return gfx::textWidth(s, scaleOf(sz), bold);
  }
  int textHeight(TSize sz) const override { return gfx::textHeight(scaleOf(sz)); }
  void textPad(const char* s, TSize sz, int& l, int& r, int& t, int& b) const override {
    gfx::textInk(s, scaleOf(sz), false, 0, l, r, t, b);
  }

 private:
  // The four buckets are pixel heights now, not multipliers.
  static int scaleOf(TSize sz) {
    switch (sz) {
      // 235 DPI, so these are millimetres as much as pixels: 1.7, 2.6, 3.4,
      // 4.8. The first set was 12/16/24/32, chosen by eye on a monitor, and on
      // the panel that put body text at 1.7 mm -- about five point. Everything
      // moved up one step the first time somebody actually held the device.
      case TS_SMALL: return 16;
      case TS_LARGE: return 32;
      case TS_HUGE: return 44;
      default: return 24;
    }
  }
};

class StickyHost : public ToolsHost {
 public:
  ToolsCanvas& canvas() override { return _canvas; }
  Preferences& prefs() override;
  void refresh(bool full) override {
    epd.clear();
    toybox.render(_canvas);
    if (full)
      epd.displayFull();
    else
      epd.displayPartial();
  }
  // Every eighth fast refresh is a full one. Eight is what commercial readers
  // settle on: one 1.7-second pause per eight turns, and the page never gets
  // visibly dirty. The driver has its own backstop at forty partials, which is
  // ghost control for the whole firmware rather than a reading cadence.
  static constexpr int CLEAN_EVERY = 8;
  void refreshFast() override {
    if (++_fastCount >= CLEAN_EVERY) {
      _fastCount = 0;
      refresh(true);
      return;
    }
    refresh(false);
  }
  void resetFastCount() { _fastCount = 0; }
  void beep(uint8_t kind) override;
  void goHub() override { toybox.goHub(); }
  void goPairPicture() override { toybox.openPairPicture(); }
  void setCanvasRotation(int r) override {
    epd.setRotation(r);
    touch.setRotation(r);
  }
  int canvasRotation() const override { return epd.rotation(); }
  void topBar(const char* t, bool withHelp, const char* backLabel) override {
    drawTopBar(_canvas, t, withHelp, backLabel);
  }
  bool isHelpTap(int x, int y) const override { return tappedHelp(x, y, EPD_W); }
  bool isBackTap(int x, int y) const override { return tappedBack(x, y); }
  int contentTop() const override { return TOPBAR_H + 4; }
  int batteryPercent() const override { return sensors::batteryPercent(); }
  bool charging() const override { return sensors::charging(); }
  bool soundOn() const override { return buzzer::enabled(); }
  void setSoundOn(bool on) override;
  int soundLevel() const override { return (int)buzzer::level(); }
  void setSoundLevel(int lv) override;
  int soundLevels() const override { return buzzer::LEVEL_COUNT; }

#ifndef TOYBOX_HOST
  uint32_t heapFree() const override { return ESP.getFreeHeap(); }
  uint32_t heapLargest() const override { return ESP.getMaxAllocHeap(); }
#endif

  bool clockHHMM(int& hour, int& minute) const override {
    sensors::Clock ck;
    if (!sensors::readClock(ck)) return false;
    hour = ck.hour;
    minute = ck.minute;
    return true;
  }

  // All of these go to the card. Declared out of line because sdcard.h and
  // this header meet awkwardly in the harness build.
  int sdWallpapers(char names[][SD_NAME_LEN], int max) override;
  bool sdWallpaperTake(const char* name) override;
  int shelfFolders(ShelfFolder* out, int max, const char* ext) override;
  int bookList(BookInfo* out, int max, const char* dir) override;
  bool bookOpen(const char* file) override;
  bool bookPage(uint32_t idx, uint8_t* dst) override;
  bool bookCover(uint8_t* dst) override { return sdcard::bookReadCover(dst); }
  void bookClose() override;
  bool bookShowGrey(const uint8_t* packed2bpp) override { return epd.displayGrey2bpp(packed2bpp); }
  bool bookShowGreyPaged(uint32_t idx) override;
  bool bookPageSlice(uint32_t idx, uint32_t off, uint8_t* dst, uint32_t n) override {
    return sdcard::bookReadPageSlice(idx, off, dst, n);
  }
  int epubList(EpubInfo* out, int max, const char* dir) override;
  bool epubOpen(const char* path) override;
  int epubRead(uint32_t pos, void* dst, uint32_t n) override;
  uint32_t epubSize() override;
  void epubClose() override;
  int sdReadFile(const char* path, void* dst, int max) override;
  bool sdWriteFileAtomic(const char* path, const void* data, int n) override;
  bool sdMgrOpen() override { return sdcard::mgrOpen(); }
  void sdMgrClose() override { sdcard::mgrClose(); }
  int sdMgrList(SdFile* out, int max) override;
  bool sdMgrDelete(const char* path) override { return sdcard::mgrDelete(path); }
  bool sdMgrRename(const char* path, const char* bare) override {
    return sdcard::mgrRename(path, bare);
  }
  bool sdMgrWriteOpen(const char* dir, const char* bare) override {
    return sdcard::mgrWriteOpen(dir, bare);
  }
  bool sdMgrWriteChunk(const uint8_t* d, uint32_t n) override {
    return sdcard::mgrWriteChunk(d, n);
  }
  bool sdMgrWriteClose(bool keep) override { return sdcard::mgrWriteClose(keep); }
  uint32_t sdMgrFreeMb() override { return sdcard::mgrFreeMb(); }
  bool sdStreamOpen(const char* path) override { return sdcard::streamOpen(path); }
  bool sdStreamWrite(const uint8_t* d, uint32_t n) override { return sdcard::streamWrite(d, n); }
  bool sdStreamClose(bool keep) override { return sdcard::streamClose(keep); }
  int sdReadSlice(const char* path, uint32_t off, void* dst, int n) override {
    return sdcard::readSlice(path, off, dst, n);
  }
  int sdReadWhole(const char* path, void* dst, int max) override {
    return sdcard::readWhole(path, dst, max);
  }

  ToolsCanvas& sharedCanvas() { return _canvas; }
  int _fastCount = 0;

#ifdef TOYBOX_HOST
  // The standalone firmware is the whole device, so it never offers a way out.
  // The preview flips this to render the hub as it will look inside the reader.
  bool canExit() const override { return _canExit; }
  void exit() override { _exited = true; }
  void hostSetCanExit(bool on) { _canExit = on; }
  bool hostExited() const { return _exited; }
  void hostClearExited() { _exited = false; }
#endif

 private:
  StickyCanvas _canvas;
#ifdef TOYBOX_HOST
  bool _canExit = false, _exited = false;
#endif
};

extern StickyHost stickyHost;
