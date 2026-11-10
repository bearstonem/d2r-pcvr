#pragma once
#include "gameplay.h"

// Joining D2RLoader clients submit ordinary native inputs, never damage or
// authoritative missile creation. The host retains all native validation.
namespace framevr::nativeclient {
struct Binding {int slot=-1,id=-1,owner=-1;};
struct Context {
    bool native=false;
    bool actionOwned=false; // glyph drawing/casting owns native input outside skill slots
    uint64_t session=0;
    Binding basic{},bow{},throwing{},melee[2]{},shield{};
    bool offhand=false;
    float swingSpeed=1.6f,shieldSpeed=1.6f;
};
class Router {
    bool primed=false;
    uint64_t session=0,assignment=0;
    uint32_t equipment=0;
    double at=0,until=0;
    gameplay::Weapon weapon=gameplay::Weapon::Unknown;
    gameplay::Pose previous[2]{};
    bool armed[2]{};
    double next[2]{};
    int pulseSlot=-1,pulseHand=0;
    math::Vec origin{},direction{};
public:
    void Reset(){*this=Router{};}
    // Returns the aiming hand for a synthesized action, -1 otherwise.
    int Step(const gameplay::Input& in,gameplay::Output& out,const Context& c) {
        if(!c.native){Reset();return -1;}
        const bool arrow=out.bowRelease,thrown=out.javelinRelease,left=out.leftRelease;
        const bool contact=out.meleeContact,shield=out.shieldContact;
        out.bowRelease=out.javelinRelease=out.leftRelease=false;
        out.meleeContact=out.shieldContact=false;
        if(c.actionOwned){Reset();return -1;}
        const double dt=in.time-at;
        if(!c.session||!in.active||out.block||!out.rayValid||in.palette||in.cancel||
           !in.tracked[0]||!in.tracked[1]||!in.aimTracked||
           !gameplay::Valid(in.hand[0])||!gameplay::Valid(in.hand[1])||!std::isfinite(in.time)) {
            Reset();return -1;
        }
        if(!primed||session!=c.session||equipment!=in.equipment||assignment!=in.skillContext||weapon!=in.weapon||
           dt<=0||dt>.1||gameplay::Length(in.hand[0].p-previous[0].p)>.35f||gameplay::Length(in.hand[1].p-previous[1].p)>.35f) {
            Reset();primed=true;session=c.session;equipment=in.equipment;assignment=in.skillContext;weapon=in.weapon;
            at=in.time;previous[0]=in.hand[0];previous[1]=in.hand[1];previousHead=in.head.p;return -1;
        }
        float speed[2]{};
        for(int h=0;h<2;++h){
            const auto delta=(in.hand[h].p-in.head.p)-(previous[h].p-previousHead);
            const float angular=gameplay::RotationDistance(previous[h].q,in.hand[h].q)/float(dt);
            speed[h]=gameplay::Length(delta)/float(dt)+angular*gameplay::MeleeLeverage(in.meleeStyle);
            previous[h]=in.hand[h];
            if(angular>gameplay::MeleeAngularTrackingLimit||speed[h]>gameplay::MeleeHandTrackingLimit){Reset();return -1;}
        }
        previousHead=in.head.p;at=in.time;
        // Explicit native casts/interaction own the action; never double up.
        if(in.quickCast||out.skill>=0||out.attack){until=0;armed[0]=armed[1]=false;return -1;}
        auto start=[&](Binding b,int h,math::Vec o,math::Vec d){
            if(in.time<until)return;
            if(b.slot<0)b=c.basic;
            if(b.slot<0||b.slot>=skills::Count||b.id<0)return;
            pulseSlot=b.slot;pulseHand=h;origin=o;direction=d;
            // Alternate-bank input needs 50 ms preparation at the consumer.
            until=in.time+(b.slot>=0?.180:.100);
        };
        if(arrow)start(c.bow,1,out.rayOrigin,out.rayDirection);
        if(thrown&&c.throwing.slot>=0)start(c.throwing,0,out.rayOrigin,out.rayDirection);
        if(left&&c.throwing.slot>=0)start(c.throwing,1,out.leftOrigin,out.leftDirection);
        for(int h=0;h<2;++h){
            const bool bash=h==1&&shield&&c.shield.slot>=0;
            const bool eligible=bash||(contact&&(h==0||c.offhand));
            const float threshold=std::max(.2f,bash?c.shieldSpeed:c.swingSpeed);
            if(!eligible){armed[h]=false;continue;}
            if(speed[h]<threshold*.4f)armed[h]=true;
            if(armed[h]&&speed[h]>=threshold&&in.time>=next[h]&&in.time>=until){
                armed[h]=false;next[h]=in.time+.25;
                start(bash?c.shield:c.melee[h],h,in.hand[h].p,math::Rotate(in.hand[h].q,{0,0,-1}));
            }
        }
        if(in.time>=until)return -1;
        out.skill=pulseSlot;out.attack=pulseSlot<0;out.rayOrigin=origin;out.rayDirection=direction;
        return pulseHand;
    }
private:
    math::Vec previousHead{};
};
}
