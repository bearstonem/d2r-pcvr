#pragma once
#include "held_pose.h"
#include <algorithm>
#include <cmath>

// The drawn hand on its controller, and the arm that reaches it (skeletons.cpp PoseBody, [arms] mode=2).
//
// Where the hand goes. The OpenXR grip pose sits in the middle of the closed hand: the palm's middle, half way from
// the wrist joint to the middle finger's knuckle, goes there and the wrist bone that far behind it. The knuckle is
// not the same joint on every rig (the class skeletons, 2026-11-25): amazon, paladin and druid list a finger from its
// metacarpal (R_mid_finger_01, 4.4 cm out of the wrist, then the knuckle 02, the middle joint 03 and the end 04);
// the sorceress-style rigs (sorceress, necromancer, barbarian, warlock) and the assassin's middle finger start at the
// knuckle (R_finger_B_A, hand_middle_01). Taken from the first joint, the palm's middle came out at 2 cm instead of
// 4.3 and those three classes' hands sat 2 cm past the grip.
//
// How long the arm is. The calibration fits the arm to the T pose (body::ArmScale), and that span is short of what
// a reach needs: the 2026-11-25 Paladin session's shield arm, the only one that grew, needed 1.10-1.35 times its
// fitted length in every 5 s sample. Any other arm stopped short of its controller by the difference (8 cm on
// average, 12 most), the hand drawn on the forearm. Now every tracked arm is drawn longer for as long as its hand
// needs it: along its own length (upper arm and forearm alike), the arm no thicker and the hand and what it holds
// unchanged in size, so the wrist lands where the controller puts it. MostStretch bounds it: further than twice the
// arm is lost tracking or a body that is not under the eyes, not a reach.
//
// Pure maths on row-vector matrices (a bone's axes in rows 0..2, its origin in 12..14), host-tested in
// steamvr/hand_reach_test.cpp with every class's rig.
namespace skel::reach {
using held::V;

// Which joint of a finger, listed from the wrist outward, is its knuckle: the third from its end on every hero rig
// (metacarpal, knuckle, middle, end - or knuckle, middle, end).
inline int Knuckle(int joints) {return joints>=4?joints-3:0;}

// The wrist -> palm's-middle vector once held::Turn has turned the hand (pLocal, kLocal, det, point, along as
// Turn takes them). palmLocal: wrist -> palm's middle in the hand bone's own frame, axes taken at unit length
// (RotOnly through the bone's inverse, times the bone's scale). Turn maps the frame (a1, a2, a3) onto (b1, b2, b3);
// so does this.
inline V Palm(V pLocal,V kLocal,float det,V palmLocal,V point,V along) {
    using namespace held;
    const V a1=pLocal,a2=Norm(Sub(kLocal,Scale(a1,Dot(kLocal,a1)))),a3=Cross(a1,a2);
    const V b1=point,b2=Norm(Sub(along,Scale(b1,Dot(along,b1)))),b3=Scale(Cross(b1,b2),det);
    return Add(Add(Scale(b1,Dot(palmLocal,a1)),Scale(b2,Dot(palmLocal,a2))),Scale(b3,Dot(palmLocal,a3)));
}

// The arm drawn at most this many times its length; past it the hand is left short (and counted).
inline constexpr float MostStretch=2.f;
// How many times its length the arm is drawn so the wrist reaches `distance` from the shoulder: 1 in reach. The IK
// stops at 0.999 of the arm (skeletons.cpp Elbow), so the factor carries that margin and the wrist lands exactly.
inline float Stretch(float distance,float arm) {
    if(!std::isfinite(distance)||!std::isfinite(arm)||arm<=1e-4f||distance<=0)return 1;
    return std::clamp(distance/(arm*.999f),1.f,MostStretch);
}

// A bone drawn k times longer along the unit direction a (model space), about the point `from`: its axes and its
// offset from `from` lose nothing across a, gain k along it. (body::ScaleArmBone grows it every way.)
inline void StretchBone(float* m,V from,V a,float k) {
    for(int r=0;r<3;++r) {
        const float d=(m[r*4]*a.x+m[r*4+1]*a.y+m[r*4+2]*a.z)*(k-1);
        m[r*4]+=a.x*d;m[r*4+1]+=a.y*d;m[r*4+2]+=a.z*d;
    }
    const V p{m[12]-from.x,m[13]-from.y,m[14]-from.z};
    const float d=held::Dot(p,a)*(k-1);
    m[12]=from.x+p.x+a.x*d;m[13]=from.y+p.y+a.y*d;m[14]=from.z+p.z+a.z*d;
}
// One arm drawn k times longer along itself: the upper arm's bones (all under the shoulder bone that are not under
// the elbow) along shoulder -> elbow; the forearm's (under the elbow) along elbow -> wrist and carried out with the
// elbow. The wrist, and an attach bone on the forearm (shield_attach: the shield's wrist stop reads its axes), are
// carried whole: moved as a point of the forearm, their own axes kept, so the hand keeps its turn and size. The
// caller re-derives what hangs under those (fingers, what the hand holds) from their local transforms.
// m: count matrices of 16 floats; segment: per bone 0 not stretched, 1 the upper arm, 2 the forearm, 3 carried whole
// on the forearm. False when the arm has no length.
inline bool StretchArm(float* m,const signed char* segment,int count,int shoulder,int elbow,int wrist,float k) {
    if(!(k>1.f)||shoulder<0||elbow<0||wrist<0||shoulder>=count||elbow>=count||wrist>=count)return false;
    auto at=[&](int b){return V{m[b*16+12],m[b*16+13],m[b*16+14]};};
    const V S=at(shoulder),E=at(elbow),W=at(wrist),upper=held::Sub(E,S),lower=held::Sub(W,E);
    if(held::Len(upper)<1e-5f||held::Len(lower)<1e-5f)return false;
    const V au=held::Norm(upper),al=held::Norm(lower),E2=held::Add(S,held::Scale(upper,k));
    for(int b=0;b<count;++b) {
        float* x=m+b*16;
        if(segment[b]==1)StretchBone(x,S,au,k);
        else if(segment[b]==2||segment[b]==3) {
            x[12]+=E2.x-E.x;x[13]+=E2.y-E.y;x[14]+=E2.z-E.z;   // carried with the elbow, then stretched from it
            if(segment[b]==2)StretchBone(x,E2,al,k);
            else {
                const V p{x[12]-E2.x,x[13]-E2.y,x[14]-E2.z};const float d=held::Dot(p,al)*(k-1);
                x[12]+=al.x*d;x[13]+=al.y*d;x[14]+=al.z*d;
            }
        }
    }
    return true;
}

// The ten-second account of one arm ([0] right, [1] left of the body): how often it was drawn longer and by how
// much, how far its hand would have stopped short of the controller without that, and how often and how far it was
// still left short (past MostStretch). Centimetres at the player's scale.
struct Tally {
    unsigned poses=0,stretched=0,shortPoses=0;
    float most=1,sum=0,wouldMissCm=0,shortCm=0;
    // need: Stretch() before the cap (distance / 0.999 arm); upm: model units per metre; distance and arm in model units;
    // leftCm: how far the posed wrist is from its target afterwards.
    void Add(float distance,float arm,float upm,float leftCm) {
        if(!std::isfinite(distance)||!std::isfinite(arm)||!(arm>0)||!(upm>0))return;
        ++poses;
        const float need=distance/(arm*.999f);
        if(need>1.f) {
            ++stretched;sum+=std::min(need,MostStretch);most=std::max(most,std::min(need,MostStretch));
            wouldMissCm=std::max(wouldMissCm,(distance-arm*.999f)/upm*100.f);
        }
        if(std::isfinite(leftCm)&&leftCm>1.f){++shortPoses;shortCm=std::max(shortCm,leftCm);}
    }
    void Merge(const Tally& o) {
        poses+=o.poses;stretched+=o.stretched;shortPoses+=o.shortPoses;sum+=o.sum;
        most=std::max(most,o.most);wouldMissCm=std::max(wouldMissCm,o.wouldMissCm);shortCm=std::max(shortCm,o.shortCm);
    }
    float Mean()const{return stretched?sum/stretched:1.f;}
    bool Matters()const{return stretched>0||shortPoses>0;}
};
}
