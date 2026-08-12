// The screen you hold UP at power-on to reach.
//
// Every other screen in this firmware assumes the display and the touch
// controller are mapped correctly. Those mappings are guesses taken from vendor
// demo code, and they are coupled: touch is transformed to match the display,
// so a mirrored panel puts every tap somewhere it was not. If that guess is
// wrong on a given board, the device cannot be used at all, and the only remedy
// is editing two files and reflashing.
//
// This screen exists so that is not the remedy. It is driven entirely by the
// three physical buttons, so it works with touch completely broken, and the
// corrections it writes are the complete set: two flips for the panel and three
// for the digitizer reach every orientation either of them can be in.
//
// The drawing lives here, apart from the button loop in service.cpp, so the
// preview harness can render it without a device.
#pragma once
#include "tools/tools_ui.h"

namespace svc {

// What the board answered when it was asked, at boot, before any of this drew.
struct Report {
  bool panelOk = false;
  bool touchOk = false;
  uint8_t touchAddr = 0;
  bool gauge = false, rtc = false, sht = false, imu = false;
  // Raw accelerometer, and the rotation the mapping currently derives from it.
  // Printed because that mapping is a guess: turn the device through four
  // positions, read four lines, and the right answer falls out of the numbers
  // instead of out of an argument.
  int accelX = 0, accelY = 0, orientation = -1;
  int battMv = -1;
  int fontFaces = 0;
  uint32_t psramKb = 0;
  // Free heap, and the largest single block in it. The second number is the
  // one that matters: the reader needs 48 KB in one run, and a heap with
  // plenty free and nothing contiguous is what a book failing to open looks
  // like from the inside.
  uint32_t heapKb = 0, blockKb = 0, psramFreeKb = 0;
  // The SD probe, which only runs when somebody asks for it on that row.
  bool sdTried = false, sdMounted = false, sdPanelOk = false;
  uint64_t sdSizeMb = 0;
  int sdFiles = 0;
  uint32_t sdKbPerSec = 0;
  const char* sdFailedAt = "";
  const char* version = "";
};

struct Config {
  bool flipX = true, flipY = true;        // panel; see service.cpp load()
  bool swapXY = true, tFlipX = true, tFlipY = true;  // digitizer
};

// Ordered as a diagnosis reads: what the screen looks like, then how touch
// maps onto it, then the two active tests (touch, panel patterns), then the
// probes and the way out.
enum Row : int {
  ROW_FLIP_X,
  ROW_FLIP_Y,
  ROW_SWAP,
  ROW_TFX,
  ROW_TFY,
  ROW_TEST,
  ROW_PATTERN,
  ROW_SD,
  ROW_SAVE,
  ROWS
};

inline const char* rowLabel(int i) {
  switch (i) {
    case ROW_FLIP_X: return "SCREEN MIRRORED LEFT/RIGHT";
    case ROW_FLIP_Y: return "SCREEN UPSIDE DOWN";
    case ROW_SWAP: return "TOUCH: SWAP X AND Y";
    case ROW_TFX: return "TOUCH: FLIP LEFT/RIGHT";
    case ROW_TFY: return "TOUCH: FLIP UP/DOWN";
    case ROW_TEST: return "TOUCH TEST";
    case ROW_PATTERN: return "TEST PATTERNS";
    case ROW_SD: return "TEST THE SD CARD";
    default: return "SAVE AND RESTART";
  }
}

inline bool rowValue(const Config& c, int i) {
  switch (i) {
    case ROW_FLIP_X: return c.flipX;
    case ROW_FLIP_Y: return c.flipY;
    case ROW_SWAP: return c.swapXY;
    case ROW_TFX: return c.tFlipX;
    default: return c.tFlipY;
  }
}

inline void toggleRow(Config& c, int i) {
  switch (i) {
    case ROW_FLIP_X: c.flipX = !c.flipX; break;
    case ROW_FLIP_Y: c.flipY = !c.flipY; break;
    case ROW_SWAP: c.swapXY = !c.swapXY; break;
    case ROW_TFX: c.tFlipX = !c.tFlipX; break;
    case ROW_TFY: c.tFlipY = !c.tFlipY; break;
    default: break;
  }
}

// Nine rows now that the pattern test is one of them: 44-px rows at a 48
// step, so the list still ends above the band where the SD result is written.
// These rows are driven by the buttons, not a finger, so they can be short.
inline constexpr int LIST_Y = 276, ROW_H = 44, ROW_GAP = 4;
inline constexpr int MARGIN = 20;

// `cross` is the last touch this screen saw, in the coordinates the rest of the
// firmware would have received. Drawing it where the finger actually was is the
// whole test: if the cross lands under the finger, the mapping is right.
inline void render(ToolsCanvas& c, const Report& r, const Config& cfg, int sel, bool haveCross,
                   int cx, int cy) {
  const int W = c.width();
  char buf[64];

  c.fillRect(0, 0, W, 44, true);
  c.textTrackedCentered(W / 2, 12, "SERVICE", TS_LARGE, false, true, 3);

  // --- what answered -----------------------------------------------------
  int y = 60;
  // The sensors read as four digits in a fixed order, which is why the key is
  // in the heading: four labelled pairs did not fit beside anything else, and
  // this line is read as "are they all 1".
  c.text(MARGIN, y, "AT BOOT   (sensors: gauge rtc temp tilt)", TS_MED, true);
  y += 28;
  c.drawLine(MARGIN, y, W - MARGIN, y, 1, true);
  y += 10;

  // First, because if this one says no then everything under it is being read
  // off a screen that should not be showing anything.
  // Five lines, not eight. This block grew a line every time something new
  // was worth knowing at boot, and by the time PSRAM arrived it had pushed
  // the version stamp into the first correction row and run the heap figures
  // off the right edge. What each line is FOR decides what shares it: the two
  // chips that must answer or nothing works, the four that only matter when
  // one is missing, and the two memories, which are only ever read together.
  snprintf(buf, sizeof(buf), "panel    %s      touch  %s", r.panelOk ? "ok" : "NO ANSWER",
           r.touchOk ? "ok" : "NOT FOUND");
  c.text(MARGIN, y, buf, TS_MED, true);
  y += 26;

  snprintf(buf, sizeof(buf), "sensors  %d%d%d%d   fonts %d", r.gauge ? 1 : 0, r.rtc ? 1 : 0,
           r.sht ? 1 : 0, r.imu ? 1 : 0, r.fontFaces);
  c.text(MARGIN, y, buf, TS_MED, true);
  y += 26;

  if (r.battMv >= 0)
    snprintf(buf, sizeof(buf), "battery  %d.%02d V", r.battMv / 1000, (r.battMv % 1000) / 10);
  else
    snprintf(buf, sizeof(buf), "battery  no gauge");
  c.text(MARGIN, y, buf, TS_MED, true);
  y += 26;

  // Free and biggest are different questions -- a heap can have plenty of the
  // first and none of the second, which is what four bugs on this device
  // turned out to be -- so both stay, abbreviated rather than dropped.
  snprintf(buf, sizeof(buf), "memory   %lu free / %lu big      psram %lu of %lu",
           (unsigned long)r.heapKb, (unsigned long)r.blockKb,
           (unsigned long)r.psramFreeKb / 1024, (unsigned long)r.psramKb / 1024);
  c.text(MARGIN, y, buf, TS_MED, true);
  y += 26;

  if (r.imu) {
    snprintf(buf, sizeof(buf), "tilt     x %d   y %d   -> turn %d", r.accelX, r.accelY,
             r.orientation);
    c.text(MARGIN, y, buf, TS_MED, true);
    y += 26;
  }

  // The font count moved up onto the sensors line; this is where it used to
  // be its own.
  c.text(MARGIN, y, r.version, TS_SMALL, true);

  // --- the corrections ---------------------------------------------------
  for (int i = 0; i < ROWS; i++) {
    const int ry = LIST_Y + i * (ROW_H + ROW_GAP);
    const bool on = (i == sel);
    if (on)
      c.fillRect(MARGIN, ry, W - 2 * MARGIN, ROW_H, true);
    else
      c.drawRect(MARGIN, ry, W - 2 * MARGIN, ROW_H, 1, true);

    const int ty = ry + (ROW_H - c.textHeight(TS_MED)) / 2;
    c.text(MARGIN + 14, ty, rowLabel(i), TS_MED, !on);
    if (i < ROW_TEST)
      c.text(W - MARGIN - 50, ty, rowValue(cfg, i) ? "YES" : "no", TS_MED, !on, true);
  }

  // --- the test ----------------------------------------------------------
  // Only while the test row is selected: the cross has to be able to land
  // anywhere, including on top of the list, or it cannot prove a corner.
  if (sel == ROW_TEST) {
    if (haveCross) {
      c.drawLine(cx - 26, cy, cx + 26, cy, 3, true);
      c.drawLine(cx, cy - 26, cx, cy + 26, 3, true);
      c.drawCircle(cx, cy, 16, true, 2);
      snprintf(buf, sizeof(buf), "%d, %d", cx, cy);
      c.text(MARGIN, 754, buf, TS_MED, true);
    } else {
      c.textCentered(W / 2, 754, "touch anywhere - the cross should land under your finger",
                     TS_SMALL, true);
    }
    return;
  }

  // What the pattern row does, while it is selected. The patterns themselves
  // are full-screen and drawn by service.cpp; the last one runs the grey
  // waveform, which makes this the one place grey can be tested with no card,
  // no book, and no phone.
  if (sel == ROW_PATTERN) {
    c.textCentered(W / 2, 740, "OK steps through 8 full-screen patterns", TS_SMALL, true);
    c.textCentered(W / 2, 766, "the last is 4-level grey -- UP or DOWN leaves", TS_SMALL, true);
    return;
  }

  // The SD result, while its row is selected. Nothing in the firmware uses the
  // card yet -- this exists to answer the one question a book format depends on,
  // and to answer it on a screen rather than in an opinion.
  if (sel == ROW_SD) {
    if (!r.sdTried) {
      c.textCentered(W / 2, 740, "OK mounts a card and reads from it", TS_SMALL, true);
      c.textCentered(W / 2, 766, "it shares the display's bus -- that is the test", TS_SMALL,
                     true);
    } else if (!r.sdMounted) {
      snprintf(buf, sizeof(buf), "no card: failed at %s", r.sdFailedAt);
      c.textCentered(W / 2, 752, buf, TS_MED, true);
    } else {
      snprintf(buf, sizeof(buf), "%lu MB, %d files, %lu KB/s", (unsigned long)r.sdSizeMb,
               r.sdFiles, (unsigned long)r.sdKbPerSec);
      c.textCentered(W / 2, 740, buf, TS_MED, true);
      c.textCentered(W / 2, 770, r.sdPanelOk ? "read ok, panel still answers"
                                             : "PANEL STOPPED ANSWERING",
                     TS_MED, true, !r.sdPanelOk);
    }
    return;
  }

  c.textCentered(W / 2, 748, "UP / DOWN move     OK changes", TS_SMALL, true);
  c.textCentered(W / 2, 772, "hold OK to save and restart", TS_SMALL, true);
}

}  // namespace svc
