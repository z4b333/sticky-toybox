// Battleship, alone against the device or across two of them.
//
// Firing is select-then-confirm rather than tap-to-fire. At this screen's
// density an 8x8 grid gives cells about 5 mm across, which is under a
// fingertip; picking a cell and then pressing a full-width FIRE button removes
// the whole class of "I meant the square above" mistakes, and on a panel that
// cannot show a hover state it also makes the target visible before it is spent.
#pragma once
#include <esp_random.h>

#include "decor.h"
#include "help.h"
#include "record.h"
#include "sea_net.h"
#include "sea_rules.h"
#include "tools_draw.h"

namespace seaui {
// menu
// Two choices and nothing else, so they are sized like two cards rather than
// two buttons: at 92 px tall and 140 apart they sat in the middle of the panel
// with 200 px of nothing under them. The rhythm below is one gap -- about 90 px
// -- repeated between the intro, each choice, and the record line.
inline constexpr TRect SOLO_BTN{40, 168, 400, 132};
inline constexpr TRect DUEL_BTN{40, 420, 400, 132};  // clears the caption above it
// Under the won/lost line on the menu, the one screen here with room to spare.
inline constexpr TRect CLEAR_BTN{(480 - record::BTN_W) / 2, 730, record::BTN_W, record::BTN_H};

// pairing
inline constexpr TRect HOST_BTN{40, 150, 400, 84};
inline constexpr TRect BROWSE_BTN{40, 254, 400, 84};
inline constexpr TRect CANCEL_BTN{40, 690, 400, 60};
inline constexpr int FOUND_Y = 200, FOUND_H = 76, FOUND_GAP = 12;

// setup
inline constexpr int SET_CELL = 50, SET_X = 40, SET_Y = 116;
inline constexpr TRect SHUFFLE_BTN{40, 552, 190, 68};
inline constexpr TRect READY_BTN{250, 552, 190, 68};

// play
inline constexpr int ENEMY_CELL = 50, ENEMY_X = 40, ENEMY_Y = 100;
inline constexpr TRect FIRE_BTN{40, 512, 400, 60};
inline constexpr int OWN_CELL = 24, OWN_X = 144, OWN_Y = 600;

// over
// Below the closing look at your fleet, not through it.
inline constexpr int OVER_GRID_Y = 340;
inline constexpr TRect AGAIN_BTN{40, 600, 190, 72};
inline constexpr TRect DONE_BTN{250, 600, 190, 72};

inline TRect foundRect(int i) {
  return TRect{40, FOUND_Y + i * (FOUND_H + FOUND_GAP), 400, FOUND_H};
}
}  // namespace seaui

