#pragma once
#include "../steamvr/character_save.h"
namespace D2RL {struct PluginContext;}

// The current character's save (name, class, level), read from disk; nothing in the game is touched (steamvr/character_save.h).
namespace charsave {
void Init(const D2RL::PluginContext* ctx);   // finds this mod's save folder
// The newest save of this hero class (any class when -1), looked up again at most every 1.5 s and parsed again only when the
// file changes. Any thread.
framevr::charsave::Status Read(int heroClass);
// How many saves of this class are at this level (Quick cast: a character is known only when exactly one is).
int SameLevel(int heroClass,int level);
}
