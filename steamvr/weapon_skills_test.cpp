#include "gameplay.h"
#include "melee_contact.h"
#include "skill_pad.h"
#include "body_tracking.h"
#include "../vr/shield_pose.h"
#include "../vr/native_skill_dispatch.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
struct Sim {
    gameplay::Engine engine;gameplay::Input in;gameplay::Output out;
    Sim() {
        in.active=true;in.tracked[0]=in.tracked[1]=in.aimTracked=true;in.weapon=gameplay::Weapon::Bow;in.equipment=1;
        in.head.p={0,1.65f,0};in.hand[0].p={0,1.3f,-.4f};in.hand[1].p={0,1.3f,-.5f};in.aim=in.hand[0];Step(2);
    }
    void Step(int n=1){while(n--){in.time+=.02;out=engine.Step(in);}}
    void Select(int id){engine.Select(2);in.selectedSkill=id;in.selectedAvailable=true;in.skillContext=7;Step(2);}
    void Draw(){in.trigger=1;Step();assert(out.bowDrawing);for(int i=0;i<10;++i){in.hand[0].p.z+=.04f;Step();}}
};
int main() {
    {
        // Shield contact -> XR reports -> native alternate-bank sequencer.
        // Vary producer cadence and the phase of the game's 25 Hz polling.
        // The former 65 ms gesture pulse was swallowed by bank preparation.
        const int slot=skills::TriggerSlot(1);
        for(int xrMs:{11,22,33,40})for(int phase=1;phase<=40;++phase) {
            weaponskills::Pulse pulse;skills::SkillPad pad;
            assert(pulse.Start(slot,weaponskills::Smite,-1,7,1.0));
            int reported=-1,edges=0;uint16_t previous=0;uint32_t packet=0;
            for(int ms=0;ms<300;++ms) {
                if(ms%xrMs==0) {
                    reported=pulse.Get(slot,weaponskills::Smite,-1,7,1.0+ms/1000.0);++packet;
                }
                if(ms>=phase&&(ms-phase)%40==0) {
                    const auto buttons=pad.Poll(skills::Buttons(reported),skills::Alternate(reported),packet,1000+ms);
                    if(buttons&&!previous) {++edges;pulse.Cancel();} // native completion
                    previous=buttons;
                }
            }
            assert(edges==1);
        }
        weaponskills::Pulse pulse;
        assert(pulse.Start(slot,weaponskills::Smite,-1,7,1.0));
        assert(pulse.Get(slot,weaponskills::Smite,-1,7,1.10)==slot);
        assert(pulse.Get(slot,weaponskills::Smite,-1,8,1.11)==-1); // reassignment cancels
        assert(pulse.Start(slot,weaponskills::Smite,-1,8,2.0));
        assert(pulse.Get(slot,weaponskills::Smite,-1,8,2.181)==-1); // bounded, no queued cast
        assert(pulse.Start(0,12,-1,9,3.0));
        assert(pulse.Get(0,12,-1,9,3.066)==-1); // other gestures retain their duration
    }
    {
        Sim s;s.in.handBindings=true;s.engine.Select(0);s.in.selectedSkill=47;
        s.in.bowSlot=4;s.in.bowSkill=12;s.in.bowOwner=-1;s.in.skillContext=7;s.Step(3);
        s.Draw();s.in.trigger=0;s.Step();assert(s.out.nativeBowRelease&&s.out.skill==4&&s.engine.Selection()==0);
    }
    {
        Sim s;s.in.handBindings=true;s.engine.Select(0);s.in.selectedSkill=47;
        s.in.bowSlot=4;s.in.bowSkill=12;s.in.skillContext=7;s.in.hand[0].p.x=.5f;s.Step(3);
        s.in.trigger=1;s.Step();assert(s.out.skill==0&&!s.out.bowDrawing);
        for(int i=0;i<10;++i){s.in.hand[0].p.x-=.05f;s.Step();assert(s.out.skill==0&&!s.out.bowDrawing);}
        s.in.trigger=0;s.Step();assert(!s.out.nativeBowRelease);
    }
    {
        Sim s;s.in.handBindings=true;s.engine.Select(0);s.in.bowSlot=4;s.in.bowSkill=12;s.Step(3);
        s.Draw();s.in.bowSlot=-1;s.in.bowSkill=-1;s.in.trigger=0;s.Step();
        assert(!s.out.nativeBowRelease&&!s.out.bowRelease&&s.out.block);
    }
    for(int id:{6,7,11,12,16,21,22,26,27,31}) {
        Sim s;s.Select(id);s.Draw();assert(s.out.skill==-1&&!s.out.attack&&!s.out.bowRelease);
        assert(s.out.bowDrawing&&!s.out.bowPhysical); // a native bow skill shows no path laser
        s.in.trigger=0;s.Step();assert(s.out.nativeBowRelease&&s.out.skill==2&&!s.out.bowRelease&&!s.out.attack);
        assert(s.out.rayDirection.z<-.99f);s.in.hand[0].p.x+=.1f;s.Step();assert(s.out.rayDirection.z<-.99f);
        s.Step(5);assert(!s.out.nativeBowRelease&&s.out.skill==-1);s.Step(30);assert(s.out.skill==-1);
    }
    for(int interruption=0;interruption<6;++interruption) {
        Sim s;s.Select(12);s.Draw();
        if(interruption==0)s.in.selectedSkill=6;
        if(interruption==1)++s.in.selectedOwner;
        if(interruption==2)++s.in.skillContext;
        if(interruption==3)s.in.selectedAvailable=false;
        if(interruption==4)s.in.tracked[1]=false;
        if(interruption==5)s.in.quickCast=true;
        s.Step();assert(!s.out.nativeBowRelease&&!s.out.bowRelease&&s.out.skill==-1);
        s.in.trigger=0;s.Step();assert(!s.out.nativeBowRelease&&!s.out.bowRelease);
    }
    {
        Sim s;s.Select(12);s.in.hand[0].p.x=.5f;s.Step(2);s.in.trigger=1;s.Step();assert(s.out.skill==2&&!s.out.bowDrawing);
        for(int i=0;i<10;++i){s.in.hand[0].p.x-=.05f;s.Step();assert(s.out.skill==2&&!s.out.bowDrawing);}
        s.in.trigger=0;s.Step();assert(!s.out.nativeBowRelease&&!s.out.bowRelease);
    }
    {
        Sim s;s.Select(12);s.in.hand[0].p.x=.5f;s.Step(2);s.in.trigger=1;s.Step();assert(s.out.skill==2);
        ++s.in.selectedOwner;s.Step();assert(s.out.skill==-1&&s.out.block);s.Step();assert(s.out.skill==-1);
    }
    {
        Sim s;s.Select(12);s.Draw();s.in.trigger=0;s.Step();assert(s.out.nativeBowRelease);
        s.in.quickCast=true;s.Step();assert(s.out.skill==-1);s.in.quickCast=false;s.Step();assert(s.out.skill==-1);
    }
    {
        Sim s;s.in.weapon=gameplay::Weapon::Melee;s.Step(2);s.Select(weaponskills::Smite);
        s.in.shieldEquipped=true;s.Step();assert(s.out.shieldContact&&s.out.meleeContact&&s.out.skill==-1);
        s.in.trigger=1;s.Step();assert(!s.out.shieldContact&&!s.out.meleeContact&&s.out.skill==2);
        s.in.trigger=0;s.in.shieldEquipped=false;s.Step();assert(!s.out.shieldContact);
    }
    {
        const weaponskills::ContactContext saved{3,4,5,6,2,-1};
        assert(weaponskills::Current(saved,saved,100,101));
        assert(!weaponskills::Current(saved,saved,100,99));
        assert(!weaponskills::Current(saved,saved,100,1601));
        assert(!weaponskills::Current(saved,saved,100,201,100));
        for(int change=0;change<6;++change) {
            auto current=saved;
            if(change==0)++current.session;
            if(change==1)++current.epoch;
            if(change==2)++current.assignment;
            if(change==3)++current.equipment;
            if(change==4)++current.slot;
            if(change==5)++current.owner;
            assert(!weaponskills::Current(saved,current,100,101));
        }
    }
    {
        weaponskills::Pulse p;assert(p.Start(2,12,-1,7,1));assert(p.Get(2,12,-1,7,1.02)==2);
        assert(!p.Start(2,12,-1,7,1.03));assert(p.Get(2,12,-1,8,1.04)==-1);
        assert(p.Start(2,12,-1,8,2));assert(p.Get(2,12,-1,8,1.9)==-1);
    }
    {
        using namespace skel::bow;
        // A 60 cm authored arm used to stop a shield wrist well short of a
        // real 85 cm reach. Check forward, lateral and raised targets against
        // an independent two-segment solver, including calibration/model scale.
        for(float units:{2.f,4.f,8.f})for(Vec direction:{Vec{0,0,1},Vec{1,0,0},Vec{0,1,0},Normalized(Vec{1,1,1})}) {
            const Vec shoulder{.2f*units,1.4f*units,.1f*units};
            const float upper=.32f*units,lower=.28f*units;
            const Vec wanted=shoulder+direction*(.85f*units);
            Vec oldTarget=wanted,elbow;
            assert(body::Knee(shoulder,upper,lower,oldTarget,{0,-1,.2f},elbow));
            assert(Length(oldTarget-wanted)>.24f*units); // reproduce the old limit
            const float fit=skel::shield::ReachScale(shoulder,wanted,upper,lower);
            Vec fittedTarget=wanted;
            assert(body::Knee(shoulder,upper*fit,lower*fit,fittedTarget,{0,-1,.2f},elbow));
            assert(Length(fittedTarget-wanted)<.0001f);
            const Vec nearby=shoulder+direction*(.35f*units);
            assert(skel::shield::ReachScale(shoulder,nearby,upper,lower)==1); // no permanent growth
        }
        assert(skel::shield::ReachScale({},{0,0,100},.3f,.3f)==2);
        assert(skel::shield::ReachScale({},{NAN,0,0},.3f,.3f)==1);
        assert(skel::shield::ReachScale({},{0,0,1},0,.3f)==1);
        assert(skel::shield::ReachScale({},{0,0,1},INFINITY,.3f)==1);
    }
    {
        using namespace skel::bow;
        auto bindWrist=Identity(),bindShield=Identity(),tracked=Identity(),result=Identity();
        Position(bindWrist,{2,1,0});Position(bindShield,{2,1.2f,.1f});
        bindShield.m[0]=-1; // the left attachment is allowed to be mirrored
        tracked=Rotation({0,0,1},{1,0,0});Position(tracked,{5,3,2});
        assert(skel::shield::Follow(bindWrist,bindShield,tracked,result));
        assert(Length(Position(result)-(Position(tracked)+Vector(tracked,{0,.2f,.1f})))<.0001f);
        assert(Dot({result.m[0],result.m[1],result.m[2]},Cross({result.m[4],result.m[5],result.m[6]},{result.m[8],result.m[9],result.m[10]}))<0);
        const auto original=result;
        const Vec movement{.6f,.3f,-.7f};Position(tracked,Position(tracked)+movement);
        assert(skel::shield::Follow(bindWrist,bindShield,tracked,result));
        assert(Length(Position(result)-Position(original)-movement)<.0001f);
        for(int i=0;i<12;++i)assert(std::fabs(result.m[i]-original.m[i])<.0001f); // no shield growth or rotation from reach
        bindWrist.m[0]=0;assert(!skel::shield::Follow(bindWrist,bindShield,tracked,result));
    }
    {   // 2026-10-21: the shield's frame is the arm's, from joint positions: the same directions on every class
        using namespace skel::bow;namespace sh=skel::shield;
        auto turnAbout=[](Vec axis,float deg){   // a turn as a matrix (row vectors)
            const Vec a=Normalized(axis);const Vec p=Normalized(Cross(a,std::fabs(a.y)<.9f?Vec{0,1,0}:Vec{1,0,0}));
            const float t=deg*.0174533f;return Rotation(p,p*std::cos(t)+Cross(a,p)*std::sin(t));
        };
        auto close=[](Vec a,Vec b,float e=1e-3f){return Length(a-b)<e;};
        auto same=[&](const sh::Frame& a,const sh::Frame& b,float e=1e-3f){return a.ok&&b.ok&&close(a.along,b.along,e)&&close(a.out,b.out,e)&&close(a.up,b.up,e);};
        // a left forearm along +z, the thumb's (index) knuckle toward +x: the back of the hand faces -y
        const Vec elbow{0,0,0},wrist{0,0,.26f},index{.03f,0,.34f},pinky{-.03f,0,.34f},middle{0,0,.345f};
        const float handed=sh::Handed(elbow,wrist,index,pinky,middle,1);assert(handed!=0);
        const sh::Frame bind=sh::ArmFrame(elbow,wrist,index,pinky,handed);
        assert(bind.ok&&close(bind.along,{0,0,1})&&close(bind.out,{0,-1,0})&&close(bind.up,{1,0,0}));   // grip, out (the back of the hand), up (the thumb)
        {const sh::Frame palm=sh::ArmFrame(elbow,wrist,index,pinky,sh::Handed(elbow,wrist,index,pinky,middle,-1));
         assert(palm.ok&&close(palm.out,{0,1,0})&&close(palm.up,{1,0,0}));}   // a shield hung on the palm's side: out turns, up stays
        // Two classes' bind poses: the same joints, the hand bones laid along different axes (the
        // Paladin's "up" moved the shield off the arm). The same wrist bend and twist for both:
        Mat handA=Identity(),handB=turnAbout({1,1,0},40);for(int i=0;i<12;++i)handB.m[i]*=1.3f;
        Position(handA,wrist);Position(handB,wrist);
        const Mat bendTwist=Multiply(turnAbout({1,0,0},35),turnAbout({0,0,1},70));   // about the wrist, in model space
        auto moved=[&](const Mat& bone,const Mat& turn){Mat m=bone;Position(m,{});m=Multiply(m,turn);Position(m,wrist);return m;};
        const sh::Frame wristA=sh::Carry(bind,handA,moved(handA,bendTwist)),wristB=sh::Carry(bind,handB,moved(handB,bendTwist));
        assert(same(wristA,wristB)&&!same(wristA,bind));
        assert(close(sh::Offset(wristA.along,wristA.out,wristA.up,3,2,2,4),sh::Offset(wristB.along,wristB.out,wristB.up,3,2,2,4),1e-4f));
        for(const auto* f:{&wristA,&wristB}) {   // orthonormal, with the bind frame's handedness
            assert(std::fabs(Length(f->along)-1)<1e-3f&&std::fabs(Length(f->out)-1)<1e-3f&&std::fabs(Length(f->up)-1)<1e-3f);
            assert(std::fabs(Dot(f->along,f->out))<1e-3f&&std::fabs(Dot(f->along,f->up))<1e-3f&&std::fabs(Dot(f->out,f->up))<1e-3f);
            assert(Dot(Cross(f->along,f->out),f->up)*Dot(Cross(bind.along,bind.out),bind.up)>0);
        }
        // Strapped to the forearm: a wrist bend (about the knuckle line) leaves it alone; a twist turns it
        auto about=[&](Vec p,const Mat& r){return wrist+Vector(r,p-wrist);};
        {const Mat flex=turnAbout({1,0,0},50);
         assert(same(sh::ArmFrame(elbow,wrist,about(index,flex),about(pinky,flex),handed),bind));
         const Mat twist=turnAbout({0,0,1},90);
         const sh::Frame t=sh::ArmFrame(elbow,wrist,about(index,twist),about(pinky,twist),handed);
         assert(t.ok&&close(t.along,bind.along)&&close(t.up,Vector(twist,bind.up))&&close(t.out,Vector(twist,bind.out)));}
        // Shield follows: 0% strapped, 100% the wrist, a slerp between - half the bend at 50%, smooth all the way
        {const Mat flex=turnAbout({1,0,0},60);
         const sh::Frame strapped=sh::ArmFrame(elbow,wrist,about(index,flex),about(pinky,flex),handed),w=sh::Carry(bind,handB,moved(handB,flex));
         Vec axis;float angle;assert(sh::TurnBetween(strapped,w,axis,angle)&&std::fabs(angle*57.2958f-60)<.1f);
         assert(same(sh::Blend(strapped,w,0),strapped)&&same(sh::Blend(strapped,w,1),w));
         assert(same(sh::Blend(strapped,w,-3),strapped)&&same(sh::Blend(strapped,w,NAN),w));   // clamped; not finite: the wrist
         assert(sh::TurnBetween(strapped,sh::Blend(strapped,w,.5f),axis,angle)&&std::fabs(angle*57.2958f-30)<.1f);
         sh::Frame last=strapped;
         for(int i=1;i<=100;++i){const sh::Frame b=sh::Blend(strapped,w,i*.01f);assert(b.ok&&sh::TurnBetween(last,b,axis,angle)&&angle*57.2958f<.7f);last=b;}
         assert(same(sh::Blend(sh::Frame{},w,.5f),w)&&same(sh::Blend(strapped,sh::Frame{},.5f),strapped));
         // the wrist end without roll is the original wrist attach exactly (Follow), for either class's hand bone
         for(const Mat& handBind:{handA,handB}) {
             Mat now=moved(handBind,flex);Position(now,wrist+Vec{.1f,.2f,.3f});
             Mat attach=turnAbout({0,1,0},-30);attach.m[0]*=-1;Position(attach,{.02f,-.05f,.2f});   // mirrored, off the arm
             Mat followed,placed;assert(sh::Follow(handBind,attach,now,followed));
             const float bs=Length({handBind.m[0],handBind.m[1],handBind.m[2]}),ns=Length({now.m[0],now.m[1],now.m[2]});
             assert(sh::Place(attach,bind,wrist,bs,sh::Carry(bind,handBind,now),Position(now),ns,placed));
             for(int i=0;i<16;++i)assert(std::fabs(placed.m[i]-followed.m[i])<1e-4f);
         }}
        // The resting roll: + turns the face from out toward the little finger. Palm down, a left arm ahead
        // (+z; the hero's left is +x): the back of the hand faces the sky and the thumb points in (-x);
        // +90 turns the tray-flat shield to face out to the left.
        {sh::Frame palmDown;palmDown.along={0,0,1};palmDown.out={0,1,0};palmDown.up={-1,0,0};palmDown.ok=true;
         const sh::Frame r=sh::Roll(palmDown,90);assert(close(r.out,{1,0,0})&&close(r.up,{0,1,0})&&close(r.along,palmDown.along));
         assert(same(sh::Roll(palmDown,0),palmDown)&&same(sh::Roll(palmDown,NAN),palmDown));
         assert(same(sh::Roll(palmDown,500),sh::Roll(palmDown,180)));   // clamped to the setting's range
         assert(same(sh::Roll(sh::Roll(palmDown,40),-40),palmDown));
         // the INI value: -180..+180 snapped to 5-degree steps; 0 (the attach's own facing) when unreadable
         assert(sh::RollSetting(0)==0&&sh::RollSetting(NAN)==0&&sh::RollSetting(INFINITY)==0);
         assert(sh::RollSetting(92)==90&&sh::RollSetting(93)==95&&sh::RollSetting(-7)==-5&&sh::RollSetting(-8)==-10);
         assert(sh::RollSetting(500)==180&&sh::RollSetting(-500)==-180&&sh::RollSetting(-1)==0&&!std::signbit(sh::RollSetting(-1)));
         // [gameplay] shield_follow_pct: 0..100 snapped to 10% steps; 50 when unreadable
         assert(sh::FollowSetting(50)==50&&sh::FollowSetting(NAN)==50&&sh::FollowSetting(INFINITY)==50);
         assert(sh::FollowSetting(0)==0&&sh::FollowSetting(100)==100&&sh::FollowSetting(34)==30&&sh::FollowSetting(35)==40);
         assert(sh::FollowSetting(-20)==0&&sh::FollowSetting(250)==100&&!std::signbit(sh::FollowSetting(-1)));
         // the trace's jump check starts over on its first frame, a new shield or arm, and after a gap (a level load)
         assert(sh::TraceFresh(false,1000,1011,true));                       // first frame
         assert(!sh::TraceFresh(true,1000,1011,true)&&!sh::TraceFresh(true,1000,1500,true));
         assert(sh::TraceFresh(true,1000,1501,true));                        // no trace frame for over 0.5 s
         assert(sh::TraceFresh(true,1000,1011,false));                       // another shield bone or skeleton
         assert(sh::TraceFresh(true,1000,999,true));                         // clock went back
         // the roll turns the shield about the forearm through the wrist, the same for either class's bones
         Mat placedA,placedB,attach=Identity();Position(attach,{0,-.04f,.24f});   // on the back of the hand at bind
         assert(sh::Place(attach,bind,wrist,1,sh::Roll(wristA,90),wrist,1,placedA)&&sh::Place(attach,bind,wrist,1,sh::Roll(wristB,90),wrist,1,placedB));
         for(int i=0;i<16;++i)assert(std::fabs(placedA.m[i]-placedB.m[i])<1e-4f);
         Mat flat;assert(sh::Place(attach,bind,wrist,1,sh::Roll(bind,90),wrist,1,flat));
         assert(close(Position(flat),wrist+Vec{-.04f,0,-.02f}));}   // off the back of the hand (-y) round to the little finger (-x)
        // nothing to read: no frame
        assert(sh::Handed(elbow,elbow,index,pinky,middle,1)==0&&sh::Handed(elbow,wrist,index,index,middle,1)==0);
        assert(!sh::ArmFrame(elbow,wrist,index,pinky,0).ok&&!sh::ArmFrame(elbow,elbow,index,pinky,1).ok);
        assert(!sh::ArmFrame(elbow,wrist,{0,0,.4f},{0,0,.3f},1).ok);   // knuckles along the forearm
        assert(!sh::ArmFrame({NAN,0,0},wrist,index,pinky,1).ok&&!sh::Carry(sh::Frame{},handA,handA).ok);
        {Mat out;assert(!sh::Place(Identity(),sh::Frame{},wrist,1,bind,wrist,1,out)&&!sh::Place(Identity(),bind,wrist,0,bind,wrist,1,out));}
        // Offset: cm at the model's scale, clamped to the settings' ranges
        const Vec x{1,0,0},y{0,1,0},z{0,0,1};
        assert(Length(sh::Offset(z,x,y,3,2,2,10)-Vec{.2f,.2f,-.3f})<1e-5f);   // grip toward the elbow (-along)
        assert(Length(sh::Offset(z,x,y,100,-100,100,100)-Vec{-30,30,-40})<1e-4f);
        assert(Length(sh::Offset(z,x,y,-5,0,0,100)-Vec{0,0,5})<1e-5f);     // a little toward the hand
        assert(Length(sh::Offset(z,x,y,-50,0,0,100)-Vec{0,0,10})<1e-5f);   // at most 10 cm
        assert(Length(sh::Offset(z,x,y,NAN,INFINITY,-INFINITY,100))<1e-6f);
        assert(Length(sh::Offset(z,x,y,3,2,2,0))==0&&Length(sh::Offset(z,x,y,3,2,2,NAN))==0);
        // the trace's turn and angles
        assert(sh::TurnDeg(Identity(),Identity())<.1f&&std::fabs(sh::TurnDeg(Identity(),turnAbout({0,1,0},90))-90)<.1f);
        assert(std::fabs(sh::TurnDeg(Identity(),turnAbout({1,0,0},180))-180)<.5f);   // a flip reads as one
        {Mat scaled=turnAbout({0,0,1},30);for(int i=0;i<12;++i)scaled.m[i]*=2;assert(std::fabs(sh::TurnDeg(Identity(),scaled)-30)<.1f);}
        {const Vec a=sh::Angles(turnAbout({0,1,0},90));assert(std::fabs(std::fabs(a.x)-90)<.1f&&std::fabs(a.y)<.1f&&std::fabs(a.z)<.1f);}
        static_assert(sh::kGripMinCm==-10&&sh::kGripMaxCm==40&&sh::kSideMaxCm==30);
    }
    {
        using ::melee::native::FunctionRange;using ::melee::native::FindFunction;
        const std::array<FunctionRange,3> ranges={FunctionRange{0x100,0x140,0x800},FunctionRange{0x200,0x280,0x810},FunctionRange{0x300,0x380,0x820}};
        assert(FindFunction(ranges,0x200,0x1000)==ranges[1]);
        assert(!FindFunction(ranges,0x210,0x1000).begin); // containing range cannot replace an exact entry
        assert(!FindFunction(ranges,0x200,0x250).begin);
        auto broken=ranges;broken[1].unwind=0x1000;assert(!FindFunction(broken,0x200,0x1000).begin);
        broken=ranges;broken[1].end=broken[1].begin+8;assert(!FindFunction(broken,0x200,0x1000).begin);
        broken=ranges;broken[2]={0x200,0x290,0x810};assert(!FindFunction(broken,0x200,0x1000).begin);
    }
    {
        std::array<uintptr_t,151> table{};table[1]=0x1000;table[3]=0x2000;table[150]=0x3000;
        assert(::melee::native::SmiteEntry(table,0x1000,0x2000)==0x3000);
        table[3]=0x4000;assert(!::melee::native::SmiteEntry(table,0x1000,0x2000));
        const uint8_t code[]={0x90,0xe8,0x0a,0,0,0,0xc3};
        assert(::melee::native::CallsTarget(code,0x1000,0x1010));assert(!::melee::native::CallsTarget(code,0x1000,0x1011));
        const uint8_t immediate[]={0x48,0xb8,0xe8,0x09,0,0,0,0,0,0,0xc3};
        assert(!::melee::native::CallsTarget(immediate,0x1000,0x1010));
        const uint8_t backward[]={0xe8,0xeb,0xff,0xff,0xff,0xc3};
        assert(::melee::native::CallsTarget(backward,0x1020,0x1010));
    }
    {
        framevr::melee::Engine e;framevr::melee::Sweep sweep;framevr::melee::Sample sample;
        sample.active=sample.shield=true;sample.skillSlot=2;sample.skillContext=1;sample.equipment=1;sample.unitsPerMetre=5;
        sample.player={0,0,0};sample.hand.p={0,1,0};sample.blade={{0,.8f,0},{0,1.2f,0},.15f};
        for(int i=0;i<20;++i){sample.time+=.02;assert(!e.Step(sample,sweep));}
        framevr::melee::HitSet hits;int bashes=0;
        for(int i=0;i<25;++i){sample.time+=.02;sample.hand.p.z-=.05f;sample.blade.base.z-=.05f;sample.blade.tip.z-=.05f;
            if(e.Step(sample,sweep)&&hits.Insert(1,sweep.stroke,UINT32_MAX))++bashes;}
        assert(bashes==1); // keep observing during wind-up: no second bash from this swing
        for(int i=0;i<10;++i){sample.time+=.02;sample.hand.p.z+=.05f;sample.blade.base.z+=.05f;sample.blade.tip.z+=.05f;
            if(e.Step(sample,sweep)&&hits.Insert(1,sweep.stroke,UINT32_MAX))++bashes;}
        assert(bashes==2);++sample.skillContext;sample.time+=.02;assert(!e.Step(sample,sweep));
        sample.active=false;sample.time+=.02;assert(!e.Step(sample,sweep));
    }
    {   // Throwing potions: the six gas and oil potions, by code (space padded as Items rows are), thrown by Attack or Throw.
        for(const char* code:{"gps","ops","gpm","opm","gpl","opl","opm ","gps "})assert(weaponskills::ThrowingPotion(code));
        for(const char* code:{"hp5","rvl","yps","wms","vps","jav","tkf","tax","op","opmx",""})assert(!weaponskills::ThrowingPotion(code));
        assert(weaponskills::PotionThrow(weaponskills::Attack)&&weaponskills::PotionThrow(weaponskills::Throw));
        assert(!weaponskills::PotionThrow(-1)&&!weaponskills::PotionThrow(weaponskills::DoubleThrow)&&!weaponskills::PotionThrow(251));
    }
    std::puts("Weapon skills: release/cancellation/fallback, shield ownership/pose/strokes, throwing potions and native dispatch guards passed");
}
