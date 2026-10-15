#include "elemental_hands.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr;
using namespace elements;
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct Sim {
    Input in;
    interaction::Snapshot snapshot;
    State state;
    Scene scene;
    Sim() {
        in.enabled=in.active=in.tracked[0]=in.tracked[1]=true;
        in.context={7,9,11,13};in.at=1000;
        in.hands[0].p={.2f,0,-.5f};in.hands[1].p={-.2f,0,-.5f};
        snapshot.session=7;snapshot.heroClass=1;snapshot.vitalsKnown=true;snapshot.health=50;
        snapshot.actionKnown=true;snapshot.actionMode=1;
        Bind(0,47);Bind(4,45);Step(0);Step(120);
    }
    void Bind(int slot,int skill){auto& s=snapshot.skills[slot];s.id=skill;s.known=s.available=true;}
    Scene Step(uint64_t ms=10){in.at+=ms;snapshot.at=in.at;return scene=state.Step(in,snapshot);}
    Cast Event(int h)const {
        const int slot=in.casting[h];const auto& s=snapshot.skills[slot];
        return {in.context,in.at,in.activation[h],slot,s.id,s.owner};
    }
};
int main() {
    for(int id:{36,41,46,47,51,52,56,62})Check(Classify(id)==Element::Fire,"active fire mapping");
    for(int id:{39,40,44,45,50,55,59,60,64})Check(Classify(id)==Element::Cold,"active cold mapping");
    for(int id:{38,42,48,49,53,57})Check(Classify(id)==Element::Lightning,"active lightning mapping");
    for(int id:{-1,0,37,43,54,58,61,63,65,219,357,4095})Check(Classify(id)==Element::None,"utility/passive/unknown neutral");
    {
        Sim s;Check(s.scene.hands[0].element==Element::Fire&&s.scene.hands[1].element==Element::Cold&&
            s.scene.hands[0].opacity==1&&s.scene.hands[1].opacity==1,"independent trigger elements reach idle visibility");
        Check(s.scene.hands[0].pose.p.x==.2f&&s.scene.hands[1].pose.p.x==-.2f,"room hand positions preserved");
        s.Bind(1,49);s.in.casting[0]=1;s.Step();s.Step(120);
        Check(s.scene.hands[0].element==Element::Lightning&&s.scene.hands[1].element==Element::Cold,"shortcut affects only its own hand");
        s.in.casting[0]=-1;s.Step();Check(s.scene.hands[0].element==Element::Fire,"release restores trigger element");
        s.Bind(1,54);s.in.casting[0]=1;s.Step();Check(s.scene.hands[0].opacity==0,"utility shortcut suppresses idle element");
        s.in.casting[0]=4;s.Step();Check(s.scene.hands[0].opacity==0,"foreign hand slot cannot borrow an element");
    }
    for(int reason=0;reason<12;++reason) {
        Sim s;
        if(reason==0)s.in.enabled=false;
        if(reason==1)s.in.active=false;
        if(reason==2)s.snapshot.heroClass=0;
        if(reason==3)s.snapshot.heroClass=-1;
        if(reason==4)s.snapshot.health=0;
        if(reason==5)s.snapshot.vitalsKnown=false;
        if(reason==6)s.snapshot.session=8;
        if(reason==7)s.in.head.q.w=0;
        if(reason==8)s.snapshot.health=std::numeric_limits<float>::quiet_NaN();
        if(reason==9)s.snapshot.actionKnown=false;
        if(reason==10)s.snapshot.actionMode=0;
        if(reason==11)s.snapshot.actionMode=17;
        Check(!s.Step().Visible(),"disabled/inactive/other class/dead/invalid state clears both hands");
    }
    {
        Sim s;s.in.at+=651;Check(!s.state.Step(s.in,s.snapshot).Visible(),"stale snapshot clears effects");
        Check(!Current(s.scene,7,s.scene.at+151)&&!Current(s.scene,8,s.scene.at)&&
            !Current(s.scene,7,s.scene.at-1)&&Current(s.scene,7,s.scene.at+150),"render refuses stale, foreign and future scenes");
    }
    for(int reason=0;reason<5;++reason) {
        Sim s;
        if(reason==0)s.in.tracked[0]=false;
        if(reason==1)s.in.occupied[0]=true;
        if(reason==2)s.snapshot.skills[0].available=false;
        if(reason==3)s.snapshot.skills[0].known=false;
        if(reason==4)s.in.hands[0].p.x=std::numeric_limits<float>::infinity();
        s.Step();Check(s.scene.hands[0].opacity==0&&s.scene.hands[1].opacity==1,"hand loss/occupancy/unknown binding leaves the other hand alone");
    }
    {
        Sim s;s.in.hands[0].p={0,0,-.15f};s.Step();Check(s.scene.hands[0].opacity==0,"face proximity hides aura");
        s.in.hands[0].p={0,0,-.225f};s.Step();Check(s.scene.hands[0].opacity>.49f&&s.scene.hands[0].opacity<.51f,"face proximity fades continuously");
    }
    {
        Sim s;s.in.casting[0]=0;s.in.activation[0]=3;s.Step();
        Check(s.scene.hands[0].pulse==0,"holding a trigger alone never claims cast success");
        s.state.Accepted(0,s.Event(0));s.Step();
        Check(s.scene.hands[0].pulse>.9f&&s.scene.hands[1].pulse==0,"confirmed cast pulses its own hand");
        s.Step(100);const float decayed=s.scene.hands[0].pulse;
        Check(decayed>0&&decayed<.6f,"success pulse decays");s.Step(110);
        Check(s.scene.hands[0].pulse==0,"success pulse expires without another cast");
    }
    for(int reason=0;reason<10;++reason) {
        Sim s;s.in.casting[0]=0;s.in.activation[0]=3;s.Step();auto event=s.Event(0);
        if(reason==0)++event.context.session;
        if(reason==1)++event.context.epoch;
        if(reason==2)++event.context.binding;
        if(reason==3)++event.context.equipment;
        if(reason==4)++event.activation;
        if(reason==5)event.skill=36;
        if(reason==6)event.owner=42;
        if(reason==7)event.at-=221;
        if(reason==8)event.at+=100;
        if(reason==9)event.slot=1;
        s.state.Accepted(0,event);s.Step();Check(s.scene.hands[0].pulse==0,"stale or mismatched cast feedback discarded");
    }
    {
        Sim s;s.in.casting[0]=0;s.in.activation[0]=1;s.Step();s.state.Accepted(0,s.Event(0));
        s.in.occupied[0]=true;s.Step();s.in.occupied[0]=false;s.Step();
        Check(s.scene.hands[0].pulse==0,"suppression cannot replay an accepted cast on resume");
        s.state.Accepted(0,s.Event(0));s.Step(151);Check(s.scene.hands[0].pulse==0,"input stall clears pending feedback");
        s.state.Accepted(0,s.Event(0));--s.in.at;s.Step(0);Check(s.scene.hands[0].pulse==0,"clock rollback clears pending feedback");
    }
    std::puts("Elemental hand state: skill selection, independent hands, lifecycle, proximity and confirmed-cast feedback passed");
}
