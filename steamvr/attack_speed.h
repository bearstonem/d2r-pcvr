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
inline uint32_t Milliseconds(int frames) {return frames>0?uint32_t(frames)*1000u/FramesPerSecond:0;}
// One attack per cycle, on the authoritative game thread. A native-resolved
// attack starts the cycle whether its roll hit or missed, as the native swing
// animation would; geometric whiffs and refused contacts never do.
class Cadence {
    uint64_t session=0,readyAt=0,landedAt=0;
public:
    bool Ready(uint64_t s,uint64_t now) const {return s!=session||now>=readyAt;}
    uint64_t Remaining(uint64_t s,uint64_t now) const {return Ready(s,now)?0:readyAt-now;}
    // Milliseconds since the previous landed hit; UINT64_MAX when there is none.
    uint64_t Since(uint64_t s,uint64_t now) const {return s==session&&landedAt&&now>=landedAt?now-landedAt:UINT64_MAX;}
    void Landed(uint64_t s,uint64_t now,uint32_t cycle) {session=s;landedAt=now;readyAt=now+cycle;}
};
}
