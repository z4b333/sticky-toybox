// Ships (Battleship), against the Sticky or across two of them.
//
// v2 (C3, kit "Ships", "Ships, place the fleet", "Ships, game over"): a 10x10
// board edge to edge, five ships placed by hand (or shuffled), three levels
// for the Sticky, and a hit that fires again -- a setting, on by default.
//
// Firing is aim-then-confirm: the first tap on a square aims (an outlined
// square), a second tap on the same square fires. At 44 px a cell is under a
// fingertip, and on a panel that cannot show a hover state the target has to
// be visible before it is spent. UP / DOWN move the aim and OK fires.
#pragma once
#include <esp_random.h>

#include "decor.h"
#include "game_kit.h"
#include "help.h"
#include "record.h"
#include "sea_net.h"
#include "sea_rules.h"
#include "tools_draw.h"

namespace seaui {
// menu (kit "Ships"): a picture, then the ways to play as rows.
inline constexpr TRect PICTURE{16, 54, 448, 186};
inline constexpr TRect SOLO_ROW{0, 254, 480, 76};
inline constexpr TRect DUEL_ROW{16, 336, 448, 76};

// pairing (unchanged in function)
inline constexpr TRect HOST_BTN{40, 150, 400, 84};
inline constexpr TRect BROWSE_BTN{40, 254, 400, 84};
inline constexpr TRect CANCEL_BTN{40, 690, 400, 60};
inline constexpr int FOUND_Y = 200, FOUND_H = 76, FOUND_GAP = 12;

// placing the fleet
inline constexpr int SET_CELL = 42, SET_X = 30, SET_Y = 54;  // 420 square
inline constexpr int CHIP_Y = SET_Y + 10 * SET_CELL + 16;    // 490
inline constexpr int CHIP_H = 44;
inline constexpr TRect TURN_BTN{20, CHIP_Y + CHIP_H + 18, 140, 64};
inline constexpr TRect SHUFFLE_BTN{170, CHIP_Y + CHIP_H + 18, 140, 64};
inline constexpr TRect PLACE_BTN{320, CHIP_Y + CHIP_H + 18, 140, 64};
inline constexpr int SET_MSG_Y = TURN_BTN.y + TURN_BTN.h + 20;

// play
inline constexpr int ENEMY_CELL = 44, ENEMY_X = 20, ENEMY_Y = 92;  // 440 square
inline constexpr int OWN_CELL = 18, OWN_X = 20, OWN_Y = ENEMY_Y + 10 * ENEMY_CELL + 14;
inline constexpr int OWN_TEXT_X = OWN_X + 10 * OWN_CELL + 24;

inline TRect foundRect(int i) {
  return TRect{40, FOUND_Y + i * (FOUND_H + FOUND_GAP), 400, FOUND_H};
}
inline TRect enemyCell(int k) {
  return TRect{ENEMY_X + sea::xOf(k) * ENEMY_CELL, ENEMY_Y + sea::yOf(k) * ENEMY_CELL, ENEMY_CELL,
               ENEMY_CELL};
}
inline TRect setCell(int k) {
  return TRect{SET_X + sea::xOf(k) * SET_CELL, SET_Y + sea::yOf(k) * SET_CELL, SET_CELL, SET_CELL};
}
}  // namespace seaui

class SeaTool : public ToolApp {
 public:
  const char* title() const override { return "SHIPS"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _view = help::suppressed(prefs(), "sea") ? View::Game : View::Rules;
    _rulesFromMore = false;
    _armedClear = false;
    _flashCell = -1;
    _flashUntil = 0;
    memset(_lastTheirs, 0, sizeof(_lastTheirs));
    _screen = Screen::Menu;
    _duelMode = false;
    _sel = -1;
    _msg[0] = 0;
  }

  // The linked game needs the clock to poll the radio; solo play is entirely
  // tap-driven apart from the shell blast, which has to take itself down again.
  bool wantsTick() const override {
    return _flashUntil != 0 || (_duelMode && _screen != Screen::Menu);
  }

  void tick() override {
    // One extra frame is what an explosion costs on this panel. Worth it for a
    // shell landing; not worth it for anything that happens every tap.
    if (_flashUntil && (int32_t)(millis() - _flashUntil) >= 0) {
      _flashUntil = 0;
      _flashCell = -1;
      if (_view == View::Game) host().refresh(false);
      return;
    }
    if (!_duelMode) return;
    const seanet::Phase before = _duel.phase();
    const bool wasMyTurn = _duel.myTurn();
    const int seenBefore = shotCount(_duel.theirs()) + shotCount(_duel.mine());
    const int mineBefore = shotCount(_duel.mine());
    _duel.poll(millis());
    const int foundNow = _duel.foundCount();

    // Repaint only when something a player would notice has changed: e-ink
    // cannot afford a frame per poll.
    bool dirty = _duel.phase() != before || _duel.myTurn() != wasMyTurn ||
                 shotCount(_duel.theirs()) + shotCount(_duel.mine()) != seenBefore ||
                 (_screen == Screen::Pair && foundNow != _foundShown);
    _foundShown = foundNow;
    if (!dirty) return;

    // Count our shots as their answers come back, and say what happened.
    for (int k = 0; k < sea::CELLS; k++) {
      const uint8_t now = _duel.theirs().shot[k];
      if (now != sea::UNSHOT && _lastTheirs[k] == sea::UNSHOT) {
        _shots++;
        if (now == sea::HIT) {
          _hits++;
          blast(k);
          snprintf(_msg, sizeof(_msg), "%s", _duel.myTurn() ? "Hit! Fire again" : "Hit");
        } else {
          snprintf(_msg, sizeof(_msg), "Miss");
        }
      }
      _lastTheirs[k] = now;
    }
    if (shotCount(_duel.mine()) != mineBefore) {
      const sea::Shot& in = _duel.lastIncoming();
      snprintf(_msg, sizeof(_msg), "%s", in.sunk >= 0 ? "They sank one" : in.hit ? "They hit you" : "They missed");
    }
    syncDuelScreen();
    if (_view == View::Game) host().refresh(_duel.phase() == seanet::Phase::Over);
  }

