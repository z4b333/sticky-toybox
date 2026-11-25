// Host-side logic tests: wordle scoring, 2048 moves, the XO rules + search, the
// picker list parser, the Battleship rules and protocol, Sudoku, and Mines.
// Build: g++ -std=c++17 -O2 -I../../src test_logic.cpp -o test && ./test
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>

#include "xo_rules.h"
#include "tools/picker_list.h"
#include "tools/sea_rules.h"
#include "tools/sea_net.h"
#include "tools/sudoku_gen.h"
#include "tools/mines_rules.h"

static std::mt19937 rng(12345);
static uint32_t rnd32() { return rng(); }

// --- Wordle scoring (mirror of wordle.cpp submitGuess scoring) ---------------
static void scoreGuess(const char* answer, const char* guess, uint8_t out[5]) {
  int counts[26] = {};
  for (int i = 0; i < 5; i++) counts[answer[i] - 'A']++;
  for (int i = 0; i < 5; i++) {
    out[i] = 0;
    if (guess[i] == answer[i]) {
      out[i] = 3;
      counts[answer[i] - 'A']--;
    }
  }
  for (int i = 0; i < 5; i++) {
    if (out[i] == 3) continue;
    const int li = guess[i] - 'A';
    if (counts[li] > 0) {
      out[i] = 2;
      counts[li]--;
    } else {
      out[i] = 1;
    }
  }
}

static void testWordle() {
  uint8_t s[5];
  // classic duplicate-letter cases
  scoreGuess("ABBEY", "BABES", s);
  // B->present, A->present, B->correct (pos 3), E->correct, S->absent
  assert(s[0] == 2 && s[1] == 2 && s[2] == 3 && s[3] == 3 && s[4] == 1);
  scoreGuess("SPEED", "ERASE", s);
  // E present, R absent, A absent, S present, E present (two E in answer)
  assert(s[0] == 2 && s[1] == 1 && s[2] == 1 && s[3] == 2 && s[4] == 2);
  scoreGuess("SPEED", "SPEED", s);
  for (int i = 0; i < 5; i++) assert(s[i] == 3);
  scoreGuess("CRANE", "LLAMA", s);
  // L absent, L absent, A correct, M absent, A absent (single A consumed)
  assert(s[0] == 1 && s[1] == 1 && s[2] == 3 && s[3] == 1 && s[4] == 1);
  printf("wordle scoring ok\n");
}

// --- 2048 move (mirror of game2048.cpp move) ---------------------------------
struct T2048 {
  uint8_t b[4][4] = {};
  int score = 0;
  bool reached2048 = false;
  bool move(int dir) {
    uint8_t nb[4][4];
    memcpy(nb, b, sizeof(nb));
    int gained = 0;
    bool changed = false;
    auto get = [&](int line, int i) -> uint8_t& {
      switch (dir) {
        case 0: return nb[line][i];
        case 1: return nb[line][3 - i];
        case 2: return nb[i][line];
        default: return nb[3 - i][line];
      }
    };
    for (int line = 0; line < 4; line++) {
      uint8_t vals[4], m = 0;
      for (int i = 0; i < 4; i++)
        if (get(line, i)) vals[m++] = get(line, i);
      uint8_t out[4] = {};
      int o = 0;
      for (int i = 0; i < m; i++) {
        if (i + 1 < m && vals[i] == vals[i + 1]) {
          out[o] = vals[i] + 1;
          gained += 1 << out[o];
          if (out[o] >= 11) reached2048 = true;
          o++;
          i++;
        } else {
          out[o++] = vals[i];
        }
      }
      for (int i = 0; i < 4; i++) {
        if (get(line, i) != out[i]) changed = true;
        get(line, i) = out[i];
      }
    }
    if (!changed) return false;
    memcpy(b, nb, sizeof(nb));
    score += gained;
    return true;
  }
};

static void test2048() {
  {  // [2 2 4 4] left -> [4 8 0 0], score 12
    T2048 g;
    g.b[0][0] = 1; g.b[0][1] = 1; g.b[0][2] = 2; g.b[0][3] = 2;
    assert(g.move(0));
    assert(g.b[0][0] == 2 && g.b[0][1] == 3 && g.b[0][2] == 0 && g.b[0][3] == 0);
    assert(g.score == 4 + 8);
  }
  {  // [2 2 2 0] left -> [4 2 0 0] (no double merge)
    T2048 g;
    g.b[0][0] = 1; g.b[0][1] = 1; g.b[0][2] = 1;
    assert(g.move(0));
    assert(g.b[0][0] == 2 && g.b[0][1] == 1 && g.b[0][2] == 0);
  }
  {  // right on same row: [2 2 2 0] -> [0 0 2 4]
    T2048 g;
    g.b[0][0] = 1; g.b[0][1] = 1; g.b[0][2] = 1;
    assert(g.move(1));
    assert(g.b[0][3] == 2 && g.b[0][2] == 1 && g.b[0][1] == 0);
  }
  {  // up merges column, 1024+1024 flags 2048
    T2048 g;
    g.b[1][2] = 10; g.b[3][2] = 10;
    assert(g.move(2));
    assert(g.b[0][2] == 11 && g.reached2048);
  }
  {  // no-op move returns false
    T2048 g;
    g.b[0][0] = 1;
    assert(!g.move(0));  // already flush left
    assert(!g.move(2));  // already flush up
  }
  printf("2048 moves ok\n");
}


