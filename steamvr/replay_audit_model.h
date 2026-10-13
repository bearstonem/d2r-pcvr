#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace framevr::replayaudit {
class CaptureGate {
    unsigned settleLimit,pairLimit,settled=0,pairs=0,eyes=0;
    bool started=false,stopped=false,open=false;
public:
    enum class Step { Waiting, Started, Collecting, Complete, Incomplete };
    CaptureGate(unsigned settle=90,unsigned count=8):settleLimit(settle),pairLimit(count){}
    Step BeginPair(bool ready) {
        if(stopped)return Step::Complete;
        open=false;eyes=0;
        if(!ready){settled=0;if(started){stopped=true;return Step::Incomplete;}return Step::Waiting;}
        if(settled<settleLimit){++settled;return Step::Waiting;}
        open=true;if(!started){started=true;return Step::Started;}return Step::Collecting;
    }
    bool PairOpen()const{return open&&!stopped;}
    unsigned CompletedPairs()const{return pairs;}
    void EyeDone(unsigned eye){if(PairOpen()&&eye<2)eyes|=1u<<eye;}
    Step EndPair() {
        if(!PairOpen())return Step::Waiting;
        open=false;if(eyes!=3){stopped=true;return Step::Incomplete;}
        if(++pairs>=pairLimit){stopped=true;return Step::Complete;}return Step::Collecting;
    }
    bool SkipPair(){settled=0;open=false;if(started&&!stopped){stopped=true;return true;}return false;}
};
// Value-only metadata. Callers serialize access; no game/GPU object is retained.
struct EyeReport {
    uint32_t submissions=0,queueKinds[4]{},queues=0,invalidatedLists=0;
    uint32_t resets=0,resourceRetirements=0,unknownBarriers=0;
    uint32_t stateRestorations=0,stateChainMismatches=0,metadataMissing=0,overflows=0;
};
class Commands {
    struct Transition {uint64_t resource;uint32_t before,after;};
    struct List {uint64_t generation=0;bool overflow=false;std::vector<Transition> transitions;};
    struct Submitted {uint64_t list,generation;};
    struct State {uint32_t first,last;};
    std::map<uint64_t,List> lists;
    std::vector<Submitted> submitted;
    std::map<uint64_t,uint32_t> queues;
    std::map<std::pair<uint64_t,uint64_t>,State> states;
    uint64_t serial=0;
    size_t listLimit,submissionLimit,transitionLimit;
    bool active=false;
    EyeReport report;
    List* Get(uint64_t key) {
        if(auto i=lists.find(key);i!=lists.end())return &i->second;
        if(lists.size()>=listLimit){if(active)++report.overflows;return nullptr;}
        return &lists.emplace(key,List{++serial}).first->second;
    }
public:
    explicit Commands(size_t maxLists=2048,size_t maxSubmissions=512,size_t maxTransitions=512):
        listLimit(maxLists),submissionLimit(maxSubmissions),transitionLimit(maxTransitions){}
    void Reset(uint64_t key){if(auto* l=Get(key)){l->generation=++serial;l->transitions.clear();l->overflow=false;}if(active)++report.resets;}
    void Destroy(uint64_t key){lists.erase(key);}
    void RetireResource(){if(active)++report.resourceRetirements;}
    void BeginEye(){active=true;report={};submitted.clear();queues.clear();states.clear();}
    void Barrier(uint64_t list,uint64_t resource,uint32_t before,uint32_t after) {
        auto* l=Get(list);if(!l)return;
        if(l->transitions.size()>=transitionLimit){l->overflow=true;return;}
        l->transitions.push_back({resource,before,after});
    }
    void Submit(uint64_t queue,unsigned kind,uint64_t list) {
        if(!active)return;
        ++report.submissions;++report.queueKinds[std::min(kind,3u)];
        if(!queues.contains(queue)) {
            if(queues.size()>=16){++report.overflows;return;}
            queues.emplace(queue,kind);
        }
        auto i=lists.find(list);
        if(i==lists.end()){++report.metadataMissing;return;}
        const auto& l=i->second;
        if(submitted.size()>=submissionLimit){++report.overflows;return;}
        submitted.push_back({list,l.generation});
        if(l.overflow)++report.overflows;
        for(const auto& t:l.transitions) {
            // ReShade does not expose all native aliasing/split-barrier details.
            if(!t.resource){++report.unknownBarriers;continue;}
            const auto key=std::pair{queue,t.resource};
            if(auto s=states.find(key);s!=states.end()) {
                if(s->second.last!=t.before)++report.stateChainMismatches;
                s->second.last=t.after;
            } else if(states.size()<2048)states.emplace(key,State{t.before,t.after});
            else ++report.overflows;
        }
    }
    EyeReport EndEye() {
        report.invalidatedLists=report.stateRestorations=0;
        for(const auto& s:submitted){auto i=lists.find(s.list);if(i==lists.end()||i->second.generation!=s.generation)++report.invalidatedLists;}
        for(const auto& [key,state]:states)if(state.first!=state.last)++report.stateRestorations;
        report.queues=uint32_t(queues.size());active=false;return report;
    }
};

