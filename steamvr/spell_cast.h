#pragma once
#include <algorithm>
#include <cstdint>
#include "frame_math.h"

namespace framevr::spellcast {
// One start sound per accepted cast, not one per member of a bolt volley.
// Called only on the UI thread after a valid visual is created.
class CastCue {
    uint64_t session=0,sequence[2]{};
public:
    bool Take(uint64_t world,int hand,uint64_t shot,uint64_t at,uint64_t now) {
        if(!world||hand<0||hand>1||!shot||now<at||now-at>250)return false;
        if(world!=session){session=world;sequence[0]=sequence[1]=0;}
        if(shot<=sequence[hand])return false;
        sequence[hand]=shot;return true;
    }
};
// Preserve continuous yaw, without amplifying tracking noise near vertical.
// No usable heading on the first sample means no shot, not a default compass
// direction. Each hand keeps its own fallback until the world/turn changes.
class PlanarAim {
    uint64_t session=0,epoch=0;
    math::Vec last{};
    static bool Project(math::Vec in,math::Vec& out) {
        if(!std::isfinite(in.x)||!std::isfinite(in.y)||!std::isfinite(in.z))return false;
        const float horizontal=in.x*in.x+in.z*in.z,total=horizontal+in.y*in.y;
        if(total<.000001f||horizontal<total*.0076f)return false; // within five degrees of vertical
        out=math::Normalized({in.x,0,in.z});return true;
    }
public:
    math::Vec Update(uint64_t world,uint64_t turn,math::Vec direction,math::Vec fallback,bool planar=true) {
        if(world!=session||turn!=epoch){session=world;epoch=turn;last={};}
        if(!Project(direction,last)&&math::Dot(last,last)<.5f)Project(fallback,last);
        return planar?last:direction;
    }
};
// Charged Bolt and Chain Lightning keep their native paths: Charged Bolt's wandering bolts and grid hits, Chain
// Lightning's leaps from monster to monster. Each has its own server function; only where the missile starts and
// heads moves to the hand (bowvr::SpellParams), the rest is the game's (owner, 2026-11-09: Chain Lightning out of
// the arm she points, "similar to how things like firebolt work"). Lightning flies like Fire Bolt.
inline bool NativePath(int id) {return id==38||id==53;}
inline bool Supported(int id) {return id==36||id==38||id==39||id==45||id==47||id==49||id==53||id==84||id==101;}
// Charged Bolt's cast throws up to 24 bolts at once; Chain Lightning's one (its leaps come later, on its hits).
inline unsigned LaunchCapacity(int id) {return id==38?24:1;}
inline bool Candidate(unsigned character,int id,int start,int action,int missile,unsigned flags,int secondary=-1) {
    if(!Supported(id)||(id==84?character!=2:id==101?character!=3:character!=1)||start!=0||(flags&3u))return false;
    if(id==84)return action==0&&missile==192;
    if(id==101)return action==0&&missile==55;
    if(id==38)return action==17&&missile==-1&&secondary==56;
    if(id==53)return action==26&&missile>1&&missile<4096;   // its server function 26 throws its srvmissile
    return action==0&&missile>1&&missile<4096;
}
// Owned AnimData: Sorceress SC has 14 frames, Necromancer/Paladin SC have 16,
// both at 256 base animation speed. Preserve class timing when adding spells.
// diminishing returns capped at 75 effective FCR, minus the terminal frame.
inline unsigned CastFrames(int fcr,unsigned character=1) {
    fcr=std::clamp(fcr,0,100000);
    const int effective=std::min(75,120*fcr/(120+fcr));
    const int speed=256*(100+effective)/100;
    const int frames=character==2||character==3?16:14;
    return unsigned((frames*256+speed-1)/speed-1);
}
class Cadence {
    uint64_t session=0;
    uint32_t last[2]{};
    unsigned duration[2]{};
    bool used[2]{};
public:
    bool Ready(uint64_t world,int hand,uint32_t tick) const {
        return world&&hand>=0&&hand<2&&(world!=session||!used[hand]||uint32_t(tick-last[hand])>=duration[hand]);
    }
    void Fired(uint64_t world,int hand,uint32_t tick,unsigned frames) {
        if(world!=session){session=world;used[0]=used[1]=false;}
        if(hand<0||hand>1)return;
        used[hand]=true;last[hand]=tick;duration[hand]=std::clamp(frames,1u,250u);
    }
};
enum class Failure : uint16_t {None=0,Mana=22,Town=25};
inline Failure Refusal(bool townRestricted,bool enoughMana) {
    return townRestricted?Failure::Town:enoughMana?Failure::None:Failure::Mana;
}
struct StartResult {bool accepted;Failure failure;};
// Native Start can clear the used skill on refusal. Diagnose while the hand's
// temporary skill is still selected; never reinterpret cleared state as mana.
template<class Diagnose,class Start> StartResult StartWithFeedback(Diagnose diagnose,Start start) {
    const auto before=diagnose();
    const bool accepted=start();
    return {accepted,accepted?Failure::None:before};
}
// One short-lived notification shared by both hands. The caller synchronizes
// game/UI access. Native speech supplies the longer voice repeat cooldown;
// this only coalesces held-trigger failures and prevents stale delivery.
class CastWarning {
    uint64_t session=0,at=0;
    uint32_t player=0;
    bool pending=false,issued=false;
    Failure reason=Failure::None;
public:
    void Request(uint64_t world,uint32_t id,uint64_t now,Failure why) {
        if(!world||why==Failure::None)return;
        if(world==session&&id==player&&why==reason&&issued&&now>=at&&now-at<250)return;
        session=world;player=id;at=now;pending=issued=true;reason=why;
    }
    Failure Take(uint64_t world,uint32_t id,uint64_t now,bool active) {
        const bool take=pending&&active&&world&&world==session&&id==player&&now>=at&&now-at<=500;
        pending=false;
        return take?reason:Failure::None;
    }
};
}
