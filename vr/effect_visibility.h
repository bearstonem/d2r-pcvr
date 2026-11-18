#pragma once
#include "portal_billboard.h"

namespace billboards::visibility {
struct Stream {uint64_t data=0;uint32_t count=0,stride=0;};
// Native effect.isRenderingEnabled_pCtxI: args+8 points to SceneContext;
// output is a packed bool32 stream (true=-1). Never edit the scene's flags.
template<class Read,class Write,class Hidden>unsigned Filter(Read read,Write write,uint64_t output,uint64_t args,uint64_t base,Hidden hidden){
    Stream stream{};uint64_t context=0;portal::Context c{};
    if(!read(&stream,output,sizeof stream)||!stream.data||!stream.count||stream.count>1024||
       (stream.stride!=0&&stream.stride!=4)||!read(&context,args+8,8)||!context||
       !read(&c,context,sizeof c)||!c.scene)return 0;
    // A shared constant cannot represent different decisions for mixed lanes.
    if(!stream.stride&&c.instance<0&&stream.count>1)return 0;
    struct Cached {int32_t id=-1;bool hidden=false;};Cached cache[8];unsigned changed=0;
    const unsigned count=stream.stride?stream.count:1;
    for(unsigned lane=0;lane<count;++lane){
        int32_t native=0,id=c.instance;const auto destination=stream.data+lane*uint64_t(stream.stride);
        if(!read(&native,destination,4)||!native)continue;
        if(id<0&&(lane>=c.count||!read(&id,c.indices+lane*4ull,4)))continue;
        if(id<0)continue;
        auto& entry=cache[uint32_t(id)%8];
        if(entry.id!=id){
            portal::Instance instance{};entry={id,false};
            entry.hidden=portal::Resolve(read,c,lane,base,instance)&&hidden(instance);
        }
        const int32_t off=0;
        if(entry.hidden&&write(destination,&off,4))++changed;
    }
    return changed;
}
}
