#pragma once
#include "../vr/bow_pose.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

// A flail's chain swings (a player with two flails, 2026-11-19: "it would be amazing if they had physics instead of
// the stiff chain that defies physics", then "Just a cosmetic change so the chain actually swings").
//
// The game's flail models are rigged: a chain of bones from the top of the handle to the ball, which the game moves
// with each class's own animation of it (data/hd/character/player/<class>/animation/weapon/*_flail.animation). That
// animation was made for the game's own arm swings; with the arm on a controller the chain kept the game's pose in
// the hand's frame, stiff whichever way the hand turned. Here the chain hangs from the handle under its own weight
// and swings with the hand: a rope of the chain's joints and the ball, stepped in the world, then laid back on the
// item's bones where the game drew them (vr/skeletons.cpp PoseFlailChain), as the bow's string is. Looks only: the
// melee contact still reaches the head straight along the handle (weapon_reach.h).
//
// Pure logic: no native types, no threads. Matrices are the game's (row vectors, translation in m[12..14]).
namespace framevr::flail {
using math::Vec;
using skel::bow::Mat;

constexpr size_t MaxBones=64;
constexpr int MaxChains=3,MaxLinks=24,MinLinks=4;

// ---- the rig ----
inline bool Digits(std::string_view s) {
    return !s.empty()&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});
}
// A chain link's bone, as the game's flail models name them: chain_bnd_jnt_01..18 (Flail, Knout and Scourge), and
// L_/C_/R_chain_NN with L_/C_/R_bindJoint_NN between (Khalim's Flail and Khalim's Will, three chains each). Not
// mainChain or a chain's _root: those hold the chains to the handle and stay where the game puts them.
inline bool Link(std::string_view name) {
    if(name.starts_with("chain_bnd_jnt_"))return Digits(name.substr(14));
    if(name.size()>2&&(name[0]=='L'||name[0]=='C'||name[0]=='R')&&name[1]=='_') {
        const auto rest=name.substr(2);
        if(rest.starts_with("chain_"))return Digits(rest.substr(6));
        if(rest.starts_with("bindJoint_"))return Digits(rest.substr(10));
    }
    return false;
}
struct Chain {int count=0;std::array<int,MaxLinks> bone{};};   // its links' bones, from the handle out
struct Rig {
    int chains=0,links=0;
    std::array<Chain,MaxChains> chain{};
    bool Valid()const{return chains>0;}
};
// The chains of a skeleton: each from a link whose parent is no link, through its one child to the last link. A link
// with any other child, a chain shorter than MinLinks or longer than MaxLinks, or more chains than a flail has: no rig,
// and the game's own animation stays.
inline Rig Find(std::span<const std::string_view> names,std::span<const int> parents) {
    if(names.size()!=parents.size()||names.empty()||names.size()>MaxBones)return {};
    const int n=int(names.size());
    std::array<bool,MaxBones> link{};
    for(int i=0;i<n;++i) {
        if(parents[i]<-1||parents[i]>=n||parents[i]==i)return {};
        link[i]=Link(names[i]);
    }
    Rig rig;
    for(int i=0;i<n;++i)if(link[i]&&(parents[i]<0||!link[parents[i]])) {
        if(rig.chains==MaxChains)return {};
        Chain c;int at=i;
        for(;;) {
            if(c.count==MaxLinks)return {};
            c.bone[c.count++]=at;
            int next=-1,children=0;
            for(int j=0;j<n;++j)if(parents[j]==at){++children;if(link[j])next=j;}
            if(!children)break;
            if(children!=1||next<0)return {};   // a link holds the next link and nothing else
            at=next;
        }
        if(c.count<MinLinks)return {};
        rig.chain[rig.chains++]=c;rig.links+=c.count;
    }
    return rig;
}

// ---- the swing ----
constexpr float Gravity=9.81f;          // m/s^2, down the world's -Y
constexpr float Drag=1.6f;              // 1/s: the links' rub; a swing about the hand dies away in about two seconds
constexpr float HeadMass=6;             // the ball against one link: it hangs the chain straight and leads a swing
constexpr float HeadLinks=3;            // the ball's centre this many links past the last joint (its own bone's length)
constexpr float SubstepSeconds=.002f;   // a step is cut into substeps this long, at most MaxSubsteps
constexpr int MaxSubsteps=16,Iterations=4;
constexpr double MinStep=.002;          // s: a pose sooner than this after the last step reuses it (several poses a frame)
constexpr double MaxStep=.1;            // s: longer (a pause, a menu, a hitch) starts afresh from the game's own pose
constexpr float Jump=1;                 // m the chain's top moved in one step: a teleport, a load - start afresh
constexpr float Resized=.1f;            // the chain's length changed by this share: another item or scale - afresh
constexpr float MaxSpeed=40;            // m/s: no point faster (a bad frame is not thrown about)
constexpr float MinChain=.05f,MaxChain=3;   // m: a chain shorter (an item the game hid by shrinking) or longer is left
constexpr float FloorDrag=12;           // 1/s: what rests on the floor slides to a stop
constexpr float Nudge=.05f;             // m/s across the chain at the ball, at a fresh start

