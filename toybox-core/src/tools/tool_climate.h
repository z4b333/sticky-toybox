// Climate (C5, UTILITY). The room from the SHT40 -- temperature, humidity,
// dew point, "feels like", one word for it -- a graph of the temperature
// since the app opened, the battery's own temperature, and the weather
// outside from Open-Meteo when Refresh is tapped.
//
// Owner, 25 Nov: degrees C by default, F in More; an offset in More (the
// Sticky reads warm while it works), -1.0 to start; the outdoor place is a
// city typed on the phone page, fetched only on Refresh. The page is
// Climate's own (More > Outdoor place): on the home Wi-Fi at toybox.local
// when one is saved, the Sticky's own Wi-Fi otherwise (owner, 25 Nov); the
// clock page (Settings > Clock) has the same field.
//
// The arithmetic is climate_calc.h, the fetch weather.h (both host-tested).
#pragma once
#include "big_digits.h"
#include "climate_calc.h"
#include "home_net.h"
#include "import_screen.h"
#include "kit_game.h"
#include "place_web.h"
#include "weather.h"

namespace clui {
// Owner, 25 Nov ("clearer, less cluttered -- contrast and size"): one big
// number, a black word for the room, three plain readings without boxes, a
// quiet graph, and the outside in a black band of its own at the foot.
inline constexpr int HEAD_Y = 52;
inline constexpr int HERO_Y = 76;                  // the temperature, 126 px numerals
inline constexpr int WORD_Y = 216;                 // the comfort word, white on black
inline constexpr int READ_Y = 286;                 // humidity / dew point / feels like
inline constexpr TRect GRAPH{20, 382, 440, 118};
inline constexpr int BAND_Y = 516, BAND_H = 222;   // outside, white on black
inline constexpr TRect REFRESH{300, BAND_Y + BAND_H - 72, 160, 56};
inline constexpr int TICKS_PER_S = 50;
inline constexpr int POINTS = 120;      // half an hour across the box
}  // namespace clui

class ClimateTool : public KitGame {
 public:
  ~ClimateTool() override { _net.stop(); }  // Back from the phone page: radio off
  const char* title() const override { return "CLIMATE"; }

  bool wantsTick() const override { return true; }
  bool napsOk() const override { return !_phone; }  // a page being served keeps the radio awake
  bool isGame() const override { return false; }
  void tick() override {
    if (_phone) {
      _net.loop();
      if (_net.received()) {
        _net.clearReceived();
        _gotPlace = true;
        host().beep(1);
        host().refresh(false);
      }
      return;
    }
    // The outside, on its own clock when it is not manual.
    if (clim::OUT_MIN[_outIdx] && ++_outBeat >= (long)clim::OUT_MIN[_outIdx] * 60 * clui::TICKS_PER_S) {
      _outBeat = 0;
      if (_view == View::Play) {
        fetchOutside();
        return;
      }
    }
    if (++_beat < clim::READ_S[_readIdx] * clui::TICKS_PER_S) return;
    _beat = 0;
    const int t = _t, rh = _rh, bt = _bt;
    const bool had = _have;
    readNow();
    if (_view != View::Play) return;
    if (had == _have && t == _t && rh == _rh && bt == _bt && (_n - 1) % graphEvery() != 0) return;
    host().refresh(false);
  }

  bool onButton(SideBtn b) override {
    if (_view != View::Play || _phone || b != SideBtn::Ok) return false;
    fetchOutside();
    return true;
  }

#ifdef TOYBOX_HOST
  int hostPoints() const { return _pts; }
  bool hostPhone() const { return _phone; }
  bool hostPhoneHome() const { return _net.home(); }
  bool hostPhoneSave(const char* city) {
    const bool ok = _net.hostSave(city);
    tick();
    return ok;
  }
  bool hostPhonePick(const char* city, int32_t lat, int32_t lon, const char* label) {
    const bool ok = _net.hostPick(city, lat, lon, label);
    tick();
    return ok;
  }
  void hostRead(int times) {
    for (int i = 0; i < times; i++) {
      _beat = clim::READ_S[_readIdx] * clui::TICKS_PER_S;
      tick();
    }
  }
#endif

