#pragma once
// Whirlwind on, steered while a whirl is under way (steamvr/whirl_turn.h): the game's side. Its two hooks - the
// server's step of a unit in a skill's move and the client's step of a unit - go in once Whirlwind on has been
// turned on in this run of the game (framevr::WhirlOnUsed), every byte they touch checked first; a build without
// them leaves whirls as they were (each turns where the next one starts).
namespace whirlturnnative {
void Install();
}
