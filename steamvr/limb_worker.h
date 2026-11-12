#pragma once
#include "limb_board.h"
#include "limb_physics.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

// Bodies that give way, stepped off the frame thread (2026-11-10). The frame
// thread is the game's draw thread and the bottleneck; the bodies' world cost
// it 0.3-0.5 ms a pair in a big fight. One worker thread owns the world: it
// makes it, steps it and destroys it, and nothing else touches it.
//
// Each frame the frame thread hands over that frame's inputs (Submit: the
// time, the player, the weapons, the bodies as posed, the hits the game
// counted) and picks up what finished since it last asked (Take). The worker
// gives each batch's answers to `publish` the moment its steps end - the
// board the skeleton hook reads - so a pose made later in the same frame is
// bent by this frame's steps, as when the frame thread stepped; one made in
// the half millisecond before the batch ends is bent by the frame before's
// (an answer is an offset from the animation, carried by whatever pose it is
// applied to). The hand's tick and the counters are picked up at the next
// frame's input: one frame later than before.
//
// Latest only: a frame handed over while the worker is still stepping takes
// the place of the one waiting; nothing queues. The world's fixed steps are
// counted from the times handed over (World::Advance), so a frame replaced
// costs no simulated time. A replaced frame's hits pass to the one that takes
// its place.
//
// Reset (a new session, tracking lost, bodies turned off) and Stop never run
// the world on the caller's thread: Reset posts and returns; Stop joins. The
// worker takes only this mailbox's lock and whatever `publish` takes, so it
// can be joined while the caller holds any other lock. `publish` runs with
// the mailbox locked (a batch stepped before a Reset is never published
// after it): keep it short, and never call back into the Stepper from it.
namespace framevr::limbs {
struct StepInput {
    Tuning tuning{};
    double time=0;                       // seconds, the frame's
    Vec player{};
    std::array<Weapon,Weapons> hands{};
    std::array<Rig,MaxPosed> rigs;       // the first `count`
    unsigned count=0;
    uint64_t now=0;                      // ms: stamps the answers
    std::array<bool,2> strike{};         // a hit the game counted, by hand
};
// What finished since the last Take. The sums and touches are handed over
// once; simulated, lifeless and ready are the world's as last stepped.
struct StepResults {
    bool ready=true;                     // false: Box3D gave no world
    unsigned simulated=0,lifeless=0;
    unsigned most=0,slips=0,strikes=0;
    std::array<Touch,MaxTouches> touches{};unsigned touched=0;
    unsigned batches=0,replaced=0;       // batches stepped; frames replaced before they were
    uint64_t micros=0,longest=0;         // the batches' time: in all, the longest one
};
class Stepper {
public:
    using Publish=std::function<void(const Reaction*,unsigned,uint64_t now)>;
    using Began=std::function<void(const StepInput&)>;
private:
    std::mutex life;                     // Start and Stop, one at a time
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::unique_ptr<StepInput> pending=std::make_unique<StepInput>();
    bool has=false,reset=false,stop=true,used=false;
    uint64_t epoch=0;                    // a batch begun under another is dropped
    StepResults results;
    std::atomic<bool> active{false};
    Publish publish;
    Began began;                         // the tests': on the worker, before each batch, unlocked
    static void Copy(StepInput& to,const StepInput& from) {
        to.tuning=from.tuning;to.time=from.time;to.player=from.player;to.hands=from.hands;
        to.count=std::min<unsigned>(from.count,MaxPosed);to.now=from.now;to.strike=from.strike;
        std::copy_n(from.rigs.begin(),to.count,to.rigs.begin());
    }
    void Run() {
        std::unique_ptr<World> world;    // made, stepped and destroyed on this thread only
        const auto job=std::make_unique<StepInput>();
        for(;;) {
            bool clear=false,work=false;uint64_t mine=0;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock,[&]{return stop||reset||has;});
                if(stop)break;
                clear=std::exchange(reset,false);
                if(has){Copy(*job,*pending);has=false;work=true;}
                mine=epoch;
                if(!work)active.store(false,std::memory_order_release);
            }
            if(clear&&world)world->Reset();
            if(!work)continue;
            if(began)began(*job);
            const auto start=std::chrono::steady_clock::now();
            StepResults out;std::array<Reaction,MaxBodies> answers;unsigned answered=0;
            if(!world&&job->count)world=std::make_unique<World>();   // the first body near: as before, no world until then
            if(world&&!world->Ready())out.ready=false;
            else if(world) {
                world->Tune(job->tuning);
                world->Bodies(job->rigs.data(),job->count,job->time);
                world->Hands(job->hands);
                world->Advance(job->time,job->player);
                for(int hand=0;hand<2;++hand)if(job->strike[size_t(hand)]&&world->Strike(hand))++out.strikes;
                out.slips=world->Slips();
                out.simulated=out.most=world->Simulated();out.lifeless=world->Lifeless();
                answered=world->Reactions(answers,job->now);
                out.touched=world->Touches(out.touches);
            }
            const auto took=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
            std::lock_guard lock(mutex);
            if(stop)break;
            if(mine!=epoch)continue;     // reset meanwhile: these answers are for a world that is to go
            publish(answers.data(),answered,job->now);
            auto& r=results;
            r.ready=out.ready;r.simulated=out.simulated;r.lifeless=out.lifeless;
            r.most=std::max(r.most,out.most);r.slips+=out.slips;r.strikes+=out.strikes;
            for(unsigned i=0;i<out.touched&&r.touched<MaxTouches;++i)r.touches[r.touched++]=out.touches[i];
            ++r.batches;r.micros+=took;r.longest=std::max(r.longest,took);
            active.store(has||reset||out.simulated>0,std::memory_order_release);
        }
        world.reset();
    }
