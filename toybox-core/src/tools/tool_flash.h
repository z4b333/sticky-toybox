// Flashcards with phone import.
//
// Three screens: pick a deck, study it, or import a new one by pairing with a
// phone over a QR-joined access point (see flash_web.h). Studying supports both
// plain flipping and Leitner spaced repetition; box levels survive reboots and
// re-imports.
#pragma once
#include <esp_random.h>

#include "flash_qr.h"
#include "flash_store.h"
#include "flash_web.h"
#include "tools_draw.h"

namespace fcui {
// deck list
inline constexpr int LIST_X = 20, LIST_Y = 56, LIST_W = 440, ROW_H = 46;
inline constexpr int DEL_W = 34;
inline constexpr int PANEL_X = 20, PANEL_W = 440;
inline constexpr TRect IMPORT_BTN{PANEL_X, 448, PANEL_W, 72};
inline constexpr TRect MODE_BTN{PANEL_X, 548, 300, 52};

// study
// The card gets the room. It used to stop 340 px down an 800 px panel, with the
// grading buttons floating in the middle and a quarter of the screen blank
// beneath them; a card is the one thing on this screen worth looking at, and the
// buttons belong where a thumb already is.
inline constexpr TRect CARD_BOX{20, 60, 440, 470};
inline constexpr int ACT_Y = 596, ACT_H = 88;
inline constexpr TRect AGAIN_BTN{20, ACT_Y, 210, ACT_H};
inline constexpr TRect GOOD_BTN{250, ACT_Y, 210, ACT_H};
inline constexpr TRect NEXT_BTN{120, ACT_Y, 240, ACT_H};
inline constexpr TRect RESTART_BTN{60, ACT_Y, 150, ACT_H};
inline constexpr TRect DECKS_BTN{270, ACT_Y, 150, ACT_H};

// import: a single centred QR keeps the screen quiet. The link QR is a
// fallback behind a button rather than a second thing to read.
inline constexpr int QR_X = 110, QR_Y = 140, QR_SIZE = 260;
// Same bottom band as the notes tool's pairing screen: these two flows look
// alike and are reached the same way, so the buttons land under the same thumb.
inline constexpr TRect ALT_BTN{40, 588, 400, 60};
inline constexpr TRect DONE_BTN{40, 672, 400, 72};

inline TRect rowRect(int i) { return TRect{LIST_X, LIST_Y + i * ROW_H, LIST_W, ROW_H - 6}; }
inline TRect delRect(int i) {
  return TRect{LIST_X + LIST_W - DEL_W - 4, LIST_Y + i * ROW_H + 3, DEL_W, ROW_H - 12};
}
}  // namespace fcui

class FlashTool : public ToolApp {
 public:
  const char* title() const override {
    switch (_screen) {
      case Screen::Study: return _deckName;
      case Screen::Import: return "IMPORT";
      default: return "FLASHCARDS";
    }
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    fcard::fsBegin();
    fcard::ensureSampleDeck();
    _srs = prefs().getBool("fc_srs", true);
    _screen = Screen::Decks;
    refreshDeckList();
  }

  // The import screen must service HTTP while it is open.
  bool wantsTick() const override { return _screen == Screen::Import; }

  void tick() override {
    if (_screen != Screen::Import) return;
    _net.loop();
    if (_net.received() && !_importShown) {
      _importShown = true;
      host().beep(3);
      host().refresh(true);
    }
  }

  void render(ToolsCanvas& c) override {
    host().topBar(title());
    switch (_screen) {
      case Screen::Decks: renderDecks(c); break;
      case Screen::Study: renderStudy(c); break;
      default: renderImport(c); break;
    }
  }

  void onTap(int x, int y) override {
    if (host().isBackTap(x, y)) {
      // Back steps out one screen at a time, then leaves the app.
      if (_screen == Screen::Study) {
        saveProgress();
        releaseCards();
        _screen = Screen::Decks;
        refreshDeckList();
        host().refresh(true);
      } else if (_screen == Screen::Import) {
        closeImport();
      } else {
        host().goHub();
      }
      return;
    }
    switch (_screen) {
      case Screen::Decks: tapDecks(x, y); break;
      case Screen::Study: tapStudy(x, y); break;
      default: tapImport(x, y); break;
    }
  }

 private:
  enum class Screen : uint8_t { Decks, Study, Import };

  // --- deck list ---------------------------------------------------------
  void refreshDeckList() { _deckCount = fcard::listDecks(_decks, fcard::MAX_DECKS); }