class SeaTool : public ToolApp {
 public:
  const char* title() const override { return "BATTLESHIP"; }

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _help = !help::suppressed(prefs(), "sea");
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
      host().refresh(false);
      return;
    }
    if (!_duelMode) return;
    const seanet::Phase before = _duel.phase();
    const bool wasMyTurn = _duel.myTurn();
    const int seenBefore = shotCount(_duel.theirs()) + shotCount(_duel.mine());
    _duel.poll(millis());
    const int foundNow = _duel.foundCount();

    // Repaint only when something a player would notice has changed: e-ink
    // cannot afford a frame per poll.
    bool dirty = _duel.phase() != before || _duel.myTurn() != wasMyTurn ||
                 shotCount(_duel.theirs()) + shotCount(_duel.mine()) != seenBefore ||
                 (_screen == Screen::Pair && foundNow != _foundShown);
    _foundShown = foundNow;
    if (!dirty) return;

    syncDuelScreen();
    // A hit that arrived over the air deserves the same shell as one fired at
    // the device: find the square that changed, and light it up.
    for (int k = 0; k < sea::CELLS; k++) {
      const uint8_t now = _duel.theirs().shot[k];
      if (now == sea::HIT && _lastTheirs[k] != sea::HIT) blast(k);
      _lastTheirs[k] = now;
    }
    host().refresh(_duel.phase() == seanet::Phase::Over);
  }

  void render(ToolsCanvas& c) override {
    host().topBar(title(), true);
    if (_help) {
      help::render(c, help::SHIPS);
      return;
    }
    switch (_screen) {
      case Screen::Menu: renderMenu(c); break;
      case Screen::Pair: renderPair(c); break;
      case Screen::Setup: renderSetup(c); break;
      case Screen::Play: renderPlay(c); break;
      default: renderOver(c); break;
    }
  }

  void onTap(int x, int y) override {
    if (host().isBackTap(x, y)) {
      if (_duelMode) _duel.end();
      host().goHub();
      return;
    }
    if (host().isHelpTap(x, y)) return toggleHelp();
    if (_help) return tapHelp(x, y);
    switch (_screen) {
      case Screen::Menu: tapMenu(x, y); break;
      case Screen::Pair: tapPair(x, y); break;
      case Screen::Setup: tapSetup(x, y); break;
      case Screen::Play: tapPlay(x, y); break;
      default: tapOver(x, y); break;
    }
  }

 private:
  // The rules card sits over whatever screen is underneath, so dismissing it
  // never loses a game in progress.
  void toggleHelp() {
    _help = !_help;
    host().beep(1);
    host().refreshUi();
  }
  void tapHelp(int x, int y) {
    const help::Tap t = help::hit(x, y);
    if (t == help::Tap::None) return;
    if (t == help::Tap::Never) help::suppress(prefs(), "sea");
    _help = false;
    host().beep(1);
    host().refreshUi();
  }

  enum class Screen : uint8_t { Menu, Pair, Setup, Play, Over };

  // --- boards ------------------------------------------------------------
  // One pair of accessors so every screen draws the same way in both modes.
  const sea::Board& myBoard() const { return _duelMode ? _duel.mine() : _mine; }
  const sea::Board& enemyBoard() const { return _duelMode ? _duel.theirs() : _enemy; }
  int myAfloat() const { return _duelMode ? _duel.myAfloat() : sea::afloat(_mine); }
  int enemyAfloat() const { return _duelMode ? _duel.theirAfloat() : sea::afloat(_enemy); }

  static int shotCount(const sea::Board& b) {
    int n = 0;
    for (int c = 0; c < sea::CELLS; c++)
      if (b.shot[c] != sea::UNSHOT) n++;
    return n;
  }

  void shuffleFleet() {
    if (_duelMode) {
      _duel.placeFleet(esp_random());
    } else {
      sea::clear(_mine);
      sea::placeRandom(_mine, [] { return esp_random(); });
    }
  }

  // --- menu --------------------------------------------------------------
  void renderMenu(ToolsCanvas& c) {
    using namespace seaui;
    c.textCentered(c.width() / 2, 60, "Sink the other fleet first", TS_MED, true);

    c.button(SOLO_BTN.x, SOLO_BTN.y, SOLO_BTN.w, SOLO_BTN.h, "PLAY THE DEVICE", true, TS_LARGE);
    c.textCentered(c.width() / 2, SOLO_BTN.y + SOLO_BTN.h + 12, "hunts like a person",
                   TS_MED, true);

    c.button(DUEL_BTN.x, DUEL_BTN.y, DUEL_BTN.w, DUEL_BTN.h, "TWO DEVICES", false, TS_LARGE);
    c.textCentered(c.width() / 2, DUEL_BTN.y + DUEL_BTN.h + 12, "hidden fleets, over the air",
                   TS_MED, true);
    c.textCentered(c.width() / 2, DUEL_BTN.y + DUEL_BTN.h + 38,
                   "needs a second Toybox nearby", TS_MED, true);

    char buf[48];
    const int w = prefs().getInt("bs_w", 0), l = prefs().getInt("bs_l", 0);
    if (w + l > 0) {
      snprintf(buf, sizeof(buf), "won %d   lost %d", w, l);
      c.textCentered(c.width() / 2, 700, buf, TS_MED, true);
      c.button(seaui::CLEAR_BTN.x, seaui::CLEAR_BTN.y, seaui::CLEAR_BTN.w, seaui::CLEAR_BTN.h,
               record::label(_armedClear), _armedClear, TS_MED);
    }
  }

  void tapMenu(int x, int y) {
    using namespace seaui;

    const bool wasArmed = _armedClear;
    _armedClear = false;
    const bool haveRecord = prefs().getInt("bs_w", 0) + prefs().getInt("bs_l", 0) > 0;
    if (CLEAR_BTN.hit(x, y) && haveRecord) {
      if (wasArmed) {
        record::clear(prefs(), record::SHIPS);
        host().beep(1);
      } else {
        _armedClear = true;
        host().beep(2);
      }
      host().refresh(false);
      return;
    }
    if (wasArmed) host().refresh(false);

    if (SOLO_BTN.hit(x, y)) {
      _duelMode = false;
      shuffleFleet();
      sea::clear(_enemy);
      sea::placeRandom(_enemy, [] { return esp_random(); });
      _gunner.reset();
      _screen = Screen::Setup;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (DUEL_BTN.hit(x, y)) {
      _duelMode = true;
      _foundShown = -1;
      if (!_duel.begin()) {
        snprintf(_msg, sizeof(_msg), "could not start the radio");
        _duelMode = false;
        host().beep(2);
        host().refresh(false);
        return;
      }
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
      c.textCentered(c.width() / 2, 516, "keep both devices in the same room", TS_SMALL, true);
    } else if (p == Phase::Browsing || p == Phase::Joining) {
      c.textCentered(c.width() / 2, 76, "GAMES NEARBY", TS_MED, true);
      const int n = _duel.foundCount();
      if (n == 0) {
        c.textCentered(c.width() / 2, 250, "looking...", TS_LARGE, true, true);
        c.textCentered(c.width() / 2, 302, "the other device must be", TS_MED, true);
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
      c.textCentered(c.width() / 2, 76, "Two devices, one game", TS_MED, true);
      c.button(HOST_BTN.x, HOST_BTN.y, HOST_BTN.w, HOST_BTN.h, "HOST A GAME", true, TS_LARGE);
      c.textCentered(c.width() / 2, HOST_BTN.y + HOST_BTN.h + 10,
                     "shows a code for the other player", TS_SMALL, true);
      c.button(BROWSE_BTN.x, BROWSE_BTN.y, BROWSE_BTN.w, BROWSE_BTN.h, "JOIN A GAME", false,
               TS_LARGE);
      c.textCentered(c.width() / 2, BROWSE_BTN.y + BROWSE_BTN.h + 10,
                     "lists the codes it can hear", TS_SMALL, true);
      c.textCentered(c.width() / 2, 452, "Both devices stay on their", TS_MED, true);
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
        _duel.startHosting(millis());
        host().beep(1);
        host().refreshUi();
      } else if (BROWSE_BTN.hit(x, y)) {
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
          shuffleFleet();
          _sel = -1;
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

  // --- setup -------------------------------------------------------------
  void renderSetup(ToolsCanvas& c) {
    using namespace seaui;
    c.textCentered(c.width() / 2, 58, "YOUR FLEET", TS_MED, true);
    c.textCentered(c.width() / 2, 86, "4 . 3 . 3 . 2", TS_MED, true);
    drawGrid(c, SET_X, SET_Y, SET_CELL, myBoard(), true, -1);

    const bool waiting = _duelMode && _duel.waitingForPeerReady();
    c.button(SHUFFLE_BTN.x, SHUFFLE_BTN.y, SHUFFLE_BTN.w, SHUFFLE_BTN.h, "SHUFFLE", false,
             TS_LARGE);
    c.button(READY_BTN.x, READY_BTN.y, READY_BTN.w, READY_BTN.h, waiting ? "WAITING" : "READY",
             !waiting, TS_LARGE);

    if (waiting) {
      c.textCentered(c.width() / 2, 646, "waiting for the other player", TS_MED, true, true);
      if (_duel.peerRestarted())
        c.textCentered(c.width() / 2, 680, "they want another game", TS_MED, true);
    } else {
      c.textCentered(c.width() / 2, 646, "SHUFFLE until you like it", TS_MED, true);
      c.textCentered(c.width() / 2, 674, "ships may touch, and none", TS_MED, true);
      c.textCentered(c.width() / 2, 702, "of this is hidden from you", TS_MED, true);
    }
  }

  void tapSetup(int x, int y) {
    using namespace seaui;
    if (_duelMode && _duel.waitingForPeerReady()) return;
    if (SHUFFLE_BTN.hit(x, y)) {
      shuffleFleet();
      host().beep(0);
      host().refresh(false);
      return;
    }
    if (!READY_BTN.hit(x, y)) return;
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
  }

  // --- play --------------------------------------------------------------
  void renderPlay(ToolsCanvas& c) {
    using namespace seaui;
    char buf[48];

    const bool canFire = _duelMode ? _duel.myTurn() : _myTurnSolo;
    if (_msg[0])
      c.textCentered(c.width() / 2, 52, _msg, TS_MED, true, true);
    else
      c.textCentered(c.width() / 2, 52, canFire ? "YOUR SHOT" : "waiting for them...", TS_MED,
                     true, canFire);

    c.text(ENEMY_X, 78, "ENEMY WATERS", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d afloat", enemyAfloat());
    c.text(ENEMY_X + 400 - c.textWidth(buf, TS_MED), 78, buf, TS_MED, true);
    drawGrid(c, ENEMY_X, ENEMY_Y, ENEMY_CELL, enemyBoard(), false, _sel);

    // The blast overrunning its own square is the whole point of it: for one
    // frame the shot is bigger than the grid it landed in.
    if (_flashCell >= 0) {
      const int fx = ENEMY_X + sea::xOf(_flashCell) * ENEMY_CELL + ENEMY_CELL / 2;
      const int fy = ENEMY_Y + sea::yOf(_flashCell) * ENEMY_CELL + ENEMY_CELL / 2;
      // For one frame the shell is bigger than the square it landed in, with
      // the radiating strokes a comic puts round a bang.
      decor::rays(c, fx, fy, ENEMY_CELL - 4, ENEMY_CELL + 26, 12, 0.13f, true);
      decor::blast(c, fx, fy, ENEMY_CELL, (uint32_t)_flashCell, true);
      decor::debris(c, fx, fy, ENEMY_CELL * 2, (uint32_t)_flashCell, true);
    }

    const bool armed = canFire && _sel >= 0;
    if (armed) {
      snprintf(buf, sizeof(buf), "FIRE AT %c%d", 'A' + sea::xOf(_sel), sea::yOf(_sel) + 1);
      c.button(FIRE_BTN.x, FIRE_BTN.y, FIRE_BTN.w, FIRE_BTN.h, buf, true, TS_LARGE);
    } else {
      c.button(FIRE_BTN.x, FIRE_BTN.y, FIRE_BTN.w, FIRE_BTN.h,
               canFire ? "PICK A SQUARE" : "THEIR TURN", false, TS_MED);
    }

    // Headed across the full width, not across the little grid: at this text
    // size the label and the count together are wider than the board below.
    c.text(ENEMY_X, 576, "YOUR FLEET", TS_MED, true);
    snprintf(buf, sizeof(buf), "%d afloat", myAfloat());
    c.text(ENEMY_X + 400 - c.textWidth(buf, TS_MED), 576, buf, TS_MED, true);
    drawGrid(c, OWN_X, OWN_Y, OWN_CELL, myBoard(), true, -1);
  }

  void tapPlay(int x, int y) {
    using namespace seaui;
    const bool canFire = _duelMode ? _duel.myTurn() : _myTurnSolo;
    if (!canFire) return;

    // Selecting is free and repeatable; only FIRE spends the turn.
    if (x >= ENEMY_X && x < ENEMY_X + 8 * ENEMY_CELL && y >= ENEMY_Y &&
        y < ENEMY_Y + 8 * ENEMY_CELL) {
      const int cell = sea::cellAt((x - ENEMY_X) / ENEMY_CELL, (y - ENEMY_Y) / ENEMY_CELL);
      if (enemyBoard().shot[cell] != sea::UNSHOT) {
        host().beep(2);
        return;
      }
      _sel = cell;
      _msg[0] = 0;
      host().beep(0);
      host().refresh(false);
      return;
    }

    if (!FIRE_BTN.hit(x, y) || _sel < 0) return;
    const int cell = _sel;
    _sel = -1;
    if (_duelMode) {
      _duel.fire(cell, millis());
      host().beep(1);
      snprintf(_msg, sizeof(_msg), "shot away...");
      host().refresh(false);
      return;
    }
    soloTurn(cell);
  }

  // Both shots resolve into a single frame: on e-ink, two refreshes to play one
  // exchange is most of a second wasted.
  void soloTurn(int cell) {
    const sea::Shot mine = sea::fire(_enemy, cell);
    if (mine.allSunk) {
      finishSolo(true);
      return;
    }
    const int reply = _gunner.choose([] { return esp_random(); });
    sea::Shot theirs;
    if (reply >= 0) {
      theirs = sea::fire(_mine, reply);
      _gunner.observe(reply, theirs);
    }
    if (theirs.allSunk) {
      finishSolo(false);
      return;
    }
    describe(mine, theirs, reply);
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

  void describe(const sea::Shot& mine, const sea::Shot& theirs, int reply) {
    const char* you = mine.sunk >= 0 ? "SUNK ONE" : (mine.hit ? "HIT" : "miss");
    if (reply < 0) {
      snprintf(_msg, sizeof(_msg), "%s", you);
      return;
    }
    const char* them = theirs.sunk >= 0 ? "sank one of yours"
                       : theirs.hit    ? "hit you"
                                       : "missed";
    snprintf(_msg, sizeof(_msg), "%s - they %s", you, them);
  }

  void finishSolo(bool won) {
    recordResult(won);
    _screen = Screen::Over;
    host().beep(won ? 3 : 2);
    host().refreshUi();
  }

  void recordResult(bool won) {
    prefs().putInt(won ? "bs_w" : "bs_l", prefs().getInt(won ? "bs_w" : "bs_l", 0) + 1);
  }

  // --- over --------------------------------------------------------------
  void renderOver(ToolsCanvas& c) {
    using namespace seaui;
    const bool lost = _duelMode && _duel.phase() == seanet::Phase::Lost;
    const bool won = _duelMode ? _duel.won() : (enemyAfloat() == 0);

    if (lost) {
      c.textCentered(c.width() / 2, 180, "THE OTHER DEVICE", TS_LARGE, true, true);
      c.textCentered(c.width() / 2, 220, "WENT AWAY", TS_LARGE, true, true);
      c.textCentered(c.width() / 2, 292, "out of range, or it left the game", TS_MED, true);
    } else {
      if (won) decor::confetti(c, 30, 90, 420, 220, 11, 16, true);
      decor::banner(c, 40, 156, 400, 70, won ? "FLEET SUNK" : "YOU ARE SUNK", TS_LARGE, won);
      c.textCentered(c.width() / 2, 244, won ? "you win" : "they win", TS_LARGE, true);
      char buf[48];
      snprintf(buf, sizeof(buf), "%d of your ships still afloat", myAfloat());
      c.textCentered(c.width() / 2, 320, buf, TS_MED, true);
    }

    drawGrid(c, OWN_X, OVER_GRID_Y, OWN_CELL, myBoard(), true, -1);
    c.textCentered(c.width() / 2, OVER_GRID_Y + 8 * OWN_CELL + 12, "your fleet at the end",
                   TS_MED, true);

    if (!lost)
      c.button(AGAIN_BTN.x, AGAIN_BTN.y, AGAIN_BTN.w, AGAIN_BTN.h, "AGAIN", true, TS_LARGE);
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", false, TS_LARGE);
  }

  void tapOver(int x, int y) {
    using namespace seaui;
    if (DONE_BTN.hit(x, y)) {
      if (_duelMode) _duel.end();
      _duelMode = false;
      _screen = Screen::Menu;
      _msg[0] = 0;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (!AGAIN_BTN.hit(x, y)) return;
    host().beep(1);
    _msg[0] = 0;
    _sel = -1;
    if (_duelMode) {
      _duel.requestRematch(millis());
      syncDuelScreen();
    } else {
      shuffleFleet();
      sea::clear(_enemy);
      sea::placeRandom(_enemy, [] { return esp_random(); });
      _gunner.reset();
      _screen = Screen::Setup;
    }
    host().refreshUi();
  }

  // --- drawing -----------------------------------------------------------
  // showShips is the whole privacy model: your own board draws its hulls, the
  // enemy board only ever draws what you have learned by firing at it.
  void drawGrid(ToolsCanvas& c, int gx, int gy, int cell, const sea::Board& b, bool showShips,
                int selected) {
    const int span = 8 * cell;
    for (int i = 0; i <= 8; i++) {
      c.fillRect(gx + i * cell, gy, 1, span + 1, true);
      c.fillRect(gx, gy + i * cell, span + 1, 1, true);
    }
    // The big board can carry drawn marks; the thumbnail of your own fleet is
    // 24 px a cell, where a burst would come out as a smudge, so it keeps the
    // plain blocks and dots it always had.
    const bool big = cell > 30;
    for (int k = 0; k < sea::CELLS; k++) {
      const int x = gx + sea::xOf(k) * cell, y = gy + sea::yOf(k) * cell;
      const int mx = x + cell / 2, my = y + cell / 2;
      const uint8_t shot = b.shot[k];
      // A sunk hull is no longer a secret: you watched it go down, so it is
      // drawn on either board, wreckage and all.
      const bool wreck = b.ship[k] && sea::sunk(b, b.ship[k] - 1);
      const bool ship = (showShips || wreck) && b.ship[k];

      if (wreck && shot == sea::HIT) {
        c.fillRect(x + 2, y + 2, cell - 3, cell - 3, true);
        if (big) {
          decor::blast(c, mx, my, cell / 3, (uint32_t)k + 7, false);
          decor::debris(c, mx, my, cell / 2, (uint32_t)k, false);
        } else {
          c.fillRect(mx - 1, my - 5, 3, 11, false);
          c.fillRect(mx - 5, my - 1, 11, 3, false);
        }
      } else if (ship && shot == sea::HIT) {
        c.fillRect(x + 2, y + 2, cell - 3, cell - 3, true);
        if (big) decor::blast(c, mx, my, cell / 3, (uint32_t)k, false);
      } else if (ship) {
        const int in = big ? 5 : 3;
        c.drawRect(x + in, y + in, cell - 2 * in, cell - 2 * in, big ? 3 : 2, true);
      } else if (shot == sea::HIT) {
        // A hit on water we cannot see means a hull is there: fill it in.
        if (big) {
          decor::blast(c, mx, my, cell / 2 - 3, (uint32_t)k, true);
        } else {
          c.fillRect(x + 4, y + 4, cell - 8, cell - 8, true);
        }
      } else if (shot == sea::MISS) {
        if (big)
          decor::peg(c, mx, my, cell / 2 - 16, true);
        else
          c.fillCircle(mx, my, 2, true);
      }
    }
    if (selected >= 0) {
      const int x = gx + sea::xOf(selected) * cell, y = gy + sea::yOf(selected) * cell;
      c.drawRect(x - 2, y - 2, cell + 5, cell + 5, 4, true);
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

 private:
#endif
  Screen _screen = Screen::Menu;
  int8_t _flashCell = -1;
  uint32_t _flashUntil = 0;
  uint8_t _lastTheirs[sea::CELLS] = {};
  bool _help = false;
  bool _armedClear = false;
  bool _duelMode = false;
  int _sel = -1;
  int _foundShown = -1;
  char _msg[48] = {};

  // solo
  sea::Board _mine{}, _enemy{};
  sea::Gunner _gunner{};
  bool _myTurnSolo = true;

  seanet::Duel _duel;
};
