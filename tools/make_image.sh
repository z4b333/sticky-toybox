#!/bin/sh
# Build the single flashable image the web installer serves.
#
# The web installer writes one file to offset 0, so bootloader, partition table,
# the OTA data block and the app have to be merged into it first. The flash size
# must say 16MB: it is written into the image header, and a bootloader that
# thinks the chip is smaller than the partition table will not start.
#
# The mode must say dio. This module's octal PSRAM shares the extra SPI data
# lines, so a header asking for QIO makes the ROM read the bootloader as
# garbage and watchdog-reset for ever. See platformio.ini.
set -e
cd "$(dirname "$0")/.."

pio run

# Nothing ships with a stack frame big enough to restart the device. Two builds
# already did; the check costs a second and reads what the compiler wrote down.
python tools/check_stack.py

BUILD=.pio/build/sticky
BOOT_APP0=$(find "$HOME/.platformio" -name boot_app0.bin | head -1)

python -m esptool --chip esp32s3 merge-bin \
  -o docs/firmware/toybox-full.bin \
  --flash-mode dio --flash-size 16MB \
  0x0     "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0xe000  "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin" \
  0x5f0000 docs/firmware/base_font.tfp

# The app has to fit an OTA slot (partitions_toybox.csv: ota_0 = 0x2f0000).
# platformio.ini's maximum_size already fails the build past it; this says why.
APP_MAX=$((0x2f0000))
APP_NOW=$(stat -c %s "$BUILD/firmware.bin")
if [ "$APP_NOW" -gt "$APP_MAX" ]; then
  echo "app is $APP_NOW bytes, the OTA slot holds $APP_MAX" >&2
  exit 1
fi

# The same app on its own, for anyone flashing over USB with the partitions
# already in place.
cp "$BUILD/firmware.bin" prebuilt/toybox_full.bin

# The pieces, individually, for the installer page. Writing them at their own
# offsets -- instead of one merged image whose 0xFF padding rolls over
# everything between them -- is what lets INSTALL leave NVS (settings, saved
# pages, stats) alone, and lets UPDATE write nothing but the app.
cp "$BUILD/bootloader.bin" docs/firmware/toybox-boot.bin
cp "$BUILD/partitions.bin" docs/firmware/toybox-parts.bin
cp "$BOOT_APP0"            docs/firmware/toybox-ota0.bin
cp "$BUILD/firmware.bin"   docs/firmware/toybox-app.bin

# Nothing the installer writes may land on settings (NVS) or notes (LittleFS).
# After the copies above: it checks the files the page will actually serve.
python tools/check_layout.py

# What the flasher page prints beside the install button. The version is the
# same string the firmware carries, so what the page says it is about to write
# and what the service screen says afterwards can be compared directly.
VERSION=$(git describe --tags --always 2>/dev/null || echo unknown)
# The cache-busting key. Not a bare commit hash: at a tag that changes on every
# rebuild, so the image committed at the tag would never match a rebuild of it.
# `describe` already carries the hash for anything that is not a tag.
COMMIT="$VERSION"
DATE=$(git log -1 --format=%cd --date=format:'%d %b %Y' 2>/dev/null || echo '')
BYTES=$(stat -c %s docs/firmware/toybox-full.bin)
SHA=$(sha256sum docs/firmware/toybox-full.bin | cut -c1-16)
APP_BYTES=$(stat -c %s docs/firmware/toybox-app.bin)
# The full hashes are what a Sticky checks an update against over Wi-Fi
# (src/ota.h): it streams the file into the spare slot, hashes it on the way,
# and refuses to boot it if the two disagree. The base font pack travels the
# same way when it changes, which is rarely.
APP_SHA=$(sha256sum docs/firmware/toybox-app.bin | cut -d' ' -f1)
BASE_BYTES=$(stat -c %s docs/firmware/base_font.tfp)
BASE_SHA=$(sha256sum docs/firmware/base_font.tfp | cut -d' ' -f1)
cat > docs/firmware/version.json <<JSON
{
  "version": "$VERSION",
  "commit": "$COMMIT",
  "date": "$DATE",
  "bytes": $BYTES,
  "sha256": "$SHA",
  "appBytes": $APP_BYTES,
  "ota": {
    "app": { "path": "firmware/toybox-app.bin", "bytes": $APP_BYTES, "sha256": "$APP_SHA" },
    "base": { "path": "firmware/base_font.tfp", "bytes": $BASE_BYTES, "sha256": "$BASE_SHA" },
    "minBattery": 20
  }
}
JSON
cat docs/firmware/version.json

ls -l docs/firmware/toybox-full.bin prebuilt/toybox_full.bin
