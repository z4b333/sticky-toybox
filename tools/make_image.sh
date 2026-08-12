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
  0x10000 "$BUILD/firmware.bin"

# The same app on its own, for anyone flashing over USB with the partitions
# already in place.
cp "$BUILD/firmware.bin" prebuilt/toybox_full.bin

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
cat > docs/firmware/version.json <<JSON
{
  "version": "$VERSION",
  "commit": "$COMMIT",
  "date": "$DATE",
  "bytes": $BYTES,
  "sha256": "$SHA"
}
JSON
cat docs/firmware/version.json

ls -l docs/firmware/toybox-full.bin prebuilt/toybox_full.bin