 protected:
  const help::Text& rulesText() const override { return help::CLIMATE; }
  const char* helpKey() const override { return "cl"; }

  void load() override {
    _f = geti("cl_unit") == 1;
    _readIdx = geti("cl_int", 0);
    if (_readIdx < 0 || _readIdx >= clim::READS) _readIdx = 0;
    _outIdx = geti("cl_out", 0);
    if (_outIdx < 0 || _outIdx >= clim::OUTS) _outIdx = 0;
    _outBeat = 0;
    _off = geti("cl_off", clim::OFFSET_DEFAULT);
    if (_off < 0 || _off >= clim::OFFSETS) _off = clim::OFFSET_DEFAULT;
    _beat = 0;
    _n = _pts = 0;
    _msg = nullptr;
    _haveOut = wx::lastNow(prefs(), _out);
    readNow();
  }
  const char* corner() override { return _f ? "\xc2\xb0" "F" : "\xc2\xb0" "C"; }

  int extraRows(gk::Row* rows) override {
    clim::fmt1(_offLabel, sizeof(_offLabel), clim::OFFSET_DECI[_off]);
    strncat(_offLabel, "\xc2\xb0" "C", sizeof(_offLabel) - strlen(_offLabel) - 1);
    char city[wx::CITY_MAX];
    wx::city(prefs(), city, sizeof(city));
    snprintf(_placeLabel, sizeof(_placeLabel), "%s", city[0] ? city : "set from phone");
    rows[0] = {"Read the room every", clim::readName(_readIdx), false};
    rows[1] = {"Outdoor place", _placeLabel, false};
    rows[2] = {"Update outside", clim::outName(_outIdx), false};
    rows[3] = {"Units", _f ? "\xc2\xb0" "F" : "\xc2\xb0" "C", false};
    rows[4] = {"Offset", _offLabel, false};
    if (_haveBt && gk::MENU_MAX > 5) {
      temp(_btLabel, sizeof(_btLabel), _bt, true);
      rows[5] = {"Battery temperature", _btLabel, false};
      return 6;
    }
    return 5;
  }
  bool pick(const char* what) override {
    if (!strcmp(what, "Read the room every")) {
      _readIdx = (_readIdx + 1) % clim::READS;
      prefs().putInt("cl_int", _readIdx);
      _beat = 0;
    } else if (!strcmp(what, "Update outside")) {
      _outIdx = (_outIdx + 1) % clim::OUTS;
      prefs().putInt("cl_out", _outIdx);
      _outBeat = 0;
    } else if (!strcmp(what, "Units")) {
      _f = !_f;
      prefs().putInt("cl_unit", _f ? 1 : 0);
    } else if (!strcmp(what, "Offset")) {
      _off = (_off + 1) % clim::OFFSETS;
      prefs().putInt("cl_off", _off);
      readNow();
    } else if (!strcmp(what, "Outdoor place")) {
      _view = View::Play;  // the phone page is drawn in the game's place
      openPhone();
      return true;
    }
    return false;  // settings: stay on More
  }

  void renderPlay(ToolsCanvas& c) override {
    using namespace clui;
    if (_phone) return renderPhone(c);
    const int w = c.width();
    char a[24];
    c.textTracked(20, HEAD_Y, "INDOOR", TS_SMALL, true, false, 3);
    if (!_have) {
      c.textCentered(w / 2, HERO_Y + 30, "No climate sensor answered", TS_LARGE, true, true);
    } else {
      hero(c, w / 2, HERO_Y, _f ? clim::toF(_t) : _t);
      gk::slab(c, w / 2, WORD_Y, clim::comfort(_t, _rh), TS_LARGE);
      // Three readings, plain: a big value over a small tracked label, thin
      // rules between them, no boxes.
      snprintf(a, sizeof(a), "%d%%", _rh);
      reading(c, 0, a, "HUMIDITY");
      temp(a, sizeof(a), clim::dewPoint(_t, _rh), false);
      reading(c, 1, a, "DEW POINT");
      temp(a, sizeof(a), clim::feelsLike(_t, _rh), false);
      reading(c, 2, a, "FEELS LIKE");
      graph(c);
    }
    outdoor(c);
    // d18: no footer -- how often the room is read is under More.
  }

