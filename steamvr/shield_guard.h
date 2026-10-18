#pragma once
#include "frame_math.h"
#include <cstdint>

// Unleashed physical shield guard. Pure logic: no native types, no threads.
// The UI-thread tick feeds the shield hand and the posed shield; the game
// thread asks Judge() when a monster's native hit check has already succeeded
// against the local player.
namespace framevr::guard {
using math::Vec;
// One completed skeleton pose. Positions are relative to the native player
// origin in world-facing metres. Hand remains head-yaw-relative for the parry.
// No camera-facing direction is used for coverage.
struct Pose {
    Vec center{},normal{},eye{},hand{};
    float unitsPerMetre=0;
    uint64_t at=0;
    uint32_t epoch=0;
    // Rendered mesh bounds, in world-facing metres. Zero means the older
    // attachment-only fallback. These include the item's own bone/transform.
    Vec halfRight{},halfUp{};
    Vec player{}; // render-world origin of this same pose; debug projection only
};
inline bool Measured(const Pose& p) {return math::Dot(p.halfRight,p.halfRight)>.0025f&&math::Dot(p.halfUp,p.halfUp)>.0025f;}
inline bool Finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool Valid(const Pose& p) {
    const float n=math::Dot(p.normal,p.normal);
    return p.at&&Finite(p.center)&&Finite(p.normal)&&Finite(p.eye)&&Finite(p.hand)&&Finite(p.halfRight)&&Finite(p.halfUp)&&
        std::isfinite(p.unitsPerMetre)&&p.unitsPerMetre>=1&&p.unitsPerMetre<=32&&n>.99f&&n<1.01f;
}
// The coverage proxy uses the shield attachment's XY plane. Cross the final posed
// axes so mirrored/nonuniform transforms retain a perpendicular surface normal.
// Use the same calibrated scale, eye and input sample that posed the arm.
inline bool FromAttachment(const float* world,const float* model,Vec eyeModel,Vec hand,
                           float modelUnitsPerMetre,uint64_t at,uint32_t epoch,Pose& out) {
    out={};
    for(int i=0;i<16;++i)if(!std::isfinite(world[i])||!std::isfinite(model[i]))return false;
    const Vec x{world[0],world[1],world[2]},y{world[4],world[5],world[6]};
    const float scale=std::sqrt(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
    const float upm=modelUnitsPerMetre*scale;
    if(!std::isfinite(upm)||upm<1||upm>32||math::Dot(x,x)<.01f||math::Dot(y,y)<.01f)return false;
    Pose p;
    p.player={model[12],model[13],model[14]};
    p.center=Vec{world[12]-model[12],world[13]-model[13],world[14]-model[14]}*(1/upm);
    p.eye=Vec{eyeModel.x*model[0]+eyeModel.y*model[4]+eyeModel.z*model[8],
              eyeModel.x*model[1]+eyeModel.y*model[5]+eyeModel.z*model[9],
              eyeModel.x*model[2]+eyeModel.y*model[6]+eyeModel.z*model[10]}*(1/upm);
    p.normal=math::Normalized(math::Cross(x,y));p.hand=hand;p.unitsPerMetre=upm;p.at=at;p.epoch=epoch;
    if(!Valid(p))return false;
    out=p;return true;
}
struct Tuning {
    // Block: the shield hand up in front of the chest. Measured from the head in
    // its turn only (the view, without pitch or roll): metres, x right, y up,
    // z back. The chest is under the eyes, not ahead of them: a shield held
    // against it reads -.16..+.03 forward, -.36..-.56 up (log 2026-10-16). A hand
    // hanging at the side (~0 forward, ~-.7 up) is what it refuses.
    float minUp=-.62f,maxUp=.30f;          // ~navel to above the eyes; an arm hanging is -.69 and below
    float reach=1.15f;                    // horizontal reach from the eyes, independent of head yaw
    float hold=.05f;                       // a raised guard holds this much past every edge
    float coverCos=.5f;                    // within 60 degrees of either face; edge-on does not cover
    float lateral=.45f,behind=.20f;       // forgiving attachment coverage proxy, not mesh-sized collision
    // 0 disables perfect parry; a swing at most this long before the hit.
    // Owner swings landed 400-1300 ms before hits; 400 ms caught almost none
    // (logs 2026-10-17).
    uint32_t parryMs=750;
    // A swing may begin this long before the attacker's attack starts. Fast
    // attackers land 250-360 ms after starting (log 2026-10-17), too soon to
    // react to the wind-up alone.
    uint32_t parryLeadMs=450;
    // Parry: a swing across the view, low on the far side up to high on the
    // shield's own side, 45 degrees (a left shield: lower right -> upper left).
    float swingMetres=.15f;                // along the diagonal, within swingMs
    uint32_t swingMs=120;
    float swingCos=.82f;                   // ~35 degrees either side of the diagonal
    float parryRange=3.f;                  // metres; ranged attackers are blocked, not parried
    uint32_t staleMs=150;                  // pose older than this cannot block
};
enum class Rejection { None, Untracked, InvalidHand, InvalidSurface, TooLow, TooHigh, TooFar, StalePose, OutsideCoverage, OutsidePlacement, Count };
inline const char* ReasonName(Rejection reason) {
    switch(reason) {
        case Rejection::None:return "covered";
        case Rejection::Untracked:return "untracked";
        case Rejection::InvalidHand:return "invalid_hand";
        case Rejection::InvalidSurface:return "invalid_surface";
        case Rejection::TooLow:return "hand_low";
        case Rejection::TooHigh:return "hand_high";
        case Rejection::TooFar:return "hand_far";
        case Rejection::StalePose:return "stale_pose";
        case Rejection::OutsideCoverage:return "shield_angle";
        case Rejection::OutsidePlacement:return "shield_position";
        default:return "unknown";
    }
}
struct State {
    bool raised=false,snapUsed=true;
    Rejection rejection=Rejection::Untracked;
    uint64_t raisedAt=0,loweredAt=0,seenAt=0;
    // Parry: the last diagonal swing that ended in guard; one parry per swing.
    uint64_t snapAt=0,swingSeen=0;
    // Diagnostic: the last melee hit blocked, to log a swing that came just after it.
    uint64_t blockAt=0;
    // the hand's recent path, one sample per >= 8 ms (render poses repeat between ticks)
    static constexpr int kPath=24;
    Vec path[kPath]{};uint64_t pathAt[kPath]{};int pathNext=0,pathCount=0;
    float swing=0,swingCos=0;  // last measured: metres along the diagonal, alignment
    Pose surface{};
    Vec hand{};   // last shield hand from the head, for [debug] guard_trace
    float forward=0,up=0,side=0;
};
enum class Verdict { Open, Block, Parry };

// The shield hand from the head (view frame): forward, up, and toward the
// shield's own side (+) or across the body (-).
inline void Axes(Vec hand,bool leftShield,float& forward,float& up,float& side) {
    forward=-hand.z;up=hand.y;side=leftShield?-hand.x:hand.x;
}
inline Rejection PositionRejection(Vec hand,bool leftShield,bool raised,const Tuning& t) {
    float f,u,s;Axes(hand,leftShield,f,u,s);
    const float m=raised?t.hold:0;
    if(!std::isfinite(f+u+s))return Rejection::InvalidHand;
    if(u<t.minUp-m)return Rejection::TooLow;
    if(u>t.maxUp+m)return Rejection::TooHigh;
    if(std::hypot(f,s)>t.reach+m)return Rejection::TooFar;
    return Rejection::None;
}
inline bool InGuard(Vec hand,bool leftShield,bool raised,const Tuning& t) {return PositionRejection(hand,leftShield,raised,t)==Rejection::None;}

inline Rejection PositionRejection(const Pose& p,bool leftShield,bool raised,const Tuning& t) {
    if(!Measured(p))return PositionRejection(p.hand,leftShield,raised,t);
    const float margin=raised?t.hold:0;
    const float height=std::fabs(p.halfRight.y)+std::fabs(p.halfUp.y);
    // A tower shield can cover the torso with its grip below the old hand
    // cutoff. Use the visible surface, still relative to this pose's eyes.
    if(p.center.y+height<p.eye.y-.50f-margin)return Rejection::TooLow;
    if(p.center.y-height>p.eye.y+.15f+margin)return Rejection::TooHigh;
    if(std::hypot(p.hand.x,p.hand.z)>t.reach+margin)return Rejection::TooFar;
    return Rejection::None;
}

// A missing render pose may briefly retain the last guard; the original pose
// timestamp expires it. Re-reading the same pose never makes it newer.
inline void Update(State& s,const Pose& pose,bool handOk,bool leftShield,uint64_t now,bool posed,const Tuning& t) {
    const auto invalidate=[&](Rejection why){s={};s.rejection=why;};
    if(!handOk){invalidate(Rejection::Untracked);return;}
    if(!posed)return; // keep the last guard; Judge() expires it after staleMs
    if(!Finite(pose.hand)){invalidate(Rejection::InvalidHand);return;}
    if(!Valid(pose)||pose.at>now){invalidate(Rejection::InvalidSurface);return;}
    if(now-pose.at>t.staleMs){invalidate(Rejection::StalePose);return;}
    const Vec hand=pose.hand;
    s.surface=pose;
    s.rejection=PositionRejection(pose,leftShield,s.raised,t);
    const bool up=s.rejection==Rejection::None;
    if(handOk){s.hand=hand;Axes(hand,leftShield,s.forward,s.up,s.side);}
    if(up&&!s.raised)s.raisedAt=now;
    if(handOk&&std::isfinite(hand.x+hand.y+hand.z)) {
        const int last=(s.pathNext+State::kPath-1)%State::kPath;
        if(!s.pathCount||now<s.pathAt[last]||now-s.pathAt[last]>=8) {
            s.path[s.pathNext]=hand;s.pathAt[s.pathNext]=now;
            s.pathNext=(s.pathNext+1)%State::kPath;if(s.pathCount<State::kPath)++s.pathCount;
        }
        // the swing over the last swingMs: from the oldest sample still inside it
        int from=-1;
        for(int i=0;i<s.pathCount;++i) {
            const int k=(s.pathNext+State::kPath-s.pathCount+i)%State::kPath;
            if(now>=s.pathAt[k]&&now-s.pathAt[k]<=t.swingMs){from=k;break;}
        }
        s.swing=0;s.swingCos=0;
        if(from>=0) {
            float f0,u0,s0,f1,u1,s1;Axes(s.path[from],leftShield,f0,u0,s0);Axes(hand,leftShield,f1,u1,s1);
            const float ds=s1-s0,du=u1-u0,planar=std::hypot(ds,du);
            s.swing=(ds+du)*.70710678f;                 // along (side +1, up +1)
            s.swingCos=planar>1e-4f?s.swing/planar:0;
            if(up&&s.swing>=t.swingMetres&&s.swingCos>=t.swingCos) {
                // A new swing arms one parry; the rest of the same swing cannot re-arm it.
                // snapAt stays at the swing's start: the game thread spends it by that value.
                if(!s.swingSeen||now<s.swingSeen||now-s.swingSeen>t.swingMs){s.snapAt=now;s.snapUsed=false;}
                s.swingSeen=now;
            }
        }
    }
    if(!up&&s.raised)s.loweredAt=now;
    if(!s.loweredAt&&!up)s.loweredAt=now;
    s.raised=up;
    s.seenAt=pose.at;
}

// D2R supplies the attacker location, not a 3D impact. Approximate horizontal
// coverage around the actual attachment. Both shield faces work; require a
// broad face toward the attacker and placement near the attacker-to-body line.
inline Rejection Coverage(const Pose& p,Vec toAttacker,const Tuning& t,float* alignment=nullptr) {
    if(alignment)*alignment=0;
    if(!Valid(p)||!Finite(toAttacker))return Rejection::InvalidSurface;
    const Vec fromBody{toAttacker.x-p.eye.x,0,toAttacker.z-p.eye.z};
    const float distance=std::hypot(fromBody.x,fromBody.z);
    const Vec offset{p.center.x-p.eye.x,0,p.center.z-p.eye.z};
    const bool measured=Measured(p);
    if(distance>.05f) {
        const Vec direction=fromBody*(1/distance);
        const float along=math::Dot(offset,direction);
        const float side=std::fabs(offset.x*direction.z-offset.z*direction.x);
        const Vec across{direction.z,0,-direction.x};
        // Project the actual face onto the incoming attack corridor. Rotation
        // continuously changes coverage instead of a hard 60-degree cutoff.
        // 12 cm allows for a native hit targeting the torso rather than its centre.
        const float width=measured?std::fabs(math::Dot(p.halfRight,across))+std::fabs(math::Dot(p.halfUp,across))+.12f:t.lateral;
        if(along < -t.behind||along>distance+t.behind||side>width)return Rejection::OutsidePlacement;
    } else if(std::hypot(offset.x,offset.z)>t.lateral)return Rejection::OutsidePlacement;
    // Native damage targets the body, not the offset shield grip. Use the same
    // corridor for both measured and fallback poses: moving a shield within
    // that corridor must not rotate the incoming direction or its facing test.
    const Vec incoming=fromBody;
    if(math::Dot(incoming,incoming)<.0025f)return Rejection::OutsideCoverage;
    const float facing=std::fabs(math::Dot(p.normal,math::Normalized(incoming)));
    if(alignment)*alignment=facing;
    return facing>=(measured?.15f:t.coverCos)?Rejection::None:Rejection::OutsideCoverage;
}

// Why a raised, fresh guard did not parry (diagnostics only).
enum class ParryMiss { None, Disabled, NoSwing, Spent, Early, BeforeAttack, Range, Side };
inline const char* ParryMissName(ParryMiss miss) {
    switch(miss) {
        case ParryMiss::None:return "ok";
        case ParryMiss::Disabled:return "disabled";
        case ParryMiss::NoSwing:return "no_swing";
        case ParryMiss::Spent:return "spent";
        case ParryMiss::Early:return "early";
        case ParryMiss::BeforeAttack:return "before_attack";
        case ParryMiss::Range:return "range";
        case ParryMiss::Side:return "side";
        default:return "unknown";
    }
}
// A parry deflects rather than covers: the diagonal swing itself turns the face
// edge-on (3 of 6 timed swings on 2026-10-17). It needs only the shield out on
// the attacker's side of the eyes, never behind or across the body from them.
inline bool ParrySide(const Pose& p,Vec toAttacker) {
    if(!Valid(p)||!Finite(toAttacker))return false;
    const Vec fromBody{toAttacker.x-p.eye.x,0,toAttacker.z-p.eye.z};
    const Vec offset{p.center.x-p.eye.x,0,p.center.z-p.eye.z};
    return math::Dot(offset,fromBody)>0;
}
// A parry is a swing while the attack is incoming: at most parryMs before the
// hit, and no more than parryLeadMs before the attack began (0 = unknown).
inline ParryMiss ParryCheck(const State& s,Vec toAttacker,uint64_t now,const Tuning& t,uint64_t attackAt=0) {
    if(!t.parryMs)return ParryMiss::Disabled;
    if(!s.snapAt||now<s.snapAt)return ParryMiss::NoSwing;
    if(s.snapUsed)return ParryMiss::Spent;
    if(now-s.snapAt>t.parryMs)return ParryMiss::Early;
    if(attackAt&&s.snapAt+t.parryLeadMs<attackAt)return ParryMiss::BeforeAttack;
    if(std::hypot(toAttacker.x,toAttacker.z)>t.parryRange)return ParryMiss::Range;
    if(!ParrySide(s.surface,toAttacker))return ParryMiss::Side;
    return ParryMiss::None;
}

// toAttacker: grid-plane vector, player -> attacker, in metres. attackAt: when
// this attacker's attack began (0 = unknown). A timed parry is judged before
// face coverage; anything else must cover the attack to block.
inline Verdict Judge(const State& s,Vec toAttacker,uint64_t now,const Tuning& t,uint64_t attackAt=0,Rejection* reason=nullptr) {
    const auto reject=[&](Rejection why){if(reason)*reason=why;return Verdict::Open;};
    if(reason)*reason=Rejection::None;
    if(!s.raised)return reject(s.rejection);
    if(!s.seenAt||now<s.seenAt||now-s.seenAt>t.staleMs)return reject(Rejection::StalePose);
    if(ParryCheck(s,toAttacker,now,t,attackAt)==ParryMiss::None)return Verdict::Parry;
    const auto coverage=Coverage(s.surface,toAttacker,t);
    if(coverage!=Rejection::None)return reject(coverage);
    return Verdict::Block;
}
// The game thread spends a swing on the parry it produced.
inline void SpendParry(State& s,uint64_t snapAt) {if(s.snapAt==snapAt)s.snapUsed=true;}

// Diagnostic only (a parry is never after the hit): a melee hit was just blocked.
inline void NoteBlock(State& s,uint64_t now) {s.blockAt=now;}
// UI thread, after Update: a swing that started within 400 ms after a blocked
// hit - too late to parry it - once, with how late it was. Spends nothing.
inline bool SwingAfterBlock(State& s,uint64_t now,const Tuning& t,uint64_t& late) {
    constexpr uint64_t kWindow=400;
    if(!s.blockAt)return false;
    if(now<s.blockAt||now-s.blockAt>kWindow+t.swingMs){s.blockAt=0;return false;}
    if(!s.snapAt||s.snapAt<s.blockAt||s.snapAt-s.blockAt>kWindow)return false;
    late=s.snapAt-s.blockAt;s.blockAt=0;return true;
}
}
