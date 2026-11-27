// Calendar (v2, kit "Calendar").
//
// A month to look at, the selected day's items under it, and a way to add one:
// a title (typed on the small keyboard), a date and time, whether it repeats,
// and whether it pops up over Home when its time comes. Nothing here rings --
// see cal_store.h for why.
#pragma once
#include "../chrome.h"
#include "cal_store.h"
#include "osk.h"
#include "tools_draw.h"
#include "numpad.h"

namespace calui {
inline constexpr int GRID_X = 16, CELL_W = 64, CELL_H = 56, WD_Y = 112, GRID_Y = 140;
inline constexpr TRect PREV{16, 52, 64, 52};
inline constexpr TRect NEXT{SCREEN_W - 80, 52, 64, 52};
inline TRect cell(int row, int col) {
  return TRect{GRID_X + col * CELL_W, GRID_Y + row * CELL_H, CELL_W, CELL_H};
}
inline constexpr int LIST_Y = GRID_Y + 6 * CELL_H + 16;  // 492
// v2 (owner, 22 Nov): the month's name opens a month-and-year picker, and a
// small Today in the bar's right corner comes straight back to today.
inline constexpr TRect TITLE_TAP{88, 52, SCREEN_W - 176, 52};
inline constexpr TRect TODAY{SCREEN_W - 110, 0, 110, 50};
inline constexpr TRect Y_PREV{16, 64, 80, 64};
inline constexpr TRect Y_NEXT{SCREEN_W - 96, 64, 80, 64};
// The year between the arrows opens a page of twelve years, tiled the same.
inline constexpr TRect YEAR_TAP{104, 64, SCREEN_W - 208, 64};
inline TRect monthTile(int m) {  // 0..11
  const int w = (SCREEN_W - 32 - 2 * 10) / 3;
  return TRect{16 + (m % 3) * (w + 10), 160 + (m / 3) * (112 + 10), w, 112};
}
inline constexpr int ROW_H = 58;
inline TRect itemRow(int i) { return TRect{16, LIST_Y + 40 + i * ROW_H, SCREEN_W - 32, ROW_H - 6}; }
inline constexpr int LIST_ROWS = 3;
inline constexpr TRect ADD{16, SCREEN_H - 84, SCREEN_W - 32, 68};

// The add page.
inline constexpr TRect TITLE_BOX{16, 70, SCREEN_W - 32, 64};
inline constexpr int A_ROW_Y = 160, A_ROW_STEP = 92;
inline TRect aRow(int i) { return TRect{16, A_ROW_Y + i * A_ROW_STEP, SCREEN_W - 32, A_ROW_STEP - 12}; }
// Two - value + pairs side by side (hours, minutes), or one wide pair from
// the left minus to the right plus (date, days ahead).
inline TRect aMinus(int i, int which) {  // which: 0 = left pair, 1 = right pair
  const TRect r = aRow(i);
  return TRect{which ? r.x + r.w - 160 : r.x + 120, r.y + 28, 52, 48};
}
inline TRect aPlus(int i, int which) {
  const TRect r = aRow(i);
  return TRect{which ? r.x + r.w - 52 : r.x + 220, r.y + 28, 52, 48};
}
inline TRect aSeg(int i, int k, int n) {
  const TRect r = aRow(i);
  const int w = (r.w - 140) / n;
  return TRect{r.x + 140 + k * w, r.y + 28, w, 48};
}
// Cancel and Save share the bottom bar.
inline constexpr TRect CANCEL_BTN{16, SCREEN_H - 84, (SCREEN_W - 40) / 3, 68};
inline constexpr TRect SAVE{24 + (SCREEN_W - 40) / 3, SCREEN_H - 84, SCREEN_W - 40 - (SCREEN_W - 40) / 3, 68};
// The item page.
inline constexpr TRect EDIT_BTN{16, 436, SCREEN_W - 32, 68};
inline constexpr TRect DONE_BTN{16, 520, SCREEN_W - 32, 68};
// Typing a time: the digits fill HH then MM.
inline TRect padKey(int i) { return numpad::key(i); }
// The Time row's "All day" chip, under its label.
inline TRect allDayChip() {
  const TRect r = aRow(1);
  return TRect{r.x, r.y + 28, 100, 48};
}
// The Repeat row is a summary that opens its own page.
inline TRect repeatRow() { return aRow(2); }
// The repeat page: four choices, then when it ends.
inline TRect repOpt(int i) { return TRect{16, 64 + i * 76, SCREEN_W - 32, 66}; }
inline constexpr int END_Y = 64 + 4 * 76 + 24;  // 392
inline TRect endSeg(int k) {
  const int w = (SCREEN_W - 32) / 2;
  return TRect{16 + k * w, END_Y + 30, w, 56};
}
// The end date, shown in a box; tap it to type year, month, day.
inline constexpr TRect END_BOX{16, END_Y + 130, SCREEN_W - 32, 64};
inline constexpr TRect REP_DONE{16, SCREEN_H - 84, SCREEN_W - 32, 68};

// The time on the add page: one box, tapped to type it on the keypad.
inline TRect timeBox() {
  const TRect r = aRow(1);
  return TRect{r.x + 120, r.y + 24, r.w - 120, 56};
}
inline constexpr TRect DEL_BTN{16, 604, SCREEN_W - 32, 68};
}  // namespace calui

