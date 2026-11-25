#pragma once
#include "../vr/bow_pose.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

// A flail's chain swings (a player with two flails, 2026-11-19: "it would be amazing if they had physics instead of
// the stiff chain that defies physics"; the owner, 2026-11-24: "Flail Weapons are not flailing around, they need to be
// flailing when I swing ... physically moving the chain and all that stuff around as I swing the weapon, realistically").
//
// The game's flail models are rigged: a chain of bones from the top of the handle to the ball, which the game moves
// with each class's own animation of it (data/hd/character/player/<class>/animation/weapon/*_flail.animation). With the
// arm on a controller that pose stayed fixed in the hand. Here the chain hangs from the handle and swings: the chain's
// joints and the ball are points stepped in the world, then laid back on the item's bones where the game drew them
// (vr/skeletons.cpp PoseFlailChain), as the bow's string is. Looks only: the melee contact still reaches the head
// straight along the handle (weapon_reach.h).
//
// The first version (2026-11-19) never moved a chain in the game: it read the item's place in the world through
// skel::bow::Inverse, whose matrix check refuses any number over 10000, and the world's own coordinates are larger
// than that nearly everywhere (the owner's 2026-11-24 session stood at 10305, 10403: every pose of both flails was left
// to the game, "unusable"). The world is read here as it is, and the points are kept about the item's own place, not
// the world's origin, so they keep their precision as far out as the world goes.
//
// The swing: each chain is its joints and the ball's centre, the first held at the handle's top, under gravity, the
// ball much heavier than a link. Every substep the points coast and fall, then the links are pulled exactly back to
// length by one solve of the whole chain at once (a tridiagonal system: the ball's weight swings the chain, the chain
// whips the ball), no joint bends further than a chain's link can, and the chain stays out of the handle, the player's
// head, the floor and (Khalim's three) the other balls. Each bone is then turned to its link: the first carried over
// from its last pose (no pop when the chain swings past where the game's pose had it), twisting about the chain no
// further than TwistLimit from hanging straight, and each next one from the one before.
//
// Pure logic: no native types, no threads. Matrices are the game's (row vectors, translation in m[12..14]).
namespace framevr::flail {
using math::Vec;
using skel::bow::Mat;

constexpr size_t MaxBones=64;
constexpr int MaxChains=3,MaxLinks=24,MinLinks=4,MaxPoints=MaxLinks+1;

// ---- the rig ----
inline bool Digits(std::string_view s) {
    return !s.empty()&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});
}
// A chain link's bone, as the game's flail models name them: chain_bnd_jnt_01..18 (Flail, Knout and Scourge:
// flail.skeleton, 23 bones), and L_/C_/R_chain_NN with L_/C_/R_bindJoint_NN between (Khalim's Flail: three chains of
// 14 in 51 bones; Khalim's Will: three of 12 in 45). Not mainChain or a chain's _root: those hold the chains to the
// handle and stay where the game puts them.
inline bool Link(std::string_view name) {
    if(name.starts_with("chain_bnd_jnt_"))return Digits(name.substr(14));
    if(name.size()>2&&(name[0]=='L'||name[0]=='C'||name[0]=='R')&&name[1]=='_') {
        const auto rest=name.substr(2);
        if(rest.starts_with("chain_"))return Digits(rest.substr(6));
        if(rest.starts_with("bindJoint_"))return Digits(rest.substr(10));
    }
    return false;
}
// Why a skeleton is no flail's rig (Found: it is).
enum class RigWhy:uint8_t {Found,NoLinks,Unreadable,Branched,Short,Long,Many};
inline const char* Text(RigWhy why) {
    switch(why) {
    case RigWhy::Found:return "a flail's chain";
    case RigWhy::NoLinks:return "no bone named as a chain's link";
    case RigWhy::Unreadable:return "its bones or their parents do not read";
    case RigWhy::Branched:return "a link holds something besides the next link";
    case RigWhy::Short:return "a chain of fewer than 4 links";
    case RigWhy::Long:return "a chain of more than 24 links";
    case RigWhy::Many:return "more than 3 chains";
    }
    return "?";
}
struct Chain {int count=0;std::array<int,MaxLinks> bone{};};   // its links' bones, from the handle out
struct Rig {
    int chains=0,links=0;
    RigWhy why=RigWhy::NoLinks;
    std::array<Chain,MaxChains> chain{};
    bool Valid()const{return chains>0;}
};
// The chains of a skeleton: each from a link whose parent is no link, through its one child to the last link. A link
// with any other child, a chain shorter than MinLinks or longer than MaxLinks, or more chains than a flail has: no rig
// (why says which), and the game's own animation stays.
inline Rig Find(std::span<const std::string_view> names,std::span<const int> parents) {
    auto no=[](RigWhy why){Rig r;r.why=why;return r;};
    if(names.size()!=parents.size()||names.empty()||names.size()>MaxBones)return no(RigWhy::Unreadable);
    const int n=int(names.size());
    std::array<bool,MaxBones> link{};bool any=false;
    for(int i=0;i<n;++i) {
        if(parents[i]<-1||parents[i]>=n||parents[i]==i)return no(RigWhy::Unreadable);
        any|=link[i]=Link(names[i]);
    }
    if(!any)return no(RigWhy::NoLinks);
    Rig rig;
    for(int i=0;i<n;++i)if(link[i]&&(parents[i]<0||!link[parents[i]])) {
        if(rig.chains==MaxChains)return no(RigWhy::Many);
        Chain c;int at=i;
        for(;;) {
            if(c.count==MaxLinks)return no(RigWhy::Long);
            c.bone[c.count++]=at;
            int next=-1,children=0;
            for(int j=0;j<n;++j)if(parents[j]==at){++children;if(link[j])next=j;}
            if(!children)break;
            if(children!=1||next<0)return no(RigWhy::Branched);   // a link holds the next link and nothing else
            at=next;
        }
        if(c.count<MinLinks)return no(RigWhy::Short);
        rig.chain[rig.chains++]=c;rig.links+=c.count;
    }
    rig.why=RigWhy::Found;
    return rig;
}