  void tapPlay(int x, int y) override {
    if (_phone) return tapPhone(x, y);
    if (clui::REFRESH.hit(x, y)) fetchOutside();
  }

 private:
  bool _f = false, _have = false, _haveBt = false, _haveOut = false, _fetching = false;
  int _off = clim::OFFSET_DEFAULT, _beat = 0;
  int _readIdx = 0, _outIdx = 0;
  long _outBeat = 0;
  // A graph point about every 15 s at the quickest rate, every reading else.
  int graphEvery() const {
    const int r = clim::READ_S[_readIdx];
    return r >= 15 ? 1 : (15 + r - 1) / r;
  }
  int _t = 0, _rh = 0, _bt = 0;  // corrected tenths C, %, battery tenths C
  int _n = 0;                    // readings since opening
  int16_t _hist[clui::POINTS] = {};
  int _pts = 0;
  wx::Now _out{};
  const char* _msg = nullptr;
  char _offLabel[16] = {}, _placeLabel[wx::CITY_MAX] = {}, _btLabel[16] = {};
  // The phone page for the place.
  plweb::PlaceServer _net;
  bool _phone = false, _joining = false, _netOk = false, _altQr = false, _gotPlace = false;
  const char* _netNote = nullptr;
  char _joinName[homenet::SSID_MAX] = {};

  // Home Wi-Fi first when one is saved: the phone keeps its own network and
  // its internet. The Sticky's own access point is the backup.
  void openPhone() {
    _phone = true;
    _gotPlace = false;
    _altQr = false;
    _netNote = nullptr;
    _netOk = false;
    if (homenet::haveWifi(prefs())) {
      homenet::wifiName(prefs(), _joinName, sizeof(_joinName));
      _joining = true;
      host().refresh(false);  // "Joining ..." while the radio looks
      _netOk = _net.startHome(prefs(), "/place");
      _joining = false;
      if (!_netOk) _netNote = "the home Wi-Fi did not answer";
    }
    if (!_netOk) _netOk = _net.start(prefs());
    host().beep(_netOk ? 1 : 2);
    host().refresh(true);
  }
  void closePhone() {
    _net.stop();
    _phone = false;
    _netOk = false;
    // A new place: forget what the old one said.
    _haveOut = wx::lastNow(prefs(), _out);
    _msg = nullptr;
    host().beep(1);
    host().refresh(true);
  }
  void renderPhone(ToolsCanvas& c) {
    using namespace importui;
    if (_joining) return drawJoining(c, _joinName);
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start Wi-Fi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Back", true, TS_LARGE);
      return;
    }
    if (_gotPlace) {
      char city[wx::CITY_MAX];
      wx::city(prefs(), city, sizeof(city));
      wx::Geo g{};
      const bool picked = wx::geo(prefs(), g) && g.found[0];
      c.textCentered(c.width() / 2, 200, "Outdoor place", TS_MED, true);
      c.textCentered(c.width() / 2, 240, city, TS_HUGE, true, true);
      if (picked) c.textCentered(c.width() / 2, 296, g.found, TS_MED, true);
      c.textCentered(c.width() / 2, 340, "tap Done, then Refresh outdoor", TS_MED, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Done", true, TS_LARGE);
      return;
    }
    static const Words W{"Scan to set the place on your phone",
                         {"Type a town or city", "Save to device", "Then Refresh outdoor here"}};
    draw(c, _net, _altQr, W, _netNote);
  }
  void tapPhone(int x, int y) {
    using namespace importui;
    if (DONE_BTN.hit(x, y)) return closePhone();
    if (!_netOk || _gotPlace || !ALT_BTN.hit(x, y)) return;
    if (_net.home()) {  // the backup: the Sticky's own access point
      _net.stop();
      _netNote = nullptr;
      _altQr = false;
      _netOk = _net.start(prefs());
    } else {
      _altQr = !_altQr;
    }
    host().beep(_netOk ? 0 : 2);
    host().refresh(true);
  }

