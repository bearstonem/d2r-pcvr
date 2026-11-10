#pragma once
#include "frame_math.h"
#include "world_coordinates.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Spells that reach the things weapons reach: the hero's own spell missiles
// break the game's breakable containers they pass or burst at, and push the
// level's small props and dropped bottles (dropped_bottles.h) out of their
// way. Everything here is plain data and arithmetic, shared by the game
// thread (vr/spell_impacts.cpp), the physics world and the tests.
//
// Two lengths are used. World units are the game's grid plane, two to a
// collision cell (a missile's or an object's path position, doubled); metres
// are the rendered world the physics runs in.
namespace framevr::spellimpacts {
using math::Vec;

// One of the hero's spell missiles (Missiles.txt row numbers, game data, the
// same on every game build). Only missiles the game steps with its standard
// server function (pSrvDoFunc 1) are listed: that one function is where their
// flight is followed. Holy Bolt (55) is left out as for arrows: it heals.
struct Profile {
    uint32_t missile=0;
    const char* name="";
    float reach=0;        // world units either side of its path that break a container; 0: its path breaks nothing
    float impulse=0;      // N s given to a prop or bottle in its way
    float blast=0;        // world units its burst reaches where it ends; 0: no burst
    float blastImpulse=0; // N s at the burst's centre, falling off to its edge
};
inline constexpr float BoltReach=1;                 // half a cell, the game's own missile size 1
inline constexpr float Light=.5f,Heavy=1.2f;        // a 0.25 kg cup leaves at 2 or 4 m/s
inline constexpr float Stream=.2f;                  // one flame of many
inline constexpr float Burst=2.f;
inline constexpr Profile Profiles[]={
    {56,"Charged Bolt",BoltReach,Light},
    {58,"Fire Bolt",BoltReach,Light},
    {59,"Ice Bolt",BoltReach,Light},
    {60,"Inferno",BoltReach,Stream},
    {61,"Inferno",BoltReach,Stream},
    {62,"Fire Ball",BoltReach,Heavy,5,Burst},          // bursts 2.5 cells around
    {90,"Nova",BoltReach,Light},
    {91,"Ice Blast",BoltReach,Heavy},
    {92,"Blessed Hammer",BoltReach,Heavy},
    {93,"Chain Lightning",BoltReach,Light},
    {96,"Glacial Spike",BoltReach,Heavy,6,Burst*.75f}, // its freeze, 3 cells
    {98,"Lightning",BoltReach,Light},
    {101,"Meteor",0,0,8,Burst*1.5f},                   // nothing until it lands
    {114,"Teeth",BoltReach,Light},
    {118,"Poison Nova",BoltReach,Stream},
    {119,"Frost Nova",BoltReach,Light},
    {192,"Bone Spear",BoltReach,Heavy},
    {235,"War Cry",BoltReach,Light},
    {261,"Frozen Orb",BoltReach,Light},
    {457,"Firestorm",BoltReach,Stream},
    {459,"Arctic Blast",BoltReach,Stream},
    {460,"Arctic Blast",BoltReach,Stream},
    {477,"Twister",BoltReach,Light},
    {511,"Shock Wave",BoltReach,Light},
    {575,"Inferno",BoltReach,Stream},
};
inline const Profile* Of(uint32_t missile) {
    for(const auto& p:Profiles)if(p.missile==missile)return &p;
    return nullptr;
}

// ---- Containers, on the grid plane (world units) ----
struct Point {float x=0,z=0;};
inline bool Finite(Point p) {return std::isfinite(p.x)&&std::isfinite(p.z);}
// A container's footprint: its Objects size in cells is its radius in world
// units, kept between a small urn and a large barrel.
inline float ContainerRadius(int sizeX,int sizeY) {return std::clamp(float(std::max(sizeX,sizeY)),.6f,2.f);}
// How far along a to b (0..1) the path comes nearest to p.
inline float Nearest(Point a,Point b,Point p) {
    const float dx=b.x-a.x,dz=b.z-a.z,n=dx*dx+dz*dz;
    return n>1e-9f?std::clamp(((p.x-a.x)*dx+(p.z-a.z)*dz)/n,0.f,1.f):0.f;
}
inline float Distance(Point a,Point b,Point p) {
    const float t=Nearest(a,b,p);
    return std::hypot(p.x-(a.x+(b.x-a.x)*t),p.z-(a.z+(b.z-a.z)*t));
}
// A step longer than this is not a flight (a teleported or reused unit).
inline constexpr float LongestStep=24;
inline bool PathBreaks(const Profile& p,Point from,Point to,Point container,float radius) {
    if(!(p.reach>0)||!Finite(from)||!Finite(to)||!Finite(container)||!(radius>0))return false;
    if(std::hypot(to.x-from.x,to.z-from.z)>LongestStep)return false;
    return Distance(from,to,container)<=p.reach+radius;
}
inline bool BlastBreaks(const Profile& p,Point centre,Point container,float radius) {
    if(!(p.blast>0)||!Finite(centre)||!Finite(container)||!(radius>0))return false;
    return std::hypot(container.x-centre.x,container.z-centre.z)<=p.blast+radius;
}

// ---- The hero's spell missiles in flight (game thread) ----
// Entered when the server makes one, stepped by its unit id, dropped when it
// ends or has not been stepped for a while (a room unloaded under it).
class Followed {
public:
    struct Entry {uintptr_t game=0;uint32_t unit=0,missile=0;Point at{};uint64_t seen=0;};
    static constexpr unsigned Most=256;
    static constexpr uint64_t Stale=20000; // ms without a step
    void Add(uintptr_t game,uint32_t unit,uint32_t missile,Point at,uint64_t now) {
        if(!game||!unit||!Finite(at))return;
        Entry* slot=nullptr;
        for(auto& e:entries) {
            if(e.unit==unit&&e.game==game){slot=&e;break;}
            if(!slot&&(!e.unit||now-e.seen>Stale))slot=&e;
        }
        if(!slot) {   // full of live flights: the one stepped longest ago goes
            slot=&entries[0];
            for(auto& e:entries)if(e.seen<slot->seen)slot=&e;
        }
        if(!slot->unit)++count;
        *slot={game,unit,missile,at,now};
    }
    // The unit must still be that missile: the game hands unit ids out again.
    Entry* Find(uintptr_t game,uint32_t unit,uint32_t missile,uint64_t now) {
        if(!count||!unit)return nullptr;
        for(auto& e:entries)if(e.unit==unit&&e.game==game) {
            if(e.missile==missile&&now-e.seen<=Stale)return &e;
            Remove(e);return nullptr;
        }
        return nullptr;
    }
    void Remove(Entry& e) {if(e.unit){e={};--count;}}
    void Clear() {entries={};count=0;}
    unsigned Count() const {return count;}
private:
    std::array<Entry,Most> entries{};
    unsigned count=0;
};

// ---- From the game thread to the physics (the XR thread) ----
// Where a missile went in one server step, or where it burst: grid-plane
// world units, with its height above the floor in metres when it was flown
// from the hand. Native missiles live on the game's flat grid and have none.
struct Passage {
    uint64_t session=0,at=0;       // the local player's session; GetTickCount64
    uint32_t missile=0;
    Point from{},to{};             // a burst is at to
    float fromHeight=0,toHeight=0; // metres above the floor
    bool flat=true;                // no height: anything above or below its path is in its way
    bool blast=false;
};
// The newest passages; older ones go first when the reader falls behind.
class Mailbox {
public:
    static constexpr unsigned Most=128;
    static constexpr uint64_t Fresh=250; // ms: later than this it is no longer where the missile is
    void Post(const Passage& p) {
        if(!p.session||!Of(p.missile)||!Finite(p.from)||!Finite(p.to))return;
        ring[(first+used)%Most]=p;
        if(used<Most)++used;else first=(first+1)%Most;
    }
    // Everything waiting is taken; stale ones and another session's are dropped.
    template<class Out> unsigned Take(uint64_t session,uint64_t now,Out& out) {
        unsigned n=0;
        for(unsigned i=0;i<used;++i) {
            const auto& p=ring[(first+i)%Most];
            if(p.session==session&&now>=p.at&&now-p.at<=Fresh&&n<out.size())out[n++]=p;
        }
        first=used=0;
        return n;
    }
    unsigned Waiting() const {return used;}
private:
    std::array<Passage,Most> ring{};
    unsigned first=0,used=0;
};

// ---- Pushing props and bottles (metres, the rendered world) ----
// A missile's step from a to b, or a burst at b, for the physics world.
struct Push {
    Vec a{},b{};
    float radius=0;   // metres around the path, or the burst's reach
    float impulse=0;  // N s on something in its way, or at the burst's centre
    bool blast=false;
    bool flat=false;  // heights are ignored: a native missile on the game's grid
};
inline constexpr float BoltRadius=.12f,HeavyRadius=.2f; // metres a flown missile reaches around its path
inline constexpr float FlatRadius=.3f;                  // a native one: the grid is coarse
inline constexpr float MaxRadius=4;                     // metres, the widest burst
inline constexpr float FlatHeight=1;                    // metres above the floor a flat push is drawn at
inline bool Valid(const Push& p) {
    const Vec d=p.b-p.a;
    return coordinates::Finite(p.a)&&coordinates::Finite(p.b)&&std::isfinite(p.radius)&&p.radius>0&&p.radius<=MaxRadius&&
        std::isfinite(p.impulse)&&p.impulse>0&&p.impulse<=10&&math::Dot(d,d)<=8*8;
}
// The passage in the rendered world: the grid plane drawn on this floor, as
// wall cells are (bottles::CellCorner), the missile its height above it.
inline Push ToWorld(const Passage& p,float floor,float unitsPerMetre) {
    Push out;const auto* profile=Of(p.missile);
    if(!profile||!std::isfinite(floor)||!(unitsPerMetre>=1&&unitsPerMetre<=12))return out;
    const auto at=[&](Point g,float height) {
        return coordinates::RenderGround({g.x/unitsPerMetre,0,g.z/unitsPerMetre},floor)+Vec{0,p.flat?FlatHeight:height,0};
    };
    out.a=at(p.blast?p.to:p.from,p.blast?p.toHeight:p.fromHeight);out.b=at(p.to,p.toHeight);
    out.blast=p.blast;out.flat=p.flat;
    if(p.blast){out.radius=std::min(profile->blast/unitsPerMetre,MaxRadius);out.impulse=profile->blastImpulse;}
    else {out.radius=p.flat?FlatRadius:profile->impulse>=Heavy?HeavyRadius:BoltRadius;out.impulse=profile->impulse;}
    return out;
}
// What a push does to a body whose centre is here and which reaches this far
// from it (its bounding radius): nothing, or an impulse along the missile's
// flight, or outward from the burst, lifted a little so it tips and tumbles.
struct Kick {bool hit=false;Vec impulse{};float distance=0;};
inline constexpr float BoltLift=.35f,BlastLift=.6f;
inline constexpr float EdgeShare=.25f; // of the impulse left at a burst's edge
inline Kick KickOn(const Push& push,Vec centre,float extent) {
    Kick k;
    if(!Valid(push)||!coordinates::Finite(centre)||!(extent>=0))return k;
    Vec a=push.a,b=push.b;
    if(push.flat){a.y=b.y=centre.y;}
    if(push.blast) {
        Vec out=centre-b;
        const float d=std::sqrt(math::Dot(out,out));
        if(d>push.radius+extent)return k;
        const float share=1-(1-EdgeShare)*std::clamp(d/push.radius,0.f,1.f);
        out=d>1e-4f?out*(1/d):Vec{0,1,0};
        k.hit=true;k.distance=d;
        k.impulse=math::Normalized(out+Vec{0,BlastLift,0})*(push.impulse*share);
        return k;
    }
    const Vec along=b-a;const float n=math::Dot(along,along);
    const float t=n>1e-9f?std::clamp(math::Dot(centre-a,along)/n,0.f,1.f):0.f;
    const Vec gap=centre-(a+along*t);
    const float d=std::sqrt(math::Dot(gap,gap));
    if(d>push.radius+extent)return k;
    Vec heading=math::Normalized(along);
    if(math::Dot(heading,heading)<.5f)heading=d>1e-4f?gap*(1/d):Vec{0,1,0}; // a missile standing still pushes away from itself
    k.hit=true;k.distance=d;
    k.impulse=math::Normalized(heading+Vec{0,BoltLift,0})*push.impulse;
    return k;
}
// The speed that impulse gives this mass, never more than a hard knock: a
// feather-light prop is not shot across the room, a chair barely shifts.
inline constexpr float MaxKickSpeed=5; // m/s
inline float KickSpeed(float impulse,float mass) {
    if(!(impulse>0)||!(mass>0)||!std::isfinite(impulse)||!std::isfinite(mass))return 0;
    return std::min(impulse/mass,MaxKickSpeed);
}
}