// ---- the swing: its tuning ----
constexpr float Gravity=9.81f;          // m/s^2, down the world's -Y
// A steel ball of about a kilogram on links of about 40 g. The ball leads a swing and pulls the chain straight behind
// it; the links, light beside it, ripple and fold when it comes back toward the hand.
constexpr float LinkMass=1,BallMass=25;
// The ball, a share of the chain's length (its first joint to its last). The game's models: the ball's centre
// 0.30-0.31 model units past the last joint and its radius 0.29-0.32, on chains of 1.45 (Khalim's Will), 1.52
// (Flail) and 1.70 (Khalim's Flail): 0.18-0.21 of the chain both.
constexpr float BallReach=.2f,BallRadius=.2f;
constexpr float LinkRadius=.012f;       // m: a link's half thickness, kept off the floor, the handle and the head
constexpr float HandleRadius=.05f;      // the handle's, a share of the chain's length (the Flail's shaft: .058)
constexpr float HeadRadius=.13f;        // m about the player's eyes: the chain swings around his head, not through it
constexpr float HeadClear=.03f;         // m: the head kept this far from the chain's top (the hand at the face)
constexpr float Tidy=.01f;              // a link still off its length by this share after the solves is set to it
constexpr float SwingDrag=.7f;          // 1/s: rub at the handle's ring and in the links - a swing halves in two seconds
constexpr float ChainDrag=25;           // 1/s: the links' rub on each other - a ripple along the chain is gone in 0.1 s
constexpr float FloorDrag=12;           // 1/s: what rests on the floor slides to a stop
constexpr float MaxBend=1.0472f;        // rad: one link turns on the next by up to 60 deg
constexpr float BallBend=1.5708f;       // rad: the ball on the last link, up to 90 deg
constexpr float TwistLimit=1.0472f;     // rad: the chain twists about itself up to 60 deg from hanging straight
constexpr float Unwind=4;               // rad/s: twisted further (an overhead swing), it turns back this fast
constexpr float SubstepSeconds=1.f/480; // a step is cut into substeps this long (5 at 90 Hz), at most MaxSubsteps
// A substep: Passes of the links, the bends and what the chain meets by turns, then Solves of the links and the floor.
// Settles: those turns, hanging afresh. (19 us a Flail's pose at 90 Hz, 45 us Khalim's three chains.)
constexpr int MaxSubsteps=48,Passes=2,Solves=2,Settles=24;
constexpr double MinStep=.002;          // s: a pose sooner than this after the last step reuses it (several poses a frame)
constexpr double MaxStep=.1;            // s: longer (a pause, a menu, a hitch): the chain is carried on, not stepped
constexpr float Jump=1;                 // m the chain's top moved in one step: a teleport, a load - carried on
constexpr float Resized=.1f;            // the chain's length changed by this share: another item or scale - hung afresh
constexpr float MaxSpeed=30;            // m/s: no point faster (a bad frame is not thrown about)
constexpr float MinChain=.05f,MaxChain=3;   // m: a chain shorter (an item the game hid by shrinking) or longer is left
constexpr float MaxUnits=1e7f;          // the world's units: an item placed farther out than this is not read

// Why a pose was left to the game (None: laid).
enum class Why:uint8_t {None,Bones,Item,World,Shape,Size};
inline const char* Text(Why why) {
    switch(why) {
    case Why::None:return "none";
    case Why::Bones:return "its chain's bones do not read as a chain (too few, too many or not affine)";
    case Why::Item:return "the item's place in the world does not read (not finite, too far out, or flat)";
    case Why::World:return "the world's size or the time is not usable";
    case Why::Shape:return "the game's pose of the chain has no length or direction";
    case Why::Size:return "the chain is under 5 cm or over 3 m in the world (an item hidden by shrinking)";
    }
    return "?";
}
struct Stats {
    unsigned poses=0,chains=0,steps=0;  // flail poses laid, chains laid, steps of the swing
    unsigned starts=0,resized=0,lost=0; // hung afresh: the first, a new size or shape, a point lost (not finite)
    unsigned pauses=0,jumps=0,turns=0;  // carried on the item: a pause, a jump, a snap turn
    unsigned refused=0;Why why=Why::None;   // left to the game, and the last reason
    float fastest=0;                    // m/s: the ball's fastest about the handle
    float widest=0;                     // deg: its widest swing away from hanging straight down
    float stretch=0;                    // the links' worst length error after a step, a share of the link
    void Add(const Stats& s) {
        poses+=s.poses;chains+=s.chains;steps+=s.steps;starts+=s.starts;resized+=s.resized;lost+=s.lost;
        pauses+=s.pauses;jumps+=s.jumps;turns+=s.turns;
        if(s.refused){refused+=s.refused;why=s.why;}
        fastest=std::max(fastest,s.fastest);widest=std::max(widest,s.widest);stretch=std::max(stretch,s.stretch);
    }
};
// The world a pose is laid in.
struct World {
    float unitsPerMetre=0;              // the world's units a metre
    float floor=NAN;                    // the ground's height, units (NaN: none)
    Vec head{NAN,NAN,NAN};              // the player's eyes, units (NaN: unknown)
    uint32_t turn=0;                    // the world's snap-turn count: a turn carries the chain along as it hangs
};
// A flail's chains this pose: the game's bones of each (the item's space, from the handle out) and where to lay them.
struct Bones {
    int chains=0;
    std::array<std::span<const Mat>,MaxChains> native{};
    std::array<std::span<Mat>,MaxChains> out{};
};

