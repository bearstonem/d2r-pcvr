#include "gameplay.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr;
using namespace framevr::gameplay;
static void Check(bool b,const char* message) {if(!b){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct Sim {
    Engine engine;Input in;Output out;
    Sim() {
        in.active=true;in.tracked[0]=in.tracked[1]=in.aimTracked=true;
        in.head.p={0,1.65f,0};in.hand[0].p={.25f,1.25f,-.4f};in.hand[1].p={-.25f,1.25f,-.4f};
        in.aim=in.hand[0];in.weapon=Weapon::Melee;in.equipment=1;
        Step();Step();
    }
    Output Step(int n=1) {while(n--){in.time+=.02;out=engine.Step(in);}return out;}
    void Move(Vec p,int n=10) {
        const Vec start=in.hand[0].p;
        for(int i=1;i<=n;++i){in.hand[0].p=start+(p-start)*(float(i)/n);Step();}
    }
    void Nock() {
        in.weapon=Weapon::Bow;in.hand[0].p={0,1.3f,-.4f};in.hand[1].p={0,1.3f,-.5f};
        in.grip[1]=0;Step();Step();in.trigger=1;
        Check(Step().bowDrawing,"near-hand pinch nocks bow");
    }
};
int main() {
    {
        Sim s;s.in.weapon=Weapon::Crossbow;s.Step(2);s.in.trigger=1;s.Step();
        Check(s.out.attack,"crossbow trigger requests a basic attack");
        auto native=s.out;
        Check(!RouteCrossbow(s.in,native,true,false,false,-1)&&native.attack,"unavailable physical path retains native attack");
        Check(RouteCrossbow(s.in,s.out,true,true,false,-1)&&!s.out.attack&&s.out.skill<0,
              "physical crossbow consumes native attack and its animation lock");
        Check(!CastingSkill(s.in,s.out,0)&&s.out.rayValid,"crossbow leaves locomotion free while keeping aim");
        s.in.trigger=0;s.engine.Select(0);s.Step(2);s.in.trigger=1;s.Step();
        Check(s.out.skill==0,"assigned primary skill reaches routing");
        native=s.out;
        Check(!RouteCrossbow(s.in,native,false,true,true,0)&&native.skill==0,"left-hand assignment is unchanged");
        Check(!RouteCrossbow(s.in,native,true,true,true,6)&&native.skill==0,"special skills retain native route");
        Check(!RouteCrossbow(s.in,native,true,true,false,0)&&native.skill==0,"unknown skill is never assumed to be Attack");
        Check(RouteCrossbow(s.in,s.out,true,true,true,0)&&s.out.skill<0,"assigned basic Attack uses independent fire");
        s.in.trigger=0;s.Step();Check(!RouteCrossbow(s.in,s.out,true,true,true,0),"trigger release ends physical firing");
        s.in.trigger=1;s.in.palette=true;s.Step();Check(!RouteCrossbow(s.in,s.out,true,true,true,0),"wheel blocks crossbow firing");
    }
    for(bool quick:{false,true})for(int slot=0;slot<skills::Count;++slot) {
        Sim s;s.engine.Select(slot);s.Step(2);
        Check(!CastingSkill(s.in,s.out,0),"selection alone does not change locomotion routing");
        s.in.quickCast=quick;s.in.quickSkill=quick?slot:-1;s.in.trigger=quick?0:1;s.Step();
        Check(CastingSkill(s.in,s.out,0),"trigger and shortcut casts keep native movement input for all slots");
        for(Vec delta: {Vec{0,0,.02f},Vec{0,0,-.02f},Vec{.02f,0,0},Vec{-.02f,0,0}}) {
            s.in.head.p=s.in.head.p+delta;s.in.aim.p=s.in.aim.p+delta;
            for(auto& hand:s.in.hand)hand.p=hand.p+delta;
            s.Step();Check(CastingSkill(s.in,s.out,149)&&!s.out.block,"moving while holding a cast retains the native locomotion route");
        }
        Check(!CastingSkill(s.in,s.out,150),"stale cast cannot retain movement routing ownership");
        s.in.cancel=true;s.Step();Check(!CastingSkill(s.in,s.out,0),"cancel clears cast locomotion ownership");
        s.in.active=false;s.Step();Check(!CastingSkill(s.in,s.out,0),"menu/tracking inactivity clears cast locomotion ownership");
    }
    {
        Sim s;s.engine.Select(2);s.Step(2);s.in.quickCast=true;s.in.quickSkill=7;
        Check(s.Step().skill==7&&!s.out.attack&&!s.out.meleeContact,"quick cast overrides motion combat");
        Check(s.engine.Selection()==2&&s.Step().skill==7,"quick cast preserves selected trigger ability");
        s.in.quickSkill=-1;Check(s.Step().skill==-1&&!s.out.meleeContact,"idle modifier does not swing or cast");
        s.in.trigger=1;s.in.quickCast=false;Check(s.Step().skill==-1,"held right trigger cannot leak out of shortcut layer");
        s.in.trigger=0;s.Step();s.in.trigger=1;Check(s.Step().skill==2,"normal trigger skill restored after release");
    }
    {
        Sim s;s.Nock();s.Move({0,1.3f,0});s.in.quickCast=true;s.in.quickSkill=4;
        Check(s.Step().skill==4&&!s.out.bowDrawing&&!s.out.bowRelease,"shortcut cancels held draw without shooting");
        s.in.quickCast=false;s.in.trigger=0;Check(!s.Step().bowRelease,"ending shortcut cannot release old arrow");
    }
    {
        Sim s;s.in.trigger=1;Check(s.Step().attack,"button attack fallback");
        s.in.active=false;Check(!s.Step().attack,"menu cancels attack");s.in.active=true;
        Check(!s.Step(3).attack,"held trigger cannot attack on menu return");
        s.in.trigger=0;s.Step();s.in.trigger=1;Check(s.Step().attack,"release rearms fallback");
    }
    {
        Sim s;Check(s.Step().meleeContact,"equipped melee permits contacts without any button");
        s.in.grip[0]=1;Check(s.Step().meleeContact&&!s.out.attack,"grip neither arms nor attacks");
        for(int i=0;i<10;++i){s.in.hand[0].p.x+=.04f;Check(!s.Step().attack,"swing never synthesizes A or auto-targets");}
        s.in.trigger=1;Check(s.Step().attack&&!s.out.meleeContact,"explicit fallback cannot double up with contact melee");
        s.in.trigger=0;s.in.palette=true;Check(!s.Step().meleeContact,"palette disables melee contact");
        s.in.palette=false;s.Step();Check(s.Step().meleeContact,"palette close restores free swing after release");
        s.in.active=false;Check(!s.Step().meleeContact,"menu/tracking inactivity disables contacts");
    }
    {
        Sim s;s.in.palette=true;s.in.hover=2;s.in.trigger=1;s.Step();s.in.palette=false;s.in.trigger=0;s.Step();
        Check(s.Step().meleeContact&&s.engine.Selection()==2,"selected spell leaves free melee available after selection trigger release");
        s.in.trigger=1;Check(s.Step().skill==2&&!s.out.meleeContact&&!s.out.attack,"casting selected skill pauses free melee without a duplicate attack");
        s.in.trigger=0;Check(s.Step().meleeContact&&s.engine.Selection()==2,"releasing cast restores free melee and keeps the selected skill");
        s.in.active=false;s.Step();s.in.active=true;s.Step(3);
        Check(s.out.meleeContact&&s.engine.Selection()==2,"area load keeps both free melee and selected trigger skill");
        s.in.palette=true;s.in.hover=8;s.in.trigger=1;s.Step();s.in.palette=false;s.in.trigger=0;s.Step();
        Check(s.engine.Selection()==-1&&s.Step().meleeContact,"Weapon clears trigger skill while keeping free melee");
        s.in.trigger=1;Check(s.Step().attack&&!s.out.meleeContact,"Weapon restores explicit basic attack fallback");s.in.trigger=0;s.Step();
        s.engine.Select(2);
        ++s.in.equipment;s.Step();Check(s.engine.Selection()==-1,"equipping melee restores attack mode");
        Check(s.Step(3).meleeContact,"new weapon needs no grip release/activation");
    }
    {
        Sim s;s.Nock();s.Move({0,1.3f,0},10);s.in.trigger=0;
        auto shot=s.Step();Check(shot.bowRelease&&!shot.attack&&shot.feedback==2,"draw release shoots");
        Check(shot.rayDirection.z<-.99f&&std::fabs(shot.rayOrigin.z+.5f)<.001,"arrow aims from string through bow hand");
        s.in.hand[0].p.x+=.1f;shot=s.Step();Check(shot.rayDirection.z<-.99f,"release direction stays latched through input pulse");
        Check(!s.Step(5).bowRelease&&!s.out.attack,"release event cannot become a held attack");
        Check(!s.Step(20).bowRelease,"bow cannot repeat without renocking");
    }
    {
        Sim s;s.in.weapon=Weapon::Javelin;s.Step(3);
        Check(s.out.javelinReady&&s.out.meleeContact,"equipped javelin starts in throw mode with free melee jabs");
        s.in.trigger=1;Check(!s.Step().attack&&!s.out.meleeContact,"holding trigger arms throw without firing or jabbing");
        s.Move({.25f,1.55f,-.2f},10);s.Move({.25f,1.55f,-.75f},5);
        s.in.trigger=.4f;Check(!s.Step().javelinRelease,"trigger noise cannot throw");
        s.in.trigger=0;Check(s.Step().javelinRelease&&s.out.rayDirection.z<-.99f&&!s.out.attack&&!s.out.meleeContact,
            "deliberate forward throw releases once along hand motion");
        Check(!s.Step().javelinRelease&&s.out.meleeContact,"throw release restores free jab without repeating");
        Check(!s.Step(20).javelinRelease,"idle hand never spends more javelins");
        s.engine.Select(2);s.Step(3);Check(s.out.meleeContact,"selected skill preserves javelin jabs");
        s.in.trigger=1;Check(s.Step().skill==2&&!s.out.javelinRelease,"selected skill retains its trigger binding");
    }
    for(int kind=0;kind<4;++kind) {
        Sim s;s.in.weapon=Weapon::Javelin;s.Step(3);s.in.trigger=1;s.Step();
        if(kind==0)s.Move({.25f,1.25f,-.45f},10); // weak reposition, not a throw
        if(kind==1){s.Move({.25f,1.25f,-.95f},5);s.Step(8);} // stopped before release
        if(kind==2){s.in.head.q=math::Yaw(1.2f);s.Step(10);} // no synthetic head velocity
        if(kind==3){s.in.hand[0].p.z-=.6f;s.Step();s.Step(5);} // tracking jump
        s.in.trigger=0;Check(!s.Step().javelinRelease&&!s.out.attack,"weak drop, stale motion, head turn and tracking jump cannot throw");
    }
    for(const Vec direction:{Vec{0,1,0},Vec{0,-1,0},Vec{.6f,0,.8f}}) {
        Sim s;s.in.weapon=Weapon::Javelin;s.Step(3);s.in.trigger=1;s.Step();
        s.Move(s.in.hand[0].p+direction*.6f,6);s.in.trigger=0;
        Check(s.Step().javelinRelease&&Length(s.out.rayDirection-direction)<.0001f,
            "vertical and oblique throws follow room-space hand motion rather than controller aim");
    }
    for(int cancel=0;cancel<7;++cancel) {
        Sim s;s.in.weapon=Weapon::Javelin;s.Step(3);s.in.trigger=1;s.Step();s.Move({.25f,1.4f,-.9f},5);
        if(cancel==0)s.in.active=false; // menu/area/shoulder owner
        if(cancel==1)s.in.tracked[0]=false;
        if(cancel==2)s.in.cancel=true;
        if(cancel==3)++s.in.equipment;
        if(cancel==4)s.in.palette=true;
        if(cancel==5)s.in.quickCast=true;
        if(cancel==6)s.in.time+=.2;
        Check(!s.Step().javelinRelease,"interrupted throw never spends ammunition");
        s.in.active=true;s.in.tracked[0]=true;s.in.cancel=s.in.palette=s.in.quickCast=false;
        Check(!s.Step(3).javelinRelease,"return with held trigger cannot replay cancelled throw");
        s.in.trigger=0;Check(!s.Step().javelinRelease,"release after cancellation cannot fire old throw");
        s.Step(2);s.in.trigger=1;s.Step();s.Move({.25f,1.4f,-.3f},6);s.Move({.25f,1.4f,-.9f},6);s.in.trigger=0;
        Check(s.Step().javelinRelease,"new deliberate throw works after area/input recovery");
    }
    {
        Sim s;s.Nock();Check(s.out.bowDrawing&&s.in.grip[1]==0,"equipped bow needs no left grip clutch");
        s.in.trigger=.4f;Check(s.Step().bowDrawing&&!s.out.attack,"trigger noise cannot release grabbed string");
        s.Move({0,1.3f,0},10);s.in.trigger=.15f;Check(s.Step().bowRelease&&!s.out.attack,"full trigger release emits one shot without pad attack");
    }
    {
        Sim s;s.Nock();s.Move({.2f,1.1f,0},20);
        s.in.hand[1].p={-.05f,1.4f,-.5f};s.Step();s.in.trigger=0;
        const auto expected=math::Normalized(s.in.hand[1].p-s.in.hand[0].p);
        const auto shot=s.Step();
        Check(shot.bowRelease&&!shot.attack&&Length(shot.rayDirection-expected)<.0001f,"release captures both anchors with pitch and yaw");
        s.in.hand[0].p.x+=.08f;s.in.hand[1].p.z+=.08f;
        Check(!s.Step().bowRelease&&Length(s.out.rayDirection-expected)<.0001f,"hand motion after release cannot steer or refire");
    }
    {
        Sim s;s.in.weapon=Weapon::Bow;s.in.hand[1].p={0,1.3f,-.5f};s.in.hand[0].p={.5f,1.3f,-.3f};s.Step(2);
        s.in.trigger=1;Check(!s.Step().bowDrawing,"trigger outside grab volume waits for approach");
        s.Move({0,1.3f,-.34f});Check(s.out.bowDrawing,"pre-squeezed trigger grabs when brought to string");
        s.in.cancel=true;Check(!s.Step().bowRelease&&!s.out.attack,"B cancels draw");s.in.cancel=false;
        Check(!s.Step(3).bowDrawing,"cancelled held trigger cannot renock until released");
    }
    {
        Sim s;s.in.weapon=Weapon::Bow;s.in.hand[1].p={0,1.3f,-.5f};s.in.hand[0].p={0,1.3f,-.12f};s.Step(2);
        s.in.trigger=1;Check(s.Step().bowDrawing,"generous string-side nock region accepts comfortable hand spacing");
        s.Step(10);s.in.trigger=0;Check(!s.Step().bowRelease&&!s.out.attack,"nocking already apart still requires additional deliberate draw");
    }
    {
        Sim s;s.in.palette=true;s.in.hover=3;s.in.trigger=1;s.Step();s.in.palette=false;s.in.trigger=0;s.Step();
        s.in.weapon=Weapon::Bow;++s.in.equipment;s.Step();s.Step();
        Check(s.engine.Selection()==-1&&s.out.bowReady,"equipping a bow automatically enters bow mode");
        s.in.palette=true;s.in.hover=2;s.in.trigger=1;s.Step();s.in.palette=false;s.in.trigger=0;s.Step();
        s.in.active=false;s.Step();s.in.active=true;s.Step(2);
        Check(s.engine.Selection()==2,"tracking or menus preserve an explicitly selected spell on the same bow");
    }
    {
        Sim s;s.Nock();s.in.trigger=0;Check(!s.Step().bowRelease&&!s.out.attack,"short draw does not shoot");
        s.Nock();s.Move({0,1.3f,0});s.in.cancel=true;s.in.trigger=0;Check(!s.Step().bowRelease&&!s.out.attack,"explicit bow cancel never shoots");
    }
    {
        Sim s;s.Nock();s.Move({0,1.3f,0});s.in.tracked[1]=false;Check(!s.Step().bowRelease&&!s.out.attack,"tracking loss cancels draw");
        s.in.tracked[1]=true;s.in.trigger=0;Check(!s.Step(4).bowRelease,"tracking restoration never fires old draw");
        s.Nock();s.Move({0,1.3f,0});++s.in.equipment;s.in.trigger=0;Check(!s.Step().bowRelease&&!s.out.attack,"weapon swap cancels draw");
    }
    {
        Sim s;s.in.palette=true;s.in.hover=5;s.in.trigger=1;
        Check(!s.Step().attack&&s.engine.Selection()==5,"palette selects skill without casting");
        s.in.palette=false;Check(s.Step().skill<0,"palette trigger cannot leak into casting");
        s.in.trigger=0;s.Step();s.in.trigger=1;
        Check(s.Step().skill==5&&!s.out.attack,"selected skill routes separately from default attack");
        Check(SkillButtons(5)==0x4000&&SkillModifier(5),"second-bank slot maps to LT plus X");
        Check(SkillButtons(-1)==0&&!SkillModifier(-1),"invalid skill cannot map to native buttons");
    }
    {
        Sim s;s.in.palette=true;s.in.hover=13;s.in.grip[0]=1;
        Check(s.Step().bottle==1&&s.out.potion<0,"grabbing belt column does not consume it");
        s.in.palette=false;s.Move({0,1.55f,-.10f});
        Check(s.Step(20).potion<0,"upright potion near mouth does not drink");
        s.in.hand[0].q={0,0,.70710678f,.70710678f};
        int drinks=0;
        for(int i=0;i<40;++i) {auto out=s.Step();drinks+=out.potion==1;Check(!out.attack&&out.skill<0,"bottle suppresses combat");}
        Check(drinks==1,"tilted potion dwell consumes once per grab");
        s.in.trigger=1;s.Step();s.in.grip[0]=0;s.Step();
        Check(!s.Step().attack,"bottle release cannot leak held trigger");
    }
    {
        Sim s;s.in.palette=true;s.in.hover=12;s.in.grip[0]=1;s.Step();s.in.active=false;s.Step();s.in.active=true;s.in.palette=false;
        s.Move({0,1.55f,-.1f});s.in.hand[0].q={0,0,1,0};
        for(int i=0;i<30;++i)Check(s.Step().potion<0,"menu cancels carried potion without consuming");
        s.in.trigger=1;s.in.aim.q.w=std::numeric_limits<float>::quiet_NaN();Check(!s.Step().attack,"invalid pose fails closed");
    }
    {
        Sim s;s.in.trigger=1;s.Step();s.in.time+=.2;Check(!s.Step().attack,"long frame gap cancels attacks");
        Check(!s.Step().attack,"stale held trigger must be released");
        for(int row=0;row<4;++row)for(int col=0;col<4;++col)
            Check(PaletteCell((col+.5f)/4,.125f+(row+.5f)*.1875f)==row*4+col,"palette hit regions match atlas");
        Check(PaletteCell(1,.5f)==-1&&PaletteCell(.5f,.95f)==-1,"palette margins never select");
    }
    std::puts("Motion gameplay: gesture, cancellation, input ownership and palette tests passed");
}
