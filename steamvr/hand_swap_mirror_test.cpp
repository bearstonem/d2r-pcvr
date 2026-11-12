// Left-handed play (#307; #513, #515) against the mirror. For every weapon kind and the shield, the
// left-handed pose for the mirror image of the controllers must be the mirror image of the right-handed
// pose: left-handed(mirror(P)) == mirror(right-handed(P)), mirror being the reflection through the body's
// middle plane (x -> -x in model space and in the head's frame; the hands exchanged). An item cannot be
// drawn from a mirrored matrix, so a left-handed attach bone is compared as the mirror image it stands for
// (hand_swap_pose.h AsMirrored, shield_pose.h PlaceAcross's mirror turn) - which is what melee contact,
// the shield's guard and the props' pushers read - and checked to be drawable as it is.
//
// A small symmetric hero rig is posed the way skeletons.cpp PoseBody poses it, from the production pure
// maths: the hand turned like its controller (held_pose.h Aim / Turn), the item seated on its wrist and,
// left-handed, on the other one (hand_swap_pose.h MirrorGrip), Weapon Adjust (held_pose.h Adjust), the
// item turned over (MatchHandedness), the shield's wrist stop and placement (shield_pose.h), the items put
// back on the wrists holding them (Reseat); for two-handed kinds the mirror world (EnterMirror /
// LeaveMirror). Modelled here as PoseBody does it: the body turned to the camera, the two-bone arm IK
// (Elbow, AimChain) and Propagate down the rig. Left out: the forearm's twist (the same code for either
// arm) and the shield's steady filter (time only).
//
// --report prints the error left-handed play had before 2026-11-11 (no Reseat: the right wrist's bend
// stop under a shield carried the main weapon, its child in the rig, out of the left hand).
#include "../vr/held_pose.h"
#include "../vr/hand_swap_pose.h"
#include "../vr/shield_pose.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace skel;
using bow::Mat;
using bow::Vec;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

static Mat Mul(const Mat& a,const Mat& b){return bow::Multiply(a,b);}
static Mat Inv(const Mat& a){Mat r;Check(bow::Inverse(a,r),"an invertible bone");return r;}
static Vec Pos(const Mat& a){return bow::Position(a);}
static Vec Row(const Mat& a,int r){return {a.m[r*4],a.m[r*4+1],a.m[r*4+2]};}
static Mat Mirror(const Mat& a){return handswap::MirrorX(a);}   // the world reflected: every axis and the origin x -> -x
static Vec Mirror(Vec v){return {-v.x,v.y,v.z};}
static held::V H(Vec v){return {v.x,v.y,v.z};}
static Vec B(held::V v){return {v.x,v.y,v.z};}
// A proper turn (pitch, yaw, roll about its own x, y, z, degrees) at a point.
static Mat Turned(float x,float y,float z,Vec at) {
    const float a[6]={0,0,0,x,y,z};Mat m;held::AdjustTurn(a,m.m);bow::Position(m,at);return m;
}
// The turn between two poses' axes, row by row, degrees (handedness and scale aside), and their origins apart, cm.
static float Degrees(const Mat& a,const Mat& b) {
    float most=0;
    for(int r=0;r<3;++r) {
        const Vec u=bow::Normalized(Row(a,r)),v=bow::Normalized(Row(b,r));
        most=std::max(most,std::atan2(bow::Length(bow::Cross(u,v)),bow::Dot(u,v))*57.29578f);
    }
    return most;
}
static float Cm(const Mat& a,const Mat& b){return bow::Length(Pos(a)-Pos(b))*100;}   // the rig is in metres
static bool Same(const Mat& a,const Mat& b,float deg=.02f,float cm=.01f) {
    for(int r=0;r<3;++r)if(std::fabs(bow::Length(Row(a,r))-bow::Length(Row(b,r)))>1e-4f)return false;
    return Degrees(a,b)<=deg&&Cm(a,b)<=cm;
}

// ---- the rig: bones parents first, the model's left at +x, the right arm out along -x, palms down ----
enum Bone {Root,Spine,RU,RL,RH,RI,RM,RP,RW,LU,LL,LH,LI,LM,LP,LW,SA,Count};
static const std::vector<int> kParents={-1,Root,Spine,RU,RL,RH,RH,RH,RH,Spine,LU,LL,LH,LH,LH,LH,LL};
static const std::vector<std::string> kNames={"root","spine_01","R_upperarm","R_forearm","R_hand","R_ind_finger_01","R_mid_finger_01",
    "R_pnk_finger_01","R_weapon_attach","L_upperarm","L_forearm","L_hand","L_ind_finger_01","L_mid_finger_01","L_pnk_finger_01",
    "L_weapon_attach","shield_attach"};
