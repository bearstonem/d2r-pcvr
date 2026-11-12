#pragma once
#include "frame_math.h"
#include "haptic_impact.h"
#include "../shared/d2r_vr_state.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Held items collide (owner, 2026-11-11: "Let's add shield and weapon collisions, so they can collide each
// other"). The weapon stops at the shield on the other arm and slides along its face; two weapons, one in each
// hand, stop at each other. The real hands cannot be stopped, so the DRAWN pose is corrected: the hand that
// yields is drawn off its controller by the smallest push that separates the two shapes, and eased back to the
// controller once they part. A push past Cap lets them through, so an item is never held far from its hand.
//
// Pure logic: no native types, no threads. vr/skeletons.cpp steps it once per pose stamp with each item where
// its controller holds it now (the item as last drawn, carried along by its controller since) and poses the arms
// at the pushes it returns. Melee contact, the shield guard, the props and bottles and the bodies' limbs all read
// the drawn skeleton, so they meet the corrected pose: what you see is what hits.
//
// Frame: the head's turn-only frame of skel::Input::hand - metres from the eyes, x right, y up, z back.
namespace framevr::held {
using math::Vec;using math::Quat;

// m: the most an item is drawn off its hand; past it the two pass through. 12 cm until the owner, 2026-11-12, with the
// Paladin's mace on the shield: "they were still able to go pass through them if you push" (nearly every contact
// reached 12 cm and went through): 40 cm, a forearm, is through only when meant.
constexpr float Cap=.40f;
constexpr float EaseSeconds=.033f;     // the push eases back with this time constant: 95% back in 0.1 s
constexpr float Slop=.002f;            // m: a touch ends only this far apart, so a resting touch does not flutter
constexpr float BladeRadius=.03f;      // m: a blade's half width (melee's own capsule is padded for reach)
constexpr float MinHalfThickness=.01f,MaxHalfThickness=.04f;   // m: a shield's box, its boss and curve within
// m: the box taken for a shield whose rendered mesh was not measured, about its attach bone in the bone's XY plane
// (a 34 x 44 cm shield; D2's run from a buckler's ~30 cm to a tower shield's ~70 cm)
constexpr float GuessHalfRight=.17f,GuessHalfUp=.22f;
constexpr float ClankSpeed=.5f;        // m/s closing at first touch: slower is a resting touch and stays silent
constexpr float FullClankSpeed=3.f;    // m/s: the strongest clank
constexpr float SoundSpeed=1.f;        // m/s: the game's metal sound too (it has one loudness)
constexpr double ClankGap=.12;         // s: one clank at most this often
constexpr double MaxStep=.25;          // s between two steps: longer starts afresh (a menu, a hitch)
constexpr float SplitFloor=.15f;       // two weapons: each hand yields at least this share of the push
constexpr float StillSpeed=.1f;        // m/s added to both hands' speeds into the touch: two still hands split evenly
constexpr float ShareSeconds=.08f;     // the split follows the hands' speeds with this time constant
constexpr int MaxSubsteps=12;          // a fast swing is followed in steps of SubstepMetres, at most this many
constexpr float SubstepMetres=.02f;

// ---- what the hands hold, and why the two may not collide now ----
enum class Pair : uint8_t {None,WeaponShield,WeaponWeapon};
inline const char* PairName(Pair p) {
    return p==Pair::WeaponShield?"weapon-shield":p==Pair::WeaponWeapon?"weapon-weapon":"none";
}
enum Reason : uint32_t {
    Off=1u<<0,         // VR Settings > Combat > Weapons collide is off
    View=1u<<1,        // not first person with the arms on the controllers
    Menu=1u<<2,        // a menu, a panel or the inventory open: the hands are pointers
    Wheel=1u<<3,       // a hand's wheel or VR Settings open
    Leather=1u<<4,     // the leather menu held
    Form=1u<<5,        // a Druid form: paws, nothing held
    Bow=1u<<6,         // a bow or crossbow: the hands meet to draw and nock
    TwoHands=1u<<7,    // a two-handed grip, or the support hand on a shaft
    Throwing=1u<<8,    // a throw armed, a trap held to toss, or a potion in a hand
    EmptyHand=1u<<9,   // no weapon and shield, nor two weapons
    Untracked=1u<<10,  // a controller not tracked
    PutAway=1u<<11,    // an item not drawn (stowed for a potion, or hidden by the game)
    Unmeasured=1u<<12, // the shield's mesh not measured yet: its face is not known
    NotPosed=1u<<13,   // no recent pose of the items to carry
};
constexpr int ReasonCount=14;
inline const char* ReasonName(int bit) {
    static const char* const names[ReasonCount]={"setting off","view","menu","wheel","leather menu","druid form","bow",
        "two hands","throwing","empty hand","untracked","put away","shield unmeasured","not posed"};
    return bit>=0&&bit<ReasonCount?names[bit]:"?";
}
// The reasons, comma separated ("none" for none).
inline void Reasons(uint32_t why,char* out,size_t size) {
    if(!out||!size)return;
    out[0]=0;size_t used=0;
    for(int i=0;i<ReasonCount;++i)if(why&(1u<<i)) {
        const int n=std::snprintf(out+used,size-used,"%s%s",used?", ":"",ReasonName(i));
        if(n<0||size_t(n)>=size-used)break;
        used+=size_t(n);
    }
    if(!used)std::snprintf(out,size,"none");
}
// One-handed weapons: what a hand swings beside a shield or a second weapon.
inline bool OneHandWeapon(uint32_t type) {
    switch(type) {
        case D2RVR_TYPE_SWORD:case D2RVR_TYPE_AXE:case D2RVR_TYPE_MACE:case D2RVR_TYPE_SCEPTER:case D2RVR_TYPE_WAND:
        case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_THROWING:case D2RVR_TYPE_JAVELIN:case D2RVR_TYPE_CLAW:case D2RVR_TYPE_ORB:return true;
        default:return false;
    }
}
inline bool BowType(uint32_t type) {return type==D2RVR_TYPE_BOW||type==D2RVR_TYPE_CROSSBOW;}
inline bool ShaftType(uint32_t type) {return type==D2RVR_TYPE_STAFF||type==D2RVR_TYPE_SPEAR||type==D2RVR_TYPE_POLEARM;}
// The blade's length from the grip: the melee capsule's (vr/vrcam.cpp MeleeContactHand).
inline float BladeLength(uint32_t type) {
    switch(type) {
        case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_CLAW:case D2RVR_TYPE_ORB:return .28f;
        case D2RVR_TYPE_WAND:return .38f;
        default:return .65f;
    }
}
struct Holding {
    bool setting=true;      // VR Settings > Combat > Weapons collide ([gameplay] held_collisions)
    bool firstPerson=false; // first person, the arms on the controllers
    bool menu=false,wheel=false,leather=false,form=false,throwing=false;
    bool twoHands=false;    // the weapon in both hands, or the support hand on a shaft
    bool shield=false;      // a shield on the off arm
    bool leftHanded=false;  // left-handed play: the main weapon in the left hand, the off item on the right arm
    uint32_t main=0,off=0;  // kinds (D2RVR_TYPE_*): the main hand's weapon, the off hand's (0: none, or the shield)
};
struct Gate {
    Pair pair=Pair::None;   // what the hands hold
    uint32_t why=0;         // why it does not act now (Reason bits); 0: it acts
    int arm[2]={0,1};       // [0] the arm whose weapon yields (the main hand's of two weapons), [1] the other: 0 right, 1 left
    uint32_t type[2]{};     // [0] arm[0]'s weapon kind, [1] arm[1]'s (0 for a shield)
    bool Acts()const{return pair!=Pair::None&&!why;}
};
// The Assassin's claws are weapons like any other: two claws held crossed rest against each other a blade's
// width apart, well within the crossed-claws guard's 32 cm (shield_guard.h Crossed), so the block still works.
inline Gate Decide(const Holding& h) {
    Gate g;
    const int mainArm=h.leftHanded?1:0;g.arm[0]=mainArm;g.arm[1]=mainArm^1;
    if(h.shield&&OneHandWeapon(h.main)){g.pair=Pair::WeaponShield;g.type[0]=h.main;}
    else if(!h.shield&&OneHandWeapon(h.main)&&OneHandWeapon(h.off)){g.pair=Pair::WeaponWeapon;g.type[0]=h.main;g.type[1]=h.off;}
    if(!h.setting)g.why|=Off;
    if(!h.firstPerson)g.why|=View;
    if(h.menu)g.why|=Menu;
    if(h.wheel)g.why|=Wheel;
    if(h.leather)g.why|=Leather;
    if(h.form)g.why|=Form;
    if(BowType(h.main)||BowType(h.off))g.why|=Bow;
    if(h.twoHands||ShaftType(h.main)||ShaftType(h.off))g.why|=TwoHands;
    if(h.throwing)g.why|=Throwing;
    if(g.pair==Pair::None)g.why|=EmptyHand;
    return g;
}

// ---- shapes ----
struct Capsule {Vec a{},b{};float radius=0;};       // a weapon: its grip a, its tip b
struct Plate {Vec center{},right{},up{},out{};};    // a shield's box: its centre and half axes, out the thin one
inline bool Finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Length(Vec v) {return std::sqrt(math::Dot(v,v));}
inline bool Valid(const Capsule& c) {return Finite(c.a)&&Finite(c.b)&&std::isfinite(c.radius)&&c.radius>0&&c.radius<.2f&&Length(c.b-c.a)<3;}
inline bool Valid(const Plate& p) {
    if(!Finite(p.center)||!Finite(p.right)||!Finite(p.up)||!Finite(p.out))return false;
    const float r=Length(p.right),u=Length(p.up),o=Length(p.out);
    return r>.02f&&u>.02f&&o>.001f&&r<1.5f&&u<1.5f&&o<.5f;
}
// A weapon from its grip along its blade.
inline Capsule Blade(Vec grip,Vec along,uint32_t type) {
    return {grip,grip+math::Normalized(along)*BladeLength(type),BladeRadius};
}
// The box of a rendered mesh: world (row vectors, the game's) times its bounds (bone coordinates); out is its thin
// axis. False for an unreadable or degenerate box.
inline bool PlateFromBounds(const float* world,Vec low,Vec high,Plate& out) {
    for(int i=0;i<16;++i)if(!std::isfinite(world[i]))return false;
    if(!Finite(low)||!Finite(high))return false;
    const Vec size=high-low;
    if(!(size.x>0)||!(size.y>0)||!(size.z>0))return false;
    auto vector=[&](Vec p){return Vec{p.x*world[0]+p.y*world[4]+p.z*world[8],p.x*world[1]+p.y*world[5]+p.z*world[9],p.x*world[2]+p.y*world[6]+p.z*world[10]};};
    const Vec mid=(low+high)*.5f;
    const Vec axes[3]={vector({size.x*.5f,0,0}),vector({0,size.y*.5f,0}),vector({0,0,size.z*.5f})};
    int thin=0;for(int i=1;i<3;++i)if(math::Dot(axes[i],axes[i])<math::Dot(axes[thin],axes[thin]))thin=i;
    out.center=vector(mid)+Vec{world[12],world[13],world[14]};
    out.right=axes[(thin+1)%3];out.up=axes[(thin+2)%3];out.out=axes[thin];
    return math::Dot(out.out,out.out)>0;
}
// The box's half thickness kept between MinHalfThickness and MaxHalfThickness (metres).
inline Plate Thin(Plate p) {
    const float o=Length(p.out);
    if(o>0)p.out=p.out*(std::clamp(o,MinHalfThickness,MaxHalfThickness)/o);
    return p;
}
// A controller where it is now and how it is turned (the head frame), and a shape held in its frame.
struct Grip {Vec p{};Quat q{0,0,0,1};};
inline Vec PointIn(const Grip& g,Vec v) {return math::Rotate(math::Conjugate(g.q),v-g.p);}
inline Vec PointOut(const Grip& g,Vec v) {return g.p+math::Rotate(g.q,v);}
inline Vec AxisIn(const Grip& g,Vec v) {return math::Rotate(math::Conjugate(g.q),v);}
inline Vec AxisOut(const Grip& g,Vec v) {return math::Rotate(g.q,v);}
inline Capsule In(const Grip& g,const Capsule& c) {return {PointIn(g,c.a),PointIn(g,c.b),c.radius};}
inline Capsule Out(const Grip& g,const Capsule& c) {return {PointOut(g,c.a),PointOut(g,c.b),c.radius};}
inline Plate In(const Grip& g,const Plate& p) {return {PointIn(g,p.center),AxisIn(g,p.right),AxisIn(g,p.up),AxisIn(g,p.out)};}
inline Plate Out(const Grip& g,const Plate& p) {return {PointOut(g,p.center),AxisOut(g,p.right),AxisOut(g,p.up),AxisOut(g,p.out)};}

// ---- geometry ----
// The closest points of segments p0-p1 and q0-q1: s along the first, t along the second.
struct Closest {float s=0,t=0,distance=0;Vec p{},q{};};
inline Closest Segments(Vec p0,Vec p1,Vec q0,Vec q1) {
    const Vec d=p1-p0,e=q1-q0,r=p0-q0;
    const float dd=math::Dot(d,d),ee=math::Dot(e,e),de=math::Dot(d,e),dr=math::Dot(d,r),er=math::Dot(e,r);
    float s=0,t=0;
    if(dd<1e-8f&&ee<1e-8f){}
    else if(dd<1e-8f)t=std::clamp(er/ee,0.f,1.f);
    else if(ee<1e-8f)s=std::clamp(-dr/dd,0.f,1.f);
    else {
        const float den=dd*ee-de*de;
        if(den>1e-8f)s=std::clamp((de*er-dr*ee)/den,0.f,1.f);
        t=(de*s+er)/ee;
        if(t<0){t=0;s=std::clamp(-dr/dd,0.f,1.f);}
        else if(t>1){t=1;s=std::clamp((de-dr)/dd,0.f,1.f);}
    }
    Closest c;c.s=s;c.t=t;c.p=p0+d*s;c.q=q0+e*t;c.distance=Length(c.p-c.q);
    return c;
}
struct Span {float low=0,high=0;};
inline Span Project(const Capsule& c,Vec axis) {
    const float a=math::Dot(c.a,axis),b=math::Dot(c.b,axis);
    return {std::min(a,b)-c.radius,std::max(a,b)+c.radius};
}
inline Span Project(const Plate& p,Vec axis) {
    const float m=math::Dot(p.center,axis);
    const float r=std::fabs(math::Dot(p.right,axis))+std::fabs(math::Dot(p.up,axis))+std::fabs(math::Dot(p.out,axis));
    return {m-r,m+r};
}
// The box's own three axes, unit: 0 right, 1 up, 2 out (the face).
inline Vec Face(const Plate& p,int k) {return math::Normalized(k==0?p.right:k==1?p.up:p.out);}
// How far the capsule must move along face axis k, toward side (+1 or -1), to lie wholly on that side of the box.
inline float Depth(const Capsule& c,const Plate& p,int k,float side) {
    const Vec axis=Face(p,k);const Span a=Project(c,axis),b=Project(p,axis);
    return side>0?b.high-a.low:a.high-b.low;
}
// Touching: overlapping on every separating axis of a segment and a box (the box's faces and the blade's
// crossings with them), the blade's radius added. Near a box edge this errs by under a radius toward touching.
inline bool Touching(const Capsule& c,const Plate& p) {
    const Vec along=math::Normalized(c.b-c.a);
    for(int k=0;k<6;++k) {
        Vec axis=k<3?Face(p,k):math::Cross(along,Face(p,k-3));
        const float n=Length(axis);
        if(n<1e-3f)continue;
        axis=axis*(1/n);
        const Span a=Project(c,axis),b=Project(p,axis);
        if(a.high<=b.low||b.high<=a.low)return false;
    }
    return true;
}
// Lying across the box on the two face axes other than k: a blade beyond the box along k has passed through it.
inline bool Across(const Capsule& c,const Plate& p,int k) {
    for(int j=0;j<3;++j)if(j!=k) {
        const Vec axis=Face(p,j);const Span a=Project(c,axis),b=Project(p,axis);
        if(a.high<=b.low||b.high<=a.low)return false;
    }
    return true;
}
// The face and side the blade lay on, apart: the axis it was farthest out along. False when the box's
// faces do not part them (only an edge crossing did).
inline bool Apart(const Capsule& c,const Plate& p,int& k,float& side) {
    float best=0;bool found=false;
    for(int j=0;j<3;++j)for(float s:{1.f,-1.f}) {
        const float gap=-Depth(c,p,j,s);
        if(gap>best){best=gap;k=j;side=s;found=true;}
    }
    return found;
}
// The face and side with the least push out now.
inline void Least(const Capsule& c,const Plate& p,int& k,float& side) {
    float best=1e9f;
    for(int j=0;j<3;++j)for(float s:{1.f,-1.f}) {
        const float d=Depth(c,p,j,s);
        if(d<best){best=d;k=j;side=s;}
    }
}
// The point of the blade deepest toward the box along side*axis (an end, or its middle when level).
inline float Deepest(const Capsule& c,Vec axis,float side) {
    const float a=math::Dot(c.a,axis)*side,b=math::Dot(c.b,axis)*side;
    return std::fabs(a-b)<1e-4f?.5f:a<b?0.f:1.f;
}
inline Vec At(const Capsule& c,float s) {return c.a+(c.b-c.a)*s;}
// A point carried with the box from before to now (its place on the box kept).
inline Vec Carried(const Plate& before,const Plate& now,Vec point) {
    const Vec d=point-now.center;Vec out=before.center;
    const Vec n[3]={now.right,now.up,now.out},b[3]={before.right,before.up,before.out};
    for(int i=0;i<3;++i){const float l=math::Dot(n[i],n[i]);if(l>1e-10f)out=out+b[i]*(math::Dot(d,n[i])/l);}
    return out;
}
inline Capsule Lerp(const Capsule& a,const Capsule& b,float t) {return {a.a+(b.a-a.a)*t,a.b+(b.b-a.b)*t,a.radius+(b.radius-a.radius)*t};}
inline Plate Lerp(const Plate& a,const Plate& b,float t) {
    return {a.center+(b.center-a.center)*t,a.right+(b.right-a.right)*t,a.up+(b.up-a.up)*t,a.out+(b.out-a.out)*t};
}

// ---- the clank ----
// Strength 0..1 by the speed the two met at, below ClankSpeed none.
inline float ClankStrength(float speed) {
    if(!std::isfinite(speed)||speed<ClankSpeed)return 0;
    return std::clamp((speed-ClankSpeed)/(FullClankSpeed-ClankSpeed),0.f,1.f);
}
// Metal on metal: a sharp high tap and a short ring after it, in both hands, on the Melee hits strength.
inline haptics::Sequence Clank(float strength) {
    strength=std::isfinite(strength)?std::clamp(strength,0.f,1.f):0;
    const float a=.3f+.7f*strength;
    haptics::Sequence s;s.category=haptics::Category::Melee;s.count=2;
    s.steps[0]={0,{a,14,240}};s.steps[1]={30,{a*.45f,35,180}};
    return s;
}

// ---- the solver ----
struct Input {
    double time=0;          // seconds, rising (one step per pose stamp)
    Gate gate{};            // the reasons the skeleton adds (untracked, put away...) included
    Capsule a{},b{};        // gate.arm[0]'s weapon; gate.arm[1]'s weapon (two weapons)
    Plate plate{};          // gate.arm[1]'s shield (weapon and shield)
};
struct Output {
    Vec push[2]{};          // per arm (0 right, 1 left): the drawn hand moved off its controller, metres
    bool touching=false;    // pushing apart this step
    float depth=0;          // the raw overlap being undone, metres
    bool clank=false;       // a first touch at ClankSpeed or more, and none within ClankGap
    float strength=0,speed=0;   // its strength 0..1 and the closing speed, m/s
};
// For the log, since the last TakeStats.
struct Stats {
    unsigned contacts[2]{};   // first touches: weapon-shield, weapon-weapon
    unsigned capped=0,clanks=0,steps=0,acting=0;
    unsigned badShapes=0;     // steps a pair would have acted on but a blade or the box was not a usable shape
    float deepest=0;          // the deepest raw overlap pushed apart, metres
    float nearest=1e9f;       // the closest the two came while acting, metres (<= 0: touching), 1e9 = never acting
    uint32_t why=0;           // every reason that kept a held pair from acting
};
// How far apart a blade and a box are along the axis that parts them most (the box's faces and the blade's crossings
// with them, the blade's radius taken off): <= 0 when no axis parts them, i.e. touching. For the log.
inline float Gap(const Capsule& c,const Plate& p) {
    const Vec along=math::Normalized(c.b-c.a);
    float best=-1e9f;
    for(int k=0;k<6;++k) {
        Vec axis=k<3?Face(p,k):math::Cross(along,Face(p,k-3));
        const float n=Length(axis);
        if(n<1e-3f)continue;
        axis=axis*(1/n);
        const Span a=Project(c,axis),b=Project(p,axis);
        best=std::max(best,std::max(b.low-a.high,a.low-b.high));
    }
    return best;
}
class Solver {
public:
    enum class Phase {Idle,Touching,Passing};
    Output Step(const Input& in) {
        Output out;
        const double dt=primed?in.time-last:0;
        if(primed&&!(dt>0)){for(int i=0;i<2;++i)out.push[i]=push[i];out.touching=phase==Phase::Touching;return out;}
        const bool fresh=!primed||dt>MaxStep;
        last=in.time;primed=true;++stats.steps;
        const float decay=fresh?0.f:float(std::exp(-dt/EaseSeconds));
        for(auto& p:push){p=p*decay;if(Length(p)<.001f)p={};}
        const Gate& g=in.gate;
        if(g.pair!=Pair::None)stats.why|=g.why&~uint32_t(Off|EmptyHand);
        const bool shapes=g.pair==Pair::WeaponShield?Valid(in.a)&&Valid(in.plate):g.pair==Pair::WeaponWeapon&&Valid(in.a)&&Valid(in.b);
        const bool same=pair==g.pair&&arm[0]==g.arm[0]&&arm[1]==g.arm[1];
        // Not acting, a new pair, a long gap, or the first pose after a pause: nothing collides; the pose is remembered
        // so the next one has a way to follow.
        if(g.Acts()&&!shapes)++stats.badShapes;
        if(g.Acts()&&shapes) {   // how close they came (the log): blade to box, or blade to blade
            float gap=1e9f;
            if(g.pair==Pair::WeaponShield)gap=Gap(in.a,in.plate);
            else {const Closest c=Segments(in.a.a,in.a.b,in.b.a,in.b.b);gap=c.distance-in.a.radius-in.b.radius;}
            if(std::isfinite(gap))stats.nearest=std::min(stats.nearest,gap);
        }
        if(!g.Acts()||!shapes||fresh||!same||!havePrevious) {
            phase=Phase::Idle;havePrevious=false;pair=g.pair;arm[0]=g.arm[0];arm[1]=g.arm[1];
            if(g.Acts()&&shapes)Remember(in);
            for(int i=0;i<2;++i)out.push[i]=push[i];
            return out;
        }
        ++stats.acting;
        const int A=g.arm[0],B=g.arm[1];
        float depth=0;Vec normal{};Vec pointA{},pointB{};float s=0,t=0;
        bool touching=false,onset=false;
        // Touching goes on while the two overlap, or while the blade has gone through and lies beyond, still across
        // the other: it is pushed back the way it came. Passing (let through past Cap) ends once they are clear.
        if(g.pair==Pair::WeaponShield) {
            if(phase==Phase::Idle) {
                // The first step along the way from the last pose that touches: the face the blade lay off just
                // before it is the face it stays out of, however deep a fast swing has gone since.
                const int steps=Substeps(in);
                for(int i=1;i<=steps&&!onset;++i) {
                    const float f=float(i)/steps;
                    const Capsule c=Lerp(previousA,in.a,f);const Plate p=Lerp(previousPlate,in.plate,f);
                    if(!Touching(c,p))continue;
                    const float f0=float(i-1)/steps;
                    if(!havePrevious||!Apart(Lerp(previousA,in.a,f0),Lerp(previousPlate,in.plate,f0),face,side))Least(c,p,face,side);
                    onset=true;
                }
            }
            if(phase!=Phase::Idle||onset) {
                const Vec axis=Face(in.plate,face);
                depth=Depth(in.a,in.plate,face,side);
                const bool overlap=Touching(in.a,in.plate);
                touching=phase==Phase::Passing?overlap:depth>-Slop&&(overlap||Across(in.a,in.plate,face));
                normal=axis*side;
                s=Deepest(in.a,axis,side);pointA=At(in.a,s);pointB=pointA;
            }
        } else {
            if(phase==Phase::Idle) {
                const int steps=Substeps(in);
                for(int i=1;i<=steps&&!onset;++i) {
                    const float f=float(i)/steps;
                    const Capsule a=Lerp(previousA,in.a,f),b=Lerp(previousB,in.b,f);
                    const Closest c=Segments(a.a,a.b,b.a,b.b);
                    if(c.distance>=a.radius+b.radius)continue;
                    const Capsule a0=havePrevious?Lerp(previousA,in.a,float(i-1)/steps):a,b0=havePrevious?Lerp(previousB,in.b,float(i-1)/steps):b;
                    const Closest c0=Segments(a0.a,a0.b,b0.a,b0.b);
                    normal=c0.distance>1e-5f?(c0.p-c0.q)*(1/c0.distance):Crossing(a,b);
                    onset=true;
                }
            }
            if(phase!=Phase::Idle||onset) {
                if(!onset)normal=stuck;
                const Closest c=Segments(in.a.a,in.a.b,in.b.a,in.b.b);
                const float r=in.a.radius+in.b.radius;
                s=c.s;t=c.t;pointA=c.p;pointB=c.q;
                if(c.distance>1e-5f) {
                    const Vec u=(c.p-c.q)*(1/c.distance);
                    const bool crossed=c.s>0&&c.s<1&&c.t>0&&c.t<1;   // both closest points inside: blade across blade
                    if(math::Dot(u,normal)>=0||!crossed){depth=r-c.distance;normal=u;}
                    else {depth=r+c.distance;normal=u*-1;}      // gone through the other blade: back the way it came
                } else depth=r;
                touching=phase==Phase::Passing?c.distance<r:depth>-Slop;
            }
        }
        if(onset) {
            if(!touching)onset=false;
            else {
                phase=Phase::Touching;++stats.contacts[g.pair==Pair::WeaponShield?0:1];
                // How fast they met, along the push: each one's point carried from the last pose to this one (none
                // known after a pause: already touching as it ended, silent).
                const Vec va=havePrevious?(pointA-At(previousA,s))*float(1/dt):Vec{};
                const Vec vb=!havePrevious?Vec{}:g.pair==Pair::WeaponShield?(pointB-Carried(previousPlate,in.plate,pointB))*float(1/dt):
                    (pointB-At(previousB,t))*float(1/dt);
                const float closing=math::Dot(vb-va,normal);
                share=ShareOf(va,vb,normal,g.pair);
                out.speed=std::max(0.f,closing);
                if(closing>=ClankSpeed&&in.time-lastClank>=ClankGap) {
                    out.clank=true;out.strength=ClankStrength(closing);lastClank=in.time;++stats.clanks;
                }
            }
        }
        if(phase!=Phase::Idle&&!touching)phase=Phase::Idle;
        if(phase==Phase::Touching&&!onset&&g.pair==Pair::WeaponWeapon) {
            const Vec va=(pointA-At(previousA,s))*float(1/dt),vb=(pointB-At(previousB,t))*float(1/dt);
            share+=(ShareOf(va,vb,normal,g.pair)-share)*std::min(1.f,float(dt)/ShareSeconds);
        }
        if(phase==Phase::Touching) {
            const float shareA=g.pair==Pair::WeaponShield?1.f:share,most=std::max(shareA,1-shareA);
            if(depth*most>Cap){phase=Phase::Passing;++stats.capped;}   // too deep to hold apart: let them through
            else {
                const float need=depth-math::Dot(push[A]-push[B],normal);
                if(need>0){push[A]=push[A]+normal*(need*shareA);push[B]=push[B]-normal*(need*(1-shareA));}
                out.touching=true;out.depth=std::max(depth,0.f);stats.deepest=std::max(stats.deepest,out.depth);
            }
        }
        stuck=normal;
        for(auto& p:push){const float l=Length(p);if(l>Cap)p=p*(Cap/l);}
        Remember(in);
        for(int i=0;i<2;++i)out.push[i]=push[i];
        return out;
    }
    Stats TakeStats(){const Stats s=stats;stats={};return s;}
    Phase State()const{return phase;}
    void Reset(){const Stats s=stats;*this=Solver{};stats=s;}
private:
    Phase phase=Phase::Idle;Pair pair=Pair::None;int arm[2]={0,1};
    int face=2;float side=1;Vec stuck{};   // the face and side the blade is kept to (shield); the push's way (two weapons)
    float share=.5f;                        // two weapons: arm[0]'s share of the push
    Vec push[2]{};
    bool primed=false,havePrevious=false;double last=0,lastClank=-1e9;
    Capsule previousA{},previousB{};Plate previousPlate{};
    Stats stats{};
    void Remember(const Input& in){previousA=in.a;previousB=in.b;previousPlate=in.plate;havePrevious=true;}
    int Substeps(const Input& in)const {
        if(!havePrevious)return 1;
        float travel=std::max(Length(in.a.a-previousA.a),Length(in.a.b-previousA.b));
        travel+=pair==Pair::WeaponShield?Length(in.plate.center-previousPlate.center)+Length(in.plate.right-previousPlate.right)+Length(in.plate.up-previousPlate.up):
            std::max(Length(in.b.a-previousB.a),Length(in.b.b-previousB.b));
        return std::clamp(int(std::ceil(travel/SubstepMetres)),1,MaxSubsteps);
    }
    // Two blades already through each other at first sight: apart across both, the first's way from the second.
    static Vec Crossing(const Capsule& a,const Capsule& b) {
        Vec n=math::Normalized(math::Cross(a.b-a.a,b.b-b.a));
        if(math::Dot(n,n)<.5f)n=math::Normalized(((a.a+a.b)-(b.a+b.b))*.5f);
        if(math::Dot(n,n)<.5f)n={0,1,0};
        if(math::Dot(n,(a.a+a.b)-(b.a+b.b))<0)n=n*-1;
        return n;
    }
    // Weapon against weapon: arm[0]'s share of the push, more to the hand that moved faster into the touch.
    static float ShareOf(Vec va,Vec vb,Vec normal,Pair pair) {
        if(pair!=Pair::WeaponWeapon)return 1;
        const float inA=std::max(0.f,-math::Dot(va,normal)),inB=std::max(0.f,math::Dot(vb,normal));
        const float share=(inA+StillSpeed)/(inA+inB+2*StillSpeed);
        return std::isfinite(share)?std::clamp(share,SplitFloor,1-SplitFloor):.5f;
    }
};
}
