// Recipes: what the internet publishes, read at arm's length in a kitchen.
//
// Two shelves feed one list. The SD card carries .json files in /recipes --
// the schema.org Recipe JSON-LD nearly every recipe website embeds, saved as
// a file on a PC -- and the device's own flash keeps up to a dozen recipes a
// phone pasted in over the portal (recipe_web.h). Both stores hold the same
// standard format and go through the same parser (recipe_data.h), so a file
// copied off the device is a valid recipe anywhere else.
//
// Reading is built for cooking, not browsing: the ingredients page carries
// tick boxes for the gathering, and COOK shows one instruction per page in
// large type -- a glance from the chopping board, a tap with a knuckle.
#pragma once
#include "decor.h"
#include "flash_qr.h"
#include "help.h"
#include "recipe_data.h"
#include "recipe_store.h"
#include "reader_menu.h"
#include "recipe_web.h"
#include "tools_ui.h"

namespace rcpui {
// the list
inline constexpr TRect PHONE_BTN{20, 64, 440, 56};
inline constexpr int LIST_Y0 = 148, LIST_ROW = 84, LIST_PER = 6;
inline constexpr int DEL_W = 44;
inline TRect listRect(int i) { return TRect{0, LIST_Y0 + i * LIST_ROW, 480 - DEL_W, LIST_ROW}; }
inline TRect delRect(int i) { return TRect{480 - DEL_W, LIST_Y0 + i * LIST_ROW, DEL_W, LIST_ROW}; }
// The recipe page and the cooking page are the two you read with your hands
// busy, so both take a text size and a rotation. Neither can keep a constant
// row height or a constant number of rows: bigger type means fewer, and a
// turned panel is 480 tall rather than 800, which the old y=228 start and six
// rows of 66 ran straight off the bottom of. Everything here is measured from
// the canvas that is live at the time.
inline constexpr int SIZES = 3;
inline const char* sizeName(int i) { return i <= 0 ? "normal" : (i == 1 ? "large" : "largest"); }
// The ingredient list and the step page do not want the same jump: a step is
// already set large because it is read from arm's length, so its top size is
// the panel's biggest, while an ingredient starts one below.
inline TSize ingSize(int i) { return i <= 0 ? TS_MED : (i == 1 ? TS_LARGE : TS_HUGE); }
inline TSize stepSize(int i) { return i <= 0 ? TS_LARGE : TS_HUGE; }

// the cooking page
inline constexpr int COOK_FOOT = 96;
// the phone page (same geometry as the flashcards import, deliberately: one
// pairing screen to learn)
inline constexpr int QR_X = 110, QR_Y = 140, QR_SIZE = 260;
inline constexpr TRect ALT_BTN{40, 588, 400, 60};
inline constexpr TRect DONE_BTN{40, 672, 400, 72};
}  // namespace rcpui