  void render(ToolsCanvas& c) override {
    if (_view == View::Rules) {
      gk::bar(host(), c, title());
      gk::rules(c, help::SHIPS);
      return;
    }
    if (_view == View::Menu) {
      gk::Row rows[gk::MENU_MAX];
      gk::menu(host(), c, title(), rows, menuRows(rows));
      return;
    }
    switch (_screen) {
      case Screen::Menu: renderMenu(c); break;
      case Screen::Pair: gk::bar(host(), c, title()); renderPair(c); break;
      case Screen::Setup: renderSetup(c); break;
      case Screen::Play: renderPlay(c); break;
      default: renderOver(c); break;
    }
  }

  void onTap(int x, int y) override {
    if (host().isBackTap(x, y)) {
      host().beep(1);
      if (_view == View::Menu || (_view == View::Rules && _rulesFromMore)) {
        _view = View::Game;
        _armedClear = false;
        host().refreshUi();
        return;
      }
      if (_duelMode) _duel.end();
      host().goHub();
      return;
    }
    if (_view == View::Rules) {
      const help::Tap t = help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Toggle) {
        host().beep(0);
        host().refreshUi();
        return;
      }
      if (t == help::Tap::Never) help::suppress(prefs(), "sea");
      _view = View::Game;
      _rulesFromMore = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (_view == View::Menu) return tapMenuRows(x, y);
    if (gk::isMore(host(), x, y)) {
      _view = View::Menu;
      _armedClear = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    switch (_screen) {
      case Screen::Menu: tapMenu(x, y); break;
      case Screen::Pair: tapPair(x, y); break;
      case Screen::Setup: tapSetup(x, y); break;
      case Screen::Play: tapPlay(x, y); break;
      default: tapOver(x, y); break;
    }
  }

  // Side buttons in play: UP / DOWN walk the aim across the squares not yet
  // fired at (upper = next), OK fires.
  bool onButton(SideBtn b) override {
    if (_view != View::Game || _screen != Screen::Play || !canFire()) return false;
    if (b == SideBtn::Ok) {
      if (_sel < 0) return true;
      fireAt(_sel);
      return true;
    }
    const int step = b == SideBtn::Up ? 1 : -1;
    int k = _sel < 0 ? (step > 0 ? -1 : sea::CELLS) : _sel;
    for (int n = 0; n < sea::CELLS; n++) {
      k += step;
      if (k < 0) k = sea::CELLS - 1;
      if (k >= sea::CELLS) k = 0;
      if (enemyBoard().shot[k] == sea::UNSHOT) break;
    }
    _sel = k;
    host().beep(0);
    host().refresh(false);
    return true;
  }

 private:
  enum class View : uint8_t { Game, Menu, Rules };
  enum class Screen : uint8_t { Menu, Pair, Setup, Play, Over };

  // --- settings ------------------------------------------------------------
  int levelPref() { return constrain(prefs().getInt("bs_lvl", sea::NORMAL), 0, sea::LEVELS - 1); }
  bool fireAgainPref() { return prefs().getBool("bs_again", true); }

  int menuRows(gk::Row* rows) {
    int n = 0;
    rows[n++] = {"How to play", nullptr, false};
    rows[n++] = {"Sticky plays", sea::levelName(levelPref()), false};
    rows[n++] = {"A hit fires again", fireAgainPref() ? "On" : "Off", false};
    if (prefs().getInt("bs_w", 0) + prefs().getInt("bs_l", 0) > 0)
      rows[n++] = {"Clear record", nullptr, _armedClear};
    return n;
  }

  void tapMenuRows(int x, int y) {
    gk::Row rows[gk::MENU_MAX];
    const int n = menuRows(rows);
    const int i = gk::menuHit(x, y, n);
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
    } else if (!strcmp(what, "Sticky plays")) {
      // Takes effect from the next game: a level that changed under a game in
      // play would be a different opponent halfway through.
      prefs().putInt("bs_lvl", (levelPref() + 1) % sea::LEVELS);
    } else if (!strcmp(what, "A hit fires again")) {
      prefs().putBool("bs_again", !fireAgainPref());
    } else if (!strcmp(what, "Clear record")) {
      if (!wasArmed) {
        _armedClear = true;
        host().beep(2);
        host().refresh(false);
        return;
      }
      record::clear(prefs(), record::SHIPS);
      _view = View::Game;
    }
    host().beep(1);
    host().refreshUi();
  }

