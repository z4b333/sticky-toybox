// Coin flip: 1, 5 or 10 coins at a time, with a persistent heads/tails tally.
#pragma once
#include <esp_random.h>

#include "tools_draw.h"

namespace coinui {
// v2 (kit "Coin"): one big coin you tap, three numbers, the lean of it.
inline constexpr int STAGE_CX = 240, STAGE_CY = 250, COIN_R = 150;
inline constexpr TRect COIN{STAGE_CX - COIN_R, STAGE_CY - COIN_R, 2 * COIN_R, 2 * COIN_R};
inline constexpr TRect FLIP1{20, 462, 200, 68};
inline constexpr TRect FLIP5{232, 462, 108, 68};
inline constexpr TRect FLIP10{352, 462, 108, 68};
inline constexpr TRect RESET{20, 736, 440, 48};
// The third box opens the full history (owner).
inline constexpr TRect HIST_BOX{20 + 2 * (136 + 16), 556, 136, 92};
// History page
inline constexpr int H_ROW0 = 100, H_ROW_H = 46, H_ROWS = 12;
inline constexpr TRect H_PREV{20, 680, 96, 60};
inline constexpr TRect H_CLEAR{128, 680, 224, 60};
inline constexpr TRect H_NEXT{364, 680, 96, 60};
inline constexpr int HIST_MAX = 300;
}  // namespace coinui

class CoinTool : public ToolApp {
 public:
  const char* title() const override { return _history ? "HISTORY" : "COIN"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _n = 0;  // nothing flipped yet this session
    _heads = prefs().getUInt("c_heads", 0);
    _tails = prefs().getUInt("c_tails", 0);
    _resetArm = false;
    _history = false;
    loadHistory();
  }

  void render(ToolsCanvas& c) override {
    using namespace coinui;
    host().topBar(title());
    if (_history) return renderHistory(c);
    char buf[48];
    const uint32_t total = _heads + _tails;

    if (_n == 0) {
      c.drawCircle(STAGE_CX, STAGE_CY, COIN_R, 6, true);
      c.drawCircle(STAGE_CX, STAGE_CY, COIN_R - 18, 2, true);
      c.textCentered(STAGE_CX, STAGE_CY - 24, "tap to flip", TS_LARGE, true, true);
    } else if (_n == 1) {
      drawBigCoin(c, STAGE_CX, STAGE_CY, COIN_R, _flips[0]);
      snprintf(buf, sizeof(buf), "flip %lu", (unsigned long)total);
      c.textCentered(STAGE_CX, STAGE_CY + 40, buf, TS_MED, !_flips[0]);
    } else {
      drawCoinGrid(c);
    }

    c.button(FLIP1.x, FLIP1.y, FLIP1.w, FLIP1.h, "Flip", true, TS_LARGE);
    c.button(FLIP5.x, FLIP5.y, FLIP5.w, FLIP5.h, "x5", false, TS_LARGE);
    c.button(FLIP10.x, FLIP10.y, FLIP10.w, FLIP10.h, "x10", false, TS_LARGE);

    // Three numbers: heads, tails, the last three.
    const int bw = 136, by = 556, bh = 92;
    char last[12] = "";
    for (int i = 0; i < _lastN; i++) {
      const size_t L = strlen(last);
      snprintf(last + L, sizeof(last) - L, i ? " %c" : "%c", _last[i] ? 'H' : 'T');
    }
    struct S { const char* l; char v[12]; } st[3] = {{"HEADS", ""}, {"TAILS", ""}, {"HISTORY >", ""}};
    snprintf(st[0].v, sizeof(st[0].v), "%lu", (unsigned long)_heads);
    snprintf(st[1].v, sizeof(st[1].v), "%lu", (unsigned long)_tails);
    snprintf(st[2].v, sizeof(st[2].v), "%s", _lastN ? last : "-");
    for (int i = 0; i < 3; i++) {
      const int x = 20 + i * (bw + 16);
      c.drawRect(x, by, bw, bh, 2, true);
      c.text(x + 12, by + 8, st[i].l, TS_MED, true);
      c.text(x + 12, by + 44, st[i].v, TS_LARGE, true, true);
    }
    if (total > 0) {
      tdraw::progressBar(c, 20, 664, c.width() - 40, 12, (int)((_heads * 1000ULL) / total));
      const int pct10 = (int)(_heads * 1000ULL / total);
      const int off = pct10 > 500 ? pct10 - 500 : 500 - pct10;
      snprintf(buf, sizeof(buf), "%d.%d%% heads  -  %s", pct10 / 10, pct10 % 10,
               total < 20 ? "early days" : (off <= 50 ? "fair enough" : "leaning"));
      c.textCentered(c.width() / 2, 688, buf, TS_MED, true);
    }
    c.button(RESET.x, RESET.y, RESET.w, RESET.h, _resetArm ? "Tap again to reset the tally" : "Reset the tally",
             false, TS_MED);
  }

