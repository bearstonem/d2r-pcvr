#pragma once
#include "elemental_hands.h"
#include <d3d12.h>
#include <memory>

namespace framevr::elements {
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Renderer();~Renderer();
    bool Initialize(ID3D12Device*,DXGI_FORMAT,uint32_t width,uint32_t height,void (*log)(const char*));
    void Reset();
    bool Ready()const;
    // Target is in RENDER_TARGET state; caller has waited this eye's fence.
    // Prototype overlays the completed eye image, without scene-depth testing.
    bool Render(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned eye,
                interaction::Pose view,math::Fov,const Scene&);
};
}
