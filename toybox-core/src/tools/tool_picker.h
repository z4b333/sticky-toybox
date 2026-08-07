// Random picker over a user-entered list (who buys lunch, which chore, ...).
// Items are kept in NVS between sessions, and can be entered either on the
// on-screen keyboard or -- far less painfully -- from a phone.
// "REMOVE PICKED" draws without replacement, which is what you want for
// assigning an order or splitting into turns.
#pragma once
#include <esp_random.h>

#include "flash_qr.h"
#include "picker_list.h"
#include "picker_web.h"
#include "tools_draw.h"

namespace pickui {
inline constexpr int MAX_ITEMS = plist::MAX_ITEMS;
inline constexpr int MAX_LEN = plist::MAX_LEN;

inline constexpr int LIST_X = 20, LIST_Y = 56, LIST_W = 440, ROW_H = 38;
inline constexpr int DEL_W = 34;
// Two input paths get equal billing on the top row; housekeeping sits below.
inline constexpr TRect ADD_BTN{20, 444, 215, 46};
inline constexpr TRect PHONE_BTN{245, 444, 215, 46};
inline constexpr TRect CLEAR_BTN{20, 496, 215, 46};
inline constexpr TRect MODE{245, 496, 215, 46};
inline constexpr TRect RESULT{20, 556, 440, 110};
inline constexpr TRect SPIN{20, 676, 440, 70};

// pairing screen, laid out like the notes and flashcard ones -- including the
// bottom band the way out now sits in on all three
inline constexpr int QR_X = 110, QR_Y = 140, QR_SIZE = 260;
inline constexpr TRect ALT_BTN{40, 588, 400, 60};
inline constexpr TRect DONE_BTN{40, 672, 400, 72};

// keyboard overlay
inline constexpr int KEY_W = 44, KEY_H = 56, KEY_GAP = 4;
inline constexpr int KROW1_Y = 340, KROW2_Y = 404, KROW3_Y = 468, KBOT_Y = 540;
inline constexpr int KROW1_X = 2, KROW2_X = 26, KROW3_X = 74;
inline constexpr TRect K_CANCEL{20, KBOT_Y, 130, 56};
inline constexpr TRect K_SPACE{160, KBOT_Y, 160, 56};
inline constexpr TRect K_DEL{330, KBOT_Y, 130, 56};
inline constexpr TRect K_OK{20, KBOT_Y + 72, 440, 64};
inline constexpr const char* ROW1 = "QWERTYUIOP";
inline constexpr const char* ROW2 = "ASDFGHJKL";
inline constexpr const char* ROW3 = "ZXCVBNM";

inline TRect rowRect(int i) { return TRect{LIST_X, LIST_Y + i * ROW_H, LIST_W, ROW_H - 4}; }
inline TRect delRect(int i) {
  return TRect{LIST_X + LIST_W - DEL_W - 4, LIST_Y + i * ROW_H + 2, DEL_W, ROW_H - 8};
}
}  // namespace pickui

class PickerTool : public ToolApp {
 public:
  const char* title() const override {
    return _screen == Screen::Typing  ? "NEW ITEM"
           : _screen == Screen::Pair  ? "PICKER"
                                      : "PICKER";
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    load();
    _screen = Screen::List;
    _picked = -1;
    _removeMode = prefs().getBool("pk_rm", false);
    memset(_used, 0, sizeof(_used));
  }

  // The pairing screen has to pump the web server, so it wants the tick.
  bool wantsTick() const override { return _screen == Screen::Pair; }
  void tick() override {
    if (_screen != Screen::Pair) return;
    _net.loop();
    if (!_net.received()) return;
    _net.clearReceived();
    _gotCount = _net.lastCount();
    host().beep(3);
    host().refresh(true);
  }

