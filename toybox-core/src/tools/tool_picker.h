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
#include "import_screen.h"
#include "osk.h"
#include "tools_draw.h"

namespace pickui {
inline constexpr int MAX_ITEMS = plist::MAX_ITEMS;
inline constexpr int MAX_CHARS = plist::MAX_CHARS;
inline constexpr int MAX_BYTES = plist::MAX_BYTES;

// v2 (kit "Picker"): a reel that rolls like a slot machine and stops on one,
// what it landed on, Spin again | List; the list is its own page.
inline constexpr TRect REEL{20, 52, 440, 300};
inline constexpr int REEL_ROWS = 5, REEL_ROW_H = 60;
inline constexpr TRect SPIN{20, 476, 214, 72};
inline constexpr TRect LIST_BTN{246, 476, 214, 72};
inline constexpr TRect KEEP_BTN{20, 608, 214, 56};
inline constexpr TRect NOREP_BTN{246, 608, 214, 56};
inline constexpr int ROLL_STEPS = 7, ROLL_MS = 160;

// the list's name in the bar, with a little down-arrow: it opens the lists
inline constexpr TRect BAR_TAP{110, 0, 260, 50};

// the list page: eight rows a page, the page line under them (owner, 24 Nov:
// the "and 1 more" line used to sit on the Add button)
inline constexpr int LIST_X = 20, LIST_Y = 56, LIST_W = 440, ROW_H = 50;
inline constexpr int DEL_W = 56;
inline constexpr int LIST_ROWS = 8;
inline constexpr TRect PG_PREV{20, 460, 72, 52};
inline constexpr TRect PG_NEXT{388, 460, 72, 52};
inline constexpr TRect ADD_BTN{20, 528, 214, 64};
inline constexpr TRect PHONE_BTN{246, 528, 214, 64};
inline constexpr TRect CLEAR_BTN{20, 604, 440, 56};

// pairing screen, laid out like the flashcards one
inline constexpr TRect ALT_BTN = importui::ALT_BTN;
inline constexpr TRect DONE_BTN = importui::DONE_BTN;

// typing: the shared keyboard, the text above it
inline constexpr TRect TEXT_BOX{16, 92, 448, 60};

// the lists page
inline constexpr int LROW_Y = 56, LROW_H = 66;
inline constexpr TRect NEW_BTN{20, 604, 440, 60};
inline TRect lrowRect(int i) { return TRect{20, LROW_Y + i * LROW_H, 440, LROW_H - 6}; }
inline TRect lrenRect(int i) { return TRect{20 + 440 - 236, LROW_Y + i * LROW_H + 6, 112, LROW_H - 18}; }
inline TRect ldelRect(int i) { return TRect{20 + 440 - 116, LROW_Y + i * LROW_H + 6, 108, LROW_H - 18}; }

inline TRect rowRect(int i) { return TRect{LIST_X, LIST_Y + i * ROW_H, LIST_W, ROW_H - 4}; }
inline TRect delRect(int i) {
  return TRect{LIST_X + LIST_W - DEL_W, LIST_Y + i * ROW_H, DEL_W, ROW_H - 4};
}
}  // namespace pickui

class PickerTool : public ToolApp {
 public:
  enum class Screen : uint8_t { List, Items, Typing, Pair, Lists };
  enum class TypeFor : uint8_t { Item, NewList, Rename };

  const char* title() const override {
    if (_screen == Screen::Typing)
      return _typeFor == TypeFor::Item ? "ADD A CHOICE" : _typeFor == TypeFor::NewList ? "NEW LIST" : "RENAME LIST";
    if (_screen == Screen::Lists) return "LISTS";
    return _bar;
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _removeMode = prefs().getBool("pk_rm", false);
    memset(_usedBits, 0, sizeof(_usedBits));
    prefs().getBytes("pk_used", _usedBits, sizeof(_usedBits));
    migrate();
    _nLists = plist::listCount();
    _cur = prefs().getInt("pk_cur", 0);
    if (_cur < 0 || _cur >= _nLists) _cur = 0;
    load();
    _screen = Screen::List;
    _picked = -1;
    _reel = 0;
    _rollLeft = 0;
  }

