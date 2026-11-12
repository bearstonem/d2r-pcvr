#pragma once
#include <cmath>
#include "controller_labels.h"

namespace framevr::haptics {
// Most pulses leave the frequency to the runtime: OpenXR's XR_FREQUENCY_UNSPECIFIED, which is 0.
// Valve's driver contract for haptic events (OpenVR Driver API, "Haptic Events") is that a driver
// plays nothing at a frequency of 0 or less, and developers have reported that SteamVR passes
// OpenXR's 0 to Valve Index controllers as it is: Quest (Touch) controllers vibrate at 0, an
// Index stays still. So a pulse with no frequency of its own goes to an Index at IndexHz, the
// rate reported to feel like a proper vibration on them. A set frequency is sent as it is, and
// every other controller still gets the runtime's choice.
constexpr float IndexHz=170;
inline float OutputFrequency(float requested,controllers::Layout layout) {
    if(std::isfinite(requested)&&requested>0)return requested;
    return layout==controllers::Layout::Index?IndexHz:0.f;   // 0: XR_FREQUENCY_UNSPECIFIED
}
}