  void onTap(int x, int y) override {
    using namespace coinui;
    if (_history) return tapHistory(x, y);
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    const bool armed = _resetArm;
    _resetArm = false;
    if (COIN.hit(x, y)) return flip(_batch);  // the coin repeats the last kind of flip
    if (HIST_BOX.hit(x, y)) {
      _history = true;
      _hPage = 0;
      _clearArm = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (FLIP1.hit(x, y)) return flip(1);
    if (FLIP5.hit(x, y)) return flip(5);
    if (FLIP10.hit(x, y)) return flip(10);
    if (RESET.hit(x, y)) {
      if (!armed) {
        _resetArm = true;  // two taps: a tally is a long time to build
        host().beep(0);
        host().refresh(false);
        return;
      }
      _heads = _tails = 0;
      _lastN = 0;
      prefs().putUInt("c_heads", 0);
      prefs().putUInt("c_tails", 0);
      host().beep(2);
      host().refreshUi();
      return;
    }
    if (armed) host().refresh(false);
  }

  // DOWN flips one, as the kit's foot says.
  bool onButton(SideBtn b) override {
    if (_history) {
      if (b == SideBtn::Up && _hPage > 0) _hPage--;
      else if (b == SideBtn::Down && _hPage + 1 < hPages()) _hPage++;
      else return b == SideBtn::Up || b == SideBtn::Down;
      host().beep(0);
      host().refresh(false);
      return true;
    }
    if (b != SideBtn::Down) return false;
    flip(1);
    return true;
  }
  void onSwipe(int dx, int dy) override {
    (void)dy;
    if (!_history || dx == 0) return;
    onButton(dx < 0 ? SideBtn::Down : SideBtn::Up);
  }

  int hostFlips() const { return _hn; }
  int hostBatch() const { return _batch; }
  bool hostHistory() const { return _history; }

 private:
  void flip(int n) {
    _n = n;
    _batch = n;
    for (int i = 0; i < n; i++) {
      const bool headsUp = (esp_random() & 1u) != 0;
      _flips[i] = headsUp;
      // The last three, newest last.
      if (_lastN < 3) _last[_lastN++] = headsUp;
      else {
        _last[0] = _last[1];
        _last[1] = _last[2];
        _last[2] = headsUp;
      }
      if (headsUp)
        _heads++;
      else
        _tails++;
    }
    prefs().putUInt("c_heads", _heads);
    prefs().putUInt("c_tails", _tails);
    for (int i = 0; i < n; i++) pushHistory(_flips[i], i == 0);
    saveHistory();
    host().beep(1);
    // Big solid shapes ghost badly under a differential refresh — repaint fully.
    host().refresh(true);
  }

  // Heads = a white coin, black word; tails = a black coin, white word.
  static void drawBigCoin(ToolsCanvas& c, int cx, int cy, int r, bool headsUp) {
    if (headsUp) {
      c.fillCircle(cx, cy, r, false);
      c.drawCircle(cx, cy, r, 6, true);
      c.drawCircle(cx, cy, r - 18, 2, true);
    } else {
      c.fillCircle(cx, cy, r, true);
      c.drawCircle(cx, cy, r - 18, 2, false);
    }
    c.textCentered(cx, cy - 36, headsUp ? "HEADS" : "TAILS", TS_HUGE, headsUp, true);
  }

  void drawCoinGrid(ToolsCanvas& c) {
    constexpr int perRow = 4, r = 44, stepX = 108, stepY = 104;
    const int rows = (_n + perRow - 1) / perRow;
    const int y0 = coinui::STAGE_CY - ((rows - 1) * stepY) / 2;
    for (int i = 0; i < _n; i++) {
      const int col = i % perRow;
      const int row = i / perRow;
      const int inRow = (row == rows - 1) ? (_n - row * perRow) : perRow;
      const int x0 = coinui::STAGE_CX - ((inRow - 1) * stepX) / 2;
      const int cx = x0 + col * stepX;
      const int cy = y0 + row * stepY;
      if (_flips[i]) {
        c.drawCircle(cx, cy, r, 4, true);
        c.textInBox(cx - r, cy - r, 2 * r, 2 * r, "H", TS_LARGE, true, true);
      } else {
        c.fillCircle(cx, cy, r, true);
        c.textInBox(cx - r, cy - r, 2 * r, 2 * r, "T", TS_LARGE, false, true);
      }
    }
  }

  // --- history: the last 300 flips, and where each tap started -------------
  // Kept as two bit rows, oldest first, in one NVS blob.
  struct HistBlob {
    uint16_t n;
    uint8_t heads[(coinui::HIST_MAX + 7) / 8];
    uint8_t start[(coinui::HIST_MAX + 7) / 8];
  };
  static bool hbit(const uint8_t* a, int i) { return (a[i >> 3] >> (i & 7)) & 1; }
  static void setBit(uint8_t* a, int i, bool v) {
    if (v) a[i >> 3] |= (uint8_t)(1u << (i & 7));
    else a[i >> 3] &= (uint8_t)~(1u << (i & 7));
  }
  void loadHistory() {
    HistBlob b{};
    if (prefs().isKey("c_hist") && prefs().getBytes("c_hist", &b, sizeof(b)) == sizeof(b) &&
        b.n <= coinui::HIST_MAX) {
      _hn = b.n;
      for (int i = 0; i < _hn; i++) {
        _hHeads[i] = hbit(b.heads, i);
        _hStart[i] = hbit(b.start, i);
      }
    } else {
      _hn = 0;
    }
  }
  void saveHistory() {
    HistBlob b{};
    b.n = (uint16_t)_hn;
    for (int i = 0; i < _hn; i++) {
      setBit(b.heads, i, _hHeads[i]);
      setBit(b.start, i, _hStart[i]);
    }
    prefs().putBytes("c_hist", &b, sizeof(b));
  }
  void pushHistory(bool heads, bool startsTap) {
    if (_hn == coinui::HIST_MAX) {
      memmove(_hHeads, _hHeads + 1, coinui::HIST_MAX - 1);
      memmove(_hStart, _hStart + 1, coinui::HIST_MAX - 1);
      _hStart[0] = true;  // whatever was cut, the oldest left starts a row
      _hn--;
    }
    _hHeads[_hn] = heads;
    _hStart[_hn] = startsTap;
    _hn++;
  }
  // Taps, newest first: tapStart(k) is the index where the k-th newest began.
  int taps() const {
    int t = 0;
    for (int i = 0; i < _hn; i++) t += _hStart[i] ? 1 : 0;
    return t;
  }
  int tapStart(int k) const {
    int seen = 0;
    for (int i = _hn - 1; i >= 0; i--)
      if (_hStart[i] && seen++ == k) return i;
    return 0;
  }
  int tapEnd(int start) const {
    int e = start + 1;
    while (e < _hn && !_hStart[e]) e++;
    return e;
  }
  int hPages() const {
    const int t = taps();
    return t ? (t + coinui::H_ROWS - 1) / coinui::H_ROWS : 1;
  }
  void streaks(int& h, int& t) const {
    h = t = 0;
    int run = 0;
    for (int i = 0; i < _hn; i++) {
      run = (i && _hHeads[i] == _hHeads[i - 1]) ? run + 1 : 1;
      if (_hHeads[i] && run > h) h = run;
      if (!_hHeads[i] && run > t) t = run;
    }
  }

  void renderHistory(ToolsCanvas& c) {
    using namespace coinui;
    char buf[64];
    int sh, st;
    streaks(sh, st);
    snprintf(buf, sizeof(buf), "longest run: %d heads, %d tails", sh, st);
    c.text(20, 52, buf, TS_MED, true);
    c.fillRect(20, 90, 440, 1, true);
    if (_hn == 0) c.textInBox(20, H_ROW0, 440, 200, "no flips yet", TS_LARGE, true);
    const int t = taps();
    for (int r = 0; r < H_ROWS; r++) {
      const int k = _hPage * H_ROWS + r;
      if (k >= t) break;
      const int s0 = tapStart(k), e = tapEnd(s0), n = e - s0;
      int heads = 0, w = 0;
      char faces[32] = "";
      for (int i = s0; i < e && w < (int)sizeof(faces) - 3; i++) {
        heads += _hHeads[i] ? 1 : 0;
        faces[w++] = _hHeads[i] ? 'H' : 'T';
        faces[w++] = ' ';
      }
      if (w) faces[w - 1] = 0;
      const int y = H_ROW0 + r * H_ROW_H;
      snprintf(buf, sizeof(buf), "x%d", n);
      c.text(20, y + 8, buf, TS_MED, true);
      c.text(80, y + 8, faces, TS_MED, true, true);
      if (n > 1) {
        snprintf(buf, sizeof(buf), "%d heads", heads);
        c.text(460 - c.textWidth(buf, TS_MED), y + 8, buf, TS_MED, true);
      }
      c.fillRect(20, y + H_ROW_H - 2, 440, 1, true);
    }
    snprintf(buf, sizeof(buf), "page %d of %d  -  %d flips kept", _hPage + 1, hPages(), _hn);
    c.textCentered(c.width() / 2, 646, buf, TS_MED, true);
    c.button(H_PREV.x, H_PREV.y, H_PREV.w, H_PREV.h, "<", false, TS_LARGE);
    c.button(H_CLEAR.x, H_CLEAR.y, H_CLEAR.w, H_CLEAR.h, _clearArm ? "Tap again to clear" : "Clear history",
             _clearArm, TS_MED);
    c.button(H_NEXT.x, H_NEXT.y, H_NEXT.w, H_NEXT.h, ">", false, TS_LARGE);
  }

  void tapHistory(int x, int y) {
    using namespace coinui;
    const bool armed = _clearArm;
    _clearArm = false;
    if (host().isBackTap(x, y)) {
      _history = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (H_PREV.hit(x, y)) {
      onButton(SideBtn::Up);
      return;
    }
    if (H_NEXT.hit(x, y)) {
      onButton(SideBtn::Down);
      return;
    }
    if (H_CLEAR.hit(x, y)) {
      if (!armed) {
        _clearArm = true;
        host().beep(0);
        host().refresh(false);
        return;
      }
      _hn = 0;
      _hPage = 0;
      _lastN = 0;
      saveHistory();
      host().beep(2);
      host().refresh(false);
      return;
    }
    if (armed) host().refresh(false);
  }

  bool _hHeads[coinui::HIST_MAX] = {};
  bool _hStart[coinui::HIST_MAX] = {};
  int _hn = 0, _hPage = 0, _batch = 1;
  bool _history = false, _clearArm = false;

  bool _flips[10] = {};
  bool _last[3] = {};
  int _lastN = 0;
  bool _resetArm = false;
  int _n = 0;
  uint32_t _heads = 0, _tails = 0;
};
