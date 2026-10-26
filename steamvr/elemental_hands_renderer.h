#pragma once
#include "elemental_hands.h"
#include <d3d12.h>
#include <filesystem>
#include <memory>

namespace framevr::elements {
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Renderer();~Renderer();
    // Uploads the spell sprite cache (d2r_vr_spell_sprites beside the plugin
    // unless a directory is given) on the queue and waits for it. Without the
    // cache, hands keep the procedural aura.
    bool Initialize(ID3D12Device*,ID3D12CommandQueue*,DXGI_FORMAT,uint32_t width,uint32_t height,void (*log)(const char*),
                    const std::filesystem::path& sprites={});
    void Reset();
    bool Ready()const;
    bool Sprites()const; // drawing the spells' own effect textures
    // Target is in RENDER_TARGET state; caller has waited this eye's fence.
    // Prototype overlays the completed eye image, without scene-depth testing.
    bool Render(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned eye,
                interaction::Pose view,math::Fov,const Scene&);
};
}
