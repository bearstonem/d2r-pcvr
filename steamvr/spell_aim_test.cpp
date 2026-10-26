#include "spell_aim.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    Check(spellaim::TrackedAim(84)&&spellaim::TrackedAim(101)&&spellaim::TrackedAim(78)&&spellaim::TrackedAim(88),"bone skills and Holy Bolt use the tracked casting hand");
    Check(!spellaim::TrackedAim(47)&&spellaim::GroundTarget(78)&&spellaim::GroundTarget(88)&&!spellaim::GroundTarget(84)&&!spellaim::GroundTarget(101),"Bone Wall and Bone Prison land like every ground skill; Bone Spear and Bone Spirit fly");
    Check(spellaim::BonePlacement(78)&&spellaim::BonePlacement(88)&&!spellaim::BonePlacement(84)&&!spellaim::BonePlacement(101),
          "with ground skills left to the game, Bone Wall and Prison keep their own floor placement");
    for(int yaw=0;yaw<360;yaw+=5)for(float floor:{-5.f,0.f,8.f}) {
        const float angle=yaw*.01745329252f;
        const math::Vec direction{std::cos(angle),-1,std::sin(angle)};
        // Identical relative hand poses on elevated/sheared floors must pick
        // the same native cell delta; world elevation is removed by caller.
        const math::Vec hero{200+floor*1.22474487f,floor,400+floor*1.22474487f};
        const math::Vec hand=hero+math::Vec{0,8,0};float x=0,y=0;
        Check(spellaim::GroundPoint(1000,2000,hand-hero,direction,x,y),"ground ray resolves at any heading/floor");
        Check(std::fabs(x-1000-4*std::cos(angle))<.001f&&std::fabs(y-2000-4*std::sin(angle))<.001f,"pitched hand sets distance and heading");
        framevr::spellaim::Cast cast{true,x,y,true};int tx=0,ty=0;
        Check(spellaim::ControllerPoint(cast,1000,2000,tx,ty)&&tx==int(x)&&ty==int(y),"the final ground command keeps placement instead of extending it 40 cells");
        Check(spellaim::GroundPoint(1000,2000,{0,8,0},{direction.x,0,direction.z},x,y)&&
              std::fabs(std::hypot(x-1000,y-2000)-6)<.001f,"level hand places bones six cells ahead");
        Check(spellaim::GroundPoint(1000,2000,{0,8,0},{direction.x,1,direction.z},x,y)&&
              std::fabs(std::hypot(x-1000,y-2000)-6)<.001f,"upward hand has a stable nearby fallback");
    }
    {
        float x=1,y=2;
        Check(spellaim::GroundPoint(100,200,{0,500,0},{1,-.1f,0},x,y)&&x==140&&y==200,"far floor intersection stays inside the native command range");
        Check(spellaim::GroundPoint(100,200,{.2f,.2f,0},{0,-1,0},x,y)&&x==102&&y==200,"near placement keeps two cells of clearance");
        x=1;y=2;
        for(const auto bad:{math::Vec{NAN,1,0},math::Vec{0,INFINITY,0},math::Vec{0,-1,0}})
            Check(!spellaim::GroundPoint(100,200,bad,{1,-1,0},x,y)&&x==1&&y==2,"invalid hand height cannot replace a ground target");
        Check(!spellaim::GroundPoint(100,200,{0,1,0},{},x,y),"zero aim is refused");
        Check(!spellaim::GroundPoint(65530,200,{0,8,0},{1,0,0},x,y),"world-edge placement is refused before integer conversion");
    }
    Check(spellaim::Directed(0,-1,8),"Teeth fan is directional without a primary missile field");
    Check(spellaim::Directed(0,36,0)&&spellaim::Directed(0,395,0),"primary missile behavior is independent of class/skill id");
    Check(spellaim::Directed(0,-1,19)&&spellaim::Directed(0,-1,10),"streams and initially aimed homing casts retain hand direction");
    for(int function:{22,28,56,60,63,68,123})Check(!spellaim::Directed(0,-1,function),"nova, ground, summon, corpse and buff behaviors retain native targeting");
    for(uint32_t flag:{1u<<4,1u<<20,1u<<24})Check(!spellaim::Directed(flag,12,8),"passive and unit/corpse-only skills never lose required targets");
    Check(!spellaim::Directed(0,-1,0)&&!spellaim::Directed(0,-1,999),"unknown behavior is not guessed to be projectile");
    spellaim::Cast context{};
    {
        spellaim::Scope cast(context,{true,.6f,.8f});
        {
            spellaim::Scope nested(context,{});
            Check(!context.directed,"nested non-projectile resolution cannot inherit target suppression");
        }
        Check(context.directed&&context.x==.6f,"nested return restores sampled direction for remaining fan resolution");
    }
    Check(!context.directed,"native targeting restored after the cast");
    for(int degree=-180;degree<=180;++degree) {
        const float angle=degree*3.14159265f/180;
        for(float pitch:{-.9f,0.f,.9f}) {
            const math::Vec direction{std::cos(angle)*std::cos(pitch),std::sin(pitch),std::sin(angle)*std::cos(pitch)};
            float x=0,y=0;
            Check(spellaim::Point(1200,1300,direction,x,y),"finite directional spell yields native aim point");
            const auto rounded=math::Normalized(math::Vec{float(int(x)-1200),0,float(int(y)-1300)});
            Check(rounded.x*std::cos(angle)+rounded.z*std::sin(angle)>.999f,"all horizontal aim angles stay within 2.6 degrees after native cell quantization");
        }
    }
    float x=5,y=7;
    for(int degree=-180;degree<=180;degree+=5) {
        const float angle=degree*3.14159265f/180;
        const spellaim::Cast cast{true,std::cos(angle),std::sin(angle)};
        // Native resolution can return the movement target without invoking
        // AttackPoint. Simulate forward, backward, strafing and idle results.
        for(float travel:{0.f,1.57079633f,3.14159265f,-1.57079633f})for(float distance:{0.f,8.f}) {
            const float playerX=1200.25f+std::cos(angle+travel)*distance;
            const float playerY=1300.75f+std::sin(angle+travel)*distance;
            x=playerX+std::cos(angle+travel)*3;y=playerY+std::sin(angle+travel)*3;
            Check(spellaim::Resolve(cast,playerX,playerY,x,y),"moving cast overrides every native resolver result");
            const auto actual=math::Normalized(math::Vec{x-playerX,0,y-playerY});
            Check(actual.x*cast.x+actual.z*cast.y>.99999f,"projectile aim is independent of forward, backward or sideways travel");
            int cx=int(playerX-3*cast.x),cy=int(playerY-3*cast.y);
            Check(spellaim::ControllerPoint(playerX,playerY,{cast.x,0,cast.y},cx,cy),"controller quick cast also replaces backward walking destination");
            const auto controller=math::Normalized(math::Vec{cx-playerX,0,cy-playerY});
            Check(controller.x*cast.x+controller.z*cast.y>.999f,"controller quick-cast integer target retains hand direction");
            Check(std::fabs(std::hypot(x-playerX,y-playerY)-40)<.001f,"cast point stays inside native command range");
        }
    }
    x=5;y=7;
    Check(!spellaim::Resolve({},1200,1300,x,y)&&x==5&&y==7,"non-projectile native targets are preserved");
    Check(spellaim::Resolve({true,23,42,true},1200,1300,x,y)&&x==23&&y==42,"shield contact keeps its exact point instead of projectile range");
    x=5;y=7;
    Check(!spellaim::Resolve({true,NAN,0},1200,1300,x,y)&&x==5&&y==7,"invalid cast aim preserves native result");
    Check(!spellaim::Resolve({true,1,0},NAN,1300,x,y)&&x==5&&y==7,"invalid player position preserves native result");
    Check(!spellaim::Point(0,0,{0,1,0},x,y)&&x==5&&y==7,"vertical aim cannot invent a horizontal direction");
    Check(!spellaim::Point(0,0,{NAN,0,1},x,y),"invalid direction is refused");
    int cx=12,cy=34;
    Check(!spellaim::ControllerPoint(NAN,0,{1,0,0},cx,cy)&&cx==12&&cy==34,"invalid controller position cannot change output");
    Check(!spellaim::ControllerPoint(65530,100,{1,0,0},cx,cy),"out-of-world controller target is refused before integer conversion");
    for(int type=0;type<18;++type)
        Check(spellaim::ControllerCast(type)==(type==7||type==8||type==9||type==10),"only native controller skill press/repeat owns a command");
    for(int degree=0;degree<360;++degree) {
        const float angle=degree*3.14159265f/180;
        Check(spellaim::ControllerPoint(4730.25f,4621.75f,{std::cos(angle),0,std::sin(angle)},cx,cy),"rotating hand yields a command target");
        // Native MapClick has reacquired a selected unit and its old point,
        // after our resolver returned. Cover left/right press and repeat flags.
        for(uint32_t flags:{5u,9u,6u,10u}) {
            spellaim::NativeCommand command{flags,0x1234,123,456,4730,4681};
            Check(spellaim::ApplyCommand({123,cx,cy},command),"final local command removes reacquired native target");
            Check(command.target==0&&command.flags==flags&&command.padding==0x1234&&command.actor==123,"only target fields change; native press/repeat and actor survive");
            const auto actual=math::Normalized(math::Vec{command.x-4730.25f,0,command.y-4621.75f});
            Check(actual.x*std::cos(angle)+actual.z*std::sin(angle)>.999f,"final command retains every intermediate heading across all four quadrants");
        }
    }
    spellaim::NativeCommand untouched{6,0,123,456,11,22};
    for(const auto aim:{spellaim::CommandAim{},spellaim::CommandAim{124,100,200},
                       spellaim::CommandAim{123,0,200},spellaim::CommandAim{123,100,65535}}) {
        Check(!spellaim::ApplyCommand(aim,untouched),"unowned, other actor and invalid command aim are refused");
        Check(untouched.target==456&&untouched.x==11&&untouched.y==22,"refused command keeps native targeting intact");
    }
    // Independently model the audited native command gate, not just the ray
    // formula. The previous 60-cell point passed only around the diagonals.
    const auto accepted=[](int px,int py,int tx,int ty) {
        return std::abs(tx-px)<=50&&std::abs(ty-py)<=50;
    };
    Check(!accepted(4734,4649,4794,4649)&&accepted(4734,4649,4776,4691),
          "old 60-cell aim reproduces cardinal rejection and diagonal acceptance");
    Check(!accepted(4734,4649,4790,4627)&&accepted(4734,4649,4769,4698),
          "observed stale versus server-created fireballs match native range gate");
    for(int degree=0;degree<360;++degree)for(float fraction:{0.f,.25f,.75f,.99f}) {
        const float angle=degree*3.14159265f/180;
        const float px=4734+fraction,py=4649+fraction;
        float tx=0,ty=0;
        Check(spellaim::Point(px,py,{std::cos(angle),0,std::sin(angle)},tx,ty),"valid hand aim produces a bounded target");
        for(int moveX:{-8,0,8})for(int moveY:{-8,0,8})
            Check(accepted(int(px)+moveX,int(py)+moveY,int(tx),int(ty)),
                  "all headings pass native validation with client/server movement margin");
    }
    Check(spellaim::GroundTarget(251)&&spellaim::GroundTarget(261)&&spellaim::GroundTarget(59)&&spellaim::GroundTarget(56)&&
          !spellaim::GroundTarget(36)&&!spellaim::GroundTarget(64)&&!spellaim::GroundTarget(-1),"traps, Blizzard and Meteor are ground-target skills; bolts and orbs are not");
    Check(spellaim::ElementOf(59)==spellaim::Cold&&spellaim::ElementOf(251)==spellaim::Fire&&spellaim::ElementOf(261)==spellaim::Lightning,
          "circle colors: Blizzard cold, Fire Blast fire, Charged Bolt Sentry lightning");
    {
        float gx=0,gy=0;
        const auto down45=math::Normalized(math::Vec{1,-1,0});
        Check(spellaim::GroundPoint({100,10,100},down45,0,.5f,50,50,gx,gy)&&std::fabs(gx-55)<.001f&&std::fabs(gy-50)<.001f,
              "45 degree aim lands one hand-height ahead, in native cells");
        // Elevated floors are sheared in render space; the cell must not be.
        const float shear=coordinates::GroundShear*20;
        Check(spellaim::GroundPoint({100+shear,30,100+shear},down45,20,.5f,50,50,gx,gy)&&std::fabs(gx-55)<.001f&&std::fabs(gy-50)<.001f,
              "raised floor removes render shear before cell conversion");
        const auto shallow=math::Normalized(math::Vec{0,-.01f,1});
        Check(spellaim::GroundPoint({100,10,100},shallow,0,.5f,50,50,gx,gy)&&std::fabs(std::hypot(gx-50,gy-50)-spellaim::AimDistance)<.01f&&
              gx==50&&gy>50,"a near-level aim is limited to the native command range on its heading");
        Check(spellaim::GroundPoint({100,10,100},shallow,0,.5f,50,50,gx,gy,63.6f)&&std::fabs(std::hypot(gx-50,gy-50)-63.6f)<.01f,
              "a ground skill may land as far as the caller's reach (the game's, up to its corners)");
        Check(spellaim::GroundPoint({100,10,100},shallow,0,.5f,50,50,gx,gy,0)&&std::fabs(std::hypot(gx-50,gy-50)-spellaim::GroundMinDistance)<.01f,
              "a reach below the minimum still throws the minimum distance");
        Check(spellaim::GroundTarget(251)&&spellaim::GroundTarget(59)&&!spellaim::GroundTarget(36),"Fire Blast is a ground skill like Blizzard; Fire Bolt is not");
        const auto steep=math::Normalized(math::Vec{.001f,-1,0});
        Check(spellaim::GroundPoint({100,10,100},steep,0,.5f,50,50,gx,gy)&&std::fabs(std::hypot(gx-50,gy-50)-spellaim::GroundMinDistance)<.01f&&gx>50,
              "aiming at your feet still throws a short distance forward");
        gx=gy=7;
        Check(!spellaim::GroundPoint({100,10,100},math::Vec{1,0,0},0,.5f,50,50,gx,gy)&&gx==7&&gy==7,"level ray never lands and keeps output");
        Check(!spellaim::GroundPoint({100,-1,100},down45,0,.5f,50,50,gx,gy),"origin under the floor is refused");
        Check(!spellaim::GroundPoint({NAN,10,100},down45,0,.5f,50,50,gx,gy),"invalid origin is refused");
        for(int degree=0;degree<360;degree+=3)for(float pitch:{.05f,.3f,1.2f}) {
            const float a=degree*3.14159265f/180;
            const auto d=math::Normalized(math::Vec{std::cos(a),-std::tan(pitch),std::sin(a)});
            Check(spellaim::GroundPoint({9460,4,9300},d,0,.5f,4730,4650,gx,gy),"every heading lands");
            Check(std::abs(int(gx)-4730)<=50&&std::abs(int(gy)-4650)<=50,"landing point passes the native command gate");
            const auto heading=math::Normalized(math::Vec{gx-4730,0,gy-4650});
            Check(heading.x*std::cos(a)+heading.z*std::sin(a)>.999f,"landing point keeps the hand heading");
        }
    }
    std::puts("Spell aiming: native behavior, scoped targeting, movement-independent cast resolution and cell precision passed");
}
