#include "../vr/effect_visibility.h"
#include <map>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
struct Memory {
    std::map<uint64_t,unsigned char> bytes;
    void Put(uint64_t p,const void* v,size_t n){const auto b=static_cast<const unsigned char*>(v);for(size_t i=0;i<n;++i)bytes[p+i]=b[i];}
    template<class T>void Put(uint64_t p,const T& v){Put(p,&v,sizeof v);}
    bool Read(void* v,uint64_t p,size_t n){auto b=static_cast<unsigned char*>(v);for(size_t i=0;i<n;++i){auto it=bytes.find(p+i);if(it==bytes.end())return false;b[i]=it->second;}return true;}
};
int main(){
    using namespace billboards;Memory m;const uint64_t base=0x140000000,scene=0x10000;
    auto read=[&](void* out,uint64_t p,size_t n){return m.Read(out,p,n);};
    auto write=[&](uint64_t p,const void* v,size_t n){m.Put(p,v,n);return true;};
    uint64_t pool[3]={0x20000,0,uint64_t(512)<<32|1};m.Put(scene+0x230,pool);
    uint64_t page=0x21000;m.Put(0x20000,page);
    uint64_t instances[512]{};instances[3]=0x30000;instances[4]=0x31000;m.Put(page,instances);
    for(unsigned i=0;i<2;++i){uint64_t h[10]{};h[0]=base+0x1da1118;h[8]=0x40000+i*0x1000;h[9]=0x50000;m.Put(instances[3+i],h);}
    portal::Context c{};c.scene=scene;c.instance=-1;c.indices=0x60000;c.count=4;m.Put(0x61000,c);
    uint64_t context=0x61000;m.Put(0x62008,context);
    int32_t ids[]={3,4,3,4};m.Put(c.indices,ids);
    visibility::Stream stream{0x64000,4,4};m.Put(0x63000,stream);
    int32_t native[]={-1,-1,0,-1};m.Put(stream.data,native);
    const auto before=m.bytes;unsigned calls=0;
    auto hidden=[&](const portal::Instance& i){++calls;return i.address==instances[3];};
    assert(visibility::Filter(read,write,0x63000,0x62000,base,hidden)==1&&calls==2);
    int32_t values[4];assert(read(values,stream.data,sizeof values));
    assert(values[0]==0&&values[1]==-1&&values[2]==0&&values[3]==-1);
    for(const auto& [p,v]:before)if(p<stream.data||p>=stream.data+16)assert(m.bytes[p]==v); // only bool results changed
    m.bytes=before;c.instance=3;m.Put(context,c);stream={0x64000,1,0};m.Put(0x63000,stream);
    assert(visibility::Filter(read,write,0x63000,0x62000,base,hidden)==1);
    m.bytes=before;stream.stride=0;stream.count=4;m.Put(0x63000,stream);
    assert(visibility::Filter(read,write,0x63000,0x62000,base,hidden)==0); // shared constant in mixed batch
    m.bytes=before;stream.stride=12;m.Put(0x63000,stream);
    assert(visibility::Filter(read,write,0x63000,0x62000,base,hidden)==0); // wrong native ABI
    stream.stride=4;stream.count=1025;m.Put(0x63000,stream);
    assert(visibility::Filter(read,write,0x63000,0x62000,base,hidden)==0);
    m.bytes=before;m.bytes.erase(0x62008);
    assert(visibility::Filter(read,write,0x63000,0x62000,base,hidden)==0);
    std::puts("Effect visibility: bool ABI, mixed owners, native false, batch cache, constants, bounds, missing reads and scene preservation passed");
}