  void readNow() {
    int t, rh;
    _have = host().climate(t, rh);
    if (_have) {
      const int off = clim::OFFSET_DECI[_off];
      _rh = clim::correctedRh(t, rh, off);
      _t = t + off;
      if (_n % graphEvery() == 0) {
        if (_pts == clui::POINTS) {
          memmove(_hist, _hist + 1, sizeof(_hist[0]) * (clui::POINTS - 1));
          _pts--;
        }
        _hist[_pts++] = (int16_t)_t;
      }
      _n++;
    }
    _haveBt = host().batteryTemp(_bt);
  }

  // "24.3°C" (big, with the unit) or "13.9°" (small boxes).
  void temp(char* out, int n, int deciC, bool unit) {
    char v[12];
    clim::fmt1(v, sizeof(v), _f ? clim::toF(deciC) : deciC);
    snprintf(out, n, "%s\xc2\xb0%s", v, unit ? (_f ? "F" : "C") : "");
  }

  void graph(ToolsCanvas& c) {
    using namespace clui;
    const TRect& g = GRAPH;
    chrome2::dottedRule(c, g.x, g.y + g.h, g.w);  // a floor, no frame
    if (_pts < 2) {
      c.textCentered(g.x + g.w / 2, g.y + g.h / 2 - 12, "the graph fills while this is open",
                     TS_SMALL, true);
      return;
    }
    int lo = _hist[0], hi = _hist[0];
    for (int i = 1; i < _pts; i++) {
      if (_hist[i] < lo) lo = _hist[i];
      if (_hist[i] > hi) hi = _hist[i];
    }
    // At least a degree of height, so a steady room draws a calm line rather
    // than its sensor noise magnified to the full box.
    if (hi - lo < 10) {
      const int mid = (hi + lo) / 2;
      lo = mid - 5;
      hi = mid + 5;
    }
    const int px = g.x, pw = g.w, py = g.y + 30, ph = g.h - 38;
    int lx = 0, ly = 0;
    for (int i = 0; i < _pts; i++) {
      const int x = px + i * pw / (POINTS - 1);
      const int y = py + ph - (_hist[i] - lo) * ph / (hi - lo);
      if (i) c.drawLine(lx, ly, x, y, 3, true);
      lx = x, ly = y;
    }
    char a[24], b[24], line[56];
    temp(a, sizeof(a), lo, false);
    temp(b, sizeof(b), hi, false);
    snprintf(line, sizeof(line), "low %s  high %s", a, b);
    c.text(g.x, g.y, line, TS_SMALL, true);
    const int mins = (int)(((long)_n * clim::READ_S[_readIdx] + 30) / 60);
    snprintf(line, sizeof(line), mins ? "last %d min" : "just opened", mins);
    c.text(g.x + g.w - c.textWidth(line, TS_SMALL), g.y, line, TS_SMALL, true);
  }