struct Rig {std::vector<Mat> bind;handswap::MirrorRig mirror;};
// left 0: each left bone is its right twin's mirror image (determinant -1, as D2R's left attach bone is);
// left 1: the same with one of its own axes turned over (another axis convention, determinant +1).
static Rig MakeRig(int left) {
    Rig rig;rig.bind.assign(Count,bow::Identity());
    rig.bind[Spine]=Turned(0,0,0,{0,1.3f,0});
    rig.bind[RU]=Turned(10,20,-80,{-.18f,1.45f,0});rig.bind[RL]=Turned(15,-5,-85,{-.45f,1.45f,0});
    rig.bind[RH]=Turned(-30,40,70,{-.70f,1.45f,0});   // the thumb forward (+z)
    rig.bind[RI]=Turned(5,10,15,{-.785f,1.452f,.028f});rig.bind[RM]=Turned(-5,0,10,{-.79f,1.45f,.004f});
    rig.bind[RP]=Turned(0,-10,5,{-.775f,1.448f,-.026f});
    rig.bind[RW]=Turned(60,-20,35,{-.76f,1.43f,.002f});   // R_weapon_attach, in the fist
    for(int r=RU;r<=RW;++r) {
        Mat m=Mirror(rig.bind[r]);
        if(left==1)handswap::TurnOver(m,r%3);
        rig.bind[r-RU+LU]=m;
    }
    Mat sa=Turned(-15,70,20,{.57f,1.49f,.01f});   // shield_attach on the back of the left forearm
    if(left==0)handswap::TurnOver(sa,1);           // of the left side's handedness
    rig.bind[SA]=sa;
    std::vector<char> inHand(Count,0);
    for(int b:{RI,RM,RP,RW,LI,LM,LP,LW})inHand[b]=1;
    const int upper[2]={RU,LU},lower[2]={RL,LL},hand[2]={RH,LH},weapon[2]={RW,LW};
    rig.mirror=handswap::LearnMirror(kNames,kParents,rig.bind,inHand,upper,lower,hand,weapon);
    Check(rig.mirror.ok&&rig.mirror.asymmetry<1e-5f,"the rig's twins are learned; its bind pose is symmetric");
    return rig;
}

// ---- the controllers: what the bridge hands the hook, by physical hand (0 right, 1 left) ----
struct Controller {float q[4];float p[3];};   // the head's turn-only frame (x right, y up, z back), metres
struct Scene {Controller hand[2];float yaw;Vec eye;};   // the camera's yaw in model space; the eye in model space
// The same moment in a mirror standing in the body's middle plane: each controller is the other's
// reflection (an OpenXR left grip is the reflection of a right one turned over its x: X R X), the yaw the
// other way, the eye across.
static Scene Mirrored(const Scene& s) {
    Scene m=s;
    for(int side=0;side<2;++side) {
        m.hand[side]=s.hand[side^1];
        handswap::MirrorQuat(m.hand[side].q);handswap::MirrorPoint(m.hand[side].p);
    }
    m.yaw=-s.yaw;m.eye=Mirror(s.eye);
    return m;
}
static uint32_t g_seed=12345;
static float Random(float a,float b){g_seed=g_seed*1664525u+1013904223u;return a+(b-a)*float(g_seed>>8)/16777216.f;}
static Scene RandomScene() {
    Scene s;
    for(int side=0;side<2;++side) {
        // a uniformly random turn (Shoemake)
        const float u1=Random(0,1),u2=Random(0,6.2831853f),u3=Random(0,6.2831853f);
        const float a=std::sqrt(1-u1),b=std::sqrt(u1);
        s.hand[side].q[0]=a*std::sin(u2);s.hand[side].q[1]=a*std::cos(u2);s.hand[side].q[2]=b*std::sin(u3);s.hand[side].q[3]=b*std::cos(u3);
        const float out=Random(.05f,.45f);
        s.hand[side].p[0]=side==0?out:-out;s.hand[side].p[1]=Random(-.65f,.05f);s.hand[side].p[2]=Random(-.55f,-.05f);
    }
    s.yaw=Random(-.5f,.5f);s.eye={Random(-.03f,.03f),1.62f,.04f};
    return s;
}
// Held the way a weapon is held: a fist upright, knuckles ahead (the grip turned a quarter about its x),
// tipped up to 35 degrees about any axis. For --report: the errors a player meets, not the extremes.
static Scene NaturalScene() {
    Scene s=RandomScene();
    for(int side=0;side<2;++side) {
        const Vec axis=bow::Normalized({Random(-1,1),Random(-1,1),Random(-1,1)});
        const float half=Random(0,35)*.00872665f,h=.70710678f;
        const held::Q tip{axis.x*std::sin(half),axis.y*std::sin(half),axis.z*std::sin(half),std::cos(half)},upright{h,0,0,h};
        const held::Q q=held::Mul(tip,upright);
        s.hand[side].q[0]=q.x;s.hand[side].q[1]=q.y;s.hand[side].q[2]=q.z;s.hand[side].q[3]=q.w;
    }
    return s;
}

