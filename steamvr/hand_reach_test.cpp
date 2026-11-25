// The drawn hand on its controller and the arm that reaches it (vr/hand_reach.h), with every hero class's rig and a
// real calibration: where the palm goes, how long the calibrated arm is, and how far the hand was left short of the
// controller before the arm could stretch - at rest, at full extension and past it.
#include "../vr/hand_reach.h"
#include "body_tracking.h"
#include "shapeshift_form.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace skel;
using held::V;

static int checks=0;
static void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
static bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
static bool NearV(V a,V b,float e=1e-4f){return Near(a.x,b.x,e)&&Near(a.y,b.y,e)&&Near(a.z,b.z,e);}

// The right arm of each class's rig as built (data/hd/character/player/<class>/skeleton, bind pose, model units):
// eyes' height, the shoulder joints apart, upper arm, forearm, wrist -> the middle finger's first joint, wrist ->
// its knuckle, and how many joints that finger has. Measured 2026-11-25; the in-game bind pose gives the same eyes
// (the body pose lines' "standing").
struct Rig {const char* name;float eye,shoulders,upper,fore,firstJoint,knuckle;int joints;};
static const Rig kRigs[]={
    {"amazon",     6.2578f,1.2547f,0.8941f,0.9875f,0.1561f,0.3237f,4},   // R_mid_finger_01..04: metacarpal first
    {"sorceress",  5.9822f,1.3054f,0.8337f,0.9487f,0.3008f,0.3008f,3},   // R_finger_B_A..C: the knuckle first
    {"necromancer",6.8061f,1.6349f,1.0241f,1.2075f,0.3390f,0.3390f,3},
    {"paladin",    6.8915f,1.6717f,1.0664f,1.1002f,0.1839f,0.3636f,4},
    {"barbarian",  7.0459f,1.9131f,1.0424f,1.1624f,0.3792f,0.3792f,3},
    {"druid",      7.0691f,1.7153f,1.0564f,1.1224f,0.2061f,0.3821f,4},
    {"assassin",   5.8869f,1.0978f,0.8480f,0.9683f,0.3019f,0.3019f,3},   // hand_middle_01..03
    {"warlock",    6.7918f,1.6349f,1.0241f,1.2075f,0.3390f,0.3390f,3},
};
// A saved calibration: eyes 1.634 m up, grips 1.314 m apart in the T pose.
constexpr float kEye=1.634f,kSpan=1.314f;
// A session with it (Paladin, 5 s samples of the shield arm, the arm that logged its target): the grip was on average
// 0.558 m and at most 0.604 m from the drawn shoulder; the farthest in 23 sessions was 0.745 m.
constexpr float kMeanGrip=.558f,kMostGrip=.604f,kFarthestGrip=.745f;

struct Fit {float upm,palm,scale,arm;};   // model units per metre; the palm's middle before the wrist; the arm's fit; shoulder -> wrist drawn
// Before: the palm's middle half way to the middle finger's first joint, the rig's own shoulders.
static Fit Before(const Rig& r) {
    Fit f;f.upm=framevr::body::UnitsPerMetre(r.eye,kEye,1);f.palm=.5f*r.firstJoint;
    f.scale=framevr::body::ArmScale(r.eye,kEye,kSpan,r.shoulders,r.upper+r.fore,f.palm);f.arm=(r.upper+r.fore)*f.scale;return f;
}
// Now: half way to the knuckle, and a person's shoulders where the rig's are wider (skeletons.cpp PoseBody).
static Fit After(const Rig& r) {
    Fit f;f.upm=framevr::body::UnitsPerMetre(r.eye,kEye,1);f.palm=.5f*(r.joints>=4?r.knuckle:r.firstJoint);
    Check(reach::Knuckle(r.joints)==(r.joints>=4?1:0),"the knuckle is the finger's third joint from its end");
    f.scale=framevr::body::ArmScale(r.eye,kEye,kSpan,framevr::shapeshift::ArmFitShoulders(r.shoulders,r.eye),r.upper+r.fore,f.palm);
    f.arm=(r.upper+r.fore)*f.scale;return f;
}
// How far the palm's middle (half way to the knuckle) ends short of a grip `grip` metres from the shoulder, along a
// straight reach, cm: + short of it, - past it. The wrist is aimed `palm` before the grip; the IK stops at 0.999 of the arm.
static float ShortBefore(const Rig& r,float grip) {
    const Fit f=Before(r);const float d=grip*f.upm,wrist=std::min(d-f.palm,.999f*f.arm);
    return (d-(wrist+.5f*r.knuckle))/f.upm*100;
}
static float ShortAfter(const Rig& r,float grip,float* stretch=nullptr) {
    const Fit f=After(r);const float d=grip*f.upm,target=d-f.palm,k=reach::Stretch(target,f.arm);
    if(stretch)*stretch=k;
    const float wrist=std::min(target,.999f*k*f.arm);
    return (d-(wrist+f.palm))/f.upm*100;
}

