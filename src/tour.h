// The four GETTING STARTED cards, shown once per firmware version, right
// after the welcome screen's which-way-up check.
//
// The welcome screen earns its place by catching a mirrored panel; these four
// earn theirs by explaining the three things the new home screen no longer
// says out loud: the dock icons have no labels, the two side buttons do their
// work by being held, and the panel keeps a picture when the power goes. The
// marks drawn here are the same functions the home screen draws, so the tour
// can never teach a glyph the device does not use.
#pragma once
#include "hub_marks.h"
#include "tools/decor.h"
#include "tools/tools_ui.h"

namespace tour {

inline constexpr int CARDS = 4;
inline constexpr TRect NEXT_BTN{60, 716, 360, 58};

inline void header(ToolsCanvas& c, int n, const char* title) {
  const int W = c.width();
  c.text(18, 20, "GETTING STARTED", TS_SMALL, true, true);
  char buf[12];
  snprintf(buf, sizeof(buf), "%d of %d", n + 1, CARDS);
  c.text(W - 18 - c.textWidth(buf, TS_SMALL), 20, buf, TS_SMALL, true);
  c.fillRect(18, 52, W - 36, 2, true);
  c.textTrackedCentered(W / 2, 78, title, TS_LARGE, true, true, 2);
}

inline void footer(ToolsCanvas& c, int n) {
  const int W = c.width();
  for (int i = 0; i < CARDS; i++) {
    const int cx = W / 2 - 36 + i * 24;
    if (i == n)
      c.fillCircle(cx, 690, 6, true);
    else
      c.drawCircle(cx, 690, 6, 2, true);
  }
  c.button(NEXT_BTN.x, NEXT_BTN.y, NEXT_BTN.w, NEXT_BTN.h,
           n < CARDS - 1 ? "NEXT" : "START USING IT", false, TS_LARGE);
}

// Card 1: the dock, and the fact that it is the only touch surface.
inline void card1(ToolsCanvas& c) {
  header(c, 0, "THE DOCK");
  struct Row { const char* name; const char* sub; };
  static constexpr Row R[3] = {{"PLAY", "six games, one at a time"},
                               {"DECIDE", "coin, dice, shuffle, picker"},
                               {"STUDY", "flashcards, notes, timer"}};
  int y = 180;
  for (int i = 0; i < 3; i++) {
    hubmarks::folder(c, i, 64, y + 18, 56, true);
    c.text(120, y, R[i].name, TS_LARGE, true, true);
    c.text(120, y + 40, R[i].sub, TS_MED, true);
    y += 112;
  }
  c.text(18, 540, "The row along the bottom is the only", TS_MED, true);
  c.text(18, 572, "thing on the home screen you can touch.", TS_MED, true);
  c.text(18, 604, "Tap a folder and it opens a page.", TS_MED, true);
}

// Card 2: the three physical buttons down the right edge of the case.
inline void card2(ToolsCanvas& c) {
  header(c, 1, "THE BUTTONS");
  const int dx0 = 40, dy0 = 158, dw = 152, dh = 300;
  tdraw::roundRect(c, dx0, dy0, dw, dh, 14, 3, true);
  c.drawRect(dx0 + 12, dy0 + 12, dw - 24, dh - 24, 2, true);
  struct Row { int by; const char* verb; const char* what; };
  static const Row R[3] = {{dy0 + 40, "press", "on and off"},
                           {dy0 + 128, "hold", "settings"},
                           {dy0 + 204, "hold", "carry on"}};
  for (int i = 0; i < 3; i++) {
    const int by = R[i].by;
    tdraw::roundRect(c, dx0 + dw, by, 12, 44, 4, 0, true);  // the button itself
    c.fillRect(dx0 + dw, by, 12, 44, true);
    c.drawLine(dx0 + dw + 18, by + 22, dx0 + dw + 40, by + 22, 3, true);
    if (i == 0)
      hubmarks::power(c, dx0 + dw + 62, by + 22, 15, true);
    else if (i == 1)
      decor::gear(c, dx0 + dw + 62, by + 22, 15, 8, true);
    else
      hubmarks::resume(c, dx0 + dw + 62, by + 22, 15, true);
    c.text(dx0 + dw + 88, by - 2, R[i].verb, TS_MED, true, true);
    c.text(dx0 + dw + 88, by + 24, R[i].what, TS_MED, true);
  }
  c.text(18, 486, "Carry on takes about a second's hold.", TS_MED, true);
  c.text(18, 518, "Settings wants a full three seconds, so", TS_MED, true);
  c.text(18, 550, "a pocket cannot open it by accident.", TS_MED, true);
  c.text(18, 594, "The top button switches the device on", TS_MED, true);
  c.text(18, 626, "and off: press to wake it, hold it for", TS_MED, true);
  c.text(18, 658, "two seconds to put it away.", TS_MED, true);
}

// Card 3: what settings holds, and the screen that fixes a wrong-way panel.
inline void card3(ToolsCanvas& c) {
  header(c, 2, "SETTINGS");
  c.text(18, 168, "What you can change", TS_LARGE, true, true);
  static const char* K[3] = {"the picture behind the home screen",
                             "what shows when you switch off",
                             "beep volume, and which apps show"};
  for (int i = 0; i < 3; i++) {
    c.fillCircle(30, 224 + i * 36 + 10, 4, true);
    c.text(46, 224 + i * 36, K[i], TS_MED, true);
  }
  c.text(18, 356, "If something looks wrong", TS_LARGE, true, true);
  c.text(18, 402, "Hold either side button while the device", TS_MED, true);
  c.text(18, 434, "is switching on. That opens the service", TS_MED, true);
  c.text(18, 466, "screen, which works even when touch", TS_MED, true);
  c.text(18, 498, "does not, and can turn the image the", TS_MED, true);
  c.text(18, 530, "right way up again.", TS_MED, true);
  tdraw::roundRect(c, 18, 576, c.width() - 36, 84, 10, 2, true);
  c.text(34, 592, "It cannot break anything. Nothing on", TS_SMALL, true);
  c.text(34, 620, "it is saved until you choose to save.", TS_SMALL, true);
}

// Card 4: e-paper keeps its last frame, and what that means at power-off.
inline void card4(ToolsCanvas& c) {
  header(c, 3, "SWITCHING OFF");
  // A miniature of the goodbye card in a frame, standing in for "the panel
  // keeps showing something".
  const int fx = 36, fy = 176, fw = 150, fh = 200;
  c.drawRect(fx, fy, fw, fh, 2, true);
  decor::ornament(c, fx + fw / 2, fy + 46, fw - 40, true);
  c.textTrackedCentered(fx + fw / 2, fy + 66, "TOYBOX", TS_SMALL, true, true, 2);
  c.textCentered(fx + fw / 2, fy + 96, "good", TS_SMALL, true);
  c.textCentered(fx + fw / 2, fy + 118, "night", TS_SMALL, true);
  decor::ornament(c, fx + fw / 2, fy + 156, fw - 40, true);

  c.text(210, 182, "The screen keeps", TS_MED, true, true);
  c.text(210, 214, "its last picture,", TS_MED, true, true);
  c.text(210, 246, "with no power", TS_MED, true);
  c.text(210, 278, "at all. Switching", TS_MED, true);
  c.text(210, 310, "off leaves some-", TS_MED, true);
  c.text(210, 342, "thing on the glass.", TS_MED, true);

  c.text(18, 420, "You choose what, in settings:", TS_MED, true);
  static const char* K[4] = {"a note you pinned", "a goodbye card",
                             "one of your pictures", "nothing at all"};
  for (int i = 0; i < 4; i++) {
    c.fillCircle(30, 466 + i * 36 + 10, 4, true);
    c.text(46, 466 + i * 36, K[i], TS_MED, true);
  }
  c.text(18, 622, "A short press of the power button", TS_MED, true);
  c.text(18, 654, "brings it back where you left it.", TS_MED, true);
}

inline void render(ToolsCanvas& c, int n) {
  switch (n) {
    case 0: card1(c); break;
    case 1: card2(c); break;
    case 2: card3(c); break;
    default: card4(c); break;
  }
  footer(c, n);
}

}  // namespace tour
