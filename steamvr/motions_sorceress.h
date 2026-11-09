#pragma once
#include "spell_motion.h"
#include <cmath>
#include <vector>

// The Sorceress's ground and summon motions (owner, 2026-11-05, from the list of her spells without one:
// "Good fits (ground targets and summons, where a motion helps)"), detected on spellmotion::Engine's
// history as the other classes' are (head-local: +X right, +Y up from the eyes, forward -Z; hand 0 the
// right). Her first ten are the Engine's own (spell_motion.h). Fire Wall is the Necromancer's Bone Wall
// sweep and Hydra his left arm's golem raise (necromancer::detail). Include spell_motion.h, not this.
// Value-only: steamvr/spell_motion_test.cpp.
namespace framevr::spellmotion::sorceress {
struct Tuning {
    float fireWallAhead=5;   // Fire Wall: the wall this many cells away where the sweep's middle heads
    // Meteor: the right fist overhead (meteorFrom above the eyes or more), then thrown down meteorFall or
    // more and forward meteorForward or more within meteorWindow, at meteorSpeed or faster, stopped (under
    // meteorStop m/s) in front (meteorFront ahead or more) under the chin (meteorEnd), the left hand not
    // brought down with it (falling meteorOther at most: that is Blizzard). It lands where the arm then
    // points from the shoulder (cast as the fist passed the shoulder, it went the full meteorFar):
    // meteorFar cells with the arm level, nearer evenly as it points down, meteorNear at meteorSteepDeg
    // below. Thunder Storm is the arm held up still for holdSeconds: a throw that waits that long is its.
    float meteorFrom=.15f,meteorFall=.30f,meteorForward=.15f,meteorWindow=.50f,meteorSpeed=2.0f,meteorStop=.8f,
        meteorFront=.30f,meteorEnd=-.05f,meteorOther=.20f;
    float meteorFar=20,meteorNear=4,meteorSteepDeg=50;
    // Blizzard: both hands overhead (blizzardFrom above the eyes or more), then pulled down blizzardFall or
    // more each within blizzardWindow, at blizzardSpeed or faster, ending in front (blizzardFront ahead or
    // more) between blizzardLow and blizzardHigh, at most blizzardApart apart (Nova flings them wide). It
    // lands blizzardAhead cells straight ahead.
    float blizzardFrom=.10f,blizzardFall=.35f,blizzardWindow=.50f,blizzardSpeed=1.5f,blizzardFront=.15f,
        blizzardLow=-.60f,blizzardHigh=-.05f,blizzardApart=.70f,blizzardAhead=8;
    // Hydra: the left arm swept up fast in front (the golems' raise, necromancer::tuning), ending hydraFront
    // ahead or more (Thunder Storm's arm goes straight up); hydraAhead cells away where the arm heads.
    float hydraFront=.20f,hydraAhead=4;
};
inline constexpr Tuning tuning{};
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::FireWall:return "sweep the right hand sideways across in front of your chest, drawing the wall";
    case Motion::Meteor:return "raise the right fist overhead, then throw it down and forward at the spot";
    case Motion::Blizzard:return "raise both arms overhead, then pull them down in front of you";
    case Motion::Hydra:return "sweep the left arm up fast in front of you, toward its spot";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    const auto& s=e.History();
    if(s.empty())return false;
    const auto& n=s.back();
    const auto& t=tuning;
    const auto F=[](math::Vec l){return Engine::Forward(l);};
    using necromancer::detail::PeakSpeed;
    switch(m) {
    case Motion::FireWall:return necromancer::detail::SideSweep(e,s,t.fireWallAhead);
    case Motion::Meteor: {   // the right fist thrown down and forward from overhead
        const auto& c=n.l[0];
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.meteorWindow;) {
            const auto& a=s[i].l[0];
            if(a.y<t.meteorFrom)continue;
            const float fall=a.y-c.y,forward=F(c)-F(a);
            if(fall>=.05f)e.Toward(fall/t.meteorFall);
            if(fall<t.meteorFall||forward<t.meteorForward||F(c)<t.meteorFront||c.y>t.meteorEnd||Engine::Len(e.Velocity(0))>=t.meteorStop||
               s[i].l[1].y-n.l[1].y>t.meteorOther||PeakSpeed(s,i,0)<t.meteorSpeed)continue;
            const math::Vec shoulder{.17f,-.22f,.05f};
            auto spot=SpotAlong(c-shoulder,t.meteorFar,t.meteorNear,t.meteorSteepDeg);
            const float cells=std::hypot(spot.ahead,spot.right);
            if(spot.ahead<.3f*cells)spot={cells,0};   // heading off behind: straight ahead
            e.Aim(cells>1e-3f?math::Vec{spot.right/cells,0,-spot.ahead/cells}:math::Vec{0,0,-1});
            e.LandAt(spot.ahead,spot.right);
            return true;
        }
        return false;
    }
    case Motion::Blizzard: {   // both arms pulled down in front from overhead
        bool end=Engine::Apart(n)<=t.blizzardApart;
        for(const auto& h:n.l)end=end&&F(h)>=t.blizzardFront&&h.y>=t.blizzardLow&&h.y<=t.blizzardHigh;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.blizzardWindow;) {
            const auto& a=s[i];
            if(a.l[0].y<t.blizzardFrom||a.l[1].y<t.blizzardFrom)continue;
            const float fall=std::min(a.l[0].y-n.l[0].y,a.l[1].y-n.l[1].y);
            if(fall>=.05f)e.Toward(fall/t.blizzardFall);
            if(end&&fall>=t.blizzardFall&&PeakSpeed(s,i,0)>=t.blizzardSpeed&&PeakSpeed(s,i,1)>=t.blizzardSpeed) {
                e.Aim({0,0,-1});e.LandAt(t.blizzardAhead,0);
                return true;
            }
        }
        return false;
    }
    case Motion::Hydra: {   // the left arm swept up in front, toward its spot
        if(!necromancer::detail::ArmUp(e,s,1)||F(n.l[1])<t.hydraFront)return false;
        auto way=necromancer::detail::ArmHeading(n.l[1],1);
        if(-way.z<.3f)way={0,0,-1};
        e.Aim(way);e.LandAt(t.hydraAhead*-way.z,t.hydraAhead*way.x);
        return true;
    }
    default:return false;
    }
}
}