static void Classes() {
    for(const Rig& r:kRigs) {
        const Fit b=Before(r),a=After(r);
        // The palm's middle sits 4.1-4.4 cm before the wrist on every class; from the first joint it was 2.0-2.4 on
        // the three rigs that list a metacarpal.
        const float palmCm=a.palm/a.upm*100,wasCm=b.palm/b.upm*100;
        Check(palmCm>4.f&&palmCm<4.5f,"the palm's middle 4-4.5 cm before the wrist, every class");
        Check(r.joints>=4?(wasCm>1.9f&&wasCm<2.5f):Near(wasCm,palmCm),"the metacarpal rigs' palm was half that");
        // The calibration's fit stays inside its bounds for every class (0.65-1.6): nothing is cut by the clamp.
        Check(a.scale>.7f&&a.scale<.95f&&b.scale>.7f&&b.scale<1.f,"no class's fit is bounded");
        // At rest in the T pose the calibration measured: the arm reaches the grip as fitted.
        float k=0;
        Check(Near(ShortAfter(r,kSpan*.5f-framevr::shapeshift::ArmFitShoulders(r.shoulders,r.eye)*.5f/a.upm,&k),0,.05f)&&k<1.0011f,
              "in the T pose the palm's middle is on the grip, the arm as fitted");
        Check(r.joints>=4?ShortBefore(r,kSpan*.5f-r.shoulders*.5f/b.upm)<-1.9f:Near(ShortBefore(r,kSpan*.5f-r.shoulders*.5f/b.upm),0,.1f),
              "before, the metacarpal rigs' palm sat 2 cm past the grip in the T pose");
        // At rest in front (the grip 0.30 m from the shoulder, the elbow bent): nothing stretches, the hand is on it.
        Check(Near(ShortAfter(r,.30f,&k),0,.01f)&&k==1,"a bent arm reaches without stretching");
        // Reaching as the session did: the hand stopped short before, on every class; now it is on the grip.
        Check(ShortBefore(r,kMeanGrip)>4.f&&ShortBefore(r,kMostGrip)>8.5f,"before: the average reach left the hand 4+ cm short, the most 8.5+");
        for(float grip:{kMeanGrip,kMostGrip,kFarthestGrip}) {
            Check(Near(ShortAfter(r,grip,&k),0,.05f)&&k>1&&k<reach::MostStretch,"full extension: the arm stretches and the hand is on the grip");
        }
        Check(ShortBefore(r,kFarthestGrip)>22.f,"the farthest reach left the hand 22+ cm short");
        // Past twice the arm (tracking lost, the body not under the eyes): the stretch stops; the hand is left short and counted.
        const float past=2.5f*(a.arm+a.palm)/a.upm;
        Check(ShortAfter(r,past,&k)>10.f&&k==reach::MostStretch,"past twice the arm the hand is left short");
        std::printf("  %-11s palm %.1f -> %.1f cm, arm fit %.3f -> %.3f (shoulder to wrist %.3f -> %.3f m); short at the average reach %.1f -> %.1f cm, "
                    "most %.1f -> %.1f cm, farthest %.1f -> %.1f cm\n",r.name,wasCm,palmCm,b.scale,a.scale,b.arm/b.upm,a.arm/a.upm,
                    ShortBefore(r,kMeanGrip),ShortAfter(r,kMeanGrip),ShortBefore(r,kMostGrip),ShortAfter(r,kMostGrip),
                    ShortBefore(r,kFarthestGrip),ShortAfter(r,kFarthestGrip));
    }
    // The paladin of that session: arm 0.437 m drawn shoulder to wrist (the shield arm's log: requested 2.478 at
    // extension 1.348 = 1.840 model units), 0.416 m now with the palm where it is; his hand was 7.9 cm short on average.
    const Rig& paladin=kRigs[3];
    Check(Near(Before(paladin).arm,1.840f,.01f)&&Near(Before(paladin).arm/Before(paladin).upm,.437f,.002f),"the paladin's arm as the log measured it");
    Check(Near(After(paladin).arm/After(paladin).upm,.416f,.002f),"the paladin's arm now");
    Check(Near(ShortBefore(paladin,kMeanGrip),7.9f,.2f)&&Near(ShortBefore(paladin,kMostGrip),12.4f,.2f),"the paladin's hand 7.9 cm short on average, 12.4 most");
    // The barbarian's wide shoulders (0.27 of his eyes) no longer cut his arms: fitted from a person's.
    const Rig& barbarian=kRigs[4];
    Check(After(barbarian).scale>Before(barbarian).scale+.04f,"the barbarian's arm fit from a person's shoulders");
    Check(Near(framevr::shapeshift::ArmFitShoulders(paladin.shoulders,paladin.eye),paladin.shoulders,1e-3f),"a person's shoulders are the paladin's");
}

