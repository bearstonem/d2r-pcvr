#pragma once
// Army X menu: what the mod's own files hand to it (vr/army_x_menu.cpp, the cross and its orders; vr/army_x_hunt.cpp, the
// summons' hunt of a marked monster), and its rules. One call a site; each handler returns at once while the menu is on no
// button, so a player who never assigns it plays exactly as before.
#include <cstddef>
#include <cstdint>

namespace framevr::commander { struct SummonArmyCoordinator; struct ArmyUnitSample; struct ArmyMoveOrder; }

namespace framevr::armyseam {
// The rules, as tuned in the headset. Every reader sees a rule's Off (0) while the menu is on no button (Gated).
namespace cfg {
struct Rules {
    float hunt = 2;             // a marked monster is hunted: 0 no, 1 melee summons only, 2 melee and ranged
    float defendRadius = 7.5f;  // Hold here and Move and hold: a monster within this many sub-tiles of the spot is the summons' target (0: none)
    bool huntLeash = true;      // a hunter is not pulled back by the game's own leash
    float leashReach = 0;       // ... within this many sub-tiles of the hero (0: no limit)
    float run = 2;              // summons run: 0 no, 1 the wolves, 2 every summon whose game data has a run
    bool runApproach = true;    // a hunting runner's own walks to a place go out as runs
    float farRetryS = 1;        // a hunter standing idle beside its mark is sent again after this long (0: never)
    bool farRefuse = true;      // a hunter's move the companion gate refuses is answered as failed, so the game thinks again
    bool hitBack = true;        // a summon hit with no mark set goes for its attacker
    float hitBackReach = 25;    // ... within this many sub-tiles of its spot (0: no limit)
    float hitBackForS = 4;      // ... and lets it go after this long with no new hit and no fight (0: never taken)
};
inline Rules rules;
}  // namespace cfg

// transport SetupStatic, once a session before the static image's upload: the menu's pictures painted into its own regions.
struct StaticAtlasFrame { uint8_t* pixels; int width, height; bool srgb; void (*say)(const char*); };
// transport Input, around interactions.Step: the hand wheel's Edit Slots lists "Army X menu" (engineIn: interaction::Input*),
// and its pick becomes the button's bind.
struct WheelInputFrame { int wheelHand; void* engineIn; };
struct WheelRequestFrame { void* request; const void* reply; void (*say)(const char*); void* engineIn; };
// transport Input, before the commander button block: the held button, once a frame.
struct ArmyXPressFrame {
    int slot, hand; bool wheel, occupied; const void* snapshot; bool fresh; const int* glyphDraw;
    const void* hands;   // const skills::HandInput*: the raw trigger and face buttons
    const void* aims; const bool* aimsTracked; float head[7]; bool headTracked; double time;
    int& requested; bool& interact;
    const void* aimCtx; void (*aimAt)(const void* ctx, int hand, float* xy);
    bool (*order)(int skill, float x, float y);
    void (*pulse)(int hand, float strength, uint64_t ns);
    void (*say)(const char*);
    const void* engineIn; float upm;
};
// transport EndPair: the frame's quads, from the static image.
struct ArmyXLayerFrame { bool layer; uint64_t chain; void* quadCtx;
                         void (*quad)(void* ctx, void* q, const float* pose, float w, float h, const int* rect, uint64_t chain, bool premultiplied);
                         uint32_t count, limit; void (*say)(const char*); };
// vrcam's aim tick: the monster each hand aims at, and the eyes' (snapshot: const targetlock::Snapshot*, null when stale).
struct HandPicksFrame { const void* snapshot; uint32_t hand[2]; uint32_t eyes; };
// companion_ai's game-thread tick, right after the army step, no lock held.
struct CompanionTickFrame { uint32_t priorityTarget; };
// companion_ai StepSummonArmyServer, right after the coordinator's Step, under its lock: this tick's moves, edited in place.
struct ArmyMovesFrame {
    framevr::commander::SummonArmyCoordinator& coord;
    const framevr::commander::ArmyUnitSample* units; size_t unitCount;
    framevr::commander::ArmyMoveOrder* moves; size_t capacity; size_t& emitted;
    float heroX, heroY;
    uint64_t nowMs;
    void (*say)(const char*);
};
// companion_ai DetourRelocateUnit: the game's own leash is about to move a summon.
struct ArmyOrderNow { bool orderPending; uint32_t priorityTarget; bool freeRoam; };
struct SummonRelocateFrame { uint32_t unitId; int x, y; ArmyOrderNow army; };
// companion_ai, where an army move is sent: walk (true, as before) or run.
struct ArmyWalkFrame { uintptr_t unit; uint32_t unitId; uint8_t petType; bool& walk; void (*say)(const char*); };
// companion_ai DetourServerMoveUnit: a summon's own move the gate let through, and one it refused.
struct SummonMoveFrame { uint32_t unitId; uintptr_t targetUnit; int x, y; int& mode; uint8_t& pathType; uint16_t& flags; };
struct SummonRefusedFrame { uint32_t unitId; int mode, x, y; int& ret; };
// companion_ai TryOverrideCombatTarget: the target a tracked summon is handed (0: none).
struct SummonTargetFrame { uint32_t unitId; bool managed; uint32_t& target; };
// combat Hit: every to-hit roll.
struct HitRollFrame { uintptr_t attacker, defender; int missile; };
}  // namespace framevr::armyseam

namespace armyxmenu {
bool On();       // the menu is on a hand button (in either weapon set)
void Input(framevr::armyseam::WheelInputFrame f);
void Request(framevr::armyseam::WheelRequestFrame f);
void Press(framevr::armyseam::ArmyXPressFrame f);
void Layer(const framevr::armyseam::ArmyXLayerFrame& f);
void Paint(const framevr::armyseam::StaticAtlasFrame& f);
void Picks(const framevr::armyseam::HandPicksFrame& f);
void GameTick(framevr::armyseam::CompanionTickFrame f);
void ArmyTick(framevr::armyseam::ArmyMovesFrame& f);
}
namespace armyxhunt {
void Moves(framevr::armyseam::ArmyMovesFrame f);
bool Leash(framevr::armyseam::SummonRelocateFrame f);
void Walk(framevr::armyseam::ArmyWalkFrame f);
void Move(framevr::armyseam::SummonMoveFrame f);
void Refused(framevr::armyseam::SummonRefusedFrame f);
void Target(framevr::armyseam::SummonTargetFrame f);
void Roll(framevr::armyseam::HitRollFrame f);
void Sample();
}
namespace framevr::armyseam::cfg {
inline float Gated(float v) { return ::armyxmenu::On() ? v : 0.f; }
inline bool Gated(bool v) { return v && ::armyxmenu::On(); }
}
