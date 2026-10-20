// Head-relative sound for VR (D2R 3.3.93787, FMOD Ex linked into D2R.exe).
//
// Stock D2R gives every positioned (3D) voice to its FMOD4SoundEngine wrapper
// as an offset from the hero in the game camera's fixed screen frame - x
// right, y down the screen, z 0 - and leaves FMOD's listener where it starts
// (at the origin, facing +Z, up +Y: the game sets it once, all null). So FMOD
// pans by screen x only, always in the camera's frame: in VR a monster you
// look away from still sounds where the flat camera would have put it, and
// turning the head or the stick changes nothing.
//
// Two hooks fix that, both through the loader (byte-checked):
//  - FMOD4SoundEngine::Set3DPosition (157D210): the screen offset becomes a
//    direction on FMOD's ground plane - right = x, ahead = forward_sign *
//    depth_scale * y (screen-up is ahead; depth undoes the isometric squash)
//    - at the voice's own distance, so loudness and falloff are the game's.
//  - FMOD Ex SystemI::set3DListenerAttributes (1591A00): the game's one call
//    hands over FMOD's system; a 10 ms thread then points listener 0 along
//    the view yaw (head + stick + mouse), so FMOD re-pans every playing 3D
//    voice on its own - looping ones the game never re-sends included.
// The game's 2D voices (music, ambience, UI - and, for now, monsters' attack
// cries, whose sound rows are 2D) and the hero's own sounds (at the hero) are
// untouched and stay centred.
//
// Stereo only: left/right by where you look plus distance falloff. A Windows
// "Mono audio" setting (Accessibility) folds it to the centre before any
// headset stream, Quest Link/Air Link/Virtual Desktop included.
//
// [audio] head_follow (1), listener_turn (1), forward_sign (-1), depth_scale (2).
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "sigscan.h"

