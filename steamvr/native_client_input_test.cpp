#include "native_client_input.h"
#include "hand_controls.h"
#include "skill_pad.h"
#include "glyph_cast.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static int checks=0;
static void Check(bool b,const char* why){++checks;if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Sim {
    gameplay::Engine engine;gameplay::Input in;nativeclient::Router router;nativeclient::Context c;
    gameplay::Output out;int hand=-1;
    Sim(){in.active=true;in.tracked[0]=in.tracked[1]=in.aimTracked=true;in.equipment=1;
        in.weapon=gameplay::Weapon::Melee;in.head.p={0,1.65f,0};
        in.hand[0].p={.25f,1.3f,-.4f};in.hand[1].p={-.25f,1.3f,-.4f};in.aim=in.hand[0];c.native=true;c.session=1;c.basic={1,0,-1};Step(4);}
    gameplay::Output Step(int n=1){while(n--){in.time+=.02;out=engine.Step(in);hand=router.Step(in,out,c);}return out;}
    void Nock(){in.weapon=gameplay::Weapon::Bow;in.hand[0].p={0,1.3f,-.4f};in.hand[1].p={0,1.3f,-.5f};Step(4);
        in.trigger=1;Check(Step().bowDrawing,"nock fixture");
        for(int i=1;i<=10;++i){in.hand[0].p.z=-.4f+.04f*i;Step();}}
    void Swing(int h=0){in.hand[h].p.x+=.08f;Step();}
};
int main(){
    {Sim s;s.c.basic={};s.Nock();s.in.trigger=0;s.Step();Check(!s.out.attack&&s.out.skill<0,"gesture must not synthesize raw A: A may be Interact or another spell");}
    {Sim s;s.Nock();s.in.trigger=0;s.Step();Check(s.out.skill==1&&!s.out.attack&&!s.out.bowRelease,"client bow uses bound native Attack");
     Check(s.hand==1&&s.out.rayDirection.z<-.99f,"bow release aim retained");Check(!s.router.MeleePulse(),"bow cannot arm melee feedback");s.Step(12);Check(!s.out.attack&&s.out.skill<0,"bow pulse expires");}
    {Sim s;s.c.native=false;s.Nock();s.in.trigger=0;s.Step();Check(!s.out.attack&&s.out.bowRelease,"host bow remains direct");}
    {Sim s;s.c.bow={2,7,-1};s.Nock();s.in.trigger=0;s.Step();Check(s.out.skill==2&&!s.out.attack&&!s.out.bowRelease,"loaded arrow uses native skill");}
    {Sim s;s.Swing();Check(s.out.skill==1&&!s.out.attack&&!s.out.meleeContact,"client swing uses bound native Attack");Check(s.router.MeleePulse(),"physical swing arms feedback");
     for(int i=0;i<18;++i){s.Swing();if(i>9)Check(!s.out.attack&&s.out.skill<0,"continuous swing cannot repeat");}
     s.Step(15);s.Swing();Check(s.out.skill==1,"slow rearm permits next swing");}
    {Sim s;s.c.melee[0]={2,144,-1};s.Swing();Check(s.out.skill==2&&!s.out.attack,"strike skill retained");}
    {Sim s;s.c.offhand=true;s.Step(2);s.Swing(1);Check(s.out.skill==1&&s.hand==1,"offhand swing uses native action");}
    {Sim s;for(int i=0;i<10;++i){s.in.head.p.x+=.08f;for(auto& h:s.in.hand)h.p.x+=.08f;s.Step();Check(!s.out.attack,"whole-body motion is not a swing");}}
    {Sim s;s.c.native=false;s.Swing();Check(!s.out.attack&&s.out.meleeContact,"host contact melee unchanged");}
    for(int event=0;event<6;++event){Sim s;s.Swing();Check(s.out.skill==1,"pulse fixture");
        if(event==0)s.in.active=false;if(event==1)s.in.tracked[0]=false;if(event==2)s.in.palette=true;
        if(event==3)++s.c.session;if(event==4)++s.in.equipment;if(event==5)++s.in.skillContext;
        s.Step();Check(!s.out.attack&&s.out.skill<0,"lifecycle event cancels native pulse");}
    {Sim s;s.in.weapon=gameplay::Weapon::Javelin;s.in.handBindings=true;s.engine.Select(0);s.in.selectedSkill=2;s.in.selectedAvailable=true;
     s.c.throwing={0,2,-1};s.Step(4);s.in.trigger=1;s.Step();
     for(int i=0;i<8;++i){s.in.hand[0].p.z-=.08f;s.Step();}s.in.trigger=0;s.Step();
     Check(s.out.skill==0&&!s.out.javelinRelease&&!s.out.attack,"throw gesture routes bound Throw once");s.Step(12);Check(s.out.skill<0,"throw pulse expires");}
    {Sim s;s.engine.Select(2);s.Step(3);s.in.trigger=1;s.Step();Check(s.out.skill==2&&!s.out.attack,"explicit spell takes priority");}
    // Both hands' physical spells become native slots when direct casting is
    // unavailable; the existing hand controller preserves release/menu gates.
    for(int h=0;h<2;++h){skills::HandControls controls;skills::HandInput in;in.active=true;in.context=1;in.layout=1;
        for(int i=0;i<3;++i){in.time+=.02;controls.Step(in);}in.trigger[h]=1;in.time+=.02;auto out=controls.Step(in);
        Check(out.slot==skills::TriggerSlot(h)&&out.spells[h]<0,"client spell reaches native slot");
        in.active=false;in.time+=.02;out=controls.Step(in);Check(out.slot<0,"menu releases native spell");}
    // Exercise the complete pure input chain used by transport: native hand
    // arbitration -> selected/quick skill -> Engine -> router -> SkillPad.
    for(int hand=0;hand<2;++hand)for(int button=0;button<5;++button){
        Sim s;skills::HandControls hands;skills::HandInput hi;skills::SkillPad pad;
        hi.active=true;hi.context=1;hi.layout=1;
        s.in.handBindings=true;s.engine.Select(skills::TriggerSlot(0));
        s.in.selectedSkill=36;s.in.selectedAvailable=true;s.Step(4);
        const int wanted=skills::HandSlot(hand,button);uint32_t packet=0;
        auto route=[&](){
            hi.time=s.in.time+.02;const auto h=hands.Step(hi);
            s.in.quickCast=h.slot>=0&&h.slot!=skills::TriggerSlot(0);
            s.in.quickSkill=s.in.quickCast?h.slot:-1;
            s.in.trigger=h.slot==skills::TriggerSlot(0)?hi.trigger[0]:0;
            s.Step();const auto bits=gameplay::SkillButtons(s.out.skill);
            return pad.Poll(bits,gameplay::SkillModifier(s.out.skill),++packet,uint64_t(s.in.time*1000));
        };
        for(int i=0;i<4;++i)route();
        if(button==0)hi.trigger[hand]=1;
        else {hi.face[hand][(button-1)%2]=true;hi.grip[hand]=button>2?1.f:0.f;}
        uint16_t delivered=0;for(int i=0;i<8;++i)delivered|=route();
        Check(s.out.skill==wanted&&delivered==skills::Buttons(wanted),"each hand spell reaches actual native pad bits");
        hi.trigger[hand]=0;hi.face[hand][0]=hi.face[hand][1]=false;
        Check(route()==0&&s.out.skill<0,"spell release clears native pad bits");
    }
    // One native action at a time: no backlog and no delayed burst.
    {Sim s;s.Swing();Check(s.out.skill==1,"basic pulse fixture");
     s.in.quickCast=true;s.in.quickSkill=2;s.Step();Check(s.out.skill==2&&!s.out.attack,"explicit spell supersedes gesture pulse");
     s.in.quickCast=false;s.in.quickSkill=-1;s.Step(12);Check(s.out.skill<0&&!s.out.attack,"cancelled gesture never resumes after spell");}
    {nativeclient::Router router;nativeclient::Context c;c.native=true;c.session=1;c.basic={1,0,-1};c.throwing={2,2,-1};
     gameplay::Input in;in.active=in.aimTracked=in.tracked[0]=in.tracked[1]=true;in.equipment=1;in.weapon=gameplay::Weapon::Bow;
     gameplay::Output out;out.rayValid=true;in.time=1;router.Step(in,out,c);
     in.time+=.02;out={};out.rayValid=true;out.bowRelease=true;router.Step(in,out,c);Check(out.skill==1,"first native pulse starts");
     in.time+=.02;out={};out.rayValid=true;out.javelinRelease=true;router.Step(in,out,c);Check(out.skill==1,"busy pulse is not replaced by another event");
     for(int i=0;i<15;++i){in.time+=.02;out={};out.rayValid=true;router.Step(in,out,c);}
     Check(out.skill<0&&!out.attack,"discarded second action never fires later");}
    // Glyphs can press the raw right-trigger record rather than out.skill.
    // They must cancel an old swing pulse, and drawing must not create attacks.
    {Sim s;s.Swing();Check(s.out.skill==1,"glyph preemption pulse fixture");
     glyphcast::Cast cast;cast.Begin({glyphcast::How::Record,glyphcast::GlyphRecord},138,-1,s.in.time);
     auto press=cast.Step(s.in.time+.02,138,-1);Check(press.rt,"glyph record trigger fixture");
     s.c.actionOwned=cast.Busy()||press.rt;s.Step();
     Check(s.out.skill<0&&!s.out.attack,"glyph native trigger cancels pending basic attack");
     s.c.actionOwned=false;s.Step(12);Check(s.out.skill<0,"old attack does not resume after glyph");
     s.c.actionOwned=true;s.Step(4);s.Swing();Check(s.out.skill<0,"glyph drawing motion cannot synthesize attack");
     s.c.actionOwned=false;s.Step(4);s.Swing();Check(s.out.skill==1,"normal attacks resume after glyph ownership ends");}
    {Sim s;s.c.actionOwned=true;s.in.quickCast=true;s.in.quickSkill=3;s.Step();
     Check(s.out.skill==3,"glyph ownership preserves explicit native skill output");}
    std::printf("native client input: %d checks passed\n",checks);
}
