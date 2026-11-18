#pragma once
// Render distance: how many rings of D2 rooms around the hero the 3D layer
// (D2Prism LevelTranslationLayer) activates and draws. Public entry points are
// d2rcam::SetRenderRadius / GetRenderRadius; this is the internal half.

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace d2rcam::drawdist {

// Installs the two room hooks; false until both are in (call again later).
bool Install(const D2RL::PluginContext* ctx);
bool Installed();
// Retry optional bindings even after the room hooks have installed. Safe to
// poll from the existing settings worker; attempts are limited to twice/second.
void Refresh();
struct RuntimeStatus {
    bool hooks=false,tiler=false,append=false,modelRadius=false,recalculate=false;
    int rings=1,expansion=0,furthestRoom=0,roomsAdded=0,roomsLoaded=0,tileAttempts=0;
    float activeModelRadius=-1;
    uint64_t lastExpansionMs=0;
};
RuntimeStatus ReadStatus();

void SetRadius(int rooms);   // clamped to [1, 8]
int  Radius();

int RoomsAddedLastTick();
int RoomsLoadedTotal();

// Model visibility radius in world units (the game's 150). False if this game
// build does not match.
bool  SetModelRadius(float units);
float ModelRadius();   // -1 when unknown

// Big models kept drawn past the model radius by their size, out to this many world units
// (steamvr/far_objects.h; 0 = off). Hooks the game's test the first time it is on; false until
// that hook is in (it is tried again at most twice a second).
bool     SetBigRadius(float units);
float    BigRadius();
uint32_t TakeBigKept();   // models kept past the model radius by their size since the last call

}   // namespace d2rcam::drawdist
