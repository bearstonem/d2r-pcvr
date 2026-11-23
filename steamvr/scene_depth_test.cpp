#include "scene_depth.h"
#include "depth_barrier_compare.h"
#include <cassert>
#include <cstdio>
using namespace framevr::scenedepth;
static void BarrierBatches() {
    using reshade::api::resource;
    Candidates scene;
    const resource depth{0x1000};
    resource collision{depth.handle + 16}, other{depth.handle + 16};
    scene.Bind(1,depth.handle,512,512,true);
    const uint64_t limit = depth.handle + 65536;
    while (collision.handle < limit && !scene.MayTransition(1,&collision)) collision.handle += 16;
    while (other.handle < limit && scene.MayTransition(1,&other)) other.handle += 16;
    assert(collision.handle < limit && other.handle < limit);

    const resource batch[]{other,depth,depth};
    assert(!scene.MayTransition(0,nullptr));
    assert(!scene.MayTransition(1,batch));
    assert(scene.MayTransition(3,batch));
    for (uint32_t i=0;i<3;++i) scene.Barrier(1,batch[i].handle,i+1);
    assert(scene.Select(1,512,512).state==3); // last transition wins

    scene.Barrier(1,collision.handle,50);
    assert(scene.Select(1,512,512).state==3); // hash hit alone cannot change state
    scene.Bind(1,depth.handle,512,512,true);  // same entry bound twice
    scene.Bind(2,depth.handle,512,512,true);  // same handle, independent device
    scene.Bind(1,collision.handle,512,512,true);
    scene.Destroy(1,depth.handle);
    scene.Destroy(2);
    assert(scene.MayTransition(1,&collision));
    scene.EndEye(1);
    assert(scene.MayTransition(1,&collision)); // retain state for a possible next-eye bind
    scene.EndEye(1);
    assert(!scene.MayTransition(3,batch));
    assert(!scene.MayTransition(1,&collision));
    scene.Bind(1,depth.handle,512,512,true);
    assert(scene.MayTransition(3,batch));
    assert(!scene.Select(1,512,512).resource); // recycled handle needs a fresh transition

    // Overflow must not publish an extra reference for a handle the table cannot retain.
    scene.Destroy(1);
    for (uint64_t i=0;i<33;++i) scene.Bind(1+i%2,depth.handle+i*16,512,512,true);
    scene.Clear();
    for (uint64_t i=0;i<33;++i) {
        const resource released{depth.handle+i*16};
        assert(!scene.MayTransition(1,&released));
    }
}
int main(){
    compare::Block block;
    for(unsigned i=0;i<8;++i) {
        assert(block.index==i && block.Filtered()==(i%4==1||i%4==2));
        for(unsigned j=0;j<32;++j)block.Pair(j<4?500:25,j<4?400:20);
        assert(block.pairs==32 && block.cycleMs==28*25 && block.eyesMs==28*20);
        block.Next();
    }
    BarrierBatches();
    Candidates scene;
    auto bind=[&](uintptr_t device,uint64_t resource,uint32_t w,uint32_t h,int n=1){
        for(int i=0;i<n;++i)scene.Bind(device,resource,w,h,true);
        scene.Barrier(device,resource,uint32_t(reshade::api::resource_usage::shader_resource));
    };
    // A heavily bound square shadow target must not beat the actual scene.
    for(int i=0;i<100;++i)scene.Bind(1,10,4096,4096,false);
    scene.Barrier(1,10,uint32_t(reshade::api::resource_usage::shader_resource));
    assert(!scene.Select(1,4096,4096).resource); // exact-size shadow still rejected
    bind(1,20,2200,2200,3);
    assert(scene.Select(1,2200,2200).resource==20);
    scene.EndEye(1);assert(!scene.Select(1,2200,2200).resource);
    // Left-eye records are not a right-eye fallback. Unknown states are unsafe.
    scene.Bind(1,30,2200,2200,true);assert(!scene.Select(1,2200,2200).resource);
    scene.Barrier(1,30,uint32_t(reshade::api::resource_usage::depth_stencil_read));
    assert(scene.Select(1,2200,2200).resource==30);
    scene.Destroy(1,30);assert(!scene.Select(1,2200,2200).resource);
    scene.Bind(1,30,2200,2200,true);assert(!scene.Select(1,2200,2200).resource); // recycled address has no state
    scene.Destroy(1);
    // Preserve supersampling and non-square windows without accepting shadows.
    bind(1,40,3840,2160,4);scene.Bind(1,41,2048,2048,false);
    assert(scene.Select(1,1920,1080).resource==40);
    bind(1,42,960,540,5);assert(scene.Select(1,1920,1080).resource==42);
    bind(1,43,1920,1080);assert(scene.Select(1,1920,1080).resource==43); // exact preferred
    bind(2,43,1920,1080,100);assert(scene.Select(1,1920,1080).binds==1); // runtime device isolation
    assert(scene.Select(2,1920,1080).binds==100);
    scene.Destroy(1);assert(!scene.Select(1,1920,1080).resource&&scene.Select(2,1920,1080).resource);
    assert(!scene.Select(2,0,1080).resource);
    scene.EndEye(2);scene.EndEye(2);assert(!scene.Select(2,1920,1080).resource);
    bind(2,43,4096,4096);assert(scene.Select(2,4096,4096).resource==43); // exact square scene is allowed
    scene.Destroy(2);bind(2,44,2048,2048);
    assert(scene.Select(2,4096,4096).resource==44); // square DLSS scene is valid when color-paired
    std::puts("Scene depth: shadow rejection, scaling, per-eye freshness, known states and resource/device lifetimes passed");
}
