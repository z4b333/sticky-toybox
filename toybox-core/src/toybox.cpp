#include "toybox.h"

#include "tools/lock_image.h"

#include "game2048.h"
#include "nonogram.h"
#include "tools/tool_book.h"
#include "tools/tool_coin.h"
#include "tools/tool_epub.h"
#include "tools/recents.h"
#include "tools/tool_dice.h"
#include "tools/tool_flash.h"
#include "tools/tool_note.h"
#include "tools/tool_picker.h"
#include "tools/tool_random.h"
#include "tools/tool_recipe.h"
#include "tools/tool_sea.h"
#include "tools/tool_sudoku.h"
#include "tools/tool_timer.h"
#include "wordle.h"
#include "xo.h"

Toybox toybox;

// Built on demand and destroyed on the way out. The note and flashcard tools
// are the expensive ones -- they carry a web server and a Markdown block table
// -- and neither should exist while the device is doing something else, which
// on the reader is most of the time.
bool Toybox::build(bool game, int idx) {
  release();
  if (game) {
    switch (idx) {
      case 0: _active = new WordleApp(); break;
      case 1: _active = new NonogramApp(); break;
      case 2: _active = new G2048App(); break;
      default: _active = new XoApp(); break;
    }
  } else {
    switch (idx) {
      case 0: _active = new CoinTool(); break;
      case 1: _active = new DiceTool(); break;
      case 2: _active = new TimerTool(); break;
      case 3: _active = new RandomTool(); break;
      case 4: _active = new PickerTool(); break;
      case 5: _active = new FlashTool(); break;
      case 6: _active = new NoteTool(); break;
      case 7: _active = new SeaTool(); break;
      case 8: _active = new SudokuTool(); break;
      case 9: _active = new BookTool(); break;
      case 11: _active = new RecipeTool(); break;
      default: _active = new EpubTool(); break;
    }
  }
  if (!_active) return false;
  _activeIsGame = game;
  _activeIdx = idx;
  return true;
}

void Toybox::release() {
  delete _active;
  _active = nullptr;
  _activeIdx = -1;
}

void Toybox::begin(ToolsHost& h) {
  _host = &h;
  appvis::load(h.prefs());
  // The note body size, restored before anything can draw a note -- the lock
  // screen paints one before the notes app has ever been opened.
  uint32_t ns = h.prefs().getUInt("nt_size", 0);
  if (ns > 2) ns = 0;
  nmd::setBody((TSize)(TS_MED + ns));
  _where = Where::Hub;
  _hub.goHome();
  release();
}

namespace {
// Which folder an app lives in, for putting the hub back where an app's HUB
// button should land. Looked up rather than remembered, so resume and a tap
// agree.
int folderOf(bool game, int idx) {
  for (int g = 0; g < applist::NGROUPS; g++)
    for (int i = 0; i < applist::GROUPS[g].n; i++)
      if (applist::GROUPS[g].items[i].game == game && applist::GROUPS[g].items[i].idx == idx)
        return g;
  return 0;
}
}  // namespace

void Toybox::open(bool game, int idx, bool paint) {
  _settings.leave();  // an access point must not outlive the screen that ran it
  if (!build(game, idx)) {
    _where = Where::Hub;  // out of memory: better to bounce back than draw nothing
    return;
  }
  _where = Where::App;
  // Remembered for the DOWN hold on home, and read back before writing: NVS
  // pages wear, and reopening the same app every day should cost nothing.
  Preferences& p = _host->prefs();
  const bool sameG = p.isKey("last_g") && p.getBool("last_g") == game;
  const bool sameI = p.isKey("last_i") && (int)p.getInt("last_i") == idx;
  if (!sameG) p.putBool("last_g", game);
  if (!sameI) p.putInt("last_i", idx);
  _active->enter(*_host);
  // Entering an app is text and hairlines replacing the home screen: partial,
  // unless a photo wallpaper is under the ink (a photograph ghosts through a
  // partial in a way hairlines never do), or entry itself borrowed the SD bus
  // -- the readers' shelves list the card, which re-initialises the panel and
  // leaves nothing valid to diff against.
  //
  // Not painted at all when the caller says so: a recents cover heading
  // straight into openDirect() would show the shelf for one refresh only to
  // replace it with the book's loading face -- a screen nobody asked for at
  // 1.7 s a showing. The caller then owns the first paint on BOTH paths,
  // opened and not-found alike.
  if (paint) _host->refresh(_active->enterTouchesCard() || wallimg::have());
}

void Toybox::openSettings() {
  release();
  _settings.enter();
  _where = Where::Settings;
  // Partial: settings is text on white, the kindest case the panel has. The
  // one exception is a wallpapered home behind it -- a photograph ghosts
  // through a partial in a way hairlines never do, so that entry stays clean.
  _host->refresh(wallimg::have());
}

bool Toybox::resumeLast() {
  Preferences& p = _host->prefs();
  if (!p.isKey("last_i") || !p.isKey("last_g")) return false;
  const bool game = p.getBool("last_g");
  const int idx = p.getInt("last_i");
  // An app hidden since it was last used stays hidden. Resuming it would
  // reopen a door the settings screen says is closed.
  if (!appvis::visible(game, idx)) return false;
  _hub.openFolder(folderOf(game, idx));  // so HUB from the app lands sensibly
  open(game, idx);
  return true;
}