  void render(ToolsCanvas& c) override {
    if (_screen == Screen::Typing) {
      renderKeyboard(c);
      return;
    }
    if (_screen == Screen::Pair) {
      renderPair(c);
      return;
    }
    using namespace pickui;
    host().topBar(title());

    // --- item list ---------------------------------------------------------
    if (_count == 0) {
      c.drawRect(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, 1, true);
      c.textInBox(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, "no items yet", TS_MED, true);
    }
    for (int i = 0; i < _count; i++) {
      const TRect r = rowRect(i);
      const bool spent = _removeMode && _used[i];
      c.drawRect(r.x, r.y, r.w, r.h, spent ? 1 : 2, true);
      const TSize nsz = scriptFloor(_items[i], TS_MED);
      c.text(r.x + 10, r.y + (r.h - c.textHeight(nsz)) / 2, _items[i], nsz, true);
      if (spent) {  // struck through once it has been drawn
        const int my = r.y + r.h / 2;
        c.drawLine(r.x + 8, my, r.x + r.w - DEL_W - 12, my, 2, true);
      }
      const TRect d = delRect(i);
      c.drawRect(d.x, d.y, d.w, d.h, 1, true);
      c.textInBox(d.x, d.y, d.w, d.h, "x", TS_MED, true);
    }

    c.button(ADD_BTN.x, ADD_BTN.y, ADD_BTN.w, ADD_BTN.h, "ADD ITEM", _count < MAX_ITEMS);
    c.button(PHONE_BTN.x, PHONE_BTN.y, PHONE_BTN.w, PHONE_BTN.h, "FROM PHONE", false);
    c.button(CLEAR_BTN.x, CLEAR_BTN.y, CLEAR_BTN.w, CLEAR_BTN.h, "CLEAR ALL", false);

    // --- result ------------------------------------------------------------
    c.drawRect(RESULT.x, RESULT.y, RESULT.w, RESULT.h, 3, true);
    if (_picked >= 0 && _picked < _count) {
      const int len = (int)strlen(_items[_picked]);
      const TSize sz = len <= 6 ? TS_HUGE : (len <= 11 ? TS_LARGE : TS_MED);
      c.textInBox(RESULT.x, RESULT.y, RESULT.w, RESULT.h, _items[_picked], sz, true, true);
    } else {
      c.textInBox(RESULT.x, RESULT.y, RESULT.w, RESULT.h, "- - -", TS_HUGE, true, true);
    }

    c.button(SPIN.x, SPIN.y, SPIN.w, SPIN.h, "PICK ONE", _count > 0, TS_HUGE);
    c.button(MODE.x, MODE.y, MODE.w, MODE.h, _removeMode ? "NO REPEAT" : "KEEP ALL",
             _removeMode);

    char buf[40];
    if (_removeMode) {
      snprintf(buf, sizeof(buf), "%d of %d left", remaining(), _count);
    } else {
      snprintf(buf, sizeof(buf), "%d items", _count);
    }
    c.textCentered(c.width() / 2, SPIN.y + SPIN.h + 14, buf, TS_MED, true);
  }