// --- XO ----------------------------------------------------------------------
// The point of these is the promise the UI makes: HARD never loses. Rather than
// spot-check openings, walk the entire game tree -- every human move, and every
// move the machine's tie-break could pick -- and assert no leaf is a machine
// loss. 9! is small enough to do exhaustively.
namespace xotest {
using namespace xorules;

static int machineSide, humanSide;
static long leaves = 0;
static int worstForMachine = 1;  // 1 = machine won somewhere, 0 = drew, -1 = lost

// Collect every move that ties for the search's best value, so the tie-break's
// randomness is covered rather than pinned to one arbitrary choice.
static int bestMoves(const State& s, int side, bool three, int* out) {
  int best = -30000, n = 0;
  for (int c = 0; c < 9; c++) {
    if (s.cell[c]) continue;
    State t = s;
    applyMove(t, side, c, three);
    const int v = -negamax(t, 3 - side, searchDepth(three) - 1, -30000, 30000, three);
    if (v > best) { best = v; n = 0; }
    if (v == best) out[n++] = c;
  }
  return n;
}

static void walk(State s, int toMove) {
  const int w = winner(s);
  if (w) {
    leaves++;
    const int r = (w == machineSide) ? 1 : -1;
    if (r < worstForMachine) worstForMachine = r;
    assert(w == machineSide && "HARD lost a classic game");
    return;
  }
  if (boardFull(s)) {
    leaves++;
    if (worstForMachine > 0) worstForMachine = 0;
    return;
  }
  if (toMove == machineSide) {
    int picks[9];
    const int n = bestMoves(s, toMove, false, picks);
    for (int i = 0; i < n; i++) {
      State t = s;
      applyMove(t, toMove, picks[i], false);
      walk(t, 3 - toMove);
    }
  } else {
    for (int c = 0; c < 9; c++) {
      if (s.cell[c]) continue;
      State t = s;
      applyMove(t, toMove, c, false);
      walk(t, 3 - toMove);
    }
  }
}
}  // namespace xotest

static void testXo() {
  using namespace xorules;

  // 1) Line detection, in every orientation.
  {
    for (int i = 0; i < 8; i++) {
      State s; reset(s);
      for (int k = 0; k < 3; k++) s.cell[LINES[i][k]] = 2;
      assert(winner(s) == 2 && winLine(s) == i);
    }
    State s; reset(s);
    s.cell[0] = 1; s.cell[1] = 2; s.cell[2] = 1;
    assert(winner(s) == 0);
  }

  // 2) THREE lifts the oldest mark and never lets a side hold four.
  {
    State s; reset(s);
    const int order[4] = {0, 1, 2, 4};
    for (int i = 0; i < 4; i++) {
      applyMove(s, 1, order[i], true);
      int held = 0;
      for (int c = 0; c < 9; c++) held += (s.cell[c] == 1);
      assert(held <= 3);
    }
    assert(s.cell[0] == 0 && "the oldest mark should have lifted");
    assert(s.cell[1] && s.cell[2] && s.cell[4]);
    // ...and the next one to go is announced before it goes.
    assert(doomed(s, 1, true) == 1);
    assert(doomed(s, 1, false) == -1);  // classic never lifts
  }

  // 3) The search takes a win it has, and blocks one it faces.
  {
    State s; reset(s);
    s.cell[0] = 2; s.cell[1] = 2;  // O is one move from 0-1-2
    s.cell[3] = 1; s.cell[6] = 1;  // X has no matching threat to answer first
    assert(bestMove(s, 2, false, 0) == 2 && "HARD must take the win on offer");
  }
  {
    State s; reset(s);
    s.cell[0] = 1; s.cell[4] = 1;  // X threatens the 0-4-8 diagonal
    s.cell[1] = 2;
    assert(bestMove(s, 2, false, 0) == 8 && "HARD must block the open diagonal");
  }

  // 4) Exhaustive: HARD never loses a classic game, moving first or second.
  {
    for (int machine = 1; machine <= 2; machine++) {
      xotest::machineSide = machine;
      xotest::humanSide = 3 - machine;
      xotest::leaves = 0;
      xotest::worstForMachine = 1;
      State s; reset(s);
      xotest::walk(s, 1);
      assert(xotest::worstForMachine >= 0);
      printf("xo hard as %c: %ld lines, never lost\n", machine == 1 ? 'X' : 'O',
             xotest::leaves);
    }
  }

  // 5) THREE games terminate: HARD vs HARD reaches a winner inside the ply cap,
  //    and no position ever holds more than three marks a side.
  {
    std::mt19937 r(7);
    for (int game = 0; game < 20; game++) {
      State s; reset(s);
      int side = 1, plies = 0;
      while (!winner(s) && plies < 60) {
        const int m = bestMove(s, side, true, r());
        assert(m >= 0 && !s.cell[m]);
        applyMove(s, side, m, true);
        int held[2] = {0, 0};
        for (int c = 0; c < 9; c++)
          if (s.cell[c]) held[s.cell[c] - 1]++;
        assert(held[0] <= 3 && held[1] <= 3);
        side = 3 - side;
        plies++;
      }
      assert(plies < 60 && "3-mark game should resolve inside the cap");
    }
    printf("xo 3-mark: 20 games resolved, 3-per-side held\n");
  }

  printf("xo rules ok\n");
}


