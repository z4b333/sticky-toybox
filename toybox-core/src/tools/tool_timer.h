// Countdown timer + stopwatch.
//
// E-ink note: a 1 Hz full-frame refresh is wasteful and ghosts, so the display
// only repaints every second inside the last minute; above that it steps every
// 10 seconds (and says so). The time shown is always current — just coarser.
#pragma once
#include "tools_draw.h"

namespace timerui {
inline constexpr TRect MODE_CD{40, 50, 190, 48};
inline constexpr TRect MODE_SW{250, 50, 190, 48};

// countdown
inline constexpr int PRESET_W = 104, PRESET_H = 52, PRESET_GAP = 8;
inline constexpr int PRESET_X0 = 20, PRESET_Y0 = 312, PRESET_Y1 = 372;
inline constexpr int PRESET_MIN[8] = {1, 3, 5, 10, 15, 25, 30, 60};
inline constexpr TRect CD_START{40, 444, 200, 66};
inline constexpr TRect CD_RESET{260, 444, 180, 66};
// Minus down the left, plus down the right, one row per size. This is the
// column the single -1 MIN / +1 MIN pair used to occupy, so the two seconds
// rows arrive under buttons the hand already knows.
//
// The alternative was all six on one line, reading as a number line, which is
// tidier on paper and leaves each button 7.3 mm wide to be pressed by a thumb.
// Two columns keep them at 20 mm. On a panel this small that is the argument
// that wins.
inline constexpr int ADJ_H = 56, ADJ_STEP = 62, ADJ_Y0 = 524;
inline constexpr int ADJ_SEC[3] = {60, 30, 5};
inline constexpr const char* ADJ_MINUS[3] = {"-1 MIN", "-30 SEC", "-5 SEC"};
inline constexpr const char* ADJ_PLUS[3] = {"+1 MIN", "+30 SEC", "+5 SEC"};
inline TRect adjMinusRect(int i) { return TRect{40, ADJ_Y0 + i * ADJ_STEP, 200, ADJ_H}; }
inline TRect adjPlusRect(int i) { return TRect{260, ADJ_Y0 + i * ADJ_STEP, 180, ADJ_H}; }

// stopwatch
inline constexpr TRect SW_START{40, 300, 190, 66};
inline constexpr TRect SW_RESET{240, 300, 90, 66};
inline constexpr TRect SW_LAP{340, 300, 100, 66};

inline TRect presetRect(int i) {
  const int col = i % 4, row = i / 4;
  return TRect{PRESET_X0 + col * (PRESET_W + PRESET_GAP),
               row ? PRESET_Y1 : PRESET_Y0, PRESET_W, PRESET_H};
}
}  // namespace timerui

class TimerTool : public ToolApp {
 public:
  const char* title() const override { return _stopwatch ? "STOPWATCH" : "TIMER"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _stopwatch = false;
    _setSec = prefs().getInt("t_set", 5 * 60);
    if (_setSec < 5 || _setSec > 99 * 60) _setSec = 5 * 60;
    _remainMs = (uint32_t)_setSec * 1000u;
    _running = false;
    _finished = false;
    _swAccum = 0;
    _lapN = 0;
    _lastShown = -1;
  }

  bool wantsTick() const override { return _running; }

  void render(ToolsCanvas& c) override {
    using namespace timerui;
    host().topBar(title());

    c.button(MODE_CD.x, MODE_CD.y, MODE_CD.w, MODE_CD.h, "COUNTDOWN", !_stopwatch);
    c.button(MODE_SW.x, MODE_SW.y, MODE_SW.w, MODE_SW.h, "STOPWATCH", _stopwatch);

    char buf[16];
    formatMs(buf, sizeof(buf), _stopwatch ? swElapsed() : remainMs());
    tdraw::seg7Centered(c, c.width() / 2, 124, 110, buf, true);

    if (_stopwatch)
      renderStopwatch(c);
    else
      renderCountdown(c);

    if (_finished) {
      c.fillRect(30, 140, 420, 90, true);
      c.textInBox(30, 140, 420, 90, "TIME'S UP!", TS_HUGE, false, true);
    }
  }

