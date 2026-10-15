#pragma once
#include <windows.h>
#include <xinput.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace framevr::haptics {
// Read by the game's XInput hook without taking the XR/render mutex. Losing
// focus, disabling motion controls or stopping input relinquishes ownership.
class RumbleLease {
    std::atomic<uint64_t> at{0};
public:
    void Update(uint64_t now,bool owns){at.store(owns?now:0);}
    bool Owns(uint64_t now) const {const auto stamp=at.load();return stamp&&now>=stamp&&now-stamp<250;}
};
// XInput's two motors encode low/high-frequency gamepad rumble, not VR hands.
// Do not let that second feedback path overwrite OpenXR's per-hand pulses,
// including with zero-valued native stop requests. Desktop calls pass through.
class GamepadRumble {
    std::mutex mutex;
    bool legacyActive=false;
public:
    template<class Forward> DWORD Send(bool vrOwns,DWORD index,XINPUT_VIBRATION* vibration,Forward&& forward) {
        if(index!=0||!vibration)return forward(index,vibration);
        std::lock_guard lock(mutex);
        if(vrOwns) {
            if(legacyActive) {
                XINPUT_VIBRATION stop{};forward(index,&stop);legacyActive=false;
            }
            return ERROR_SUCCESS; // the mod supplies virtual pad 0 and its haptics
        }
        const DWORD result=forward(index,vibration);
        legacyActive=result==ERROR_SUCCESS&&(vibration->wLeftMotorSpeed||vibration->wRightMotorSpeed);
        return result;
    }
};
}
