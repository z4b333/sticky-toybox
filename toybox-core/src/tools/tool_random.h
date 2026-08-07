// Two randomisers behind one screen: a number picker (with an optional
// no-repeat mode) and a 52-card deck that deals without replacement.
#pragma once
#include <esp_random.h>

#include "tools_draw.h"

namespace randui {
inline constexpr TRect MODE_NUM{40, 50, 190, 48};
inline constexpr TRect MODE_CARD{250, 50, 190, 48};

inline constexpr int PRESET_Y = 386, PRESET_W = 86, PRESET_H = 50, PRESET_GAP = 6;
inline constexpr int PRESET_X0 = 13;
inline constexpr int PRESET_MAX[5] = {6, 10, 20, 100, 1000};
inline constexpr int STEP = 38;
inline constexpr int STEP_Y = 480;
inline constexpr TRect MIN_DN{40, STEP_Y, STEP, STEP};
inline constexpr TRect MIN_UP{178, STEP_Y, STEP, STEP};
inline constexpr TRect MAX_DN{260, STEP_Y, STEP, STEP};
inline constexpr TRect MAX_UP{398, STEP_Y, STEP, STEP};
inline constexpr TRect UNIQUE{40, 560, 180, 62};
inline constexpr TRect DRAW_NUM{240, 560, 200, 62};

inline constexpr TRect CARD_BOX{140, 112, 200, 280};
inline constexpr TRect DRAW_CARD{40, 548, 200, 66};
inline constexpr TRect SHUFFLE{260, 548, 180, 66};

inline TRect presetRect(int i) {
  return TRect{PRESET_X0 + i * (PRESET_W + PRESET_GAP), PRESET_Y, PRESET_W, PRESET_H};
}
}  // namespace randui

class RandomTool : public ToolApp {
 public:
  const char* title() const override { return _cardMode ? "CARD DRAW" : "RANDOM NUMBER"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _cardMode = false;
    _min = prefs().getInt("r_min", 1);
    _max = prefs().getInt("r_max", 100);
    if (_min < 0 || _max <= _min || _max > 9999) {
      _min = 1;
      _max = 100;
    }
    _unique = prefs().getBool("r_uniq", false);
    _hasValue = false;
    _histN = 0;
    clearSeen();
    shuffleDeck();
  }

  void render(ToolsCanvas& c) override {
    using namespace randui;
    host().topBar(title());
    c.button(MODE_NUM.x, MODE_NUM.y, MODE_NUM.w, MODE_NUM.h, "NUMBER", !_cardMode);
    c.button(MODE_CARD.x, MODE_CARD.y, MODE_CARD.w, MODE_CARD.h, "CARD", _cardMode);
    if (_cardMode)
      renderCards(c);
    else
      renderNumbers(c);
  }

  void onTap(int x, int y) override {
    using namespace randui;
    if (host().isBackTap(x, y)) {
      host().goHub();
      return;
    }
    if (MODE_NUM.hit(x, y) && _cardMode) return switchMode(false);
    if (MODE_CARD.hit(x, y) && !_cardMode) return switchMode(true);
    if (_cardMode)
      tapCards(x, y);
    else
      tapNumbers(x, y);
  }

