#pragma once
#include <string>
// The draw sampler's render task times ([debug] draw_sampler, steamvr/PERFORMANCE.md): hooks on the game's
// render task runner and on the render graph's drain, which time each call while a capture runs and cost
// one flag test a call otherwise. Put in when the first capture is asked for, never before.
namespace rendertasks {
// Puts in what is not in yet. Returns why the tasks, their names or the drain are not timed, for the
// capture's report; empty when all three are. Says what it did in the frame log, once per change.
std::string Install();
}
