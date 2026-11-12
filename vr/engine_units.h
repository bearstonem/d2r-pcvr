#pragma once
// Units the game draws as its own with no unit of the game's behind them: the dungeon ceiling's slabs and rim
// walls ([ceiling] game_ceiling, shared/game_ceiling.h). Adapted from natpoh/diablo2-vr-port's
// vr/engine/engitem.cpp (MIT, see THIRD_PARTY.md); vr/engine_units.cpp says how.
#include <cstdint>

namespace engineunits {

// What vrcam knows of the area's ceiling now, asked on the game's render thread each frame its registry may be
// changed. Live: the settings, the hero, the area's floor and height.
struct CeilingIn {
    bool ticked = false;       // [ceiling] game_ceiling
    bool f4 = false;           // VR first person (F4) with the VR camera on
    bool wanted = false;       // [ceiling] enabled, in a world, the area on [ceiling] game_ceiling_biomes with a kind, no menu
    bool heroOk = false;       // the hero's place is known
    bool wallTick = false;     // [ceiling] game_ceiling_wall
    int kind = -1;             // the area's kind (gameceiling::kKinds), -1 none
    float hero[3]{};           // the hero's feet, world units
    float floorY = 0.0f;       // the area's floor (the lowest the hero stood since it changed; the shader's too)
    float height = 0.0f;       // the slab's top over the floor
    float tileWorld = 40.0f;   // world units a tile (4 x [ceiling] scale)
    float reach = 0.0f;        // the rim wall's reach down from the slab's top
    const char* heightFrom = "";   // where the height came from, for the log
};
using LogFn = void (*)(const char*);
using CeilingFn = void (*)(CeilingIn*);

// vrcam's: its log, the ceiling's inputs and the mod's .mpq folder (PluginContext::modDirectory; null: no
// mod - nothing is placed). Before Install.
void Init(LogFn log, CeilingFn ceiling, const wchar_t* modDirectory);
// The registry's set-mutable hook and the world's teardown hook, once wanted (vrcam: the VR platform with
// [ceiling] game_ceiling and [ceiling] enabled - a desktop session never hooks). Every frame until in, or given
// up after a minute of the game's bytes not being there.
void Install(bool wanted);
// vrcam's area changes: a world, or a menu (after a game is left nothing is placed until a menu was seen).
void WorldSeen(bool inWorld);

// For the shader's hand-over (gameceiling::Drawer): ready - both hooks in and the game's functions where
// 3.3.93787 has them (until then, and for good when not, the shader draws); a kind given up on (its files
// not made or refused, its placings run out); a slab laid this frame.
bool Ready();
bool GivenUp(int kind);
bool CeilingShown();

}  // namespace engineunits