  // The pairing screen has to pump the web server, so it wants the tick.
  bool wantsTick() const override { return _screen == Screen::Pair || _rollLeft > 0; }
  void tick() override {
    if (_rollLeft > 0) return rollStep();
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
    if (_screen == Screen::Items) {
      renderItems(c);
      return;
    }
    if (_screen == Screen::Lists) {
      renderLists(c);
      return;
    }
    using namespace pickui;
    barTop(c);
    char buf[48];

    // --- the reel: five rows, the middle one is the pick ------------------
    const TRect R = REEL;
    c.drawRect(R.x, R.y, R.w, R.h, 3, true);
    if (_count == 0) {
      c.textInBox(R.x, R.y, R.w, R.h, "no choices yet", TS_LARGE, true, true);
    } else {
      const bool landed = _picked >= 0 && _rollLeft == 0;
      for (int k = 0; k < REEL_ROWS; k++) {
        const int off = k - REEL_ROWS / 2;
        const int idx = ((_reel + off) % _count + _count) % _count;
        const int ry = R.y + 3 + k * REEL_ROW_H - 2;
        const bool mid = off == 0;
        if (k) c.fillRect(R.x + 12, ry, R.w - 24, 1, true);
        if (_count < REEL_ROWS && (off < -(_count / 2) || off > (_count - 1) / 2)) continue;
        if (mid && landed) c.fillRect(R.x + 3, ry + 1, R.w - 6, REEL_ROW_H - 2, true);
        const TSize sz = scriptFloor(_items[idx], mid ? TS_LARGE : TS_MED);
        const int tx = R.x + (mid ? 40 : 28);
        if (mid) c.text(R.x + 14, ry + (REEL_ROW_H - c.textHeight(TS_LARGE)) / 2, ">", TS_LARGE, !landed, true);
        c.textClipped(tx, ry + (REEL_ROW_H - c.textHeight(sz)) / 2, R.w - (mid ? 130 : 56), _items[idx], sz,
                      !(mid && landed), mid);
        if (mid) {
          snprintf(buf, sizeof(buf), "%d / %d", idx + 1, _count);
          c.text(R.x + R.w - 16 - c.textWidth(buf, TS_MED), ry + (REEL_ROW_H - c.textHeight(TS_MED)) / 2, buf,
                 TS_MED, !landed);
        }
      }
    }

    // --- what it landed on -------------------------------------------------
    if (_picked >= 0 && _rollLeft == 0 && _picked < _count) {
      c.textTrackedCentered(c.width() / 2, 372, "IT LANDED ON", TS_MED, true, true, 3);
      const int len = uni::count(_items[_picked]);
      const TSize sz = len <= 10 ? TS_HUGE : TS_LARGE;
      c.textCentered(c.width() / 2, 408, _items[_picked], scriptFloor(_items[_picked], sz), true, true);
    } else if (_rollLeft > 0) {
      c.textCentered(c.width() / 2, 392, "rolling...", TS_LARGE, true);
    } else {
      c.textCentered(c.width() / 2, 392, _count ? "spin, and it stops on one" : "add some choices first",
                     TS_MED, true);
    }

    c.button(SPIN.x, SPIN.y, SPIN.w, SPIN.h, _picked >= 0 ? "Spin again" : "Spin", _count > 0, TS_LARGE);
    c.button(LIST_BTN.x, LIST_BTN.y, LIST_BTN.w, LIST_BTN.h, "List", false, TS_LARGE);

    c.text(20, KEEP_BTN.y - 34, "EACH SPIN", TS_MED, true, true);
    choice(c, KEEP_BTN, "Any of them", !_removeMode);
    choice(c, NOREP_BTN, "No repeats", _removeMode);
    if (_removeMode)
      snprintf(buf, sizeof(buf), "%d of %d left this round", remaining(), _count);
    else
      snprintf(buf, sizeof(buf), "%d choices  -  edit them under List", _count);
    c.textCentered(c.width() / 2, 700, buf, TS_MED, true);
  }

  static void choice(ToolsCanvas& c, const TRect& r, const char* label, bool on) {
    c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
    c.textInBox(r.x, r.y, r.w, r.h, label, TS_MED, true, on);
  }

