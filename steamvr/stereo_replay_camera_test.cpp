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
    // An object CBV immediately before the camera can see the same complete
    // records in its 4 KiB tail. Ownership must select the actual closer
    // window regardless of submission order, without inventing CBV lengths.
    std::array<float,2048> pool;pool.fill(12345.f);
    std::copy(original.begin(),original.end(),pool.begin()+64);
    const auto broadWords=std::span<const float>(pool).first(1024);
    const auto cameraWords=std::span<const float>(pool).subspan(64,1024);
    const auto broadRecords=stereoreplay::FindCameraRecords(broadWords,left);
    const auto cameraRecords=stereoreplay::FindCameraRecords(cameraWords,left);
    assert(broadRecords.size()==5&&cameraRecords==offsets);
    using Role=std::pair<uint64_t,uint32_t>;
    const std::array<Role,1> objectRole{Role{11,0}},cameraRole{Role{22,0}},smallerRole{Role{33,1}};
    const std::array<stereoreplay::CameraWindow,2> beforeCamera{{{1,0,broadWords,broadRecords,objectRole},{1,256,cameraWords,cameraRecords,cameraRole}}};
    auto ownership=stereoreplay::OwnCameraRecords(beforeCamera);
    assert(ownership.consistent&&ownership.records[0].empty()&&ownership.records[1]==offsets);
    assert(!ownership.roles.contains(objectRole[0])&&ownership.roles.at(cameraRole[0])==offsets);
    const std::array<stereoreplay::CameraWindow,2> cameraBefore{{beforeCamera[1],beforeCamera[0]}};
    ownership=stereoreplay::OwnCameraRecords(cameraBefore);
    assert(ownership.consistent&&ownership.records[0]==offsets&&ownership.records[1].empty());
    assert(!ownership.roles.contains(objectRole[0])&&ownership.roles.at(cameraRole[0])==offsets);
    // Equal-base aliases (different root roles) retain all their validated
    // offsets. An incomplete closer window cannot steal a complete record.
    const std::array<stereoreplay::CameraWindow,2> aliases{{beforeCamera[1],{1,256,cameraWords,cameraRecords,smallerRole}}};
    ownership=stereoreplay::OwnCameraRecords(aliases);
    assert(ownership.consistent&&ownership.records[0]==offsets&&ownership.records[1]==offsets);
    assert(ownership.roles.at(cameraRole[0])==offsets&&ownership.roles.at(smallerRole[0])==offsets);
    const auto smallerWords=cameraWords.subspan(192,132);
    const auto smallerRecords=stereoreplay::FindCameraRecords(smallerWords,left);
    assert((smallerRecords==std::vector<size_t>{20}));
    const std::array<stereoreplay::CameraWindow,2> smaller{{beforeCamera[1],{1,256+192*4,smallerWords,smallerRecords,smallerRole}}};
    ownership=stereoreplay::OwnCameraRecords(smaller);
    assert(ownership.consistent&&(ownership.records[0]==std::vector<size_t>{20,404,596,788})&&ownership.records[1]==smallerRecords);
    assert(ownership.roles.at(cameraRole[0])==offsets&&ownership.roles.at(smallerRole[0])==smallerRecords);
    // If the closer root is absent next pair, the retained broad role must
    // still recognize its complete validated layout and cover that record.
    assert(stereoreplay::SelectCameraRecords(cameraWords,ownership.roles.at(cameraRole[0]),left)==offsets);
    const std::array<stereoreplay::CameraWindow,1> withoutCloser{{beforeCamera[1]}};
    const auto nextOwnership=stereoreplay::OwnCameraRecords(withoutCloser);
    assert(nextOwnership.consistent&&nextOwnership.records[0]==offsets);
    const auto truncatedWords=smallerWords.first(131);
    const auto truncatedRecords=stereoreplay::FindCameraRecords(truncatedWords,left);
    const std::array<stereoreplay::CameraWindow,2> incomplete{{beforeCamera[1],{1,256+192*4,truncatedWords,truncatedRecords}}};
    ownership=stereoreplay::OwnCameraRecords(incomplete);
    assert(ownership.consistent&&ownership.records[0]==offsets&&ownership.records[1].empty());
    // Keep a broad window's unique camera record even when other records are
    // owned elsewhere. Physical record addresses are scoped to each resource.
    std::copy(original.begin(),original.begin()+132,pool.begin());
    std::copy(original.begin(),original.end(),pool.begin()+256);
    const auto uniqueWords=std::span<const float>(pool).first(1024);
    const auto laterWords=std::span<const float>(pool).subspan(256,1024);
    const auto uniqueRecords=stereoreplay::FindCameraRecords(uniqueWords,left);
    const auto laterRecords=stereoreplay::FindCameraRecords(laterWords,left);
    assert(uniqueRecords.size()==5&&laterRecords==offsets);
    const std::array<stereoreplay::CameraWindow,2> unique{{{1,0,uniqueWords,uniqueRecords,objectRole},{1,1024,laterWords,laterRecords,cameraRole}}};
    ownership=stereoreplay::OwnCameraRecords(unique);
    assert(ownership.consistent&&(ownership.records[0]==std::vector<size_t>{20})&&ownership.records[1]==offsets);
    assert(ownership.roles.at(objectRole[0])==uniqueRecords&&ownership.roles.at(cameraRole[0])==offsets);
    // Different submitted windows on one retained root contribute the union
    // of all their validated offsets, rather than freezing the first layout.
    const std::array<size_t,1> relative20{20},relative212{212};
    const std::array<stereoreplay::CameraWindow,2> sharedRole{{{1,0,original,relative20,cameraRole},{2,0,original,relative212,cameraRole}}};
    ownership=stereoreplay::OwnCameraRecords(sharedRole);
    assert((ownership.roles.at(cameraRole[0])==std::vector<size_t>{20,212}));
    // A retained role also keeps a layout from a DIFFERENT window that was
    // wholly pruned this pair. That closer role may disappear next pair.
    const auto closeWords=std::span<const float>(original).subspan(192);
    const std::array<stereoreplay::CameraWindow,3> shadowedLayout{{
        sharedRole[0],sharedRole[1],{2,192*4,closeWords,relative20,smallerRole}}};
    ownership=stereoreplay::OwnCameraRecords(shadowedLayout);
    assert(ownership.consistent&&ownership.records[1].empty());
    assert((ownership.roles.at(cameraRole[0])==std::vector<size_t>{20,212}));
    assert(stereoreplay::SelectCameraRecords(original,ownership.roles.at(cameraRole[0]),left)==std::vector<size_t>({20,212}));
    const std::array<stereoreplay::CameraWindow,2> differentBuffers{{unique[1],{2,1024,laterWords,laterRecords}}};
    ownership=stereoreplay::OwnCameraRecords(differentBuffers);
    assert(ownership.consistent&&ownership.records[0]==offsets&&ownership.records[1]==offsets);
    auto changed=original;changed[20+96]+=1;
    const std::array<stereoreplay::CameraWindow,2> mutableAlias{{{1,0,original,offsets},{1,0,changed,offsets}}};
    assert(!stereoreplay::OwnCameraRecords(mutableAlias).consistent);
    // Every accepted cached record must survive the cheap upload prefilter.
    assert(stereoreplay::MayContainCachedCameraRecord(words,offsets,left));
    for(const size_t o:offsets){const std::array<size_t,1> single{o};assert(stereoreplay::MayContainCachedCameraRecord(words,single,left));}
    std::array<float,1024> unrelated;unrelated.fill(12345.f);
    assert(!stereoreplay::MayContainCachedCameraRecord(unrelated,offsets,left));
    const std::array<size_t,1> onlyFirst{20};
    auto nonfinite=original;nonfinite[20]=std::numeric_limits<float>::quiet_NaN();
    assert(!stereoreplay::MayContainCachedCameraRecord(nonfinite,onlyFirst,left));
    nonfinite=original;nonfinite[20+7]=std::numeric_limits<float>::infinity();
    assert(!stereoreplay::MayContainCachedCameraRecord(nonfinite,onlyFirst,left));
    // A plausible prefix still requires all sixteen view values to match.
    unrelated[20]=original[20];const std::array<size_t,1> unrelatedRecord{20};
    assert(!stereoreplay::MayContainCachedCameraRecord(unrelated,unrelatedRecord,left));
    auto nonfiniteView=original;nonfiniteView[20+15]=std::numeric_limits<float>::quiet_NaN();
    assert(!stereoreplay::MayContainCachedCameraRecord(nonfiniteView,unrelatedRecord,left));
    // A failed first record must not hide a later valid record on the same root.
    auto laterRecord=original;laterRecord[20]+=1;
    assert(!stereoreplay::CameraRecord(laterRecord,20,left));
    assert(stereoreplay::MayContainCachedCameraRecord(laterRecord,offsets,left));
    assert((stereoreplay::SelectCameraRecords(laterRecord,offsets,left)==std::vector<size_t>{212,404,596,788}));
    assert(!stereoreplay::MayContainCachedCameraRecord(std::span(laterRecord).first(323),offsets,left));
    assert(stereoreplay::MayContainCachedCameraRecord(std::span(laterRecord).first(324),offsets,left));
    // A matching standalone view is insufficient if its complete record does
    // not fit. Bounds checks must also tolerate stale or overflowing offsets.
    const std::array<size_t,1> firstRecord{20},standaloneView{980};
    assert(!stereoreplay::MayContainCachedCameraRecord(std::span(original).first(131),firstRecord,left));
    assert(stereoreplay::MayContainCachedCameraRecord(std::span(original).first(132),firstRecord,left));
    assert(stereoreplay::MayContainCachedCameraRecord(std::span(original).first(510),offsets,left));
    assert(!stereoreplay::MayContainCachedCameraRecord(original,standaloneView,left));
    const std::array<size_t,2> outOfRange{original.size(),std::numeric_limits<size_t>::max()};
    assert(!stereoreplay::MayContainCachedCameraRecord(original,outOfRange,left));
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
    submitted=original;auto nearby=captured.view;nearby[12]+=.01;nearby[0]+=.01;
    Matrix nearbyInverse,nearbyVpInverse;assert(Invert(nearby,nearbyInverse)&&Invert(Multiply(nearby,captured.projection),nearbyVpInverse));
    auto setSubmitted=[&](size_t o,const Matrix& m){for(unsigned i=0;i<16;++i)submitted[o+i]=float(m[i]);};
    setSubmitted(20,nearby);setSubmitted(36,nearbyInverse);setSubmitted(84,Multiply(nearby,captured.projection));setSubmitted(100,nearbyVpInverse);
    const std::array<size_t,1> one{20};
    assert(stereoreplay::CameraRecord(submitted,20,captured));
    assert(stereoreplay::MayContainCachedCameraRecord(submitted,one,captured));
    assert(stereoreplay::FoldCameraAt(submitted,one,captured,right)==1);
    assert(Near(std::span(submitted).subspan(84),expected,1e-4,.001));
    auto unsupported=original;Camera missing;
    assert(stereoreplay::FoldCamera(unsupported,left,missing)==0&&unsupported==original);
    unsupported=original;unsupported[20+64]+=1;
    const auto malformed=unsupported;
    // The prefilter deliberately cannot approve a projection or inverse. A
    // plausible view must still fail the existing full checks when malformed.
    assert(stereoreplay::MayContainCachedCameraRecord(unsupported,one,left));
    assert(stereoreplay::SelectCameraRecords(unsupported,one,left).empty());
    auto malformedProjection=original;malformedProjection[20+32]+=1;
    assert(stereoreplay::MayContainCachedCameraRecord(malformedProjection,one,left));
    assert(stereoreplay::SelectCameraRecords(malformedProjection,one,left).empty());
    auto malformedInverse=original;malformedInverse[20+16]+=1;
    assert(stereoreplay::MayContainCachedCameraRecord(malformedInverse,one,left));
    assert(stereoreplay::SelectCameraRecords(malformedInverse,one,left).empty());
    assert(!stereoreplay::MayContainCachedCameraRecord(original,one,missing));
    assert(stereoreplay::FoldCameraAt(unsupported,offsets,left,right)==0&&unsupported==malformed);
    const auto before=unsupported;assert(stereoreplay::FoldCamera(unsupported,left,right)==4);
    assert(!std::memcmp(unsupported.data()+20,before.data()+20,112*4));
    std::puts("Synchronous replay camera: five validated records, full XR eye transform/FOV, inverse/history consistency, standalone view preservation and rejection passed");
}
