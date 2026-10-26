// Offline D2R 3.3.93787 contact resolution. Native range/collision, damage,
// skills and costs stay in charge; only the local player's random misses go.
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "combat.h"
#include "../steamvr/shield_debug.h"
#include "combat_rules.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "shield_guard.h"
#include "guard_strain.h"
#include "melee.h"
#include "native_ui_signatures.h"
#include "../steamvr/transport.h"

namespace gamestate {uint64_t LocalPlayer();int32_t LeftDurability();}

namespace combat {
namespace {
constexpr uint64_t HitRva=0x44ba60,SeedRva=0x34a1e0,BlockRva=0x44b930,DodgeRva=0x44ef70;
constexpr uint64_t HitSeedReturn=0x44bd6d;
constexpr uint8_t HitSig[]={0x4c,0x8b,0xdc,0x53,0x55,0x56,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x90,0x04,0x00,0x00};
constexpr uint8_t SeedSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x1d};
constexpr uint8_t BlockSig[]={0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x30};
constexpr uint8_t DodgeSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x30};
// Verify the particular roll as well as the function prologues. Its seed is
// consumed inline, then the remainder of HitSuccessful applies native on-hit
// behavior (including Prevent Monster Heal). Returning 1 from the entry hook
// would silently skip that behavior.
constexpr uint8_t RollSig[]={0x48,0x8b,0xce,0xe8,0x73,0xe4,0xef,0xff,0x8b,0x08,0x48,0x69,0xd9,0xc5,0x90,0xc6,0x6a};
using HitFn=int(*)(uintptr_t,uintptr_t,int,int);
using SeedFn=Seed*(*)(uintptr_t);
using BlockFn=int(*)(uintptr_t,uintptr_t,uintptr_t,int,int);
using DodgeFn=int(*)(uintptr_t,uintptr_t,int);
HitFn originalHit=nullptr;SeedFn originalSeed=nullptr;BlockFn originalBlock=nullptr;DodgeFn originalDodge=nullptr;
std::atomic<bool> enabled{false},ready{false};
bool hitIn=false,seedIn=false,blockIn=false,dodgeIn=false,rollChecked=false;
// One atomic value keeps freshness and id coherent, including native id zero.
std::atomic<uint64_t> localIdentity{0},localSession{0};
std::mutex localPlayerMutex;
struct UiPlayer {uintptr_t unit=0;uint64_t session=0,at=0;uint32_t id=0;} uiPlayer;
std::atomic<unsigned> meleeChecks{0},missileChecks{0},rolls{0},blocks{0},dodges{0},failed{0},nativeMelee{0},nativeGuards{0};
// Melee attack rating: the local player's non-missile hit checks (physical
// swings, trigger/button attacks, melee skills) keep the native roll.
// Missile hit checks keep the contact_hits override.
std::atomic<bool> meleeRolls{false};
thread_local bool meleeHit=false; // inside such a native melee hit check
// Classic: trigger/button melee shares the physical swing's attack cycle. The
// latest native melee hit check made outside a swing, read by melee.cpp.
std::atomic<uint64_t> buttonMeleeAt{0},buttonMeleeSession{0};
// Unleashed physical guard. 44B930 (game, attacker, defender, avoid, block):
// block==0 tail-jumps to Dodge; else rate=347DA0(defender) (<=0 skips to
// Dodge, running divides by 3), then the DEFENDER's seed from 34A1E0 returns
// to 44B9D1, is advanced inline exactly like the hit roll, roll=low%100, and
// roll<rate returns 1 (blocked; 44B720 turns that into result flag 0x10).
// Verified in a decrypted 3.3.93847 snapshot; same RVAs as 3.3.93787.
constexpr uint64_t BlockSeedReturn=0x44b9d1,BlockRollAt=0x44b9c9;
constexpr uint8_t BlockRollSig[]={0x48,0x8b,0xcf,0xe8,0x0f,0xe8,0xef,0xff,0x8b,0x08,0x48,0x69,0xd9,0xc5,0x90,0xc6,0x6a};
std::atomic<bool> guardEnabled{false},guardTrace{false};std::atomic<uint32_t> guardParryMs{450};
// [gameplay] parry_late_ms: a swing this long after a blocked melee hit still
// parries it. The server's hit frame comes before the blow visibly lands: 15
// of 24 swings (log 2026-10-19 14:29-14:55) started 0-156 ms after a blocked
// hit, timed to the animation, against 6 parries before it. 0 = none.
// 15:00-15:18: parry swings centred 55 ms after the hit frame (-62..+188 ms);
// the window is about -33..+200 ms around it (with parry_downstroke 90).
std::atomic<uint32_t> guardLateMs{200};
std::atomic<unsigned> lateParries{0};
bool guardChecked=false;
struct GuardShared {framevr::guard::State state;uint64_t session=0,at=0;float upm=1;bool eligible=false;
    framevr::guard::Debug debug{};uint32_t debugMonster=UINT32_MAX;
    uint32_t blockMonster=UINT32_MAX;int32_t blockDurability=-1;};   // the last blocked melee hit, for a late parry
std::mutex guardMutex;GuardShared guard;
enum class Forced:uint8_t {None,Block,Open};
enum class Guard:uint8_t {Native,Open,Block,Parry};
thread_local Forced forced=Forced::None;thread_local bool forcedServed=false;thread_local Seed forcedRoll;
std::atomic<unsigned> guardBlocks{0},guardOpen{0},guardParries{0},guardUnserved{0},guardFallback{0},guardLate{0};
std::atomic<unsigned> guardAttackKnown{0};
// Monster melee to-hit results against the local player; a miss never reaches
// the block check, so missParries counts parries made on missed attacks.
std::atomic<unsigned> incomingHits{0},incomingMisses{0},missParries{0};
std::atomic<unsigned> earlyParries{0};   // staggered before the hit frame (EarlyParry)
std::atomic<unsigned> parryMisses{0};    // parry swings whose window ran out unspent (haptic buzz)
std::array<std::atomic<unsigned>,size_t(framevr::guard::Rejection::Count)> guardRejections{};
// Unit stats Get/Set (D2Common GetUnitStat / SetUnitStat) are jump stubs
// through the game's own table; Set is a set, not an add (native fills life
// with Set(u,6,Get(u,7))).
constexpr uint64_t GetStatRva=0x2f5020,SetStatRva=0x2f7d10;
constexpr uint8_t GetStatSig[]={0xff,0x25,0xf2,0x51,0xb3,0x03,0x90,0x90,0x90,0x90};
constexpr uint8_t SetStatSig[]={0xff,0x25,0x32,0x25,0xb3,0x03,0x90,0x90,0x90,0x90};
using GetStatFn=int(*)(uintptr_t,int,int);using SetStatFn=void(*)(uintptr_t,int,int,int);
SetStatFn originalSetStat=nullptr;bool setStatIn=false;
// Damage application (44CE80: game, attacker, defender, flag, damage*). Its
// life write - Set(defender,6,max(Get(defender,6)-damage[+0x134],0)) - returns
// to 44D098 (seen for every landed weapon hit, 2026-10-17). 44DF10 can
// recompute +0x134 inside, so damage is scaled at that final write, not at entry.
constexpr uint64_t ApplyDamageRva=0x44ce80,LifeWriteReturn=0x44d098,LifeWriteAt=0x44d08f;
constexpr uint8_t ApplyDamageSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0xe8,0xfe,0xff,0xff};
constexpr uint8_t LifeWriteSig[]={0x41,0x8d,0x51,0x06,0xe8,0x78,0xac,0xea,0xff,0x8b,0x9f,0x24,0x01,0x00,0x00};
using ApplyDamageFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,int,uintptr_t);
ApplyDamageFn originalApplyDamage=nullptr;bool applyDamageIn=false;std::atomic<bool> damageScaleReady{false};
// This game thread is inside a physical swing's native attack on this monster
// (melee.cpp), and inside its damage application with this record.
constexpr uint32_t NoSwing=UINT32_MAX;
thread_local uint32_t swingTarget=NoSwing;thread_local uintptr_t damageRecord=0;
// Riposte: a parry opens a window for one stronger hit on the parried monster.
std::atomic<float> riposteBonus{0};std::atomic<uint32_t> riposteMs{1500};
std::atomic<uint32_t> riposteMonster{NoSwing};std::atomic<uint64_t> riposteUntil{0};
std::atomic<unsigned> ripostesLanded{0};
// Unleashed swing damage: attack rating and attack speed scale a physical
// swing's damage instead of deciding whether it lands. HitSuccessful computes
// the clamped chance (44BD44: edi=min(95,max(5,...))), rolls, then passes both
// to 427EA0 (game, attacker, defender, chance, roll) - its only call site. A
// pass-through hook there records the chance of the swing on this thread.
constexpr uint64_t HitChanceRva=0x427ea0,ChanceClampAt=0x44bd44,ChanceCallAt=0x44bda2;
constexpr uint8_t HitChanceSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x98,0x02,0x00,0x00};
constexpr uint8_t ChanceClampSig[]={0xbf,0x5f,0x00,0x00,0x00,0x41,0x03,0xcd,0x41,0x0f,0xaf,0xd5,0x8d,0x04,0x12,0x99,0xf7,0xf9,
    0xb9,0x05,0x00,0x00,0x00,0x3b,0xc1,0x0f,0x4f,0xc8,0x3b,0xcf,0x0f,0x4c,0xf9};