  void onTap(int x, int y) override {
    using namespace timerui;
    if (host().isBackTap(x, y)) {
      host().goHub();
      return;
    }
    if (_finished) {  // any tap dismisses the alarm banner
      _finished = false;
      _remainMs = (uint32_t)_setSec * 1000u;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (MODE_CD.hit(x, y) && _stopwatch) return switchMode(false);
    if (MODE_SW.hit(x, y) && !_stopwatch) return switchMode(true);
    if (_stopwatch)
      tapStopwatch(x, y);
    else
      tapCountdown(x, y);
  }

  void tick() override {
    if (!_running) return;
    if (!_stopwatch && remainMs() == 0) {
      _running = false;
      _finished = true;
      _remainMs = 0;
      host().beep(3);
      host().refreshUi();
      return;
    }
    const int sec = (int)(displayMs() / 1000u);
    if (sec == _lastShown) return;
    // Fine-grained only near the end (or the first minute of a stopwatch).
    const bool fine = _stopwatch ? (sec < 60) : (sec <= 60);
    if (fine || sec % 10 == 0) {
      _lastShown = sec;
      host().refresh(false);
    }
  }

 private:
  // --- countdown ---------------------------------------------------------
  void renderCountdown(ToolsCanvas& c) {
    using namespace timerui;
    const uint32_t total = (uint32_t)_setSec * 1000u;
    const int permille = total ? (int)((remainMs() * 1000ULL) / total) : 0;
    tdraw::progressBar(c, 40, 258, 400, 26, permille);

    char buf[12];
    for (int i = 0; i < 8; i++) {
      const TRect r = presetRect(i);
      snprintf(buf, sizeof(buf), "%d", PRESET_MIN[i]);
      c.button(r.x, r.y, r.w, r.h, buf, PRESET_MIN[i] * 60 == _setSec);
    }
    c.text(PRESET_X0, PRESET_Y0 - 22, "MINUTES", TS_MED, true);

    c.button(CD_START.x, CD_START.y, CD_START.w, CD_START.h, _running ? "PAUSE" : "START",
             true, TS_LARGE);
    c.button(CD_RESET.x, CD_RESET.y, CD_RESET.w, CD_RESET.h, "RESET", false);
    for (int i = 0; i < 3; i++) {
      const TRect m = adjMinusRect(i), p = adjPlusRect(i);
      c.button(m.x, m.y, m.w, m.h, ADJ_MINUS[i], false);
      c.button(p.x, p.y, p.w, p.h, ADJ_PLUS[i], false);
    }

    if (_running && remainMs() > 61000u)
      c.textCentered(c.width() / 2, 726, "the display steps every 10s", TS_MED, true);
  }

  void tapCountdown(int x, int y) {
    using namespace timerui;
    for (int i = 0; i < 8; i++) {
      if (presetRect(i).hit(x, y)) {
        _setSec = PRESET_MIN[i] * 60;
        prefs().putInt("t_set", _setSec);
        _remainMs = (uint32_t)_setSec * 1000u;
        _running = false;
        host().beep(1);
        host().refreshUi();
        return;
      }
    }
    if (CD_START.hit(x, y)) {
      if (_running) {
        _remainMs = remainMs();  // freeze what is left
        _running = false;
      } else {
        if (_remainMs == 0) _remainMs = (uint32_t)_setSec * 1000u;
        _endMs = millis() + _remainMs;
        _running = true;
        _lastShown = -1;
      }
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (CD_RESET.hit(x, y)) {
      _running = false;
      _remainMs = (uint32_t)_setSec * 1000u;
      host().beep(1);
      host().refreshUi();
      return;
    }
    for (int i = 0; i < 3; i++) {
      if (adjMinusRect(i).hit(x, y)) return adjust(-ADJ_SEC[i]);
      if (adjPlusRect(i).hit(x, y)) return adjust(ADJ_SEC[i]);
    }
  }

  void adjust(int deltaSec) {
    _setSec += deltaSec;
    // Five seconds is a sensible floor now that five-second steps exist: one
    // second of countdown is a stopwatch with extra steps, and the tool has one
    // of those already.
    if (_setSec < 5) _setSec = 5;
    if (_setSec > 99 * 60) _setSec = 99 * 60;
    prefs().putInt("t_set", _setSec);
    if (_running) {
      // Adjust the live deadline instead of restarting the countdown.
      const uint32_t left = remainMs();
      const long adjusted = (long)left + deltaSec * 1000L;
      _endMs = millis() + (uint32_t)(adjusted < 0 ? 0 : adjusted);
    } else {
      _remainMs = (uint32_t)_setSec * 1000u;
    }
    host().beep(0);
    host().refresh(false);
  }

  // --- stopwatch ---------------------------------------------------------
  void renderStopwatch(ToolsCanvas& c) {
    using namespace timerui;
    c.button(SW_START.x, SW_START.y, SW_START.w, SW_START.h, _running ? "STOP" : "START", true,
             TS_LARGE);
    c.button(SW_RESET.x, SW_RESET.y, SW_RESET.w, SW_RESET.h, "RESET", false);
    c.button(SW_LAP.x, SW_LAP.y, SW_LAP.w, SW_LAP.h, "LAP", false);

    // The caption belongs to the buttons it explains, not to the bottom of the
    // panel: it used to sit 250 px below the last lap with nothing in between.
    if (_running && swElapsed() > 60000u)
      c.textCentered(c.width() / 2, SW_START.y + SW_START.h + 12, "the display steps every 10s",
                     TS_MED, true);

    if (_lapN > 0) {
      c.drawLine(40, 412, 440, 412, 1, true);
      c.text(40, 422, "LAPS", TS_MED, true);
      char buf[24], t[16];
      for (int i = 0; i < _lapN; i++) {
        formatMs(t, sizeof(t), _laps[i]);
        snprintf(buf, sizeof(buf), "%d   %s", i + 1, t);
        c.text(56, 464 + i * 44, buf, TS_LARGE, true);
      }
    }
  }

  void tapStopwatch(int x, int y) {
    using namespace timerui;
    if (SW_START.hit(x, y)) {
      if (_running) {
        _swAccum = swElapsed();
        _running = false;
      } else {
        _swStart = millis();
        _running = true;
        _lastShown = -1;
      }
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (SW_RESET.hit(x, y)) {
      _running = false;
      _swAccum = 0;
      _lapN = 0;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (SW_LAP.hit(x, y) && _lapN < kLaps) {
      _laps[_lapN++] = swElapsed();
      host().beep(0);
      host().refresh(false);
    }
  }

  void switchMode(bool stopwatch) {
    _stopwatch = stopwatch;
    _running = false;
    _finished = false;
    _lastShown = -1;
    if (stopwatch) {
      _swAccum = 0;
      _lapN = 0;
    } else {
      _remainMs = (uint32_t)_setSec * 1000u;
    }
    host().beep(1);
    host().refreshUi();
  }

  // --- helpers -----------------------------------------------------------
  uint32_t remainMs() const {
    if (!_running) return _remainMs;
    const uint32_t now = millis();
    return (_endMs > now) ? (_endMs - now) : 0u;
  }
  uint32_t swElapsed() const {
    return _running ? _swAccum + (millis() - _swStart) : _swAccum;
  }
  uint32_t displayMs() const { return _stopwatch ? swElapsed() : remainMs(); }

  static void formatMs(char* out, size_t n, uint32_t ms) {
    // Round the countdown up so a fresh 5:00 timer reads 05:00, not 04:59.
    const uint32_t sec = (ms + 999u) / 1000u;
    uint32_t m = sec / 60, s = sec % 60;
    if (m > 99) m = 99;
    snprintf(out, n, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
  }

  static constexpr int kLaps = 6;
  bool _stopwatch = false, _running = false, _finished = false;
  int _setSec = 300;
  uint32_t _remainMs = 0, _endMs = 0;
  uint32_t _swStart = 0, _swAccum = 0;
  uint32_t _laps[kLaps] = {};
  int _lapN = 0;
  int _lastShown = -1;
};