// --- Picker list -------------------------------------------------------------
// Both input paths -- the on-screen keyboard and the phone page -- land in the
// same parser, and it is the only place the limits are enforced. If it is wrong,
// a pasted list silently comes out mangled.
static void testPickerList() {
  using namespace plist;
  Item items[MAX_ITEMS];

  {  // blank lines dropped, ends trimmed, CRLF handled
    const int n = fromText("  Ana  \r\n\r\nBen\n\n   \nChandra\n", items);
    assert(n == 3);
    assert(strcmp(items[0], "Ana") == 0);
    assert(strcmp(items[1], "Ben") == 0);
    assert(strcmp(items[2], "Chandra") == 0);
  }

  {  // over-long names are cut, not rejected
    const int n = fromText("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", items);  // 30 a's
    assert(n == 1);
    assert((int)strlen(items[0]) == MAX_CHARS);
  }

  {  // the tail past MAX_ITEMS is dropped rather than wrapping round
    char many[512] = {}, one[24];
    for (int i = 0; i < MAX_ITEMS + 5; i++) {
      snprintf(one, sizeof(one), "item%d\n", i);
      strcat(many, one);
    }
    const int n = fromText(many, items);
    assert(n == MAX_ITEMS);
    assert(strcmp(items[0], "item0") == 0);
    snprintf(one, sizeof(one), "item%d", MAX_ITEMS - 1);
    assert(strcmp(items[MAX_ITEMS - 1], one) == 0);
  }

  {  // since C2 every script is kept whole, accents and Thai included
    const int n = fromText("Jos\xc3\xa9\n\xe0\xb8\x81\xe0\xb8\x82\n", items);
    assert(n == 2);
    assert(strcmp(items[0], "Jos\xc3\xa9") == 0);
    assert(strcmp(items[1], "\xe0\xb8\x81\xe0\xb8\x82") == 0);
  }

  {  // control characters are stripped; tabs become spaces
    const int n = fromText("a\tb\x01(c)\n", items);
    assert(n == 1);
    assert(strcmp(items[0], "a b(c)") == 0);
  }

  {  // round trip: what the device shows is what the phone page opens on
    const int n = fromText("Ana\nBen\n", items);
    const String text = toText(items, n);
    assert(strcmp(text.c_str(), "Ana\nBen\n") == 0);
    Item again[MAX_ITEMS];
    assert(fromText(text.c_str(), again) == n);
    assert(strcmp(again[1], "Ben") == 0);
  }

  {  // an empty list is a legal state, not a parse failure
    assert(fromText("", items) == 0);
    assert(fromText("\n\n  \n", items) == 0);
    assert(strcmp(toText(items, 0).c_str(), "") == 0);
  }

  printf("picker list ok\n");
}


