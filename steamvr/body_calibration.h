#pragma once
#include "body_tracking.h"
#include "vr_settings.h"

namespace framevr::calibration {
using math::Vec;
enum class Phase { Idle, Release, Ready, Hold, Saving, Saved, Failed };
enum class Hint { Pose, Release, Floor, Height, Tracking, Upright, Arms, Still, Saving, Saved, Failed, Conflict };
struct Measurement { float eyeHeight=0,armSpan=0; };
inline bool ValidEyeHeight(float height) {return std::isfinite(height)&&height>=.8f&&height<=2.3f;}
inline bool Valid(Measurement m) {
    return ValidEyeHeight(m.eyeHeight)&&std::isfinite(m.armSpan)&&m.armSpan>=.85f&&m.armSpan<=2.6f&&
        m.armSpan/m.eyeHeight>=.65f&&m.armSpan/m.eyeHeight<=1.45f;
}
inline bool Stored(const settings::Values& v) {
    return v[settings::BodyCalibration]==1&&Valid({v[settings::EyeHeight],v[settings::ArmSpan]});
}
inline settings::Request SaveRequest(const settings::Snapshot& s,Measurement m) {
    if(!s.ready||s.result==settings::Result::Saving||!Valid(m))return {};
    auto values=s.values;values[settings::EyeHeight]=m.eyeHeight;values[settings::ArmSpan]=m.armSpan;
    values[settings::BodyCalibration]=1;
    return {s.values,values,settings::Changed(s.values,values),s.sequence+1};
}
struct Sample {
    double time=0;
    bool floor=false,headTracked=false,handTracked[2]{},cancel=false;
    Vec head{},hand[2]{};math::Quat rotation{0,0,0,1};
    float trigger[2]{};
};
inline float Length(Vec v){return std::sqrt(math::Dot(v,v));}
// How CheckPose reads the T cross. The defaults are the capture's own limits;
// the T cross gesture (body_calibration_gesture.h) starts on them, keeps a
// wider set while the pose is held, and asks about the floor separately.
struct PoseLimits {
    float upright=.96f,side=.35f,center=.18f,level=.09f,forward=.22f,dropLow=.10f,dropHigh=.50f,spanSlack=1;
    bool floor=true;
};
inline Hint CheckPose(const Sample& s,const PoseLimits& limits={}) {
    if(!s.headTracked||!s.handTracked[0]||!s.handTracked[1]||!body::Finite(s.head)||
       !body::Finite(s.hand[0])||!body::Finite(s.hand[1]))return Hint::Tracking;
    if(limits.floor&&!s.floor)return Hint::Floor;
    if(!ValidEyeHeight(s.head.y))return Hint::Height;
    const auto q=s.rotation;
    const float q2=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!std::isfinite(q2)||std::fabs(q2-1)>.05f||math::Rotate(q,{0,1,0}).y<limits.upright)return Hint::Upright;
    const auto undo=math::Conjugate(math::Yaw(math::Heading(q)));
    const auto r=math::Rotate(undo,s.hand[0]-s.head),l=math::Rotate(undo,s.hand[1]-s.head);
    const float span=Length(s.hand[0]-s.hand[1]),drop=-(r.y+l.y)*.5f;
    // A resting/bent arm makes the controller span short even with a correct
    // stage floor. Only a missing floor space gets the playspace hint.
    if(!Valid({s.head.y,span})&&!Valid({s.head.y,span*limits.spanSlack}))return Hint::Arms;
    // Opposite sides, level shoulders and little forward reach. Grip centres
    // measure arm reach, not fingertip-to-fingertip anatomical wingspan.
    if(r.x<limits.side||l.x>-limits.side||std::fabs(r.x+l.x)>limits.center||std::fabs(r.y-l.y)>limits.level||
       std::fabs(r.z)>limits.forward||std::fabs(l.z)>limits.forward||drop<limits.dropLow||drop>limits.dropHigh)return Hint::Arms;
    return Hint::Pose;
}
class Flow {
    double previous=0,started=0,completed=0;
    Sample anchor{};
    double heightSum=0,spanSum=0;
    unsigned samples=0;
    bool cancelWas=true;
    uint64_t sequence=0;
    Measurement measured{};
public:
    Phase phase=Phase::Idle;
    Hint hint=Hint::Release;
    float progress=0;
    bool Active()const{return phase!=Phase::Idle;}
    Measurement Get()const{return measured;}
    void Begin(){*this=Flow{};phase=Phase::Release;}
    void Cancel(){phase=Phase::Idle;progress=0;}
    void Suspend(){if(phase==Phase::Ready||phase==Phase::Hold){phase=Phase::Release;hint=Hint::Tracking;progress=0;}previous=0;cancelWas=true;}
    void Submitted(uint64_t serial){sequence=serial;}
    void Failed(bool conflict=false){phase=Phase::Failed;hint=conflict?Hint::Conflict:Hint::Failed;progress=0;}
    void Observe(const settings::Snapshot& s,double now){
        if(phase!=Phase::Saving||!sequence||s.sequence!=sequence||s.result==settings::Result::Saving)return;
        if(s.result==settings::Result::Saved&&Stored(s.values)){phase=Phase::Saved;hint=Hint::Saved;completed=now;}
        else Failed(s.result==settings::Result::Conflict);
    }
    // Returns true exactly once per complete, stable capture. Saving remains
    // input-owned until the store acknowledges it and the triggers release.
    bool Step(const Sample& s){
        if(!Active())return false;
        const bool cancel=s.cancel&&!cancelWas;cancelWas=s.cancel;
        if(cancel&&phase!=Phase::Saving){Cancel();return false;}
        const bool released=s.trigger[0]<.2f&&s.trigger[1]<.2f;
        const bool both=s.trigger[0]>.65f&&s.trigger[1]>.65f;
        if(phase==Phase::Saving)return false;
        if(phase==Phase::Saved){if(released&&s.time-completed>=1)Cancel();return false;}
        if(!std::isfinite(s.time)||s.time<=0){Suspend();return false;}
        const bool gap=previous!=0&&(s.time<=previous||s.time-previous>.15);
        previous=s.time;
        if(gap)Suspend();
        const auto pose=CheckPose(s);
        if(pose!=Hint::Pose){hint=pose;phase=Phase::Release;progress=0;return false;}
        if(phase==Phase::Release||phase==Phase::Failed){
            if(released){phase=Phase::Ready;hint=Hint::Pose;}
            return false;
        }
        if(phase==Phase::Ready){
            if(!both)return false;
            anchor=s;started=s.time;heightSum=spanSum=0;samples=0;phase=Phase::Hold;
        }
        if(!both){phase=Phase::Release;hint=Hint::Release;progress=0;return false;}
        if(Length(s.head-anchor.head)>.03f||Length(s.hand[0]-anchor.hand[0])>.045f||Length(s.hand[1]-anchor.hand[1])>.045f){
            phase=Phase::Release;hint=Hint::Still;progress=0;return false;
        }
        heightSum+=s.head.y;spanSum+=Length(s.hand[0]-s.hand[1]);++samples;
        progress=std::clamp(float((s.time-started)/1.2),0.f,1.f);
        if(progress<1||samples<30)return false;
        measured={float(heightSum/samples),float(spanSum/samples)};
        if(!Valid(measured)){phase=Phase::Release;hint=Hint::Arms;progress=0;return false;}
        phase=Phase::Saving;hint=Hint::Saving;return true;
    }
};
// Cancelling or completing must not turn a still-held trigger or face button
// into an attack, menu command or quick cast on the following frame.
struct ReleaseGate {
    bool blocked=false;
    void Consume(){blocked=true;}
    bool Step(bool active,float right,float left,bool buttons){
        const bool owned=active||blocked;
        if(active)blocked=true;
        else if(right<.2f&&left<.2f&&!buttons)blocked=false;
        return owned;
    }
};
}
