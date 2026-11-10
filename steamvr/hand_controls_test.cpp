#include "hand_controls.h"
#include "hand_role.h"
#include "interactions.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
struct Sim {
    skills::HandControls e;skills::HandInput in;skills::HandOutput out;
    Sim(){in.active=true;in.context=1;in.layout=1;Step(3);}
    void Step(int n=1){while(n--){in.time+=.02;out=e.Step(in);}}
};
int main(){
    unsigned slots=0;
    for(int h=0;h<2;++h)for(int key=0;key<5;++key){
        Sim s;const int slot=skills::HandSlot(h,key);assert(slot>=0&&slot<skills::Count);
        assert(!(slots&(1u<<slot)));slots|=1u<<slot;
        if(key==0)s.in.trigger[h]=1;
        else {s.in.grip[h]=key>2?1:0;s.in.face[h][(key-1)%2]=true;}
        s.Step();assert(s.out.slot==slot&&s.out.hand==h);s.Step(6);assert(s.out.slot==slot);
        s.in.trigger[h]=0;s.in.face[h][0]=s.in.face[h][1]=false;s.Step();assert(s.out.slot==-1);
    }
    assert(slots==(1u<<skills::Count)-1);
    // Every binding has its own native button/modifier pair, including A banks.
    for(int a=0;a<skills::Count;++a)for(int b=a+1;b<skills::Count;++b)
        assert(skills::Buttons(a)!=skills::Buttons(b)||skills::Alternate(a)!=skills::Alternate(b));
    for(int h=0;h<2;++h){
        Sim s;s.in.click[h]=true;s.Step();assert(s.out.wheel==h&&s.out.slot==-1);
        s.Step(4);assert(s.out.wheel==h);s.in.click[h]=false;s.Step();assert(s.out.wheel==h);
        s.in.trigger[1-h]=1;s.Step();assert(s.out.slot==-1);
        s.in.click[1-h]=true;s.Step();assert(s.out.wheel==1-h&&s.out.slot==-1);
        s.in.click[1-h]=false;s.Step();s.in.click[1-h]=true;s.Step();assert(s.out.wheel==-1&&s.out.slot==-1);
        s.Step(4);assert(s.out.slot==-1);s.in.trigger[1-h]=0;s.Step();s.in.trigger[1-h]=1;s.Step();
        assert(s.out.slot==skills::TriggerSlot(1-h));
    }
    {
        Sim s;s.in.trigger[0]=s.in.trigger[1]=1;s.Step();assert(s.out.slot==0);
        s.in.trigger[0]=0;s.Step();assert(s.out.slot==-1);s.Step(4);assert(s.out.slot==-1);
        s.in.trigger[1]=0;s.Step();s.in.trigger[1]=1;s.Step();assert(s.out.slot==4);
    }
    for(int h=0;h<2;++h){
        Sim s;s.in.face[h][0]=true;s.Step();assert(s.out.slot==skills::HandSlot(h,1));
        s.in.grip[h]=1;s.Step(3);assert(s.out.slot==-1);
        s.in.face[h][0]=false;s.Step();s.in.face[h][0]=true;s.Step();assert(s.out.slot==skills::HandSlot(h,3));
        s.in.occupied=true;s.Step();assert(s.out.slot==-1);s.in.occupied=false;s.Step(3);assert(s.out.slot==-1);
    }
    for(int event=0;event<4;++event){
        Sim s;s.in.trigger[0]=1;s.Step();assert(s.out.slot==0);
        if(event==0)s.in.active=false;if(event==1)s.in.context++;if(event==2)s.in.layout++;if(event==3)s.in.time+=1;
        s.Step();assert(s.out.slot==-1);s.in.active=true;s.Step(4);assert(s.out.slot==-1);
    }
    {
        Sim s;s.in.click[1]=true;s.Step();s.in.click[1]=false;s.Step();s.in.layout++;s.Step();assert(s.out.wheel==1&&s.out.slot==-1);
    }
    // The same five wedge centers map to the same logical hand buttons.
    for(int h=0;h<2;++h)for(int b=0;b<5;++b){
        const float angle=b*2*interaction::Pi/5;
        assert(interaction::WheelCell(.5f+.28f*std::sin(angle),.45f-.28f*std::cos(angle),-1,interaction::MenuPage::Spells,h)==b);
    }
    // Left-handed play (hand_role.h): the engine's hand is a ROLE. Fed in role order, a controller casts the
    // slots of the role it plays: right-handed exactly as above; left-handed the left controller has the
    // main hand's saved slots (0, 8, 1, 2, 3) and the right one the off hand's (4, 5, 6, 7, 9).
    for(const bool leftHanded:{false,true})for(int controller=0;controller<2;++controller)for(int key=0;key<5;++key){
        const handrole::Map map{leftHanded};
        float trigger[2]{},grip[2]{};bool face[2][2]{};   // by physical controller, 0 right
        Sim s;
        const auto step=[&]{for(int role=0;role<2;++role){const int p=map.Physical(role);s.in.trigger[role]=trigger[p];s.in.grip[role]=grip[p];
            s.in.face[role][0]=face[p][0];s.in.face[role][1]=face[p][1];}s.Step();};
        if(key==0)trigger[controller]=1;else{grip[controller]=key>2?1.f:0.f;face[controller][(key-1)%2]=true;}
        step();
        const int role=map.Role(controller);
        assert(s.out.slot==skills::HandSlot(role,key)&&s.out.hand==role);
        assert(leftHanded?s.out.slot==skills::HandSlot(1-controller,key):s.out.slot==skills::HandSlot(controller,key));
        assert(map.Physical(s.out.hand)==controller);   // and what it casts comes back to the controller that pressed
    }
    assert(skills::TriggerSlot(handrole::Map{true}.Role(1))==skills::TriggerSlot(0));   // the left trigger is the main trigger
    puts("Hand controls: ten bindings, two toggle wheels, opposite-hand ownership, grip/release, lifecycle gates and left-handed roles passed");
}
