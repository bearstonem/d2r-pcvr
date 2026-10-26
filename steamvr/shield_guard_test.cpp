#include "shield_guard.h"
#include "shield_surface.h"
#include "shield_debug.h"
#include "active_effects.h"
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace framevr::guard;
using framevr::math::Vec;

// Default fixture: shield faces ahead (-Z). The production path has no view
// argument: these helpers rotate both the held shield and hand for old gestures.
static const Vec view{0,0,-1},ahead{0,0,-1.5f};
static Pose Surface(Vec hand,Vec facing,uint64_t at) {
    const Vec right=framevr::math::Cross(facing,{0,1,0});
    const Vec eye{0,1.7f,0};
    return {eye+right*hand.x+Vec{0,hand.y,0}+facing*(-hand.z),facing,eye,hand,4,at,1};
}
static void Update(State& s,Vec hand,bool handOk,bool left,Vec facing,uint64_t now,bool posed,const Tuning& t) {
    framevr::guard::Update(s,Surface(hand,facing,now),handOk,left,now,posed,t);
}

// Feed the hand (head frame) along a straight path from a to b over ms, one
// pose per 11 ms frame but sampled every 1 ms tick, as vrcam's timer does.
static void Move(State& s,Vec a,Vec b,bool left,uint64_t& now,uint64_t ms,const Tuning& t) {
    for(uint64_t i=1;i<=ms;++i) {
        const uint64_t frame=i-(i%11);
        const float f=float(frame)/float(ms);
        Update(s,a+(b-a)*f,true,left,view,++now,true,t);
    }
    Update(s,b,true,left,view,++now,true,t);
}
static void Hold(State& s,Vec p,bool left,uint64_t& now,uint64_t ms,const Tuning& t) {
    for(uint64_t i=0;i<ms;++i)Update(s,p,true,left,view,++now,true,t);
}

