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
#include "recipe_web.h"
#include "tools_ui.h"

namespace rcpui {
// the list
inline constexpr TRect PHONE_BTN{20, 64, 440, 56};
inline constexpr int LIST_Y0 = 148, LIST_ROW = 84, LIST_PER = 6;
inline constexpr int DEL_W = 44;
inline TRect listRect(int i) { return TRect{0, LIST_Y0 + i * LIST_ROW, 480 - DEL_W, LIST_ROW}; }
inline TRect delRect(int i) { return TRect{480 - DEL_W, LIST_Y0 + i * LIST_ROW, DEL_W, LIST_ROW}; }
// the recipe page
inline constexpr int ING_Y0 = 228, ING_ROW = 66, ING_PER = 6;
inline TRect ingRect(int i) { return TRect{0, ING_Y0 + i * ING_ROW, 480, ING_ROW}; }
inline constexpr TRect COOK_BTN{40, 692, 400, 72};
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
    reload();
  }

  void render(ToolsCanvas& c) override {
    switch (_screen) {
      case Screen::List: renderList(c); break;
      case Screen::View: renderView(c); break;
      case Screen::Cook: renderCook(c); break;
      case Screen::Import: renderImport(c); break;
    }
    if (_help && _screen == Screen::List) renderHelp(c);
  }

  void onTap(int x, int y) override {
    if (_help && _screen == Screen::List) {
      const help::Tap t = help::hit(x, y);
      if (t == help::Tap::None) return;
      if (t == help::Tap::Never) help::suppress(prefs(), "rcp");
      _help = false;
      host().beep(1);
      host().refreshUi();
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
    if (_screen == Screen::Cook) {
      if (b == SideBtn::Up) stepTo(_step - 1);
      else if (b == SideBtn::Down) stepTo(_step + 1);
      else return false;
      return true;
    }
    if (_screen == Screen::View && _r.nIng > rcpui::ING_PER) {
      const int pages = (_r.nIng + rcpui::ING_PER - 1) / rcpui::ING_PER;
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
  int hostCount() const { return _nFlash + _nCard; }
  const rcp::Recipe& hostRecipe() const { return _r; }
  int hostStep() const { return _step; }
  bool hostTicked(int i) const { return _tick[i]; }
  rweb::RecipeServer& hostNet() { return _net; }
  void hostCloseHelp() { _help = false; }
#endif

 private:
  enum class Screen : uint8_t { List, View, Cook, Import };

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
    if (host().isBackTap(x, y)) {
      host().beep(1);
      host().goHub();
      return;
    }
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
    host().beep(1);
    // Same reason: a card read released the bus and reset the panel.
    host().refresh(true);
  }

  // --- one recipe -------------------------------------------------------------

  void renderView(ToolsCanvas& c) {
    using namespace rcpui;
    host().topBar("RECIPE", false, "RECIPES");
    // The name, up to two lines, wrapped forward a word at a time.
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
      c.textCentered(c.width() / 2, 70, line, TS_LARGE, true, true);
      if (rest) c.textClipped(24, 112, c.width() - 48, rest, TS_LARGE, true, true);
    }
    char head[48];
    rcp::headline(_r, head, sizeof(head));
    if (head[0]) c.textCentered(c.width() / 2, 158, head, TS_SMALL, true);
    c.textTracked(16, ING_Y0 - 34, "INGREDIENTS", TS_MED, true, false, 1);
    c.fillRect(16, ING_Y0 - 8, c.width() - 32, 1, true);
    if (_r.nIng == 0) c.text(24, ING_Y0 + 10, "the recipe lists none", TS_MED, true);
    const int first = _ingPage * ING_PER;
    for (int k = 0; k < ING_PER; k++) {
      const int i = first + k;
      if (i >= _r.nIng) break;
      const int y = ING_Y0 + k * ING_ROW;
      // The tick box: gathering is half of cooking.
      if (_tick[i]) {
        c.fillRect(20, y + 18, 28, 28, true);
        c.drawLine(26, y + 32, 32, y + 38, 3, false);
        c.drawLine(32, y + 38, 42, y + 26, 3, false);
      } else {
        c.drawRect(20, y + 18, 28, 28, 2, true);
      }
      // Up to two lines of the ingredient, wrapped like the name.
      char line[rcp::ING_LEN] = "", cand[rcp::ING_LEN + 4];
      const char* rest = nullptr;
      const int tx = 64, tw = c.width() - tx - 20;
      for (const char* p = _r.ing[i]; *p;) {
        const char* e = p;
        while (*e && *e != ' ') e++;
        snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
        if (line[0] && c.textWidth(cand, TS_MED) > tw) {
          rest = p;
          break;
        }
        snprintf(line, sizeof(line), "%s", cand);
        p = e;
        while (*p == ' ') p++;
      }
      c.textClipped(tx, y + 4, tw, line, TS_MED, true);
      if (rest) c.textClipped(tx, y + 32, tw, rest, TS_MED, true);
    }
    const int pages = (_r.nIng + ING_PER - 1) / ING_PER;
    if (pages > 1) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%d of %d - side buttons page", _ingPage + 1, pages);
      c.textCentered(c.width() / 2, 648, buf, TS_SMALL, true);
    }
    if (_r.nSteps > 0)
      c.button(COOK_BTN.x, COOK_BTN.y, COOK_BTN.w, COOK_BTN.h, "COOK - STEP BY STEP", true,
               TS_LARGE);
  }

  void tapView(int x, int y) {
    using namespace rcpui;
    if (host().isBackTap(x, y)) {
      _screen = Screen::List;
      host().beep(1);
      host().refreshUi();
      return;
    }
    if (_r.nSteps > 0 && COOK_BTN.hit(x, y)) {
      _step = 0;
      _screen = Screen::Cook;
      host().beep(1);
      host().refreshUi();
      return;
    }
    const int first = _ingPage * ING_PER;
    for (int k = 0; k < ING_PER; k++) {
      const int i = first + k;
      if (i >= _r.nIng) break;
      if (!ingRect(k).hit(x, y)) continue;
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
    // The step, large, wrapped over the whole page. TS_LARGE because this is
    // read from arm's length with wet hands; a 320-byte step fits.
    {
      char line[96] = "", cand[96 + 4];
      int y = 130;
      const int w = c.width() - 56;
      for (const char* p = _r.steps[_step]; *p && y < c.height() - COOK_FOOT - 40;) {
        const char* e = p;
        while (*e && *e != ' ') e++;
        snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - p), p);
        if (line[0] && c.textWidth(cand, TS_LARGE) > w) {
          c.text(28, y, line, TS_LARGE, true);
          y += 46;
          line[0] = 0;
          continue;  // the word that did not fit starts the next line
        }
        snprintf(line, sizeof(line), "%s", cand);
        p = e;
        while (*p == ' ') p++;
      }
      if (line[0] && y < c.height() - COOK_FOOT - 40) c.text(28, y, line, TS_LARGE, true);
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
