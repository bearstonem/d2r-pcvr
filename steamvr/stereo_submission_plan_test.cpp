#include "stereo_submission_plan.h"
#include <array>
#include <cassert>
#include <cstdio>

using namespace framevr::stereoreplay;
namespace {
Recordings Known() {
    Recordings r;
    for(uint64_t i=1;i<=3;++i){r.AllocatorCreated(100+i);r.ListReset(i,100+i);}
    return r;
}
void Add(Capture& c,const Recordings& r,uint64_t queue,uint64_t batch,uint64_t list) {
    const std::array ids{list};c.Batch(queue,batch,ids,r);
}
void Dependencies() {
    auto r=Known();Capture c;
    // The consumer is submitted before the producer's Signal CPU call. Its
    // queue waits in the original workload; a serial replay needs the graph.
    c.Wait(20,1000,7);Add(c,r,20,2,2);c.Signal(20,2000,9);
    c.Wait(30,2000,9);Add(c,r,30,3,3);
    Add(c,r,10,1,1);c.Signal(10,1000,7);
    auto plan=c.Plan(r);assert(plan.Ready()&&plan.internalWaits==2&&plan.externalWaits==0);
    assert((plan.order==std::vector<size_t>{2,0,1}));
    const auto trace=c.DiagnosticTrace();assert(trace.events.size()==7&&!trace.truncated);
    assert(trace.events[0].kind==DependencyKind::Wait&&trace.events[0].queue==0&&trace.events[0].fence==0&&!trace.events[0].startKnown);
    assert(trace.events[6].kind==DependencyKind::Signal&&trace.events[6].queue==2&&trace.events[6].fence==0&&trace.events[6].value==7);
    assert(c.DiagnosticTrace(3).events.size()==3&&c.DiagnosticTrace(3).truncated);
    Capture higher;higher.Wait(20,1000,6);Add(higher,r,20,2,2);
    Add(higher,r,10,1,1);higher.Signal(10,1000,7);
    assert((higher.Plan(r).order==std::vector<size_t>{1,0}));
    Capture external;external.FenceAtStart(1000,7);external.Wait(10,1000,7);Add(external,r,10,1,1);
    plan=external.Plan(r);assert(plan.Ready()&&plan.externalWaits==1&&!plan.internalWaits);
    Capture future;future.Wait(20,1000,8);Add(future,r,20,2,2);future.Signal(10,1000,7);
    assert(future.Plan(r).status==PlanStatus::MissingDependency);
    Capture late;Add(late,r,10,1,1);late.FenceAtStart(1000,7);
    assert(late.Plan(r).status==PlanStatus::InvalidMetadata);
}
void Lifecycle() {
    auto r=Known();Capture c;Add(c,r,10,1,1);assert(c.Plan(r).Ready());
    r.ListReset(1,101);assert(c.Plan(r).status==PlanStatus::StaleRecording);
    Capture current;Add(current,r,10,2,1);r.AllocatorReset(101);
    assert(current.Plan(r).status==PlanStatus::StaleRecording);
    assert(!r.Snapshot(1)); // Allocator Reset alone invalidates this recording.
    r.ListReset(1,101);Capture reused;Add(reused,r,10,3,1);
    r.ListDestroyed(1);r.ListReset(1,101);
    assert(reused.Plan(r).status==PlanStatus::StaleRecording);
    Capture allocatorReused;Add(allocatorReused,r,10,4,1);
    r.AllocatorDestroyed(101);r.AllocatorCreated(101);r.ListReset(1,101);
    assert(allocatorReused.Plan(r).status==PlanStatus::StaleRecording);
    Capture missing;Add(missing,r,10,5,99);assert(missing.Plan(r).status==PlanStatus::InvalidMetadata);
}
void RejectUnsafe() {
    auto r=Known();Capture cycle;
    cycle.Wait(10,2000,1);Add(cycle,r,10,1,1);cycle.Signal(10,1000,1);
    cycle.Wait(20,1000,1);Add(cycle,r,20,2,2);cycle.Signal(20,2000,1);
    const auto p=cycle.Plan(r);assert(p.status==PlanStatus::CyclicDependency&&p.order.empty());
    Capture missing;missing.Wait(10,1000,1);Add(missing,r,10,1,1);
    assert(missing.Plan(r).status==PlanStatus::MissingDependency);
    Capture rewind;Add(rewind,r,10,1,1);rewind.Signal(10,1000,5);rewind.Signal(10,1000,4);
    assert(rewind.Plan(r).status==PlanStatus::AmbiguousFence);
    assert(rewind.Plan(r).rewoundFences==1&&!rewind.Plan(r).multiSignalFences);
    Capture producers;Add(producers,r,10,1,1);producers.Signal(10,1000,5);producers.Signal(20,1000,6);
    assert(producers.Plan(r).status==PlanStatus::AmbiguousFence);
    assert(producers.Plan(r).multiSignalFences==1&&!producers.Plan(r).rewoundFences);
    Capture below;below.FenceAtStart(1000,7);Add(below,r,10,1,1);below.Signal(10,1000,6);
    assert(below.Plan(r).status==PlanStatus::AmbiguousFence&&below.Plan(r).belowStartFences==1);
    Capture removed;removed.FenceAtStart(1000,UINT64_MAX);Add(removed,r,10,1,1);
    assert(removed.Plan(r).status==PlanStatus::InvalidMetadata);
    Capture duplicate;Add(duplicate,r,10,1,1);Add(duplicate,r,20,1,2);
    assert(duplicate.Plan(r).status==PlanStatus::InvalidMetadata);
}
void BoundsAndBatches() {
    auto r=Known();Capture c;
    const std::array<uint64_t,2> ids{1,2};c.Batch(10,1,ids,r);
    assert(c.Plan(r).Ready()&&c.Submissions().size()==1&&c.Submissions()[0].lists.size()==2);
    Capture overflow(1);Add(overflow,r,10,1,1);overflow.Signal(10,1000,1);
    assert(overflow.Plan(r).status==PlanStatus::Overflow&&overflow.Plan(r).order.empty());
    Capture batches(10,1);Add(batches,r,10,1,1);Add(batches,r,10,2,2);
    assert(batches.Plan(r).status==PlanStatus::Overflow);
    Capture lists(10,10,1);lists.Batch(10,1,ids,r);assert(lists.Plan(r).status==PlanStatus::Overflow);
    Capture queues(10,10,10,1);Add(queues,r,10,1,1);Add(queues,r,20,2,2);
    assert(queues.Plan(r).status==PlanStatus::Overflow);
    Capture fences(10,10,10,10,1);Add(fences,r,10,1,1);fences.Signal(10,1000,1);fences.Signal(10,2000,1);
    assert(fences.Plan(r).status==PlanStatus::Overflow);
    Recordings bounded(1);bounded.AllocatorCreated(1);bounded.AllocatorCreated(2);bounded.ListReset(1,1);
    assert(!bounded.Snapshot(1));
    Capture empty;assert(empty.Plan(r).status==PlanStatus::Empty&&!empty.Plan(r).Ready());
}
}
int main(){Dependencies();Lifecycle();RejectUnsafe();BoundsAndBatches();std::puts("Stereo submission planning: queue/fence dependencies, native batches, recording/allocator invalidation and bounds passed");}
