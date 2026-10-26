#include "melee_contact.h"
#include "melee_hand_state.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr;using namespace framevr::melee;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Sim {
    Engine engine;Sample sample;Sweep sweep;
    Sim(){sample.minWeaponSpeed=3;sample.active=true;sample.epoch=1;sample.equipment=1;sample.unitsPerMetre=4;
        sample.hand.p={0,1,-.5f};sample.blade={{0,1,-.5f},{0,1,-1.2f},.04f};sample.thrustAxis={0,0,-1};Step(8);}
    bool Step(int n=1){bool result=false;while(n--){sample.time+=.02;result=engine.Step(sample,sweep);}return result;}
    bool Move(Vec d){sample.hand.p=sample.hand.p+d;sample.blade.base=sample.blade.base+d;sample.blade.tip=sample.blade.tip+d;return Step();}
};
int main(){
    {
        Sim right,left;left.sample.handIndex=1;left.Step(8);
        Check(Lane(right.sample)==0&&Lane(left.sample)==2,"weapons use separate pose engines");
        Sample shield=left.sample;shield.shield=true;
        Check(Lane(shield)==1,"shield contact retains its own engine");
        bool r=false,l=false;
        for(int i=0;i<6;++i){r|=right.Move({.08f,0,0});l|=left.Move({-.08f,0,0});}
        Check(r&&l,"simultaneous mirrored swings both generate contacts");
        std::array<HandCombat,2> state;
        for(auto& hand:state) {
            Check(hand.hits.Insert(1,1,7),"each blade can hit the same target once");
            Check(!hand.hits.Insert(1,1,7),"one blade cannot repeat its contact");
            Check(hand.felt.Insert(1,1,7),"each controller receives its own impact");
        }
        state[0].recovery.Landed(1,1000,600);
        Check(state[1].recovery.Since(1,1000)==UINT64_MAX,"right swing does not weaken first left swing");
        state[1].recovery.Landed(1,1100,400);
        Check(state[0].recovery.Since(1,1200)==200&&state[1].recovery.Since(1,1200)==100,"alternating strokes keep independent recovery");
        ++state[0].cleaveLanded;
        Check(state[1].cleaveLanded==0,"right cleave never consumes the left stroke's first target");
        auto changed=left.sample;changed.weapons={{41,42},{D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE},{41,42}};
        changed.time+=.02;Sweep sweep;
        Check(!left.engine.Step(changed,sweep),"equipping a second weapon cancels the old stroke");
        changed.weapons.id[1]=43;changed.time+=.02;
        Check(!left.engine.Step(changed,sweep),"same-type replacement weapon cannot inherit a stroke");
        changed.active=false;changed.time+=.02;
        Check(!left.engine.Step(changed,sweep),"menu/stow cancellation disables the left stroke");
    }
    {
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);
        bool thrust=false;
        for(int i=0;i<8;++i)thrust|=s.Move({0,0,-.05f});
        Check(thrust,"a deliberate straight thrust works at the default swing threshold");
    }
    // Axial thrusts do not need a fast rotating tip. Match the posed blade to
    // its tracking-space axis across headings, pitches, lengths and XR rates.
    for(double dt:{1./36,1./44,1./72,1./90,1./120,1./144})
    for(Vec axis:{Vec{0,0,-1},Vec{1,0,0},Vec{0,1,0},math::Normalized(Vec{-1,1,-1})})
    for(float length:{.28f,.7f,1.5f}) {
        Sim s;s.sample.minWeaponSpeed=12;s.sample.thrustAxis=axis;
        s.sample.blade.tip=s.sample.blade.base+axis*length;s.engine.Reset();s.Step(8);
        bool hit=false;uint64_t stroke=0;HitSet hits;
        const Vec target=s.sample.blade.tip+axis*.22f;
        const Body body{target-Vec{0,.05f,0},target+Vec{0,.05f,0},.05f};
        for(double t=0;t<.18;t+=dt) {
            const Vec delta=axis*(2.5f*float(dt));const Blade before=s.sample.blade;
            s.sample.time+=dt;s.sample.hand.p=s.sample.hand.p+delta;
            s.sample.blade.base=s.sample.blade.base+delta;s.sample.blade.tip=s.sample.blade.tip+delta;
            if(s.engine.Step(s.sample,s.sweep)) {
                Check(s.sweep.thrust&&s.sweep.speed<12,"thrust uses axial speed independently of swing setting");
                Check(Length(s.sweep.before.base-before.base)<.0001f,"thrust never replays earlier unpowered approach");
                if(!stroke)stroke=s.sweep.stroke;
                Check(stroke==s.sweep.stroke,"one thrust retains one stroke through overlap");
                if(Touches(s.sweep.before,s.sweep.after,body)) {
                    const bool first=hits.Insert(1,stroke,42);Check(first!=hit,"overlap deduplicates the contacted creature");hit=true;
                }
            }
        }
        Check(hit,"dagger, sword and spear thrusts hit across orientations and headset rates");
        Check(!s.Step(8),"holding the thrust inside a target stops damaging");
    }
    {
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);uint64_t last=0;
        for(int jab=0;jab<6;++jab) {
            uint64_t current=0;
            for(int i=0;i<3;++i)if(s.Move({0,0,-.06f}))current=s.sweep.stroke;
            Check(current>last,"quick repeated jabs rearm after a real pullback, without the slash timer");last=current;
            for(int i=0;i<3;++i)Check(!s.Move({0,0,.06f}),"withdrawing a thrust does not deal another hit");
        }
    }
    {
        Sim s;s.sample.minWeaponSpeed=1.5f;s.Step(8);
        for(int i=0;i<5;++i)s.Move({0,0,-.05f});
        for(int i=0;i<7;++i)Check(!s.Move({0,0,.05f}),"lowering swing threshold does not make a long thrust pullback into a hit");
    }
    for(Vec move:{Vec{.05f,0,0},Vec{0,0,.05f},Vec{0,0,-.02f},Vec{.04f,0,-.03f}}) {
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);
        for(int i=0;i<16;++i)Check(!s.Move(move),"sideways, backward, slow and poorly aligned motions are not thrusts");
    }
    {
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);
        Check(!s.Move({0,0,-.10f}),"a short fast poke does not qualify");s.Step(8);
        for(int i=0;i<40;++i)Check(!s.Move({0,0,i%2?.04f:-.04f}),"axial overlap jitter cannot accumulate a thrust");
        bool hit=false;for(int i=0;i<6;++i)hit|=s.Move({0,0,-.05f});
        Check(hit,"jitter does not poison the next deliberate thrust");
        for(int i=0;i<12;++i)Check(!s.Move({0,0,-.01f}),"slow follow-through cannot prolong thrust damage");
    }
    for(float speed:{.75f,2.f,4.f}) {
        Sim s;s.sample.minWeaponSpeed=12;s.sample.minThrustSpeed=speed;s.Step(8);
        bool hit=false;for(int i=0;i<12;++i)hit|=s.Move({0,0,-.03f});
        Check(hit==(speed<1.5f),"separate thrust setting controls a modest forward jab");
        s.sample.minThrustSpeed=3;
        Check(!s.Move({0,0,-.07f}),"changing thrust sensitivity discards the old stroke");
    }
    for(Vec badAxis:{Vec{},Vec{NAN,0,0}}) {
        Sim s;s.sample.minWeaponSpeed=12;s.sample.thrustAxis=badAxis;s.Step(8);
        for(int i=0;i<10;++i)Check(!s.Move({0,0,-.05f}),"missing or invalid posed axis cannot grant thrust hits");
    }
    {
        // A weapon's local blade axis need not be controller forward. Recover
        // it from the posed grip, then apply the matching absolute XR pose.
        const Vec local=math::Normalized(Vec{.2f,1,-.4f});
        const auto relative=math::Yaw(.7f),headYaw=math::Yaw(1.4f);
        const Vec headAxis=math::Rotate(relative,local);
        const Vec recovered=math::Rotate(math::Conjugate(relative),headAxis);
        const Vec actual=TrackingThrustAxis(recovered,math::Multiply(headYaw,relative));
        Check(Length(actual-math::Rotate(headYaw,headAxis))<.0001f,"rendered blade calibration follows matching tracked grip across head yaw");
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);
        for(int i=0;i<12;++i) {
            s.sample.blade.base.z-=.05f;s.sample.blade.tip.z-=.05f;
            Check(!s.Step(),"world movement alone cannot supply thrust energy");
        }
    }
    {
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);uint64_t thrust=0;
        for(int i=0;i<5;++i)if(s.Move({0,0,-.05f}))thrust=s.sweep.stroke;
        bool slash=false;
        for(int i=1;i<6;++i) {
            s.sample.hand.q=math::Yaw(i*.5f);s.sample.thrustAxis=math::Rotate(s.sample.hand.q,{0,0,-1});
            s.sample.blade.tip=s.sample.blade.base+s.sample.thrustAxis*.7f;
            if(s.Step()) {slash=true;Check(!s.sweep.thrust&&s.sweep.stroke!=thrust,"a deliberate slash can follow a thrust");}
        }
        Check(slash,"thrust state cannot lock out a subsequent slash");
    }
    for(float setting:{1.5f,3.f,6.f,12.f,99.f}) {
        Sim s;s.sample.minWeaponSpeed=setting;s.Step(8);
        bool hit=false;for(int i=0;i<8;++i)hit|=s.Move({.07f,0,0});
        Check(hit==(setting<3.5f),"applied speed changes which swings qualify");
        s.sample.minWeaponSpeed=6;
        Check(!s.Move({.07f,0,0}),"speed adjustment cancels the previous stroke");
    }
    Check(Sample{}.minWeaponSpeed==12&&WeaponContactSpeed(NAN)==12&&WeaponContactSpeed(0)==1.5f&&WeaponContactSpeed(99)==99,"twelve m/s default and lower bound without upper cap");
    {
        Sim s;s.sample.minWeaponSpeed=12;s.Step(8);
        bool hit=false;for(int i=1;i<=6;++i) {
            s.sample.hand.q=math::Yaw(i*.2f);
            s.sample.blade.tip=s.sample.blade.base+math::Rotate(s.sample.hand.q,{0,0,-.7f});
            hit|=s.Step();
        }
        Check(!hit,"seven m/s rotation stays below a configured threshold above the old cap");
        for(int i=1;i<=6;++i) {
            s.sample.hand.q=math::Yaw(1.2f+i*.5f);
            s.sample.blade.tip=s.sample.blade.base+math::Rotate(s.sample.hand.q,{0,0,-.7f});
            hit|=s.Step();
        }
        Check(hit,"faster rotation can satisfy the higher configured threshold");
    }
    {
        Sim s;
        Check(!s.Move({.10f,0,0}),"fast but short ten-centimeter twitch cannot arm a stroke");
        s.Step(8);
        Check(!s.Move({-.10f,0,0}),"separate small twitches cannot accumulate into a stroke");
    }
    for(double dt:{1./36,1./72,1./90,1./120}) {
        Sim s;
        // Slow contact must fail both before and after a real swing.
        auto move=[&](float speed){
            const Vec d{float(dt)*speed,0,0};s.sample.time+=dt;
            s.sample.hand.p=s.sample.hand.p+d;s.sample.blade.base=s.sample.blade.base+d;s.sample.blade.tip=s.sample.blade.tip+d;
            return s.engine.Step(s.sample,s.sweep);
        };
        for(int i=0;i<10;++i)Check(!move(2.2f),"slow approach cannot power weapon damage");
        bool swung=false;for(int i=0;i<10;++i)swung|=move(3.5f);
        Check(swung,"deliberate swing still arms");
        for(int i=0;i<10;++i)Check(!move(2.2f),"slow follow-through cannot tap after an energetic swing");
    }
    {
        Sim s;
        for(int i=0;i<20;++i) {
            const float a=.2f*i;
            s.sample.hand.q={0,0,std::sin(a/2),std::cos(a/2)};
            // Shaft lies on Z; wrist roll leaves the actual blade axis still.
            Check(!s.Step(),"twisting a resting shaft cannot power a hit");
        }
    }
    {
        Sim s;
        for(int i=0;i<20;++i) {
            s.sample.blade.tip=s.sample.blade.base+math::Rotate(math::Yaw(i*.1f),{0,0,-.7f});
            Check(!s.Step(),"camera turning a blade with a tracked stationary hand cannot power a hit");
        }
    }
    {
        Sim s;s.sample.shield=true;s.Step(8);
        for(int i=0;i<8;++i)Check(!s.Move({.028f,0,0}),"slowly repositioning a guard never bashes");
        bool bash=false;for(int i=0;i<5;++i)bash|=s.Move({.05f,0,0});
        Check(bash,"deliberate hand travel powers shield bash at the default two m/s");
        const auto stroke=s.sweep.stroke;
        Check(!s.Move({.02f,0,0}),"bash must still meet its threshold at contact");
        Check(s.Move({.05f,0,0})&&s.sweep.stroke==stroke,"same forward stroke cannot bash twice after slowing");
        s.sample.minShieldBashSpeed=4;s.Step(8);
        for(int i=0;i<5;++i)Check(!s.Move({.06f,0,0}),"higher shield threshold takes effect independently of weapon speed");
        bool strong=false;for(int i=0;i<4;++i)strong|=s.Move({.09f,0,0});
        Check(strong,"a faster bash meets the adjusted threshold");
    }
    for(float length:{.25f,1.2f}) {
        Sim s;s.sample.shield=true;s.Step(8);
        for(int i=0;i<32;++i) {
            s.sample.hand.q=math::Yaw(i*.2f);
            s.sample.blade.tip=s.sample.blade.base+math::Rotate(s.sample.hand.q,{0,0,-length});
            Check(!s.Step(),"wrist rotation alone cannot bash with either shield size");
        }
        for(int i=0;i<24;++i)Check(!s.Move({i%2?.045f:-.045f,0,0}),"fast short guard jitter does not accumulate into a bash");
    }
    {
        Sim s;s.sample.shield=true;s.Step(8);
        // An earlier low-speed approach arms no retroactive contact sweep.
        for(int i=0;i<6;++i)s.Move({.015f,0,0});
        const auto before=s.sample.blade;
        Check(s.Move({.06f,0,0})&&Length(s.sweep.before.base-before.base)<1e-5f,"bash tests only the current fast contact interval");
        PoseHistory h;Sample matched;h.Push(1,s.sample);
        s.sample.minShieldBashSpeed=3;s.sample.time+=.02;h.Push(2,s.sample);
        Check(!h.Find(1,s.sample.time,matched),"a live bash speed change invalidates old shield geometry");
    }
    {
        Sim s;s.sample.shield=true;s.sample.minShieldBashSpeed=.75f;s.Step(8);
        bool easy=false;for(int i=0;i<9;++i)easy|=s.Move({.016f,0,0});
        Check(easy,"lower accessibility threshold still accepts deliberate twelve cm travel");
    }
    Check(ShieldBashSpeed(NAN)==2&&ShieldBashSpeed(0)==.75f&&ShieldBashSpeed(99)==10,"shield bash threshold rejects nonfinite and out-of-range values");
    const Body body{{0,.3f,0},{0,1.5f,0},.25f};
    Check(Touches({{-.6f,.8f,-.3f},{-.6f,.8f,.3f},.04f},{{.6f,.8f,-.3f},{.6f,.8f,.3f},.04f},body),"fast blade crossing between endpoints hits");
    Check(!Touches({{-.6f,2.1f,-.3f},{-.6f,2.1f,.3f},.04f},{{.6f,2.1f,-.3f},{.6f,2.1f,.3f},.04f},body),"swing above head misses");
    Check(!Touches({{-.6f,.8f,1},{-.6f,.8f,2},.04f},{{.6f,.8f,1},{.6f,.8f,2},.04f},body),"swing beside creature misses");
    Check(Touches({{0,1,-1},{0,1,-.5f},.04f},{{0,1,-.5f},{0,1,.1f},.04f},body),"thrust tip contact");
    Check(Touches({{0,2,0},{0,2.6f,0},.04f},{{0,1,0},{0,1.6f,0},.04f},body),"vertical chop contact");
    Check(Touches({{0,1,-.7f},{0,1,-.7f},.08f},{{0,1,.1f},{0,1,.1f},.08f},body),"unarmed degenerate segment is a swept fist");
    Check(SegmentDistanceSquared({0,0,0},{1,0,0},{0,1,0},{1,1,0})==1,"parallel segment distance");
    {
        const Blade left{{0,1,0},{-.707107f,1,-.707107f},.02f},right{{0,1,0},{.707107f,1,-.707107f},.02f};
        const Body edge{{0,.95f,-1},{0,1.05f,-1},.04f},beyond{{0,.95f,-1.3f},{0,1.05f,-1.3f},.04f};
        Check(Touches(left,right,edge),"swept slash reaches outer arc, not just shortened endpoint chord");
        Check(!Touches(left,right,beyond),"arc sweep does not extend weapon reach");
    }
    {
        Sim s;int powered=0;uint64_t stroke=0;
        for(int i=0;i<12;++i)if(s.Move({.07f,0,0})){++powered;if(!stroke)stroke=s.sweep.stroke;Check(stroke==s.sweep.stroke,"one stroke through follow-through");}
        Check(powered>0,"ordinary free slash has energy without any button state");
        s.Step(10);Check(!s.sweep.stroke,"resting blade has no damaging sweep");
        bool backhand=false;for(int i=0;i<8;++i)if(s.Move({-.07f,0,0})){backhand=true;Check(s.sweep.stroke!=stroke,"new slash gets distinct identity");}
        Check(backhand,"free backhand works");
    }
    {
        Sim s;uint64_t stroke=0;for(int i=0;i<16;++i)if(s.Move({.07f,0,0}))stroke=s.sweep.stroke;
        bool reversed=false;for(int i=0;i<8;++i)if(s.Move({-.07f,0,0})){reversed=true;Check(s.sweep.stroke>stroke,"deliberate reversal rearms without a pause");}
        Check(reversed,"continuous alternating slashes work");
    }
    {
        Sim s;
        for(int i=0;i<30;++i){s.sample.blade.base.x+=.03f;s.sample.blade.tip.x+=.03f;Check(!s.Step(),"camera/locomotion moving weapon cannot supply swing power");}
        for(int i=0;i<100;++i)Check(!s.Move({i%2?.02f:-.02f,0,0}),"tiny repeated hand jitter cannot become a stroke");
        bool afterJitter=false;for(int i=0;i<8;++i)afterJitter|=s.Move({-.07f,0,0});
        Check(afterJitter,"sub-threshold jitter cannot poison the next deliberate slash's energy history");
        Check(!s.Move({1,0,0}),"tracking teleport cannot hit");
        for(int i=0;i<3;++i)Check(!s.Move({.07f,0,0}),"discontinuity discards immediate motion");
        bool recovered=false;for(int i=0;i<10;++i)recovered|=s.Move({-.07f,0,0});
        Check(recovered,"fresh continuous motion recovers from tracking jump without holding still");
    }
    {
        Sim s;for(int i=0;i<5;++i)s.Move({.07f,0,0});
        ++s.sample.epoch;Check(!s.Move({.07f,0,0}),"snap turn/recenter cancels old stroke");
        s.Step(8);++s.sample.equipment;Check(!s.Move({.07f,0,0}),"equipment cancels previous geometry");
        s.Step(8);s.sample.claws=true;Check(!s.Move({.07f,0,0}),"taking a form: a weapon's pose and a claw's never join one sweep");
        s.Step(8);s.sample.claws=false;Check(!s.Move({.07f,0,0}),"and leaving it");
        s.Step(8);s.sample.active=false;Check(!s.Move({.07f,0,0}),"menus and pose loss clear sweep");
        s.sample.active=true;Check(!s.Move({.07f,0,0}),"returning from menu primes anew");
        s.Step(8);s.sample.time+=.3;Check(!s.Move({.07f,0,0}),"hitch cannot replay stale stroke");
        s.sample.hand.p.x=std::numeric_limits<float>::quiet_NaN();Check(!s.Step(),"invalid pose rejected");
    }
    for(double dt:{1./36,1./72,1./120}) {
        Sim s;bool contact=false;
        for(int i=0;i<30;++i){s.sample.time+=dt;const Vec d{float(dt)*3.5f,0,0};s.sample.hand.p=s.sample.hand.p+d;s.sample.blade.base=s.sample.blade.base+d;s.sample.blade.tip=s.sample.blade.tip+d;contact|=s.engine.Step(s.sample,s.sweep);}
        Check(contact,"frame-rate independent energy threshold");
    }
    {
        Sim s;bool powered=false;
        for(int i=1;i<10;++i){s.sample.hand.q=math::Yaw(i*.1f);s.sample.blade.tip=s.sample.blade.base+math::Rotate(s.sample.hand.q,{0,0,-.7f});powered|=s.Step();}
        Check(powered,"wrist-led slash supplies blade energy");
        s.Step(8);for(int i=0;i<8;++i){auto& q=s.sample.hand.q;q={-q.x,-q.y,-q.z,-q.w};Check(!s.Step(),"quaternion sign is not a new rotation");}
    }
    {
        Sim s;uint64_t first=0,last=0;int distinct=0;
        for(int half=0;half<4;++half)for(int i=1;i<=14;++i) {
            const float angle=half%2?1.4f-i*.1f:i*.1f;
            s.sample.hand.q=math::Yaw(angle);
            s.sample.blade.tip=s.sample.blade.base+math::Rotate(s.sample.hand.q,{0,0,-.7f});
            if(s.Step()&&s.sweep.stroke!=last){last=s.sweep.stroke;if(!first)first=last;++distinct;}
        }
        Check(first&&distinct==4,"four wrist-only alternating slashes are four strokes without pausing or translating the hand");
    }
    {
        Sim s;
        bool started=false;
        for(int i=0;i<5&&!started;++i) {
            const auto prior=s.sample.blade;
            if(s.Move({.07f,0,0})) {
                started=true;Check(Length(s.sweep.before.base-prior.base)<.001f,"arming a stroke does not replay earlier unpowered contacts");
            }
        }
        Check(started,"short deliberate swing starts");
    }
    // The owner's captured slashes reached 53 rad/s. The old upper speed
    // cutoff rejected the strongest part of a slash, then a tracking guard
    // required 80 ms below 0.45 m/s: 35 seconds of combat produced no sweeps.
    // Synthetic traces cover each swing plane without shipping pose captures.
    for(Vec axis:{Vec{1,0,0},Vec{0,1,0},Vec{0,0,1},math::Normalized(Vec{1,1,1})}) {
        Sim s;int strong=0;uint64_t last=0;int strokes=0;
        const Vec blade=std::fabs(axis.z)>.5f?Vec{.7f,0,0}:Vec{0,0,-.7f};
        for(int loop=0;loop<8;++loop)for(float a:{-1.4f,-.5f,.4f,1.3f,1.5f,.6f,-.3f,-1.2f}) {
            s.sample.hand.q={axis.x*std::sin(a/2),axis.y*std::sin(a/2),axis.z*std::sin(a/2),std::cos(a/2)};
            s.sample.blade.tip=s.sample.blade.base+math::Rotate(s.sample.hand.q,blade);
            if(s.Step()) {
                strong+=s.sweep.speed>18;
                if(s.sweep.stroke!=last){last=s.sweep.stroke;++strokes;}
            }
        }
        Check(strong>=12,"strong lateral, vertical and diagonal rotations remain damaging");
        Check(strokes>=4,"continuous strong swings rearm without a stationary pause");
    }
    // Exercise the input gate as well as contact recognition. A stale angular
    // limit in the input gate used to cancel every strong slash before the
    // contact engine could finish its recovery, including after area loads.
    for(double dt:{1./36,1./44,1./72,1./90,1./120}) {
        gameplay::Engine input;Engine contact;gameplay::Input in;Sample sample;Sweep sweep;
        in.active=in.tracked[0]=in.tracked[1]=in.aimTracked=true;
        in.weapon=gameplay::Weapon::Melee;in.equipment=1;
        in.head.p={0,1.65f,0};in.hand[0].p={0,1,-.5f};in.hand[1].p={-.25f,1,-.5f};in.aim=in.hand[0];
        sample.epoch=1;sample.equipment=1;sample.unitsPerMetre=4;
        auto step=[&] {
            in.time+=dt;const auto out=input.Step(in);
            sample.time=in.time;sample.active=out.meleeContact;sample.hand=in.hand[0];
            sample.blade={in.hand[0].p,in.hand[0].p+math::Rotate(in.hand[0].q,{0,0,-.7f}),.04f};
            if(!out.meleeContact){contact.Reset();return false;}
            return contact.Step(sample,sweep);
        };
        for(int area=0;area<3;++area) {
            input.Select(area?2:-1);
            // Loading screens interrupt the old stroke; fresh motion must
            // recover on both sides of repeated outdoor/dungeon transitions.
            in.active=false;Check(!step(),"area load cancels old contact");in.active=true;
            for(int i=0;i<20;++i)step();
            int powered=0;float angle=0,direction=1;
            for(int i=0;i<80;++i) {
                angle+=direction*50*float(dt);if(std::fabs(angle)>=1.4f)direction=-direction;
                in.hand[0].q=math::Yaw(angle);powered+=step();
            }
            Check(powered>16,"strong swings survive input and contact gates after area loads at every headset rate");
            Check(input.Selection()==(area?2:-1),"free swings preserve the selected trigger skill across area loads");
            // The hardest real swings (a riposte straight after a parry) turn the
            // wrist faster than the old 60 rad/s limit: 90 rad/s must survive...
            {   gameplay::Engine fastIn;Engine fastContact;bool alive=true;auto q=in.hand[0].q;
                for(int i=0;i<12&&alive;++i){q=math::Multiply(math::Yaw(90*float(dt)),q);auto f=in;f.time+=dt*(i+1);f.hand[0].q=q;
                    const auto out=fastIn.Step(f);if(i>2&&!out.meleeContact)alive=false;}
                Check(alive,"a 90 rad/s riposte swing is not cancelled as a tracking jump"); }
            // ...but a real tracking jump (over 100 rad/s) must still drop this frame's geometry.
            in.hand[0].q=math::Multiply(math::Yaw(std::min(3.1f,105*float(dt))),in.hand[0].q);
            Check(!step(),"angular tracking jump cannot damage through the full pipeline");
        }
    }
    {
        Sim s;for(int i=0;i<8;++i)s.Move({.07f,0,0});
        s.sample.time+=.25;Check(!s.Move({.07f,0,0}),"hitch itself never damages");
        for(int i=0;i<3;++i)Check(!s.Move({-.07f,0,0}),"hitch recovery drops old geometry");
        bool resumed=false;for(int i=0;i<12;++i)resumed|=s.Move({-.07f,0,0});
        Check(resumed,"continued swinging recovers after hitch without requiring a quiet hand");
    }
    {
        PoseHistory history;Sim s;Sample matched;
        history.Push(10,s.sample);s.sample.time+=.02;s.sample.hand.p.x=.1f;history.Push(11,s.sample);
        Check(history.Find(10,s.sample.time,matched)&&matched.hand.p.x==0,"rendered blade resolves its own earlier hand sample");
        Check(!history.Find(9,s.sample.time,matched),"unknown stamp cannot use latest hand as fallback");
        Check(!history.Find(10,s.sample.time+.11,matched),"expired input cannot replay a contact");
        ++s.sample.epoch;history.Push(12,s.sample);
        Check(!history.Find(10,s.sample.time,matched),"recenter invalidates earlier rendered pose");
        ++s.sample.equipment;history.Push(13,s.sample);
        Check(!history.Find(12,s.sample.time,matched),"weapon switch invalidates earlier rendered pose");
        s.sample.active=false;history.Push(14,s.sample);
        Check(!history.Find(13,s.sample.time,matched),"menu or trigger immediately disables older melee poses");
        history.Clear();Check(!history.Find(14,s.sample.time,matched),"tracking loss clears history");
        s.sample.active=true;history.Push(0,s.sample);
        Check(history.Find(0,s.sample.time,matched),"wrapped zero input stamp remains valid");
        s.sample.weapons={{41,42},{D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE},{41,42}};
        s.sample.time+=.02;history.Push(1,s.sample);
        Check(!history.Find(0,s.sample.time,matched),"same-code equipment identity change invalidates older poses");
        std::swap(s.sample.weapons.displayed[0],s.sample.weapons.displayed[1]);
        s.sample.time+=.02;history.Push(2,s.sample);
        Check(!history.Find(1,s.sample.time,matched),"reversed native primary order invalidates older poses");
    }
    {
        // A quick slash ends between input update and the weapon pose. Pairing
        // its stale blade with the new velocity misses; when the blade catches
        // up the current hand is stationary. Timestamp pairing retains the hit.
        Engine mismatched,matched;PoseHistory history;Sample input;Sweep sw;
        input.minWeaponSpeed=3;input.active=true;input.epoch=1;input.equipment=1;input.unitsPerMetre=4;
        input.hand.p={-.19f,1,-.5f};input.blade={{-.19f,1,-.5f},{-.19f,1,-1.2f},.025f};
        const Body target{{0,.9f,-.7f},{0,1.1f,-.7f},.025f};
        Sample rendered=input;uint32_t renderStamp=1;history.Push(renderStamp,input);
        bool oldHit=false,newHit=false;
        for(uint32_t stamp=2;stamp<24;++stamp) {
            input.time+=.02;
            if(stamp==12){input.hand.p.x=0;input.blade.base.x=input.blade.tip.x=0;}
            history.Push(stamp,input);
            auto mixed=input;mixed.blade=rendered.blade;
            if(mismatched.Step(mixed,sw))oldHit|=Touches(sw.before,sw.after,target);
            Sample exact;
            Check(history.Find(renderStamp,input.time,exact),"one-frame pose history remains available");
            exact.blade=rendered.blade;
            if(matched.Step(exact,sw))newHit|=Touches(sw.before,sw.after,target);
            rendered=input;renderStamp=stamp;
        }
        Check(!oldHit,"reproduction: latest input with delayed blade loses the fast slash");
        Check(newHit,"matched blade and input retain the fast slash after hand stops");
    }
    HitSet hits;Check(hits.Insert(1,1,0),"unit zero is valid");Check(!hits.Insert(1,1,0),"one damage request per target per stroke");
    Check(hits.Insert(1,1,9),"same stroke can strike another actual contact");Check(hits.Insert(1,2,0),"next stroke can hit same creature");
    Check(hits.Insert(2,1,0),"new session clears old identities");
    std::puts("Melee contact: swept geometry, stroke energy, cancellation and per-target deduplication passed");
}
