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
#include "sensors.h"
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
  void beep(uint8_t kind) override;
  void goHub() override { toybox.goHub(); }
  void goPairPicture() override { toybox.openPairPicture(); }
  void topBar(const char* t, bool withHelp) override { drawTopBar(_canvas, t, withHelp); }
  bool isHelpTap(int x, int y) const override { return tappedHelp(x, y, EPD_W); }
  bool isBackTap(int x, int y) const override { return tappedBack(x, y); }
  int contentTop() const override { return TOPBAR_H + 4; }
  int batteryPercent() const override { return sensors::batteryPercent(); }
  bool charging() const override { return sensors::charging(); }
  bool soundOn() const override { return buzzer::enabled(); }
  void setSoundOn(bool on) override;

  ToolsCanvas& sharedCanvas() { return _canvas; }

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
