#include "spell_motion.h"
#include "motion_left.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <string>
using namespace framevr;
using spellmotion::Motion;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

// The Paladin's motions (motions_paladin.h) on spellmotion::Engine: the scenarios of their own engines' tests
// (hammer-gesture-test, charge-gesture-test, cross-gesture-test, heavens-gesture-test until 2026-11-08) on the
// watcher, and the warmer/colder hum rising before each cast. Head-local points (yaw-local: +X right, +Y up,
// -Z forward) relative to the head; hands {right, left}. The controllers move between keyframes at 50 Hz
// (linear) unless Natural (minimum-jerk easing at 90 Hz, as a real arm moves), so velocities come from the
// motion (no runtime velocity).
struct Sim {
    // The motions' gate (spell_motion.h, owner 2026-11-04: "Both grips, like Sorceress"): gated, in.active is its
    // watch (the Paladin's are watched the frame the grips open too: spellmotion::WatchesOpening).
    bool gated=false;spellmotion::Gate gate;float grips[2]{};
    spellmotion::Engine g;spellmotion::Input in;spellmotion::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0,drops=0;std::string lastDrop,lastLine;
    float first=-1,peak=0;   // the first hum (progress at Begun or more) and the most, before the first cast
    float from=0,to=0;       // the last cast's figures (Fist of the Heavens: degrees)
    bool natural=false;float frame=.02f;
    void Natural(){natural=true;frame=1/90.f;}
    float Ease(float t)const{return natural?t*t*t*(10+t*(-15+6*t)):t;}
    Sim(Motion m,float heading,math::Vec right,math::Vec left,float span=0) {
        yaw=math::Yaw(heading);in.time=1;in.session=7;in.epoch=1;in.active=true;in.free=true;in.motion=m;
        in.tracked[0]=in.tracked[1]=true;in.grip[0]=in.grip[1]=0;in.armSpan=span;
        in.bodyScale=spellmotion::BodyScale(span);   // as the transport: the Paladin's metres stay its own
        in.head=head;in.headRotation=yaw;Place(0,right);Place(1,left);Step();
    }
    void Place(int h,math::Vec p){in.hand[h]=head+math::Rotate(yaw,p);}
    void Move(math::Vec right,math::Vec left,float seconds) {
        const math::Vec a0=in.hand[0],b0=in.hand[1];
        const math::Vec a1=head+math::Rotate(yaw,right),b1=head+math::Rotate(yaw,left);
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){const float t=Ease(float(i)/steps);in.hand[0]=a0+(a1-a0)*t;in.hand[1]=b0+(b1-b0)*t;Step();}
    }
    math::Vec Local(int h)const{return math::Rotate(math::Conjugate(yaw),in.hand[h]-head);}
    void MoveHand(int h,math::Vec p,float seconds){if(h)Move(Local(0),p,seconds);else Move(p,Local(1),seconds);}
    void Hold(float seconds){for(int i=0;i<int(seconds/frame);++i)Step();}
    void Step() {
        in.time+=frame;
        if(gated)in.active=gate.Step(grips,true).watch;
        out=motionleft::Seen(g.Step(motionleft::Played(in)));
        if(out.cast){++casts;from=out.castFigures[0];to=out.castFigures[1];}
        if(out.abandoned) {
            ++drops;lastDrop=out.abandoned;
            char line[200];Check(spellmotion::DropLine(in.motion,out,line,sizeof line),"a gesture given up has its line");lastLine=line;
        }
        if(!casts&&out.progress>=spellmotion::Begun){if(first<0)first=out.progress;peak=std::max(peak,out.progress);}
    }
    // The hum before the cast: begun, then warmer.
    bool Rose()const{return first>=spellmotion::Begun&&peak>first&&peak<=1;}
};
static bool Has(const std::string& s,const char* part){return s.find(part)!=std::string::npos;}

