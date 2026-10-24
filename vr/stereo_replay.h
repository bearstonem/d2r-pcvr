// One CPU pass, two pictures (VR Settings > Performance > Eyes drawn per frame; Replay was its third choice, since removed).
// Adapted from natpoh/diablo2-vr-port 8bd1d3b vr/vrcam.cpp namespaces camfix
// and replay (MIT, Copyright (c) 2026 BodyWalkVR).
//
// The game draws the left eye as usual while every direct and compute command
// list it submits is noted. The camera constant buffers those lists bind are
// redirected into slots of our own GPU buffer, holding the left eye's values
// for the left pass and the right eye's (steamvr/replay_camera.h) for the
// replay. The game's present at the end of the left pass is held back: the left
// eye's effects run on its back buffer and it goes to the headset (the
// transport's capture). Then the noted lists run again on the same queues, the
// right eye's camera in the slots, into the same back buffer and depth; the
// right eye's effects run, and one real Present takes it to the headset too.
// Whatever stops it (an area that has not settled, a menu, a missing hook) and
// the frame is drawn in two passes as before.
#pragma once

#include <cstdint>
#include <reshade.hpp>

namespace stereoreplay {

struct Settings {
    int marks = 3;       // [stereo] replay_marks: how the lists go again (steamvr/replay_groups.h)
    int ahead = 2;       // [stereo] replay_ahead: replays the CPU may run ahead of (1 or 2)
    int positions = 2;   // [stereo] replay_positions (steamvr/replay_camera.h)
    bool prev = true;    // [stereo] replay_prev
};
// At the plugin's start, after MinHook's, before the game makes its device:
// the upload buffers are known from their creation.
void Register(void (*log)(const char*), const Settings& settings);
void Configure(const Settings& settings);

// From the camera, each time the left eye's view and projection are built.
void SetLeftView(const float view[16], float shiftWorld);
void SetProjection(float projSx, float projShift);
// From the effects: the runtime and the right eye's technique.
void SetEffects(reshade::api::effect_runtime* rt, reshade::api::effect_technique right);

// Once a pair, before it begins: may this pair be replayed? Steps the settle.
bool Wanted(bool inWorld);
void AreaChanged();   // the game set a biome
void BeginLeft();     // before the left pass
// After the left pass: the right eye from it. False: draw the right pass.
bool RightFromLeft();

bool Capturing();     // inside a left pass that will be replayed
bool RightEffects();  // the right eye's effects are being rendered now

// biomeSets: the game's biome sets; restartArea/restartLong/outOfWorld: pairs
// that started the settle again for a biome set, a game frame of over 150 ms,
// or no world; settling: the settle's pairs so far (of 240).
struct Stats { uint32_t replayed = 0, fallbacks = 0, slots = 0, overflow = 0, lateSlots = 0, lists = 0, calls = 0, folded = 0, positions = 0;
               uint32_t biomeSets = 0, restartArea = 0, restartLong = 0, outOfWorld = 0; int settling = 0, settleOf = 0; double submitMs = 0.0; };
Stats TakeStats();    // since the last call

}  // namespace stereoreplay
