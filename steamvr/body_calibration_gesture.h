#pragma once
// The T cross: stand in it with no menu up, nothing pressed, and the body calibration
// prompt opens by itself (steamvr/BODY_CALIBRATION.md). The pose is the capture's
// own check (CheckPose). Both controllers buzz lightly for as long as the pose is
// held and the timer runs; losing the pose stops the buzz and resets the timer, so
// the buzz tells the player whether they are still in it. Nothing is saved here:
// the prompt still needs its two-trigger hold, and Right B cancels it.
// One Step a frame; the transport fills the input and does what the output says.
#include "body_calibration.h"
#include "target_cone.h"
#include <cstdint>
namespace framevr::calibration {
inline constexpr double TCrossHoldSeconds=2.0;     // the pose held, buzzing, before the prompt opens
inline constexpr double TCrossSettleSeconds=.2;    // still in the pose this long before the buzz starts: a swing through it never buzzes
inline constexpr double TCrossGraceSeconds=.1;     // past the wider limits this briefly is a wobble, not leaving the pose
inline constexpr double TCrossRearmSeconds=.5;     // after the prompt: out of the pose this long before it can start again
inline constexpr double TCrossBuzzEvery=.1;        // one pulse per hand this often; TCrossBuzzMs overlaps it well past a slow frame, so it reads as one buzz
inline constexpr double TCrossLossEvery=30;        // a lost hold logs at most this often
inline constexpr double TCrossScanWait=.3;         // the monster scan's first answer may wait this long after the pose began, then the hold goes on without it
inline constexpr uint64_t TCrossScanFreshMs=800;   // an older answer is no answer
inline constexpr double TCrossRefusalEvery=30;     // a refusal logs at most this often
inline constexpr float TCrossBuzzStrength=.22f;    // before the player's Vibration strengths
inline constexpr int TCrossBuzzMs=180;             // a late pulse must not read as the pose lost; leaving cancels at once, and the haptic timer ends it this soon if Input stops
inline constexpr float TCrossOpenStrength=.7f;     // the prompt opened: clearly not the light buzz
inline constexpr int TCrossOpenMs=220;
inline constexpr double TCrossSpeedWindow=.1;      // speed is measured over this long: one predicted pose's jitter is not motion
inline constexpr float TCrossEnterSpeed=.5f;       // m/s, hands and head: slower than this to start
inline constexpr float TCrossKeepSpeed=1.f;        // m/s to stay in it: an arm swing is far faster, a settling arm is not
inline constexpr float TCrossHostileCells=12;      // a monster in sight this close: not now
inline constexpr float TCrossTriggerFree=.3f,TCrossGripFree=.55f,TCrossStickFree=.3f;
// Starting needs the capture's own limits, so a pose that starts is one the prompt accepts.
inline constexpr PoseLimits TCrossEnter{.floor=false};
// Held, the limits widen: a tired arm or a nod does not end it.
inline constexpr PoseLimits TCrossKeep{.upright=.92f,.side=.30f,.center=.26f,.level=.16f,.forward=.30f,.dropLow=.04f,.dropHigh=.58f,.spanSlack=1.12f,.floor=false};
// Why a T cross did not start.
enum class TCrossRefusal:uint8_t {None,Floor,Seated,Menu,Hands,Holding,Stick,Hostile};
inline const char* TCrossRefusalName(TCrossRefusal r){
    switch(r){
        case TCrossRefusal::Floor:return "no floor-relative tracking";
        case TCrossRefusal::Seated:return "Seated play is on";
        case TCrossRefusal::Menu:return "a menu or wheel is open";
        case TCrossRefusal::Hands:return "a trigger, grip or button is down";
        case TCrossRefusal::Holding:return "a hand is holding something or casting";
        case TCrossRefusal::Stick:return "a stick is pushed";
        case TCrossRefusal::Hostile:return "a monster is close";
        default:return "none";
    }
}
// Why a running hold ended (a guard arriving says so as a refusal instead).
enum class TCrossLoss:uint8_t {None,Tracking,Pose,Speed,Stall};
inline const char* TCrossLossName(TCrossLoss l){
    switch(l){
        case TCrossLoss::Tracking:return "tracking";
        case TCrossLoss::Pose:return "pose";
        case TCrossLoss::Speed:return "speed";
        case TCrossLoss::Stall:return "stall";
        default:return "none";
    }
}
struct TCrossInput {
    Sample pose{};              // time, head, hands, tracking, floor, triggers; cancel is unused
    float grip[2]{},stick=0;    // stick: the larger push, 0-1
    bool on=true;               // the switch, and a game world to open it in
    bool calibrating=false;     // the prompt is up, or still holds the input it consumed
    bool seated=false,menu=false,buttons=false,holding=false,hostile=false;
    bool scanAnswered=false;    // the monster list is fresh; a game you joined has none: there, monsters do not block the T cross
};
struct TCrossOutput {
    bool buzz=false;    // the pose is held and the timer runs
    bool pulse=false;   // this step: one light pulse on each hand
    bool stop=false;    // this step: the buzz ended, stop the controllers now
    bool open=false;    // this step: open the prompt and give the distinct buzz
    bool scan=false;    // the pose is there and only monsters could refuse it: look for them
    bool unguarded=false;   // this step: a hold began with no monster list to check
    TCrossRefusal refused=TCrossRefusal::None;   // this step: tell the log why not
    TCrossLoss lost=TCrossLoss::None;            // this step: tell the log why the hold ended
    float lostAfter=0;                           // ...and how long it had run, seconds
};
// Whether the T cross can open the prompt, as far as settings and the floor tell:
// the prompt says so only then.
inline bool TCrossAvailable(const settings::Values& v,bool floor){return floor&&v[settings::TCrossCalibrate]>.5f&&v[settings::Seated]<.5f;}
// Hands too close for any T cross: one dot product, the usual answer in play.
// The transport reads the rest of the input only past this.
inline bool TCrossHandsWide(const Sample& s){
    const auto d=s.hand[0]-s.hand[1];
    constexpr float span=.85f/1.12f;   // Valid's shortest controller span, with TCrossKeep's slack
    return math::Dot(d,d)>=span*span;
}
inline bool TCrossHostileClose(const targetcone::Target* targets,int count,float heroX,float heroY){
    for(int i=0;i<count;++i)if(targets[i].clear&&std::hypot(targets[i].x-heroX,targets[i].y-heroY)<=TCrossHostileCells)return true;
    return false;
}
// The monster scan's answer: fresh or none. With none (at 0: a game you joined, or not yet) no
// monster can be called close.
inline void TCrossReadScan(TCrossInput& in,uint64_t at,uint64_t now,const targetcone::Target* targets,int count,float heroX,float heroY){
    in.scanAnswered=at>0&&now-at<=TCrossScanFreshMs;
    in.hostile=in.scanAnswered&&TCrossHostileClose(targets,count,heroX,heroY);
}
class TCrossGesture {
    enum class State:uint8_t {Idle,Settling,Holding};
    State state=State::Idle;
    bool armed=true,told=false,fast=false;
    TCrossLoss outWhy=TCrossLoss::None;
    double previous=0,entered=0,started=0,outSince=0,poseSince=0,lastPulse=0,markAt=0;
    double refusedAt=-TCrossRefusalEvery,lostAt=-TCrossLossEvery;
    Vec markHead{},markHand[2]{};
    void End(TCrossOutput& out){if(state==State::Holding)out.stop=true;state=State::Idle;}
    void ForgetSpeed(){markAt=0;fast=false;}
    void Lose(TCrossOutput& out,const Sample& s,TCrossLoss why){
        if(state==State::Holding&&s.time-lostAt>=TCrossLossEvery){lostAt=s.time;out.lost=why;out.lostAfter=float(s.time-started);}
        End(out);
    }
    void Mark(const Sample& s){markAt=s.time;markHead=s.head;markHand[0]=s.hand[0];markHand[1]=s.hand[1];}
    // The last whole window's speed: it stands until the next one.
    void MeasureSpeed(const Sample& s,float limit){
        if(markAt==0){Mark(s);return;}
        if(s.time-markAt<TCrossSpeedWindow)return;
        const float most=limit*float(s.time-markAt);
        const auto beyond=[&](Vec a,Vec b){const auto d=a-b;return math::Dot(d,d)>most*most;};
        fast=beyond(s.head,markHead)||beyond(s.hand[0],markHand[0])||beyond(s.hand[1],markHand[1]);
        Mark(s);
    }
public:
    void Reset(){*this=TCrossGesture{};}
    TCrossOutput Step(const TCrossInput& in){
        TCrossOutput out;
        const auto& s=in.pose;
        // The prompt up: it can only start again after the arms come down.
        if(in.calibrating){End(out);ForgetSpeed();armed=false;outSince=poseSince=0;previous=s.time;return out;}
        if(!in.on||!std::isfinite(s.time)||s.time<=0){End(out);ForgetSpeed();previous=outSince=poseSince=0;return out;}
        const double dt=previous>0?s.time-previous:0;
        const bool timed=dt>0&&dt<=.25;
        if(previous>0&&!timed){Lose(out,s,TCrossLoss::Stall);ForgetSpeed();poseSince=0;}   // a stall or a jump: no speed to read, no hold to trust
        previous=s.time;
        const bool held=state==State::Holding;
        const bool posed=TCrossHandsWide(s)&&CheckPose(s,held||!armed?TCrossKeep:TCrossEnter)==Hint::Pose;
        if((posed||held)&&armed)MeasureSpeed(s,held?TCrossKeepSpeed:TCrossEnterSpeed);
        else ForgetSpeed();
        if(!posed||(held&&fast)){
            if(outSince==0){
                outSince=s.time;
                outWhy=!s.headTracked||!s.handTracked[0]||!s.handTracked[1]?TCrossLoss::Tracking:posed?TCrossLoss::Speed:TCrossLoss::Pose;
            }
            poseSince=0;
            const double away=s.time-outSince;
            if(away>=TCrossRearmSeconds){armed=true;told=false;}
            if(!held||away>=TCrossGraceSeconds)Lose(out,s,outWhy);
            if(state!=State::Holding)return out;
        }else{
            outSince=0;
            if(poseSince==0||fast)poseSince=s.time;   // a refusal needs a pose that stayed, not a swing through it
        }
        if(!armed)return out;
        const auto refusal=
            !s.floor?TCrossRefusal::Floor:in.seated?TCrossRefusal::Seated:in.menu?TCrossRefusal::Menu:
            in.buttons||s.trigger[0]>TCrossTriggerFree||s.trigger[1]>TCrossTriggerFree||in.grip[0]>TCrossGripFree||in.grip[1]>TCrossGripFree?TCrossRefusal::Hands:
            in.holding?TCrossRefusal::Holding:in.stick>TCrossStickFree?TCrossRefusal::Stick:in.hostile?TCrossRefusal::Hostile:TCrossRefusal::None;
        out.scan=refusal==TCrossRefusal::None||refusal==TCrossRefusal::Hostile;
        if(refusal!=TCrossRefusal::None){
            End(out);
            if(!told&&poseSince>0&&s.time-poseSince>=TCrossSettleSeconds&&s.time-refusedAt>=TCrossRefusalEvery){told=true;refusedAt=s.time;out.refused=refusal;}
            return out;
        }
        if(state==State::Idle){state=State::Settling;entered=started=s.time;return out;}
        if(state==State::Settling){
            if(fast){started=s.time;return out;}
            if(s.time-started<TCrossSettleSeconds)return out;
            if(!in.scanAnswered&&s.time-entered<TCrossScanWait)return out;   // a local game's first answer still counts
            state=State::Holding;started=s.time;lastPulse=0;out.unguarded=!in.scanAnswered;
        }
        if(s.time-started>=TCrossHoldSeconds){
            out.open=true;state=State::Idle;armed=false;outSince=0;
            return out;
        }
        out.buzz=true;
        if(lastPulse==0||s.time-lastPulse>=TCrossBuzzEvery){lastPulse=s.time;out.pulse=true;}
        return out;
    }
};
}
