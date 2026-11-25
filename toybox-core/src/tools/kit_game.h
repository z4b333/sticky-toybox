// The shell every C4 game sits in (Four in a Row, Blackjack, Higher or Lower,
// Codebreaker, Tower of Hanoi, Memory).
//
// The C3 games each carry the same few dozen lines: the rules card on first
// open, More with How to play and a two-tap Clear record, Back that returns
// from More to the game and from the game to the hub. Six more copies of that
// would be six more places for one of them to drift, so the new games share
// it here and only say what is theirs: the board, its taps, and the rows of
// their More page.
//
// A game turned on its side (Hanoi) answers wide() and gets the landscape
// rules card and its More corner measured from the canvas.
#pragma once
#include "game_kit.h"
#include "help.h"

class KitGame : public ToolApp {
 public:
  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _armedClear = false;
    _rulesFromMore = false;
    load();
    _view = help::suppressed(prefs(), helpKey()) ? View::Play : View::Rules;
  }

  void render(ToolsCanvas& c) override {
    if (_view == View::Rules) {
      gk::bar(host(), c, title());
      if (wide())
        help::renderWide(c, rulesText(), "HOW TO PLAY", "More > How to play brings this back");
      else
        gk::rules(c, rulesText());
      return;
    }
    if (_view == View::Menu) {
      gk::Row rows[gk::MENU_MAX];
      const int n = menuRows(rows);
      gk::menu(host(), c, title(), rows, n);
      return;
    }
    gk::bar(host(), c, title(), corner());
    renderPlay(c);
  }

  void onTap(int x, int y) override {
    if (host().isBackTap(x, y)) {
      host().beep(1);
      if (_view == View::Menu || (_view == View::Rules && _rulesFromMore)) {
        _view = View::Play;
        _armedClear = false;
        _rulesFromMore = false;
        backToPlay();
        return;
      }
      host().goHub();
      return;
    }
    if (_view == View::Rules) {
      const help::Tap t = wide() ? help::hitWide(x, y) : help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Never) help::suppress(prefs(), helpKey());
      _view = View::Play;
      _rulesFromMore = false;
      host().beep(1);
      backToPlay();
      return;
    }
    if (_view == View::Menu) {
      tapMenu(x, y);
      return;
    }
    if (isMore(x, y)) {
      _view = View::Menu;
      _armedClear = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    tapPlay(x, y);
  }

#ifdef TOYBOX_HOST
  bool hostInMenu() const { return _view == View::Menu; }
  bool hostInRules() const { return _view == View::Rules; }
#endif

 protected:
  enum class View : uint8_t { Play, Menu, Rules };
  View _view = View::Play;
  bool _armedClear = false;
  bool _rulesFromMore = false;

  // --- what each game says about itself ---------------------------------------
  virtual const help::Text& rulesText() const = 0;
  virtual const char* helpKey() const = 0;  // NVS "h_<key>", also in settings' list
  virtual void load() = 0;                  // the saved game and settings, on every open
  virtual void renderPlay(ToolsCanvas& c) = 0;
  virtual void tapPlay(int x, int y) = 0;
  virtual const char* corner() { return nullptr; }
  virtual bool wide() const { return false; }
  // More's rows after How to play: settings and actions, then Clear record
  // only when there is a record to clear (hasRecord).
  virtual int extraRows(gk::Row* rows) = 0;
  // A tap on one of those rows, by label. Return true to go back to the game
  // (New game does), false to stay on More (a setting just moved on).
  virtual bool pick(const char* label) = 0;
  virtual bool hasRecord() { return false; }
  virtual void clearRecord() {}
  // The repaint on the way back to the game from More or the card. Full when
  // a game had to change the panel's rotation on the way.
  virtual void backToPlay() { host().refreshUi(); }

  bool isMore(int x, int y) {
    if (!wide()) return gk::isMore(host(), x, y);
    return x >= canvas().width() - HELP_W && y < BAR_TOUCH_H;
  }

  int menuRows(gk::Row* rows) {
    int n = 0;
    rows[n++] = {"How to play", nullptr, false};
    n += extraRows(rows + n);
    if (hasRecord() && n < gk::MENU_MAX) rows[n++] = {"Clear record", nullptr, _armedClear};
    return n;
  }

  void tapMenu(int x, int y) {
    gk::Row rows[gk::MENU_MAX];
    const int n = menuRows(rows);
    const int i = gk::menuHit(x, y, n, canvas().width());
    const bool wasArmed = _armedClear;
    _armedClear = false;
    if (i < 0) {
      if (wasArmed) host().refresh(false);
      return;
    }
    const char* what = rows[i].label;
    if (!strcmp(what, "How to play")) {
      _view = View::Rules;
      _rulesFromMore = true;
    } else if (!strcmp(what, "Clear record")) {
      if (!wasArmed) {
        _armedClear = true;
        host().beep(2);
        host().refresh(false);
        return;
      }
      clearRecord();
      _view = View::Play;
      host().beep(1);
      backToPlay();
      return;
    } else if (pick(what)) {
      _view = View::Play;
      host().beep(1);
      backToPlay();
      return;
    }
    host().beep(1);
    host().refreshUi();
  }

  // NVS counters, the way every game keeps its record.
  int geti(const char* k, int d = 0) { return prefs().getInt(k, d); }
  void bump(const char* k, int by = 1) { prefs().putInt(k, prefs().getInt(k, 0) + by); }
  static void key(char* out, size_t n, const char* pre, int i) { snprintf(out, n, "%s%d", pre, i); }
};
