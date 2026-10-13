#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace framevr::stereoreplay {

// Values only. Native capture must serialize calls in their actual queue order.
// A ready plan does NOT retain resources/descriptors or freeze native recordings:
// the caller must hold that lifetime/reset protection through GPU completion.
struct Recording {
    uint64_t list=0,generation=0,allocator=0,allocatorGeneration=0;
};
class Recordings {
    struct List {uint64_t generation,allocator,allocatorGeneration;};
    std::map<uint64_t,List> lists;
    std::map<uint64_t,uint64_t> allocators;
    uint64_t serial=0;
    size_t limit;
    bool incomplete=false;
    uint64_t Next() {
        if(serial==std::numeric_limits<uint64_t>::max()){incomplete=true;return 0;}
        return ++serial;
    }
public:
    explicit Recordings(size_t capacity=4096):limit(capacity){}
    void AllocatorCreated(uint64_t allocator) {
        if(!allocator){incomplete=true;return;}
        if(allocators.size()>=limit&&!allocators.contains(allocator)){incomplete=true;return;}
        allocators[allocator]=Next();
    }
    void AllocatorReset(uint64_t allocator){AllocatorCreated(allocator);}
    void AllocatorDestroyed(uint64_t allocator){allocators.erase(allocator);}
    void ListReset(uint64_t list,uint64_t allocator) {
        if(!list||!allocators.contains(allocator)){incomplete=true;return;}
        if(lists.size()>=limit&&!lists.contains(list)){incomplete=true;return;}
        lists[list]={Next(),allocator,allocators.at(allocator)};
    }
    void ListDestroyed(uint64_t list){lists.erase(list);}
    std::optional<Recording> Snapshot(uint64_t list)const {
        const auto i=lists.find(list);
        if(incomplete||i==lists.end())return {};
        const auto& l=i->second;
        const auto a=allocators.find(l.allocator);
        if(a==allocators.end()||a->second!=l.allocatorGeneration)return {};
        return Recording{list,l.generation,l.allocator,l.allocatorGeneration};
    }
    bool Current(const Recording& snapshot)const {
        const auto now=Snapshot(snapshot.list);
        return now&&now->generation==snapshot.generation&&now->allocator==snapshot.allocator&&
            now->allocatorGeneration==snapshot.allocatorGeneration;
    }
};

