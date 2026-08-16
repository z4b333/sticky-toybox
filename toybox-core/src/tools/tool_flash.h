// Flashcards with phone import.
//
// Three screens: pick a deck, study it, or import a new one by pairing with a
// phone over a QR-joined access point (see flash_web.h). Studying supports both
// plain flipping and Leitner spaced repetition; box levels survive reboots and
// re-imports.
#pragma once
#include <esp_random.h>

#include "decor.h"
#include "flash_qr.h"
#include "help.h"
#include "flash_store.h"
#include "flash_web.h"
#include "tools_draw.h"

namespace fcui {
// deck list
inline constexpr int LIST_X = 20, LIST_Y = 56, LIST_W = 440;
inline constexpr int DEL_W = 34;
inline constexpr int PANEL_X = 20, PANEL_W = 440;
inline constexpr TRect IMPORT_BTN{PANEL_X, 480, PANEL_W, 68};
// Two buttons for two modes rather than one button that renames itself. The
// timer and the randomiser already offer their modes this way, and a toggle
// whose label is the state you are not in is a puzzle every time.
inline constexpr TRect SRS_BTN{PANEL_X, 584, 216, 54};
inline constexpr TRect FLIP_BTN{PANEL_X + 224, 584, 216, 54};

// The list has to hold every deck the storage layer allows, and twelve rows at
// a comfortable height do not fit above the buttons. Rows give up height as
// they crowd rather than running underneath IMPORT, which is what they used to
// do past the eighth deck.
inline constexpr int LIST_BOTTOM = 468;
// 56, not 46: a 24 px name plus the progress bar under it needs 44, and at 46
// the bar was silently dropped on every row -- the guard below is for hosts
// with a much taller face, not for the device's own default.
inline constexpr int ROW_MAX = 56, ROW_MIN = 34;
inline int rowH(int n) {
  if (n <= 0) return ROW_MAX;
  const int fits = (LIST_BOTTOM - LIST_Y) / n;
  if (fits > ROW_MAX) return ROW_MAX;
  return fits < ROW_MIN ? ROW_MIN : fits;
}

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

inline TRect rowRect(int i, int n) {
  const int h = rowH(n);
  return TRect{LIST_X, LIST_Y + i * h, LIST_W, h - 6};
}
inline TRect delRect(int i, int n) {
  // Capped: on a tall row a full-height delete key is a stripe down the side,
  // and a bigger target for the one tap nobody wants to make by accident.
  const int h = rowH(n);
  const int dh = (h - 12) > 40 ? 40 : (h - 12);
  return TRect{LIST_X + LIST_W - DEL_W - 4, LIST_Y + i * h + 6, DEL_W, dh};
}
inline int listBottom(int n) { return LIST_Y + n * rowH(n); }
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

