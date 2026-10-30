#pragma once
#include <cstdint>
namespace D2RL {struct PluginContext;}
// Reset Skills & Stats (steamvr/respec_button.h): the game's own reset, through
// a Token of Absolution minted for it and used at once (owner 2026-10-30: free
// and unlimited). Proven by the DEV token probe the same day: createItem makes
// toa and its native use refunds stats and skills. Offline games only.
namespace respec {
enum class State:int {Idle,Busy,Done,NoRoom,Failed};
void Init(const D2RL::PluginContext*);
// Offline, with the item and thread services: the button may show.
bool Available();
// Any thread. True when a reset started; Status() reads Busy from then on.
bool Request();
State Status();
// UI thread, gamestate's 200 ms read: hands the minted token to the native use
// once the client holds it, then waits until it is gone.
void Poll();
void PollAt(uint64_t now);   // Poll at a given GetTickCount64 (tests: the timeouts)
}
