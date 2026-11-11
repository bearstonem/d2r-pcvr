// Held items collide (held_collision.h): closest points, the box's faces, the push and its cap, easing back,
// the split between two weapons, the clank's speed and wait, what keeps the pair from colliding, and the
// Assassin's crossed claws still guarding.
#include "held_collision.h"
#include "shield_guard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace framevr;using namespace framevr::held;using math::Vec;
static int checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e=.0005f){return std::fabs(a-b)<=e;}
static bool Near(Vec a,Vec b,float e=.0005f){return Near(a.x,b.x,e)&&Near(a.y,b.y,e)&&Near(a.z,b.z,e);}
static Vec Add(Vec a,Vec b){return a+b;}
static Capsule Moved(Capsule c,Vec d){c.a=c.a+d;c.b=c.b+d;return c;}

// A shield 40 cm ahead facing the player (its face +z), 40 x 50 cm, 4 cm thick; a sword across in front of it.
static Plate Shield(){return {{0,0,-.4f},{.2f,0,0},{0,.25f,0},{0,0,.02f}};}
static Capsule Sword(float z,float x=0){return {{.3f+x,0,z},{-.35f+x,0,z},BladeRadius};}
static Gate ShieldGate(){Holding h;h.firstPerson=true;h.shield=true;h.main=D2RVR_TYPE_SWORD;return Decide(h);}
static Gate TwoGate(){Holding h;h.firstPerson=true;h.main=D2RVR_TYPE_AXE;h.off=D2RVR_TYPE_SWORD;return Decide(h);}
constexpr double Tick=1/90.;

