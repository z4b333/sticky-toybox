#!/bin/sh
# Build the single flashable image the web installer serves.
#
# The web installer writes one file to offset 0, so bootloader, partition table,
# the OTA data block and the app have to be merged into it first. The flash size
# must say 16MB: it is written into the image header, and a bootloader that
# thinks the chip is smaller than the partition table will not start.
set -e
cd "$(dirname "$0")/.."

pio run

BUILD=.pio/build/sticky
BOOT_APP0=$(find "$HOME/.platformio" -name boot_app0.bin | head -1)

python -m esptool --chip esp32s3 merge-bin \
  -o docs/firmware/toybox-full.bin \
  --flash-mode qio --flash-size 16MB \
  0x0     "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0xe000  "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin"

# The same app on its own, for anyone flashing over USB with the partitions
# already in place.
cp "$BUILD/firmware.bin" prebuilt/toybox_full.bin

ls -l docs/firmware/toybox-full.bin prebuilt/toybox_full.bin