// ---- PoseBody's arms, as skeletons.cpp has them ----
static void TurnAboutSelf(Mat& w,const Mat& R){const Vec p=Pos(w);bow::Position(w,{0,0,0});w=Mul(w,R);bow::Position(w,p);}
// Re-derive every bone under `from`, keeping its relation to its parent in `old` (skeletons.cpp Propagate).
static void Propagate(const std::vector<Mat>& old,std::vector<Mat>& w,int from) {
    std::vector<char> moved(Count,0);moved[from]=1;
    for(int i=from+1;i<Count;++i) {
        const int p=kParents[i];
        if(p<0||!moved[p])continue;
        w[i]=Mul(Mul(old[i],Inv(old[p])),w[p]);moved[i]=1;
    }
}
static Vec Elbow(Vec S,float L1,float L2,Vec& T,Vec pole) {   // skeletons.cpp Elbow
    Vec st=T-S;float d=bow::Length(st);
    const float maxD=(L1+L2)*.999f,minD=std::fabs(L1-L2)*1.001f+1e-4f;
    if(d>maxD){T=S+st*(maxD/d);st=T-S;d=maxD;}
    if(d<minD){T=S+bow::Normalized(st)*minD;st=T-S;d=minD;}
    const Vec dir=st*(1/d);
    const float a=(L1*L1-L2*L2+d*d)/(2*d),hgt=std::sqrt(std::max(0.f,L1*L1-a*a));
    const Vec p=bow::Normalized(pole-dir*bow::Dot(pole,dir));
    return S+dir*a+p*hgt;
}
static std::vector<Mat> PoseArms(const Rig& rig,const Scene& sc,const float wristDeg[3]) {
    const float c=std::cos(sc.yaw),s=std::sin(sc.yaw);
    Mat T=bow::Identity();T.m[0]=c;T.m[2]=-s;T.m[8]=s;T.m[10]=c;   // the body turned to face where the camera looks
    std::vector<Mat> w(Count);
    for(int i=0;i<Count;++i)w[i]=Mul(rig.bind[i],T);
    const std::vector<Mat> base=w;
    const int U[2]={RU,LU},L[2]={RL,LL},K[2]={RH,LH},I[2]={RI,LI},M[2]={RM,LM},P[2]={RP,LP};
    for(int side=0;side<2;++side) {
        const int u=U[side],l=L[side],k=K[side];
        // PlanWrist: the hand's own directions from its knuckles, and where the controller turns them
        const Mat inverse=Inv(base[k]);
        const Vec point=Pos(base[M[side]])-Pos(base[k]),along=Pos(base[I[side]])-Pos(base[P[side]]);
        const held::V pLocal=held::Norm(H(bow::Vector(inverse,point))),kLocal=held::Norm(H(bow::Vector(inverse,along)));
        const float det=handswap::Det(base[k])<0?-1.f:1.f,reach=bow::Length(point);
        held::V aimPoint,aimAlong;
        Check(held::Aim(sc.hand[side].q,wristDeg,side,c,s,aimPoint,aimAlong),"the controller turns its hand");
        // ControllerPoint, the wrist half a hand behind it; Reach: the elbow down, out to its side and back
        const held::V offset=held::ToModel(sc.hand[side].p,c,s);
        Vec target=sc.eye+B(offset)-B(aimPoint)*(.5f*reach);
        const Vec S=Pos(w[u]);
        const float L1=bow::Length(Pos(w[l])-S),L2=bow::Length(Pos(w[k])-Pos(w[l]));
        Vec pole{side==0?-.5f:.5f,-1,-.3f};pole={pole.x*c+pole.z*s,pole.y,-pole.x*s+pole.z*c};
        const Vec E=Elbow(S,L1,L2,target,pole);
        TurnAboutSelf(w[u],bow::Rotation(Pos(w[l])-Pos(w[u]),E-Pos(w[u])));Propagate(base,w,u);   // AimChain
        TurnAboutSelf(w[l],bow::Rotation(Pos(w[k])-Pos(w[l]),target-Pos(w[l])));Propagate(base,w,l);
        held::Turn(w[k].m,pLocal,kLocal,det,aimPoint,aimAlong);Propagate(base,w,k);   // ApplyWrist
    }
    return w;
}

