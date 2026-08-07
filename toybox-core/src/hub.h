// The home screen: every app that has not been hidden, in one grid.
#pragma once
#include "chrome.h"

class HubScreen {
 public:
  struct Tap {
    enum Kind : uint8_t { None, App, Settings, Exit } kind = None;
    bool game = false;
    int idx = 0;
  };

  void render(ToolsHost& host, ToolsCanvas& c);
  Tap hit(const ToolsHost& host, int x, int y) const;
};
