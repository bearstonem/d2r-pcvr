#include "stereo_replay_camera.h"
#include "frame_math.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace framevr;
using namespace framevr::replayaudit;
int main(){
    float lv[16],rv[16],lp[16],rp[16];
    math::View({123.0f,25,-83},math::Yaw(.37f),lv);
    math::View({123.064f,25.002f,-82.999f},math::Yaw(.372f),rv);
    math::Projection(-.9f,.85f,-.8f,.95f,.2f,lp);
    math::Projection(-.84f,.91f,-.82f,.94f,.2f,rp);
    Camera left,right;left.View(lv);left.Projection(lp);right.View(rv);right.Projection(rp);
    std::array<float,1024> words{};words.fill(12345.f);
    auto put=[&](size_t o,const Matrix& m){for(unsigned i=0;i<16;++i)words[o+i]=float(m[i]);};
    Matrix iv,ip,ivp;assert(Invert(left.view,iv)&&Invert(left.projection,ip)&&Invert(Multiply(left.view,left.projection),ivp));
    for(const size_t o:{20u,212u,404u,596u,788u}){
        put(o,left.view);put(o+16,iv);put(o+32,left.projection);put(o+48,ip);
        put(o+64,Multiply(left.view,left.projection));put(o+80,ivp);put(o+96,Multiply(left.view,left.projection));
    }
    put(980,left.view);const auto original=words;
    const auto offsets=stereoreplay::FindCameraRecords(words,left);
    assert((offsets==std::vector<size_t>{20,212,404,596,788}));
    assert(stereoreplay::FoldCameraAt(words,offsets,left,right)==5);
    const auto expected=Multiply(right.view,right.projection);
    for(const size_t o:{20u,212u,404u,596u,788u}){
        assert(!std::memcmp(words.data()+o,original.data()+o,32*4));
        assert(Near(std::span(words).subspan(o+64),expected,1e-4,.001));
        const auto actual=Multiply(left.view,MatrixAt(words,o+32));
        for(unsigned i=0;i<16;++i)assert(std::abs(actual[i]-expected[i])<.002);
        Matrix invExpected;assert(Invert(expected,invExpected));assert(Near(std::span(words).subspan(o+80),invExpected,1e-4,.02));
        assert(Near(std::span(words).subspan(o+96),expected,1e-4,.002));
    }
    assert(!std::memcmp(words.data()+980,original.data()+980,16*4));
    // The host camera can refresh again after a CBV was submitted. The saved
    // left reference must still transform that range into the actual right eye.
    const auto captured=left;auto later=left;later.view[12]+=1;
    auto submitted=original;
    assert(stereoreplay::FoldCameraAt(submitted,offsets,later,right)==0&&submitted==original);
    assert(stereoreplay::FoldCameraAt(submitted,offsets,captured,right)==5);
    assert(Near(std::span(submitted).subspan(20+64),expected,1e-4,.001));
    // A second CBV on the same root need not cover the entire 4 KiB window.
    // Cached offsets are filtered without another unbounded layout scan.
    submitted=original;const auto subset=stereoreplay::SelectCameraRecords(std::span(submitted).first(510),offsets,captured);
    assert((subset==std::vector<size_t>{20,212}));
    assert(stereoreplay::FoldCameraAt(std::span(submitted).first(510),subset,captured,right)==2);
    assert(!std::memcmp(submitted.data()+404,original.data()+404,(1024-404)*4));
    // Small differences within discovery tolerance must not survive as a
    // right-eye translation error: fold each record's own inverse view.
    submitted=original;auto nearby=captured.view;nearby[12]+=.01;
    Matrix nearbyInverse,nearbyVpInverse;assert(Invert(nearby,nearbyInverse)&&Invert(Multiply(nearby,captured.projection),nearbyVpInverse));
    auto setSubmitted=[&](size_t o,const Matrix& m){for(unsigned i=0;i<16;++i)submitted[o+i]=float(m[i]);};
    setSubmitted(20,nearby);setSubmitted(36,nearbyInverse);setSubmitted(84,Multiply(nearby,captured.projection));setSubmitted(100,nearbyVpInverse);
    const std::array<size_t,1> one{20};assert(stereoreplay::FoldCameraAt(submitted,one,captured,right)==1);
    assert(Near(std::span(submitted).subspan(84),expected,1e-4,.001));
    auto unsupported=original;Camera missing;
    assert(stereoreplay::FoldCamera(unsupported,left,missing)==0&&unsupported==original);
    unsupported=original;unsupported[20+64]+=1;
    const auto malformed=unsupported;
    assert(stereoreplay::FoldCameraAt(unsupported,offsets,left,right)==0&&unsupported==malformed);
    const auto before=unsupported;assert(stereoreplay::FoldCamera(unsupported,left,right)==4);
    assert(!std::memcmp(unsupported.data()+20,before.data()+20,112*4));
    std::puts("Synchronous replay camera: five validated records, full XR eye transform/FOV, inverse/history consistency, standalone view preservation and rejection passed");
}
