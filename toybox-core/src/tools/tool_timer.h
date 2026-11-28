// Countdown timer + stopwatch.
//
// E-ink note: a 1 Hz full-frame refresh is wasteful and ghosts, so the display
// only repaints every second inside the last minute; above that it steps every
// 10 seconds (and says so). The time shown is always current — just coarser.
#pragma once
#include "big_digits.h"
#include "numpad.h"
#include "tools_draw.h"

namespace timerui {
inline constexpr TRect MODE_CD{20, 60, 220, 48};
inline constexpr TRect MODE_SW{240, 60, 220, 48};

// countdown
inline constexpr int PRESET_W = 104, PRESET_H = 52, PRESET_GAP = 8;
inline constexpr int PRESET_X0 = 20, PRESET_Y0 = 312, PRESET_Y1 = 372;
// The presets a first start has; the owner's own replace them (24 Nov: the
// time is typed on a keypad, and "Save as preset" keeps it as a chip).
inline constexpr int MAX_PRESETS = 8;
inline constexpr int PRESET_DEF_MIN[MAX_PRESETS] = {1, 3, 5, 10, 15, 25, 30, 60};
inline constexpr int MIN_SEC = 5, MAX_SEC = 99 * 60 + 59;
inline constexpr TRect CD_START{40, 444, 200, 66};
inline constexpr TRect CD_RESET{260, 444, 180, 66};
inline constexpr TRect SAVE_BTN{20, 532, 440, 60};
// The big time: tap it to type one.
inline constexpr TRect TIME_TAP{20, 112, 440, 144};

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
    _typing = false;
    _armPre = -1;
    loadPresets();
    _setSec = prefs().getInt("t_set", 5 * 60);
    if (_setSec < timerui::MIN_SEC || _setSec > timerui::MAX_SEC) _setSec = 5 * 60;
    _remainMs = (uint32_t)_setSec * 1000u;
    _running = false;
    _finished = false;
    _swAccum = 0;
    _lapN = 0;
    _lastShown = -1;
  }

  bool wantsTick() const override { return _running; }
  // Naps are fine until the last few seconds, which want the 3-2-1 beeps on
  // time rather than up to a second late.
  bool napsOk() const override { return _running && (_stopwatch || remainMs() > 5000u); }

  void render(ToolsCanvas& c) override {
    using namespace timerui;
    if (_typing) return renderKeypad(c);
    host().topBar(title());

    choice(c, MODE_CD, "Countdown", !_stopwatch);
    choice(c, MODE_SW, "Stopwatch", _stopwatch);

    // The time in the UI face's digits, bold and big (owner), the colon drawn.
    char buf[16];
    formatMs(buf, sizeof(buf), _stopwatch ? swElapsed() : remainMs());
    bigTime(c, c.width() / 2, 122, buf);

    if (_stopwatch)
      renderStopwatch(c);
    else
      renderCountdown(c);

    if (_finished) {
      c.fillRect(20, 132, 440, 110, true);
      c.textInBox(20, 132, 440, 110, "Time is up", TS_HUGE, false, true);
    }
  }

  static void choice(ToolsCanvas& c, const TRect& r, const char* label, bool on) {
    c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
    c.textInBox(r.x, r.y, r.w, r.h, label, TS_MED, true, on);
  }

  // "25:00": digits from big_digits, the colon as two squares.
  static void bigTime(ToolsCanvas& c, int cx, int y, const char* t) {
    char a[8] = "", b[8] = "";
    const char* colon = strchr(t, ':');
    if (!colon) return bigdigits::drawCentered(c, cx, y, t, true);
    snprintf(a, sizeof(a), "%.*s", (int)(colon - t), t);
    snprintf(b, sizeof(b), "%s", colon + 1);
    const int wa = bigdigits::width(a), wb = bigdigits::width(b), gap = 34;
    const int x0 = cx - (wa + gap + wb) / 2;
    bigdigits::drawCentered(c, x0 + wa / 2, y, a, true);
    bigdigits::drawCentered(c, x0 + wa + gap + wb / 2, y, b, true);
    const int sq = 12, mx = x0 + wa + gap / 2 - sq / 2;
    c.fillRect(mx, y + bigdigits::H * 3 / 10, sq, sq, true);
    c.fillRect(mx, y + bigdigits::H * 7 / 10 - sq, sq, sq, true);
  }

  void onTap(int x, int y) override {
    using namespace timerui;
    if (_typing) return tapKeypad(x, y);
    if (host().isBackTap(x, y)) {
      host().beep(1);
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

  // Hold a preset: it offers Remove.
  void onHold(int x, int y) override {
    if (_typing || _stopwatch || _finished) return;
    for (int i = 0; i < _nPre; i++)
      if (timerui::presetRect(i).hit(x, y)) {
        _armPre = i;
        host().beep(0);
        host().refresh(false);
        return;
      }
  }

  int hostSetSec() const { return _setSec; }
  int hostPresets() const { return _nPre; }
  int hostPreset(int i) const { return i >= 0 && i < _nPre ? _pre[i] : -1; }
  bool hostTyping() const { return _typing; }

  void tick() override {
    if (!_running) return;
    if (!_stopwatch) {
      // The last three seconds count down out loud (owner, 21 Nov): one
      // short beep at 3, 2 and 1, then the alarm at 0.
      const uint32_t rem = remainMs();
      const int left = (int)((rem + 999u) / 1000u);
      if (rem > 0 && left <= 3 && left != _warned) {
        _warned = left;
        host().beep(1);
      }
    }
    if (!_stopwatch && remainMs() == 0) {
      _warned = 0;
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
    tdraw::progressBar(c, 20, 262, 440, 12, permille);

    char buf[40];
    c.text(PRESET_X0, PRESET_Y0 - 30, "PRESETS", TS_MED, true);
    if (_nPre == 0)
      c.textInBox(PRESET_X0, PRESET_Y0, 440, PRESET_Y1 + PRESET_H - PRESET_Y0, "none yet: save one below", TS_MED,
                  true);
    for (int i = 0; i < _nPre; i++) {
      const TRect r = presetRect(i);
      if (i == _armPre) {
        c.button(r.x, r.y, r.w, r.h, "Remove", true, TS_MED);
        continue;
      }
      presetLabel(_pre[i], buf, sizeof(buf));
      choice(c, r, buf, _pre[i] == _setSec);
    }

    c.button(CD_START.x, CD_START.y, CD_START.w, CD_START.h, _running ? "Pause" : "Start", true, TS_LARGE);
    c.button(CD_RESET.x, CD_RESET.y, CD_RESET.w, CD_RESET.h, "Reset", false, TS_LARGE);

    // Save the time on the clock as a chip -- or say why it cannot be.
    char t[12];
    presetLabel(_setSec, t, sizeof(t));
    const int have = presetIndex(_setSec);
    if (have >= 0)
      snprintf(buf, sizeof(buf), "%s is a preset", t);
    else if (_nPre >= MAX_PRESETS)
      snprintf(buf, sizeof(buf), "8 presets: hold one to remove it");
    else
      snprintf(buf, sizeof(buf), "Save %s as a preset", t);
    if (have < 0 && _nPre < MAX_PRESETS) {
      c.button(SAVE_BTN.x, SAVE_BTN.y, SAVE_BTN.w, SAVE_BTN.h, buf, false, TS_MED);
    }
    // d18: nothing when the time is already a preset, or when all eight are
    // taken -- the filled chip says the first; the "?" card the second.
  }

  void tapCountdown(int x, int y) {
    using namespace timerui;
    const int armed = _armPre;
    _armPre = -1;
    if (armed >= 0 && presetRect(armed).hit(x, y)) return removePreset(armed);
    if (!_running && TIME_TAP.hit(x, y)) {
      _typing = true;
      _nd = 0;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (SAVE_BTN.hit(x, y)) return savePreset();
    for (int i = 0; i < _nPre; i++) {
      if (presetRect(i).hit(x, y)) {
        if (armed >= 0) break;  // a tap away from Remove just puts it back
        _setSec = _pre[i];
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
        _warned = 0;
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
    if (armed >= 0) host().refresh(false);
  }

  // --- typing the time -------------------------------------------------------
  // Digits come in from the right, as on a microwave: 2 5 0 0 is 25:00, 9 0 is
  // 00:90, which is a minute and a half. Five seconds to 99:59.
  int typedSec() const {
    int v = 0;
    for (int i = 0; i < _nd; i++) v = v * 10 + (_dig[i] - '0');
    return (v / 100) * 60 + v % 100;
  }

  void renderKeypad(ToolsCanvas& c) {
    using namespace timerui;
    host().topBar("SET THE TIME");
    char buf[12];
    if (_nd) {
      int v = 0;
      for (int i = 0; i < _nd; i++) v = v * 10 + (_dig[i] - '0');
      snprintf(buf, sizeof(buf), "%02d:%02d", v / 100, v % 100);
    } else {
      formatMs(buf, sizeof(buf), (uint32_t)_setSec * 1000u);
    }
    bigTime(c, c.width() / 2, 96, buf);
    c.textCentered(c.width() / 2, 240, "minutes, then seconds", TS_MED, true);
    if (_nd && (typedSec() < MIN_SEC || typedSec() > MAX_SEC))
      c.textCentered(c.width() / 2, 270, "5 seconds to 99:59", TS_MED, true, true);
    numpad::draw(c);
  }

  void tapKeypad(int x, int y) {
    using namespace timerui;
    if (host().isBackTap(x, y)) {
      _typing = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    const int k = numpad::hit(x, y);
    if (k < 0) return;
    if (k == numpad::CLEAR) {
      _nd = 0;
    } else if (k == numpad::DONE) {
      if (_nd == 0) {  // nothing typed: keep the time there was
        _typing = false;
        host().beep(1);
        host().refreshUi();
        return;
      }
      const int sec = typedSec();
      if (sec < MIN_SEC || sec > MAX_SEC) {
        host().beep(2);
        host().refresh(false);
        return;
      }
      _setSec = sec;
      prefs().putInt("t_set", _setSec);
      _remainMs = (uint32_t)_setSec * 1000u;
      _running = false;
      _typing = false;
      host().beep(1);
      host().refreshUi();
      return;
    } else {
      if (_nd >= 4) {
        host().beep(2);
        return;
      }
      if (_nd == 0 && numpad::digit(k) == 0) {  // a leading zero changes nothing
        host().beep(0);
        return;
      }
      _dig[_nd++] = (char)('0' + numpad::digit(k));
    }
    host().beep(0);
    host().refresh(false);
  }

  // --- presets ----------------------------------------------------------------
  // NVS "t_pre": a count, then up to eight times in seconds.
  void loadPresets() {
    using namespace timerui;
    uint16_t b[1 + MAX_PRESETS] = {};
    if (prefs().getBytes("t_pre", b, sizeof(b)) >= sizeof(uint16_t) && b[0] <= MAX_PRESETS) {
      _nPre = 0;
      for (int i = 0; i < b[0]; i++)
        if (b[1 + i] >= MIN_SEC && b[1 + i] <= MAX_SEC) _pre[_nPre++] = b[1 + i];
      return;
    }
    _nPre = MAX_PRESETS;
    for (int i = 0; i < MAX_PRESETS; i++) _pre[i] = PRESET_DEF_MIN[i] * 60;
  }
  void savePresets() {
    uint16_t b[1 + timerui::MAX_PRESETS] = {};
    b[0] = (uint16_t)_nPre;
    for (int i = 0; i < _nPre; i++) b[1 + i] = (uint16_t)_pre[i];
    prefs().putBytes("t_pre", b, sizeof(b));
  }
  int presetIndex(int sec) const {
    for (int i = 0; i < _nPre; i++)
      if (_pre[i] == sec) return i;
    return -1;
  }
  // Whole minutes read "25 min"; anything else "2:30".
  static void presetLabel(int sec, char* out, size_t n) {
    if (sec % 60 == 0)
      snprintf(out, n, "%d min", sec / 60);
    else
      snprintf(out, n, "%d:%02d", sec / 60, sec % 60);
  }

  // Kept in order, shortest first, so the chips read like a ruler.
  void savePreset() {
    if (presetIndex(_setSec) >= 0 || _nPre >= timerui::MAX_PRESETS) {
      host().beep(2);
      return;
    }
    int at = _nPre;
    while (at > 0 && _pre[at - 1] > _setSec) {
      _pre[at] = _pre[at - 1];
      at--;
    }
    _pre[at] = _setSec;
    _nPre++;
    savePresets();
    host().beep(1);
    host().refresh(false);
  }

  void removePreset(int i) {
    for (int j = i; j + 1 < _nPre; j++) _pre[j] = _pre[j + 1];
    _nPre--;
    savePresets();
    host().beep(2);
    host().refresh(false);
  }

  // --- stopwatch ---------------------------------------------------------
  void renderStopwatch(ToolsCanvas& c) {
    using namespace timerui;
    c.button(SW_START.x, SW_START.y, SW_START.w, SW_START.h, _running ? "Stop" : "Start", true, TS_LARGE);
    c.button(SW_RESET.x, SW_RESET.y, SW_RESET.w, SW_RESET.h, "Reset", false, TS_MED);
    c.button(SW_LAP.x, SW_LAP.y, SW_LAP.w, SW_LAP.h, "Lap", false, TS_MED);

    // The caption belongs to the buttons it explains, not to the bottom of the
    // panel: it used to sit 250 px below the last lap with nothing in between.
    if (_running && swElapsed() > 60000u)
      c.textCentered(c.width() / 2, SW_START.y + SW_START.h + 12, "the time steps every 10 s", TS_MED, true);

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
  int _warned = 0;  // the last of the 3-2-1 warning beeps sounded

  bool _typing = false;
  char _dig[4] = {};
  int _nd = 0;
  int _pre[timerui::MAX_PRESETS] = {};
  int _nPre = 0, _armPre = -1;
};