class CalendarTool : public ToolApp {
 public:
  const char* title() const override { return "CALENDAR"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    cal::load(_store);
    today(_ty, _tm, _td);
    _vy = _ty;
    _vm = _tm;
    _sd = _td;
    _page = Page::Month;
  }

  void render(ToolsCanvas& c) override {
    switch (_page) {
      case Page::Month: renderMonth(c); break;
      case Page::Item: renderItem(c); break;
      case Page::Add: renderAdd(c); break;
      case Page::Type: renderType(c); break;
      case Page::Time: renderTime(c); break;
      case Page::Pick: renderPick(c); break;
      case Page::Years: renderYears(c); break;
      case Page::Repeat: renderRepeat(c); break;
      case Page::Until: renderUntil(c); break;
    }
  }

  void onTap(int x, int y) override {
    switch (_page) {
      case Page::Month: tapMonth(x, y); break;
      case Page::Item: tapItem(x, y); break;
      case Page::Add: tapAdd(x, y); break;
      case Page::Type: tapType(x, y); break;
      case Page::Time: tapTime(x, y); break;
      case Page::Pick: tapPick(x, y); break;
      case Page::Years: tapYears(x, y); break;
      case Page::Repeat: tapRepeat(x, y); break;
      case Page::Until: tapUntil(x, y); break;
    }
  }

  // The side buttons (owner, 22 Nov): UP the month before, DOWN the month
  // after -- or the year, on the month picker.
  bool onButton(SideBtn b) override {
    if (b != SideBtn::Up && b != SideBtn::Down) return false;
    const int dir = b == SideBtn::Up ? 1 : -1;  // upper = next, lower = previous (owner)
    if (_page == Page::Month) {
      stepMonth(dir);
      return true;
    }
    if (_page == Page::Years) {
      stepYears(dir);
      return true;
    }
    if (_page == Page::Pick) {
      _pickYear += dir;
      if (_pickYear < 2000) _pickYear = 2000;
      if (_pickYear > 2099) _pickYear = 2099;
      host().beep(0);
      host().refresh(false);
      return true;
    }
    return false;
  }

  void onSwipe(int dx, int dy) override {
    (void)dy;
    if (_page != Page::Month || dx == 0) return;
    stepMonth(dx < 0 ? 1 : -1);
  }

  // For the harness.
  int hostCount() const { return _store.n; }
  int hostPage() const { return (int)_page; }
  const char* hostTitle(int i) const { return _store.items[i].title; }
  int hostHour(int i) const { return _store.items[i].hour; }
  int hostViewYear() const { return _vy; }
  cal::Item hostItem(int i) const { return _store.items[i]; }
  void hostRemove(int i) {
    cal::removeAt(_store, i);
    cal::save(_store);
  }
  int hostViewMonth() const { return _vm; }
  int hostMinute(int i) const { return _store.items[i].minute; }
  uint32_t hostDraftUntil() const { return _draft.until; }
  int hostDraftHour() const { return _draft.hour; }
  int hostDraftMinute() const { return _draft.minute; }

 private:
  enum class Page : uint8_t { Month, Item, Add, Type, Time, Pick, Repeat, Until, Years };
  Page _page = Page::Month;
  cal::Store _store;
  int _ty = 2026, _tm = 1, _td = 1;  // today, as well as the clock knows it
  int _vy = 2026, _vm = 1;           // the month on show
  int _sd = 1;                       // the selected day in it
  int _sel = -1;                     // the item open on the item page
  bool _delArmed = false;
  cal::Item _draft{};
  osk::State _kb;
  int _editIdx = -1;
  int _ustep = 0, _uy = 0, _um = 0;  // typing the end date     // the item being edited, or -1 for a new one
  char _digits[5] = {};  // typed time, HHMM as it comes
  int _ndigits = 0;
  int _pickYear = 2026;
  int _yearBase = 2016;  // the first of the twelve years shown

  bool today(int& y, int& m, int& d) {
    if (host().clockDate(y, m, d) && m >= 1 && m <= 12) return true;
    y = 2026;
    m = 1;
    d = 1;
    return false;
  }

  static const char* monthName(int m) {
    static const char* N[12] = {"January", "February", "March",     "April",   "May",      "June",
                                "July",    "August",   "September", "October", "November", "December"};
    return N[(m - 1) % 12];
  }

  void stepMonth(int dir) {
    _vm += dir;
    if (_vm < 1) {
      _vm = 12;
      _vy--;
    } else if (_vm > 12) {
      _vm = 1;
      _vy++;
    }
    _sd = (_vy == _ty && _vm == _tm) ? _td : 1;
    host().beep(0);
    host().refresh(false);
  }