  // The access point must not outlive the screen that started it, however
  // the tool is left -- the hub button included.
  ~FlashTool() override { _net.stop(); }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    fcard::fsBegin();
    fcard::ensureSampleDeck();
    _srs = prefs().getBool("fc_srs", true);
    _screen = Screen::Decks;
    _help = !help::suppressed(prefs(), "fc");
    refreshDeckList();
  }

  // The import screen must service HTTP while it is open.
  bool wantsTick() const override { return _screen == Screen::Import; }

  // The side buttons, on the study screen only. DOWN always moves forward --
  // reveal the answer, then take the card as known -- and UP is the one that
  // says you did not. Studying is the one thing on this device anybody does for
  // twenty minutes at a stretch, and reaching up to the panel for every card is
  // the part that makes you stop.
  //
  // On the front of a card UP does nothing: there is nothing yet to admit you
  // could not remember. In JUST FLIP mode there is no grading at all, so UP
  // turns the card back over -- the same word at a smaller scale.
  bool onButton(SideBtn b) override {
    if (_screen != Screen::Study) return false;
    if (b == SideBtn::Ok) return false;  // grading is UP and DOWN only
    if (_pos >= _qLen) {
      if (b == SideBtn::Down) restart();
      return b == SideBtn::Down;
    }
    if (!_flipped) {
      if (b == SideBtn::Down) reveal();
      return b == SideBtn::Down;
    }
    if (_srs) {
      if (b == SideBtn::Up)
        gradeAgain();
      else
        gradeGood();
      return true;
    }
    if (b == SideBtn::Down)
      advance(0);
    else
      unreveal();
    return true;
  }

  void tick() override {
    if (_screen != Screen::Import) return;
    _net.loop();
    if (_net.received() && !_importShown) {
      _importShown = true;
      host().beep(3);
      host().refreshUi();
    }
  }

  void render(ToolsCanvas& c) override {
    // The four import steps used to be printed under the buttons on the deck
    // list, permanently, on the screen you see every time you open the app.
    // They belong on the card the rest of the device already keeps its
    // instructions on, behind the "?" that is always in the corner.
    host().topBar(title(), true);
    if (_help) {
      help::render(c, help::FLASHCARDS, "HOW IT WORKS");
      return;
    }
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
        host().beep(1);
        host().refreshUi();
      } else if (_screen == Screen::Import) {
        closeImport();
      } else {
        host().beep(1);
        host().goHub();
      }
      return;
    }
    if (host().isHelpTap(x, y)) {
      _help = !_help;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (_help) {
      const help::Tap t = help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Never) help::suppress(prefs(), "fc");
      _help = false;
      host().beep(1);
      host().refreshUi();
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
    const int capH = c.textHeight(TS_MED);
    char buf[40];

    if (_deckCount == 0) {
      c.drawRect(LIST_X, LIST_Y, LIST_W, LIST_BOTTOM - LIST_Y, 1, true);
      c.textInBox(LIST_X, LIST_Y, LIST_W, LIST_BOTTOM - LIST_Y, "no decks yet", TS_LARGE, true,
                  true);
    }

    for (int i = 0; i < _deckCount; i++) {
      const TRect r = rowRect(i, _deckCount);
      const TRect d = delRect(i, _deckCount);
      const int permille =
          _decks[i].cards ? (_decks[i].mastered * 1000) / _decks[i].cards : 0;

      // A rule between rows rather than a box around each one. Every deck used
      // to be drawn as a button, which put three heavy outlines above IMPORT
      // and made the list compete with the thing you actually press.
      const TSize nsz = scriptFloor(_decks[i].name, TS_MED);
      c.text(r.x + 6, r.y + 4, _decks[i].name, nsz, true);
      snprintf(buf, sizeof(buf), "%d/%d", _decks[i].mastered, _decks[i].cards);
      const int tw = c.textWidth(buf, TS_SMALL);
      // Centred on the delete key beside it rather than on the name above it:
      // the two sit on the same edge of the row and a few pixels of daylight
      // between them reads as a mistake.
      c.text(r.x + r.w - DEL_W - 12 - tw, d.y + (d.h - c.textHeight(TS_SMALL)) / 2, buf,
             TS_SMALL, true);
      // The bar goes under the name, if the name leaves room for it. A host
      // with a taller face (the CrossPoint port's is half again as tall) would
      // otherwise push it through the row's own separator; there the count on
      // the right says the same thing in less space.
      const int barY = r.y + 8 + c.textHeight(nsz) + 4;
      if (barY + 8 <= r.y + r.h)
        tdraw::progressBar(c, r.x + 6, barY, r.w - DEL_W - 20, 8, permille);
      c.textInBox(d.x, d.y, d.w, d.h, "x", TS_MED, true);

      // Only between rows: the last one is closed by the summary rule below,
      // and two lines four pixels apart read as a mistake.
      if (i + 1 < _deckCount)
        c.drawLine(r.x, r.y + r.h + 2, r.x + r.w, r.y + r.h + 2, 1, true);
    }

    // A rule and a count close the list off, so the room left under it reads as
    // space for more decks rather than as a hole in the middle of the screen.
    if (_deckCount > 0) {
      int totalCards = 0;
      for (int i = 0; i < _deckCount; i++) totalCards += _decks[i].cards;
      const int y = listBottom(_deckCount) + 6;
      if (y + capH + 10 < IMPORT_BTN.y) {
        c.drawLine(PANEL_X, y, PANEL_X + IMPORT_BTN.w, y, 1, true);
        snprintf(buf, sizeof(buf), "%d deck%s   %d cards", _deckCount,
                 _deckCount == 1 ? "" : "s", totalCards);
        c.text(PANEL_X, y + 8, buf, TS_MED, true);
      }
    }

    // The caption hangs off the button beneath it rather than the one above:
    // it labels the import flow, and the gap that has to survive is the one
    // that would otherwise be an overlap when a taller face is used.
    c.button(IMPORT_BTN.x, IMPORT_BTN.y, IMPORT_BTN.w, IMPORT_BTN.h, "IMPORT", true, TS_LARGE);
    c.text(PANEL_X, SRS_BTN.y - capH - 6, "scan a QR with your phone", TS_MED, true);

    c.button(SRS_BTN.x, SRS_BTN.y, SRS_BTN.w, SRS_BTN.h, "SPACED REPEAT", _srs, TS_MED);
    c.button(FLIP_BTN.x, FLIP_BTN.y, FLIP_BTN.w, FLIP_BTN.h, "JUST FLIP", !_srs, TS_MED);
    c.text(PANEL_X, SRS_BTN.y + SRS_BTN.h + 8,
           _srs ? "hard cards come back until you know them" : "shuffle and flip, nothing kept",
           TS_MED, true);
  }

  void tapDecks(int x, int y) {
    using namespace fcui;
    for (int i = 0; i < _deckCount; i++) {
      if (delRect(i, _deckCount).hit(x, y)) {
        fcard::deleteDeck(_decks[i].name);
        refreshDeckList();
        host().beep(2);
        host().refreshUi();
        return;
      }
      if (rowRect(i, _deckCount).hit(x, y)) return startStudy(i);
    }
    if (IMPORT_BTN.hit(x, y)) return openImport();
    if (SRS_BTN.hit(x, y) || FLIP_BTN.hit(x, y)) {
      const bool want = SRS_BTN.hit(x, y);
      if (want == _srs) return;
      _srs = want;
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
    host().refreshUi();
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
      c.textInBox(NEXT_BTN.x, ACT_Y, NEXT_BTN.w, ACT_H, "tap the card, or press DOWN", TS_MED,
                  true);
    } else if (_srs) {
      c.button(AGAIN_BTN.x, AGAIN_BTN.y, AGAIN_BTN.w, AGAIN_BTN.h, "AGAIN", false, TS_LARGE);
      c.button(GOOD_BTN.x, GOOD_BTN.y, GOOD_BTN.w, GOOD_BTN.h, "GOT IT", true, TS_LARGE);
      // Which side button does which, over the button it does -- in the gap
      // between the card and the buttons, not under them, where the count and
      // the box number already are. Smallest size on the screen: it is a thing
      // to learn on the first card and never read again.
      c.textInBox(AGAIN_BTN.x, ACT_Y - 30, AGAIN_BTN.w, 26, "side UP", TS_SMALL, true);
      c.textInBox(GOOD_BTN.x, ACT_Y - 30, GOOD_BTN.w, 26, "side DOWN", TS_SMALL, true);
    } else {
      c.button(NEXT_BTN.x, NEXT_BTN.y, NEXT_BTN.w, NEXT_BTN.h, "NEXT", true, TS_LARGE);
      c.textInBox(NEXT_BTN.x, ACT_Y - 30, NEXT_BTN.w, 26, "side DOWN", TS_SMALL, true);
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

  // --- the things the study screen can do --------------------------------
  // Named, because the buttons down the side reach them too, and a hit test is
  // not something a button can call.
  void restart() {
    buildQueue();
    _pos = 0;
    _flipped = false;
    host().beep(1);
    host().refreshUi();
  }

  void backToDecks() {
    saveProgress();
    releaseCards();
    _screen = Screen::Decks;
    refreshDeckList();
    host().beep(1);
    host().refreshUi();
  }

  void reveal() {
    _flipped = true;
    host().beep(0);
    host().refreshUi();
  }

  void unreveal() {
    _flipped = false;
    host().beep(0);
    host().refreshUi();
  }

  void gradeAgain() {
    fcard::Card& card = _cards[_queue[_pos]];
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
  }

  void gradeGood() {
    fcard::Card& card = _cards[_queue[_pos]];
    if (card.box < fcard::MAX_BOX) card.box++;
    advance(1);
  }

  void tapStudy(int x, int y) {
    using namespace fcui;
    if (_pos >= _qLen) {
      if (RESTART_BTN.hit(x, y))
        restart();
      else if (DECKS_BTN.hit(x, y))
        backToDecks();
      return;
    }

    if (!_flipped) {
      if (CARD_BOX.hit(x, y)) reveal();
      return;
    }

    if (_srs) {
      if (AGAIN_BTN.hit(x, y)) return gradeAgain();
      if (GOOD_BTN.hit(x, y)) return gradeGood();
    } else if (NEXT_BTN.hit(x, y)) {
      advance(0);
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
    host().refreshUi();
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
    host().refreshUi();
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
    // A confirmation is read once and then dismissed, so it is laid out as one
    // block sitting above its button rather than as a paragraph pinned to the
    // top of an otherwise empty panel.
    snprintf(buf, sizeof(buf), "%d", _net.count());
    tdraw::seg7Centered(c, c.width() / 2, 200, 140, buf, true);
    c.textCentered(c.width() / 2, 372, "cards added to", TS_LARGE, true, true);
    snprintf(buf, sizeof(buf), "%s", _net.deckName());
    c.textCentered(c.width() / 2, 414, buf, TS_LARGE, true, true);
    decor::ornament(c, c.width() / 2, 472, 300, true);
    c.textCentered(c.width() / 2, 500, "send more from the same page,", TS_MED, true);
    c.textCentered(c.width() / 2, 528, "or tap DONE", TS_MED, true);
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
  bool _help = false;
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
