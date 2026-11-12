// Held items meet the world (held_world.h): the hands' frame in the world, the wall cells and the floor as boxes,
// what meets them, and the solver - a blade into a wall, along it across the cells' seams, into the floor, a corner,
// a shield, a fast swing, the cap letting through, easing back, never trapped, and what it costs.
#include "held_world.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace framevr;using namespace framevr::held;using math::Vec;
static int checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e=.0005f){return std::fabs(a-b)<=e;}
static bool Near(Vec a,Vec b,float e=.0005f){return Near(a.x,b.x,e)&&Near(a.y,b.y,e)&&Near(a.z,b.z,e);}
constexpr double Tick=1/90.;
constexpr float Units=4;   // the world's units per metre here: a cell (2 units) is 50 cm

// A patch around the origin: cells -16..15 each way.
static bottles::Patch Empty(){bottles::Patch p;p.session=1;p.x0=-16;p.z0=-16;return p;}
static void Set(bottles::Patch& p,int x,int z,std::array<uint32_t,bottles::PatchCells>* also=nullptr) {
    p.rows[z-p.z0]|=1u<<(x-p.x0);if(also)(*also)[z-p.z0]|=1u<<(x-p.x0);
}
// A wall along x, its cells at z = 2: its near face at z = 1 m, its far one at 1.5 m.
static bottles::Patch Corridor(){bottles::Patch p=Empty();for(int x=-16;x<16;++x)Set(p,x,2);return p;}
// The world around the items: the floor at y = 0 and the patch's walls (the hero's ground at height floorUnits).
static Surroundings Around(const bottles::Patch* p,Vec lo,Vec hi,float floorUnits=0) {
    Surroundings s;AddFloor(s,floorUnits/Units,lo,hi);
    if(p)AddWalls(s,*p,floorUnits,Units,lo,hi);
    return s;
}
static Shape Blade(Vec a,Vec b){Shape s;s.blade={a,b,BladeRadius};return s;}
// A sword level across in front of you, 1 m up, at depth z (its near side), x shifted.
static Shape Sword(float z,float x=0,float y=1){return Blade({x-.3f,y,z},{x+.35f,y,z});}
// A sword pointing ahead (+z) from a hand at z.
static Shape Thrust(float z,float x=0,float y=1){return Blade({x,y,z},{x,y,z+.65f});}
static Reach OneSword(int arm=0){Reach r;r.item[arm]=true;r.type[arm]=D2RVR_TYPE_SWORD;return r;}
struct Run {
    WorldSolver solver;WorldInput in;Surroundings world;double time=0;WorldOutput out;const bottles::Patch* patch=nullptr;
    explicit Run(const bottles::Patch* p,Reach gate=OneSword()):patch(p){in.gate=gate;}
    WorldOutput Step(const Shape& right,const Shape& left={}) {
        time+=Tick;in.time=time;in.item[0]=right;in.item[1]=left;
        Vec lo,hi;
        if(solver.Need(in,lo,hi)){world=Around(patch,lo,hi);in.world=&world;}else in.world=nullptr;
        out=solver.Step(in);return out;
    }
    Shape Drawn(const Shape& s,int arm=0) const {return Moved(s,out.push[arm]);}
};

