#include "whirl_on.h"
#include "whirl_steer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace framevr::whirlon;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(double a,double b,double e=1e-3){return std::fabs(a-b)<=e;}
// A live frame of motion combat in the field: full mana, Whirlwind on the right trigger's record and reading back.
static Input Live(double t) {
    Input in;in.time=t;in.hero=7;in.combat=true;in.live=true;in.available=true;in.manaKnown=true;in.mana=500;in.level=20;
    in.press=Press::Record;in.slot=5;in.recordHolds=true;
    return in;
}
// Steps the mode from `from` to `to` s every 20 ms with the same input otherwise; the first stop, if any.
static Output Run(Mode& m,Input in,double from,double to) {
    Output last;
    for(double t=from;t<=to+1e-9;t+=.02){in.time=t;last=m.Step(in);if(last.stopped!=Why::None)return last;}
    return last;
}
int main(){
    {   // Its time by Whirlwind's level, +skills counted: 5 s at 1, a tenth of a second a level, at most 8 s.
        Check(Near(Seconds(1),5)&&Near(Seconds(2),5.1)&&Near(Seconds(11),6)&&Near(Seconds(20),6.9),"5 s at level 1, 0.1 s a level");
        Check(Near(Seconds(25),7.4)&&Near(Seconds(30),7.9),"endgame +skills: 7.4-7.9 s at 25-30");
        Check(Near(Seconds(31),8)&&Near(Seconds(45),8)&&Near(Seconds(1000),8),"capped at 8 s from level 31");
        Check(Near(Seconds(0),5)&&Near(Seconds(-3),5),"a level unread: 5 s");
        for(int l=1;l<60;++l)Check(Seconds(l+1)>=Seconds(l),"never shorter for a higher level");
    }
    {   // A whirl's mana, the game's sum from skills.txt: 12.5 at level 1, half a point a level.
        Check(Near(WhirlMana(1),12.5)&&Near(WhirlMana(20),22)&&Near(WhirlMana(30),27),"12.5 / 22 / 27 mana at levels 1 / 20 / 30");
        Check(Near(WhirlMana(0),12.5)&&Near(WhirlMana(-5),12.5),"a level unread counts as 1");
    }
    {   // A whirl's length: a second and a half of the speed whirls go, 24 to 36 cells (three times the first 8-12).
        Check(Near(SegmentCells(DefaultSpeed),24)&&Near(SegmentCells(9.4f),24),"slow whirls: 24 cells");
        Check(Near(SegmentCells(20),30)&&Near(SegmentCells(24),36)&&Near(SegmentCells(40),36),"20 cells/s: 30; 24 and faster: 36");
        Check(Near(SegmentCells(NAN),24)&&Near(SegmentCells(-1),24)&&Near(SegmentCells(0),24),"no speed: the default's length");
        // Mana a second against the first version's whirls (half a second, 8 to 12 cells): a third at any speed.
        for(float s=5;s<=40;s+=.5f) {
            const float first=std::clamp(s*.5f,8.f,12.f);
            Check(Near((s/SegmentCells(s))/(s/first),1/3.,1e-4),"a third of the mana a second");
        }
    }
    {   // The chain: whirls the server's hero began (else the commands taken), the ground whirled and its speed.
        Chain c;
        Check(!c.taken&&c.Took(10,100,100,24)&&c.count==1&&c.taken,"the first command counts, and a whirl is taken");
        Check(!c.Took(10.05,100.4,100,24)&&c.count==1,"its command again at once: the same whirl");
        Check(!c.Took(10.6,108,100,24)&&c.count==1,"a third of the way: the same whirl (the game refuses that one)");
        Check(c.Took(11.8,124,100,24)&&c.count==2,"as far as its length: the next");
        Check(!c.measured&&Near(c.speed,DefaultSpeed),"commands alone measure no speed");
        c.Started(12);
        Check(c.count==1,"the server's whirls seen: they are the count from then on");
        Check(!c.Took(14,160,100,24)&&c.count==1,"commands no longer count");
        c.Seen(12,100,100);c.Seen(12.04,100.6,100);c.Seen(12.08,101.2,100);   // 15 cells/s
        for(int i=3;i<=25;++i)c.Seen(12+.04*i,100+.6f*i,100);
        Check(Near(c.moved,15,1e-3),"the ground whirled: 15 cells in a second");
        c.Seen(13.5,115,100);   // a pause between whirls: no ground, no time
        c.Seen(13.54,115.6,100);
        c.Started(13.5);
        Check(c.count==2&&c.measured&&Near(c.speed,15,1e-2),"15 cells/s, the stop between whirls left out");
        Check(Near(c.Cells(),24),"15 cells/s: the next whirl 24 cells");
        Chain j;j.Seen(0,10,10);j.Seen(.04,90,10);
        Check(Near(j.moved,0),"a jump far past a whirl (a waypoint) is no ground");
        Check(!j.Took(NAN,1,1,8)&&!j.Took(1,1,1,0),"nonsense is no whirl");
        Chain s;s.speed=20;s.measured=true;s.Begin();
        Check(s.count==0&&s.moved==0&&!s.taken&&Near(s.speed,20),"a new turn-on keeps the speed");
        s.Forget();
        Check(Near(s.speed,DefaultSpeed)&&!s.measured,"another hero: the default again");
    }
    {   // The stick: whirls on while pushed, the first before any push; let go after a push, they stop.
        Mode m;Input in=Live(0);in.motion=true;
        auto o=m.Step(in);in.motion=false;
        Check(o.started&&o.holdRecord&&!o.stop,"on at rest: the first whirl, where you face");
        in.taken=true;in.time=.2;o=m.Step(in);
        Check(o.on&&!o.holdRecord&&!o.stop,"its first whirl taken, the stick never pushed: no more, nothing to cut");
        in.pushed=true;in.time=.3;o=m.Step(in);
        Check(o.holdRecord&&!o.stop,"pushed: whirls on");
        in.pushed=false;in.time=.38;o=m.Step(in);
        Check(o.holdRecord&&!o.stop,"through the stick's centre (under 0.15 s): still whirling");
        in.time=.46;o=m.Step(in);
        Check(o.on&&!o.holdRecord&&o.stop,"let go: stops, the whirl under way ends short, still on");
        in.time=2;o=m.Step(in);
        Check(o.on&&o.stop&&!o.holdRecord,"standing, still on");
        in.pushed=true;in.time=2.1;o=m.Step(in);
        Check(o.holdRecord&&!o.stop,"pushed again: whirls again");
        in.pushed=false;o=Run(m,in,2.12,7);
        Check(o.stopped==Why::Time,"its time runs out as ever");
    }
    {   // Its button held: whirls on whatever the stick; let go after a hold, off.
        Mode m;Input in=Live(0);in.press=Press::Slot;in.button=3;
        auto o=m.Step(in);
        in.taken=true;in.time=.3;o=m.Step(in);
        Check(o.on&&o.holdSlot==3&&!o.stop,"held at rest: whirls on");
        in.pushed=true;in.time=.35;o=m.Step(in);in.pushed=false;in.time=.6;o=m.Step(in);
        Check(o.holdSlot==3&&!o.stop,"held: letting go of the stick does not stop it");
        in.button=-1;in.time=.7;o=m.Step(in);
        Check(o.stopped==Why::Released,"the button let go after a hold: off");
    }
    {   // On by the T: held on the right trigger's record, off when its time runs out (level 20: 6.9 s).
        Mode m;Input in=Live(0);in.motion=true;
        auto o=m.Step(in);
        Check(o.started&&o.by==By::Motion&&o.on&&o.holdRecord&&o.holdSlot<0&&Near(o.seconds,6.9),"the T turns it on for 6.9 s at level 20");
        in.motion=false;
        o=Run(m,in,.02,6.8);
        Check(o.on&&o.holdRecord&&o.stopped==Why::None,"still on, the record held, at 6.8 s");
        o=Run(m,in,6.82,7.2);
        Check(o.stopped==Why::Time&&!o.on&&!o.holdRecord&&o.by==By::Motion&&Near(o.elapsed,6.9,.03),"off as its time runs out");
        in.time=8;in.motion=true;o=m.Step(in);
        Check(o.started,"and on again with the next T");
    }
    {   // A level unread: 5 s.
        Mode m;Input in=Live(0);in.level=0;in.motion=true;
        Check(Near(m.Step(in).seconds,5),"a level unread: 5 s");
    }
    {   // Whirlwind on a hand slot: that slot held.
        Mode m;Input in=Live(0);in.motion=true;in.press=Press::Slot;in.slot=3;
        const auto o=m.Step(in);
        Check(o.on&&o.holdSlot==3&&!o.holdRecord,"its hand slot held");
    }
    {   // Not yet on the record: put there, held once it reads back; never read back, off.
        Mode m;Input in=Live(0);in.motion=true;in.press=Press::Park;in.recordHolds=false;
        auto o=m.Step(in);
        Check(o.started&&o.park&&!o.holdRecord,"put on the record first, nothing held yet");
        in.motion=false;in.time=.1;o=m.Step(in);
        Check(o.on&&o.park&&!o.holdRecord,"still asked while it has not read back");
        in.recordHolds=true;in.time=.15;o=m.Step(in);
        Check(o.on&&!o.park&&o.holdRecord,"read back: held");
        Mode n;Input p=Live(0);p.motion=true;p.press=Press::Park;p.recordHolds=false;n.Step(p);p.motion=false;
        o=Run(n,p,.02,1);
        Check(o.stopped==Why::Record&&Near(o.elapsed,RecordWaitSeconds,.03),"never read back: off after 0.6 s");
        Mode r;Input q=Live(0);q.motion=true;r.Step(q);q.motion=false;q.recordHolds=false;
        o=Run(r,q,.02,1);
        Check(o.stopped==Why::Record,"the record taken by something else while on: off");
    }
    {   // On by its button, tapped: on for its time whether held or not; pressed again: off, the press not the game's.
        Mode m;Input in=Live(0);in.button=4;in.press=Press::Record;
        auto o=m.Step(in);
        Check(o.started&&o.by==By::Button&&o.consume&&o.holdSlot==4&&!o.holdRecord,"a press turns it on, holding its own slot");
        in.time=.15;o=m.Step(in);
        Check(o.on&&o.consume&&o.holdSlot==4,"held: still its press");
        in.button=-1;in.time=.2;o=m.Step(in);
        Check(o.on&&!o.consume&&o.holdSlot==4&&o.stopped==Why::None,"let go quickly: a tap, it stays on");
        o=Run(m,in,.22,4);
        Check(o.on&&o.stopped==Why::None,"on at 4 s");
        in.button=4;in.time=4.1;o=m.Step(in);
        Check(o.stopped==Why::Button&&!o.on&&o.consume&&o.holdSlot<0,"pressed again: off, and the press is not the game's");
        in.time=4.3;o=m.Step(in);
        Check(!o.on&&!o.started&&o.consume,"still held after: nothing new, still not the game's");
        in.button=-1;in.time=4.4;o=m.Step(in);
        Check(!o.consume&&!o.on,"let go");
        in.button=4;in.time=4.6;o=m.Step(in);
        Check(o.started,"pressed again: on again");
    }
    {   // A tap runs out with its time; level 1: 5 s.
        Mode m;Input in=Live(0);in.level=1;in.button=2;m.Step(in);
        in.button=-1;
        const auto o=Run(m,in,.1,6);
        Check(o.stopped==Why::Time&&Near(o.elapsed,5,.03),"tapped at level 1: off after 5 s");
    }
    {   // Held, it chains as the game's own hold did: on while held, past its time too, off when let go.
        Mode m;Input in=Live(0);in.level=1;in.button=1;m.Step(in);
        auto o=Run(m,in,.02,7);
        Check(o.on&&o.stopped==Why::None&&o.holdSlot==1,"held 7 s, past its 5: still on");
        in.button=-1;in.time=7.02;o=m.Step(in);
        Check(o.stopped==Why::Released&&!o.consume,"let go: off");
        Mode n;Input p=Live(0);p.button=1;n.Step(p);
        o=Run(n,p,.02,1);p.button=-1;p.time=1.02;o=n.Step(p);
        Check(o.stopped==Why::Released,"held a second and let go before its time: off, as the game's hold");
    }
    {   // The T again, or another cast, turns it off.
        Mode m;Input in=Live(0);in.motion=true;m.Step(in);in.motion=false;Run(m,in,.02,1);
        in.time=1.1;in.motion=true;auto o=m.Step(in);
        Check(o.stopped==Why::Motion&&!o.on,"the T again: off");
        in.time=1.2;in.motion=false;o=m.Step(in);
        Check(!o.on&&!o.started,"and stays off");
        Mode n;Input p=Live(0);p.motion=true;n.Step(p);p.motion=false;p.time=.5;p.other=true;o=n.Step(p);
        Check(o.stopped==Why::Other,"another cast or action: off");
        Mode k;Input q=Live(0);q.button=3;k.Step(q);q.motion=true;q.time=.3;o=k.Step(q);
        Check(o.stopped==Why::Motion,"on by its button, the T turns it off too");
    }
    {   // Out of mana: off once the mana is below a whirl and no whirl is under way; refused from the start.
        Mode m;Input in=Live(0);in.motion=true;m.Step(in);in.motion=false;
        in.mana=10;in.whirling=true;in.time=.5;auto o=m.Step(in);
        Check(o.on&&o.stopped==Why::None,"a whirl under way finishes first (its hits may leech mana back)");
        in.mana=30;in.whirling=false;in.time=.6;o=m.Step(in);
        Check(o.on,"leeched back above a whirl's 22: on");
        in.mana=21.5f;in.time=.7;o=m.Step(in);
        Check(o.stopped==Why::Mana,"below one whirl's 22 mana at level 20, between whirls: off");
        Mode n;Input p=Live(0);p.mana=12.5f;p.level=1;p.motion=true;o=n.Step(p);
        Check(o.on&&o.started,"exactly one whirl's 12.5 mana at level 1: on");
        Mode r;Input q=Live(0);q.level=1;q.motion=true;q.mana=12.4f;o=r.Step(q);
        Check(!o.on&&!o.started&&o.refused&&std::strstr(o.refused,"mana"),"short of one whirl: not turned on");
        Mode u;Input w=Live(0);w.manaKnown=false;w.mana=0;w.motion=true;o=u.Step(w);
        Check(o.on,"mana unread: no mana rule");
    }
    {   // Town, a menu, death, another game: off at once; and never turned on there.
        const auto stopsOn=[](void(*set)(Input&),Why why) {
            Mode m;Input in=Live(0);in.motion=true;m.Step(in);in.motion=false;in.time=.3;set(in);
            const auto o=m.Step(in);
            return o.stopped==why&&!o.on&&o.holdSlot<0&&!o.holdRecord;
        };
        Check(stopsOn([](Input& i){i.town=true;},Why::Town),"town: off");
        Check(stopsOn([](Input& i){i.menu=true;},Why::Menu),"a menu: off");
        Check(stopsOn([](Input& i){i.dead=true;},Why::Died),"death: off");
        Check(stopsOn([](Input& i){i.hero=8;},Why::Left),"another game: off");
        Check(stopsOn([](Input& i){i.combat=false;},Why::Off),"motion combat off: off");
        Check(stopsOn([](Input& i){i.available=false;},Why::Unavailable),"Whirlwind unavailable: off");
        const auto refused=[](void(*set)(Input&)) {
            Mode m;Input in=Live(0);in.motion=true;set(in);
            const auto o=m.Step(in);
            return !o.on&&!o.started&&o.refused!=nullptr;
        };
        Check(refused([](Input& i){i.town=true;}),"not in town");
        Check(refused([](Input& i){i.menu=true;}),"not with a menu");
        Check(refused([](Input& i){i.dead=true;}),"not dead");
        Check(refused([](Input& i){i.available=false;}),"not unavailable");
        Check(refused([](Input& i){i.press=Press::None;}),"not without a way to press it");
        Check(refused([](Input& i){i.live=false;}),"not with the hands taken");
        Mode m;Input in=Live(0);in.town=true;in.button=2;auto o=m.Step(in);
        Check(!o.on&&o.consume&&o.refused,"its button in town: refused, the press still not the game's");
    }
    {   // The hands taken a moment (a visor wipe, tracking): nothing pressed meanwhile, on again after; a second: off.
        Mode m;Input in=Live(0);in.motion=true;m.Step(in);in.motion=false;
        in.live=false;in.time=.3;auto o=m.Step(in);
        Check(o.on&&!o.holdRecord&&o.holdSlot<0,"paused: nothing pressed");
        in.live=true;in.time=.5;o=m.Step(in);
        Check(o.on&&o.holdRecord,"back: held again");
        in.live=false;o=Run(m,in,.52,2);
        Check(o.stopped==Why::Lost&&Near(o.elapsed,1.52,.03),"taken a second: off");
    }
    {   // Reset: gone, nothing pressed.
        Mode m;Input in=Live(0);in.motion=true;m.Step(in);m.Reset();
        Check(!m.On(),"reset: off");
    }
    {   // The reasons read as words.
        for(int w=1;w<=int(Why::Off);++w)Check(std::strcmp(Name(Why(w)),"-")!=0,"every stop has its words");
    }
    {   // A whirl ahead is never past the game's reach, so its spot is never shortened.
        Check(MaxCells<=framevr::aimreach::GameCells,"inside the game's reach");
        float x=0,y=0;
        Check(framevr::whirlsteer::Spot(5000,4000,.6f,.8f,MaxCells,x,y)&&Near(std::hypot(x-5000,y-4000),MaxCells),"the longest whirl's spot is whole");
    }
    std::puts("whirl-on-test: ok");
}
