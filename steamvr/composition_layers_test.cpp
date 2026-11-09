#include "composition_layers.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace framevr::composition;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
// Reproduce the observed bridge allocator, including its incorrect reuse of
// the first element's size. Conversion preserves fields except native handles.
bool SurvivesBridge(const XrCompositionLayerBaseHeader* const* layers,uint32_t count) {
    const auto size=[](XrStructureType t){return t==XR_TYPE_COMPOSITION_LAYER_QUAD?sizeof(XrCompositionLayerQuad):sizeof(XrCompositionLayerProjection);};
    const size_t stride=size(layers[0]->type);
    std::vector<unsigned char> arena(count*stride+sizeof(XrCompositionLayerQuad));
    for(uint32_t i=0;i<count;++i)std::memcpy(arena.data()+i*stride,layers[i],size(layers[i]->type));
    for(uint32_t i=0;i<count;++i)if(std::memcmp(arena.data()+i*stride,layers[i],size(layers[i]->type)))return false;
    return true;
}
// SteamVR's copies, one per place (StablePlaces' comment): a frame placed as
// transport places it, a stand-in showing what its place showed (else the
// pointer's transparent texel). Returns the copies made or remade.
struct Copies{std::array<Place,MaxPlaces> kept{};uint32_t count=0;};
const auto Eye=reinterpret_cast<XrSwapchain>(uintptr_t(0x3000)),Atlas=reinterpret_cast<XrSwapchain>(uintptr_t(0x3100)),
           Pointer=reinterpret_cast<XrSwapchain>(uintptr_t(0x3200));