// ---- small geometry ----
inline float Length(Vec v) {return std::sqrt(math::Dot(v,v));}
inline bool Finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline Vec Row(const Mat& m,int r) {return {m.m[r*4],m.m[r*4+1],m.m[r*4+2]};}
inline void SetRow(Mat& m,int r,Vec v) {m.m[r*4]=v.x;m.m[r*4+1]=v.y;m.m[r*4+2]=v.z;}
inline Vec Lerp(Vec a,Vec b,float t) {return a+(b-a)*t;}
// The inverse of a's turn and scale (its translation left out): false when it does not read or is flat.
inline bool InvertLinear(const Mat& a,Mat& r) {
    const float* m=a.m;
    const float c0=m[5]*m[10]-m[6]*m[9],c1=m[6]*m[8]-m[4]*m[10],c2=m[4]*m[9]-m[5]*m[8];
    const float det=m[0]*c0+m[1]*c1+m[2]*c2;
    const float size=Length(Row(a,0))*Length(Row(a,1))*Length(Row(a,2));
    if(!std::isfinite(det)||!(size>1e-24f)||!(std::fabs(det)>1e-4f*size))return false;
    r=skel::bow::Identity();
    r.m[0]=c0/det;r.m[1]=(m[2]*m[9]-m[1]*m[10])/det;r.m[2]=(m[1]*m[6]-m[2]*m[5])/det;
    r.m[4]=c1/det;r.m[5]=(m[0]*m[10]-m[2]*m[8])/det;r.m[6]=(m[2]*m[4]-m[0]*m[6])/det;
    r.m[8]=c2/det;r.m[9]=(m[1]*m[8]-m[0]*m[9])/det;r.m[10]=(m[0]*m[5]-m[1]*m[4])/det;
    return true;
}
// An item's place in the world as the game keeps it: finite, affine, within MaxUnits, not flat. No bound on how far
// from the world's origin within that (skel::bow::Affine's 10000 is a bone's bound, not the world's).
inline bool Placement(const Mat& item,Mat& inverse) {
    for(float x:item.m)if(!std::isfinite(x))return false;
    if(std::fabs(item.m[3])+std::fabs(item.m[7])+std::fabs(item.m[11])>.001f||std::fabs(item.m[15]-1)>.001f)return false;
    for(int i:{0,1,2,4,5,6,8,9,10})if(std::fabs(item.m[i])>1000)return false;
    for(int i:{12,13,14})if(std::fabs(item.m[i])>MaxUnits)return false;
    return InvertLinear(item,inverse);
}
// A turn by angle about a unit axis, for row vectors (as skel::bow::Rotation builds them).
inline Mat Spin(Vec k,float angle) {
    const float s=std::sin(angle),c=std::cos(angle),t=1-c;
    const float v[]={k.x,k.y,k.z};
    Mat r=skel::bow::Identity();
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[4*i+j]=t*v[i]*v[j]+(i==j?c:0.f);
    r.m[1]+=k.z*s;r.m[2]-=k.y*s;r.m[4]-=k.z*s;r.m[6]+=k.x*s;r.m[8]+=k.y*s;r.m[9]-=k.x*s;
    return r;
}
// a's axes made unit and square to each other, its handedness kept; no translation.
inline Mat Orthonormal(const Mat& a) {
    const Vec x=math::Normalized(Row(a,0));Vec y=Row(a,1);y=math::Normalized(y-x*math::Dot(x,y));
    if(Length(x)<.5f||Length(y)<.5f)return skel::bow::Identity();
    Vec z=math::Cross(x,y);
    if(math::Dot(z,Row(a,2))<0)z=z*-1;
    Mat r=skel::bow::Identity();SetRow(r,0,x);SetRow(r,1,y);SetRow(r,2,z);
    return r;
}
inline Mat Transposed(const Mat& a) {
    Mat r=skel::bow::Identity();
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[4*i+j]=a.m[4*j+i];
    return r;
}
inline Mat Times(const Mat& a,const Mat& b) {return skel::bow::Multiply(a,b);}
// A unit vector square to a.
inline Vec Across(Vec a) {return math::Normalized(math::Cross(a,std::fabs(a.x)<.9f?Vec{1,0,0}:Vec{0,1,0}));}
// p kept at least r from the segment a-b (pushed straight out; on the segment itself, out to the side).
inline void OutOfSegment(Vec& p,Vec a,Vec b,float r) {
    const Vec ab=b-a;const float l2=math::Dot(ab,ab);
    const float t=l2>1e-12f?std::clamp(math::Dot(p-a,ab)/l2,0.f,1.f):0.f;
    const Vec q=a+ab*t,d=p-q;const float l=Length(d);
    if(l>=r)return;
    p=q+(l>1e-7f?d*(1/l):Across(l2>1e-12f?ab:Vec{0,1,0}))*r;
}
inline void OutOfSphere(Vec& p,Vec c,float r) {OutOfSegment(p,c,c,r);}