// Palm(): the palm's middle where held::Turn puts the hand, the knuckle off the pointing line (the paladin's is 4 deg off).
static void PalmFollowsTurn() {
    const float s=std::sin(.6f),c=std::cos(.6f);
    float hand[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, 3,4,5,1};   // a bone turned about y, unit axes
    auto through=[&](V local){return V{local.x*hand[0]+local.y*hand[4]+local.z*hand[8],local.x*hand[1]+local.y*hand[5]+local.z*hand[9],
                                       local.x*hand[2]+local.y*hand[6]+local.z*hand[10]};};
    auto back=[&](V world){return V{world.x*hand[0]+world.y*hand[1]+world.z*hand[2],world.x*hand[4]+world.y*hand[5]+world.z*hand[6],
                                    world.x*hand[8]+world.y*hand[9]+world.z*hand[10]};};   // the inverse of a turn: its transpose
    const V midLocal{0,-.184f,.0f},knuckleLocal{.027f,-.362f,.0f},indexLocal{0,-.19f,.1f},pinkyLocal{0,-.2f,-.1f};
    const V pLocal=held::Norm(back(through(midLocal))),kLocal=held::Norm(back(held::Sub(through(indexLocal),through(pinkyLocal))));
    const V point=held::Norm({.2f,-.3f,.9f}),along=held::Norm({0,1,.3f});
    const V palmLocal=held::Scale(knuckleLocal,.5f);
    const V palm=reach::Palm(pLocal,kLocal,1,palmLocal,point,along);
    held::Turn(hand,pLocal,kLocal,1,point,along);
    Check(NearV(palm,held::Scale(through(knuckleLocal),.5f)),"the palm's middle is where the turned hand puts it");
    Check(Near(held::Len(palm),.5f*held::Len(knuckleLocal)),"half the knuckle's distance");
    // On the pointing line (the sorceress-style rigs), it is half the reach along the controller's point: as before.
    const V straight=reach::Palm(pLocal,kLocal,1,held::Scale(pLocal,.15f),point,along);
    Check(NearV(straight,held::Scale(point,.15f)),"a knuckle on the line: half a hand along the point");
}

