#include "../vr/frozen_armor_overlay.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <vector>
using namespace billboards;
void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Memory {
    std::map<uint64_t,std::vector<unsigned char>> blocks;
    bool Read(void* out,uint64_t p,size_t n)const{
        auto i=blocks.upper_bound(p);if(i==blocks.begin())return false;--i;
        auto off=p-i->first;if(off>i->second.size()||n>i->second.size()-off)return false;
        std::memcpy(out,i->second.data()+off,n);return true;
    }
    template<class T>void Put(uint64_t p,const T& v){std::vector<unsigned char>b(sizeof v);std::memcpy(b.data(),&v,sizeof v);blocks[p]=b;}
};
int main(){
    using namespace frozen_overlay;
    Check(Asset(Path),"archive-confirmed permanent particle shared by all three ice armors");
    Check(Asset("DATA\\HD\\VFX\\PARTICLES\\OVERLAYS\\SORCERESS\\SHIVERARMOR\\FX_ICE_SHIVERN_ARMOR.PARTICLES"),"case/separator variants");
    for(auto p:{"data/hd/vfx/particles/overlays/sorceress/frozenarmor_hit/fx_ice_frozenarmor_hit.particles",
        "data/hd/vfx/particles/overlays/sorceress/ice_icefrozenarmor/fx_ice_frozen_armor.particles",
        "data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_bonearmor_front.particles",""})
        Check(!Asset(p),"hit effect, unused lookalike, Bone Armor and empty path excluded");
    Check(PointMask(17)&&PointMask(19)&&!PointMask(0)&&!PointMask(1)&&!PointMask(18),"only native local position paths");
    Memory m;const uint64_t base=0x140000000;portal::Context c{};c.scene=0x10000;c.indices=0x18000;c.count=3;
    struct Pool{uint64_t pages,reserved;uint32_t count,capacity;};m.Put(c.scene+0x230,Pool{0x11000,0,1,1});
    m.Put(0x11000,uint64_t(0x12000));std::array<uint64_t,512> page{};
    for(unsigned i=0;i<3;++i){
        const uint64_t instance=0x20000+i*0x1000,def=0x30000+i*0x1000,compiled=0x40000+i*0x1000,strings=0x50000+i*0x1000,transform=0x60000+i*0x1000;
        page[i]=instance;std::array<uint64_t,10> header{};header[0]=base+0x1da1118;header[8]=def;header[9]=transform;m.Put(instance,header);
        header={};header[0]=base+0x1da0ff0;header[9]=compiled;m.Put(def,header);
        m.Put(compiled,std::array<uint64_t,3>{base+0x1da67f0,0,strings});m.Put(strings,base+0x1d9d258);
        std::array<char,160> path{};const auto text=i==2?std::string_view("data/hd/vfx/particles/overlays/sorceress/frozenarmor_hit/fx_ice_frozenarmor_hit.particles"):Path;
        std::memcpy(path.data(),text.data(),text.size());m.Put(strings+0x20,path);
        std::array<float,16> matrix{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=i==1?10.f:0.f;m.Put(transform,matrix);
    }
    m.Put(0x12000,page);m.Put(c.indices,std::array<int32_t,3>{0,1,2});
    auto read=[&](void* out,uint64_t p,size_t n){return m.Read(out,p,n);};
    portal::Vec points[]={{0,3,0},{0,3,0},{0,3,0}};
    Check(Hide(points,3,c,base,read,{0,0,0},true),"mixed batch corrected");
    Check(points[0].y==-197&&points[1].y==3&&points[2].y==3,"own permanent effect hidden; distant unit and hit effect unchanged");
    points[0]={0,3,0};Check(!Hide(points,3,c,base,read,{0,0,0},false)&&points[0].y==3,"inactive ice armor or disabled graft leaves shared particle native");
    Check(!Hide(points,65,c,base,read,{0,0,0},true),"oversized batch rejected");
    auto missing=[](void*,uint64_t,size_t){return false;};
    Check(!Hide(points,3,c,base,missing,{0,0,0},true),"unreadable instance leaves native effect");
    c.instance=0;points[0]={2,4,1};
    Check(Hide(points,1,c,base,read,{0,0,0},true)&&points[0].x==2&&points[0].y==-196&&points[0].z==1,"constant instance, arbitrary local points translated without changing shape");
    points[0]={0,std::numeric_limits<float>::quiet_NaN(),0};Check(!Hide(points,1,c,base,read,{0,0,0},true),"invalid coordinates untouched");
    std::puts("Frozen Armor overlay: scoped asset, active-state gate, owner proximity and mixed batches passed.");
}
