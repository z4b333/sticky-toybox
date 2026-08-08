#pragma once
#include "service_ui.h"

namespace svc {

// Was UP held as the device came up? Checked once, early, before anything that
// might be misconfigured has had to work.
bool requested();

// Read the saved corrections, and hand them to the display and touch drivers.
// Called on every boot, not just in service mode.
Config load();
void apply(const Config& c);
void save(const Config& c);

// The screen itself. Never returns: it ends in a restart.
void run();

}  // namespace svc
