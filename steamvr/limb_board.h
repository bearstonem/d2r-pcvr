#pragma once
#include "limb_rig.h"
#include <atomic>
#include <mutex>

// Bodies that give way: what the skeleton hook's pose threads and the
// physics (the XR thread) hand each other. The hook posts each body near the
// player as posed and bends the ones the physics answers for. An answer the
// physics stops giving (the body let go, the physics reset or off, a menu)
// is eased out over its fade (Ease back, limb_tuning.h), so a pose never
// snaps back.
namespace framevr::limbs {
inline constexpr unsigned MaxPosed=24;        // bodies within reach, posted at once
inline constexpr uint64_t FreshPose=200;      // ms: an older pose is not simulated
inline constexpr uint64_t FreshReaction=100;  // ms: an answer is drawn as it is this long
inline constexpr uint64_t FadeTime=100;       // ms: then eased out over this long, unless set otherwise (Ease back's default)
inline constexpr unsigned MaxAnswers=MaxBodies*2;
class Board {
    mutable std::mutex mutex;
    std::array<Rig,MaxPosed> rigs{};unsigned count=0;
    std::array<Reaction,MaxAnswers> reactions{};unsigned answered=0;
    std::atomic<unsigned> reacting{0};
    std::atomic<uint64_t> fade{FadeTime};
public:
    void Fade(uint64_t ms) {fade.store(ms,std::memory_order_relaxed);}
    // The hook's counters, for the physics' log line.
    std::atomic<unsigned> posed{0},bent{0},refused{0},learned{0};
    std::atomic<unsigned> kept{0},restored{0};   // the game had not posed a body again (its own pose kept); a bent one put back
    // The skeleton hook's own cost (owner 2026-11-01: a player measured 10-15 fps with frame generation on):
    // bodies skipped as beyond the radius, the hook's calls for bodies, and its time in microseconds.
    std::atomic<unsigned> outside{0},calls{0};
    std::atomic<uint64_t> micros{0};
    // This body's pose, in place of its last; the stalest goes when full.
    void Pose(const Rig& r) {
        std::lock_guard lock(mutex);
        unsigned at=count;
        for(unsigned i=0;i<count;++i)if(rigs[i].entity==r.entity){at=i;break;}
        if(at==count) {
            if(count<MaxPosed)++count;
            else {at=0;for(unsigned i=1;i<count;++i)if(rigs[i].at<rigs[at].at)at=i;}
        }
        rigs[at]=r;
    }
    // The bodies posed within FreshPose ms of now; older ones are forgotten.
    unsigned Posed(Rig* out,unsigned max,uint64_t now) {
        std::lock_guard lock(mutex);
        unsigned kept=0,n=0;
        for(unsigned i=0;i<count;++i) {
            if(rigs[i].at>now||now-rigs[i].at>FreshPose)continue;
            rigs[kept++]=rigs[i];
            if(n<max)out[n++]=rigs[i];
        }
        count=kept;
        return n;
    }
    // The physics' answers now. A body it no longer answers for keeps its
    // last answer, eased out; once faded it is forgotten.
    void React(const Reaction* r,unsigned n,uint64_t now) {
        std::lock_guard lock(mutex);
        unsigned kept=0;
        for(unsigned i=0;i<answered;++i) {
            const auto& a=reactions[i];
            if(a.at>now||now-a.at>FreshReaction+fade.load(std::memory_order_relaxed))continue;
            bool replaced=false;
            for(unsigned k=0;k<n&&!replaced;++k)replaced=r[k].entity==a.entity;
            if(!replaced)reactions[kept++]=a;
        }
        answered=kept;
        for(unsigned k=0;k<n;++k) {
            unsigned at=answered;
            if(answered<MaxAnswers)++answered;
            else {at=0;for(unsigned i=1;i<answered;++i)if(reactions[i].at<reactions[at].at)at=i;}
            reactions[at]=r[k];
        }
        reacting.store(answered,std::memory_order_release);
    }
    // The answer for this body's pose of this model, eased out when stale.
    bool Answer(uint32_t entity,uint64_t layout,uint64_t now,Reaction& out) const {
        if(!reacting.load(std::memory_order_acquire))return false;
        std::lock_guard lock(mutex);
        for(unsigned i=0;i<answered;++i) {
            const auto& r=reactions[i];
            if(r.entity!=entity||r.layout!=layout||r.at>now)continue;
            const uint64_t age=now-r.at,ease=fade.load(std::memory_order_relaxed);
            if(age>FreshReaction+ease)return false;
            out=age<=FreshReaction?r:Faded(r,1-float(age-FreshReaction)/float(std::max<uint64_t>(ease,1)));
            return true;
        }
        return false;
    }
    void Clear() {
        std::lock_guard lock(mutex);
        count=answered=0;reacting=0;
    }
};
inline Board& Shared() {static Board board;return board;}
}