  void onTap(int x, int y) override {
    if (_screen == Screen::Typing) return tapKeyboard(x, y);
    if (_screen == Screen::Pair) return tapPair(x, y);
    if (_screen == Screen::Lists) return tapLists(x, y);
    if (_screen == Screen::Items) return tapItems(x, y);
    using namespace pickui;
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    if (_rollLeft > 0) return;  // let it stop
    if (BAR_TAP.hit(x, y)) return openLists();
    if (SPIN.hit(x, y) && _count > 0) return pick();
    if (LIST_BTN.hit(x, y)) {
      _screen = Screen::Items;
      _clearArm = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (KEEP_BTN.hit(x, y) || NOREP_BTN.hit(x, y)) {
      const bool want = NOREP_BTN.hit(x, y);
      if (want == _removeMode) return;
      _removeMode = want;
      prefs().putBool("pk_rm", _removeMode);
      memset(_used, 0, sizeof(_used));
      saveUsed();
      host().beep(1);
      host().refresh(false);
    }
  }

  // UP or DOWN spins too: the reel is something you pull. On the list page
  // they turn its pages.
  bool onButton(SideBtn b) override {
    if (b != SideBtn::Up && b != SideBtn::Down) return false;
    if (_screen == Screen::Items) {
      turnPage(b == SideBtn::Down ? 1 : -1);
      return true;
    }
    if (_screen != Screen::List) return false;
    if (_count > 0 && _rollLeft == 0) pick();
    return true;
  }
  void onSwipe(int dx, int dy) override {
    (void)dy;
    if (_screen == Screen::Items && dx != 0) turnPage(dx < 0 ? 1 : -1);
  }
  void onHold(int x, int y) override {
    if (_screen != Screen::Lists) return;
    for (int i = 0; i < _nLists; i++)
      if (pickui::lrowRect(i).hit(x, y)) {
        _armList = i;
        host().beep(0);
        host().refresh(false);
        return;
      }
  }

  int hostPicked() const { return _picked; }
  bool hostNetHome() const { return _net.home(); }
  bool hostNetUp() const { return _netOk; }
  int hostCount() const { return _count; }
  bool hostRolling() const { return _rollLeft > 0; }
  int hostLists() const { return _nLists; }
  int hostCur() const { return _cur; }
  const char* hostListName(int i) const { return i >= 0 && i < _nLists ? _names[i] : ""; }
  int hostPage() const { return _page; }
  int hostRemaining() const { return remaining(); }
  // What the phone page's save and its list menu do, without the radio.
  pweb::ListServer::Saved hostPhoneSave(const char* text, int l, const char* name) {
    return applyFromPhone(String(text), l, String(name));
  }
  String hostListsJson() { return lister()(); }
  void hostTypeName(const char* s) {  // the keyboard, without the keys
    snprintf(_draft, sizeof(_draft), "%s", s);
  }

  // --- the list page --------------------------------------------------------
  int pages() const { return _count <= pickui::LIST_ROWS ? 1 : (_count + pickui::LIST_ROWS - 1) / pickui::LIST_ROWS; }

  void renderItems(ToolsCanvas& c) {
    using namespace pickui;
    barTop(c);
    char buf[40];
    if (_page >= pages()) _page = pages() - 1;
    if (_count == 0) c.textInBox(LIST_X, LIST_Y, LIST_W, 3 * ROW_H, "no choices yet", TS_LARGE, true, true);
    const int first = _page * LIST_ROWS;
    for (int k = 0; k < LIST_ROWS && first + k < _count; k++) {
      const int i = first + k;
      const TRect r = rowRect(k);
      c.fillRect(r.x, r.y + r.h + 1, r.w, 1, true);
      const TSize nsz = scriptFloor(_items[i], TS_MED);
      c.textClipped(r.x + 8, r.y + (r.h - c.textHeight(nsz)) / 2, r.w - DEL_W - 16, _items[i], nsz, true);
      const TRect d = delRect(k);
      c.textInBox(d.x, d.y, d.w, d.h, "x", TS_LARGE, true);
    }
    if (pages() > 1) {
      snprintf(buf, sizeof(buf), "page %d of %d", _page + 1, pages());
      c.textInBox(PG_PREV.x + PG_PREV.w, PG_PREV.y, PG_NEXT.x - PG_PREV.x - PG_PREV.w, PG_PREV.h, buf, TS_MED, true);
      if (_page > 0) {
        c.drawRect(PG_PREV.x, PG_PREV.y, PG_PREV.w, PG_PREV.h, 1, true);
        c.textInBox(PG_PREV.x, PG_PREV.y, PG_PREV.w, PG_PREV.h, "<", TS_LARGE, true, true);
      }
      if (_page + 1 < pages()) {
        c.drawRect(PG_NEXT.x, PG_NEXT.y, PG_NEXT.w, PG_NEXT.h, 1, true);
        c.textInBox(PG_NEXT.x, PG_NEXT.y, PG_NEXT.w, PG_NEXT.h, ">", TS_LARGE, true, true);
      }
    }
    c.button(ADD_BTN.x, ADD_BTN.y, ADD_BTN.w, ADD_BTN.h, "Add one", false, TS_MED);
    c.button(PHONE_BTN.x, PHONE_BTN.y, PHONE_BTN.w, PHONE_BTN.h, "From your phone", false, TS_MED);
    if (_clearArm) chrome2::armed(c, CLEAR_BTN, "Clear every choice?");
    else c.button(CLEAR_BTN.x, CLEAR_BTN.y, CLEAR_BTN.w, CLEAR_BTN.h, "Clear all", false, TS_MED);
    snprintf(buf, sizeof(buf), "%d of %d choices", _count, MAX_ITEMS);
    c.textCentered(c.width() / 2, 700, buf, TS_MED, true);
    c.textCentered(c.width() / 2, 734, pages() > 1 ? "x takes one out  -  UP / DOWN pages" : "x takes one out",
                   TS_MED, true);
  }

  void turnPage(int d) {
    const int np = _page + d;
    if (np < 0 || np >= pages()) {
      host().beep(2);
      return;
    }
    _page = np;
    _clearArm = false;
    host().beep(0);
    host().refresh(false);
  }

  void tapItems(int x, int y) {
    using namespace pickui;
    if (host().isBackTap(x, y)) {
      _screen = Screen::List;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (BAR_TAP.hit(x, y)) return openLists();
    const bool wasArmed = _clearArm;
    _clearArm = false;
    const int first = _page * LIST_ROWS;
    for (int k = 0; k < LIST_ROWS && first + k < _count; k++)
      if (delRect(k).hit(x, y)) return removeItem(first + k);
    if (pages() > 1 && PG_PREV.hit(x, y)) return turnPage(-1);
    if (pages() > 1 && PG_NEXT.hit(x, y)) return turnPage(1);
    if (ADD_BTN.hit(x, y)) {
      if (_count >= MAX_ITEMS) {
        host().beep(2);
        return;
      }
      startTyping(TypeFor::Item);
      return;
    }
    if (PHONE_BTN.hit(x, y)) return openPair();
    if (CLEAR_BTN.hit(x, y)) {
      if (!wasArmed) {
        _clearArm = true;  // two taps: a whole list is a lot to lose
        host().beep(0);
        host().refresh(false);
        return;
      }
      _count = 0;
      _picked = -1;
      _page = 0;
      memset(_used, 0, sizeof(_used));
      save();
      host().beep(2);
      host().refreshUi();
      return;
    }
    if (wasArmed) host().refresh(false);
  }

  // --- the lists -------------------------------------------------------------
  void openLists() {
    _listsFrom = _screen;
    _screen = Screen::Lists;
    _armList = -1;
    refreshNames();
    host().beep(1);
    host().refreshUi();
  }

  void renderLists(ToolsCanvas& c) {
    using namespace pickui;
    host().topBar(title());
    char buf[40];
    for (int i = 0; i < _nLists; i++) {
      const TRect r = lrowRect(i);
      const bool cur = i == _cur, armed = i == _armList;
      if (cur)
        c.drawRect(r.x, r.y, r.w, r.h, 3, true);
      else
        c.fillRect(r.x, r.y + r.h + 2, r.w, 1, true);
      const TSize nsz = scriptFloor(_names[i], TS_MED);
      const int nameW = armed ? r.w - 260 : r.w - 100;
      c.textClipped(r.x + 16, r.y + (r.h - c.textHeight(nsz)) / 2, nameW, _names[i], nsz, true, cur);
      if (armed) {
        const TRect rn = lrenRect(i), dl = ldelRect(i);
        c.button(rn.x, rn.y, rn.w, rn.h, "Rename", false, TS_MED);
        if (_nLists > 1) c.button(dl.x, dl.y, dl.w, dl.h, "Delete", true, TS_MED);
      } else {
        snprintf(buf, sizeof(buf), "%d", _listN[i]);
        c.text(r.x + r.w - 44 - c.textWidth(buf, TS_MED), r.y + (r.h - c.textHeight(TS_MED)) / 2, buf, TS_MED,
               true);
        c.text(r.x + r.w - 28, r.y + (r.h - c.textHeight(TS_MED)) / 2, ">", TS_MED, true, true);
      }
    }
    if (_nLists < plist::MAX_LISTS)
      c.button(NEW_BTN.x, NEW_BTN.y, NEW_BTN.w, NEW_BTN.h, "New list", false, TS_MED);
    c.textCentered(c.width() / 2, 700, "tap one to use it  -  hold to rename or delete", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d of %d lists", _nLists, plist::MAX_LISTS);
    c.textCentered(c.width() / 2, 734, buf, TS_MED, true);
  }

  void tapLists(int x, int y) {
    using namespace pickui;
    if (host().isBackTap(x, y)) {
      _screen = _listsFrom;
      host().beep(1);
      host().refreshUi();
      return;
    }
    const int armed = _armList;
    _armList = -1;
    if (armed >= 0) {
      if (lrenRect(armed).hit(x, y)) {
        _editList = armed;
        startTyping(TypeFor::Rename);
        return;
      }
      if (_nLists > 1 && ldelRect(armed).hit(x, y)) return deleteList(armed);
    }
    for (int i = 0; i < _nLists; i++)
      if (lrowRect(i).hit(x, y)) {
        if (armed >= 0) break;  // a tap away from the buttons just disarms
        switchTo(i);
        _screen = Screen::List;
        host().beep(1);
        host().refreshUi();
        return;
      }
    if (_nLists < plist::MAX_LISTS && NEW_BTN.hit(x, y)) {
      startTyping(TypeFor::NewList);
      return;
    }
    if (armed >= 0) host().refresh(false);
  }

  void deleteList(int i) {
    packUsed();
    plist::removeList(i, _nLists);
    for (int j = i; j + 1 < plist::MAX_LISTS; j++) _usedBits[j] = _usedBits[j + 1];
    _usedBits[plist::MAX_LISTS - 1] = 0;
    _nLists--;
    if (_cur > i || _cur >= _nLists) _cur = _cur > 0 ? _cur - 1 : 0;
    prefs().putInt("pk_cur", _cur);
    prefs().putBytes("pk_used", _usedBits, sizeof(_usedBits));
    load();
    refreshNames();
    host().beep(2);
    host().refreshUi();
  }

  void switchTo(int i) {
    if (i == _cur) return;
    packUsed();
    _cur = i;
    prefs().putInt("pk_cur", _cur);
    load();
  }

 private:
  int remaining() const {
    int n = 0;
    for (int i = 0; i < _count; i++)
      if (!_used[i]) n++;
    return n;
  }

  // Choose now, then let the reel roll a few rows and stop on it: a few
  // quiet partial refreshes, the last one with the pick inverted.
  void pick() {
    int idx;
    if (_removeMode) {
      if (remaining() == 0) memset(_used, 0, sizeof(_used));  // new round
      do {
        idx = (int)(esp_random() % (uint32_t)_count);
      } while (_used[idx]);
      _used[idx] = 1;
      saveUsed();
    } else {
      idx = (int)(esp_random() % (uint32_t)_count);
    }
    _target = idx;
    _picked = idx;
    _rollLeft = pickui::ROLL_STEPS;
    _reel = ((idx - pickui::ROLL_STEPS) % _count + _count) % _count;
    _rollMs = host().nowMs();
    host().beep(0);
    host().refresh(false);
  }

  void rollStep() {
    if (host().nowMs() - _rollMs < (uint32_t)pickui::ROLL_MS) return;
    _rollMs = host().nowMs();
    _reel = (_reel + 1) % (_count ? _count : 1);
    if (--_rollLeft == 0) {
      _reel = _target;
      host().beep(3);
    }
    host().refresh(false);
  }

  void removeItem(int i) {
    for (int j = i; j < _count - 1; j++) {
      memcpy(_items[j], _items[j + 1], pickui::MAX_BYTES + 1);
      _used[j] = _used[j + 1];
    }
    _count--;
    _picked = -1;
    _reel = 0;
    save();
    host().beep(1);
    host().refreshUi();
  }

  // --- keyboard (the shared one) ------------------------------------------
  // One keyboard for a choice, a new list's name and a list's new name.
  void startTyping(TypeFor f) {
    _typeFor = f;
    _screen = Screen::Typing;
    _draft[0] = 0;
    if (f == TypeFor::Rename) snprintf(_draft, sizeof(_draft), "%s", _names[_editList]);
    _kb = osk::State{};
    host().beep(1);
    host().refreshUi();
  }

  void renderKeyboard(ToolsCanvas& c) {
    using namespace pickui;
    host().topBar(title());
    const bool item = _typeFor == TypeFor::Item;
    c.text(16, 52, item ? "A new choice" : "The list's name", TS_MED, true);
    c.drawRect(TEXT_BOX.x, TEXT_BOX.y, TEXT_BOX.w, TEXT_BOX.h, 2, true);
    char shown[MAX_BYTES + 2];
    snprintf(shown, sizeof(shown), "%s_", _draft);
    c.textClipped(TEXT_BOX.x + 12, TEXT_BOX.y + (TEXT_BOX.h - c.textHeight(TS_MED)) / 2, TEXT_BOX.w - 24, shown,
                  scriptFloor(_draft, TS_MED), true);
    // What is already there, as a reminder.
    c.text(16, 176, item ? "IN THE LIST" : "YOUR LISTS", TS_MED, true, true);
    if (item) {
      const int shownN = _count < 6 ? _count : 6;
      for (int i = 0; i < shownN; i++)
        c.textClipped(16, 212 + i * 44, 448, _items[_count - 1 - i], scriptFloor(_items[_count - 1 - i], TS_MED),
                      true);
    } else {
      const int shownN = _nLists < 6 ? _nLists : 6;
      for (int i = 0; i < shownN; i++)
        c.textClipped(16, 212 + i * 44, 448, _names[i], scriptFloor(_names[i], TS_MED), true);
    }
    osk::draw(c, _kb);
  }

  void tapKeyboard(int x, int y) {
    using namespace pickui;
    const bool item = _typeFor == TypeFor::Item;
    if (host().isBackTap(x, y)) {
      _screen = item ? Screen::Items : Screen::Lists;
      host().beep(1);
      host().refreshUi();
      return;
    }
    const int cap = (item ? MAX_CHARS : plist::NAME_CHARS) + 1;
    const int code = osk::tap(_kb, _draft, cap, x, y, host().nowMs());
    if (code == osk::K_NONE) return;
    if (code == osk::K_DONE) return finishTyping();
    host().beep(0);
    host().refresh(false);  // every key a plain partial
  }

  void finishTyping() {
    using namespace pickui;
    if (!_draft[0]) {
      host().beep(2);
      return;
    }
    if (_typeFor == TypeFor::Item) {
      if (_count < MAX_ITEMS) {
        memcpy(_items[_count], _draft, MAX_BYTES + 1);
        _used[_count] = 0;
        _count++;
        _page = pages() - 1;  // where the new one is
        save();
      }
      _screen = Screen::Items;
    } else if (_typeFor == TypeFor::NewList) {
      if (_nLists < plist::MAX_LISTS) {
        plist::Name nm;
        plist::cleanName(_draft, nm);
        packUsed();
        plist::writeList(_nLists, nm, _items, 0);
        _usedBits[_nLists] = 0;
        _nLists++;
        _cur = _nLists - 1;
        prefs().putInt("pk_cur", _cur);
        load();
      }
      _screen = Screen::Items;  // a new list wants its choices next
    } else {
      plist::Name nm;
      plist::cleanName(_draft, nm);
      plist::Name old;
      plist::Item* tmp = (plist::Item*)ps_malloc(sizeof(plist::Item) * plist::MAX_ITEMS);
      int n = 0;
      if (tmp && plist::readList(_editList, old, tmp, &n)) plist::writeList(_editList, nm, tmp, n);
      free(tmp);
      if (_editList == _cur) load();
      refreshNames();
      _screen = Screen::Lists;
    }
    host().beep(1);
    host().refreshUi();
  }

  // --- phone -------------------------------------------------------------
  // The server is handed a reader and a writer rather than a copy of the list,
  // so the page always opens on what is actually on the device and a save lands
  // straight in NVS. Editing on the phone and on the keyboard cannot diverge.
  void openPair() {
    _altQr = false;
    _gotCount = 0;
    _screen = Screen::Pair;
    // The home Wi-Fi first when one is saved (owner): the phone keeps its own
    // network. The Sticky's own access point is the backup.
    _netNote = nullptr;
    _netOk = false;
    if (homenet::haveWifi(prefs())) {
      homenet::wifiName(prefs(), _joinName, sizeof(_joinName));
      _joining = true;
      host().refresh(false);  // "Joining ..." while the radio looks
      _netOk = _net.startHome(prefs(), "/list", provider(), lister(), receiver());
      _joining = false;
      if (!_netOk) _netNote = "the home Wi-Fi did not answer";
    }
    if (!_netOk) _netOk = _net.start(provider(), lister(), receiver());

    host().beep(_netOk ? 1 : 2);
    host().refresh(true);
  }

  pweb::ListServer::Provider provider() {
    return [this](int l) {
      if (l < 0 || l >= _nLists || l == _cur) return plist::toText(_items, _count);
      plist::Item* tmp = (plist::Item*)ps_malloc(sizeof(plist::Item) * plist::MAX_ITEMS);
      if (!tmp) return String();
      plist::Name nm;
      int n = 0;
      plist::readList(l, nm, tmp, &n);
      String out = plist::toText(tmp, n);
      free(tmp);
      return out;
    };
  }
  pweb::ListServer::Lister lister() {
    return [this] {
      refreshNames();
      char num[16];
      snprintf(num, sizeof(num), "%d", _cur);
      String j = "{\"cur\":";
      j += num;
      j += ",\"lists\":[";
      for (int i = 0; i < _nLists; i++) {
        if (i) j += ',';
        j += "{\"name\":\"";
        for (const char* p = _names[i]; *p; p++) {
          if (*p == '"' || *p == '\\') j += '\\';
          j += *p;
        }
        j += "\",\"n\":";
        snprintf(num, sizeof(num), "%d", _listN[i]);
        j += num;
        j += '}';
      }
      j += "]}";
      return j;
    };
  }
  pweb::ListServer::Receiver receiver() {
    return [this](const String& text, int l, const String& name) { return applyFromPhone(text, l, name); };
  }

  // A save from the phone lands in the list it names (a new one when asked),
  // and that list comes onto the screen.
  pweb::ListServer::Saved applyFromPhone(const String& text, int l, const String& name) {
    if (l == -2 || l >= _nLists) l = _cur;
    if (l == -1) {
      if (_nLists >= plist::MAX_LISTS) return {0, -1};
      plist::Name nm;
      plist::cleanName(name.c_str(), nm);
      packUsed();
      plist::writeList(_nLists, nm, _items, 0);
      _usedBits[_nLists] = 0;
      l = _nLists++;
    }
    switchTo(l);
    _count = plist::fromText(text.c_str(), _items);
    _picked = -1;
    _page = 0;
    memset(_used, 0, sizeof(_used));
    save();
    refreshNames();
    _gotCount = _count;
    return {_count, _cur};
  }

  void closePair() {
    _net.stop();
    _netOk = false;
    _screen = Screen::Items;
    load();
    host().beep(1);
    host().refreshUi();
  }

  void renderPair(ToolsCanvas& c) {
    using namespace pickui;
    host().topBar("FROM YOUR PHONE");
    if (_joining) return importui::drawJoining(c, _joinName);
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start Wi-Fi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Back", true, TS_LARGE);
      return;
    }
    char buf[64];
    if (_gotCount > 0) {
      snprintf(buf, sizeof(buf), "%d", _gotCount);
      c.textCentered(c.width() / 2, 120, buf, TS_HUGE, true, true);
      c.textCentered(c.width() / 2, 176, _gotCount == 1 ? "choice on the Sticky" : "choices on the Sticky", TS_MED,
                     true);
      for (int i = 0; i < _count && i < 7; i++)
        c.textCentered(c.width() / 2, 236 + i * 40, _items[i], scriptFloor(_items[i], TS_MED), true);
      if (_count > 7) c.textCentered(c.width() / 2, 236 + 7 * 40, "...", TS_MED, true);
      c.textCentered(c.width() / 2, 620, "save again on the phone to change it", TS_MED, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Done", true, TS_LARGE);
      return;
    }
    static const importui::Words W{"Scan to edit the list on your phone",
                                   {"One choice a line", "Paste a whole list at once", "It shows here when saved"}};
    importui::draw(c, _net, _altQr, W, _netNote);
  }

  void tapPair(int x, int y) {
    using namespace pickui;
    if (DONE_BTN.hit(x, y)) return closePair();
    if (_netOk && _gotCount == 0 && ALT_BTN.hit(x, y)) {
    if (_net.home()) {
      // Backup: the Sticky's own access point.
      _net.stop();
      _netNote = nullptr;
      _altQr = false;
      _netOk = _net.start(provider(), lister(), receiver());
      host().beep(_netOk ? 1 : 2);
      host().refresh(true);
      return;
    }
      _altQr = !_altQr;
      host().beep(0);
      host().refresh(true);
    }
  }

  // --- persistence -------------------------------------------------------
  // Each list is a file holding the text the phone posts, so there is one
  // parser for both paths. Which list is on the screen, and each list's
  // no-repeats round (owner: a round belongs to its list), live in NVS.
  void load() {
    _count = 0;
    if (!plist::readList(_cur, _names[_cur], _items, &_count)) _count = 0;
    memset(_used, 0, sizeof(_used));
    for (int i = 0; i < _count; i++) _used[i] = (uint8_t)((_usedBits[_cur] >> i) & 1u);
    _picked = -1;
    _reel = 0;
    _page = 0;
    makeBar();
  }

  void save() {
    plist::writeList(_cur, _names[_cur], _items, _count);
    _listN[_cur] = _count;
    saveUsed();
  }

  void packUsed() {
    uint64_t b = 0;
    for (int i = 0; i < _count && i < 64; i++)
      if (_used[i]) b |= (uint64_t)1 << i;
    _usedBits[_cur] = b;
  }
  void saveUsed() {
    packUsed();
    prefs().putBytes("pk_used", _usedBits, sizeof(_usedBits));
  }

  void refreshNames() {
    _nLists = plist::listCount();
    for (int i = 0; i < _nLists; i++)
      if (!plist::readList(i, _names[i], nullptr, &_listN[i])) _listN[i] = 0;
    _listN[_cur] = _count;
  }

  // Before named lists the one list lived in NVS ("pk_items"): it becomes
  // "My list", and a first start makes an empty one.
  void migrate() {
    if (plist::listCount() > 0) return;
    // The old list held ten at most: its text fits a few hundred bytes.
    constexpr size_t OLD = (size_t)(plist::MAX_BYTES + 1) * 10 + 8;
    plist::Item* tmp = (plist::Item*)ps_malloc(sizeof(plist::Item) * plist::MAX_ITEMS + OLD);
    if (!tmp) return;
    char* blob = (char*)(tmp + plist::MAX_ITEMS);
    blob[0] = 0;
    int n = 0;
    if (prefs().isKey("pk_items")) {
      prefs().getString("pk_items", blob, OLD);
      n = plist::fromText(blob, tmp);
    }
    if (plist::writeList(0, "My list", tmp, n)) prefs().remove("pk_items");
    free(tmp);
    prefs().putInt("pk_cur", 0);
  }

  // The list's name in tracked caps for the bar.
  void makeBar() {
    int j = 0;
    for (const char* p = _names[_cur]; *p && j < (int)sizeof(_bar) - 1; p++)
      _bar[j++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    _bar[j] = 0;
  }

  // The bar, with a small down-arrow after the name: it opens the lists.
  void barTop(ToolsCanvas& c) {
    host().topBar(_bar);
    const int w = c.width(), room = w - 2 * 110 - 8;
    TSize sz = scriptFloor(_bar, TS_MED);
    int sp = 2;
    if (c.textTrackedWidth(_bar, sz, true, sp) > room) sp = 0;
    if (c.textTrackedWidth(_bar, sz, true, sp) > room && sz == TS_MED) {
      sz = TS_SMALL;
      sp = 1;
    }
    int tw = c.textTrackedWidth(_bar, sz, true, sp);
    if (tw > room) tw = room;
    int ax = w / 2 + tw / 2 + 10;
    if (ax > w - 110 - 14) ax = w - 110 - 14;
    for (int r = 0; r < 6; r++) c.fillRect(ax + r, 17 + r, 12 - 2 * r, 1, true);
  }

  plist::Item _items[plist::MAX_ITEMS] = {};
  uint8_t _used[pickui::MAX_ITEMS] = {};
  int _count = 0;
  int _picked = -1;
  bool _removeMode = false;
  int _page = 0;

  plist::Name _names[plist::MAX_LISTS] = {};
  int _listN[plist::MAX_LISTS] = {};
  uint64_t _usedBits[plist::MAX_LISTS] = {};
  int _nLists = 0, _cur = 0, _armList = -1, _editList = 0;
  char _bar[plist::NAME_BYTES + 1] = "MY LIST";

  Screen _screen = Screen::List, _listsFrom = Screen::List;
  TypeFor _typeFor = TypeFor::Item;
  char _draft[pickui::MAX_BYTES + 1] = {};
  osk::State _kb;
  bool _joining = false;
  const char* _netNote = nullptr;
  char _joinName[34] = {};

  bool _clearArm = false;
  int _reel = 0, _target = -1, _rollLeft = 0;
  uint32_t _rollMs = 0;

  pweb::ListServer _net;
  bool _netOk = false, _altQr = false;
  int _gotCount = 0;
};
