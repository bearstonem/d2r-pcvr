#include "shapeshift_form.h"
#include "body_tracking.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace framevr;
using namespace framevr::shapeshift;
static bool Near(float a,float b,float e=.01f){return std::fabs(a-b)<=e;}
int main() {
    {   // The forms' models (F10 capture 2026-10-21), and nothing that merely starts the same.
        assert(FromModel("data/hd/character/enemy/wolf/skeleton/wolf.skeleton")==Wolf);
        assert(FromModel("data/hd/character/enemy/bear/skeleton/bear.skeleton")==Bear);
        assert(FromModel("data/hd/character/player/druid/skeleton/druid.skeleton")==None);
        assert(FromModel("data/hd/character/enemy/wolfrider/skeleton/wolfrider.skeleton")==None);
        assert(FromModel("data/hd/character/enemy/bearshaman/skeleton/bearshaman.skeleton")==None);
        assert(FromModel("")==None);
    }
    {   // The player's states: 139 Werewolf, 140 Werebear; an unread status is no form.
        effects::Status s;s.Set(WerewolfState);
        assert(FromStates(s)==None);
        s.known=true;assert(FromStates(s)==Wolf);
        effects::Status b;b.known=true;b.Set(WerebearState);b.Set(32);assert(FromStates(b)==Bear);
        effects::Status none;none.known=true;none.Set(32);assert(FromStates(none)==None);
    }
    {   // Heights: the form's own eyes, the setting as a bounded fraction of them.
        assert(Near(TargetEye(Wolf,1),6.93f)&&Near(TargetEye(Bear,1),7.92f)&&TargetEye(None,1)==0);
        assert(Near(TargetEye(Wolf,1.02f),7.07f,.02f));      // the werewolf at the druid's height
        assert(Near(TargetEye(Wolf,9),10.40f)&&Near(TargetEye(Bear,0),3.96f));
        assert(Near(StandingEye(Wolf)/7.07f,.98f)&&Near(StandingEye(Bear)/7.07f,1.12f));
        // the druid drawn at 0.95 of world, a form at 1.0: her units are 0.95 of the form's
        assert(Near(PersonUnits(.95f,1.f),.95f)&&Near(PersonUnits(0,1),1)&&Near(PersonUnits(1,0),1)&&Near(PersonUnits(1,NAN),1));
        assert(Near(TargetEye(Wolf,1)*PersonUnits(.95f,1.f),.98f*7.07f*.95f));
    }
    {   // Graft, with the capture's idle frames: torso upright about the spine root,
        // shoulders where a person's are, scale so the eyes stand at the form's height.
        // Werewolf: cog 3.853,-0.787; shoulder joints' midpoint 5.363,0.757.
        const float wolfUp=UprightShoulders(3.853f,-.787f,5.363f,.757f);
        assert(Near(wolfUp,6.01f));
        const float wolfRig=RigEye(wolfUp);
        assert(Near(wolfRig,7.12f));
        assert(Near(Scale(TargetEye(Wolf,1),wolfRig),.973f));
        // Werebear: cog 4.037,-0.067; shoulder joints' midpoint 6.789,0.366.
        const float bearRig=RigEye(UprightShoulders(4.037f,-.067f,6.789f,.366f));
        assert(Near(bearRig,8.07f));
        assert(Near(Scale(7.69f,bearRig),.95f));
        // The rest poses (F10, 2026-10-21): werewolf rig eye 8.32, werebear 8.49.
        assert(Near(Scale(TargetEye(Wolf,1),8.32f),.833f)&&Near(Scale(TargetEye(Bear,1),8.49f),.933f));
        // The druid's own proportions give back the druid.
        assert(Near(RigEye(5.972f),7.07f)&&Near(Scale(7.07f,7.07f),1));
        assert(Near(ShoulderRatio+NeckAboveShoulders+EyeAboveNeck,1,.001f));
        // An already upright torso stays as it is.
        assert(Near(UprightShoulders(4,0,6,0),6));
        // Bad input never scales a body away.
        assert(Scale(0,7)==1&&Scale(5,0)==1&&Scale(NAN,7)==1&&Scale(100,1)==2&&Scale(1,100)==.3f);
        // every [shapeshift] height from 50 to 150 % is drawn as asked, inside the clamp
        for(float f=.5f;f<=1.5f;f+=.05f) {
            const float w=Scale(TargetEye(Wolf,f),wolfRig),b=Scale(TargetEye(Bear,f),bearRig);
            assert(Near(w*wolfRig,TargetEye(Wolf,f),1e-3f)&&Near(b*bearRig,TargetEye(Bear,f),1e-3f));
        }
    }
    {   // Arm fit (rest poses, F10 2026-10-21; Cal's calibration eye 1.635 m, span 1.525 m).
        // Werebear: shoulders 3.11 apart, arm 3.707, half palm 0.413, rig eye 8.49. Fitted from
        // its own shoulders the arms were cut to half (the clamp); from a person's, about 0.68.
        const float bearOwn=framevr::body::ArmScale(8.49f,1.635f,1.525f,3.11f,3.707f,.413f,.5f);
        const float bearFit=framevr::body::ArmScale(8.49f,1.635f,1.525f,ArmFitShoulders(3.11f,8.49f),3.707f,.413f,.5f);
        assert(bearOwn<.55f&&Near(bearFit,.68f,.02f));
        // Werewolf: shoulders 1.80 apart (narrower than a person's 2.02 at rig eye 8.32): unchanged.
        assert(Near(ArmFitShoulders(1.80f,8.32f),1.80f)&&Near(ArmFitShoulders(3.11f,8.49f),2.06f));
        assert(ArmFitShoulders(1.5f,0)==1.5f);
        // The druid (shoulders 1.715 at eyes 7.07) is a person already.
        assert(Near(ArmFitShoulders(1.715f,7.07f),1.715f,1e-3f));
    }
    {   // Eye ease: the first height is taken at once; a transform eases over 500 ms.
        EyeEase e;
        assert(Near(e.Step(7.07f,1000),7.07f));
        assert(Near(e.Step(5.20f,1010),7.07f));              // the werewolf appears: starts at the druid's
        assert(Near(e.Step(5.20f,1260),(7.07f+5.20f)/2));    // halfway at 250 ms (smoothstep)
        assert(Near(e.Step(5.20f,1510),5.20f)&&Near(e.Settled(),5.20f));
        const float before=e.Step(7.69f,1600);                // a new target mid-way continues from where it is
        assert(Near(before,5.20f));
        const float mid=e.Step(7.07f,1800);
        assert(mid>5.20f&&mid<7.69f&&Near(e.Settled(),7.07f));
        assert(Near(e.Step(7.07f,2400),7.07f));
        assert(Near(e.Step(5.65f,5000),5.65f));              // unseen for more than a second: at once
        assert(Near(e.Step(5.20f,5010),5.65f));              // a form eases in
        assert(Near(e.Step(7.07f,5100,false),7.07f)&&Near(e.Settled(),7.07f));   // a person's rig: at once
        assert(Near(e.Step(7.07f,5110),7.07f));
        assert(Near(e.Step(0,5120),7.07f)&&Near(e.Step(NAN,5130),7.07f));
    }
    {   // Leg hold: standing holds, walking lets the stride play, both blend in 150 ms.
        LegHold h;
        uint64_t t=1000;float x=100,z=200;
        assert(h.Step(t,x,z)==1);
        for(int i=0;i<30;++i){t+=10;assert(h.Step(t,x+.001f*i,z)==1);}   // sway-size drift: still standing
        float w=1;
        for(int i=0;i<30;++i){t+=10;x+=.04f;w=h.Step(t,x,z);}            // 4 units/s
        assert(w==0);
        for(int i=0;i<30;++i){t+=10;w=h.Step(t,x,z);}
        assert(w==1);
        {   // halfway through the blend after the window that sees the stop
            LegHold m;uint64_t u=0;float p=0;m.Step(u,p,0);
            for(int i=0;i<40;++i){u+=10;p+=.05f;m.Step(u,p,0);}
            float v=0;for(int i=0;i<13;++i){u+=10;v=m.Step(u,p,0);}
            assert(v>0&&v<1);
        }
        t+=5000;assert(h.Step(t,x+50,z)==1);                                // after a pause: standing again
        assert(h.Step(t+10,NAN,z)==1);
    }
    {   // The world at the druid's size in form: the werewolf's rig (eye 8.32, drawn at 6.36) hangs the
        // hands by the druid's units per metre, taken back into the rig's units by the draw scale.
        const float drawn=Scale(6.36f,8.32f)*8.32f,unit=7.07f/drawn;
        assert(Near(framevr::body::UnitsPerMetre(8.32f,1.635f,unit),framevr::body::UnitsPerMetre(7.07f,1.635f,1)/Scale(6.36f,8.32f)));
        const float bear=Scale(7.78f,8.49f)*8.49f;
        assert(Near(framevr::body::UnitsPerMetre(8.49f,1.635f,7.07f/bear),framevr::body::UnitsPerMetre(7.07f,1.635f,1)/Scale(7.78f,8.49f)));
    }
    {   // The swap: a rig the sample still names is gone once the game has not drawn it for ~3 frames.
        assert(!RigDrawn(1000,0)&&!RigGone(1000,0));                     // never drawn: neither
        assert(RigDrawn(1000,1000)&&RigDrawn(1000,1012)&&RigDrawn(1040,1000));   // this frame, a later stamp, 40 ms
        assert(!RigGone(1012,1000)&&!RigGone(1040,1000));                 // a frame or three apart: still drawn
        assert(RigGone(1041,1000)&&!RigDrawn(1041,1000));
        assert(RigJustGone(1041,1000)&&!RigJustGone(2600,1000));          // a swap, not a body gone long ago
        const uint64_t druid[3]={1000,0,0},wolf[3]={1000,1030,0},none[3]={0,0,0};
        assert(PosedRig(1050,druid)==0&&PosedRig(1050,wolf)==1&&PosedRig(1050,none)==-1);
        assert(PosedRig(1130,wolf)==1&&PosedRig(1131,wolf)==-1);           // nothing drawn for 100 ms: the sample answers
        const uint64_t back[3]={1200,1030,0};assert(PosedRig(1210,back)==0); // back in human form
    }
    std::puts("shapeshift form: models, states, heights, graft, eye ease, leg hold and rig swap passed");
    return 0;
}