// ---- what the hands hold ----
struct ShieldSettings {float follow=50,roll=15,grip=2,out=5,up=2;int turn=0;};   // VR Settings > Shield, the owner's offsets
struct Setup {float wristDeg[3]={10,-5,5};float adjust[6]={};bool shield=false,dual=false;ShieldSettings sh;};
struct Posed {std::vector<Mat> w;int axis[2]={-1,-1};Mat shieldMirror=bow::Identity(),wristMirror=bow::Identity();float heldBack=0;};
// The palm's normal from the bind pose's knuckles (skeletons.cpp, the axis an item is turned over).
static Vec Palm(const Rig& rig,int side) {
    const int k=side?LH:RH,i=side?LI:RI,p=side?LP:RP,m=side?LM:RM;
    return bow::Cross(Pos(rig.bind[i])-Pos(rig.bind[p]),Pos(rig.bind[m])-Pos(rig.bind[k]));
}
// The shield's block: the wrist's bend stop, then the shield's bones in the arm's frame. Right-handed on
// the left arm (StopShieldWrist, Follow, Place); left-handed carried across to the right one (WristStop,
// PlaceAcross). The stop turns the wrist and carries what hangs under it in the rig (Propagate).
static void PlaceShield(const Rig& rig,Posed& out,const ShieldSettings& sh,bool leftHanded) {
    const auto& b=rig.bind;auto& w=out.w;
    int side=1;   // the shield's side of the forearm, from the bind pose
    {
        const Vec back=Palm(rig,1),fb=bow::Normalized(Pos(b[LH])-Pos(b[LL])),ob=Pos(b[SA])-Pos(b[LL]),off=ob-fb*bow::Dot(ob,fb);
        side=bow::Length(off)>.05f*bow::Length(Pos(b[LH])-Pos(b[LL]))&&bow::Dot(off,back)<0?-1:1;
    }
    const int k=leftHanded?RH:LH,l=leftHanded?RL:LL,ki=leftHanded?RI:LI,kp=leftHanded?RP:LP,km=leftHanded?RM:LM;
    {   // the stop: the back of the hand eased short of where it lies straight on the forearm
        const Vec back=leftHanded?bow::Cross(Pos(b[km])-Pos(b[k]),Pos(b[ki])-Pos(b[kp])):Palm(rig,1);
        const Vec local=bow::Vector(Inv(b[k]),back);
        const Mat rigid=leftHanded?Mul(Mul(b[k],Inv(b[l])),w[l]):Mul(Mul(b[k],Inv(b[SA])),w[SA]);
        const Vec now=bow::Normalized(bow::Vector(w[k],local)),stop=bow::Normalized(bow::Vector(rigid,local)),f=bow::Normalized(Pos(w[k])-Pos(w[l]));
        Vec given;out.heldBack=shield::WristStop(now,stop,f,given);
        if(out.heldBack>0){const auto before=w;TurnAboutSelf(w[k],bow::Rotation(now,given));Propagate(before,w,k);}
    }
    const float handed=shield::Handed(Pos(b[LL]),Pos(b[LH]),Pos(b[LI]),Pos(b[LP]),Pos(b[LM]),side);
    const auto bindFrame=shield::ArmFrame(Pos(b[LL]),Pos(b[LH]),Pos(b[LI]),Pos(b[LP]),handed);
    shield::Frame wristFrame,strapped;
    if(leftHanded) {
        const auto bindOther=shield::ArmFrame(Pos(b[RL]),Pos(b[RH]),Pos(b[RI]),Pos(b[RP]),-handed);
        wristFrame=shield::Carry(bindOther,b[RH],w[RH]);strapped=shield::ArmFrame(Pos(w[RL]),Pos(w[RH]),Pos(w[RI]),Pos(w[RP]),-handed);
    } else {
        wristFrame=shield::Carry(bindFrame,b[LH],w[LH]);strapped=shield::ArmFrame(Pos(w[LL]),Pos(w[LH]),Pos(w[LI]),Pos(w[LP]),handed);
    }
    const auto frame=shield::Roll(shield::Blend(strapped,wristFrame,sh.follow/shield::kFollowMaxPct),sh.roll);
    Check(handed!=0&&bindFrame.ok&&frame.ok,"the arm's frames");
    const float bindScale=bow::Length(Row(b[LH],0)),scale=bow::Length(Row(w[k],0)),units=bow::Length(Pos(w[k])-Pos(w[l]));
    Mat placed,mirror;
    if(leftHanded) {
        Check(shield::PlaceAcross(b[LW],bindFrame,Pos(b[LH]),bindScale,wristFrame.ok?wristFrame:frame,Pos(w[k]),scale,sh.turn,placed,&mirror),"the wrist's shield bone across");
        w[LW]=placed;out.wristMirror=mirror;
        Check(shield::PlaceAcross(b[SA],bindFrame,Pos(b[LH]),bindScale,frame,Pos(w[k]),scale,sh.turn,placed,&mirror),"shield_attach across");
        w[SA]=placed;out.shieldMirror=mirror;
    } else {
        Check(shield::Follow(b[LH],b[LW],w[LH],placed),"the wrist's shield bone");w[LW]=placed;
        Check(shield::Place(b[SA],bindFrame,Pos(b[LH]),bindScale,frame,Pos(w[k]),scale,placed),"shield_attach");w[SA]=placed;
    }
    const Vec move=shield::Offset(frame.along,frame.out,frame.up,sh.grip,sh.out,sh.up,units/.26f);
    w[SA].m[12]+=move.x;w[SA].m[13]+=move.y;w[SA].m[14]+=move.z;
}
// Right-handed: the main weapon in the right hand as the game holds it (grip[0]), a shield or a second
// weapon (grip[1]) in the left; Weapon Adjust on each weapon.
static Posed RightHanded(const Rig& rig,const Scene& sc,const Setup& set,const Mat grip[2]) {
    Posed out;out.w=PoseArms(rig,sc,set.wristDeg);auto& w=out.w;
    w[RW]=Mul(grip[0],w[RH]);w[LW]=Mul(grip[1],w[LH]);
    if(held::Adjusts(set.adjust)) {
        float local[16];held::AdjustTurn(set.adjust,local);
        held::Adjust(w[RW].m,set.adjust,local,1);
        if(!set.shield)held::Adjust(w[LW].m,set.adjust,local,1);
    }
    if(set.shield)PlaceShield(rig,out,set.sh,false);
    return out;
}
// Left-handed, phase 1: each attach bone on the other wrist as the mirror image of its own grip, adjusted,
// one of its axes turned over; the shield on the right forearm; reseat false: as before 2026-11-11.
static Posed LeftHanded(const Rig& rig,const Scene& sc,const Setup& set,const Mat grip[2],bool reseat=true) {
    Posed out;out.w=PoseArms(rig,sc,set.wristDeg);auto& w=out.w;
    const int own[2]={RH,LH},other[2]={LH,RH},bone[2]={RW,LW};
    for(int side=0;side<2;++side) {
        Mat mirrored;
        Check(handswap::MirrorGrip(grip[side],rig.bind[own[side]],rig.bind[other[side]],mirrored),"the mirror grip");
        out.axis[side]=handswap::TurnAxis(0,handswap::FlatAxis(Mul(grip[side],rig.bind[own[side]]),Palm(rig,side)));
        w[bone[side]]=Mul(mirrored,w[other[side]]);
    }
    if(held::Adjusts(set.adjust)) {
        float local[16];held::AdjustTurn(set.adjust,local);
        held::Adjust(w[RW].m,set.adjust,local,1);
        if(!set.shield)held::Adjust(w[LW].m,set.adjust,local,1);
    }
    for(int side=0;side<2;++side)Check(handswap::MatchHandedness(w[bone[side]],rig.bind[bone[side]],out.axis[side]),"a mirrored bone is turned over");
    handswap::Seat seats[2];
    for(int side=0;side<2;++side)if(!(side==1&&set.shield))seats[side]=handswap::SeatOn(bone[side],other[side],w);
    if(set.shield)PlaceShield(rig,out,set.sh,true);
    if(reseat)for(const auto& seat:seats)handswap::Reseat(seat,kParents,w);
    return out;
}

