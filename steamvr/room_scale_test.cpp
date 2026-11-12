#include "room_scale.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;using namespace framevr::roomscale;using math::Vec;using math::Quat;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static float Len(Vec v){return std::sqrt(math::Dot(v,v));}
int main(){
    const float upm=4.f;const Vec hero{100,10,200};
    const auto camera=[&](Quat base,Vec head,Vec origin){return hero+math::Rotate(base,head-origin)*upm;};
    {   // A step from the room's centre, then a snap turn: the camera stays where the head is.
        Vec origin{0,1.6f,0};const Vec head{.8f,1.6f,-.5f};
        const Quat was=math::Yaw(.3f),now=math::Yaw(.3f-.785398f);   // 45 degrees right
        const Vec before=camera(was,head,origin);
        const Vec turnedAboutCentre=camera(now,head,origin);
        Check(Len(turnedAboutCentre-before)>1.f,"turned about the room's centre, the view jumped (the old way)");
        origin=TurnAboutHead(origin,head,was,now);
        Check(Len(camera(now,head,origin)-before)<1e-4f,"turned about the head, the view stays");
        Check(origin.y==1.6f,"the origin's height is left alone");
        Check(std::fabs(Apart(head,origin)-Apart(head,Vec{0,1.6f,0}))<1e-5f,"you stay as far from the room's centre");
    }
    {   // Smooth turning, many small steps: still no drift; standing at the centre nothing moves.
        Vec origin{.2f,1.5f,.1f};const Vec head{-.6f,1.7f,.9f};Quat base=math::Yaw(1.f);
        const Vec start=camera(base,head,origin);
        for(int i=1;i<=500;++i){const Quat next=math::Yaw(1.f+.01f*i);origin=TurnAboutHead(origin,head,base,next);base=next;}   // View builds each turn fresh from its angle
        Check(Len(camera(base,head,origin)-start)<1e-3f,"500 smooth-turn steps leave the view in place");
        const Vec centred{.3f,1.6f,.3f};
        const Vec o=TurnAboutHead(centred,{.3f,1.6f,.3f},math::Yaw(0),math::Yaw(2.f));
        Check(Len(o-centred)<1e-6f,"at the room's centre a turn moves nothing");
        Check(Len(TurnAboutHead(origin,head,base,base)-origin)<1e-6f,"no turn, no change");
    }
    std::puts("room-scale-test: ok");
}
