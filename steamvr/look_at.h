#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

// Heads turn to you ([gameplay] look_at_you): monsters, mercenaries, summons and
// townsfolk near the player turn their heads to the player's real head, not to the
// hero's spot on the floor. Drawn only: where a body stands, what it attacks and every
// game state stay the game's.
//
// The skeleton hook (vr/skeletons.cpp) finds a model's head once from its bones' names
// and bind matrices (Learn), asks each frame how far the head should turn (Want), eases
// to that (Step) and turns the neck and head in the game's pose (Apply).
//
// Bone matrices are the model's own (row vectors, translation in the last row); the
// placement takes the model to the world (render units, y up). A model's bind pose faces
// its +z with +y up, as the hero's does (vr/skeletons.cpp): a bone's own "ahead" and "up"
// are those two directions seen from its bind matrix, so they follow the bone through
// any animation. Every angle is taken with atan2(x, z) about the world's up and turned
// back with the same rule, so the world's handedness does not matter.
namespace framevr::lookat {
using math::Vec;
inline constexpr int MaxBones=256;
inline constexpr float Pi=3.14159265358979f,Rad=Pi/180;

struct Limits {
    float yaw=70*Rad;          // the head's turn to either side of where the body faces
    float behindFrom=105*Rad;  // a player further round than this is let go of...
    float behindTo=135*Rad;    // ...and past this the head is the animation's
    float up=40*Rad,down=30*Rad;   // the head's tilt from level
    float turnMost=95*Rad,tiltMost=50*Rad;   // the most ever added to the animation's own head
    float range=7;             // metres from the head to the player's eyes: full within range-fade
    float fade=1.5f;
    float inside=.2f;          // metres: eyes this close to the head (inside it) turn nothing
    float uprightFrom=.35f,uprightTo=.7f;   // the body's up against the world's: a body lying down is left alone
    float neckShare=.35f;      // the share of the turn taken at the neck's root
    float settle=.16f;         // seconds: the ease's time constant
    float fastest=300*Rad;     // radians a second, the quickest a head turns
};

// ---- Names ----
inline std::string Lower(std::string_view s) {
    std::string r(s);
    for(auto& c:r)c=char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
// A head's own joint: head_bind_jnt, head, head_main_00_M_JNT, M_head_bind_jnt, C_NeckHead_head;
// not what hangs from it or is only named for it.
inline bool HeadName(std::string_view name) {
    const std::string n=Lower(name);
    if(n.find("head")==std::string::npos)return false;
    for(const char* word:{"nub","_end","attach","helper","dummy","effect","fx","eye","jaw","hair","helm","crown","band","dress",
                          "gear","piece","weapon","arrow","spear","axe","hammer","shield","tentacle","horn","ear","tongue","teeth","lip","brow"})
        if(n.find(word)!=std::string::npos)return false;
    return true;
}
inline bool NeckName(std::string_view name) {
    const std::string n=Lower(name);
    return n.find("neck")!=std::string::npos&&n.find("necklace")==std::string::npos&&n.find("head")==std::string::npos;
}

// ---- Row-vector helpers ----
inline Vec Direction(const float* m,Vec v) {
    return {v.x*m[0]+v.y*m[4]+v.z*m[8],v.x*m[1]+v.y*m[5]+v.z*m[9],v.x*m[2]+v.y*m[6]+v.z*m[10]};
}
inline bool Finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Wrap(float a) {
    if(!std::isfinite(a))return 0;
    a=std::fmod(a+Pi,2*Pi);if(a<0)a+=2*Pi;
    return a-Pi;
}
inline float Smooth(float from,float to,float x) {
    if(!(to>from))return x>=to?1.f:0.f;
    const float t=std::clamp((x-from)/(to-from),0.f,1.f);
    return t*t*(3-2*t);
}
// v turned about the unit axis by angle (right-hand rule, as plain vector algebra).
inline Vec About(Vec axis,float angle,Vec v) {
    const float c=std::cos(angle),s=std::sin(angle);
    return v*c+math::Cross(axis,v)*s+axis*(math::Dot(axis,v)*(1-c));
}
// A direction's yaw about the world's up (atan2(x, z)) and its tilt above level.
inline float YawOf(Vec v) {return std::atan2(v.x,v.z);}
inline float TiltOf(Vec v) {return std::atan2(v.y,std::sqrt(v.x*v.x+v.z*v.z));}
// A world turn: yaw about the world's up, then tilt upward about the level axis square to
// `ahead` (the head's level heading after the yaw). Rows are the turned axes (v' = v . R).
inline void WorldTurn(Vec ahead,float yaw,float tilt,float r[9]) {
    const Vec up{0,1,0};
    Vec level=math::Normalized(About(up,yaw,Vec{ahead.x,0,ahead.z}));
    if(math::Dot(level,level)<.5f)level=About(up,yaw,Vec{0,0,1});
    const Vec axis=math::Cross(level,up);   // turning about it lifts `level` (the triple product rule)
    const Vec basis[3]{{1,0,0},{0,1,0},{0,0,1}};
    for(int i=0;i<3;++i) {
        const Vec v=About(axis,tilt,About(up,yaw,basis[i]));
        r[i*3]=v.x;r[i*3+1]=v.y;r[i*3+2]=v.z;
    }
}
inline bool Invert3(const float* m,float out[9]) {
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],k=m[10];
    const float A=e*k-f*h,B=f*g-d*k,C=d*h-e*g,det=a*A+b*B+c*C;
    if(!std::isfinite(det)||std::fabs(det)<1e-12f)return false;
    const float s=1/det;
    out[0]=A*s;out[1]=(c*h-b*k)*s;out[2]=(b*f-c*e)*s;
    out[3]=B*s;out[4]=(a*k-c*g)*s;out[5]=(c*d-a*f)*s;
    out[6]=C*s;out[7]=(b*g-a*h)*s;out[8]=(a*e-b*d)*s;
    return true;
}

// ---- A model's head ----
struct Bone {
    std::string_view name;
    int parent=-1;                    // below its own index (Granny's order), -1 the root
    const float* inverseBind=nullptr; // 16 floats: the model's bind space to the bone's own
};
struct Rig {
    int head=-1;      // the head's joint
    int root=-1;      // the lowest neck joint under it in one run, or the head itself
    int body=-1;      // root's parent: what the head turns against (-1: the model itself)
    Vec headAhead{0,0,1},headUp{0,1,0};   // the head bone's own directions for the model's ahead and up
    Vec bodyAhead{0,0,1},bodyUp{0,1,0};   // the body bone's
    bool Usable()const{return head>=0&&root>=0;}
};
enum class Refusal:uint8_t {None,Bones,NoHead,Bind};
inline const char* Name(Refusal r) {
    return r==Refusal::Bones?"its bones do not read":r==Refusal::NoHead?"no bone named as a head":r==Refusal::Bind?"its head's bind matrix does not read":"";
}
// A bone's own directions for the model's ahead (+z) and up (+y): the rows of its inverse
// bind matrix carry the model's axes into the bone, made unit (a scaled rig).
inline bool BindAxes(const float* inverseBind,Vec& ahead,Vec& up) {
    if(!inverseBind)return false;
    for(int i=0;i<16;++i)if(!std::isfinite(inverseBind[i]))return false;
    ahead=math::Normalized(Direction(inverseBind,{0,0,1}));up=math::Normalized(Direction(inverseBind,{0,1,0}));
    return math::Dot(ahead,ahead)>.5f&&math::Dot(up,up)>.5f;
}
// The first bone named as a head whose parent is not one (a rig's second "head" is a leaf
// under the first); the neck joints straight under it.
inline Refusal Learn(const Bone* bones,int count,Rig& out) {
    out=Rig{};
    if(!bones||count<2||count>MaxBones)return Refusal::Bones;
    for(int i=0;i<count;++i)if(bones[i].parent>=i||bones[i].parent<-1)return Refusal::Bones;
    for(int i=0;i<count&&out.head<0;++i)if(HeadName(bones[i].name))out.head=i;
    if(out.head<0)return Refusal::NoHead;
    out.root=out.head;
    for(int b=bones[out.head].parent;b>=0&&NeckName(bones[b].name);b=bones[b].parent)out.root=b;
    out.body=bones[out.root].parent;
    if(!BindAxes(bones[out.head].inverseBind,out.headAhead,out.headUp)){out=Rig{};return Refusal::Bind;}
    // A body bone whose bind does not read: the model's own axes stand in.
    if(out.body>=0&&!BindAxes(bones[out.body].inverseBind,out.bodyAhead,out.bodyUp))out.body=-1;
    if(out.body<0){out.bodyAhead={0,0,1};out.bodyUp={0,1,0};}
    return Refusal::None;
}

// ---- Each frame ----
struct Aim {
    float yaw=0,tilt=0;    // radians to add to the animation's head, already scaled by weight
    float weight=0;        // 0..1: range, standing and not behind
    float metres=0;        // the head to the eyes
};
// How far the head should turn from where the game's pose has it, to look at eye (world
// render units). pose: count bone matrices. False when the pose or placement does not read.
inline bool Want(const Rig& rig,const float* pose,int count,const float* placement,Vec eye,float unitsPerMetre,const Limits& l,Aim& out) {
    out=Aim{};
    if(!rig.Usable()||!pose||!placement||rig.head>=count||rig.body>=count||!(unitsPerMetre>=1&&unitsPerMetre<=12)||!Finite(eye))return false;
    const float* head=pose+size_t(rig.head)*16;
    for(int i=0;i<16;++i)if(!std::isfinite(head[i])||!std::isfinite(placement[i]))return false;
    const Vec headAhead=math::Normalized(Direction(placement,Direction(head,rig.headAhead)));
    Vec bodyAhead,bodyUp;
    if(rig.body>=0) {
        const float* body=pose+size_t(rig.body)*16;
        for(int i=0;i<16;++i)if(!std::isfinite(body[i]))return false;
        bodyAhead=Direction(placement,Direction(body,rig.bodyAhead));bodyUp=Direction(placement,Direction(body,rig.bodyUp));
    } else {bodyAhead=Direction(placement,{0,0,1});bodyUp=Direction(placement,{0,1,0});}
    bodyAhead=math::Normalized(bodyAhead);bodyUp=math::Normalized(bodyUp);
    if(math::Dot(headAhead,headAhead)<.5f||math::Dot(bodyAhead,bodyAhead)<.5f||math::Dot(bodyUp,bodyUp)<.5f)return false;
    const Vec at{head[12]*placement[0]+head[13]*placement[4]+head[14]*placement[8]+placement[12],
                 head[12]*placement[1]+head[13]*placement[5]+head[14]*placement[9]+placement[13],
                 head[12]*placement[2]+head[13]*placement[6]+head[14]*placement[10]+placement[14]};
    const Vec to=eye-at;
    const float away=std::sqrt(math::Dot(to,to))/unitsPerMetre;
    if(!std::isfinite(away))return false;
    out.metres=away;
    // Standing: a body fallen over (dying, a corpse posed again) or one whose heading is straight up or down.
    const float level=std::sqrt(bodyAhead.x*bodyAhead.x+bodyAhead.z*bodyAhead.z);
    float weight=Smooth(l.uprightFrom,l.uprightTo,bodyUp.y)*Smooth(.3f,.6f,level);
    weight*=1-Smooth(l.range-l.fade,l.range,away);
    if(away<l.inside)weight=0;
    const float round=Wrap(YawOf(to)-YawOf(bodyAhead));
    weight*=1-Smooth(l.behindFrom,l.behindTo,std::fabs(round));
    // A head the animation has pointing straight up or down has no heading to turn from.
    const float headLevel=std::sqrt(headAhead.x*headAhead.x+headAhead.z*headAhead.z);
    weight*=Smooth(.2f,.5f,headLevel);
    if(!(weight>0)){out.weight=0;return true;}
    const float wantYaw=YawOf(bodyAhead)+std::clamp(round,-l.yaw,l.yaw);
    const float wantTilt=std::clamp(TiltOf(to),-l.down,l.up);
    out.yaw=std::clamp(Wrap(wantYaw-YawOf(headAhead)),-l.turnMost,l.turnMost)*weight;
    out.tilt=std::clamp(wantTilt-TiltOf(headAhead),-l.tiltMost,l.tiltMost)*weight;
    out.weight=weight;
    return true;
}

// What a head has been turned by, eased toward what is wanted.
struct Eased {float yaw=0,tilt=0;};
inline float Ease(float now,float want,float seconds,const Limits& l) {
    if(!std::isfinite(now))now=0;
    if(!std::isfinite(want))want=0;
    if(!(seconds>0))return now;
    seconds=std::min(seconds,.1f);
    const float step=(want-now)*(1-std::exp(-seconds/std::max(l.settle,1e-3f)));
    const float most=l.fastest*seconds;
    return now+std::clamp(step,-most,most);
}
inline void Step(Eased& e,const Aim& want,float seconds,const Limits& l) {
    e.yaw=std::clamp(Ease(e.yaw,want.yaw,seconds,l),-l.turnMost,l.turnMost);
    e.tilt=std::clamp(Ease(e.tilt,want.tilt,seconds,l),-l.tiltMost,l.tiltMost);
}
// Too little to see: the pose is left as the game's.
inline bool Still(const Eased& e) {return std::fabs(e.yaw)<.2f*Rad&&std::fabs(e.tilt)<.2f*Rad;}

// Turns the neck and head in pose by e: neckShare of it about the neck's root, carried by
// everything under the root, and the rest about the head's joint, carried by everything under
// the head (hair, jaw, helmet). parents[i] < i. False when refused: the pose is then not to be used.
inline bool Apply(const Rig& rig,const int16_t* parents,float* pose,int count,const float* placement,const Eased& e,const Limits& l) {
    if(!rig.Usable()||!parents||!pose||!placement||count<1||count>MaxBones||rig.head>=count||rig.root>rig.head||
       !std::isfinite(e.yaw)||!std::isfinite(e.tilt))return false;
    float inverse[9];
    if(!Invert3(placement,inverse))return false;
    const float x3[9]{placement[0],placement[1],placement[2],placement[4],placement[5],placement[6],placement[8],placement[9],placement[10]};
    // The head's level heading in the world, as the game posed it: what the tilt is square to.
    const Vec ahead=Direction(placement,Direction(pose+size_t(rig.head)*16,rig.headAhead));
    if(!Finite(ahead))return false;
    const float share=rig.root==rig.head?0.f:std::clamp(l.neckShare,0.f,1.f);
    bool under[MaxBones];
    auto turn=[&](int from,float yaw,float tilt,Vec heading) {
        float world[9],t[9],model[9];
        WorldTurn(heading,yaw,tilt,world);
        // In the model's space: X . R . X^-1.
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){float s=0;for(int k=0;k<3;++k)s+=x3[i*3+k]*world[k*3+j];t[i*3+j]=s;}
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){float s=0;for(int k=0;k<3;++k)s+=t[i*3+k]*inverse[k*3+j];model[i*3+j]=s;}
        for(int i=0;i<9;++i)if(!std::isfinite(model[i]))return false;
        const Vec pivot{pose[size_t(from)*16+12],pose[size_t(from)*16+13],pose[size_t(from)*16+14]};
        for(int b=from;b<count;++b) {
            const int p=parents[b];
            under[b]=b==from||(p>=from&&p<b&&under[p]);
            if(!under[b])continue;
            float* m=pose+size_t(b)*16;
            for(int row=0;row<4;++row) {
                Vec v{m[row*4],m[row*4+1],m[row*4+2]};
                if(row==3)v=v-pivot;
                Vec r{v.x*model[0]+v.y*model[3]+v.z*model[6],v.x*model[1]+v.y*model[4]+v.z*model[7],v.x*model[2]+v.y*model[5]+v.z*model[8]};
                if(row==3)r=r+pivot;
                m[row*4]=r.x;m[row*4+1]=r.y;m[row*4+2]=r.z;
            }
        }
        return true;
    };
    if(share>0&&!turn(rig.root,e.yaw*share,e.tilt*share,ahead))return false;
    // The neck's share has turned the heading too.
    const Vec after=About({0,1,0},e.yaw*share,ahead);
    return turn(rig.head,e.yaw*(1-share),e.tilt*(1-share),after);
}

// ---- Bodies the game has stopped posing ----
// A corpse is not posed again once its death has played out (nor is a body frozen solid, or any
// while the game is paused), and the game's pose is all this side has of a body: one whose pose has
// not changed for StillAfter is left alone, whatever way up it lies (owner 2026-11-11: "dead NPCs
// still try to track the player with their heads" - corpses fallen sitting, kneeling or in a heap
// kept their bodies upright enough for Want). A body the game animates is posed again many times a
// second, so three changes inside StillAfter count as moving: one change alone - the limbs put back
// after a push (bodies::PutBack) - does not wake a corpse.
inline constexpr unsigned long long StillAfter=600;   // milliseconds
struct Posing {
    unsigned long long at[3]={};   // the last three times the game's pose was seen to change, newest first
    void Changed(unsigned long long now){at[2]=at[1];at[1]=at[0];at[0]=now;}
    bool Moving(unsigned long long now) const {return at[2]!=0&&now>=at[2]&&now-at[2]<=StillAfter;}
};
}  // namespace framevr::lookat
