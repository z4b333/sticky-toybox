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
#include "unicode.h"

namespace rcpui {
// the list
// v2 (kit "Recipes"): rows with a chevron, the phone at the foot; hold a
// row from the phone to delete it (a card file is the card's business).
inline constexpr TRect PHONE_BTN{20, 664, 440, 64};
inline constexpr int LIST_Y0 = 60, LIST_ROW = 84, LIST_PER = 6;
inline constexpr int DEL_W = 120;
inline TRect listRect(int i) { return TRect{0, LIST_Y0 + i * LIST_ROW, 480, LIST_ROW}; }
inline TRect delRect(int i) { return TRect{480 - 16 - DEL_W, LIST_Y0 + i * LIST_ROW + 10, DEL_W, LIST_ROW - 24}; }
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
inline constexpr TRect ALT_BTN = importui::ALT_BTN;
inline constexpr TRect DONE_BTN = importui::DONE_BTN;
}  // namespace rcpui

class RecipeTool : public ToolApp {
 public:
  const char* title() const override { return "RECIPES"; }
  int fontSlot() const override { return ToolsHost::FONT_RECIPES; }
  bool enterTouchesCard() const override { return true; }  // the list reads /recipes

  void enter(ToolsHost& h) override {
    ToolApp::enter(h);
    _screen = Screen::List;
    _help = !help::suppressed(prefs(), "rcp");
    _note = nullptr;
    _page = 0;
    _size = (uint8_t)prefs().getUInt("rc_size", 0);
    if (_size >= rcpui::SIZES) _size = 0;
    _lead = (uint8_t)prefs().getUInt("rc_lead", 1);
    if (_lead >= rmenu::LEADS) _lead = 1;
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
      if (b == SideBtn::Up) back();
      else if (b == SideBtn::Down) forward();
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
  // Opens a recipe from JSON straight into cooking, at its first step.
  bool hostCook(const char* json) {
    if (!rcp::parse(json, strlen(json), _r)) return false;
    _screen = Screen::Cook;
    _menu = false;
    _step = _part = 0;
    _partAt[0] = 0;
    return true;
  }
  int hostPart() const { return _part; }
  bool hostMore() const { return _more; }
  void hostForward() { forward(); }
  // The geometry the guards aim at: computed now, so a guard that wrote the
  // old constants down would tap empty panel the first time the size or the
  // rotation moved.
  int hostIngTop() { return ingTop(); }
  int hostIngRowH() { return ingRowH(); }
  int hostIngPer() { return ingPer(); }
  TRect hostCookBtn() { return cookBtn(); }
  int hostSize() const { return _size; }
  int hostRot() const { return _rot; }
  int hostLead() const { return _lead; }
  TRect hostRotRect(int k) { return rotRect(k); }
  static int hostStepperY(int r) { return stepperY(r); }
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
  uint8_t _lead = 1;   // rmenu::LEADS -- normal
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

  int air() { return rmenu::leadAir(_lead); }

  int ingRowH() {
    ToolsCanvas& c = host().canvas();
    const int line = c.textHeight(rcpui::ingSize(_size));
    // Two wrapped lines in portrait, one in landscape: the panel is wider that
    // way round, so an ingredient that needed two lines rarely still does.
    // The air goes between the two lines AND under the row, so "airy" opens
    // the list up rather than just its second lines.
    return landscape() ? line + air() * 2 + 6 : line * 2 + air() * 2 - 2;
  }

  // The bottom of the list: above the pager line, which is itself above the
  // COOK button when there is one. Measured, because the pager is set in the
  // smallest face and the smallest face has moved once already.
  int pagerH() { return host().canvas().textHeight(TS_MED) + 14; }
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
    const int total = _nFlash + _nCard;
    if (total == 0) {
      c.textCentered(c.width() / 2, 250, _cardOk ? "no recipes yet" : "no card found", TS_LARGE, true, true);
      c.textCentered(c.width() / 2, 310, "add one from your phone,", TS_MED, true);
      c.textCentered(c.width() / 2, 344, "or save a recipe page's .json", TS_MED, true);
      c.textCentered(c.width() / 2, 378, "in /recipes on the card", TS_MED, true);
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
      const bool armed = flash && idx == _delArm;
      if (armed) c.fillRect(8, y + 4, c.width() - 16, LIST_ROW - 8, true);
      c.textClipped(24, y + 10, c.width() - 72 - (armed ? DEL_W : 0), nm, scriptFloor(nm, TS_MED), !armed, true);
      c.text(24, y + 44, flash ? "from the phone" : "on the card", TS_MED, !armed);
      if (armed) {
        const TRect d = delRect(k);
        c.fillRect(d.x, d.y, d.w, d.h, false);
        c.textInBox(d.x, d.y, d.w, d.h, "Delete", TS_MED, true, true);
      } else {
        c.text(c.width() - 36, y + 26, ">", TS_MED, true, true);
      }
      c.fillRect(16, y + LIST_ROW - 2, c.width() - 32, 1, true);
    }
    if (listPages() > 1) {
      char buf[40];
      snprintf(buf, sizeof(buf), "page %d of %d - side buttons turn", _page + 1, listPages());
      c.textCentered(c.width() / 2, 584, buf, TS_MED, true);
    }
    c.button(PHONE_BTN.x, PHONE_BTN.y, PHONE_BTN.w, PHONE_BTN.h, "Add from your phone", false, TS_MED);
    if (_note) c.textCentered(c.width() / 2, 620, _note, TS_MED, true);
    else if (_nFlash) c.textCentered(c.width() / 2, 750, "hold one from the phone to delete it", TS_MED, true);
  }

