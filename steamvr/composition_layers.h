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
}