struct ProbeRange {uint64_t relativeOffset=0;uint32_t bytes=0;};
inline ProbeRange ReadableRange(uint64_t bufferSize,uint64_t mappedOffset,uint64_t mappedSize,
                               uint64_t offset,uint64_t size) {
    if(mappedOffset>bufferSize||offset<mappedOffset||offset>=bufferSize)return {};
    const uint64_t extent=std::min(mappedSize,bufferSize-mappedOffset),relative=offset-mappedOffset;
    if(relative>=extent)return {};
    const uint64_t bytes=std::min({size,extent-relative,bufferSize-offset,uint64_t(4096)})&~uint64_t(3);
    return {relative,uint32_t(bytes)};
}
using Matrix=std::array<double,16>;
struct Camera {Matrix view{},projection{};bool haveView=false,haveProjection=false;};
struct CameraLayout {uint32_t completeRecords=0,partialRecords=0,viewOnly=0;};
inline Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix out{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)out[r*4+c]+=a[r*4+k]*b[k*4+c];return out;
}
inline bool Invert(const Matrix& m,Matrix& out) {
    double a[4][8]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){if(!std::isfinite(m[r*4+c]))return false;a[r][c]=m[r*4+c];a[r][c+4]=r==c;}
    for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[p][c]))p=r;
        if(std::abs(a[p][c])<1e-12)return false;
        if(p!=c)for(int k=0;k<8;++k)std::swap(a[p][k],a[c][k]);
        const double d=a[c][c];for(int k=0;k<8;++k)a[c][k]/=d;
        for(int r=0;r<4;++r)if(r!=c){const double x=a[r][c];for(int k=0;k<8;++k)a[r][k]-=x*a[c][k];}
    }
    Matrix result{};for(int r=0;r<4;++r)for(int c=0;c<4;++c){result[r*4+c]=a[r][c+4];if(!std::isfinite(result[r*4+c]))return false;}
    out=result;return true;
}
inline bool Near(std::span<const float> values,const Matrix& m,double rel=1e-4,double abs=1e-2) {
    if(values.size()<16)return false;
    for(size_t i=0;i<16;++i)if(!std::isfinite(values[i])||!std::isfinite(m[i])||std::abs(values[i]-m[i])>abs+rel*std::abs(m[i]))return false;
    return true;
}
inline Matrix MatrixAt(std::span<const float> words,size_t offset){Matrix m{};for(size_t i=0;i<16;++i)m[i]=words[offset+i];return m;}
inline bool EyeProjection(std::span<const float> words,const Matrix& p) {
    if(words.size()<16)return false;
    for(size_t i=0;i<16;++i){const double abs=(i==8||i==9)?1e-2:1e-5;
        if(!std::isfinite(words[i])||!std::isfinite(p[i])||std::abs(words[i]-p[i])>abs+1e-4*std::abs(p[i]))return false;}
    return std::abs(p[11])>.5&&std::abs(p[15])<1e-6;
}
// Copyright (c) 2026 BodyWalkVR. MIT license; see ../LICENSE.
// Camera-record structure is informed by the upstream camfix investigation
// at natpoh/diablo2-vr-port 4b6284c0. See THIRD_PARTY.md. This only recognizes
// current matrices; it cannot establish safe history, shading or replay behavior.
inline CameraLayout InspectCamera(std::span<const float> words,const Camera& camera) {
    CameraLayout out;
    if(!camera.haveView||!camera.haveProjection)return out;
    for(size_t o=0;o+16<=words.size();++o) {
        bool rotation=true;for(size_t i=0;i<12;++i)if(!std::isfinite(words[o+i])||!std::isfinite(camera.view[i])||std::abs(words[o+i]-camera.view[i])>1e-4){rotation=false;break;}
        if(!rotation)continue;
        bool translation=true;for(size_t i=12;i<16;++i)if(!std::isfinite(words[o+i])||!std::isfinite(camera.view[i])||std::abs(words[o+i]-camera.view[i])>.025){translation=false;break;}
        if(!translation)continue;
        if(o+48>words.size()||!EyeProjection(words.subspan(o+32),camera.projection)){++out.viewOnly;o+=15;continue;}
        if(o+96>words.size()){++out.partialRecords;o+=15;continue;}
        const auto v=MatrixAt(words,o),p=MatrixAt(words,o+32),vp=Multiply(v,p);
        Matrix iv{},ip{},ivp{};
        const bool complete=Invert(v,iv)&&Invert(p,ip)&&Invert(vp,ivp)&&
            Near(words.subspan(o+16),iv)&&Near(words.subspan(o+48),ip)&&
            Near(words.subspan(o+64),vp)&&Near(words.subspan(o+80),ivp);
        if(complete)++out.completeRecords;else ++out.partialRecords;
        o+=15;
    }
    return out;
}
}