Place Quad(XrSwapchain chain,XrRect2Di rect){return {XR_TYPE_COMPOSITION_LAYER_QUAD,chain,rect,true};}
int Submit(Copies& s,const std::vector<Place>& want,uint32_t capacity,std::vector<uint32_t>* places=nullptr) {
    std::array<uint8_t,MaxPlaces> at{};
    const uint32_t total=StablePlaces(want.data(),uint32_t(want.size()),s.kept.data(),s.count,capacity,at.data());
    Check(total>=want.size()&&total<=capacity,"the layers placed within the runtime's limit");
    std::array<Place,MaxPlaces> shown{};std::array<bool,MaxPlaces> taken{};
    for(size_t k=0;k<want.size();++k) {
        Check(!taken[at[k]]&&(k==0||at[k]>at[k-1]),"one layer a place, each above the ones before it");
        taken[at[k]]=true;shown[at[k]]=want[k];
    }
    for(uint32_t j=0;j<total;++j)if(!taken[j])shown[j]=j<s.count&&s.kept[j].standIn?s.kept[j]:Quad(Pointer,{{0,32},{1,1}});
    int made=0;
    for(uint32_t j=0;j<total;++j){if(j>=s.count||!SameSize(s.kept[j],shown[j]))++made;s.kept[j]=shown[j];}
    s.count=total>s.count?total:s.count;
    if(places)places->assign(at.begin(),at.begin()+want.size());
    return made;
}
void StablePlacesChecks() {
    const Place scene{XR_TYPE_COMPOSITION_LAYER_PROJECTION,Eye,{{0,0},{2472,2472}},false};
    const Place right=Quad(Atlas,{{768,0},{96,96}}),left=Quad(Atlas,{{768,200},{96,96}}),wheel=Quad(Atlas,{{0,0},{768,768}});
    const Place laser=Quad(Pointer,{{0,0},{64,32}}),dot=Quad(Pointer,{{0,32},{64,64}});
    std::vector<Place> belt;for(int i=0;i<4;++i)belt.push_back(Quad(Atlas,{{i*128,943},{128,81}}));
    const std::vector<Place> play{scene,right,left},menu{scene,wheel,laser,dot};
    std::vector<Place> looking{scene};looking.insert(looking.end(),belt.begin(),belt.end());looking.push_back(right);looking.push_back(left);
    Copies s;std::vector<uint32_t> at;
    Check(Submit(s,play,16,&at)==3&&at==std::vector<uint32_t>{0,1,2},"the first frame in order from the bottom");
    // The wheel opening, as on 2026-11-06 21:04:37: its three quads went where
    // the skill icons were; now above them, the icons' places held.
    Check(Submit(s,menu,16,&at)==3&&at==std::vector<uint32_t>{0,3,4,5},"the wheel at new places");
    Check(SameSize(s.kept[1],right)&&SameSize(s.kept[2],left),"the icons' copies kept under stand-ins");
    Check(Submit(s,play,16,&at)==0&&at==std::vector<uint32_t>{0,1,2},"closing the wheel remakes nothing");
    Check(Submit(s,menu,16)==0,"opening it again remakes nothing");
    // Looking down: the belt's labels go below the icons, so the icons move up once.
    Check(Submit(s,looking,16)==6,"the belt's labels and the icons above them at new places, once");
    for(int round=0;round<3;++round) {
        Check(Submit(s,play,16)==0&&Submit(s,looking,16)==0&&Submit(s,menu,16)==0,"looking down and up, the wheel open and shut: nothing remade");
    }
    Check(s.count<=16&&SameSize(s.kept[0],scene),"the world stays at the bottom");
    // Short of places the old order comes back: each layer straight above the last.
    Copies few;
    Check(Submit(few,play,4)==3&&Submit(few,menu,4,&at)==3&&at==std::vector<uint32_t>{0,1,2,3},"four places: the wheel over the icons");
    std::array<uint8_t,MaxPlaces> unused{};
    Check(StablePlaces(menu.data(),4,few.kept.data(),few.count,3,unused.data())==0,"more layers than places: not placed");
    Check(StablePlaces(menu.data(),0,few.kept.data(),few.count,16,unused.data())==0,"no layers: nothing placed");
}
int main(){
    StablePlacesChecks();
    XrCompositionLayerProjection scene{XR_TYPE_COMPOSITION_LAYER_PROJECTION};scene.viewCount=2;
    XrCompositionLayerQuad menu{XR_TYPE_COMPOSITION_LAYER_QUAD},laser{XR_TYPE_COMPOSITION_LAYER_QUAD},padding{};
    menu.pose.orientation.w=laser.pose.orientation.w=1;menu.size={.95f,.76f};laser.size={.003f,1.5f};
    menu.subImage.imageRect={{0,0},{1280,1024}};laser.subImage.imageRect={{0,0},{64,32}};
    const auto base=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&scene);
    const auto atlas=reinterpret_cast<XrSwapchain>(uintptr_t(0x1000));
    const auto space=reinterpret_cast<XrSpace>(uintptr_t(0x2000));
    std::array<const XrCompositionLayerBaseHeader*,4> layers{base,
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&laser)};
    Check(!SurvivesBridge(layers.data(),3),"reproduces overlapping quad corruption");
    Check(NeedsQuadPadding(true,base,atlas),"mixed Wine frame needs padding");
    Check(!NeedsQuadPadding(false,base,atlas),"native Windows unchanged");
    Check(!NeedsQuadPadding(true,layers[1],atlas),"flat menu already sizes quads correctly");
    uint32_t count=3;
    Check(PrependQuadPadding(layers.data(),count,layers.size(),padding,space,atlas),"prefix added");
    Check(count==4&&layers[1]==base&&layers[2]==reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu),"scene and overlay order retained");
    Check(SurvivesBridge(layers.data(),count),"mixed frame survives actual bridge allocation rule");
    Check(padding.pose.orientation.w==1&&padding.subImage.imageRect.offset.y==32&&padding.subImage.imageRect.extent.width==1,"valid transparent prefix");
    Check(!PrependQuadPadding(layers.data(),count,layers.size(),padding,space,atlas),"capacity respected");
    count=1;layers[0]=base;
    Check(!PrependQuadPadding(layers.data(),count,layers.size(),padding,space,atlas)&&count==1,"scene-only frame adds nothing");
    std::puts("Composition: reproduced Wine quad overlap; padding preserves fields, order, capacity and native path");
    std::puts("Composition: layers keep places of their size; the wheel and the belt's labels remake nothing after their first time");
}
