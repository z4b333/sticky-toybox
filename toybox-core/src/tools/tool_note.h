// Notes: write or dictate on your phone, read and tick off on the fridge.
//
// Three screens — the note list, the rendered note (paged, with checkboxes you
// can tap), and the pairing screen that puts the editor on your phone.
#pragma once
#include "flash_qr.h"
#include "lockscreen.h"
#include "note_md.h"
#include "note_store.h"
#include "note_web.h"
#include "tools_draw.h"

namespace nui {
// list
inline constexpr int LIST_X = 20, LIST_Y = 56, LIST_W = 440, ROW_H = 46;
inline constexpr int DEL_W = 34;
inline constexpr int PANEL_X = 20, PANEL_W = 440;
inline constexpr TRect WRITE_BTN{PANEL_X, 448, PANEL_W, 72};

// view
inline constexpr TRect BODY{20, 52, 440, 590};
inline constexpr TRect PREV_BTN{20, 654, 130, 50};
inline constexpr TRect NEXT_BTN{160, 654, 130, 50};
inline constexpr TRect PIN_BTN{20, 716, 150, 50};
inline constexpr TRect EDIT_BTN{190, 716, 150, 50};
// The text size, cycled in place: normal, large, largest. It lives here and
// not in settings because the note you are looking at is the only preview
// that means anything, and the change lands on it immediately.
inline constexpr TRect SIZE_BTN{360, 716, 100, 50};

// "what should happen to this note" prompt, shown once per arrival. Two
// choices and nothing else, so they are sized and spread like two cards; at 84
// and 72 px they huddled in the upper half and left 300 px of blank panel.
inline constexpr TRect PINNOW_BTN{60, 320, 360, 120};
inline constexpr TRect KEEP_BTN{60, 540, 360, 120};

// the "which way up" step, drawn over the note itself at whatever rotation is
// being tried. Anchored to the bottom-left and bottom-right of the *rotated*
// canvas rather than to fixed numbers, because the canvas is 480x800 at two of
// the four angles and 800x480 at the other two.
inline constexpr int ORIENT_H = 60, ORIENT_M = 20;
inline TRect turnRect(int w, int h) {
  return TRect{ORIENT_M, h - ORIENT_H - ORIENT_M, 200, ORIENT_H};
}
inline TRect keepRect(int w, int h) {
  return TRect{w - 200 - ORIENT_M, h - ORIENT_H - ORIENT_M, 200, ORIENT_H};
}

// pairing
inline constexpr int QR_X = 110, QR_Y = 140, QR_SIZE = 260;
// The way out sits in the bottom band on every screen that has one. White space
// above a bottom-anchored action reads as breathing room; the same white space
// below it reads as a screen that stopped early.
inline constexpr TRect ALT_BTN{40, 588, 400, 60};
inline constexpr TRect DONE_BTN{40, 672, 400, 72};

inline TRect rowRect(int i) { return TRect{LIST_X, LIST_Y + i * ROW_H, LIST_W, ROW_H - 6}; }
inline TRect delRect(int i) {
  return TRect{LIST_X + LIST_W - DEL_W - 4, LIST_Y + i * ROW_H + 3, DEL_W, ROW_H - 12};
}
}  // namespace nui


// The two buttons on the live pinned screen. Bottom-left, out of the way of the
// note itself, which owns the rest of the panel.
//
// UNPIN is here because this is the screen you are looking at when you decide
// you are finished with a note. Sending someone to the notes list to find the
// note they are already reading, to press a button that says the opposite of
// what they want, is the kind of path that gets designed and never walked.
inline constexpr TRect PINNED_HUB{20, 748, 110, 44};
inline constexpr TRect PINNED_UNPIN{144, 748, 120, 44};