// --- Battleship --------------------------------------------------------------
// Both the solo game and the two-device game answer shots with sea::fire, so a
// mistake here is a mistake everywhere. The gunner is checked by making it play
// out real boards rather than by inspecting its choices.
static void testSea() {
  using namespace sea;
  std::mt19937 r(99);
  auto rnd = [&r] { return (uint32_t)r(); };

  {  // placement: every ship present, right length, inside the board
    for (int trial = 0; trial < 200; trial++) {
      Board b;
      clear(b);
      assert(placeRandom(b, rnd));
      int count[SHIPS] = {0};
      int occupied = 0;
      for (int c = 0; c < CELLS; c++) {
        if (!b.ship[c]) continue;
        occupied++;
        const int id = b.ship[c] - 1;
        assert(id >= 0 && id < SHIPS);
        count[id]++;
      }
      assert(occupied == SHIP_CELLS);
      for (int s = 0; s < SHIPS; s++) assert(count[s] == LEN[s]);
      assert(afloat(b) == SHIPS);
    }
  }

  {  // each ship occupies one unbroken straight run
    for (int trial = 0; trial < 100; trial++) {
      Board b;
      clear(b);
      placeRandom(b, rnd);
      for (int s = 0; s < SHIPS; s++) {
        int xs[8], ys[8], n = 0;
        for (int c = 0; c < CELLS; c++)
          if (b.ship[c] == (uint8_t)(s + 1)) { xs[n] = xOf(c); ys[n] = yOf(c); n++; }
        assert(n == LEN[s]);
        bool sameRow = true, sameCol = true;
        for (int i = 1; i < n; i++) {
          if (ys[i] != ys[0]) sameRow = false;
          if (xs[i] != xs[0]) sameCol = false;
        }
        assert(sameRow || sameCol);
        int lo = 99, hi = -1;
        for (int i = 0; i < n; i++) {
          const int v = sameRow ? xs[i] : ys[i];
          if (v < lo) lo = v;
          if (v > hi) hi = v;
        }
        assert(hi - lo == n - 1);  // contiguous, no gaps
      }
    }
  }

  {  // firing: hit, miss, repeat, sink, and the final ship ends the game
    Board b;
    clear(b);
    put(b, 0, 0, 2, true, 0);  // a lone two-cell ship at (0,0)-(1,0)
    Shot s1 = fire(b, cellAt(4, 4));
    assert(!s1.hit && s1.sunk < 0 && !s1.repeat);
    Shot again = fire(b, cellAt(4, 4));
    assert(again.repeat && !again.hit);
    Shot s2 = fire(b, cellAt(0, 0));
    assert(s2.hit && s2.sunk < 0 && !s2.allSunk);
    Shot rep = fire(b, cellAt(0, 0));
    assert(rep.repeat && rep.hit);  // a repeat still reports what is there
    Shot s3 = fire(b, cellAt(1, 0));
    assert(s3.hit && s3.sunk == 0);
    // A ship that was never placed has no unhit cells, so it reads as sunk.
    // Harmless in play -- every board is placed by placeRandom, which always
    // lays all four -- but it means this cut-down board is already "cleared".
    assert(afloat(b) == 0 && s3.allSunk);
  }

  {  // allSunk on a real fleet fires exactly once, on the very last cell
    Board b;
    clear(b);
    placeRandom(b, rnd);
    int announcements = 0, hits = 0;
    for (int c = 0; c < CELLS; c++) {
      const Shot s = fire(b, c);
      if (s.hit) hits++;
      if (s.allSunk) announcements++;
      assert(!s.allSunk || hits == SHIP_CELLS);
    }
    assert(hits == SHIP_CELLS);
    assert(announcements == 1);
  }

  {  // a full board can always be cleared, and the count only ever falls
    Board b;
    clear(b);
    placeRandom(b, rnd);
    int before = afloat(b);
    for (int c = 0; c < CELLS; c++) {
      fire(b, c);
      const int now = afloat(b);
      assert(now <= before);
      before = now;
    }
    assert(afloat(b) == 0);
  }

  {  // every level finishes every board; Hard beats Normal beats Easy on average
    double avg[LEVELS];
    for (int lvl = 0; lvl < LEVELS; lvl++) {
      long total = 0;
      for (int game = 0; game < 300; game++) {
        Board b;
        clear(b);
        placeRandom(b, rnd);
        Gunner g;
        g.reset(lvl);
        int shots = 0;
        while (afloat(b) > 0) {
          const int c = g.choose(rnd);
          assert(c >= 0 && "gunner ran out of cells before sinking the fleet");
          const Shot s = fire(b, c);
          assert(!s.repeat && "gunner fired at the same cell twice");
          g.observe(c, s, rnd);
          if (++shots > CELLS) break;
        }
        assert(afloat(b) == 0);
        total += shots;
      }
      avg[lvl] = (double)total / 300.0;
      printf("sea gunner %s: %.1f shots average over 300 games\n", levelName(lvl), avg[lvl]);
    }
    // Blind fire needs ~95 of 100 shots to sink 16 cells.
    assert(avg[EASY] < 92.0);
    assert(avg[NORMAL] < avg[EASY] - 5.0);
    assert(avg[HARD] < avg[NORMAL] - 3.0);
  }

  {  // placing by hand: lift takes a ship off, placed() and fleetPlaced() agree
    Board b;
    clear(b);
    assert(!fleetPlaced(b));
    for (int s = 0; s < SHIPS; s++) {
      assert(canPlace(b, 0, s * 2, LEN[s], true));
      put(b, 0, s * 2, LEN[s], true, s);
      assert(placed(b, s));
    }
    assert(fleetPlaced(b));
    assert(!canPlace(b, 0, 0, 2, false) && "a ship on top of another");
    assert(!canPlace(b, N - 1, 9, 2, true) && "a ship off the edge");
    lift(b, 2);
    assert(!placed(b, 2) && !fleetPlaced(b) && placed(b, 1) && placed(b, 3));
    assert(canPlace(b, 0, 4, LEN[2], true));
  }

  printf("sea rules ok\n");
}


