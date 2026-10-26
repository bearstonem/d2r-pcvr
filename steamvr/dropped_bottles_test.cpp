#include "dropped_bottles.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

using namespace framevr;
using namespace framevr::bottles;
using math::Vec;
namespace {
constexpr float Upm=3.3f;
Release At(Vec p,Vec v={},float floor=0) {
    Release r;r.session=7;r.time=0;r.p=p;r.v=v;r.floor=floor;r.unitsPerMetre=Upm;r.radius=.045f;r.code='h'|'p'<<8|'1'<<16;
    return r;
}
// Run the world at a 90 Hz display rate; returns the time reached.
double Run(World& world,double from,double seconds,Vec player={},void (*each)(const World&)=nullptr) {
    double t=from;
    for(int i=0;i<int(seconds*90);++i){t+=1/90.;world.Advance(t,player);if(each)each(world);}
    return t;
}
Shown First(const World& world) {std::array<Shown,MaxBottles> s{};assert(world.Show(s)>=1);return s[0];}
// A 10 cm cup (origin at its base) at p, resting on what it is given.
Prop Cup(uint32_t entity,Vec p,props::Support support={}) {
    Prop c;c.entity=entity;c.kind=3;c.p=p;c.bounds={{-.04f,0,-.04f},{.04f,.1f,.04f}};c.support=support;c.unitsPerMetre=Upm;return c;
}
PropScan Scan(Vec centre,std::initializer_list<Prop> list,float floor=0) {
    PropScan s;s.session=7;s.serial=1;s.floor=floor;s.unitsPerMetre=Upm;s.centre=centre;
    for(const auto& p:list)s.props[s.count++]=p;
    return s;
}
std::vector<Moved> Moves(const World& world) {
    std::array<Moved,props::MaxMoving> m{};const unsigned n=world.Moving(m);return {m.begin(),m.begin()+n};
}
Collider Blade(Vec base,Vec tip,float radius=.03f) {Collider c;c.active=true;c.base=base;c.tip=tip;c.radius=radius;return c;}
}
int main() {
    // Merged walls cover each blocked cell exactly once.
    {
        Patch p;p.session=7;p.x0=100;p.z0=200;
        p.rows[2]=p.rows[3]=0b1110000;   // a 3x2 block
        p.rows[10]=1u<<31;                // a single cell at the edge
        p.rows[20]=~0u;                   // a whole row
        std::array<Rect,MaxRects> rects{};
        const unsigned n=MergeWalls(p,rects);
        assert(n==3);
        assert((rects[0]==Rect{104,202,3,2}));
        assert((rects[1]==Rect{131,210,1,1}));
        assert((rects[2]==Rect{100,220,32,1}));
        Patch check;check.x0=p.x0;check.z0=p.z0;
        for(unsigned i=0;i<n;++i)for(int z=0;z<rects[i].d;++z)for(int x=0;x<rects[i].w;++x) {
            const int cx=rects[i].x+x-p.x0,cz=rects[i].z+z-p.z0;
            assert(!((check.rows[cz]>>cx)&1u));check.rows[cz]|=1u<<cx;
        }
        assert(check.rows==p.rows);
        Patch board;for(int z=0;z<PatchCells;++z)board.rows[z]=z&1?0xaaaaaaaau:0x55555555u;
        assert(MergeWalls(board,rects)==MaxRects);
        Patch full;full.rows.fill(~0u);
        assert(MergeWalls(full,rects)==1&&rects[0].w==32&&rects[0].d==32);
    }
    // A cell's centre maps back to that cell on any floor (the floor's shear only).
    for(float floor:{-3.f,0.f,2.3f})for(int x:{-5,0,812})for(int z:{-1,4,1500}) {
        const auto c=CellAt(CellCorner(x+.5f,z+.5f,floor,Upm),floor,Upm);
        assert(c.x==x&&c.z==z);
    }
    // Releases outside a real hand's reach are refused.
    {
        assert(Valid(At({0,1,0})));
        auto r=At({0,1,0});r.session=0;assert(!Valid(r));
        r=At({0,1,0});r.code=0;assert(!Valid(r));
        r=At({std::numeric_limits<float>::quiet_NaN(),1,0});assert(!Valid(r));
        r=At({0,1,0},{MaxSpeed+1,0,0});assert(!Valid(r));
        r=At({0,1,0});r.w={0,MaxSpin+1,0};assert(!Valid(r));
        r=At({0,1,0});r.q={0,0,0,2};assert(!Valid(r));
        r=At({0,1,0});r.unitsPerMetre=0;assert(!Valid(r));
        r=At({0,-1,0});assert(!Valid(r));
        r=At({0,5,0});assert(!Valid(r));
        assert(Radius(0)==DefaultRadius&&Radius(1)==MaxRadius&&Radius(.001f)==MinRadius);
    }
    // A bottle let go of a metre up falls, bounces and comes to rest on the floor.
    {
        World world;assert(world.Ready());
        const Vec start{1000.4f,3.f,2000.6f};
        assert(world.Drop(At(start,{},2)));
        float lowest=10,bounce=0;double t=0;
        for(int i=0;i<90*4;++i) {
            t+=1/90.;world.Advance(t,start);
            const float y=First(world).p.y;
            if(y<lowest)lowest=y;
            else bounce=std::max(bounce,y-lowest);
        }
        assert(bounce>.01f); // it bounced, not just stopped dead
        const auto rest=First(world);
        // Lying on its side (radius above the floor) or standing (half its height).
        assert(rest.p.y>2+.045f-.01f&&rest.p.y<2+Height*.5f+.01f);
        assert(std::hypot(rest.p.x-start.x,rest.p.z-start.z)<.5f);
        assert(world.Count()==1);
        WallRequest want;assert(world.WallsWanted(want));
        assert(want.session==7&&want.floor==2&&want.unitsPerMetre==Upm&&std::hypot(want.center.x-rest.p.x,want.center.z-rest.p.z)<.01f);
        // Removed once its time is up.
        t=Run(world,t,Lifetime,start);
        assert(world.Count()==0);
        assert(!world.WallsWanted(want));
    }
    // Thrown along +x into a wall: it stops at the wall's face and comes back.
    {
        World world;
        const float floor=1.5f;
        const Vec start{500.2f,floor+1,800.7f};
        const auto c=CellAt(start,floor,Upm);
        Patch p;p.session=7;p.serial=1;p.x0=c.x-16;p.z0=c.z-16;
        for(int z=0;z<PatchCells;++z)p.rows[z]=1u<<20; // a wall column four cells ahead
        const float face=CellCorner(float(p.x0+20),float(c.z),floor,Upm).x;
        assert(face>start.x+1&&face<start.x+3);
        assert(world.Drop(At(start,{6,1,0},floor)));
        world.Walls(p);
        assert(world.Shapes()==1);
        float furthest=-1e9f;double t=0;
        for(int i=0;i<90*3;++i){t+=1/90.;world.Advance(t,start);furthest=std::max(furthest,First(world).p.x);}
        assert(furthest<face);           // never through the wall
        assert(furthest>face-.2f);       // it did reach it
        assert(First(world).p.x<furthest-.05f); // and came back off it
        // The same cells again build nothing new; another game's are ignored.
        world.Walls(p);assert(world.Shapes()==1);
        Patch other=p;other.session=8;other.serial=2;other.rows.fill(0);
        world.Walls(other);assert(world.Shapes()==1);
        // Opened (cleared) cells remove the wall.
        Patch open=p;open.serial=3;open.rows.fill(0);
        world.Walls(open);assert(world.Shapes()==0);
    }
    // At most MaxBottles; a different floor or a far player forgets them.
    {
        World world;
        for(unsigned i=0;i<MaxBottles+2;++i)assert(world.Drop(At({float(i)*.3f,1,0})));
        assert(world.Count()==MaxBottles);
        Run(world,0,.5);
        assert(world.Count()==MaxBottles);
        assert(world.Drop(At({0,2,0},{},1)));
        assert(world.Count()==1);
        Run(world,.5,.1,{Range+1,0,0});
        assert(world.Count()==0);
        assert(world.Drop(At({0,1,0})));
        world.Reset();assert(world.Count()==0);
        assert(!world.Drop(At({0,1,0},{MaxSpeed+1,0,0})));
        assert(world.Count()==0);
    }
    // A sword swung through a resting bottle knocks it away and is felt.
    {
        World world;const Vec start{3.4f,.5f,7.1f};
        assert(world.Drop(At(start)));
        double t=Run(world,0,3,start); // settled and asleep
        const auto rest=First(world);
        // A blade lying flat across the floor, swept along +x at 4 m/s through the bottle.
        auto blade=[&](float x){Collider c;c.active=true;c.base={x,rest.p.y,rest.p.z-.4f};c.tip={x,rest.p.y,rest.p.z+.4f};c.radius=.065f;return c;};
        std::array<Collider,Colliders> weapons{};
        float x=rest.p.x-.6f;
        weapons[0]=blade(x);world.Weapons(weapons);t+=1/90.;world.Advance(t,start);
        assert(world.Pushers()==1);
        assert(std::hypot(First(world).p.x-rest.p.x,First(world).p.z-rest.p.z)<.01f); // placed, not swept from nowhere
        float struck=0;
        for(int i=0;i<30;++i){x+=4/90.f;weapons[0]=blade(x);world.Weapons(weapons);t+=1/90.;world.Advance(t,start);struck=std::max(struck,world.Struck(0));}
        assert(First(world).p.x>rest.p.x+.15f); // pushed ahead of the blade
        assert(struck>1);                    // a hit worth a haptic tick
        assert(world.Struck(0)==0);          // reported once
        // Put away: the weapon leaves the world.
        weapons[0]={};world.Weapons(weapons);t+=1/90.;world.Advance(t,start);
        assert(world.Pushers()==0);
    }
    // A warp moves a weapon without sweeping it through the bottles between.
    {
        World world;const Vec start{0,.5f,0};
        assert(world.Drop(At(start)));
        double t=Run(world,0,3,start);
        const auto rest=First(world);
        auto post=[&](float x){Collider c;c.active=true;c.base={x,rest.p.y-.05f,rest.p.z};c.tip={x,rest.p.y+.5f,rest.p.z};c.radius=.05f;return c;};
        std::array<Collider,Colliders> weapons{};
        weapons[1]=post(rest.p.x-2);world.Weapons(weapons);t+=1/90.;world.Advance(t,start);
        weapons[1]=post(rest.p.x+2);world.Weapons(weapons);t+=1/90.;world.Advance(t,start);
        t=Run(world,t,.5,start);
        assert(std::hypot(First(world).p.x-rest.p.x,First(world).p.z-rest.p.z)<.01f);
        assert(world.Struck(1)==0);
        // A weapon whose pose stops arriving stays put rather than coasting on.
        weapons[1]=post(rest.p.x+2.05f);world.Weapons(weapons);t+=1/90.;world.Advance(t,start);
        weapons[1]=post(rest.p.x+2.15f);world.Weapons(weapons);t+=1/90.;world.Advance(t,start);
        t=Run(world,t,1,start); // same target: no further motion toward the bottle
        assert(std::hypot(First(world).p.x-rest.p.x,First(world).p.z-rest.p.z)<.01f);
        // Invalid colliders are refused.
        Collider bad=post(0);bad.tip={std::numeric_limits<float>::quiet_NaN(),0,0};assert(!Valid(bad));
        bad=post(0);bad.radius=0;assert(!Valid(bad));
        bad=post(0);bad.tip=bad.base+Vec{4,0,0};assert(!Valid(bad));
        assert(!Valid(Collider{}));
    }
    // A cup near a table's edge stays where the game put it until a sword
    // sweeps through it; then it goes over the edge, lands and is frozen again.
    {
        World world;
        const props::Support table{false,.75f,9.5f,19.5f,10.5f,20.5f};
        const Vec player{10,0,19};
        world.Props(Scan(player,{Cup(41,{10.4f,.75f,20},table),Cup(42,{9.8f,.75f,20},table)}));
        assert(world.Frozen()==2&&world.Supports()==1);
        double t=Run(world,0,1,player);
        assert(Moves(world).empty());
        // A blade lying along z at cup height, beside the far cup: nothing wakes.
        std::array<Collider,Colliders> weapons{};
        auto blade=[&](float x){return Blade({x,.8f,19.6f},{x,.8f,20.4f});};
        weapons[0]=blade(10.2f);world.Weapons(weapons);t+=1/90.;world.Advance(t,player);
        assert(Moves(world).empty());
        std::array<Knock,props::MaxMoving> knocks{};assert(world.Knocks(knocks)==0);
        // Swept along +x at 3 m/s through the near cup.
        float x=10.2f,struck=0;unsigned knocked=0;
        for(int i=0;i<30;++i) {
            x+=3/90.f;weapons[0]=blade(x);world.Weapons(weapons);t+=1/90.;world.Advance(t,player);
            struck=std::max(struck,world.Struck(0));
            const unsigned k=world.Knocks(knocks);
            for(unsigned j=0;j<k;++j){assert(knocks[j].entity==41&&knocks[j].slot==0&&knocks[j].kind==3);++knocked;}
        }
        assert(knocked==1&&struck>1);
        weapons[0]={};world.Weapons(weapons);
        // It falls past the edge to the floor, then rests and is reported once.
        bool rested=false;float lowest=10;Vec last{};
        for(int i=0;i<90*6&&!rested;++i) {
            t+=1/90.;world.Advance(t,player);
            for(const auto& m:Moves(world)) {
                assert(m.entity==41&&Finite(m.p));
                lowest=std::min(lowest,m.p.y);last=m.p;rested=m.resting;
            }
        }
        assert(rested&&lowest<.1f&&last.x>10.5f);
        t+=1/90.;world.Advance(t,player);
        assert(Moves(world).empty()&&world.Frozen()==2);
        // A later scan reports it where it lies; it is kept, not added twice.
        world.Props(Scan(player,{Cup(41,last),Cup(42,{9.8f,.75f,20},table)}));
        assert(world.Frozen()==2);
        // Out of reach and lost ones go.
        world.Props(Scan(player,{Cup(42,{9.8f,.75f,20},table)}));
        assert(world.Frozen()==1&&world.Supports()==1);
        auto lost=Scan(player,{Cup(42,{9.8f,.75f,20},table)});lost.lost=1;lost.lostEntities[0]=42;
        world.Props(lost);
        assert(world.Frozen()==0&&world.Supports()==0);
    }
    // A bottle rolled into a cup on the floor sets it going.
    {
        World world;const Vec player{0,0,0};
        world.Props(Scan(player,{Cup(51,{1,0,0})}));
        assert(world.Frozen()==1&&world.Supports()==0); // the floor holds it
        assert(world.Drop(At({.2f,.1f,0},{3,0,0})));
        double t=0;bool moved=false;
        std::array<Knock,props::MaxMoving> knocks{};unsigned k=0;
        for(int i=0;i<90&&!moved;++i){t+=1/90.;world.Advance(t,player);k+=world.Knocks(knocks);moved=!Moves(world).empty();}
        assert(moved&&k==1&&knocks[0].slot<0);
        assert(world.Count()==1);
    }
    // Moving props hold the world: another floor waits until they rest.
    {
        World world;const Vec player{0,0,0};
        world.Props(Scan(player,{Cup(61,{.5f,0,0})}));
        std::array<Collider,Colliders> weapons{};
        double t=0;
        for(int i=0;i<12;++i) {
            weapons[1]=Blade({.2f+i*.05f,.05f,-.3f},{.2f+i*.05f,.05f,.3f});
            world.Weapons(weapons);t+=1/90.;world.Advance(t,player);
        }
        assert(!Moves(world).empty());
        // The game took a moving one back (an area reload): it goes at once.
        {
            World other;other.Props(Scan(player,{Cup(71,{.5f,0,0})}));
            std::array<Collider,Colliders> w{};double s=0;
            for(int i=0;i<12;++i){w[0]=Blade({.2f+i*.05f,.05f,-.3f},{.2f+i*.05f,.05f,.3f});other.Weapons(w);s+=1/90.;other.Advance(s,player);}
            assert(!Moves(other).empty());
            auto lost=Scan(player,{});lost.lost=1;lost.lostEntities[0]=71;
            other.Props(lost);
            assert(Moves(other).empty()&&other.Frozen()==0);
        }
        world.Props(Scan(player,{},2)); // up the stairs
        assert(!Moves(world).empty()&&world.Frozen()==0);
        weapons[1]={};world.Weapons(weapons);
        t=Run(world,t,PropMoveLimit+1,player);
        assert(Moves(world).empty()&&world.Frozen()==1);
        world.Props(Scan(player,{Cup(62,{0,2,0})},2));
        assert(world.Frozen()==1); // the new floor's cup alone
        // Invalid props are never added.
        Prop huge=Cup(63,{0,2,1});huge.bounds.hi.y=2;
        Prop broken=Cup(64,{0,2,1});broken.p.x=std::numeric_limits<float>::quiet_NaN();
        world.Props(Scan(player,{huge,broken,Cup(62,{0,2,0})},2));
        assert(world.Frozen()==1);
        world.Props(PropScan{});
        assert(world.Frozen()==0);
    }
    // A row of books touching on a shelf: knocking one settles, without the
    // row waking itself over and over.
    {
        World world;const Vec player{0,0,0};
        const props::Support shelf{false,1,-.5f,-.2f,.5f,.2f};
        PropScan row=Scan(player,{});
        for(int i=0;i<8;++i) {
            Prop book=Cup(200+i,{-.21f+i*.06f,1,0},shelf);book.shape=props::Shape::Box;book.material=props::Material::Paper;
            book.bounds={{-.03f,0,-.1f},{.03f,.22f,.1f}};row.props[row.count++]=book;
        }
        world.Props(row);
        std::array<Collider,Colliders> weapons{};double t=0;
        std::array<Knock,props::MaxMoving> knocks{};unsigned knocked=0;
        for(int i=0;i<6;++i) {
            weapons[0]=Blade({-.21f,1.15f,-.4f+i*.05f},{-.21f,1.15f,-.1f+i*.05f},.02f);
            world.Weapons(weapons);t+=1/90.;world.Advance(t,player);knocked+=world.Knocks(knocks);
        }
        weapons[0]={};world.Weapons(weapons);
        assert(knocked>=1);
        for(int i=0;i<90*6;++i){t+=1/90.;world.Advance(t,player);knocked+=world.Knocks(knocks);}
        assert(Moves(world).empty()&&world.Frozen()==8);
        assert(knocked<=8); // each at most once: no wake-and-freeze loop
    }
    // With as many moving as allowed, a weapon still sets another going.
    {
        World world;const Vec player{0,0,0};
        PropScan many=Scan(player,{});
        for(unsigned i=0;i<props::MaxMoving;++i)many.props[many.count++]=Cup(300+i,{float(i)*.5f,0,0});
        many.props[many.count++]=Cup(399,{0,0,3});
        world.Props(many);
        std::array<Collider,Colliders> weapons{};double t=0;
        std::array<Knock,props::MaxMoving> knocks{};
        // Knock all the row hard, along z, one after another.
        for(unsigned i=0;i<props::MaxMoving;++i)for(int k=0;k<4;++k) {
            const float x=float(i)*.5f,z=-.3f+k*.15f;
            weapons[0]=Blade({x-.2f,.05f,z},{x+.2f,.05f,z});world.Weapons(weapons);t+=1/90.;world.Advance(t,player);world.Knocks(knocks);
        }
        assert(Moves(world).size()>=props::MaxMoving-2);
        weapons[0]=Blade({-.2f,.05f,2.7f},{.2f,.05f,2.7f});world.Weapons(weapons);t+=1/90.;world.Advance(t,player);
        weapons[0]=Blade({-.2f,.05f,3.f},{.2f,.05f,3.f});world.Weapons(weapons);t+=1/90.;world.Advance(t,player);
        bool last=false;
        for(unsigned i=0,n=world.Knocks(knocks);i<n;++i)last|=knocks[i].entity==399;
        assert(last);
        assert(Moves(world).size()<=props::MaxMoving);
    }
    // A cup on a plate rests on the plate's body: no plane is added under it,
    // so with the plate gone it falls to the floor when knocked.
    {
        World world;const Vec player{0,0,0};
        const props::Support table{false,.75f,-.5f,-.5f,.5f,.5f};
        props::Support onPlate{false,.77f};onPlate.onProp=true;
        Prop plate=Cup(81,{0,.75f,0},table);plate.bounds={{-.1f,0,-.1f},{.1f,.02f,.1f}};
        world.Props(Scan(player,{plate,Cup(82,{.45f,.77f,0},onPlate)}));
        assert(world.Frozen()==2&&world.Supports()==1); // the table alone
        auto gone=Scan(player,{Cup(82,{.45f,.77f,0},onPlate)});gone.lost=1;gone.lostEntities[0]=81;
        world.Props(gone);
        assert(world.Frozen()==1&&world.Supports()==0);
        std::array<Collider,Colliders> weapons{};double t=0;float lowest=10;
        for(int i=0;i<8;++i){weapons[0]=Blade({.3f+i*.03f,.82f,-.3f},{.3f+i*.03f,.82f,.3f});world.Weapons(weapons);t+=1/90.;world.Advance(t,player);}
        weapons[0]={};world.Weapons(weapons);
        for(int i=0;i<90*3;++i){t+=1/90.;world.Advance(t,player);for(const auto& m:Moves(world))lowest=std::min(lowest,m.p.y);}
        assert(lowest<.1f);
    }
    // No more than MaxMoving props move at once.
    {
        World world;const Vec player{0,0,0};
        PropScan many=Scan(player,{});
        for(unsigned i=0;i<props::MaxMoving+4;++i)many.props[many.count++]=Cup(100+i,{.5f+i*.1f,0,0});
        world.Props(many);
        assert(world.Frozen()==props::MaxMoving+4);
        std::array<Collider,Colliders> weapons{};double t=0;
        for(int i=0;i<40;++i) {
            weapons[0]=Blade({.3f+i*.06f,.05f,-.3f},{.3f+i*.06f,.05f,.3f},.05f);
            world.Weapons(weapons);t+=1/90.;world.Advance(t,player);
            assert(Moves(world).size()<=props::MaxMoving);
        }
    }
    // A thrown potion lands where it first touches anything.
    {
        auto flyUntilLanded=[](World& world,Vec player,double& t,Thrown& out){
            for(int i=0;i<90*3;++i){t+=1/90.;world.Advance(t,player);if(world.Tossed(out)&&out.landed)return true;}
            return false;
        };
        // Onto the floor: about where the arc lands (the body starts above the floor).
        {
            World world;const Vec start{10,1.3f,20},v{0,1,-3};
            assert(world.Toss(At(start,v),5));
            Thrown in;assert(world.Tossed(in)&&in.id==5&&!in.landed);
            double t=0;Thrown out;assert(flyUntilLanded(world,start,t,out));
            const float arc=(v.y+std::sqrt(v.y*v.y+2*9.81f*1.3f))/9.81f;
            assert(std::fabs(out.p.z-(start.z+v.z*arc))<.3f&&std::fabs(out.p.x-start.x)<.05f&&out.p.y<.2f);
            world.EndToss();assert(!world.Tossed(out));
        }
        // Into a wall: it stops at the face, off the floor.
        {
            World world;const float floor=0;const Vec start{500.2f,1.3f,800.7f};
            const auto c=CellAt(start,floor,Upm);
            Patch p;p.session=7;p.serial=1;p.x0=c.x-16;p.z0=c.z-16;
            for(int z=0;z<PatchCells;++z)p.rows[z]=1u<<19;
            const float face=CellCorner(float(p.x0+19),float(c.z),floor,Upm).x;
            assert(world.Toss(At(start,{8,1,0},floor),6));world.Walls(p);
            double t=0;Thrown out;assert(flyUntilLanded(world,start,t,out));
            assert(out.p.x<face&&out.p.x>face-.2f&&out.p.y>.5f);
        }
        // Through the thrower's own weapon, which never touches it.
        {
            World world;const Vec start{0,1.3f,0};
            assert(world.Toss(At(start,{0,0,-4}),7));
            std::array<Collider,Colliders> weapons{};
            weapons[0]=Blade({-.3f,1.3f,-.3f},{.3f,1.3f,-.3f},.05f);world.Weapons(weapons);
            double t=0;Thrown out;assert(flyUntilLanded(world,start,t,out));
            assert(out.p.z<-1&&out.p.y<.2f);
        }
        // Into a cup on the floor: it lands there and sets the cup going.
        {
            World world;const Vec player{0,0,0};
            world.Props(Scan(player,{Cup(91,{0,0,-.8f})}));
            assert(world.Toss(At({0,.25f,0},{0,0,-4}),8));
            double t=0;Thrown out;std::array<Knock,props::MaxMoving> knocks{};unsigned k=0;
            for(int i=0;i<90*2&&!(world.Tossed(out)&&out.landed);++i){t+=1/90.;world.Advance(t,player);k+=world.Knocks(knocks);}
            assert(out.landed&&out.p.z>-1&&out.p.z<-.5f);
            assert(k>=1);
        }
        // Refused for invalid releases.
        {
            World world;assert(!world.Toss(At({0,1,0},{MaxSpeed+1,0,0}),9));assert(!world.Toss(At({0,1,0}),0));
            Thrown out;assert(!world.Tossed(out));
        }
    }
    // A thrown trap, traced through the world at release.
    {
        const float r=.06f;
        World world;Vec hit{};
        assert(!world.Trace({0,1.5f,0},{5,0,0},r,3,hit)); // no world here yet
        assert(world.Prepare(7,0,Upm,{0,0,0}));
        // Over open floor: where the arc meets it, the ball a radius above.
        const float fall=std::sqrt(2*(1.5f-r)/9.81f);
        assert(world.Trace({0,1.5f,0},{5,0,0},r,3,hit)&&std::fabs(hit.x-5*fall)<.05f&&std::fabs(hit.z)<1e-3f&&std::fabs(hit.y-r)<.02f);
        // A wall in the way: at its face, still in the air.
        const auto c=CellAt({0,0,0},0,Upm);
        Patch p;p.session=7;p.serial=1;p.x0=c.x-16;p.z0=c.z-16;
        for(int z=0;z<PatchCells;++z)p.rows[z]=1u<<20;
        world.Walls(p);
        const float face=CellCorner(float(p.x0+20),float(c.z),0,Upm).x;
        assert(face<5*fall);
        assert(world.Trace({0,1.5f,0},{5,0,0},r,3,hit)&&hit.x<face&&hit.x>face-r-.05f&&hit.y>.3f);
        // A table top on the way down, from a prop's support.
        World table;assert(table.Prepare(7,0,Upm,{0,0,0}));
        table.Props(Scan({0,0,0},{Cup(95,{1.6f,.75f,0},props::Support{false,.75f,1,-.5f,2.5f,.5f})}));
        assert(table.Supports()==1);
        assert(table.Trace({0,1.5f,0},{3,0,0},r,3,hit)&&std::fabs(hit.y-(.75f+r))<.02f&&hit.x>1&&hit.x<2.5f);
        // The thrower's own weapon never stops it.
        World open;assert(open.Prepare(7,0,Upm,{0,0,0}));
        std::array<Collider,Colliders> weapons{};weapons[0]=Blade({.2f,1.3f,-.3f},{.2f,1.6f,.3f},.05f);
        open.Props(Scan({0,0,0},{Cup(96,{0,0,10})})); // something in the world, so the weapon is made
        open.Weapons(weapons);open.Advance(1/90.,{0,0,0});open.Advance(2/90.,{0,0,0});
        assert(open.Pushers()==1);
        assert(open.Trace({0,1.5f,0},{5,0,0},r,3,hit)&&hit.x>2);
        // Another floor while props move: the world stays where it is.
        assert(!World().Prepare(0,0,Upm,{0,0,0})&&!World().Prepare(7,0,0,{0,0,0}));
    }
    // A bottle leaving the hand passes through that hand's fist for a moment.
    {
        World world;const Vec start{2,.3f,2};
        std::array<Collider,Colliders> weapons{};
        weapons[0]=Blade({1.9f,.3f,2},{2.1f,.3f,2},.06f); // a fist right where the bottle is
        world.Weapons(weapons);
        assert(world.Drop(At(start)));
        double t=0;
        for(int i=0;i<18;++i){t+=1/90.;world.Advance(t,start);}  // 0.2 s
        const auto early=First(world);
        assert(std::fabs(early.p.x-start.x)<.01f&&std::fabs(early.p.z-start.z)<.01f);
    }
    // A pause or clock jump resumes in place instead of stepping seconds at once.
    {
        World world;assert(world.Drop(At({0,1.5f,0})));
        world.Advance(1,{});world.Advance(1+1/90.,{});
        const float before=First(world).p.y;
        world.Advance(11,{});
        assert(std::fabs(First(world).p.y-before)<.05f);
    }
    std::puts("dropped bottles: ok");
}
