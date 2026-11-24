// Two randomisers behind one screen: a number picker (with an optional
// no-repeat mode) and a 52-card deck that deals without replacement.
#pragma once
#include <esp_random.h>

#include "big_digits.h"
#include "numpad.h"
#include "tools_draw.h"

namespace randui {
// v2 (kit "Random"): Number | Card; from and to; the result big in a box
// you can tap; no repeats and what came before on one line.
inline constexpr TRect MODE_NUM{20, 52, 220, 52};
inline constexpr TRect MODE_CARD{240, 52, 220, 52};

// From and To are boxes: a tap types the number on the keypad (owner).
inline constexpr TRect FROM_BOX{96, 120, 140, 56};
inline constexpr TRect TO_BOX{320, 120, 140, 56};
inline constexpr int PRESET_Y = 196, PRESET_W = 84, PRESET_H = 48, PRESET_GAP = 5;
inline constexpr int PRESET_X0 = 20;
inline constexpr int PRESET_MAX[5] = {10, 20, 50, 100, 1000};
inline constexpr TRect BOX{20, 262, 440, 250};
// No repeats: a switch, plainly on or off (owner: the old button did not say).
inline constexpr TRect UNIQUE{20, 528, 440, 56};
inline constexpr int VAL_MAX = 9999;
inline constexpr TRect DRAW_NUM{20, 724, 440, 64};

inline constexpr TRect CARD_BOX{110, 124, 260, 364};
inline constexpr TRect DRAW_CARD{20, 724, 214, 64};
inline constexpr TRect SHUFFLE{246, 724, 214, 64};

inline TRect presetRect(int i) {
  return TRect{PRESET_X0 + i * (PRESET_W + PRESET_GAP), PRESET_Y, PRESET_W, PRESET_H};
}
}  // namespace randui

