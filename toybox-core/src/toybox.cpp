#include "toybox.h"

#include "game2048.h"
#include "nonogram.h"
#include "tools/tool_coin.h"
#include "tools/tool_dice.h"
#include "tools/tool_flash.h"
#include "tools/tool_note.h"
#include "tools/tool_picker.h"
#include "tools/tool_random.h"
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
      default: _active = new SudokuTool(); break;
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
  _where = Where::Hub;
  release();
}

void Toybox::open(bool game, int idx) {
  if (!build(game, idx)) {
    _where = Where::Hub;  // out of memory: better to bounce back than draw nothing
    return;
  }
  _where = Where::App;
  _active->enter(*_host);
  _host->refresh(true);
}

void Toybox::goHub() {
  release();
  _where = Where::Hub;
  _host->refresh(true);
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
  if (t.kind == HubScreen::Tap::Exit) {
    _host->exit();  // hands the screen back to whatever Toybox is a guest in
    return;
  }
  if (t.kind == HubScreen::Tap::Settings) {
    _settings.enter();
    _where = Where::Settings;
    _host->refresh(true);
    return;
  }
  open(t.game, t.idx);
}

void Toybox::onSwipe(int dx, int dy) {
  if (_where == Where::App && _active) _active->onSwipe(dx, dy);
}

void Toybox::tick() {
  if (_where == Where::App && _active) _active->tick();
}

bool Toybox::wantsTick() const {
  return _where == Where::App && _active && _active->wantsTick();
}