  // The items on the selected day, as store indexes.
  int dayItems(int* idx, int cap) {
    int k = 0;
    for (int i = 0; i < _store.n && k < cap; i++)
      if (cal::on(_store.items[i], _vy, _vm, _sd)) idx[k++] = i;
    return k;
  }

  static void timeOf(const cal::Item& it, char* out, int cap) {
    if (it.allDay) snprintf(out, (size_t)cap, "all day");
    else snprintf(out, (size_t)cap, "%02d:%02d", it.hour, it.minute);
  }

  // "Every week until 30 Nov 2026", "Once", ...
  static void repeatText(const cal::Item& it, char* out, int cap) {
    static const char* R[cal::REPEATS] = {"Once", "Every day", "Every week", "Every year"};
    const char* r = R[it.repeat < cal::REPEATS ? it.repeat : 0];
    if (it.repeat == cal::REPEAT_NONE || !it.until) {
      snprintf(out, (size_t)cap, "%s", r);
      return;
    }
    const int uy = (int)(it.until / 10000), um = (int)(it.until / 100 % 100), ud = (int)(it.until % 100);
    snprintf(out, (size_t)cap, "%s until %d %.3s %d", r, ud, monthName(um), uy);
  }

  // --- the month --------------------------------------------------------------
  void renderMonth(ToolsCanvas& c) {
    using namespace calui;
    const bool onToday = _vy == _ty && _vm == _tm && _sd == _td;
    host().topBar("CALENDAR", false, "HUB", onToday ? nullptr : "Today");
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d", monthName(_vm), _vy);
    c.textCentered(SCREEN_W / 2, 64, buf, TS_LARGE, true, true);
    // A small mark under the name says it opens something.
    {
      const int tw = c.textWidth(buf, TS_LARGE, true);
      c.fillRect(SCREEN_W / 2 - tw / 2, 64 + c.textHeight(TS_LARGE) + 1, tw, 1, true);
    }
    c.button(PREV.x, PREV.y, PREV.w, PREV.h, "<", false, TS_MED);
    c.button(NEXT.x, NEXT.y, NEXT.w, NEXT.h, ">", false, TS_MED);
    static const char* WD = "SMTWTFS";
    for (int k = 0; k < 7; k++) {
      char w[2] = {WD[k], 0};
      c.textCentered(GRID_X + k * CELL_W + CELL_W / 2, WD_Y, w, TS_MED, true, true);
    }
    const int first = cal::weekday(_vy, _vm, 1), days = cal::daysIn(_vy, _vm);
    for (int d = 1; d <= days; d++) {
      const int slot = first + d - 1;
      const TRect r = cell(slot / 7, slot % 7);
      const bool isToday = _vy == _ty && _vm == _tm && d == _td;
      const bool isSel = d == _sd;
      if (isToday) c.fillRect(r.x + 4, r.y + 2, r.w - 8, r.h - 4, true);
      else if (isSel) c.drawRect(r.x + 4, r.y + 2, r.w - 8, r.h - 4, 2, true);
      snprintf(buf, sizeof(buf), "%d", d);
      c.textCentered(r.x + r.w / 2, r.y + 8, buf, TS_MED, !isToday, isSel || isToday);
      bool any = false;
      for (int i = 0; i < _store.n && !any; i++) any = cal::on(_store.items[i], _vy, _vm, d);
      if (any) c.fillCircle(r.x + r.w / 2, r.y + r.h - 9, 3, !isToday);
    }
    // The selected day's items.
    static const char* WDN[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(buf, sizeof(buf), "%s %d %s", WDN[cal::weekday(_vy, _vm, _sd)], _sd, monthName(_vm));
    c.fillRect(16, LIST_Y, SCREEN_W - 32, 1, true);
    c.text(16, LIST_Y + 8, buf, TS_MED, true, true);
    int idx[8];
    const int n = dayItems(idx, 8);
    if (n == 0) c.text(16, LIST_Y + 50, "nothing on this day", TS_MED, true);
    for (int i = 0; i < n && i < LIST_ROWS; i++) {
      const cal::Item& it = _store.items[idx[i]];
      const TRect r = itemRow(i);
      timeOf(it, buf, sizeof(buf));
      if (it.allDay) c.text(r.x + 4, r.y + 16, buf, TS_MED, true, true);
      else c.text(r.x + 4, r.y + 12, buf, TS_MED, true, true);
      c.textClipped(r.x + 96, r.y + 12, r.w - 140, it.title, TS_MED, true);
      if (it.popup) {  // a small card: it pops up over Home
        c.drawRect(r.x + r.w - 30, r.y + 16, 26, 20, 2, true);
        c.fillRect(r.x + r.w - 25, r.y + 23, 16, 2, true);
      }
      c.fillRect(r.x, r.y + r.h + 2, r.w, 1, true);
    }
    if (n > LIST_ROWS) {
      snprintf(buf, sizeof(buf), "+%d more", n - LIST_ROWS);
      c.textCentered(SCREEN_W / 2, itemRow(LIST_ROWS).y, buf, TS_MED, true);
    }
    c.button(ADD.x, ADD.y, ADD.w, ADD.h, "+ Add", true);
  }

  void tapMonth(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    if (TODAY.hit(x, y) && !(_vy == _ty && _vm == _tm && _sd == _td)) {
      _vy = _ty;
      _vm = _tm;
      _sd = _td;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (PREV.hit(x, y)) return stepMonth(-1);
    if (NEXT.hit(x, y)) return stepMonth(1);
    if (TITLE_TAP.hit(x, y)) {
      _pickYear = _vy;
      _page = Page::Pick;
      host().beep(0);
      host().refresh(false);
      return;
    }
    const int first = cal::weekday(_vy, _vm, 1), days = cal::daysIn(_vy, _vm);
    for (int d = 1; d <= days; d++) {
      const int slot = first + d - 1;
      if (cell(slot / 7, slot % 7).hit(x, y)) {
        _sd = d;
        host().beep(0);
        host().refresh(false);
        return;
      }
    }
    int idx[8];
    const int n = dayItems(idx, 8);
    for (int i = 0; i < n && i < LIST_ROWS; i++)
      if (itemRow(i).hit(x, y)) {
        _sel = idx[i];
        _delArmed = false;
        _page = Page::Item;
        host().beep(0);
        host().refresh(false);
        return;
      }
    if (ADD.hit(x, y)) {
      _draft = cal::Item{};
      _draft.year = (uint16_t)_vy;
      _draft.month = (uint8_t)_vm;
      _draft.day = (uint8_t)_sd;
      int hh = 9, mm = 0;
      if (host().clockHHMM(hh, mm)) hh = hh < 23 ? hh + 1 : 23;
      _draft.hour = (uint8_t)hh;
      _draft.minute = 0;
      _draft.popup = 1;
      _editIdx = -1;
      _kb = osk::State{};
      _page = Page::Type;  // the title first: an item is its words
      host().beep(1);
      host().refresh(false);
    }
  }

  // --- choosing a month and year ------------------------------------------------
  void renderPick(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("CHOOSE MONTH");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", _pickYear);
    c.textCentered(SCREEN_W / 2, 74, buf, TS_HUGE, true, true);
    c.button(Y_PREV.x, Y_PREV.y, Y_PREV.w, Y_PREV.h, "<", false, TS_LARGE);
    c.button(Y_NEXT.x, Y_NEXT.y, Y_NEXT.w, Y_NEXT.h, ">", false, TS_LARGE);
    for (int m = 0; m < 12; m++) {
      const TRect r = monthTile(m);
      const bool shown = _pickYear == _vy && m + 1 == _vm;
      const bool now = _pickYear == _ty && m + 1 == _tm;
      if (shown) c.fillRect(r.x, r.y, r.w, r.h, true);
      else c.drawRect(r.x, r.y, r.w, r.h, now ? 4 : 2, true);
      char nm[4];
      snprintf(nm, sizeof(nm), "%.3s", monthName(m + 1));
      c.textInBox(r.x, r.y, r.w, r.h, nm, TS_LARGE, !shown, true);
    }
  }

  // --- choosing a year: twelve at a time ----------------------------------------
  static constexpr int YEAR_MIN = 2000, YEAR_MAX = 2099;
  void openYears() {
    _yearBase = _pickYear - (_pickYear - YEAR_MIN) % 12;
    _page = Page::Years;
    host().beep(0);
    host().refresh(false);
  }
  void stepYears(int dir) {
    const int nb = _yearBase + dir * 12;
    if (nb < YEAR_MIN || nb > YEAR_MAX) {
      host().beep(2);
      return;
    }
    _yearBase = nb;
    host().beep(0);
    host().refresh(false);
  }
  void renderYears(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("CHOOSE YEAR");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d - %d", _yearBase, _yearBase + 11);
    c.textCentered(SCREEN_W / 2, 80, buf, TS_LARGE, true, true);
    c.button(Y_PREV.x, Y_PREV.y, Y_PREV.w, Y_PREV.h, "<", false, TS_LARGE);
    c.button(Y_NEXT.x, Y_NEXT.y, Y_NEXT.w, Y_NEXT.h, ">", false, TS_LARGE);
    for (int i = 0; i < 12; i++) {
      const int yr = _yearBase + i;
      if (yr > YEAR_MAX) break;
      const TRect r = monthTile(i);
      const bool shown = yr == _pickYear;
      if (shown) c.fillRect(r.x, r.y, r.w, r.h, true);
      else c.drawRect(r.x, r.y, r.w, r.h, yr == _ty ? 4 : 2, true);
      snprintf(buf, sizeof(buf), "%d", yr);
      c.textInBox(r.x, r.y, r.w, r.h, buf, TS_LARGE, !shown, true);
    }
  }
  void tapYears(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y)) {
      _page = Page::Pick;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (Y_PREV.hit(x, y) || Y_NEXT.hit(x, y)) {
      stepYears(Y_NEXT.hit(x, y) ? 1 : -1);
      return;
    }
    for (int i = 0; i < 12; i++)
      if (monthTile(i).hit(x, y) && _yearBase + i <= YEAR_MAX) {
        _pickYear = _yearBase + i;
        _page = Page::Pick;
        host().beep(1);
        host().refresh(false);
        return;
      }
  }

  void tapPick(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y)) {
      _page = Page::Month;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (YEAR_TAP.hit(x, y)) {
      openYears();
      return;
    }
    if (Y_PREV.hit(x, y) || Y_NEXT.hit(x, y)) {
      _pickYear += Y_NEXT.hit(x, y) ? 1 : -1;
      if (_pickYear < 2000) _pickYear = 2000;
      if (_pickYear > 2099) _pickYear = 2099;
      host().beep(0);
      host().refresh(false);
      return;
    }
    for (int m = 0; m < 12; m++)
      if (monthTile(m).hit(x, y)) {
        _vy = _pickYear;
        _vm = m + 1;
        _sd = (_vy == _ty && _vm == _tm) ? _td : 1;
        _page = Page::Month;
        host().beep(1);
        host().refresh(false);
        return;
      }
  }

