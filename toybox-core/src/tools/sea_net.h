// The two-device Battleship protocol, with no display attached.
//
// Kept apart from the screen so a test can stand up two Duels, wire them
// through the host link, and play whole games — including games where the radio
// drops frames. That is the only way to be sure the retries are right, because
// on real hardware a lost packet is exactly the thing you cannot reproduce.
//
// Every message is idempotent. A shot carries a sequence number and the
// defender answers a repeat from its cached result rather than firing again, so
// a resend can never punch a second hole in the same hull.
#pragma once
#include "link.h"
#include "sea_rules.h"

namespace seanet {

enum : uint8_t {
  T_HELLO = 1,   // host -> everyone: "a game is open here"
  T_JOIN,        // guest -> host
  T_ACCEPT,      // host -> guest
  T_READY,       // either: "my fleet is placed"
  T_FIRE,        // {seq, cell}
  T_RESULT,      // {seq, cell, hit, sunk, allSunk}
  T_BYE,         // leaving
  T_REMATCH,     // "again?"
};

enum class Phase : uint8_t {
  Idle,      // link down
  Hosting,   // advertising, waiting for a guest
  Browsing,  // listening for hosts
  Joining,   // asked to join, waiting to be accepted
  Setup,     // paired; both sides placing fleets
  Play,
  Over,
  Lost,      // peer went away mid-game
};

constexpr uint32_t RETRY_MS = 700;
constexpr uint32_t HELLO_MS = 700;
constexpr uint32_t PEER_TIMEOUT_MS = 12000;  // silence this long means gone
constexpr int MAX_FOUND = 4;

struct Found {
  uint8_t mac[radio::MAC_LEN];
  uint16_t code;
  uint32_t seen;
};

class Duel {
 public:
  // --- lifecycle ---------------------------------------------------------
  bool begin() {
    if (!_link.begin()) return false;
    _phase = Phase::Idle;
    return true;
  }

  void end() {
    if (_link.up() && _peerKnown) _link.sendTo(_peer, T_BYE, nullptr, 0);
    _link.end();
    reset();
    _phase = Phase::Idle;
  }

  radio::Link& transport() { return _link; }  // tests reach the drop hook here

  void reset() {
    _peerKnown = false;
    _iAmHost = false;
    _myReady = _peerReady = false;
    _foundN = 0;
    _fireSeq = 0;
    _pending = false;
    _lastAnswered = 0xFF;
    _peerRestarted = false;
    _won = false;
    sea::clear(_mine);
    sea::clear(_theirs);
    _theirAfloat = sea::SHIPS;
  }

  // --- pairing -----------------------------------------------------------
  void startHosting(uint32_t now) {
    reset();
    _iAmHost = true;
    _phase = Phase::Hosting;
    _lastHello = now - HELLO_MS;
  }

  void startBrowsing() {
    reset();
    _iAmHost = false;
    _phase = Phase::Browsing;
  }

  int foundCount() const { return _foundN; }
  const Found& found(int i) const { return _found[i]; }

  bool joinFound(int i, uint32_t now) {
    if (i < 0 || i >= _foundN) return false;
    memcpy(_peer, _found[i].mac, radio::MAC_LEN);
    _peerKnown = true;
    _phase = Phase::Joining;
    _lastTry = now - RETRY_MS;
    _peerSeen = now;
    return true;
  }

  // --- setup and play ----------------------------------------------------
  void placeFleet(uint32_t seed) {
    uint32_t s = seed ? seed : 1u;
    sea::placeRandom(_mine, [&s] {  // xorshift: the caller supplies the entropy
      s ^= s << 13;
      s ^= s >> 17;
      s ^= s << 5;
      return s;
    });
  }

  void setReady(uint32_t now) {
    _myReady = true;
    _lastTry = now - RETRY_MS;
    tryAdvance();
  }

  bool myTurn() const { return _phase == Phase::Play && _turn && !_pending; }

  bool fire(int cell, uint32_t now) {
    if (!myTurn() || cell < 0 || cell >= sea::CELLS) return false;
    if (_theirs.shot[cell] != sea::UNSHOT) return false;
    _fireSeq++;
    _pendingCell = (uint8_t)cell;
    _pending = true;
    _lastTry = now;
    sendFire();
    return true;
  }