 private:
  // --- number mode -------------------------------------------------------
  void renderNumbers(ToolsCanvas& c) {
    using namespace randui;
    char buf[24];
    if (_hasValue) {
      snprintf(buf, sizeof(buf), "%d", _value);
      tdraw::seg7Centered(c, c.width() / 2, 160, 130, buf, true);
    } else {
      c.textCentered(c.width() / 2, 212, "tap DRAW", TS_HUGE, true, true);
    }

    snprintf(buf, sizeof(buf), "%d to %d", _min, _max);
    c.textCentered(c.width() / 2, 330, buf, TS_LARGE, true, true);

    c.text(PRESET_X0, PRESET_Y - 24, "UP TO", TS_MED, true);
    for (int i = 0; i < 5; i++) {
      const TRect r = presetRect(i);
      snprintf(buf, sizeof(buf), "1-%d", PRESET_MAX[i]);
      c.button(r.x, r.y, r.w, r.h, buf, _min == 1 && _max == PRESET_MAX[i], TS_SMALL);
    }

    c.text(40, 454, "MIN", TS_MED, true);
    c.stepper(MIN_DN.x, MIN_DN.y, STEP, "-", _min > 0);
    snprintf(buf, sizeof(buf), "%d", _min);
    c.textInBox(MIN_DN.x + STEP, MIN_DN.y, MIN_UP.x - MIN_DN.x - STEP, STEP, buf, TS_MED, true,
                true);
    c.stepper(MIN_UP.x, MIN_UP.y, STEP, "+", _min < _max - 1);

    c.text(260, 454, "MAX", TS_MED, true);
    c.stepper(MAX_DN.x, MAX_DN.y, STEP, "-", _max > _min + 1);
    snprintf(buf, sizeof(buf), "%d", _max);
    c.textInBox(MAX_DN.x + STEP, MAX_DN.y, MAX_UP.x - MAX_DN.x - STEP, STEP, buf, TS_MED, true,
                true);
    c.stepper(MAX_UP.x, MAX_UP.y, STEP, "+", _max < 9999);

    c.button(UNIQUE.x, UNIQUE.y, UNIQUE.w, UNIQUE.h, _unique ? "NO REPEAT" : "REPEATS OK",
             _unique, TS_MED);
    c.button(DRAW_NUM.x, DRAW_NUM.y, DRAW_NUM.w, DRAW_NUM.h, "DRAW", true, TS_HUGE);

    if (_histN > 0) {
      char line[120] = "recent: ";
      for (int i = 0; i < _histN; i++) {
        char n[12];
        snprintf(n, sizeof(n), "%d  ", _hist[i]);
        strncat(line, n, sizeof(line) - strlen(line) - 1);
      }
      c.textCentered(c.width() / 2, 650, line, TS_MED, true);
    }
  }