constexpr uint8_t ChanceCallSig[]={0x89,0x5c,0x24,0x20,0x44,0x8b,0xcf,0x4d,0x8b,0xc6,0x48,0x8b,0xd6,0xe8,0xec,0xc0,0xfd,0xff};
using HitChanceFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,int,int);
HitChanceFn originalHitChance=nullptr;bool hitChanceIn=false;
std::atomic<bool> arDamage{false};std::atomic<float> arFloor{0};
thread_local int swingChance=-1;     // this swing's native to-hit %, -1 = not seen
thread_local float swingRecovery=1;  // this stroke's recovery factor (melee.cpp)
std::atomic<unsigned> scaledHits{0},chanceUnseen{0};
std::atomic<int> lastChance{-1};std::atomic<float> lastRecovery{1},lastFactor{1};
// Unleashed on-hit gate (combat_rules.h OnHitGate; melee.cpp decides, around one
// physical swing's native attack). Each effect is switched off where it rolls,
// for this attacker on this thread only; damage, XP, wear and the rest of the
// attack run as native. Mapped in a decrypted 3.3.93847 snapshot (2026-10-21)
// against D2MOO's map of the 1.10 code; stat ids from D2R's ItemStatCost:
// - critical/deadly strike: the damage fill 44C030 rolls the weapon mastery's
//   crit (33D4F0), passive_critical_strike (337) and item_deadlystrike (141) on
//   the attacker's seed (34A1E0, advanced inline, roll = low % 100, crit when
//   roll < chance: +0x18 doubled, result flag 0x2000). Gated: the real seed
//   advances once, the roll reads 99 (NoCritRoll). A chance of 100+ still crits.
// - procs: ItemStatCost item events on domeleedamage (5: crushing blow 136, open
//   wounds 135, freeze 134, slow 150, knockback 81, flee 112, blind 113, chance
//   to cast on striking 198) and domeleeattack (7: chance to cast on attack 195)
//   fire through 44D570(game, event, unit, source, damage), from 44CE80 and the
//   melee executor 44B2B0. Skill events of the player's on those two (Warlock
//   Hex Bane/Siphon/Purge) are gated with them; the defender's (thorns,
//   Life Tap...) are not.
// - leech: 450C90 (only from 44CE80) reads the record's +0x120 life and +0x124
//   mana leech (lifedrainmindam 60, manadrainmindam 62, skill leech): zeroed.
// - prevent monster heal (117): inline in the hit check, state 52 applied by
//   433D20(curse), returning to 44BE61: skipped.
// - defense lowered per hit (item_damagetargetac 120): 44C030 sets the
//   defender's armorclass (31), returning to 44C155: skipped.
thread_local uintptr_t onHitGated=0,onHitTarget=0;   // the attacker whose on-hit effects on this target are off (0 = none)
thread_local Seed noCritRoll;
constexpr uint64_t TriggerRva=0x44d570,CurseRva=0x433d20,PreventHealReturn=0x44be61,DefensePerHitReturn=0x44c155;
constexpr uint64_t CritSeedReturn[3]={0x44c2e6,0x44c33e,0x44c392};   // weapon mastery, critical strike, deadly strike
constexpr int DoMeleeDamage=5,DoMeleeAttack=7;   // Events.txt rows
constexpr uint8_t TriggerSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x60};
constexpr uint8_t CurseSig[]={0xff,0x25,0x1a,0x67,0x9f,0x03,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
struct GateSite {uint64_t rva;uint8_t bytes[64];uint8_t size;};
// Each roll site as this build has it: the seed fetches and their stat ids, the
// doubling, both event calls, the leech reads and call, prevent heal's state 52
// and its call, the defense-per-hit read and write.
constexpr GateSite CritSites[]={
    {0x44c2de,{0x48,0x8b,0xce,0xe8,0xfa,0xde,0xef,0xff,0x4c,0x8b,0xc8,0x8b,0x08,0x4c,0x69,0xc1,0xc5,0x90,0xc6,0x6a},20},
    {0x44c323,{0xba,0x51,0x01,0x00,0x00},5},
    {0x44c336,{0x48,0x8b,0xce,0xe8,0xa2,0xde,0xef,0xff,0x4c,0x8b,0xc8,0x8b,0x08,0x4c,0x69,0xc1,0xc5,0x90,0xc6,0x6a},20},
    {0x44c377,{0xba,0x8d,0x00,0x00,0x00},5},
    {0x44c38a,{0x48,0x8b,0xce,0xe8,0x4e,0xde,0xef,0xff,0x4c,0x8b,0xc8,0x8b,0x08,0x4c,0x69,0xc1,0xc5,0x90,0xc6,0x6a},20},
    {0x44c3c8,{0x8b,0x47,0x18,0x03,0xc0,0x89,0x47,0x18,0xb8,0x00,0x20,0x00,0x00,0x66,0x09,0x47},16}};
constexpr GateSite EventSites[]={
    {0x44cfdc,{0xba,0x05,0x00,0x00,0x00,0x49,0x8b,0xcf,0xe8,0x87,0x05,0x00,0x00},13},
    {0x44b463,{0xba,0x07,0x00,0x00,0x00,0x48,0x8b,0xcf,0xe8,0x00,0x21,0x00,0x00},13}};
constexpr GateSite LeechSites[]={
    {0x450c9a,{0x41,0x83,0xb9,0x20,0x01,0x00,0x00,0x00},8},
    {0x450cb0,{0x41,0x83,0xb9,0x24,0x01,0x00,0x00,0x00},8},
    {0x44d02c,{0x4c,0x8b,0xcf,0x4c,0x8b,0xc6,0x49,0x8b,0xd6,0x49,0x8b,0xcf,0xe8,0x53,0x3c,0x00,0x00},17}};
constexpr GateSite HealSites[]={
    {0x44be2e,{0x66,0x0f,0x6f,0x05,0x9a,0xee,0x8c,0x01,0x48,0x8d,0x4c,0x24,0x40,0xf3,0x0f,0x7f,0x44,0x24,0x50,0x48,0x89,0x6c,0x24,0x68,0x48,0x89,0x74,0x24,0x40,0x4c,0x89,0x74,0x24,0x48,0x89,0x6c,0x24,0x60,0xc7,0x44,0x24,0x64,0x34,0x00,0x00,0x00,0xe8,0xbf,0x7e,0xfe,0xff},51}};
constexpr GateSite DefenseSites[]={
    {0x44c117,{0x45,0x33,0xc0,0x8d,0x53,0x77,0x48,0x8b,0xce,0xe8,0x3b,0x9b,0xea,0xff,0x8b,0xd8,0x85,0xc0,0x74,0x2a,0x45,0x33,0xc0,0x49,0x8b,0xcc,0x41,0x8d,0x50,0x1f,0xe8,0x86,0x87,0xea,0xff,0x45,0x33,0xc9,0x45,0x8b,0xc7,0x8d,0x0c,0x18,0x85,0xc9,0x41,0x8d,0x51,0x1f,0x44,0x0f,0x49,0xc1,0x49,0x8b,0xcc,0xe8,0xbb,0xbb,0xea,0xff},62}};
template<size_t N> bool CheckSites(const GateSite (&sites)[N]) {
    for(const auto& s:sites)if(!d2rsig::Check(s.rva,s.bytes,s.size))return false;
    return true;
}
using TriggerFn=uint32_t(*)(uintptr_t,int,uintptr_t,uintptr_t,uintptr_t);
using CurseFn=uintptr_t(*)(uintptr_t);
TriggerFn originalTrigger=nullptr;CurseFn originalCurse=nullptr;
bool triggerIn=false,curseIn=false,critGate=false,leechGate=false,defenseGate=false,onHitLogged=false;
std::atomic<unsigned> gatedMeleeDamage{0},gatedMeleeAttack{0},gatedLeech{0},gatedCrits{0},gatedHeal{0},gatedDefense{0};
// Unleashed would-have-hit gate (Cal, 2026-10-22; combat_rules.h WouldHit). Every
// forced contact - a monster's melee hit or missile on the local player
// (enemies_always_hit) and the local player's physical swing - lands, and its
// damage follows the Unleashed formula (Defense / attack-rating factor). Every
// other effect of that attack delivers natively: only when the native roll would
// have hit. The roll is exact, not just its odds: GetSeed still advances the
// attacker's real seed once, and its low % 100 against the clamped chance
// HitChance receives is the game's own verdict. A riposte is a clean hit. On a
// would-miss, for that attacker and defender on this game thread (3.3.93847 map,
// basic attack 42FF..430156 -> hit check 44B720 -> fill 44B600 -> executor 44B2B0):
// - events that only fire on a landed hit (44CE80 44CFC5..44CFFC): domeleedamage
//   / domissiledamage (5/6: the attacker's procs, Iron Maiden on a cursed
//   attacker) and damagedinmelee / damagedbymissile (1/2: the struck unit's chance
//   to cast when struck, damage to mana, attacker takes lightning damage, Frozen
//   Armor, Life Tap): skipped.
// - events that fire on a miss too (executor 44B46B / 44B488): domeleeattack (7:
//   chance to cast on attack) and attackedinmelee (3: attacker takes damage incl.
//   Thorns' flat part, Shiver Armor): run with the record's hit flag cleared, as on
//   the miss it would have been.
// - 450C90 (leech, only from 44D038), at entry, the record fields it and the rest
//   of 44CE80 read: life/mana/stamina leech and drain (+0x120/+0x124/+0x128),
//   stun (+0x12C), chill (+0x118) and freeze (+0x11C) lengths, absorb heal (+0x130,
//   451820): zeroed.
// - monster mods' on-hit functions 4990C0 (44D04F) and thorns 436FC0 / 436FE0
//   (4395E0 with thorns_percent 131 / missile_thorns_percent 208, landed hits only):
//   skipped.
// - prevent monster heal and defense per hit (the player's, as the on-hit gate).
// - item wear 4526C0 (executor 44B44B, landed melee hits only: armor 10%, weapon 4%):
//   skipped. Missiles never wear an item natively. A block's shield wear is the
//   designed exception and stays.
// The local player's arrows and throws (missile hit checks forced by
// contact_hits) are gated the same way in Unleashed, and attack rating scales
// their damage at the life write like a swing's (ar_damage).
// Absorb skills (absorbdamage, 11, fired in 44DF10 at 44EB3A before the total is
// summed: Energy Shield, Bone Armor, Cyclone Armor, Psychic Ward, Health Link) run
// on the forced contact's damage already scaled by its factor, then what they
// leave is scaled back, so they soak and drain at native-average rates while the
// life write still applies the factor once.
// Critical and deadly strike are not gated: they multiply the damage the chance
// already scales. Damage over time - poison (+0x38 rate, +0x3C length, the
// +0x40 list; 4518C0) and burn (+0x24, +0x28; 451380) - is damage that never
// reaches the scaled life write: scaled by the hit's factor on every contact.
struct MissScope {uintptr_t attacker=0,defender=0;uint64_t at=0;bool missile=false;};
thread_local MissScope missScope;                     // the would-miss attack running on this thread
thread_local bool forcedCheck=false,forcedGate=false,forcedMissile=false;
thread_local int nativeHitRoll=-1,nativeHitChance=-1;   // this forced check's real roll and clamped chance
thread_local int lastSwingVerdict=-1;                   // the latest forced swing check: 1 would hit, 0 would miss, -1 unknown
thread_local int lastIncomingVerdict=-1;                // the same for the latest forced incoming hit (guard trace)
thread_local uintptr_t damageDefender=0;
std::atomic<unsigned> verdictInHit{0},verdictInMiss{0},verdictSwingHit{0},verdictSwingMiss{0},verdictShotHit{0},verdictShotMiss{0},verdictUnknown{0},dotScaled{0};
std::atomic<unsigned> absorbScaled{0},shotScaled{0},shotUnseen{0};std::atomic<int> lastShotChance{-1};
std::atomic<bool> shotRules{false};   // Unleashed: the player's arrows and throws follow the contact rules
// The local player's latest forced missile hit (arrow, throw), for its life write.
struct ShotHit {uintptr_t attacker=0,defender=0;int chance=-1;uint64_t at=0;};
thread_local ShotHit shotHit;
// Unleashed trigger/button melee (ConfigureButtonRules): the local player's latest landed
// melee hit check outside a physical swing, for its life write: its recovery since the
// last swing (melee::ButtonHit) and, unless Smite, its attack rating. Consumed once.
struct ButtonHit {uintptr_t attacker=0,defender=0;int chance=-1;float recovery=1;bool ar=true;uint64_t at=0;};
thread_local ButtonHit buttonHit;
// A trigger hit inside a swing's cycle: its on-hit effects are off, for its own call chain
// on this thread (the same window as the would-hit gate's), like a gated swing's.
struct ButtonGate {uintptr_t attacker=0,target=0;uint64_t at=0;};
thread_local ButtonGate buttonOnHit;
std::atomic<bool> buttonRules{false};
std::atomic<unsigned> verdictButtonHit{0},verdictButtonMiss{0},buttonScaled{0},buttonUnseen{0};
std::atomic<int> lastButtonChance{-1};std::atomic<float> lastButtonFactor{1};
std::atomic<unsigned> missEvents{0},missAsMiss{0},missFields{0},missUmods{0},missThorns{0},missWear{0},missHeal{0},missDefense{0};
constexpr int DamagedInMelee=1,DamagedByMissile=2,AttackedInMelee=3,DoMissileDamage=6,AbsorbDamage=11;   // Events.txt rows
bool absorbReady=false;
double ContactFactor(uintptr_t attacker,uintptr_t defender);
uint32_t AbsorbScaled(uintptr_t game,uintptr_t unit,uintptr_t source,uintptr_t damage,double factor);
bool missEventsReady=false;   // the hit-only / miss-too event sites match this build
bool MissGated(uintptr_t attacker,uintptr_t defender);
bool MissGatedDefender(uintptr_t defender);
bool OnHitOff(uintptr_t attacker,uintptr_t target);
bool OnHitOffAttacker(uintptr_t attacker);
bool OnHitOffTarget(uintptr_t target);
uint32_t Trigger(uintptr_t game,int event,uintptr_t unit,uintptr_t source,uintptr_t damage) {
    // domeleedamage / domeleeattack: unit = the attacker, source = its defender.
    if((event==DoMeleeDamage||event==DoMeleeAttack)&&OnHitOff(unit,source)) {
        ++(event==DoMeleeDamage?gatedMeleeDamage:gatedMeleeAttack);return 0;
    }
    // Absorb skills soak the forced contact's damage at its factor (unit = the struck unit).
    if(event==AbsorbDamage&&absorbReady&&damage)
        if(const double factor=ContactFactor(source,unit);factor!=1&&factor>0)return AbsorbScaled(game,unit,source,damage,factor);
    // Would-have-hit gate. 5/6/7: unit = the attacker; 1/2/3: unit = the struck unit.
    if(missEventsReady&&missScope.attacker) {
        const bool byAttacker=event==DoMeleeDamage||event==DoMissileDamage||event==DoMeleeAttack;
        const bool byStruck=event==DamagedInMelee||event==DamagedByMissile||event==AttackedInMelee;
        if((byAttacker&&MissGated(unit,source))||(byStruck&&MissGated(source,unit))) {
            if(event!=DoMeleeAttack&&event!=AttackedInMelee){++missEvents;return 0;}
            // Fired natively on a miss too: run it as the miss it would have been.
            uint16_t flags=0;
            if(damage&&framevr::ReadMemory(&flags,reinterpret_cast<const void*>(damage+4),sizeof flags)&&(flags&1)) {
                const uint16_t miss=uint16_t(flags&~1u);
                if(framevr::WriteMemory(reinterpret_cast<void*>(damage+4),&miss,sizeof miss)) {
                    ++missAsMiss;
                    const uint32_t result=originalTrigger(game,event,unit,source,damage);
                    uint16_t now=0;
                    if(framevr::ReadMemory(&now,reinterpret_cast<const void*>(damage+4),sizeof now)) {
                        now|=1;framevr::WriteMemory(reinterpret_cast<void*>(damage+4),&now,sizeof now);   // the landed contact again
                    }
                    return result;
                }
            }
        }
    }
    return originalTrigger(game,event,unit,source,damage);
}
uintptr_t Curse(uintptr_t curse) {
    // Prevent monster heal from a gated swing (the curse record names its caster at +0, its target at +8).
    uintptr_t units[2]{};
    if((onHitGated||buttonOnHit.attacker||missScope.attacker)&&reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(PreventHealReturn)&&
       framevr::ReadMemory(units,reinterpret_cast<const void*>(curse),sizeof units)) {
        if(OnHitOff(units[0],units[1])){++gatedHeal;return 0;}
        if(MissGated(units[0],units[1])){++missHeal;return 0;}
    }
    return originalCurse(curse);
}
// The riposte's own sound: a WAV next to d2r_vr.ini, played from memory at
// the game's own volume (Settings.json: Master x the active preset's Combat).
std::mutex riposteSoundMutex;std::vector<std::vector<uint8_t>> riposteWavs; // every buffer kept alive: one may be playing
std::vector<uint8_t> riposteSource;float riposteGain=-1,riposteExtra=1;
std::atomic<const uint8_t*> riposteWav{nullptr};
// 16-bit PCM WAV at this gain (the source untouched). riposteSoundMutex held.
void PublishRiposteWav(float gain) {
    if(riposteSource.empty()){riposteWav=nullptr;return;}
    std::vector<uint8_t> w=riposteSource;
    for(size_t i=12;i+8<=w.size();) {   // walk the chunks to "data"
        uint32_t n=0;memcpy(&n,w.data()+i+4,4);
        if(!memcmp(w.data()+i,"data",4)) {
            for(size_t k=i+8;k+1<i+8+n&&k+1<w.size();k+=2) {
                int16_t v;memcpy(&v,w.data()+k,2);
                v=int16_t(std::clamp(float(v)*gain,-32768.f,32767.f));memcpy(w.data()+k,&v,2);
            }
            break;
        }
        i+=8+n+(n&1);
    }
    riposteWavs.push_back(std::move(w));riposteWav=riposteWavs.back().data();riposteGain=gain;
}
std::wstring GameSettingsPath() {
    wchar_t home[MAX_PATH];if(!GetEnvironmentVariableW(L"USERPROFILE",home,MAX_PATH))return {};
    return std::wstring(home)+L"\\Saved Games\\Diablo II Resurrected\\Settings.json";
}
// D2R's Settings.json: "Master Volume" x the active "Advanced Audio Preset"'s Combat slider.
float GameCombatVolume() {
    std::string text;
    if(FILE* f=_wfopen(GameSettingsPath().c_str(),L"rb")){char buf[4096];size_t n;while((n=fread(buf,1,sizeof buf,f))>0&&text.size()<256*1024)text.append(buf,n);fclose(f);}
    auto number=[&](const std::string& key,float fallback){
        const auto at=text.find("\""+key+"\"");if(at==std::string::npos)return fallback;
        const auto colon=text.find(':',at);if(colon==std::string::npos)return fallback;
        return float(atof(text.c_str()+colon+1));
    };
    const int preset=int(number("Advanced Audio Preset",0));
    const float master=number("Master Volume",100),combat=number("Audio AdvancedCombat Preset "+std::to_string(preset),100);
    return std::clamp(master/100.f,0.f,1.f)*std::clamp(combat/100.f,0.f,1.f);
}
// UI thread, every 2 s: follow the game's volume sliders when it saves them.
void FollowGameVolume() {
    static ULONGLONG checkAt=0;static FILETIME seen{};
    const auto now=GetTickCount64();if(now<checkAt)return;checkAt=now+2000;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    const bool known=GetFileAttributesExW(GameSettingsPath().c_str(),GetFileExInfoStandard,&a);
    std::lock_guard lock(riposteSoundMutex);
    if(riposteSource.empty())return;
    if(riposteGain>=0&&(!known||!CompareFileTime(&a.ftLastWriteTime,&seen)))return;
    if(known)seen=a.ftLastWriteTime;
    const float gain=GameCombatVolume()*riposteExtra;
    if(gain!=riposteGain) {
        PublishRiposteWav(gain);
        char line[120];snprintf(line,sizeof line,"Frame riposte: sound at %.0f%% (game Master x Combat x riposte_volume)",gain*100);
        framevr::logging::Write(line);
    }
}
void PlayRiposteSound() {
    using PlaySoundFn=BOOL(WINAPI*)(LPCWSTR,HMODULE,DWORD);
    static const PlaySoundFn play=[]{HMODULE m=LoadLibraryW(L"winmm.dll");return m?reinterpret_cast<PlaySoundFn>(GetProcAddress(m,"PlaySoundW")):nullptr;}();
    if(const uint8_t* wav=riposteWav.load();wav&&play)play(reinterpret_cast<LPCWSTR>(wav),nullptr,0x0004|0x0001|0x0002); // SND_MEMORY|SND_ASYNC|SND_NODEFAULT
}
// [gameplay] enemies_always_hit: monsters' to-hit rolls against the local
// player always connect (same scratch-seed path as the player's own hits), so
// only the physical shield decides whether they land. Default on in Unleashed;
// applied live.
std::atomic<bool> enemiesAlwaysHit{false};std::atomic<unsigned> forcedHits{0};
// Incoming defense: the native to-hit chance of a forced incoming hit (read at
// 427EA0 like a swing's) scales that attack's life damage - DefenseFactor. The
// hit check, block and damage application run synchronously on the game
// thread, so the landed hit waits here for its life write, consumed once.
// IncomingWindowMs only bounds a hit whose damage never arrived (blocked).
constexpr uint64_t IncomingWindowMs=50;
struct IncomingHit {uintptr_t attacker=0,defender=0;int chance=-1;uint64_t at=0;};
thread_local IncomingHit incomingHit;thread_local bool incomingCheck=false;thread_local int incomingChance=-1;
thread_local uintptr_t damageAttacker=0;
std::atomic<unsigned> defenseScaled{0},defenseUnseen{0};std::atomic<int> lastDefenseChance{-1};
// [gameplay] guard_break_chip: while the guard is broken (guard strain full) a
// block becomes a chip - the hit lands natively (damage reduction, resists,
// absorb; armor wear; when-struck procs) and its life damage is scaled by
// (1 - chance to block) x guard_break_chip_scale, on top of the Defense factor.
// Only the physical part (+0x18) is chipped: elemental damage and its effects
// (poison, chill...) land as normal, Defense-scaled like any hit.
// CTB is the native block rate 347DA0(defender) the block roll uses, divided
// by 3 while running as native does, capped at 75.
// The hit sound is left native (2026-10-19); in Unleashed the hero's hit recovery
// itself is refused by default ([gameplay] player_flinch, melee.cpp, 2026-10-22).
// On by default in Unleashed; [debug] guard_trace logs each chip.
constexpr uint64_t BlockRateRva=0x347da0;
using BlockRateFn=int(*)(uintptr_t);
std::atomic<bool> chipEnabled{false};std::atomic<float> chipScale{1};
std::atomic<unsigned> chipHits{0},chipLanded{0};std::atomic<int> lastChipRate{-1};
struct ChipHit {uintptr_t attacker=0,defender=0;float factor=1;int mana=-1;uint64_t at=0;};
thread_local ChipHit chipHit;
framevr::guard::Tuning GuardTuning() {framevr::guard::Tuning t;t.parryMs=guardParryMs.load(std::memory_order_relaxed);return t;}
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
// Would-have-hit gate: this attacker striking this defender is the would-miss
// attack running on this thread. Inside 44CE80 a missile's own unit (type 3) may
// name the attacker, as at the incoming life write.
bool MissGated(uintptr_t attacker,uintptr_t defender) {
    const auto& s=missScope;
    if(!s.attacker||!defender||defender!=s.defender||GetTickCount64()-s.at>IncomingWindowMs)return false;
    if(attacker==s.attacker)return true;
    uint32_t type=~0u;
    return s.missile&&Read(attacker,type)&&type==3;
}
bool MissGatedDefender(uintptr_t defender) {
    return missScope.attacker&&defender&&defender==missScope.defender&&GetTickCount64()-missScope.at<=IncomingWindowMs;
}
// The on-hit gate: this attacker's on-hit effects on this target are off on this
// thread - around a gated swing's native attack (GateOnHit), or in the call chain of a
// trigger hit inside a swing's cycle (buttonOnHit, within the would-hit gate's window).
bool ButtonGateOpen() {return buttonOnHit.attacker&&GetTickCount64()-buttonOnHit.at<=IncomingWindowMs;}
bool OnHitOff(uintptr_t attacker,uintptr_t target) {
    if(onHitGated&&attacker==onHitGated&&target==onHitTarget)return true;
    return buttonOnHit.attacker&&attacker==buttonOnHit.attacker&&target==buttonOnHit.target&&ButtonGateOpen();
}
bool OnHitOffAttacker(uintptr_t attacker) {
    if(onHitGated&&attacker==onHitGated)return true;
    return buttonOnHit.attacker&&attacker==buttonOnHit.attacker&&ButtonGateOpen();
}
bool OnHitOffTarget(uintptr_t target) {
    if(onHitGated&&target==onHitTarget)return true;
    return buttonOnHit.attacker&&target==buttonOnHit.target&&ButtonGateOpen();
}
// A trigger hit's life factor: its recovery, and its attack rating (not Smite).
double ButtonFactor(const ButtonHit& hit) {
    double factor=hit.recovery;
    if(hit.ar&&hit.chance>=0&&arDamage.load(std::memory_order_relaxed))factor*=ArFactor(hit.chance,arFloor.load(std::memory_order_relaxed));
    return factor;
}
// 450C90 (game, attacker, defender, record): leech, called once from 44CE80
// (44D038) after the hit-only events and before every other effect field is read.
using EffectsFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
using UmodFn=uint64_t(*)(uintptr_t,uintptr_t);
using ThornsFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
constexpr uint64_t EffectsRva=0x450c90,UmodRva=0x4990c0,ThornsRva=0x436fc0,MissileThornsRva=0x436fe0;
constexpr uint8_t EffectsSig[]={0x40,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x58,0x41,0x83,0xb9,0x20,0x01,0x00,0x00,0x00};
constexpr uint8_t UmodSig[]={0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x20};
constexpr uint8_t ThornsSig[]={0x48,0x83,0xec,0x38,0xc7,0x44,0x24,0x20,0x83,0x00,0x00,0x00};
constexpr uint8_t MissileThornsSig[]={0x48,0x83,0xec,0x38,0xc7,0x44,0x24,0x20,0xd0,0x00,0x00,0x00};
// Where 44CE80 and the executors read what the gate changes, as this build has it:
// the 1/2/6 events, attackedinmelee, the umod call and the absorb, stun, chill,
// freeze, poison and burn reads; thorns and item wear calls in the executors.
constexpr GateSite MissEventSites[]={
    {0x44cfb2,{0x4c,0x8b,0xce,0x48,0x89,0x7c,0x24,0x20,0x4d,0x8b,0xc6,0xba,0x06,0x00,0x00,0x00,0x49,0x8b,0xcf,0xe8,0xa6,0x05,0x00,0x00},24},
    {0x44cfca,{0xba,0x02,0x00,0x00,0x00,0xeb,0x1d,0x4c,0x8b,0xce,0x48,0x89,0x7c,0x24,0x20,0x4d,0x8b,0xc6,0xba,0x05,0x00,0x00,0x00,0x49,0x8b,0xcf,0xe8,0x87,0x05,0x00,0x00,
               0xba,0x01,0x00,0x00,0x00,0x4d,0x8b,0xce,0x48,0x89,0x7c,0x24,0x20,0x4c,0x8b,0xc6,0x49,0x8b,0xcf,0xe8,0x6f,0x05,0x00,0x00},55},
    {0x44b470,{0x48,0x8d,0x44,0x24,0x30,0x4c,0x8b,0xce,0x4d,0x8b,0xc6,0x48,0x89,0x44,0x24,0x20,0xba,0x03,0x00,0x00,0x00,0x48,0x8b,0xcf,0xe8,0xe3,0x20,0x00,0x00},29}};
constexpr GateSite EffectSites[]={
    {0x44d03d,{0x41,0x83,0xfd,0x01,0x75,0x11,0x44,0x84,0x6f,0x04,0x74,0x0b,0x49,0x8b,0xd6,0x49,0x8b,0xcf,0xe8,0x6c,0xc0,0x04,0x00},23},
    {0x44d054,{0x8b,0x97,0x30,0x01,0x00,0x00,0x48,0x8b,0xce,0xe8,0xbe,0x47,0x00,0x00},14},
    {0x44d098,{0x8b,0x9f,0x24,0x01,0x00,0x00,0x85,0xdb},8},
    {0x44d0ce,{0x8b,0x9f,0x28,0x01,0x00,0x00,0x85,0xdb},8},
    {0x44d10f,{0x44,0x8b,0x87,0x2c,0x01,0x00,0x00,0x48,0x8b,0xd6,0x49,0x8b,0xce,0xe8,0x0f,0x4b,0x00,0x00,0x44,0x8b,0x87,0x18,0x01,0x00,0x00,0x48,0x8b,0xd6,0x49,0x8b,0xce,0xe8,0x7d,0x38,0x00,0x00,
               0x44,0x8b,0x87,0x1c,0x01,0x00,0x00,0x48,0x8b,0xd6,0x49,0x8b,0xce,0xe8,0x2b,0x44,0x00,0x00},54},
    {0x44d1cf,{0x48,0x8b,0x5f,0x40,0x33,0xc0,0x44,0x8b,0xe0,0x44,0x8b,0xe8,0x48,0x8b,0x47,0x48,0x48,0x8d,0x0c,0x40},20},
    {0x44d237,{0x44,0x8b,0x4f,0x3c,0x44,0x8b,0x47,0x38},8},
    {0x44d271,{0x44,0x8b,0x4f,0x28,0x48,0x8b,0xd6,0x44,0x8b,0x47,0x24,0x49,0x8b,0xce},14}};
constexpr GateSite ThornsSites[]={
    {0x44b4d3,{0x4c,0x8d,0x4c,0x24,0x30,0x4d,0x8b,0xc6,0x48,0x8b,0xd6,0x48,0x8b,0xcf,0xe8,0xda,0xba,0xfe,0xff},19},
    {0x4630b1,{0x4c,0x8b,0xcb,0x4d,0x8b,0xc7,0x48,0x8b,0xd6,0x49,0x8b,0xce,0xe8,0x1e,0x3f,0xfd,0xff},17}};
constexpr GateSite AbsorbSites[]={
    {0x44eb25,{0x48,0x8b,0x4c,0x24,0x30,0x4d,0x8b,0xcd,0x4c,0x8b,0xc6,0x4c,0x89,0x7c,0x24,0x20,0xba,0x0b,0x00,0x00,0x00,0xe8,0x31,0xea,0xff,0xff},26},
    {0x44ec71,{0x41,0x8b,0x47,0x30,0x4d,0x8b,0xcd,0x41,0x03,0x47,0x34,0x4c,0x8b,0xc6,0x41,0x03,0x47,0x18,0xba,0x10,0x00,0x00,0x00,0x41,0x03,0x47,0x2c,
               0x41,0x03,0x47,0x38,0x41,0x03,0x47,0x20},35}};
constexpr GateSite WearSites[]={
    {0x44b438,{0x44,0x38,0xa4,0x24,0x20,0x02,0x00,0x00,0x74,0x0e,0x4d,0x8b,0xc6,0x48,0x8b,0xd6,0x48,0x8b,0xcf,0xe8,0x70,0x72,0x00,0x00},24}};
EffectsFn originalEffects=nullptr;UmodFn originalUmod=nullptr;ThornsFn originalThorns=nullptr,originalMissileThorns=nullptr;
bool effectsIn=false,umodIn=false,thornsIn=false,missileThornsIn=false,missWearReady=false,missLogged=false;
// The life factor of a physical swing on this monster, as SetStat applies it at
// the life write (a riposte: clean, plus its bonus). Peeks; consumes nothing.
double SwingLifeFactor(uint32_t monster) {
    if(riposteMonster.load()==monster&&GetTickCount64()<riposteUntil.load())return 1+riposteBonus.load();
    double factor=1;
    if(arDamage.load(std::memory_order_relaxed)&&swingChance>=0)factor*=ArFactor(swingChance,arFloor.load(std::memory_order_relaxed));
    return factor*swingRecovery;
}
// The factor a forced contact's life damage gets: the incoming Defense factor,
// the swing's, or an arrow's / throw's attack rating. 1 = not a forced contact.
// Inside 44CE80 / 44DF10 a missile's own unit (type 3) may name the attacker.
double ContactFactor(uintptr_t attacker,uintptr_t defender) {
    const auto now=GetTickCount64();
    uint32_t type=~0u;
    const auto striker=[&](uintptr_t expected){return attacker==expected||(Read(attacker,type)&&type==3);};
    if(incomingHit.attacker&&defender==incomingHit.defender&&now-incomingHit.at<=IncomingWindowMs&&incomingHit.chance>=0&&
       striker(incomingHit.attacker))return DefenseFactor(incomingHit.chance);
    if(shotHit.attacker&&defender==shotHit.defender&&now-shotHit.at<=IncomingWindowMs&&shotHit.chance>=0&&
       arDamage.load(std::memory_order_relaxed)&&striker(shotHit.attacker))return ArFactor(shotHit.chance,arFloor.load(std::memory_order_relaxed));
    if(buttonHit.attacker&&attacker==buttonHit.attacker&&defender==buttonHit.defender&&now-buttonHit.at<=IncomingWindowMs)return ButtonFactor(buttonHit);
    if(swingTarget!=NoSwing) {
        uint32_t a=~0u,d[3]{};
        if(Read(attacker,a)&&a==0&&Read(defender,d)&&d[0]==1&&d[2]==swingTarget)return SwingLifeFactor(swingTarget);
    }
    return 1;
}
// absorbdamage (11) on a forced contact: the absorb skills see the damage
// components 44DF10 sums into the total (physical, fire, lightning, magic, cold,
// poison) at the contact's factor; what they leave is scaled back by it, so the
// life write's factor still applies once. An untouched component is restored exactly.
uint32_t AbsorbScaled(uintptr_t game,uintptr_t unit,uintptr_t source,uintptr_t damage,double factor) {
    static constexpr int Parts[]={0x18,0x20,0x2c,0x30,0x34,0x38};
    int32_t before[6]{},scaled[6]{};bool ok[6]{};
    for(int i=0;i<6;++i)if(Read(damage+Parts[i],before[i])&&before[i]>0) {
        scaled[i]=int32_t(std::min(double(before[i])*factor+.5,2147483647.));
        ok[i]=framevr::WriteMemory(reinterpret_cast<void*>(damage+Parts[i]),&scaled[i],sizeof scaled[i]);
    }
    const uint32_t result=originalTrigger(game,AbsorbDamage,unit,source,damage);
    bool changed=false;
    for(int i=0;i<6;++i)if(ok[i]) {
        int32_t after=0;if(!Read(damage+Parts[i],after))continue;
        int32_t back=before[i];
        if(after!=scaled[i]){back=after<=0?after:int32_t(std::min(double(after)/factor+.5,2147483647.));changed=true;}
        framevr::WriteMemory(reinterpret_cast<void*>(damage+Parts[i]),&back,sizeof back);
    }
    if(changed)++absorbScaled;
    return result;
}
void ScaleRecordDots(uintptr_t record,double factor) {
    bool scaled=false;
    const auto scale=[&](uintptr_t at){
        int32_t rate=0;
        if(!Read(at,rate)||rate<=0)return;
        const int32_t to=combat::ScaleDamageOverTime(rate,factor);
        if(to!=rate&&framevr::WriteMemory(reinterpret_cast<void*>(at),&to,sizeof to))scaled=true;
    };
    int32_t length=0;
    if(Read(record+0x3c,length)&&length>0) {
        // 44D1CF: each list entry {state, rate, length} is applied, then the rest of +0x38 / +0x3C.
        scale(record+0x38);
        uint64_t list[2]{};   // begin, count (12-byte entries)
        if(Read(record+0x40,list)&&list[0]&&list[1]&&list[1]<=16)
            for(uint64_t i=0;i<list[1];++i)scale(uintptr_t(list[0]+i*12+4));
    }
    if(Read(record+0x28,length)&&length>0)scale(record+0x24);
    if(scaled)++dotScaled;
}
uint64_t DamageEffects(uintptr_t game,uintptr_t attacker,uintptr_t defender,uintptr_t record) {
    if(record) {
        if(MissGated(attacker,defender)) {
            // Leech/drain, stun, chill, freeze and absorb heal: only a landed hit's.
            static constexpr int Fields[]={0x118,0x11c,0x120,0x124,0x128,0x12c,0x130};
            const int32_t none=0;
            for(const int at:Fields) {
                int32_t v=0;
                if(Read(record+at,v)&&v&&framevr::WriteMemory(reinterpret_cast<void*>(record+at),&none,sizeof none))++missFields;
            }
        }
        if(const double factor=ContactFactor(attacker,defender);factor!=1)ScaleRecordDots(record,factor);
    }
    return originalEffects(game,attacker,defender,record);
}
uint64_t MonsterOnHit(uintptr_t game,uintptr_t monster) {
    // The attacking monster's mods' on-hit functions (MonUMod), on its landed hit only.
    if(damageRecord&&MissGated(monster,damageDefender)){++missUmods;return 0;}
    return originalUmod(game,monster);
}
uint64_t Thorns(uintptr_t game,uintptr_t attacker,uintptr_t defender,uintptr_t record) {
    if(MissGated(attacker,defender)){++missThorns;return 0;}
    return originalThorns(game,attacker,defender,record);
}
uint64_t MissileThorns(uintptr_t game,uintptr_t attacker,uintptr_t defender,uintptr_t record) {
    if(MissGated(attacker,defender)){++missThorns;return 0;}
    return originalMissileThorns(game,attacker,defender,record);
}
void SetStat(uintptr_t unit,int stat,int value,int layer) {
    // Unleashed on-hit gate: a gated swing does not lower the target's Defense,
    // nor does a swing that would have missed.
    if(stat==31&&defenseGate&&reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(DefensePerHitReturn)) {
        if(OnHitOffTarget(unit)){++gatedDefense;return;}
        if(MissGatedDefender(unit)){++missDefense;return;}
    }
    // The life write of a forced monster hit on the local player: its native
    // to-hit chance against the player's Defense decides how hard it lands;
    // through a broken guard, chance to block cuts it to a chip.
    if(stat==6&&layer==0&&damageRecord&&
       ((incomingHit.attacker&&unit==incomingHit.defender)||(chipHit.attacker&&unit==chipHit.defender))&&
       damageScaleReady.load(std::memory_order_relaxed)&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(LifeWriteReturn)) {
        IncomingHit hit{};if(unit==incomingHit.defender){hit=incomingHit;incomingHit={};}
        ChipHit chipped{};if(unit==chipHit.defender){chipped=chipHit;chipHit={};}
        uint32_t source=~0u;Read(damageAttacker,source);
        const auto now=GetTickCount64();
        // The striking monster, or its missile (type 3) when the record names it.
        const auto mine=[&](uintptr_t attacker,uint64_t at){return attacker&&now-at<=IncomingWindowMs&&(damageAttacker==attacker||source==3);};
        int damage=0;double factor=1;
        if(Read(damageRecord+0x134,damage)&&damage>0) {
            if(mine(hit.attacker,hit.at)) {
                if(hit.chance<0)++defenseUnseen;
                else {factor*=DefenseFactor(hit.chance);++defenseScaled;lastDefenseChance=hit.chance;}
            }
            if(mine(chipped.attacker,chipped.at)) {
                // Chip the physical part only (+0x18, final after damage reduction).
                int physical=0;Read(damageRecord+0x18,physical);
                physical=std::clamp(physical,0,damage);
                factor*=(physical*double(chipped.factor)+(damage-physical))/damage;++chipLanded;
                if(guardTrace.load(std::memory_order_relaxed)) {
                    // Energy Shield check: mana dropping between the block and
                    // this write means it was drawn before +0x134 was final.
                    // The record's non-zero dwords locate the on-hit effect fields.
                    const int mana=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,8,0);
                    char line[1400];int n=snprintf(line,sizeof line,"Frame guard chip: damage=%d factor=%.3f mana_at_block=%d mana_now=%d record:",
                        damage,factor,chipped.mana,mana);
                    for(int off=0;off<0x140&&n>0&&n<int(sizeof line)-24;off+=4) {
                        uint32_t v=0;if(Read(damageRecord+off,v)&&v)n+=snprintf(line+n,sizeof line-n," %x=%x",off,v);
                    }
                    framevr::logging::Write(line);
                }
            }
            if(factor!=1) {
                const int before=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,6,0);
                const int64_t life=int64_t(before)-int64_t(double(damage)*factor);
                value=life>=0x100?int(life):0;   // native: under one point of life is death
            }
        }
    }
    // The life write of the local player's arrow or throw: attack rating decides
    // how hard it lands (Unleashed, ar_damage), consumed once.
    if(stat==6&&layer==0&&shotHit.attacker&&unit==shotHit.defender&&damageRecord&&damageScaleReady.load(std::memory_order_relaxed)&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(LifeWriteReturn)) {
        const ShotHit shot=shotHit;shotHit={};
        uint32_t source=~0u;Read(damageAttacker,source);
        int damage=0;
        if(arDamage.load(std::memory_order_relaxed)&&GetTickCount64()-shot.at<=IncomingWindowMs&&(damageAttacker==shot.attacker||source==3)&&
           Read(damageRecord+0x134,damage)&&damage>0) {
            if(shot.chance<0)++shotUnseen;
            else {
                const int before=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,6,0);
                const int64_t life=int64_t(before)-int64_t(double(damage)*ArFactor(shot.chance,arFloor.load(std::memory_order_relaxed)));
                value=life>=0x100?int(life):0;   // native: under one point of life is death
                ++shotScaled;lastShotChance=shot.chance;
            }
        }
    }
    // The life write of the local player's trigger/button melee hit: its recovery since
    // the last swing and its attack rating (not Smite) decide how hard it lands, consumed once.
    if(stat==6&&layer==0&&buttonHit.attacker&&unit==buttonHit.defender&&damageRecord&&damageScaleReady.load(std::memory_order_relaxed)&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(LifeWriteReturn)) {
        const ButtonHit hit=buttonHit;buttonHit={};
        int damage=0;
        if(GetTickCount64()-hit.at<=IncomingWindowMs&&damageAttacker==hit.attacker&&Read(damageRecord+0x134,damage)&&damage>0) {
            if(hit.ar&&hit.chance<0&&arDamage.load(std::memory_order_relaxed))++buttonUnseen;
            if(const double factor=ButtonFactor(hit);factor!=1) {
                const int before=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,6,0);
                const int64_t life=int64_t(before)-int64_t(double(damage)*factor);
                value=life>=0x100?int(life):0;   // native: under one point of life is death
                ++buttonScaled;lastButtonChance=hit.chance;lastButtonFactor=float(factor);
            }
        }
    }
    // The life write of a physical swing's damage: a riposte on the parried
    // monster is stronger, once.
    if(stat==6&&layer==0&&swingTarget!=NoSwing&&damageRecord&&damageScaleReady.load(std::memory_order_relaxed)&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(LifeWriteReturn)) {
        uint32_t header[3]{};int damage=0;
        if(Read(unit,header)&&header[0]==1&&header[2]==swingTarget&&Read(damageRecord+0x134,damage)&&damage>0) {
            float factor=1;
            const auto now=GetTickCount64();
            if(riposteMonster.load()==header[2]&&now<riposteUntil.load()) {
                // A riposte is a clean hit: no attack-rating or recovery penalty.
                factor=1+riposteBonus.load();riposteMonster=NoSwing;riposteUntil=0;++ripostesLanded;PlayRiposteSound();
            } else {
                // Unleashed: attack rating and recovery decide how hard it lands.
                if(arDamage.load(std::memory_order_relaxed)) {
                    if(swingChance>=0)factor*=ArFactor(swingChance,arFloor.load(std::memory_order_relaxed));
                    else ++chanceUnseen;
                }
                factor*=swingRecovery;
                if(factor!=1){++scaledHits;lastChance=swingChance;lastRecovery=swingRecovery;lastFactor=factor;}
            }
            if(factor!=1) {
                const int before=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,6,0);
                const int64_t life=int64_t(before)-int64_t(double(damage)*factor);
                value=life>=0x100?int(life):0;   // native: under one point of life is death
            }
        }
    }
    originalSetStat(unit,stat,value,layer);
}
uint64_t HitChance(uintptr_t game,uintptr_t attacker,uintptr_t defender,int chance,int roll) {
    // Only the hit check of the physical swing running on this thread.
    if(swingTarget!=NoSwing) {
        uint32_t a=~0u,d[3]{};
        if(Read(attacker,a)&&a==0&&Read(defender,d)&&d[0]==1&&d[2]==swingTarget)swingChance=chance;
    }
    if(incomingCheck)incomingChance=chance;   // the forced incoming hit check running on this thread
    // Would-have-hit gate: decided here, before the rest of the hit check (prevent
    // heal at 44BE5C) and the attack's fill, damage and executor.
    if(forcedCheck) {
        nativeHitChance=chance;
        if(forcedGate&&nativeHitRoll>=0)
            missScope=WouldHit(nativeHitRoll,chance)?MissScope{}:MissScope{attacker,defender,GetTickCount64(),forcedMissile};
    }
    return originalHitChance(game,attacker,defender,chance,roll);
}
uint64_t ApplyDamage(uintptr_t game,uintptr_t attacker,uintptr_t defender,int flag,uintptr_t damage) {
    const auto savedDefender=damageDefender;damageDefender=defender;
    struct Restore {uintptr_t v;~Restore(){damageDefender=v;}} restoreDefender{savedDefender};
    // Unleashed on-hit gate: a gated swing (or trigger hit) leeches nothing (450C90 reads these two).
    if(!flag&&leechGate&&damage&&OnHitOff(attacker,defender)) {
        int32_t leech[2]{};const int32_t none[2]{};
        if(framevr::ReadMemory(leech,reinterpret_cast<const void*>(damage+0x120),sizeof leech)&&(leech[0]||leech[1])&&
           framevr::WriteMemory(reinterpret_cast<void*>(damage+0x120),none,sizeof none))++gatedLeech;
    }
    const auto saved=damageRecord;const auto savedAttacker=damageAttacker;damageRecord=damage;damageAttacker=attacker;
    const auto result=originalApplyDamage(game,attacker,defender,flag,damage);
    damageRecord=saved;damageAttacker=savedAttacker;return result;
}
// Set only by melee.cpp around ONE physical swing's native basic attack
// (synchronous, game thread). Missiles and button attacks never set it.
thread_local bool nativeRolls=false;
bool Qualifies(uintptr_t attacker,uintptr_t defender) {
    if(nativeRolls||!enabled.load(std::memory_order_relaxed)||!ready.load(std::memory_order_relaxed))return false;
    const uint64_t identity=localIdentity.load();
    const auto session=localSession.load();
    if(!identity||!session||session!=gamestate::LocalPlayer()||uint32_t(GetTickCount())-uint32_t(identity>>32)>1500)return false;
    uint32_t a[3]{},d=~0u;
    return Read(attacker,a)&&Read(defender,d)&&PlayerContact(uint32_t(identity),a[0],a[2],d);
}
struct RollContext {uintptr_t attacker=0;bool missile=false;};
thread_local RollContext roll;
thread_local Seed acceptedRoll;
void IncomingRoll(uintptr_t attacker,uintptr_t defender,bool hit);
int Hit(uintptr_t attacker,uintptr_t defender,int accuracy,int missile) {
    const auto saved=roll;const bool savedMelee=meleeHit;
    roll={};
    const bool local=Qualifies(attacker,defender);
    // A Classic swing's own attack runs under NativeRolls, so local is false
    // there: only trigger/button melee reaches this native melee branch.
    meleeHit=local&&!missile&&meleeRolls.load(std::memory_order_relaxed);
    if(meleeHit) {
        nativeMelee.fetch_add(1,std::memory_order_relaxed);
        buttonMeleeSession=gamestate::LocalPlayer();buttonMeleeAt=GetTickCount64();
    } else if(local) {
        roll={attacker,missile!=0};
        (missile?missileChecks:meleeChecks).fetch_add(1,std::memory_order_relaxed);
    } else if(enemiesAlwaysHit.load(std::memory_order_relaxed)&&enabled.load(std::memory_order_relaxed)&&ready.load(std::memory_order_relaxed)) {
        uint32_t id=0,a[3]{},d[3]{};
        if(LocalPlayerId(id)&&Read(attacker,a)&&Read(defender,d)&&IncomingContact(id,a[0],d[0],d[2])) {
            roll={attacker,missile!=0};forcedHits.fetch_add(1,std::memory_order_relaxed);
            incomingCheck=true;incomingChance=-1;
        }
    }
    const bool forcedIncoming=incomingCheck;
    // A new attack of the local player (swing, trigger, arrow): an earlier trigger hit's
    // gate, or its damage record when no life write consumed it, never outlives it.
    if(local){buttonOnHit={};buttonHit={};}
    // Would-have-hit gate: a forced incoming hit, or the local player's physical
    // swing on its target (not a riposte: that is a clean hit), arrow, throw or
    // trigger/button melee hit (not Smite: it always hits natively).
    bool swingGate=false,shotGate=false,buttonGate=false;
    if(roll.attacker&&local&&!missile&&swingTarget!=NoSwing) {
        uint32_t d[3]{};
        swingGate=Read(defender,d)&&d[0]==1&&d[2]==swingTarget&&!RiposteDue(swingTarget);
    }
    // The local player's arrow or throw (contact_hits): Unleashed only.
    if(roll.attacker&&local&&missile&&shotRules.load(std::memory_order_relaxed)) {
        uint32_t d=~0u;
        shotGate=Read(defender,d)&&d==1;
    }
    // The local player's melee hit check outside a physical swing (the trigger's basic
    // attack or melee skill, Smite): a stroke on the swing clocks (Unleashed only).
    bool buttonStroke=false;melee::ButtonStroke button{};
    if(roll.attacker&&local&&!missile&&swingTarget==NoSwing&&buttonRules.load(std::memory_order_relaxed)) {
        uint32_t d[3]{};
        if(Read(defender,d)&&d[0]==1) {
            button=melee::ButtonHit(d[2]);buttonStroke=true;buttonGate=!button.smite;
            if(!button.onHit)buttonOnHit={attacker,defender,GetTickCount64()};   // inside a swing's cycle: no on-hit effects
        }
    }
    const bool forcedHit=roll.attacker!=0;
    const bool savedCheck=forcedCheck,savedGate=forcedGate,savedMissile=forcedMissile;
    const int savedRoll=nativeHitRoll,savedChance=nativeHitChance;
    if(forcedHit) {
        forcedCheck=true;forcedGate=forcedIncoming||swingGate||shotGate||buttonGate;forcedMissile=missile!=0;nativeHitRoll=-1;nativeHitChance=-1;
        if(forcedGate)missScope={};   // this attack's own verdict replaces any earlier one
    }
    const int result=originalHit(attacker,defender,accuracy,missile);
    if(forcedHit) {
        if(forcedGate) {
            const bool known=nativeHitRoll>=0&&nativeHitChance>=0;
            const bool would=WouldHit(nativeHitRoll,nativeHitChance);
            if(!result||!known)missScope={};   // unknown: native effects, as before the gate
            if(!known)++verdictUnknown;
            else if(forcedIncoming)++(would?verdictInHit:verdictInMiss);
            else if(shotGate)++(would?verdictShotHit:verdictShotMiss);
            else if(buttonGate)++(would?verdictButtonHit:verdictButtonMiss);
            else ++(would?verdictSwingHit:verdictSwingMiss);
            if(swingGate)lastSwingVerdict=known?int(would):-1;
            else if(forcedIncoming)lastIncomingVerdict=known?int(would):-1;
        }
        // An arrow or throw that landed: its attack rating scales it at the life write.
        if(shotGate)shotHit=result?ShotHit{attacker,defender,nativeHitChance,GetTickCount64()}:ShotHit{};
        // A trigger hit that landed: its recovery and attack rating scale it at the life write.
        if(buttonStroke)buttonHit=result?ButtonHit{attacker,defender,buttonGate?nativeHitChance:-1,button.recovery,buttonGate,GetTickCount64()}:ButtonHit{};
        forcedCheck=savedCheck;forcedGate=savedGate;forcedMissile=savedMissile;nativeHitRoll=savedRoll;nativeHitChance=savedChance;
    }
    if(forcedIncoming) {   // landed: its damage is scaled at the life write
        incomingCheck=false;
        incomingHit=result?IncomingHit{attacker,defender,incomingChance,GetTickCount64()}:IncomingHit{};
    }
    roll=saved;meleeHit=savedMelee;
    if(!local&&!missile)IncomingRoll(attacker,defender,result!=0);
    return result;
}
Seed* GetSeed(uintptr_t unit) {
    Seed* native=originalSeed(unit);
    // Unleashed on-hit gate: a gated swing's critical/deadly strike rolls never
    // crit. The real stream advances once, as the inline roll would.
    if(critGate&&OnHitOffAttacker(unit)) {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        for(const auto at:CritSeedReturn)if(caller==d2rsig::Addr(at)) {
            Seed current{};
            if(!framevr::ReadMemory(&current,native,sizeof current)) {++failed;return native;}
            const Seed next=AdvanceSeed(current);
            if(!framevr::WriteMemory(native,&next,sizeof next)) {++failed;return native;}
            noCritRoll=NoCritRoll();++gatedCrits;
            return &noCritRoll;
        }
    }
    // Physical guard: only the block roll inside one judged incoming hit.
    // Advance the real stream once, exactly as the inline roll would.
    if(forced!=Forced::None&&!forcedServed&&reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(BlockSeedReturn)) {
        Seed current{};
        if(!framevr::ReadMemory(&current,native,sizeof current)) {++failed;return native;}
        const Seed next=AdvanceSeed(current);
        if(!framevr::WriteMemory(native,&next,sizeof next)) {++failed;return native;}
        forcedRoll=GuardRoll(forced==Forced::Block);forcedServed=true;
        return &forcedRoll;
    }
    // Every unrelated seed consumer is an unchanged fast path. Only this exact
    // call site inside an eligible HitSuccessful evaluation receives a local
    // zero roll, below the game's minimum 5% threshold.
    if(!roll.attacker||unit!=roll.attacker||reinterpret_cast<uintptr_t>(_ReturnAddress())!=d2rsig::Addr(HitSeedReturn))return native;
    Seed current{};
    if(!framevr::ReadMemory(&current,native,sizeof current)) {++failed;return native;}
    const Seed next=AdvanceSeed(current);
    // Advance the real stream exactly once as the original inline roll would,
    // preserving subsequent damage/critical/loot randomness. The forced roll
    // writes only thread-local scratch. No stats or save data are altered.
    if(!framevr::WriteMemory(native,&next,sizeof next)) {++failed;return native;}
    acceptedRoll={};++rolls;
    if(forcedCheck)nativeHitRoll=NativeHitRoll(current);   // the roll the game would have compared
    return &acceptedRoll;
}
bool GridPosition(uintptr_t unit,float& x,float& z) {
    uintptr_t path=0;uint16_t p[4]{};
    if(!Read(unit+0x38,path)||!Read(path,p))return false;
    x=2*(p[1]+p[0]/65536.f);z=2*(p[3]+p[2]/65536.f);return true;
}
// The attacker's rank, for guard strain and the act-boss parry gate. Same
// layout as the monster bars (cleanroom/camera/monster_bars.h,
// docs/monster-bars.md): MonsterData at unit+0x10, its special flags at +0x1a
// (superunique 0x02, champion 0x04, unique 0x08, minion 0x10) and MonStats
// row at +0; the row's id at +0 and flags at +0x3c (primeevil bit 7: the five
// act bosses). Anything unreadable is Normal. Champion is tested first: a
// Tristram champion pack (2026-10-19) read as unique, so champions carry a
// unique bit as well; `special` is logged on every guard trace to confirm.
framevr::strain::Rank AttackerRank(uintptr_t unit,uint8_t* specialOut=nullptr) {
    using framevr::strain::Rank;
    uint32_t a[2]{};uintptr_t data=0,row=0;uint8_t special=0;uint16_t rowId=0;uint32_t flags=0;
    if(specialOut)*specialOut=0xff;
    if(!Read(unit,a)||a[0]!=1||!Read(unit+0x10,data))return Rank::Normal;
    const bool special_ok=Read(data+0x1a,special);
    if(special_ok&&specialOut)*specialOut=special;
    if(Read(data,row)&&Read(row,rowId)&&rowId==a[1]&&Read(row+0x3c,flags)&&(flags&(1u<<7)))return Rank::PrimeEvil;
    if(!special_ok)return Rank::Normal;
    if(special&0x04)return Rank::Champion;
    if(special&(0x02|0x08))return Rank::Unique;
    if(special&0x10)return Rank::Minion;
    return Rank::Normal;
}
// Act bosses cannot be parried: a parry swing at one is judged as a plain guard.
bool Parryable(framevr::strain::Rank rank){return rank!=framevr::strain::Rank::PrimeEvil;}
// A monster (or its missile's owner) whose native to-hit roll already passed
// against the local player. Native = keep the stat block roll.
Guard Incoming(uintptr_t attacker,uintptr_t defender,uint32_t& monster,bool& inReach,uint64_t& parryAt,framevr::strain::Rank& rank) {
    uint8_t special=0xff;
    parryAt=0;rank=framevr::strain::Rank::Normal;
    if(!guardChecked||!guardEnabled.load(std::memory_order_relaxed)||!enabled.load(std::memory_order_relaxed)||!ready.load(std::memory_order_relaxed))return Guard::Native;
    uint32_t id=0,a[3]{},d[3]{};
    if(!LocalPlayerId(id)||!Read(attacker,a)||!Read(defender,d)||!IncomingContact(id,a[0],d[0],d[2]))return Guard::Native;
    GuardShared g;{std::lock_guard lock(guardMutex);g=guard;}
    const auto now=GetTickCount64();
    if(!g.eligible||g.session!=gamestate::LocalPlayer()||now-g.at>250)return Guard::Native;
    float ax=0,az=0,px=0,pz=0;
    if(!GridPosition(attacker,ax,az)||!GridPosition(defender,px,pz)) {++guardFallback;return Guard::Native;}
    monster=a[2];rank=AttackerRank(attacker,&special);
    using framevr::guard::Verdict;
    const framevr::math::Vec to{(ax-px)/g.upm,0,(az-pz)/g.upm};
    // when this attacker's attack began (melee.cpp's mode hook; 0 = not seen)
    const uint64_t attackAt=melee::AttackStart(monster);if(attackAt)++guardAttackKnown;
    const uint64_t opensAt=melee::ParryOpens(monster);   // its downstroke (0 = wind-up not measured yet)
    framevr::guard::Rejection reason;
    const auto v=framevr::guard::Judge(g.state,to,now,GuardTuning(),attackAt,&reason,opensAt,Parryable(rank));
    {
        std::lock_guard lock(guardMutex);
        if(guard.session==g.session) {
            const auto& p=g.state.surface;
            guard.debug.hitFrom=p.player+framevr::math::Vec{to.x,p.center.y,to.z}*g.upm;
            guard.debug.hitTo=p.player+framevr::math::Vec{p.eye.x,p.center.y,p.eye.z}*g.upm;
            guard.debug.hitAt=now;guard.debug.hit=0;guard.debugMonster=monster;
        }
    }
    if(v==Verdict::Open)++guardRejections[size_t(reason)];
    // Walking/running does not invalidate a raised physical guard. Native
    // block/dodge handling still runs below; keep the mode for diagnostics.
    uint32_t playerMode=UINT32_MAX;
    Read(defender+0xc,playerMode);
    inReach=std::hypot(to.x,to.z)<=GuardTuning().parryRange;   // melee range (for the late-swing diagnostic)
    if(v==Verdict::Parry)parryAt=g.state.snapAt;
    if(guardTrace.load(std::memory_order_relaxed)) {
        const float d=std::hypot(to.x,to.z);const auto& s=g.state;
        float cover=0;framevr::guard::Coverage(s.surface,to,GuardTuning(),&cover);
        const auto parry=framevr::guard::ParryCheck(s,to,now,GuardTuning(),attackAt,opensAt);   // why a raised guard did not parry
        char line[940];snprintf(line,sizeof line,
            "Frame guard trace: monster=%u class=%u rank=%s special=0x%02x verdict=%s reason=%s parry=%s player_mode=%u raised=%d forward=%.2f up=%.2f side=%.2f m cover_cos=%.2f attacker=%.2f m swing=%.2f m swing_cos=%.2f snap_age=%lld ms snap_spent=%d attack_age=%lld ms downstroke_at=%lld ms pose_age=%llu ms center=%.2f,%.2f,%.2f normal=%.2f,%.2f,%.2f eye=%.2f,%.2f,%.2f attacker_xz=%.2f,%.2f upm=%.3f",
            a[2],a[1],framevr::strain::RankName(rank),unsigned(special),v==Verdict::Parry?"PARRY":v==Verdict::Block?"BLOCK":"OPEN",framevr::guard::ReasonName(reason),framevr::guard::ParryMissName(parry),playerMode,int(s.raised),s.forward,s.up,s.side,cover,d,s.swing,s.swingCos,
            s.snapAt&&now>=s.snapAt?(long long)(now-s.snapAt):-1LL,int(s.snapUsed),
            attackAt&&now>=attackAt?(long long)(now-attackAt):-1LL,
            attackAt&&opensAt>=attackAt?(long long)(opensAt-attackAt):-1LL,
            (unsigned long long)(s.seenAt&&now>=s.seenAt?now-s.seenAt:0),
            s.surface.center.x,s.surface.center.y,s.surface.center.z,s.surface.normal.x,s.surface.normal.y,s.surface.normal.z,
            s.surface.eye.x,s.surface.eye.y,s.surface.eye.z,to.x,to.z,g.upm);
        const auto& p=s.surface;
        snprintf(line+strlen(line),sizeof line-strlen(line)," mesh=%d size=%.3f,%.3f m",
            int(framevr::guard::Measured(p)),2*std::sqrt(framevr::math::Dot(p.halfRight,p.halfRight)),2*std::sqrt(framevr::math::Dot(p.halfUp,p.halfUp)));
        framevr::logging::Write(line);
    }
    return v==Verdict::Parry?Guard::Parry:v==Verdict::Block?Guard::Block:Guard::Open;
}
// Item wear, from the game's own code (dump of this build, SUnitDmg.cpp,
// 2026-10-19). 4526C0(game, attacker, defender) runs for every landed hit: an
// attacking player's weapon may wear, and a struck player's armor piece is
// picked by body-location weight (head 3, torso 5, left hand 4, belt, feet and
// gloves 2) for 441B10(game, owner, item): a 10% roll for armor (4% weapons)
// against the owner's seed, 1 point off, broken at 0, synced to the client.
// A block never reaches it, so a raised shield never wore. A physical block
// runs 441B10 on the shield alone, and a chip through a broken guard has its
// struck wear redirected to the shield. [gameplay] shield_block_wear is the %
// of those contacts that get the native roll: 22 (default) gives the shield its
// native share - 4 of 18 weight x 10% = 2.2% a contact; 100 = the full 10%; 0 off.
// A parry wears nothing. 34A360(unit, file, line) is the inventory and
// 3886D0(inventory, body location) the equipped item, as 4526C0 uses them.
constexpr uint64_t StruckWearRva=0x4526c0,ItemWearRva=0x441b10,InventoryRva=0x34a360,EquippedRva=0x3886d0;
constexpr uint64_t InventoryFileRva=0x1d1a930;   // "D2Game\\src\\Unit\\SUnitDmg.cpp", as 4526C0 passes it
constexpr uint8_t StruckWearSig[]={0x40,0x53,0x41,0x54,0x41,0x55,0x48,0x81,0xec,0x90,0x00,0x00,0x00,0x48,0x8b,0x05};
constexpr uint8_t ItemWearSig[]={0x48,0x89,0x6c,0x24,0x10,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x4c};
constexpr uint8_t InventorySig[]={0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0xf1,0x48,0x85,0xc9,0x75,0x13};
constexpr uint8_t EquippedSig[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x63,0xfa,0x48,0x8b,0xd9,0x48,0x85};
constexpr uint8_t InventoryFileSig[]={'D','2','G','a','m','e','\\','s','r','c','\\','U','n','i','t','\\','S','U','n','i','t','D','m','g'};
using WearFn=void(*)(uintptr_t,uintptr_t,uintptr_t);
using InventoryFn=uintptr_t(*)(uintptr_t,const char*,int);
using EquippedFn=uintptr_t(*)(uintptr_t,int);
WearFn originalStruckWear=nullptr,originalItemWear=nullptr;bool struckWearIn=false,itemWearIn=false;
std::atomic<unsigned> clientMirrored{0},clientMissing{0};
// Every item wear (native or ours): the server item changes and is saved, but in
// this setup the client copy - what tooltips read - kept the old value until a
// relaunch (2026-10-19), the game's own armor wear included. After a wear on the
// local player's gear, the new value is copied to the client item (UI thread).
void ItemWear(uintptr_t game,uintptr_t owner,uintptr_t item) {
    const auto stat=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva));
    const int before=item?stat(item,72,0):0;
    originalItemWear(game,owner,item);
    uint32_t o[3]{},i[3]{};
    if(!item||!Read(owner,o)||o[0]!=0||!Read(item,i)||i[0]!=4)return;   // a player's item
    if(const int after=stat(item,72,0);after<before)melee::QueueClientDurability(i[2],after);
}
std::atomic<int> shieldWearShare{22};std::atomic<unsigned> shieldWearRolls{0},shieldWorn{0},chipWearRedirects{0};std::atomic<int> shieldDurability{-1};
// A chip's pending struck wear on this thread: the block decided it, 4526C0 follows.
struct ChipWear {uintptr_t defender=0;uint64_t at=0;};
thread_local ChipWear chipWear;
// Game thread: the native wear roll on the defender's left-hand item (the shield).
void WearShield(uintptr_t game,uintptr_t player) {
    if(!struckWearIn||!game||!player)return;
    // The share of contacts that get the native roll (independent of the game's seeds).
    static std::atomic<uint32_t> state{0x9e3779b9u};
    uint32_t x=state.load()^uint32_t(GetTickCount64());x^=x<<13;x^=x>>17;x^=x<<5;state=x;
    if(int(x%100)>=shieldWearShare.load(std::memory_order_relaxed))return;
    const uintptr_t inventory=reinterpret_cast<InventoryFn>(d2rsig::Addr(InventoryRva))(player,reinterpret_cast<const char*>(d2rsig::Addr(InventoryFileRva)),0xb9e);
    if(!inventory)return;
    const uintptr_t shield=reinterpret_cast<EquippedFn>(d2rsig::Addr(EquippedRva))(inventory,5);
    if(!shield)return;
    ++shieldWearRolls;
    const auto stat=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva));
    const int before=stat(shield,72,0);
    (itemWearIn?&ItemWear:reinterpret_cast<WearFn>(d2rsig::Addr(ItemWearRva)))(game,player,shield);   // its own 10% roll (and client copy)
    const int after=stat(shield,72,0);
    if(after<before)++shieldWorn;
    shieldDurability=after;
}
void StruckWear(uintptr_t game,uintptr_t attacker,uintptr_t defender) {
    if(chipWear.defender&&defender==chipWear.defender&&GetTickCount64()-chipWear.at<=IncomingWindowMs) {
        chipWear={};++chipWearRedirects;WearShield(game,defender);   // a chip is a use of the shield
        return;
    }
    // Would-have-hit gate: native wear comes with a landed melee hit only.
    if(missWearReady&&MissGated(attacker,defender)){++missWear;return;}
    originalStruckWear(game,attacker,defender);
}
// Native still runs in full (avoid/dodge, sounds; in Unleashed the hero's block
// animation is refused by default, [gameplay] player_flinch); only its block
// roll is replaced by the physical verdict.
uint64_t NativeCallerRva(uintptr_t caller) {
    // Addr(0) means unresolved, not the image base. Subtract the resolved
    // block offset so diagnostics also work when signatures relocate it.
    const auto address=d2rsig::Addr(BlockRva),offset=d2rsig::Rva(BlockRva);
    const auto base=address>offset?address-offset:0;
    return base&&caller>=base&&caller-base<0x4000000?caller-base:0;
}
int GuardedBlock(uintptr_t game,uintptr_t attacker,uintptr_t defender,int avoid,int block,Guard g,uint32_t monster,bool inReach,uint64_t parryAt,framevr::strain::Rank rank,uintptr_t caller) {
    const int32_t durability=gamestate::LeftDurability();   // before this block wears the shield
    // A broken guard leaks: the block becomes a chip (see chipEnabled).
    ChipHit chip{};
    if(g==Guard::Block&&chipEnabled.load(std::memory_order_relaxed)&&damageScaleReady.load(std::memory_order_relaxed)&&framevr::GuardBroken()) {
        int rate=reinterpret_cast<BlockRateFn>(d2rsig::Addr(BlockRateRva))(defender);
        uint32_t mode=0;
        if(Read(defender+0xc,mode)&&mode==3)rate/=3;   // running, as the native roll does
        rate=std::clamp(rate,0,75);lastChipRate=rate;
        chip={attacker,defender,(1-rate/100.f)*chipScale.load(std::memory_order_relaxed),
              reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(defender,8,0),GetTickCount64()};
        g=Guard::Open;
    }
    const auto savedForced=forced;const bool savedServed=forcedServed;
    forced=g==Guard::Open?Forced::Open:Forced::Block;forcedServed=false;
    const int result=originalBlock(game,attacker,defender,avoid,block);
    const bool served=forcedServed;forced=savedForced;forcedServed=savedServed;
    {
        std::lock_guard lock(guardMutex);
        if(guard.debugMonster==monster&&guard.session==gamestate::LocalPlayer())
            guard.debug.hit=!served?3:result==1?1:2;
    }
    if(guardTrace.load(std::memory_order_relaxed)) {
        const auto rva=NativeCallerRva(caller);
        char line[320];snprintf(line,sizeof line,
            "Frame guard native: monster=%u rank=%s verdict=%s block_flag=%d avoid_flag=%d result=%d roll_served=%d durability=%d caller=0x%llx would_hit=%d",
            monster,framevr::strain::RankName(rank),g==Guard::Parry?"PARRY":g==Guard::Block?"BLOCK":"OPEN",block,avoid,result,int(served),durability,(unsigned long long)rva,
            lastIncomingVerdict);
        framevr::logging::Write(line);
    }
    if(!served)++guardUnserved; // native never reached its block roll (dodged/avoided first, or cannot block)
    else if(chip.attacker) {
        // Lands as a chip at its life write; the shield still took it.
        chipHit=chip;++chipHits;
        framevr::ShieldGuardImpact(false,framevr::strain::RankWeight(rank));
        melee::QueueBlockSound();
        if(shieldWearShare.load(std::memory_order_relaxed)>0)chipWear={defender,GetTickCount64()};   // its struck wear goes to the shield
    }
    else if(g==Guard::Open)++guardOpen;
    else {
        ++guardBlocks;
        // Unblockable damage can visit this adapter too. Do not consume the
        // pending parry unless the native shield roll actually took place.
        if(g==Guard::Parry){std::lock_guard lock(guardMutex);framevr::guard::SpendParry(guard.state,parryAt);}
        const bool parried=g==Guard::Parry&&melee::QueueParry(monster,durability);
        framevr::ShieldGuardImpact(parried,framevr::strain::RankWeight(rank));   // a block strains the guard by the attacker's rank; a parry does not
        if(parried)++guardParries;
        else {
            // The native block's own sound never plays in first person (only the
            // haptics did, 2026-10-16): the game's block sound at the hero.
            melee::QueueBlockSound();
            WearShield(game,defender);   // the blow met the shield; a parry spares it
            // Act bosses: no late parry either (Parryable gates Judge only; a
            // blocked hit is how a late parry starts - Andariel, 2026-10-20).
            if(g==Guard::Block&&inReach&&Parryable(rank)) {
                std::lock_guard lock(guardMutex);framevr::guard::NoteBlock(guard.state,GetTickCount64());
                guard.blockMonster=monster;guard.blockDurability=durability;   // durability before this hit, for a late parry's refund
            }
        }
    }
    return result;
}
// A monster's melee to-hit result against the local player. A miss never
// reaches the block check, yet the player saw the attack and answered it: a
// timed parry swing still staggers the attacker. Native hit/miss is unchanged.
void IncomingRoll(uintptr_t attacker,uintptr_t defender,bool hit) {
    if(!guardChecked||!guardEnabled.load(std::memory_order_relaxed)||!enabled.load(std::memory_order_relaxed)||!ready.load(std::memory_order_relaxed))return;
    uint32_t id=0,a[3]{},d[3]{};
    if(!Read(attacker,a))return;
    if(a[0]==1)melee::NoteAttackRolled(a[2]);   // whatever its target: too late to stagger it before the hit
    if(!LocalPlayerId(id)||!Read(defender,d)||!IncomingContact(id,a[0],d[0],d[2]))return;
    if(hit){++incomingHits;return;}   // a hit is judged at the block check
    ++incomingMisses;
    GuardShared g;{std::lock_guard lock(guardMutex);g=guard;}
    const auto now=GetTickCount64();
    if(!g.eligible||g.session!=gamestate::LocalPlayer()||now-g.at>250)return;
    float ax=0,az=0,px=0,pz=0;
    if(!GridPosition(attacker,ax,az)||!GridPosition(defender,px,pz))return;
    const framevr::math::Vec to{(ax-px)/g.upm,0,(az-pz)/g.upm};
    const uint64_t attackAt=melee::AttackStart(a[2]);const auto t=GuardTuning();
    const uint64_t opensAt=melee::ParryOpens(a[2]);
    const auto v=framevr::guard::Judge(g.state,to,now,t,attackAt,nullptr,opensAt,Parryable(AttackerRank(attacker)));
    if(guardTrace.load(std::memory_order_relaxed)) {
        const auto& s=g.state;
        char line[340];snprintf(line,sizeof line,
            "Frame guard miss: monster=%u class=%u verdict=%s parry=%s raised=%d attacker=%.2f m snap_age=%lld ms snap_spent=%d attack_age=%lld ms downstroke_at=%lld ms",
            a[2],a[1],v==framevr::guard::Verdict::Parry?"PARRY":"MISSED",framevr::guard::ParryMissName(framevr::guard::ParryCheck(s,to,now,t,attackAt,opensAt)),
            int(s.raised),std::hypot(to.x,to.z),s.snapAt&&now>=s.snapAt?(long long)(now-s.snapAt):-1LL,int(s.snapUsed),
            attackAt&&now>=attackAt?(long long)(now-attackAt):-1LL,attackAt&&opensAt>=attackAt?(long long)(opensAt-attackAt):-1LL);
        framevr::logging::Write(line);
    }
    if(v!=framevr::guard::Verdict::Parry)return;
    {std::lock_guard lock(guardMutex);framevr::guard::SpendParry(guard.state,g.state.snapAt);}
    if(melee::QueueParry(a[2],-1)){++guardParries;++missParries;framevr::ShieldGuardImpact(true);}   // -1: no shield wear to refund
}
// A block/dodge check made inside a native melee hit check stays native.
// Outside one (or after it returns) the contact_hits bypass still applies.
int Block(uintptr_t game,uintptr_t attacker,uintptr_t defender,int avoid,int block) {
    if(meleeHit)++nativeGuards;
    else if(Qualifies(attacker,defender)) {++blocks;return 0;}
    else {
        uint32_t monster=0;bool inReach=false;uint64_t parryAt=0;framevr::strain::Rank rank{};
        if(const auto g=Incoming(attacker,defender,monster,inReach,parryAt,rank);g!=Guard::Native)
            return GuardedBlock(game,attacker,defender,avoid,block,g,monster,inReach,parryAt,rank,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    return originalBlock(game,attacker,defender,avoid,block);
}
int Dodge(uintptr_t attacker,uintptr_t defender,int avoid) {
    if(meleeHit)++nativeGuards;
    else if(Qualifies(attacker,defender)) {++dodges;return 0;}
    return originalDodge(attacker,defender,avoid);
}
}
void ObserveLocalPlayer(uintptr_t unit) {
    uint32_t header[3]{};
    if(!Read(unit,header)||header[0]!=0||header[2]==UINT32_MAX)return;
    localSession=gamestate::LocalPlayer();
    localIdentity=(uint64_t(GetTickCount())<<32)|header[2];
}
bool ObserveUiPlayer(uintptr_t unit,uint64_t session) {
    uint32_t header[3]{};
    if(!session||session!=gamestate::LocalPlayer()||!Read(unit,header)||header[0]!=0||header[2]==UINT32_MAX)return false;
    {std::lock_guard lock(localPlayerMutex);uiPlayer={unit,session,GetTickCount64(),header[2]};}
    ObserveLocalPlayer(unit);return true;
}
// UI thread: copy a worn item's durability to the local player's client item
// (equipped: head, torso, hands, belt, feet, gloves).
void MirrorClientDurability(uint32_t itemId,int value) {
    uintptr_t player=0;
    if(!struckWearIn||!originalSetStat||!LocalPlayerUnit(player)){++clientMissing;return;}
    const uintptr_t inventory=reinterpret_cast<InventoryFn>(d2rsig::Addr(InventoryRva))(player,reinterpret_cast<const char*>(d2rsig::Addr(InventoryFileRva)),0xb9e);
    if(!inventory){++clientMissing;return;}
    for(const int location:{5,3,1,4,8,9,10}) {
        const uintptr_t item=reinterpret_cast<EquippedFn>(d2rsig::Addr(EquippedRva))(inventory,location);
        uint32_t h[3]{};
        if(!item||!Read(item,h)||h[0]!=4||h[2]!=itemId)continue;
        if(reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(item,72,0)!=value){originalSetStat(item,72,value,0);++clientMirrored;}
        return;
    }
    ++clientMissing;
}
int EquippedFireDamage(int location) {
    uintptr_t player=0;
    if(!LocalPlayerUnit(player)) {
        const auto idxFn=reinterpret_cast<int(*)()>(d2rsig::Addr(nativeui::abi::LocalIndexRva));
        const auto clientFn=reinterpret_cast<uintptr_t(*)(int)>(d2rsig::Addr(nativeui::abi::ClientPlayerRva));
        if(idxFn&&clientFn) {
            const int idx=idxFn();
            if(idx>=0&&idx<8)player=clientFn(idx);
        }
    }
    if(!player)return 0;
    const auto invFn=reinterpret_cast<InventoryFn>(d2rsig::Addr(InventoryRva));
    if(!invFn)return 0;
    const uintptr_t inventory=invFn(player,reinterpret_cast<const char*>(d2rsig::Addr(InventoryFileRva)),0xb9e);
    if(!inventory)return 0;
    const auto eqFn=reinterpret_cast<EquippedFn>(d2rsig::Addr(EquippedRva));
    if(!eqFn)return 0;
    const uintptr_t item=eqFn(inventory,location);
    if(!item)return 0;
    uint32_t h[3]{};
    if(!Read(item,h)||h[0]!=4)return 0;
    const auto getStat=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva));
    if(!getStat)return 0;
    const int maxFire=getStat(item,49,0);
    const int minFire=getStat(item,48,0);
    return maxFire>0?maxFire:(minFire>0?minFire:0);
}
bool LocalPlayerUnit(uintptr_t& unit) {
    UiPlayer player;{std::lock_guard lock(localPlayerMutex);player=uiPlayer;}
    uint32_t header[3]{};
    if(!player.unit||!player.session||player.session!=gamestate::LocalPlayer()||GetTickCount64()-player.at>650||
       !Read(player.unit,header)||header[0]!=0||header[2]!=player.id)return false;
    unit=player.unit;return true;
}
bool UnitStat(uintptr_t unit,int stat,int& value) {
    if(unit<0x10000||!d2rsig::Check(GetStatRva,GetStatSig,sizeof GetStatSig))return false;
    value=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,stat,0);return true;
}
bool LocalPlayerId(uint32_t& id) {
    const auto identity=localIdentity.load();const auto session=localSession.load();
    if(!identity||!session||session!=gamestate::LocalPlayer()||uint32_t(GetTickCount())-uint32_t(identity>>32)>1500)return false;
    id=uint32_t(identity);return true;
}
void Configure(bool on) {enabled=on;}
bool NativeRolls(bool on) {const bool was=nativeRolls;nativeRolls=on;return was;}
void ConfigureMeleeRolls(bool native) {meleeRolls=native;}
bool TakeButtonMelee(uint64_t& session,uint64_t& at) {
    at=buttonMeleeAt.exchange(0);session=buttonMeleeSession.load();
    return at!=0;
}
void ConfigureGuard(bool on,uint32_t parryMs,bool trace) {guardEnabled=on;guardParryMs=parryMs;guardTrace=trace;}
void ConfigureLateParry(uint32_t ms) {guardLateMs=std::min<uint32_t>(ms,400);}   // SwingAfterBlock looks 400 ms back
void ConfigureEnemiesAlwaysHit(bool on) {enemiesAlwaysHit=on;}
void ConfigureShieldWear(int share) {shieldWearShare=std::clamp(share,0,100);}
void ConfigureGuardChip(bool on,float scale) {chipEnabled=on;chipScale=std::clamp(std::isfinite(scale)?scale:1.f,0.f,4.f);}
float GameVolume() {
    static std::mutex m;static ULONGLONG checkAt=0;static FILETIME seen{};static float volume=-1;
    std::lock_guard lock(m);
    const auto now=GetTickCount64();
    if(volume>=0&&now<checkAt)return volume;
    checkAt=now+2000;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    const bool known=GetFileAttributesExW(GameSettingsPath().c_str(),GetFileExInfoStandard,&a);
    if(volume<0||!known||CompareFileTime(&a.ftLastWriteTime,&seen)){if(known)seen=a.ftLastWriteTime;volume=GameCombatVolume();}
    return volume;
}
uint32_t PhysicalSwing(uint32_t target) {
    const auto was=swingTarget;swingTarget=target;swingChance=-1;
    if(target!=NoSwing)lastSwingVerdict=-1;   // a new swing's verdict (the restoring call keeps it for the trace)
    return was;
}
float SwingRecovery(float factor) {const float was=swingRecovery;swingRecovery=factor;return was;}
void ConfigureArDamage(bool on,float floor) {arDamage=on;arFloor=std::clamp(floor,0.f,1.f);}
void NoteRiposte(uint32_t monster) {
    if(riposteMs.load()==0)return;   // a riposte is worth having with no bonus: it is a clean hit
    riposteMonster=monster;riposteUntil=GetTickCount64()+riposteMs.load();
}
bool RiposteDue(uint32_t monster) {return monster!=NoSwing&&riposteMonster.load()==monster&&GetTickCount64()<riposteUntil.load();}
OnHitScope GateOnHit(OnHitScope scope) {
    const OnHitScope was{onHitGated,onHitTarget};
    onHitGated=scope.attacker&&scope.target?scope.attacker:0;onHitTarget=onHitGated?scope.target:0;
    missScope={};   // around one swing's native attack: a would-miss verdict never outlives it
    buttonOnHit={};buttonHit={};   // nor does a trigger hit's gate or unwritten damage
    return was;
}
int LastSwingWouldHit() {return lastSwingVerdict;}
void ConfigureShotRules(bool unleashed) {shotRules=unleashed;}
void ConfigureButtonRules(bool unleashed) {buttonRules=unleashed;}
void ConfigureRiposte(float bonus,uint32_t windowMs,float volume,const wchar_t* soundFile) {
    riposteBonus=std::clamp(bonus,0.f,4.f);riposteMs=windowMs;
    {std::lock_guard lock(riposteSoundMutex);const float v=std::clamp(volume,0.f,4.f);if(riposteExtra!=v){riposteExtra=v;riposteGain=-1;}}
    static std::wstring loaded=L"?"; // settings thread only
    if(soundFile&&loaded!=soundFile) {
        loaded=soundFile;std::vector<uint8_t> bytes;
        if(!*soundFile) {   // the default: the WAV embedded in this plugin (steamvr/assets/riposte-impact.wav)
            HMODULE module=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&PlayRiposteSound),&module);
            if(HRSRC res=module?FindResourceW(module,L"D2RVR_RIPOSTE_SOUND",MAKEINTRESOURCEW(10)):nullptr)
                if(HGLOBAL h=LoadResource(module,res))
                    if(const auto* p=static_cast<const uint8_t*>(LockResource(h)))bytes.assign(p,p+SizeofResource(module,res));
        } else if(FILE* f=_wfopen(soundFile,L"rb")) {
            fseek(f,0,SEEK_END);const long n=ftell(f);fseek(f,0,SEEK_SET);
            if(n>44&&n<8*1024*1024){bytes.resize(size_t(n));if(fread(bytes.data(),1,bytes.size(),f)!=bytes.size())bytes.clear();}
            fclose(f);
        }
        const bool wav=bytes.size()>12&&!memcmp(bytes.data(),"RIFF",4)&&!memcmp(bytes.data()+8,"WAVE",4);
        std::lock_guard lock(riposteSoundMutex);
        riposteSource=wav?std::move(bytes):std::vector<uint8_t>{};riposteGain=-1;   // published at the game's volume next tick
        if(!wav)riposteWav=nullptr;
        char line[160];snprintf(line,sizeof line,"Frame riposte: %s sound %s",*soundFile?"ini":"built-in",wav?"loaded":"missing or not a WAV; silent");
        framevr::logging::Write(line);
    }
}
// Owner parries resolved at the hit frame landed 63-657 ms after the swing
// (median 516, 2026-10-18): most swings came before the attack even began.
// Resolving as soon as both had begun staggered monsters that had barely
// raised their arms (2026-10-19); now the swing must come in the downstroke
// (melee::ParryOpens), and then it staggers the attacker before its hit.
bool ParryArmed() {
    if(!guardEnabled.load(std::memory_order_relaxed))return false;
    const auto now=GetTickCount64();
    std::lock_guard lock(guardMutex);const auto& s=guard.state;
    return guard.eligible&&!s.snapUsed&&s.snapAt&&now>=s.snapAt&&now-s.snapAt<=GuardTuning().parryMs;
}
uint64_t EarlyParry(uintptr_t monster,uintptr_t player,uint64_t attackAt,uint64_t opensAt,const char* trigger) {
    if(!guardChecked||!guardEnabled.load(std::memory_order_relaxed)||!enabled.load(std::memory_order_relaxed)||!ready.load(std::memory_order_relaxed))return 0;
    uint32_t id=0,a[3]{},d[3]{};
    if(!LocalPlayerId(id)||!Read(monster,a)||!Read(player,d)||!IncomingContact(id,a[0],d[0],d[2]))return 0;
    GuardShared g;{std::lock_guard lock(guardMutex);g=guard;}
    const auto now=GetTickCount64();
    if(!g.eligible||g.session!=gamestate::LocalPlayer()||now-g.at>250)return 0;
    float ax=0,az=0,px=0,pz=0;
    if(!GridPosition(monster,ax,az)||!GridPosition(player,px,pz))return 0;
    const framevr::math::Vec to{(ax-px)/g.upm,0,(az-pz)/g.upm};
    if(framevr::guard::Judge(g.state,to,now,GuardTuning(),attackAt,nullptr,opensAt,Parryable(AttackerRank(monster)))!=framevr::guard::Verdict::Parry)return 0;
    if(guardTrace.load(std::memory_order_relaxed)) {
        const auto& s=g.state;
        char line[280];snprintf(line,sizeof line,"Frame guard early parry: monster=%u class=%u trigger=%s swing_age=%lld ms attack_age=%lld ms downstroke_at=%lld ms attacker=%.2f m",
            a[2],a[1],trigger,now>=s.snapAt?(long long)(now-s.snapAt):-1LL,attackAt&&now>=attackAt?(long long)(now-attackAt):-1LL,
            attackAt&&opensAt>=attackAt?(long long)(opensAt-attackAt):-1LL,std::hypot(to.x,to.z));
        framevr::logging::Write(line);
    }
    return g.state.snapAt;
}
void ConfirmParry(uint64_t swing) {
    {std::lock_guard lock(guardMutex);framevr::guard::SpendParry(guard.state,swing);}
    ++guardParries;++earlyParries;framevr::ShieldGuardImpact(true);
}
void PublishGuard(uint64_t session,bool eligible,bool posed,const framevr::guard::Pose& pose,
                  bool handOk,bool leftShield) {
    const auto now=GetTickCount64();const auto t=GuardTuning();
    uint64_t lateAfter=0;bool late=false,raised=false,newSwing=false,missed=false;
    uint32_t lateMonster=UINT32_MAX;int32_t lateDurability=-1;   // a late parry to queue (UINT32_MAX = none)
    {
    std::lock_guard lock(guardMutex);
    if(guard.session!=session||!eligible||guard.state.surface.epoch!=pose.epoch){guard.state={};guard.debug={};}
    const uint64_t armed=guard.state.snapAt;
    if(eligible)framevr::guard::Update(guard.state,pose,handOk,leftShield,now,posed,t);
    newSwing=eligible&&guard.state.snapAt&&guard.state.snapAt!=armed;
    // Trace: each newly armed parry swing, to time it against the next hit.
    if(newSwing&&guardTrace.load(std::memory_order_relaxed)) {
        const auto& s=guard.state;
        char line[200];snprintf(line,sizeof line,"Frame guard swing: armed swing=%.2f m swing_cos=%.2f forward=%.2f up=%.2f side=%.2f m",
            s.swing,s.swingCos,s.forward,s.up,s.side);
        framevr::logging::Write(line);
    }
    if(eligible&&framevr::guard::SwingAfterBlock(guard.state,now,t,lateAfter)) {
        // Within parry_late_ms of the blocked hit: the blow was still coming
        // down on screen, so the swing parries it. Later than that is too late.
        const uint32_t grace=guardLateMs.load(std::memory_order_relaxed);
        if(grace&&lateAfter<=grace&&!guard.state.snapUsed&&guard.blockMonster!=UINT32_MAX&&guardEnabled.load(std::memory_order_relaxed)) {
            framevr::guard::SpendParry(guard.state,guard.state.snapAt);
            lateMonster=guard.blockMonster;lateDurability=guard.blockDurability;
        } else {late=true;++guardLate;}
        guard.blockMonster=UINT32_MAX;
    }
    // A parry swing whose window ran out unspent: tell the shield hand, once.
    if(eligible&&guardEnabled.load(std::memory_order_relaxed)&&framevr::guard::ParryMissed(guard.state,now,t)){missed=true;++parryMisses;}
    guard.session=session;guard.eligible=eligible;guard.upm=std::max(1.f,guard.state.surface.unitsPerMetre);guard.at=now;
    raised=eligible&&guard.state.raised;
    // Trace: where the posed shield sits relative to the body, every 2 s, so a
    // held guard can be checked against the thresholds without being hit.
    static uint64_t traceAt=0;
    if(guardTrace.load(std::memory_order_relaxed)&&now>=traceAt) {
        traceAt=now+2000;const auto& s=guard.state;
        char line[300];snprintf(line,sizeof line,"Frame guard pose: eligible=%d posed=%d hand=%d raised=%d reason=%s forward=%.2f up=%.2f side=%.2f m swing=%.2f m swing_cos=%.2f",
            int(eligible),int(posed),int(handOk),int(s.raised),framevr::guard::ReasonName(s.rejection),s.forward,s.up,s.side,s.swing,s.swingCos);
        framevr::logging::Write(line);
    }
    }
    // A swing during an attacker's wind-up parries now, not at its hit frame.
    if(newSwing&&guardEnabled.load(std::memory_order_relaxed))melee::QueueParrySwing();
    if(missed)framevr::ShieldParryMiss();
    // A late parry: stagger the attacker whose hit was just blocked, refund the
    // wear that block caused. The block's strain has already been applied.
    if(lateMonster!=UINT32_MAX&&melee::QueueParry(lateMonster,lateDurability)) {
        ++guardParries;++lateParries;framevr::ShieldGuardImpact(true);
        if(guardTrace.load(std::memory_order_relaxed)) {
            char line[160];snprintf(line,sizeof line,"Frame guard late parry: monster=%u %llu ms after the blocked hit",lateMonster,(unsigned long long)lateAfter);
            framevr::logging::Write(line);
        }
    }
    framevr::ShieldGuardState(eligible,raised);   // the strain light drains only while the shield is down
    FollowGameVolume();
    // A parry swing that came after the hit it was meant for: how late (no parry).
    if(late&&guardTrace.load(std::memory_order_relaxed)) {
        char line[160];snprintf(line,sizeof line,"Frame guard swing too late: %llu ms after the blocked hit (no parry)",(unsigned long long)lateAfter);
        framevr::logging::Write(line);
    }
}
bool ReadGuardDebug(framevr::guard::Debug& out) {
    std::lock_guard lock(guardMutex);out={};
    const auto now=GetTickCount64();
    if(!guardEnabled.load()||!guard.eligible||guard.session!=gamestate::LocalPlayer()||!guard.state.seenAt||
       now<guard.state.seenAt||now-guard.state.seenAt>GuardTuning().staleMs)return false;
    out=guard.debug;out.pose=guard.state.surface;out.raised=guard.state.raised;return framevr::guard::Valid(out.pose);
}
void Install() {
    if(!enabled.load()||ready.load())return;
    if(!rollChecked)rollChecked=d2rsig::Check(0x44bd65,RollSig,sizeof RollSig);
    if(!rollChecked)return;
    if(!guardChecked&&BlockSeedReturn&&BlockRollAt) {
        guardChecked=d2rsig::Check(BlockRollAt,BlockRollSig,sizeof BlockRollSig);
        framevr::logging::Write(guardChecked?"Frame guard: block roll verified; Unleashed physical shield blocking ready":
                                             "Frame guard: block roll signature mismatch; native block roll retained");
    }
    // The stat setter and damage application: riposte damage
    // scaling on physical swings.
    if(!setStatIn&&d2rsig::Check(GetStatRva,GetStatSig,sizeof GetStatSig))
        setStatIn=d2rsig::Hook(SetStatRva,SetStatSig,sizeof SetStatSig,reinterpret_cast<void*>(&SetStat),reinterpret_cast<void**>(&originalSetStat));
    if(setStatIn&&!applyDamageIn&&d2rsig::Check(LifeWriteAt,LifeWriteSig,sizeof LifeWriteSig))
        applyDamageIn=d2rsig::Hook(ApplyDamageRva,ApplyDamageSig,sizeof ApplyDamageSig,reinterpret_cast<void*>(&ApplyDamage),reinterpret_cast<void**>(&originalApplyDamage));
    if(applyDamageIn&&!damageScaleReady.exchange(true))
        framevr::logging::Write("Frame riposte: damage application verified; riposte damage ready");
    // Unleashed attack-rating damage: the native to-hit chance, read where
    // HitSuccessful hands it to 427EA0. Without it swings keep full damage.
    if(damageScaleReady.load()&&!hitChanceIn&&d2rsig::Check(ChanceClampAt,ChanceClampSig,sizeof ChanceClampSig)&&
       d2rsig::Check(ChanceCallAt,ChanceCallSig,sizeof ChanceCallSig)) {
        hitChanceIn=d2rsig::Hook(HitChanceRva,HitChanceSig,sizeof HitChanceSig,reinterpret_cast<void*>(&HitChance),reinterpret_cast<void**>(&originalHitChance));
        framevr::logging::Write(hitChanceIn?"Frame swing damage: native hit chance verified; attack-rating damage ready":
                                            "Frame swing damage: hit chance hook failed; swings keep full damage");
    }
    // Shield wear on physical blocks: the game's own item wear (see StruckWear).
    if(!struckWearIn&&d2rsig::Check(ItemWearRva,ItemWearSig,sizeof ItemWearSig)&&d2rsig::Check(InventoryRva,InventorySig,sizeof InventorySig)&&
       d2rsig::Check(EquippedRva,EquippedSig,sizeof EquippedSig)&&d2rsig::Check(InventoryFileRva,InventoryFileSig,sizeof InventoryFileSig)) {
        struckWearIn=d2rsig::Hook(StruckWearRva,StruckWearSig,sizeof StruckWearSig,reinterpret_cast<void*>(&StruckWear),reinterpret_cast<void**>(&originalStruckWear));
        itemWearIn=struckWearIn&&d2rsig::Hook(ItemWearRva,ItemWearSig,sizeof ItemWearSig,reinterpret_cast<void*>(&ItemWear),reinterpret_cast<void**>(&originalItemWear));
        framevr::logging::Write(itemWearIn?"Frame guard: item wear hooked; worn durability reaches the client":
                                           "Frame guard: item wear hook failed; worn durability shows after a relaunch");
        framevr::logging::Write(struckWearIn?"Frame guard: native item wear verified; blocks wear the shield":
                                             "Frame guard: item wear hook failed; blocks do not wear the shield");
    }
    if(!seedIn)seedIn=d2rsig::Hook(SeedRva,SeedSig,sizeof SeedSig,reinterpret_cast<void*>(&GetSeed),reinterpret_cast<void**>(&originalSeed));
    if(!hitIn)hitIn=d2rsig::Hook(HitRva,HitSig,sizeof HitSig,reinterpret_cast<void*>(&Hit),reinterpret_cast<void**>(&originalHit));
    if(!blockIn)blockIn=d2rsig::Hook(BlockRva,BlockSig,sizeof BlockSig,reinterpret_cast<void*>(&Block),reinterpret_cast<void**>(&originalBlock));
    if(!dodgeIn)dodgeIn=d2rsig::Hook(DodgeRva,DodgeSig,sizeof DodgeSig,reinterpret_cast<void*>(&Dodge),reinterpret_cast<void**>(&originalDodge));
    // Unleashed on-hit gate: each part only where its roll site matches this build;
    // a part that does not stays native (its effects on every contact, as before).
    if(!critGate)critGate=seedIn&&CheckSites(CritSites);
    if(!leechGate)leechGate=applyDamageIn&&CheckSites(LeechSites);
    if(!defenseGate)defenseGate=setStatIn&&CheckSites(DefenseSites);
    if(!triggerIn&&CheckSites(EventSites))
        triggerIn=d2rsig::Hook(TriggerRva,TriggerSig,sizeof TriggerSig,reinterpret_cast<void*>(&Trigger),reinterpret_cast<void**>(&originalTrigger));
    if(!curseIn&&hitIn&&CheckSites(HealSites))
        curseIn=d2rsig::Hook(CurseRva,CurseSig,sizeof CurseSig,reinterpret_cast<void*>(&Curse),reinterpret_cast<void**>(&originalCurse));
    // Would-have-hit gate: each part only where this build has its sites; a part
    // that does not match stays native (its effects on every forced contact, as before).
    if(!missEventsReady)missEventsReady=triggerIn&&CheckSites(MissEventSites);
    if(!effectsIn&&leechGate&&applyDamageIn&&CheckSites(EffectSites))   // after leechGate: this hook covers 450C9A
        effectsIn=d2rsig::Hook(EffectsRva,EffectsSig,sizeof EffectsSig,reinterpret_cast<void*>(&DamageEffects),reinterpret_cast<void**>(&originalEffects));
    if(!umodIn&&effectsIn)
        umodIn=d2rsig::Hook(UmodRva,UmodSig,sizeof UmodSig,reinterpret_cast<void*>(&MonsterOnHit),reinterpret_cast<void**>(&originalUmod));
    if(!thornsIn&&CheckSites(ThornsSites)) {
        thornsIn=d2rsig::Hook(ThornsRva,ThornsSig,sizeof ThornsSig,reinterpret_cast<void*>(&Thorns),reinterpret_cast<void**>(&originalThorns));
        missileThornsIn=thornsIn&&d2rsig::Hook(MissileThornsRva,MissileThornsSig,sizeof MissileThornsSig,reinterpret_cast<void*>(&MissileThorns),reinterpret_cast<void**>(&originalMissileThorns));
    }
    if(!missWearReady)missWearReady=struckWearIn&&CheckSites(WearSites);
    if(!absorbReady)absorbReady=triggerIn&&CheckSites(AbsorbSites);
    if(seedIn&&hitIn&&blockIn&&dodgeIn&&!ready.exchange(true))
        framevr::logging::Write("Frame combat: deterministic local-player hit checks ready; native collision/range and damage retained");
    if(ready.load()&&!onHitLogged) {
        onHitLogged=true;
        char line[260];snprintf(line,sizeof line,"Frame on-hit gate: crit/deadly strike=%s proc events=%s leech=%s prevent heal=%s defense per hit=%s (Unleashed: a full-cycle stroke's first contact only; off = native on every contact)",
            critGate?"ready":"off",triggerIn?"ready":"off",leechGate?"ready":"off",curseIn?"ready":"off",defenseGate?"ready":"off");
        framevr::logging::Write(line);
    }
    if(ready.load()&&!missLogged) {
        missLogged=true;
        char line[400];snprintf(line,sizeof line,"Frame would-hit gate: verdict=%s events=%s effects=%s monster mods=%s thorns=%s missile thorns=%s wear=%s prevent heal=%s defense per hit=%s absorb=%s (Unleashed: a forced contact's other effects only when its native roll would have hit; off = on every contact)",
            hitChanceIn&&seedIn?"ready":"off",missEventsReady?"ready":"off",effectsIn?"ready":"off",umodIn?"ready":"off",thornsIn?"ready":"off",
            missileThornsIn?"ready":"off",missWearReady?"ready":"off",curseIn?"ready":"off",defenseGate?"ready":"off",absorbReady?"ready":"off");
        framevr::logging::Write(line);
    }
}
void Report() {
    const auto gb=guardBlocks.exchange(0),go=guardOpen.exchange(0),gp=guardParries.exchange(0),gu=guardUnserved.exchange(0),gf=guardFallback.exchange(0),gl=guardLate.exchange(0);
    const auto ga=guardAttackKnown.exchange(0);
    std::string rejected;
    for(size_t i=1;i<guardRejections.size();++i)if(const auto n=guardRejections[i].exchange(0)) {
        if(rejected.empty())rejected="Frame guard rejected:";
        rejected+=" ";rejected+=framevr::guard::ReasonName(framevr::guard::Rejection(i));rejected+="=";rejected+=std::to_string(n);
    }
    if(!rejected.empty())framevr::logging::Write(rejected.c_str());
    const auto fh=forcedHits.exchange(0);
    if(uint32_t m=riposteMonster.load(),seen=m;m!=NoSwing&&GetTickCount64()>riposteUntil.load()&&riposteMonster.compare_exchange_strong(m,NoSwing)) {
        char line[120];snprintf(line,sizeof line,"Frame riposte: window expired unused (monster=%u)",seen);framevr::logging::Write(line);
    }
    if(const auto sh=scaledHits.exchange(0),cu=chanceUnseen.exchange(0);sh||cu) {
        char line[200];snprintf(line,sizeof line,"Frame swing damage: scaled hits=%u last chance=%d%% last recovery x cleave=%.2f last factor=%.2f chance unseen=%u",
            sh,lastChance.load(),lastRecovery.load(),lastFactor.load(),cu);
        framevr::logging::Write(line);
    }
    if(const auto ds=defenseScaled.exchange(0),du=defenseUnseen.exchange(0);ds||du) {
        char line[200];snprintf(line,sizeof line,"Frame incoming defense: scaled hits=%u last chance=%d%% chance unseen=%u",
            ds,lastDefenseChance.load(),du);
        framevr::logging::Write(line);
    }
    if(const auto ih=verdictInHit.exchange(0),im=verdictInMiss.exchange(0),sh=verdictSwingHit.exchange(0),sm=verdictSwingMiss.exchange(0),
                  th=verdictShotHit.exchange(0),tm=verdictShotMiss.exchange(0),vu=verdictUnknown.exchange(0),ds=dotScaled.exchange(0),
                  ab=absorbScaled.exchange(0);ih||im||sh||sm||th||tm||vu||ds||ab) {
        char line[300];snprintf(line,sizeof line,"Frame would-hit gate: incoming would hit=%u would miss=%u swings would hit=%u would miss=%u shots would hit=%u would miss=%u unknown=%u damage over time scaled=%u absorbs scaled=%u",
            ih,im,sh,sm,th,tm,vu,ds,ab);
        framevr::logging::Write(line);
    }
    if(const auto ss=shotScaled.exchange(0),su=shotUnseen.exchange(0);ss||su) {
        char line[160];snprintf(line,sizeof line,"Frame shot damage: scaled hits=%u last chance=%d%% chance unseen=%u",ss,lastShotChance.load(),su);
        framevr::logging::Write(line);
    }
    {
        const auto bh=verdictButtonHit.exchange(0),bm=verdictButtonMiss.exchange(0),bs=buttonScaled.exchange(0),bu=buttonUnseen.exchange(0);
#ifdef D2RVR_DEVELOPER
        if(bh||bm||bs||bu) {   // trigger/button melee: would-hit verdicts and life writes scaled
            char line[220];snprintf(line,sizeof line,"Frame trigger melee damage: would hit=%u would miss=%u scaled hits=%u last chance=%d%% last factor=%.2f chance unseen=%u",
                bh,bm,bs,lastButtonChance.load(),lastButtonFactor.load(),bu);
            framevr::logging::Write(line);
        }
#else
        (void)bh;(void)bm;(void)bs;(void)bu;
#endif
    }
#ifdef D2RVR_DEVELOPER
    // What the would-hit gate switched off on would-miss contacts.
    if(const auto ev=missEvents.exchange(0),am=missAsMiss.exchange(0),fi=missFields.exchange(0),um=missUmods.exchange(0),
                  th=missThorns.exchange(0),we=missWear.exchange(0),he=missHeal.exchange(0),de=missDefense.exchange(0);ev||am||fi||um||th||we||he||de) {
        char line[280];snprintf(line,sizeof line,"Frame would-hit gate native: hit-only events skipped=%u events run as a miss=%u effect fields zeroed=%u monster mods skipped=%u thorns skipped=%u item wear skipped=%u prevent heal=%u defense per hit=%u",
            ev,am,fi,um,th,we,he,de);
        framevr::logging::Write(line);
    }
#endif
    if(const auto cm=clientMirrored.exchange(0),cx=clientMissing.exchange(0);cm||cx) {
        char line[120];snprintf(line,sizeof line,"Frame item wear: client durability updated=%u item not found on the client=%u",cm,cx);
        framevr::logging::Write(line);
    }
    if(const auto wr=shieldWearRolls.exchange(0),cw=chipWearRedirects.exchange(0);wr||cw) {
        char line[200];snprintf(line,sizeof line,"Frame guard: shield wear native rolls=%u worn=%u durability=%d (%d%% of contacts x native 10%%) chip struck wear on the shield=%u",
                                wr,shieldWorn.exchange(0),shieldDurability.load(),shieldWearShare.load(),cw);
        framevr::logging::Write(line);
    }
    if(const auto ch=chipHits.exchange(0),cl=chipLanded.exchange(0);ch||cl) {
        char line[160];snprintf(line,sizeof line,"Frame guard chip: broken-guard hits=%u scaled at life write=%u last block rate=%d%%",ch,cl,lastChipRate.load());
        framevr::logging::Write(line);
    }
    if(const auto rl=ripostesLanded.exchange(0)) {
        char line[160];snprintf(line,sizeof line,"Frame riposte: landed=%u",rl);
        framevr::logging::Write(line);
    }
#ifdef D2RVR_DEVELOPER
    // What the on-hit gate switched off in gated swings' native attacks.
    if(const auto md=gatedMeleeDamage.exchange(0),ma=gatedMeleeAttack.exchange(0),le=gatedLeech.exchange(0),
                  cr=gatedCrits.exchange(0),he=gatedHeal.exchange(0),de=gatedDefense.exchange(0);md||ma||le||cr||he||de) {
        char line[260];snprintf(line,sizeof line,"Frame on-hit gate native: domeleedamage events=%u domeleeattack events=%u leech zeroed=%u crit rolls=%u prevent heal=%u defense per hit=%u",
            md,ma,le,cr,he,de);
        framevr::logging::Write(line);
    }
#endif
    const auto ih=incomingHits.exchange(0),im=incomingMisses.exchange(0),mp=missParries.exchange(0),ep=earlyParries.exchange(0),pm=parryMisses.exchange(0),lp=lateParries.exchange(0);
    if(gb||go||gp||gu||gf||fh||gl||ih||im||ep||pm) {
        char guardLine[448];snprintf(guardLine,sizeof guardLine,"Frame guard: blocked=%u open=%u parries=%u swings_too_late=%u attack_start_known=%u no_block_roll=%u position_fallback=%u enemy_forced_hits=%u incoming_hits=%u incoming_misses=%u miss_parries=%u early_parries=%u late_parries=%u parry_misses=%u",gb,go,gp,gl,ga,gu,gf,fh,ih,im,mp,ep,lp,pm);
        framevr::logging::Write(guardLine);
    }
    const auto m=meleeChecks.exchange(0),p=missileChecks.exchange(0),r=rolls.exchange(0),b=blocks.exchange(0),d=dodges.exchange(0),f=failed.exchange(0);
    const auto n=nativeMelee.exchange(0),g=nativeGuards.exchange(0);
    if(!m&&!p&&!r&&!b&&!d&&!f&&!n&&!g)return;
    char line[320];snprintf(line,sizeof line,"Frame combat: melee checks=%u missile checks=%u accepted rolls=%u block bypass=%u dodge bypass=%u failures=%u native melee checks=%u native block/dodge in melee=%u",m,p,r,b,d,f,n,g);
    framevr::logging::Write(line);
}
}