int main() {
    const Tuning t;
    // Head frame: x right, y up, z back (metres from the eyes). A left shield.
    const Vec hanging{-.22f,-.72f,-.02f};   // arm down at the side
    const Vec chest{-.12f,-.35f,-.25f};     // held close to the chest
    const Vec high{-.15f,.10f,-.35f};       // up at the face
    const Vec tucked{.05f,-.30f,-.14f};     // tight in, toward the middle
    const Vec far{-.10f,-.25f,-.70f};       // pushed well out ahead
    const Vec against{-.12f,-.45f,.14f};    // flat on the chest: behind the eyes (log 2026-10-16)
    const Vec againstLow{-.09f,-.56f,.05f};
    const Vec low{-.10f,-.66f,-.35f};       // ahead but down at the belt
    const Vec wide{-.80f,-.30f,-.20f};      // out to the left side
    const Vec swingFrom{.18f,-.48f,-.30f};  // lower right
    const Vec swingTo{-.20f,-.06f,-.32f};   // upper left
    State s;uint64_t now=1000;

    // Hanging at the side: open.
    Hold(s,hanging,true,now,40,t);
    assert(!s.raised&&Judge(s,ahead,now,t)==Verdict::Open);
    // Anywhere a shield naturally covers the chest blocks; a slow raise is no parry.
    for(Vec p:{chest,high,tucked,far,against,againstLow}) {
        State g;uint64_t n=2000;Hold(g,hanging,true,n,40,t);Move(g,hanging,p,true,n,700,t);
        assert(g.raised&&Judge(g,ahead,n,t)==Verdict::Block);
    }
    // Down at the belt or held off the incoming line: open.
    for(Vec p:{low,wide}) {State g;uint64_t n=3000;Hold(g,p,true,n,40,t);assert(Judge(g,ahead,n,t)==Verdict::Open);}
    // A raised guard holds a few cm past the edge (no flicker at the line).
    {State g;uint64_t n=4000;Hold(g,chest,true,n,40,t);Hold(g,Vec{-.12f,-.66f,-.25f},true,n,40,t);assert(g.raised);
     Hold(g,Vec{-.12f,-.70f,-.25f},true,n,40,t);assert(!g.raised);}
    // Directional surface: ahead blocks, attackers beside/behind this shield do not.
    Move(s,hanging,chest,true,now,700,t);
    assert(Judge(s,{.8f,0,-1.f},now,t)==Verdict::Block);
    assert(Judge(s,{1.5f,0,0},now,t)==Verdict::Open);
    assert(Judge(s,{0,0,1.5f},now,t)==Verdict::Open);
    // Moving and rotating the actual shield changes its covered direction.
    {State g;uint64_t n=5000;for(int i=0;i<40;++i)Update(g,chest,true,true,Vec{1,0,0},++n,true,t);
     assert(Judge(g,{1.5f,0,0},n,t)==Verdict::Block&&Judge(g,ahead,n,t)==Verdict::Open);}
    assert(std::fabs(s.forward-.25f)<1e-4f&&std::fabs(s.up+.35f)<1e-4f&&std::fabs(s.side-.12f)<1e-4f); // trace inputs

    // Facing describes the incoming attack corridor, not a ray redirected
    // toward an offset shield grip. Pulling a tilted guard against the torso
    // must not change its angle; translation still has placement limits.
    for(float yaw:{0.f,1.5707963f,3.1415926f})for(float face:{-1.f,1.f})for(float depth:{-.4f,-.14f,0.f,.14f}) {
        const auto q=framevr::math::Yaw(yaw);
        Pose p=Surface(chest,view,6000);p.eye=framevr::math::Rotate(q,{.2f,1.63f,-.09f});
        p.center=p.eye+framevr::math::Rotate(q,{-.2f,-.45f,depth});
        p.hand={-.2f,-.45f,depth};p.normal=framevr::math::Rotate(q,Vec{-.8f,0,-.6f}*face);
        const Vec attack=Vec{p.eye.x,0,p.eye.z}+framevr::math::Rotate(q,{0,0,-1.2f});
        State g;Update(g,p,true,true,p.at,true,t);Rejection why;
        assert(Judge(g,attack,p.at,t,0,&why)==Verdict::Block&&why==Rejection::None);
        p.center=p.eye+framevr::math::Rotate(q,{-.6f,-.45f,depth});
        Update(g,p,true,true,p.at,true,t);
        assert(Judge(g,attack,p.at,t,0,&why)==Verdict::Open&&why==Rejection::OutsidePlacement);
    }
    {
        // Rounded pose from the owner's 21:21:53.403 trace. It passed placement
        // but the grip-directed ray rejected an otherwise valid facing angle.
        Pose p=Surface(chest,view,6100);
        p.normal=framevr::math::Normalized(Vec{-.67f,-.20f,-.72f});
        p.eye={-.11f,1.57f,-.02f};p.center={-.26f,1.1f,-.13f};p.hand={-.15f,-.54f,-.18f};
        const Vec attack{.17f,0,-1.12f};State g;Update(g,p,true,true,p.at,true,t);
        assert(Judge(g,attack,p.at,t)==Verdict::Block);
        // Conversely, pointing at the grip cannot rescue a face edge-on to the
        // actual incoming corridor, even with a nearby enemy.
        p.normal=framevr::math::Normalized(Vec{.98f,0,-.2f});
        p.eye={0,1.7f,0};p.center={-.3f,1.25f,-.1f};Update(g,p,true,true,p.at,true,t);
        Rejection why;assert(Judge(g,{0,0,-.5f},p.at,t,0,&why)==Verdict::Open&&why==Rejection::OutsideCoverage);
    }

    // The diagonal: lower right to upper left in 90 ms parries...
    Hold(s,swingFrom,true,now,200,t);
    Move(s,swingFrom,swingTo,true,now,90,t);
    const uint64_t snap=s.snapAt;
    assert(snap&&!s.snapUsed&&s.swingCos>.9f&&Judge(s,ahead,now+20,t)==Verdict::Parry);
    // ...an act boss (not parryable) meets the same swing as a plain guard...
    assert(Judge(s,ahead,now+20,t,0,nullptr,0,false)==Verdict::Block);
    // ...ranged attackers are blocked, never parried...
    assert(Judge(s,{0,0,-8.f},now+20,t)==Verdict::Block);
    // ...one parry per swing, even while the same swing goes on...
    SpendParry(s,snap);
    Move(s,swingTo,swingTo+Vec{-.06f,.06f,0},true,now,20,t);
    assert(s.snapAt==snap&&Judge(s,ahead,now,t)==Verdict::Block);
    // ...and it expires after the window.
    Hold(s,swingFrom,true,now,300,t);Move(s,swingFrom,swingTo,true,now,90,t);
    assert(!s.snapUsed&&Judge(s,ahead,s.snapAt+t.parryMs+1,t)!=Verdict::Parry);

    // The wrong diagonal, straight up, straight across and the reverse never parry.
    const Vec wrong[][2]={{{-.20f,-.48f,-.30f},{.18f,-.06f,-.32f}},   // lower left -> upper right
                          {{-.05f,-.50f,-.30f},{-.05f,-.05f,-.30f}},  // straight up
                          {{.20f,-.30f,-.30f},{-.25f,-.30f,-.30f}},   // straight across
                          {swingTo,swingFrom}};                       // upper left -> lower right
    for(const auto& w:wrong){State g;uint64_t n=20000;Hold(g,w[0],true,n,200,t);Move(g,w[0],w[1],true,n,90,t);
        assert(Judge(g,ahead,n,t)!=Verdict::Parry);}
    // Too slow, or too short, is a block.
    {State g;uint64_t n=30000;Hold(g,swingFrom,true,n,200,t);Move(g,swingFrom,swingTo,true,n,600,t);assert(Judge(g,ahead,n,t)==Verdict::Block);}
    {State g;uint64_t n=31000;Hold(g,chest,true,n,200,t);Move(g,chest,chest+Vec{-.06f,.06f,0},true,n,60,t);assert(Judge(g,ahead,n,t)==Verdict::Block);}
    // A swing that ends out of guard (over the head) cannot parry.
    {State g;uint64_t n=32000;Hold(g,swingFrom,true,n,200,t);Move(g,swingFrom,Vec{-.30f,.45f,-.30f},true,n,90,t);
     assert(Judge(g,ahead,n,t)==Verdict::Open);}
    // A right-hand shield mirrors: lower left -> upper right.
    {State g;uint64_t n=33000;const Vec a{-.18f,-.48f,-.30f},b{.20f,-.06f,-.32f};
     Hold(g,a,false,n,200,t);Move(g,a,b,false,n,90,t);assert(Judge(g,ahead,n+10,t)==Verdict::Parry);}

    // Holding still never produces swings.
    {State g;uint64_t n=34000;Hold(g,chest,true,n,500,t);assert(g.snapUsed&&Judge(g,ahead,n,t)==Verdict::Block);}
    // An untracked hand cannot guard.
    {State g;uint64_t n=35000;Hold(g,chest,true,n,40,t);Update(g,chest,false,true,view,++n,true,t);assert(!g.raised);}

    // The parry is a swing while the attack is incoming: after the attacker's
    // attack began, before the hit (within parryMs)...
    {State g;uint64_t n=40000;Hold(g,swingFrom,true,n,300,t);
     const uint64_t attack=n;Hold(g,swingFrom,true,n,150,t);              // the attack winds up
     Move(g,swingFrom,swingTo,true,n,90,t);
     assert(Judge(g,ahead,n+30,t,attack)==Verdict::Parry);
     // ...once the attack is coming down: a swing before the downstroke opens
     // is a windup miss (2026-10-19), at or after it a parry.
     const uint64_t snap=g.snapAt;
     assert(Judge(g,ahead,n+30,t,attack,nullptr,snap+1)==Verdict::Block);
     assert(ParryCheck(g,ahead,n+30,t,attack,snap+1)==ParryMiss::Windup);
     assert(Judge(g,ahead,n+30,t,attack,nullptr,snap)==Verdict::Parry);
     assert(!strcmp(ParryMissName(ParryMiss::Windup),"windup"));}
    // ...but not with a swing that began before the attack did (parryLeadMs
    // is 0: no lead, 2026-10-18)...
    {State g;uint64_t n=42000;Hold(g,swingFrom,true,n,300,t);Move(g,swingFrom,swingTo,true,n,90,t);
     const uint64_t snap=g.snapAt;Hold(g,swingTo,true,n,t.parryLeadMs+20,t);  // the guard stays up after the swing
     assert(snap&&g.snapAt==snap&&n>snap+t.parryLeadMs&&n-snap<=t.parryMs);
     assert(Judge(g,ahead,n,t,snap+t.parryLeadMs)==Verdict::Parry);
     assert(Judge(g,ahead,n,t,snap+t.parryLeadMs+1)==Verdict::Block);
     assert(ParryCheck(g,ahead,n,t,snap+t.parryLeadMs+1)==ParryMiss::BeforeAttack);
     assert(Judge(g,ahead,n,t,0)==Verdict::Parry);}                        // (attack start unknown: window alone)
    // 450 ms, no lead (2026-10-18): a swing during the wind-up parries a hit
    // up to parryMs after it; one ms later is a block. The old habit of a swing
    // ~700 ms before the hit no longer parries.
    {State g;uint64_t n=42500;Hold(g,swingFrom,true,n,300,t);
     const uint64_t attack=n;Move(g,swingFrom,swingTo,true,n,90,t);       // the swing comes after the attack began
     const uint64_t snap=g.snapAt;assert(snap>=attack);Hold(g,swingTo,true,n,snap+t.parryMs-n,t);
     assert(n==snap+t.parryMs&&Judge(g,ahead,n,t,attack)==Verdict::Parry);
     ++n;assert(Judge(g,ahead,n,t,attack)==Verdict::Block&&ParryCheck(g,ahead,n,t,attack)==ParryMiss::Early);
     Hold(g,swingTo,true,n,snap+700-n,t);assert(Judge(g,ahead,n,t,attack)==Verdict::Block);}
    // A missed parry swing is reported once, after its window (plus grace) runs
    // out unspent; a spent swing, or one still inside its window, is not.
    {State g;uint64_t n=42800;Hold(g,swingFrom,true,n,300,t);Move(g,swingFrom,swingTo,true,n,90,t);
     const uint64_t snap=g.snapAt;
     assert(!ParryMissed(g,snap+t.parryMs,t)&&!ParryMissed(g,snap+t.parryMs+50,t));
     assert(ParryMissed(g,snap+t.parryMs+51,t));
     assert(!ParryMissed(g,snap+t.parryMs+200,t));                        // once per swing
     State spent;n=43900;Hold(spent,swingFrom,true,n,300,t);Move(spent,swingFrom,swingTo,true,n,90,t);
     SpendParry(spent,spent.snapAt);assert(!ParryMissed(spent,spent.snapAt+t.parryMs+500,t));
     Tuning none=t;none.parryMs=0;State g2;n=44900;Hold(g2,swingFrom,true,n,300,t);Move(g2,swingFrom,swingTo,true,n,90,t);
     assert(!ParryMissed(g2,g2.snapAt+5000,none));}                      // parry disabled: no buzz
    // A timed swing parries even after it turned the face edge-on to the
    // attacker (3 of 6 timed swings, log 2026-10-17); the same pose without a
    // live swing is open.
    {State g;uint64_t n=43000;Hold(g,swingFrom,true,n,300,t);Move(g,swingFrom,swingTo,true,n,90,t);
     Pose p=Surface(swingTo,view,++n);p.normal={1,0,0};framevr::guard::Update(g,p,true,true,n,true,t);
     Rejection why;assert(Judge(g,ahead,n,t,0,&why)==Verdict::Parry&&why==Rejection::None);
     SpendParry(g,g.snapAt);
     assert(Judge(g,ahead,n,t,0,&why)==Verdict::Open&&why==Rejection::OutsideCoverage);}
    // The shield must be out on the attacker's side: a swing never parries an
    // attacker behind, or across the body from, a left shield.
    {State g;uint64_t n=43500;Hold(g,swingFrom,true,n,300,t);Move(g,swingFrom,swingTo,true,n,90,t);
     for(Vec from:{Vec{0,0,1.5f},Vec{1.5f,0,0}}) {
         assert(Judge(g,from,n,t)==Verdict::Open&&ParryCheck(g,from,n,t)==ParryMiss::Side);
     }
     assert(Judge(g,{-1.5f,0,-.2f},n,t)==Verdict::Parry);}                // the shield's own side
    // A big swing may finish with the shield beside the head, slightly behind
    // the eyes along the attack line (log 2026-10-18: ~10 cm), not further.
    {State g;uint64_t n=43600;Hold(g,swingFrom,true,n,300,t);Move(g,swingFrom,swingTo,true,n,90,t);
     Pose p=Surface(swingTo,view,++n);p.center={-.50f,1.75f,.08f};framevr::guard::Update(g,p,true,true,n,true,t);
     assert(Judge(g,ahead,n,t)==Verdict::Parry);
     p.center.z=.15f;p.at=++n;framevr::guard::Update(g,p,true,true,n,true,t);
     assert(Judge(g,ahead,n,t)==Verdict::Open&&ParryCheck(g,ahead,n,t)==ParryMiss::Side);}
    // Diagnostics name why a raised guard did not parry.
    {State g;uint64_t n=43800;Hold(g,chest,true,n,300,t);
     assert(ParryCheck(g,ahead,n,t)==ParryMiss::NoSwing);
     Hold(g,swingFrom,true,n,300,t);Move(g,swingFrom,swingTo,true,n,90,t);
     assert(ParryCheck(g,{0,0,-8.f},n,t)==ParryMiss::Range);
     assert(ParryCheck(g,ahead,g.snapAt+t.parryMs+1,t)==ParryMiss::Early);
     Tuning none=t;none.parryMs=0;assert(ParryCheck(g,ahead,n,none)==ParryMiss::Disabled);
     assert(ParryCheck(g,ahead,n,t)==ParryMiss::None);
     SpendParry(g,g.snapAt);assert(ParryCheck(g,ahead,n,t)==ParryMiss::Spent);
     assert(!strcmp(ParryMissName(ParryMiss::BeforeAttack),"before_attack"));}
    // ...and never after the hit: a swing after a blocked hit is only logged, and spends nothing.
    {State g;uint64_t n=44000;Hold(g,swingFrom,true,n,300,t);
     assert(Judge(g,ahead,n,t,n-200)==Verdict::Block);NoteBlock(g,n);
     uint64_t late=0;assert(!SwingAfterBlock(g,n,t,late));
     Hold(g,swingFrom,true,n,90,t);Move(g,swingFrom,swingTo,true,n,90,t);
     assert(SwingAfterBlock(g,n,t,late)&&late>=90&&late<=200&&!g.snapUsed);
     assert(!SwingAfterBlock(g,n,t,late));}                                // once

    // Parry disabled (window 0) leaves blocking intact.
    Tuning off=t;off.parryMs=0;
    {State g;uint64_t n=36000;Hold(g,swingFrom,true,n,200,t);Move(g,swingFrom,swingTo,true,n,90,t);assert(Judge(g,ahead,n+10,off)==Verdict::Block);}

    // A brief unposed frame (snap turn / recenter epoch) keeps the guard;
    // a real tracking or posing loss expires it after staleMs.
    Hold(s,chest,true,now,40,t);
    Update(s,{NAN,NAN,NAN},true,true,view,now+50,false,t);
    assert(s.raised&&Judge(s,ahead,now+50,t)!=Verdict::Open);
    assert(Judge(s,ahead,now+t.staleMs+1,t)==Verdict::Open);

    // Report the actual rejection without changing the guard thresholds.
    {const struct {Vec hand;Rejection why;} cases[]={
        {hanging,Rejection::TooLow},{{-.1f,.4f,-.2f},Rejection::TooHigh},
        {{-.1f,-.3f,-1.3f},Rejection::TooFar},
        {wide,Rejection::OutsidePlacement},{{.6f,-.3f,-.2f},Rejection::OutsidePlacement},
        {{NAN,0,0},Rejection::InvalidHand}};
     for(const auto& c:cases){State g;Update(g,c.hand,true,true,view,1000,true,t);Rejection why;
         assert(Judge(g,ahead,1000,t,0,&why)==Verdict::Open&&why==c.why);}
     State g;Update(g,chest,true,true,view,1000,true,t);Rejection why;
     assert(Judge(g,ahead,1000,t,0,&why)==Verdict::Block&&why==Rejection::None);
     assert(Judge(g,{0,0,1},1000,t,0,&why)==Verdict::Open&&why==Rejection::OutsidePlacement);
     assert(Judge(g,ahead,1151,t,0,&why)==Verdict::Open&&why==Rejection::StalePose);
     Update(g,chest,false,true,view,1001,true,t);
     assert(Judge(g,ahead,1001,t,0,&why)==Verdict::Open&&why==Rejection::Untracked);}

    // Head yaw alone changes the head-relative hand, never the physical shield.
    {State g;Pose p=Surface(chest,view,50000);Update(g,p,true,true,p.at,true,t);
     assert(Judge(g,ahead,p.at,t)==Verdict::Block);
     for(float angle:{1.5707963f,3.1415926f,-1.5707963f}) {
         p.hand=framevr::math::Rotate(framevr::math::Yaw(angle),chest);p.at++;
         Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Block);
     }
     p.normal={1,0,0};p.at++;Update(g,p,true,true,p.at,true,t);
     Rejection why;assert(Judge(g,ahead,p.at,t,0,&why)==Verdict::Open&&why==Rejection::OutsideCoverage);
     p.normal={0,1,0};p.at++;Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Open);
     p.normal={0,0,1};p.at++;Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Block); // either face
     p.center.x=-.8f;p.at++;Update(g,p,true,true,p.at,true,t);
     assert(Judge(g,ahead,p.at,t,0,&why)==Verdict::Open&&why==Rejection::OutsidePlacement);
    }
    // Same sample must age even if the UI polls it repeatedly. Tracking loss
    // opens immediately even when no new rendered pose arrives.
    {State g;const Pose p=Surface(chest,view,60000);Update(g,p,true,true,p.at,true,t);
     Update(g,p,true,true,p.at+t.staleMs+1,true,t);assert(Judge(g,ahead,p.at+t.staleMs+1,t)==Verdict::Open);
     Update(g,p,true,true,p.at,true,t);Update(g,p,false,true,p.at+1,false,t);assert(!g.raised);
     auto invalid=p;invalid.normal={NAN,0,0};Update(g,invalid,true,true,p.at,true,t);assert(!g.raised);
     invalid=p;invalid.at++;Update(g,invalid,true,true,p.at,true,t);assert(!g.raised);
    }
    // Attachment conversion: large world offsets, calibration scale, mirrored
    // axes and character yaw preserve the same physical coverage in metres.
    for(float units:{2.f,4.f,8.f})for(float yaw:{0.f,1.5707963f,3.1415926f})for(float mirror:{-1.f,1.f}) {
        const auto q=framevr::math::Yaw(yaw);
        const Vec r=framevr::math::Rotate(q,{1,0,0}),u{0,1,0},b=framevr::math::Rotate(q,{0,0,1});
        constexpr float scale=.93f;
        float model[16]={r.x*scale,r.y*scale,r.z*scale,0,u.x*scale,u.y*scale,u.z*scale,0,b.x*scale,b.y*scale,b.z*scale,0,23000,50,-18000,1};
        const Vec eye{.1f,1.7f,-.05f}; // include calibrated camera/body offset
        const Vec center=eye+chest;
        const Vec w=framevr::math::Rotate(q,center)*(units*scale);
        float world[16]={r.x*mirror,r.y*mirror,r.z*mirror,0,0,1,0,0,b.x,b.y,b.z,0,23000+w.x,50+w.y,-18000+w.z,1};
        Pose p;assert(FromAttachment(world,model,eye*units,chest,units,70000,9,p));
        const Vec expected=framevr::math::Rotate(q,center);
        assert(std::sqrt(framevr::math::Dot(p.center-expected,p.center-expected))<.002f);
        assert(std::fabs(p.unitsPerMetre-units*scale)<.0001f);
        State g;Update(g,p,true,true,p.at,true,t);
        assert(Judge(g,framevr::math::Rotate(q,ahead+Vec{eye.x,0,eye.z}),p.at,t)==Verdict::Block);
        world[0]=world[1]=world[2]=0;assert(!FromAttachment(world,model,eye*units,chest,units,70000,9,p));
    }
    // Fully evaluated item geometry: wrist and item attachment may disagree by
    // 90 degrees. Coverage must use the drawn face, including mirrored transforms.
    for(float units:{2.f,4.f,8.f})for(float yaw:{0.f,1.5707963f,3.1415926f})for(float mirror:{-1.f,1.f}) {
        auto context=Surface(chest,{1,0,0},80000);context.unitsPerMetre=units;
        const auto q=framevr::math::Yaw(yaw);
        const Vec r=framevr::math::Rotate(q,{1,0,0}),u{0,1,0},z=framevr::math::Rotate(q,{0,0,1});
        const Vec eye=framevr::math::Rotate(q,{.1f,1.7f,.05f}),center=eye+framevr::math::Rotate(q,{-.1f,-.45f,-.3f});
        context.eye=eye;context.center=center;
        const Vec player{12300,24,-11000};const Vec origin=player+center*units;
        float world[16]={r.x*units*mirror,r.y*units*mirror,r.z*units*mirror,0,
            u.x*units,u.y*units,u.z*units,0,z.x*units,z.y*units,z.z*units,0,origin.x,origin.y,origin.z,1};
        Pose p;assert(FromBounds(world,{-.25f,-.5f,-.04f},{.25f,.5f,.04f},player,context,p));
        assert(Measured(p)&&std::sqrt(framevr::math::Dot(p.center-center,p.center-center))<.002f);
        assert(std::fabs(framevr::math::Dot(p.normal,z))>.999f);
        State g;Update(g,p,true,true,p.at,true,t);
        const auto attack=framevr::math::Rotate(q,{.1f,0,-1.5f});
        assert(Judge(g,attack,p.at,t)==Verdict::Block);
        // Physically rotate the item, not the controller/hero attachment.
        const Vec edge=framevr::math::Rotate(q,{0,0,1});
        world[0]=edge.x*units;world[1]=edge.y*units;world[2]=edge.z*units;
        world[8]=r.x*units;world[9]=r.y*units;world[10]=r.z*units;
        assert(FromBounds(world,{-.25f,-.5f,-.04f},{.25f,.5f,.04f},player,context,p));
        Update(g,p,true,true,p.at,true,t);assert(Judge(g,attack,p.at,t)==Verdict::Open);
        world[0]=NAN;assert(!FromBounds(world,{-.25f,-.5f,-.04f},{.25f,.5f,.04f},player,context,p));
    }
    {
        Pose p=Surface({-.1f,-.72f,-.3f},view,90000);
        p.halfRight={.3f,0,0};p.halfUp={0,.6f,0}; // large face still covers torso
        State g;Update(g,p,true,true,p.at,true,t);assert(g.raised&&Judge(g,ahead,p.at,t)==Verdict::Block);
        p.halfRight={.13f,0,0};p.halfUp={0,.13f,0};p.at++;
        g={};Update(g,p,true,true,p.at,true,t);assert(!g.raised); // low buckler exposes torso
        p=Surface(chest,view,90002);p.center.x=.39f;p.halfRight={.13f,0,0};p.halfUp={0,.13f,0};
        g={};Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Open);
        p.halfRight={.3f,0,0};p.halfUp={0,.6f,0};p.at++;
        Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Block);
        // A tilted face that still covers the torso is not discarded at 60 degrees.
        p.center.x=0;p.normal={.916515f,0,.4f};p.halfRight={.12f,0,-.2749545f};p.at++;
        Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Block);
        p.center.z=.6f;p.at++;Update(g,p,true,true,p.at,true,t);assert(Judge(g,ahead,p.at,t)==Verdict::Open);
        p.center.z=-.3f;p.center.y=.3f;p.at++;Update(g,p,true,true,p.at,true,t);assert(!g.raised);
        p.center.y=2.6f;p.at++;Update(g,p,true,true,p.at,true,t);assert(!g.raised);
        assert(ShieldMesh("armor_bucklerShape")&&ShieldMesh("armor_aerin_shield1Shape")&&!ShieldMesh("weapon_swordShape"));
        const Pose context=Surface(chest,view,91000);Pose out;
        float world[16]={4,0,0,0,0,4,0,0,0,0,4,0,-.48f,5.4f,-1,1};
        assert(FromBounds(world,{-.2f,-.4f,-.03f},{.2f,.4f,.03f},{},context,out));
        assert(!FromBounds(world,{-.2f,-.4f,-.03f},{-.3f,.4f,.03f},{},context,out)); // reversed bounds
        assert(!FromBounds(world,{-.2f,-.2f,-.2f},{.2f,.2f,.2f},{},context,out)); // no broad face
        world[0]=world[5]=world[10]=.0001f;
        assert(!FromBounds(world,{-.2f,-.4f,-.03f},{.2f,.4f,.03f},{},context,out)); // hidden attachment
        world[0]=world[5]=world[10]=400;
        assert(!FromBounds(world,{-.2f,-.4f,-.03f},{.2f,.4f,.03f},{},context,out)); // implausible scale
        world[0]=world[5]=world[10]=4;world[12]=300;
        assert(!FromBounds(world,{-.2f,-.4f,-.03f},{.2f,.4f,.03f},{},context,out)); // another item/location
    }
    {
        Debug d;d.pose=Surface(chest,view,100000);d.raised=true;
        d.pose.player={10000,20,-20000};d.pose.halfRight={.3f,0,0};d.pose.halfUp={0,.6f,0};
        const Vec camera=d.pose.player+d.pose.eye*d.pose.unitsPerMetre;
        DebugDraw a,b;assert(DrawDebug(d,camera,100001,a));
        assert(std::abs(a.center.y-(d.pose.center.y-d.pose.eye.y)*4)<.001f&&std::abs(a.right.x-1.2f)<.001f);
        assert(a.color.y==1&&a.color.z==1&&!a.hit);
        // Translation of both player and camera cannot make the wireframe drift.
        d.pose.player=d.pose.player+Vec{3000,100,-2000};
        assert(DrawDebug(d,camera+Vec{3000,100,-2000},100001,b));
        assert(std::sqrt(framevr::math::Dot(a.center-b.center,a.center-b.center))<.003f);
        d.pose.halfRight=d.pose.halfUp={};assert(DrawDebug(d,camera,100001,b));
        assert(b.color.x==1&&b.color.y<1&&std::abs(framevr::math::Dot(b.right,d.pose.normal))<.001f);
        d.raised=false;assert(DrawDebug(d,camera,100001,b)&&b.color.x==.5f&&b.color.y==.5f);
        d.hitAt=100000;d.hitFrom=camera+Vec{0,0,-2};d.hitTo=camera;
        for(int hit:{1,2,3}) {
            d.hit=hit;assert(DrawDebug(d,camera,100100,b)&&b.hit&&b.hitFrom.z==-2);
            assert(hit==1?b.color.y==1:hit==2?b.color.x==1&&b.color.z==.1f:b.color.z==1);
        }
        d.pose.at=100700;assert(DrawDebug(d,camera,100700,b)&&!b.hit);
        assert(!DrawDebug(d,camera,100699,b)&&!DrawDebug(d,camera,100851,b));
        assert(!DrawDebug(d,{NAN,0,0},100700,b));
        d.pose.player.x=NAN;assert(!DrawDebug(d,camera,100700,b));
    }
    {   // 2026-10-20: Holy Shield's glow: coverage x1.6 about the shield's center, mesh or fallback
        const Tuning t;
        assert(CoverScale(false)==1&&CoverScale(true)==HolyShieldCover&&HolyShieldCover>1&&HolyShieldCover<=MaxCoverScale);
        bool listed=false;for(const auto& d:framevr::effects::Definitions)listed|=d.state==framevr::effects::HolyShieldState&&d.skill==117;
        assert(listed);
        Pose p=Surface({-.1f,-.45f,-.3f},view,95000);p.center.x=.3f;p.halfRight={.13f,0,0};p.halfUp={0,.13f,0};   // a buckler off to the side
        assert(Coverage(p,ahead,t)==Rejection::OutsidePlacement);
        p.coverScale=CoverScale(true);assert(Coverage(p,ahead,t)==Rejection::None);
        p.center.x=.6f;assert(Coverage(p,ahead,t)==Rejection::OutsidePlacement);   // the glow has an edge too
        Pose f=Surface({-.1f,-.45f,-.3f},view,95001);f.center.x=.6f;   // no mesh: the fallback grows the same way
        assert(Coverage(f,ahead,t)==Rejection::OutsidePlacement);
        f.coverScale=CoverScale(true);assert(Coverage(f,ahead,t)==Rejection::None);
        f.center.x=.8f;assert(Coverage(f,ahead,t)==Rejection::OutsidePlacement);
        for(const float bad:{NAN,.5f,4.f}){Pose b=p;b.coverScale=bad;assert(!Valid(b)&&Coverage(b,ahead,t)==Rejection::InvalidSurface);}
        Pose turned=p;turned.normal={1,0,0};turned.coverScale=CoverScale(true);turned.center.x=0;
        turned.halfRight={0,0,.13f};assert(Coverage(turned,ahead,t)==Rejection::OutsideCoverage);   // edge-on stays open
        // Only the width grows: a shield held too low (or high) to count as raised stays so under Holy Shield.
        Pose low=p;low.center.x=0;low.center.y=low.eye.y-.50f-.13f-.04f;
        assert(Measured(low)&&PositionRejection(low,true,false,t)==Rejection::TooLow);
        low.coverScale=CoverScale(true);assert(PositionRejection(low,true,false,t)==Rejection::TooLow);
        Pose high=p;high.center.x=0;high.center.y=high.eye.y+.15f+.13f+.04f;high.coverScale=CoverScale(true);
        assert(PositionRejection(high,true,false,t)==Rejection::TooHigh);
    }
    std::puts("Shield guard: rendered bounds, coverage, calibration, movement, parries, debug placement and expiry passed");
}