struct Stats {
    unsigned poses=0,steps=0;       // calls laid on the bones; of those, steps of the rope
    unsigned starts=0,pauses=0,jumps=0,resized=0,turns=0;   // fresh starts: first, after a pause, a jump, a new size; turns carried
    unsigned refused=0;             // calls left to the game (nothing usable)
    float fastest=0;                // m/s: the ball's fastest
    void Add(const Stats& s) {
        poses+=s.poses;steps+=s.steps;starts+=s.starts;pauses+=s.pauses;jumps+=s.jumps;resized+=s.resized;turns+=s.turns;
        refused+=s.refused;fastest=std::max(fastest,s.fastest);
    }
};
inline float Length(Vec v) {return std::sqrt(math::Dot(v,v));}
inline bool Finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}

// One chain: its joints and the ball as points in the world (metres), the first held at the handle's top.
class Swing {
public:
    // native: the chain's bones as the game posed them this call, in the item's space, from the handle out; item: the
    // item's space to the world's units; unitsPerMetre: the world's units a metre; floorUnits: the ground's height in
    // the world (NaN: none); time: seconds, rising; turn: the world's turn count - a snap turn carries the chain along
    // as it hangs, instead of flinging it. out: the bones to draw, as many as native. False: out is untouched and the
    // game's own pose stays.
    bool Pose(std::span<const Mat> native,const Mat& item,float unitsPerMetre,float floorUnits,double time,uint32_t turn,
              std::span<Mat> out,Stats& stats) {
        const int links=int(native.size());
        if(links<2||links>MaxLinks||out.size()!=native.size()||!std::isfinite(time)||
           !std::isfinite(unitsPerMetre)||unitsPerMetre<.1f||unitsPerMetre>100){++stats.refused;seeded=false;return false;}
        Mat inverse;
        if(!skel::bow::Inverse(item,inverse)){++stats.refused;seeded=false;return false;}
        for(const Mat& m:native)if(!skel::bow::Affine(m)){++stats.refused;seeded=false;return false;}
        const float metre=1/unitsPerMetre;
        auto world=[&](Vec p){return (skel::bow::Vector(item,p)+skel::bow::Position(item))*metre;};
        auto local=[&](Vec p){return skel::bow::Vector(inverse,p*unitsPerMetre)+skel::bow::Position(inverse);};
        // The game's pose: the joints, and the ball along the last bone the way the links run along theirs.
        std::array<Vec,MaxLinks+1> joint{},at{};
        for(int i=0;i<links;++i)joint[i]=skel::bow::Position(native[i]);
        float itemLength=0;
        for(int i=0;i+1<links;++i)itemLength+=Length(joint[i+1]-joint[i]);
        Mat before;
        if(!(itemLength>0)||!skel::bow::Inverse(native[links-2],before)){++stats.refused;seeded=false;return false;}
        const Vec headAxis=math::Normalized(skel::bow::Vector(native[links-1],skel::bow::Vector(before,joint[links-1]-joint[links-2])));
        if(Length(headAxis)<.5f){++stats.refused;seeded=false;return false;}
        joint[links]=joint[links-1]+headAxis*(HeadLinks*itemLength/float(links-1));
        float chain=0;
        for(int i=0;i<=links;++i){at[i]=world(joint[i]);if(!Finite(at[i])){++stats.refused;seeded=false;return false;}}
        for(int i=0;i+1<links;++i)chain+=Length(at[i+1]-at[i]);
        if(!(chain>=MinChain&&chain<=MaxChain)){++stats.refused;seeded=false;return false;}
        const float floor=std::isfinite(floorUnits)?floorUnits*metre:-1e9f;
        const double dt=time-last;
        bool start=false;
        if(!seeded||count!=links){start=true;++stats.starts;}
        else if(!(dt>=0)||dt>MaxStep){start=true;++stats.pauses;}
        else if(Length(at[0]-x[0])>Jump){start=true;++stats.jumps;}
        else if(std::fabs(chain/total-1)>Resized){start=true;++stats.resized;}
        if(start) {
            count=links;total=chain;seeded=true;last=time;turned=turn;
            // From the game's pose, still but for a nudge across the chain: one held straight up (the model's rest pose)
            // stands balanced on its top otherwise, as a real one never does.
            const Vec along=math::Normalized(at[links]-at[0]);
            Vec across=math::Normalized(math::Cross(along,{0,0,1}));
            if(Length(across)<.5f)across=math::Normalized(math::Cross(along,{1,0,0}));
            for(int i=0;i<=links;++i){x[i]=at[i];v[i]=across*(Nudge*float(i)/float(links));}
            for(int i=0;i<links;++i)length[i]=Length(at[i+1]-at[i]);
        } else if(turn!=turned) {
            // A snap turn moves the world about the player in one frame: the chain keeps the shape it had on the item.
            turned=turn;last=time;++stats.turns;
            for(int i=0;i<=links;++i){x[i]=world(held[i]);v[i]={};}
            x[0]=at[0];
        } else if(dt>=MinStep) {
            Step(at[0],float(dt),floor,stats);last=time;
        }
        // Back on the item: each bone at its point, turned from where the game had its next point to where it is now.
        for(int i=0;i<=links;++i){held[i]=local(x[i]);if(!Finite(held[i])){++stats.refused;seeded=false;return false;}}
        held[0]=joint[0];
        for(int i=0;i<links;++i) {
            const Vec from=joint[i+1]-joint[i],to=held[i+1]-held[i];
            const Mat turnBy=Length(to)>1e-6f&&Length(from)>1e-6f?skel::bow::Rotation(from,to):skel::bow::Identity();
            out[i]=skel::bow::Turn(native[i],turnBy,held[i]);
        }
        ++stats.poses;
        return true;
    }
    void Forget(){seeded=false;}
    // For the tests: the ball's centre and a joint, world metres; the rope's links.
    Vec Head()const{return x[count];}
    Vec Joint(int i)const{return x[std::clamp(i,0,count)];}
    float LinkLength(int i)const{return length[std::clamp(i,0,MaxLinks-1)];}
private:
    int count=0;bool seeded=false;double last=0;uint32_t turned=0;float total=1;
    std::array<Vec,MaxLinks+1> x{},v{},held{};   // the points (world metres), their speeds, and where the last pose laid them on the item
    std::array<float,MaxLinks> length{};        // m between each point and the next
    // Position-based: the points fall and coast, the links pull them back to length (the ball heavy), in substeps.
    void Step(Vec anchor,float dt,float floor,Stats& stats) {
        const int steps=std::clamp(int(std::ceil(dt/SubstepSeconds)),1,MaxSubsteps);
        const float h=dt/float(steps),keep=std::exp(-Drag*h),slide=std::exp(-FloorDrag*h);
        const Vec from=x[0],fall{0,-Gravity*h,0};
        // The rub slows the swing about the hand, not the walk: a flail carried along hangs as it does standing.
        const Vec carried=(anchor-from)*(1/dt);
        const float ball=HeadLinks*total/float(std::max(1,count-1));   // the ball's half size: it rests this far above the floor
        std::array<Vec,MaxLinks+1> previous{};
        for(int s=1;s<=steps;++s) {
            for(int i=1;i<=count;++i){previous[i]=x[i];v[i]=carried+(v[i]-carried)*keep+fall;x[i]=x[i]+v[i]*h;}
            x[0]=from+(anchor-from)*(float(s)/float(steps));
            for(int k=0;k<Iterations;++k)for(int i=0;i<count;++i) {
                const Vec d=x[i+1]-x[i];const float l=Length(d);
                if(l<1e-6f)continue;
                const float a=i==0?0.f:1.f,b=i+1==count?1/HeadMass:1.f;
                const Vec move=d*((l-length[i])/(l*(a+b)));
                x[i]=x[i]+move*a;x[i+1]=x[i+1]-move*b;
            }
            for(int i=1;i<=count;++i) {
                const float lowest=floor+(i==count?ball:0.f);
                const bool resting=x[i].y<lowest;
                if(resting)x[i].y=lowest;
                v[i]=(x[i]-previous[i])*(1/h);
                if(resting){v[i].x*=slide;v[i].z*=slide;}
                if(const float speed=Length(v[i]);speed>MaxSpeed)v[i]=v[i]*(MaxSpeed/speed);
            }
        }
        // The links held exactly to length from the handle out (positions only: the speeds stay as stepped).
        for(int i=0;i<count;++i) {
            const Vec d=x[i+1]-x[i];const float l=Length(d);
            if(l>1e-6f)x[i+1]=x[i]+d*(length[i]/l);
        }
        stats.fastest=std::max(stats.fastest,Length(v[count]));
        ++stats.steps;
    }
};
}
