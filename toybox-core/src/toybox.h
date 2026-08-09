// The whole Toybox as one object: the hub, the settings screen, and the
// thirteen apps behind them.
//
// This is the seam that lets the same code be a firmware on its own and an
// activity inside somebody else's. A host supplies a canvas, a Preferences, a
// beeper and a way to push a frame; Toybox supplies everything above that and
// never learns which of the two it is running in.
#pragma once
#include "applist.h"
#include "appvis.h"
#include "chrome.h"
#include "hub.h"
#include "settings.h"

class Toybox {
 public:
  // The host hands itself over once. Nothing is drawn yet -- the host paints
  // when it is ready, by calling render() through its own refresh path.
  void begin(ToolsHost& h);

  void render(ToolsCanvas& c);
  void onTap(int x, int y);
  void onSwipe(int dx, int dy);
  // A side button press, offered to the open app. False means nothing wanted
  // it, and the caller can do whatever it would have done otherwise.
  bool onButton(SideBtn b);
  void tick();
  bool wantsTick() const;

  // Where the back button goes. Apps reach this through ToolsHost::goHub(),
  // which every host forwards here.
  void goHub();

  // Opens an app by its place in the two icon tables, the same coordinates the
  // hub and the visibility mask use.
  void open(bool game, int idx);
  // Opens the notes tool at its pairing screen. Settings uses this for the lock
  // screen picture; see ToolsHost::goPairPicture.
  void openPairPicture();

  const char* activeTitle() const { return _active ? _active->title() : "TOYBOX"; }

  // True when nothing is open over the hub. A host with a physical back button
  // needs this to decide between "leave this app" and "leave Toybox"; the
  // standalone firmware, which has nowhere to leave to, ignores it.
  bool atHub() const { return _where == Where::Hub; }

#ifdef TOYBOX_HOST
  // What the preview harness needs to assert on: which app is up, and whether
  // the settings screen is showing.
  ToolApp* hostActive() { return _active; }
  bool hostIsGame() const { return _activeIsGame; }
  int hostIdx() const { return _activeIdx; }
  bool hostInApp() const { return _where == Where::App; }
  bool hostInSettings() const { return _where == Where::Settings; }
#endif

 private:
  enum class Where : uint8_t { Hub, Settings, App };

  // Apps are built when opened and destroyed when left, so an idle Toybox costs
  // a pointer. That matters most in the reader, where most of the time the
  // device is a book and none of this should be resident.
  bool build(bool game, int idx);
  void release();

  ToolsHost* _host = nullptr;
  Where _where = Where::Hub;
  ToolApp* _active = nullptr;
  bool _activeIsGame = false;
  int _activeIdx = -1;
  HubScreen _hub;
  SettingsScreen _settings;
};

extern Toybox toybox;