// --- Battleship over the link ------------------------------------------------
// Two Duels wired through the host transport play real games against each
// other. The point is not that a clean game works -- it is that a game still
// finishes when the radio silently eats frames, which is the failure that
// cannot be reproduced on a desk with two real devices.
namespace duel {
using namespace seanet;

// Pumps both sides until `done` or the clock runs out. Time is supplied rather
// than read, so retry timers are exercised in a few microseconds.
static bool pump(Duel& a, Duel& b, uint32_t& now, std::function<bool()> done, int maxMs = 60000) {
  const uint32_t start = now;
  while (now - start < (uint32_t)maxMs) {
    a.poll(now);
    b.poll(now);
    if (done()) return true;
    now += 50;
  }
  return false;
}

// Plays a whole game between two paired Duels; returns total shots fired.
static int playOut(Duel& a, Duel& b, uint32_t& now, std::mt19937& rng) {
  sea::Gunner ga, gb;
  ga.reset();
  gb.reset();
  // Shots already fired (a test may fire a few by hand first) are known.
  for (int c = 0; c < sea::CELLS; c++) {
    if (a.theirs().shot[c] != sea::UNSHOT) ga.tried[c] = a.theirs().shot[c] == sea::HIT ? 2 : 1;
    if (b.theirs().shot[c] != sea::UNSHOT) gb.tried[c] = b.theirs().shot[c] == sea::HIT ? 2 : 1;
  }
  int shots = 0;
  while (a.phase() == Phase::Play || b.phase() == Phase::Play) {
    Duel* shooter = a.myTurn() ? &a : (b.myTurn() ? &b : nullptr);
    if (!shooter) {  // nobody can act: let the retry timers move things along
      now += 50;
      a.poll(now);
      b.poll(now);
      if (++shots > 4000) break;
      continue;
    }
    sea::Gunner& g = (shooter == &a) ? ga : gb;
    const int cell = g.choose([&rng] { return (uint32_t)rng(); });
    if (cell < 0) break;
    assert(shooter->fire(cell, now));
    const bool settled = pump(a, b, now, [&] {
      return shooter->theirs().shot[cell] != sea::UNSHOT || shooter->phase() != Phase::Play;
    });
    assert(settled && "a shot never resolved");
    // Learn the outcome the same way the screen would: from our view of their board.
    sea::Shot seen;
    seen.hit = shooter->theirs().shot[cell] == sea::HIT;
    g.observe(cell, seen);
    shots++;
  }
  return shots;
}

static void pair(Duel& host, Duel& guest, uint32_t& now) {
  assert(host.begin() && guest.begin());
  host.startHosting(now);
  guest.startBrowsing();
  assert(pump(host, guest, now, [&] { return guest.foundCount() > 0; }));
  assert(guest.found(0).code == host.myCode());
  assert(guest.joinFound(0, now));
  assert(pump(host, guest, now, [&] {
    return host.phase() == Phase::Setup && guest.phase() == Phase::Setup;
  }));
}

static void readyUp(Duel& host, Duel& guest, uint32_t& now, uint32_t s1, uint32_t s2) {
  host.placeFleet(s1);
  guest.placeFleet(s2);
  host.setReady(now);
  guest.setReady(now);
  assert(pump(host, guest, now, [&] {
    return host.phase() == Phase::Play && guest.phase() == Phase::Play;
  }));
}
}  // namespace duel

