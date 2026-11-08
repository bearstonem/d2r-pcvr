#include "walk_direction.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;using namespace framevr::walkdir;using math::Quat;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<1e-3f;}
static Quat Pitch(float a){return {std::sin(a*.5f),0,0,std::cos(a*.5f)};}
int main(){
    const float quarter=1.5707963f;
    {   // The controller where you look: the stick is unchanged.
        float x=.3f,y=.8f;ToView(x,y,.7f,.7f);Check(Near(x,.3f)&&Near(y,.8f),"pointing where you look changes nothing");
    }
    {   // The controller turned 90 degrees left of the view: stick ahead walks left; stick right walks ahead.
        float x=0,y=1;ToView(x,y,quarter,0);Check(Near(x,-1)&&Near(y,0),"pointing left, ahead walks left");
        x=1;y=0;ToView(x,y,quarter,0);Check(Near(x,0)&&Near(y,1),"pointing left, right walks ahead");
        x=0;y=1;ToView(x,y,0,quarter);Check(Near(x,1)&&Near(y,0),"looking left with the controller ahead, ahead walks right in the view");
        x=0;y=.6f;ToView(x,y,3.1415927f,0);Check(Near(x,0)&&Near(y,-.6f),"pointing behind you walks back, the length kept");
    }
    {   // The controller's heading from its aim pose: level or tilted, the same; straight down keeps the last.
        Pointing p;Check(!p.Update(math::Yaw(.5f),false),"never tracked: unknown");
        Check(p.Update(math::Yaw(.5f),true)&&Near(p.heading,.5f),"the aim's heading");
        Check(p.Update(math::Multiply(math::Yaw(-1.f),Pitch(.6f)),true)&&Near(p.heading,-1.f),"tilted up, the same heading");
        Check(p.Update(math::Multiply(math::Yaw(2.f),Pitch(-1.5f)),true)&&Near(p.heading,-1.f),"pointing at the floor keeps the last heading");
        Check(p.Update(math::Yaw(2.f),false)&&Near(p.heading,-1.f),"lost tracking keeps it too");
    }
    std::puts("walk-direction-test: ok");
}