  void renderDecks(ToolsCanvas& c) {
    using namespace fcui;
    if (_deckCount == 0) {
      c.drawRect(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, 1, true);
      c.textInBox(LIST_X, LIST_Y, LIST_W, 4 * ROW_H, "no decks yet", TS_MED, true);
    }
    char buf[40];
    for (int i = 0; i < _deckCount; i++) {
      const TRect r = rowRect(i);
      c.drawRect(r.x, r.y, r.w, r.h, 2, true);
      const TSize nsz = scriptFloor(_decks[i].name, TS_MED);
      c.text(r.x + 10, r.y + (r.h - c.textHeight(nsz)) / 2, _decks[i].name, nsz, true);

      // mastery bar + count, right-aligned before the delete button
      snprintf(buf, sizeof(buf), "%d/%d", _decks[i].mastered, _decks[i].cards);
      const int tw = c.textWidth(buf, TS_SMALL);
      const int barW = 90;
      const int barX = r.x + r.w - DEL_W - 14 - tw - 8 - barW;
      const int permille =
          _decks[i].cards ? (_decks[i].mastered * 1000) / _decks[i].cards : 0;
      tdraw::progressBar(c, barX, r.y + (r.h - 16) / 2, barW, 16, permille);
      c.text(barX + barW + 8, r.y + (r.h - c.textHeight(TS_SMALL)) / 2, buf, TS_SMALL, true);

      const TRect d = delRect(i);
      c.drawRect(d.x, d.y, d.w, d.h, 1, true);
      c.textInBox(d.x, d.y, d.w, d.h, "x", TS_MED, true);
    }

    // This half of the screen is the only place in Toybox where captions,
    // buttons, a rule and a numbered list all stack in one column, and it is
    // the only place that broke when the same layout was drawn with the
    // reader's UI faces -- half again as tall as this firmware's, so a caption
    // that cleared the button below it by 8 px landed 4 px inside it. Below,
    // every step is measured from the text that precedes it and floored at the
    // spacing this panel was drawn with: identical here, and it still fits when
    // the glyphs grow.
    const int capH = c.textHeight(TS_MED);
    const auto below = [](int measured, int floorY) { return measured > floorY ? measured : floorY; };

    c.button(IMPORT_BTN.x, IMPORT_BTN.y, IMPORT_BTN.w, IMPORT_BTN.h, "IMPORT", true, TS_LARGE);
    // Hung off the button beneath it rather than the one above: this caption
    // labels the import flow, and the gap that has to survive is the one that
    // would otherwise be an overlap.
    c.text(PANEL_X, MODE_BTN.y - capH - 2, "scan a QR with your phone", TS_MED, true);
    c.button(MODE_BTN.x, MODE_BTN.y, MODE_BTN.w, MODE_BTN.h,
             _srs ? "SPACED REPEAT" : "JUST FLIP", _srs, TS_MED);

    int y = MODE_BTN.y + MODE_BTN.h + 6;
    c.text(PANEL_X, y, _srs ? "hard cards come back" : "shuffle and flip", TS_MED, true);

    y = below(y + capH + 4, 632);
    c.drawLine(PANEL_X, y, c.width() - 20, y, 1, true);
    y += 8;
    c.text(PANEL_X, y, "HOW IT WORKS", TS_MED, true);

    y = below(y + capH + 2, 666);
    const int step = capH + 2 > 26 ? capH + 2 : 26;
    const char* steps[4] = {"1  tap IMPORT", "2  scan to join wifi",
                            "3  page opens on phone", "4  paste or upload cards"};
    for (int i = 0; i < 4; i++) c.text(PANEL_X, y + i * step, steps[i], TS_MED, true);

    int totalCards = 0;
    for (int i = 0; i < _deckCount; i++) totalCards += _decks[i].cards;
    snprintf(buf, sizeof(buf), "%d decks  %d cards", _deckCount, totalCards);
    c.text(PANEL_X, below(y + 3 * step + capH + 4, 770), buf, TS_MED, true);
  }

  void tapDecks(int x, int y) {
    using namespace fcui;
    for (int i = 0; i < _deckCount; i++) {
      if (delRect(i).hit(x, y)) {
        fcard::deleteDeck(_decks[i].name);
        refreshDeckList();
        host().beep(2);
        host().refresh(true);
        return;
      }
      if (rowRect(i).hit(x, y)) return startStudy(i);
    }
    if (IMPORT_BTN.hit(x, y)) return openImport();
    if (MODE_BTN.hit(x, y)) {
      _srs = !_srs;
      prefs().putBool("fc_srs", _srs);
      host().beep(1);
      host().refresh(false);
    }
  }