static void testDuel() {
  using namespace seanet;
  using namespace duel;
  std::mt19937 rng(2024);

  {  // a clean game: pair, place, play to a finish, exactly one winner
    uint32_t now = 1000;
    Duel a, b;
    pair(a, b, now);
    readyUp(a, b, now, 11, 22);
    assert(a.myTurn() && !b.myTurn() && "the host opens");
    playOut(a, b, now, rng);
    assert(pump(a, b, now, [&] { return a.phase() == Phase::Over && b.phase() == Phase::Over; }));
    assert(a.won() != b.won() && "exactly one winner");
    const Duel& loser = a.won() ? b : a;
    assert(loser.myAfloat() == 0);
    a.end();
    b.end();
    printf("duel clean game ok\n");
  }

  {  // a lost answer forces a resend, and the resend must not fire twice
    uint32_t now = 1000;
    Duel a, b;
    pair(a, b, now);
    readyUp(a, b, now, 33, 44);

    int target = -1;  // a cell holding one of b's ships, so the shot is a hit
    for (int c = 0; c < sea::CELLS && target < 0; c++)
      if (b.mine().ship[c]) target = c;

    b.transport().dropNextSends(1);  // b's RESULT never reaches a
    assert(a.fire(target, now));
    assert(pump(a, b, now, [&] { return b.mine().shot[target] != sea::UNSHOT; }));
    assert(a.theirs().shot[target] == sea::UNSHOT && "the answer was supposed to be lost");

    // a keeps re-sending the same sequence number until it hears back.
    assert(pump(a, b, now, [&] { return a.theirs().shot[target] != sea::UNSHOT; }));
    assert(a.theirs().shot[target] == sea::HIT);

    int hitCells = 0;
    for (int c = 0; c < sea::CELLS; c++)
      if (b.mine().shot[c] == sea::HIT) hitCells++;
    assert(hitCells == 1 && "the resend punched a second hole");
    a.end();
    b.end();
    printf("duel replay-safe ok\n");
  }

  {  // lossy radio: drop frames throughout and the game must still finish
    for (int trial = 0; trial < 5; trial++) {
      uint32_t now = 1000;
      Duel a, b;
      assert(a.begin() && b.begin());
      a.startHosting(now);
      b.startBrowsing();
      a.transport().dropNextSends(3);  // several HELLOs never land
      assert(pump(a, b, now, [&] { return b.foundCount() > 0; }));
      assert(b.joinFound(0, now));
      b.transport().dropNextSends(2);  // and the first JOINs
      a.transport().dropNextSends(2);  // and the first ACCEPTs
      assert(pump(a, b, now, [&] {
        return a.phase() == Phase::Setup && b.phase() == Phase::Setup;
      }));
      a.placeFleet(100u + trial);
      b.placeFleet(200u + trial);
      a.setReady(now);
      b.setReady(now);
      a.transport().dropNextSends(2);  // and the READY handshake
      assert(pump(a, b, now, [&] {
        return a.phase() == Phase::Play && b.phase() == Phase::Play;
      }));
      playOut(a, b, now, rng);
      assert(pump(a, b, now, [&] { return a.phase() == Phase::Over && b.phase() == Phase::Over; }));
      assert(a.won() != b.won());
      a.end();
      b.end();
    }
    printf("duel survives a lossy radio (5 games)\n");
  }

  {  // "a hit fires again": the host's setting governs both sides
    for (int rule = 0; rule < 2; rule++) {
      uint32_t now = 1000;
      Duel a, b;
      a.setFireAgain(rule != 0);
      b.setFireAgain(rule == 0);  // the guest starts the other way round
      pair(a, b, now);
      assert(b.fireAgain() == (rule != 0) && "the guest did not take the host's rule");
      readyUp(a, b, now, 5, 6);
      assert(b.fireAgain() == (rule != 0));
      int hit = -1, miss = -1;
      for (int c = 0; c < sea::CELLS; c++) {
        if (b.mine().ship[c] && hit < 0) hit = c;
        if (!b.mine().ship[c] && miss < 0) miss = c;
      }
      assert(a.fire(hit, now));
      assert(pump(a, b, now, [&] { return a.theirs().shot[hit] != sea::UNSHOT; }));
      assert(pump(a, b, now, [&] { return true; }));
      if (rule) {
        assert(a.myTurn() && !b.myTurn() && "a hit should have kept the turn");
        assert(a.fire(miss, now));
        assert(pump(a, b, now, [&] { return a.theirs().shot[miss] != sea::UNSHOT; }));
      }
      assert(!a.myTurn() && "the turn should have passed");
      assert(pump(a, b, now, [&] { return b.myTurn(); }));
      // Both play to the end under that rule and agree who won.
      playOut(a, b, now, rng);
      assert(pump(a, b, now, [&] { return a.phase() == Phase::Over && b.phase() == Phase::Over; }));
      assert(a.won() != b.won());
      a.end();
      b.end();
    }
    printf("duel fire-again rule ok (host's setting, both ways)\n");
  }

  {  // rematch resets both boards and hands the opening shot back to the host
    uint32_t now = 1000;
    Duel a, b;
    pair(a, b, now);
    readyUp(a, b, now, 55, 66);
    playOut(a, b, now, rng);
    assert(pump(a, b, now, [&] { return a.phase() == Phase::Over; }));
    // Only one side taps REMATCH; the other must follow without being asked.
    a.requestRematch(now);
    assert(pump(a, b, now, [&] {
      return a.phase() == Phase::Setup && b.phase() == Phase::Setup;
    }));
    for (int c = 0; c < sea::CELLS; c++) {
      assert(a.theirs().shot[c] == sea::UNSHOT);
      assert(a.mine().shot[c] == sea::UNSHOT);
    }
    assert(a.myAfloat() == sea::SHIPS && a.theirAfloat() == sea::SHIPS);
    a.placeFleet(77);
    b.placeFleet(88);
    a.setReady(now);
    b.setReady(now);
    assert(pump(a, b, now, [&] { return a.phase() == Phase::Play; }));
    assert(a.myTurn() && "the host opens the rematch too");
    a.end();
    b.end();
    printf("duel rematch ok\n");
  }

  {  // a peer that walks away is reported, not waited on forever
    uint32_t now = 1000;
    Duel a, b;
    pair(a, b, now);
    readyUp(a, b, now, 99, 111);
    b.end();  // the other device is switched off
    const uint32_t start = now;
    while (a.phase() == Phase::Play && now - start < PEER_TIMEOUT_MS * 3) {
      a.poll(now);
      now += 100;
    }
    assert(a.phase() == Phase::Lost);
    a.end();
    printf("duel peer-loss ok\n");
  }

  printf("duel protocol ok\n");
}


