# Porting Toybox into another firmware

`toybox-core/` is the whole product — hub, settings, four games, Battleship,
eight tools, Unicode text rules — and it includes no display, touch or board
headers. A host firmware supplies two small interfaces and owns the loop; the
core never learns which firmware it is running in. The standalone Sticky
firmware (`src/sticky_host.*`) and the CrossPoint Reader port
(`src/activities/toybox/` in that repository, ~110 lines) are the two
existing hosts, and either is a usable template.

## What a host provides

**`ToolsCanvas`** (`toybox-core/src/tools/tools_ui.h`) — pixels and text:
`width/height`, `clear`, `fillRect/drawRect/drawLine/fillCircle/drawCircle`,
`text/textWidth/textHeight` for the four `TSize` buckets, and optionally
`textPad()` reporting the blank space glyphs leave in their box, which the
core uses to centre big single characters on their ink (leave it zero and
centring falls back to the box). Layouts assume a **480×800 portrait**
canvas; the core's `chrome.h` hardcodes `SCREEN_W/H` to match.

**`ToolsHost`** — everything else: `canvas()`, `prefs()` (an ESP32 Arduino
`Preferences`), `refresh(full)` (re-render the active screen and push it to
the panel; full = the slow, ghost-clearing refresh — the Sticky host calls
`toybox.render()` then `displayFull/Partial`), `beep(kind)` (0 tap, 1
confirm, 2 reject, 3 alarm), `topBar(title, withHelp)` and the matching
`isBackTap/isHelpTap`, `contentTop()`, `canExit()/exit()` (whether there is
anything above Toybox to leave to — the hub only draws its BACK button when
there is), and `soundOn/setSoundOn` (sound belongs to the host; a reader has
its own idea about it).

## Wiring it up

1. Consume the directory as a PlatformIO library:
   `lib_deps = Toybox=symlink://toybox-core` (the CrossPoint port points the
   symlink at a checkout of this repository).
2. Implement the two interfaces.
3. Drive the singleton:

```cpp
toybox.begin(myHost);          // once; nothing is drawn yet
myHost.refresh(true);          // first paint

// in the loop:
toybox.onTap(x, y);            // logical portrait coordinates
toybox.onSwipe(dx, dy);        // 2048 uses swipes
if (toybox.wantsTick()) toybox.tick();   // timers, pairing servers
toybox.atHub();                // physical back button: leave app vs leave Toybox
```

Apps are constructed when opened and destroyed when left; an idle Toybox
costs a pointer, which matters in a host that is usually busy being
something else.

## What travels and what doesn't

State (NVS namespace `"toybox"`, LittleFS files for notes and decks) follows
the ESP32 Arduino APIs, so any ESP32 host inherits it. Fonts do **not**
travel: the core asks the canvas to draw text and the host answers with
whatever faces it has. The standalone firmware's non-Latin support
(`src/fonts_intl.*`, see LANGUAGES.md) lives in its canvas, not in the core —
a new host needs its own answer for scripts beyond what its fonts cover
(CrossPoint currently has none for Thai). The core's Unicode *rules* — UTF-8
walking, line breaking, per-script size floors, name sanitising — do travel,
since they live in `toybox-core`.

Input is tap-first. A buttons-only host would need a focus/cursor layer that
does not exist yet; the hooks to build it against are `hub.h`'s tile table
and each app's tap handler.

## Keeping a port honest

The preview harness renders every screen and runs behavioral guards without
hardware (see README). Its `-DTOYBOX_CP_FONTS` build exists precisely for
ports: it re-renders all screens with the CrossPoint fonts (up to twice the
line height) and re-runs every guard, which is how this codebase keeps one
layout working under two hosts' metrics. A new port with very different
fonts deserves the same treatment: bake its faces into the harness format
(`tools/make_fonts_cp.py` is the worked example) and look at the pictures
before flashing anything.