// ---- the swing ----
// A flail: its chains (up to three, Khalim's), each its joints and the ball's centre, in world metres about the
// item's place (the points' origin follows the item every pose, so far from the world's origin they stay exact).
class Swing {
public:
    // bones: the game's pose of each chain this call, and where to lay it; item: the item's space to the world's units
    // (as its transform's world matrix has it); world: the world's size, floor, the player's eyes and snap turns; time:
    // seconds, rising. False: no out is touched, the game's own pose stays, and stats.why says why.
    bool Pose(const Bones& bones,const Mat& item,const World& world,double time,Stats& stats) {
        auto refuse=[&](Why why){++stats.refused;stats.why=why;seeded=false;return false;};
        if(bones.chains<1||bones.chains>MaxChains)return refuse(Why::Bones);
        for(int c=0;c<bones.chains;++c) {
            const auto& native=bones.native[size_t(c)];
            if(native.size()<2||native.size()>size_t(MaxLinks)||bones.out[size_t(c)].size()!=native.size())return refuse(Why::Bones);
            for(const Mat& m:native)if(!skel::bow::Affine(m))return refuse(Why::Bones);
        }
        if(!std::isfinite(time)||!std::isfinite(world.unitsPerMetre)||world.unitsPerMetre<.1f||world.unitsPerMetre>100)
            return refuse(Why::World);
        Mat inverse;
        if(!Placement(item,inverse))return refuse(Why::Item);
        const float metre=1/world.unitsPerMetre;
        const Vec place=skel::bow::Position(item);
        // The game's pose of each chain: its joints on the item, the ball's centre on along the last bone the way the
        // links run along theirs, and all of them in the world.
        std::array<Native,MaxChains> now{};
        for(int c=0;c<bones.chains;++c) {
            Native& g=now[size_t(c)];const auto& native=bones.native[size_t(c)];
            g.count=int(native.size());const int n=g.count;
            for(int i=0;i<n;++i)g.joint[size_t(i)]=skel::bow::Position(native[size_t(i)]);
            float itemLength=0;
            for(int i=0;i+1<n;++i)itemLength+=Length(g.joint[size_t(i+1)]-g.joint[size_t(i)]);
            Mat before;
            if(!(itemLength>1e-6f)||!skel::bow::Inverse(native[size_t(n-2)],before))return refuse(Why::Shape);
            const Vec headAxis=math::Normalized(skel::bow::Vector(native[size_t(n-1)],
                skel::bow::Vector(before,g.joint[size_t(n-1)]-g.joint[size_t(n-2)])));
            if(Length(headAxis)<.5f)return refuse(Why::Shape);
            g.joint[size_t(n)]=g.joint[size_t(n-1)]+headAxis*(BallReach*itemLength);
            for(int i=0;i<=n;++i)g.at[size_t(i)]=skel::bow::Vector(item,g.joint[size_t(i)])*metre;
            for(int i=0;i+1<n;++i)g.chain+=Length(g.at[size_t(i+1)]-g.at[size_t(i)]);
            if(!(g.chain>=MinChain&&g.chain<=MaxChain))return refuse(Why::Size);
            g.handed=Handed(native[0]);
        }
        // The grip (the item's origin) and the handle's top (where the chains hang from), on the item and in the world.
        Vec top{};
        for(int c=0;c<bones.chains;++c)top=top+now[size_t(c)].joint[0];
        top=top*(1.f/float(bones.chains));
        const Vec topAt=skel::bow::Vector(item,top)*metre;
        const bool haveHead=Finite(world.head);
        const Vec headAt=haveHead?(world.head-place)*metre:Vec{};
        const float floor=std::isfinite(world.floor)?(world.floor-place.y)*metre:-1e9f;
        // The points' origin follows the item: what was about its last place is moved to be about this one.
        const Vec shift=(place-placed)*metre;
        bool fresh=!seeded||chains!=bones.chains;
        for(int c=0;c<bones.chains&&!fresh;++c)fresh=rope[size_t(c)].count!=now[size_t(c)].count;
        if(fresh)++stats.starts;
        else for(int c=0;c<bones.chains;++c)if(std::fabs(now[size_t(c)].chain/rope[size_t(c)].total-1)>Resized||now[size_t(c)].handed!=rope[size_t(c)].handed) {
            fresh=true;++stats.resized;break;
        }
        const double dt=time-last;
        bool laidFresh=false;
        if(fresh) {
            Hang(bones,now,top,topAt,headAt,haveHead,floor);
            laidFresh=true;last=time;turned=world.turn;
        } else {
            for(int c=0;c<chains;++c){Rope& r=rope[size_t(c)];for(int i=0;i<=r.count;++i)r.x[size_t(i)]=r.x[size_t(i)]-shift;}
            grip=grip-shift;handleTop=handleTop-shift;head=head-shift;
            bool jumped=false;
            for(int c=0;c<chains;++c)jumped|=Length(now[size_t(c)].at[0]-rope[size_t(c)].x[0])>Jump;
            if(world.turn!=turned||!(dt>=0)||dt>MaxStep||jumped) {
                // A snap turn moves the world about the player in one frame, a pause or a teleport leaves no path to
                // step along: the chain keeps the shape it had on the item. Through a turn it keeps its swing too.
                const bool turn=world.turn!=turned;
                if(turn)++stats.turns;else if(jumped)++stats.jumps;else ++stats.pauses;
                for(int c=0;c<chains;++c) {
                    Rope& r=rope[size_t(c)];
                    for(int i=0;i<=r.count;++i) {
                        r.x[size_t(i)]=skel::bow::Vector(item,r.held[size_t(i)])*metre;
                        r.v[size_t(i)]=turn?skel::bow::Vector(item,skel::bow::Vector(lastInverse,r.v[size_t(i)])):Vec{};
                    }
                    r.x[0]=now[size_t(c)].at[0];
                }
                turned=world.turn;last=time;
            } else if(dt>=MinStep) {
                std::array<Vec,MaxChains> anchors{};
                for(int c=0;c<chains;++c)anchors[size_t(c)]=now[size_t(c)].at[0];
                if(!Step(anchors,topAt,headAt,haveHead,floor,float(dt),stats)) {
                    ++stats.lost;Hang(bones,now,top,topAt,headAt,haveHead,floor);laidFresh=true;turned=world.turn;
                }
                last=time;
            }
        }
        grip={};handleTop=topAt;head=headAt;placed=place;
        lastInverse=inverse;
        // Back on the item: each bone at its point, turned to its link.
        const float lastStep=laidFresh?0.f:float(std::clamp(dt,0.,MaxStep));
        for(int c=0;c<chains;++c)if(!Lay(rope[size_t(c)],now[size_t(c)],bones.out[size_t(c)],inverse,
                                           world.unitsPerMetre,laidFresh,lastStep))
            return refuse(Why::Shape);
        ++stats.poses;stats.chains+=unsigned(chains);
        return true;
    }
    void Forget(){seeded=false;}
    // For the tests and the log: the chains, a point of one (0 its top, Links(c) the ball's centre) in world metres
    // about the item's place, a link's length, the chain's length and the ball's radius (m), and the first bone's twist
    // about the chain from hanging straight (rad; NaN when the chain points up the handle and hanging says nothing).
    int Chains()const{return seeded?chains:0;}
    int Links(int c)const{return rope[size_t(std::clamp(c,0,MaxChains-1))].count;}
    Vec Point(int c,int i)const{const Rope& r=rope[size_t(std::clamp(c,0,MaxChains-1))];return r.x[size_t(std::clamp(i,0,r.count))];}
    Vec Speed(int c,int i)const{const Rope& r=rope[size_t(std::clamp(c,0,MaxChains-1))];return r.v[size_t(std::clamp(i,0,r.count))];}
    Vec Head(int c=0)const{return Point(c,Links(c));}
    Vec Top(int c=0)const{return Point(c,0);}
    float LinkLength(int c,int i)const{const Rope& r=rope[size_t(std::clamp(c,0,MaxChains-1))];return r.length[size_t(std::clamp(i,0,MaxLinks-1))];}
    float ChainLength(int c)const{return rope[size_t(std::clamp(c,0,MaxChains-1))].total;}
    float BallSize(int c)const{return rope[size_t(std::clamp(c,0,MaxChains-1))].ball;}
    float Twist(int c)const{const Rope& r=rope[size_t(std::clamp(c,0,MaxChains-1))];return TwistOf(r,r.frame[0]);}
private:
    struct Native {
        int count=0;bool handed=true;float chain=0;
        std::array<Vec,MaxPoints> joint{},at{};   // the game's joints and ball on the item, and in the world (m about the item)
    };
    struct Rope {
        int count=0;bool handed=true;   // links (bones): points 0..count, the last the ball's centre; the bones' handedness
        float total=0,ball=0,handle=0;  // m: the chain's length (first joint to last), the ball's radius, the handle's
        std::array<Vec,MaxPoints> x{},v{},held{},previous{};  // the points, their speeds, where last laid on the item
        std::array<float,MaxPoints> w{};    // 1 / mass (the top: 0, held by the handle)
        std::array<float,MaxLinks> length{},nearest{};   // m between a point and the next; the closest the two either side of a joint come
        std::array<Vec,MaxLinks> axis{};    // each bone's own axis toward its next point
        std::array<float,MaxLinks> scale{}; // each bone's size as the game draws it
        std::array<Mat,MaxLinks> frame{};   // each bone's turn on the item (unit axes, the game's handedness)
        std::array<Mat,MaxLinks> link{};    // each bone's turn on the one before as the game's pose had it, its bend taken out
        Mat hanging{};                      // the first bone hanging straight down the handle: no twist
        Vec down{};                         // the item's way down the handle, from its top toward the grip
    };
    int chains=0;bool seeded=false;double last=0;uint32_t turned=0;
    std::array<Rope,MaxChains> rope{};
    Vec placed{},grip{},handleTop{},head{};   // the item's place (world units) and, about it, the last pose's grip, top and eyes (m)
    Mat lastInverse=skel::bow::Identity();
    static bool Handed(const Mat& m) {return math::Dot(math::Cross(Row(m,0),Row(m,1)),Row(m,2))>=0;}