  // Restarting is unilateral rather than a two-way agreement. A symmetric
  // "both must accept" handshake deadlocks the moment one confirmation is lost:
  // the side that advanced stops answering, and the side still waiting asks for
  // ever. Here whoever taps first simply restarts and tells the peer; the peer
  // restarts on hearing either the REMATCH or, if that was lost, the first
  // READY of the new game. Consent is still real -- the peer must place a fleet
  // and tap READY before anything is played.
  void requestRematch(uint32_t now) {
    if (_phase != Phase::Over) return;
    beginRematch();
    _lastTry = now - RETRY_MS;
    _link.sendTo(_peer, T_REMATCH, nullptr, 0);
  }

  // --- pump --------------------------------------------------------------
  // Called every loop. Drains the radio, then re-sends whatever is outstanding;
  // with one message in flight at a time that is all the reliability needed.
  void poll(uint32_t now) {
    if (!_link.up()) return;

    radio::Msg m;
    while (_link.poll(m)) handle(m, now);

    if (_phase == Phase::Hosting && now - _lastHello >= HELLO_MS) {
      _lastHello = now;
      _link.broadcast(T_HELLO, nullptr, 0);
    }
    if (_phase == Phase::Browsing) expireFound(now);
    if (now - _lastTry < RETRY_MS) return;
    _lastTry = now;

    switch (_phase) {
      case Phase::Joining:
        _link.sendTo(_peer, T_JOIN, nullptr, 0);
        break;
      case Phase::Setup:
        if (_myReady) sendReady(false);
        break;
      case Phase::Play:
        if (_pending) sendFire();
        break;
      case Phase::Over:
        break;
      default:
        break;
    }

    // A peer that has stopped answering is more useful reported than waited on.
    if (_peerKnown && (_phase == Phase::Setup || _phase == Phase::Play) &&
        now - _peerSeen > PEER_TIMEOUT_MS) {
      _phase = Phase::Lost;
    }
  }

  // --- state for the screen ----------------------------------------------
  Phase phase() const { return _phase; }
  bool isHost() const { return _iAmHost; }
  bool waitingForPeerReady() const { return _myReady && !_peerReady; }
  bool won() const { return _won; }
  bool peerRestarted() const { return _peerRestarted; }
  uint16_t myCode() const { return _link.code(); }
  const sea::Board& mine() const { return _mine; }
  sea::Board& mineMutable() { return _mine; }
  const sea::Board& theirs() const { return _theirs; }  // only shot[] is known
  int theirAfloat() const { return _theirAfloat; }
  int myAfloat() const { return sea::afloat(_mine); }
  const sea::Shot& lastIncoming() const { return _lastIncoming; }
  int lastIncomingCell() const { return _lastIncomingCell; }

 private:
  void sendReady(bool confirm) {
    const uint8_t p = confirm ? 1 : 0;
    _link.sendTo(_peer, T_READY, &p, 1);
  }

  void sendFire() {
    const uint8_t p[2] = {_fireSeq, _pendingCell};
    _link.sendTo(_peer, T_FIRE, p, 2);
  }

  void tryAdvance() {
    if (_phase == Phase::Setup && _myReady && _peerReady) {
      _phase = Phase::Play;
      _turn = _iAmHost;  // the host opens; someone has to
    }
  }

  void beginRematch() {
    sea::clear(_theirs);
    memset(_mine.shot, 0, sizeof(_mine.shot));
    _theirAfloat = sea::SHIPS;
    _myReady = _peerReady = false;
    _pending = false;
    _fireSeq = 0;
    _lastAnswered = 0xFF;
    _won = false;
    _phase = Phase::Setup;
  }

  void noteFound(const radio::Msg& m, uint32_t now) {
    for (int i = 0; i < _foundN; i++) {
      if (radio::sameMac(_found[i].mac, m.from)) {
        _found[i].seen = now;
        return;
      }
    }
    if (_foundN >= MAX_FOUND) return;
    memcpy(_found[_foundN].mac, m.from, radio::MAC_LEN);
    _found[_foundN].code = radio::codeOf(m.from);
    _found[_foundN].seen = now;
    _foundN++;
  }

  void expireFound(uint32_t now) {
    for (int i = 0; i < _foundN;) {
      if (now - _found[i].seen > PEER_TIMEOUT_MS) {
        for (int j = i + 1; j < _foundN; j++) _found[j - 1] = _found[j];
        _foundN--;
      } else {
        i++;
      }
    }
  }

