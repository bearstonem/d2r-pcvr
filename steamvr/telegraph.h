#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

// Landing circles for ground skills: a circle at the point each cast was
// aimed at, placed the same way for every ground skill, Fire Blast included.
// Its fill grows from the centre outward over the skill's time (ForSkill) and then
// it fades. A skill whose missiles are known (Flights) is then timed by the
// game itself (attack speed, skill levels and gear change the throw, so
// nothing is hard-coded but the first guess): the launch missile re-times the
// circle from the moment it really leaves the hand, at the velocity the game
// gave that very missile; the landing missile ends it. The speed it flew is
// also remembered, in case the velocity cannot be read. Several can show at
// once. The game thread records; the render thread samples. Positions are
// native grid cells; no pointers are kept.
namespace framevr::telegraph {
inline constexpr float CellsPerSecond=14.f;   // first guess, measured: 31 cells in 2.4 s, 22 in 1.6 s
inline constexpr float RadiusCells=2.5f;      // approximate burst area
inline constexpr float ShockWebCells=6.f;    // Shock Web's ~9 bombs land 2-8 cells round the aim, most within 6
inline constexpr float MatchCells=12.f;       // a burst farther than this from every circle ends the oldest
inline constexpr uint64_t MinFlightMs=250,LateMs=1500,FadeMs=350,ThrowMs=280;
inline constexpr int MaxMarks=6;              // D2R_DepthFog.fx draws RingPos0..5
struct Mark {
    float x=0,y=0;            // target cell
    uint64_t cast=0,flight=0; // ms; flight is the expected time to land
    uint64_t landed=0;        // ms; 0 until the burst is seen
    uint32_t sequence=0;
    int element=0;            // spellaim::Element: the circle's color
    float radius=RadiusCells; // cells; for a strip, its half-width
    float dirX=0,dirY=0;      // a strip's direction on the grid (unit)
    float halfLength=0;       // cells; 0 = a circle
    int skill=-1;             // Skills.txt id, to match its missiles
    uint64_t launched=0;      // ms; when its launch missile appeared (0: not yet / none)
    float cells=0;            // the launch missile's distance to fly
};
// Skills whose missiles the game reports (projectiles.cpp): the one that leaves
// the hand and the one where it lands. Add a skill here once both are measured.
struct Flight {int skill;uint32_t launch,land;};
inline constexpr Flight Flights[]={
    {251,385,386},   // Fire Blast: "bomb in air", "bomb on ground"
    {256,388,389},   // Shock Web: its bombs in the air, the webs on the ground
};
inline constexpr int FlightCount=int(sizeof Flights/sizeof Flights[0]);
// The game's missile velocity (its set-velocity call) in cells per second: the
// direction has magnitude 4096 and (direction * velocity) >> 8 is 16.16 cells
// per tick, 25 ticks a second (bowflight::FlightSpeed). Fire Blast: missiles.txt
// Vel 12 -> 12 << 8, 75% at creation = 2304 -> 14.06 cells/s, as measured.
inline float CellsPerSecondOf(int nativeVelocity) {return nativeVelocity>0?float(nativeVelocity)*25.f/4096.f:0.f;}
inline bool SaneSpeed(float cellsPerSecond) {return std::isfinite(cellsPerSecond)&&cellsPerSecond>2.f&&cellsPerSecond<80.f;}
// The Flights entry a missile belongs to, and whether it is the landing one; -1 = none.
inline int FlightOf(uint32_t missile,bool& landing) {
    for(int i=0;i<FlightCount;++i) {
        if(Flights[i].launch==missile){landing=false;return i;}
        if(Flights[i].land==missile){landing=true;return i;}
    }
    return -1;
}
// Each ground skill's area and time, measured from the missiles the game
// created (2026-10-18, 5 casts each): Blizzard's shards land up to ~7 cells
// out for 4.1 s; Meteor's 18 fire patches within 5 cells at 2.65 s; Fire Wall's
// flames on a 10-cell line square to the cast; Fissure's vents within ~7 cells
// for 3.5 s. Hydra and traps are placed objects: a small spot.
// Fire Blast's first guess, until its bomb is seen: the bomb leaves the hand
// ThrowMs after the cast and flies CellsPerSecond (measured 2026-10-19, 11
// throws: 0.27-0.30 s, then 45 cells in 3.20 s, 11 in 0.78 s); cells is the
// throw's distance. Faster attacks shorten the wind-up, so the launch missile
// re-times it (Board::Launched). Shock Web the same (2026-10-21, 3 throws: its
// bombs leave 0.27-0.30 s after the cast at ~15 cells/s, 41-47 cells in 2.8-3.2 s,
// and land 2-8 cells round the aim, each a web for ~3.6 s).
struct Area {uint64_t showMs;float radius,halfLength;};
inline uint64_t FlightMs(float cells) {
    if(!std::isfinite(cells))return MinFlightMs;
    return std::max<uint64_t>(MinFlightMs,uint64_t(cells/CellsPerSecond*1000.f));
}
inline Area ForSkill(int skill,float cells=0) {
    switch(skill) {
        case 251: return {ThrowMs+FlightMs(cells),RadiusCells,0};   // Fire Blast: until the bomb lands
        case 256: return {ThrowMs+FlightMs(cells),ShockWebCells,0};   // Shock Web: thrown like Fire Blast, a wider spread
        case 59: return {4100,7.f,0};      // Blizzard
        case 56: return {2650,5.f,0};      // Meteor: fills until it lands
        case 51: return {700,.75f,5.f};    // Fire Wall: a strip across the cast
        case 234: return {3500,7.f,0};     // Fissure
        case 244: return {900,2.5f,0};     // Volcano (not measured yet)
        default: return {700,2.f,0};       // traps, Hydra
    }
}
struct Draw {bool on=false;float fill=0,alpha=0;bool burst=false;};
using Marks=std::array<Mark,MaxMarks>;
// Fill grows 0 -> 1 until the expected landing; the burst (or a missed burst
// event, LateMs after the expected landing) fades the circle out.
inline Draw Sample(const Mark& m,uint64_t now) {
    Draw d;
    if(!m.cast||now<m.cast)return d;
    const uint64_t end=m.landed?m.landed:m.cast+m.flight+LateMs;
    if(now>=end+FadeMs)return d;
    d.on=true;
    d.fill=m.flight?std::clamp(float(now-m.cast)/float(m.flight),0.f,1.f):1.f;
    if(now>=end){d.burst=m.landed!=0;d.fill=1;d.alpha=1.f-float(now-end)/float(FadeMs);}
    else d.alpha=1;
    return d;
}
inline bool AnyOn(const Marks& marks,uint64_t now) {
    for(const auto& m:marks)if(Sample(m,now).on)return true;
    return false;
}
class Board {
    std::mutex mutex;
    Marks marks{};
    uint32_t sequence=0;
public:
    // A ground skill cast at a point: its circle fills over showMs and then
    // fades; a new one takes a finished slot, else the oldest. A repeat at
    // nearly the same point within RepeatMs is the same cast.
    static constexpr uint64_t RepeatMs=350;
    bool Place(float x,float y,uint64_t now,uint64_t showMs,int element,float radius,float dirX=0,float dirY=0,float halfLength=0,int skill=-1) {
        std::lock_guard lock(mutex);
        for(const auto& m:marks)
            if(m.cast&&now>=m.cast&&now-m.cast<RepeatMs&&std::hypot(m.x-x,m.y-y)<1.5f)return false;
        Mark* slot=&marks[0];
        for(auto& m:marks) {
            if(!Sample(m,now).on){slot=&m;break;}
            if(m.cast<slot->cast)slot=&m;
        }
        showMs=std::max<uint64_t>(showMs,MinFlightMs);
        const float d=std::hypot(dirX,dirY);
        if(!(halfLength>0&&d>1e-4f))dirX=dirY=halfLength=0;else{dirX/=d;dirY/=d;}
        *slot={x,y,now,showMs,now+showMs,++sequence,element,radius,dirX,dirY,halfLength,skill};
        return true;
    }
    // A skill's launch missile appeared, flying from (ox, oy) to (tx, ty) at
    // live cells/s (the game's velocity for it; 0 = unknown, then the speed it
    // last flew): its circle (that skill's, nearest the target, not launched
    // yet) now fills until the missile should land, and waits for the landing
    // missile to end it. Returns the speed used (0: no circle).
    float Launched(int flight,float ox,float oy,float tx,float ty,uint64_t now,float live=0) {
        std::lock_guard lock(mutex);
        if(flight<0||flight>=FlightCount)return 0;
        Mark* m=Find(Flights[flight].skill,tx,ty,now,false);
        if(!m)return 0;
        const float cells=std::hypot(tx-ox,ty-oy),speed=SaneSpeed(live)?live:speeds[flight];
        m->launched=now;m->cells=std::isfinite(cells)?cells:0;
        m->flight=std::max<uint64_t>(MinFlightMs,(now-m->cast)+uint64_t(m->cells/speed*1000.f));
        m->landed=0;   // ends at the landing missile (or LateMs after the guess)
        return speed;
    }
    // The landing missile appeared at (x, y): its circle ends now, and the
    // speed it flew is blended into the next guess. Returns that circle (cast 0: none).
    Mark Landed(int flight,float x,float y,uint64_t now) {
        std::lock_guard lock(mutex);
        if(flight<0||flight>=FlightCount)return {};
        Mark* m=Find(Flights[flight].skill,x,y,now,true);
        if(!m)return {};
        if(m->launched&&now>m->launched+50&&m->cells>2) {
            const float flown=m->cells/(float(now-m->launched)*.001f);
            if(SaneSpeed(flown))speeds[flight]=speeds[flight]*.5f+flown*.5f;
        }
        m->landed=now;
        return *m;
    }
    float Speed(int flight){std::lock_guard lock(mutex);return flight>=0&&flight<FlightCount?speeds[flight]:CellsPerSecond;}
    // For the logs only: the showing circle nearest a point the game reports
    // (where a Fire Blast bomb burst), within MatchCells; cast 0 = none.
    Mark Nearest(float x,float y,uint64_t now) {
        std::lock_guard lock(mutex);
        const Mark* best=nullptr;float bestCells=MatchCells;
        for(const auto& m:marks) {
            if(!Sample(m,now).on)continue;
            const float cells=std::hypot(m.x-x,m.y-y);
            if(cells<=bestCells){bestCells=cells;best=&m;}
        }
        return best?*best:Mark{};
    }
    Marks Get() {std::lock_guard lock(mutex);return marks;}
private:
    std::array<float,FlightCount> speeds=[]{std::array<float,FlightCount> s{};s.fill(CellsPerSecond);return s;}();
    // The showing circle of that skill nearest (x, y), within MatchCells; launched: already in flight.
    Mark* Find(int skill,float x,float y,uint64_t now,bool launched) {
        Mark* best=nullptr;float bestCells=MatchCells;
        for(auto& m:marks) {
            if(m.skill!=skill||!Sample(m,now).on||(m.launched!=0)!=launched||(launched&&m.landed&&m.landed<=now))continue;
            const float cells=std::hypot(m.x-x,m.y-y);
            if(cells<=bestCells){bestCells=cells;best=&m;}
        }
        return best;
    }
};
inline Board& Current() {static Board board;return board;}
}
