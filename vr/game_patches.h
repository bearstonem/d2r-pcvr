#pragma once
// [game_patches]: small changes to the game's own render code, each with its own switch in d2r_pcvr.ini and
// each off unless asked for (steamvr/PERFORMANCE.md "Render task patches"; the bytes each one needs are in
// game_patch_signatures.h, what it writes in steamvr/game_patch_plan.h).
//
// A patch goes in, and comes out, only in BetweenFrames: on the thread that draws, before a frame's first eye
// pass, when the game's render tasks of the frame before have all ended and no thread is in the code that
// is changed. The game's version is never asked: the bytes at each place are compared with what this mod
// knows, and a patch whose bytes differ is left out and says so once.
namespace gamepatches {

struct Wanted {
    bool transparentFilter = false;    // [game_patches] transparent_filter
};

// From the settings, on any thread, as often as they are read. The first call says what is wanted in the log.
void Configure(const Wanted& wanted);

// On the draw thread, at the top of a frame's draw (never inside an eye pass or between the two of a pair).
// Costs one flag test once everything wanted is in or refused.
void BetweenFrames();

}  // namespace gamepatches