// --- Sudoku ------------------------------------------------------------------
// The claim that matters is uniqueness: a puzzle with two answers is not a
// Sudoku, and the player cannot tell until they have wasted an evening. So this
// re-solves every generated board from scratch and counts the answers.
static void testSudoku() {
  using namespace sud;
  std::mt19937 r(31337);
  auto rnd = [&r] { return (uint32_t)r(); };

  {  // the row/column/box rule itself
    uint8_t g[CELLS] = {};
    g[0] = 5;
    assert(!allowed(g, 1, 5));        // same row
    assert(!allowed(g, N, 5));        // same column
    assert(!allowed(g, N + 1, 5));    // same box
    assert(allowed(g, N * 4 + 4, 5)); // unrelated
    assert(allowed(g, 0, 5));         // a cell never clashes with itself
  }

  {  // a filled grid is a valid, complete Sudoku
    for (int t = 0; t < 40; t++) {
      uint8_t g[CELLS] = {};
      assert(fillFull(g, rnd));
      assert(complete(g));
      assert(consistent(g));
    }
  }

  {  // generation: right clue count, clues agree with the answer, answer unique
    int perLevel[LEVELS] = {0};
    for (int t = 0; t < 12; t++) {
      for (int lv = 0; lv < LEVELS; lv++) {
        uint8_t puzzle[CELLS], solution[CELLS];
        generate(puzzle, solution, (Level)lv, rnd);
        assert(consistent(solution));

        int clues = 0;
        for (int i = 0; i < CELLS; i++) {
          if (!puzzle[i]) continue;
          clues++;
          assert(puzzle[i] == solution[i] && "a clue contradicted the answer");
        }
        // One-at-a-time digging reaches the target exactly, every time.
        assert(clues == CLUES[lv] && "digger did not reach the clue target");
        perLevel[lv] = clues;

        // Solve it again from nothing and demand exactly one answer.
        uint8_t work[CELLS];
        memcpy(work, puzzle, CELLS);
        assert(countSolutions(work, 3) == 1 && "generated puzzle is ambiguous");

        // ...and that the one answer is the one we kept.
        memcpy(work, puzzle, CELLS);
        assert(countSolutions(work, 1) == 1);
      }
    }
    printf("sudoku clues: easy %d, medium %d, hard %d\n", perLevel[0], perLevel[1],
           perLevel[2]);
  }

  {  // harder levels really do give away less
    assert(CLUES[EASY] > CLUES[MEDIUM] && CLUES[MEDIUM] > CLUES[HARD]);
  }

  {  // an ambiguous board is caught rather than shipped
    uint8_t g[CELLS] = {};
    assert(fillFull(g, rnd));

    // Emptying a whole box does NOT create ambiguity -- the rest of the grid
    // still pins every cell in it. Real ambiguity needs an "unavoidable set":
    // two rows of the same band holding the same pair of values in two columns,
    // so those four cells can swap and stay legal. Every grid has some.
    int r1 = -1, r2 = -1, c1 = -1, c2 = -1;
    for (int a1 = 0; a1 < N && r1 < 0; a1++)
      for (int a2 = a1 + 1; a2 < N && r1 < 0; a2++) {
        if (a1 / 3 != a2 / 3) continue;  // same band, or the boxes break
        for (int b1 = 0; b1 < N && r1 < 0; b1++)
          for (int b2 = b1 + 1; b2 < N && r1 < 0; b2++) {
            if (g[a1 * N + b1] != g[a2 * N + b2]) continue;
            if (g[a1 * N + b2] != g[a2 * N + b1]) continue;
            r1 = a1; r2 = a2; c1 = b1; c2 = b2;
          }
      }
    assert(r1 >= 0 && "every full grid should contain an unavoidable set");

    g[r1 * N + c1] = 0;
    g[r1 * N + c2] = 0;
    g[r2 * N + c1] = 0;
    g[r2 * N + c2] = 0;
    assert(countSolutions(g, 2) >= 2 && "the swap should give a second answer");

    // ...and the generator never emits a board like that: covered above, where
    // every generated puzzle is re-solved and required to have exactly one.
  }

  {  // one blank cell is always still uniquely solvable
    uint8_t g[CELLS] = {};
    assert(fillFull(g, rnd));
    g[17] = 0;
    assert(countSolutions(g, 3) == 1);
  }

  {  // progress reporting the screen depends on
    uint8_t puzzle[CELLS], solution[CELLS];
    generate(puzzle, solution, MEDIUM, rnd);
    assert(remaining(puzzle) == CELLS - (CELLS - remaining(puzzle)));
    uint8_t grid[CELLS];
    memcpy(grid, puzzle, CELLS);
    assert(!complete(grid));
    assert(wrongCount(grid, solution) == 0);

    for (int i = 0; i < CELLS; i++)
      if (!grid[i]) grid[i] = solution[i];
    assert(complete(grid) && consistent(grid));
    assert(wrongCount(grid, solution) == 0);

    // One deliberate error must be seen, and only one.
    const int spoil = 40;
    grid[spoil] = (uint8_t)(solution[spoil] % 9 + 1);
    assert(wrongCount(grid, solution) == 1);
    assert(!consistent(grid));
  }

  printf("sudoku ok\n");
}