int main(){
    {   // The hands' frame in the world: the skeleton hook's own mapping, round trip, and what is refused.
        const float model[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 10,2,20,1};   // the hero at (10, 2, 20) units, facing +z
        const Frame f=FrameOf(model,{0,7,0},0,4);
        Check(f.ok&&Near(f.unitsPerMetre,4)&&Near(f.origin,{2.5f,2.25f,5})&&Near(f.FloorY(),.5f),"the eye 7 units up the hero, in world metres");
        Check(Near(f.Point({0,0,-1}),{2.5f,2.25f,6})&&Near(f.Point({1,0,0}),{1.5f,2.25f,5}),
              "ahead of the eyes is where the camera looks (the model's forward at yaw 0); right is the model's -x");
        const float yaw=.7f,c=std::cos(1.1f),s=std::sin(1.1f);
        const float turned[16]={c*1.2f,0,-s*1.2f,0, 0,1.2f,0,0, s*1.2f,0,c*1.2f,0, -3,1,4,1};   // turned 1.1 rad, scale 1.2
        const Frame g=FrameOf(turned,{.1f,6.5f,-.2f},yaw,4.3f);
        Check(g.ok&&Near(g.unitsPerMetre,1.2f*4.3f,.001f),"the world's units per metre: the model's scale times its own");
        const Vec v{.3f,-.4f,-.5f};
        Check(Near(Length(g.Axis(v)),Length(v),.001f)&&Near(g.Back(g.Axis(v)),v,.001f),"a rotation: lengths kept, and back again");
        Check(Near(g.Axis({0,1,0}),{0,1,0},.001f),"up stays up");
        {   // The same point the skeleton hook draws a hand at (skeletons.cpp ControllerPoint: the eye plus the hand turned
            // into model space by ToModel and scaled by its units per metre; the model matrix, row vectors, to the world).
            const Vec eye{.1f,6.5f,-.2f},hand{.25f,-.3f,-.45f};const float upm=4.3f;
            const float cy=std::cos(yaw),sy=std::sin(yaw);
            const Vec m{-hand.x,hand.y,-hand.z},inModel{m.x*cy+m.z*sy,m.y,-m.x*sy+m.z*cy};
            const Vec p=eye+inModel*upm;
            const Vec world{p.x*turned[0]+p.y*turned[4]+p.z*turned[8]+turned[12],p.x*turned[1]+p.y*turned[5]+p.z*turned[9]+turned[13],
                            p.x*turned[2]+p.y*turned[6]+p.z*turned[10]+turned[14]};
            Check(Near(g.Point(hand),world*(1/g.unitsPerMetre),.0005f),"a hand lands where the skeleton hook draws it, in world metres");
            Check(Near(g.FloorY(),1/g.unitsPerMetre)&&Near(g.heroUnits,{-3,1,4}),"the floor: the hero's ground");
        }
        float bad[16];std::memcpy(bad,model,sizeof bad);bad[5]=1.5f;
        Check(!FrameOf(bad,{0,7,0},0,4).ok,"a stretched model is refused");
        std::memcpy(bad,model,sizeof bad);bad[5]=.8f;bad[6]=.6f;bad[9]=-.6f;bad[10]=.8f;
        Check(!FrameOf(bad,{0,7,0},0,4).ok,"a tilted one too");
        std::memcpy(bad,model,sizeof bad);bad[12]=NAN;
        Check(!FrameOf(bad,{0,7,0},0,4).ok&&!FrameOf(model,{0,7,0},0,0).ok&&!FrameOf(model,{0,7,0},0,20).ok,"unreadable, no scale, or past 12 units a metre");
    }
    {   // The wall cells as boxes: faces open only to open neighbours; objects and unknown rooms left out; doors kept.
        bottles::Patch p=Corridor();
        Set(p,3,0,&p.objects);    // a barrel's footprint in the corridor
        Set(p,-3,0,&p.unknown);   // no room loaded there
        Set(p,0,2,&p.doors);      // a closed door in the wall
        for(int x=-2;x<=2;++x)Set(p,x,3);   // the wall two cells thick here
        Surroundings s;AddWalls(s,p,0,Units,{-1.6f,0,-.5f},{1.6f,2,1.9f});
        Check(s.cells.patch&&s.cells.objects==1&&s.cells.unknown==1&&s.cells.doors==1,"a barrel and an unknown room left out, a closed door kept");
        const Block* wall=nullptr;const Block* door=nullptr;const Block* back=nullptr;
        for(unsigned i=0;i<s.count;++i){const Block& b=s.blocks[i];if(b.x==1&&b.z==2)wall=&b;if(b.x==0&&b.z==2)door=&b;if(b.x==1&&b.z==3)back=&b;
            Check(!(b.x==3&&b.z==0)&&!(b.x==-3&&b.z==0),"no box for the barrel or the unknown cell");}
        Check(wall&&wall->open==FaceBit(2,-1),"a wall cell between two others, a second row behind: only its near face is open");
        Check(door&&door->kind==Meets::Door&&wall->kind==Meets::Wall,"the door is told apart");
        Check(back&&back->open==FaceBit(2,1),"the back row opens behind");
        Check(Near(wall->box.center,{.75f,(bottles::WallHeight-WallBelow)*.5f,1.25f})&&Near(wall->box.right.x,.25f)&&Near(wall->box.out.z,.25f),
              "cell (1, 2) is 50 cm square at x .5-1 m, z 1-1.5 m, from half a metre under the floor up 6 m");
        // An elevated floor: the cells stand where the renderer shears that ground.
        Surroundings up;AddWalls(up,Corridor(),2,Units,{-1,0,0},{1,2,2});
        const float shift=2*coordinates::GroundShear/Units;
        bool found=false;
        for(unsigned i=0;i<up.count;++i)if(up.blocks[i].x==0&&up.blocks[i].z==2){found=true;Check(Near(up.blocks[i].box.center.z,1.25f+shift)&&Near(up.blocks[i].box.center.x,.25f+shift),"sheared by the floor's height");}
        Check(found,"the cell is there on an elevated floor");
        Surroundings floor;AddFloor(floor,.5f,{-1,0,-1},{1,2,1});
        Check(floor.count==1&&floor.blocks[0].kind==Meets::Floor&&floor.blocks[0].open==FaceBit(1,1)&&Near(floor.blocks[0].box.center.y+floor.blocks[0].box.up.y,.5f),"the floor: its top at the ground, pushing out only upward");
    }
    {   // What meets the world: a weapon in either hand and the shield, a lone weapon too; what keeps them off.
        Holding h;h.firstPerson=true;h.main=D2RVR_TYPE_SWORD;
        Reach r=DecideWorld(h);
        Check(r.Acts(0)&&!r.item[1]&&!r.why,"a lone sword meets the walls (the other hand empty)");
        h.shield=true;r=DecideWorld(h);
        Check(r.Acts(0)&&r.Acts(1)&&r.shield[1]&&!r.shield[0],"a sword and a shield: both");
        h.leftHanded=true;r=DecideWorld(h);
        Check(r.item[1]&&r.type[1]==D2RVR_TYPE_SWORD&&r.shield[0],"left-handed: the sword in the left hand, the shield on the right arm");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_UNARMED;h.shield=true;r=DecideWorld(h);
        Check(r.Acts(1)&&!r.item[0],"a shield alone");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_CLAW;h.off=D2RVR_TYPE_CLAW;r=DecideWorld(h);
        Check(r.Acts(0)&&r.Acts(1),"two claws");
        h={};h.firstPerson=true;r=DecideWorld(h);Check(!r.Any()&&(r.why&EmptyHand),"empty hands: nothing");
        h.main=D2RVR_TYPE_BOW;r=DecideWorld(h);Check(r.why&Bow,"a bow: left alone");
        h.main=D2RVR_TYPE_STAFF;r=DecideWorld(h);Check(r.why&TwoHands,"a staff: left alone");
        h={};h.firstPerson=true;h.main=D2RVR_TYPE_SWORD;h.twoHands=true;r=DecideWorld(h);Check(r.why&TwoHands,"a two-handed grip");
        h.twoHands=false;h.world=false;r=DecideWorld(h);Check(r.why==WorldOff,"Weapons hit walls off");
        h.world=true;h.setting=false;r=DecideWorld(h);Check(!r.why,"Weapons collide off does not turn the walls off");
        h.menu=true;r=DecideWorld(h);Check(r.why&Menu,"a menu");
        h.menu=false;h.throwing=true;r=DecideWorld(h);Check(r.why&Throwing,"a throw armed");
        h.throwing=false;h.form=true;r=DecideWorld(h);Check(r.why&Form,"a Druid form");
        h.form=false;h.firstPerson=false;r=DecideWorld(h);Check(r.why&View,"not first person");
        char text[200];Reasons(WorldOff|NoFrame,text,sizeof text);Check(!std::strcmp(text,"walls setting off, no world frame"),"the new reasons for the log");
    }
    const bottles::Patch corridor=Corridor();
    {   // A blade pushed into a wall stops at its face and slides along it across the cells' seams.
        Run r(&corridor);
        float z=.75f;
        for(int i=0;i<20;++i){z+=.01f;r.Step(Sword(z));}   // 0.9 m/s toward the wall; its near side reaches 0.98
        Check(!r.out.touching[0]&&Near(r.out.push[0],{0,0,0}),"apart: no push");
        for(int i=0;i<5;++i){z+=.01f;r.Step(Sword(z));}    // 3 cm into the face (the blade's 3 cm radius in front)
        Check(r.out.touching[0]&&Near(r.out.push[0],{0,0,-(z+BladeRadius-1)},.001f),"pushed straight back out of the wall's face");
        Check(Near(r.Drawn(Sword(z)).blade.a.z+BladeRadius,1,.001f),"the drawn blade rests on the face");
        auto stats=r.solver.TakeStats();
        Check(stats.contacts[int(Meets::Wall)]==1&&stats.contacts[int(Meets::Floor)]==0&&stats.acting>0,"one wall touch counted");
        const float held=r.out.push[0].z;
        float worst=0;
        for(int i=0;i<60;++i){r.Step(Sword(z,.02f*i));worst=std::max(worst,std::fabs(r.out.push[0].x));
            Check(r.out.touching[0]&&Near(r.out.push[0].z,held,.001f),"sliding 1.2 m along the wall over its cells: held the same depth");}
        Check(worst<.0005f,"never caught at a seam between cells (no push along the wall)");
        Check(r.solver.TakeStats().contacts[int(Meets::Wall)]==0,"one surface: no new touches while sliding");
        // Pulled back: eased off, about 0.1 s, not snapped.
        r.Step(Sword(.6f,1.2f));
        Check(!r.out.touching[0]&&r.out.push[0].z<held*.6f&&r.out.push[0].z>held*.99f-.0001f,"pulled back: easing");
        for(int i=0;i<10;++i)r.Step(Sword(.6f,1.2f));
        Check(Near(r.out.push[0],{0,0,0},.002f),"back on the hand within about 0.1 s");
    }
    {   // A thrust ahead into the wall: the hand gives way along the face's normal by the tip's depth.
        Run r(&corridor);
        float z=0;r.Step(Thrust(z));
        for(int i=0;i<40;++i){z+=.01f;r.Step(Thrust(z));}   // the tip's front to 1.08: 8 cm in
        Check(r.out.touching[0]&&Near(r.out.push[0].z,-(z+.65f+BladeRadius-1),.001f)&&Near(r.out.push[0].x,0)&&Near(r.out.push[0].y,0),"a thrust: held back by the tip's depth");
    }
    {   // The floor: a blade scraped down into it slides on it, quietly; a stab clanks in that hand only.
        Run r(nullptr);
        float y=.3f;
        for(int i=0;i<10;++i){y-=.003f;r.Step(Blade({0,y,.3f},{0,y-.25f,.6f}));}   // 0.27 m/s down
        Check(r.out.touching[0]&&Near(r.out.push[0],{0,-(y-.25f-BladeRadius),0},.001f)&&!r.out.clankArms,"scraping the floor: held on it, silent");
        for(int i=0;i<20;++i)r.Step(Blade({.01f*i,y,.3f},{.01f*i,y-.25f,.6f}));
        Check(r.out.touching[0]&&Near(r.out.push[0].x,0)&&Near(r.out.push[0].z,0),"dragged along it: pushed only up");
        Check(r.solver.TakeStats().contacts[int(Meets::Floor)]==1,"a floor touch counted");
        Run stab(nullptr,[]{Reach g;g.item[0]=g.item[1]=true;g.type[0]=g.type[1]=D2RVR_TYPE_DAGGER;return g;}());
        const Shape left=Blade({-.4f,1,0},{-.4f,1.3f,0});
        y=.5f;stab.Step(Blade({0,y,0},{0,y-.28f,0}),left);
        unsigned arms=0;float speed=0;
        for(int i=0;i<12;++i){y-=.025f;stab.Step(Blade({0,y,0},{0,y-.28f,0}),left);if(stab.out.clankArms){arms|=stab.out.clankArms;speed=stab.out.speed;}}
        Check(arms==1u&&speed>2&&speed<2.5f,"a 2.25 m/s stab into the floor clanks in the right hand only");
        Check(stab.out.clankOn==Meets::Floor||arms,"struck the floor");
    }
    {   // An inner corner: held out of both walls at once.
        bottles::Patch p=Corridor();for(int z=-16;z<16;++z)Set(p,2,z);   // a second wall along z, its near face at x = 1 m
        Run r(&p);
        float d=.2f;r.Step(Sword(d,d));
        for(int i=0;i<55;++i){d+=.01f;r.Step(Blade({d,1,d+.3f},{d+.3f,1,d}));}   // a blade across the corner, pushed 8 cm into each
        const Shape drawn=r.Drawn(Blade({d,1,d+.3f},{d+.3f,1,d}));
        Check(r.out.touching[0]&&r.out.push[0].x<-.05f&&r.out.push[0].z<-.05f,"pushed back out of both walls");
        Check(drawn.blade.a.z+BladeRadius<=1.0011f&&drawn.blade.b.x+BladeRadius<=1.0011f,"the drawn blade out of both faces");
    }
    {   // An outer corner: a blade coming at the end of a wall from the side stays on that side, not pushed through.
        bottles::Patch p=Empty();for(int x=-16;x<=0;++x)Set(p,x,2);   // the wall ends at x = 0.5 m
        Run r(&p);
        float x=1.2f;
        for(int i=0;i<75;++i){x-=.01f;r.Step(Blade({x,1,1.1f},{x,1,1.4f}));}   // along the end, moving -x: 8 cm past it
        Check(r.out.touching[0]&&r.out.push[0].x>.05f&&Near(r.out.push[0].z,0,.001f),"held off the wall's end face (+x)");
    }
    {   // A fast swing into the wall within one pose: held at the face it came from, not pushed on through.
        Run r(&corridor);
        r.Step(Sword(.85f));r.Step(Sword(.86f));
        r.Step(Sword(1.3f));   // 44 cm in one pose (40 m/s): past the wall's middle, nearer its far face
        Check(r.out.touching[0]&&Near(r.out.push[0].z,-(1.3f+BladeRadius-1),.001f),"pushed back out of the face it came at, not on through");
        Check(r.solver.State(0)!=WorldSolver::Phase::Idle,"and it is not let off");
    }
    {   // The cap: pushed in deeper than Cap (60 cm), the item passes into the wall; the push eases out; clear, it is free.
        Run r(&corridor);
        float z=0;r.Step(Thrust(z));
        while(z+.65f+BladeRadius<1+Cap-.02f){z+=.01f;r.Step(Thrust(z));}
        Check(r.out.touching[0]&&r.out.push[0].z<-(Cap-.04f),"58 cm in: still held");
        z+=.04f;r.Step(Thrust(z));
        Check(!r.out.touching[0]&&r.solver.State(0)==WorldSolver::Phase::Passing&&r.solver.TakeStats().capped==1,"past the cap: let through");
        for(int i=0;i<20;++i)r.Step(Thrust(z));
        Check(Near(r.out.push[0],{0,0,0},.002f)&&r.solver.State(0)==WorldSolver::Phase::Passing,"held in the wall, the hand is back on its controller (not pinned)");
        for(int i=0;i<80&&r.solver.State(0)!=WorldSolver::Phase::Idle;++i){z-=.01f;r.Step(Thrust(z));}
        Check(r.solver.State(0)==WorldSolver::Phase::Idle&&z+.65f+BladeRadius<=1.0001f,"drawn back out of the wall: free again");
        for(int i=0;i<10;++i){z+=.01f;r.Step(Thrust(z));}
        Check(r.out.touching[0],"and stopped at the face again");
    }
    {   // Walking into a wall with the sword ahead (the wall comes at the still hand at 3 m/s), then away: never trapped.
        Run r(&corridor);
        float z=0;unsigned steps=0;r.Step(Thrust(z));
        while(r.solver.State(0)!=WorldSolver::Phase::Passing&&steps<200){z+=3*float(Tick);r.Step(Thrust(z));++steps;}
        Check(r.solver.State(0)==WorldSolver::Phase::Passing&&steps<60,"the push grows to the cap within a stride, then lets the sword in");
        for(int i=0;i<15;++i)r.Step(Thrust(z));
        Check(Length(r.out.push[0])<.01f,"standing at the wall, the drawn hand is back at the controller");
        for(int i=0;i<60;++i){z-=2*float(Tick);r.Step(Thrust(z));}
        Check(r.solver.State(0)==WorldSolver::Phase::Idle&&Near(r.out.push[0],{0,0,0}),"walked away: free, on the hand");
    }
    {   // Through to the far side (let through) and back from there: held on the far side, as at the shield's back.
        Run r(&corridor);
        float z=0;r.Step(Thrust(z));
        for(int i=0;i<160;++i){z+=.01f;r.Step(Thrust(z));}   // the whole blade past the wall's far face at 1.5 m
        Check(r.solver.State(0)==WorldSolver::Phase::Idle&&z>1.5f+BladeRadius,"beyond the wall: free");
        for(int i=0;i<10;++i)r.Step(Thrust(z));
        for(int i=0;i<10;++i){z-=.01f;r.Step(Thrust(z));}
        Check(r.out.touching[0]&&r.out.push[0].z>0,"coming back: held behind the far face");
    }
    {   // A shield against the wall: its box stops at the face.
        Reach g;g.item[1]=true;g.shield[1]=true;
        Run r(&corridor,g);
        auto shield=[](float z){Shape s;s.plate=true;s.box={{-.3f,1.1f,z},{.17f,0,0},{0,.22f,0},{0,0,.02f}};return s;};
        float z=.6f;r.Step({},shield(z));
        for(int i=0;i<40;++i){z+=.01f;r.Step({},shield(z));}
        Check(r.out.touching[1]&&Near(r.out.push[1],{0,0,-(z+.02f-1)},.001f)&&Near(r.out.push[0],{0,0,0}),"the shield's face held at the wall; the other hand untouched");
        // Turned edge-on into the wall, its corner leads.
        const float c=std::cos(.5f),s=std::sin(.5f);
        Shape turned=shield(1.1f);turned.box.right={.17f*c,0,.17f*s};turned.box.out={-.02f*s,0,.02f*c};
        r.Step({},turned);
        Check(r.out.touching[1]&&r.out.push[1].z<-(1.1f+.17f*s+.02f*c-1)+.002f,"tilted, pushed out by its deepest corner");
    }
    {   // A barrel's footprint is not a wall: the blade swings through it (the breakable takes the hit).
        bottles::Patch p=Empty();Set(p,0,2,&p.objects);
        Run r(&p);
        float z=.4f;for(int i=0;i<60;++i){z+=.01f;r.Step(Sword(z));Check(!r.out.touching[0],"no push from a barrel's cell");}
    }
    {   // A teleport: the items jump; nothing collides from the jump, and no clank.
        Run r(&corridor);
        r.Step(Sword(.5f));r.Step(Sword(.51f));
        r.Step(Sword(1.05f,3));   // 3 m away, in the wall
        Check(!r.out.touching[0]&&!r.out.clankArms,"after a jump the first pose only starts afresh");
        r.Step(Sword(1.051f,3));
        Check(r.out.touching[0]&&!r.out.clankArms,"then it is pushed out, silently");
    }
    {   // Not acting (a menu, the setting, untracked): no push, and a push in progress eases away.
        Run r(&corridor);
        float z=.9f;r.Step(Sword(z));for(int i=0;i<8;++i){z+=.01f;r.Step(Sword(z));}
        Check(r.out.touching[0],"touching");
        const float was=Length(r.out.push[0]);
        r.in.gate.why|=Menu;r.Step(Sword(z));
        Check(!r.out.touching[0]&&Length(r.out.push[0])<was&&Length(r.out.push[0])>was*.5f,"a menu: eased, not snapped");
        Check(r.solver.TakeStats().why&Menu,"noted for the log");
        r.in.gate.why=0;r.in.gate.armWhy[0]=Untracked;for(int i=0;i<12;++i)r.Step(Sword(z));
        Check(Near(r.out.push[0],{0,0,0},.002f)&&!r.out.touching[0],"untracked: none");
        Run off(&corridor,[]{Reach g=OneSword();g.why=WorldOff;return g;}());
        for(int i=0;i<30;++i){off.Step(Sword(.8f+.01f*i));Check(Near(off.out.push[0],{0,0,0}),"the setting off: never a push");}
    }
    {   // Two weapons, each pushed on its own; the clank only in the hand that struck.
        Reach g;g.item[0]=g.item[1]=true;g.type[0]=g.type[1]=D2RVR_TYPE_SWORD;
        Run r(&corridor,g);
        float z=.2f;unsigned arms=0;
        r.Step(Thrust(z,.3f),Thrust(.2f,-.3f));
        for(int i=0;i<12;++i){z+=.02f;r.Step(Thrust(z,.3f),Thrust(.2f,-.3f));arms|=r.out.clankArms;}
        Check(r.out.touching[0]&&!r.out.touching[1]&&Near(r.out.push[1],{0,0,0}),"the right one held, the left untouched");
        Check(arms==1u,"a 1.8 m/s thrust clanks the right hand only");
        Check(r.solver.TakeStats().clanks==1,"one clank counted");
    }
    {   // What it costs: a step with a sword and a shield in a corner of a 1-cell corridor (walls on three sides), the
        // sword striking in and out of the wall 1.4 times a second, the boxes gathered each step as the hook does.
        bottles::Patch p=Empty();
        for(int x=-16;x<16;++x){Set(p,x,2);Set(p,x,-1);}   // a corridor 1 m wide (z 0..1)
        for(int z=-16;z<16;++z)Set(p,2,z);                  // closed at x = 1 m
        Reach g;g.item[0]=g.item[1]=true;g.shield[1]=true;
        Run r(&p,g);
        auto shield=[](float x,float z){Shape s;s.plate=true;s.box={{x,1.1f,z},{.17f,0,0},{0,.22f,0},{0,0,.02f}};return s;};
        const int n=20000;double phase=0,worst=0;
        const auto began=std::chrono::steady_clock::now();
        for(int i=0;i<n;++i) {
            phase+=.1;const float swing=.35f*float(std::sin(phase));
            const auto at=std::chrono::steady_clock::now();
            r.Step(Blade({.3f+swing*.5f,1,.5f},{.5f+swing*.5f,1.2f,.95f+swing}),shield(-.2f,.5f+.25f*float(std::cos(phase))));
            worst=std::max(worst,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-at).count());
        }
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count()/n;
        const auto stats=r.solver.TakeStats();
        std::printf("held world: %.2f us a step, %.1f us the slowest (a sword striking a wall in a corridor's end and a shield; "
                    "%u wall cells, %u boxes in the last window; %u wall touches, %u clanks, %u capped in %d steps)\n",
            us,worst,stats.cells.walls,r.world.count,stats.contacts[int(Meets::Wall)],stats.clanks,stats.capped,n);
        Check(stats.contacts[int(Meets::Wall)]>100,"the sword met the wall again and again");
        Check(us<500,"a step costs well under a millisecond");
    }
    std::printf("held world: %d checks passed\n",checks);
    return 0;
}