// ---- the weapon kinds: the game's grip on each (the attach bone in its wrist's frame) and an adjustment ----
struct Kind {const char* name;float grip[6];float adjust[6];};   // grip: pitch yaw roll (deg), x y z (cm)
static const Kind kKinds[]={
    {"sword",{80,-10,25,3,-2,1},{0,0,0,0,0,0}},{"axe",{70,15,-30,2,-3,2},{0,0,0,0,18,0}},   // [weapon_axe] yaw=18 ships
    {"mace",{95,5,40,4,-1,0},{0,0,0,0,0,0}},{"scepter",{100,-20,10,3,-2,-1},{1,-2,.5f,5,-7,3}},
    {"wand",{60,30,5,2,1,1},{-1.5f,2,0,-10,4,0}},{"dagger",{-75,10,-15,1,-2,0},{0,1,-1,0,0,12}},
    {"throwing",{40,-35,60,2,0,2},{0,0,0,8,8,8}},{"javelin",{88,2,-5,1,-1,1},{2,0,0,0,-6,0}},
    {"claw",{-20,85,10,5,0,-1},{0,0,3,15,0,-5}},{"orb",{10,0,90,6,2,0},{0,0,0,0,0,0}},
    {"torch",{85,-5,15,3,-3,0},{0,-3,0,-5,0,0}},
};
static void GripOf(const Kind& kind,Mat grip[2]) {
    grip[0]=Turned(kind.grip[0],kind.grip[1],kind.grip[2],{kind.grip[3]*.01f,kind.grip[4]*.01f,kind.grip[5]*.01f});
    // the game's own left-hand grip (a second weapon, a shield's wrist bone): another one, not a mirror
    grip[1]=Turned(kind.grip[0]-25,-kind.grip[1]+10,kind.grip[2]+35,{-kind.grip[3]*.01f,kind.grip[4]*.012f,-kind.grip[5]*.01f});
}

