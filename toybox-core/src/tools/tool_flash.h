// Flashcards with phone import.
//
// Three screens: pick a deck, study it, or import a new one by pairing with a
// phone over a QR-joined access point (see flash_web.h). Studying supports both
// plain flipping and Leitner spaced repetition; box levels survive reboots and
// re-imports.
#pragma once
#include <esp_random.h>

#include "card_text.h"
#include "decor.h"
#include "flash_qr.h"
#include "help.h"
#include "flash_store.h"
#include "flash_web.h"
#include "import_screen.h"
#include "tools_draw.h"

namespace fcui {
// v2 (kit "Flashcards"): decks as rows with a bar, how to study, two ways to
// add a deck; the card with Put back / Flip / Next under it; 24 px hints.
inline constexpr int LIST_X = 20, LIST_Y = 96, LIST_W = 440;
inline constexpr int DEL_W = 120;
inline constexpr int PANEL_X = 20, PANEL_W = 440;
// How to study: two choices, the chosen one outlined thick.
inline constexpr TRect SRS_BTN{PANEL_X, 560, 214, 56};
inline constexpr TRect FLIP_BTN{PANEL_X + 226, 560, 214, 56};
// Adding a deck: from a phone (the wider door), or a file on the card.
inline constexpr TRect IMPORT_BTN{PANEL_X, 664, 260, 64};
inline constexpr TRect CARD_BTN{PANEL_X + 272, 664, 168, 64};

// the card list
inline constexpr int CARD_CAP_Y = 56, CARD_Y = 100, CARD_ROW_H = 56;
inline constexpr TRect CARD_DONE{PANEL_X, 700, PANEL_W, 72};
inline TRect cardRow(int i) { return TRect{LIST_X, CARD_Y + i * CARD_ROW_H, LIST_W, CARD_ROW_H - 6}; }

inline constexpr int LIST_BOTTOM = 520;
inline constexpr int ROW_MAX = 78, ROW_MIN = 40;
inline int rowH(int n) {
  if (n <= 0) return ROW_MAX;
  const int fits = (LIST_BOTTOM - LIST_Y) / n;
  if (fits > ROW_MAX) return ROW_MAX;
  return fits < ROW_MIN ? ROW_MIN : fits;
}

// study
inline constexpr TRect CARD_BOX{24, 112, 424, 456};
inline constexpr int ACT_Y = 600, ACT_H = 72;
inline constexpr TRect AGAIN_BTN{24, ACT_Y, 136, ACT_H};   // Put back
inline constexpr TRect TURN_BTN{172, ACT_Y, 136, ACT_H};   // Flip
inline constexpr TRect GOOD_BTN{320, ACT_Y, 136, ACT_H};   // Next
inline constexpr TRect NEXT_BTN = GOOD_BTN;
// done
inline constexpr TRect DECKS_BTN{24, 700, 196, 72};
inline constexpr TRect RESTART_BTN{232, 700, 224, 72};  // Study more

// import
inline constexpr int QR_X = importui::QR_X, QR_Y = importui::QR_Y, QR_SIZE = importui::QR_SIZE;
inline constexpr TRect ALT_BTN = importui::ALT_BTN;
inline constexpr TRect DONE_BTN = importui::DONE_BTN;

inline TRect rowRect(int i, int n) {
  const int h = rowH(n);
  return TRect{LIST_X, LIST_Y + i * h, LIST_W, h - 6};
}
// The Delete key a held row shows at its right end.
inline TRect delRect(int i, int n) {
  const TRect r = rowRect(i, n);
  return TRect{r.x + r.w - DEL_W, r.y + 4, DEL_W, r.h - 8};
}
inline int listBottom(int n) { return LIST_Y + n * rowH(n); }
}  // namespace fcui

class FlashTool : public ToolApp {
 public:
  int fontSlot() const override { return ToolsHost::FONT_CARDS; }
  const char* title() const override {
    switch (_screen) {
      case Screen::Study: return "FLASHCARDS";
      case Screen::Import: return "ADD A DECK";
      case Screen::Card: return "FROM THE CARD";
      default: return "FLASHCARDS";
    }
  }