  // --- boards ------------------------------------------------------------
  // One pair of accessors so every screen draws the same way in both modes.
  const sea::Board& myBoard() const { return _duelMode ? _duel.mine() : _mine; }
  sea::Board& myBoardMutable() { return _duelMode ? _duel.mineMutable() : _mine; }
  const sea::Board& enemyBoard() const { return _duelMode ? _duel.theirs() : _enemy; }
  int myAfloat() const { return _duelMode ? _duel.myAfloat() : sea::afloat(_mine); }
  int enemyAfloat() const { return _duelMode ? _duel.theirAfloat() : sea::afloat(_enemy); }
  bool canFire() const { return _duelMode ? _duel.myTurn() : _myTurnSolo; }
  bool againRule() const { return _duelMode ? _duel.fireAgain() : _againSolo; }

  static int shotCount(const sea::Board& b) {
    int n = 0;
    for (int c = 0; c < sea::CELLS; c++)
      if (b.shot[c] != sea::UNSHOT) n++;
    return n;
  }
  static int hitsTaken(const sea::Board& b) {
    int n = 0;
    for (int c = 0; c < sea::CELLS; c++)
      if (b.shot[c] == sea::HIT) n++;
    return n;
  }

  // --- menu --------------------------------------------------------------
  void drawPicture(ToolsCanvas& c) {
    const TRect& p = seaui::PICTURE;
    c.drawRect(p.x, p.y, p.w, p.h, 2, true);
    // Sea: ruled lines, a boat riding them (kit).
    for (int y = p.y + 10; y < p.y + p.h - 4; y += 10) c.fillRect(p.x + 4, y, p.w - 8, 1, true);
    const int cx = p.x + p.w / 2, wl = p.y + 108;
    c.fillRect(cx - 86, wl - 34, 172, 46, false);
    c.fillRect(cx - 30, wl - 70, 60, 40, false);
    c.drawRect(cx - 30, wl - 70, 60, 40, 3, true);
    c.fillRect(cx - 84, wl - 34, 168, 3, true);
    c.fillRect(cx - 84, wl - 34, 3, 26, true);
    c.fillRect(cx + 81, wl - 34, 3, 26, true);
    for (int i = 0; i < 26; i++) {
      // the hull's rounded foot
      const int inset = i * i / 18;
      c.fillRect(cx - 84 + inset, wl - 8 + i, 3, 1, true);
      c.fillRect(cx + 81 - inset, wl - 8 + i, 3, 1, true);
    }
    c.fillRect(cx - 84 + 37, wl + 17, 168 - 74, 3, true);
  }

  void renderMenu(ToolsCanvas& c) {
    using namespace seaui;
    gk::bar(host(), c, title());
    drawPicture(c);
    // The first way to play is THE action: a black row (kit).
    c.fillRect(SOLO_ROW.x, SOLO_ROW.y, SOLO_ROW.w, SOLO_ROW.h, true);
    c.text(16, SOLO_ROW.y + 10, "Against the Sticky", TS_LARGE, false);
    char buf[48];
    snprintf(buf, sizeof(buf), "it shoots back - %s", sea::levelName(levelPref()));
    c.text(16, SOLO_ROW.y + 46, buf, TS_SMALL, false);
    c.text(SOLO_ROW.w - 36, SOLO_ROW.y + 20, ">", TS_LARGE, false, true);
    c.text(DUEL_ROW.x, DUEL_ROW.y + 10, "Two Stickies", TS_LARGE, true);
    c.text(DUEL_ROW.x, DUEL_ROW.y + 46, "near each other, over Wi-Fi", TS_SMALL, true);
    c.text(DUEL_ROW.x + DUEL_ROW.w - 20, DUEL_ROW.y + 20, ">", TS_LARGE, true, true);
    c.fillRect(DUEL_ROW.x, DUEL_ROW.y + DUEL_ROW.h, DUEL_ROW.w, 1, true);
    if (_msg[0]) c.textCentered(c.width() / 2, 460, _msg, TS_MED, true, true);
    snprintf(buf, sizeof(buf), "wins %d - losses %d - in a row %d", prefs().getInt("bs_w", 0),
             prefs().getInt("bs_l", 0), prefs().getInt("bs_strk", 0));
    gk::foot(c, buf);
  }

