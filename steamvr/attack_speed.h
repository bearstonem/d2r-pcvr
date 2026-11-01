#pragma once
#include <algorithm>
#include <cstdint>
// D2 normal-attack (A1) cadence for free-swing melee. Physical contact replaces
// the native attack animation, so this restores the animation's pace: one
// landed hit per attack cycle, shortened by IAS exactly as the game's own
// breakpoints would. Per-class frame data: The Amazon Basin wiki, "Attack
// speed" (theamazonbasin.com/wiki/index.php/Attack_speed). Formula as given by
// Maxroll ("Attack Speed"), which reproduces the Basin's published breakpoint
// tables at every IAS (the "-1": consecutive attacks share a frame):
//   EIAS     = floor(120*IAS / (120+IAS))
//   AnimRate = floor(AnimationSpeed * (100 + EIAS - WSM + SIAS) / 100)
//   frames   = ceil((FramesPerDirection - StartingFrame) * 256 / AnimRate) - 1
// at 25 frames per second. Facts about the game's mechanics; no game data files.
namespace framevr::attackspeed {
enum class Weapon:uint8_t {HandToHand,OneHandSwing,OneHandThrust,TwoHandSwing,TwoHandThrust,Staff,Bow,Crossbow,Claw,Count};
struct Animation {uint8_t frames=0,start=0;uint16_t speed=256;};
inline constexpr int ClassCount=7;
inline constexpr int FramesPerSecond=25;
// Rows are D2 class ids: 0 Amazon, 1 Sorceress, 2 Necromancer, 3 Paladin,
// 4 Barbarian, 5 Druid, 6 Assassin. Columns follow Weapon. {0} = not usable.
inline constexpr Animation Table[ClassCount][int(Weapon::Count)]={
    {{13,1},{16,2},{15,2},{20,2},{18,2},{20,2},{14,0},{20,0},{}},             // Amazon
    {{16,1},{20,2},{19,2},{24,2},{23,2},{18,2},{17,0},{20,0},{}},             // Sorceress
    {{15,0},{19,0},{19,0},{23,0},{24,0},{20,0},{18,0},{20,0},{}},             // Necromancer
    {{14,0},{15,0},{17,0},{18,0},{20,0},{18,0},{16,0},{20,0},{}},             // Paladin
    {{12,0},{16,0},{16,0},{18,0},{19,0},{19,0},{15,0},{20,0},{}},             // Barbarian
    {{16,0},{19,0},{19,0},{21,0},{23,0},{17,0},{16,0},{20,0},{}},             // Druid
    {{11,0},{15,0},{15,0},{23,0},{23,0},{19,0},{16,0},{21,0},{11,0,208}},     // Assassin
};
// Weapons.txt "wclass" codes (first three characters; storage pads the fourth).
inline bool FromCode(const char code[3],Weapon& out) {
    static constexpr struct {char c[4];Weapon w;} Codes[]={
        {"hth",Weapon::HandToHand},{"1hs",Weapon::OneHandSwing},{"1ht",Weapon::OneHandThrust},
        {"2hs",Weapon::TwoHandSwing},{"2ht",Weapon::TwoHandThrust},{"stf",Weapon::Staff},
        {"bow",Weapon::Bow},{"xbw",Weapon::Crossbow},{"ht1",Weapon::Claw},{"ht2",Weapon::Claw}};
    for(const auto& c:Codes)if(code[0]==c.c[0]&&code[1]==c.c[1]&&code[2]==c.c[2]){out=c.w;return true;}
    return false;
}
inline int Eias(int ias) {ias=std::max(ias,0);return 120*ias/(120+ias);}
// Frames in one normal-attack cycle; 0 when the class/weapon pair is unknown.
// SIAS (skill/aura speed, slows) is a separate input and defaults to none.
// The speed sum is bounded to the game's usual 15..175 range.
inline int Frames(int classId,Weapon weapon,int ias,int wsm,int sias=0) {
    if(classId<0||classId>=ClassCount||weapon>=Weapon::Count)return 0;
    const auto a=Table[classId][int(weapon)];
    if(!a.frames)return 0;
    const int percent=std::clamp(100+Eias(ias)-wsm+sias,15,175);
    const int rate=a.speed*percent/100;
    const int length=(a.frames-a.start)*256;
    return rate>0?(length+rate-1)/rate-1:0;
}
// A druid's Werewolf / Werebear normal attack (D2R 2.4). The form uses the
// human class's A1 animation speed and starting frame for the weapon class,
// with the increment slowed by the form's A1 length over the human's, and its
// own A1 length: Werewolf 13 frames, Werebear 12. EIAS here is the whole sum
// past 100 % - item IAS after diminishing returns plus skill speed (Werewolf,
// Maul, Fanaticism, Burst of Speed), less WSM and slows - capped at -85..150
// in form (75 in human form). Per ChthonVII's 2.4 PTR2 wereform calculator
// (github.com/ChthonVII/d2r_24ptr2_wereform_calculator, tested with
// Warren1001) and Warren1001's IAS calculator: this closed form reproduces
// the former's animation loop for every weapon class and EIAS. Facts about
// the game's mechanics; no game data files.
enum Form:int {Human=0,Wolf=1,Bear=2};
inline int FormFrames(int form) {return form==Wolf?13:form==Bear?12:0;}
inline int TruncDiv(int value,int by) {return value/by;}   // C++: toward zero, as D2R
inline int WereformFrames(int classId,int form,Weapon weapon,int eias) {
    if(classId<0||classId>=ClassCount||weapon>=Weapon::Count||!FormFrames(form))return 0;
    const auto human=Table[classId][int(weapon)];
    if(!human.frames)return 0;
    const int length=FormFrames(form),capped=std::clamp(eias,-85,150);
    int increment=human.speed+TruncDiv(human.speed*capped,100);
    increment=TruncDiv(increment*length,human.frames);
    if(increment<=0)return 0;
    const int counter=(length-human.start)*256;
    return (counter+increment-1)/increment-1;
}
// The speed sum past 100 % in form: item IAS (ItemStatCost 93, diminished) and
// the unit's attackrate (68): 100, plus skill speed, less the weapon's WSM and slows.
inline int FormEias(int ias,int attackRate) {return Eias(ias)+attackRate-100;}
// Human form's skill speed (SIAS) for Frames: the unit's attackrate (68) less 100, with the
// main-hand weapon's WSM it already subtracts put back (Frames takes WSM separately).
// Fanaticism, Frenzy, Burst of Speed and slows (chill, Decrepify) move it. Logged
// 2026-10-21, no buffs: WSM 0 -> attackrate 100, +10 -> 90, -10 -> 110 (SkillSpeed 0).
inline int SkillSpeed(int attackRate,int mainWsm) {return attackRate-100+mainWsm;}
// Dual wield (a Barbarian's two weapons, an Assassin's two claws), Cal 2026-10-30: native D2
// animates every attack at one rate. D2Common UNITS_UpdateAttackAnimRateAndVelocity (D2MOO)
// merges the swinging weapon's stat lists into the hero (its IAS and its attackrate, which is
// 100 less its WSM, count; the other weapon's do not), then, with a weapon in each arm, adds
// (right + left) / 2 - right of the two weapons' own attackrates (-WSM). Before D2R 2.4 that
// "right" was always the right-arm weapon, so a left-arm stroke ran at the average plus the
// two WSMs' difference; 2.4 "changed how attack speed is determined while dual-wielding
// weapons. Swapping weapons between hands should no longer result in dramatically different
// attack speeds": every stroke runs at the two WSMs' average. C division of the negated
// attackrates truncates toward zero, as the average here does.
inline int DualWsm(int ownWsm,int otherWsm) {return (ownWsm+otherWsm)/2;}
// One dual-wield stroke's cycle, read with its own weapon merged (melee_weapon Scope): item
// IAS and attackrate are the hero's stats at that moment, ownWsm the swinging weapon's.
// The speed sum is EIAS + attackrate + ownWsm - DualWsm: the attackrate's own WSM swapped for the average.
inline int DualFrames(int classId,Weapon weapon,int ias,int attackRate,int ownWsm,int otherWsm) {
    return Frames(classId,weapon,ias,DualWsm(ownWsm,otherWsm),SkillSpeed(attackRate,ownWsm));
}
inline uint32_t Milliseconds(int frames) {return frames>0?uint32_t(frames)*1000u/FramesPerSecond:0;}
// One attack per cycle, on the authoritative game thread. A native-resolved
// attack starts the cycle whether its roll hit or missed, as the native swing
// animation would; geometric whiffs and refused contacts never do.
class Cadence {
    uint64_t session=0,readyAt=0,landedAt=0,priorAt=0;   // priorAt: the landed hit before landedAt (0 = none)
public:
    bool Ready(uint64_t s,uint64_t now) const {return s!=session||now>=readyAt;}
    uint64_t Remaining(uint64_t s,uint64_t now) const {return Ready(s,now)?0:readyAt-now;}
    // Milliseconds since the previous landed hit; UINT64_MAX when there is none. One batch
    // can apply its contacts out of time order (creature by creature): a contact timed before
    // the latest landed hit is measured from the hit before that one (its own predecessor), so
    // it is not scored as zero (audit 2026-10-24); the clock itself never runs back.
    uint64_t Since(uint64_t s,uint64_t now) const {
        if(s!=session||!landedAt)return UINT64_MAX;
        if(now>=landedAt)return now-landedAt;
        return priorAt&&now>=priorAt?now-priorAt:UINT64_MAX;   // no known predecessor: a first stroke
    }
    void Landed(uint64_t s,uint64_t now,uint32_t cycle) {
        if(s==session&&landedAt&&now<landedAt){if(now>priorAt)priorAt=now;readyAt=std::max(readyAt,now+cycle);return;}
        priorAt=s==session?landedAt:0;session=s;landedAt=now;readyAt=now+cycle;
    }
};
}
