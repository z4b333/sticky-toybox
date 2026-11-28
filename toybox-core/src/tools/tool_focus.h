// Focus timer (v2, kit "Focus").
//
// A timer that does not count down in front of you. Pick a length, start, and
// the panel shows only that you are focusing and since when -- no numbers
// ticking away, which is the whole point. It beeps at the end. The one way to
// stop early is to hold the Stop button (about a second and a half): a brush
// of the glass, a pocket, a cat cannot end it.
//
// Power: nothing on the panel changes while it runs, so the host may nap in
// light sleep between ticks right up to the last few seconds.
#pragma once
#include "../chrome.h"
#include "tools_draw.h"
#include "numpad.h"
#include "help.h"

namespace focusui {
inline constexpr int PRESETS = 4;
inline constexpr int PRESET_MIN[PRESETS] = {15, 25, 45, 60};  // until the owner saves their own
inline constexpr int MIN_MIN = 1, MAX_MIN = 180;
// The length, in a box: tap it to type on the keypad.
inline constexpr TRect VALUE{24, 90, SCREEN_W - 48, 170};
inline constexpr int RING_CY = 330, RING_R = 160;
// The tone it ends with, under the time (owner): Fast is the two-bell chime,
// Slow a lower, unhurried one.
inline TRect toneSeg(int k) {
  const int w = (SCREEN_W - 48 - 140) / 2;
  return TRect{24 + 140 + k * w, 310, w, 52};
}
inline TRect presetRect(int i) { return TRect{24 + i * 110, 410, 100, 64}; }
inline constexpr TRect SAVE_PRESET{24, 482, SCREEN_W - 48, 60};
inline constexpr TRect START{24, 640, SCREEN_W - 48, 80};
inline constexpr TRect STOP{40, 600, SCREEN_W - 80, 110};
// Done (kit "Focus, done"): Break 5 min | Again (dark), then Done across.
inline constexpr TRect BREAK{24, 600, (SCREEN_W - 56) / 2, 72};
inline constexpr TRect AGAIN{32 + (SCREEN_W - 56) / 2, 600, (SCREEN_W - 56) / 2, 72};
inline constexpr TRect DONE{24, 684, SCREEN_W - 48, 64};
inline constexpr int BREAK_MIN = 5;

// The kit's sprout: a stem and two leaves, drawn from its 64-unit path.
inline void sprout(ToolsCanvas& c, int cx, int top, int size, int t) {
  const float k = size / 64.0f;
  const int ox = cx - (int)(32 * k);
  auto P = [&](float x, float y, int& px, int& py) {
    px = ox + (int)(x * k + 0.5f);
    py = top + (int)(y * k + 0.5f);
  };
  auto cubic = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
    int lx = 0, ly = 0;
    for (int i = 0; i <= 16; i++) {
      const float u = i / 16.0f, v = 1 - u;
      const float x = v * v * v * x0 + 3 * v * v * u * x1 + 3 * v * u * u * x2 + u * u * u * x3;
      const float y = v * v * v * y0 + 3 * v * v * u * y1 + 3 * v * u * u * y2 + u * u * u * y3;
      int px, py;
      P(x, y, px, py);
      if (i) c.drawLine(lx, ly, px, py, t, true);
      c.fillCircle(px, py, t / 2, true);
      lx = px;
      ly = py;
    }
  };
  cubic(32, 56, 32, 47, 32, 39, 32, 30);                                      // stem
  cubic(32, 34, 22, 34, 16, 26, 16, 16);                                      // left leaf
  cubic(16, 16, 26, 16, 32, 24, 32, 34);
  cubic(32, 30, 32, 20, 39, 13, 49, 13);                                      // right leaf
  cubic(49, 13, 49, 23, 42, 30, 32, 30);
}
}  // namespace focusui

class FocusTool : public ToolApp {
 public:
  const char* title() const override { return "FOCUS"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _min = prefs().getInt("f_min", 25);
    if (_min < focusui::MIN_MIN || _min > focusui::MAX_MIN) _min = 25;
    for (int i = 0; i < focusui::PRESETS; i++) {
      char k[8];
      snprintf(k, sizeof(k), "f_p%d", i);
      _preset[i] = prefs().getInt(k, focusui::PRESET_MIN[i]);
      if (_preset[i] < focusui::MIN_MIN || _preset[i] > focusui::MAX_MIN) _preset[i] = focusui::PRESET_MIN[i];
    }
    _stage = Stage::Set;
    _help = !help::suppressed(prefs(), "fo");
  }