namespace spatialaudio {
namespace {
void (*g_log)(const char*) = nullptr;
float (*g_viewYaw)() = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

struct V3 { float x, y, z; };
std::atomic<bool> g_follow{true}, g_listenerTurn{true};
std::atomic<float> g_fwdSign{-1.0f}, g_depth{2.0f};

// FMOD Ex SystemI::set3DListenerAttributes(this, listener, pos, vel, forward, up):
// listener index checked against 4 (else FMOD_ERR_INVALID_PARAM 0x25),
// listeners 0x70 apart, forward at +0x91C8; it only stores floats and sets
// the listener's moved flags.
inline constexpr uint64_t ListenerRva = 0x1591a00;
inline constexpr uint8_t ListenerSig[] = {0x48,0x83,0xec,0x48,0x4d,0x8b,0xd8,0x4c,0x8b,0xd1,0x83,0xfa,0x04,0x0f,0x87,0x0a,0x03,0x00,0x00,0x48};
using ListenerFn = int (*)(void*, int, const V3*, const V3*, const V3*, const V3*);
ListenerFn g_listenerOrig = nullptr;
bool g_listenerIn = false;
std::atomic<void*> g_system{nullptr};
std::atomic<float> g_sentYaw{1000.0f};

// FMOD4SoundEngine::Set3DPosition(engine, AudioInstance**, x, y, z) - the
// "Attempted to set 3D position on invalid AudioInstance" setter; called from
// the game's per-frame emitter update (8398F0, +839E89) for 3D voices only.
inline constexpr uint64_t SetPosRva = 0x157d210;
inline constexpr uint8_t SetPosSig[] = {0x48,0x83,0xec,0x28,0x48,0x8b,0x02,0x4c,0x8b,0xc9,0x48,0x85,0xc0,0x74,0x7a,0x48,0x8b,0x40,0x08,0x48,0x85,0xc0,0x74,0x0a,0x8b,0x40,0x08,0x85,0xc0,0x0f,0x94,0xc1,0xeb,0x05,0xb9,0x01,0x00,0x00,0x00,0x84,0xc9,0x75,0x5e,0x48,0x8b,0x02,0x48,0x8b,0x48,0x10,0x48,0x85,0xc9,0x75,0x1a,0x49,0x8b,0x81,0xc8,0x00,0x00,0x00,0x48,0x85,0xc0,0x74,0x6d,0x4c,0x8b,0x0d,0x6e,0xab};
using SetPosFn = bool (*)(void*, void*, float, float, float);
SetPosFn g_posOrig = nullptr;
bool g_posIn = false, g_announced = false;

int Listener(void* sys, int i, const V3* pos, const V3* vel, const V3* fwd, const V3* up) {
    if (i == 0 && sys) g_system.store(sys);
    return g_listenerOrig(sys, i, pos, vel, fwd, up);
}

bool SetPos(void* engine, void* instance, float x, float y, float z) {
    const float dist = std::sqrt(x * x + y * y + z * z);
    if (g_follow.load() && std::isfinite(dist) && dist > 0.001f) {
        const float right = x, ahead = g_fwdSign.load() * g_depth.load() * y;
        const float n = std::sqrt(right * right + ahead * ahead);
        if (n > 0.0001f) return g_posOrig(engine, instance, right * dist / n, 0.0f, ahead * dist / n);
    }
    return g_posOrig(engine, instance, x, y, z);
}

// Listener 0 along the view yaw (+ = turned right; FMOD is left-handed: +X
// right, +Y up, +Z ahead), sent when it moved half a degree.
void TurnListener() {
    void* sys = g_system.load();
    if (!sys || !g_listenerOrig) return;
    float yaw = g_follow.load() && g_listenerTurn.load() && g_viewYaw ? g_viewYaw() : 0.0f;
    if (!std::isfinite(yaw)) yaw = 0.0f;
    if (std::fabs(std::remainder(yaw - g_sentYaw.load(), 360.0f)) < 0.5f) return;
    const float t = yaw * 0.0174532925f;
    const V3 fwd{std::sin(t), 0.0f, std::cos(t)}, up{0.0f, 1.0f, 0.0f};
    g_listenerOrig(sys, 0, nullptr, nullptr, &fwd, &up);
    g_sentYaw.store(yaw);
}
DWORD WINAPI TurnThread(void*) {
    for (;;) { Sleep(10); TurnListener(); }
}
}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
void SetViewYaw(float (*yaw)()) { g_viewYaw = yaw; }
void Configure(bool headFollow, bool listenerTurn, float forwardSign, float depthScale) {
    g_follow.store(headFollow);
    g_listenerTurn.store(listenerTurn);
    g_fwdSign.store(forwardSign < 0 ? -1.0f : 1.0f);
    g_depth.store(std::isfinite(depthScale) && depthScale > 0.1f ? depthScale : 2.0f);
}
// Safe to call repeatedly: hooks go in once their code is there.
void Poll() {
    if (!g_listenerIn) {
        g_listenerIn = d2rsig::Hook(ListenerRva, ListenerSig, sizeof ListenerSig, reinterpret_cast<void*>(&Listener), reinterpret_cast<void**>(&g_listenerOrig));
        if (g_listenerIn) { if (HANDLE h = CreateThread(nullptr, 0, &TurnThread, nullptr, 0, nullptr)) CloseHandle(h); }
    }
    if (!g_posIn) g_posIn = d2rsig::Hook(SetPosRva, SetPosSig, sizeof SetPosSig, reinterpret_cast<void*>(&SetPos), reinterpret_cast<void**>(&g_posOrig));
    if (g_listenerIn && g_posIn && !g_announced) {
        g_announced = true;
        char b[200];
        snprintf(b, sizeof b, "spatial audio: sounds follow the head (head_follow %d, listener_turn %d, forward_sign %.0f, depth_scale %.2f)",
                 g_follow.load() ? 1 : 0, g_listenerTurn.load() ? 1 : 0, g_fwdSign.load(), g_depth.load());
        Log(b);
    }
}
}  // namespace spatialaudio