  void tapNumbers(int x, int y) {
    using namespace randui;
    for (int i = 0; i < 5; i++) {
      if (presetRect(i).hit(x, y)) {
        _min = 1;
        _max = PRESET_MAX[i];
        saveRange();
        resetDraws();
        host().beep(1);
        host().refresh(true);
        return;
      }
    }
    const int step = _max < 20 ? 1 : (_max < 200 ? 10 : 100);
    if (MIN_DN.hit(x, y) && _min > 0) return adjust(&_min, -1);
    if (MIN_UP.hit(x, y) && _min < _max - 1) return adjust(&_min, +1);
    if (MAX_DN.hit(x, y) && _max > _min + 1) return adjust(&_max, -step);
    if (MAX_UP.hit(x, y) && _max < 9999) return adjust(&_max, +step);
    if (UNIQUE.hit(x, y)) {
      _unique = !_unique;
      prefs().putBool("r_uniq", _unique);
      resetDraws();
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (DRAW_NUM.hit(x, y)) return drawNumber();
  }

  void adjust(int* field, int delta) {
    *field += delta;
    if (_min < 0) _min = 0;
    if (_max <= _min) _max = _min + 1;
    if (_max > 9999) _max = 9999;
    saveRange();
    resetDraws();
    host().beep(0);
    host().refresh(false);
  }

  void saveRange() {
    prefs().putInt("r_min", _min);
    prefs().putInt("r_max", _max);
  }

  int span() const { return _max - _min + 1; }
  bool uniqueUsable() const { return _unique && span() <= kSeen; }

  void clearSeen() {
    memset(_seen, 0, sizeof(_seen));
    _seenCount = 0;
  }
  void resetDraws() {
    clearSeen();
    _histN = 0;
    _hasValue = false;
  }

  void drawNumber() {
    if (uniqueUsable()) {
      if (_seenCount >= span()) clearSeen();  // exhausted: start a new pass
      int v;
      do {
        v = _min + (int)(esp_random() % (uint32_t)span());
      } while (_seen[v - _min]);
      _seen[v - _min] = 1;
      _seenCount++;
      _value = v;
    } else {
      _value = _min + (int)(esp_random() % (uint32_t)span());
    }
    _hasValue = true;
    for (int i = kHist - 1; i > 0; i--) _hist[i] = _hist[i - 1];
    _hist[0] = _value;
    if (_histN < kHist) _histN++;
    host().beep(1);
    host().refresh(true);
  }

  // --- card mode ---------------------------------------------------------
  void renderCards(ToolsCanvas& c) {
    using namespace randui;
    const TRect& b = CARD_BOX;
    c.drawRect(b.x, b.y, b.w, b.h, 3, true);
    if (_dealt == 0) {
      c.textInBox(b.x, b.y, b.w, b.h, "?", TS_HUGE, true, true);
    } else {
      const int card = _deck[_dealt - 1];
      const int suitIdx = card / 13, rank = card % 13;
      const char* rn = tdraw::rankName(rank);
      c.text(b.x + 14, b.y + 12, rn, TS_LARGE, true, true);
      tdraw::suit(c, suitIdx, b.x + 26, b.y + 66, 26, true);
      tdraw::suit(c, suitIdx, b.x + b.w / 2, b.y + b.h / 2, 96, true);
      const int rw = c.textWidth(rn, TS_LARGE, true);
      c.text(b.x + b.w - 14 - rw, b.y + b.h - 12 - c.textHeight(TS_LARGE), rn, TS_LARGE, true,
             true);
      tdraw::suit(c, suitIdx, b.x + b.w - 26, b.y + b.h - 66, 26, true);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", 52 - _dealt);
    c.text(40, 420, "CARDS LEFT", TS_MED, true);
    tdraw::seg7Text(c, 40, 450, 76, buf, true);

    c.button(DRAW_CARD.x, DRAW_CARD.y, DRAW_CARD.w, DRAW_CARD.h, "DRAW", _dealt < 52,
             TS_LARGE);
    c.button(SHUFFLE.x, SHUFFLE.y, SHUFFLE.w, SHUFFLE.h, "SHUFFLE", false);

    if (_dealt > 1) {
      char line[100] = "recent: ";
      for (int i = _dealt - 2; i >= 0 && i > _dealt - 8; i--) {
        const int card = _deck[i];
        char n[10];
        snprintf(n, sizeof(n), "%s%c ", tdraw::rankName(card % 13), suitLetter(card / 13));
        strncat(line, n, sizeof(line) - strlen(line) - 1);
      }
      c.textCentered(c.width() / 2, 636, line, TS_MED, true);
    }
  }

  void tapCards(int x, int y) {
    using namespace randui;
    if (DRAW_CARD.hit(x, y) && _dealt < 52) {
      _dealt++;
      host().beep(1);
      host().refresh(true);
      return;
    }
    if (SHUFFLE.hit(x, y)) {
      shuffleDeck();
      host().beep(1);
      host().refresh(true);
    }
  }

  static char suitLetter(int idx) { return "SHDC"[idx]; }

  void shuffleDeck() {
    for (int i = 0; i < 52; i++) _deck[i] = i;
    for (int i = 51; i > 0; i--) {  // Fisher-Yates
      const int j = (int)(esp_random() % (uint32_t)(i + 1));
      const uint8_t t = _deck[i];
      _deck[i] = _deck[j];
      _deck[j] = t;
    }
    _dealt = 0;
  }

  void switchMode(bool cards) {
    _cardMode = cards;
    host().beep(1);
    host().refresh(true);
  }

  static constexpr int kHist = 10;
  static constexpr int kSeen = 256;  // no-repeat mode only covers ranges this wide

  bool _cardMode = false;
  int _min = 1, _max = 100, _value = 0;
  bool _hasValue = false, _unique = false;
  int _hist[kHist] = {};
  int _histN = 0;
  uint8_t _seen[kSeen] = {};
  int _seenCount = 0;

  uint8_t _deck[52] = {};
  int _dealt = 0;
};
