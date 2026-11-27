// What the level props nobody touches cost the draw thread (dropped_bottles.h):
// a crowded room of them around a player who walks, swings and drops bottles
// near them without reaching one. Timed on this machine and printed; the
// assertions only check that nothing was touched. steamvr/PERFORMANCE.md keeps
// the numbers.
#include "dropped_bottles.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace framevr;
using namespace framevr::bottles;
using math::Vec;
namespace {
using Clock=std::chrono::steady_clock;
double Us(Clock::duration d) {return std::chrono::duration<double,std::micro>(d).count();}
constexpr float Upm=3.3f;
constexpr float Spacing=.3f,Reach=3,Aisle=.7f;
const Vec Centre{100,0,200};
// A 14 m room with a prop every 30 cm: on six tables (75 cm high, 1.2 x 0.8 m)
// cups, plates, books and apples; on the floor between them pails, books and
// apples. An aisle 1.4 m wide along +x down the middle is clear.
struct Placed {Prop prop;};
std::vector<Prop> Room() {
    std::vector<Prop> out;uint32_t entity=1000;
    for(int i=0;i<47;++i)for(int j=0;j<47;++j) {
        const float x=Centre.x-7+i*Spacing,z=Centre.z-7+j*Spacing;
        if(std::fabs(z-Centre.z)<Aisle)continue;
        // Tables every 2.4 m along x, either side of the aisle.
        const float tx=std::round((x-Centre.x)/2.4f)*2.4f+Centre.x,tz=z>Centre.z?Centre.z+1.6f:Centre.z-1.6f;
        const bool onTable=std::fabs(x-tx)<.6f&&std::fabs(z-tz)<.4f;
        Prop p;p.entity=++entity;p.unitsPerMetre=Upm;p.p={x,onTable?.75f:0,z};
        const int k=(i+j)%4;
        if(onTable) {
            p.support={false,.75f,tx-.6f,tz-.4f,tx+.6f,tz+.4f};
            if(k==0){p.kind=3;p.shape=props::Shape::Round;p.material=props::Material::Ceramic;p.bounds={{-.04f,0,-.04f},{.04f,.1f,.04f}};}
            else if(k==1){p.kind=4;p.shape=props::Shape::Round;p.material=props::Material::Ceramic;p.bounds={{-.1f,0,-.1f},{.1f,.03f,.1f}};}
            else if(k==2){p.kind=5;p.shape=props::Shape::Box;p.material=props::Material::Paper;p.bounds={{-.075f,0,-.1f},{.075f,.04f,.1f}};}
            else {p.kind=6;p.shape=props::Shape::Ball;p.material=props::Material::Food;p.bounds={{-.035f,0,-.035f},{.035f,.07f,.035f}};}
        } else {
            if(k<2){p.kind=7;p.shape=props::Shape::Round;p.material=props::Material::Wood;p.bounds={{-.12f,0,-.12f},{.12f,.3f,.12f}};}
            else if(k==2){p.kind=5;p.shape=props::Shape::Box;p.material=props::Material::Paper;p.bounds={{-.075f,0,-.1f},{.075f,.04f,.1f}};}
            else {p.kind=6;p.shape=props::Shape::Ball;p.material=props::Material::Food;p.bounds={{-.035f,0,-.035f},{.035f,.07f,.035f}};}
        }
        out.push_back(p);
    }
    return out;
}
// What the scan hands over around the player: the nearest 160 within 3 m.
PropScan ScanAt(const std::vector<Prop>& room,Vec player,uint64_t serial) {
    std::vector<const Prop*> near;
    for(const auto& p:room)if(std::hypot(p.p.x-player.x,p.p.z-player.z)<=Reach)near.push_back(&p);
    std::sort(near.begin(),near.end(),[&](const Prop* a,const Prop* b){
        return std::hypot(a->p.x-player.x,a->p.z-player.z)<std::hypot(b->p.x-player.x,b->p.z-player.z);});
    PropScan s;s.session=7;s.serial=serial;s.floor=0;s.unitsPerMetre=Upm;s.centre=player;
    for(const auto* p:near){if(s.count>=props::MaxSeen)break;s.props[s.count++]=*p;}
    return s;
}
// Swinging over the tables: a sword in the right hand, a fist on the left and a
// shield, all above everything (shoulder height), never reaching a prop.
std::array<Collider,Colliders> Swinging(Vec player,double t) {
    const float a=float(std::sin(t*6.283))*1.f,b=float(std::cos(t*5.1))*.8f;
    std::array<Collider,Colliders> w{};
    auto at=[&](float x,float y,float z){return player+Vec{x,y,z};};
    w[0]={true,at(.25f,1.35f,0),at(.25f+.8f*std::sin(a),1.45f,-.8f*std::cos(a)),.03f,1.6f,350};
    w[1]={true,at(-.25f+.1f*b,1.3f,-.3f),at(-.25f+.1f*b,1.3f,-.41f),.045f,0,0};
    w[2]={true,at(-.3f,1.25f,-.25f),at(-.3f,1.6f,-.25f),.06f,3,400};
    return w;
}
struct Timing {double total=0,worst=0;unsigned n=0;void Add(double us){total+=us;worst=std::max(worst,us);++n;} double Mean() const {return n?total/n:0;}};
}
int main() {
    const auto room=Room();
    const Vec start=Centre+Vec{-4,0,0};
    {
        const auto s=ScanAt(room,start,1);
        std::printf("prop rest cost: %zu props in the room, %u handed over within %.0f m of the player\n",room.size(),s.count,Reach);
        assert(s.count==props::MaxSeen);   // a crowded tavern: the scan is full
    }
    // 1. Entering the area: a new world given 160 props.
    Timing enter;
    for(int r=0;r<20;++r) {
        World world;const auto s=ScanAt(room,start,1);
        const auto t0=Clock::now();world.Props(s);enter.Add(Us(Clock::now()-t0));
        assert(world.Frozen()==props::MaxSeen);
    }
    // 2. The same props again (standing still, a scan twice a second).
    Timing again;
    {
        World world;world.Props(ScanAt(room,start,1));
        for(int r=0;r<200;++r) {
            const auto s=ScanAt(room,start,uint64_t(r+2));
            const auto t0=Clock::now();world.Props(s);again.Add(Us(Clock::now()-t0));
        }
        assert(world.Frozen()==props::MaxSeen);
    }
    // 3. Walking down the aisle at 1.5 m/s, swinging over the tables: a scan
    //    every half second, a frame at 90 Hz. Nothing is reached.
    Timing walkScans,walkFrames;
    uint64_t walkSteps=0;unsigned walkKnocks=0;
    {
        World world;double t=0;uint64_t serial=1;
        Vec player=start;
        world.Props(ScanAt(room,player,serial));
        std::array<Knock,props::MaxMoving> knocks{};
        for(int f=0;f<90*16;++f) {
            t+=1/90.;
            // Out and back along the aisle, 6 m each way.
            const double leg=std::fmod(t*1.5,12.);
            player=start+Vec{float(leg<6?leg:12-leg),0,0};
            if(f%45==0) {
                const auto s=ScanAt(room,player,++serial);
                const auto t0=Clock::now();world.Props(s);walkScans.Add(Us(Clock::now()-t0));
            }
            const auto weapons=Swinging(player,t);
            const auto t0=Clock::now();
            world.Weapons(weapons);world.Advance(t,player);
            walkFrames.Add(Us(Clock::now()-t0));
            walkKnocks+=world.Knocks(knocks);
        }
        walkSteps=world.Steps();
    }
    assert(walkKnocks==0&&walkSteps==0);
    // 4. Standing among them, swinging, for 10 s.
    Timing standFrames;
    {
        World world;double t=0;
        world.Props(ScanAt(room,start,1));
        for(int f=0;f<900;++f) {
            t+=1/90.;
            const auto weapons=Swinging(start,t);
            const auto t0=Clock::now();
            world.Weapons(weapons);world.Advance(t,start);
            standFrames.Add(Us(Clock::now()-t0));
        }
        assert(world.Steps()==0&&world.Frozen()==props::MaxSeen);
    }
    // 5. A bottle let go of in the aisle among them: it falls, rolls a little
    //    and lies there; the world steps while it is there. The same bottle
    //    with no props about, for comparison.
    Timing bottleProps,bottleAlone;
    uint64_t stepsProps=0,stepsAlone=0;
    for(int withProps=1;withProps>=0;--withProps) {
        World world;double t=0;
        if(withProps)world.Props(ScanAt(room,start,1));
        Release r;r.session=7;r.time=0;r.p=start+Vec{0,1.1f,0};r.v={.6f,0,.05f};r.floor=0;r.unitsPerMetre=Upm;r.radius=.045f;r.code='h'|'p'<<8|'1'<<16;
        assert(world.Drop(r));
        auto& timing=withProps?bottleProps:bottleAlone;
        std::array<Knock,props::MaxMoving> knocks{};unsigned knocked=0;
        for(int f=0;f<90*10;++f) {
            t+=1/90.;
            const auto weapons=Swinging(start,t);
            const auto t0=Clock::now();
            world.Weapons(weapons);world.Advance(t,start);
            timing.Add(Us(Clock::now()-t0));
            knocked+=world.Knocks(knocks);
        }
        assert(world.Count()==1&&knocked==0);
        (withProps?stepsProps:stepsAlone)=world.Steps();
    }
    std::printf("prop rest cost: entering with 160 props %.0f us (mean of %u, worst %.0f)\n",enter.Mean(),enter.n,enter.worst);
    std::printf("prop rest cost: the same 160 scanned again %.1f us (mean of %u, worst %.0f)\n",again.Mean(),again.n,again.worst);
    std::printf("prop rest cost: walking, a scan %.0f us (mean of %u, worst %.0f); a frame %.2f us (worst %.0f), %llu steps, %u knocks\n",
        walkScans.Mean(),walkScans.n,walkScans.worst,walkFrames.Mean(),walkFrames.worst,static_cast<unsigned long long>(walkSteps),walkKnocks);
    std::printf("prop rest cost: standing and swinging, a frame %.2f us (worst %.0f)\n",standFrames.Mean(),standFrames.worst);
    std::printf("prop rest cost: a bottle lying among 160 props, a frame %.2f us (%llu steps); alone %.2f us (%llu steps)\n",
        bottleProps.Mean(),static_cast<unsigned long long>(stepsProps),bottleAlone.Mean(),static_cast<unsigned long long>(stepsAlone));
    std::puts("prop rest cost: ok");
}