// Blessed Hammer (hammer_gesture_test.cpp): a fist from below the head punched up past it.
static const math::Vec HammerRest[2]{{.25f,-.55f,-.2f},{-.25f,-.55f,-.2f}};
struct Hammer:Sim {
    explicit Hammer(float heading=0):Sim(Motion::BlessedHammer,heading,HammerRest[0],HammerRest[1]){}
    // Right fist from the shoulder straight up past the head.
    void Pump(int hand=0,float seconds=.2f,float from=-.3f,float to=.2f) {
        const float x=hand?-.2f:.2f;
        in.grip[hand]=1;
        math::Vec r=hand?HammerRest[0]:math::Vec{x,from,-.2f},l=hand?math::Vec{x,from,-.2f}:HammerRest[1];
        Move(r,l,.25f);Hold(.1f);
        r=hand?HammerRest[0]:math::Vec{x,to,-.25f};l=hand?math::Vec{x,to,-.25f}:HammerRest[1];
        Move(r,l,seconds);Hold(.1f);
    }
    void Lower(int hand=0){const math::Vec r=hand?math::Vec{.25f,-.55f,-.2f}:math::Vec{.2f,-.3f,-.2f},l=hand?math::Vec{-.2f,-.3f,-.2f}:math::Vec{-.25f,-.55f,-.2f};Move(r,l,.25f);}
};
// Charge (charge_gesture_test.cpp): the weapon (right) hand's fist drawn back, then punched straight out.
static const math::Vec ChargeRest{.25f,-.6f,-.1f},LeftRest{-.25f,-.6f,-.15f};
struct Charge:Sim {
    explicit Charge(float heading=0,float span=0):Sim(Motion::Charge,heading,ChargeRest,LeftRest,span){}
    void Punch(float seconds=.15f,math::Vec from={.2f,-.25f,-.15f},math::Vec to={.15f,-.2f,-.62f}) {
        in.grip[0]=1;MoveHand(0,from,.25f);Hold(.1f);MoveHand(0,to,seconds);Hold(.1f);
    }
};
// Holy Shield (cross_gesture_test.cpp): the moving hand eased between points at 90 Hz.
static const math::Vec Rest{.25f,-.6f,-.15f},Forehead{0,.06f,-.12f},Chest{0,-.42f,-.12f},LeftShoulder{-.2f,-.28f,-.05f},RightShoulder{.2f,-.28f,-.05f};
struct Cross:Sim {
    explicit Cross(float heading=0):Sim(Motion::HolyShield,heading,Rest,LeftRest){Natural();}
    // The sign at a deliberate pace: each move ~0.35 s, a short touch at each point.
    void Sign(int h=0,bool rightFirst=false,float move=.35f,float touch=.08f) {
        const math::Vec a=rightFirst?RightShoulder:LeftShoulder,b=rightFirst?LeftShoulder:RightShoulder;
        for(const auto& p:{Forehead,Chest,a,b}){MoveHand(h,p,move);Hold(touch);}
        MoveHand(h,h?LeftRest:Rest,.4f);
    }
};
// Fist of the Heavens (heavens_gesture_test.cpp): the right shoulder where the tuning expects it; points on the arm
// by elevation (degrees above level) and reach (m from the shoulder), straight ahead unless yawed.
static math::Vec Arm(float elevationDeg,float reach,float yawDeg=0) {
    const float e=elevationDeg*3.14159265f/180,y=yawDeg*3.14159265f/180;
    return {.18f+reach*std::cos(e)*std::sin(y),-.22f+reach*std::sin(e),.06f-reach*std::cos(e)*std::cos(y)};
}
struct Heavens:Sim {
    explicit Heavens(float heading=0,float span=0):Sim(Motion::FistOfHeavens,heading,Rest,LeftRest,span){Natural();}
    // Swing the straight arm about the shoulder from one elevation to another.
    void Sweep(float from,float to,float s,float reach=.6f,float yawDeg=0) {
        const int n=std::max(1,int(s/frame));
        for(int i=1;i<=n;++i){Place(0,Arm(from+(to-from)*Ease(float(i)/n),reach,yawDeg));Step();}
    }
    // Raise the straight arm to 45 degrees, pause, bring it down to level, pause.
    void Call(float down=.35f,float to=0,float reach=.6f,float from=45){in.grip[0]=1;MoveHand(0,Arm(from,reach),.4f);Hold(.15f);Sweep(from,to,down,reach);Hold(.2f);}
};

