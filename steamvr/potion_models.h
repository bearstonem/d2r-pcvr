#pragma once
#include "potion_scene.h"
#include <d3d12.h>
#include <filesystem>
#include <memory>
namespace framevr::potions {
// A current scene-depth copy, in a shader-resource state. Pose/FOV belong to
// the eye that produced it; reprojection allows the other eye to use a fresh
// source in By turns. nearMetres is the GAME near plane converted to metres.
struct SceneDepth {
    ID3D12Resource* resource=nullptr;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    float scale[2]{1,1};float nearMetres=0;
    interaction::Pose view{};math::Fov fov{};
};
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
    unsigned Bones() const;  // Bone Armor pieces loaded (bn1..bn3; bone_graft.h)
    unsigned IcePieces() const; // original translucent plates, built in (optional ic1..ic3 overrides)
    void Reset();
    // Caller waited the eye's copy fence before reusing its RTV/depth resource.
    bool Render(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned eye,
                interaction::Pose view,math::Fov,const Scene&,const SceneDepth& depth={});
};
}