  // --- study -------------------------------------------------------------
  void startStudy(int deckIdx) {
    strncpy(_deckName, _decks[deckIdx].name, fcard::NAME_LEN);
    _deckName[fcard::NAME_LEN] = 0;
    if (!_cards) _cards = (fcard::Card*)malloc(sizeof(fcard::Card) * fcard::MAX_CARDS);
    if (!_cards) {
      host().beep(2);
      return;
    }
    _cardCount = fcard::loadDeck(_deckName, _cards, fcard::MAX_CARDS);
    if (_cardCount == 0) {
      releaseCards();
      host().beep(2);
      return;
    }
    buildQueue();
    _pos = 0;
    _flipped = false;
    _graded = 0;
    _screen = Screen::Study;
    host().beep(1);
    host().refresh(true);
  }

  // Shuffle, then bring low Leitner boxes to the front. Cards you keep getting
  // wrong lead the session; mastered ones trail it.
  void buildQueue() {
    _qLen = _cardCount;
    for (int i = 0; i < _qLen; i++) _queue[i] = (uint16_t)i;
    for (int i = _qLen - 1; i > 0; i--) {
      const int j = (int)(esp_random() % (uint32_t)(i + 1));
      const uint16_t t = _queue[i];
      _queue[i] = _queue[j];
      _queue[j] = t;
    }
    if (!_srs) return;
    for (int i = 1; i < _qLen; i++) {  // insertion sort keeps the shuffle stable
      const uint16_t v = _queue[i];
      int j = i - 1;
      while (j >= 0 && _cards[_queue[j]].box > _cards[v].box) {
        _queue[j + 1] = _queue[j];
        j--;
      }
      _queue[j + 1] = v;
    }
  }

  void renderStudy(ToolsCanvas& c) {
    using namespace fcui;
    if (_pos >= _qLen) return renderSessionDone(c);

    const fcard::Card& card = _cards[_queue[_pos]];
    c.drawRect(CARD_BOX.x, CARD_BOX.y, CARD_BOX.w, CARD_BOX.h, _flipped ? 4 : 2, true);
    drawWrapped(c, _flipped ? card.back : card.front, CARD_BOX);

    if (!_flipped) {
      // Sat in the band the buttons will occupy, so the answer appears where the
      // eye is already waiting rather than somewhere new.
      c.textInBox(NEXT_BTN.x, ACT_Y, NEXT_BTN.w, ACT_H, "tap the card to reveal", TS_MED, true);
    } else if (_srs) {
      c.button(AGAIN_BTN.x, AGAIN_BTN.y, AGAIN_BTN.w, AGAIN_BTN.h, "AGAIN", false, TS_LARGE);
      c.button(GOOD_BTN.x, GOOD_BTN.y, GOOD_BTN.w, GOOD_BTN.h, "GOT IT", true, TS_LARGE);
    } else {
      c.button(NEXT_BTN.x, NEXT_BTN.y, NEXT_BTN.w, NEXT_BTN.h, "NEXT", true, TS_LARGE);
    }

    // Where you are in the deck is the least urgent thing here, so it takes the
    // strip below the buttons rather than a slot beside the card.
    char buf[48];
    snprintf(buf, sizeof(buf), "%d of %d", _pos + 1, _qLen);
    c.text(20, 706, buf, TS_MED, true);
    if (_srs) {
      snprintf(buf, sizeof(buf), "box %d/%d", card.box, fcard::MAX_BOX);
      c.text(160, 706, buf, TS_MED, true);
      tdraw::progressBar(c, 20, 744, 250, 18, masteredPermille());
      snprintf(buf, sizeof(buf), "%d mastered", masteredCount());
      c.text(286, 742, buf, TS_MED, true);
    }
  }