  // The outside: a black band, white type, the Refresh button white in it.
  void outdoor(ToolsCanvas& c) {
    using namespace clui;
    const int w = c.width();
    c.fillRect(0, BAND_Y, w, BAND_H, true);
    char city[wx::CITY_MAX], head[wx::CITY_MAX + 12];
    wx::city(prefs(), city, sizeof(city));
    wx::Geo g{};
    if (wx::geo(prefs(), g) && g.found[0]) snprintf(city, sizeof(city), "%s", g.found);
    snprintf(head, sizeof(head), "OUTSIDE%s%s", city[0] ? "  -  " : "", city);
    c.textTracked(20, BAND_Y + 16, head, TS_SMALL, false, false, 3);
    const TRect& r = REFRESH;
    c.fillRect(r.x, r.y, r.w, r.h, false);
    c.textInBox(r.x, r.y, r.w, r.h, _fetching ? "Fetching..." : "Refresh", TS_LARGE, true, true);
    if (!_haveOut) {
      const char* why = _msg ? _msg
                        : city[0] ? "tap Refresh for the weather there"
                                  : "set a place: More > Outdoor place";
      c.textClipped(20, BAND_Y + 60, w - 40, why, TS_MED, false);
      return;
    }
    char a[24], f[16], line[56];
    temp(a, sizeof(a), _out.t, true);
    c.text(20, BAND_Y + 50, a, TS_HUGE, false, true);
    c.text(40 + c.textWidth(a, TS_HUGE, true), BAND_Y + 62, wx::codeWord(_out.code), TS_LARGE, false,
           true);
    temp(f, sizeof(f), _out.feels, false);
    snprintf(line, sizeof(line), "%d%% humidity  -  feels %s", _out.rh, f);
    c.text(20, BAND_Y + 112, line, TS_MED, false);
    if (_msg)
      c.textClipped(20, BAND_Y + 146, r.x - 36, _msg, TS_SMALL, false);
    else if (_out.hhmm >= 0) {
      snprintf(line, sizeof(line), "at %02d:%02d there", _out.hhmm / 100, _out.hhmm % 100);
      c.text(20, BAND_Y + 146, line, TS_SMALL, false);
    }
    // Where it comes from (owner; Open-Meteo's data is CC BY 4.0).
    c.text(20, BAND_Y + 178, "weather by Open-Meteo.com", TS_SMALL, false);
  }

  // The temperature as the hero: whole degrees in the Timer's big numerals
  // (big_digits.h, Noto Sans SemiBold at 126 px), the point and the tenth
  // at the largest text size beside them, the unit over the tenth.
  void hero(ToolsCanvas& c, int cx, int y, int deci) {
    char whole[8], tenth[4];
    const int a = deci < 0 ? -deci : deci;
    snprintf(whole, sizeof(whole), "%d", a / 10);
    snprintf(tenth, sizeof(tenth), ".%d", a % 10);
    const char* unit = _f ? "\xc2\xb0" "F" : "\xc2\xb0" "C";
    const int minus = deci < 0 ? 44 : 0;  // a bar the numerals' weight
    const int ww = bigdigits::width(whole);
    const int tw = c.textWidth(tenth, TS_HUGE, true), uw = c.textWidth(unit, TS_HUGE, true);
    const int side = tw > uw ? tw : uw;
    int x = cx - (minus + ww + 8 + side) / 2;
    if (minus) {
      c.fillRect(x, y + bigdigits::H / 2 - 7, minus - 10, 14, true);
      x += minus;
    }
    bigdigits::drawCentered(c, x + ww / 2, y, whole, true);
    x += ww + 8;
    c.text(x, y + 6, unit, TS_HUGE, true, true);
    c.text(x, y + bigdigits::H - c.textHeight(TS_HUGE) - 4, tenth, TS_HUGE, true, true);
  }

  void reading(ToolsCanvas& c, int i, const char* v, const char* label) {
    using namespace clui;
    const int cw = (c.width() - 40) / 3, x = 20 + i * cw;
    if (i) c.fillRect(x, READ_Y + 4, 1, 70, true);
    c.textCentered(x + cw / 2, READ_Y, v, TS_HUGE, true, true);
    c.textTrackedCentered(x + cw / 2, READ_Y + 56, label, TS_SMALL, true, false, 2);
  }

  void fetchOutside() {
    if (_fetching) return;
    _fetching = true;
    _msg = nullptr;
    host().beep(0);
    host().refresh(false);  // "Fetching..." before the radio wakes
    wx::Now n{};
    const wx::Result r = wx::fetch(prefs(), n);
    _fetching = false;
    if (r == wx::Result::Ok) {
      _out = n;
      _haveOut = true;
      host().beep(1);
    } else {
      _msg = wx::describe(r);
      host().beep(2);
    }
    host().refresh(false);
  }
};
