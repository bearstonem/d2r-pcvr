#include "body_calibration.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
using namespace calibration;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static Sample Pose(){Sample s;s.time=1;s.floor=s.headTracked=s.handTracked[0]=s.handTracked[1]=true;
    s.head={1,1.7f,2};s.hand[0]={1.8f,1.4f,2};s.hand[1]={.2f,1.4f,2};return s;}
static bool Capture(Flow& f,Sample& s){bool captured=false;for(int i=0;i<122;++i){s.time+=.01;captured=f.Step(s)||captured;}return captured;}
int main(){
    settings::Snapshot config;config.ready=true;
    Check(!Stored(config.values),"uncalibrated first load prompts");
    Sample s=Pose();Check(CheckPose(s)==Hint::Pose,"standing T-pose");
    for(float yaw:{-2.f,0.f,2.f}){auto turned=s;turned.rotation=math::Yaw(yaw);
        for(int i=0;i<2;++i)turned.hand[i]=s.head+math::Rotate(turned.rotation,s.hand[i]-s.head);
        Check(CheckPose(turned)==Hint::Pose,"pose works at arbitrary room yaw");}
    {   // Left-handed play hands the sample over in main / off order: read so, a T cross is crossed arms.
        auto roles=s;std::swap(roles.hand[0],roles.hand[1]);roles.handTracked[0]=false;roles.trigger[1]=1;
        roles.handTracked[0]=true;Check(CheckPose(roles)==Hint::Arms,"hands in main / off order are not a T cross as they are");
        BodyOrder(roles,true);Check(CheckPose(roles)==Hint::Pose&&roles.trigger[0]==1&&roles.trigger[1]==0,"left-handed: put in body order, the same T cross is the pose");
        auto same=s;BodyOrder(same,false);Check(same.hand[0].x==s.hand[0].x,"right-handed: nothing moves");
    }
    auto bad=s;bad.floor=false;Check(CheckPose(bad)==Hint::Floor,"LOCAL space cannot invent a floor");
    bad=s;bad.handTracked[1]=false;Check(CheckPose(bad)==Hint::Tracking,"one untracked controller rejected");
    bad=s;bad.hand[0].x=NAN;Check(CheckPose(bad)==Hint::Tracking,"nonfinite tracking rejected");
    bad=s;bad.rotation={.3f,0,0,std::sqrt(.91f)};Check(CheckPose(bad)==Hint::Upright,"head tilt rejected");
    bad=s;bad.hand[0].y=.8f;Check(CheckPose(bad)==Hint::Arms,"arms down rejected");
    bad=s;bad.hand[1].z+=.3f;Check(CheckPose(bad)==Hint::Arms,"forward arm rejected");
    bad=s;bad.head.y=.1f;Check(CheckPose(bad)==Hint::Height,"implausible eye height has separate standing-height feedback");
    bad=s;bad.floor=false;bad.headTracked=false;
    Check(CheckPose(bad)==Hint::Tracking,"startup tracking loss does not prematurely request playspace setup");
    // Regression: hands held normally in front before stretching out formerly
    // failed the joint height/span check and falsely blamed SteamVR's floor.
    auto resting=s;resting.hand[0]={1.25f,1.2f,1.7f};resting.hand[1]={.75f,1.2f,1.7f};
    Check(CheckPose(resting)==Hint::Arms,"short resting controller span cannot request playspace setup");
    Flow start;start.Begin();start.Step(resting);
    Check(start.hint==Hint::Arms&&start.phase==Phase::Release,"first-load resting arms receive T-pose instructions");
    s.time+=.01;start.Step(s);s.trigger[0]=s.trigger[1]=1;
    Check(Capture(start,s)&&start.phase==Phase::Saving,"raising arms after the initial hint calibrates with the existing floor");
    s=Pose();
    Flow f;f.Begin();s.trigger[0]=s.trigger[1]=1;
    Check(!Capture(f,s)&&f.phase==Phase::Release,"opening with both triggers held cannot capture");
    s.trigger[0]=s.trigger[1]=0;s.time+=.01;f.Step(s);Check(f.phase==Phase::Ready,"release arms the capture");
    s.trigger[0]=1;Check(!Capture(f,s)&&f.phase==Phase::Ready,"one trigger never samples");
    s.trigger[1]=1;Check(Capture(f,s)&&f.phase==Phase::Saving,"stable dual-trigger hold captures once");
    Check(std::fabs(f.Get().eyeHeight-1.7f)<1e-5f&&std::fabs(f.Get().armSpan-1.6f)<1e-5f,"measurement averages stage eye height and grip span");
    Check(!Capture(f,s),"saving never recaptures");
    auto request=SaveRequest(config,f.Get());Check(request.sequence&&request.changed&&request.values[settings::BodyCalibration]==1,"capture queues atomic measurements and version");
    f.Submitted(request.sequence);auto result=config;result.sequence=request.sequence;result.values=request.values;result.result=settings::Result::Saving;
    f.Observe(result,s.time);Check(f.phase==Phase::Saving,"await real save acknowledgement");
    result.result=settings::Result::Saved;f.Observe(result,s.time);Check(Stored(result.values)&&f.phase==Phase::Saved,"successful save enables body fit");
    Check(!Capture(f,s)&&f.Active(),"held triggers keep completion input-owned");
    s.trigger[0]=s.trigger[1]=0;s.time+=.01;f.Step(s);Check(!f.Active(),"released triggers return to game after confirmation");
    f.Begin();s=Pose();f.Step(s);s.trigger[0]=s.trigger[1]=1;s.time+=.01;f.Step(s);
    s.time+=.01;s.hand[0].x+=.08f;Check(!f.Step(s)&&f.phase==Phase::Release,"moving hands reset hold");
    s=Pose();f.Begin();f.Step(s);s.trigger[0]=s.trigger[1]=1;s.time+=.01;f.Step(s);
    f.Suspend();Check(!Capture(f,s)&&f.phase==Phase::Release,"tracking return requires trigger release");
    f.Begin();s=Pose();f.Step(s);s.trigger[0]=s.trigger[1]=1;s.time+=.01;f.Step(s);
    s.time+=2;Check(!f.Step(s)&&f.phase==Phase::Release,"frame gap cannot complete a stale capture");
    f.Begin();s=Pose();f.Step(s);s.trigger[0]=s.trigger[1]=1;Capture(f,s);f.Submitted(8);
    result.sequence=8;result.result=settings::Result::Conflict;f.Observe(result,s.time);
    Check(f.phase==Phase::Failed&&f.hint==Hint::Conflict,"save conflict remains unsaved");
    s.trigger[0]=s.trigger[1]=0;s.time+=.01;f.Step(s);s.trigger[0]=s.trigger[1]=1;
    Check(Capture(f,s),"failed calibration can be retried without restarting");
    f.Begin();s=Pose();s.cancel=true;f.Step(s);Check(f.Active(),"held B on opening is swallowed");
    s.cancel=false;s.time+=.01;f.Step(s);s.cancel=true;s.time+=.01;f.Step(s);Check(!f.Active(),"fresh B cancels without changing settings");
    f.Begin();s=Pose();f.Step(s);s.trigger[0]=s.trigger[1]=1;Check(Capture(f,s),"settings menu can recalibrate after a previous capture");
    result.values[settings::EyeHeight]=2.3f;result.values[settings::ArmSpan]=.85f;
    Check(!Stored(result.values)&&!SaveRequest(config,{0,1.5f}).sequence,"invalid anthropometry cannot persist");
    ReleaseGate gate;Check(gate.Step(true,1,1,false),"calibration owns input");
    Check(gate.Step(false,1,1,false)&&gate.Step(false,0,0,true),"held triggers or B remain blocked after closing");
    Check(gate.Step(false,0,0,false)&&!gate.Step(false,0,0,false),"clean release restores controls on subsequent frame");
    Check(!SaveRequest(settings::Snapshot{}, {1.7f,1.6f}).sequence,"missing configuration cannot save");
    std::puts("Body calibration: pose validation, stable capture, first load, recalibration, persistence acknowledgement and input release passed");
}
