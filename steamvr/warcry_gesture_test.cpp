#include "warcry_gesture.h"
#include "weapon_skills.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>
using namespace framevr;
using math::Vec;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// 90 Hz steps. The head stands 1.6 m up facing -Z unless turned; the mouth is
// then at (0,1.5,-0.08) in its frame.
struct Sim {
    warcry::Shout shout;
    double time=1;
    Vec head{0,1.6f,0};math::Quat q{0,0,0,1};
    Vec hand[2]{{.25f,1.0f,-.2f},{-.25f,1.0f,-.2f}};
    bool eligible[2]{true,true};
    int ready=-1,cast=-1,casts=0;
    Vec Local(Vec v)const{return head+math::Rotate(q,v);}
    // Hold the hands still for seconds.
    void Hold(double seconds){for(int n=int(seconds*90+.5);n>0;--n)Tick();}
    // Move one hand to a head-local point at speed (m/s), one step at a time.
    void Move(int h,Vec local,float speed){
        const Vec to=Local(local);
        for(int guard=0;guard<2000;++guard){
            const Vec d=to-hand[h];const float l=std::sqrt(math::Dot(d,d));
            if(l<1e-4f)break;
            const float step=speed/90;
            hand[h]=l<=step?to:hand[h]+d*(step/l);
            Tick();
        }
    }
    void Tick(){
        time+=1.0/90;
        const auto s=shout.Update(time,head,q,hand,eligible);
        if(s.ready>=0)ready=s.ready;
        if(s.cast>=0){cast=s.cast;++casts;}
    }
};
// At the mouth: 3 cm below it, 5 cm in front.
constexpr Vec AtMouth{0,-.13f,-.13f};
int main(){
    Check(warcry::Gesture(130)&&warcry::Gesture(138)&&warcry::Gesture(146)&&warcry::Gesture(149)&&warcry::Gesture(154)&&warcry::Gesture(155),
          "Howl, Shout, Battle Cry, Battle Orders, War Cry and Battle Command are shouted");
    Check(warcry::Gesture(warcry::Taunt),"Taunt is shouted at the cast's own monster");
    Check(!warcry::Gesture(131)&&!warcry::Gesture(142)&&!warcry::Gesture(150),"the corpse warcries stay buttons");
    Check(!warcry::Gesture(126)&&!warcry::Gesture(132)&&!warcry::Gesture(-1),"Bash, Leap and nothing are not warcries");
    {   // the shout: rest at the mouth, then push straight out
        Sim s;s.Move(0,AtMouth,1.f);s.Hold(.25);
        Check(s.ready==0&&s.cast<0,"a hand held at the mouth readies the shout, nothing cast yet");
        s.Move(0,{0,-.15f,-.6f},2.5f);
        Check(s.cast==0&&s.casts==1,"pushed out forward, the right hand shouts once");
        s.Hold(.5);Check(s.casts==1,"held out, nothing more");
    }
    {   // the left hand, the head turned 90 degrees: forward follows the head
        Sim s;s.q=math::Yaw(1.5707963f);s.Move(1,AtMouth,1.f);s.Hold(.25);
        s.Move(1,{.02f,-.12f,-.55f},2.f);
        Check(s.cast==1,"the left hand shouts the way the head faces");
    }
    {   // a dwell too short: a guard passing the face
        Sim s;s.Move(0,AtMouth,1.f);s.Hold(.12);s.Move(0,{0,-.15f,-.6f},2.5f);
        Check(s.casts==0,"a moment at the mouth shorter than the dwell is no shout");
    }
    {   // a normal melee thrust started near the face, never resting there
        Sim s;s.Move(0,{.05f,-.35f,-.05f},1.f);s.Hold(.3);
        s.Move(0,{0,-.12f,-.12f},3.f);s.Move(0,{0,-.1f,-.75f},4.f);
        Check(s.casts==0,"a thrust that passes the mouth without resting is no shout");
    }
    {   // a high guard held beside the cheek, not at the mouth
        Sim s;s.Move(0,{.20f,-.08f,-.05f},1.f);s.Hold(.5);s.Move(0,{.2f,-.1f,-.7f},3.f);
        Check(s.ready<0&&s.casts==0,"a guard 20 cm beside the mouth never readies");
    }
    {   // too slow: a reach, not a shout
        Sim s;s.Move(0,AtMouth,1.f);s.Hold(.25);s.Move(0,{0,-.15f,-.6f},.7f);
        Check(s.casts==0,"a slow reach out from the mouth is no shout");
    }
    {   // pushed down or sideways
        Sim s;s.Move(0,AtMouth,1.f);s.Hold(.25);s.Move(0,{0,-.6f,-.2f},2.5f);
        Check(s.casts==0,"lowering the hand from the mouth is no shout");
        s.Move(0,AtMouth,1.f);s.time+=1;s.Hold(.25);s.Move(0,{.45f,-.13f,-.2f},2.5f);
        Check(s.casts==0,"swinging the hand out to the side is no shout");
    }
    {   // a bottle in hand, or the trigger held: not eligible
        Sim s;s.eligible[0]=false;s.Move(0,AtMouth,1.f);s.Hold(.25);s.Move(0,{0,-.15f,-.6f},2.5f);
        Check(s.ready<0&&s.casts==0,"a hand holding a bottle (drinking) never shouts");
        Sim t;t.Move(0,AtMouth,1.f);t.Hold(.25);t.eligible[0]=false;t.Tick();t.eligible[0]=true;t.Move(0,{0,-.15f,-.6f},2.5f);
        Check(t.casts==0,"grabbing a bottle mid-gesture starts it over");
    }
    {   // rearm: a fresh rest at the mouth, and not at once
        Sim s;s.Move(0,AtMouth,1.f);s.Hold(.25);s.Move(0,{0,-.15f,-.6f},2.5f);Check(s.casts==1,"first shout");
        s.Move(0,AtMouth,6.f);s.Hold(.25);s.Move(0,{0,-.15f,-.6f},2.5f);
        Check(s.casts==1,"straight back and out again within 0.6 s is no second shout");
        s.Hold(.4);s.Move(0,AtMouth,1.f);s.Hold(.25);s.Move(0,{0,-.15f,-.6f},2.5f);
        Check(s.casts==2,"after the rearm time, a fresh rest and push shouts again");
    }
    {   // a tracking hitch mid-rest starts it over
        Sim s;s.Move(0,AtMouth,1.f);s.Hold(.15);s.time+=.2;s.Hold(.1);s.Move(0,{0,-.15f,-.6f},2.5f);
        Check(s.casts==0,"a hitch during the rest needs a full rest again");
    }
    {   // both hands at once: one shout, the right hand's
        Sim s;s.Move(0,AtMouth,1.f);s.Move(1,{-.01f,-.13f,-.13f},1.f);s.Hold(.25);
        for(int i=0;i<40;++i){
            for(int h=0;h<2;++h){const Vec to=s.Local({h?-.01f:0.f,-.15f,-.6f});const Vec d=to-s.hand[h];const float l=std::sqrt(math::Dot(d,d));
                if(l>1e-4f)s.hand[h]=l<=2.5f/90?to:s.hand[h]+d*(2.5f/90/l);}
            s.Tick();
        }
        Check(s.casts==1&&s.cast==0,"two hands pushed together shout once, the right hand's");
    }
    {   // the native press: the left trigger's alternate bank holds it as long as Smite's
        weaponskills::Pulse right,left;
        Check(right.Start(0,149,-1,7,10.0)&&right.Get(0,149,-1,7,10.06)==0&&right.Get(0,149,-1,7,10.07)<0,"a right trigger shout presses for 65 ms");
        Check(left.Start(4,149,-1,7,10.0,true)&&left.Get(4,149,-1,7,10.17)==4&&left.Get(4,149,-1,7,10.19)<0,"a left trigger shout presses for 180 ms");
        Check(std::string_view(weaponskills::Hint(149,false,false))=="Hand to mouth, push out","the cue names the shout");
    }
    std::puts("warcry gesture ok");
    return 0;
}
