#pragma once
#include <openxr/openxr.h>

namespace framevr::composition {
// Proton Experimental's win64 layer-array converter (observed 2026-10-09)
// sizes EVERY output element from input[0]->type. Projection is 48 bytes;
// quad is 112. A projection followed by quads therefore overlaps outputs.
// Put an invisible quad below the scene, so each slot is large enough. This
// is valid OpenXR ordering and needs no changes to Proton or the runtime.
inline bool NeedsQuadPadding(bool wine, const XrCompositionLayerBaseHeader* first,
                             XrSwapchain transparentAtlas) {
    return wine&&first&&first->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION&&transparentAtlas;
}
inline bool PrependQuadPadding(const XrCompositionLayerBaseHeader** layers,
                              uint32_t& count,uint32_t capacity,
                              XrCompositionLayerQuad& padding,
                              XrSpace viewSpace,XrSwapchain transparentAtlas,
                              XrOffset2Di transparentTexel={0,32}) {
    if(count<=1||count>=capacity||!viewSpace||!transparentAtlas)return false;
    padding={XR_TYPE_COMPOSITION_LAYER_QUAD};
    padding.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|
                       XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    padding.space=viewSpace;padding.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    padding.pose.orientation.w=1;padding.pose.position.z=-1;
    padding.size={.001f,.001f};
    // A texel with zero alpha: the pointer's (0,32), where its atlas puts it.
    padding.subImage.swapchain=transparentAtlas;
    padding.subImage.imageRect={transparentTexel,{1,1}};
    for(uint32_t i=count;i>0;--i)layers[i]=layers[i-1];
    layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&padding);
    ++count;return true;
}
// SteamVR keeps its own copy of each layer, one per place in the frame's
// list, and remakes the copy when the picture at a place changes size (its
// xrclient log: "Created shared texture 'Scene resize ...'"). A quad that
// comes or goes moves every layer above it to another place, so the spell
// wheel opening, or the belt's labels as the head looks down, remade
// several. From the Steam Frame build of 2026-10-22 with SteamVR 2.19.2
// (2026-11-06) each remake flashed across the view, in this game only.
// So each layer goes to a place whose copy has its size, and a place left
// between holds an invisible stand-in: what it showed last, behind the head.
struct Place {
    XrStructureType type=XR_TYPE_UNKNOWN;
    XrSwapchain chain=XR_NULL_HANDLE;
    XrRect2Di rect{};
    bool standIn=false;   // its image lasts the session: a stand-in may show it again
};
constexpr uint32_t MaxPlaces=32;
inline Place PlaceOf(const XrCompositionLayerBaseHeader* layer) {
    Place p;if(!layer)return p;p.type=layer->type;
    if(layer->type==XR_TYPE_COMPOSITION_LAYER_QUAD) {
        const auto& q=*reinterpret_cast<const XrCompositionLayerQuad*>(layer);
        p.chain=q.subImage.swapchain;p.rect=q.subImage.imageRect;
    } else if(layer->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
        const auto& s=*reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if(s.viewCount&&s.views){p.chain=s.views[0].subImage.swapchain;p.rect=s.views[0].subImage.imageRect;}
    }
    return p;
}
inline bool SameSize(const Place& a,const Place& b) {
    return a.type!=XR_TYPE_UNKNOWN&&a.type==b.type&&
           a.rect.extent.width==b.rect.extent.width&&a.rect.extent.height==b.rect.extent.height;
}
// Places for want[0..n), in their order, among the first `capacity`: the
// fewest copies remade, then the fewest stand-ins. A place never used costs
// one copy made; one over another size costs a little more, since that size
// would be remade when it comes back. Returns the places used (the last + 1),
// or 0 when the layers do not fit.
inline uint32_t StablePlaces(const Place* want,uint32_t n,const Place* kept,uint32_t keptCount,
                             uint32_t capacity,uint8_t* placeOf) {
    constexpr int Remake=1000,Evict=10,StandIn=1,Never=1<<28;
    if(!n||capacity>MaxPlaces||n>capacity)return 0;
    const auto used=[&](uint32_t j){return j<keptCount&&kept[j].type!=XR_TYPE_UNKNOWN;};
    const auto layerCost=[&](uint32_t k,uint32_t j){return used(j)?(SameSize(kept[j],want[k])?0:Remake+Evict):Remake;};
    // A stand-in shows what the place showed, else a transparent texel (a remake).
    const auto fillCost=[&](uint32_t j){return used(j)?(kept[j].standIn?StandIn:Remake+Evict+StandIn):Remake+StandIn;};
    int cost[MaxPlaces][MaxPlaces];uint8_t from[MaxPlaces][MaxPlaces]{};
    for(auto& row:cost)for(int& c:row)c=Never;
    int fill=0;
    for(uint32_t j=0;j+n<=capacity;++j){cost[0][j]=fill+layerCost(0,j);fill+=fillCost(j);}
    for(uint32_t k=1;k<n;++k)for(uint32_t j=k;j+n-k<=capacity;++j) {
        int between=0;   // the stand-ins after place i and before j
        for(uint32_t i=j;i-->k-1;) {
            if(cost[k-1][i]<Never) {
                const int c=cost[k-1][i]+between+layerCost(k,j);
                if(c<cost[k][j]){cost[k][j]=c;from[k][j]=uint8_t(i);}
            }
            between+=fillCost(i);
        }
    }
    uint32_t last=n-1;
    for(uint32_t j=n;j<capacity;++j)if(cost[n-1][j]<cost[n-1][last])last=j;
    for(uint32_t k=n,j=last;k-->0;){placeOf[k]=uint8_t(j);if(k)j=from[k][j];}
    return last+1;
}
}
