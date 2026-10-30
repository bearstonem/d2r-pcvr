#include "../vr/combat_rules.h"
#include "attack_speed.h"
#undef NDEBUG
#include <cassert>
#include <cstdio>

int main() {
    using namespace combat;
    assert(PlayerContact(0,0,0,1)); // id zero is a valid player
    assert(PlayerContact(42,0,42,1));
    assert(!PlayerContact(UINT32_MAX,0,UINT32_MAX,1));
    assert(!PlayerContact(42,1,42,0)); // incoming attacks
    assert(!PlayerContact(42,1,42,1)); // summons/monsters with overlapping ids
    assert(!PlayerContact(42,0,41,1)); // another player
    assert(!PlayerContact(42,0,42,0)); // PvP
    assert(!PlayerContact(42,3,42,1)); // projectile must resolve its native owner
    assert(!PlayerContact(42,0,42,2)); // scenery is not a damage target
    assert(IncomingContact(42,1,0,42));
    assert(IncomingContact(0,1,0,0));
    assert(!IncomingContact(42,1,0,41)); // another player is struck
    assert(!IncomingContact(42,0,0,42)); // PvP is not a guard
    assert(!IncomingContact(UINT32_MAX,1,0,UINT32_MAX));
    assert(!IncomingContact(42,3,0,42)); // missiles resolve their native owner first
    for(int rate=1;rate<=75;++rate) {
        assert(AdvanceSeed(GuardRoll(true)).low%100<uint32_t(rate));
        assert(!(AdvanceSeed(GuardRoll(false)).low%100<uint32_t(rate)));
    }
    // The native stream after the intercepted roll must be identical, so the
    // following damage/proc roll is unchanged. Exercise carries and overflow.
    Seed seed{0xffffffffu,0xffffffffu};
    for(int i=0;i<10000;++i) {
        const uint64_t native=uint64_t(seed.low)*0x6ac690c5u+seed.high;
        const Seed advanced=AdvanceSeed(seed);
        assert(advanced.low==uint32_t(native)&&advanced.high==uint32_t(native>>32));
        const auto nextNative=uint64_t(uint32_t(native))*0x6ac690c5u+uint32_t(native>>32);
        assert(AdvanceSeed(advanced).low==uint32_t(nextNative));
        for(int chance=5;chance<=95;++chance)assert(AdvanceSeed({}).low%100<uint32_t(chance));
        seed=advanced;
    }
    // Unleashed swing damage (compared with a tolerance: float maths may contract).
    const auto near=[](float a,float b){return a>b-1e-4f&&a<b+1e-4f;};
    // Attack rating: raw chance by default, a floor lifts the bottom only.
    assert(near(ArFactor(95,0),.95f)&&near(ArFactor(5,0),.05f)&&near(ArFactor(50,0),.5f));
    assert(near(ArFactor(0,.25f),.25f)&&near(ArFactor(100,.25f),1)&&near(ArFactor(50,.5f),.75f));
    assert(near(ArFactor(-3,0),0)&&near(ArFactor(140,0),1)&&near(ArFactor(50,-1),.5f)&&near(ArFactor(50,2),1));
    // Incoming defense: the raw native chance, no floor (5% chance = 5% of the hit).
    assert(near(DefenseFactor(5),.05f)&&near(DefenseFactor(95),.95f)&&near(DefenseFactor(60),.6f));
    assert(near(DefenseFactor(-1),0)&&near(DefenseFactor(120),1));
    // Recovery, linear (curve 1).
    assert(RecoveryFactor(UINT64_MAX,600)==1.f); // first stroke
    assert(RecoveryFactor(300,0)==1.f);          // unknown cycle never penalizes
    assert(near(RecoveryFactor(0,600),0)&&near(RecoveryFactor(150,600),.25f)&&near(RecoveryFactor(300,600),.5f));
    assert(RecoveryFactor(600,600)==1.f&&RecoveryFactor(5000,600)==1.f);
    // Curve 2: flailing at twice the rate lands half the damage per second.
    assert(near(RecoveryFactor(300,600,2),.25f)&&near(2*RecoveryFactor(300,600,2),.5f));
    assert(near(RecoveryFactor(150,600,2),.0625f)&&RecoveryFactor(600,600,2)==1.f&&RecoveryFactor(UINT64_MAX,600,2)==1.f);
    assert(RecoveryFactor(300,0,2)==1.f);
    // Grace: from that share of the cycle a stroke is fully recovered.
    assert(RecoveryFactor(520,600,2,.85f)==1.f&&near(RecoveryFactor(510,600,2,.85f),1)&&near(RecoveryFactor(255,600,2,.85f),.25f));
    assert(near(RecoveryFactor(300,600,2,1),.25f)&&near(RecoveryFactor(300,600,2,0),.25f)); // out-of-range grace = none
    assert(near(RecoveryFactor(300,600,9),.5f));  // out-of-range curve falls back to linear
    // Cleave falloff: 100, 75, 50, 25 then the floor; no step = no falloff.
    assert(near(CleaveFactor(0,.25f,.25f),1)&&near(CleaveFactor(1,.25f,.25f),.75f)&&near(CleaveFactor(2,.25f,.25f),.5f));
    assert(near(CleaveFactor(3,.25f,.25f),.25f)&&near(CleaveFactor(9,.25f,.25f),.25f));
    assert(near(CleaveFactor(5,0,.25f),1)&&near(CleaveFactor(5,-1,.25f),1)&&near(CleaveFactor(4,.25f,0),0));
    // On-hit gate: a full-cycle stroke's first landed contact only.
    assert(OnHitGate(true,600,600)==OnHit::Allowed&&OnHitGate(true,5000,600)==OnHit::Allowed);           // at the breakpoint and after
    assert(OnHitGate(true,599,600)==OnHit::NotFullCycle&&OnHitGate(true,0,600)==OnHit::NotFullCycle);    // a stroke inside the cycle
    assert(OnHitGate(false,5000,600)==OnHit::CleaveFollowUp&&OnHitGate(false,UINT64_MAX,0)==OnHit::CleaveFollowUp); // the stroke's next creature
    assert(OnHitGate(true,UINT64_MAX,600)==OnHit::Allowed);   // the first stroke
    assert(OnHitGate(true,10,0)==OnHit::Allowed);             // an unknown cycle counts as full
    // Grace gives full damage early but never opens the gate before the full cycle.
    assert(RecoveryFactor(520,600,2,.85f)==1.f&&OnHitGate(true,520,600)==OnHit::NotFullCycle);
    {   // Trigger/button melee: a stroke on the swing clocks, never on another trigger hit.
        assert(StrokeGap(0,500)==UINT64_MAX);                       // no trigger hit yet
        assert(StrokeGap(1000,1250)==250&&StrokeGap(1250,1000)==250); // either order in one batch
        assert(StrokeGap(1000,1000)==0);
        const auto none=TriggerRecovery(UINT64_MAX,UINT64_MAX,600,true,2,1);   // trigger only: native pace, full
        assert(none.recovery==1.f&&none.onHit==OnHit::Allowed);
        const auto soon=TriggerRecovery(300,300,600,true,2,1);          // half a cycle after a full swing
        assert(near(soon.recovery,.25f)&&soon.onHit==OnHit::NotFullCycle);
        const auto full=TriggerRecovery(600,600,600,true,2,1);
        assert(full.recovery==1.f&&full.onHit==OnHit::Allowed);
        const auto off=TriggerRecovery(300,300,600,false,2,1);          // swing_recovery_damage 0: the gate still holds
        assert(off.recovery==1.f&&off.onHit==OnHit::NotFullCycle);
        assert(TriggerRecovery(10,10,0,true,2,1).recovery==1.f&&TriggerRecovery(10,10,0,true,2,1).onHit==OnHit::Allowed); // unknown cycle
        // A full cycle after the full swing, 100 ms after an in-between one: full (fixed cycle).
        const auto due=TriggerRecovery(650,100,600,true,2,1);
        assert(due.recovery==1.f&&due.onHit==OnHit::Allowed);
        // Alternating: full swing at 0, trigger hit at 300, swing at 600 (cycle 600). The trigger
        // is half recovered and in between; the second swing is a full cycle after the full
        // swing, so it is full (fixed cycle, 2026-10-30), although only 300 ms after the trigger.
        framevr::attackspeed::Cadence swing,swingFull;const uint64_t s=1;const uint32_t cycle=600;
        swing.Landed(s,1000,cycle);swingFull.Landed(s,1000,cycle);
        const auto mid=TriggerRecovery(swingFull.Since(s,1300),swing.Since(s,1300),cycle,true,2,1);
        assert(near(mid.recovery,.25f)&&mid.onHit==OnHit::NotFullCycle);   // in between: triggerFull not stamped
        const uint64_t trigger=1300;
        const uint64_t since=SwingSince(swing.Since(s,1600),trigger,1600);
        const uint64_t sinceFull=SwingSince(swingFull.Since(s,1600),0,1600);
        const auto second=FixedCycle(sinceFull,since,cycle,true,2,1);
        assert(since==300&&sinceFull==600&&second.full&&second.recovery==1.f&&OnHitGate(true,sinceFull,cycle)==OnHit::Allowed);
        // A second trigger hit 200 ms after the first, 500 ms after the swing: measured from
        // the swing ((500/600)^2), not from the trigger hit ((200/600)^2).
        assert(near(TriggerRecovery(swingFull.Since(s,1500),swing.Since(s,1500),cycle,true,2,1).recovery,.25f*100/36));
        // A swing sampled just before a trigger hit the batch already applied: the gap counts.
        assert(SwingSince(5000,1300,1260)==40&&SwingSince(20,1300,1600)==20);
        assert(SwingSince(UINT64_MAX,0,1600)==UINT64_MAX&&SwingSince(700,0,1600)==700);   // no trigger hit: its own clock
    }
    {   // A form's two claws share one recovery clock: of two landing together, one carries effects.
        framevr::attackspeed::Cadence clock;const uint64_t s=1;const uint32_t cycle=600;
        assert(OnHitGate(true,clock.Since(s,1000),cycle)==OnHit::Allowed);clock.Landed(s,1000,cycle);   // right claw
        assert(OnHitGate(true,clock.Since(s,1030),cycle)==OnHit::NotFullCycle);clock.Landed(s,1030,cycle); // left claw, its own stroke
        assert(OnHitGate(true,clock.Since(s,1400),cycle)==OnHit::NotFullCycle);                         // flailing
        assert(OnHitGate(true,clock.Since(s,1630),cycle)==OnHit::Allowed);                              // a full cycle after the last
        // Dual wield shares the claws' one clock (2026-10-30): the left blade 30 ms after a full
        // right-blade stroke is in between, and the next full one is a cycle after the right's.
        framevr::attackspeed::Cadence blades,bladesFull;
        auto blade=[&](uint64_t at,uint32_t own){const auto c=FixedCycle(bladesFull.Since(s,at),blades.Since(s,at),own,true,2,1);blades.Landed(s,at,own);if(c.full)bladesFull.Landed(s,at,own);return c.full;};
        assert(blade(1000,cycle)&&!blade(1030,cycle)&&!blade(1400,720)&&blade(1600,cycle)&&!blade(1610,720));
        assert(blade(2320,720));   // the left blade's own (slower) cycle after the right's full hit
    }
    {   // Fixed attack cycle (Cal, 2026-10-30): in-between strokes never move the next breakpoint.
        // Cycle 720 ms, curve 2. Flailing every 300 ms: full at 0, in between at 300 and 600
        // (each measured from the previous stroke: (300/720)^2), full again at 900.
        const uint32_t cycle=720;const uint64_t s=1;
        framevr::attackspeed::Cadence any,full;
        auto stroke=[&](uint64_t at){
            const auto c=FixedCycle(full.Since(s,at),any.Since(s,at),cycle,true,2,1);
            any.Landed(s,at,cycle);if(c.full)full.Landed(s,at,cycle);
            return c;
        };
        const auto a0=stroke(1000);assert(a0.full&&a0.recovery==1.f);   // the first stroke
        const auto a1=stroke(1300);assert(!a1.full&&near(a1.recovery,(300.f/720)*(300.f/720)));
        const auto a2=stroke(1600);assert(!a2.full&&near(a2.recovery,(300.f/720)*(300.f/720)));   // from the previous stroke
        const auto a3=stroke(1900);assert(a3.full&&a3.recovery==1.f);   // 900 ms after the full one: the breakpoint was never delayed
        // A patient player: every 720 ms, all full.
        framevr::attackspeed::Cadence any2,full2;
        for(uint64_t at=5000;at<9000;at+=720){const auto c=FixedCycle(full2.Since(s,at),any2.Since(s,at),cycle,true,2,1);assert(c.full);any2.Landed(s,at,cycle);full2.Landed(s,at,cycle);}
        // One extra swing at half a cycle: +25% damage per cycle (0.5^2), the most any extra swing earns.
        assert(near(FixedCycle(360,360,cycle,true,2,1).recovery,.25f));
        // Very fast flailing (100 ms): each extra swing (100/720)^2 ~ 2%.
        assert(near(FixedCycle(500,100,cycle,true,2,1).recovery,(100.f/720)*(100.f/720)));
        // swing_recovery_damage off: in-between strokes deal full damage but still never move the cycle.
        const auto off=FixedCycle(300,300,cycle,false,2,1);assert(!off.full&&off.recovery==1.f);
        // Unknown cycle / no full stroke yet: full.
        assert(FixedCycle(10,10,0,true,2,1).full&&FixedCycle(UINT64_MAX,10,cycle,true,2,1).full);
        // Exactly at the breakpoint: full; grace never makes an in-between stroke full.
        assert(FixedCycle(720,50,cycle,true,2,1).full&&!FixedCycle(719,719,cycle,true,2,.5f).full&&FixedCycle(719,719,cycle,true,2,.5f).recovery==1.f);
        // A shapeshift form's claws share the clocks: right claw full at 1000, left claw 30 ms later
        // in between, a flail at 1400 in between; full again at 1600 (600 ms cycle) - not 1630.
        framevr::attackspeed::Cadence clawAny,clawFull;const uint32_t form=600;
        auto claw=[&](uint64_t at){const auto c=FixedCycle(clawFull.Since(s,at),clawAny.Since(s,at),form,true,2,1);clawAny.Landed(s,at,form);if(c.full)clawFull.Landed(s,at,form);return c.full;};
        assert(claw(1000)&&!claw(1030)&&!claw(1400)&&claw(1600));
    }
    // A gated critical/deadly strike roll reads 99: under no chance from 0 to 99.
    assert(AdvanceSeed(NoCritRoll()).low%100==99);
    for(int chance=0;chance<=99;++chance)assert(!(AdvanceSeed(NoCritRoll()).low%100<uint32_t(chance)));
    // Would-have-hit gate: the roll is the game's own (advanced low % 100), hit when roll < chance.
    {
        Seed s{0x12345678u,0x9abcdef0u};
        for(int i=0;i<1000;++i){assert(NativeHitRoll(s)==int(AdvanceSeed(s).low%100));s=AdvanceSeed(s);}
        assert(WouldHit(4,5)&&!WouldHit(5,5)&&WouldHit(94,95)&&!WouldHit(95,95)&&!WouldHit(99,95));   // the clamp ends
        assert(WouldHit(-1,50)&&WouldHit(50,-1)&&WouldHit(-1,-1));   // unknown = native effects
        // Over the real stream, the would-hit share is the chance: effects match native on average.
        for(int chance:{5,30,61,95}) {
            Seed r{0xdeadbeefu,0x1234u};int hits=0;const int n=200000;
            for(int i=0;i<n;++i){hits+=WouldHit(NativeHitRoll(r),chance);r=AdvanceSeed(r);}
            const double share=double(hits)/n;
            assert(share>chance/100.-.01&&share<chance/100.+.01);
        }
        // Damage over time follows the hit's factor; a positive rate never rounds to nothing.
        assert(ScaleDamageOverTime(400,.25)==100&&ScaleDamageOverTime(50,.05)==3&&ScaleDamageOverTime(16,.3)==5);
        assert(ScaleDamageOverTime(10,.01)==1&&ScaleDamageOverTime(10,0)==0&&ScaleDamageOverTime(10,1)==10);
        assert(ScaleDamageOverTime(0,.5)==0&&ScaleDamageOverTime(-3,.5)==-3&&ScaleDamageOverTime(100,1.5)==150);
    }
    std::puts("Combat rules: local ownership, guaranteed accuracy, subsequent random-stream, swing damage, on-hit and would-hit gate tests passed");
}