  void renderSessionDone(ToolsCanvas& c) {
    using namespace fcui;
    c.drawRect(CARD_BOX.x, CARD_BOX.y, CARD_BOX.w, CARD_BOX.h, 3, true);
    c.textInBox(CARD_BOX.x, CARD_BOX.y - 40, CARD_BOX.w, CARD_BOX.h, "DECK COMPLETE", TS_HUGE,
                true, true);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d of %d cards mastered", masteredCount(), _cardCount);
    c.textCentered(c.width() / 2, CARD_BOX.y + CARD_BOX.h / 2 + 30, buf, TS_LARGE, true);
    tdraw::progressBar(c, CARD_BOX.x + 100, CARD_BOX.y + CARD_BOX.h / 2 + 70, CARD_BOX.w - 200,
                       26, masteredPermille());
    c.button(RESTART_BTN.x, RESTART_BTN.y, RESTART_BTN.w, RESTART_BTN.h, "AGAIN", true,
             TS_LARGE);
    c.button(DECKS_BTN.x, DECKS_BTN.y, DECKS_BTN.w, DECKS_BTN.h, "DECKS", false, TS_LARGE);
  }

  void tapStudy(int x, int y) {
    using namespace fcui;
    if (_pos >= _qLen) {
      if (RESTART_BTN.hit(x, y)) {
        buildQueue();
        _pos = 0;
        _flipped = false;
        host().beep(1);
        host().refresh(true);
      } else if (DECKS_BTN.hit(x, y)) {
        saveProgress();
        releaseCards();
        _screen = Screen::Decks;
        refreshDeckList();
        host().beep(1);
        host().refresh(true);
      }
      return;
    }

    if (!_flipped) {
      if (CARD_BOX.hit(x, y)) {
        _flipped = true;
        host().beep(0);
        host().refresh(true);
      }
      return;
    }

    fcard::Card& card = _cards[_queue[_pos]];
    if (_srs) {
      if (AGAIN_BTN.hit(x, y)) {
        card.box = 0;
        // Re-queue it a few cards later so it comes back this session.
        if (_qLen < fcard::MAX_CARDS) {
          const uint16_t idx = _queue[_pos];
          const int insertAt = (_pos + 4 < _qLen) ? _pos + 4 : _qLen;
          for (int i = _qLen; i > insertAt; i--) _queue[i] = _queue[i - 1];
          _queue[insertAt] = idx;
          _qLen++;
        }
        advance(2);
        return;
      }
      if (GOOD_BTN.hit(x, y)) {
        if (card.box < fcard::MAX_BOX) card.box++;
        advance(1);
        return;
      }
    } else if (NEXT_BTN.hit(x, y)) {
      advance(0);
      return;
    }
  }

  void advance(uint8_t beepKind) {
    _pos++;
    _flipped = false;
    if (++_graded >= 10) {  // batch flash writes rather than one per card
      saveProgress();
      _graded = 0;
    }
    host().beep(beepKind);
    host().refresh(true);
  }

  int masteredCount() const {
    int n = 0;
    for (int i = 0; i < _cardCount; i++)
      if (_cards[i].box >= fcard::MAX_BOX) n++;
    return n;
  }
  int masteredPermille() const {
    return _cardCount ? (masteredCount() * 1000) / _cardCount : 0;
  }

  void saveProgress() {
    if (_cards && _cardCount > 0 && _deckName[0])
      fcard::saveBoxes(_deckName, _cards, _cardCount);
  }

  void releaseCards() {
    free(_cards);
    _cards = nullptr;
    _cardCount = 0;
  }

  // Word-wrap into the box, stepping the type size down until it fits.
  static void drawWrapped(ToolsCanvas& c, const char* text, const TRect& box) {
    const TSize sizes[3] = {TS_HUGE, TS_LARGE, TS_MED};
    char lines[6][80];
    int count = 0;
    TSize used = TS_MED;
    const int maxW = box.w - 40;
    const int maxLines = 5;

    // The ladder stops at the script's readable floor: a long Thai answer takes
    // more lines at TS_LARGE rather than shrinking into a size it cannot be
    // read at. Latin still gets all three steps.
    const TSize floorSz = scriptFloor(text, TS_MED);
    for (int s = 0; s < 3; s++) {
      if (sizes[s] < floorSz && count > 0) break;
      count = wrap(c, text, maxW, sizes[s], lines, maxLines);
      used = sizes[s];
      if (count > 0 && count <= (s == 0 ? 2 : (s == 1 ? 3 : maxLines))) break;
      if (sizes[s] == floorSz) break;
    }

    const int lh = c.textHeight(used) + 8;
    int y = box.y + (box.h - count * lh) / 2;
    for (int i = 0; i < count; i++) {
      c.textCentered(box.x + box.w / 2, y, lines[i], used, true, true);
      y += lh;
    }
  }

