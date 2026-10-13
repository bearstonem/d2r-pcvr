#include "replay_audit_model.h"
#include <cassert>
#include <cstdio>

using namespace framevr::replayaudit;
namespace {
void CaptureBounds() {
    using Step=CaptureGate::Step;
    CaptureGate g(2,3);
    assert(g.BeginPair(true)==Step::Waiting&&!g.PairOpen());
    assert(g.BeginPair(false)==Step::Waiting); // Inactive XR restarts settling.
    assert(g.BeginPair(true)==Step::Waiting&&g.BeginPair(true)==Step::Waiting);
    assert(g.BeginPair(true)==Step::Started&&g.PairOpen());
    for(unsigned p=0;p<3;++p) {
        if(p)assert(g.BeginPair(true)==Step::Collecting);
        g.EyeDone(0);g.EyeDone(0); // A duplicate eye is not a complete stereo pair.
        g.EyeDone(1);assert(g.EndPair()==(p==2?Step::Complete:Step::Collecting));
    }
    assert(g.CompletedPairs()==3&&!g.PairOpen());
    assert(g.BeginPair(true)==Step::Complete&&!g.PairOpen());
    CaptureGate interrupted(0,8);assert(interrupted.BeginPair(true)==Step::Started);
    assert(interrupted.BeginPair(false)==Step::Incomplete&&!interrupted.PairOpen());
    CaptureGate partial(0,8);partial.BeginPair(true);partial.EyeDone(0);
    assert(partial.EndPair()==Step::Incomplete&&partial.CompletedPairs()==0);
    CaptureGate menu(0,8);menu.BeginPair(true);assert(menu.SkipPair()&&!menu.PairOpen());
}
void Lifecycle() {
    Commands c;
    c.Reset(10);c.BeginEye();c.Submit(100,0,10);
    auto atPresent=c.EndEye();assert(atPresent.submissions==1&&atPresent.invalidatedLists==0);
    // D3D12 permits this reset after submission. The old pointer now names new commands.
    c.Reset(10);auto atDrawEnd=c.EndEye();assert(atDrawEnd.invalidatedLists==1);
    assert(c.EndEye().invalidatedLists==1);
    c.BeginEye();c.Submit(100,0,10);c.Destroy(10);c.Reset(10);
    assert(c.EndEye().invalidatedLists==1); // Same address, different object/recording.
    c.BeginEye();c.Submit(100,0,999);assert(c.EndEye().metadataMissing==1);
    c.Reset(11);c.BeginEye();c.Submit(100,0,11);c.Submit(100,0,11);
    auto repeated=c.EndEye();assert(repeated.submissions==2&&repeated.invalidatedLists==0);
}
void Transitions() {
    Commands c;c.Reset(10);c.Reset(20);
    c.Barrier(10,1,0,4);c.Barrier(10,1,4,0);c.Barrier(10,2,8,16);
    c.Barrier(20,2,16,32);c.Barrier(20,0,0,0);
    c.BeginEye();c.Submit(100,0,10);c.Submit(100,0,20);c.RetireResource();
    auto r=c.EndEye();assert(r.queues==1&&r.queueKinds[0]==2);
    assert(r.stateRestorations==1&&r.stateChainMismatches==0&&r.unknownBarriers==1&&r.resourceRetirements==1);
    c.BeginEye();c.Submit(100,0,20);c.Submit(100,0,10);
    assert(c.EndEye().stateChainMismatches==1);
    c.BeginEye();c.Submit(100,0,10);c.Submit(200,1,20);c.Submit(300,2,20);
    r=c.EndEye();assert(r.queues==3&&r.queueKinds[0]==1&&r.queueKinds[1]==1&&r.queueKinds[2]==1);
    // Cross-queue dependencies are unknown here; do not invent a global state chain.
    assert(r.stateChainMismatches==0);
    c.BeginEye();assert(c.EndEye().submissions==0);
}
void Limits() {
    Commands c(1,1,1);c.Reset(1);c.Reset(2);c.Barrier(1,1,0,1);c.Barrier(1,1,1,0);
    c.BeginEye();c.Submit(100,0,1);c.Submit(100,0,1);c.Submit(100,0,2);
    auto r=c.EndEye();assert(r.overflows>=2&&r.metadataMissing==1);
    const auto max=std::numeric_limits<uint64_t>::max();
    assert(ReadableRange(8192,0,8192,256,max).bytes==4096);
    auto range=ReadableRange(8192,512,1024,768,max);assert(range.relativeOffset==256&&range.bytes==768);
    assert(ReadableRange(8192,0,8192,8192,max).bytes==0);
    assert(ReadableRange(8192,4096,max,4096,max).bytes==4096);
    assert(ReadableRange(8192,max,4096,0,max).bytes==0);
    assert(ReadableRange(8192,0,8192,max,4).bytes==0);
    assert(ReadableRange(8192,512,32,0,16).bytes==0);
    assert(ReadableRange(8192,0,8192,8190,max).bytes==0);
}
void DeferredBindings() {
    Bindings b(3);
    b.Add({10,100,256,4096});b.Add({10,100,256,4096});b.Add({20,200,0,128});
    assert(b.Pending()==2&&b.Submit(99).empty());
    auto first=b.Submit(10);assert(first.size()==1&&first[0].resource==100&&first[0].offset==256&&first[0].size==4096);
    assert(b.Submit(10).empty());
    b.DestroyResource(200);assert(b.Pending()==0&&b.Discarded()==1);
    b.Add({20,200,0,128});assert(b.Pending()==1); // Address reuse is a new candidate.
    b.ResetList(20);assert(b.Pending()==0&&b.Discarded()==2);
    b.Add({20,300,0,128});assert(b.Pending()==0&&b.Overflow()==1); // Dropped work does not refund the read budget.
    Bindings reset;
    reset.Add({1,100,0,64});reset.ResetList(1);reset.Add({1,100,0,64});
    assert(reset.Submit(1).size()==1);
}
Camera Reference() {
    Camera c;c.haveView=c.haveProjection=true;
    const double a=.35,co=std::cos(a),si=std::sin(a);
    c.view={co,0,-si,0,0,1,0,0,si,0,co,0,-10351.7,-12.5,-12376.8,1};
    c.projection={.91,0,0,0,0,1.28,0,0,.08,-.03,0,-1,0,0,.2,0};return c;
}
void Put(std::vector<float>& words,size_t o,const Matrix& m){for(size_t i=0;i<16;++i)words[o+i]=float(m[i]);}
void Record(std::vector<float>& words,size_t o,const Camera& c) {
    Matrix iv,ip,ivp;const auto vp=Multiply(c.view,c.projection);
    assert(Invert(c.view,iv)&&Invert(c.projection,ip)&&Invert(vp,ivp));
    Put(words,o,c.view);Put(words,o+16,iv);Put(words,o+32,c.projection);
    Put(words,o+48,ip);Put(words,o+64,vp);Put(words,o+80,ivp);
}
void Cameras() {
    auto camera=Reference();std::vector<float> words(1024);
    for(auto o:{20,212,404,596,788})Record(words,o,camera);
    Put(words,980,camera.view);
    auto copy=words;auto r=InspectCamera(words,camera);
    assert(r.completeRecords==5&&r.partialRecords==0&&r.viewOnly==1&&copy==words);
    auto noView=camera;noView.haveView=false;assert(InspectCamera(words,noView).completeRecords==0);
    auto otherEye=camera;otherEye.view[12]-=.064;assert(InspectCamera(words,otherEye).completeRecords==0);
    assert(InspectCamera(std::span(words.data(),15),camera).completeRecords==0);
    // Similar shadow/light views with orthographic projections must stay out.
    auto shadow=camera;shadow.projection={.006,0,0,0,0,.006,0,0,0,0,-.001,0,0,0,1,1};
    std::vector<float> sh(116);Record(sh,20,shadow);r=InspectCamera(sh,camera);assert(r.completeRecords==0&&r.viewOnly==1);
    words=copy;words[20+64]+=50.f;r=InspectCamera(words,camera);assert(r.completeRecords==4&&r.partialRecords==1);
    words=copy;words[20+48]=std::numeric_limits<float>::quiet_NaN();r=InspectCamera(words,camera);assert(r.completeRecords==4&&r.partialRecords==1);
    words=copy;words[20]=std::numeric_limits<float>::infinity();assert(InspectCamera(words,camera).completeRecords==4);
    Matrix singular{},out;out.fill(123);const auto before=out;assert(!Invert(singular,out)&&out==before);
    auto invalid=camera;invalid.projection[0]=std::numeric_limits<double>::quiet_NaN();assert(InspectCamera(copy,invalid).completeRecords==0);
    // Pre-draw Refresh can be stale; later callbacks must replace the reference.
    Camera reference=camera;reference.view[12]+=2;
    Bindings pending;pending.Add({1,100,0,4096});
    assert(InspectCamera(copy,reference).completeRecords==0);
    float currentView[16],currentProjection[16];
    for(unsigned i=0;i<16;++i){currentView[i]=float(camera.view[i]);currentProjection[i]=float(camera.projection[i]);}
    reference.View(currentView);reference.Projection(currentProjection);
    assert(reference.viewUpdates==1&&reference.projectionUpdates==1);
    assert(pending.Submit(1).size()==1&&InspectCamera(copy,reference).completeRecords==5);
    currentView[12]+=2;reference.View(currentView);assert(reference.viewUpdates==2&&InspectCamera(copy,reference).completeRecords==0);
    std::puts("Replay audit model: list reuse/destruction, queue-local state chains, bounded metadata, mapped ranges, camera/inverse consistency and shadow/invalid-data rejection passed");
}
}
int main(){CaptureBounds();Lifecycle();Transitions();Limits();DeferredBindings();Cameras();}
