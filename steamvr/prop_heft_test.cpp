// Props with weight (prop_heft.h): masses by kind and size, the weapon's
// striking mass and strength, the hand's tick or thud, and in the real Box3D
// world (dropped_bottles.cpp) what a light tap and a full swing do to a cup,
// a chair, a barrel and a sack, with weight on and off.
#include "dropped_bottles.h"
#include "prop_heft.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

using namespace framevr;
using namespace framevr::bottles;
using math::Vec;
namespace ph=framevr::propheft;
namespace {
constexpr float Upm=3.3f;
bool Near(float a,float b,float share){return std::fabs(a-b)<=share*std::fabs(b);}
float Mass(std::string_view kind,props::Material m,props::Shape s,Vec size){return ph::Mass(kind,m,s,size);}
// A prop on the floor at x=1 (origin at its base), of this kind, shape, material and size.
struct Kind {const char* name;props::Shape shape;props::Material material;Vec size;};
constexpr Kind Cup{"cup",props::Shape::Round,props::Material::Ceramic,{.08f,.1f,.08f}};
constexpr Kind Chair{"chair",props::Shape::Box,props::Material::Furniture,{.5f,1.f,.5f}};
constexpr Kind Barrel{"barrel",props::Shape::Round,props::Material::Wood,{.6f,.9f,.6f}};
constexpr Kind Sack{"sack",props::Shape::Box,props::Material::Sack,{.5f,.7f,.4f}};
Prop Make(const Kind& k,uint32_t entity,Vec at={1,0,0}) {
    Prop p;p.entity=entity;p.kind=1;p.shape=k.shape;p.material=k.material;p.p=at;
    p.bounds={{-k.size.x*.5f,0,-k.size.z*.5f},{k.size.x*.5f,k.size.y,k.size.z*.5f}};
    p.unitsPerMetre=Upm;p.mass=ph::Mass(k.name,k.material,k.shape,k.size);
    return p;
}
PropScan Scan(std::initializer_list<Prop> list) {
    PropScan s;s.session=7;s.serial=1;s.floor=0;s.unitsPerMetre=Upm;s.centre={0,0,0};
    for(const auto& p:list)s.props[s.count++]=p;
    return s;
}
// A weapon of this heft (weapon_heft.h Weight; <0 a fist) as the transport gives it.
Collider Weapon(Vec base,Vec tip,float weight) {
    Collider c;c.active=true;c.base=base;c.tip=tip;c.radius=.04f;
    c.mass=ph::StrikingMass(std::max(weight,0.f));c.strength=ph::PushStrength(std::max(weight,0.f));
    return c;
}
struct Outcome {float moved=0,tilt=0,fastest=0,felt=0;unsigned slips=0,knocks=0;float strikeSpeed=0,strikeMass=0,strikeWeapon=0;};
// A blade lying along z at height h above the prop's base, from 0.6 m before the
// prop moving +x at speed. A swing goes on through it and 0.6 m past; a tap goes
// 4 cm into its near face and comes back out at the same speed. Then 3 s to settle.
Outcome Hit(const Kind& k,float weight,float speed,bool tap,bool heft=true,float h=-1) {
    World world;world.Heft(heft);
    const Prop prop=Make(k,90);
    world.Props(Scan({prop}));
    assert(world.Frozen()==1);
    if(h<0)h=k.size.y*.6f;
    const float near=prop.p.x-k.size.x*.5f;
    float x=near-.6f;
    std::array<Collider,Colliders> weapons{};
    auto at=[&](float bx){return Weapon({bx,h,-.33f},{bx,h,.33f},weight);};
    double t=0;Outcome o;Vec last=prop.p;Quat q{0,0,0,1};
    std::array<Knock,props::MaxMoving> knocks{};
    auto frame=[&](){
        world.Weapons(weapons);t+=1/90.;world.Advance(t,{0,0,0});
        o.knocks+=world.Knocks(knocks);
        const auto s=world.Struck(0);
        const float felt=ph::Felt(s.speed,s.mass,s.weapon);
        if(felt>o.felt){o.felt=felt;o.strikeSpeed=s.speed;o.strikeMass=s.mass;o.strikeWeapon=s.weapon;}
        std::array<Moved,props::MaxMoving> m{};
        for(unsigned i=0,n=world.Moving(m);i<n;++i) {
            const Vec d=m[i].p-last;const float v=std::sqrt(math::Dot(d,d))*90;
            if(m[i].p.y>-1)o.fastest=std::max(o.fastest,v);
            last=m[i].p;q=m[i].q;
        }
    };
    const float end=tap?near+.04f:prop.p.x+k.size.x*.5f+.6f;
    while(x<end){x=std::min(end,x+speed/90);weapons[0]=at(x);frame();}
    if(tap)while(x>near-.6f){x-=speed/90;weapons[0]=at(x);frame();}
    weapons[0]={};
    for(int i=0;i<90*3;++i)frame();
    o.slips=world.Slips();
    const Vec d=last-prop.p;o.moved=std::hypot(d.x,d.z);
    o.tilt=2*std::acos(std::min(1.f,std::fabs(q.w)))*57.2958f;
    return o;
}
void Print(const char* what,const Outcome& o) {
    std::printf("  %-34s moved %6.3f m  tilt %5.1f deg  fastest %5.2f m/s  felt %.2f (%.1f m/s, %.1f kg vs %.1f kg)  slips %u\n",
                what,o.moved,o.tilt,o.fastest,o.felt,o.strikeSpeed,o.strikeMass,o.strikeWeapon,o.slips);
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);   // the table is read before a failed assert ends the run
    // ---- Masses by kind and size ----
    {
        const float cup=Mass("cup",props::Material::Ceramic,props::Shape::Round,{.08f,.1f,.08f});
        const float bottle=Mass("bottle",props::Material::Glass,props::Shape::Round,{.08f,.25f,.08f});
        const float plate=Mass("plate",props::Material::Ceramic,props::Shape::Round,{.24f,.025f,.24f});
        const float book=Mass("book",props::Material::Paper,props::Shape::Box,{.24f,.04f,.18f});
        const float skull=Mass("skull",props::Material::Bone,props::Shape::Box,{.16f,.2f,.2f});
        const float apple=Mass("apple",props::Material::Food,props::Shape::Ball,{.08f,.08f,.08f});
        const float pail=Mass("pail",props::Material::Metal,props::Shape::Round,{.28f,.26f,.28f});
        const float vase=Mass("vase",props::Material::Ceramic,props::Shape::Round,{.24f,.5f,.24f});
        const float urn=Mass("expansion_urn",props::Material::Ceramic,props::Shape::Round,{.6f,.9f,.6f});
        const float stool=Mass("stool",props::Material::Furniture,props::Shape::Box,{.36f,.5f,.36f});
        const float chair=Mass("chair",props::Material::Furniture,props::Shape::Box,{.5f,1.f,.5f});
        const float barrel=Mass("barrel",props::Material::Wood,props::Shape::Round,{.6f,.9f,.6f});
        const float crate=Mass("crate",props::Material::Wood,props::Shape::Box,{.6f,.6f,.6f});
        const float sack=Mass("sack",props::Material::Sack,props::Shape::Box,{.5f,.7f,.4f});
        const float bale=Mass("hay_bale",props::Material::Sack,props::Shape::Box,{.9f,.4f,.5f});
        const float pillow=Mass("pillow_blue",props::Material::Cloth,props::Shape::Box,{.5f,.16f,.36f});
        const float tablet=Mass("tablet",props::Material::Stone,props::Shape::Box,{.3f,.4f,.05f});
        std::printf("masses (kg): cup %.2f bottle %.2f plate %.2f book %.2f skull %.2f apple %.2f pail %.2f vase %.2f urn %.1f stool %.1f chair %.1f barrel %.1f crate %.1f sack %.1f hay %.1f pillow %.2f tablet %.1f\n",
                    cup,bottle,plate,book,skull,apple,pail,vase,urn,stool,chair,barrel,crate,sack,bale,pillow,tablet);
        // Plausible, each within what such a thing weighs.
        assert(cup>.1f&&cup<.4f);assert(bottle>.25f&&bottle<.9f);assert(plate>.3f&&plate<1.2f);
        assert(book>.6f&&book<2);assert(skull>.5f&&skull<2);assert(apple>.1f&&apple<.4f);assert(pail>1&&pail<4);
        assert(vase>2&&vase<8);assert(urn>20&&urn<80);assert(stool>1.5f&&stool<5);assert(chair>5&&chair<15);
        assert(barrel>35&&barrel<70);assert(crate>20&&crate<80);assert(sack>40&&sack<=60);assert(bale>15&&bale<40);
        assert(pillow>.3f&&pillow<3);assert(tablet>5&&tablet<20);
        // Light things to heavy ones.
        assert(cup<bottle&&bottle<plate&&plate<book&&book<pail&&pail<vase&&vase<chair&&chair<barrel&&chair<sack);
        // Size counts: a vessel's walls grow with its surface, a solid with its volume.
        const float smallVase=Mass("vase_small",props::Material::Ceramic,props::Shape::Round,{.12f,.25f,.12f});
        assert(smallVase<vase*.4f&&smallVase>vase*.1f);
        const float bigBook=Mass("book",props::Material::Paper,props::Shape::Box,{.48f,.08f,.36f});
        assert(Near(bigBook,book*8,.01f));
        const float smallBarrel=Mass("barrel",props::Material::Wood,props::Shape::Round,{.4f,.6f,.4f});
        assert(smallBarrel<barrel*.5f&&smallBarrel>5);
        // Names: a family by its stem, an unknown kind by its material, nothing unreadable.
        assert(ph::RecipeFor("pillow_red",props::Material::Cloth).density==60&&ph::RecipeFor("package_white",props::Material::Paper).density==300);
        assert(ph::RecipeFor("pillowcase",props::Material::Cloth).density==60);   // a family takes any name it begins
        assert(ph::RecipeFor("barrels",props::Material::Wood).build==ph::Build::Solid);   // an exact name does not
        assert(ph::RecipeFor("",props::Material::Stone).density==2500&&ph::RecipeFor("",props::Material(200)).build==ph::Build::Solid);
        assert(Mass("cup",props::Material::Ceramic,props::Shape::Round,{std::numeric_limits<float>::quiet_NaN(),.1f,.08f})==ph::LeastMass);
        assert(Mass("cup",props::Material::Ceramic,props::Shape::Round,{0,0,0})==ph::LeastMass);
        assert(Mass("chair",props::Material::Furniture,props::Shape::Box,{100,100,100})==15);   // its kind's most
        // The shapes' volumes are the bodies the world builds.
        assert(Near(ph::ShapeVolume(props::Shape::Box,{.1f,.2f,.3f}),.048f,1e-4f));
        assert(Near(ph::ShapeVolume(props::Shape::Round,{.1f,.2f,.1f}),3*.1f*.1f*.4f,1e-4f));
        assert(Near(ph::ShapeVolume(props::Shape::Ball,{.1f,.1f,.1f}),4.18879f*.001f,1e-4f));
    }
    // ---- The weapon ----
    {
        assert(ph::StrikingMass(0)==ph::LightestStrike&&ph::StrikingMass(1)==ph::HeaviestStrike);
        assert(ph::StrikingMass(.4f)>1.4f&&ph::StrikingMass(.4f)<1.8f);   // a one-handed sword
        assert(ph::StrikingMass(.8f)>3.2f&&ph::StrikingMass(.8f)<3.8f);   // a two-handed one
        assert(ph::StrikingMass(-3)==ph::LightestStrike&&ph::StrikingMass(std::numeric_limits<float>::quiet_NaN())==ph::LightestStrike);
        assert(ph::PushStrength(0)==ph::WeakestPush&&ph::PushStrength(1)==ph::StrongestPush&&ph::PushStrength(.4f)>ph::PushStrength(.2f));
        // A cup takes nearly all of a sword's speed; a barrel almost none, and stops it.
        const float sword=ph::StrikingMass(.4f),cup=Mass(Cup.name,Cup.material,Cup.shape,Cup.size),barrel=Mass(Barrel.name,Barrel.material,Barrel.shape,Barrel.size);
        assert(ph::Share(cup,sword)>.85f&&ph::Share(barrel,sword)<.05f);
        assert(ph::Resistance(cup,sword)<.15f&&ph::Resistance(barrel,sword)>.95f);
        assert(ph::Share(0,sword)==1&&ph::Resistance(barrel,0)==0);   // unknown: as an immovable weapon was
        // Movers: a rolling cup leaves a barrel frozen, a rolling bottle sets a cup going.
        assert(!ph::Bumps(cup,2,barrel)&&ph::Bumps(.4f,1,cup)&&ph::Bumps(0,1,barrel)&&ph::Bumps(cup,1,0));
    }
    // ---- The hand ----
    {
        const float sword=ph::StrikingMass(.4f);
        // Weight off (no weapon mass): the old light tick, whatever was struck.
        auto old=ph::Thud(3,50,0);
        assert(old.count==1&&std::fabs(old.steps[0].sample.amplitude-.18f)<1e-5f&&old.steps[0].sample.milliseconds==15&&old.steps[0].sample.frequency==0);
        assert(ph::Thud(30,.2f,0).steps[0].sample.amplitude==.35f&&ph::Thud(0,1,1).count==0);
        assert(ph::Thud(3,50,0,true).steps[0].sample.milliseconds==15);   // short taps leave the old tick alone
        // A cup: a light tick, a little stronger for a full swing.
        const auto tap=ph::Thud(1.5f,.17f,sword),swing=ph::Thud(5,.17f,sword);
        assert(tap.count==1&&swing.count==1&&tap.steps[0].sample.amplitude<swing.steps[0].sample.amplitude&&swing.steps[0].sample.amplitude<.3f);
        // A barrel: a hard knock and a low thud after it; harder for a full swing, longer for a heavier thing.
        const auto barrelTap=ph::Thud(1.5f,48,sword),barrelSwing=ph::Thud(5,48,sword),sackSwing=ph::Thud(5,60,sword),chairSwing=ph::Thud(5,9.8f,sword);
        assert(barrelSwing.count==2&&barrelSwing.steps[0].sample.amplitude>.8f&&barrelSwing.steps[1].at>barrelSwing.steps[0].sample.milliseconds);
        assert(barrelTap.steps[0].sample.amplitude<barrelSwing.steps[0].sample.amplitude&&barrelTap.steps[0].sample.amplitude>swing.steps[0].sample.amplitude);
        assert(barrelSwing.steps[1].sample.frequency<barrelSwing.steps[0].sample.frequency);
        assert(chairSwing.steps[1].sample.milliseconds<barrelSwing.steps[1].sample.milliseconds&&barrelSwing.steps[1].sample.milliseconds<=sackSwing.steps[1].sample.milliseconds);
        assert(chairSwing.steps[0].sample.amplitude<barrelSwing.steps[0].sample.amplitude);
        // A maul feels a barrel less than a fist does: it takes less of the heavier weapon's speed.
        assert(ph::Felt(5,48,ph::StrikingMass(1))<ph::Felt(5,48,ph::StrikingMass(0)));
        const auto shortTaps=ph::Thud(5,48,sword,true);
        assert(shortTaps.count==2&&shortTaps.steps[0].sample.milliseconds==haptics::ShortTapMs&&shortTaps.steps[1].sample.frequency==haptics::ShortTapHz);
        for(const auto& s:{tap,swing,barrelTap,barrelSwing,sackSwing})assert(s.category==haptics::Category::Melee&&s.Length()<200);
    }
    // ---- In the world: a light tap and a full swing ----
    {
        std::printf("weight on:\n");
        const auto cupTap=Hit(Cup,.4f,1,true),cupSwing=Hit(Cup,.4f,5,false);
        Print("cup, sword tap 1 m/s",cupTap);Print("cup, sword swing 5 m/s",cupSwing);
        const auto chairTap=Hit(Chair,.4f,1,true),chairSwing=Hit(Chair,.4f,5,false);
        Print("chair, sword tap",chairTap);Print("chair, sword swing",chairSwing);
        const auto barrelTap=Hit(Barrel,.4f,1,true),barrelSwing=Hit(Barrel,.4f,5,false);
        Print("barrel, sword tap",barrelTap);Print("barrel, sword swing",barrelSwing);
        const auto barrelFist=Hit(Barrel,-1,5,false),barrelMaul=Hit(Barrel,1,5,false),barrelLow=Hit(Barrel,.4f,5,false,true,.15f);
        Print("barrel, fist swing",barrelFist);Print("barrel, maul swing",barrelMaul);Print("barrel, sword swing low",barrelLow);
        const auto sackTap=Hit(Sack,.4f,1,true),sackSwing=Hit(Sack,.4f,5,false),sackMaul=Hit(Sack,1,5,false);
        Print("sack, sword tap",sackTap);Print("sack, sword swing",sackSwing);Print("sack, maul swing",sackMaul);
        std::printf("weight off:\n");
        const auto oldCup=Hit(Cup,.4f,5,false,false),oldBarrelTap=Hit(Barrel,.4f,1,true,false),oldBarrel=Hit(Barrel,.4f,5,false,false),oldSack=Hit(Sack,.4f,5,false,false);
        Print("cup, sword swing",oldCup);Print("barrel, sword tap",oldBarrelTap);Print("barrel, sword swing",oldBarrel);Print("sack, sword swing",oldSack);
        // A cup flies: off a tap, and fast and far off a full swing; the blade goes on through it.
        assert(cupTap.moved>.05f&&cupSwing.fastest>4&&cupSwing.moved>1&&cupSwing.slips==0&&cupSwing.knocks==1);
        // A chair is shoved and knocked over, but slower than a cup.
        assert(chairSwing.moved>chairTap.moved&&chairSwing.fastest<cupSwing.fastest*.6f&&chairSwing.fastest>1);
        // A barrel: a tap leaves it standing where it was; a full swing rocks or shifts it; struck low it
        // slides further; a maul knocks it over; a fist does less than a sword. Each full swing stops the
        // blade, which slips through it.
        assert(barrelTap.moved<.005f&&barrelTap.tilt<2&&barrelTap.slips==0);
        assert(barrelSwing.fastest>2*barrelTap.fastest&&(barrelSwing.moved>.005f||barrelSwing.tilt>2)&&barrelSwing.tilt<45);
        assert(barrelLow.moved>barrelSwing.moved&&barrelLow.moved>.02f);
        assert(barrelMaul.tilt>45&&barrelMaul.moved>barrelSwing.moved&&barrelMaul.fastest>barrelSwing.fastest);
        assert(barrelFist.fastest<barrelSwing.fastest&&barrelFist.moved<.005f);
        assert(barrelSwing.slips>=1&&barrelMaul.slips>=1);
        // A sack of grain hardly moves for a sword; a maul shoves it a little.
        assert(sackTap.moved<.005f&&sackSwing.moved<.01f&&sackMaul.moved>sackSwing.moved&&sackMaul.moved<.2f);
        // Light to heavy, the same full swing leaves each slower.
        assert(cupSwing.fastest>chairSwing.fastest&&chairSwing.fastest>barrelSwing.fastest&&barrelSwing.fastest>=sackSwing.fastest);
        // The hand: a cup a light tick, a heavy prop a hard knock (the swing it stopped).
        assert(cupSwing.felt<.3f&&barrelSwing.felt>.7f&&sackSwing.felt>.7f&&chairSwing.felt>cupSwing.felt&&barrelTap.felt<barrelSwing.felt);
        assert(barrelSwing.strikeWeapon>ph::StrikingMass(.4f)*2.5f);   // a full swing strikes with the body behind it
        // Weight off is what it always was: the barrel and the sack fly off a sword like the cup.
        assert(oldBarrel.fastest>5&&oldBarrel.moved>1&&oldSack.moved>1&&oldBarrelTap.moved>barrelTap.moved);
        assert(barrelSwing.fastest<oldBarrel.fastest*.1f&&std::fabs(oldCup.moved-cupSwing.moved)<1);
        assert(oldBarrel.strikeWeapon==0&&oldBarrel.felt<=.35f);   // and the old light tick
    }
    // ---- The bodies weigh what they should, and the switch changes them live ----
    {
        World world;
        world.Props(Scan({Make(Barrel,1,{1,0,0}),Make(Cup,2,{-1,0,0}),Make(Sack,3,{0,0,1.5f})}));
        const float barrel=ph::Mass(Barrel.name,Barrel.material,Barrel.shape,Barrel.size);
        assert(Near(world.Mass(1),barrel,.02f)&&Near(world.Mass(2),ph::Mass(Cup.name,Cup.material,Cup.shape,Cup.size),.03f));
        assert(world.Mass(99)==0);
        // Knocked over by a maul, the Box3D body has that mass (its twelve-sided hull, its inset).
        std::array<Collider,Colliders> weapons{};double t=0;
        for(int i=0;i<40;++i){weapons[0]=Weapon({.3f+i*.05f,.5f,-.33f},{.3f+i*.05f,.5f,.33f},1);world.Weapons(weapons);t+=1/90.;world.Advance(t,{0,0,0});}
        std::array<Moved,props::MaxMoving> m{};assert(world.Moving(m)>=1);
        assert(Near(world.Mass(1),barrel,.03f));
        world.Heft(false);assert(!world.Heft());
        assert(world.Mass(1)>100);   // the old density: solid wood
        world.Heft(true);assert(Near(world.Mass(1),barrel,.03f));
        assert(world.Slips()>=1&&world.Slips()==0);   // counted, then cleared
    }
    // ---- What moves a frozen heavy prop ----
    {
        // A bottle rolled into a frozen barrel bounces off it; with weight off it set the barrel going.
        for(bool heft:{true,false}) {
            World world;world.Heft(heft);
            world.Props(Scan({Make(Barrel,1,{1.2f,0,0})}));
            Release r;r.session=7;r.time=0;r.p={.3f,.1f,0};r.v={3,0,0};r.floor=0;r.unitsPerMetre=Upm;r.radius=.045f;r.code='h'|'p'<<8|'1'<<16;
            assert(world.Drop(r));
            std::array<Knock,props::MaxMoving> knocks{};unsigned k=0;double t=0;
            for(int i=0;i<90;++i){t+=1/90.;world.Advance(t,{0,0,0});k+=world.Knocks(knocks);}
            assert(heft?k==0&&world.Frozen()==1:k==1);
        }
        // A light spell bolt through a barrel leaves it; through a cup it sets it going. Weight off, both go.
        for(bool heft:{true,false}) {
            World world;world.Heft(heft);
            world.Props(Scan({Make(Barrel,1,{1,0,0}),Make(Cup,2,{1,0,2})}));
            for(float z:{0.f,2.f}) {
                spellimpacts::Push bolt;bolt.a={0,.5f,z};bolt.b={2,.5f,z};bolt.radius=spellimpacts::BoltRadius;bolt.impulse=spellimpacts::Light;
                if(z==2){bolt.a.y=bolt.b.y=.05f;}
                const auto pushed=world.Push(bolt,0);
                assert(pushed.props==(z==0&&heft?0u:1u));
            }
        }
    }
    // ---- The cost: twelve props moving and three weapons sweeping them, weight on and off ----
    {
        for(bool heft:{false,true}) {
            World world;world.Heft(heft);
            PropScan row=Scan({});
            for(unsigned i=0;i<props::MaxMoving;++i)row.props[row.count++]=Make(Cup,uint32_t(100+i),{float(i%4)*.3f,0,float(i/4)*.3f});
            world.Props(row);
            std::array<Collider,Colliders> weapons{};double t=0;
            const auto start=std::chrono::steady_clock::now();
            unsigned frames=0;
            for(int i=0;i<90*2;++i,++frames) {
                const float s=std::sin(float(i)*.15f);
                weapons[0]=Weapon({-.2f+s*.8f,.05f,-.3f},{-.2f+s*.8f,.05f,1},.4f);
                weapons[1]=Weapon({.5f,.05f,-.3f+s*.8f},{1.2f,.05f,-.3f+s*.8f},-1);
                weapons[2]=Weapon({.3f+s*.5f,.3f,.2f},{.3f+s*.5f,.5f,.2f},0);weapons[2].mass=ph::ShieldMass;weapons[2].strength=ph::ShieldPush;
                world.Weapons(weapons);t+=1/90.;world.Advance(t,{0,0,0});
            }
            const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/frames;
            std::array<Moved,props::MaxMoving> m{};
            std::printf("cost, weight %s: %.1f us a frame (%u steps, %u moving at the end)\n",heft?"on":"off",us,unsigned(world.Steps()),world.Moving(m));
        }
    }
    std::printf("prop-heft-test: ok\n");
    return 0;
}