public:
    explicit Stepper(Publish to,Began before={}):publish(std::move(to)),began(std::move(before)){}
    ~Stepper(){Stop();}
    Stepper(const Stepper&)=delete;
    Stepper& operator=(const Stepper&)=delete;
    // The worker thread, if it is not running: false when it cannot be made.
    bool Start() {
        std::lock_guard guard(life);
        if(worker.joinable())return true;
        {std::lock_guard lock(mutex);stop=false;}
        try {worker=std::thread([this]{Run();});return true;}
        catch(...) {std::lock_guard lock(mutex);stop=true;return false;}
    }
    // This frame's inputs, in place of any still waiting. False when stopped.
    bool Submit(const StepInput& input) {
        std::lock_guard lock(mutex);
        if(stop)return false;
        std::array<bool,2> strike=input.strike;
        if(has){++results.replaced;for(size_t i=0;i<2;++i)strike[i]=strike[i]||pending->strike[i];}
        Copy(*pending,input);pending->strike=strike;
        has=used=true;active.store(true,std::memory_order_release);
        wake.notify_one();
        return true;
    }
    // What finished since the last call, handed over once: false when no batch did.
    bool Take(StepResults& out) {
        std::lock_guard lock(mutex);
        out=results;
        const StepResults kept{results.ready,results.simulated,results.lifeless};
        results=kept;
        return out.batches>0;
    }
    // A frame waits or is being stepped, or a body is simulated: with
    // neither, and no body posed, there is nothing to hand over.
    bool Active() const {return active.load(std::memory_order_acquire);}
    // Every body let go, the frame waiting and the results not yet taken
    // dropped; a batch being stepped is not published. Returns at once.
    void Reset() {
        std::lock_guard lock(mutex);
        if(stop||!used)return;
        used=false;has=false;reset=true;++epoch;results={};
        wake.notify_one();
    }
    // The worker joined and its world destroyed (on the worker). Start begins anew.
    void Stop() {
        std::lock_guard guard(life);
        {std::lock_guard lock(mutex);stop=true;has=reset=used=false;++epoch;results={};wake.notify_one();}
        if(worker.joinable())worker.join();
        active.store(false,std::memory_order_release);
    }
};
}
