#include "haptic_pulse.h"
#include "haptic_actions.h"
#include "xinput_rumble.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <map>
#include <string>
using namespace std::chrono_literals;
using namespace framevr::haptics;
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
namespace {
// Resolve the actual suggested bindings, rather than treating a hand index
// passed to Pulse as proof that the intended controller received anything.
struct Runtime {
    static inline uintptr_t serial=100;
    static inline std::map<XrAction,std::string> names;
    static XrResult XRAPI_CALL Create(XrActionSet,const XrActionCreateInfo* info,XrAction* action) {
        Check(info->type==XR_TYPE_ACTION_CREATE_INFO&&info->actionType==XR_ACTION_TYPE_VIBRATION_OUTPUT,"create output action");
        Check(info->countSubactionPaths==0&&!info->subactionPaths,"dedicated output does not depend on subaction filtering");
        *action=reinterpret_cast<XrAction>(++serial);
        names[*action]=info->actionName;return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL Path(XrInstance,const char* name,XrPath* output) {
        const std::string path=name;
        if(path=="/user/hand/right/output/haptic")*output=91;
        else if(path=="/user/hand/left/output/haptic")*output=47;
        else return XR_ERROR_PATH_FORMAT_INVALID;
        return XR_SUCCESS;
    }
    std::map<XrAction,std::vector<XrPath>> bound;
    std::map<XrPath,int> applied,stopped;
    std::mutex mutex;std::condition_variable changed;
    void Send(XrHapticActionInfo info,bool stop) {
        std::lock_guard lock(mutex);
        Check(info.type==XR_TYPE_HAPTIC_ACTION_INFO&&info.action&&info.subactionPath==XR_NULL_PATH,"send through dedicated action without hand filter");
        const auto& outputs=bound[info.action];
        Check(outputs.size()==1,"a hand's action must bind exactly one physical output");
        ++(stop?stopped:applied)[outputs[0]];changed.notify_all();
    }
    void Verify(int right,int left) {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock,2s,[&]{return stopped[91]>=right&&stopped[47]>=left;}),"timed stops reach requested controllers");
        Check(applied[91]==right&&applied[47]==left&&stopped[91]==right&&stopped[47]==left,"only the intended hand receives apply and stop");
    }
};
void Routing() {
    Actions actions;
    Check(actions.Create(XR_NULL_HANDLE,Runtime::Create)==XR_SUCCESS,"create per-hand outputs");
    Check(actions.Info(0).action!=actions.Info(1).action,"hands use distinct action handles");
    Check(Runtime::names[actions.Info(0).action]=="feedback_right"&&Runtime::names[actions.Info(1).action]=="feedback_left","stable named outputs replace shared feedback binding");
    std::vector<XrActionSuggestedBinding> bindings;
    Check(actions.Bind(XR_NULL_HANDLE,Runtime::Path,bindings)==XR_SUCCESS&&bindings.size()==2,"bind both outputs");
    Runtime runtime;
    for(const auto& binding:bindings)runtime.bound[binding.action].push_back(binding.binding);
    Pulses pulses;
    Check(pulses.Start([outputs=actions,&runtime](int hand){runtime.Send(outputs.Info(hand),true);}),"start routed timer");
    auto preview=[&](Category category,int choice){
        const auto pattern=Preview(category,choice);
        for(int hand=0;hand<2;++hand)if(pattern[hand].milliseconds){
            const auto sample=pattern[hand];
            Check(pulses.Pulse(hand,category,std::chrono::milliseconds(sample.milliseconds),sample.amplitude,[&](float){runtime.Send(actions.Info(hand),false);return true;}),"send preview to its gameplay output");
        }
    };
    preview(Category::Melee,0);
    runtime.Verify(1,0);
    preview(Category::Melee,1);
    runtime.Verify(1,1);
    for(int hand=0;hand<2;++hand)
        Check(pulses.Pulse(hand,Category::Spell,5s,.4f,[&](float){runtime.Send(actions.Info(hand),false);return true;}),"simultaneous per-hand feedback");
    pulses.Cancel();runtime.Verify(2,2);
    preview(Category::BowDraw,0);pulses.Cancel();runtime.Verify(3,3);
    preview(Category::BowRelease,0);pulses.Cancel();runtime.Verify(4,4);
    preview(Category::BowRelease,1);pulses.Cancel();runtime.Verify(5,4);
    preview(Category::Alert,0);pulses.Cancel();runtime.Verify(5,5);pulses.Stop();
    const auto previous=actions.Info(0).action;
    actions.Reset();Check(!actions.Info(0).action&&!actions.Info(1).action&&!actions.Info(-1).action&&!actions.Info(2).action,"shutdown and invalid hands have no output");
    Check(actions.Create(XR_NULL_HANDLE,Runtime::Create)==XR_SUCCESS&&actions.Info(0).action!=previous,"new session replaces old haptic handles");
}
void NativeRumble() {
    RumbleLease lease;GamepadRumble rumble;
    XINPUT_VIBRATION low{50000,0},high{0,45000},zero{};
    unsigned calls=0;DWORD result=ERROR_SUCCESS,lastIndex=99;XINPUT_VIBRATION last{};bool gotNull=false;
    auto forward=[&](DWORD index,XINPUT_VIBRATION* value){++calls;lastIndex=index;gotNull=!value;if(value)last=*value;return result;};
    Check(!lease.Owns(1000),"desktop starts without VR feedback ownership");
    Check(rumble.Send(lease.Owns(1000),0,&low,forward)==ERROR_SUCCESS&&calls==1&&last.wLeftMotorSpeed==50000,"desktop rumble passes through intact");
    lease.Update(1000,true);
    Check(rumble.Send(lease.Owns(1001),0,&high,forward)==ERROR_SUCCESS&&calls==2&&!last.wLeftMotorSpeed&&!last.wRightMotorSpeed,"VR takeover clears previously active desktop rumble once");
    for(auto* request:{&low,&high,&zero})
        Check(rumble.Send(lease.Owns(1002),0,request,forward)==ERROR_SUCCESS&&calls==2,"native low/high motor pulses and stop commands cannot overwrite OpenXR feedback");
    Check(low.wLeftMotorSpeed==50000&&high.wRightMotorSpeed==45000,"filter never alters caller vibration data");
    Check(rumble.Send(true,1,&low,forward)==ERROR_SUCCESS&&calls==3&&lastIndex==1&&last.wLeftMotorSpeed==50000,"other gamepads retain native rumble");
    result=ERROR_BAD_ARGUMENTS;
    Check(rumble.Send(true,0,nullptr,forward)==result&&calls==4&&gotNull,"invalid requests preserve native error behavior");
    result=ERROR_DEVICE_NOT_CONNECTED;
    Check(rumble.Send(true,0,&high,forward)==ERROR_SUCCESS&&calls==4,"VR virtual pad succeeds without a physical Xbox controller");
    Check(!lease.Owns(999)&&lease.Owns(1249)&&!lease.Owns(1250),"ownership expires and rejects a backwards clock");
    Check(rumble.Send(lease.Owns(1250),0,&high,forward)==result&&calls==5,"expired VR input restores native calls and error codes");
    lease.Update(1300,true);
    Check(rumble.Send(true,0,&low,forward)==ERROR_SUCCESS&&calls==5,"failed desktop output needs no takeover stop");
    lease.Update(1301,false);Check(!lease.Owns(1301),"focus loss or disabled motion immediately relinquishes output");
    result=ERROR_SUCCESS;
    Check(rumble.Send(lease.Owns(1301),0,&high,forward)==ERROR_SUCCESS&&calls==6&&last.wRightMotorSpeed==45000,"returning to desktop restores the original motor levels");
    Check(rumble.Send(false,0,&zero,forward)==ERROR_SUCCESS&&calls==7,"desktop stop remains effective");
    Check(rumble.Send(true,0,&zero,forward)==ERROR_SUCCESS&&calls==7,"fresh VR ownership does not send redundant stops into new haptics");
}
}
int main() {
    Routing();
    NativeRumble();
    {
        const auto weapon=Preview(Category::Melee,0),shield=Preview(Category::Melee,1);
        Check(weapon[0].milliseconds==90&&weapon[0].amplitude==.95f&&!weapon[1].milliseconds,"weapon test uses longer right impact");
        Check(shield[1].milliseconds==45&&shield[1].amplitude==.95f&&!shield[0].milliseconds,"shield test retains left impact");
        for(int hand=0;hand<2;++hand){
            const auto sharp=Preview(Category::Melee,hand,true)[hand];
            Check(sharp.milliseconds==10&&sharp.frequency==180,"short pulse opt-in covers both impact types");
        }
        const auto pull=Preview(Category::BowDraw,0);
        Check(PreviewCount(Category::BowDraw)==1&&pull[0].milliseconds==25&&pull[1].milliseconds==20&&
            std::fabs(pull[0].amplitude-.36f)<.00001f&&std::fabs(pull[1].amplitude-.175f)<.00001f,"draw test represents a half draw on both hands with gameplay strengths");
        Check(PreviewCount(Category::Alert)==1&&Preview(Category::Alert,0)[1].milliseconds==100&&!Preview(Category::Alert,0)[0].milliseconds,"alert has one preview on its gameplay hand");
        Check(Preview(Category::Spell,0)[1].milliseconds==25&&Preview(Category::Spell,1)[0].milliseconds==25,"spell left/right previews keep their named hands");
        Check(Preview(Category::Interaction,0)[1].milliseconds==45&&Preview(Category::Interaction,1)[0].milliseconds==45,"interaction previews support either hand");
        Check(!Preview(Category::Alert,1)[1].milliseconds&&!Preview(Category::Melee,-1)[0].milliseconds,"invalid previews stay silent");
    }
    Pulses pulses;
    auto pulse=[&](int hand,auto duration,bool accept=true){return pulses.Pulse(hand,Category::Melee,duration,.95f,[&](float amplitude){Check(amplitude==.95f,"default strength preserves native amplitude");return accept;});};
    std::mutex mutex;std::condition_variable changed;std::array<int,2> stops{};
    auto callback=[&](int hand){std::lock_guard lock(mutex);++stops[hand];changed.notify_all();};
    auto wait=[&](int hand,int count,auto timeout){std::unique_lock lock(mutex);return changed.wait_for(lock,timeout,[&]{return stops[hand]>=count;});};
    Check(pulses.Start(callback),"start timer");
    Check(pulse(0,10ms),"send short impact");
    Check(wait(0,1,2s),"single impact stops even when no render/input update runs");
    Check(!wait(0,2,30ms),"one impact expires only once");
    Check(pulse(0,40ms),"old pulse");
    Check(pulse(0,200ms),"new pulse replaces deadline");
    Check(!wait(0,2,80ms),"old timeout cannot stop replacement pulse early");
    Check(pulse(1,10ms),"other hand impact");
    Check(wait(1,1,2s)&&wait(0,2,2s),"hands expire independently");
    Check(!pulse(0,10ms,false),"failed apply is not scheduled");
    Check(!wait(0,3,30ms),"failed pulse does not trigger later stop");
    pulse(0,5s);pulse(1,5s);
    pulses.Cancel();Check(wait(0,3,0ms)&&wait(1,2,0ms),"focus loss cancels both hands immediately");
    pulse(0,5s);pulses.Stop();
    Check(wait(0,4,0ms),"shutdown stops active haptic before session destruction");
    Check(!pulse(0,10ms),"no calls after shutdown");
    Check(pulses.Start(callback)&&pulse(1,10ms),"new session can restart");
    Check(wait(1,3,2s),"restarted timer expires");pulses.Stop();
    {
        Pulses impacts;std::atomic<int> ended{0};bool weakerApplied=false;
        Check(impacts.Start([&](int hand){if(hand==0)++ended;}),"start impact protection fixture");
        auto impact=[&]{return impacts.Pulse(0,Category::Melee,200ms,.95f,[](float){return true;});};
        Check(impact(),"landed impact begins");
        Check(!impacts.Pulse(0,Category::Interaction,10ms,.25f,[&](float){weakerApplied=true;return true;})&&!weakerApplied,"weaker cue cannot overwrite a landed impact");
        Check(impacts.Pulse(1,Category::Interaction,10ms,.25f,[](float){return true;}),"impact protection is per hand");
        std::this_thread::sleep_for(30ms);Check(ended==0,"rejected weak cue cannot shorten impact deadline");
        impacts.Cancel();Check(ended==1,"explicit cancellation still stops protected impact");
        Check(impact()&&impacts.Pulse(0,Category::Alert,10ms,1.f,[](float){return true;}),"stronger alert can replace an impact");
        impacts.Cancel();Check(ended==2,"replacement owns the single stop");
        Strengths preview;
        Check(impact()&&impacts.Pulse(0,Category::Interaction,10ms,.25f,[](float){return true;},&preview),"deliberate settings preview can replace an impact");
        impacts.Stop();Check(ended==3,"shutdown stops preview once");
    }
    {
        Pulses tuned;Strengths levels;std::array<std::atomic<int>,2> canceled{};
        Check(tuned.Start([&](int h){++canceled[h];}),"start strength fixture");
        auto send=[&](int h,Category category,float amplitude,float expected,const Strengths* preview=nullptr){
            bool called=false;
            const bool sent=tuned.Pulse(h,category,5s,amplitude,[&](float scaled){called=true;Check(std::fabs(scaled-expected)<.00001f,"runtime receives scaled amplitude");return true;},preview);
            Check(sent==(expected>0)&&called==sent,"Off suppresses the actual runtime call");
        };
        for(int h=0;h<2;++h)for(int c=0;c<CategoryCount;++c)send(h,Category(c),.4f,.4f);
        levels.master=50;levels.category[int(Category::BowDraw)]=50;tuned.Configure(levels);
        send(0,Category::BowDraw,.4f,.1f);send(1,Category::Melee,.4f,.2f);
        levels.master=200;levels.category[int(Category::Melee)]=200;tuned.Configure(levels);
        send(0,Category::Melee,.95f,1);send(1,Category::BowDraw,.4f,.4f);
        levels.category[int(Category::BowDraw)]=0;tuned.Configure(levels);
        Check(canceled[0]==0&&canceled[1]==1,"muting bow draw stops only that category");
        send(1,Category::BowDraw,.4f,0);send(0,Category::Melee,.95f,1);
        levels.master=0;tuned.Configure(levels);
        Check(canceled[0]==1&&canceled[1]==1,"master Off stops the remaining active hand");
        for(int h=0;h<2;++h)for(int c=0;c<CategoryCount;++c)send(h,Category(c),.5f,0);
        Strengths preview;preview.master=50;preview.category[int(Category::Spell)]=150;
        send(0,Category::Spell,.4f,.3f,&preview);send(1,Category::Spell,.4f,.3f,&preview);
        tuned.Configure(levels); // unchanged saved Off must not cancel a deliberate unsaved preview
        Check(canceled[0]==1&&canceled[1]==1,"preview can test re-enabling muted feedback");
        send(0,Category::Spell,.4f,0); // preview never changes the saved strength
        tuned.Cancel();Check(canceled[0]==2&&canceled[1]==2,"closing settings cancels both previews");
        levels={};levels.master=NAN;tuned.Configure(levels);send(0,Category::Melee,.4f,.4f);
        send(0,Category::Melee,NAN,0);send(0,Category::Melee,-1,0);send(0,Category::Melee,2,1);
        tuned.Stop();
    }
    Check(MotionCategory(1,true,false)==Category::BowDraw&&MotionCategory(2,true,false)==Category::BowRelease,"bow draw and release have independent categories");
    Check(MotionCategory(4,false,false)==Category::BowDraw&&MotionCategory(5,true,false)==Category::BowDraw,"string proximity and full draw use draw strength");
    Check(MotionCategory(3,true,false)==Category::Interaction&&MotionCategory(1,false,false)==Category::Interaction,"potions and selection do not inherit equipped bow strength");
    Check(MotionCategory(2,false,true)==Category::BowRelease&&MotionCategory(6,false,false)==Category::Melee,"throw release and melee readiness route correctly");
    {
        CastEvents casts;casts.Publish(0,1,1000);casts.Publish(1,1,1000);
        Check(casts.Take(0,1,1001,true)&&!casts.Take(0,1,1001,true)&&casts.Take(1,1,1001,true),"each hand's successful cast is consumed once");
        casts.Publish(0,1,1100);Check(!casts.Take(0,2,1101,true),"another character cannot inherit spell feedback");
        casts.Publish(0,1,1200);Check(!casts.Take(0,1,1350,true),"stale spell pulse expires");
        casts.Publish(0,1,1400);Check(!casts.Take(0,1,1401,false)&&!casts.Take(0,1,1402,true),"menu or inactive frame consumes feedback without replay");
        casts.Publish(0,1,1500);casts.Clear();Check(!casts.Take(0,1,1501,true),"focus loss clears pending spells");
        casts.Publish(-1,1,1600);casts.Publish(2,1,1600);casts.Publish(0,0,1600);
        Check(!casts.Take(0,1,1601,true)&&!casts.Take(2,1,1601,true),"invalid hand or session never produces feedback");
    }
    std::puts("Haptics: native gamepad rumble isolation, per-hand output bindings, master/categories, runtime amplitude, mute/cancel, preview isolation, cast events, expiry and lifecycle passed");
}