  void onHold(int x, int y) override {
    if (_screen != Screen::List || _help) return;
    for (int k = 0; k < rcpui::LIST_PER; k++) {
      const int idx = _page * rcpui::LIST_PER + k;
      if (idx >= _nFlash) break;
      if (rcpui::listRect(k).hit(x, y)) {
        _delArm = idx;
        host().beep(1);
        host().refresh(false);
        return;
      }
    }
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
    if (_delArm >= 0) {
      // A held row offers Delete; any other tap puts it back.
      const int idx = _delArm, k = idx - _page * LIST_PER;
      _delArm = -1;
      if (k >= 0 && k < LIST_PER && delRect(k).hit(x, y)) {
        rstore::remove(_slots[idx]);
        reload();
        if (_page >= listPages()) _page = listPages() - 1;
        host().beep(2);
      } else {
        host().beep(0);
      }
      host().refresh(false);
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

  // Three controls, and all three are the ones the readers already use: two
  // minus/plus steppers and the rotation buttons, drawn from reader_menu.h.
  // Size and spacing are scales with ends you can reach, so they step;
  // rotation is three alternatives, so it does not.
  static constexpr int MENU_Y0 = 96;
  static int stepperY(int r) { return MENU_Y0 + r * rmenu::STEP_H; }
  int rotY() { return stepperY(2) - 10; }

  void renderMenu(ToolsCanvas& c) {
    char clk[8];
    host().topBar("OPTIONS", false, _screen == Screen::Cook ? "STEP" : "RECIPE",
                  rmenu::clockCorner(host(), clk, sizeof(clk)));
    rmenu::drawStepper(c, stepperY(0), "Text size", rcpui::sizeName(_size));
    rmenu::drawStepper(c, stepperY(1), "Line spacing", rmenu::leadName(_lead));
    c.fillRect(24, rotY() - 8, c.width() - 48, 1, true);
    c.text(28, rotY() + 10, "Rotation", TS_MED, true);
    // The turn waits for the panel to close, so the panel is never asked to
    // draw itself sideways.
    for (int k = 0; k < 3; k++) {
      static const char* kLab[3] = {"< LEFT", "PORTRAIT", "RIGHT >"};
      const TRect b = rotRect(k);
      c.button(b.x, b.y, b.w, b.h, kLab[k], _rot == rmenu::ROT_BTN[k], TS_MED);
    }
    // A sample, for the same reason the reader has one: nobody can picture
    // "largest, airy" from the words. Two lines, because two lines is what a
    // long ingredient takes and the spacing between them is half the setting.
    {
      const int sy = rotY() + 128;
      c.fillRect(24, sy - 16, c.width() - 48, 1, true);
      const TSize its = rcpui::ingSize(_size);
      const int lh = c.textHeight(its);
      c.drawRect(28, sy + (lh - 28) / 2, 28, 28, 2, true);
      c.textClipped(72, sy, c.width() - 96, "2-2 ⅓ cups all-purpose", its, true);
      c.textClipped(72, sy + lh + air(), c.width() - 96, "flour, divided", its, true);
    }
    c.textCentered(c.width() / 2, c.height() - 100, "for the recipe and its steps", TS_MED, true);
    c.textCentered(c.width() / 2, c.height() - 64, "the OK button closes this", TS_MED, true);
  }

  TRect rotRect(int k) {
    ToolsCanvas& c = host().canvas();
    const int bw = (c.width() - 56 - 16) / 3;
    return TRect{28 + k * (bw + 8), rotY() + 44, bw, 52};
  }

  void tapMenu(int x, int y) {
    if (host().isBackTap(x, y)) {
      closeMenu();
      return;
    }
    const int w = host().canvas().width();
    for (int r = 0; r < 2; r++) {
      const int step = rmenu::hitStepper(x, y, stepperY(r), w);
      if (step == 0) continue;
      uint8_t& v = r == 0 ? _size : _lead;
      const int lim = r == 0 ? rcpui::SIZES : rmenu::LEADS;
      const int nv = (int)v + step;
      if (nv < 0 || nv >= lim) {
        host().beep(2);  // the end of the scale, and it says so
        return;
      }
      v = (uint8_t)nv;
      prefs().putUInt(r == 0 ? "rc_size" : "rc_lead", v);
      _ingPage = 0;  // fewer rows per page: the old page number may not exist
      host().beep(0);
      host().refreshUi();
      return;
    }
    for (int k = 0; k < 3; k++) {
      if (!rotRect(k).hit(x, y)) continue;
      if (_rot == rmenu::ROT_BTN[k]) return;  // already there: nothing to say
      _rot = rmenu::ROT_BTN[k];
      prefs().putUInt("rc_rot", _rot);
      _ingPage = 0;
      host().beep(0);
      host().refreshUi();
      return;
    }
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
      if (head[0]) c.textCentered(c.width() / 2, 158, head, TS_MED, true);
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
      if (k + 1 < per && i + 1 < _r.nIng) c.fillRect(64, y + rowH - 2, c.width() - 84, 1, true);
      if (land) {
        c.textClipped(tx, y + (rowH - lh) / 2, tw, line, its, true);
      } else {
        c.textClipped(tx, y + 2, tw, line, its, true);
        if (rest) c.textClipped(tx, y + 2 + lh + air(), tw, rest, its, true);
      }
    }
    const int pages = ingPages();
    if (pages > 1) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%d of %d - side buttons turn", _ingPage + 1, pages);
      c.textCentered(c.width() / 2, ingBottom() + 6, buf, TS_MED, true);
    }
    if (_r.nSteps > 0) {
      const TRect b = cookBtn();
      c.button(b.x, b.y, b.w, b.h, "Start cooking", true, TS_LARGE);
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
      _part = 0;
      _partAt[0] = 0;
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

  // Centred text on up to `maxLines` lines, broken at a space where one fits
  // (else between characters, for scripts without spaces); the last line
  // ends in dots when there is still more.
  static void nameLines(ToolsCanvas& c, int cx, int y, int maxW, const char* s, TSize want, int maxLines) {
    const TSize sz = scriptFloor(s, want);
    const int step = c.textHeight(sz) + 8;
    char buf[160];
    for (int line = 0; line < maxLines && *s; line++) {
      if (line == maxLines - 1 || c.textWidth(s, sz, true) <= maxW) {
        if (c.textWidth(s, sz, true) <= maxW) c.textCentered(cx, y, s, sz, true, true);
        else c.textClipped(cx - maxW / 2, y, maxW, s, sz, true, true);
        return;
      }
      int n = 0, lastSpace = -1;
      const char* p = s;
      while (*p) {
        const char* q = p;
        uni::next(q);
        const int add = (int)(q - p);
        if (n + add >= (int)sizeof(buf)) break;
        memcpy(buf + n, p, (size_t)add);
        buf[n + add] = 0;
        if (c.textWidth(buf, sz, true) > maxW) break;
        if (*p == ' ') lastSpace = n;
        n += add;
        p = q;
      }
      const int cut = (*p && lastSpace > 0) ? lastSpace : n;
      buf[cut] = 0;
      c.textCentered(cx, y, buf, sz, true, true);
      s += cut;
      while (*s == ' ') s++;
      y += step;
    }
  }

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
    _part = 0;
    _partAt[0] = 0;
    host().beep(0);
    host().refreshUi();
  }

  // A step longer than the screen goes on over more screens (owner, 24 Nov):
  // forward shows the rest of it before the next step, back the part before.
  void forward() {
    if (_more && _part + 1 < MAX_PARTS) {
      _part++;
      host().beep(0);
      host().refreshUi();
      return;
    }
    stepTo(_step + 1);
  }
  void back() {
    if (_part > 0) {
      _part--;
      host().beep(0);
      host().refreshUi();
      return;
    }
    stepTo(_step - 1);
  }

  void renderCook(ToolsCanvas& c) {
    using namespace rcpui;
    char bar[32];
    if (_part > 0) snprintf(bar, sizeof(bar), "STEP %d OF %d, GOING ON", _step + 1, (int)_r.nSteps);
    else snprintf(bar, sizeof(bar), "STEP %d OF %d", _step + 1, (int)_r.nSteps);
    host().topBar(bar, false, "RECIPE");
    const bool land = landscape();
    // The recipe's name and a bar of the steps, under the title (kit).
    int y0 = 48;
    if (!land) {
      c.textClipped(24, y0, c.width() - 48, _r.name, scriptFloor(_r.name, TS_MED), true);
      y0 += 36;
    }
    {
      const int n = _r.nSteps, gap = 4, w = c.width() - 48;
      const int seg = n <= 24 ? (w - (n - 1) * gap) / n : 0;
      for (int i = 0; seg && i < n; i++) {
        const int sx = 24 + i * (seg + gap);
        if (i <= _step) c.fillRect(sx, y0, seg, 8, true);
        else c.drawRect(sx, y0, seg, 8, 1, true);
      }
      y0 += 24;
    }
    // The step number, big, then the step itself -- read from arm's length
    // with wet hands, which is why its smallest size is the recipe page's
    // largest. The line step follows the face.
    char num[8];
    snprintf(num, sizeof(num), "%d", _step + 1);
    if (_part == 0) c.text(24, y0, num, TS_HUGE, true, true);
    ContentFace cf(host());
    {
      const TSize sts = rcpui::stepSize(_size);
      const int lineH = c.textHeight(sts) + air();
      char line[96] = "", cand[96 + 4];
      int y = y0 + 6;
      const int x0 = 24 + c.textWidth(num, TS_HUGE, true) + 18;
      const int w = c.width() - x0 - 24;
      const int floorY = c.height() - COOK_FOOT - lineH;
      const char* const step = _r.steps[_step];
      const char* p = step + _partAt[_part];
      _more = false;
      for (; *p;) {
        if (y >= floorY) {  // the screen is full: the rest is the next part
          _more = true;
          break;
        }
        const char* e = p;
        while (*e && *e != ' ') e++;
        snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
        if (line[0] && c.textWidth(cand, sts) > w) {
          c.text(x0, y, line, sts, true);
          y += lineH;
          line[0] = 0;
          continue;  // the word that did not fit starts the next line
        }
        snprintf(line, sizeof(line), "%s", cand);
        p = e;
        while (*p == ' ') p++;
      }
      // Where this part stopped is where the next one starts: the line in
      // hand was not drawn, so it goes back to the start of its first word.
      if (_more) {
        const char* back = p - strlen(line);
        if (back < step + _partAt[_part]) back = step + _partAt[_part];
        if (_part + 1 < MAX_PARTS) _partAt[_part + 1] = (uint16_t)(back - step);
      } else if (line[0]) {
        c.text(x0, y, line, sts, true);
      }
    }
    // The foot: two buttons a knuckle can hit, the way on dark.
    const int fy = c.height() - COOK_FOOT + 14;
    const int bw = (c.width() - 48 - 12) / 2;
    char lab[24];
    if (_part > 0) {
      snprintf(lab, sizeof(lab), "< Back");
      c.button(24, fy, bw, 68, lab, false, TS_MED);
    } else if (_step > 0) {
      snprintf(lab, sizeof(lab), "< Step %d", _step);
      c.button(24, fy, bw, 68, lab, false, TS_MED);
    }
    if (_more) snprintf(lab, sizeof(lab), "Go on >");
    else if (_step + 1 < _r.nSteps) snprintf(lab, sizeof(lab), "Step %d >", _step + 2);
    else snprintf(lab, sizeof(lab), "Done");
    c.button(24 + bw + 12, fy, bw, 68, lab, true, TS_MED);
  }

  void tapCook(int x, int y) {
    if (host().isBackTap(x, y)) {
      _screen = Screen::View;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (y < host().canvas().height() - rcpui::COOK_FOOT) return;
    if (x < host().canvas().width() / 2 && (_step > 0 || _part > 0))
      back();
    else
      forward();
  }

  // --- the phone --------------------------------------------------------------

  void openImport() {
    _screen = Screen::Import;
    _gotShown = false;
    _altQr = false;
    // The home Wi-Fi first when one is saved (owner): the phone keeps its own
    // network. The Sticky's own access point is the backup.
    _netNote = nullptr;
    _netOk = false;
    if (homenet::haveWifi(prefs())) {
      homenet::wifiName(prefs(), _joinName, sizeof(_joinName));
      _joining = true;
      host().refresh(false);  // "Joining ..." while the radio looks
      _netOk = _net.startHome(prefs(), "/recipes");
      _joining = false;
      if (!_netOk) _netNote = "the home Wi-Fi did not answer";
    }
    if (!_netOk) _netOk = _net.start();
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
    host().topBar("ADD A RECIPE", false, "RECIPES");
    if (_joining) return importui::drawJoining(c, _joinName);
    if (!_netOk) {
      c.textCentered(c.width() / 2, 300, "could not start Wi-Fi", TS_LARGE, true, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Back", true, TS_LARGE);
      return;
    }
    if (_net.received()) {
      c.textCentered(c.width() / 2, 240, "Kept", TS_HUGE, true, true);
      // The whole name, on up to two lines (owner: a long one ran off both
      // sides).
      nameLines(c, c.width() / 2, 300, c.width() - 48, _net.recipeName(), TS_LARGE, 2);
      decor::ornament(c, c.width() / 2, 400, 300, true);
      c.textCentered(c.width() / 2, 436, "send another from the same page,", TS_MED, true);
      c.textCentered(c.width() / 2, 470, "or tap Done", TS_MED, true);
      c.button(DONE_BTN.x, DONE_BTN.y, DONE_BTN.w, DONE_BTN.h, "Done", true, TS_LARGE);
      return;
    }
    static const importui::Words W{"Scan to paste a recipe",
                                   {"Paste the recipe page's text", "Check the ingredients and steps",
                                    "It is kept here at once"}};
    importui::draw(c, _net, _altQr, W, _netNote);
  }

  void tapImport(int x, int y) {
    using namespace rcpui;
    if (DONE_BTN.hit(x, y) || host().isBackTap(x, y)) return closeImport();
    if (!_net.received() && _netOk && ALT_BTN.hit(x, y)) {
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
                                  "...or Add from your phone",
                                  "and paste the page there.",
                                  "",
                                  "Tick, then Start cooking.",
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
  int _delArm = -1;  // the row a hold offered to delete
  bool _joining = false;
  const char* _netNote = nullptr;
  char _joinName[34] = {};


  rcp::Recipe _r{};
  bool _tick[rcp::MAX_ING] = {};
  int _ingPage = 0;
  int _step = 0;
  static constexpr int MAX_PARTS = 8;
  int _part = 0;
  uint16_t _partAt[MAX_PARTS] = {};
  bool _more = false;  // set by the render: this part did not reach the end

  rweb::RecipeServer _net;
  bool _netOk = false, _altQr = false, _gotShown = false;
};
