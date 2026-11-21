// Updates over Wi-Fi, the firmware's half. The decisions are in
// toybox-core/src/tools/ota_core.h; this file only moves bytes.
//
//   * From the site: version.json names the app and the CJK base pack with
//     their sizes and SHA-256. The app streams into the slot that is not
//     running, hashed on the way; only a file that matches becomes the boot
//     slot. The base pack, when it changed, is written first, the same way.
//   * From a computer on the home network: http://toybox.local, a page that
//     takes a .bin after the six digits on the panel. The file must carry this
//     build's marker (it is a Toybox image for a Sticky), and the image's own
//     checksum must hold, before it can become the boot slot.
//   * Either way the new build boots ON TRIAL: three boots that never reach
//     the home screen and the device goes back to the slot it came from.
#pragma once
#include <Preferences.h>

#include "tools/ota_core.h"

namespace otadev {

// The one instance, made on first use. nullptr if this image has no OTA
// partitions (an old partition table): the settings page then shows the
// version alone and says why.
ota::Device* device(Preferences& prefs);

// setup(), before anything that could crash a bad build: counts the boot of a
// build on trial and, out of tries, switches back and restarts.
void bootGuard(Preferences& prefs);

// The home screen has been drawn: the build on trial works. Clears the trial.
void confirm(Preferences& prefs);

}  // namespace otadev