class RecipeTool : public ToolApp {
 public:
  const char* title() const override { return "RECIPES"; }
  bool enterTouchesCard() const override { return true; }  // the list reads /recipes

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _screen = Screen::List;
    _help = !help::suppressed(prefs(), "rcp");
    _note = nullptr;
    _page = 0;
    _size = (uint8_t)prefs().getUInt("rc_size", 0);
    if (_size >= rcpui::SIZES) _size = 0;
    _rot = (uint8_t)prefs().getUInt("rc_rot", 0);
    if (_rot != 1 && _rot != 3) _rot = 0;
    _menu = false;
    reload();
  }

  // The access point must not outlive the screen that started it, however
  // the tool is left -- the hub button included.
  ~RecipeTool() override { _net.stop(); }

  void render(ToolsCanvas& c) override {
    // The card REPLACES the page, as it does in every game: drawn over the
    // list it double-exposes with whatever is underneath, which on glass
    // reads as two screens fighting.
    if (_help && _screen == Screen::List) {
      host().topBar("RECIPES", true);
      renderHelp(c);
      return;
    }
    if (_menu) {
      renderMenu(c);
      return;
    }
    switch (_screen) {
      case Screen::List: renderList(c); break;
      case Screen::View: renderView(c); break;
      case Screen::Cook: renderCook(c); break;
      case Screen::Import: renderImport(c); break;
    }
  }

  void onTap(int x, int y) override {
    // Back first, like everywhere else: the way out works even with the
    // help card up.
    if (_screen == Screen::List && host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
    if (_help && _screen == Screen::List) {
      const help::Tap t = help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Never) help::suppress(prefs(), "rcp");
      _help = false;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (_menu) {
      tapMenu(x, y);
      return;
    }
    switch (_screen) {
      case Screen::List: tapList(x, y); break;
      case Screen::View: tapView(x, y); break;
      case Screen::Cook: tapCook(x, y); break;
      case Screen::Import: tapImport(x, y); break;
    }
  }

  bool onButton(SideBtn b) override {
    // OK opens the panel, and closes it, on the two screens it governs --
    // the same button that opens the readers' own options.
    if (b == SideBtn::Ok && (_screen == Screen::View || _screen == Screen::Cook)) {
      _menu = !_menu;
      host().setCanvasRotation(_menu ? 0 : _rot);
      host().beep(1);
      host().refresh(true);  // the whole screen changes, and it may have turned
      return true;
    }
    if (_menu) return false;  // the panel is a list of rows, not a pager
    if (_screen == Screen::Cook) {
      if (b == SideBtn::Up) stepTo(_step - 1);
      else if (b == SideBtn::Down) stepTo(_step + 1);
      else return false;
      return true;
    }
    if (_screen == Screen::View && _r.nIng > ingPer()) {
      const int pages = ingPages();
      if (b == SideBtn::Up && _ingPage > 0) _ingPage--;
      else if (b == SideBtn::Down && _ingPage < pages - 1) _ingPage++;
      else return false;
      host().beep(0);
      host().refreshUi();
      return true;
    }
    if (_screen == Screen::List) {
      const int pages = listPages();
      if (b == SideBtn::Up && _page > 0) _page--;
      else if (b == SideBtn::Down && _page < pages - 1) _page++;
      else return false;
      host().beep(0);
      host().refreshUi();
      return true;
    }
    return false;
  }

  void tick() override {
    if (_screen != Screen::Import) return;
    _net.loop();
    if (_net.received() && !_gotShown) {
      _gotShown = true;
      host().beep(3);
      host().refreshUi();
    }
    // The moment a phone joins, the WiFi QR has done its work; show the page
    // address instead, the same self-advance the notes pairing screen does.
    if (!_altQr && !_net.received() && _net.hasClient()) {
      _altQr = true;
      host().beep(1);
      host().refresh(true);  // a QR with ghosting is a QR a camera refuses
    }
  }
  bool wantsTick() const override { return _screen == Screen::Import; }

#ifdef TOYBOX_HOST
  int hostScreen() const { return (int)_screen; }
  // The geometry the guards aim at: computed now, so a guard that wrote the
  // old constants down would tap empty panel the first time the size or the
  // rotation moved.
  int hostIngTop() { return ingTop(); }
  int hostIngRowH() { return ingRowH(); }
  int hostIngPer() { return ingPer(); }
  TRect hostCookBtn() { return cookBtn(); }
  int hostSize() const { return _size; }
  int hostRot() const { return _rot; }
  bool hostMenu() const { return _menu; }
  int hostCount() const { return _nFlash + _nCard; }
  const rcp::Recipe& hostRecipe() const { return _r; }
  int hostStep() const { return _step; }
  bool hostTicked(int i) const { return _tick[i]; }
  rweb::RecipeServer& hostNet() { return _net; }
  void hostCloseHelp() { _help = false; }
#endif

 private:
  enum class Screen : uint8_t { List, View, Cook, Import };
  uint8_t _size = 0;   // rcpui::SIZES
  uint8_t _rot = 0;    // 0 upright, 1 and 3 the two landscapes
  bool _menu = false;  // the options panel, over the recipe or the step

  // Where the ingredient list starts, how tall its rows are and how many fit:
  // all three follow the chosen size and the live canvas, so a bigger face and
  // a turned panel are the same question asked twice.
  //
  // Landscape gets a shorter head -- one line of title, no headline -- because
  // 480 px of height has to hold the name, the label and at least two rows,
  // and a headline is the least of those.
  bool landscape() { return host().canvas().width() > host().canvas().height(); }

  int ingTop() { return landscape() ? 128 : 228; }

  int ingRowH() {
    ToolsCanvas& c = host().canvas();
    const int line = c.textHeight(rcpui::ingSize(_size));
    // Two wrapped lines in portrait, one in landscape: the panel is wider that
    // way round, so an ingredient that needed two lines rarely still does.
    return landscape() ? line + 26 : line * 2 + 18;
  }

  // The bottom of the list: above the pager line, which is itself above the
  // COOK button when there is one. Measured, because the pager is set in the
  // smallest face and the smallest face has moved once already.
  int pagerH() { return host().canvas().textHeight(TS_SMALL) + 14; }
  int ingBottom() {
    ToolsCanvas& c = host().canvas();
    const int below = (_r.nSteps > 0 ? 88 + 16 : 8) + pagerH();
    return c.height() - below;
  }

  int ingPer() {
    const int n = (ingBottom() - ingTop()) / ingRowH();
    return n < 1 ? 1 : n;
  }
  int ingPages() {
    const int per = ingPer();
    return (_r.nIng + per - 1) / per;
  }
  TRect ingRow(int k) {
    return TRect{0, ingTop() + k * ingRowH(), host().canvas().width(), ingRowH()};
  }

  // Anchored to the bottom rather than written down: on a turned panel y=692
  // is off the screen entirely.
  TRect cookBtn() {
    ToolsCanvas& c = host().canvas();
    const int w = c.width() - 80 > 400 ? 400 : c.width() - 80;
    return TRect{(c.width() - w) / 2, c.height() - 88, w, 72};
  }

  Preferences& prefs() { return host().prefs(); }

  void reload() {
    _nFlash = rstore::list(_slots, _flashNames, rstore::SLOTS);
    const int n = host().sdRecipes(_cardNames, MAX_CARD);
    _cardOk = n >= 0;
    _nCard = n < 0 ? 0 : n;
  }

  int listPages() {
    const int total = _nFlash + _nCard;
    return total <= 0 ? 1 : (total + rcpui::LIST_PER - 1) / rcpui::LIST_PER;
  }

  // --- the list ---------------------------------------------------------------

  void renderList(ToolsCanvas& c) {
    using namespace rcpui;
    host().topBar("RECIPES", true);
    c.button(PHONE_BTN.x, PHONE_BTN.y, PHONE_BTN.w, PHONE_BTN.h, "GET FROM THE PHONE", false,
             TS_MED);
    const int total = _nFlash + _nCard;
    if (total == 0) {
      c.textCentered(c.width() / 2, 330, _cardOk ? "no recipes yet" : "no card found", TS_LARGE,
                     true);
      c.textCentered(c.width() / 2, 378, "save a recipe page's .json into", TS_SMALL, true);
      c.textCentered(c.width() / 2, 404, "/recipes on the card, or use the", TS_SMALL, true);
      c.textCentered(c.width() / 2, 430, "phone button above", TS_SMALL, true);
    }
    for (int k = 0; k < LIST_PER; k++) {
      const int idx = _page * LIST_PER + k;
      if (idx >= total) break;
      const bool flash = idx < _nFlash;
      char nm[ToolsHost::RECIPE_NAME_LEN];
      snprintf(nm, sizeof(nm), "%s", flash ? _flashNames[idx] : _cardNames[idx - _nFlash]);
      // The extension is the file's business, not the shelf's.
      const size_t L = strlen(nm);
      if (!flash && L > 5 && strcasecmp(nm + L - 5, ".json") == 0) nm[L - 5] = 0;
      const int y = LIST_Y0 + k * LIST_ROW;
      c.textClipped(24, y + 12, c.width() - 48 - (flash ? DEL_W : 0), nm, TS_MED, true);
      c.text(24, y + 46, flash ? "from the phone" : "on the card", TS_SMALL, true);
      if (flash) c.textCentered(delRect(k).x + DEL_W / 2, y + 24, "x", TS_MED, true);
      if (k + 1 < LIST_PER && idx + 1 < total)
        c.fillRect(16, y + LIST_ROW - 4, c.width() - 32, 1, true);
    }
    if (listPages() > 1) {
      char buf[24];
      snprintf(buf, sizeof(buf), "page %d of %d", _page + 1, listPages());
      c.textCentered(c.width() / 2, 700, buf, TS_SMALL, true);
      c.textCentered(c.width() / 2, 726, "side buttons turn the page", TS_SMALL, true);
    }
    if (_note) c.textCentered(c.width() / 2, 768, _note, TS_SMALL, true);
  }

  void tapList(int x, int y) {
    using namespace rcpui;
    _note = nullptr;
    if (host().isHelpTap(x, y)) {
      _help = true;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (PHONE_BTN.hit(x, y)) {
      openImport();
      return;
    }
    const int total = _nFlash + _nCard;
    for (int k = 0; k < LIST_PER; k++) {
      const int idx = _page * LIST_PER + k;
      if (idx >= total) break;
      const bool flash = idx < _nFlash;
      if (flash && delRect(k).hit(x, y)) {
        rstore::remove(_slots[idx]);
        reload();
        if (_page >= listPages()) _page = listPages() - 1;
        host().beep(2);
        host().refreshUi();
        return;
      }
      if (!listRect(k).hit(x, y)) continue;
      openRecipe(idx);
      return;
    }
  }

  void openRecipe(int idx) {
    bool ok = false;
    if (idx < _nFlash) {
      ok = rstore::load(_slots[idx], _r);
    } else {
      // Off the card: the whole file into PSRAM, parsed, and dropped. A
      // page's saved JSON block can be big; the recipe it holds is not.
      char path[80];
      snprintf(path, sizeof(path), "/recipes/%s", _cardNames[idx - _nFlash]);
      constexpr int CAP = 256 << 10;
      char* buf = (char*)ps_malloc(CAP);
      if (buf) {
        const int n = host().sdReadWhole(path, buf, CAP);
        if (n > 0) ok = rcp::parse(buf, (size_t)n, _r);
        free(buf);
      }
    }
    if (!ok) {
      _note = "could not read a recipe out of that file";
      host().beep(2);
      // The failed card read may have borrowed the bus; paint from scratch.
      host().refresh(true);
      return;
    }
    memset(_tick, 0, sizeof(_tick));
    _ingPage = 0;
    _screen = Screen::View;
    host().setCanvasRotation(_rot);  // the two reading screens wear the angle
    host().beep(1);
    // Same reason: a card read released the bus and reset the panel.
    host().refresh(true);
  }

  // --- the options panel ------------------------------------------------------
  // Two rows, both about the screen you are reading rather than the recipe on
  // it, which is why they are here and not in settings. Portrait whatever the
  // page under it is doing: the panel is a portrait design, like every screen
  // that is not the page itself.

  void renderMenu(ToolsCanvas& c) {
    const rmenu::Item items[2] = {
        {"Text size", rcpui::sizeName(_size), true},
        {"Rotation", "", false},  // three buttons, drawn under the label
    };
    rmenu::drawRoot(host(), c, "OPTIONS", items, 2,
                    _screen == Screen::Cook ? "STEP" : "RECIPE");
    // The same three buttons the readers offer, from the same place: one tap
    // to any of the three rather than a cycle through the one you do not
    // want. The turn waits for the panel to close, so the panel is never
    // asked to draw itself sideways.
    rmenu::drawRotRow(c, 1, _rot);
    c.textCentered(c.width() / 2, c.height() - 96,
                   _screen == Screen::Cook ? "these apply to the steps and the recipe"
                                           : "these apply to the recipe and the steps",
                   TS_SMALL, true);
    c.textCentered(c.width() / 2, c.height() - 64, "the OK button closes this", TS_SMALL, true);
  }

  void tapMenu(int x, int y) {
    if (host().isBackTap(x, y)) {
      closeMenu();
      return;
    }
    const int w = host().canvas().width();
    const int row = rmenu::hitRoot(x, y, 2, w);
    if (row < 0) return;
    if (row == 0) {
      // Three sizes, so the row itself cycles: a stepper for three things is
      // a stepper nobody needs. Rotation gets buttons because its three are
      // not a sequence -- left and right are alternatives, not more of each
      // other.
      _size = (uint8_t)((_size + 1) % rcpui::SIZES);
      prefs().putUInt("rc_size", _size);
      _ingPage = 0;  // fewer rows per page: the old page number may not exist
    } else {
      const int want = rmenu::hitRot(x, y, 1, w);
      if (want < 0 || want == _rot) return;  // the row outside them chooses nothing
      _rot = (uint8_t)want;
      prefs().putUInt("rc_rot", _rot);
      _ingPage = 0;
    }
    host().beep(0);
    host().refreshUi();
  }

  void closeMenu() {
    _menu = false;
    host().setCanvasRotation(_rot);
    host().beep(1);
    host().refresh(true);
  }

  // --- one recipe -------------------------------------------------------------

  void renderView(ToolsCanvas& c) {
    using namespace rcpui;
    host().topBar("RECIPE", false, "RECIPES");
    const TSize its = ingSize(_size);
    const int top = ingTop(), rowH = ingRowH(), per = ingPer();
    const bool land = landscape();
    // The name: two lines when there is room for two, one when there is not.
    {
      char line[rcp::NAME_LEN] = "", cand[rcp::NAME_LEN + 4];
      const char* rest = nullptr;
      for (const char* p = _r.name; *p;) {
        const char* e = p;
        while (*e && *e != ' ') e++;
        snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
        if (line[0] && c.textWidth(cand, TS_LARGE, true) > c.width() - 48) {
          rest = p;
          break;
        }
        snprintf(line, sizeof(line), "%s", cand);
        p = e;
        while (*p == ' ') p++;
      }
      c.textCentered(c.width() / 2, land ? 58 : 70, line, TS_LARGE, true, true);
      if (rest && !land) c.textClipped(24, 112, c.width() - 48, rest, TS_LARGE, true, true);
    }
    if (!land) {
      char head[48];
      rcp::headline(_r, head, sizeof(head));
      if (head[0]) c.textCentered(c.width() / 2, 158, head, TS_SMALL, true);
    }
    c.textTracked(16, top - 34, "INGREDIENTS", TS_MED, true, false, 1);
    c.fillRect(16, top - 8, c.width() - 32, 1, true);
    if (_r.nIng == 0) c.text(24, top + 10, "the recipe lists none", its, true);
    const int first = _ingPage * per;
    for (int k = 0; k < per; k++) {
      const int i = first + k;
      if (i >= _r.nIng) break;
      const int y = top + k * rowH;
      // The tick box: gathering is half of cooking.
      const int by = y + (rowH - 28) / 2;  // the box, centred in whatever the row is
      if (_tick[i]) {
        c.fillRect(20, by, 28, 28, true);
        c.drawLine(26, by + 14, 32, by + 20, 3, false);
        c.drawLine(32, by + 20, 42, by + 8, 3, false);
      } else {
        c.drawRect(20, by, 28, 28, 2, true);
      }
      // Up to two lines of the ingredient, wrapped like the name.
      char line[rcp::ING_LEN] = "", cand[rcp::ING_LEN + 4];
      const char* rest = nullptr;
      const int tx = 64, tw = c.width() - tx - 20;
      for (const char* p = _r.ing[i]; *p;) {
        const char* e = p;
        while (*e && *e != ' ') e++;
        snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
        if (line[0] && c.textWidth(cand, its) > tw) {
          rest = p;
          break;
        }
        snprintf(line, sizeof(line), "%s", cand);
        p = e;
        while (*p == ' ') p++;
      }
      // One line where a row is one line high, two where it is two. A second
      // line drawn into a landscape row would sit on top of the row below it.
      const int lh = c.textHeight(its);
      if (land) {
        c.textClipped(tx, y + (rowH - lh) / 2, tw, line, its, true);
      } else {
        c.textClipped(tx, y + 4, tw, line, its, true);
        if (rest) c.textClipped(tx, y + 4 + lh + 4, tw, rest, its, true);
      }
    }
    const int pages = ingPages();
    if (pages > 1) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%d of %d - side buttons page", _ingPage + 1, pages);
      c.textCentered(c.width() / 2, ingBottom() + 6, buf, TS_SMALL, true);
    }
    if (_r.nSteps > 0) {
      const TRect b = cookBtn();
      c.button(b.x, b.y, b.w, b.h, "COOK - STEP BY STEP", true, TS_LARGE);
    }
  }

  void tapView(int x, int y) {
    using namespace rcpui;
    if (host().isBackTap(x, y)) {
      _screen = Screen::List;
      host().setCanvasRotation(0);  // every other screen here is portrait
      host().beep(1);
      host().refresh(true);         // the panel may have just stood up
      return;
    }
    if (_r.nSteps > 0 && cookBtn().hit(x, y)) {
      _step = 0;
      _screen = Screen::Cook;
      host().beep(1);
      host().refreshUi();
      return;
    }
    const int per = ingPer();
    const int first = _ingPage * per;
    for (int k = 0; k < per; k++) {
      const int i = first + k;
      if (i >= _r.nIng) break;
      if (!ingRow(k).hit(x, y)) continue;
      _tick[i] = !_tick[i];
      host().beep(0);
      host().refreshUi();
      return;
    }
  }

  // --- cooking ----------------------------------------------------------------

  void stepTo(int s) {
    if (s < 0) return;
    if (s >= _r.nSteps) {
      // Past the last step is done: back to the recipe, ticks and all.
      _screen = Screen::View;
      host().beep(1);
      host().refreshUi();
      return;
    }
    _step = s;
    host().beep(0);
    host().refreshUi();
  }

  void renderCook(ToolsCanvas& c) {
    using namespace rcpui;
    char bar[24];
    snprintf(bar, sizeof(bar), "STEP %d OF %d", _step + 1, (int)_r.nSteps);
    host().topBar(bar, false, "RECIPE");
    // The step, large, wrapped over the whole page -- read from arm's length
    // with wet hands, which is why its smallest size is the recipe page's
    // largest. The line step follows the face rather than a written-down 46:
    // at the biggest size that number overlapped every line with the next.
    {
      const TSize sts = rcpui::stepSize(_size);
      const int lineH = c.textHeight(sts) + 10;
      char line[96] = "", cand[96 + 4];
      int y = landscape() ? 88 : 130;
      const int w = c.width() - 56;
      const int floorY = c.height() - COOK_FOOT - lineH;
      for (const char* p = _r.steps[_step]; *p && y < floorY;) {
        const char* e = p;
        while (*e && *e != ' ') e++;
        snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
        if (line[0] && c.textWidth(cand, sts) > w) {
          c.text(28, y, line, sts, true);
          y += lineH;
          line[0] = 0;
          continue;  // the word that did not fit starts the next line
        }
        snprintf(line, sizeof(line), "%s", cand);
        p = e;
        while (*p == ' ') p++;
      }
      if (line[0] && y < floorY) c.text(28, y, line, sts, true);
    }
    // The footer: two tap zones a knuckle can hit.
    const int fy = c.height() - COOK_FOOT;
    c.fillRect(16, fy, c.width() - 32, 1, true);
    if (_step > 0) c.text(28, fy + 34, "< back", TS_MED, true);
    const char* fwd = _step + 1 < _r.nSteps ? "next >" : "done";
    c.text(c.width() - 28 - c.textWidth(fwd, TS_MED, true), fy + 34, fwd, TS_MED, true, true);
  }

  void tapCook(int x, int y) {
    if (host().isBackTap(x, y)) {
      _screen = Screen::View;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (y < host().canvas().height() - rcpui::COOK_FOOT) return;
    if (x < host().canvas().width() / 3)
      stepTo(_step - 1);
    else
      stepTo(_step + 1);
  }

  // --- the phone --------------------------------------------------------------

  void openImport() {
    _screen = Screen::Import;
    _gotShown = false;
    _altQr = false;
    _netOk = _net.start();
    host().beep(_netOk ? 1 : 2);
    host().refresh(true);  // a clean panel under a QR a camera has to read
  }

  void closeImport() {
    _net.stop();
    _screen = Screen::List;
    reload();
    _page = 0;
    host().beep(1);
    host().refreshUi();
  }

  void renderImport(ToolsCanvas& c) {
    using namespace rcpui;
    host().topBar("FROM THE PHONE", false, "RECIPES");
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start wifi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "BACK", true, TS_LARGE);
      return;
    }
    if (_net.received()) {
      c.textCentered(c.width() / 2, 260, "KEPT", TS_HUGE, true, true);
      c.textCentered(c.width() / 2, 340, _net.recipeName(), TS_LARGE, true, true);
      decor::ornament(c, c.width() / 2, 400, 300, true);
      c.textCentered(c.width() / 2, 440, "send another from the same page,", TS_MED, true);
      c.textCentered(c.width() / 2, 468, "or tap DONE", TS_MED, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", true, TS_LARGE);
      return;
    }
    char buf[64];
    if (_altQr) {
      c.textCentered(c.width() / 2, 100, "if it did not open by itself", TS_MED, true);
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, _net.url());
      c.textCentered(c.width() / 2, 440, "Open this in your browser", TS_MED, true, true);
      c.textCentered(c.width() / 2, 478, _net.url(), TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "BACK TO WIFI", false, TS_MED);
    } else {
      c.textCentered(c.width() / 2, 100, "the page opens by itself", TS_MED, true);
      const String wifi = _net.wifiPayload();
      fqr::draw(c, QR_X, QR_Y, QR_SIZE, wifi.c_str());
      c.textCentered(c.width() / 2, 440, "Scan with your phone camera", TS_MED, true, true);
      snprintf(buf, sizeof(buf), "%s   key %s", _net.ssid(), _net.password());
      c.textCentered(c.width() / 2, 478, buf, TS_MED, true);
      c.button(ALT_BTN.x, ALT_BTN.y, ALT_BTN.w, ALT_BTN.h, "PAGE DIDN'T OPEN?", false, TS_MED);
    }
    c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "DONE", false, TS_LARGE);
  }

  void tapImport(int x, int y) {
    using namespace rcpui;
    if (DONE_BTN.hit(x, y) || host().isBackTap(x, y)) return closeImport();
    if (!_net.received() && _netOk && ALT_BTN.hit(x, y)) {
      _altQr = !_altQr;
      host().beep(0);
      host().refresh(true);  // QR pages stay clean
    }
  }

  // --- the first-open card ----------------------------------------------------

  void renderHelp(ToolsCanvas& c) {
    static const help::Text t{{
                                  "Recipes come from two places.",
                                  "",
                                  "Save a recipe page's hidden",
                                  "JSON (schema.org - nearly",
                                  "every site embeds it) as a",
                                  ".json file in /recipes on",
                                  "the SD card...",
                                  "",
                                  "...or GET FROM THE PHONE and",
                                  "paste the page over WiFi.",
                                  "",
                                  "Tick, then COOK, step by step.",
                              },
                              false,
                              11};
    help::render(c, t, "RECIPES");
  }

  Screen _screen = Screen::List;
  bool _help = false;
  const char* _note = nullptr;

  static constexpr int MAX_CARD = 16;
  char _cardNames[MAX_CARD][ToolsHost::RECIPE_NAME_LEN];
  int _nCard = 0;
  bool _cardOk = false;
  char _flashNames[rstore::SLOTS][rcp::NAME_LEN];
  int _slots[rstore::SLOTS];
  int _nFlash = 0;
  int _page = 0;

  rcp::Recipe _r{};
  bool _tick[rcp::MAX_ING] = {};
  int _ingPage = 0;
  int _step = 0;

  rweb::RecipeServer _net;
  bool _netOk = false, _altQr = false, _gotShown = false;
};