struct Error {float deg=0,cm=0,sumDeg=0,sumCm=0;int n=0,engaged=0;void Add(float d,float c){deg=std::max(deg,d);cm=std::max(cm,c);sumDeg+=d;sumCm+=c;++n;}};
static void Print(const char* what,const Error& e,int decimals=1) {
    std::printf("  %-40s at most %.*f deg %.*f cm, mean %.*f deg %.*f cm",what,decimals,e.deg,decimals,e.cm,decimals,e.n?e.sumDeg/e.n:0,decimals,e.n?e.sumCm/e.n:0);
    if(e.engaged)std::printf(" (stop engaged in %d of %d)",e.engaged,e.n);
    std::printf("\n");
}

int main(int argc,char** argv) {
    const bool report=argc>1&&!std::strcmp(argv[1],"--report");
    constexpr int kPoses=300;
    {   // the hand itself: turned like the mirrored controller it is the mirror image of the other hand
        const Rig rig=MakeRig(0);const float wrist[3]={10,-5,5};
        for(int i=0;i<kPoses;++i) {
            const Scene P=RandomScene();
            const auto right=PoseArms(rig,P,wrist),left=PoseArms(rig,Mirrored(P),wrist);
            for(int b=RU;b<=RP;++b) {
                const int twin=b-RU+LU;
                Check(Same(left[twin],handswap::Across(rig.mirror.carrier[twin],right[b])),"each arm bone is its twin's mirror image through the bind pose");
                Check(Same(left[b],handswap::Across(rig.mirror.carrier[b],right[twin])),"the other arm too");
            }
            // [hands] yaw and roll mirror on the left: a setting means the same bend on either hand
            held::V p0,a0,p1,a1;const float c=std::cos(P.yaw),s=std::sin(P.yaw);
            const Scene M=Mirrored(P);
            Check(held::Aim(P.hand[0].q,wrist,0,c,s,p0,a0)&&held::Aim(M.hand[1].q,wrist,1,c,-s,p1,a1),"aimed");
            Check(std::fabs(p0.x+p1.x)<1e-5f&&std::fabs(p0.y-p1.y)<1e-5f&&std::fabs(p0.z-p1.z)<1e-5f&&
                  std::fabs(a0.x+a1.x)<1e-5f&&std::fabs(a0.y-a1.y)<1e-5f&&std::fabs(a0.z-a1.z)<1e-5f,"the left hand points where the right one's mirror does");
        }
    }
    if(report)std::printf("left-handed(mirror(P)) against mirror(right-handed(P)), %d controller poses per row:\n",kPoses);
    for(int convention=0;convention<2;++convention) {
        const Rig rig=MakeRig(convention);
        for(const Kind& kind:kKinds) {
            Mat grip[2];GripOf(kind,grip);
            for(int holding=0;holding<3;++holding) {   // 0 with a shield, 1 two of the kind (dual wield), 2 with a shield and another Weapon Adjust
                Setup set;set.shield=holding!=1;set.dual=holding==1;
                if(holding!=2)std::memcpy(set.adjust,kind.adjust,sizeof set.adjust);
                else {const float a[6]={1.5f,-2,.5f,6,-9,4};std::memcpy(set.adjust,a,sizeof a);}
                for(int turn=0;turn<(set.shield?3:1);++turn) {
                    set.sh.turn=turn;
                    Error mainAfter,mainBefore,natural,off,shield,wristBone,capsuleBefore;
                    g_seed=777u+unsigned(convention*131+holding*17+turn);
                    for(int i=0;i<kPoses;++i) {
                        const Scene P=RandomScene();
                        const Posed right=RightHanded(rig,P,set,grip),left=LeftHanded(rig,Mirrored(P),set,grip);
                        // the main weapon: the mirror image, drawable as it is
                        const Mat main=handswap::AsMirrored(left.w[RW],left.axis[0]);
                        mainAfter.Add(Degrees(main,Mirror(right.w[RW])),Cm(main,Mirror(right.w[RW])));
                        if(right.heldBack>0)++mainAfter.engaged;
                        Check(Same(main,Mirror(right.w[RW])),"left-handed, the main weapon is the mirror image of right-handed's");
                        Check((handswap::Det(left.w[RW])<0)==(handswap::Det(rig.bind[RW])<0),"and of its own bone's handedness, to be drawn");
                        Check(std::fabs(left.heldBack-right.heldBack)<.01f,"the shield wrist's stop holds back as much on either arm");
                        if(set.dual) {   // the second weapon in the right hand
                            const Mat second=handswap::AsMirrored(left.w[LW],left.axis[1]);
                            off.Add(Degrees(second,Mirror(right.w[LW])),Cm(second,Mirror(right.w[LW])));
                            Check(Same(second,Mirror(right.w[LW])),"dual wield: the off-hand weapon is the mirror image too");
                            Check((handswap::Det(left.w[LW])<0)==(handswap::Det(rig.bind[LW])<0),"and drawable");
                        }
                        if(set.shield) {   // the shield on the right forearm: its mirror image, and drawable as it is
                            const Mat face=Mul(left.shieldMirror,left.w[SA]),wristShield=Mul(left.wristMirror,left.w[LW]);
                            shield.Add(Degrees(face,Mirror(right.w[SA])),Cm(face,Mirror(right.w[SA])));
                            wristBone.Add(Degrees(wristShield,Mirror(right.w[LW])),Cm(wristShield,Mirror(right.w[LW])));
                            Check(Same(face,Mirror(right.w[SA])),"the shield (shield_attach) is the mirror image of right-handed's");
                            Check(Same(wristShield,Mirror(right.w[LW])),"the shield's wrist bone too");
                            Check((handswap::Det(left.w[SA])<0)==(handswap::Det(rig.bind[SA])<0)&&(handswap::Det(left.w[LW])<0)==(handswap::Det(rig.bind[LW])<0),
                                "both of their own handedness, to be drawn");
                            // the contact capsule runs along the bone's y: before, the turned bone's
                            const Vec y=bow::Normalized(Row(left.w[SA],1)),want=bow::Normalized(Mirror(Row(right.w[SA],1)));
                            const float d=std::atan2(bow::Length(bow::Cross(y,want)),std::fabs(bow::Dot(y,want)))*57.29578f;
                            capsuleBefore.Add(d,0);
                            // before 2026-11-11: no Reseat - the stop carried the main weapon with the right wrist
                            const Posed old=LeftHanded(rig,Mirrored(P),set,grip,false);
                            const Mat was=handswap::AsMirrored(old.w[RW],old.axis[0]);
                            mainBefore.Add(Degrees(was,Mirror(right.w[RW])),Cm(was,Mirror(right.w[RW])));
                            if(right.heldBack>0)++mainBefore.engaged;
                            if(right.heldBack>.5f)Check(!Same(was,Mirror(right.w[RW])),"without Reseat the stop takes the main weapon out of the left hand");
                        }
                    }
                    if(set.shield)Check(mainBefore.engaged>kPoses/10&&mainBefore.deg>5,"the poses engage the shield wrist's stop: the regression is exercised");
                    if(report&&set.shield&&convention==0&&turn==0)for(int i=0;i<kPoses;++i) {   // as weapons are held
                        const Scene P=NaturalScene();
                        const Posed right=RightHanded(rig,P,set,grip),old=LeftHanded(rig,Mirrored(P),set,grip,false),now=LeftHanded(rig,Mirrored(P),set,grip);
                        const Mat was=handswap::AsMirrored(old.w[RW],old.axis[0]),is=handswap::AsMirrored(now.w[RW],now.axis[0]);
                        natural.Add(Degrees(was,Mirror(right.w[RW])),Cm(was,Mirror(right.w[RW])));if(right.heldBack>0)++natural.engaged;
                        Check(Same(is,Mirror(right.w[RW])),"held naturally, the main weapon is the mirror image as well");
                    }
                    if(report&&convention==0&&turn==0) {
                        std::printf("%s%s:\n",kind.name,holding==1?" + the same in the other hand":holding==2?" + shield, Weapon Adjust 1.5 -2 0.5 cm 6 -9 4 deg":" + shield");
                        if(set.shield){Print("main weapon before, any controller turn",mainBefore);Print("main weapon before, held naturally",natural);}
                        Print("main weapon now",mainAfter,4);
                        if(set.dual)Print("off-hand weapon now",off,4);
                        if(set.shield) {
                            Print("shield (shield_attach) now",shield,4);Print("shield's wrist bone now",wristBone,4);
                            Print("shield capsule axis from the drawn bone",capsuleBefore);
                        }
                    }
                }
            }
        }
        {   // nothing moved after seating (no shield): Reseat leaves every bone exactly as it was
            Mat grip[2];GripOf(kKinds[0],grip);Setup set;set.dual=true;
            const Scene P=RandomScene();
            const Posed a=LeftHanded(rig,P,set,grip,false),b=LeftHanded(rig,P,set,grip,true);
            Check(std::memcmp(a.w.data(),b.w.data(),sizeof(Mat)*Count)==0,"Reseat touches nothing, bit for bit, while nothing moved");
        }
    }
    {   // Reseat on its own: the item follows the wrist that holds it, and what hangs under it follows the item
        std::vector<int> parents={-1,0,0,1,3};   // a body, two wrists, the item under wrist 1, a bone under the item
        std::vector<Mat> w={bow::Identity(),Turned(0,0,90,{-.5f,1,0}),Turned(0,0,-90,{.5f,1,0}),Turned(10,20,30,{.45f,.9f,.1f}),Turned(0,0,0,{.45f,.8f,.1f})};
        const auto seat=handswap::SeatOn(3,2,w);
        Check(seat.bone==3&&seat.wrist==2&&!handswap::Reseat(seat,parents,w),"seated on wrist 2, nothing to do yet");
        const Mat item=w[3],under=w[4];
        TurnAboutSelf(w[1],Turned(30,0,0,{0,0,0}));
        w[3]=Mul(Mul(item,Inv(Turned(0,0,90,{-.5f,1,0}))),w[1]);w[4]=Mul(Mul(under,Inv(item)),w[3]);   // carried by its parent wrist
        Check(handswap::Reseat(seat,parents,w)&&Same(w[3],item)&&Same(w[4],under),"put back on the wrist holding it, the bone under it with it");
        TurnAboutSelf(w[2],Turned(0,25,0,{0,0,0}));
        Check(handswap::Reseat(seat,parents,w)&&Same(w[3],Mul(seat.local,w[2]))&&Same(Mul(w[4],Inv(w[3])),Mul(under,Inv(item))),"and it follows that wrist when it turns");
        Check(!handswap::Reseat(handswap::Seat{},parents,w)&&handswap::SeatOn(3,3,w).bone<0,"no seat, no change");
    }
    {   // two-handed kinds and the bow (phase 3): the right-handed solver run on the mirror world, reflected back
        for(int convention=0;convention<2;++convention) {
            const Rig rig=MakeRig(convention);
            const char* kinds[]={"bow","crossbow","staff","spear","polearm","two-handed sword"};
            for(int kind=0;kind<6;++kind) {
                Mat grip[2];GripOf(kKinds[kind],grip);
                const bool isBow=kind==0;const int item=isBow?LW:RW,holder=isBow?LH:RH,support=isBow?RH:LH;
                const float wrist[3]={10,-5,5};
                const float adjust[6]={.5f,1,-1,4,-6,9};
                // a right-handed solver: the item on its wrist, adjusted, the other hand onto it (a staff's
                // support hand along the shaft; a bow's string hand behind it)
                auto solve=[&](std::vector<Mat>& w) {
                    const auto old=w;
                    w[item]=Mul(grip[isBow?1:0],w[holder]);
                    float local[16];held::AdjustTurn(adjust,local);held::Adjust(w[item].m,adjust,local,1);
                    bow::Position(w[support],Pos(w[item])+bow::Normalized(Row(w[item],1))*(isBow?-.25f:.35f));
                    Propagate(old,w,support);
                };
                g_seed=4242u+unsigned(kind*7+convention);
                for(int i=0;i<kPoses/3;++i) {
                    const Scene P=RandomScene();
                    auto right=PoseArms(rig,P,wrist);solve(right);
                    auto real=PoseArms(rig,Mirrored(P),wrist);
                    std::vector<Mat> mirrored,entered;
                    handswap::EnterMirror(rig.mirror,kParents,real,mirrored);entered=mirrored;
                    solve(mirrored);
                    handswap::LeaveMirror(rig.mirror,kParents,entered,mirrored,real);
                    const int side=isBow?1:0;
                    const int axis=handswap::SymmetryAxis(Mul(grip[side],rig.bind[holder]),Palm(rig,side),-1);
                    Check(handswap::MatchHandedness(real[item],rig.bind[item],axis),"the item comes back mirrored and is turned over");
                    Check(Same(handswap::AsMirrored(real[item],axis),Mirror(right[item]),.03f,.02f),kinds[kind]);
                    const int supportTwin=support==RH?LH:RH,holderTwin=holder==RH?LH:RH;
                    Check(Same(real[supportTwin],handswap::Across(rig.mirror.carrier[supportTwin],right[support]),.03f,.02f),"the support hand is the mirror image of right-handed's");
                    Check(Same(real[holderTwin],handswap::Across(rig.mirror.carrier[holderTwin],right[holder]),.03f,.02f),"the holding hand too");
                }
            }
        }
    }
    std::printf("hand-swap-mirror-test: ok\n");
    return 0;
}
