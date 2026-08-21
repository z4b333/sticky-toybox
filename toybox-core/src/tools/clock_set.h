// Where the device learns what time it is.
//
// There is no network time here and no way to type a time into an e-paper
// keyboard that anyone would enjoy, so the clock comes from a phone: the phone
// already knows the time and the timezone, and it is standing right there. Any
// screen that pairs with a phone can hand the number over through this hook,
// and the firmware — which is the only half that knows whether there is an RTC
// at all — decides what to do with it.
//
// The number is milliseconds since the Unix epoch ALREADY SHIFTED into the
// phone's local time, because the RTC stores wall-clock digits and nothing
// else: no zone, no offset. That is also why travelling makes the clock wrong
// until a phone tells it again.
#pragma once
#include <stdint.h>

namespace clockset {

using Fn = void (*)(int64_t localEpochMs);
inline Fn g_set = nullptr;

// Called once at boot by a firmware that has a clock to set. A host without
// one leaves it null and every screen below quietly does nothing.
inline void hook(Fn f) { g_set = f; }
inline bool available() { return g_set != nullptr; }

// True when there was somewhere to put it. False is not an error worth a
// screen of its own -- it means this build has no clock.
inline bool apply(int64_t localEpochMs) {
  if (!g_set) return false;
  g_set(localEpochMs);
  return true;
}

}  // namespace clockset