  bool wantsTick() const override { return _stage == Stage::Run; }
  bool napsOk() const override { return _stage == Stage::Run && remainMs() > 5000u; }

  void tick() override {
    if (_stage != Stage::Run) return;
    if (remainMs() > 0) {
      // The dots move at each third: two quiet partial refreshes in a run.
      if (third() != _shownThird) {
        _shownThird = third();
        host().refresh(false);
      }
      return;
    }
    if (!_isBreak) countSession();
    _stage = Stage::Done;
    host().refresh(true);  // the screen first, then the bell
    host().beep(prefs().getInt("f_tone", 0) == 1 ? 5 : 4);
  }

  // Running, the power button belongs to the timer: it does not leave.
  bool onButton(SideBtn b) override { return _stage == Stage::Run && b == SideBtn::Ok; }

  void onHold(int x, int y) override {
    if (_stage == Stage::Run && focusui::STOP.hit(x, y)) stopEarly();
  }
  // Or both side buttons, held two seconds (kit).
  void onBothHeld() override {
    if (_stage == Stage::Run) stopEarly();
  }

  void render(ToolsCanvas& c) override {
    using namespace focusui;
    char buf[40];
    if (_stage == Stage::Run) {
      // Kit "Focus, running": the sprout, what it is, how long and since
      // when, three dots for the thirds. No number counts down.
      sprout(c, SCREEN_W / 2, 150, 150, 6);
      c.textCentered(SCREEN_W / 2, 330, _isBreak ? "Break" : "Focusing", TS_HUGE, true, true);
      if (_startedHH >= 0)
        snprintf(buf, sizeof(buf), "%d min  -  started %02d:%02d", _runMin, _startedHH, _startedMM);
      else
        snprintf(buf, sizeof(buf), "%d min", _runMin);
      c.textCentered(SCREEN_W / 2, 390, buf, TS_MED, true);
      for (int i = 0; i < 3; i++) {
        const int dx = SCREEN_W / 2 + (i - 1) * 22;
        if (i <= third()) c.fillCircle(dx, 446, 5, true);
        else c.drawCircle(dx, 446, 5, 1, true);
      }
      // Stopping stays quiet, at the foot (owner): a hold on the line, or
      // both side buttons.
      chrome2::dottedRule(c, STOP.x, STOP.y + 30, STOP.w);
      c.textCentered(SCREEN_W / 2, STOP.y + 40, "hold here to stop", TS_MED, true);
      c.textCentered(SCREEN_W / 2, STOP.y + 78, "or UP + DOWN for 2 s", TS_MED, true);
      return;
    }
    if (_stage == Stage::Type) {
      host().topBar("HOW LONG");
      if (_ndigits) snprintf(buf, sizeof(buf), "%.*s", _ndigits, _digits);
      else snprintf(buf, sizeof(buf), "--");
      c.textCentered(SCREEN_W / 2, 110, buf, TS_HUGE, true, true);
      snprintf(buf, sizeof(buf), "minutes, %d - %d", MIN_MIN, MAX_MIN);
      c.textCentered(SCREEN_W / 2, 210, buf, TS_MED, true);
      numpad::draw(c);
      return;
    }
    host().topBar("FOCUS", _stage == Stage::Set);
    if (_help && _stage == Stage::Set) {
      // d18: what the set-up page used to say under its buttons.
      static const help::Text t{{
                                    "Pick how long, then Start.",
                                    "Tap the number to type one.",
                                    "",
                                    "There is no countdown on the",
                                    "screen: it only shows that you",
                                    "are focusing, and since when.",
                                    "It chimes at the end.",
                                    "",
                                    "To stop early, hold the button",
                                    "at its foot, or UP + DOWN.",
                                },
                                false,
                                13};
      help::render(c, t, "FOCUS");
      return;
    }
    if (_stage == Stage::Done) {
      // Kit "Focus, done".
      sprout(c, SCREEN_W / 2, 150, 110, 5);
      c.textTrackedCentered(SCREEN_W / 2, 290, _isBreak ? "BREAK IS OVER" : "TIME IS UP", TS_MED, true,
                            true, 3);
      c.textCentered(SCREEN_W / 2, 322, _isBreak ? "Back to it" : "Focus done", TS_HUGE, true, true);
      if (_isBreak) {
        snprintf(buf, sizeof(buf), "%d min break", _runMin);
      } else {
        const int n = prefs().getInt("f_cnt", 0);
        const char* suf = (n % 100 >= 11 && n % 100 <= 13) ? "th"
                          : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
        snprintf(buf, sizeof(buf), "%d min  -  %d%s today", _runMin, n, suf);
      }
      c.textCentered(SCREEN_W / 2, 382, buf, TS_MED, true);
      if (_isBreak) {
        c.button(BREAK.x, BREAK.y, AGAIN.x + AGAIN.w - BREAK.x, BREAK.h, "Focus again", true);
      } else {
        snprintf(buf, sizeof(buf), "Break %d min", BREAK_MIN);
        c.button(BREAK.x, BREAK.y, BREAK.w, BREAK.h, buf, false);
        c.button(AGAIN.x, AGAIN.y, AGAIN.w, AGAIN.h, "Again", true);
      }
      c.button(DONE.x, DONE.y, DONE.w, DONE.h, "Done", false);
      return;
    }
    // Set: the length (tap to type), presets as quiet chips, one dark Start.
    c.textCentered(SCREEN_W / 2, VALUE.y - 4, "how long", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d", _min);
    c.textCentered(SCREEN_W / 2, VALUE.y + 30, buf, TS_HUGE, true, true);
    c.textCentered(SCREEN_W / 2, VALUE.y + 112, "minutes", TS_MED, true);
    chrome2::dottedRule(c, VALUE.x + 80, VALUE.y + VALUE.h - 4, VALUE.w - 160);
    // End tone, under the time.
    c.text(24, 310 + 11, "End tone", TS_MED, true);
    {
      const bool slow = prefs().getInt("f_tone", 0) == 1;
      static const char* T[2] = {"Fast", "Slow"};
      for (int k = 0; k < 2; k++) {
        const TRect r = toneSeg(k);
        const bool on = (k == 1) == slow;
        c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);  // calm, like the presets
        c.textInBox(r.x, r.y, r.w, r.h, T[k], TS_MED, true, on);
      }
    }
    c.text(24, 376, "PRESETS", TS_MED, true, true);
    for (int i = 0; i < PRESETS; i++) {
      const TRect r = presetRect(i);
      snprintf(buf, sizeof(buf), "%d", _preset[i]);
      const bool on = _min == _preset[i];
      c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
      c.textInBox(r.x, r.y, r.w, r.h, buf, TS_MED, true, on);
    }
    if (!isPreset(_min)) {
      snprintf(buf, sizeof(buf), "+ keep %d min as a preset", _min);
      c.textCentered(SCREEN_W / 2, SAVE_PRESET.y + 18, buf, TS_MED, true, true);
    }
    c.button(START.x, START.y, START.w, START.h, "Start", true);
  }

