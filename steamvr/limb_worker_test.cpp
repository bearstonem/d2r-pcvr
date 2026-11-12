#include "limb_worker.h"
#include <cassert>
#include <cstdio>

// The bodies' worker (limb_worker.h) without the game: frames handed over and
// stepped on its thread, latest only, results handed back once, reset and
// stop / start again.
using namespace framevr;
using namespace framevr::limbs;
using math::Vec;using math::Quat;
namespace {
constexpr float Pi=3.14159265f;
Quat About(Vec axis,float angle) {const Vec n=math::Normalized(axis);const float s=std::sin(angle*.5f);return {n.x*s,n.y*s,n.z*s,std::cos(angle*.5f)};}
// limb_physics_test.cpp's body: a torso and an arm hanging from its shoulder.
Rig Body(uint32_t entity=0x100042) {
    Rig r;r.entity=entity;r.kind=Kind::Monster;r.layout=7;r.count=2;
    const Vec where{0,0,2};
    auto& torso=r.limbs[0];
    torso.joint=where+Vec{0,1,0};torso.a=where+Vec{0,1.1f,0};torso.b=where+Vec{0,1.5f,0};torso.radius=.15f;
    auto& arm=r.limbs[1];
    arm.joint=where+Vec{.25f,1.5f,0};arm.q=About({0,0,1},-Pi/2);
    arm.a=where+Vec{.25f,1.45f,0};arm.b=where+Vec{.25f,.95f,0};arm.radius=.05f;
    r.parent[0]=-1;r.parent[1]=0;
    return r;
}
std::array<Weapon,Weapons> Blade(Vec base,Vec tip) {std::array<Weapon,Weapons> w{};w[0]={true,base,tip,.03f};return w;}
const auto Beside=Blade({.25f,1.2f,2.17f},{.25f,1.2f,2.9f});   // 10 cm off the arm
Tuning Lively() {Tuning t;t.value={2.5f,2,.4f,600,20,40,3,69,30,300};return t;}

// What the worker published and when it was held, seen from the test's thread.
struct Seen {
    std::mutex mutex;std::condition_variable changed;
    unsigned published=0,answers=0;uint64_t stamp=0;    // batches published, the last one's answers and stamp
    unsigned begun=0;double time=0;bool strike=false;   // batches begun, the last one's time and right-hand hit
    bool hold=false;                                    // the next batch waits here until let go
    void Publish(unsigned n,uint64_t now) {std::lock_guard lock(mutex);++published;answers=n;stamp=now;changed.notify_all();}
    void Began(const StepInput& in) {
        std::unique_lock lock(mutex);
        ++begun;time=in.time;strike=in.strike[0];changed.notify_all();
        changed.wait(lock,[&]{return !hold;});
    }
    template<class F> void Until(F done) {
        std::unique_lock lock(mutex);
        const bool ok=changed.wait_for(lock,std::chrono::seconds(20),done);
        assert(ok);(void)ok;
    }
    void Published(unsigned n) {Until([&]{return published>=n;});}
    void Begun(unsigned n) {Until([&]{return begun>=n;});}
    void Hold(bool on) {std::lock_guard lock(mutex);hold=on;changed.notify_all();}
};
// One display frame at 90 Hz, as the frame thread hands it over; a living
// body's pose is new each frame and its hips rise or sink 4 mm.
struct Frames {
    double t=0;uint64_t now=1000;int ticks=0;
    StepInput Next(bool body,const std::array<Weapon,Weapons>& hands) {
        StepInput in;in.tuning=Lively();t+=1/90.;now+=11;in.time=t;in.now=now;in.hands=hands;
        if(body) {
            Rig r=Body();r.at=uint64_t(++ticks);
            const Vec breath{0,ticks&1?.004f:0,0};
            for(int i=0;i<r.count;++i){auto& l=r.limbs[size_t(i)];l.joint=l.joint+breath;l.a=l.a+breath;l.b=l.b+breath;}
            in.rigs[0]=r;in.count=1;
        }
        return in;
    }
};
}
int main() {
    Seen seen;
    Stepper stepper([&](const Reaction* r,unsigned n,uint64_t now){for(unsigned i=0;i<n;++i)assert(r[i].at==now&&r[i].entity==0x100042);seen.Publish(n,now);},
        [&](const StepInput& in){seen.Began(in);});
    Frames frames;StepResults got;unsigned sent=0;
    // One frame handed over, stepped and published before the next: every frame is a batch.
    auto step=[&](bool body,const std::array<Weapon,Weapons>& hands,bool strike=false) {
        auto in=frames.Next(body,hands);in.strike[0]=strike;
        assert(stepper.Submit(in));seen.Published(++sent);
        assert(seen.stamp==in.now);
    };

    // Not started: nothing is taken in, nothing comes back.
    assert(!stepper.Submit(frames.Next(true,Beside))&&!stepper.Take(got)&&!stepper.Active());
    stepper.Reset();stepper.Stop();   // both harmless before a start
    assert(stepper.Start()&&stepper.Start());

    // No body near: a batch with no world, no answers.
    step(false,{});
    assert(stepper.Take(got)&&got.batches==1&&got.ready&&got.simulated==0&&seen.answers==0);
    // The worker settles to idle once nothing waits and nothing is simulated.
    for(int i=0;i<2000&&stepper.Active();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(!stepper.Active());

    // A blade beside the arm: a ragdoll, its answer published each batch.
    for(int i=0;i<20;++i)step(true,Beside);
    assert(stepper.Active());
    assert(stepper.Take(got)&&got.batches==20&&got.replaced==0&&got.simulated==1&&got.most==1&&seen.answers==1);
    assert(got.micros>0&&got.longest>0&&got.longest<=got.micros);
    // Handed back once: a second Take has no batches, sums or touches, and still knows the world.
    assert(!stepper.Take(got)&&got.batches==0&&got.micros==0&&got.touched==0&&got.strikes==0&&got.simulated==1);

    // Sums are kept across batches until taken: a blade swept through the
    // arm touches it in some of 36 batches; one Take has them all, the next none.
    auto across=[](float z){return Blade({.2f,1.2f,2+z},{.5f,1.2f,2+z});};
    for(int i=0;i<30;++i)step(true,across(-.6f));
    assert(stepper.Take(got)&&got.batches==30);
    for(int i=0;i<36;++i)step(true,across(-.6f+1.2f*float(i)/35));   // 3 m/s
    assert(stepper.Take(got)&&got.batches==36&&got.touched>0&&got.touches[0].entity==0x100042&&got.touches[0].slot==0);
    std::printf("limb worker: 36 batches, %u touches, mean %.3f ms, longest %.3f ms\n",got.touched,double(got.micros)/36000.0,double(got.longest)/1000.0);
    assert(!stepper.Take(got)&&got.touched==0);
    for(int i=0;i<20;++i)step(true,Beside);
    assert(stepper.Take(got)&&got.simulated==1);

    // Latest only: while a batch is being stepped, two more frames arrive.
    // The second takes the first's place - with the hit the first carried -
    // and is the only one stepped; nothing queues.
    {
        const unsigned begun=seen.begun;
        seen.Hold(true);
        assert(stepper.Submit(frames.Next(true,Beside)));
        seen.Begun(begun+1);                         // held on the worker
        auto hit=frames.Next(true,Beside);hit.strike[0]=true;
        assert(stepper.Submit(hit));                 // waits
        const auto last=frames.Next(true,Beside);
        assert(stepper.Submit(last));                // takes its place
        seen.Hold(false);
        sent+=2;seen.Published(sent);
        assert(seen.begun==begun+2&&seen.time==last.time&&seen.strike&&seen.stamp==last.now);
        assert(stepper.Take(got)&&got.batches==2&&got.replaced==1&&got.strikes==1&&got.simulated==1);
        // The clock is the frames' own: the replaced frame cost no simulated time, and the body is still held.
        step(true,Beside);
        assert(stepper.Take(got)&&got.batches==1&&got.replaced==0&&got.strikes==0&&got.simulated==1&&seen.answers==1);
    }

    // Reset while a batch is being stepped: that batch is not published and
    // not counted, the frame waiting is dropped, and the next frame finds a
    // world with no bodies (a few frames beside a blade wake it again).
    {
        const unsigned begun=seen.begun,published=seen.published;
        seen.Hold(true);
        assert(stepper.Submit(frames.Next(true,Beside)));
        seen.Begun(begun+1);
        assert(stepper.Submit(frames.Next(true,Beside)));
        stepper.Reset();                             // returns at once, the worker still held
        assert(!stepper.Take(got)&&got.simulated==0&&got.replaced==0);
        seen.Hold(false);
        step(false,{});                              // the first batch after it
        assert(seen.published==published+1&&seen.begun==begun+2&&seen.answers==0);
        assert(stepper.Take(got)&&got.batches==1&&got.simulated==0);
        stepper.Reset();stepper.Reset();             // nothing handed over since: nothing to do
        for(int i=0;i<5;++i)step(true,Beside);
        assert(stepper.Take(got)&&got.batches==5&&got.simulated==1);
    }

    // Stop with a batch being stepped: joined, that batch never published;
    // then nothing is taken in until it is started again, with a new world.
    {
        const unsigned begun=seen.begun,published=seen.published;
        seen.Hold(true);
        assert(stepper.Submit(frames.Next(true,Beside)));
        seen.Begun(begun+1);
        std::thread release([&]{std::this_thread::sleep_for(std::chrono::milliseconds(30));seen.Hold(false);});
        stepper.Stop();
        release.join();
        assert(seen.published==published&&!stepper.Active());
        assert(!stepper.Submit(frames.Next(true,Beside))&&!stepper.Take(got)&&got.simulated==0);
        assert(stepper.Start());
        step(false,{});
        assert(stepper.Take(got)&&got.batches==1&&got.simulated==0);
        for(int i=0;i<20;++i)step(true,Beside);
        assert(stepper.Take(got)&&got.batches==20&&got.simulated==1&&seen.answers==1);
    }
    // Many stops and starts, some with work waiting: no hang, no loss of the thread.
    for(int i=0;i<50;++i) {
        assert(stepper.Start());
        if(i&1){assert(stepper.Submit(frames.Next(true,Beside)));}
        stepper.Stop();
    }
    sent=seen.published;
    assert(stepper.Start());
    step(true,Beside);
    assert(stepper.Take(got)&&got.batches==1);
    stepper.Stop();
    std::puts("limb worker: all checks passed");
    return 0;
}
