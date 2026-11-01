#pragma once
#include <cmath>
#include <cstdint>

namespace combat {
struct Seed {uint32_t low=0,high=0;};
inline Seed AdvanceSeed(Seed seed) {
    const uint64_t next=uint64_t(seed.low)*0x6ac690c5u+seed.high;
    return {uint32_t(next),uint32_t(next>>32)};
}
inline bool PlayerContact(uint32_t localId,uint32_t attackerType,uint32_t attackerId,uint32_t defenderType) {
    return localId!=UINT32_MAX&&attackerType==0&&attackerId==localId&&defenderType==1;
}
// A monster (or its missile, resolved to the native owner) striking the local player.
inline bool IncomingContact(uint32_t localId,uint32_t attackerType,uint32_t defenderType,uint32_t defenderId) {
    return localId!=UINT32_MAX&&attackerType==1&&defenderType==0&&defenderId==localId;
}
// Scratch seed fed to the block roll: advances to 0 (always under any block
// rate) or 99 (never under D2's 75% cap). Same inline step as the hit roll.
inline Seed GuardRoll(bool block) {return block?Seed{0,0}:Seed{0,99};}
// Unleashed swing damage: attack rating scales a landed swing instead of
// deciding a miss. chance: the native, already clamped to-hit % (5..95).
// floor: the share kept at zero chance ([gameplay] ar_damage_floor; 0 = the
// raw chance).
inline float ArFactor(int chance,float floor) {
    const float c=float(chance<0?0:chance>100?100:chance)/100.f;
    const float f=floor<0?0.f:floor>1?1.f:floor;
    return f+(1-f)*c;
}
// Unleashed incoming defense: a monster's to-hit roll against the local player
// always connects, and the native chance it would have had (its attack rating
// against the player's Defense, clamped 5..95) scales the whole landed attack,
// elemental riders included. Applied at the final life write, after damage
// reduction, resistances and absorb, so the expected damage equals native
// (chance x a landed hit) without the hit-or-miss variance. No floor.
inline float DefenseFactor(int chance) {return ArFactor(chance,0);}
// Unleashed would-have-hit gate (Cal, 2026-10-22): every forced contact lands and
// its damage follows the Unleashed formula, but its other effects deliver natively:
// only when the native roll would have hit. The forced hit check still advances the
// attacker's real seed once (GetSeed); that advanced seed's low % 100 is the roll
// the game itself would have compared (44BD6D..44BDC6: roll >= chance misses).
inline int NativeHitRoll(Seed current) {return int(AdvanceSeed(current).low%100);}
// An unknown roll or chance (-1) counts as a hit: native effects, as before the gate.
inline bool WouldHit(int roll,int chance) {return roll<0||chance<0||roll<chance;}
// Damage over time (Cal, 2026-10-30): poison and burn are states that don't stack - a
// new one only replaces a weaker one, and refreshes it (D2MOO SUnitDmg.cpp 1472-1566) -
// so scaling their rate by a contact's factor undercut native even without flailing (at
// 60% chance about 60% of native poison damage, where native hits keep the full rate up).
// Like an on-hit effect: full native rate and length from a contact that would have hit
// and isn't a gated stroke (not a full cycle, a cleave follow-up, a trigger hit inside a
// swing's cycle); none from any other.
inline bool DamageOverTime(bool wouldMiss,bool gatedStroke) {return !wouldMiss&&!gatedStroke;}
// Unleashed swing damage: how recovered a stroke is - the time since the
// previous landed stroke over the native attack cycle, raised to `curve`, at
// most 1. On the fixed cycle (FixedCycle, 2026-10-30) it scales only the strokes
// in between full-cycle ones, so it decides how little an extra swing adds.
// grace (0..1]: the share of the cycle after which a stroke counts as fully
// recovered (1 = none). An unknown cycle (0) or no previous stroke
// (UINT64_MAX) is fully recovered.
inline float RecoveryFactor(uint64_t sinceMs,uint32_t cycleMs,float curve=1,float grace=1) {
    if(!cycleMs||sinceMs==UINT64_MAX)return 1;
    const float g=grace>.05f&&grace<1?grace:1;
    const float x=float(sinceMs)/(float(cycleMs)*g);
    if(x>=1)return 1;
    const float k=curve>=1&&curve<=4?curve:1;
    return k==1?x:k==2?x*x:float(std::pow(double(x),double(k)));
}
// Unleashed fixed attack cycle (Cal, 2026-10-30: "The IAS cooldown/breakpoint should be
// fixed and you should only be penalized for swinging within a fixed window between full
// cycles ... Swings out of cycle should never delay your access to the next breakpoint").
// A stroke one full native cycle (or more) after the latest full-cycle attack (sinceFullMs)
// is full: full damage, on-hit effects, a strike skill's cast, and it starts the next cycle.
// A stroke before that is in between: it never moves the next breakpoint, carries no on-hit
// effects, and its damage scales by the time since the previous stroke of any kind
// (sinceAnyMs; RecoveryFactor's curve and grace), so one well-timed extra swing per cycle
// pays a little and flailing pays almost nothing. No full-cycle attack yet (UINT64_MAX) or
// an unknown cycle (0): full. scale: [gameplay] swing_recovery_damage.
struct CycleStroke {bool full=true;float recovery=1;};
inline CycleStroke FixedCycle(uint64_t sinceFullMs,uint64_t sinceAnyMs,uint32_t cycleMs,bool scale,float curve,float grace) {
    if(!cycleMs||sinceFullMs==UINT64_MAX||sinceFullMs>=cycleMs)return {};
    const uint64_t since=sinceAnyMs<sinceFullMs?sinceAnyMs:sinceFullMs;
    return {false,scale?RecoveryFactor(since,cycleMs,curve,grace):1.f};
}
// Unleashed cleave falloff: the n-th creature (0 = first) one stroke lands on
// deals 1 - step*n of its damage, never below floor. step 0 = no falloff.
inline float CleaveFactor(unsigned index,float step,float floor) {
    const float s=step>0?step:0.f,f=floor<0?0.f:floor>1?1.f:floor;
    const float v=1-s*float(index);
    return v<f?f:v>1?1.f:v;
}
// Unleashed on-hit gate (Cal, 2026-10-21): a physical contact's on-hit effects
// (leech, chance-on-hit procs, crushing blow, open wounds, prevent heal,
// critical and deadly strike) roll only on the stroke's first landed contact,
// and only when that stroke spent a full native attack cycle (the breakpoint)
// since the latest full-cycle attack on its clock (FixedCycle). swing_recovery_grace
// does not shorten it. An unknown cycle (0) or no previous stroke (UINT64_MAX)
// counts as full. Every other contact still lands its scaled damage.
enum class OnHit:uint8_t {Allowed,NotFullCycle,CleaveFollowUp};
inline OnHit OnHitGate(bool firstContact,uint64_t sinceMs,uint32_t cycleMs) {
    if(!firstContact)return OnHit::CleaveFollowUp;
    if(cycleMs&&sinceMs!=UINT64_MAX&&sinceMs<cycleMs)return OnHit::NotFullCycle;
    return OnHit::Allowed;
}
// Unleashed trigger/button melee (Cal, 2026-10-24): a melee hit check outside a
// physical swing (the trigger's basic attack or melee skill, the shield's Smite) is a
// stroke too, so mixing it with swings is never faster than swinging. It is measured
// from the swings on any hand, never from another trigger hit (the native animation
// already paces those, and a multi-hit cast's later hits); a swing is measured from
// the latest trigger hit as well as its own clock. Both on the fixed cycle (FixedCycle):
// full ones start the next cycle, in-between ones never move it.
// StrokeGap: the time between a landed stroke and now, in either order (one batch
// can apply a swing sampled just before a trigger hit); UINT64_MAX when none.
inline uint64_t StrokeGap(uint64_t landedAt,uint64_t now) {
    if(!landedAt)return UINT64_MAX;
    return now>=landedAt?now-landedAt:landedAt-now;
}
// A swing's time since the previous stroke: its own clock or the latest trigger hit
// (triggerAt, 0 = none this session), whichever is nearer.
inline uint64_t SwingSince(uint64_t ownSinceMs,uint64_t triggerAt,uint64_t now) {
    const uint64_t t=StrokeGap(triggerAt,now);
    return t<ownSinceMs?t:ownSinceMs;
}
// Life Tap on a gated stroke (Cal, 2026-10-30; audit 2026-10-25 B2): a monster
// cursed with Life Tap heals its striker from its own damagedinmelee /
// damagedbymissile events (D2MOO EventFunc05: life += phys% of the record, set
// with SetStat(striker, 6)). It is leech by another name, so a stroke the on-hit
// gate switches off heals nothing, like leech; a would-miss contact already
// skips those events. Inside a gated stroke's struck events any rise of the
// striker's life is that heal; a fall (attacker takes damage) stays native.
inline bool TapHealBlocked(bool gatedStroke,int lifeBefore,int lifeAfter) {
    return gatedStroke&&lifeAfter>lifeBefore;
}
struct TriggerStroke {float recovery=1;OnHit onHit=OnHit::Allowed;};
// sinceFullSwingMs: since the latest full-cycle swing on any hand; sinceAnySwingMs: since the
// latest landed swing on any hand. scale: [gameplay] swing_recovery_damage; the on-hit gate
// applies without it too, as for a swing. A full trigger hit (onHit Allowed) starts the next cycle.
inline TriggerStroke TriggerRecovery(uint64_t sinceFullSwingMs,uint64_t sinceAnySwingMs,uint32_t cycleMs,bool scale,float curve,float grace) {
    const auto c=FixedCycle(sinceFullSwingMs,sinceAnySwingMs,cycleMs,scale,curve,grace);
    return {c.recovery,c.full?OnHit::Allowed:OnHit::NotFullCycle};
}
// Scratch seed for a gated critical/deadly strike roll: it advances to 99,
// under no chance below 100 (native: roll = low % 100, crit when roll < chance).
inline Seed NoCritRoll() {return {0,99};}
}
