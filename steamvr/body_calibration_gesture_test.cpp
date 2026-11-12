#include "body_calibration_gesture.h"
#include "haptic_pulse.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
using namespace framevr;
using namespace calibration;
static void Check(bool b,const std::string& label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label.c_str());std::exit(1);}}
static constexpr double Frame=1./90;
// The same standing T cross body_calibration_test.cpp starts from: hands free, every guard clear.
static TCrossInput Standing(){
    TCrossInput in;auto& s=in.pose;s.time=1;s.floor=s.headTracked=s.handTracked[0]=s.handTracked[1]=true;
    s.head={1,1.7f,2};s.hand[0]={1.8f,1.4f,2};s.hand[1]={.2f,1.4f,2};in.scanAnswered=true;return in;
}
struct Tally {int steps=0,buzzing=0,pulses=0,stops=0,opens=0,refused=0,lost=0;TCrossRefusal last=TCrossRefusal::None;TCrossLoss why=TCrossLoss::None;float after=0;bool scan=false;};
static Tally Run(TCrossGesture& g,TCrossInput& in,double seconds){
    Tally t;
    for(int i=0,n=int(seconds/Frame+.5);i<n;++i){
        in.pose.time+=Frame;const auto o=g.Step(in);++t.steps;
        t.buzzing+=o.buzz;t.pulses+=o.pulse;t.stops+=o.stop;t.opens+=o.open;t.scan=t.scan||o.scan;
        if(o.refused!=TCrossRefusal::None){++t.refused;t.last=o.refused;}
        if(o.lost!=TCrossLoss::None){++t.lost;t.why=o.lost;t.after=o.lostAfter;}
        Check(!o.pulse||o.buzz,"a pulse only while buzzing");
        Check(!o.open||!o.buzz,"the prompt opening ends the light buzz");
    }
    return t;
}
static bool Near(double value,double want,double within){return std::fabs(value-want)<=within;}
static std::string Slurp(const char* path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    Check(argc>=3,"usage: transport.cpp vrcam.cpp");
    {   // Off is really off: no reads, no state, no buzz, no log.
        TCrossGesture g;auto in=Standing();in.on=false;
        const auto t=Run(g,in,5);
        Check(!t.buzzing&&!t.pulses&&!t.opens&&!t.refused&&!t.scan&&!t.stops,"switched off, the T cross does nothing");
    }
    {   // Starts, stays on, opens: the buzz runs from the moment the pose settles until the prompt.
        TCrossGesture g;auto in=Standing();
        const auto settle=Run(g,in,TCrossSettleSeconds-.03);
        Check(!settle.buzzing&&!settle.pulses&&!settle.opens,"nothing before the pose has settled");
        const auto first=Run(g,in,.06);
        Check(first.buzzing>0&&first.pulses==1,"the buzz starts as the timer starts, one pulse at once");
        const auto hold=Run(g,in,1.8);
        Check(hold.buzzing==hold.steps,"the buzz stays on every frame of the hold");
        Check(Near(hold.pulses,1.8/TCrossBuzzEvery,2)&&!hold.opens&&!hold.stops,"one pulse per hand-interval, no pile-up, nothing else");
        Check(double(TCrossBuzzMs)/1000>TCrossBuzzEvery+1./30,"each pulse outlasts the gap to the next, even at 30 frames a second: one buzz, not a stutter");
        const auto end=Run(g,in,.3);
        Check(end.opens==1,"the prompt opens once, about 2 seconds after the buzz began");
        const auto after=Run(g,in,3);
        Check(!after.opens&&!after.pulses&&!after.buzzing,"still in the pose: no second prompt and no buzz");
    }
    {   // The hold is 2 seconds to within a frame or two.
        TCrossGesture g;auto in=Standing();double began=0,opened=0;
        for(int i=0;i<600&&opened==0;++i){in.pose.time+=Frame;const auto o=g.Step(in);if(o.pulse&&began==0)began=in.pose.time;if(o.open)opened=in.pose.time;}
        Check(began>0&&opened>0&&Near(opened-began,TCrossHoldSeconds,2*Frame),"2 seconds from the first pulse to the prompt");
    }
    {   // Losing the pose: the buzz stops at once, the timer resets, coming back buzzes and times again.
        TCrossGesture g;auto in=Standing();Run(g,in,1.5);
        auto out=in;out.pose.hand[0]={1.3f,1.1f,2.2f};out.pose.hand[1]={.7f,1.1f,2.2f};out.pose.time=in.pose.time+Frame;
        // (arms dropping is not instant in life; the speed rule ends it too)
        const auto left=Run(g,out,.3);
        Check(left.stops==1,"leaving stops the controllers, once");
        Check(left.buzzing<=int(TCrossGraceSeconds/Frame)+1,"within the short grace");
        const auto away=Run(g,out,1);
        Check(!away.buzzing&&!away.pulses&&!away.stops&&!away.opens,"away: silent");
        in.pose.time=out.pose.time+Frame;
        const auto back=Run(g,in,TCrossSettleSeconds+1.5);
        Check(back.buzzing>0&&back.pulses>0&&!back.opens,"back in: buzz again, and the timer started over (1.5 s is not enough)");
        Check(Run(g,in,.6).opens==1,"the full 2 seconds opens it");
    }
    {   // Small wobbles do not break it: past the capture's limits, inside the held ones.
        const auto base=Standing();
        Check(CheckPose(base.pose,TCrossEnter)==Hint::Pose&&CheckPose(base.pose)==Hint::Pose,"the capture and the start accept the same pose");
        auto noFloor=base;noFloor.pose.floor=false;
        Check(CheckPose(noFloor.pose,TCrossEnter)==Hint::Pose&&CheckPose(noFloor.pose)==Hint::Floor,"the gesture asks about the floor itself; the capture still refuses");
        auto tilt=base;tilt.pose.hand[0].y+=.06f;tilt.pose.hand[1].y-=.06f;   // 12 cm out of level: the capture allows 9
        Check(CheckPose(tilt.pose,TCrossEnter)==Hint::Arms&&CheckPose(tilt.pose,TCrossKeep)==Hint::Pose,"a sagging arm: too far to start, fine to keep");
        auto nod=base;nod.pose.rotation={std::sin(.17f),0,0,std::cos(.17f)};   // 19.5 degrees: the capture allows 16
        Check(CheckPose(nod.pose,TCrossEnter)==Hint::Upright&&CheckPose(nod.pose,TCrossKeep)==Hint::Pose,"a nod: too far to start, fine to keep");
        TCrossGesture g;auto in=base;Run(g,in,.5);
        // Drift out to those limits at a settling arm's pace, and hold there.
        Tally drift;for(int i=0;i<60;++i){in.pose.time+=Frame;in.pose.hand[0].y+=.001f;in.pose.hand[1].y-=.001f;const auto o=g.Step(in);drift.stops+=o.stop;drift.buzzing+=o.buzz;drift.opens+=o.open;}
        Check(!drift.stops&&drift.buzzing==60&&!drift.opens,"drifting 12 cm out of level keeps the buzz");
        Check(CheckPose(in.pose,TCrossEnter)==Hint::Arms,"(the drift really left the start's limits)");
        // A tracking blink shorter than the grace.
        auto blink=in;blink.pose.handTracked[1]=false;blink.pose.time=in.pose.time;
        const auto gone=Run(g,blink,.05);in.pose.time=blink.pose.time;
        Check(!gone.stops&&gone.buzzing==gone.steps,"a blink inside the grace keeps it");
        Check(Run(g,in,.2).stops==0,"and it carries on");
    }
    {   // Tracking lost for longer than the grace ends it.
        TCrossGesture g;auto in=Standing();Run(g,in,1);
        in.pose.handTracked[0]=false;const auto t=Run(g,in,.3);
        Check(t.stops==1,"a controller lost stops the buzz");
    }
    {   // Every guard: no buzz, no timer, no prompt, however long the pose is held; the reason logs once.
        struct Guard {const char* name;void(*set)(TCrossInput&);TCrossRefusal why;};
        const Guard guards[]={
            {"no floor",[](TCrossInput& i){i.pose.floor=false;},TCrossRefusal::Floor},
            {"Seated play",[](TCrossInput& i){i.seated=true;},TCrossRefusal::Seated},
            {"a menu or wheel",[](TCrossInput& i){i.menu=true;},TCrossRefusal::Menu},
            {"a button",[](TCrossInput& i){i.buttons=true;},TCrossRefusal::Hands},
            {"the right trigger",[](TCrossInput& i){i.pose.trigger[0]=.5f;},TCrossRefusal::Hands},
            {"the left trigger",[](TCrossInput& i){i.pose.trigger[1]=.5f;},TCrossRefusal::Hands},
            {"the right grip",[](TCrossInput& i){i.grip[0]=.8f;},TCrossRefusal::Hands},
            {"the left grip",[](TCrossInput& i){i.grip[1]=.8f;},TCrossRefusal::Hands},
            {"a held bottle or a cast",[](TCrossInput& i){i.holding=true;},TCrossRefusal::Holding},
            {"a pushed stick",[](TCrossInput& i){i.stick=.6f;},TCrossRefusal::Stick},
            {"a monster close",[](TCrossInput& i){i.hostile=true;},TCrossRefusal::Hostile},
        };
        for(const auto& guard:guards){
            const std::string name=guard.name;
            {   // refused from the start
                TCrossGesture g;auto in=Standing();guard.set(in);
                const auto t=Run(g,in,6);
                Check(!t.buzzing&&!t.pulses&&!t.opens&&!t.stops,name+": no buzz, no prompt");
                Check(t.refused==1&&t.last==guard.why,name+": the reason logs once in 6 seconds");
                Check(t.scan==(guard.why==TCrossRefusal::Hostile),name+": monsters are only looked for when nothing else refuses");
            }
            {   // arriving in the middle of a hold: stops at once (no grace), and clearing it starts over
                TCrossGesture g;auto in=Standing();Run(g,in,1.5);
                auto refused=in;guard.set(refused);refused.pose.time=in.pose.time+Frame;
                const auto o=g.Step(refused);
                Check(o.stop&&!o.buzz&&!o.pulse,name+": the buzz stops on that very frame");
                in.pose.time=refused.pose.time;
                const auto back=Run(g,in,TCrossSettleSeconds+1.5);
                Check(back.buzzing>0&&!back.opens,name+": cleared, the 2 seconds start over");
            }
        }
        // Levels under the guards' thresholds: fingers resting on a controller are not "holding".
        TCrossGesture g;auto in=Standing();in.pose.trigger[0]=.2f;in.grip[1]=.4f;in.stick=.2f;
        Check(Run(g,in,3).opens==1,"resting fingers and a stick's drift do not refuse");
        // Refusals: at most one line per entry, and one per 30 seconds across entries.
        TCrossGesture r;auto seated=Standing();seated.seated=true;
        Check(Run(r,seated,1).refused==1,"first refusal logs");
        auto away=seated;away.pose.hand[0]=away.pose.hand[1]={1,1,2};away.pose.time=seated.pose.time+Frame;
        Run(r,away,1);seated.pose.time=away.pose.time+Frame;
        Check(Run(r,seated,1).refused==0,"a second try within 30 seconds stays quiet");
        away.pose.time=seated.pose.time+Frame;Run(r,away,TCrossRefusalEvery+1);seated.pose.time=away.pose.time+Frame;
        Check(Run(r,seated,1).refused==1,"later it logs again");
    }
    {   // The monster scan: a game you joined never answers, and the T cross still works there.
        TCrossGesture never;auto in=Standing();in.scanAnswered=false;
        const auto early=Run(never,in,TCrossScanWait-.05);
        Check(!early.buzzing&&!early.pulses&&!early.refused&&early.scan,"no answer yet: a local game's first answer may still come, no buzz, asking");
        Tally joined;int unguarded=0;double began=0,opened=0;
        for(int i=0;i<int(4/Frame);++i){in.pose.time+=Frame;const auto o=never.Step(in);joined.pulses+=o.pulse;joined.opens+=o.open;joined.stops+=o.stop;joined.refused+=o.refused!=TCrossRefusal::None;
            unguarded+=o.unguarded;if(o.pulse&&began==0)began=in.pose.time;if(o.open&&opened==0)opened=in.pose.time;}
        Check(joined.pulses>0&&joined.opens==1&&!joined.stops&&!joined.refused,"the scan never answers: it buzzes and opens, nothing refused");
        Check(unguarded==1,"and says once that this hold had no monster list");
        Check(Near(opened-began,TCrossHoldSeconds,2*Frame),"the same 2 seconds");
        Check(began-1<TCrossScanWait+.3,"the wait is brief");
        static_assert(TCrossScanWait<=.3,"the docs say the first answer waits at most 0.3 s");
        // A local game: the first answer still counts.
        TCrossGesture late;auto in2=Standing();in2.scanAnswered=false;
        Check(!Run(late,in2,TCrossScanWait-.05).buzzing,"an answer on its way: no buzz");
        in2.scanAnswered=true;
        Tally soon;int quiet=0;for(int i=0;i<int(1.8/Frame);++i){in2.pose.time+=Frame;const auto o=late.Step(in2);soon.buzzing+=o.buzz;soon.pulses+=o.pulse;soon.refused+=o.refused!=TCrossRefusal::None;quiet+=o.unguarded;}
        Check(soon.buzzing>0&&soon.pulses>0&&!soon.refused&&!quiet,"the scan answers late: the hold starts, guarded");
        Check(Run(late,in2,.4).opens==1,"and opens after its full 2 seconds");
        TCrossGesture close;auto in4=Standing();in4.scanAnswered=false;Run(close,in4,TCrossScanWait-.05);
        in4.scanAnswered=true;in4.hostile=true;
        const auto monsters=Run(close,in4,5);
        Check(!monsters.buzzing&&!monsters.opens&&monsters.refused==1&&monsters.last==TCrossRefusal::Hostile,"a first answer naming a close monster refuses before any buzz");
        // A stale answer in a local game never refuses for good, and never ends a hold.
        TCrossGesture stale;auto in3=Standing();Run(stale,in3,1);
        in3.scanAnswered=false;const auto gone=Run(stale,in3,.5);
        Check(!gone.stops&&gone.buzzing==gone.steps&&!gone.refused,"an answer going stale mid-hold changes nothing");
        Check(Run(stale,in3,1).opens==1,"and it still opens");
        TCrossGesture later;auto in5=Standing();Run(later,in5,1);in5.hostile=true;in5.pose.time+=Frame;
        Check(later.Step(in5).stop,"a monster walking up mid-hold still stops the buzz at once");
        // The reader: only a fresh read answers.
        const targetcone::Target near_[]={{1,101,100,true}};
        TCrossInput r;
        TCrossReadScan(r,0,5000,near_,1,100,100);Check(!r.scanAnswered&&!r.hostile,"never read (a joined game): no answer, no monster called close");
        TCrossReadScan(r,5000-TCrossScanFreshMs-1,5000,near_,1,100,100);Check(!r.scanAnswered&&!r.hostile,"too old: no answer");
        TCrossReadScan(r,5000-TCrossScanFreshMs,5000,near_,1,100,100);Check(r.scanAnswered&&r.hostile,"fresh, a monster close");
        TCrossReadScan(r,4990,5000,near_,0,100,100);Check(r.scanAnswered&&!r.hostile,"fresh, none: all clear");
    }
    {   // A refusal only after the pose settled: a fight with a trigger down logs nothing.
        TCrossGesture g;auto in=Standing();const auto base=in;in.pose.trigger[0]=.8f;in.hostile=true;Tally t;
        for(int i=0;i<90*60;++i){
            in.pose.time+=Frame;
            const float z=.2f*std::sin(float(in.pose.time)*9.42f);
            in.pose.hand[0].z=base.pose.hand[0].z+z;in.pose.hand[1].z=base.pose.hand[1].z-z;
            const auto o=g.Step(in);t.refused+=o.refused!=TCrossRefusal::None;t.buzzing+=o.buzz;
        }
        Check(!t.refused&&!t.buzzing,"a minute of fast arms with a guard up: no line, no buzz");
        TCrossGesture h;auto brief=Standing();brief.seated=true;
        Check(!Run(h,brief,TCrossSettleSeconds-.05).refused,"a pose shorter than the settle: no line");
    }
    {   // Why a hold ended: one capped line, with the reason and how long it ran.
        TCrossGesture g;auto in=Standing();Run(g,in,1);
        auto gone=in;gone.pose.handTracked[1]=false;
        auto t=Run(g,gone,.3);
        Check(t.lost==1&&t.why==TCrossLoss::Tracking&&Near(t.after,.8+TCrossGraceSeconds,.05),"a controller lost: tracking, and the hold's length");
        auto cap=Standing();cap.pose.time=gone.pose.time+Frame;Run(g,cap,1);
        auto down=cap;down.pose.hand[0]={1.3f,1.1f,2.2f};down.pose.hand[1]={.7f,1.1f,2.2f};
        t=Run(g,down,.3);Check(t.stops==1&&!t.lost,"a second loss inside 30 seconds: the buzz stops, the log stays quiet");
        TCrossGesture p;auto a=Standing();Run(p,a,1);auto b=a;b.pose.hand[0]={1.3f,1.1f,2.2f};b.pose.hand[1]={.7f,1.1f,2.2f};
        t=Run(p,b,.3);Check(t.lost==1&&t.why==TCrossLoss::Pose,"arms down: pose");
        TCrossGesture f;auto c=Standing();const auto still=c;Run(f,c,1);Tally x;const double from=c.pose.time;
        for(int i=0;i<45;++i){c.pose.time+=Frame;const float z=.2f*std::sin(float(c.pose.time-from)*9.42f);
            c.pose.hand[0].z=still.pose.hand[0].z+z;c.pose.hand[1].z=still.pose.hand[1].z-z;const auto o=f.Step(c);if(o.lost!=TCrossLoss::None){++x.lost;x.why=o.lost;}}
        Check(x.lost==1&&x.why==TCrossLoss::Speed,"a swing inside the limits: speed");
        TCrossGesture h;auto d=Standing();Run(h,d,1);d.pose.time+=.5;const auto o=h.Step(d);
        Check(o.lost==TCrossLoss::Stall&&o.stop,"a stall: stall");
        for(auto l:{TCrossLoss::Tracking,TCrossLoss::Pose,TCrossLoss::Speed,TCrossLoss::Stall})Check(std::string(TCrossLossName(l))!="none","every loss has its words");
        TCrossGesture r;auto refused=Standing();Run(r,refused,1);refused.seated=true;
        t=Run(r,refused,.1);Check(t.stops==1&&!t.lost&&t.refused==1,"a guard arriving says so as a refusal, not a loss");
    }
    {   // Fighting: fast arms never buzz, even through the pose's own limits with the hands free.
        TCrossGesture g;auto in=Standing();const auto base=in;Tally t;
        for(int i=0;i<90*20;++i){   // both arms swinging fore and aft inside the +-22 cm the pose allows, 1.5 Hz: about 2 m/s
            in.pose.time+=Frame;
            const float z=.2f*std::sin(float(in.pose.time)*9.42f);
            in.pose.hand[0].z=base.pose.hand[0].z+z;in.pose.hand[1].z=base.pose.hand[1].z-z;
            const auto o=g.Step(in);t.pulses+=o.pulse;t.buzzing+=o.buzz;t.opens+=o.open;
        }
        Check(!t.pulses&&!t.buzzing&&!t.opens,"swings through the pose's limits never buzz");
        TCrossGesture h;auto cut=Standing();const auto rest=cut;Tally u;
        for(int i=0;i<90*20;++i){   // a right-arm slash across the body, back through the T, again and again
            cut.pose.time+=Frame;
            const float phase=std::fmod(float(cut.pose.time)*2.f,1.f);
            cut.pose.hand[0]=phase<.5f?Vec{1.8f-1.6f*phase,1.4f+.5f*phase,2-.8f*phase}:Vec{1.f+1.6f*(phase-.5f),1.65f-.5f*(phase-.5f),1.6f+.8f*(phase-.5f)};
            cut.pose.hand[1]=rest.pose.hand[1];
            const auto o=h.Step(cut);u.pulses+=o.pulse;u.opens+=o.open;
        }
        Check(!u.pulses&&!u.opens,"a repeated slash through the T never buzzes");
        // A held pose broken by a fast move ends like any other exit.
        TCrossGesture f;auto hold=Standing();Run(f,hold,1);
        Tally v;for(int i=0;i<23;++i){hold.pose.time+=Frame;hold.pose.hand[0].z+=.03f;const auto o=f.Step(hold);v.stops+=o.stop;}   // 2.7 m/s for a quarter second
        Check(v.stops==1,"a swing out of a held pose stops the buzz within a quarter second");
        // Fast arms inside the held limits: only the speed rule can end this one.
        TCrossGesture k;auto keep=Standing();const auto still=keep;Run(k,keep,1);
        Tally x;const double swingFrom=keep.pose.time;
        for(int i=0;i<45;++i){
            keep.pose.time+=Frame;
            const float z=.2f*std::sin(float(keep.pose.time-swingFrom)*9.42f);
            keep.pose.hand[0].z=still.pose.hand[0].z+z;keep.pose.hand[1].z=still.pose.hand[1].z-z;
            Check(CheckPose(keep.pose,TCrossKeep)==Hint::Pose,"(the swing stays inside the held limits)");
            const auto o=k.Step(keep);x.stops+=o.stop;x.opens+=o.open;
        }
        Check(x.stops==1&&!x.opens,"a swing that never leaves the limits still ends the hold");
        // A slow settle is not fighting.
        TCrossGesture s;auto settle=Standing();Tally w;
        for(int i=0;i<90*3;++i){settle.pose.time+=Frame;settle.pose.hand[0].z=2+.05f*std::sin(float(settle.pose.time));const auto o=s.Step(settle);w.opens+=o.open;}
        Check(w.opens==1,"slow arm movement inside the pose still opens it");
    }
    {   // After the prompt: it cannot open again until the arms come down.
        for(const char* how:{"cancel","save"}){
            const std::string name=how;
            TCrossGesture g;auto in=Standing();
            Check(Run(g,in,3).opens==1,name+": opened");
            auto prompt=in;prompt.calibrating=true;
            const auto up=Run(g,prompt,4);   // the prompt up, then its release gate holding the input
            Check(!up.buzzing&&!up.pulses&&!up.opens&&!up.scan,name+": the prompt owns it");
            in.pose.time=prompt.pose.time;
            const auto still=Run(g,in,6);
            Check(!still.buzzing&&!still.pulses&&!still.opens&&!still.refused,name+": closed with the pose still held: no buzz and no reopen");
            auto down=in;down.pose.hand[0]={1.3f,1.1f,2.2f};down.pose.hand[1]={.7f,1.1f,2.2f};down.pose.time=in.pose.time;
            Run(g,down,TCrossRearmSeconds-.2);
            in.pose.time=down.pose.time;
            Check(!Run(g,in,3).buzzing,name+": a brief dip does not rearm it");
            down.pose.time=in.pose.time;Run(g,down,TCrossRearmSeconds+.1);
            in.pose.time=down.pose.time;
            const auto again=Run(g,in,3);
            Check(again.buzzing>0&&again.opens==1,name+": arms down and back: it works again");
        }
        // The menu's Calibrate body and the first-load prompt count too: the same flag.
        TCrossGesture g;auto in=Standing();in.calibrating=true;Run(g,in,1);in.calibrating=false;
        Check(!Run(g,in,3).buzzing,"opened another way with the pose held: no second prompt");
    }
    {   // A stall or a bad clock cannot carry a hold across it.
        TCrossGesture g;auto in=Standing();Run(g,in,1.5);
        in.pose.time+=.5;const auto o=g.Step(in);Check(o.stop&&!o.open,"a half-second stall ends the hold");
        Check(!Run(g,in,TCrossSettleSeconds+1.5).opens,"and the timer starts over");
        TCrossGesture b;auto bad=Standing();Run(b,bad,1);bad.pose.time=NAN;Check(b.Step(bad).stop,"a nonfinite clock ends it");
        bad.pose.time=0;Check(!b.Step(bad).buzz,"no clock, no buzz");
    }
    {   // Reset (focus lost): a clean start.
        TCrossGesture g;auto in=Standing();Run(g,in,1);g.Reset();
        const auto t=Run(g,in,.1);Check(!t.buzzing&&!t.stops,"Reset forgets the hold");
    }
    {   // Pure helpers.
        auto s=Standing().pose;Check(TCrossHandsWide(s),"T cross hands are wide");
        s.hand[0]={1.25f,1.2f,1.7f};s.hand[1]={.75f,1.2f,1.7f};Check(!TCrossHandsWide(s),"resting hands are not");
        s.hand[0].x=NAN;Check(!TCrossHandsWide(s),"nonfinite hands are not");
        settings::Values v=settings::Defaults();Check(TCrossAvailable(v,true),"on by default, standing, with a floor");
        Check(!TCrossAvailable(v,false),"no floor: the prompt must not offer it");
        v[settings::Seated]=1;Check(!TCrossAvailable(v,true),"not seated");
        v=settings::Defaults();v[settings::TCrossCalibrate]=0;Check(!TCrossAvailable(v,true),"not switched off");
        const targetcone::Target seen[]={{1,100,100,false},{2,100+TCrossHostileCells-1,100,true}};
        Check(!TCrossHostileClose(seen,1,100,100),"a monster behind a wall does not count");
        Check(TCrossHostileClose(seen,2,100,100),"a monster in sight and close does");
        Check(!TCrossHostileClose(seen+1,1,100+TCrossHostileCells*2,100),"a far one does not");
        Check(!TCrossHostileClose(seen,0,100,100),"no monsters");
        for(auto r:{TCrossRefusal::Floor,TCrossRefusal::Seated,TCrossRefusal::Menu,TCrossRefusal::Hands,TCrossRefusal::Holding,TCrossRefusal::Stick,TCrossRefusal::Hostile})
            Check(std::string(TCrossRefusalName(r))!="none","every refusal has its words");
    }
    {   // The real haptic path (haptic_pulse.h): the player's Vibration strengths, one timer per hand, and a stop that sticks.
        using namespace std::chrono;
        haptics::Pulses pulses;std::atomic<int> stops[2]{};
        Check(pulses.Start([&](int hand){++stops[hand];}),"the timer starts");
        std::atomic<int> applied{};float sent=0;
        const auto apply=[&](float amplitude){sent=amplitude;++applied;return true;};
        const auto buzz=[&](int hand,float strength,int ms){return pulses.Pulse(hand,haptics::Category::Interaction,milliseconds(ms),strength,apply);};
        for(int i=0;i<12;++i){buzz(0,TCrossBuzzStrength,TCrossBuzzMs);buzz(1,TCrossBuzzStrength,TCrossBuzzMs);std::this_thread::sleep_for(milliseconds(10));}
        Check(applied==24&&stops[0]==0&&stops[1]==0,"each pulse replaces the last: no stop fires while the buzz runs on");
        Check(sent>0&&sent<.5f,"the buzz is light");
        pulses.Cancel();
        Check(stops[0]==1&&stops[1]==1,"leaving the pose stops each hand once, at once");
        std::this_thread::sleep_for(milliseconds(TCrossBuzzMs+60));
        Check(stops[0]==1&&stops[1]==1,"and nothing lingers: no late stop, no late buzz");
        // The distinct buzz is stronger and longer than the light one at every strength a player can set.
        Check(TCrossOpenStrength>2*TCrossBuzzStrength,"the pop-up's buzz reads differently");
        // Vibration Off: no pulse goes out. The doc says the pop-up is then the only cue.
        haptics::Strengths off;off.category[int(haptics::Category::Interaction)]=0;pulses.Configure(off);
        applied=0;Check(!buzz(0,TCrossBuzzStrength,TCrossBuzzMs)&&applied==0,"VR Settings > Vibration > Loot, potions and menus at 0 mutes it");
        haptics::Strengths muted;muted.master=0;pulses.Configure(muted);
        Check(!buzz(1,TCrossOpenStrength,TCrossOpenMs)&&applied==0,"the master strength at 0 mutes the pop-up's buzz too");
        pulses.Stop();
    }
    {   // The wiring: what the transport feeds the gesture (this file has no game to run it in).
        const auto transport=Slurp(argv[1]),vrcam=Slurp(argv[2]);
        Check(!transport.empty()&&!vrcam.empty(),"the sources read");
        const auto from=transport.find("void TCrossStep("),to=transport.find("void CalibrationInput()");
        Check(from!=std::string::npos&&to!=std::string::npos&&from<to,"TCrossStep found");
        const auto step=transport.substr(from,to-from);
        for(const char* source:{"config.values[settings::TCrossCalibrate]","config.ready","calibrationPrompted","inWorld()",   // on
                                "bodyCalibration.Active()","calibrationRelease.blocked",                                       // calibrating
                                "config.values[settings::Seated]","s.floor=floorSpace",
                                "nativeUi","settingsModel.open","interactionOutput.wheel","interactionOutput.settings","interactionOutput.inventory",   // menu
                                "Float(trigger,i)","Float(squeeze,i)","physical.buttons","physical.lx","physical.rx",                  // hands
                                "interactionOutput.bottleHand","motionOutput.bowDrawing","motionOutput.tossHeld","glyphStroke.Drawing()","gameplay::CastingSkill(",   // holding
                                "targetlock::Get()","TCrossReadScan(","in.scanAnswered=tCrossAnswered","TCrossHandsWide(s)",
                                "hapticPulses.Cancel()","haptics::Category::Interaction","calibration::TCrossBuzzStrength","calibration::TCrossOpenStrength","OpenBodyCalibration()"})
            Check(step.find(source)!=std::string::npos,std::string("TCrossStep reads ")+source);
        Check(step.find("OpenBodyCalibration()")<step.find("calibration::TCrossOpenStrength"),"the distinct buzz comes after the open, which cancels haptics");
        const auto seam=transport.find("TCrossStep(frameConfig,physical,nativeUi);");
        Check(seam!=std::string::npos&&seam<transport.find("calibrationRelease.Step(bodyCalibration.Active()"),"it runs before the release gate takes this frame's input");
        Check(transport.find("calibration::TCrossAvailable(config,floorSpace)")!=std::string::npos,"the prompt's line follows the floor");
        Check(step.find("out.unguarded")!=std::string::npos&&step.find("the monster guard is unavailable")!=std::string::npos,"the log says when the monster guard cannot run");
        const auto clear=transport.find("void ClearInput()");
        Check(transport.find("tCross.Reset()",clear)<transport.find("settingsInputGate.Suspend()",clear),"focus lost resets it");
        Check(vrcam.find("targetlock::Pump(wanted || framevr::TCrossWantsTargets())")!=std::string::npos,"a held pose keeps the monster list fresh");
    }
    std::puts("Body calibration T cross: buzz start/stay/stop/restart, wobble, every guard, swings, rearm, stalls and haptics passed");
}