    // Hung afresh: straight down from each chain's top, still, then settled out of the handle, the head, the floor and
    // the other balls; the bones' turns from the game's pose.
    void Hang(const Bones& bones,const std::array<Native,MaxChains>& now,Vec top,Vec topAt,Vec headAt,bool haveHead,float floor) {
        chains=bones.chains;seeded=true;
        for(int c=0;c<chains;++c) {
            Rope& r=rope[size_t(c)];const Native& g=now[size_t(c)];const auto& native=bones.native[size_t(c)];
            r.count=g.count;const int n=r.count;
            r.total=g.chain;r.handed=g.handed;r.ball=BallRadius*g.chain;r.handle=HandleRadius*g.chain;
            for(int i=0;i<n;++i)r.length[size_t(i)]=std::max(1e-4f,Length(g.at[size_t(i+1)]-g.at[size_t(i)]));
            for(int i=1;i<n;++i) {
                const float a=r.length[size_t(i-1)],b=r.length[size_t(i)],bend=i==n-1?BallBend:MaxBend;
                r.nearest[size_t(i)]=std::sqrt(std::max(0.f,a*a+b*b+2*a*b*std::cos(bend)));
            }
            r.w[0]=0;
            for(int i=1;i<=n;++i)r.w[size_t(i)]=1/(i==n?BallMass:LinkMass);
            // Straight down; what reaches the floor lies along it, the way the handle points.
            Vec spread=topAt;spread.y=0;spread=math::Normalized(spread);
            if(Length(spread)<.5f)spread={1,0,0};
            r.x[0]=g.at[0];
            for(int i=1;i<=n;++i) {
                const float lowest=floor+(i==n?r.ball:LinkRadius),l=r.length[size_t(i-1)];
                Vec p=r.x[size_t(i-1)]+Vec{0,-l,0};
                if(p.y<lowest) {
                    const float drop=std::max(0.f,r.x[size_t(i-1)].y-lowest);
                    p=r.x[size_t(i-1)]+Vec{0,-std::min(drop,l),0}+spread*std::sqrt(std::max(0.f,l*l-std::min(drop,l)*std::min(drop,l)));
                    p.y=std::max(p.y,lowest);
                }
                r.x[size_t(i)]=p;
            }
            for(int i=0;i<=n;++i){r.v[size_t(i)]={};r.held[size_t(i)]=g.joint[size_t(i)];}
            // The bones: unit axes as the game's pose has them, each one's axis toward its next point, and each one's
            // turn on the one before with the bend taken out - the twist the links have between them.
            std::array<Mat,MaxLinks> unit{};
            for(int i=0;i<n;++i) {
                const Mat& m=native[size_t(i)];
                r.scale[size_t(i)]=std::cbrt(std::fabs(math::Dot(math::Cross(Row(m,0),Row(m,1)),Row(m,2))));
                unit[size_t(i)]=Orthonormal(m);
                r.axis[size_t(i)]=math::Normalized(skel::bow::Vector(Transposed(unit[size_t(i)]),g.joint[size_t(i+1)]-g.joint[size_t(i)]));
                if(Length(r.axis[size_t(i)])<.5f)r.axis[size_t(i)]={1,0,0};
            }
            for(int i=1;i<n;++i) {
                const Mat relative=Times(unit[size_t(i)],Transposed(unit[size_t(i-1)]));
                const Vec bent=skel::bow::Vector(relative,r.axis[size_t(i)]);
                r.link[size_t(i)]=Times(relative,skel::bow::Rotation(bent,r.axis[size_t(i-1)]));
            }
            const Vec handle=math::Normalized(top);   // grip to top, on the item
            r.down=Length(handle)>.5f?handle*-1:Vec{0,-1,0};
            const Vec along=math::Normalized(g.joint[1]-g.joint[0]);
            r.hanging=Times(unit[0],skel::bow::Rotation(along,r.down));
            r.frame=unit;   // the first is carried on from here; all of them, should a link ever have no direction
        }
        grip={};handleTop=topAt;head=headAt;
        for(int k=0;k<Settles;++k) {
            for(int c=0;c<chains;++c){Rope& r=rope[size_t(c)];Lengths(r);Bend(r);Collide(r,{},topAt,headAt,haveHead,floor);}
            Apart();
            for(int c=0;c<chains;++c){Rope& r=rope[size_t(c)];Lengths(r);Floor(r,floor);}
        }
        for(int c=0;c<chains;++c){Rope& r=rope[size_t(c)];for(int i=0;i<=r.count;++i)r.v[size_t(i)]={};}
    }

