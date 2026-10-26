#include "frame_generation.h"
#include <cassert>
#include <cstddef>
#include <cstring>
using namespace framevr::framegen;

int main() {
    // The control ABI keeps OFXR's layout (provider_api.hpp V1).
    static_assert(offsetof(ControlSnapshotV1,desired)==8&&offsetof(ControlSnapshotV1,revision)==40&&offsetof(ControlSnapshotV1,lastError)==68);
    assert(ControlSettingsV1{}.structSize==28&&ControlSnapshotV1{}.structSize==72&&ControlSnapshotV1{}.apiVersion==1);
    assert(std::strcmp(LayerName,"XR_APILAYER_XRFrameBridge_diagnostic")==0);

    // Settings: out of range or fractional values fall back to Off / Medium / 50%.
    assert((Read(0,1,2)==Options{}));
    assert((Read(2,0,1)==Options{Mode::Nvidia,0,1}));
    assert((Read(3,-1,2.5f)==Options{}));
    assert((Read(NAN,9,INFINITY)==Options{}));
    assert(!Read(0,0,0).On()&&Read(1,0,0).On());

    // Requests: on with its backend; Off asks the loaded layer to step aside.
    auto c=Control({Mode::FidelityFx,0,1});
    assert(c.enabled==1&&c.backend==0&&c.preset==0&&c.scale==1&&c.backward==0&&c.motionVectors==0);
    c=Control({Mode::Nvidia,2,0});
    assert(c.enabled==1&&c.backend==1&&c.preset==2&&c.scale==0);
    assert(Control({}).enabled==0);
    assert(Describe(0,1,2)=="FidelityFX");
    assert(Describe(1,1,2)=="NVIDIA Medium, 50% scale");
    assert(Describe(1,0,0)=="NVIDIA Slow, 100% scale");

    // Not loaded: off stays quiet; on says why it is not running.
    const Options on{Mode::Nvidia,1,2},off{};
    assert(Classify(Start::NotAsked,true,off,nullptr).state==State::Off);
    assert(Classify(Start::NotAsked,true,on,nullptr).state==State::RestartToStart);
    assert(Classify(Start::NotAsked,false,on,nullptr).state==State::NotInstalled);
    assert(Classify(Start::Missing,false,on,nullptr).state==State::NotInstalled);
    assert(Classify(Start::Missing,true,on,nullptr).state==State::RestartToStart);   // installed since
    // ... and the transport restarts the session for it, only when it would run
    assert(LoadsWithNewSession(Classify(Start::NotAsked,true,on,nullptr).state));
    assert(LoadsWithNewSession(Classify(Start::Missing,true,on,nullptr).state));
    assert(!LoadsWithNewSession(Classify(Start::NotAsked,true,on,nullptr,false).state));   // by turns: it waits for Both eyes
    assert(!LoadsWithNewSession(Classify(Start::NotAsked,true,off,nullptr).state));
    assert(!LoadsWithNewSession(Classify(Start::NotAsked,false,on,nullptr).state));
    assert(!LoadsWithNewSession(Classify(Start::Failed,true,on,nullptr).state));
    assert(!LoadsWithNewSession(Classify(Start::Unavailable,true,on,nullptr).state));
    assert(Line(Classify(Start::NotAsked,true,on,nullptr)).size()<90);
    assert(Classify(Start::Unavailable,true,on,nullptr).state==State::Unavailable);
    assert(Classify(Start::Failed,true,on,nullptr).state==State::Failed);
    assert(Classify(Start::Failed,true,off,nullptr).state==State::Off);

    // Loaded: the layer's sessions say what it does with the last request.
    ControlSnapshotV1 s;s.desired.backend=1;s.desired.preset=1;s.desired.scale=2;
    assert(Classify(Start::Loaded,true,on,&s).state==State::Starting);
    s.sessions=1;s.pending=1;
    assert(Classify(Start::Loaded,true,on,&s).state==State::Applying);
    s.pending=0;s.ready=1;
    auto r=Classify(Start::Loaded,true,on,&s);
    assert(r.state==State::Active&&r.backend==1&&r.sessions==1&&r.ready==1);
    assert(Line(r)=="Frame generation: active, OFXR Bridge NVIDIA Medium, 50% scale"&&!Warning(r));
    // By turns: the loaded layer steps aside (each eye would get a quarter of the frames).
    r=Classify(Start::Loaded,true,on,&s,false);
    assert(r.state==State::NeedsBothEyes&&Warning(r)&&Line(r)=="Frame generation: paused; it needs Stereo rendering: Both eyes");
    assert(Classify(Start::NotAsked,true,on,nullptr,false).state==State::NeedsBothEyes);
    assert(Classify(Start::NotAsked,false,on,nullptr,false).state==State::NotInstalled);   // what blocks it first
    assert(Classify(Start::Loaded,true,off,&s,false).state==State::Paused&&Classify(Start::NotAsked,true,off,nullptr,false).state==State::Off);
    assert(Control({Mode::Nvidia,1,2},false).enabled==0&&Control({Mode::Nvidia,1,2},true).enabled==1);
    s.ready=1;
    s.ready=0;s.bypass=1;
    assert(Classify(Start::Loaded,true,on,&s).state==State::Bypassed);
    s.bypass=0;s.errors=1;s.lastError=int32_t(0x887A0005);
    r=Classify(Start::Loaded,true,on,&s);
    assert(r.state==State::Error&&Line(r)=="Frame generation: OFXR Bridge failed (0x887A0005); no generated frames");
    assert(Classify(Start::Loaded,true,on,nullptr).state==State::Error);
    assert(Classify(Start::Loaded,true,off,&s).state==State::Paused);   // stays loaded until a restart
    assert(Classify(Start::NoControl,true,on,nullptr).state==State::NoControl);

    // Every state has a line that fits the Perf page, and a log name.
    for(int i=0;i<=int(State::NeedsBothEyes);++i){
        Readout each;each.state=State(i);each.backend=1;each.errors=1;
        const auto line=Line(each);
        assert(line.rfind("Frame generation: ",0)==0&&line.size()<90);
        assert(std::strlen(StateName(State(i)))>0);
    }
    assert(Warning({State::Error})&&Warning({State::Failed})&&Warning({State::NeedsBothEyes})&&!Warning({State::Active})&&!Warning({State::Off}));

    // A failed live request (VDXR, resuming after By turns: 0x80070057) gets a new session;
    // an error without the layer's count (no answer) or any other state does not.
    s.errors=1;s.lastError=int32_t(0x80070057);
    assert(RetriesWithNewSession(Classify(Start::Loaded,true,on,&s)));
    assert(!RetriesWithNewSession(Classify(Start::Loaded,true,on,nullptr)));
    s.errors=0;s.ready=1;
    assert(!RetriesWithNewSession(Classify(Start::Loaded,true,on,&s))&&!RetriesWithNewSession(Classify(Start::Loaded,true,on,&s,false)));

    // Virtual Desktop halving under the bridge: the tester's 90 Hz session, then SSW's 45.
    HalvingWatch watch;
    for(int i=0;i<10;++i)assert(!watch.Note(90.f));
    assert(!watch.Note(45.f)&&!watch.Note(45.f)&&watch.Note(45.f));   // three seconds in a row
    assert(watch.Note(90.f)&&watch.Note(90.f)&&!watch.Note(90.f));     // and three back
    assert(!watch.Note(45.f)&&!watch.Note(90.f)&&!watch.Note(45.f)&&!watch.Note(45.f));   // a second back restarts the count
    assert(watch.Note(45.f));
    assert(watch.Note(0.f)&&watch.Note(std::nanf("")));   // no reading keeps it
    HalvingWatch slower;   // a lower headset rate is no halving
    for(int i=0;i<5;++i)slower.Note(90.f);
    for(int i=0;i<10;++i)assert(!slower.Note(72.f));
    r=Classify(Start::Loaded,true,on,&s);r.runtimeHalves=true;
    assert(r.state==State::Active&&Warning(r)&&Line(r).size()<90&&Line(r).find("SSW")!=std::string::npos);
    return 0;
}
