#pragma once
// [game_patches]: small changes to the game's own render code, each on for every install (no setting
// in VR Settings or the shipped d2r_pcvr.ini; [game_patches] <name>=0 written there by hand takes one out, to tell
// a fault from a patch) (steamvr/PERFORMANCE.md "Render task patches"; the bytes each one needs are in
// game_patch_signatures.h, what it writes in steamvr/game_patch_plan.h). Three more, on for every install
// in the same way (PERFORMANCE.md "Once per pair"): selection_walk is a patch like
// the others; shadows_once and cull_once are hooks that act in the second eye pass of a pair (EyePass below).
// present_once (off unless written by hand; PERFORMANCE.md "One present a pair") holds back the first pass's desktop Present.
//
// A patch goes in, and comes out, only in BetweenFrames: on the thread that draws, before a frame's first eye
// pass, when the game's render tasks of the frame before have all ended and no thread is in the code that
// is changed. The game's version is never asked: the bytes at each place are compared with what this mod
// knows, and a patch whose bytes differ is left out and says so once.
namespace gamepatches {

struct Wanted {
    bool transparentFilter = true;    // [game_patches] transparent_filter
    bool profilerAccounting = true;   // [game_patches] profiler_accounting
    bool drawDebugNames = true;       // [game_patches] draw_debug_names
    // On for every install since the November 10 headset run (steamvr/PERFORMANCE.md "Once per pair").
    bool selectionWalk = true;       // [game_patches] selection_walk
    bool shadowsOnce = true;         // [game_patches] shadows_once
    bool cullOnce = true;            // [game_patches] cull_once
    // Off unless the key is written by hand: not measured and not seen in a headset yet (PERFORMANCE.md "One present a pair").
    bool presentOnce = false;        // [game_patches] present_once
};

// From the settings, on any thread, as often as they are read. The first call says what is wanted in the log.
void Configure(const Wanted& wanted);

// On the draw thread, at the top of a frame's draw (never inside an eye pass or between the two of a pair).
// Costs one flag test once everything wanted is in or refused.
void BetweenFrames();

// Which draw of the game's frame begins now: the first or the second eye pass of a pair drawn from one game
// frame, or a draw that stands alone (a menu, a loading screen, an eye by turns, a pass that is not drawn
// twice). On the draw thread, before each of a pair's two draws and Alone after the second; shadows_once and
// cull_once act in a Second pass only, and only on what the First of the same pair left. Alone at any moment
// is safe: the rest of that draw runs as the game does.
enum class Pass : unsigned char { Alone, First, Second };
void EyePass(Pass pass);
// After EyePass(First), before its draw: this first pass presents after all (no second draw follows it).
void KeepPresent();

}  // namespace gamepatches
