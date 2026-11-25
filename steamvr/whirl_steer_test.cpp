#include "whirl_steer.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;using namespace framevr::whirlsteer;using math::Vec;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e=1e-3f){return std::fabs(a-b)<=e;}
// The view's level right and ahead in the world for a head heading (frame_math Heading: grows turning left,
// 0 = -z ahead), as vrcam's StickFrame has them.
static Vec ViewAhead(float heading){return math::Rotate(math::Yaw(heading),{0,0,-1});}
static Vec ViewRight(float heading){return math::Rotate(math::Yaw(heading),{1,0,0});}
int main(){
    {   // The game's own dead zone: steering begins where walking does.
        Check(!Pushed(0,0)&&!Pushed(.31f,0)&&!Pushed(0,-.31f)&&!Pushed(.22f,.22f),"inside the dead zone: at rest");
        Check(Pushed(.32f,0)&&Pushed(0,-.5f)&&Pushed(.25f,.25f)&&Pushed(1,1),"past it: pushed, any way");
        Check(!Pushed(NAN,.8f)&&!Pushed(.8f,INFINITY),"nonsense is at rest");
    }
    {   // At rest, no way: Whirlwind on keeps the last whirl's (whirl_on.h).
        float gx=7,gy=7;
        Check(!GridWay(.1f,.2f,ViewRight(0),ViewAhead(0),gx,gy)&&gx==7&&gy==7,"a stick at rest gives no way, the outputs untouched");
    }
    {   // Facing -z in the world: ahead goes -z (grid y down), right goes +x, back +z.
        float gx=0,gy=0;
        Check(GridWay(0,1,ViewRight(0),ViewAhead(0),gx,gy)&&Near(gx,0)&&Near(gy,-1),"pushed ahead: where you face");
        Check(GridWay(1,0,ViewRight(0),ViewAhead(0),gx,gy)&&Near(gx,1)&&Near(gy,0),"pushed right: to your right");
        Check(GridWay(0,-.6f,ViewRight(0),ViewAhead(0),gx,gy)&&Near(gx,0)&&Near(gy,1),"pulled back: behind you, a unit way however far");
        Check(GridWay(-.5f,.5f,ViewRight(0),ViewAhead(0),gx,gy)&&Near(gx,-.70711f)&&Near(gy,-.70711f),"ahead and left: halfway between");
    }
    {   // Turned: the way turns with the view, as the walk does (walkdir::OnGround), for every heading and push.
        for(int d=0;d<360;d+=15)for(int s=0;s<360;s+=30) {
            const float h=d*3.1415927f/180,a=s*3.1415927f/180,x=.8f*std::sin(a),y=.8f*std::cos(a);
            float gx=0,gy=0;
            Check(GridWay(x,y,ViewRight(h),ViewAhead(h),gx,gy),"a push past the dead zone always has a way");
            const Vec walk=math::Normalized(walkdir::OnGround(x,y,ViewRight(h),ViewAhead(h)));
            Check(Near(gx,walk.x)&&Near(gy,walk.z)&&Near(std::hypot(gx,gy),1),"the way is the walk's own, on the grid's x and z");
        }
    }
    {   // Walk direction's Controller mode turns the stick before it reaches here (transport walkLx / walkLy):
        // the controller pointing 90 degrees left of the view, pushed ahead, whirls left, as it walks.
        float x=0,y=1;walkdir::ToView(x,y,1.5707963f,0);
        float gx=0,gy=0;
        Check(GridWay(x,y,ViewRight(0),ViewAhead(0),gx,gy)&&Near(gx,-1)&&Near(gy,0),"pointing left, ahead whirls left");
    }
    {   // The spot: cells from the hero along the way, inside the game's reach.
        float x=0,y=0;
        Check(Spot(5000,4000,0,-1,10,x,y)&&Near(x,5000)&&Near(y,3990),"10 cells along the way");
        Check(Spot(5000,4000,.6f,.8f,8,x,y)&&Near(std::hypot(x-5000,y-4000),8),"any way: the length asked");
        Check(Spot(5000,4000,1,0,200,x,y)&&Near(x-5000,aimreach::GameCells),"never past the game's reach");
        Check(!Spot(5000,4000,NAN,0,10,x,y)&&!Spot(5000,4000,1,0,0,x,y)&&!Spot(NAN,4000,1,0,10,x,y),"nonsense or no length: no spot");
    }
    std::puts("whirl-steer-test: ok");
}