bool Toybox::canCarryOn() const {
  recents::Entry rec[recents::MAX];
  if (recents::list(_host->prefs(), rec) > 0) return true;
  Preferences& p = _host->prefs();
  if (!p.isKey("last_i") || !p.isKey("last_g")) return false;
  return appvis::visible(p.getBool("last_g"), (int)p.getInt("last_i"));
}

bool Toybox::carryOnReading() {
  recents::Entry rec[recents::MAX];
  const int n = recents::list(_host->prefs(), rec);
  if (n == 0) return resumeLast();  // nothing read yet: the old behaviour
  const int idx = rec[0].kind == recents::KIND_EPUB ? 10 : 9;
  _hub.openFolder(folderOf(false, idx));
  // Unpainted: carrying on reading should land on the book's own face, not
  // the shelf it happens to live on. If the book is gone (card out, file
  // renamed), the list is painted here instead, and it says so.
  open(false, idx, false);
  if (_active && !_active->openDirect(rec[0].file)) _host->refresh(true);
  return true;
}

void Toybox::openPairPicture() {
  // Notes is tool 6. If it will not build -- which here means the allocation
  // failed -- staying in settings is better than a blank screen, and the row
  // the user just tapped is still on the panel to try again.
  if (!build(false, 6)) return;
  _where = Where::App;
  _hub.openFolder(2);  // notes lives in STUDY; HUB from here should land there
  _active->enter(*_host);
  _active->openPairing();
  _host->refresh(true);
}

void Toybox::goHub() {
  release();
  _settings.leave();
  _where = Where::Hub;
  // Same rule as the drawer: partial onto a plain home, full when the photo
  // wallpaper is about to be under the ink.
  _host->refresh(wallimg::have());
}

void Toybox::render(ToolsCanvas& c) {
  switch (_where) {
    case Where::Hub: _hub.render(*_host, c); break;
    case Where::Settings: _settings.render(*_host, c); break;
    default:
      if (_active) _active->render(c);
      break;
  }
}

void Toybox::onTap(int x, int y) {
  if (_where == Where::App) {
    if (_active) _active->onTap(x, y);
    return;
  }

  if (_where == Where::Settings) {
    if (_host->isBackTap(x, y)) {
      // Settings gets first refusal: it has a second page, and back there means
      // up one rather than out.
      const bool wasFiles = _settings.onBusPage();
      if (_settings.back()) {
        _host->beep(1);
        // Backing up a settings page is text over text -- partial -- except
        // out of the files page, whose card session re-initialised the panel.
        _host->refresh(wasFiles);
        return;
      }
      appvis::save(_host->prefs());
      _host->beep(1);
      goHub();
      return;
    }
    if (_settings.onTap(*_host, x, y)) _host->refresh(false);
    return;
  }

  const HubScreen::Tap t = _hub.hit(*_host, x, y);
  if (t.kind == HubScreen::Tap::None) return;
  _host->beep(1);
  switch (t.kind) {
    case HubScreen::Tap::Exit:
      _host->exit();  // hands the screen back to whatever Toybox is a guest in
      return;
    case HubScreen::Tap::Settings:
      // Guest hosts only: the standalone device reaches this by holding UP.
      _settings.enter();
      _where = Where::Settings;
      _host->refresh(wallimg::have());
      return;
    case HubScreen::Tap::Folder:
      // The drawer over the home screen, and home again after it: partial on
      // a plain device, full when a photograph is under (or about to be under)
      // the ink -- pictures are what partials visibly smear.
      _hub.openFolder(t.idx);
      _host->refresh(wallimg::have());
      return;
    case HubScreen::Tap::Home:
      _hub.goHome();
      _host->refresh(wallimg::have());
      return;
    case HubScreen::Tap::PagePrev:
    case HubScreen::Tap::PageNext:
      // Turning a drawer's page is tiles over tiles: partial is plenty.
      _hub.folderPageStep(t.kind == HubScreen::Tap::PageNext ? 1 : -1);
      _host->refresh(false);
      return;
    case HubScreen::Tap::Recent: {
      // A recently-read cover: open its reader, then the book itself, which
      // resumes at the saved position the way it always does -- with no stop
      // at the shelf on the way: the cover was the promise, the loading face
      // keeps it. If the book is gone (card out, file renamed) the reader's
      // list is painted instead, which says so better than a beep would.
      recents::Entry rec[recents::MAX];
      const int n = recents::list(_host->prefs(), rec);
      if (t.idx >= n) return;
      open(false, rec[t.idx].kind == recents::KIND_EPUB ? 10 : 9, false);
      if (_active && !_active->openDirect(rec[t.idx].file)) _host->refresh(true);
      return;
    }
    default:
      open(t.game, t.idx);
      return;
  }
}

bool Toybox::onButton(SideBtn b) {
  return (_where == Where::App && _active) ? _active->onButton(b) : false;
}

void Toybox::onSwipe(int dx, int dy) {
  if (_where == Where::App && _active) _active->onSwipe(dx, dy);
}

void Toybox::tick() {
  if (_where == Where::App && _active) {
    _active->tick();
    return;
  }
  // Settings wants ticks too, for the one page that runs a web server. A true
  // answer means the screen changed while nobody was tapping -- a phone
  // joined, or the card was handed back -- so it has to be repainted here.
  if (_where == Where::Settings && _settings.tick(*_host)) _host->refresh(true);
}

bool Toybox::wantsTick() const {
  if (_where == Where::Settings) return _settings.wantsTick();
  return _where == Where::App && _active && _active->wantsTick();
}
