#pragma once
#include <cmath>
#include "frame_math.h"

// Warcries by gesture (#283): a hand brought up to the mouth, held there a
// moment, then pushed out forward like a shout casts that hand's trigger
// warcry. Room space; no game memory. Right hand = 0, left = 1.
namespace framevr::warcry {
using math::Vec; using math::Quat;
// Skills.txt ids. Howl 130, Shout 138, Battle Cry 146, Battle Orders 149, War
// Cry 154 and Battle Command 155 centre on the hero; Taunt 137 takes its
// monster from the cast's own aim, as a trigger press would. Find Potion 131,
// Find Item 142 and Grim Ward 150 need a corpse and stay buttons.
inline constexpr int Taunt=137;
inline bool Gesture(int skill) {
    switch(skill) {
        case 130:case 138:case 146:case 149:case 154:case 155:case Taunt:return true;
        default:return false;
    }
}
// The mouth as potion drinking places it: 10 cm below the eyes, 8 cm ahead.
inline Vec Mouth(Vec head,Quat rotation){return head+math::Rotate(rotation,{0,-.10f,-.08f});}
// The controller's grip comes within MouthRadius of the mouth and stays
// within LeaveRadius, nearly still (StillSpeed), for Dwell: a guard or a swing
// passing the face is no shout. Then, within PushTime of starting to move, it
// travels PushDistance from where it rested, at least PushForward of that
// along the head's level forward (about 45 degrees off it at most).
inline constexpr float MouthRadius=.15f,LeaveRadius=.19f;
inline constexpr float StillSpeed=.6f;     // m/s
inline constexpr double Dwell=.20;
inline constexpr float PushDistance=.22f,PushForward=.70f;
inline constexpr double PushTime=.25;   // under about 0.9 m/s on average is a reach
inline constexpr double Rearm=.60;         // between two shouts from one hand
inline constexpr double MaxStep=.10;       // a longer gap is a hitch: start over

struct Step {int ready=-1,cast=-1;};   // the hand that came to rest at the mouth, the hand that shouted
class Shout {
    enum class Stage {Idle,Resting,Ready,Pushing};
    struct Hand {
        Stage stage=Stage::Idle;
        bool primed=false;
        double previousTime=0,since=0,shoutedAt=-1e9;
        Vec previous{},rest{},forward{};
    } hands[2];
public:
    void Reset(){*this=Shout{};}
    bool Ready(int hand) const {return hand>=0&&hand<2&&hands[hand].stage>=Stage::Ready;}
    // eligible[h]: that hand is tracked, empty, its trigger released and its
    // trigger skill a warcry (Gesture). Not eligible, it starts over.
    Step Update(double time,Vec head,Quat headRotation,const Vec hand[2],const bool eligible[2]) {
        Step out;
        if(!std::isfinite(time))return out;
        const Vec mouth=Mouth(head,headRotation);
        Vec level=math::Rotate(headRotation,{0,0,-1});level.y=0;
        level=math::Normalized(level);
        for(int h=0;h<2;++h) {
            auto& s=hands[h];
            const Vec p=hand[h];
            const bool finite=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
            const double dt=time-s.previousTime;
            if(!eligible[h]||!finite||!s.primed||dt<=0||dt>MaxStep) {
                const double shouted=s.shoutedAt;
                s=Hand{};s.shoutedAt=shouted;s.primed=eligible[h]&&finite;s.previous=p;s.previousTime=time;
                continue;
            }
            const float speed=std::sqrt(math::Dot(p-s.previous,p-s.previous))/float(dt);
            s.previous=p;s.previousTime=time;
            const Vec fromMouth=p-mouth;
            const float distance=std::sqrt(math::Dot(fromMouth,fromMouth));
            switch(s.stage) {
                case Stage::Idle:
                    if(distance<MouthRadius&&speed<StillSpeed&&time-s.shoutedAt>=Rearm){s.stage=Stage::Resting;s.since=time;}
                    break;
                case Stage::Resting:
                    if(distance>=LeaveRadius||speed>=StillSpeed){s.stage=Stage::Idle;break;}
                    if(time-s.since>=Dwell&&level.x*level.x+level.z*level.z>.5f) {
                        s.stage=Stage::Ready;s.rest=p;s.forward=level;out.ready=h;
                    }
                    break;
                case Stage::Ready:
                    if(speed<StillSpeed) {
                        // Still resting: keep the latest rest point and heading.
                        if(distance>=LeaveRadius){s.stage=Stage::Idle;break;}
                        s.rest=p;if(level.x*level.x+level.z*level.z>.5f)s.forward=level;
                        break;
                    }
                    s.stage=Stage::Pushing;s.since=time;
                    [[fallthrough]];
                case Stage::Pushing: {
                    const Vec push=p-s.rest;
                    const float travel=std::sqrt(math::Dot(push,push));
                    if(travel>=PushDistance) {
                        // One shout per step: the right hand's wins a tie.
                        if(math::Dot(push,s.forward)>=PushForward*travel&&out.cast<0){out.cast=h;s.shoutedAt=time;}
                        s.stage=Stage::Idle;break;   // pushed far, forward or not: a fresh rest is needed
                    }
                    if(time-s.since>PushTime)s.stage=Stage::Idle;   // too slow: a reach, not a shout
                    break;
                }
            }
        }
        return out;
    }
};
}