// StretchArm: an arm drawn longer along itself, no thicker, the hand unchanged.
static void Stretching() {
    // 0 the chest, 1 the shoulder, 2 an upper twist (off the bone's line), 3 the elbow, 4 an elbow plate (off it),
    // 5 a forearm twist, 6 the wrist, 7 a finger, 8 shield_attach on the forearm (carried whole).
    float m[9*16]={};
    auto set=[&](int b,V x,V y,V z,V p){float* q=m+b*16;q[0]=x.x;q[1]=x.y;q[2]=x.z;q[4]=y.x;q[5]=y.y;q[6]=y.z;q[8]=z.x;q[9]=z.y;q[10]=z.z;
                                        q[12]=p.x;q[13]=p.y;q[14]=p.z;q[15]=1;};
    const V X{1,0,0},Y{0,1,0},Z{0,0,1};
    const V S{-.8f,5.8f,-.1f},E{-.9f,4.8f,.2f},W{-.8f,4.0f,.9f};
    set(0,X,Y,Z,{0,5.5f,0});set(1,X,Y,Z,S);set(2,Z,X,Y,{-.88f,5.3f,.03f});set(3,X,Y,Z,E);set(4,Y,Z,X,{-.95f,4.85f,.12f});
    set(5,X,Y,Z,{-.85f,4.4f,.55f});set(6,held::Norm({1,1,0}),held::Norm({-1,1,0}),Z,W);set(7,X,Y,Z,{-.8f,3.8f,1.05f});
    set(8,held::Norm({0,1,1}),held::Norm({0,-1,1}),X,{-.7f,4.5f,.5f});
    const signed char segment[9]={0,1,1,2,2,2,3,0,3};
    float was[9*16];for(int i=0;i<9*16;++i)was[i]=m[i];
    auto at=[](const float* q,int b){return V{q[b*16+12],q[b*16+13],q[b*16+14]};};
    auto row=[](const float* q,int b,int r){return V{q[b*16+r*4],q[b*16+r*4+1],q[b*16+r*4+2]};};
    Check(!reach::StretchArm(m,segment,9,1,3,6,1.f)&&!reach::StretchArm(m,segment,9,1,3,6,.8f),"nothing to stretch at or below its length");
    const float k=1.3f;
    Check(reach::StretchArm(m,segment,9,1,3,6,k),"stretched");
    const V au=held::Norm(held::Sub(E,S)),al=held::Norm(held::Sub(W,E));
    const V E2=at(m,3),W2=at(m,6);
    Check(NearV(at(m,1),S)&&NearV(E2,held::Add(S,held::Scale(held::Sub(E,S),k))),"the elbow 1.3 times as far out, the shoulder where it was");
    Check(NearV(W2,held::Add(E2,held::Scale(held::Sub(W,E),k))),"the forearm 1.3 times as long, carried with the elbow");
    for(int b:{1,2,3,4,5})for(int r=0;r<3;++r) {
        const V a=b<3?au:al,before=row(was,b,r),after=row(m,b,r),change=held::Sub(after,before);
        const V across=held::Sub(change,held::Scale(a,held::Dot(change,a)));
        Check(held::Len(across)<1e-5f&&Near(held::Dot(after,a),k*held::Dot(before,a)),"axes longer along the arm only: no thicker");
    }
    for(int r=0;r<3;++r)Check(NearV(row(m,6,r),row(was,6,r))&&NearV(row(m,8,r),row(was,8,r)),"the hand and the shield's bone keep their turn and size");
    {   // the shield's bone moved as the point of the forearm it is: as far off its line, k times as far along it
        const V d0=held::Sub(at(was,8),E),d1=held::Sub(at(m,8),E2);
        Check(Near(held::Dot(d1,al),k*held::Dot(d0,al))&&NearV(held::Sub(d1,held::Scale(al,held::Dot(d1,al))),held::Sub(d0,held::Scale(al,held::Dot(d0,al)))),
              "the shield's bone carried with the forearm");
    }
    Check(NearV(at(m,7),at(was,7))&&NearV(at(m,0),at(was,0)),"under the hand and off the arm: left to the caller, untouched");
    auto offLine=[&](V p,V from,V a){const V d=held::Sub(p,from);return held::Len(held::Sub(d,held::Scale(a,held::Dot(d,a))));};
    Check(Near(offLine(at(m,4),E2,al),offLine(at(was,4),E,al))&&Near(offLine(at(m,2),S,au),offLine(at(was,2),S,au)),
          "pieces off the arm's line stay as far from it");
    // Turned as the IK turns it (row-vector, about the shoulder), the arm keeps the stretched lengths.
    const float t=.7f,ct=std::cos(t),st=std::sin(t);
    auto turn=[&](V p){const V d=held::Sub(p,S);return held::Add(S,V{d.x*ct+d.z*st,d.y,-d.x*st+d.z*ct});};
    Check(Near(held::Len(held::Sub(turn(E2),S)),k*held::Len(held::Sub(E,S)))&&Near(held::Len(held::Sub(turn(W2),turn(E2))),k*held::Len(held::Sub(W,E))),
          "the IK's turn keeps the lengths");
}

// The ten-second account.
static void Tallies() {
    reach::Tally t;
    Check(!t.Matters()&&t.Mean()==1,"nothing yet");
    const float upm=4.22f,arm=1.75f;
    t.Add(1.f,arm,upm,0);                          // in reach
    t.Add(arm*.999f*1.2f,arm,upm,0);               // 1.2 times: stretched, on the grip
    t.Add(arm*.999f*1.3f,arm,upm,.2f);             // 1.3 times, the IK's rounding: not short
    t.Add(arm*.999f*2.5f,arm,upm,25.f);            // past the cap: short
    t.Add(NAN,arm,upm,0);
    Check(t.poses==4&&t.stretched==3&&t.shortPoses==1&&Near(t.shortCm,25.f),"counted");
    Check(Near(t.most,reach::MostStretch)&&Near(t.Mean(),(1.2f+1.3f+2.f)/3,1e-3f),"stretch, most and average (the cap counted at the cap)");
    Check(Near(t.wouldMissCm,arm*.999f*1.5f/upm*100,1e-2f),"how far the hand would have stopped short");
    reach::Tally u;u.Add(arm*.999f*1.1f,arm,upm,0);u.Merge(t);
    Check(u.poses==5&&u.stretched==4&&u.shortPoses==1&&u.Matters(),"merged");
    Check(reach::Stretch(NAN,1)==1&&reach::Stretch(1,0)==1&&reach::Stretch(-1,1)==1&&reach::Stretch(10,1)==reach::MostStretch,"bad input never stretches");
}

int main() {
    std::puts("Hand reach, every class at eyes 1.634 m and a 1.314 m span:");
    Classes();
    PalmFollowsTurn();
    Stretching();
    Tallies();
    std::printf("Hand reach: %d checks passed\n",checks);
}
