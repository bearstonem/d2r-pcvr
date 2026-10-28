#pragma once
#include <d3d12.h>
#include <memory>
#include <vector>
#include "frame_math.h"
#include "interactions.h"
#include "d2r_vr_decapitation.h"

namespace framevr::decap {

class SeveredHeadRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    SeveredHeadRenderer();
    ~SeveredHeadRenderer();

    // Initializes D3D12 pipeline, root signature, compiled shaders, and 3D skull meshes.
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, DXGI_FORMAT format,
                    uint32_t width, uint32_t height, void (*log)(const char*));
    void Reset();
    bool Ready() const;

    // Renders active decoupled 3D severed heads into the eye render target.
    bool Render(ID3D12GraphicsCommandList* list, ID3D12Resource* target, unsigned eye,
                math::Vec viewPos, math::Quat viewRot, math::Fov fov,
                const std::vector<SeveredHeadProp>& heads,
                const interaction::Stage& stage, float upm);
};

} // namespace framevr::decap