  void tapMenu(int x, int y) {
    using namespace seaui;
    if (SOLO_ROW.hit(x, y)) {
      _duelMode = false;
      _msg[0] = 0;
      _againSolo = fireAgainPref();
      sea::clear(_enemy);
      sea::placeRandom(_enemy, [] { return esp_random(); });
      _gunner.reset(levelPref());
      startPlacing();
      _screen = Screen::Setup;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (DUEL_ROW.hit(x, y)) {
      _duelMode = true;
      _foundShown = -1;
      if (!_duel.begin()) {
        snprintf(_msg, sizeof(_msg), "could not start the radio");
        _duelMode = false;
        host().beep(2);
        host().refresh(false);
        return;
      }
      _msg[0] = 0;
      _screen = Screen::Pair;
      host().beep(1);
      host().refreshUi();
    }
  }

  // --- pairing -----------------------------------------------------------
  void renderPair(ToolsCanvas& c) {
    using namespace seaui;
    using seanet::Phase;
    const Phase p = _duel.phase();
    char buf[48];

    if (p == Phase::Hosting) {
      // The code is the whole content of this screen and someone is reading it
      // out across a room, so it gets the size that implies. Everything else
      // here is a caption to it.
      c.textCentered(c.width() / 2, 76, "YOUR GAME CODE", TS_MED, true);
      snprintf(buf, sizeof(buf), "%04u", _duel.myCode());
      tdraw::seg7Centered(c, c.width() / 2, 120, 160, buf, true);
      c.textCentered(c.width() / 2, 340, "Tell the other player", TS_MED, true);
      c.textCentered(c.width() / 2, 368, "this number, then have", TS_MED, true);
      c.textCentered(c.width() / 2, 396, "them tap JOIN A GAME", TS_MED, true);
      decor::ornament(c, c.width() / 2, 452, 300, true);
      c.textCentered(c.width() / 2, 476, "waiting for a player...", TS_MED, true, true);
      c.textCentered(c.width() / 2, 516, "keep both Stickies in the same room", TS_SMALL, true);
    } else if (p == Phase::Browsing || p == Phase::Joining) {
      c.textCentered(c.width() / 2, 76, "GAMES NEARBY", TS_MED, true);
      const int n = _duel.foundCount();
      if (n == 0) {
        c.textCentered(c.width() / 2, 250, "looking...", TS_LARGE, true, true);
        c.textCentered(c.width() / 2, 302, "the other Sticky must be", TS_MED, true);
        c.textCentered(c.width() / 2, 330, "on HOST A GAME", TS_MED, true);
      }
      for (int i = 0; i < n; i++) {
        const TRect r = foundRect(i);
        snprintf(buf, sizeof(buf), "%04u", _duel.found(i).code);
        c.button(r.x, r.y, r.w, r.h, buf, false, TS_HUGE);
      }
      if (p == Phase::Joining)
        c.textCentered(c.width() / 2, 620, "joining...", TS_MED, true, true);
    } else {
      c.textCentered(c.width() / 2, 76, "Two Stickies, one game", TS_MED, true);
      c.button(HOST_BTN.x, HOST_BTN.y, HOST_BTN.w, HOST_BTN.h, "HOST A GAME", true, TS_LARGE);
      c.textCentered(c.width() / 2, HOST_BTN.y + HOST_BTN.h + 10,
                     "shows a code for the other player", TS_SMALL, true);
      c.button(BROWSE_BTN.x, BROWSE_BTN.y, BROWSE_BTN.w, BROWSE_BTN.h, "JOIN A GAME", false,
               TS_LARGE);
      c.textCentered(c.width() / 2, BROWSE_BTN.y + BROWSE_BTN.h + 10,
                     "lists the codes it can hear", TS_SMALL, true);
      c.textCentered(c.width() / 2, 452, "Both Stickies stay on their", TS_MED, true);
      c.textCentered(c.width() / 2, 480, "own screen - neither can", TS_MED, true);
      c.textCentered(c.width() / 2, 508, "see the other's fleet.", TS_MED, true);
    }
    c.button(CANCEL_BTN.x, CANCEL_BTN.y, CANCEL_BTN.w, CANCEL_BTN.h, "BACK", false, TS_MED);
  }

  void tapPair(int x, int y) {
    using namespace seaui;
    using seanet::Phase;
    if (CANCEL_BTN.hit(x, y)) {
      _duel.end();
      _duelMode = false;
      _screen = Screen::Menu;
      host().beep(1);
      host().refreshUi();
      return;
    }
    const Phase p = _duel.phase();
    if (p == Phase::Idle) {
      if (HOST_BTN.hit(x, y)) {
        _duel.setFireAgain(fireAgainPref());
        _duel.startHosting(millis());
        host().beep(1);
        host().refreshUi();
      } else if (BROWSE_BTN.hit(x, y)) {
        _duel.setFireAgain(fireAgainPref());
        _duel.startBrowsing();
        _foundShown = 0;
        host().beep(1);
        host().refreshUi();
      }
      return;
    }
    if (p == Phase::Browsing) {
      for (int i = 0; i < _duel.foundCount(); i++) {
        if (!foundRect(i).hit(x, y)) continue;
        _duel.joinFound(i, millis());
        host().beep(1);
        host().refreshUi();
        return;
      }
    }
  }

  // A linked game drives the screen from the protocol's phase, so both devices
  // always agree about what is on show.
  void syncDuelScreen() {
    using seanet::Phase;
    switch (_duel.phase()) {
      case Phase::Setup:
        if (_screen != Screen::Setup) {
          startPlacing();
          _screen = Screen::Setup;
        }
        break;
      case Phase::Play:
        _screen = Screen::Play;
        break;
      case Phase::Over:
        if (_screen != Screen::Over) {
          recordResult(_duel.won());
          _screen = Screen::Over;
        }
        break;
      case Phase::Lost:
        _screen = Screen::Over;
        break;
      default:
        _screen = Screen::Pair;
        break;
    }
  }

  // --- placing the fleet ----------------------------------------------------
  // Kit "Ships, place the fleet": tap a square and the ship in hand is drawn
  // there striped; Turn swings it; Place puts it down and hands you the next.
  // The chips under the board say which are down (struck) and which is in
  // hand (black); tapping a struck chip picks that ship up again. Shuffle
  // throws the whole fleet down at random, the old way.
  void startPlacing() {
    sea::Board& b = myBoardMutable();
    memset(b.ship, 0, sizeof(b.ship));
    memset(b.shot, 0, sizeof(b.shot));
    _hand = 0;
    _horiz = true;
    _aim = -1;
    _sel = -1;
    _shots = _hits = 0;
    _msg[0] = 0;
    memset(_lastTheirs, 0, sizeof(_lastTheirs));
  }

  void nextInHand() {
    const sea::Board& b = myBoard();
    _hand = -1;
    for (int s = 0; s < sea::SHIPS; s++)
      if (!sea::placed(b, s)) {
        _hand = s;
        break;
      }
  }

  // Where the ship in hand would lie with its first square at _aim, pulled
  // back inside the board if it ran off the edge.
  bool handAt(int& x, int& y) const {
    if (_aim < 0 || _hand < 0) return false;
    const int len = sea::LEN[_hand];
    x = sea::xOf(_aim);
    y = sea::yOf(_aim);
    if (_horiz && x + len > sea::N) x = sea::N - len;
    if (!_horiz && y + len > sea::N) y = sea::N - len;
    return true;
  }

  void renderSetup(ToolsCanvas& c) {
    using namespace seaui;
    const sea::Board& b = myBoard();
    char corner[8];
    int down = 0;
    for (int s = 0; s < sea::SHIPS; s++) down += sea::placed(b, s);
    snprintf(corner, sizeof(corner), "%d/%d", down, sea::SHIPS);
    gk::bar(host(), c, "PLACE SHIPS", corner);

    // The grid, the ships already down solid, the one in hand striped.
    for (int k = 0; k < sea::CELLS; k++) {
      const TRect r = setCell(k);
      if (b.ship[k]) c.fillRect(r.x + 1, r.y + 1, r.w - 1, r.h - 1, true);
    }
    int hx, hy;
    bool fits = false;
    if (handAt(hx, hy)) {
      const int len = sea::LEN[_hand];
      fits = sea::canPlace(b, hx, hy, len, _horiz);
      for (int i = 0; i < len; i++) {
        const int k = sea::cellAt(_horiz ? hx + i : hx, _horiz ? hy : hy + i);
        const TRect r = setCell(k);
        gk::stripes(c, r.x + 1, r.y + 1, r.w - 1, r.h - 1);
      }
    }
    for (int i = 0; i <= sea::N; i++) {
      c.fillRect(SET_X + i * SET_CELL, SET_Y, 1, sea::N * SET_CELL + 1, true);
      c.fillRect(SET_X, SET_Y + i * SET_CELL, sea::N * SET_CELL + 1, 1, true);
    }
    c.drawRect(SET_X - 2, SET_Y - 2, sea::N * SET_CELL + 5, sea::N * SET_CELL + 5, 2, true);

    // The chips: struck when down, black when in hand.
    int x = 20;
    for (int s = 0; s < sea::SHIPS; s++) {
      char lbl[16];
      snprintf(lbl, sizeof(lbl), "%s %d", sea::shipName(s), sea::LEN[s]);
      const int w = c.textWidth(lbl, TS_SMALL) + 16;
      _chipX[s] = (int16_t)x;
      _chipW[s] = (int16_t)w;
      if (s == _hand) {
        c.fillRect(x, CHIP_Y, w, CHIP_H, true);
        c.textInBox(x, CHIP_Y, w, CHIP_H, lbl, TS_SMALL, false, true);
      } else {
        c.drawRect(x, CHIP_Y, w, CHIP_H, 2, true);
        c.textInBox(x, CHIP_Y, w, CHIP_H, lbl, TS_SMALL, true);
        if (sea::placed(b, s)) c.fillRect(x + 6, CHIP_Y + CHIP_H / 2, w - 12, 2, true);
      }
      x += w + 6;
    }

    const bool waiting = _duelMode && _duel.waitingForPeerReady();
    const bool all = sea::fleetPlaced(b);
    c.button(TURN_BTN.x, TURN_BTN.y, TURN_BTN.w, TURN_BTN.h, "Turn", false, TS_LARGE);
    c.button(SHUFFLE_BTN.x, SHUFFLE_BTN.y, SHUFFLE_BTN.w, SHUFFLE_BTN.h, "Shuffle", false, TS_LARGE);
    if (waiting)
      c.button(PLACE_BTN.x, PLACE_BTN.y, PLACE_BTN.w, PLACE_BTN.h, "Waiting", false, TS_LARGE);
    else
      c.button(PLACE_BTN.x, PLACE_BTN.y, PLACE_BTN.w, PLACE_BTN.h, all ? "Start" : "Place",
               all || (fits && _aim >= 0), TS_LARGE);

    if (waiting) {
      c.textCentered(c.width() / 2, SET_MSG_Y, "waiting for the other player", TS_MED, true, true);
      if (_duel.peerRestarted())
        c.textCentered(c.width() / 2, SET_MSG_Y + 30, "they want another game", TS_MED, true);
    } else if (_msg[0]) {
      c.textCentered(c.width() / 2, SET_MSG_Y, _msg, TS_MED, true, true);
    } else if (_aim >= 0 && !fits) {
      c.textCentered(c.width() / 2, SET_MSG_Y, "it overlaps a ship there", TS_MED, true, true);
    } else if (all) {
      c.textCentered(c.width() / 2, SET_MSG_Y, "all five are down - Start", TS_MED, true);
    }
    gk::foot(c, all ? "tap a ship's chip to move it" : "tap a square - striped = where it goes");
  }

  void tapSetup(int x, int y) {
    using namespace seaui;
    if (_duelMode && _duel.waitingForPeerReady()) return;
    sea::Board& b = myBoardMutable();
    _msg[0] = 0;
    if (x >= SET_X && x < SET_X + sea::N * SET_CELL && y >= SET_Y && y < SET_Y + sea::N * SET_CELL) {
      if (_hand < 0) {  // nothing in hand: a tap on a ship picks it up
        const int k = sea::cellAt((x - SET_X) / SET_CELL, (y - SET_Y) / SET_CELL);
        if (!b.ship[k]) return;
        _hand = b.ship[k] - 1;
        sea::lift(b, _hand);
        _aim = k;
      } else {
        _aim = sea::cellAt((x - SET_X) / SET_CELL, (y - SET_Y) / SET_CELL);
      }
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (y >= CHIP_Y && y < CHIP_Y + CHIP_H) {
      for (int s = 0; s < sea::SHIPS; s++) {
        if (x < _chipX[s] || x >= _chipX[s] + _chipW[s]) continue;
        if (sea::placed(b, s)) sea::lift(b, s);  // pick it up again
        _hand = s;
        host().beep(0);
        host().refresh(false);
        return;
      }
      return;
    }
    if (TURN_BTN.hit(x, y)) {
      _horiz = !_horiz;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (SHUFFLE_BTN.hit(x, y)) {
      if (_duelMode) {
        _duel.placeFleet(esp_random());
      } else {
        memset(b.ship, 0, sizeof(b.ship));
        sea::placeRandom(b, [] { return esp_random(); });
      }
      _hand = -1;
      _aim = -1;
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (!PLACE_BTN.hit(x, y)) return;
    if (sea::fleetPlaced(b)) {
      host().beep(1);
      if (_duelMode) {
        _duel.setReady(millis());
        syncDuelScreen();
      } else {
        _screen = Screen::Play;
        _myTurnSolo = true;
      }
      _sel = -1;
      host().refreshUi();
      return;
    }
    int hx, hy;
    if (!handAt(hx, hy) || !sea::canPlace(b, hx, hy, sea::LEN[_hand], _horiz)) {
      snprintf(_msg, sizeof(_msg), _aim < 0 ? "tap a square first" : "it overlaps a ship there");
      host().beep(2);
      host().refresh(false);
      return;
    }
    sea::put(b, hx, hy, sea::LEN[_hand], _horiz, _hand);
    _aim = -1;
    nextInHand();
    host().beep(1);
    host().refresh(false);
  }

  // --- play --------------------------------------------------------------
  void renderPlay(ToolsCanvas& c) {
    using namespace seaui;
    char buf[48];
    gk::bar(host(), c, title(), _duelMode ? "vs Sticky" : sea::levelName(_gunner.level));
    const bool fire = canFire();
    const char* head = _msg[0] ? _msg : fire ? "Your shot" : "Their turn";
    c.textClipped(ENEMY_X, 50, 250, head, TS_LARGE, true, true);
    snprintf(buf, sizeof(buf), "their fleet: %d of %d afloat", enemyAfloat(), sea::SHIPS);
    c.text(ENEMY_X + 440 - c.textWidth(buf, TS_SMALL), 60, buf, TS_SMALL, true);
    drawGrid(c, ENEMY_X, ENEMY_Y, ENEMY_CELL, enemyBoard(), false, fire ? _sel : -1);

    // The blast overrunning its own square is the whole point of it: for one
    // frame the shot is bigger than the grid it landed in.
    if (_flashCell >= 0) {
      const int fx = ENEMY_X + sea::xOf(_flashCell) * ENEMY_CELL + ENEMY_CELL / 2;
      const int fy = ENEMY_Y + sea::yOf(_flashCell) * ENEMY_CELL + ENEMY_CELL / 2;
      decor::rays(c, fx, fy, ENEMY_CELL - 4, ENEMY_CELL + 26, 12, 0.13f, true);
      decor::blast(c, fx, fy, ENEMY_CELL, (uint32_t)_flashCell, true);
      decor::debris(c, fx, fy, ENEMY_CELL * 2, (uint32_t)_flashCell, true);
    }

    // Your own fleet, small, with what has happened to it beside it (kit).
    drawGrid(c, OWN_X, OWN_Y, OWN_CELL, myBoard(), true, -1);
    int ty = OWN_Y + 8;
    c.text(OWN_TEXT_X, ty, "YOUR FLEET", TS_SMALL, true, true);
    ty += 30;
    snprintf(buf, sizeof(buf), "%d of %d afloat", myAfloat(), sea::SHIPS);
    c.text(OWN_TEXT_X, ty, buf, TS_SMALL, true);
    ty += 28;
    const int taken = hitsTaken(myBoard());
    snprintf(buf, sizeof(buf), "%d %s taken", taken, taken == 1 ? "hit" : "hits");
    c.text(OWN_TEXT_X, ty, buf, TS_SMALL, true);
    ty += 40;
    c.text(OWN_TEXT_X, ty, againRule() ? "a hit fires again" : "one shot a turn", TS_SMALL, true);
    ty += 28;
    c.text(OWN_TEXT_X, ty, "UP / DOWN aim - OK fire", TS_SMALL, true);

    if (fire && _sel >= 0)
      snprintf(buf, sizeof(buf), "tap %c%d again to fire", 'A' + sea::xOf(_sel), sea::yOf(_sel) + 1);
    else
      snprintf(buf, sizeof(buf), "shots %d - hits %d", _shots, _hits);
    gk::foot(c, buf);
  }

  void tapPlay(int x, int y) {
    using namespace seaui;
    if (!canFire()) return;
    if (x < ENEMY_X || x >= ENEMY_X + sea::N * ENEMY_CELL || y < ENEMY_Y ||
        y >= ENEMY_Y + sea::N * ENEMY_CELL)
      return;
    const int cell = sea::cellAt((x - ENEMY_X) / ENEMY_CELL, (y - ENEMY_Y) / ENEMY_CELL);
    if (enemyBoard().shot[cell] != sea::UNSHOT) {
      host().beep(2);
      return;
    }
    // The first tap aims; the second, on the same square, fires.
    if (cell != _sel) {
      _sel = cell;
      host().beep(0);
      host().refresh(false);
      return;
    }
    fireAt(cell);
  }

  void fireAt(int cell) {
    _sel = -1;
    if (_duelMode) {
      if (!_duel.fire(cell, millis())) return;
      host().beep(1);
      snprintf(_msg, sizeof(_msg), "shot away...");
      host().refresh(false);
      return;
    }
    soloTurn(cell);
  }

  // Your shot and the Sticky's reply resolve into a single frame: on e-ink,
  // two refreshes to play one exchange is most of a second wasted. With "a hit
  // fires again", a hit of yours keeps the turn, and the Sticky's hits let it
  // fire on in the same frame.
  void soloTurn(int cell) {
    const sea::Shot mine = sea::fire(_enemy, cell);
    _shots++;
    if (mine.hit) _hits++;
    if (mine.allSunk) {
      finishSolo(true);
      return;
    }
    if (mine.hit && _againSolo) {
      snprintf(_msg, sizeof(_msg), "%s", mine.sunk >= 0 ? "Sunk! Fire again" : "Hit! Fire again");
      host().beep(mine.sunk >= 0 ? 3 : 1);
      blast(cell);
      host().refresh(false);
      return;
    }
    int theirHits = 0, theirSunk = 0, theirShots = 0;
    for (;;) {
      const int reply = _gunner.choose([] { return esp_random(); });
      if (reply < 0) break;
      const sea::Shot theirs = sea::fire(_mine, reply);
      _gunner.observe(reply, theirs, [] { return esp_random(); });
      theirShots++;
      if (theirs.hit) theirHits++;
      if (theirs.sunk >= 0) theirSunk++;
      if (theirs.allSunk) {
        finishSolo(false);
        return;
      }
      if (!(theirs.hit && _againSolo)) break;
    }
    const char* you = mine.sunk >= 0 ? "Sunk one" : mine.hit ? "Hit" : "Miss";
    if (theirSunk)
      snprintf(_msg, sizeof(_msg), "%s - they sank one", you);
    else if (theirHits > 1)
      snprintf(_msg, sizeof(_msg), "%s - hit %d times", you, theirHits);
    else if (theirHits)
      snprintf(_msg, sizeof(_msg), "%s - they hit you", you);
    else
      snprintf(_msg, sizeof(_msg), "%s - they missed", you);
    (void)theirShots;
    host().beep(mine.sunk >= 0 ? 3 : (mine.hit ? 1 : 0));
    if (mine.hit) blast(cell);
    host().refresh(false);
  }

  // A shell landing gets one oversized frame before the board settles back to
  // its ordinary marks.
  void blast(int cell) {
    _flashCell = (int8_t)cell;
    _flashUntil = millis() + 320;
  }

  void finishSolo(bool won) {
    recordResult(won);
    _screen = Screen::Over;
    host().beep(won ? 3 : 2);
    host().refresh(true);
  }

  void recordResult(bool won) {
    prefs().putInt(won ? "bs_w" : "bs_l", prefs().getInt(won ? "bs_w" : "bs_l", 0) + 1);
    prefs().putInt("bs_strk", won ? prefs().getInt("bs_strk", 0) + 1 : 0);
  }

  // --- over (kit "Ships, game over") ------------------------------------------
  void renderOver(ToolsCanvas& c) {
    gk::bar(host(), c, title());
    const bool lost = _duelMode && _duel.phase() == seanet::Phase::Lost;
    const bool won = _duelMode ? _duel.won() : (enemyAfloat() == 0);
    char pct[8], wins[8], row[8], line[48];
    snprintf(pct, sizeof(pct), "%d%%", _shots ? (100 * _hits + _shots / 2) / _shots : 0);
    snprintf(wins, sizeof(wins), "%d", prefs().getInt("bs_w", 0));
    snprintf(row, sizeof(row), "%d", prefs().getInt("bs_strk", 0));
    const gk::Stat st[3] = {{pct, "hits"}, {wins, "wins"}, {row, "in a row"}};
    if (lost) {
      gk::result(c, "GAME ENDED", "They left", "the other Sticky went away", nullptr, 0, "Menu",
                 nullptr);
      return;
    }
    if (won)
      snprintf(line, sizeof(line), "every ship sunk in %d shots", _shots);
    else
      snprintf(line, sizeof(line), "%d of theirs still afloat", enemyAfloat());
    gk::result(c, "GAME OVER", won ? "You won" : "You lost", line, st, 3, "Menu", "Again");
    gk::foot(c, "your fleet's record against all comers");
  }

  void tapOver(int x, int y) {
    if (gk::RES_LEFT.hit(x, y)) {
      if (_duelMode) _duel.end();
      _duelMode = false;
      _screen = Screen::Menu;
      _msg[0] = 0;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (!gk::RES_RIGHT.hit(x, y)) return;
    if (_duelMode && _duel.phase() == seanet::Phase::Lost) return;
    host().beep(1);
    _msg[0] = 0;
    _sel = -1;
    if (_duelMode) {
      _duel.requestRematch(millis());
      startPlacing();
      syncDuelScreen();
    } else {
      _againSolo = fireAgainPref();
      sea::clear(_enemy);
      sea::placeRandom(_enemy, [] { return esp_random(); });
      _gunner.reset(levelPref());
      startPlacing();
      _screen = Screen::Setup;
    }
    host().refreshUi();
  }

  // --- drawing -----------------------------------------------------------
  // showShips is the whole privacy model: your own board draws its hulls, the
  // enemy board only ever draws what you have learned by firing at it.
  void drawGrid(ToolsCanvas& c, int gx, int gy, int cell, const sea::Board& b, bool showShips,
                int selected) {
    const int span = sea::N * cell;
    for (int i = 0; i <= sea::N; i++) {
      c.fillRect(gx + i * cell, gy, 1, span + 1, true);
      c.fillRect(gx, gy + i * cell, span + 1, 1, true);
    }
    c.drawRect(gx - 2, gy - 2, span + 5, span + 5, 2, true);
    const bool big = cell > 30;
    for (int k = 0; k < sea::CELLS; k++) {
      const int x = gx + sea::xOf(k) * cell, y = gy + sea::yOf(k) * cell;
      const int mx = x + cell / 2, my = y + cell / 2;
      const uint8_t shot = b.shot[k];
      // A sunk hull is no longer a secret: you watched it go down, so it is
      // drawn on either board.
      const bool wreck = b.ship[k] && sea::sunk(b, b.ship[k] - 1);
      const bool ship = (showShips || wreck) && b.ship[k];
      if (shot == sea::HIT && (ship || !showShips)) {
        // Kit: a hit is a black square with a white cross; a wreck the same
        // with the cross heavier.
        c.fillRect(x + 2, y + 2, cell - 3, cell - 3, true);
        const int in = big ? 11 : 4, t = big ? (wreck ? 5 : 4) : 2;
        c.drawLine(x + in, y + in, x + cell - in, y + cell - in, t, false);
        c.drawLine(x + cell - in, y + in, x + in, y + cell - in, t, false);
      } else if (ship) {
        // Your own hulls: striped on the small map, as the kit draws them.
        if (big)
          c.drawRect(x + 5, y + 5, cell - 10, cell - 10, 3, true);
        else
          gk::stripes(c, x + 1, y + 1, cell - 1, cell - 1);
        if (shot == sea::HIT) c.fillRect(x + 2, y + 2, cell - 3, cell - 3, true);
      } else if (shot == sea::MISS) {
        c.fillCircle(mx, my, big ? 3 : 2, true);
      }
    }
    if (selected >= 0) {
      const int x = gx + sea::xOf(selected) * cell, y = gy + sea::yOf(selected) * cell;
      c.fillRect(x + 1, y + 1, cell - 1, cell - 1, false);
      c.drawRect(x - 1, y - 1, cell + 3, cell + 3, 4, true);
    }
  }

#ifdef TOYBOX_HOST
 public:
  // Sinks one enemy ship outright, so the preview can show wreckage without
  // depending on where a random fleet happened to be placed.
  void hostSinkShip(int id) {
    for (int k = 0; k < sea::CELLS; k++)
      if (_enemy.ship[k] == (uint8_t)(id + 1)) sea::fire(_enemy, k);
  }
  int hostScreen() const { return (int)_screen; }
  const sea::Board& hostMine() const { return myBoard(); }
  const sea::Board& hostEnemy() const { return enemyBoard(); }
  int hostSel() const { return _sel; }
  int hostShots() const { return _shots; }
  const char* hostMsg() const { return _msg; }
  int hostChipX(int s) const { return _chipX[s] + _chipW[s] / 2; }

 private:
#endif
  View _view = View::Game;
  Screen _screen = Screen::Menu;
  int8_t _flashCell = -1;
  uint32_t _flashUntil = 0;
  uint8_t _lastTheirs[sea::CELLS] = {};
  bool _armedClear = false;
  bool _duelMode = false;
  bool _rulesFromMore = false;  // the card came from More: Back returns to the game
  int _sel = -1;
  int _foundShown = -1;
  char _msg[48] = {};

  // placing
  int _hand = 0;      // the ship in hand, -1 when none
  bool _horiz = true;
  int _aim = -1;      // the square tapped, where the ship in hand would start
  int16_t _chipX[sea::SHIPS] = {}, _chipW[sea::SHIPS] = {};

  // this game
  int _shots = 0, _hits = 0;

  // solo
  sea::Board _mine{}, _enemy{};
  sea::Gunner _gunner{};
  bool _myTurnSolo = true;
  bool _againSolo = true;

  seanet::Duel _duel;
};
