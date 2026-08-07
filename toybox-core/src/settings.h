// Device settings: which apps the hub shows, sound, and two things you can put
// back the way they were.
#pragma once
#include "chrome.h"

class SettingsScreen {
 public:
  void enter();  // called each time it is opened, to clear any armed button
  void render(ToolsHost& host, ToolsCanvas& c);
  // Returns true if the tap changed something and the screen needs repainting;
  // leaving is handled by the shell, which owns the back button.
  bool onTap(ToolsHost& host, int x, int y);

 private:
  // Erasing every score on the device deserves a second tap, not a second
  // screen: the button asks, and any other tap takes the question away.
  bool _armed = false;
  const char* _note = nullptr;
};
