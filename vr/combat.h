#pragma once
#include <cstdint>
namespace framevr::guard {struct Pose;struct Debug;}

namespace combat {
void Configure(bool enabled);
// Calling thread only: true restores native hit/block/dodge rolls until reset.
// Returns the previous value so the caller can restore it.
bool NativeRolls(bool on);
// Native to-hit rolls for every local-player non-missile hit check (swings,
// trigger/button melee, melee skills). Missiles keep contact_hits.
void ConfigureMeleeRolls(bool native);
// Latest trigger/button melee hit check (outside physical swings), consumed
// once: its session and GetTickCount64 time. False when there is none.
bool TakeButtonMelee(uint64_t& session,uint64_t& at);
// Unleashed physical guard. A monster hit that passes its native to-hit roll
// is blocked by a raised shield facing the attacker, not by the block stat.
// parryMs 0 disables perfect parry. Outside first person, menus or without a
// shield (eligible=false) the native block roll is kept.
void ConfigureGuard(bool enabled,uint32_t parryMs,bool trace=false); // trace: [debug] guard_trace
// [gameplay] parry_late_ms (default 200, max 400, 0 = off): a parry swing up to
// this long after a blocked melee hit still parries it (the blow lands on screen
// after the game's hit frame). It staggers that attacker and refunds the wear.
void ConfigureLateParry(uint32_t ms);
// [gameplay] enemies_always_hit (Unleashed, default on): monsters' to-hit rolls against
// the local player always connect; the native chance scales the landed attack's damage.
void ConfigureEnemiesAlwaysHit(bool on);
// [gameplay] guard_break_chip (Unleashed, default on) and
// guard_break_chip_scale (1): through a broken guard a block lands as a chip,
// its life damage x (1 - chance to block) x scale.
void ConfigureGuardChip(bool on,float scale);
// [gameplay] shield_block_wear (22): % of physical blocks and chips that give the
// shield the game's own 10% when-struck wear roll (22 = its native share, 2.2%);
// nothing else wears for them. A parry wears nothing.
void ConfigureShieldWear(int share);
// The game's Master x Combat volume (Settings.json, 0..1), re-read at most every
// 2 s and only when the game saved it. Any thread.
float GameVolume();
// Game thread (melee.cpp): a physical swing's native attack on this monster id
// is running (NoSwing UINT32_MAX = none). Returns the previous value.
uint32_t PhysicalSwing(uint32_t target);
// Game thread (melee.cpp), around one physical swing's native attack: that
// stroke's recovery damage factor (0..1, Unleashed). Returns the previous value.
float SwingRecovery(float factor);
// Unleashed: attack rating scales physical swing damage - the native 5..95 to-hit
// chance, read where HitSuccessful passes it on - instead of deciding a miss.
// floor: the share of damage kept at zero chance (0 = the raw chance).
void ConfigureArDamage(bool on,float floor);
// Game thread: a parry flinched this monster; its next physical hit within the window is a riposte.
void NoteRiposte(uint32_t monster);
// A parry swing made during the monster's downstroke (opensAt, melee::ParryOpens)
// resolves at once, before its hit frame. ParryArmed: an unspent recent swing (cheap gate,
// thread-safe). Game thread: EarlyParry judges this attacking monster unit
// against the local player unit and returns the swing to spend (0 = none);
// after staggering it, ConfirmParry spends that swing once.
bool ParryArmed();
uint64_t EarlyParry(uintptr_t monster,uintptr_t player,uint64_t attackAt,uint64_t opensAt,const char* trigger);
void ConfirmParry(uint64_t swing);
// A riposte is a clean hit: no attack-rating or recovery penalty.
// [gameplay] riposte_bonus (% extra damage on top, default 0), riposte_ms, riposte_sound_file (empty:
// the built-in sound; else a WAV, next to the ini unless a full path).
// riposte_volume: % on top of the game's own Master x Combat volume (Settings.json).
void ConfigureRiposte(float bonus,uint32_t windowMs,float volume,const wchar_t* soundFile);
// UI thread: one completed shield pose, including its calibrated scale and
// timestamp. Latest tracking can invalidate it; coverage never follows gaze.
void PublishGuard(uint64_t session,bool eligible,bool posed,const framevr::guard::Pose& pose,
                  bool handOk,bool leftShield);
bool ReadGuardDebug(framevr::guard::Debug& out);
void Install();
void Report();
// The UI-thread local-player accessor is authoritative across area changes;
// controller aim can also refresh identity. SDK handles are never pointers.
void ObserveLocalPlayer(uintptr_t unit);
bool ObserveUiPlayer(uintptr_t unit,uint64_t session);
bool LocalPlayerUnit(uintptr_t& unit);
// UI thread: a worn item's new durability onto the local player's client item.
void MirrorClientDurability(uint32_t itemId,int value);
bool LocalPlayerId(uint32_t& id);
}