  static int wrap(ToolsCanvas& c, const char* text, int maxW, TSize sz, char lines[][80],
                  int maxLines) {
    // Chinese and Thai cards have no spaces to break on, so the cut falls at
    // the last position uni::breakBefore allowed -- any character boundary for
    // han, cluster boundaries for Thai -- with spaces still preferred when the
    // text has them. The probe walks codepoints, never splitting inside one.
    int n = 0;
    const char* p = text;
    while (*p && n < maxLines) {
      int take = 0, lastCut = -1;
      uint32_t prev = 0;
      char probe[80];
      while (p[take] && take < 76) {
        const char* q = p + take;
        const uint32_t cp = uni::next(q);
        const int cn = (int)(q - (p + take));
        if (p[take] == ' ')
          lastCut = take;
        else if (take > 0 && uni::breakBefore(prev, cp))
          lastCut = take;
        memcpy(probe + take, p + take, cn);
        probe[take + cn] = 0;
        if (c.textWidth(probe, sz, true) > maxW) break;
        take += cn;
        prev = cp;
      }
      if (!p[take]) {  // remainder fits
        memcpy(lines[n], p, take);
        lines[n][take] = 0;
        n++;
        break;
      }
      const int cut = (lastCut > 0) ? lastCut : (take > 0 ? take : 1);
      memcpy(lines[n], p, cut);
      lines[n][cut] = 0;
      n++;
      p += cut;
      while (*p == ' ') p++;
    }
    return n;
  }

  // --- import ------------------------------------------------------------
  void openImport() {
    _screen = Screen::Import;
    _importShown = false;
    _altQr = false;
    _netOk = _net.start();
    host().beep(_netOk ? 1 : 2);
    host().refresh(true);
  }

  void closeImport() {
    _net.stop();
    _screen = Screen::Decks;
    refreshDeckList();
    host().beep(1);
    host().refresh(true);
  }

  void renderImport(ToolsCanvas& c) {
    using namespace fcui;
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start wifi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "BACK", true, TS_LARGE);
      return;
    }
    if (_net.received()) return renderImportDone(c);

    char buf[64];
    if (_altQr) {
      c.textCentered(c.width() / 2, 60, "if it did not open by itself", TS_MED, true);
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, _net.url());
      c.textCentered(c.width() / 2, 390, "Open this in your browser", TS_MED, true, true);
      c.textCentered(c.width() / 2, 428, _net.url(), TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "BACK TO WIFI", false, TS_MED);
    } else {
      c.textCentered(c.width() / 2, 60, "the page opens by itself", TS_MED,
                     true);
      const String wifi = _net.wifiPayload();
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, wifi.c_str());
      c.textCentered(c.width() / 2, 390, "Scan with your phone camera", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   key %s", _net.ssid(), _net.password());
      c.textCentered(c.width() / 2, 428, buf, TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "PAGE DIDN'T OPEN?", false,
               TS_MED);
    }
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", false, TS_LARGE);
  }

  void renderImportDone(ToolsCanvas& c) {
    using namespace fcui;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", _net.count());
    tdraw::seg7Centered(c, c.width() / 2, 150, 120, buf, true);
    c.textCentered(c.width() / 2, 310, "cards added to", TS_LARGE, true, true);
    snprintf(buf, sizeof(buf), "%s", _net.deckName());
    c.textCentered(c.width() / 2, 352, buf, TS_MED, true, true);
    c.textCentered(c.width() / 2, 408, "send more from the same page,", TS_MED, true);
    c.textCentered(c.width() / 2, 434, "or tap DONE", TS_MED, true);
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", true, TS_LARGE);
  }

  void tapImport(int x, int y) {
    if (fcui::DONE_BTN.hit(x, y)) return closeImport();
    if (!_net.received() && _netOk && fcui::ALT_BTN.hit(x, y)) {
      _altQr = !_altQr;
      host().beep(0);
      host().refresh(true);
    }
  }

  // --- state -------------------------------------------------------------
  Screen _screen = Screen::Decks;
  bool _srs = true;

  fcard::DeckInfo _decks[fcard::MAX_DECKS] = {};
  int _deckCount = 0;

  char _deckName[fcard::NAME_LEN + 1] = {};
  // Allocated on entering a deck and released on leaving it, so the ~33 KB it
  // needs is not held while the WiFi stack is up on the import screen.
  fcard::Card* _cards = nullptr;
  int _cardCount = 0;
  uint16_t _queue[fcard::MAX_CARDS] = {};
  int _qLen = 0, _pos = 0, _graded = 0;
  bool _flipped = false;

  fweb::ImportServer _net;
  bool _netOk = false, _importShown = false, _altQr = false;
};
