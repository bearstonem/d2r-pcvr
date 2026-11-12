#include "gameplay.h"
#include "hand_role.h"
#include "toss.h"
#include "telegraph.h"
#include <cstdio>
#include <cstdlib>
// A throwing potion in either hand (weapon_skills.h RoutePotions, gameplay.h Input::tossHand / offToss):
// which trigger arms which hand's throw, the off trigger's toss, both hands at once, the mirrored
// (left-handed) case, and each hand's flight to its sigil.
using namespace framevr;
using namespace framevr::gameplay;
using weaponskills::Held;
static void Check(bool b,const char* message) {if(!b){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static weaponskills::PotionRoute Route(Held main,Held off,int mainSkill,int offSkill) {
    const Held held[2]={main,off};const int skill[2]={mainSkill,offSkill};
    return weaponskills::RoutePotions(held,skill);
}
static bool Is(const weaponskills::PotionRoute& r,int mainHand,int offHand){return r.trigger[0]==mainHand&&r.trigger[1]==offHand;}
// Controllers in physical order (0 right, 1 left), fed to the engine in role order through a Map, as the
// transport does; Mirror is the same play seen in a mirror.
struct Controllers {Vec hand[2]{{.25f,1.25f,-.2f},{-.25f,1.25f,-.2f}};float trigger[2]{};};
static Vec Mirror(Vec v){return {-v.x,v.y,v.z};}
static Controllers Mirror(Controllers c){std::swap(c.hand[0],c.hand[1]);std::swap(c.trigger[0],c.trigger[1]);for(auto& h:c.hand)h=Mirror(h);return c;}
static bool Near(Vec a,Vec b,float e=1e-4f){return Length(a-b)<e;}
struct Play {
    handrole::Map map;Engine engine;Input in;Output out;
    explicit Play(handrole::Map m={}):map(m) {
        in.active=true;in.tracked[0]=in.tracked[1]=in.aimTracked=in.offAimTracked=true;in.head.p={0,1.65f,0};
        in.weapon=Weapon::Melee;in.equipment=1;in.handBindings=true;
        in.offSlot=skills::TriggerSlot(1);in.offSkill=weaponskills::Attack;in.offOwner=0;
        Step({});Step({});Step({});
    }
    Output Step(const Controllers& c) {
        in.time+=.02;
        for(int role=0;role<2;++role)in.hand[role].p=c.hand[map.Physical(role)];
        in.trigger=c.trigger[map.Physical(0)];in.leftTrigger=c.trigger[map.Physical(1)];
        in.aim=in.hand[0];in.offAim=in.hand[1];
        return out=engine.Step(in);
    }
    // The main trigger's skill, as the transport selects it; a potion thrown by `hand` when potion.
    void Main(int skill,bool potion=false,int hand=0) {
        in.selectedSkill=skill;in.selectedAvailable=true;in.toss=in.potion=potion;in.tossHand=hand;
        engine.Select(skills::TriggerSlot(0));Step({});Step({});Step({});
    }
    // One physical hand held down on its trigger and thrown forward; the release step's output.
    Output Throw(Controllers& c,int hand,int trigger,const handrole::Map& seen={}) {
        const auto step=[&](const Controllers& now){return Step(seen.left?Mirror(now):now);};
        c.trigger[trigger]=1;step(c);
        for(int i=0;i<6;++i){c.hand[hand].z-=.07f;c.hand[hand].y+=.02f;step(c);}
        c.trigger[trigger]=0;c.hand[hand].z-=.07f;return step(c);
    }
};
int main() {
    const int A=weaponskills::Attack,T=weaponskills::Throw,L=weaponskills::LeftHandThrow,none=-1,other=251;
    {   // which trigger arms which hand's throw
        Check(Is(Route(Held::Potion,Held::None,A,other),0,-1)&&Is(Route(Held::Potion,Held::Shield,T,other),0,-1),
              "a potion in the main hand: its own trigger's Attack or Throw, as before");
        Check(Is(Route(Held::Potion,Held::Weapon,A,A),0,-1),"beside an off hand weapon too: Attack throws the main hand's");
        Check(Is(Route(Held::None,Held::Potion,other,A),-1,1)&&Is(Route(Held::Shield,Held::Potion,other,T),-1,1)&&Is(Route(Held::None,Held::Potion,A,L),-1,1),
              "a potion alone in the off hand: its own trigger's Attack, Throw or Left Hand Throw");
        Check(Is(Route(Held::None,Held::Potion,A,other),-1,0)&&Is(Route(Held::Shield,Held::Potion,T,none),-1,0),
              "its own trigger holding another skill: the main trigger's Attack or Throw arms the off hand's throw");
        Check(Is(Route(Held::Potion,Held::None,other,A),1,-1),"and the same the other way: the off trigger arms the main hand's");
        Check(Is(Route(Held::None,Held::Potion,other,other),-1,-1)&&Is(Route(Held::Potion,Held::None,none,none),-1,-1),
              "no Attack or Throw on either trigger: the game's own button");
        Check(Is(Route(Held::Weapon,Held::Potion,A,A),-1,-1)&&Is(Route(Held::Weapon,Held::Potion,A,T),-1,-1),
              "an off hand potion beside a main hand weapon: Attack and Throw would throw the weapon, so no hand throw");
        Check(Is(Route(Held::Weapon,Held::Potion,A,L),-1,1),"Left Hand Throw on its trigger throws it");
        Check(Is(Route(Held::Potion,Held::Potion,A,L),0,1)&&Is(Route(Held::Potion,Held::Potion,T,L),0,1),"a potion in each hand: each its own trigger");
        Check(Is(Route(Held::Potion,Held::Potion,A,A),0,-1)&&Is(Route(Held::Potion,Held::Potion,other,L),-1,1),
              "of two potions, only the hand whose trigger throws its own");
        Check(Is(Route(Held::Weapon,Held::Shield,A,A),-1,-1)&&Is(Route(Held::None,Held::None,A,T),-1,-1),"no potion, no route");
        const auto both=Route(Held::Potion,Held::Potion,A,L),borrowed=Route(Held::None,Held::Potion,A,other);
        Check(both.Hand(0)==0&&both.Hand(1)==1&&borrowed.Hand(0)==1&&borrowed.Hand(1)==-1,"the hand a trigger throws");
        // the two hands are symmetrical while each is the hero's only weapon
        for(const int main:{A,T,other,none})for(const int off:{A,T,other,none}) {
            const auto right=Route(Held::Potion,Held::None,main,off),left=Route(Held::None,Held::Potion,off,main);
            Check(right.trigger[0]==(left.trigger[1]<0?-1:1-left.trigger[1])&&right.trigger[1]<0&&left.trigger[0]<0,"either hand alone takes the same route, mirrored");
        }
    }
    const float lift=10;
    {   // the off trigger's toss: a quick press, a still hold, a throw - as the main trigger's
        Play p;p.in.offToss=true;p.in.throwLift=lift;Controllers c;
        c.trigger[1]=1;p.Step(c);
        Check(p.out.offTossHeld&&p.out.leftFeedback==1&&p.out.skill<0&&!p.out.tossHeld,"the off trigger holds the potion ready without casting yet");
        c.trigger[1]=0;p.Step(c);
        Check(p.out.skill==skills::TriggerSlot(1)&&p.out.offPress&&!p.out.offTossRelease&&!p.out.offTossHeld,"a quick still press throws where the off hand points");
        Check(Near(p.out.rayOrigin,p.in.hand[1].p),"aimed from the off hand");
        for(int i=0;i<20;++i)p.Step(c);
        Check(p.out.skill<0&&!p.out.offPress,"one short press");
        c.trigger[1]=1;for(int i=0;i<18;++i)p.Step(c);
        Check(p.out.skill<0&&p.out.offTossHeld,"held still a moment, it is still in the hand");
        for(int i=0;i<4;++i)p.Step(c);
        Check(p.out.skill==skills::TriggerSlot(1)&&p.out.offPress&&!p.out.offTossHeld,"held still, the game's own throw holds as before");
        c.trigger[1]=0;p.Step(c);Check(p.out.skill<0,"until the trigger is let go");
        for(int i=0;i<10;++i)p.Step(c);
        const auto out=p.Throw(c,1,1);
        Check(out.offTossRelease&&out.leftFeedback==2&&out.skill==skills::TriggerSlot(1)&&out.offPress&&!out.offTossHeld&&!out.tossRelease,
              "a throw releases the potion with one native press of the off trigger's slot");
        const Vec v=out.offTossVelocity;const float pitch=std::atan2(v.y,std::sqrt(v.x*v.x+v.z*v.z))*180/3.14159265f;
        Check(v.z<-3&&Length(v)<=TossMaxSpeed&&pitch>lift,"along the off hand's motion, lifted like any throw");
        Check(Near(out.offTossOrigin,c.hand[1])&&out.offTossOrigin.x<0,"from the off hand");
    }
    {   // the same motion by either hand gives the same throw, mirrored
        Play main;main.Main(A,true);
        Controllers a;const auto right=main.Throw(a,0,0);
        Play off;off.in.offToss=true;Controllers b;const auto left=off.Throw(b,1,1);
        Check(right.tossRelease&&left.offTossRelease,"each hand throws its own");
        Check(Near(left.offTossOrigin,Mirror(right.tossOrigin))&&Near(left.offTossVelocity,Mirror(right.tossVelocity),1e-3f),
              "the off hand's release point and velocity are the main hand's, mirrored");
        Check(right.skill==skills::TriggerSlot(0)&&left.skill==skills::TriggerSlot(1),"each presses its own trigger's slot");
    }
    {   // the main trigger arming the off hand's throw (the potion alone in the off hand, Attack on the main trigger)
        Play p;p.Main(A,true,1);
        Controllers c;p.Step(c);
        Controllers still=c;still.trigger[0]=1;p.Step(still);
        for(int i=0;i<6;++i){still.hand[0].z-=.07f;p.Step(still);}   // the main hand moving is not the throw
        still.trigger[0]=0;p.Step(still);
        Check(!p.out.tossRelease,"the hand without the potion does not throw it");
        for(int i=0;i<10;++i)p.Step(c);
        const auto out=p.Throw(c,1,0);
        Check(out.tossRelease&&out.skill==skills::TriggerSlot(0)&&Near(out.tossOrigin,c.hand[1])&&!out.meleeContact,
              "the off hand throws it, armed by the main trigger, with that trigger's native press");
    }
    {   // a potion in each hand: each throws its own, and both can be held ready at once
        Play p;p.Main(T,true);p.in.offToss=true;p.in.offSkill=L;
        Controllers c;c.trigger[0]=c.trigger[1]=1;p.Step(c);
        Check(p.out.tossHeld&&p.out.offTossHeld,"both held ready");
        for(int i=0;i<6;++i){c.hand[0].z-=.07f;c.hand[1].z-=.07f;p.Step(c);}
        c.trigger[1]=0;c.hand[0].z-=.07f;c.hand[1].z-=.07f;p.Step(c);
        Check(p.out.offTossRelease&&!p.out.tossRelease&&p.out.tossHeld&&p.out.skill==skills::TriggerSlot(1),"the off hand lets go first: its own potion, its own press");
        c.trigger[0]=0;c.hand[0].z-=.07f;p.Step(c);
        Check(p.out.tossRelease&&!p.out.offTossRelease&&p.out.skill==skills::TriggerSlot(0)&&!p.out.offPress,"then the main hand's: its press comes first");
        bool offAgain=false;for(int i=0;i<8;++i){p.Step(c);offAgain|=p.out.offPress;}
        Check(offAgain,"and the off trigger's press finishes after it");
    }
    {   // an off trigger toss beside a main trigger skill held down, and beside a thrown weapon in the main hand
        Play p;p.Main(other);p.in.offToss=true;
        Controllers c;c.trigger[0]=1;p.Step(c);
        Check(p.out.skill==skills::TriggerSlot(0),"the main trigger holds its skill");
        const auto out=p.Throw(c,1,1);
        Check(out.offTossRelease&&out.skill==skills::TriggerSlot(1)&&out.offPress,"the potion's press takes its turn");
        for(int i=0;i<20;++i)p.Step(c);
        Check(p.out.skill==skills::TriggerSlot(0)&&!p.out.offPress,"and the held skill resumes");
        Play j;j.in.weapon=Weapon::Javelin;j.in.offToss=true;j.in.offSkill=L;Controllers d;j.Step(d);j.Step(d);j.Step(d);
        Check(j.Throw(d,1,1).offTossRelease&&j.out.javelinReady,"beside a javelin in the main hand the off hand's potion is still thrown");
        Play dt;dt.in.weapon=Weapon::Javelin;dt.in.offToss=true;dt.in.leftThrow=true;Controllers e;dt.Step(e);dt.Step(e);dt.Step(e);
        Check(!dt.Throw(e,1,1).offTossRelease&&dt.out.leftRelease,"Double Throw's left hand keeps its own throw");
    }
    {   // without the route the off trigger is not the engine's
        Play p;Controllers c;const auto out=p.Throw(c,1,1);
        Check(!out.offTossRelease&&!out.offTossHeld&&!out.offPress&&out.skill<0,"no off toss unless a potion is routed to the off trigger");
    }
    {   // a wheel, a face button's cast or a cancel drops a potion held ready
        Play p;p.in.offToss=true;Controllers c;c.trigger[1]=1;p.Step(c);Check(p.out.offTossHeld,"held");
        p.in.quickCast=true;p.Step(c);p.in.quickCast=false;c.trigger[1]=0;
        bool cast=false;for(int i=0;i<8;++i){p.Step(c);cast|=p.out.offPress||p.out.offTossRelease;}
        Check(!cast,"a face button's cast lets it go without a throw");
    }
    {   // left-handed: the same play in a mirror - the main hand is the LEFT controller, the off hand the RIGHT
        const handrole::Map maps[2]={handrole::Map{},handrole::Map{true}};Output off[2],main[2];
        for(int lh=0;lh<2;++lh) {
            Play p(maps[lh]);p.Main(T,true);p.in.offToss=true;
            Controllers c;
            off[lh]=p.Throw(c,1,1,maps[lh]);
            for(int i=0;i<12;++i)p.Step(lh?Mirror(c):c);
            main[lh]=p.Throw(c,0,0,maps[lh]);
            Check(off[lh].offTossRelease&&main[lh].tossRelease,"either hand's potion is thrown, right- or left-handed");
        }
        Check(Near(off[1].offTossOrigin,Mirror(off[0].offTossOrigin))&&Near(off[1].offTossVelocity,Mirror(off[0].offTossVelocity),1e-3f)&&
              off[0].offTossOrigin.x<0&&off[1].offTossOrigin.x>0,"left-handed the off hand's potion leaves the RIGHT controller with the mirrored velocity");
        Check(Near(main[1].tossOrigin,Mirror(main[0].tossOrigin))&&Near(main[1].tossVelocity,Mirror(main[0].tossVelocity),1e-3f)&&main[1].tossOrigin.x<0,
              "and the main hand's the LEFT controller");
        Check(handrole::Map{true}.Physical(1)==0&&handrole::Map{}.Physical(1)==1,"the off hand's controller, for its log line and pulse");
    }
    {   // the sigil: either hand aims it alike, and each hand's throw flies the one arc that ends on it
        const math::Vec head{0,1.65f,0};const math::Quat facing=math::Yaw(.6f);
        const auto out=[&](float side,float forward){return head+math::Rotate(facing,math::Vec{side,-.3f,-forward});};
        Check(std::fabs(toss::Ahead(head,facing,out(.25f,.4f))-.4f)<1e-4f&&std::fabs(toss::Ahead(head,facing,out(-.25f,.4f))-.4f)<1e-4f,
              "a hand held out in front is as far ahead on either side");
        Check(toss::Aims(toss::Ahead(head,facing,out(-.25f,.4f)),.2f)&&!toss::Aims(toss::Ahead(head,facing,out(-.25f,-.2f)),.2f)&&
              !toss::Aims(toss::Ahead(head,facing,out(-.25f,.4f)),3.f),"the off hand aims out in front and slow, not wound up behind the head, not while it throws");
        const float floor=0,upm=2;const auto target=toss::World(140,230,floor,upm);
        float cells[2][2]{};
        for(int hand=0;hand<2;++hand) {
            const math::Vec p{target.x-6+(hand?-.25f:.25f),1.4f,target.z+3};math::Vec v{};
            Check(toss::Launch(p,target,7,v),"a launch from either hand");
            const float along=std::hypot(target.x-p.x,target.z-p.z),time=along/std::hypot(v.x,v.z);
            const double lands=interaction::LandingTime(p,v,floor);
            Check(lands>0&&std::fabs(float(lands)-time)<1e-3f,"the flight the thrown bottle really makes reaches the floor when the arc does");
            Check(toss::Cell(interaction::Ballistic(p,v,float(lands)),floor,upm,cells[hand][0],cells[hand][1]),"and lands in a cell");
            Check(std::fabs(cells[hand][0]-140)<.05f&&std::fabs(cells[hand][1]-230)<.05f,"the sigil's own cell");
        }
        Check(std::fabs(cells[0][0]-cells[1][0])<.05f&&std::fabs(cells[0][1]-cells[1][1])<.05f,"whichever hand threw it");
        static_assert(telegraph::TossSigil(telegraph::TossMark)&&telegraph::TossSigil(telegraph::TossOffMark)&&telegraph::TossMark!=telegraph::TossOffMark&&
            !telegraph::TossSigil(telegraph::TeleportMark)&&!telegraph::TossSigil(telegraph::CompanionMark)&&!telegraph::TossSigil(-1),"each trigger's throw has its own sigil mark");
        telegraph::Board board;
        Check(board.Place(140,230,1000,250,1,2,0,0,0,telegraph::TossMark)&&board.Place(150,230,1000,250,1,2,0,0,0,telegraph::TossOffMark)&&
              telegraph::SkillOn(board.Get(),telegraph::TossMark,1020)&&telegraph::SkillOn(board.Get(),telegraph::TossOffMark,1020),"both hands' sigils show together");
        board.ClearSkill(telegraph::TossOffMark);
        Check(telegraph::SkillOn(board.Get(),telegraph::TossMark,1020)&&!telegraph::SkillOn(board.Get(),telegraph::TossOffMark,1020),"and one going leaves the other");
    }
    std::puts("potion hands tests passed");
}
