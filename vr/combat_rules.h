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
// Damage over time (poison, burn) never reaches the scaled life write, so it takes
// the hit's factor itself. A positive rate never rounds away to nothing.
inline int32_t ScaleDamageOverTime(int32_t rate,double factor) {
    if(rate<=0||!(factor>=0)||factor==1)return rate;
    const double v=double(rate)*(factor>8?8:factor)+.5;
    if(v>=2147483647.)return 2147483647;
    const int32_t scaled=int32_t(v);
    return factor>0&&scaled<1?1:scaled;
}
// Unleashed swing damage: how recovered a stroke is - the time since the
// previous landed stroke over the native attack cycle, raised to `curve`, at
// most 1. curve 1 is damage-per-second neutral (twice the strokes at half the
// damage); 2 makes flailing lose damage (twice the rate = half the DPS).
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
// since the previous landed stroke on its recovery clock. swing_recovery_grace
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
// stroke too, so mixing it with swings is never faster than swinging. Its recovery is
// measured from the latest landed swing on any hand, never from another trigger hit
// (the native animation already paces those, and a multi-hit cast's later hits); a
// swing is measured from the latest trigger hit as well as its own clock.
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
struct TriggerStroke {float recovery=1;OnHit onHit=OnHit::Allowed;};
// scale: [gameplay] swing_recovery_damage; the on-hit gate applies without it too, as for a swing.
inline TriggerStroke TriggerRecovery(uint64_t sinceSwingMs,uint32_t cycleMs,bool scale,float curve,float grace) {
    return {scale?RecoveryFactor(sinceSwingMs,cycleMs,curve,grace):1.f,OnHitGate(true,sinceSwingMs,cycleMs)};
}
// Scratch seed for a gated critical/deadly strike roll: it advances to 99,
// under no chance below 100 (native: roll = low % 100, crit when roll < chance).
inline Seed NoCritRoll() {return {0,99};}
}
