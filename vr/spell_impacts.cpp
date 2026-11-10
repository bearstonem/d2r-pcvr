// The hero's spell missiles break containers and push props, on the D2R
// 3.3.93787 layouts shared with physical arrows (vr/bow.cpp) and
// breakables.cpp. No game code is hooked here: the server's missile creation
// (vr/projectiles.cpp) and missile update (vr/bow.cpp) hooks call in.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "spell_impacts.h"
#include "breakables.h"
#include "combat.h"
#include "melee.h"
#include "projectile_rules.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/bow_flight.h"
namespace gamestate {uint64_t LocalPlayer();}
namespace impactvr {
namespace {
using namespace framevr::spellimpacts;
using framevr::math::Vec;
constexpr float ContainerReach=200; // world units around the hero: farther than any listed missile flies
constexpr float PropReach=48;       // world units around the hero where a passage can meet a prop or a bottle
constexpr unsigned MostBroken=8;    // containers one step may break
std::atomic<bool> enabled{true};
std::atomic<unsigned> tracked{0};
std::mutex mutex;        // followed, mailbox
Followed followed;       // game thread; the mutex only guards against a second game
Mailbox mailbox;
// A hand-cast spell's real flight this step (Flew), on the grid plane with its heights.
struct Flown {bool valid=false;Point from{},to{};float fromHeight=0,toHeight=0;};
thread_local Flown flown;
thread_local bool operating=false; // inside a container's native operation: an exploding barrel's own missiles are not spells
// The containers around the hero, listed a few times a second and shared by
// every missile stepping meanwhile (a Nova steps dozens; each guarded read of
// the game's memory is a system call). Containers do not move. Only ids and
// places are kept; breakables::OperateMissile finds and checks the object again.
constexpr uint64_t LookEvery=300; // ms
struct Around {
    uintptr_t game=0;uint64_t at=0;bool hero=false;
    Point player{};
    size_t count=0;
    std::array<breakables::Container,192> items{};
};
Around around; // game thread only
std::atomic<unsigned> followedTotal{0},brokenPath{0},brokenBlast{0},refused{0},bursts{0},posted{0};
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
bool PathPoint(uintptr_t unit,Point& out) {
    uintptr_t path=0;uint16_t p[4]{};
    if(!Read(unit+0x38,path)||!Read(path,p))return false;
    out={2*(p[1]+p[0]/65536.f),2*(p[3]+p[2]/65536.f)};return true;
}
bool MissilePoint(uintptr_t unit,uint32_t id,uint32_t missile,Point& out) {
    uint32_t h[3]{};
    return Read(unit,h)&&h[0]==3&&h[1]==missile&&h[2]==id&&PathPoint(unit,out);
}
Around& Look(uintptr_t game,uintptr_t player,uint64_t now) {
    auto& n=around;
    n.hero=PathPoint(player,n.player);
    if(n.hero&&n.game==game&&n.at&&now>=n.at&&now-n.at<LookEvery)return n;
    n.game=game;n.at=now;
    n.count=n.hero?breakables::Nearby(game,n.player.x,n.player.z,ContainerReach,n.items):0;
    return n;
}
// At most six lines a second; the next line shown says how many were not.
void Say(const char* text) {
    static uint64_t second=0;static unsigned lines=0,skipped=0;
    const auto now=GetTickCount64()/1000;
    if(now!=second){second=now;lines=0;}
    if(lines>=6){++skipped;return;}
    ++lines;
    char line[320];
    if(skipped)std::snprintf(line,sizeof line,"%s (%u more lines not shown)",text,skipped);
    else std::snprintf(line,sizeof line,"%s",text);
    skipped=0;
    framevr::logging::Write(line);
}
void Summary(uint64_t now) {
    static uint64_t at=0;
    if(at&&now>=at&&now-at<10000)return;
    at=now;
    const auto f=followedTotal.exchange(0),p=brokenPath.exchange(0),b=brokenBlast.exchange(0),r=refused.exchange(0),u=bursts.exchange(0),m=posted.exchange(0);
    if(!f&&!p&&!b&&!r&&!u&&!m)return;
    char line[288];std::snprintf(line,sizeof line,"Frame spell impact: ten seconds: spell missiles followed=%u (now %u), containers broken in their path=%u, by bursts=%u, "
        "native refused=%u, bursts=%u, passages posted for props and bottles=%u",f,tracked.load(),p,b,r,u,m);
    framevr::logging::Write(line);
}
// Break the containers this step's path or burst reaches.
void Break(uintptr_t game,uintptr_t player,const Profile& profile,Point from,Point to,bool blast) {
    auto& n=around;unsigned done=0;
    for(size_t i=0;i<n.count&&done<MostBroken;++i) {
        auto& c=n.items[i];
        if(!c.id)continue;
        const Point at{c.x,c.z};const float radius=ContainerRadius(c.sizeX,c.sizeY);
        if(blast?!BlastBreaks(profile,to,at,radius):!PathBreaks(profile,from,to,at,radius))continue;
        const uint32_t id=c.id;c.id=0; // one try: broken, or refused by the game
        operating=true;
        const bool broke=breakables::OperateMissile(game,player,id);
        operating=false;
        ++done;
        if(!broke)++refused;else if(blast)++brokenBlast;else ++brokenPath;
        char line[224];std::snprintf(line,sizeof line,"Frame spell impact: %s (missile %u) %s container %u at cell %.0f,%.0f: %s",
            profile.name,profile.missile,blast?"burst at":"passed",id,at.x*.5f,at.z*.5f,broke?"broken":"the game refused (already open, or the hero cannot act)");
        Say(line);
        // A native trap may have killed the hero or unloaded things: look again before the next one.
        uintptr_t liveGame=0,livePlayer=0;
        if(!melee::LocalServer(liveGame,livePlayer)||liveGame!=game||livePlayer!=player)return;
    }
}
void Post(uint64_t session,uint64_t now,uint32_t missile,Point from,Point to,float fromHeight,float toHeight,bool flat,bool blast) {
    Passage p;p.session=session;p.at=now;p.missile=missile;p.from=from;p.to=to;
    p.fromHeight=fromHeight;p.toHeight=toHeight;p.flat=flat;p.blast=blast;
    {std::lock_guard lock(mutex);mailbox.Post(p);}
    ++posted;
}
}
void Configure(bool on) {
    if(enabled.exchange(on)==on)return;
    framevr::logging::Write(on?"Frame spell impact: spells break containers and push props and bottles: on":
        "Frame spell impact: off ([interactions] spell_impacts=0); spells pass containers and props as in the game");
}
bool Containers(const framevr::bowflight::Shot& shot) {
    return shot.kind!=framevr::bowflight::Kind::Spell||enabled.load(std::memory_order_relaxed);
}
void Created(uintptr_t game,uintptr_t params,uintptr_t unit) {
    if(!enabled.load(std::memory_order_relaxed)||operating||!game||!unit)return;
    uint32_t missile=~0u,local=~0u,type=~0u,ownerId=~0u,h[3]{};uintptr_t owner=0;
    if(!Read(params+0x20,missile)||!Of(missile)||!Read(params+8,owner))return;
    if(!combat::LocalPlayerId(local)||!Read(owner,type)||!Read(owner+8,ownerId)||!projectiles::SamePlayer(0,local,type,ownerId))return;
    Point at{};
    if(!Read(unit,h)||h[0]!=3||h[1]!=missile||!PathPoint(unit,at))return;
    std::lock_guard lock(mutex);
    followed.Add(game,h[2],missile,at,GetTickCount64());
    tracked.store(followed.Count(),std::memory_order_relaxed);
    ++followedTotal;
}
void Flew(const framevr::bowflight::Shot& shot,Vec from,Vec to,float stop) {
    if(shot.kind!=framevr::bowflight::Kind::Spell||!(shot.unitsPerMetre>=1&&shot.unitsPerMetre<=12)||!std::isfinite(stop))return;
    const Vec end=from+(to-from)*std::clamp(stop,0.f,1.f);
    const Vec a=framevr::bowflight::GridPoint(shot,from)*shot.unitsPerMetre,b=framevr::bowflight::GridPoint(shot,end)*shot.unitsPerMetre;
    flown={true,{a.x,a.z},{b.x,b.z},from.y-shot.player.y,end.y-shot.player.y};
}
Step::Step(uintptr_t g,uintptr_t u):game(g),unit(u) {
    flown={};
    if(!tracked.load(std::memory_order_relaxed))return;
    uint32_t h[3]{};
    if(!Read(unit,h)||h[0]!=3)return;
    id=h[2];missile=h[1];active=true;
}
Step::~Step() {flown={};}
int Step::Done(int result) {
    if(!active)return result;
    const auto now=GetTickCount64();
    Point from{},to{};
    {
        std::lock_guard lock(mutex);
        auto* e=followed.Find(game,id,missile,now);
        if(!e){tracked.store(followed.Count(),std::memory_order_relaxed);return result;}
        from=to=e->at;
        Point at{};
        if(MissilePoint(unit,id,missile,at))to=at;
        if(result==2)followed.Remove(*e);else {e->at=to;e->seen=now;}
        tracked.store(followed.Count(),std::memory_order_relaxed);
    }
    const auto* profile=Of(missile);
    uintptr_t liveGame=0,player=0;
    if(!profile||!enabled.load(std::memory_order_relaxed)||!melee::LocalServer(liveGame,player)||liveGame!=game)return result;
    const auto session=gamestate::LocalPlayer();
    const auto& seen=Look(game,player,now);
    if(!session||!seen.hero)return result;
    const Flown flew=flown;
    const Point hero=seen.player;
    const auto inReach=[&](Point p){return std::hypot(p.x-hero.x,p.z-hero.z)<=PropReach;};
    // The path. A hand-cast flight has already met containers in three
    // dimensions (the arrows' test, which ends it there); a native missile
    // on the grid plane passes over them, as the game has it pass.
    if(!flew.valid)Break(game,player,*profile,from,to,false);
    if(profile->impulse>0) {
        const Point a=flew.valid?flew.from:from,b=flew.valid?flew.to:to;
        if(std::hypot(b.x-a.x,b.z-a.z)<=LongestStep&&(inReach(a)||inReach(b)))
            Post(session,now,missile,a,b,flew.fromHeight,flew.toHeight,!flew.valid,false);
    }
    // The burst where it ended: on a flown path, the point of it nearest the missile's last cell.
    if(result==2&&profile->blast>0) {
        Point centre=to;float height=0;
        if(flew.valid) {
            const float t=Nearest(flew.from,flew.to,to);
            centre={flew.from.x+(flew.to.x-flew.from.x)*t,flew.from.z+(flew.to.z-flew.from.z)*t};
            height=flew.fromHeight+(flew.toHeight-flew.fromHeight)*t;
        }
        ++bursts;
        if(inReach(centre))Post(session,now,missile,centre,centre,height,height,!flew.valid,true);
        Break(game,player,*profile,centre,centre,true);
    }
    Summary(now);
    return result;
}
unsigned Take(uint64_t session,std::array<Passage,Mailbox::Most>& out) {
    std::lock_guard lock(mutex);
    return mailbox.Take(session,GetTickCount64(),out);
}
}