// Paints the pinned note edge to edge, with no app chrome — this is what stays
// on the panel after the device powers down, and also what you see the moment
// you wake it. `live` is the difference: awake, the footer says what the finger
// and the button will do and offers a way into the hub; asleep, it just says
// how to wake. Returns false when nothing is pinned (or the pinned note has
// since been deleted), so the caller can fall back to its own goodbye screen.
inline bool drawPinnedFullScreen(ToolsCanvas& c, bool live = false) {
  char name[note::NAME_LEN + 1];
  if (!note::getPinned(name)) return false;
  String body;
  if (!note::load(name, body) || body.length() == 0) return false;

  // Runs once, on the way to power-off. A permanent block table would cost the
  // e-reader ~1.2 KB for every second it is not shutting down.
  nmd::Block* blocks = (nmd::Block*)malloc(sizeof(nmd::Block) * nmd::MAX_BLOCKS);
  if (!blocks) return false;
  const int n = nmd::parse(body.c_str(), blocks, nmd::MAX_BLOCKS);
  const TRect area{34, 26, c.width() - 68, c.height() - 76};
  nmd::render(c, body.c_str(), blocks, n, 0, area, nullptr, 0, nullptr);
  free(blocks);

  c.drawLine(34, c.height() - 58, c.width() - 34, c.height() - 58, 1, true);
  if (live) {
    // The note's own heading already names it, so the footer spends its width on
    // the two things a hand can do instead.
    c.button(PINNED_HUB.x, PINNED_HUB.y, PINNED_HUB.w, PINNED_HUB.h, "HUB", false, TS_MED);
    c.button(PINNED_UNPIN.x, PINNED_UNPIN.y, PINNED_UNPIN.w, PINNED_UNPIN.h, "UNPIN", false,
             TS_MED);
    // Shorter than they were, because the buttons now take the left half of
    // this band and text that ran under them was unreadable in both directions.
    const char* hint = "tap a line to tick";
    c.text(c.width() - 34 - c.textWidth(hint, TS_SMALL), c.height() - 46, hint, TS_SMALL,
           true);
    const char* lock = "power puts it back";
    c.text(c.width() - 34 - c.textWidth(lock, TS_SMALL), c.height() - 26, lock, TS_SMALL,
           true);
  } else {
    // The name is in whatever language the note is; it floors at its script's
    // readable size and hangs from the same bottom margin either way.
    const TSize nsz = scriptFloor(name, TS_SMALL);
    c.text(34, c.height() - 22 - c.textHeight(nsz), name, nsz, true);
    // The sleeping panel is the one that stays in view for hours, so it is the
    // one worth spending the footer on: the time it was last touched, and how
    // warm the room is. Falls back to the wake hint when the device has no
    // clock, or has one that has never been set.
    char status[48];
    const int n = lock::footer(status, sizeof(status), lock::config(), lock::read());
    const char* hint = n > 0 ? status : "press power to wake";
    c.text(c.width() - 34 - c.textWidth(hint, TS_SMALL), c.height() - 34, hint, TS_SMALL,
           true);
  }
  return true;
}

// Applies a tap to the pinned note without any app being open -- this is what
// the lock screen calls when a finger wakes the device. Renders once to work
// out where the lines are, edits the note, and leaves the repaint to the
// caller. Returns false when the tap missed, so nothing needs redrawing.
inline bool tapPinnedFullScreen(ToolsCanvas& c, int x, int y) {
  char name[note::NAME_LEN + 1];
  if (!note::getPinned(name)) return false;
  String body;
  if (!note::load(name, body) || body.length() == 0) return false;

  nmd::Block* blocks = (nmd::Block*)malloc(sizeof(nmd::Block) * nmd::MAX_BLOCKS);
  if (!blocks) return false;
  nmd::CheckHit hits[24];
  int hitCount = 0;
  const int n = nmd::parse(body.c_str(), blocks, nmd::MAX_BLOCKS);
  const TRect area{34, 26, c.width() - 68, c.height() - 76};
  nmd::render(c, body.c_str(), blocks, n, 0, area, hits, 24, &hitCount);

  bool changed = false;
  for (int i = 0; i < hitCount && !changed; i++) {
    if (!hits[i].box.hit(x, y)) continue;
    // Heap, not static: this runs for a fraction of a second on the way out of
    // sleep, and 4 KB held for ever is exactly what the rest of the tool goes
    // to such lengths to avoid.
    char* buf = (char*)malloc(note::MAX_BYTES + 8);
    if (!buf) break;
    int len = (int)body.length();
    if (len > note::MAX_BYTES) len = note::MAX_BYTES;
    memcpy(buf, body.c_str(), len);
    buf[len] = 0;
    const int nl = nmd::applyTap(buf, len, note::MAX_BYTES, blocks[hits[i].block]);
    if (nl >= 0) {
      buf[nl] = 0;
      note::save(name, buf, nl);
      changed = true;
    }
    free(buf);
  }
  free(blocks);
  return changed;
}

