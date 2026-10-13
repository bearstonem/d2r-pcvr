#include "stereo_camera_bindings.h"
#include <array>
#include <cassert>
#include <cstdio>

using namespace framevr::stereoreplay;
namespace {
constexpr CameraRole firstRole{10,1},secondRole{20,2};
constexpr CameraBindingKey window{100,256};
const std::array<size_t,1> firstLayout{20},secondLayout{212};
const CameraBindingUse& Use(const CameraBindingRegistry& registry,uint64_t list){
    const auto* uses=registry.List(list);assert(uses&&uses->contains(window));return uses->at(window);
}
void PerListAliasesAndLayouts(){
    CameraBindingRegistry registry;
    for(const bool resolved:{false,true}){
        registry.BeginPair(resolved);
        assert(registry.Add(1,100,256,4096,firstRole,firstLayout)==CameraBindingResult::Recorded);
        assert(registry.Add(2,100,256,4096,secondRole,secondLayout)==CameraBindingResult::Recorded);
        assert(registry.UniqueWindows()==1);
        assert(Use(registry,1).discoveryAliases==std::set<CameraRole>{firstRole});
        assert(Use(registry,2).discoveryAliases==std::set<CameraRole>{secondRole});
        assert(Use(registry,1).cachedOffsets==std::vector<size_t>{20});
        assert(Use(registry,2).cachedOffsets==std::vector<size_t>{212});
        assert(registry.Add(1,100,256,4096,secondRole,secondLayout)==CameraBindingResult::Recorded);
        assert((Use(registry,1).discoveryAliases==std::set<CameraRole>{firstRole,secondRole}));
        assert((Use(registry,1).cachedOffsets==std::vector<size_t>{20,212}));
        // Looking up a submitted list must neither consume its uses nor hide
        // another list using the same upload window. Every A/B/A submission
        // therefore remains observable by Execute's immutable-snapshot check.
        unsigned reads=0;bool changed=false;const unsigned original=7;
        for(const auto& [list,value]:std::array<std::pair<uint64_t,unsigned>,3>{{{1,7},{2,9},{1,7}}}){
            assert(Use(registry,list).buffer==100);++reads;changed|=value!=original;
        }
        assert(reads==3&&changed&&registry.UniqueWindows()==1);
    }
}
void RecentCacheLifecycle(){
    CameraBindingRegistry registry;RecentCameraBinding recent;
    registry.BeginPair(true);
    assert(!registry.RecentMatches(recent,1,512));
    assert(registry.Add(1,100,256,4096,firstRole,firstLayout)==CameraBindingResult::Recorded);
    registry.Remember(recent,1,512);
    assert(registry.RecentMatches(recent,1,512));
    assert(!registry.RecentMatches(recent,2,512));
    assert(!registry.RecentMatches(recent,1,768));
    registry.ResetList(1);
    assert(!registry.List(1)&&!registry.RecentMatches(recent,1,512));
    assert(registry.UniqueWindows()==1);
    assert(registry.Add(1,100,256,4096,firstRole,firstLayout)==CameraBindingResult::Recorded);
    registry.Remember(recent,1,512);
    registry.BeginPair(true);
    assert(!registry.List(1)&&registry.UniqueWindows()==0&&!registry.RecentMatches(recent,1,512));
    registry.Remember(recent,1,512);registry.Clear();
    assert(!registry.RecentMatches(recent,1,512));
    // Slot caches survive mode changes, so a distinct registry must not accept
    // another registry's matching list/address stamp either.
    registry.Remember(recent,1,512);CameraBindingRegistry other;
    assert(!other.RecentMatches(recent,1,512));
}
void WindowAndReferenceLimits(){
    CameraBindingRegistry registry;
    for(const auto& [resolved,count]:std::array<std::pair<bool,unsigned>,2>{{{false,64},{true,512}}}){
        registry.BeginPair(resolved);
        for(unsigned i=0;i<count;++i)assert(registry.Add(1,100,256ull*i,4096,firstRole)==CameraBindingResult::Recorded);
        assert(registry.Add(1,100,256ull*count,4096,firstRole)==CameraBindingResult::WindowCapacity);
        // A full distinct-window budget still permits another submitting list
        // to retain a reference to an existing window.
        assert(registry.Add(2,100,0,4096,firstRole)==CameraBindingResult::Recorded);
        assert(registry.UniqueWindows()==count&&registry.List(2)->contains({100,0}));
    }
    CameraBindingLimits limits;limits.discoveryWindows=1;limits.listReferences=2;
    CameraBindingRegistry bounded(limits);bounded.BeginPair(false);
    assert(bounded.Add(1,100,256,4096,firstRole)==CameraBindingResult::Recorded);
    assert(bounded.Add(2,100,256,4096,firstRole)==CameraBindingResult::Recorded);
    assert(bounded.Add(3,100,256,4096,firstRole)==CameraBindingResult::ReferenceCapacity);
    assert(!bounded.List(3));
    assert(bounded.Add(1,100,256,4096,secondRole)==CameraBindingResult::Recorded);
    bounded.ResetList(1);
    assert(bounded.Add(3,100,256,4096,firstRole)==CameraBindingResult::Recorded);
    assert(bounded.Add(2,200,256,4096,firstRole)==CameraBindingResult::WindowCapacity);
    assert(bounded.UniqueWindows()==1);
}
void RoleAndLayoutLimits(){
    CameraBindingRegistry registry;registry.BeginPair(false);
    for(unsigned i=0;i<64;++i)assert(registry.Add(1,100,256,4096,{10,i})==CameraBindingResult::Recorded);
    assert(registry.Add(1,100,256,4096,{10,64})==CameraBindingResult::RoleCapacity);
    assert(Use(registry,1).discoveryAliases.size()==64&&!Use(registry,1).discoveryAliases.contains({10,64}));
    CameraBindingLimits limits;limits.layoutOffsets=3;CameraBindingRegistry bounded(limits);
    bounded.BeginPair(true);const std::array<size_t,3> repeated{212,20,212};
    assert(bounded.Add(1,100,256,4096,firstRole,repeated)==CameraBindingResult::Recorded);
    const std::array<size_t,1> third{404},fourth{596};
    assert(bounded.Add(1,100,256,4096,secondRole,third)==CameraBindingResult::Recorded);
    assert(bounded.Add(1,100,256,4096,{30,3},fourth)==CameraBindingResult::LayoutCapacity);
    assert((Use(bounded,1).cachedOffsets==std::vector<size_t>{20,212,404}));
    assert(!Use(bounded,1).discoveryAliases.contains({30,3}));
}
}
int main(){
    PerListAliasesAndLayouts();RecentCacheLifecycle();WindowAndReferenceLimits();RoleAndLayoutLimits();
    std::puts("Camera binding registry: per-list retention, A/B/A observation, alias/layout unions, reset/mode cache invalidation and bounded failures passed");
}
