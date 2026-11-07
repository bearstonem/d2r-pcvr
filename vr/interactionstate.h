#pragma once
#include "../steamvr/interactions.h"
namespace D2RL {struct PluginContext;}
namespace interactionstate {
void Init(const D2RL::PluginContext*);
void Capture(); // SDK UI thread, after equipment refresh
framevr::interaction::Snapshot Read();
int CurrentLevel(uint64_t session,uint64_t now); // Fresh location only; avoids copying inventory/skills in target queries.
// The druid shapeshift form the player is in (framevr::shapeshift: 1 Werewolf, 2 Werebear;
// states 139/140, sampled with the hireling every 200 ms); 0 none, not known or stale.
int ShapeForm(uint64_t session,uint64_t now);
framevr::interaction::Reply Status();
void Request(const framevr::interaction::Request&);
// Glyph casting (glyph_cast.h): put a skill on the right trigger's record, on the UI thread; apart from
// Request's one slot so a cast never waits on the wheel. False while the previous one is scheduled.
bool Park(uint64_t session,int skill,int owner);
// The game's run / walk toggle (UI command 0x23; Movement speed and the combat walk, run_speed.h
// GaitSync): its own slot on the UI thread, apart from Request's, so a toggle never holds back a
// potion, holster draw, merc toss or quick-use item, never blocks motion input while it is in flight
// and never buzzes. Refused (on the UI thread) with a menu open or a stale snapshot. False while
// the previous one is still scheduled.
bool ToggleRunWalk(uint64_t session);
void Cancel();
}
