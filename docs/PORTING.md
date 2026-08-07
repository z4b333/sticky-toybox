# Porting Toybox to another firmware

All apps and screens live in `toybox-core/`, which has no display, touch or
board code. It draws through two small interfaces. To embed Toybox in
another firmware you implement those interfaces and forward input to it.
There are two working examples: the standalone firmware in this repository
(`src/sticky_host.*`) and the CrossPoint Reader port, which is about 110
lines.

## The two interfaces

Both are defined in `toybox-core/src/tools/tools_ui.h`.

**ToolsCanvas** is drawing: width and height, clear, rectangles, lines,
circles, and text at four sizes (`TS_SMALL` to `TS_HUGE`). Layouts assume a
480×800 portrait canvas. The optional `textPad()` method reports the empty
space around glyphs so large single characters can be centered on their ink.
If you don't implement it, centering falls back to the text box, which is
fine for most fonts.

**ToolsHost** is everything else:

- `canvas()` and `prefs()` (an Arduino `Preferences` object)
- `refresh(full)` – re-render the current screen and push it to the panel.
  `full` means the slow flicker refresh that clears ghosting.
- `beep(kind)` – 0 tap, 1 confirm, 2 reject, 3 alarm
- `topBar(title)`, `isBackTap()`, `isHelpTap()`, `contentTop()`
- `canExit()` and `exit()` – whether there is something above Toybox to
  return to. The hub only shows a BACK button when there is.
- `soundOn()` / `setSoundOn()` – sound settings belong to the host

## Wiring

Add the library and drive the singleton:

```
lib_deps = Toybox=symlink://toybox-core
```

```cpp
toybox.begin(myHost);        // once, draws nothing yet
myHost.refresh(true);        // first paint

// in your loop:
toybox.onTap(x, y);
toybox.onSwipe(dx, dy);      // 2048 uses swipes
if (toybox.wantsTick()) toybox.tick();
toybox.atHub();              // for a physical back button
```

Apps are created when opened and destroyed when closed, so Toybox costs
almost nothing while idle. That matters when the host firmware is usually
busy being something else, like an e-reader.

## What carries over and what doesn't

Settings and files use standard ESP32 APIs (NVS and LittleFS), so they work
on any ESP32 host.

Fonts do not carry over. The core asks the canvas to draw text, and the
host draws it with whatever fonts it has. The multilingual support in this
repository lives in the standalone firmware's font tables, not in the core.
A new host needs its own fonts for any script beyond ASCII. The text rules
themselves (UTF-8 handling, line breaking, minimum sizes) are in the core
and work everywhere.

Input is touch only. A buttons-only device would need a cursor or focus
system, which doesn't exist yet.

## Verifying a port

The preview harness in `test/host/` renders every screen and runs checks
without hardware. It can also be built with a different font set
(`-DTOYBOX_CP_FONTS`) to check that layouts survive other font metrics.
If your host's fonts differ a lot, bake them into the harness format
(`tools/make_fonts_cp.py` shows how) and review the rendered screens before
flashing.
