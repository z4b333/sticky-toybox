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

// Returns whether the time actually landed somewhere -- written to the chip
// AND read back from it. A hook that reports success it did not have produces
// a screen that says "the clock is set" over a device that has no idea what
// time it is, which is worse than a screen that admits it failed.
using Fn = bool (*)(int64_t localEpochMs);
inline Fn g_set = nullptr;

// Called once at boot by a firmware that has a clock to set. A host without
// one leaves it null and every screen below quietly does nothing.
inline void hook(Fn f) { g_set = f; }
inline bool available() { return g_set != nullptr; }

// True when the time was taken AND read back. False means either that this
// build has no clock at all or that the one it has refused the write; the
// screens say so rather than claiming a success nobody verified.
inline bool apply(int64_t localEpochMs) { return g_set && g_set(localEpochMs); }

}  // namespace clockset
