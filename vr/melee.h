#pragma once
#include "../steamvr/melee_contact.h"
namespace D2RL {struct PluginContext;}
namespace melee {
void Init(const D2RL::PluginContext* context);
void Install();
void Submit(const framevr::melee::Sample& sample);
void Cancel();
void CancelShield();
void CancelWeapon(int hand=0);
bool ShieldReady();
void Report();
// Attack speed: one landed weapon hit per native attack cycle, one creature
// per stroke. Off restores unlimited free-swing contacts. Cycle 0 = unknown,
// which never gates. SetAttackCycle comes from the UI thread's stat read.
void ConfigureAttackSpeed(bool enabled,bool traceHits); // trace: [debug] melee_trace
void SetAttackCycle(uint64_t session,uint32_t milliseconds,int hand=0);
// Dual wield (attack_speed.h DualFrames): the hero class and, per inventory arm (0 right,
// 1 left), the weapon class and WSM; classId < 0 when not dual wielding. UI thread's read;
// the game thread times each stroke from them with the swinging weapon's IAS and attackrate.
void SetDualSpeed(uint64_t session,int classId,const int weaponClass[2],const int wsm[2]);
// Physical swings and trigger/button melee use the native to-hit/block/level
// rolls. Off restores the contact_hits policy for them. Missiles unaffected.
void ConfigureAttackRating(bool native); // Classic rules (unused: melee is Unleashed only)
// [gameplay] player_flinch (Unleashed): native=false (default) refuses the local
// hero's hit recovery (GetHit) and block animation when struck, so no hit locks
// out casting, skills or movement; true (and Classic) keeps native flinching. Any thread.
void ConfigurePlayerFlinch(bool native);
// Zeal's light: a short Den of Evil light beam over the enemy on each Zeal hit
// (steamvr/zeal_light.h); [gameplay] zeal_light (default on), zeal_light_frames,
// zeal_light_pool (beams kept lit in advance while Zeal is armed), zeal_light_exit
// (0 off, 1 rise, 2 sink, 3 shrink), zeal_light_exit_ms (the way out) and
// zeal_light_turn (degrees trimming the turn that keeps its light beyond the creature),
// zeal_light_behind (cells behind the creature) and zeal_light_spread (degrees either side).
void ConfigureZealLight(bool on,float frames,float pool,float exit,float exitMs,float turn,float behind,float spread);
// Unleashed: a stroke's damage scales with how much of the native attack cycle
// passed since the previous landed stroke; every creature one stroke reaches
// shares that factor. Contacts are never refused. [gameplay] swing_recovery_damage;
// curve: the factor's power (swing_recovery_curve); grace: the share of the
// cycle after which a stroke is fully recovered (swing_recovery_grace, 1 = none).
void ConfigureSwingRecovery(bool on,float curve,float grace);
// Unleashed cleave falloff: each further creature one stroke lands on deals
// `step` less (0..1, 0 = off), never below `floor`. [gameplay] cleave_falloff, cleave_min
void ConfigureCleave(float step,float floor);
// Unleashed trigger/button melee (combat_rules.h TriggerRecovery): the local player's
// melee hit check on this monster outside a physical swing, as combat.cpp's hit check
// sees it. Measured from the latest landed swing on any hand (recovery damage factor
// and the on-hit cycle gate), it becomes the latest trigger hit every later swing is
// also measured from. smite: inside the native Smite (it always hits natively). Off the
// owning game thread the clocks are neither read nor stamped: full recovery, effects on.
struct ButtonStroke {float recovery=1;bool onHit=true,smite=false;};
ButtonStroke ButtonHit(uint32_t monster);
// Owning game thread only; shared with physical arrows. Re-resolves the unit
// from its session/id rather than passing a native pointer across threads.
bool LocalServer(uintptr_t& game,uintptr_t& player);
uintptr_t ServerUnit(uintptr_t game,int type,uint32_t id); // owning game thread only
// Perfect parry: native hit recovery for this monster on a LATER game update,
// never inside its own attack. Thread-safe. False when unavailable/not queued.
// durability: the shield's before the parried hit, refunded; -2 = read it now.
bool QueueParry(uint32_t monster,int durability=-2);
// When this monster's current attack began (GetTickCount64), from the native
// mode setter on the server; 0 when not seen in the last 3 s. Thread-safe.
uint64_t AttackStart(uint32_t monster);
// When this monster's current attack starts coming down at you: its start plus
// [gameplay] parry_downstroke % of the wind-up its type took last time (start
// to to-hit roll, per attack mode). 0 = not seen, or wind-up not measured yet.
// A parry swing must begin at or after it. Thread-safe.
uint64_t ParryOpens(uint32_t monster);
void ConfigureParryDownstroke(uint32_t percent);   // 0..95, default 90
// On (default): a parry resolves at the attacker's hit frame, after the blow
// comes down; off: it staggers the attacker the moment the swing is made.
void ConfigureParryAtHit(bool on);
// This monster's current attack reached its to-hit roll (hit or miss): too
// late to stagger it before the hit; its first roll times the wind-up. Game thread.
void NoteAttackRolled(uint32_t monster);
// A new parry swing: on a later game update, stagger the first monster whose
// melee attack is under way and not yet rolled (combat::EarlyParry). Thread-safe.
void QueueParrySwing();
// The block sound at the hero ([gameplay] block_sound). Thread-safe.
void QueueBlockSound();
// A worn item's durability onto its client copy, on the UI thread. Thread-safe.
void QueueClientDurability(uint32_t itemId,int value);
// [gameplay] block_sound: sounds.txt row played at the hero on every physical
// block (default 398 block_weapon_1); 0 is silent. A change plays it once.
void ConfigureBlockSound(int sound);
// [gameplay] parry_sound: sounds.txt row played at the hero on a parry; 0 is
// silent. Changing it while in game plays the new sound once (audition).
void ConfigureParrySound(int sound);
// Owning game thread only. The live, non-hostile hireling nearest a thrown
// bottle's landing point (world metres, with the rendered player and scale of
// that frame); true when it landed at the hireling's feet. distance<0: none.
bool NearHireling(framevr::math::Vec world,framevr::math::Vec player,float unitsPerMetre,float& distance,uint32_t& classId);
// Owning game thread only. The local hireling's life, max life, class and
// unit id; hint is the last known id (direct lookup). False when none is alive.
bool HirelingVitals(uint32_t hint,float& health,float& maxHealth,uint32_t& classId,uint32_t& unitId);
// Owning game thread only, on that exact hireling. Native cosmetic effects
// synced to the client like the game's own: a state (colour tint) and a
// one-shot overlay. Setting needs a live hireling; clearing a state also
// reaches a dead one. A state set here outranks the hireling's other colour
// tints (cold, poison) until it is cleared. False when unavailable or gone.
bool HirelingState(uint32_t unitId,int state,bool on);
// Owning game thread only. Live monsters hostile to the local player within
// range grid cells: id, class, cell, and clear = no wall between. Returns the
// count, -1 when the local server is unavailable; hero receives the player's cell.
struct Hostile {uint32_t id,classId;float x,y;bool clear;};
int Hostiles(Hostile* out,int max,float range,float& heroX,float& heroY);
bool HirelingOverlay(uint32_t unitId,int overlay);
}