int main(){
    {   // Closest points of two segments.
        auto c=Segments({-1,0,0},{1,0,0},{0,-1,.5f},{0,1,.5f});
        Check(Near(c.distance,.5f)&&Near(c.s,.5f)&&Near(c.t,.5f)&&Near(c.p,{0,0,0})&&Near(c.q,{0,0,.5f}),"crossing segments meet at their middles");
        c=Segments({0,0,0},{1,0,0},{0,.2f,0},{1,.2f,0});
        Check(Near(c.distance,.2f),"parallel segments: their gap");
        c=Segments({0,0,0},{1,0,0},{2,0,0},{3,0,0});
        Check(Near(c.distance,1)&&Near(c.s,1)&&Near(c.t,0),"end to end: the nearest ends");
        c=Segments({0,0,0},{0,0,0},{-1,1,0},{1,1,0});
        Check(Near(c.distance,1)&&Near(c.t,.5f),"a point and a segment");
        c=Segments({0,0,0},{1,0,0},{.5f,.3f,-1},{.5f,.3f,1});
        Check(Near(c.distance,.3f)&&Near(c.p,{.5f,0,0}),"skew segments: the common perpendicular");
    }
    {   // The box: its faces, touching, the push out along a face.
        const Plate p=Shield();
        Check(Near(Face(p,2),{0,0,1})&&Near(Face(p,0),{1,0,0}),"unit face axes, out is the face");
        Check(!Touching(Sword(-.30f),p),"a sword 4 cm in front does not touch");
        Check(Touching(Sword(-.37f),p),"a sword at the face touches");
        Check(Near(Depth(Sword(-.37f),p,2,1),.02f),"it is pushed 2 cm back out of the face");
        Check(!Touching(Sword(-.47f),p)&&Across(Sword(-.47f),p,2),"a sword behind the shield lies across it");
        Check(!Across(Sword(-.47f,.9f),p,2),"one off to the side does not");
        // An edge: a blade passing diagonally beside a corner is apart along its own crossing, not the faces.
        const Capsule beside{{.15f,.45f,-.4f},{.45f,.15f,-.4f},.01f};
        Check(!Touching(beside,p),"a blade diagonal past the corner does not touch");
        int k=-1;float side=0;
        Check(Apart(Sword(-.30f),p,k,side)&&k==2&&side>0,"the sword lay off the front face");
        Least(Sword(-.39f),p,k,side);Check(k==2&&side>0,"the least push out is through the nearer face");
        Plate turned=p;turned.right={0,.2f,0};turned.up={-.25f,0,0};   // turned a quarter about its face
        Check(Near(Carried(p,turned,{0,.2f,-.38f}),{.2f,0,-.38f},.001f),"a turning box carries the point round with it");
        Check(Near(Carried(p,p,{.1f,.1f,-.38f}),{.1f,.1f,-.38f}),"a still box carries a point to itself");
        const Plate shifted{{0,0,-.5f},p.right,p.up,p.out};
        Check(Near(Carried(shifted,p,{.1f,.1f,-.38f}),{.1f,.1f,-.48f}),"a moved box carries the point with it");
    }
    {   // A rendered mesh's bounds as a box: the thin axis is the face; the half thickness is kept in bounds.
        float world[16]={2,0,0,0, 0,2,0,0, 0,0,2,0, 1,2,3,1};
        Plate p;
        Check(PlateFromBounds(world,{-.1f,-.2f,-.01f},{.1f,.2f,.03f},p),"bounds read");
        Check(Near(p.out,{0,0,.04f})&&Near(p.center,{1,2,3.02f}),"the thin axis is out, the centre through the bone");
        Check(Near(Length(p.right),.2f)&&Near(Length(p.up),.4f),"the other two half axes");
        Check(Near(Length(Thin({{},{.2f,0,0},{0,.2f,0},{0,0,.2f}}).out),MaxHalfThickness),"a deep curved shield is held to 4 cm");
        Check(Near(Length(Thin({{},{.2f,0,0},{0,.2f,0},{0,0,.001f}}).out),MinHalfThickness),"a flat one to 1 cm");
        float bad[16]={};bad[0]=NAN;
        Check(!PlateFromBounds(bad,{-1,-1,-1},{1,1,1},p)&&!PlateFromBounds(world,{0,0,0},{0,1,1},p),"unreadable bounds are refused");
    }
    {   // A shape held in a controller's frame, and carried by it.
        const Grip g{{.1f,-.2f,-.3f},{0,.7071068f,0,.7071068f}};
        const Capsule c{{.1f,.2f,.3f},{-.4f,0,.1f},.03f};
        const Capsule back=Out(g,In(g,c));
        Check(Near(back.a,c.a)&&Near(back.b,c.b),"in and out of a controller's frame");
        const Plate p=Shield(),q=Out(g,In(g,p));
        Check(Near(q.center,p.center)&&Near(q.out,p.out)&&Near(q.right,p.right),"the box too");
        const Capsule blade=Blade({0,0,0},{0,2,0},D2RVR_TYPE_DAGGER);
        Check(Near(blade.b,{0,.28f,0})&&Near(blade.radius,BladeRadius),"a dagger: 28 cm, as melee contact has it");
        Check(Near(BladeLength(D2RVR_TYPE_WAND),.38f)&&Near(BladeLength(D2RVR_TYPE_SWORD),.65f),"a wand 38 cm, a sword 65");
    }
    {   // Who collides: a one-handed weapon and a shield, or two weapons; why not.
        Holding h;h.firstPerson=true;h.shield=true;h.main=D2RVR_TYPE_SWORD;
        Gate g=Decide(h);
        Check(g.pair==Pair::WeaponShield&&g.Acts()&&g.arm[0]==0&&g.arm[1]==1&&g.type[0]==D2RVR_TYPE_SWORD,"a sword and a shield: the right hand's sword yields");
        h.leftHanded=true;g=Decide(h);
        Check(g.Acts()&&g.arm[0]==1&&g.arm[1]==0,"left-handed: the sword in the left hand, the shield on the right arm");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_CLAW;h.off=D2RVR_TYPE_CLAW;g=Decide(h);
        Check(g.pair==Pair::WeaponWeapon&&g.Acts(),"an Assassin's two claws collide");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_SWORD;g=Decide(h);
        Check(!g.Acts()&&(g.why&EmptyHand),"one weapon, the other hand empty: nothing");
        h={};h.firstPerson=true;h.shield=true;h.main=D2RVR_TYPE_UNARMED;g=Decide(h);
        Check(!g.Acts()&&(g.why&EmptyHand),"a fist and a shield: nothing");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_BOW;g=Decide(h);
        Check(!g.Acts()&&(g.why&Bow),"a bow: nothing, the hands draw and nock");
        h.main=D2RVR_TYPE_CROSSBOW;g=Decide(h);Check(!g.Acts()&&(g.why&Bow),"a crossbow likewise");
        h.main=D2RVR_TYPE_STAFF;g=Decide(h);Check(!g.Acts()&&(g.why&TwoHands),"a staff: two hands");
        h.main=D2RVR_TYPE_SPEAR;g=Decide(h);Check(!g.Acts()&&(g.why&TwoHands),"a spear");
        h.main=D2RVR_TYPE_POLEARM;g=Decide(h);Check(!g.Acts()&&(g.why&TwoHands),"a polearm");
        h={};h.firstPerson=true;h.shield=true;h.main=D2RVR_TYPE_SWORD;h.twoHands=true;g=Decide(h);
        Check(!g.Acts()&&(g.why&TwoHands),"a two-handed grip or a support hand on the shaft");
        h.twoHands=false;h.throwing=true;g=Decide(h);Check(!g.Acts()&&(g.why&Throwing),"a throw armed or a potion in hand");
        h.throwing=false;h.form=true;g=Decide(h);Check(!g.Acts()&&(g.why&Form),"a Druid form");
        h.form=false;h.menu=true;g=Decide(h);Check(!g.Acts()&&(g.why&Menu),"a menu: the hands are pointers");
        h.menu=false;h.wheel=true;g=Decide(h);Check(!g.Acts()&&(g.why&Wheel),"a wheel");
        h.wheel=false;h.leather=true;g=Decide(h);Check(!g.Acts()&&(g.why&Leather),"the leather menu");
        h.leather=false;h.firstPerson=false;g=Decide(h);Check(!g.Acts()&&(g.why&View),"not first person");
        h.firstPerson=true;h.setting=false;g=Decide(h);Check(!g.Acts()&&g.why==Off&&g.pair==Pair::WeaponShield,"the setting off");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_JAVELIN;h.shield=true;g=Decide(h);Check(g.Acts(),"a javelin and a shield, no throw armed");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_ORB;h.shield=true;g=Decide(h);Check(g.Acts(),"an orb and a shield");
        char text[200];Reasons(Menu|Bow|Throwing,text,sizeof text);
        Check(!std::strcmp(text,"menu, bow, throwing"),"the reasons for the log");
        Reasons(0,text,sizeof text);Check(!std::strcmp(text,"none"),"no reason");
        Check(!std::strcmp(PairName(Pair::WeaponWeapon),"weapon-weapon"),"pair names");
    }
    {   // Weapon against shield: the sword stops at the face and slides along it; only the sword's hand moves.
        Solver solver;Input in;in.gate=ShieldGate();in.plate=Shield();
        double time=0;Output out;
        auto step=[&](Capsule sword){time+=Tick;in.time=time;in.a=sword;out=solver.Step(in);return out;};
        float z=-.25f;
        for(int i=0;i<10;++i){z-=.01f;step(Sword(z));}   // 1 cm a step, about 1 m/s
        Check(!out.touching&&Near(out.push[0],{0,0,0}),"apart: no push");
        for(int i=0;i<3;++i){z-=.01f;step(Sword(z));}      // into the face: 3 cm deep
        Check(out.touching&&Near(out.push[0],{0,0,.03f},.001f)&&Near(out.push[1],{0,0,0}),"the sword is pushed out of the face; the shield stays");
        const Capsule drawn=Moved(Sword(z),out.push[0]);
        Check(Near(Depth(drawn,Shield(),2,1),0,.001f),"the drawn sword rests on the face");
        const auto stats=solver.TakeStats();
        Check(stats.contacts[0]==1&&stats.contacts[1]==0&&Near(stats.deepest,.03f,.001f),"one touch, 3 cm deepest");
        for(int i=0;i<10;++i)step(Sword(z,-.005f*i));      // across the face: it slides, held the same depth
        Check(out.touching&&Near(out.push[0].z,.03f,.001f)&&Near(out.push[0].x,0,.001f),"sliding along the face");
        // Pulled back clear: the push eases back over about 0.1 s, never at once.
        step(Sword(-.30f));
        Check(!out.touching&&out.push[0].z>.03f*.6f&&out.push[0].z<.03f,"pulled back: easing, not snapped");
        for(int i=0;i<8;++i)step(Sword(-.30f));            // 0.1 s
        Check(out.push[0].z<.03f*.06f,"back on the hand within about 0.1 s");
        step(Sword(-.30f));step(Sword(-.30f));
        Check(Near(out.push[0],{0,0,0}),"and exactly on it");
    }
    {   // A fast swing through the shield within one pose is caught and held at the face it came from.
        Solver solver;Input in;in.gate=ShieldGate();in.plate=Shield();
        in.time=0;in.a=Sword(-.30f);solver.Step(in);
        in.time+=Tick;in.a=Sword(-.31f);solver.Step(in);
        in.time+=Tick;in.a=Sword(-.45f);const auto out=solver.Step(in);   // 14 cm in one pose: behind the shield
        Check(out.touching&&Near(out.push[0],{0,0,.10f},.001f),"pushed back out the front, not on through");
        Check(solver.TakeStats().contacts[0]==1,"counted once");
    }
    {   // The cap: held deeper than 12 cm, they pass through; the push eases back and the next touch is afresh.
        Solver solver;Input in;in.gate=ShieldGate();in.plate=Shield();
        double time=0;Output out;
        auto step=[&](Capsule sword){time+=Tick;in.time=time;in.a=sword;out=solver.Step(in);return out;};
        float z=-.33f;step(Sword(z));
        while(z>-.46f){z-=.01f;step(Sword(z));}
        Check(out.touching&&out.push[0].z>.10f&&out.push[0].z<=Cap+.0001f,"11 cm deep: still held");
        z-=.02f;step(Sword(z));
        Check(!out.touching&&solver.State()==Solver::Phase::Passing&&out.push[0].z<Cap,"past the cap: let through, easing");
        Check(solver.TakeStats().capped==1,"counted as capped");
        for(int i=0;i<4;++i){z-=.03f;step(Sword(z));}
        Check(solver.State()==Solver::Phase::Idle,"clear behind the shield: free again");
        for(int i=0;i<20;++i)step(Sword(z));
        Check(Near(out.push[0],{0,0,0}),"the hand back on its controller");
        // Coming back toward its back face it now stays behind: the face it lay off is the back.
        for(int i=0;i<16;++i){z+=.01f;step(Sword(z));}   // to 1 cm into its back face
        Check(out.touching&&out.push[0].z<0,"against the back face: held behind");
    }
    {   // No push at all for a pair that may not act; an exclusion mid-touch eases the push back.
        Solver solver;Input in;in.gate=ShieldGate();in.plate=Shield();
        double time=0;Output out;
        auto step=[&](Capsule sword){time+=Tick;in.time=time;in.a=sword;out=solver.Step(in);return out;};
        step(Sword(-.33f));step(Sword(-.36f));step(Sword(-.38f));
        Check(out.touching&&out.push[0].z>.02f,"touching");
        const float was=out.push[0].z;
        in.gate.why|=Menu;step(Sword(-.38f));
        Check(!out.touching&&out.push[0].z<was&&out.push[0].z>was*.5f,"a menu opens: the push eases back");
        Check(solver.TakeStats().why&Menu,"the reason is noted for the log");
        // The menu closes long after, the sword moved far meanwhile: no clank from that jump, and it still stops.
        for(int i=0;i<30;++i)step(Sword(-.25f));
        in.gate.why=0;step(Sword(-.37f));
        Check(!out.clank&&!out.touching,"the first pose after the menu only starts afresh: no clank from the jump");
        step(Sword(-.373f));   // 0.3 cm: 0.27 m/s
        Check(out.touching&&!out.clank&&Near(out.push[0].z,.023f,.001f),"then it is held at the face, silently (barely moving)");
        Solver fresh;Input off;off.gate=ShieldGate();off.gate.why|=Off;off.plate=Shield();
        for(int i=0;i<5;++i){off.time=i*Tick;off.a=Sword(-.30f-.03f*i);out=fresh.Step(off);}
        Check(!out.touching&&Near(out.push[0],{0,0,0}),"the setting off: never a push");
        Solver gap;Input slow=in;slow.gate=ShieldGate();slow.time=0;slow.a=Sword(-.33f);gap.Step(slow);
        slow.time=1;slow.a=Sword(-.38f);out=gap.Step(slow);
        Check(!out.touching,"after a long gap the first pose only starts afresh");
    }
    {   // Weapon against weapon: the push is split, most to the hand that moved into the other.
        Solver solver;Input in;in.gate=TwoGate();
        const Capsule still{{0,-.2f,-.45f},{0,.45f,-.45f},BladeRadius};   // the left hand's, upright
        double time=0;Output out;
        auto step=[&](float z){time+=Tick;in.time=time;in.a=Sword(z);in.b=still;out=solver.Step(in);return out;};
        float z=-.30f;step(z);
        for(int i=0;i<8;++i){z-=.02f;step(z);}    // 2 m/s into the still blade, 1 cm past it
        Check(out.touching,"the blades touch");
        const float depth=out.depth;
        Check(out.push[0].z>0&&out.push[1].z<0,"pushed apart, each its own way");
        Check(Near(out.push[0].z-out.push[1].z,depth,.002f),"together they undo the overlap");
        Check(out.push[0].z>=out.push[1].z*-3,"the moving hand yields most");
        const Closest c=Segments(Add(in.a.a,out.push[0]),Add(in.a.b,out.push[0]),Add(still.a,out.push[1]),Add(still.b,out.push[1]));
        Check(Near(c.distance,2*BladeRadius,.002f),"the drawn blades rest a blade's width apart");
        Check(solver.TakeStats().contacts[1]==1,"a weapon-weapon touch");
        // Two hands meeting evenly split evenly.
        Solver even;Input both;both.gate=TwoGate();
        float za=-.30f,zb=-.50f;Output o;
        for(int i=0;i<12;++i){both.time=(i+1)*Tick;both.a=Sword(za);both.b={{0,-.2f,zb},{0,.45f,zb},BladeRadius};o=even.Step(both);za-=.01f;zb+=.01f;}
        Check(o.touching&&Near(o.push[0].z,-o.push[1].z,.002f),"met evenly: split evenly");
        // Crossed through within one pose: pushed back the way they came.
        Solver through;Input x;x.gate=TwoGate();
        x.time=0;x.a=Sword(-.40f);x.b=still;through.Step(x);
        x.time=Tick;x.a=Sword(-.49f);o=through.Step(x);
        Check(o.touching&&o.push[0].z>0,"a blade swung through the other is held on its side");
    }
    {   // The clank: a touch at speed, none for a resting touch, at most one each ClankGap.
        Check(ClankStrength(.4f)==0&&ClankStrength(ClankSpeed)==0&&Near(ClankStrength(FullClankSpeed),1)&&Near(ClankStrength(9),1),"strength by closing speed");
        Check(ClankStrength(1.75f)>.4f&&ClankStrength(1.75f)<.6f&&ClankStrength(NAN)==0,"half way, and unreadable");
        const auto soft=Clank(0),hard=Clank(1);
        Check(soft.category==haptics::Category::Melee&&soft.count==2&&Near(soft.steps[0].sample.amplitude,.3f)&&Near(hard.steps[0].sample.amplitude,1),"a two-step metal tap on the Melee strength");
        Check(hard.steps[1].at>hard.steps[0].sample.milliseconds,"the ring after the tap");
        Solver solver;Input in;in.gate=ShieldGate();in.plate=Shield();
        double time=0;Output out;unsigned clanks=0;float strength=0,speed=0;
        auto step=[&](float z){time+=Tick;in.time=time;in.a=Sword(z);out=solver.Step(in);if(out.clank){++clanks;strength=out.strength;speed=out.speed;}};
        // Resting: 0.3 m/s into the face. Silent.
        for(float z=-.33f;z>-.37f;z-=.003f)step(z);
        Check(out.touching&&clanks==0,"a slow touch is silent");
        step(-.30f);step(-.30f);
        // A strike at 2.7 m/s clanks, by its speed; a second one straight after does not.
        step(-.33f);step(-.36f);step(-.39f);
        Check(clanks==1&&speed>2.5f&&speed<2.9f&&strength>.6f&&strength<.95f,"a fast strike clanks, scaled by its 2.7 m/s");
        step(-.30f);step(-.33f);step(-.36f);step(-.39f);
        Check(clanks==1,"another within 0.12 s does not");
        for(int i=0;i<12;++i)step(-.30f);
        step(-.33f);step(-.36f);step(-.39f);
        Check(clanks==2,"after the wait it clanks again");
        Check(solver.TakeStats().clanks==2,"clanks counted for the log");
    }
    {   // The Assassin's crossed claws: held apart a blade's width, still within the claw guard's reach.
        Solver solver;Input in;
        Holding h;h.firstPerson=true;h.main=D2RVR_TYPE_CLAW;h.off=D2RVR_TYPE_CLAW;in.gate=Decide(h);
        const Vec rightGrip{.12f,-.3f,-.35f},leftGrip{-.12f,-.3f,-.35f};
        const Vec rightAlong=math::Normalized({-.6f,1,0}),leftAlong=math::Normalized({.6f,1,0});
        Output out;
        for(int i=0;i<=10;++i) {
            in.time=i*Tick;
            in.a=Blade(rightGrip+Vec{0,0,.05f-.01f*i},rightAlong,D2RVR_TYPE_CLAW);   // the right claw comes back across the left
            in.b=Blade(leftGrip,leftAlong,D2RVR_TYPE_CLAW);
            out=solver.Step(in);
        }
        Check(out.touching,"the claws meet");
        const Vec mainBlade=in.a.a+rightAlong*guard::ClawBlade+out.push[0],offBlade=in.b.a+leftAlong*guard::ClawBlade+out.push[1];
        guard::Pose off;off.at=1;off.unitsPerMetre=4;off.normal={0,0,1};off.eye={0,0,0};off.hand=leftGrip;off.center=offBlade;
        guard::Pose crossed;float apart=0;
        Check(guard::Crossed(off,offBlade,mainBlade,rightGrip,crossed,&apart),"crossed claws still guard");
        Check(apart<guard::ClawsTogether&&apart>=2*BladeRadius-.002f,"their blades a blade's width apart, within 32 cm");
    }
    std::printf("held collision: %d checks passed\n",checks);
    return 0;
}