  void onTap(int x, int y) override {
    using namespace focusui;
    if (_stage == Stage::Run) return;  // only a hold ends it
    if (_stage == Stage::Type && host().isBackTap(x, y)) {
      _stage = Stage::Set;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    if (_help && _stage == Stage::Set) {
      const help::Tap t = help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Toggle) {
        host().beep(0);
        host().refresh(false);
        return;
      }
      if (t == help::Tap::Never) help::suppress(prefs(), "fo");
      _help = false;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (_stage == Stage::Set && host().isHelpTap(x, y)) {
      _help = true;
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (_stage == Stage::Type) {
      tapType(x, y);
      return;
    }
    if (_stage == Stage::Done) {
      if (_isBreak ? (BREAK.hit(x, y) || AGAIN.hit(x, y)) : AGAIN.hit(x, y)) {
        start();
      } else if (!_isBreak && BREAK.hit(x, y)) {
        start(true);
      } else if (DONE.hit(x, y)) {
        _stage = Stage::Set;
        host().beep(0);
        host().refresh(true);
      }
      return;
    }
    if (VALUE.hit(x, y)) {
      _ndigits = 0;
      _stage = Stage::Type;
      host().beep(0);
      host().refresh(false);
      return;
    }
    for (int k = 0; k < 2; k++)
      if (toneSeg(k).hit(x, y)) {
        prefs().putInt("f_tone", k);
        host().refresh(false);
        host().beep(k ? 5 : 4);  // let it be heard
        return;
      }
    for (int i = 0; i < PRESETS; i++)
      if (presetRect(i).hit(x, y)) {
        setMin(_preset[i]);
        return;
      }
    if (SAVE_PRESET.hit(x, y) && !isPreset(_min)) {
      for (int i = PRESETS - 1; i > 0; i--) _preset[i] = _preset[i - 1];
      _preset[0] = _min;
      for (int i = 0; i < PRESETS; i++) {
        char k[8];
        snprintf(k, sizeof(k), "f_p%d", i);
        prefs().putInt(k, _preset[i]);
      }
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (START.hit(x, y)) start();
  }

  // For the harness.
  int hostStage() const { return (int)_stage; }
  int hostMin() const { return _min; }
  int hostPreset(int i) const { return _preset[i]; }
  int hostTone() { return prefs().getInt("f_tone", 0); }
  void hostSkipToEnd() { _startMs = host().nowMs() - (uint32_t)_runMin * 60000u; }
  void hostSkipToSecondThird() { _startMs = host().nowMs() - (uint32_t)_runMin * 60000u / 2; }

 private:
  enum class Stage : uint8_t { Set, Run, Done, Type };
  Stage _stage = Stage::Set;
  bool _help = false;  // the "?" card (d18)
  int _min = 25;
  uint32_t _startMs = 0;
  int _startedHH = -1, _startedMM = 0;
  int _preset[focusui::PRESETS] = {15, 25, 45, 60};
  char _digits[3] = {0, 0, 0};
  int _ndigits = 0;

  bool isPreset(int m) const {
    for (int i = 0; i < focusui::PRESETS; i++)
      if (_preset[i] == m) return true;
    return false;
  }

  void tapType(int x, int y) {
    using namespace focusui;
    const int k = numpad::hit(x, y);
    if (k < 0) return;
    if (k == numpad::CLEAR) {
      _ndigits = 0;
    } else if (k == numpad::DONE) {
      int v = 0;
      for (int i = 0; i < _ndigits; i++) v = v * 10 + (_digits[i] - '0');
      if (v < MIN_MIN || v > MAX_MIN) {
        host().beep(2);
        return;
      }
      _stage = Stage::Set;
      setMin(v);
      return;
    } else {
      const int d = numpad::digit(k);
      if (_ndigits >= 3 || (_ndigits == 0 && d == 0)) {
        host().beep(2);
        return;
      }
      _digits[_ndigits++] = (char)('0' + d);
    }
    host().beep(0);
    host().refresh(false);
  }

  int _runMin = 25;      // this run's length: the focus length, or a break's
  bool _isBreak = false;
  int _shownThird = 0;

  uint32_t totalMs() const { return (uint32_t)_runMin * 60000u; }
  // 0, 1 or 2: which third of the run we are in.
  int third() const {
    const uint32_t gone = totalMs() - remainMs();
    const int t = (int)((uint64_t)gone * 3 / (totalMs() ? totalMs() : 1));
    return t > 2 ? 2 : t;
  }

  void stopEarly() {
    _stage = Stage::Set;
    host().beep(2);
    host().refresh(true);
  }

  // Sessions finished today, for "3rd today". A new day starts the count again.
  void countSession() {
    int y = 0, m = 0, d = 0;
    const int key = host().clockDate(y, m, d) ? y * 10000 + m * 100 + d : 0;
    int n = prefs().getInt("f_cnt", 0);
    if (prefs().getInt("f_day", -1) != key) n = 0;
    prefs().putInt("f_day", key);
    prefs().putInt("f_cnt", n + 1);
  }

  uint32_t remainMs() const {
    const uint32_t total = (uint32_t)_runMin * 60000u;
    const uint32_t gone = _host ? _host->nowMs() - _startMs : 0;
    return gone >= total ? 0 : total - gone;
  }

  void setMin(int m) {
    _min = m;
    prefs().putInt("f_min", _min);
    host().beep(0);
    host().refresh(false);
  }

  void start(bool isBreak = false) {
    _isBreak = isBreak;
    _runMin = isBreak ? focusui::BREAK_MIN : _min;
    _shownThird = 0;
    _startMs = host().nowMs();
    int hh = 0, mm = 0;
    if (host().clockHHMM(hh, mm)) {
      _startedHH = hh;
      _startedMM = mm;
    } else {
      _startedHH = -1;
    }
    _stage = Stage::Run;
    host().beep(1);
    host().refresh(true);
  }
};