static void HammerTests() {
    {   // Under the motions' gate (both grips, owner 2026-11-04): the old fist alone (one grip) no longer pumps.
        Hammer one;one.gated=true;one.grips[0]=1;one.Pump();Check(one.casts==0,"hammer: gated, one grip (the old fist) no longer pumps");
        Hammer both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Pump();Check(both.casts==1,"hammer: gated, both grips held: the pump casts");
        Hammer left;left.gated=true;left.grips[0]=left.grips[1]=1;left.Pump(1);Check(left.casts==1,"hammer: gated, either fist pumps");
    }
    for(float heading:{-2.f,0.f,2.f}) {
        Hammer s(heading);s.Pump();Check(s.casts==1,"hammer: a right fist punched up past the head casts once at any heading");
        Hammer l(heading);l.Pump(1);Check(l.casts==1,"hammer: the left fist casts too");
    }
    {Hammer s;s.Pump(0,.2f,-.3f,.2f);s.Hold(1);Check(s.casts==1,"hammer: a fist held high after the pump never repeats");
     s.Lower();s.Pump();Check(s.casts==2,"hammer: bringing the fist back down loads the next pump");}
    {Hammer s;s.Pump();s.Pump();Check(s.casts==2,"hammer: pump, down, pump: one hammer each");}
    {Hammer s;s.in.grip[0]=0;const math::Vec r{.2f,-.3f,-.2f},l{-.25f,-.55f,-.2f};s.Move(r,l,.25f);s.Hold(.1f);
     s.Move({.2f,.2f,-.25f},l,.2f);Check(s.casts==0,"hammer: an open hand raised fast is not a pump");}
    {Hammer s;s.in.active=false;s.Pump();Check(s.casts==0,"hammer: a busy hand (the gate closed: trigger squeezed, bottle) never pumps");}
    {Hammer s;s.Pump(0,.8f);Check(s.casts==0,"hammer: a slow raise is not a punch");}
    {Hammer s;s.Pump(0,.12f,-.3f,-.02f);Check(s.casts==0,"hammer: a punch that stops below the head does not cast");
     Check(s.drops==1&&Has(s.lastDrop,"stalled"),"hammer: the stalled punch logs once");
     Check(s.lastLine.rfind("Frame hammer: right fist pump dropped: punch stalled below the head (rose ",0)==0,"hammer: its line as before");}
    {Hammer s;s.in.grip[0]=1;s.Move({.2f,.3f,-.25f},{-.25f,-.55f,-.2f},1.f);s.Hold(.2f);
     s.Move({.2f,-.05f,-.25f},{-.25f,-.55f,-.2f},.2f);s.Move({.2f,.3f,-.25f},{-.25f,-.55f,-.2f},.12f);
     Check(s.casts==0,"hammer: a fist bobbing just under the head without coming down never loads");}
    {Hammer s;s.in.grip[0]=1;const math::Vec l{-.25f,-.55f,-.2f};s.Move({.2f,-.3f,-.2f},l,.25f);s.Hold(.1f);
     s.Move({.2f,.2f,-.9f},l,.2f);Check(s.casts==0,"hammer: a forward punch (strike) is not a pump");}
    {Hammer s;s.in.grip[0]=1;const math::Vec l{-.25f,-.55f,-.2f};s.Move({.2f,-.3f,-.2f},l,.25f);s.Hold(.1f);
     s.Move({.2f,-.05f,-.25f},l,.1f);s.in.grip[0]=0;s.Move({.2f,.2f,-.25f},l,.1f);
     Check(s.casts==0&&Has(s.lastDrop,"opened"),"hammer: opening the fist mid-punch abandons it");
     Check(s.lastLine.rfind("Frame hammer: right fist pump dropped: fist opened before clearing the head",0)==0,"hammer: the opened fist's line as before");}
    {Hammer s;s.Pump();s.in.session=8;s.Hold(.1f);s.Lower();s.Pump();Check(s.casts==2,"hammer: a new session starts clean");}
    {Hammer s;s.in.grip[0]=s.in.grip[1]=1;
     s.Move({.2f,-.3f,-.2f},{-.2f,-.3f,-.2f},.25f);s.Hold(.1f);
     s.Move({.2f,.2f,-.25f},{-.2f,.2f,-.25f},.2f);
     Check(s.casts==1,"hammer: both fists together cast one hammer");}
    {   // one fist's cast leaves the other's pump going (Engine::KeepAfterCast): each fist its own hammer
        Hammer s;s.in.grip[0]=s.in.grip[1]=1;
        s.Move({.2f,-.3f,-.2f},{-.2f,-.3f,-.2f},.25f);s.Hold(.1f);
        for(int i=1;i<=16;++i) {
            const float r=std::min(i,10)/10.f,l=std::clamp(i-3,0,10)/10.f;
            s.Place(0,{.2f,-.3f+.5f*r,-.2f-.05f*r});s.Place(1,{-.2f,-.3f+.5f*l,-.2f-.05f*l});s.Step();
        }
        s.Hold(.1f);
        Check(s.casts==2,"hammer: two fists a moment apart, two hammers (the second rising as the first casts)");
    }
    // Real arm motion (minimum-jerk, 90 Hz): travel counts from where the punch began.
    for(float heading:{-2.f,0.f,2.f}) {
        Hammer s(heading);s.Natural();s.Pump(0,.3f,-.3f,.25f);Check(s.casts==1,"hammer: a natural pump from the shoulder casts");
        Hammer l(heading);l.Natural();l.Pump(1,.35f,-.55f,.2f);Check(l.casts==1,"hammer: a natural pump from the hip casts (left)");
    }
    {Hammer s;s.Natural();for(int i=0;i<5;++i){s.Pump(0,.3f,-.3f,.25f);}Check(s.casts==5,"hammer: five natural pumps, five hammers");}
    {Hammer s;s.Natural();s.Pump(0,.25f,-.3f,-.05f);Check(s.casts==0,"hammer: a natural punch that stops under the head does not cast");}
    {Hammer s;s.Natural();s.Pump(0,.9f,-.3f,.25f);Check(s.casts==0,"hammer: a natural slow raise is not a punch");}
    {Hammer s;s.Natural();s.in.grip[0]=1;const math::Vec l{-.25f,-.55f,-.2f};s.Move({.2f,-.3f,-.2f},l,.3f);s.Hold(.1f);
     s.natural=false;s.Move({.2f,-.12f,-.2f},l,1.f);s.natural=true;s.Move({.2f,.1f,-.25f},l,.12f);s.Hold(.1f);
     Check(s.casts==0,"hammer: a slow drift up then a short flick is not a pump");}
    // The hum: none with the fist resting below the head (loaded), then warmer as a punch rises.
    {Hammer s;s.in.grip[0]=s.in.grip[1]=1;s.Move({.2f,-.3f,-.2f},{-.2f,-.3f,-.2f},.25f);s.Hold(2.f);
     Check(s.first<0&&s.casts==0,"hammer: fists held loaded at the shoulders do not hum");}
    {Hammer s;s.Natural();s.Pump(0,.3f,-.3f,.25f);Check(s.casts==1&&s.Rose(),"hammer: the hum rises as the fist punches up, before the hammer");}
}
static void ChargeTests() {
    {   // Under the motions' gate (both grips, owner 2026-11-04): the weapon hand's grip alone no longer charges.
        Charge one;one.gated=true;one.grips[0]=1;one.Punch();Check(one.casts==0,"charge: gated, the weapon hand's grip alone no longer charges");
        Charge both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Punch();Check(both.casts==1,"charge: gated, both grips held: the punch charges");
    }
    for(float heading:{-2.f,0.f,2.f}){Charge s(heading);s.Punch();Check(s.casts==1,"charge: a fist punched straight out ahead casts Charge at any heading");}
    {Charge s;s.Punch();s.Hold(1);Check(s.casts==1,"charge: an arm held out never repeats");
     s.Punch();Check(s.casts==2,"charge: drawing the fist back in loads the next punch");}
    {Charge s;s.MoveHand(0,{.2f,-.25f,-.15f},.25f);s.Hold(.1f);s.MoveHand(0,{.15f,-.2f,-.62f},.15f);Check(s.casts==0,"charge: an open hand punching is a swing, not Charge");}
    {Charge s;s.in.active=false;s.Punch();Check(s.casts==0,"charge: a busy hand (the gate closed: trigger squeezed, bottle) never charges");}
    {Charge s;s.Punch(.6f);Check(s.casts==0,"charge: a slow reach is not a punch");}
    {Charge s;s.Punch(.06f,{.2f,-.25f,-.15f},{.15f,-.2f,-.34f});Check(s.casts==0,"charge: a half punch does not cast");
     Check(s.drops==1&&Has(s.lastDrop,"stalled"),"charge: the half punch logs once");
     Check(s.lastLine.rfind("Frame charge: punch dropped: punch stalled before the arm was out (moved ",0)==0&&Has(s.lastLine,"extended at 45 cm)"),
           "charge: its line as before (extended at 45 cm uncalibrated)");}
    {Charge s;s.Punch(.15f,{.2f,-.25f,-.15f},{.2f,.35f,-.35f});Check(s.casts==0,"charge: punching up to the sky is the hammer, not Charge");}
    {Charge s;s.Punch(.15f,{.2f,.0f,-.15f},{.2f,-.7f,-.45f});Check(s.casts==0,"charge: a downward chop is not a punch");}
    {Charge s;s.Punch(.15f,{.35f,-.25f,-.2f},{-.35f,-.25f,-.5f});Check(s.casts==0,"charge: a horizontal slash across the body is not a punch");}
    {Charge s;s.in.grip[0]=1;s.MoveHand(0,{.15f,-.2f,-.62f},.8f);s.Hold(.2f);s.MoveHand(0,{.15f,-.2f,-.4f},.2f);s.MoveHand(0,{.15f,-.2f,-.65f},.1f);
     Check(s.casts==0,"charge: jabbing from an arm already out never loads");}
    {Charge s;s.in.grip[0]=1;s.MoveHand(0,{.2f,-.25f,-.15f},.25f);s.Hold(.1f);s.MoveHand(0,{.17f,-.22f,-.35f},.06f);s.in.grip[0]=0;s.MoveHand(0,{.15f,-.2f,-.62f},.08f);
     Check(s.casts==0&&Has(s.lastDrop,"opened"),"charge: opening the fist mid-punch abandons it");}
    {Charge tall(0,2.1f);tall.Punch(.15f,{.2f,-.25f,-.15f},{.15f,-.2f,-.55f});Check(tall.casts==0,"charge: a tall player's half extension is not enough");
     tall.Punch(.15f,{.2f,-.25f,-.15f},{.15f,-.2f,-.66f});Check(tall.casts==1,"charge: a tall player's full extension casts");
     Charge small(0,1.3f);small.Punch(.15f,{.2f,-.25f,-.1f},{.15f,-.2f,-.42f});Check(small.casts==1,"charge: a small player's full extension casts");}
    {Charge s;s.Punch();s.in.session=8;s.Hold(.1f);s.Punch();Check(s.casts==2,"charge: a new session starts clean");}
    // Real arm motion (minimum-jerk, 90 Hz): travel counts from the drawn-back point.
    for(float heading:{-2.f,0.f,2.f}){Charge s(heading);s.Natural();s.Punch(.25f);Check(s.casts==1,"charge: a natural punch casts Charge");}
    {Charge s;s.Natural();s.Punch(.25f,{.2f,-.25f,-.28f},{.15f,-.2f,-.62f});
     Check(s.casts==1,"charge: a natural punch from the edge of the drawn-back zone casts (it leaves the zone before full speed)");}
    {Charge s;s.Natural();for(int i=0;i<5;++i)s.Punch(.25f);Check(s.casts==5,"charge: five natural punches, five charges");}
    {Charge s;s.Natural();s.Punch(.25f,{.2f,-.25f,-.15f},{.15f,-.2f,-.38f});Check(s.casts==0,"charge: a natural half punch does not cast");}
    {Charge s;s.Natural();s.Punch(.9f);Check(s.casts==0,"charge: a natural slow reach is not a punch");}
    {Charge tall(0,2.1f);tall.Natural();tall.Punch(.28f,{.2f,-.25f,-.15f},{.15f,-.2f,-.66f});Check(tall.casts==1,"charge: a tall player's natural full punch casts");}
    // The hum: none with the fist drawn back at rest, then warmer as the punch goes out.
    {Charge s;s.in.grip[0]=s.in.grip[1]=1;s.MoveHand(0,{.2f,-.25f,-.15f},.25f);s.Hold(2.f);Check(s.first<0,"charge: a fist resting drawn back does not hum");}
    {Charge s;s.Natural();s.Punch(.25f);Check(s.casts==1&&s.Rose(),"charge: the hum rises as the fist punches out, before the charge");}
    {   // the watch stopped mid-punch (a trigger squeezed): its line, as its engine's
        Charge s;s.Natural();s.in.grip[0]=1;s.MoveHand(0,{.2f,-.25f,-.15f},.25f);s.Hold(.1f);s.MoveHand(0,{.17f,-.22f,-.30f},.06f);
        s.in.active=false;s.Step();
        Check(s.casts==0&&s.lastLine.rfind("Frame charge: punch dropped: hand busy, untracked or input inactive (moved ",0)==0,"charge: stopped mid-punch, logged");
    }
}
static void CrossTests() {
    {   // Under the motions' gate (both grips, owner 2026-11-04): a sign with no grip, as before, no longer casts.
        Cross none;none.gated=true;none.Sign();Check(none.casts==0,"holy shield: gated, a sign with no grip no longer casts");
        Cross one;one.gated=true;one.grips[1]=1;one.Sign();Check(one.casts==0,"holy shield: gated, one grip is not enough");
        Cross both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Sign();Check(both.casts==1,"holy shield: gated, both grips held: the sign casts");
    }
    for(float heading:{-2.f,0.f,2.f}){Cross s(heading);s.Sign();Check(s.casts==1,"holy shield: forehead, chest, left, right casts at any heading");}
    {Cross s;s.Sign(0,true);Check(s.casts==1,"holy shield: right shoulder before left (Orthodox) casts too");}
    {Cross s;s.Sign(1);Check(s.casts==1,"holy shield: the left hand can sign");}
    {Cross s;s.Sign(0,false,.35f,0);Check(s.casts==1,"holy shield: a flowing sign with no hold at each point still casts (the hand slows to turn)");}
    {Cross s;s.Sign();s.Sign();Check(s.casts==1,"holy shield: a second sign inside the cooldown is ignored");s.Hold(1.6f);s.Sign();Check(s.casts==2,"holy shield: after the cooldown it casts again");}
    {Cross s;for(const auto& p:{Forehead,Chest,LeftShoulder}){s.MoveHand(0,p,.35f);s.Hold(.08f);}s.MoveHand(0,Rest,.4f);s.Hold(1.2f);
     Check(s.casts==0&&Has(s.lastDrop,"too long"),"holy shield: three touches then nothing: dropped, logged");
     Check(s.lastLine=="Frame holy shield: right hand sign dropped after 3 of 4 touches: too long between touches","holy shield: its line as before");}
    {Cross s;s.in.active=false;s.Sign();Check(s.casts==0,"holy shield: a busy hand (the gate closed: trigger squeezed, bottle) never signs");}
    {Cross s;for(const auto& p:{Chest,Forehead,LeftShoulder,RightShoulder}){s.MoveHand(0,p,.35f);s.Hold(.08f);}Check(s.casts==0,"holy shield: chest first is not the sign");}
    {Cross s;for(const auto& p:{Forehead,LeftShoulder,Chest,RightShoulder}){s.MoveHand(0,p,.35f);s.Hold(.08f);}Check(s.casts==0,"holy shield: forehead then a shoulder is not the sign");}
    {Cross s;for(const auto& p:{Forehead,Chest,LeftShoulder,LeftShoulder}){s.MoveHand(0,p,.35f);s.Hold(.08f);}Check(s.casts==0,"holy shield: the same shoulder twice is not the sign");}
    {Cross s;for(int i=0;i<3;++i){s.MoveHand(0,{-.2f,.05f,-.15f},.12f);s.MoveHand(0,{.2f,.05f,-.15f},.12f);}Check(s.casts==0,"holy shield: a face wipe never signs");}
    {Cross s;for(const auto& p:{Forehead,Chest,LeftShoulder,RightShoulder})s.MoveHand(0,p,.07f);Check(s.casts==0,"holy shield: a fast swing through the same points is not a touch");}
    {Cross s;s.MoveHand(0,{.2f,.3f,-.3f},.3f);s.MoveHand(0,{-.2f,-.7f,-.5f},.15f);s.MoveHand(0,{.3f,-.2f,-.6f},.15f);s.MoveHand(0,{-.3f,-.3f,-.6f},.15f);
     Check(s.casts==0,"holy shield: overhead chop then slashes in front never sign");}
    {Cross s;s.MoveHand(0,Forehead,.35f);s.Hold(.08f);s.MoveHand(0,Chest,.35f);s.Hold(.08f);s.in.session=4;s.MoveHand(0,LeftShoulder,.35f);s.Hold(.08f);s.MoveHand(0,RightShoulder,.35f);
     Check(s.casts==0,"holy shield: a new session forgets a sign in progress");}
    // The hum: from the forehead's touch, warmer at each touch.
    {Cross s;s.Sign();Check(s.casts==1&&s.Rose()&&s.peak>=.74f,"holy shield: the hum begins at the forehead and rises with each touch, before the shield");}
    {Cross s;s.Hold(2.f);Check(s.first<0,"holy shield: hands at rest do not hum");}
}
static void HeavensTests() {
    {   // Under the motions' gate (both grips, owner 2026-11-04): the weapon hand's fist alone no longer calls.
        Heavens one;one.gated=true;one.grips[0]=1;one.Call();Check(one.casts==0,"heavens: gated, the weapon hand's grip alone no longer calls");
        Heavens both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Call();Check(both.casts==1,"heavens: gated, both grips held: the call casts");
        Heavens open;open.gated=true;open.grips[0]=open.grips[1]=1;open.in.grip[0]=1;open.MoveHand(0,Arm(45,.6f),.4f);open.Hold(.15f);open.Sweep(45,-40,.3f);
        open.grips[0]=open.grips[1]=0;open.in.grip[0]=open.in.grip[1]=0;open.Step();open.Hold(.2f);
        Check(open.casts==1,"heavens: gated, opening both fists at the end of a fast sweep still casts (the frame the grips open)");
    }
    for(float h:{-2.f,0.f,2.f}){Heavens s(h);s.Call();Check(s.casts==1,"heavens: straight arm from 45 degrees down to the shoulder casts, at any heading");}
    {Heavens s;s.Call();Check(s.from>40&&s.from<50&&std::fabs(s.to)<15,"heavens: the cast carries its call's angles, from about 45 to about level");}
    {Heavens s;s.Call(.2f);Check(s.casts==1,"heavens: a quick call casts");}
    {Heavens s;s.Call(.6f);Check(s.casts==1,"heavens: a deliberate call casts");}
    {Heavens s;s.Call(1.2f);Check(s.casts==0&&Has(s.lastDrop,"slow"),"heavens: a slow lowering is not a call");}
    {Heavens s;s.Call();s.Hold(1);Check(s.casts==1,"heavens: an arm held at the shoulder never repeats");s.Call();Check(s.casts==2,"heavens: raising it again loads the next");}
    {Heavens s;s.Call(.35f,-75);Check(s.casts==0&&Has(s.lastDrop,"chop"),"heavens: a straight arm carried down past -65 degrees is a chop");
     Check(s.lastLine.rfind("Frame heavens: call dropped: carried on below the shoulder (a chop) (from ",0)==0,"heavens: its line as before");}
    {Heavens s;s.Call(.35f,-8);Check(s.casts==1,"heavens: stopping a little below level still counts");}
    {Heavens s;s.Call(.35f,-28);Check(s.casts==1,"heavens: the headset test's overshoot (stopping near -25 degrees) counts");}
    {Heavens s;s.Call(.25f,-45);Check(s.casts==1,"heavens: the owner's natural stop (-36..-47 degrees, 2026-11-01) counts");}
    {Heavens s;s.Call(.3f,-53);Check(s.casts==1,"heavens: a stop a little past -50 still counts");}
    // The fist opening ends the call: after a fast sweep far enough down it casts, before that it drops.
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-40,.3f);s.in.grip[0]=0;s.Step();s.Hold(.2f);
     Check(s.casts==1,"heavens: opening the fist at -40 after a fast sweep casts (the owner's 29 dropped calls)");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-5,.3f);s.in.grip[0]=0;s.Step();s.Hold(.2f);
     Check(s.casts==1,"heavens: opening the fist at level casts once, not twice");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(40,.6f),.4f);s.Hold(.15f);s.Sweep(40,22,.1f);s.in.grip[0]=0;s.Step();
     Check(s.casts==0&&s.lastDrop=="fist opened","heavens: opening the fist early in the sweep (18 degrees down, under 22) drops the call");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-30,2.5f);s.in.grip[0]=0;s.Step();
     Check(s.casts==0,"heavens: opening the fist after a slow lowering does not cast");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-80,.35f);s.in.grip[0]=0;s.Step();
     Check(s.casts==0,"heavens: opening the fist after a chop past -65 does not cast");}
    {Heavens s;s.Call(.35f,30);Check(s.casts==0,"heavens: stopping above the shoulder does not cast");}
    {Heavens s;s.in.grip[0]=0;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,0,.35f);s.Hold(.2f);Check(s.casts==0,"heavens: an open hand is not a fist: no cast");}
    {Heavens s;s.in.active=false;s.Call();Check(s.casts==0,"heavens: a busy hand (the gate closed) never calls");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.MoveHand(0,{.2f,-.45f,-.25f},.3f);s.Hold(.2f);
     Check(s.casts==0,"heavens: bending the elbow and pulling the fist in (a chop's follow-through) does not cast");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,{.2f,-.3f,-.2f},.3f);s.Hold(.1f);s.MoveHand(0,{.2f,.2f,-.25f},.2f);s.Hold(.2f);s.MoveHand(0,{.2f,-.3f,-.2f},.3f);s.Hold(.2f);
     Check(s.casts==0,"heavens: the hammer pump (fist straight up beside the head) and back down never calls");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,{.2f,-.25f,-.15f},.3f);s.Hold(.1f);s.MoveHand(0,{.15f,-.2f,-.62f},.15f);s.Hold(.3f);
     Check(s.casts==0,"heavens: the Charge punch never calls");}
    {Heavens s;s.Call(.35f,0,.38f);Check(s.casts==0,"heavens: a half-bent arm at 45 degrees never loads");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f,80),.4f);s.Hold(.15f);s.Sweep(45,0,.35f,.6f,80);s.Hold(.2f);Check(s.casts==0,"heavens: an arm out to the side does not call");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(80,.6f),.4f);s.Hold(.15f);s.Sweep(80,0,.4f);s.Hold(.2f);Check(s.casts==1,"heavens: rev3: from higher up, the straight arm passing the raised pose loads and calls");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(-10,.6f),.3f);s.Sweep(-10,45,.25f);s.Sweep(45,-5,.3f);s.Hold(.2f);
     Check(s.casts==1,"heavens: rev3: a fluid up-and-down with no rest at the top calls");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(-10,.6f),.3f);s.Sweep(-10,40,.2f);s.Sweep(40,-15,.22f);s.Hold(.15f);
     Check(s.casts==1,"heavens: rev3: a quick flick up and down calls");}
    {Heavens tall(0,2.1f);tall.Call(.35f,0,.78f);Check(tall.casts==1,"heavens: a tall player's straight arm casts");
     Heavens tallBent(0,2.1f);tallBent.Call(.35f,0,.55f);Check(tallBent.casts==0,"heavens: a tall player's half-bent arm does not");
     Heavens small(0,1.3f);small.Call(.35f,0,.47f);Check(small.casts==1,"heavens: a small player's straight arm casts");}
    {Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,20,.15f);s.in.session=6;s.Sweep(20,0,.2f);s.Hold(.2f);
     Check(s.casts==0,"heavens: a new session forgets a call in progress");}
    // The hum: the raised straight arm begins it, warmer as it comes down to the shoulder.
    {Heavens s;s.Call();Check(s.casts==1&&s.Rose(),"heavens: the hum begins with the arm raised and rises on the way down, before the call");}
    {Heavens s;s.in.grip[0]=s.in.grip[1]=1;s.Hold(2.f);Check(s.first<0,"heavens: a fist at rest does not hum");}
    {   // the watch stopped mid-sweep (a trigger squeezed): its line, as its engine's
        Heavens s;s.in.grip[0]=1;s.MoveHand(0,Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,20,.15f);s.in.active=false;s.Step();
        Check(s.casts==0&&s.lastLine.rfind("Frame heavens: call dropped: hand busy, untracked or input inactive (from 4",0)==0,"heavens: stopped mid-sweep, logged");
    }
}
static void Run() {
    HammerTests();ChargeTests();CrossTests();HeavensTests();
    {   // The page, the order and the engine's policies for them
        const auto page=spellmotion::PageOf(3);const auto order=spellmotion::OrderOf(3);
        Check(page.count==4&&order.count==4,"the Paladin's page: four cards, each watched");
        const Motion motions[]={Motion::Charge,Motion::BlessedHammer,Motion::HolyShield,Motion::FistOfHeavens};
        for(int i=0;i<4;++i) {
            Check(spellmotion::For(page.skills[i])==motions[i]&&std::count(order.motions,order.motions+order.count,motions[i])==1,"each card's motion, watched once");
            Check(spellmotion::Bit(3,page.skills[i])==i,"its bit is its card's, as saved since 2026-11-04 ([motions] paladin_off)");
            Check(spellmotion::WatchesOpening(motions[i])&&spellmotion::Rapid(motions[i])==(motions[i]==Motion::BlessedHammer),
                  "watched the frame the grips open; only Blessed Hammer's pumps skip the wait");
        }
        Check(order.motions[order.count-1]==Motion::BlessedHammer,"Blessed Hammer watched last (its pumps leave the others be)");
        Check(spellmotion::ClassRest(Motion::HolyShield)==1.5f&&spellmotion::ClassRest(Motion::FistOfHeavens)==.4f&&
              spellmotion::ClassRest(Motion::Charge)==0&&spellmotion::ClassRest(Motion::BlessedHammer)==0&&spellmotion::ClassRest(Motion::FlingOut)<0,
              "their engines' rests: Holy Shield 1.5 s, Fist of the Heavens 0.4 s, none for Charge and the hammer; others the tuning's");
        Check(std::string(spellmotion::CueOf(107))=="draw the right fist in, then punch it straight out at a monster"&&
              std::string(spellmotion::CueOf(112))=="punch a fist from below your head up past it, one hammer a punch"&&
              std::string(spellmotion::CueOf(117))=="make the sign of the cross: forehead, chest, one shoulder, the other"&&
              std::string(spellmotion::CueOf(121))=="raise the right arm straight, then bring it down level with the shoulder","the cards' cues as before");
        Check(!spellmotion::WatchesOpening(Motion::FlingOut)&&!spellmotion::Rapid(Motion::Shout),"the other classes': the grips' hold only, and the wait");
    }
}
// Everything above, right-handed as written, then played by a left-handed player (motion_left.h): the mirror
// image of every fixture, the left hand as the engine's hand 0, with the same expectations.
int main() {
    motionleft::Both(Run);
    std::puts("motions-paladin-test: ok (right-handed, and every fixture again left-handed as its mirror image)");
    return 0;
}
