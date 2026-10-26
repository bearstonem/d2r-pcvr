#pragma once
#include "potion_scene.h"
#include <d3d12.h>
#include <filesystem>
#include <memory>
namespace framevr::potions {
class Models {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Models();~Models();
    // Loading/decoding/upload occurs only on session initialization.
    bool Initialize(ID3D12Device*,ID3D12CommandQueue*,DXGI_FORMAT,uint32_t width,uint32_t height,
                    void (*log)(const char*),const std::filesystem::path& directory={});
    bool Ready() const;
    bool HasArrow() const;   // the cache's arrow loaded: Scene items with ArrowCode draw it
    float Radius(uint32_t code) const; // widest horizontal extent of that model, metres; 0 unknown
    unsigned Tosses() const; // thrown trap models loaded (toss::Model codes; #224)
    void Reset();
    // Caller waited the eye's copy fence before reusing its RTV/depth resource.
    bool Render(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned eye,
                interaction::Pose view,math::Fov,const Scene&);
};
}