  // The access point must not outlive the screen that started it, however
  // the tool is left -- the hub button included.
  ~FlashTool() override {
    _net.stop();
    free(_cardFiles);
  }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    fcard::fsBegin();
    fcard::ensureSampleDeck();
    _srs = prefs().getBool("fc_srs", true);
    // Leaving straight for the hub from the card screen skips closeCard(); the
    // names are of no use next time anyway, so entering starts clean.
    free(_cardFiles);
    _cardFiles = nullptr;
    _fileCount = 0;
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
      gradeGood();
    else
      gradeAgain();
    return true;
  }

  void tick() override {
    if (_screen != Screen::Import) return;
    _net.loop();
    // The moment a phone is on the access point, the wifi code is a picture of
    // a thing already done and the link is the useful one. The notes tool has
    // moved itself on like this since the pairing screen was built; this one
    // was left asking, so the deck import sat on step one with a joined phone
    // in front of it and a button that had to be found.
    if (!_altQr && !_net.received() && _net.hasClient()) {
      _altQr = true;
      host().beep(1);
      host().refresh(true);
      return;
    }
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
      case Screen::Card: renderCard(c); break;
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
      } else if (_screen == Screen::Card) {
        closeCard();
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
      if (t == help::Tap::Toggle) {
        host().beep(0);
        host().refreshUi();
        return;
      }
      if (t == help::Tap::Never) help::suppress(prefs(), "fc");
      _help = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    switch (_screen) {
      case Screen::Decks: tapDecks(x, y); break;
      case Screen::Study: tapStudy(x, y); break;
      case Screen::Card: tapCard(x, y); break;
      default: tapImport(x, y); break;
    }
  }

 public:
  const char* hostDeckName() const { return _screen == Screen::Study ? _deckName : ""; }
  // The last face drawn: did all of it fit, and at what size.
  static bool hostFitAll() { return s_fitAll; }
  static int hostFitSize() { return s_fitSize; }
  int hostCardCount() const { return _cardCount; }
  bool hostHelp() const { return _help; }
  const char* hostFace(bool back) const {
    if (_screen != Screen::Study || !_cards || _qLen == 0) return "";
    const fcard::Card& k = _cards[_queue[_pos < _qLen ? _pos : 0]];
    return back ? k.back : k.front;
  }

 private:
  enum class Screen : uint8_t { Decks, Study, Import, Card };

  // --- deck list ---------------------------------------------------------
  void refreshDeckList() { _deckCount = fcard::listDecks(_decks, fcard::MAX_DECKS); }

  void renderDecks(ToolsCanvas& c) {
    using namespace fcui;
    char buf[48];
    int totalCards = 0;
    for (int i = 0; i < _deckCount; i++) totalCards += _decks[i].cards;
    c.text(LIST_X, 60, "DECKS", TS_MED, true, true);
    snprintf(buf, sizeof(buf), "%d cards", totalCards);
    c.text(LIST_X + LIST_W - c.textWidth(buf, TS_MED), 60, buf, TS_MED, true);
    c.fillRect(LIST_X, 90, LIST_W, 1, true);

    if (_deckCount == 0)
      c.textInBox(LIST_X, LIST_Y, LIST_W, 200, "no decks yet", TS_LARGE, true, true);

    for (int i = 0; i < _deckCount; i++) {
      const TRect r = rowRect(i, _deckCount);
      const int permille = fcard::boxPermille(_decks[i].boxSum, _decks[i].cards);
      const bool armed = i == _delArm;
      if (armed) c.fillRect(r.x, r.y, r.w, r.h, true);
      const TSize nsz = scriptFloor(_decks[i].name, TS_MED);
      c.textClipped(r.x + 8, r.y + 6, r.w - (armed ? DEL_W : 40), _decks[i].name, nsz, !armed, true);
      const int lineY = r.y + 6 + c.textHeight(nsz) + 6;
      if (lineY + 24 <= r.y + r.h) {
        if (!armed) tdraw::progressBar(c, r.x + 8, lineY + 8, 120, 10, permille);
        if (armed) snprintf(buf, sizeof(buf), "%d cards", _decks[i].cards);
        else snprintf(buf, sizeof(buf), "%d cards  -  %d%% known", _decks[i].cards, permille / 10);
        c.text(r.x + (armed ? 8 : 140), lineY, buf, TS_MED, !armed);
      }
      if (armed) {
        const TRect d = delRect(i, _deckCount);
        c.fillRect(d.x, d.y, d.w, d.h, false);
        c.textInBox(d.x, d.y, d.w, d.h, "Delete", TS_MED, true, true);
      } else {
        c.text(r.x + r.w - 20, r.y + (r.h - c.textHeight(TS_MED)) / 2, ">", TS_MED, true, true);
      }
      c.fillRect(r.x, r.y + r.h + 2, r.w, 1, true);
    }

    c.text(PANEL_X, SRS_BTN.y - 32, "HOW TO STUDY", TS_MED, true, true);
    choice(c, SRS_BTN, "Spaced repeat", _srs);
    choice(c, FLIP_BTN, "Just flip", !_srs);
    c.text(PANEL_X, IMPORT_BTN.y - 32, "ADD A DECK", TS_MED, true, true);
    c.button(IMPORT_BTN.x, IMPORT_BTN.y, IMPORT_BTN.w, IMPORT_BTN.h, "From your phone", false, TS_MED);
    c.button(CARD_BTN.x, CARD_BTN.y, CARD_BTN.w, CARD_BTN.h, "From card", false, TS_MED);
    c.textCentered(c.width() / 2, 748, _deckCount ? "hold a deck to delete it" : "add one from your phone",
                   TS_MED, true);
  }

  // A choice of two: the chosen one outlined thick, nothing filled black.
  static void choice(ToolsCanvas& c, const TRect& r, const char* label, bool on) {
    c.drawRect(r.x, r.y, r.w, r.h, on ? 3 : 1, true);
    c.textInBox(r.x, r.y, r.w, r.h, label, TS_MED, true, on);
  }

  void onHold(int x, int y) override {
    if (_screen != Screen::Decks || _help) return;
    for (int i = 0; i < _deckCount; i++)
      if (fcui::rowRect(i, _deckCount).hit(x, y)) {
        _delArm = i;
        host().beep(1);
        host().refresh(false);
        return;
      }
  }

  void tapDecks(int x, int y) {
    using namespace fcui;
    if (_delArm >= 0) {
      // A held row offers Delete; any other tap puts it back.
      const int i = _delArm;
      _delArm = -1;
      if (delRect(i, _deckCount).hit(x, y)) {
        fcard::deleteDeck(_decks[i].name);
        refreshDeckList();
        host().beep(2);
      } else {
        host().beep(0);
      }
      host().refresh(false);
      return;
    }
    for (int i = 0; i < _deckCount; i++)
      if (rowRect(i, _deckCount).hit(x, y)) return startStudy(i);
    if (IMPORT_BTN.hit(x, y)) return openImport();
    if (CARD_BTN.hit(x, y)) return openCard();
    if (SRS_BTN.hit(x, y) || FLIP_BTN.hit(x, y)) {
      const bool want = SRS_BTN.hit(x, y);
      if (want == _srs) return;
      _srs = want;
      prefs().putBool("fc_srs", _srs);
      host().beep(1);
      host().refresh(false);
    }
  }

  // --- from the card -----------------------------------------------------
  // A .tsv, .csv or .txt file in /decks on the card, brought in as a deck. The
  // parser is the one the phone import already uses, so a file exported from
  // Anki, Quizlet or a spreadsheet reads the same either way -- what changes is
  // only that no wifi has to come up to receive it.
  //
  // Re-importing a deck of the same name keeps the Leitner box of every card
  // whose front text is unchanged; that is importDeck's own promise and it
  // holds here too. Editing a deck on a laptop and dropping it back does not
  // cost you your progress.
  void openCard() {
    if (!_cardFiles)
      _cardFiles = (char(*)[cardtext::NAME_LEN])malloc(cardtext::MAX_FILES *
                                                       cardtext::NAME_LEN);
    _fileCount = _cardFiles ? cardtext::list(host(), cardtext::DECKS_DIR, _cardFiles,
                                             cardtext::MAX_FILES)
                            : -1;
    _cardMsg = nullptr;
    _screen = Screen::Card;
    host().beep(_fileCount > 0 ? 1 : 2);
    // Listing borrows the SD bus, which resets the panel.
    host().refresh(true);
  }

  void closeCard() {
    free(_cardFiles);
    _cardFiles = nullptr;
    _fileCount = 0;
    _screen = Screen::Decks;
    refreshDeckList();
    host().beep(1);
    host().refresh(true);
  }

  void renderCard(ToolsCanvas& c) {
    using namespace fcui;
    c.text(LIST_X, CARD_CAP_Y, "ON THE CARD, /decks", TS_MED, true, true);
    c.fillRect(LIST_X, CARD_Y - 10, LIST_W, 1, true);
    if (_fileCount < 0) {
      c.textInBox(LIST_X, CARD_Y, LIST_W, 200, "no card", TS_LARGE, true, true);
      c.textInBox(LIST_X, CARD_Y + 120, LIST_W, 80, "put a FAT32 card in the slot", TS_MED, true);
    } else if (_fileCount == 0) {
      c.textInBox(LIST_X, CARD_Y, LIST_W, 200, "no files here", TS_LARGE, true, true);
      c.textInBox(LIST_X, CARD_Y + 120, LIST_W, 80, "decks are .tsv, .csv or .txt", TS_MED, true);
    }
    for (int i = 0; i < _fileCount; i++) {
      const TRect r = cardRow(i);
      if (r.y + r.h > CARD_DONE.y - 100) break;
      c.fillRect(r.x, r.y + r.h + 2, r.w, 1, true);
      const TSize nsz = scriptFloor(_cardFiles[i], TS_MED);
      c.textClipped(r.x + 8, r.y + (r.h - c.textHeight(nsz)) / 2, r.w - 16, _cardFiles[i], nsz, true);
    }
    if (_cardMsg) c.textInBox(PANEL_X, CARD_DONE.y - 92, fcui::PANEL_W, 40, _cardMsg, TS_MED, true, true);
    c.textCentered(c.width() / 2, CARD_DONE.y - 44, "one card a line: front, then back", TS_MED, true);
    c.button(CARD_DONE.x, CARD_DONE.y, CARD_DONE.w, CARD_DONE.h, "Done", true, TS_LARGE);
  }

  void tapCard(int x, int y) {
    using namespace fcui;
    if (CARD_DONE.hit(x, y)) return closeCard();
    for (int i = 0; i < _fileCount; i++)
      if (cardRow(i).hit(x, y)) return importFromCard(i);
  }

  void importFromCard(int idx) {
    // Sixteen kilobytes takes 200 cards of ordinary length, which is the most
    // a deck may hold anyway. Read on its own rather than into the study
    // buffer: importDeck wants ~35 KB of its own while it runs, and the two
    // are alive at the same moment.
    static constexpr int kMaxText = 16 * 1024;
    char* text = (char*)malloc(kMaxText + 1);
    if (!text) {
      _cardMsg = "not enough memory";
      host().beep(2);
      host().refreshUi();
      return;
    }
    int n = cardtext::read(host(), cardtext::DECKS_DIR, _cardFiles[idx], text, kMaxText);
    if (n <= 0) {
      free(text);
      _cardMsg = "could not read that file";
      host().beep(2);
      host().refresh(true);  // the read borrowed the bus even when it failed
      return;
    }
    if (n == kMaxText) {  // truncated: end on the last whole line, not mid-card
      while (n > 0 && text[n - 1] != '\n') n--;
      text[n] = 0;
    }

    char raw[cardtext::NAME_LEN];
    cardtext::stem(_cardFiles[idx], raw, sizeof(raw));
    char saved[fcard::NAME_LEN + 1] = {};
    const int cards = fcard::importDeck(raw, text, saved);
    free(text);

    if (cards <= 0) {
      _cardMsg = "no cards found in that file";
      host().beep(2);
      host().refresh(true);
      return;
    }
    snprintf(_cardMsgBuf, sizeof(_cardMsgBuf), "%s -- %d card%s", saved, cards,
             cards == 1 ? "" : "s");
    _cardMsg = _cardMsgBuf;
    host().beep(3);
    host().refresh(true);
  }

  // --- study -------------------------------------------------------------
  void startStudy(int deckIdx) {
    strncpy(_deckName, _decks[deckIdx].name, fcard::NAME_LEN);
    _deckName[fcard::NAME_LEN] = 0;
    if (!_cards) _cards = (fcard::Card*)ps_malloc(sizeof(fcard::Card) * fcard::MAX_CARDS);
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
    _known = _putBack = 0;
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
    char buf[48];
    const fcard::Card& card = _cards[_queue[_pos]];

    // The deck, where you are in it, and a bar of the session.
    snprintf(buf, sizeof(buf), "%d / %d", _pos + 1, _qLen);
    const int cw = c.textWidth(buf, TS_MED);
    c.textClipped(24, 52, 432 - cw - 16, _deckName, scriptFloor(_deckName, TS_MED), true, true);
    c.text(456 - cw, 52, buf, TS_MED, true);
    sessionBar(c, 24, 86, 432);

    // The card, with two more behind it.
    const TRect k = CARD_BOX;
    c.drawRect(k.x + 8, k.y + 8, k.w, k.h, 1, true);
    c.drawRect(k.x + 4, k.y + 4, k.w, k.h, 1, true);
    c.fillRect(k.x, k.y, k.w, k.h, false);
    c.drawRect(k.x, k.y, k.w, k.h, 3, true);
    if (!_flipped) {
      ContentFace cf(host());
      drawWrapped(c, card.front, k);
    } else {
      c.textTrackedCentered(k.x + k.w / 2, k.y + 14, "BACK", TS_SMALL, true, true, 3);
      {
        ContentFace cf(host());
        drawWrapped(c, card.back, TRect{k.x, k.y + 40, k.w, k.h - 40 - 60});
      }
      c.fillRect(k.x + 60, k.y + k.h - 58, k.w - 120, 1, true);
      ContentFace cf(host());
      c.textClipped(k.x + 24, k.y + k.h - 46, k.w - 48, card.front, scriptFloor(card.front, TS_MED), true);
    }

    c.button(AGAIN_BTN.x, AGAIN_BTN.y, AGAIN_BTN.w, AGAIN_BTN.h, "Put back", false, TS_MED);
    c.button(TURN_BTN.x, TURN_BTN.y, TURN_BTN.w, TURN_BTN.h, "Flip", false, TS_MED);
    c.button(GOOD_BTN.x, GOOD_BTN.y, GOOD_BTN.w, GOOD_BTN.h, "Next", true, TS_MED);
    c.textCentered(c.width() / 2, 700, "UP put back  -  DOWN flip, then next", TS_MED, true);
    if (_srs) {
      snprintf(buf, sizeof(buf), "box %d of %d  -  %d%% known", card.box, fcard::MAX_BOX,
               progressPermille() / 10);
      c.textCentered(c.width() / 2, 740, buf, TS_MED, true);
    }
  }

  // Segments for a short session, a plain bar for a long one.
  void sessionBar(ToolsCanvas& c, int x, int y, int w) const {
    if (_qLen <= 20) {
      const int gap = 4, seg = (w - (_qLen - 1) * gap) / (_qLen ? _qLen : 1);
      for (int i = 0; i < _qLen; i++) {
        const int sx = x + i * (seg + gap);
        if (i < _pos) c.fillRect(sx, y, seg, 8, true);
        else c.drawRect(sx, y, seg, 8, 1, true);
      }
    } else {
      tdraw::progressBar(c, x, y, w, 8, _qLen ? _pos * 1000 / _qLen : 0);
    }
  }

  void renderSessionDone(ToolsCanvas& c) {
    using namespace fcui;
    char buf[48];
    c.textTrackedCentered(c.width() / 2, 200, _deckName, TS_MED, true, true, 2);
    snprintf(buf, sizeof(buf), "%d / %d", _known, _known + _putBack);
    c.textCentered(c.width() / 2, 248, buf, TS_HUGE, true, true);
    c.textCentered(c.width() / 2, 306, "known this time", TS_MED, true);
    // Three numbers, the first one dark.
    const int bw = 136, bx = 24, by = 370, bh = 100;
    struct S { int n; const char* l; } st[3] = {{_known, "known"}, {_putBack, "put back"},
                                               {progressPermille() / 10, _srs ? "% learned" : "cards"}};
    if (!_srs) st[2].n = _cardCount;
    for (int i = 0; i < 3; i++) {
      const int x = bx + i * (bw + 12);
      if (i == 0) c.fillRect(x, by, bw, bh, true);
      else c.drawRect(x, by, bw, bh, 2, true);
      snprintf(buf, sizeof(buf), "%d", st[i].n);
      c.textCentered(x + bw / 2, by + 14, buf, TS_LARGE, i != 0, true);
      c.textCentered(x + bw / 2, by + 60, st[i].l, TS_MED, i != 0);
    }
    c.button(DECKS_BTN.x, DECKS_BTN.y, DECKS_BTN.w, DECKS_BTN.h, "Decks", false, TS_MED);
    c.button(RESTART_BTN.x, RESTART_BTN.y, RESTART_BTN.w, RESTART_BTN.h, "Study more", true, TS_MED);
  }

  // --- the things the study screen can do --------------------------------
  // Named, because the buttons down the side reach them too, and a hit test is
  // not something a button can call.
  void restart() {
    _known = _putBack = 0;
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

  // Put back: the card comes round again a few later. Spaced repeat also
  // sends it back to the first box; Just flip keeps nothing.
  void gradeAgain() {
    fcard::Card& card = _cards[_queue[_pos]];
    if (_srs) card.box = 0;
    _putBack++;
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
    if (_srs && card.box < fcard::MAX_BOX) card.box++;
    _known++;
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

    if (CARD_BOX.hit(x, y) || TURN_BTN.hit(x, y)) return _flipped ? unreveal() : reveal();
    if (AGAIN_BTN.hit(x, y)) return gradeAgain();
    if (GOOD_BTN.hit(x, y)) return gradeGood();
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
  // Every box of every card against the box they could all be in: it moves
  // after every session, where the mastered count moves once every four.
  int progressPermille() const {
    int sum = 0;
    for (int i = 0; i < _cardCount; i++) sum += _cards[i].box;
    return fcard::boxPermille(sum, _cardCount);
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

  // Word-wrap into the box, stepping the type size down until it fits. A face
  // is never cut (owner, 24 Nov): big type while it is short, then smaller,
  // down to TS_MED; a script whose floor is higher stays at its floor while
  // that still fits. Only a face that fits nowhere else goes to TS_SMALL.
  static constexpr int WRAP_LINES = 16, WRAP_BYTES = 96;
  static inline bool s_fitAll = true;
  static inline int s_fitSize = TS_HUGE;
  static TSize drawWrapped(ToolsCanvas& c, const char* text, const TRect& box) {
    static char lines[WRAP_LINES][WRAP_BYTES];
    const int maxW = box.w - 40;
    const TSize floorSz = scriptFloor(text, TS_MED);
    struct Step {
      TSize sz;
      int prefer;  // most lines this size is worth using for
    };
    const Step steps[] = {{TS_HUGE, 3}, {TS_LARGE, WRAP_LINES}, {TS_MED, WRAP_LINES}, {TS_SMALL, WRAP_LINES}};
    int count = 0;
    TSize used = TS_SMALL;
    bool fitAll = false;
    for (const Step& st : steps) {
      const int lh = c.textHeight(st.sz) + (st.sz >= TS_LARGE ? 8 : 6);
      int fit = (box.h - 16) / lh;
      if (fit > st.prefer && st.sz != floorSz) fit = st.prefer;
      if (fit > WRAP_LINES) fit = WRAP_LINES;
      count = wrap(c, text, maxW, st.sz, lines, fit, &fitAll);
      used = st.sz;
      if (fitAll) break;
    }
    s_fitAll = fitAll;
    s_fitSize = used;
    const int lh = c.textHeight(used) + (used >= TS_LARGE ? 8 : 6);
    int y = box.y + (box.h - count * lh) / 2;
    for (int i = 0; i < count; i++) {
      c.textCentered(box.x + box.w / 2, y, lines[i], used, true, used >= TS_MED);
      y += lh;
    }
    return used;
  }

  static int wrap(ToolsCanvas& c, const char* text, int maxW, TSize sz, char lines[][WRAP_BYTES],
                  int maxLines, bool* all = nullptr) {
    // Chinese and Thai cards have no spaces to break on, so the cut falls at
    // the last position uni::breakBefore allowed -- any character boundary for
    // han, cluster boundaries for Thai -- with spaces still preferred when the
    // text has them. The probe walks codepoints, never splitting inside one.
    int n = 0;
    const char* p = text;
    if (all) *all = false;
    while (*p == ' ') p++;
    while (*p && n < maxLines) {
      int take = 0, lastCut = -1;
      uint32_t prev = 0;
      char probe[WRAP_BYTES];
      while (p[take] && take < WRAP_BYTES - 5) {
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
        p += take;
        break;
      }
      const int cut = (lastCut > 0) ? lastCut : (take > 0 ? take : 1);
      memcpy(lines[n], p, cut);
      lines[n][cut] = 0;
      n++;
      p += cut;
      while (*p == ' ') p++;
    }
    if (all) *all = !*p;
    return n;
  }

  // --- import ------------------------------------------------------------
  void openImport() {
    _screen = Screen::Import;
    _importShown = false;
    _altQr = false;
    // The home Wi-Fi first when one is saved (owner): the phone keeps its own
    // network. The Sticky's own access point is the backup.
    _netNote = nullptr;
    _netOk = false;
    if (homenet::haveWifi(prefs())) {
      homenet::wifiName(prefs(), _joinName, sizeof(_joinName));
      _joining = true;
      host().refresh(false);  // "Joining ..." while the radio looks
      _netOk = _net.startHome(prefs(), "/cards");
      _joining = false;
      if (!_netOk) _netNote = "the home Wi-Fi did not answer";
    }
    if (!_netOk) _netOk = _net.start();
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
    if (_joining) return importui::drawJoining(c, _joinName);
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start Wi-Fi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Back", true, TS_LARGE);
      return;
    }
    if (_net.received()) return renderImportDone(c);
    static const importui::Words W{"Scan to add cards on your phone",
                                   {"Paste two columns: front, back", "One card a line, tab or comma",
                                    "The deck appears here at once"}};
    importui::draw(c, _net, _altQr, W, _netNote);
  }

  void renderImportDone(ToolsCanvas& c) {
    using namespace fcui;
    char buf[64];
    // A confirmation is read once and then dismissed, so it is laid out as one
    // block sitting above its button rather than as a paragraph pinned to the
    // top of an otherwise empty panel.
    snprintf(buf, sizeof(buf), "%d", _net.count());
    tdraw::seg7Centered(c, c.width() / 2, 200, 140, buf, true);
    c.textCentered(c.width() / 2, 372, "cards added to", TS_MED, true);
    snprintf(buf, sizeof(buf), "%s", _net.deckName());
    c.textCentered(c.width() / 2, 414, buf, TS_LARGE, true, true);
    decor::ornament(c, c.width() / 2, 472, 300, true);
    c.textCentered(c.width() / 2, 500, "send more from the same page,", TS_MED, true);
    c.textCentered(c.width() / 2, 528, "or tap DONE", TS_MED, true);
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Done", true, TS_LARGE);
  }

  void tapImport(int x, int y) {
    if (fcui::DONE_BTN.hit(x, y)) return closeImport();
    if (!_net.received() && _netOk && fcui::ALT_BTN.hit(x, y)) {
    if (_net.home()) {
      // Backup: the Sticky's own access point.
      _net.stop();
      _netNote = nullptr;
      _altQr = false;
      _netOk = _net.start();
      host().beep(_netOk ? 1 : 2);
      host().refresh(true);
      return;
    }
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

  // The card's own /decks folder. A kilobyte of names, held only while that
  // screen is open -- the same rule the study buffer and the WiFi stack follow.
  char (*_cardFiles)[cardtext::NAME_LEN] = nullptr;
  int _fileCount = 0;
  const char* _cardMsg = nullptr;
  char _cardMsgBuf[64] = {};

  char _deckName[fcard::NAME_LEN + 1] = {};
  // Allocated on entering a deck and released on leaving it, so the ~33 KB it
  // needs is not held while the WiFi stack is up on the import screen.
  fcard::Card* _cards = nullptr;
  int _cardCount = 0;
  uint16_t _queue[fcard::MAX_CARDS] = {};
  int _qLen = 0, _pos = 0, _graded = 0;
  int _known = 0, _putBack = 0;  // this session
  int _delArm = -1;              // the deck a hold offered to delete
  bool _joining = false;
  const char* _netNote = nullptr;
  char _joinName[34] = {};

  bool _flipped = false;

  fweb::ImportServer _net;
  bool _netOk = false, _importShown = false, _altQr = false;
};