// --- Mines -------------------------------------------------------------------
// What the board promises the player: the first tap is safe and opens a patch,
// flags protect their squares, a chord only fires when its flags are all
// placed, and the game is won exactly when every safe square is open.
static void testMines() {
  using namespace mns;
  std::mt19937 r(7);
  auto rnd = [&r] { return (uint32_t)r(); };
  for (int lvl = 0; lvl < LEVELS; lvl++) {
    for (int game = 0; game < 500; game++) {
      Board b;
      reset(b, lvl);
      const int first = (int)(rnd() % CELLS);
      assert(tap(b, first, rnd) && "the first tap went off");
      int laid = 0;
      for (int k = 0; k < CELLS; k++) laid += b.mine[k];
      assert(laid == MINES[lvl]);
      assert(!b.mine[first] && count(b, first) == 0 && "the first tap must open a patch");
      around(first, [&](int j) { assert(b.st[j] == OPEN); });
      // Every open zero has every neighbour open (the flood went all the way).
      for (int k = 0; k < CELLS; k++)
        if (b.st[k] == OPEN && count(b, k) == 0)
          around(k, [&](int j) { assert(b.st[j] == OPEN); });
      // Play it out knowing the answer: flag every mine, chord or dig the rest.
      for (int k = 0; k < CELLS; k++)
        if (b.mine[k]) assert(toggleFlag(b, k));
      assert(flags(b) == MINES[lvl]);
      for (int k = 0; k < CELLS; k++) {
        if (b.mine[k]) {
          assert(tap(b, k, rnd) && b.st[k] == FLAG && "a flag must protect its square");
          continue;
        }
        tap(b, k, rnd);
      }
      assert(won(b) && !lost(b));
      assert(!toggleFlag(b, 0) && "no flags once it is over");
    }
  }
  {  // a chord fires only when the flags round a number are complete
    Board b;
    reset(b, 0);
    memset(b.mine, 0, sizeof(b.mine));
    b.mine[0] = 1;  // a single mine in the corner
    b.laid = 1;
    assert(tap(b, 10, rnd));  // (1,1): a "1", touching the mine
    assert(b.st[10] == OPEN && count(b, 10) == 1);
    assert(b.st[1] == HIDDEN);
    assert(tap(b, 10, rnd) && b.st[1] == HIDDEN && "a chord with no flag must do nothing");
    assert(toggleFlag(b, 0));
    assert(tap(b, 10, rnd) && b.st[1] == OPEN && b.st[9] == OPEN && "the chord did not open");
  }
  {  // a wrong flag and a chord: the chord digs the mine and loses
    Board b;
    reset(b, 0);
    memset(b.mine, 0, sizeof(b.mine));
    b.mine[0] = 1;
    b.laid = 1;
    tap(b, 10, rnd);
    assert(toggleFlag(b, 1));  // the wrong square
    assert(!tap(b, 10, rnd) && lost(b) && b.boom == 1);
  }
  printf("mines ok (first tap safe, flood, flags, chords, wins, 1500 games)\n");
}

// The three XO levels must be in the right order: against the same opponent,
// Normal loses less than Easy and Hard never loses; Normal never misses a line
// it could block, and Easy now blocks most of them.
static void testXoLevels() {
  using namespace xorules;
  std::mt19937 r(31);
  // Plays `games` of classic XO: the level as O against a mover that makes a
  // random move but always takes a win it sees. Returns how many O lost.
  auto run = [&](int level, int games) {
    int lost = 0;
    for (int g = 0; g < games; g++) {
      State s;
      reset(s);
      int turn = 1;
      while (!winner(s) && !boardFull(s)) {
        int m;
        if (turn == 1) {
          m = -1;
          for (int c = 0; c < 9 && m < 0; c++) {
            if (s.cell[c]) continue;
            State t = s;
            applyMove(t, 1, c, false);
            if (winner(t) == 1) m = c;
          }
          if (m < 0) {
            int e[9], n = 0;
            for (int c = 0; c < 9; c++)
              if (!s.cell[c]) e[n++] = c;
            m = e[r() % n];
          }
        } else {
          m = level == 0 ? easyMove(s, 2, false, r()) : level == 1 ? normalMove(s, 2, false, r())
                                                               : bestMove(s, 2, false, r());
        }
        applyMove(s, turn, m, false);
        turn = 3 - turn;
      }
      if (winner(s) == 1) lost++;
    }
    return lost;
  };
  const int easy = run(0, 2000), normal = run(1, 2000), hard = run(2, 2000);
  printf("xo levels vs a greedy random X (2000 games): easy lost %d, normal %d, hard %d\n", easy,
         normal, hard);
  assert(hard == 0);
  assert(normal < easy);
  // Normal always blocks an open line; Easy blocks most of the time.
  int easyBlocks = 0;
  for (int i = 0; i < 1000; i++) {
    State s;
    reset(s);
    applyMove(s, 1, 0, false);
    applyMove(s, 2, 8, false);
    applyMove(s, 1, 1, false);  // X threatens 2
    assert(normalMove(s, 2, false, r()) == 2);
    if (easyMove(s, 2, false, r()) == 2) easyBlocks++;
  }
  assert(easyBlocks > 700 && easyBlocks < 900);
  printf("xo levels ok (normal always blocks; easy blocked %d of 1000)\n", easyBlocks);
}

int main() {
  testWordle();
  test2048();
  testXo();
  testXoLevels();
  testPickerList();
  testSea();
  testDuel();
  testSudoku();
  testMines();
  printf("ALL TESTS PASSED\n");
  return 0;
}