  void handle(const radio::Msg& m, uint32_t now) {
    // Once paired, anything from a third device is not part of this game.
    const bool fromPeer = _peerKnown && radio::sameMac(m.from, _peer);
    if (_peerKnown && !fromPeer && m.type != T_HELLO) return;
    if (fromPeer) _peerSeen = now;

    switch (m.type) {
      case T_HELLO:
        if (_phase == Phase::Browsing) noteFound(m, now);
        break;

      case T_JOIN:
        // Accept the first caller; answer any repeat so a lost ACCEPT recovers.
        if (_phase == Phase::Hosting || (_phase == Phase::Setup && fromPeer)) {
          if (!_peerKnown) {
            memcpy(_peer, m.from, radio::MAC_LEN);
            _peerKnown = true;
            _peerSeen = now;
            _phase = Phase::Setup;
          }
          _link.sendTo(_peer, T_ACCEPT, nullptr, 0);
        }
        break;

      case T_ACCEPT:
        if (_phase == Phase::Joining && fromPeer) _phase = Phase::Setup;
        break;

      case T_READY: {
        // READY is tagged request-or-confirmation. Without that tag two devices
        // answer each other's answers for the rest of the session, and a stray
        // echo arriving after the last ship sinks reads as "they restarted".
        const bool confirm = m.len > 0 && m.data[0] != 0;
        if (_phase == Phase::Over && !confirm) {
          // The peer is asking to start again and its REMATCH never arrived.
          beginRematch();
          _peerRestarted = true;
        }
        if (_phase == Phase::Joining) _phase = Phase::Setup;  // our ACCEPT was lost
        if (_phase == Phase::Setup) {
          _peerReady = true;
          if (_myReady && !confirm) sendReady(true);
          tryAdvance();
        } else if (!confirm && (_phase == Phase::Play || _phase == Phase::Over)) {
          // We have already started; the peer is still asking because our own
          // READY never landed. Answer once so it can catch up.
          sendReady(true);
        }
        break;
      }

      case T_FIRE: {
        if (m.len < 2) break;
        // Answered in Over as well as Play: the shot that ended the game is the
        // one whose result is most likely to have been lost.
        if (_phase != Phase::Play && _phase != Phase::Over) break;
        const uint8_t seq = m.data[0], cell = m.data[1];
        sea::Shot r;
        if (seq == _lastAnswered) {
          r = _cached;  // a resend: answer from the log, never fire twice
        } else if (_phase != Phase::Play) {
          break;  // a new shot after the fleet is gone is not ours to answer
        } else {
          r = sea::fire(_mine, cell);
          _lastAnswered = seq;
          _cached = r;
          _lastIncoming = r;
          _lastIncomingCell = cell;
          _turn = true;  // their shot is spent; the board is ours now
          if (r.allSunk) {
            _won = false;
            _phase = Phase::Over;
          }
        }
        const uint8_t p[5] = {seq, cell, (uint8_t)(r.hit ? 1 : 0), (uint8_t)(r.sunk + 1),
                              (uint8_t)(r.allSunk ? 1 : 0)};
        _link.sendTo(_peer, T_RESULT, p, 5);
        break;
      }

      case T_RESULT: {
        if (m.len < 5 || !_pending) break;
        if (m.data[0] != _fireSeq) break;  // an answer to a shot already settled
        const uint8_t cell = m.data[1];
        _theirs.shot[cell] = m.data[2] ? sea::HIT : sea::MISS;
        if (m.data[3] > 0) _theirAfloat--;
        _pending = false;
        _turn = false;
        if (m.data[4]) {
          _won = true;
          _theirAfloat = 0;
          _phase = Phase::Over;
        }
        break;
      }

      case T_REMATCH:
        if (_phase == Phase::Over) {
          beginRematch();
          _peerRestarted = true;
        }
        break;

      case T_BYE:
        if (fromPeer && _phase != Phase::Idle) _phase = Phase::Lost;
        break;

      default:
        break;
    }
  }

  radio::Link _link;
  Phase _phase = Phase::Idle;

  uint8_t _peer[radio::MAC_LEN] = {};
  bool _peerKnown = false, _iAmHost = false;
  uint32_t _peerSeen = 0, _lastHello = 0, _lastTry = 0;

  Found _found[MAX_FOUND] = {};
  int _foundN = 0;

  bool _myReady = false, _peerReady = false, _turn = false;
  sea::Board _mine{}, _theirs{};
  int _theirAfloat = sea::SHIPS;

  uint8_t _fireSeq = 0, _pendingCell = 0, _lastAnswered = 0xFF;
  bool _pending = false;
  sea::Shot _cached{}, _lastIncoming{};
  int _lastIncomingCell = -1;

  bool _peerRestarted = false, _won = false;
};

}  // namespace seanet