class NoteTool : public ToolApp {
 public:
  const char* title() const override {
    switch (_screen) {
      case Screen::View: return _name;
      case Screen::Orient: return "WHICH WAY UP";
      case Screen::Pair: return "NOTES";
      default: return "NOTES";
    }
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    tfs::begin();
    note::ensureSample();
    _screen = Screen::List;
    refreshList();
  }

  // Two screens tick: pairing (the web server) and the which-way-up step,
  // which follows the accelerometer while its question is open.
  bool wantsTick() const override {
    return _screen == Screen::Pair || (_screen == Screen::Orient && _gyro);
  }

  // Settings sends people here for the lock screen picture: the phone's page
  // carries the uploader, and there is no reason for a second one.
  bool openPairing() override {
    openPair();
    return true;
  }

  void tick() override {
    if (_screen == Screen::Orient) return tickOrient();
    if (_screen != Screen::Pair) return;
    _net.loop();
    // The moment a phone is on the access point, step two is the useful screen
    // and step one is a picture of a thing already done. The device can see
    // this, so it should not be asking.
    if (!_altQr && !_got && _net.hasClient()) {
      _altQr = true;
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (_net.received()) {
      // A save landed: show it immediately, so the phone edit and the panel
      // stay in step without the user tapping anything.
      _net.clearReceived();
      _gotBytes = _net.lastBytes();
      strncpy(_gotName, _net.lastName(), note::NAME_LEN);
      _gotName[note::NAME_LEN] = 0;
      _got = true;
      _answered = false;
      host().beep(3);
      host().refresh(true);
    }
  }

  void render(ToolsCanvas& c) override {
    // The orientation step owns the whole panel: it is showing the note the
    // size and shape it will be asleep, and a top bar in the app's own
    // orientation would be a lie sitting on top of it.
    if (_screen == Screen::Orient) return renderOrient(c);
    host().topBar(title());
    switch (_screen) {
      case Screen::List: renderList(c); break;
      case Screen::View: renderView(c); break;
      default: renderPair(c); break;
    }
  }

  void onTap(int x, int y) override {
    // No back button here: the only ways out are the two buttons on it, and
    // both leave the panel the right way up again.
    if (_screen == Screen::Orient) return tapOrient(x, y);
    if (host().isBackTap(x, y)) {
      if (_screen == Screen::View) {
        closeNote();
      } else if (_screen == Screen::Pair) {
        closePair();
      } else {
        host().goHub();
      }
      return;
    }
    switch (_screen) {
      case Screen::List: tapList(x, y); break;
      case Screen::View: tapView(x, y); break;
      default: tapPair(x, y); break;
    }
  }

#ifdef TOYBOX_HOST
  // Where the preview harness should aim to hit a given kind of line. The tap
  // bands are laid out from the text metrics, so a harness that hard-codes a y
  // only tests the fonts it was written against -- and the CrossPoint pass
  // renders the same screens with faces twice as tall.
  int hostLineY(nmd::Type want) const {
    for (int i = 0; i < _hitCount; i++)
      if (_blocks[_hits[i].block].type == want) return _hits[i].box.y + _hits[i].box.h / 2;
    return -1;
  }
#endif

 private:
  enum class Screen : uint8_t { List, View, Pair, Orient };

  // --- list --------------------------------------------------------------
  void refreshList() { _count = note::list(_infos, note::MAX_NOTES); }

  void renderList(ToolsCanvas& c) {
    using namespace nui;
    if (_count == 0) {
      c.textInBox(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, "no notes yet", TS_MED, true);
    }
    char buf[40];
    for (int i = 0; i < _count; i++) {
      const TRect r = rowRect(i);
      const bool pinned = note::isPinned(_infos[i].name);
      // Hairlines between rows, not boxes around them -- the hub's language,
      // the same one the shelves and the settings pages speak now. The pinned
      // note keeps its dot and gains the thin side-rule the readers use for
      // "you are here", which is exactly what pinned means on this device.
      if (pinned) c.fillRect(r.x - 8, r.y + 6, 3, r.h - 12, true);
      if (i + 1 < _count) c.fillRect(r.x, r.y + r.h + 2, r.w, 1, true);
      int nameX = r.x + 10;
      if (pinned) {  // a filled dot marks the note left on the screen
        c.fillCircle(r.x + 16, r.y + r.h / 2, 6, true);
        nameX = r.x + 32;
      }
      const TSize nsz = scriptFloor(_infos[i].name, TS_MED);
      c.text(nameX, r.y + (r.h - c.textHeight(nsz)) / 2, _infos[i].name, nsz, true);

      const int tasks = _infos[i].todo + _infos[i].done;
      if (tasks > 0)
        snprintf(buf, sizeof(buf), "%d/%d done", _infos[i].done, tasks);
      else
        snprintf(buf, sizeof(buf), "%d chars", _infos[i].bytes);
      const int tw = c.textWidth(buf, TS_SMALL);
      c.text(r.x + r.w - DEL_W - 16 - tw, r.y + (r.h - c.textHeight(TS_SMALL)) / 2, buf,
             TS_SMALL, true);

      // The x stands alone. It is still the whole delRect to a finger; the
      // frame around it was decoration, and eight frames a page was a wall.
      const TRect d = delRect(i);
      c.textInBox(d.x, d.y, d.w, d.h, "x", TS_MED, true);
    }

    c.button(WRITE_BTN.x, WRITE_BTN.y, WRITE_BTN.w, WRITE_BTN.h, "WRITE", true, TS_LARGE);
    c.text(PANEL_X, WRITE_BTN.y + WRITE_BTN.h + 8, "type or talk on your phone", TS_MED, true);

    // Body text is 24 px now, so the line pitch is 30 and the block below has
    // one line less to spend. The dictation step lost its turnover, and the note
    // count moved up beside the heading that follows it -- a number the list
    // above already shows does not deserve a line of its own when the panel is
    // this short of them.
    c.drawLine(PANEL_X, 556, c.width() - 20, 556, 1, true);
    c.text(PANEL_X, 564, "ON YOUR PHONE", TS_MED, true);
    const char* steps[3] = {"1  tap WRITE, scan QR", "2  editor opens by itself",
                            "3  dictate with the mic key"};
    for (int i = 0; i < 3; i++) c.text(PANEL_X, 594 + i * 30, steps[i], TS_MED, true);

    c.drawLine(PANEL_X, 686, c.width() - 20, 686, 1, true);
    c.text(PANEL_X, 694, "ON THE DEVICE", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d notes", _count);
    c.text(c.width() - 20 - c.textWidth(buf, TS_MED), 694, buf, TS_MED, true);
    c.text(PANEL_X, 724, "tap a line to tick or cross", TS_MED, true);
    c.text(PANEL_X, 754, "a dot marks the pinned note", TS_MED, true);
  }

  void tapList(int x, int y) {
    using namespace nui;
    for (int i = 0; i < _count; i++) {
      if (delRect(i).hit(x, y)) {
        note::remove(_infos[i].name);
        refreshList();
        host().beep(2);
        host().refresh(true);
        return;
      }
      if (rowRect(i).hit(x, y)) return openNote(i);
    }
    if (WRITE_BTN.hit(x, y)) return openPair();
  }

  // --- view --------------------------------------------------------------
  void openNote(int idx) {
    strncpy(_name, _infos[idx].name, note::NAME_LEN);
    _name[note::NAME_LEN] = 0;
    if (!loadBody()) {
      host().beep(2);
      return;
    }
    _pageDepth = 0;
    _pageStack[0] = 0;
    _screen = Screen::View;
    host().beep(1);
    host().refresh(true);
  }

  bool loadBody() {
    if (!_buf) _buf = (char*)malloc(note::MAX_BYTES + 1);
    if (!_buf) return false;
    String body;
    if (!note::load(_name, body)) {
      free(_buf);
      _buf = nullptr;
      return false;
    }
    size_t n = body.length();
    if (n > note::MAX_BYTES) n = note::MAX_BYTES;
    memcpy(_buf, body.c_str(), n);
    _buf[n] = 0;
    _len = (int)n;
    _blockCount = nmd::parse(_buf, _blocks, nmd::MAX_BLOCKS);
    return true;
  }

  void closeNote() {
    free(_buf);
    _buf = nullptr;
    _screen = Screen::List;
    refreshList();
    host().refresh(true);
  }

  void renderView(ToolsCanvas& c) {
    using namespace nui;
    if (!_buf) return;
    _next = nmd::render(c, _buf, _blocks, _blockCount, _pageStack[_pageDepth], BODY, _hits,
                        kMaxHits, &_hitCount);

    const bool hasNext = _next < _blockCount;
    const bool hasPrev = _pageDepth > 0;
    if (hasPrev || hasNext) {
      c.button(PREV_BTN.x, PREV_BTN.y, PREV_BTN.w, PREV_BTN.h, "< BACK", false, TS_MED);
      c.button(NEXT_BTN.x, NEXT_BTN.y, NEXT_BTN.w, NEXT_BTN.h, "MORE >", hasNext, TS_MED);
      char buf[24];
      snprintf(buf, sizeof(buf), "page %d", _pageDepth + 1);
      c.text(NEXT_BTN.x + NEXT_BTN.w + 24, PREV_BTN.y + 16, buf, TS_MED, true);
    }
    const bool pinned = note::isPinned(_name);
    c.button(PIN_BTN.x, PIN_BTN.y, PIN_BTN.w, PIN_BTN.h, pinned ? "UNPIN" : "PIN",
             pinned, TS_MED);
    c.button(EDIT_BTN.x, EDIT_BTN.y, EDIT_BTN.w, EDIT_BTN.h, "EDIT", false, TS_MED);
    // The label is the state: one A at each size the body can be.
    c.button(SIZE_BTN.x, SIZE_BTN.y, SIZE_BTN.w, SIZE_BTN.h, "A",
             false, nmd::g_body == TS_MED ? TS_SMALL : nmd::g_body == TS_LARGE ? TS_MED : TS_LARGE);
  }

  void tapView(int x, int y) {
    using namespace nui;
    if (PIN_BTN.hit(x, y)) {
      // Unpinning is just unpinning. Pinning asks which way up, the same way
      // the phone's prompt does -- it is the same decision and it deserves the
      // same screen rather than a different answer depending on which door you
      // came through.
      if (note::isPinned(_name)) {
        note::setPinned("");
        host().beep(1);
        host().refresh(false);
        return;
      }
      note::setPinned(_name);
      _returnTo = Screen::View;
      return openOrient();
    }
    if (EDIT_BTN.hit(x, y)) {
      free(_buf);
      _buf = nullptr;
      return openPair();
    }
    if (SIZE_BTN.hit(x, y)) {
      const TSize next = nmd::g_body == TS_MED    ? TS_LARGE
                         : nmd::g_body == TS_LARGE ? TS_HUGE
                                                   : TS_MED;
      nmd::setBody(next);
      prefs().putUInt("nt_size", (uint32_t)(next - TS_MED));
      // Bigger lines hold fewer blocks, so the page boundaries just moved;
      // page one is the only page whose start is still true.
      _pageDepth = 0;
      _pageStack[0] = 0;
      host().beep(0);
      host().refresh(false);
      return;
    }
    const bool hasNext = _next < _blockCount;
    if (NEXT_BTN.hit(x, y) && hasNext) {
      if (_pageDepth + 1 < kMaxPages) {
        _pageStack[++_pageDepth] = _next;
        host().beep(0);
        host().refresh(true);
      }
      return;
    }
    if (PREV_BTN.hit(x, y) && _pageDepth > 0) {
      _pageDepth--;
      host().beep(0);
      host().refresh(true);
      return;
    }
    // Tapping any line acts on it: a checkbox flips, anything else is crossed
    // out. Both are written straight back into the note's own Markdown.
    for (int i = 0; i < _hitCount; i++) {
      if (!_hits[i].box.hit(x, y)) continue;
      const int n = nmd::applyTap(_buf, _len, note::MAX_BYTES, _blocks[_hits[i].block]);
      if (n < 0) {
        host().beep(2);
        return;
      }
      _len = n;
      _buf[_len] = 0;
      note::save(_name, _buf, _len);
      _blockCount = nmd::parse(_buf, _blocks, nmd::MAX_BLOCKS);
      host().beep(1);
      host().refresh(true);
      return;
    }
  }

  // --- pairing -----------------------------------------------------------
  void openPair() {
    _screen = Screen::Pair;
    _altQr = false;
    _got = false;
    _answered = false;
    _netOk = _net.start();
    host().beep(_netOk ? 1 : 2);
    host().refresh(true);
  }

  void closePair() {
    _net.stop();
    _screen = Screen::List;
    refreshList();
    host().beep(1);
    host().refresh(true);
  }

  void renderPair(ToolsCanvas& c) {
    using namespace nui;
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start wifi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "BACK", true, TS_LARGE);
      return;
    }
    if (_got) return renderPairDone(c);

    char buf[64];
    // Two steps, and the device moves between them itself when a phone joins
    // its access point. It used to ask -- a button reading PAGE DIDN'T OPEN?
    // under a QR code that is only a wifi credential, which promised something
    // the code does not do and made the honest answer ("it joins the wifi, and
    // then your phone usually opens the page on its own") sound like an excuse.
    if (_altQr) {
      c.textCentered(c.width() / 2, 64, "STEP 2 OF 2", TS_MED, true, true);
      c.textCentered(c.width() / 2, 96, "phone joined", TS_MED, true);
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, _net.url());
      c.textCentered(c.width() / 2, 406, "The editor should have opened.", TS_MED, true);
      c.textCentered(c.width() / 2, 434, "If not, scan this or type it in:", TS_MED, true);
      c.textCentered(c.width() / 2, 470, _net.url(), TS_MED, true, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "BACK TO THE WIFI CODE", false,
               TS_MED);
    } else {
      c.textCentered(c.width() / 2, 64, "STEP 1 OF 2", TS_MED, true, true);
      c.textCentered(c.width() / 2, 96, "join the device's wifi", TS_MED, true);
      const String wifi = _net.wifiPayload();
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, wifi.c_str());
      c.textCentered(c.width() / 2, 406, "Scan with your phone camera", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   key %s", _net.ssid(), _net.password());
      c.textCentered(c.width() / 2, 444, buf, TS_MED, true);
      c.textCentered(c.width() / 2, 480, "this code joins the wifi, nothing more", TS_SMALL,
                     true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "SHOW THE LINK", false, TS_MED);
    }
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", false, TS_LARGE);
  }

  void renderPairDone(ToolsCanvas& c) {
    using namespace nui;
    char buf[80];
    c.textCentered(c.width() / 2, 70, "SAVED", TS_LARGE, true, true);
    c.textCentered(c.width() / 2, 110, _gotName, TS_HUGE, true, true);

    if (!_answered) {
      c.textCentered(c.width() / 2, 210, "Leave this on the screen", TS_MED, true);
      c.textCentered(c.width() / 2, 236, "when the device sleeps?", TS_MED, true);
      c.button(PINNOW_BTN.x, PINNOW_BTN.y, PINNOW_BTN.w, PINNOW_BTN.h, "SHOW ON SCREEN", true,
               TS_LARGE);
      c.textCentered(PINNOW_BTN.x + PINNOW_BTN.w / 2, PINNOW_BTN.y + PINNOW_BTN.h + 12,
                     "stays on with the power off", TS_MED, true);
      c.button(KEEP_BTN.x, KEEP_BTN.y, KEEP_BTN.w, KEEP_BTN.h, "JUST SAVE", false, TS_LARGE);
      c.textCentered(KEEP_BTN.x + KEEP_BTN.w / 2, KEEP_BTN.y + KEEP_BTN.h + 12,
                     "keep it in the notes list", TS_MED, true);
      return;
    }

    if (_pinnedIt) {
      c.textCentered(c.width() / 2, 220, "Pinned to screen", TS_LARGE, true, true);
      c.textCentered(c.width() / 2, 274, "Hold the power button and it", TS_MED, true);
      c.textCentered(c.width() / 2, 300, "will stay on the panel until", TS_MED, true);
      c.textCentered(c.width() / 2, 326, "you change it.", TS_MED, true);
    } else {
      snprintf(buf, sizeof(buf), "Saved to the notes list");
      c.textCentered(c.width() / 2, 230, buf, TS_MED, true);
      snprintf(buf, sizeof(buf), "(%d characters).", _gotBytes);
      c.textCentered(c.width() / 2, 256, buf, TS_MED, true);
      c.textCentered(c.width() / 2, 300, "You can pin it later from", TS_MED, true);
      c.textCentered(c.width() / 2, 326, "the note itself.", TS_MED, true);
    }
    c.textCentered(c.width() / 2, 420, "keep editing on your phone", TS_MED, true);
    c.textCentered(c.width() / 2, 446, "and send again, or tap DONE", TS_MED, true);
    c.textCentered(c.width() / 2, 472, "to read it here", TS_MED, true);
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", true, TS_LARGE);
  }

  // --- which way up ------------------------------------------------------
  // Shown once, at the moment of pinning, because that is the only moment the
  // question has an obvious answer: the note is in front of you and the magnet
  // is about to go somewhere. Asking it from a settings row later would mean
  // choosing an angle for a note you cannot see.
  void openOrient() {
    _screen = Screen::Orient;
    _wasScreen = _returnTo;
    // With an accelerometer, the answer to "which way up" is the way the
    // device is already being held: the hand doing the pinning turned it the
    // right way round before the screen could ask. The step opens at that
    // angle and follows the device live; the button only confirms. Without
    // one, the TURN button carries on asking the old way.
    const int held = host().deviceOrientation();
    _gyro = held >= 0;
    _orient = _gyro ? held : (lock::config().pinRotation & 3);
    _tickDiv = 0;
    host().setCanvasRotation(_orient);
    host().beep(1);
    host().refresh(true);
  }

  void tickOrient() {
    if (!_gyro) return;
    // ~6 Hz is plenty: the sensor itself updates at 26 Hz and a person
    // turning a device is slower than either.
    if (++_tickDiv < 8) return;
    _tickDiv = 0;
    const int held = host().deviceOrientation();
    if (held < 0 || held == _orient) return;
    _orient = held;
    host().setCanvasRotation(_orient);
    // A quarter turn changes every pixel, so this cannot be a partial update.
    host().refresh(true);
  }

  void closeOrient(bool save) {
    if (save) lock::setPinRotation(prefs(), (uint8_t)_orient);
    host().setCanvasRotation(0);  // apps are portrait; put the panel back
    _screen = _wasScreen;
    _returnTo = Screen::Pair;
    _answered = true;
    host().beep(1);
    host().refresh(true);
  }

  void renderOrient(ToolsCanvas& c) {
    using namespace nui;
    // The note itself, edge to edge, exactly as it will be with the power off.
    // A thumbnail would answer a different question.
    if (!drawPinnedFullScreen(c)) {
      c.textCentered(c.width() / 2, c.height() / 2, "nothing pinned", TS_LARGE, true);
    }
    const TRect t = turnRect(c.width(), c.height());
    const TRect k = keepRect(c.width(), c.height());
    // Painted over the note rather than beside it: there is no beside. White
    // boxes first, so a dense note does not swallow its own way out.
    c.fillRect(t.x - 6, t.y - 6, (k.x + k.w) - (t.x - 6) + 6, t.h + 12, false);
    if (_gyro) {
      // No TURN button: turning the device IS the control. Two short lines in
      // its place say so, and the confirm stays where a thumb knows it.
      c.text(t.x + 4, t.y + 2, "turn the device -", TS_SMALL, true);
      c.text(t.x + 4, t.y + 26, "the note follows", TS_SMALL, true);
    } else {
      c.button(t.x, t.y, t.w, t.h, "TURN", false, TS_LARGE);
    }
    c.button(k.x, k.y, k.w, k.h, "THIS WAY UP", true, TS_LARGE);
  }

  void tapOrient(int x, int y) {
    using namespace nui;
    if (!_gyro && turnRect(canvas().width(), canvas().height()).hit(x, y)) {
      _orient = (_orient + 1) & 3;
      host().setCanvasRotation(_orient);
      host().beep(0);
      // A quarter turn changes every pixel, so this cannot be a partial update.
      host().refresh(true);
      return;
    }
    if (keepRect(canvas().width(), canvas().height()).hit(x, y)) closeOrient(true);
  }

  void tapPair(int x, int y) {
    using namespace nui;
    if (_got && !_answered) {  // the prompt owns the screen until it is answered
      if (PINNOW_BTN.hit(x, y)) {
        note::setPinned(_gotName);
        _pinnedIt = true;
        return openOrient();
      }
      if (KEEP_BTN.hit(x, y)) {
        _pinnedIt = false;
        _answered = true;
        host().beep(1);
        host().refresh(true);
      }
      return;
    }
    if (DONE_BTN.hit(x, y)) return closePair();
    if (!_got && _netOk && nui::ALT_BTN.hit(x, y)) {
      _altQr = !_altQr;
      host().beep(0);
      host().refresh(true);
    }
  }

  // --- state -------------------------------------------------------------
  int _orient = 0;      // the angle being tried on the which-way-up step
  bool _gyro = false;   // that angle follows the device rather than a button
  uint8_t _tickDiv = 0;
  // Which screen the which-way-up step came from, and so where it goes back to:
  // the phone prompt, or the note you were reading when you pinned it.
  Screen _returnTo = Screen::Pair;
  Screen _wasScreen = Screen::Pair;
  static constexpr int kMaxHits = 24;
  static constexpr int kMaxPages = 12;

  Screen _screen = Screen::List;
  note::Info _infos[note::MAX_NOTES] = {};
  int _count = 0;

  char _name[note::NAME_LEN + 1] = {};
  // Held only while a note is open, so the WiFi stack has the heap to itself.
  char* _buf = nullptr;
  int _len = 0;
  nmd::Block _blocks[nmd::MAX_BLOCKS] = {};
  int _blockCount = 0, _next = 0;
  nmd::CheckHit _hits[kMaxHits] = {};
  int _hitCount = 0;
  int _pageStack[kMaxPages] = {};
  int _pageDepth = 0;

  nweb::NoteServer _net;
  bool _netOk = false, _altQr = false, _got = false;
  bool _answered = false, _pinnedIt = false;
  char _gotName[note::NAME_LEN + 1] = {};
  int _gotBytes = 0;
};