  // --- one item ---------------------------------------------------------------
  void renderItem(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("ITEM");
    if (_sel < 0 || _sel >= _store.n) return;
    const cal::Item& it = _store.items[_sel];
    char buf[64];
    c.textClipped(16, 80, SCREEN_W - 32, it.title, TS_LARGE, true, true);
    char tm[12];
    timeOf(it, tm, sizeof(tm));
    snprintf(buf, sizeof(buf), "%d %s %d, %s", it.day, monthName(it.month), it.year, tm);
    c.text(16, 140, buf, TS_MED, true);
    repeatText(it, buf, sizeof(buf));
    c.text(16, 180, buf, TS_MED, true);
    if (!it.popup)
      snprintf(buf, sizeof(buf), "no pop-up");
    else if (it.before)
      snprintf(buf, sizeof(buf), "remind me %d day%s before", it.before, it.before == 1 ? "" : "s");
    else
      snprintf(buf, sizeof(buf), "pops up on Home on the day");
    c.text(16, 216, buf, TS_MED, true);
    const bool doneToday =
        it.repeat == cal::REPEAT_NONE ? it.doneDay != 0 : it.doneDay == cal::dayKey(_vy, _vm, _sd);
    c.button(EDIT_BTN.x, EDIT_BTN.y, EDIT_BTN.w, EDIT_BTN.h, "Edit", false);
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, doneToday ? "Done (undo)" : "Mark done",
             !doneToday);
    if (_delArmed) chrome2::armed(c, DEL_BTN, "Delete it?");
    else c.button(DEL_BTN.x, DEL_BTN.y, DEL_BTN.w, DEL_BTN.h, "Delete", false);
  }

  void tapItem(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y) || _sel < 0 || _sel >= _store.n) {
      _page = Page::Month;
      host().beep(1);
      host().refresh(false);
      return;
    }
    cal::Item& it = _store.items[_sel];
    if (EDIT_BTN.hit(x, y)) {
      _draft = it;
      _editIdx = _sel;
      _kb = osk::State{};
      _kb.shift = false;
      _page = Page::Add;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (DONE_BTN.hit(x, y)) {
      const uint32_t key = cal::dayKey(_vy, _vm, _sd);
      const bool doneToday = it.repeat == cal::REPEAT_NONE ? it.doneDay != 0 : it.doneDay == key;
      it.doneDay = doneToday ? 0 : key;
      cal::save(_store);
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (DEL_BTN.hit(x, y)) {
      if (!_delArmed) {
        _delArmed = true;
        host().beep(0);
        host().refresh(false);
        return;
      }
      cal::removeAt(_store, _sel);
      cal::save(_store);
      _sel = -1;
      _page = Page::Month;
      host().beep(1);
      host().refresh(false);
    }
  }

  // --- typing the title -------------------------------------------------------
  void renderType(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("NEW ITEM");
    c.text(16, 52, "What is it?", TS_MED, true);
    c.drawRect(TITLE_BOX.x, TITLE_BOX.y + 12, TITLE_BOX.w, TITLE_BOX.h, 2, true);
    char shown[cal::TITLE + 2];
    snprintf(shown, sizeof(shown), "%s_", _draft.title);
    c.textClipped(TITLE_BOX.x + 12, TITLE_BOX.y + 30, TITLE_BOX.w - 24, shown, TS_MED, true);
    osk::draw(c, _kb);
  }

  void tapType(int x, int y) {
    if (host().isBackTap(x, y)) {
      _page = _draft.title[0] ? Page::Add : (_editIdx >= 0 ? Page::Item : Page::Month);
      host().beep(1);
      host().refresh(false);
      return;
    }
    const int code = osk::tap(_kb, _draft.title, cal::TITLE, x, y, host().nowMs());
    if (code == osk::K_NONE) return;
    if (code == osk::K_DONE) {
      if (!_draft.title[0]) {
        host().beep(2);
        return;
      }
      _page = Page::Add;
      host().beep(1);
      host().refresh(false);
      return;
    }
    // Every key a plain partial (owner: no full-screen flash while typing);
    // the page is cleaned once, on the way out of the keyboard.
    host().beep(0);
    host().refresh(false);
  }

  // --- how it repeats, and until when -----------------------------------------
  void renderRepeat(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("REPEAT");
    static const char* OPT[cal::REPEATS] = {"Once", "Every day", "Every week", "Every year"};
    for (int i = 0; i < cal::REPEATS; i++) {
      const TRect r = repOpt(i);
      const bool on = _draft.repeat == i;
      if (on) c.fillRect(r.x, r.y, r.w, r.h, true);
      else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
      c.text(r.x + 20, r.y + (r.h - c.textHeight(TS_MED)) / 2, OPT[i], TS_MED, !on, true);
    }
    if (_draft.repeat == cal::REPEAT_NONE) {
      c.textCentered(SCREEN_W / 2, END_Y + 30, "it happens once, on its date", TS_MED, true);
    } else {
      c.text(16, END_Y, "ENDS", TS_MED, true, true);
      seg(c, endSeg(0), "Never", !_draft.until);
      seg(c, endSeg(1), "On a date", _draft.until != 0);
      if (_draft.until) {
        char buf[32];
        const int uy = (int)(_draft.until / 10000), um = (int)(_draft.until / 100 % 100),
                  ud = (int)(_draft.until % 100);
        snprintf(buf, sizeof(buf), "%d %s %d", ud, monthName(um), uy);
        c.text(16, END_Y + 104, "LAST DAY", TS_MED, true, true);
        c.drawRect(END_BOX.x, END_BOX.y, END_BOX.w, END_BOX.h, 2, true);
        c.textInBox(END_BOX.x, END_BOX.y, END_BOX.w, END_BOX.h, buf, TS_MED, true, true);
        c.textCentered(SCREEN_W / 2, END_BOX.y + END_BOX.h + 10, "tap to change", TS_MED, true);
      }
    }
    c.button(REP_DONE.x, REP_DONE.y, REP_DONE.w, REP_DONE.h, "Done", true);
  }

  // Typing the end date: year, then month, then day, each on the keypad.
  void openUntil() {
    _ustep = 0;
    _ndigits = 0;
    _page = Page::Until;
    host().beep(0);
    host().refresh(false);
  }

  void renderUntil(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("ENDS ON");
    char buf[32], y[5] = "----", m[3] = "--", d[3] = "--";
    if (_ustep > 0) snprintf(y, sizeof(y), "%04d", _uy);
    if (_ustep > 1) snprintf(m, sizeof(m), "%02d", _um);
    char* cur = _ustep == 0 ? y : (_ustep == 1 ? m : d);
    for (int i = 0; i < _ndigits; i++) cur[i] = _digits[i];
    snprintf(buf, sizeof(buf), "%s / %s / %s", y, m, d);
    c.textCentered(SCREEN_W / 2, 110, buf, TS_LARGE, true, true);
    static const char* ASK[3] = {"year", "month, 1 - 12", "day"};
    c.textCentered(SCREEN_W / 2, 200, ASK[_ustep], TS_MED, true);
    numpad::draw(c);
  }

  void tapUntil(int x, int y) {
    if (host().isBackTap(x, y)) {
      _page = Page::Repeat;
      host().beep(1);
      host().refresh(false);
      return;
    }
    const int k = numpad::hit(x, y);
    if (k < 0) return;
    const int maxDigits = _ustep == 0 ? 4 : 2;
    if (k == numpad::CLEAR) {
      if (_ndigits) _ndigits = 0;
      else if (_ustep) _ustep--;  // clear on an empty field steps back
    } else if (k == numpad::DONE) {
      int v = 0;
      for (int i = 0; i < _ndigits; i++) v = v * 10 + (_digits[i] - '0');
      bool ok = _ndigits > 0;
      if (_ustep == 0) ok = ok && v >= _draft.year && v <= 2099;
      else if (_ustep == 1) ok = ok && v >= 1 && v <= 12 && (_uy > _draft.year || v >= _draft.month);
      else ok = ok && v >= 1 && v <= cal::daysIn(_uy, _um) &&
                cal::dayNum(_uy, _um, v) >= cal::dayNum(_draft.year, _draft.month, _draft.day);
      if (!ok) {
        host().beep(2);
        return;
      }
      if (_ustep == 0) _uy = v;
      else if (_ustep == 1) _um = v;
      _ndigits = 0;
      if (_ustep < 2) {
        _ustep++;
      } else {
        _draft.until = cal::dayKey(_uy, _um, v);
        _page = Page::Repeat;
        host().beep(1);
        host().refresh(false);
        return;
      }
    } else {
      if (_ndigits >= maxDigits) {
        host().beep(2);
        return;
      }
      _digits[_ndigits++] = (char)('0' + numpad::digit(k));
    }
    host().beep(0);
    host().refresh(false);
  }

  void tapRepeat(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y) || REP_DONE.hit(x, y)) {
      _page = Page::Add;
      host().beep(1);
      host().refresh(false);
      return;
    }
    for (int i = 0; i < cal::REPEATS; i++)
      if (repOpt(i).hit(x, y)) {
        _draft.repeat = (uint8_t)i;
        if (i == cal::REPEAT_NONE) _draft.until = 0;
        host().beep(0);
        host().refresh(false);
        return;
      }
    if (_draft.repeat == cal::REPEAT_NONE) return;
    if (endSeg(0).hit(x, y)) {
      _draft.until = 0;
      host().beep(0);
      host().refresh(false);
    } else if (endSeg(1).hit(x, y) || (_draft.until && END_BOX.hit(x, y))) {
      openUntil();
    }
  }

  // --- typing a time ----------------------------------------------------------
  void renderTime(ToolsCanvas& c) {
    using namespace calui;
    host().topBar("TIME");
    char shown[8];
    char d[5] = {'-', '-', '-', '-', 0};
    for (int i = 0; i < _ndigits; i++) d[i] = _digits[i];
    snprintf(shown, sizeof(shown), "%c%c:%c%c", d[0], d[1], d[2], d[3]);
    c.textCentered(SCREEN_W / 2, 120, shown, TS_HUGE, true, true);
    c.textCentered(SCREEN_W / 2, 210, _ndigits < 2 ? "hours, 00 - 23" : "minutes, 00 - 59", TS_MED,
                   true);
    numpad::draw(c);
  }

  void tapTime(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y)) {
      _page = Page::Add;
      host().beep(1);
      host().refresh(false);
      return;
    }
    const int key = numpad::hit(x, y);
    if (key < 0) return;
    if (key == 9) {  // clear
      _ndigits = 0;
    } else if (key == 11) {  // done: needs all four, and a real time
      const int hh = _ndigits >= 2 ? (_digits[0] - '0') * 10 + (_digits[1] - '0') : -1;
      const int mm = _ndigits >= 4 ? (_digits[2] - '0') * 10 + (_digits[3] - '0') : (_ndigits == 2 ? 0 : -1);
      if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        host().beep(2);
        return;
      }
      _draft.hour = (uint8_t)hh;
      _draft.minute = (uint8_t)mm;
      _draft.allDay = 0;
      _page = Page::Add;
      host().beep(1);
      host().refresh(false);
      return;
    } else {
      const char dig = key == 10 ? '0' : (char)('1' + key);
      if (_ndigits >= 4) return;
      // Refuse a digit that cannot start a valid hour or minute.
      const int v = dig - '0';
      if ((_ndigits == 0 && v > 2) || (_ndigits == 1 && _digits[0] == '2' && v > 3) ||
          (_ndigits == 2 && v > 5)) {
        host().beep(2);
        return;
      }
      _digits[_ndigits++] = dig;
    }
    host().beep(0);
    host().refresh(false);
  }

  // --- the rest of a new item -------------------------------------------------
  void renderAdd(ToolsCanvas& c) {
    using namespace calui;
    host().topBar(_editIdx >= 0 ? "EDIT ITEM" : "NEW ITEM");
    c.drawRect(TITLE_BOX.x, TITLE_BOX.y - 14, TITLE_BOX.w, TITLE_BOX.h, 2, true);
    c.textClipped(TITLE_BOX.x + 12, TITLE_BOX.y + 4, TITLE_BOX.w - 24, _draft.title, TS_MED, true,
                  true);
    char buf[32];
    const char* labels[5] = {"Date", "Time", "Repeat", "Pop up on Home", "Remind me"};
    for (int i = 0; i < 5; i++) {
      const TRect r = aRow(i);
      c.text(r.x, r.y, labels[i], TS_MED, true, true);
      c.fillRect(r.x, r.y + r.h + 4, r.w, 1, true);
    }
    // Date: < day month >.
    snprintf(buf, sizeof(buf), "%d %s", _draft.day, monthName(_draft.month));
    stepper(c, aMinus(0, 0), aPlus(0, 1), buf);
    // Time: an "All day" chip, or the time in a box -- tap it to type.
    seg(c, allDayChip(), "All day", _draft.allDay);
    {
      const TRect t = timeBox();
      c.drawRect(t.x, t.y, t.w, t.h, 2, true);
      if (_draft.allDay) {
        c.textInBox(t.x, t.y, t.w, t.h, "tap to set a time", TS_MED, true);
      } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", _draft.hour, _draft.minute);
        c.textInBox(t.x, t.y, t.w, t.h, buf, TS_MED, true, true);
      }
    }
    // Repeat: what it is, and > for its own page.
    {
      const TRect r = repeatRow();
      repeatText(_draft, buf, sizeof(buf));
      c.text(r.x, r.y + 36, buf, TS_MED, true);
      c.text(r.x + r.w - 20, r.y + 36, ">", TS_MED, true, true);
    }
    seg(c, aSeg(3, 0, 2), "off", !_draft.popup);
    seg(c, aSeg(3, 1, 2), "on", _draft.popup);
    // How many days ahead it starts to pop up (owner, 22 Nov).
    if (_draft.before == 0) snprintf(buf, sizeof(buf), "on the day");  // "Remind me ..."
    else snprintf(buf, sizeof(buf), _draft.before == 1 ? "1 day before" : "%d days before", _draft.before);
    stepper(c, aMinus(4, 0), aPlus(4, 1), buf);
    c.button(CANCEL_BTN.x, CANCEL_BTN.y, CANCEL_BTN.w, CANCEL_BTN.h, "Cancel", false);
    c.button(SAVE.x, SAVE.y, SAVE.w, SAVE.h, "Save", true);
  }

  static void seg(ToolsCanvas& c, const TRect& r, const char* lab, bool on) {
    if (on) c.fillRect(r.x, r.y, r.w, r.h, true);
    else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
    c.textInBox(r.x, r.y, r.w, r.h, lab, TS_MED, !on, true);
  }

  static void stepper(ToolsCanvas& c, const TRect& mi, const TRect& pl, const char* val) {
    c.drawRect(mi.x, mi.y, mi.w, mi.h, 2, true);
    c.fillRect(mi.x + 16, mi.y + mi.h / 2 - 1, mi.w - 32, 3, true);
    c.drawRect(pl.x, pl.y, pl.w, pl.h, 2, true);
    c.fillRect(pl.x + 16, pl.y + pl.h / 2 - 1, pl.w - 32, 3, true);
    c.fillRect(pl.x + pl.w / 2 - 1, pl.y + 12, 3, pl.h - 24, true);
    c.textCentered((mi.x + mi.w + pl.x) / 2, mi.y + (mi.h - c.textHeight(TS_MED)) / 2, val, TS_MED,
                   true, true);
  }

  void stepDay(int dir) {
    int y = _draft.year, m = _draft.month, d = _draft.day + dir;
    if (d < 1) {
      if (--m < 1) {
        m = 12;
        y--;
      }
      d = cal::daysIn(y, m);
    } else if (d > cal::daysIn(y, m)) {
      d = 1;
      if (++m > 12) {
        m = 1;
        y++;
      }
    }
    _draft.year = (uint16_t)y;
    _draft.month = (uint8_t)m;
    _draft.day = (uint8_t)d;
  }

  void tapAdd(int x, int y) {
    using namespace calui;
    if (host().isBackTap(x, y) || CANCEL_BTN.hit(x, y)) {
      // Nothing kept: the store still holds what it had.
      _page = _editIdx >= 0 ? Page::Item : Page::Month;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (TITLE_BOX.hit(x, y + 14)) {
      _page = Page::Type;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (allDayChip().hit(x, y)) {
      _draft.allDay = !_draft.allDay;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (repeatRow().hit(x, y)) {
      _page = Page::Repeat;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (timeBox().hit(x, y)) {
      _ndigits = 0;
      _digits[0] = 0;
      _page = Page::Time;
      host().beep(0);
      host().refresh(false);
      return;
    }
    bool changed = true;
    if (aMinus(0, 0).hit(x, y)) stepDay(-1);
    else if (aPlus(0, 1).hit(x, y)) stepDay(1);
    else if (aSeg(3, 0, 2).hit(x, y)) _draft.popup = 0;
    else if (aSeg(3, 1, 2).hit(x, y)) _draft.popup = 1;
    else if (aMinus(4, 0).hit(x, y) && _draft.before > 0) _draft.before--;
    else if (aPlus(4, 1).hit(x, y) && _draft.before < cal::BEFORE_MAX) _draft.before++;
    else changed = false;
    if (changed) {
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (SAVE.hit(x, y)) {
      if (_editIdx >= 0 && _editIdx < _store.n) {
        // An edit is the old item out and the changed one in, so it lands in
        // time order. A changed date or time is a new occurrence: not done.
        const cal::Item& old = _store.items[_editIdx];
        if (old.year != _draft.year || old.month != _draft.month || old.day != _draft.day ||
            old.hour != _draft.hour || old.minute != _draft.minute || old.allDay != _draft.allDay)
          _draft.doneDay = 0;
        cal::removeAt(_store, _editIdx);
        cal::snoozed() = 0;  // indexes moved
        _editIdx = -1;
      }
      if (!cal::add(_store, _draft)) {
        host().beep(2);
        return;
      }
      cal::save(_store);
      _vy = _draft.year;
      _vm = _draft.month;
      _sd = _draft.day;
      _page = Page::Month;
      host().beep(1);
      host().refresh(false);
    }
  }
};