  void onTap(int x, int y) override {
    if (_screen == Screen::Typing) return tapKeyboard(x, y);
    if (_screen == Screen::Pair) return tapPair(x, y);
    using namespace pickui;
    if (host().isBackTap(x, y)) {
      host().goHub();
      return;
    }
    for (int i = 0; i < _count; i++) {
      if (delRect(i).hit(x, y)) return removeItem(i);
    }
    if (ADD_BTN.hit(x, y) && _count < MAX_ITEMS) {
      _screen = Screen::Typing;
      _draft[0] = 0;
      _draftLen = 0;
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (PHONE_BTN.hit(x, y)) return openPair();
    if (CLEAR_BTN.hit(x, y)) {
      _count = 0;
      _picked = -1;
      memset(_used, 0, sizeof(_used));
      save();
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (MODE.hit(x, y)) {
      _removeMode = !_removeMode;
      prefs().putBool("pk_rm", _removeMode);
      memset(_used, 0, sizeof(_used));
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (SPIN.hit(x, y) && _count > 0) return pick();
  }

 private:
  int remaining() const {
    int n = 0;
    for (int i = 0; i < _count; i++)
      if (!_used[i]) n++;
    return n;
  }

  void pick() {
    if (_removeMode) {
      if (remaining() == 0) memset(_used, 0, sizeof(_used));  // new pass
      int idx;
      do {
        idx = (int)(esp_random() % (uint32_t)_count);
      } while (_used[idx]);
      _used[idx] = 1;
      _picked = idx;
    } else {
      _picked = (int)(esp_random() % (uint32_t)_count);
    }
    host().beep(3);
    host().refresh(true);
  }

  void removeItem(int i) {
    for (int j = i; j < _count - 1; j++) {
      memcpy(_items[j], _items[j + 1], pickui::MAX_LEN + 1);
      _used[j] = _used[j + 1];
    }
    _count--;
    _picked = -1;
    save();
    host().beep(1);
    host().refresh(true);
  }

  // --- keyboard ----------------------------------------------------------
  void renderKeyboard(ToolsCanvas& c) {
    using namespace pickui;
    c.textCentered(c.width() / 2, 54, "TYPE A NEW ITEM", TS_LARGE, true, true);
    c.drawRect(20, 110, 440, 64, 3, true);
    c.textInBox(20, 110, 440, 64, _draftLen ? _draft : "_", TS_LARGE, true, true);

    char lbl[2] = {0, 0};
    for (int i = 0; ROW1[i]; i++) {
      lbl[0] = ROW1[i];
      c.button(KROW1_X + i * (KEY_W + KEY_GAP), KROW1_Y, KEY_W, KEY_H, lbl, false, TS_MED);
    }
    for (int i = 0; ROW2[i]; i++) {
      lbl[0] = ROW2[i];
      c.button(KROW2_X + i * (KEY_W + KEY_GAP), KROW2_Y, KEY_W, KEY_H, lbl, false, TS_MED);
    }
    for (int i = 0; ROW3[i]; i++) {
      lbl[0] = ROW3[i];
      c.button(KROW3_X + i * (KEY_W + KEY_GAP), KROW3_Y, KEY_W, KEY_H, lbl, false, TS_MED);
    }
    c.button(K_CANCEL.x, K_CANCEL.y, K_CANCEL.w, K_CANCEL.h, "CANCEL", false);
    c.button(K_SPACE.x, K_SPACE.y, K_SPACE.w, K_SPACE.h, "SPACE", false);
    c.button(K_DEL.x, K_DEL.y, K_DEL.w, K_DEL.h, "DEL", false);
    c.button(K_OK.x, K_OK.y, K_OK.w, K_OK.h, "OK", _draftLen > 0, TS_LARGE);
  }

  void tapKeyboard(int x, int y) {
    using namespace pickui;
    if (K_CANCEL.hit(x, y)) {
      _screen = Screen::List;
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (K_OK.hit(x, y)) {
      if (_draftLen > 0 && _count < MAX_ITEMS) {
        memcpy(_items[_count], _draft, MAX_LEN + 1);
        _used[_count] = 0;
        _count++;
        save();
      }
      _screen = Screen::List;
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (K_DEL.hit(x, y)) {
      if (_draftLen > 0) _draft[--_draftLen] = 0;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (K_SPACE.hit(x, y)) return typeChar(' ');

    auto rowHit = [&](const char* row, int x0, int rowY) -> char {
      if (y < rowY || y >= rowY + KEY_H || x < x0) return 0;
      const int i = (x - x0) / (KEY_W + KEY_GAP);
      const int n = (int)strlen(row);
      if (i < 0 || i >= n) return 0;
      // reject taps that land in the gap between keys
      if ((x - x0) - i * (KEY_W + KEY_GAP) >= KEY_W) return 0;
      return row[i];
    };
    char ch = rowHit(ROW1, KROW1_X, KROW1_Y);
    if (!ch) ch = rowHit(ROW2, KROW2_X, KROW2_Y);
    if (!ch) ch = rowHit(ROW3, KROW3_X, KROW3_Y);
    if (ch) typeChar(ch);
  }

  void typeChar(char ch) {
    if (_draftLen >= pickui::MAX_LEN) {
      host().beep(2);
      return;
    }
    _draft[_draftLen++] = ch;
    _draft[_draftLen] = 0;
    host().beep(0);
    host().refresh(false);
  }

  // --- phone -------------------------------------------------------------
  // The server is handed a reader and a writer rather than a copy of the list,
  // so the page always opens on what is actually on the device and a save lands
  // straight in NVS. Editing on the phone and on the keyboard cannot diverge.
  void openPair() {
    _netOk = _net.start([this] { return plist::toText(_items, _count); },
                        [this](const String& text) { return applyFromPhone(text); });
    _altQr = false;
    _gotCount = 0;
    _screen = Screen::Pair;
    host().beep(_netOk ? 1 : 2);
    host().refresh(true);
  }

  int applyFromPhone(const String& text) {
    _count = plist::fromText(text.c_str(), _items);
    _picked = -1;
    memset(_used, 0, sizeof(_used));
    save();
    _gotCount = _count;
    return _count;
  }

  void closePair() {
    _net.stop();
    _netOk = false;
    _screen = Screen::List;
    load();
    host().beep(1);
    host().refresh(true);
  }

  void renderPair(ToolsCanvas& c) {
    using namespace pickui;
    host().topBar(title());

    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start wifi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "BACK", true, TS_LARGE);
      return;
    }

    if (_gotCount > 0) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%d", _gotCount);
      tdraw::seg7Centered(c, c.width() / 2, 150, 120, buf, true);
      c.textCentered(c.width() / 2, 310, _gotCount == 1 ? "item on the device" : "items on the device",
                     TS_LARGE, true, true);
      for (int i = 0; i < _count && i < 5; i++)
        c.textCentered(c.width() / 2, 366 + i * 26, _items[i], TS_MED, true);
      if (_count > 5) c.textCentered(c.width() / 2, 366 + 5 * 26, "...", TS_MED, true);
      c.textCentered(c.width() / 2, 528, "keep editing on your phone", TS_MED, true);
      c.textCentered(c.width() / 2, 556, "and save again, or tap DONE", TS_MED, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", true, TS_LARGE);
      return;
    }

    char buf[64];
    if (_altQr) {
      c.textCentered(c.width() / 2, 64, "if it did not open by itself", TS_MED, true);
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, _net.url());
      c.textCentered(c.width() / 2, 406, "Open this in your browser", TS_MED, true, true);
      c.textCentered(c.width() / 2, 444, _net.url(), TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "BACK TO WIFI", false, TS_MED);
    } else {
      c.textCentered(c.width() / 2, 64, "the list opens by itself", TS_MED, true);
      const String wifi = _net.wifiPayload();
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, wifi.c_str());
      c.textCentered(c.width() / 2, 406, "Scan with your phone camera", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   key %s", _net.ssid(), _net.password());
      c.textCentered(c.width() / 2, 444, buf, TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "PAGE DIDN'T OPEN?", false, TS_MED);
    }
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", false, TS_LARGE);
  }

  void tapPair(int x, int y) {
    using namespace pickui;
    if (DONE_BTN.hit(x, y)) return closePair();
    if (_netOk && _gotCount == 0 && ALT_BTN.hit(x, y)) {
      _altQr = !_altQr;
      host().beep(0);
      host().refresh(true);
    }
  }

  // --- persistence -------------------------------------------------------
  // NVS holds exactly the text format the phone posts, so there is one parser
  // for both paths and one place where the limits are enforced.
  void load() {
    char blob[plist::BLOB] = {};
    prefs().getString("pk_items", blob, sizeof(blob));
    _count = plist::fromText(blob, _items);
  }

  void save() {
    const String text = plist::toText(_items, _count);
    prefs().putString("pk_items", text.c_str());
  }

  plist::Item _items[plist::MAX_ITEMS] = {};
  uint8_t _used[pickui::MAX_ITEMS] = {};
  int _count = 0;
  int _picked = -1;
  bool _removeMode = false;

  enum class Screen : uint8_t { List, Typing, Pair };
  Screen _screen = Screen::List;
  char _draft[pickui::MAX_LEN + 1] = {};
  int _draftLen = 0;

  pweb::ListServer _net;
  bool _netOk = false, _altQr = false;
  int _gotCount = 0;
};