enum class PlanStatus {Ready,Empty,Overflow,InvalidMetadata,StaleRecording,AmbiguousFence,MissingDependency,CyclicDependency};
struct Submission {uint64_t id=0,queue=0;std::vector<Recording> lists;};
struct SubmissionPlan {
    PlanStatus status=PlanStatus::Empty;
    std::vector<size_t> order; // Indices into Capture::Submissions(), preserving native batches.
    uint32_t internalWaits=0,externalWaits=0;
    uint32_t multiSignalFences=0,unorderedSignalFences=0,rewoundFences=0,belowStartFences=0;
    bool Ready()const{return status==PlanStatus::Ready;}
};
enum class DependencyKind {Batch,Signal,Wait};
struct DependencyEvent {
    DependencyKind kind;
    uint32_t queue=0,fence=0,lists=0; // Synthetic IDs, never native addresses.
    uint64_t value=0,start=0;
    bool startKnown=false;
};
struct DependencyTrace {std::vector<DependencyEvent> events;bool truncated=false;};
class Capture {
    enum class Kind {Batch,Signal,Wait};
    struct Event {Kind kind;uint64_t queue,fence,value;size_t batch=0;};
    std::vector<Event> events;
    std::vector<Submission> submissions;
    std::map<uint64_t,uint64_t> fenceStarts;
    std::set<uint64_t> queues,batchIds,fences;
    size_t eventLimit,batchLimit,listLimit,queueLimit,fenceLimit,analysisLimit;
    bool overflow=false,invalid=false;
    bool EventAllowed(uint64_t queue,uint64_t fence=0) {
        if(!queue){invalid=true;return false;}
        if(events.size()>=eventLimit||(!queues.contains(queue)&&queues.size()>=queueLimit)||
           (fence&&!fences.contains(fence)&&fences.size()>=fenceLimit)){overflow=true;return false;}
        queues.insert(queue);if(fence)fences.insert(fence);return true;
    }
public:
    explicit Capture(size_t maxEvents=2048,size_t maxBatches=256,size_t maxListsPerBatch=64,
                     size_t maxQueues=16,size_t maxFences=128,size_t maxAnalysisSteps=500000):
        eventLimit(maxEvents),batchLimit(maxBatches),listLimit(maxListsPerBatch),queueLimit(maxQueues),fenceLimit(maxFences),analysisLimit(maxAnalysisSteps){}
    // Must be sampled BEFORE this eye's submissions, never inferred from a later
    // completed value. UINT64_MAX means a removed D3D12 device, not completed work.
    void FenceAtStart(uint64_t fence,uint64_t completed) {
        if(!fence||completed==UINT64_MAX||!events.empty()||fenceStarts.contains(fence)){invalid=true;return;}
        if(!fences.contains(fence)&&fences.size()>=fenceLimit){overflow=true;return;}
        fences.insert(fence);
        fenceStarts.emplace(fence,completed);
    }
    void Batch(uint64_t queue,uint64_t id,std::span<const uint64_t> listIds,const Recordings& recordings) {
        if(!id||listIds.empty()||batchIds.contains(id)){invalid=true;return;}
        if(submissions.size()>=batchLimit||listIds.size()>listLimit){overflow=true;return;}
        if(!EventAllowed(queue))return;
        Submission batch{id,queue,{}};
        for(const auto list:listIds) {
            const auto recording=recordings.Snapshot(list);
            if(!recording){invalid=true;return;}
            batch.lists.push_back(*recording);
        }
        events.push_back({Kind::Batch,queue,0,0,submissions.size()});
        submissions.push_back(std::move(batch));batchIds.insert(id);
    }
    void Signal(uint64_t queue,uint64_t fence,uint64_t value) {
        if(!fence||value==UINT64_MAX){invalid=true;return;}
        if(EventAllowed(queue,fence))events.push_back({Kind::Signal,queue,fence,value});
    }
    void Wait(uint64_t queue,uint64_t fence,uint64_t value) {
        if(!fence||value==UINT64_MAX){invalid=true;return;}
        if(EventAllowed(queue,fence))events.push_back({Kind::Wait,queue,fence,value});
    }
    const std::vector<Submission>& Submissions()const{return submissions;}
    DependencyTrace DiagnosticTrace(size_t limit=64)const {
        DependencyTrace out;out.truncated=events.size()>limit;
        std::map<uint64_t,uint32_t> queueIds,fenceIds;
        for(size_t i=0;i<std::min(limit,events.size());++i) {
            const auto& event=events[i];
            const auto queue=queueIds.try_emplace(event.queue,uint32_t(queueIds.size())).first->second;
            DependencyEvent value{};value.queue=queue;
            if(event.kind==Kind::Batch){value.kind=DependencyKind::Batch;value.lists=uint32_t(submissions[event.batch].lists.size());}
            else {
                value.kind=event.kind==Kind::Signal?DependencyKind::Signal:DependencyKind::Wait;
                value.fence=fenceIds.try_emplace(event.fence,uint32_t(fenceIds.size())).first->second;value.value=event.value;
                if(const auto start=fenceStarts.find(event.fence);start!=fenceStarts.end()){value.startKnown=true;value.start=start->second;}
            }
            out.events.push_back(value);
        }return out;
    }
    SubmissionPlan Plan(const Recordings& recordings)const {
        SubmissionPlan out;
        auto reject=[&](PlanStatus status){out.status=status;out.order.clear();return out;};
        if(overflow)return reject(PlanStatus::Overflow);
        if(invalid)return reject(PlanStatus::InvalidMetadata);
        if(submissions.empty())return out;
        for(const auto& batch:submissions)for(const auto& list:batch.lists)
            if(!recordings.Current(list))return reject(PlanStatus::StaleRecording);

        struct Timeline {uint64_t queue=0,last=0;bool multi=false,rewound=false,belowStart=false;std::vector<std::pair<uint64_t,size_t>> signals;};
        std::map<uint64_t,Timeline> timelines;
        for(size_t i=0;i<events.size();++i) {
            const auto& event=events[i];if(event.kind!=Kind::Signal)continue;
            auto& timeline=timelines[event.fence];
            const auto start=fenceStarts.find(event.fence);
            if(timeline.queue&&timeline.queue!=event.queue)timeline.multi=true;
            if(!timeline.signals.empty()&&event.value<timeline.last)timeline.rewound=true;
            if(start!=fenceStarts.end()&&event.value<start->second)timeline.belowStart=true;
            if(!timeline.queue)timeline.queue=event.queue;
            timeline.last=event.value;timeline.signals.emplace_back(event.value,i);
        }
        for(const auto& [id,timeline]:timelines){out.multiSignalFences+=timeline.multi;out.rewoundFences+=timeline.rewound;out.belowStartFences+=timeline.belowStart;}
        if(out.rewoundFences||out.belowStartFences)return reject(PlanStatus::AmbiguousFence);
        std::vector<std::vector<size_t>> edges(events.size());
        std::vector<size_t> incoming(events.size(),0);
        auto edge=[&](size_t before,size_t after){
            auto& next=edges[before];
            if(std::find(next.begin(),next.end(),after)==next.end()){next.push_back(after);++incoming[after];}
        };
        std::map<uint64_t,size_t> previous;
        std::vector<size_t> pending;
        for(size_t i=0;i<events.size();++i) {
            const auto& event=events[i];
            if(const auto p=previous.find(event.queue);p!=previous.end())edge(p->second,i);
            previous[event.queue]=i;
            if(event.kind!=Kind::Wait)continue;
            const auto start=fenceStarts.find(event.fence);
            if(!event.value||(start!=fenceStarts.end()&&start->second>=event.value)){++out.externalWaits;continue;}
            pending.push_back(i);
        }
        size_t analysisSteps=0;bool limited=false;
        std::vector<size_t> stack;stack.reserve(events.size());
        std::vector<uint32_t> marks(events.size(),0);uint32_t epoch=0;
        auto reaches=[&](size_t from,size_t to){
            if(from==to)return true;
            if(++epoch==0){std::fill(marks.begin(),marks.end(),0);++epoch;}
            stack.clear();stack.push_back(from);marks[from]=epoch;
            while(!stack.empty()) {
                if(analysisSteps++>=analysisLimit){limited=true;return false;}
                const auto current=stack.back();stack.pop_back();
                for(const auto next:edges[current]) {
                    if(analysisSteps++>=analysisLimit){limited=true;return false;}
                    if(next==to)return true;
                    if(marks[next]!=epoch){marks[next]=epoch;stack.push_back(next);}
                }
            }return false;
        };
        while(!pending.empty()) {
            bool progress=false;
            for(auto wait=pending.begin();wait!=pending.end();) {
                const size_t index=*wait;const auto& event=events[index];
                const auto timeline=timelines.find(event.fence);
                if(timeline==timelines.end())return reject(PlanStatus::MissingDependency);
                std::vector<size_t> candidates;bool foundValue=false;
                for(const auto& [value,signal]:timeline->second.signals)if(value>=event.value) {
                    foundValue=true;
                    // A signal behind this wait cannot be its producer. This
                    // removes the later handoff signal on the waiting queue.
                    if(!reaches(index,signal))candidates.push_back(signal);
                }
                if(limited)return reject(PlanStatus::Overflow);
                if(candidates.empty())return reject(foundValue?PlanStatus::CyclicDependency:PlanStatus::MissingDependency);
                size_t producer=events.size();
                for(const auto candidate:candidates)if(reaches(candidate,index)){producer=candidate;break;}
                if(producer==events.size())for(const auto candidate:candidates) {
                    bool earliest=true;
                    for(const auto other:candidates)if(other!=candidate&&!reaches(candidate,other)){earliest=false;break;}
                    if(earliest){producer=candidate;break;}
                }
                if(limited)return reject(PlanStatus::Overflow);
                if(producer==events.size()){++wait;continue;}
                edge(producer,index);++out.internalWaits;wait=pending.erase(wait);progress=true;
            }
            if(!progress)return reject(PlanStatus::AmbiguousFence);
        }
        // Stable topological order also handles a CPU Wait call preceding its
        // producer's Signal on another queue. Replaying CPU call order alone
        // would allow stale data, or deadlock if translated into GPU waits.
        std::priority_queue<size_t,std::vector<size_t>,std::greater<size_t>> ready;
        for(size_t i=0;i<events.size();++i)if(!incoming[i])ready.push(i);
        size_t visited=0;
        while(!ready.empty()) {
            const size_t i=ready.top();ready.pop();++visited;
            if(events[i].kind==Kind::Batch)out.order.push_back(events[i].batch);
            for(const size_t after:edges[i])if(!--incoming[after])ready.push(after);
        }
        if(visited!=events.size())return reject(PlanStatus::CyclicDependency);
        for(const auto& [id,timeline]:timelines)if(timeline.multi) {
            bool ordered=true;
            for(size_t i=1;i<timeline.signals.size();++i)
                if(!reaches(timeline.signals[i-1].second,timeline.signals[i].second)){ordered=false;break;}
            if(limited)return reject(PlanStatus::Overflow);
            if(!ordered)++out.unorderedSignalFences;
        }
        if(out.unorderedSignalFences)return reject(PlanStatus::AmbiguousFence);
        out.status=PlanStatus::Ready;return out;
    }
};
}
