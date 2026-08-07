// Notes: write or dictate on your phone, read and tick off on the fridge.
//
// Three screens — the note list, the rendered note (paged, with checkboxes you
// can tap), and the pairing screen that puts the editor on your phone.
#pragma once
#include "flash_qr.h"
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

// "what should happen to this note" prompt, shown once per arrival. Two
// choices and nothing else, so they are sized and spread like two cards; at 84
// and 72 px they huddled in the upper half and left 300 px of blank panel.
inline constexpr TRect PINNOW_BTN{60, 320, 360, 120};
inline constexpr TRect KEEP_BTN{60, 540, 360, 120};

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

// The one button on the live pinned screen. Bottom-left, out of the way of the
// note itself, which owns the rest of the panel.
inline constexpr TRect PINNED_HUB{20, 748, 110, 44};

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
    const char* hint = "tap a line to tick or cross it";
    c.text(c.width() - 34 - c.textWidth(hint, TS_SMALL), c.height() - 46, hint, TS_SMALL,
           true);
    const char* lock = "press power to put it back";
    c.text(c.width() - 34 - c.textWidth(lock, TS_SMALL), c.height() - 26, lock, TS_SMALL,
           true);
  } else {
    // The name is in whatever language the note is; it floors at its script's
    // readable size and hangs from the same bottom margin either way.
    const TSize nsz = scriptFloor(name, TS_SMALL);
    c.text(34, c.height() - 22 - c.textHeight(nsz), name, nsz, true);
    const char* hint = "press power to wake";
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

  bool wantsTick() const override { return _screen == Screen::Pair; }

  void tick() override {
    if (_screen != Screen::Pair) return;
    _net.loop();
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
    host().topBar(title());
    switch (_screen) {
      case Screen::List: renderList(c); break;
      case Screen::View: renderView(c); break;
      default: renderPair(c); break;
    }
  }

  void onTap(int x, int y) override {
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
  enum class Screen : uint8_t { List, View, Pair };

  // --- list --------------------------------------------------------------
  void refreshList() { _count = note::list(_infos, note::MAX_NOTES); }

  void renderList(ToolsCanvas& c) {
    using namespace nui;
    if (_count == 0) {
      c.drawRect(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, 1, true);
      c.textInBox(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, "no notes yet", TS_MED, true);
    }
    char buf[40];
    for (int i = 0; i < _count; i++) {
      const TRect r = rowRect(i);
      const bool pinned = note::isPinned(_infos[i].name);
      c.drawRect(r.x, r.y, r.w, r.h, pinned ? 4 : 2, true);
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

      const TRect d = delRect(i);
      c.drawRect(d.x, d.y, d.w, d.h, 1, true);
      c.textInBox(d.x, d.y, d.w, d.h, "x", TS_MED, true);
    }

    c.button(WRITE_BTN.x, WRITE_BTN.y, WRITE_BTN.w, WRITE_BTN.h, "WRITE", true, TS_LARGE);
    c.text(PANEL_X, WRITE_BTN.y + WRITE_BTN.h + 8, "type or talk on your phone", TS_MED, true);

    c.drawLine(PANEL_X, 556, c.width() - 20, 556, 1, true);
    c.text(PANEL_X, 564, "ON YOUR PHONE", TS_MED, true);
    const char* steps[4] = {"1  tap WRITE, scan QR", "2  editor opens by itself",
                            "3  dictate with the", "   keyboard mic key"};
    for (int i = 0; i < 4; i++) c.text(PANEL_X, 590 + i * 26, steps[i], TS_MED, true);

    c.drawLine(PANEL_X, 694, c.width() - 20, 694, 1, true);
    c.text(PANEL_X, 700, "ON THE DEVICE", TS_MED, true);
    c.text(PANEL_X, 726, "tap a line to tick or cross", TS_MED, true);
    c.text(PANEL_X, 752, "a dot marks the pinned note", TS_MED, true);

    snprintf(buf, sizeof(buf), "%d notes", _count);
    c.text(PANEL_X, 778, buf, TS_MED, true);
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
  }

  void tapView(int x, int y) {
    using namespace nui;
    if (PIN_BTN.hit(x, y)) {
      note::setPinned(note::isPinned(_name) ? "" : _name);
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (EDIT_BTN.hit(x, y)) {
      free(_buf);
      _buf = nullptr;
      return openPair();
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
    if (_altQr) {
      c.textCentered(c.width() / 2, 64, "if it did not open by itself", TS_MED, true);
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, _net.url());
      c.textCentered(c.width() / 2, 406, "Open this in your browser", TS_MED, true, true);
      c.textCentered(c.width() / 2, 444, _net.url(), TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "BACK TO WIFI", false, TS_MED);
    } else {
      c.textCentered(c.width() / 2, 64, "the editor opens by itself", TS_MED,
                     true);
      const String wifi = _net.wifiPayload();
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, wifi.c_str());
      c.textCentered(c.width() / 2, 406, "Scan with your phone camera", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   key %s", _net.ssid(), _net.password());
      c.textCentered(c.width() / 2, 444, buf, TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "PAGE DIDN'T OPEN?", false,
               TS_MED);
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

  void tapPair(int x, int y) {
    using namespace nui;
    if (_got && !_answered) {  // the prompt owns the screen until it is answered
      if (PINNOW_BTN.hit(x, y) || KEEP_BTN.hit(x, y)) {
        _pinnedIt = PINNOW_BTN.hit(x, y);
        if (_pinnedIt) note::setPinned(_gotName);
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
