# Updates over Wi-Fi

From v2 a Sticky updates itself. The cable is needed once, for the move from
v1, and after that only to rescue a device that will not boot.

## The flash layout

```
nvs        0x009000   20 KB   settings, saved pages, Wi-Fi     (where v1 had it)
otadata    0x00e000    8 KB   which slot boots
ota_0      0x010000  2.94 MB  app slot
ota_1      0x300000  2.94 MB  app slot
base_font  0x5f0000  1.12 MB  CJK base set (TFP2), always written
zh_font    0x710000  2.81 MB  optional packs (TFP2)
ko_font    0x9e0000  0.88 MB
ja_font    0xac0000  0.56 MB
spiffs     0xb50000  4.69 MB  LittleFS: notes, decks, pictures (where v1 had it)
```

NVS and LittleFS did not move, which is why a v1 device keeps everything on
the move to v2. `tools/check_layout.py` fails a release that would write over
either one, and runs inside `tools/make_image.sh`.

The app fits a slot because the common Chinese, Japanese and Korean sets left
it for `base_font`, and the language packs are compressed (TFP2,
`tools/tfp2.py`). See `docs/LANGUAGES.md`.

## Two ways in

**From the site.** Settings > Update & about > CHECK FOR UPDATE. The Sticky
joins the saved home Wi-Fi, reads `firmware/version.json` from
`https://z4b333.github.io/sticky-toybox/` (NVS `ota_src` overrides), and
offers a version newer than its own. INSTALL writes the base font pack first
if it changed, then streams the app into the slot that is not running,
hashing as it goes. Only a file whose SHA-256 matches `version.json` and whose
image checksum holds becomes the boot slot. 30 % battery or USB power.

**From a computer.** FROM A COMPUTER opens http://sticky.local (and the IP,
for phones that cannot resolve `.local`) on the home Wi-Fi. The page asks for
the six digits on the panel (new each time, five wrong tries lock it) and
takes a `.bin`. The file must carry the Toybox-for-Sticky marker
(`toybox_app_marker` in `src/ota.cpp`) or it is refused before it can boot.

The GitHub page cannot push to the Sticky: an https page may not fetch from
http on the local network. It only links to sticky.local.

## Rollback

A new build boots on trial. `otadev::bootGuard()` counts every boot of the
trial slot; the second pass through `loop()` confirms it
(`otadev::confirm()`). Three boots that never get that far, and the Sticky
switches back to the slot it came from, and says so under Update & about.

A wake from deep sleep is a boot, and it gets to `loop()`, so a device
picked up and put down again does not count against a good build.

## Releasing

`tools/make_image.sh` writes `version.json` with an `ota` block:

```json
"ota": {
  "app":  { "path": "firmware/toybox-app.bin", "bytes": ..., "sha256": "..." },
  "base": { "path": "firmware/base_font.tfp",  "bytes": ..., "sha256": "..." },
  "minBattery": 30
}
```

Devices see a release as soon as Pages serves the new `version.json`.

## Tests

- `test/host/ota_test.cpp`: version order, `version.json` reading (including
  hostile paths and truncation), the battery gate, pairing and lock-out,
  the rollback rule.
- `host_preview.cpp`: every screen of the update page against a fake device,
  damaged font packs refused, the base pack loaded like the device does.