    // The links back to their lengths, all at once: one Newton step of the chain's distance constraints, solved exactly
    // (tridiagonal) and weighted by the points' masses. The top does not move.
    static void Lengths(Rope& r) {
        const int n=r.count;
        std::array<Vec,MaxLinks> dir{};std::array<float,MaxLinks> rhs{},diag{},off{},cp{},dp{},lambda{};
        for(int j=0;j<n;++j) {
            const Vec e=r.x[size_t(j+1)]-r.x[size_t(j)];const float l=Length(e);
            dir[size_t(j)]=l>1e-7f?e*(1/l):j?dir[size_t(j-1)]:Vec{0,-1,0};
            rhs[size_t(j)]=r.length[size_t(j)]-l;
            diag[size_t(j)]=r.w[size_t(j)]+r.w[size_t(j+1)];
        }
        for(int j=0;j+1<n;++j)off[size_t(j)]=-r.w[size_t(j+1)]*math::Dot(dir[size_t(j)],dir[size_t(j+1)]);
        float m=diag[0];
        if(!(m>1e-12f))return;
        cp[0]=n>1?off[0]/m:0;dp[0]=rhs[0]/m;
        for(int j=1;j<n;++j) {
            m=diag[size_t(j)]-off[size_t(j-1)]*cp[size_t(j-1)];
            if(!(m>1e-12f))return;
            cp[size_t(j)]=j+1<n?off[size_t(j)]/m:0;
            dp[size_t(j)]=(rhs[size_t(j)]-off[size_t(j-1)]*dp[size_t(j-1)])/m;
        }
        lambda[size_t(n-1)]=dp[size_t(n-1)];
        for(int j=n-2;j>=0;--j)lambda[size_t(j)]=dp[size_t(j)]-cp[size_t(j)]*lambda[size_t(j+1)];
        for(int k=1;k<=n;++k) {
            Vec move=dir[size_t(k-1)]*lambda[size_t(k-1)];
            if(k<n)move=move-dir[size_t(k)]*lambda[size_t(k)];
            r.x[size_t(k)]=r.x[size_t(k)]+move*r.w[size_t(k)];
        }
    }
    // No joint bent past its limit: the two points either side of it kept at least nearest apart.
    static void Bend(Rope& r) {
        for(int k=1;k<r.count;++k) {
            const Vec e=r.x[size_t(k+1)]-r.x[size_t(k-1)];const float d=Length(e),want=r.nearest[size_t(k)];
            if(d>=want)continue;
            // Folded right back (the two points on each other): pushed apart square to the link.
            const Vec way=d>1e-6f?e*(1/d):Across(r.x[size_t(k)]-r.x[size_t(k-1)]);
            const float wa=r.w[size_t(k-1)],wb=r.w[size_t(k+1)];
            const Vec push=way*((want-d)/(wa+wb));
            r.x[size_t(k-1)]=r.x[size_t(k-1)]-push*wa;r.x[size_t(k+1)]=r.x[size_t(k+1)]+push*wb;
        }
    }
    // Out of the handle (the grip to just under the top, where the chain hangs from), the player's head and the floor.
    static void Collide(Rope& r,Vec gripAt,Vec topAt,Vec headAt,bool haveHead,float floor) {
        const Vec shaft=topAt-gripAt;const float shaftLength=Length(shaft);
        const Vec capped=shaftLength>r.handle+LinkRadius?topAt-shaft*((r.handle+LinkRadius)/shaftLength):gripAt;
        // The hand at the face: the head kept clear of the chain's top by HeadClear, so there is always room for the
        // chain to leave it (smaller as the hand comes in, none with the hand inside it).
        const float headRadius=haveHead?std::min(HeadRadius,Length(r.x[0]-headAt)-LinkRadius-HeadClear):0.f;
        const bool head=headRadius>.02f;
        for(int k=1;k<=r.count;++k) {
            const float radius=k==r.count?r.ball:LinkRadius;
            Vec& p=r.x[size_t(k)];
            if(shaftLength>1e-6f)OutOfSegment(p,gripAt,capped,r.handle+radius);
            if(head)OutOfSphere(p,headAt,headRadius+radius);
            if(p.y<floor+radius)p.y=floor+radius;
        }
    }
    // A link the solves left more than Tidy off its length (the chain caught between the handle, the head and a hard
    // pull): set to it from the handle out, its point moved along the link. Positions only: the move adds no speed.
    static void Straighten(Rope& r) {
        for(int j=0;j<r.count;++j) {
            const Vec e=r.x[size_t(j+1)]-r.x[size_t(j)];const float l=Length(e),want=r.length[size_t(j)];
            if(std::fabs(l-want)<=Tidy*want||l<1e-7f)continue;
            const Vec move=e*(want/l-1);
            r.x[size_t(j+1)]=r.x[size_t(j+1)]+move;r.previous[size_t(j+1)]=r.previous[size_t(j+1)]+move;
        }
    }
    static void Floor(Rope& r,float floor) {
        for(int k=1;k<=r.count;++k){const float lowest=floor+(k==r.count?r.ball:LinkRadius);if(r.x[size_t(k)].y<lowest)r.x[size_t(k)].y=lowest;}
    }
    // Khalim's three balls do not pass through each other.
    void Apart() {
        for(int a=0;a<chains;++a)for(int b=a+1;b<chains;++b) {
            Rope& ra=rope[size_t(a)];Rope& rb=rope[size_t(b)];
            Vec& pa=ra.x[size_t(ra.count)];Vec& pb=rb.x[size_t(rb.count)];
            const Vec e=pb-pa;const float d=Length(e),want=ra.ball+rb.ball;
            if(d>=want)continue;
            const Vec way=d>1e-6f?e*(1/d):Vec{1,0,0};
            const Vec push=way*((want-d)*.5f);
            pa=pa-push;pb=pb+push;
        }
    }
    // dt seconds in substeps: each point coasts and falls, its speed about the hand rubbed down and smoothed along the
    // chain; the tops follow the handle there in a straight line; the links back to length, the bends, the handle, the
    // head, the floor and the balls; the speeds from where the points went. False: a point was lost (not finite).
    bool Step(const std::array<Vec,MaxChains>& anchors,Vec topAt,Vec headAt,bool haveHead,float floorAt,float dt,Stats& stats) {
        const int steps=std::clamp(int(std::ceil(dt/SubstepSeconds)),1,MaxSubsteps);
        const float h=dt/float(steps),keep=std::exp(-SwingDrag*h),smooth=1-std::exp(-ChainDrag*h),slide=std::exp(-FloorDrag*h);
        std::array<Vec,MaxChains> from{},carried{};
        for(int c=0;c<chains;++c){from[size_t(c)]=rope[size_t(c)].x[0];carried[size_t(c)]=(anchors[size_t(c)]-from[size_t(c)])*(1/dt);}
        const Vec gripFrom=grip,topFrom=handleTop,headFrom=head;
        const Vec fall{0,-Gravity*h,0};
        for(int s=1;s<=steps;++s) {
            const float f=float(s)/float(steps);
            const Vec gripNow=Lerp(gripFrom,Vec{},f),topNow=Lerp(topFrom,topAt,f),headNow=Lerp(headFrom,headAt,f);
            for(int c=0;c<chains;++c) {
                Rope& r=rope[size_t(c)];const int n=r.count;const Vec hand=carried[size_t(c)];
                r.v[0]=hand;
                const auto was=r.v;
                for(int k=1;k<n;++k)r.v[size_t(k)]=r.v[size_t(k)]+((was[size_t(k-1)]+was[size_t(k+1)])*.5f-r.v[size_t(k)])*smooth;
                for(int k=1;k<=n;++k) {
                    r.previous[size_t(k)]=r.x[size_t(k)];
                    r.v[size_t(k)]=hand+(r.v[size_t(k)]-hand)*keep+fall;
                    r.x[size_t(k)]=r.x[size_t(k)]+r.v[size_t(k)]*h;
                }
                r.x[0]=Lerp(from[size_t(c)],anchors[size_t(c)],f);
            }
            // The links, the bends, the handle, the head, the floor and the balls by turns, a few times over: the chain
            // drapes round the head or the handle at its length instead of being stretched through them.
            for(int pass=0;pass<Passes;++pass) {
                for(int c=0;c<chains;++c){Rope& r=rope[size_t(c)];Lengths(r);Bend(r);Collide(r,gripNow,topNow,headNow,haveHead,floorAt);}
                Apart();
            }
            for(int c=0;c<chains;++c) {
                Rope& r=rope[size_t(c)];const int n=r.count;
                // The links back to length and the points up out of the floor, by turns: what lies on the floor spreads
                // along it at its length instead of being squashed into it.
                for(int k=0;k<Solves;++k){Lengths(r);Floor(r,floorAt);}
                Straighten(r);Floor(r,floorAt);
                for(int k=1;k<=n;++k) {
                    const float lowest=floorAt+(k==n?r.ball:LinkRadius);
                    const bool resting=r.x[size_t(k)].y<=lowest+1e-4f;
                    Vec v=(r.x[size_t(k)]-r.previous[size_t(k)])*(1/h);
                    if(resting){v.x*=slide;v.z*=slide;}
                    if(const float speed=Length(v);speed>MaxSpeed)v=v*(MaxSpeed/speed);
                    r.v[size_t(k)]=v;
                }
            }
        }
        for(int c=0;c<chains;++c) {
            const Rope& r=rope[size_t(c)];const int n=r.count;
            for(int k=0;k<=n;++k)if(!Finite(r.x[size_t(k)])||!Finite(r.v[size_t(k)]))return false;
            stats.fastest=std::max(stats.fastest,Length(r.v[size_t(n)]-carried[size_t(c)]));
            const Vec out=r.x[size_t(n)]-r.x[0];
            if(const float l=Length(out);l>1e-4f)
                stats.widest=std::max(stats.widest,std::acos(std::clamp(-out.y/l,-1.f,1.f))*57.29578f);
            for(int k=0;k<n;++k)
                stats.stretch=std::max(stats.stretch,std::fabs(Length(r.x[size_t(k+1)]-r.x[size_t(k)])/r.length[size_t(k)]-1));
        }
        ++stats.steps;
        return true;
    }
    // The first bone's twist about its link from the turn it would have hanging straight down the handle and swung
    // out the shortest way from there (rad). NaN within 60 deg of straight up the handle: no such turn says anything.
    static float TwistOf(const Rope& r,const Mat& frame) {
        const Vec along=math::Normalized(skel::bow::Vector(frame,r.axis[0]));
        if(math::Dot(along,r.down)<-.5f)return NAN;
        const Mat reference=Times(r.hanging,skel::bow::Rotation(r.down,along));
        const Vec side=Across(r.axis[0]);
        const Vec want=skel::bow::Vector(reference,side),have=skel::bow::Vector(frame,side);
        return std::atan2(math::Dot(math::Cross(want,have),along),math::Dot(want,have));
    }
    // Each bone at its point on the item, turned to its link: the first from its turn of the last pose (or the game's
    // pose, hung afresh) the shortest way to its link now, its twist held within TwistLimit; each next from the one
    // before it, the shortest way from where that one would hold it straight. out is written only once all are good.
    static bool Lay(Rope& r,const Native& g,std::span<Mat> out,const Mat& inverse,float unitsPerMetre,bool fresh,float dt) {
        const int n=r.count;
        std::array<Vec,MaxPoints> held{};
        for(int i=0;i<=n;++i)held[size_t(i)]=skel::bow::Vector(inverse,r.x[size_t(i)]*unitsPerMetre);
        held[0]=g.joint[0];
        for(int i=0;i<=n;++i)if(!Finite(held[size_t(i)]))return false;
        std::array<Vec,MaxLinks> to{};
        for(int i=0;i<n;++i) {
            to[size_t(i)]=math::Normalized(held[size_t(i+1)]-held[size_t(i)]);
            if(Length(to[size_t(i)])<.5f)to[size_t(i)]=math::Normalized(skel::bow::Vector(r.frame[size_t(i)],r.axis[size_t(i)]));
            if(Length(to[size_t(i)])<.5f)return false;
        }
        std::array<Mat,MaxLinks> frame{};
        const Vec was=math::Normalized(skel::bow::Vector(r.frame[0],r.axis[0]));
        frame[0]=Orthonormal(Times(r.frame[0],skel::bow::Rotation(was,to[0])));
        if(const float twist=TwistOf(r,frame[0]);std::isfinite(twist)&&std::fabs(twist)>TwistLimit) {
            const float back=std::min(std::fabs(twist)-TwistLimit,fresh?3.2f:Unwind*dt);
            frame[0]=Orthonormal(Times(frame[0],Spin(to[0],twist>0?-back:back)));
        }
        for(int i=1;i<n;++i) {
            const Mat straight=Times(r.link[size_t(i)],frame[size_t(i-1)]);
            const Vec would=math::Normalized(skel::bow::Vector(straight,r.axis[size_t(i)]));
            frame[size_t(i)]=Times(straight,skel::bow::Rotation(would,to[size_t(i)]));
        }
        for(int i=0;i<n;++i){for(float x:frame[size_t(i)].m)if(!std::isfinite(x))return false;}
        for(int i=0;i<n;++i) {
            Mat m=frame[size_t(i)];
            for(int k:{0,1,2,4,5,6,8,9,10})m.m[k]*=r.scale[size_t(i)];
            m.m[3]=m.m[7]=m.m[11]=0;m.m[15]=1;
            skel::bow::Position(m,held[size_t(i)]);
            out[size_t(i)]=m;
        }
        r.frame=frame;r.held=held;
        return true;
    }
};
}
