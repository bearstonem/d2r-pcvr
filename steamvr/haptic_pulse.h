#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include "haptic_strength.h"

namespace framevr::haptics {
// Game-thread spell success is consumed once on the XR thread, per hand.
// Session/freshness guards prevent a delayed pulse after a menu or character change.
class CastEvents {
    struct Event {uint64_t session=0,at=0;};
    std::array<Event,2> pending{};
    std::mutex mutex;
public:
    void Publish(int hand,uint64_t session,uint64_t at) {
        if(hand<0||hand>=2||!session||!at)return;
        std::lock_guard lock(mutex);pending[hand]={session,at};
    }
    bool Take(int hand,uint64_t session,uint64_t now,bool active) {
        if(hand<0||hand>=2)return false;
        std::lock_guard lock(mutex);const auto event=pending[hand];pending[hand]={};
        return active&&session&&event.session==session&&event.at&&now>=event.at&&now-event.at<150;
    }
    void Clear(){std::lock_guard lock(mutex);pending={};}
};
// Some controller runtimes sustain vibration beyond the requested duration.
// Stop it explicitly on a timer independent of rendering. Apply, replacement,
// cancellation and expiry share a lock so an old timeout cannot stop a new tap.
// The owner must Stop() before destroying the captured XR session/action.
class Pulses {
    using Clock=std::chrono::steady_clock;
    struct Hand {bool active=false;Clock::time_point end{};Category category=Category::Interaction;float amplitude=0;};
    std::array<Hand,2> hands{};
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::function<void(int)> stopPulse;
    bool stopped=true;
    Strengths strengths;
    void CancelLocked() {
        for(int i=0;i<2;++i)if(hands[i].active){hands[i].active=false;stopPulse(i);}
    }
    void Run() {
        std::unique_lock lock(mutex);
        while(!stopped) {
            auto next=Clock::time_point::max();
            for(const auto& hand:hands)if(hand.active)next=std::min(next,hand.end);
            if(next==Clock::time_point::max())wake.wait(lock);
            else wake.wait_until(lock,next);
            if(stopped)break;
            const auto now=Clock::now();
            for(int i=0;i<2;++i)if(hands[i].active&&now>=hands[i].end){hands[i].active=false;stopPulse(i);}
        }
    }
public:
    ~Pulses(){Stop();}
    // Start/Stop are serialized by the owner's session lifecycle lock.
    bool Start(std::function<void(int)> callback) {
        if(worker.joinable())return true;
        {std::lock_guard lock(mutex);stopPulse=std::move(callback);stopped=false;hands={};}
        try {worker=std::thread([this]{Run();});return true;}
        catch(...) {std::lock_guard lock(mutex);stopped=true;return false;}
    }
    void Configure(Strengths next) {
        std::lock_guard lock(mutex);
        next.master=NormalizeStrength(next.master);
        for(auto& value:next.category)value=NormalizeStrength(value);
        if(next.master==strengths.master&&next.category==strengths.category)return;
        strengths=next;
        for(int h=0;h<2;++h)if(hands[h].active&&
            (strengths.master==0||strengths.category[int(hands[h].category)]==0)) {
            hands[h].active=false;stopPulse(h);
        }
        wake.notify_one();
    }
    // A preview uses the unsaved percentages for this pulse only. Normal events
    // use saved values. Muting a category stops it without stopping other cues.
    template<class Apply> bool Pulse(int hand,Category category,std::chrono::nanoseconds duration,float amplitude,Apply&& apply,const Strengths* preview=nullptr) {
        std::lock_guard lock(mutex);
        if(stopped||hand<0||hand>=2||int(category)<0||int(category)>=CategoryCount||duration.count()<=0)return false;
        const auto& levels=preview?*preview:strengths;
        const float scaled=ScaleAmplitude(amplitude,levels.master,levels.category[int(category)]);
        if(scaled<=0)return false;
        const auto& current=hands[hand];
        // Let a landed impact finish before a weaker incidental cue. A new
        // impact, explicit preview, mute, focus loss or cancellation still wins.
        if(!preview&&category!=Category::Melee&&current.active&&current.category==Category::Melee&&
           Clock::now()<current.end&&scaled<current.amplitude)return false;
        if(!apply(scaled))return false;
        hands[hand]={true,Clock::now()+duration,category,scaled};wake.notify_one();return true;
    }
    void Cancel() {std::lock_guard lock(mutex);CancelLocked();wake.notify_one();}
    void Stop() {
        {std::lock_guard lock(mutex);stopped=true;CancelLocked();wake.notify_one();}
        if(worker.joinable())worker.join();
    }
};
}
