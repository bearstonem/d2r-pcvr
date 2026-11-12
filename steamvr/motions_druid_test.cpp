#include "spell_motion.h"
#include "motion_left.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
using namespace framevr;
using spellmotion::Motion;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

// The Druid's motions (motions_druid.h) on spellmotion::Engine: the scenarios of their own engine's test
// (shapeshift-gesture-test until 2026-11-08), with both forms' watchers fed the same frames as the transport feeds
// them, and the warmer/colder hum rising before each cast. Body frame keyframes (yaw-local: +X right, +Y up, -Z
// forward) relative to the standing head; hands {right, left}. The controllers walk between keyframes at 50 Hz, so
// velocities come from the motion (no runtime velocity).
struct Sim {
    // The motions' gate (spell_motion.h, owner 2026-11-04: "Both grips, like Sorceress"): gated, in.active is its watch.
    bool gated=false;spellmotion::Gate gate;float grips[2]{};
    spellmotion::Engine wolf,bear;spellmotion::Input in;spellmotion::Output out[2];   // Werewolf's, Werebear's
    math::Quat yaw{0,0,0,1};math::Vec standing{0,1.7f,0};
    int casts=0;Motion last=Motion::None;
    int drops=0;std::string lastDrop,lastLine;
    float first[2]{-1,-1},peak[2]{};   // each form's first hum and its most, before the first cast
    explicit Sim(float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=7;in.epoch=1;in.active=true;in.free=true;in.tracked[0]=in.tracked[1]=true;
        Pose({0,0,0},0,{{.25f,-.4f,-.3f},{-.25f,-.4f,-.3f}});Step();}
    void Pose(math::Vec headOffset,float pitchDeg,std::initializer_list<math::Vec> hands) {
        in.head=standing+math::Rotate(yaw,headOffset);
        const float p=pitchDeg*3.14159265f/180;
        in.headRotation=math::Multiply(yaw,{-std::sin(p*.5f),0,0,std::cos(p*.5f)}); // positive pitch looks down
        int h=0;for(const auto& v:hands)in.hand[h++]=in.head+math::Rotate(yaw,v);
    }
    // Move from the current pose to the target over `seconds`.
    void Move(math::Vec headOffset,float pitchDeg,std::initializer_list<math::Vec> hands,float seconds) {
        const math::Vec head0=in.head,hand0[2]{in.hand[0],in.hand[1]};const auto rot0=in.headRotation;
        Pose(headOffset,pitchDeg,hands);const math::Vec head1=in.head,hand1[2]{in.hand[0],in.hand[1]};const auto rot1=in.headRotation;
        const int steps=std::max(1,int(seconds/.02f));
        for(int i=1;i<=steps;++i) {
            const float t=float(i)/steps;
            in.head=head0+(head1-head0)*t;in.headRotation=t<.5f?rot0:rot1;
            for(int h=0;h<2;++h)in.hand[h]=hand0[h]+(hand1[h]-hand0[h])*t;
            Step();
        }
    }
    void Hold(float seconds){for(int i=0;i<int(seconds/.02f);++i)Step();}
    void Step() {
        in.time+=.02;
        if(gated)in.active=gate.Step(grips,true).watch;
        spellmotion::Engine* engines[2]{&wolf,&bear};const Motion forms[2]{Motion::Werewolf,Motion::Werebear};
        for(int k=0;k<2;++k) {
            in.motion=forms[k];out[k]=motionleft::Seen(engines[k]->Step(motionleft::Played(in)));
            if(out[k].cast){++casts;last=forms[k];}
            if(out[k].abandoned) {
                ++drops;lastDrop=out[k].abandoned;
                char line[200];Check(spellmotion::DropLine(forms[k],out[k],line,sizeof line),"a performance given up has its line");lastLine=line;
            }
            if(!casts&&out[k].progress>=spellmotion::Begun){if(first[k]<0)first[k]=out[k].progress;peak[k]=std::max(peak[k],out[k].progress);}
        }
    }
    // A form's stance in progress: its watcher hums (Output::progress).
    Motion Stance()const{return out[0].progress>=spellmotion::Begun?Motion::Werewolf:out[1].progress>=spellmotion::Begun?Motion::Werebear:Motion::None;}
    bool Rose(int k)const{return first[k]>=spellmotion::Begun&&peak[k]>first[k]&&peak[k]<=1;}
    void Werewolf(float flingSeconds=.25f) {
        Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);   // cross arms: right hand on the left side
        Hold(.2f);
        Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f); // bow forward over them
        Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},flingSeconds); // wrench up, fling open
        Hold(.1f);
    }
    void Werebear(float slamSeconds=.2f) {
        Move({0,0,0},0,{{.15f,.25f,-.2f},{-.15f,.25f,-.2f}},.4f);       // hands above the head
        Hold(.35f);
        Move({0,-.1f,.1f},20,{{.15f,-.5f,-.3f},{-.15f,-.5f,-.3f}},slamSeconds); // slam down
        Hold(.1f);
    }
};
static void Run() {
    {   // Under the motions' gate (both grips, owner 2026-11-04): free hands, as before, no longer change form.
        Sim none;none.gated=true;none.Werewolf();Check(none.casts==0,"gated, free hands no longer shift");
        Sim wolf;wolf.gated=true;wolf.grips[0]=wolf.grips[1]=1;wolf.Werewolf();Check(wolf.casts==1&&wolf.last==Motion::Werewolf,"gated, both grips held: Werewolf");
        Sim bear;bear.gated=true;bear.grips[0]=bear.grips[1]=1;bear.Werebear();Check(bear.casts==1&&bear.last==Motion::Werebear,"gated, both grips held: Werebear");
    }
    for(float heading:{-2.f,0.f,2.f}) {
        Sim s(heading);s.Werewolf();
        Check(s.casts==1&&s.last==Motion::Werewolf,"crossed arms, bow and wrench open casts Werewolf at any heading");
        Sim b(heading);b.Werebear();
        Check(b.casts==1&&b.last==Motion::Werebear,"hands high then a slam casts Werebear at any heading");
    }
    {Sim s;s.Werewolf();s.Werewolf();Check(s.casts==1,"a second performance inside the cooldown is ignored");
     s.Hold(1.5f);s.Werewolf();Check(s.casts==2&&s.last==Motion::Werewolf,"after the cooldown the gesture casts again, which reverts the form");}
    {Sim s;s.Werewolf(1.2f);Check(s.casts==0,"a slow, lazy opening is not a wrench");}
    for(float hold:{0.f,.5f,1.2f}) {
        // Deep bow: the head drops 35 cm and comes forward past the folded
        // arms, which end level with or above it, then a pause before the wrench.
        Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
        s.Move({0,-.35f,.30f},70,{{-.12f,.03f,.10f},{.12f,.01f,.12f}},.5f);
        s.Hold(hold);
        Check(s.casts==0&&s.Stance()==Motion::Werewolf,"a deep bow keeps the wolf stance while the arms stay folded");
        s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
        Check(s.casts==1&&s.last==Motion::Werewolf,"a deep bow, held a moment, then the wrench casts Werewolf");
    }
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
     s.Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);
     s.Move({0,-.2f,.18f},45,{{.30f,-.25f,-.22f},{-.30f,-.27f,-.20f}},.3f);s.Hold(.8f);
     Check(s.casts==0&&s.Stance()==Motion::None,"uncrossing while still bowed starts the wrench window, which then expires");}
    {// Bow, then open the arms too slowly: the performance is dropped with a reason.
     Sim r;bool reason=false;
     r.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);r.Hold(.2f);
     r.Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);
     const math::Vec h0[2]{r.in.hand[0],r.in.hand[1]};
     for(int i=1;i<=60;++i){
         r.in.time+=.02;
         for(int k=0;k<2;++k)r.in.hand[k]=h0[k]+math::Rotate(r.yaw,{k?-.004f*i:.004f*i,.004f*i,0});
         r.in.motion=Motion::Werewolf;r.out[0]=motionleft::Seen(r.wolf.Step(motionleft::Played(r.in)));
         if(r.out[0].abandoned) {
             reason=true;Check(std::string(spellmotion::druid::Name(int(r.out[0].abandonedFigures[0])))=="fling","the drop names the fling stage");
             char line[200];Check(spellmotion::DropLine(Motion::Werewolf,r.out[0],line,sizeof line)&&
                 std::string(line).rfind("Frame shapeshift: dropped at fling: ",0)==0,"its line as before");
         }
     }
     Check(reason&&r.casts==0,"an abandoned performance reports why");}
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
     s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
     Check(s.casts==0,"flinging open without bowing first does not cast");}
    {Sim s;s.Move({0,0,0},0,{{.12f,-.25f,-.22f},{-.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
     s.Move({0,-.2f,.18f},45,{{.12f,-.25f,-.22f},{-.12f,-.27f,-.20f}},.4f);
     s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
     Check(s.casts==0,"uncrossed hands at the chest never arm the wolf");}
    {Sim s;s.Move({0,-.2f,.18f},45,{{.2f,-.5f,-.3f},{-.2f,-.5f,-.3f}},.4f);s.Move({0,0,0},0,{{.25f,-.4f,-.3f},{-.25f,-.4f,-.3f}},.2f);
     Check(s.casts==0,"stooping for loot and standing back up does not cast");}
    {Sim s;s.Move({0,0,0},0,{{.15f,.25f,-.2f},{-.15f,.25f,-.2f}},.4f);s.Hold(.35f);
     s.Move({0,0,0},0,{{.15f,-.2f,-.3f},{-.15f,-.2f,-.3f}},.6f);s.Hold(.1f);
     Check(s.casts==0,"lowering raised hands gently is not a slam");
     s.Hold(.5f);Check(s.lastLine=="Frame shapeshift: dropped at slam: slam too slow (both hands)","the gentle lowering logs its slam's drop, as before");}
    {Sim s;s.Move({0,0,0},0,{{.15f,.25f,-.2f},{-.15f,.25f,-.2f}},.4f);
     s.Move({0,-.1f,.1f},20,{{.15f,-.5f,-.3f},{-.15f,-.5f,-.3f}},.2f);s.Hold(.1f);
     Check(s.casts==0,"a slam without first holding the hands high (an overhead swing) does not cast");}
    {Sim s;s.in.active=false;s.Werewolf();Check(s.casts==0,"a busy hand (trigger, bottle, bow: the gate closed) blocks the gesture");}
    {Sim s;s.in.tracked[0]=false;s.Werebear();Check(s.casts==0,"an untracked controller cannot perform");}
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);Check(s.Stance()==Motion::Werewolf,"crossed arms report the wolf stance");
     s.in.epoch=2;s.Step();Check(s.Stance()==Motion::None,"an epoch change drops the stance");
     s.Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
     Check(s.casts==1,"the stance re-arms from the still-crossed arms and the performance completes");}
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);s.in.time+=1;s.Step();
     Check(s.Stance()==Motion::None,"a frame gap resets the stance");
     Check(s.lastLine=="Frame shapeshift: dropped at crossed: frame gap","the gap logs the stage it ended, as before");}
    {Sim s;s.in.head.x=NAN;s.Step();Check(s.Stance()==Motion::None&&!s.out[0].cast&&!s.out[1].cast,"non-finite poses are ignored");}
    // Each form's watcher runs the whole performance: one form's stance keeps the other from starting, and each logs
    // its own stages only.
    {Sim s;s.Werewolf();Check(s.casts==1&&s.last==Motion::Werewolf&&!s.out[1].cast&&s.first[1]<0,"the wolf's performance never warms or casts the bear");}
    {Sim s;s.Werebear();Check(s.casts==1&&s.last==Motion::Werebear&&s.first[0]<0,"the bear's never warms the wolf");}
    // The hum: the arms crossed begin the wolf's, warmer as it bows and flings; the hands overhead the bear's, warmer
    // as they are held there and brought down.
    {Sim s;s.Werewolf();Check(s.casts==1&&s.Rose(0),"Werewolf: the hum rises from the crossed arms to the fling, before the cast");}
    {Sim s;s.Werebear();Check(s.casts==1&&s.Rose(1),"Werebear: the hum rises from the hands overhead to the slam, before the cast");}
    {Sim s;s.Hold(2.f);Check(s.first[0]<0&&s.first[1]<0,"hands at rest do not hum");}
    {   // The page, the order and the engine's policies for them
        const auto page=spellmotion::PageOf(5);const auto order=spellmotion::OrderOf(5);
        Check(page.count==2&&order.count==2&&spellmotion::For(page.skills[0])==Motion::Werewolf&&spellmotion::For(page.skills[1])==Motion::Werebear&&
              order.motions[0]==Motion::Werewolf&&order.motions[1]==Motion::Werebear,"the Druid's page: Werewolf, then Werebear, each watched");
        Check(spellmotion::Bit(5,223)==0&&spellmotion::Bit(5,228)==1,"their bits are their cards', as saved since 2026-11-04 ([motions] druid_off)");
        for(const auto m:{Motion::Werewolf,Motion::Werebear})
            Check(spellmotion::ClassRest(m)==1.5f&&spellmotion::WatchesOpening(m)&&!spellmotion::Rapid(m),"either form: its engine's 1.5 s rest, watched the frame the grips open");
        Check(std::string(spellmotion::CueOf(223))=="cross the arms on the chest, bow, then straighten up flinging them out"&&
              std::string(spellmotion::CueOf(228))=="raise both hands overhead, hold, then slam them down together","the cards' cues as before");
    }
}
// Everything above, right-handed as written, then played by a left-handed player (motion_left.h): the mirror
// image of every fixture, the left hand as the engine's hand 0, with the same expectations.
int main() {
    motionleft::Both(Run);
    std::puts("motions-druid-test: ok (right-handed, and every fixture again left-handed as its mirror image)");
    return 0;
}