class RandomTool : public ToolApp {
 public:
  const char* title() const override { return "RANDOM"; }

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
    if (!_cardMode && _typing) return renderKeypad(c);
    choice(c, MODE_NUM, "Number", !_cardMode);
    choice(c, MODE_CARD, "Card", _cardMode);
    if (_cardMode)
      renderCards(c);
    else
      renderNumbers(c);
  }

  void onTap(int x, int y) override {
    using namespace randui;
    if (!_cardMode && _typing) return tapKeypad(x, y);
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    if (MODE_NUM.hit(x, y)) return switchMode(false);
    if (MODE_CARD.hit(x, y)) return switchMode(true);
    if (_cardMode)
      tapCards(x, y);
    else if (_typing)
      tapKeypad(x, y);
    else
      tapNumbers(x, y);
  }

  // DOWN draws, a number or a card (kit).
  bool onButton(SideBtn b) override {
    if (b != SideBtn::Down || _typing) return false;
    if (_cardMode) {
      if (_dealt < 52) {
        _dealt++;
        host().beep(1);
        host().refreshUi();
      }
    } else {
      drawNumber();
    }
    return true;
  }

  int hostMin() const { return _min; }
  int hostMax() const { return _max; }
  bool hostUnique() const { return _unique; }

 private:
  static void choice(ToolsCanvas& c, const TRect& r, const char* label, bool on) {
    c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
    c.textInBox(r.x, r.y, r.w, r.h, label, TS_MED, true, on);
  }
  // --- number mode -------------------------------------------------------
  void renderNumbers(ToolsCanvas& c) {
    using namespace randui;
    char buf[64];
    c.text(20, FROM_BOX.y + 14, "From", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d", _min);
    c.drawRect(FROM_BOX.x, FROM_BOX.y, FROM_BOX.w, FROM_BOX.h, 2, true);
    c.textInBox(FROM_BOX.x, FROM_BOX.y, FROM_BOX.w, FROM_BOX.h, buf, TS_LARGE, true, true);
    c.text(TO_BOX.x - 56, TO_BOX.y + 14, "To", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d", _max);
    c.drawRect(TO_BOX.x, TO_BOX.y, TO_BOX.w, TO_BOX.h, 2, true);
    c.textInBox(TO_BOX.x, TO_BOX.y, TO_BOX.w, TO_BOX.h, buf, TS_LARGE, true, true);
    for (int i = 0; i < 5; i++) {
      const TRect r = presetRect(i);
      snprintf(buf, sizeof(buf), "1-%d", PRESET_MAX[i]);
      choice(c, r, buf, _min == 1 && _max == PRESET_MAX[i]);
    }

    // The result: the UI face's digits, bold and big, in a box you can tap.
    c.drawRect(BOX.x, BOX.y, BOX.w, BOX.h, 3, true);
    if (_hasValue) {
      snprintf(buf, sizeof(buf), "%d", _value);
      bigdigits::drawCentered(c, BOX.x + BOX.w / 2, BOX.y + (BOX.h - bigdigits::H) / 2, buf, true);
    } else {
      c.textInBox(BOX.x, BOX.y, BOX.w, BOX.h, "tap to draw", TS_LARGE, true);
    }

    // No repeats: label, switch, On/Off; then what is left of the round.
    {
      const TRect u = UNIQUE;
      c.text(u.x, u.y + 14, "No repeats", TS_MED, true);
      const int sw = 72, sh = 36, sx = u.x + u.w - sw - 64, sy = u.y + (u.h - sh) / 2;
      tdraw::roundRect(c, sx, sy, sw, sh, sh / 2, 2, true);
      if (_unique) {
        tdraw::fillRound(c, sx, sy, sw, sh, sh / 2, true);
        c.fillCircle(sx + sw - sh / 2, sy + sh / 2, sh / 2 - 6, false);
      } else {
        c.fillCircle(sx + sh / 2, sy + sh / 2, sh / 2 - 6, true);
      }
      c.text(sx + sw + 12, u.y + 14, _unique ? "On" : "Off", TS_MED, true, _unique);
    }
    if (_unique) {
      if (!uniqueUsable())
        snprintf(buf, sizeof(buf), "works for ranges up to %d numbers", kSeen);
      else if (_seenCount >= span())
        snprintf(buf, sizeof(buf), "all drawn - the next one starts again");
      else
        snprintf(buf, sizeof(buf), "%d of %d left", span() - _seenCount, span());
      c.textCentered(c.width() / 2, 600, buf, TS_MED, true);
    }
    if (_histN > 1) {
      int n = snprintf(buf, sizeof(buf), "before:");
      for (int i = 1; i < _histN && i < 7 && n < (int)sizeof(buf) - 8; i++)
        n += snprintf(buf + n, sizeof(buf) - n, " %d", _hist[i]);
      c.textCentered(c.width() / 2, _unique ? 640 : 610, buf, TS_MED, true);
    }
    c.button(DRAW_NUM.x, DRAW_NUM.y, DRAW_NUM.w, DRAW_NUM.h, "Draw", true, TS_LARGE);
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
        host().refreshUi();
        return;
      }
    }
    if (FROM_BOX.hit(x, y) || TO_BOX.hit(x, y)) {
      _typing = FROM_BOX.hit(x, y) ? 1 : 2;
      _nd = 0;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (UNIQUE.hit(x, y)) {
      _unique = !_unique;
      prefs().putBool("r_uniq", _unique);
      resetDraws();
      host().beep(1);
      host().refresh(false);
      return;
    }
    if (DRAW_NUM.hit(x, y) || BOX.hit(x, y)) return drawNumber();
  }

  // --- typing From or To ----------------------------------------------------
  void renderKeypad(ToolsCanvas& c) {
    char buf[40];
    c.textTrackedCentered(c.width() / 2, 70, _typing == 1 ? "FROM" : "TO", TS_MED, true, true, 3);
    if (_nd) snprintf(buf, sizeof(buf), "%.*s", _nd, _dig);
    else snprintf(buf, sizeof(buf), "%d", _typing == 1 ? _min : _max);
    c.textCentered(c.width() / 2, 110, buf, TS_HUGE, true, true);
    if (_typing == 1) snprintf(buf, sizeof(buf), "0 to %d, below %d", _max - 1, _max);
    else snprintf(buf, sizeof(buf), "up to %d, above %d", randui::VAL_MAX, _min);
    c.textCentered(c.width() / 2, 200, buf, TS_MED, true);
    numpad::draw(c);
  }

  void tapKeypad(int x, int y) {
    if (host().isBackTap(x, y)) {
      _typing = 0;
      host().beep(1);
      host().refresh(false);
      return;
    }
    const int k = numpad::hit(x, y);
    if (k < 0) return;
    if (k == numpad::CLEAR) {
      _nd = 0;
    } else if (k == numpad::DONE) {
      if (_nd == 0) {  // nothing typed: keep what was there
        _typing = 0;
        host().beep(1);
        host().refresh(false);
        return;
      }
      int v = 0;
      for (int i = 0; i < _nd; i++) v = v * 10 + (_dig[i] - '0');
      const bool ok = _typing == 1 ? (v < _max) : (v > _min && v <= randui::VAL_MAX);
      if (!ok) {
        host().beep(2);
        return;
      }
      (_typing == 1 ? _min : _max) = v;
      _typing = 0;
      saveRange();
      resetDraws();
      host().beep(1);
      host().refresh(false);
      return;
    } else {
      if (_nd >= 4) {
        host().beep(2);
        return;
      }
      _dig[_nd++] = (char)('0' + numpad::digit(k));
    }
    host().beep(0);
    host().refresh(false);
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
    host().refreshUi();
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
      tdraw::suit(c, suitIdx, b.x + b.w / 2, b.y + b.h / 2, 120, true);
      const int rw = c.textWidth(rn, TS_LARGE, true);
      c.text(b.x + b.w - 14 - rw, b.y + b.h - 12 - c.textHeight(TS_LARGE), rn, TS_LARGE, true,
             true);
      tdraw::suit(c, suitIdx, b.x + b.w - 26, b.y + b.h - 66, 26, true);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", 52 - _dealt);
    c.text(40, 520, "CARDS LEFT", TS_MED, true);
    c.text(40, 556, buf, TS_HUGE, true, true);

    // The cards already out, beside the count rather than squeezed into one
    // line under the buttons. Drawn as rank and suit so they read like cards.
    if (_dealt > 1) {
      c.text(270, 520, "BEFORE THAT", TS_MED, true);
      const int mid = c.textHeight(TS_MED) / 2;
      int y = 556;
      for (int i = _dealt - 2; i >= 0 && i > _dealt - 7; i--, y += 28) {  // the last five (owner)
        const int card = _deck[i];
        c.text(270, y, tdraw::rankName(card % 13), TS_MED, true);
        tdraw::suit(c, card / 13, 300, y + mid, 18, true);
      }
    }

    c.button(DRAW_CARD.x, DRAW_CARD.y, DRAW_CARD.w, DRAW_CARD.h, "Draw", _dealt < 52, TS_LARGE);
    c.button(SHUFFLE.x, SHUFFLE.y, SHUFFLE.w, SHUFFLE.h, "Shuffle", false, TS_LARGE);
  }

  void tapCards(int x, int y) {
    using namespace randui;
    if ((DRAW_CARD.hit(x, y) || CARD_BOX.hit(x, y)) && _dealt < 52) {
      _dealt++;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (SHUFFLE.hit(x, y)) {
      shuffleDeck();
      host().beep(1);
      host().refreshUi();
    }
  }

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
    if (cards == _cardMode) return;
    _cardMode = cards;
    host().beep(1);
    host().refreshUi();
  }

  static constexpr int kHist = 10;
  static constexpr int kSeen = 256;  // no-repeat mode only covers ranges this wide

  bool _cardMode = false;
  int _typing = 0;  // 1 From, 2 To on the keypad
  char _dig[4] = {};
  int _nd = 0;
  int _min = 1, _max = 100, _value = 0;
  bool _hasValue = false, _unique = false;
  int _hist[kHist] = {};
  int _histN = 0;
  uint8_t _seen[kSeen] = {};
  int _seenCount = 0;

  uint8_t _deck[52] = {};
  int _dealt = 0;
};
