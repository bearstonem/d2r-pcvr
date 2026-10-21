#pragma once
#include "frame_math.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace framevr {
// Eyes by turns: the eye not drawn this frame is turned on the GPU from the
// head it was drawn at to this frame's, and goes to the runtime at this
// frame's pose, both eyes at one head as from a pair. Handed in at its own
// older pose instead, it jittered and lost convergence in every head turn on
// SteamVR (owner's Steam Frame, 2026-10-19). The turn alone is rotation only,
// as the runtime's own reprojection; with the scene's depth (TurnDepth) the
// eye is also moved, for walking.
//
// This frame's image coordinates (u right, v down, 0..1) to the drawn image's,
// homogeneous and row-major. OpenXR poses and FOVs: -Z ahead, +Y up.
inline std::array<float,9> TurnHomography(math::Quat drawn,math::Fov drawnFov,math::Quat now,math::Fov nowFov) {
    const auto multiply=[](const float* a,const float* b) {
        std::array<float,9> m{};
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)m[r*3+c]=a[r*3]*b[c]+a[r*3+1]*b[3+c]+a[r*3+2]*b[6+c];
        return m;
    };
    // this frame's pixel to its ray in this frame's view
    const float l=std::tan(nowFov.left),r=std::tan(nowFov.right),u=std::tan(nowFov.up),d=std::tan(nowFov.down);
    const float ray[9]={r-l,0,l, 0,d-u,u, 0,0,-1};
    // that ray in the drawn view: the columns are this frame's axes there
    const auto turn=math::Multiply(math::Conjugate(drawn),now);
    const auto x=math::Rotate(turn,{1,0,0}),y=math::Rotate(turn,{0,1,0}),z=math::Rotate(turn,{0,0,1});
    const float axes[9]={x.x,y.x,z.x, x.y,y.y,z.y, x.z,y.z,z.z};
    // the drawn view's ray to its pixel, times its depth along -Z
    const float L=std::tan(drawnFov.left),R=std::tan(drawnFov.right),U=std::tan(drawnFov.up),D=std::tan(drawnFov.down);
    const float pixel[9]={1/(R-L),0,L/(R-L), 0,-1/(U-D),-U/(U-D), 0,0,-1};
    const auto inDrawn=multiply(axes,ray);
    return multiply(pixel,inDrawn.data());
}

// The drawn view's space to this frame's view's: x_now = R x_drawn + t, rows
// of [R | t], from where each eye stood and how it was turned in the world.
inline std::array<float,12> TurnMove(math::Vec drawnAt,math::Quat drawn,math::Vec nowAt,math::Quat now) {
    const auto back=math::Conjugate(now);
    const auto turn=math::Multiply(back,drawn);
    const auto x=math::Rotate(turn,{1,0,0}),y=math::Rotate(turn,{0,1,0}),z=math::Rotate(turn,{0,0,1});
    const auto t=math::Rotate(back,drawnAt-nowAt);
    return {x.x,y.x,z.x,t.x, x.y,y.y,z.y,t.y, x.z,y.z,z.z,t.z};
}

// TurnMove for what the hero carries: the drawn eye taken along by the hero's
// step (heroWas to heroNow) and the body's turn (baseWas to baseNow), then to
// where this frame's eye is. With the head still on the body, no move at all.
inline std::array<float,12> TurnCarry(math::Vec drawnAt,math::Quat drawn,math::Vec heroWas,math::Quat baseWas,
                                      math::Vec heroNow,math::Quat baseNow,math::Vec nowAt,math::Quat now) {
    const auto body=math::Multiply(baseNow,math::Conjugate(baseWas));
    return TurnMove(heroNow+math::Rotate(body,drawnAt-heroWas),math::Multiply(body,drawn),nowAt,now);
}

// The scene's depth as the eye was drawn: a grid over the drawn image, each
// point put back at its distance and seen from this frame's eye, nearer
// points over farther ones. The drawn eye's projection is infinite reverse-Z:
// distance = nearPlane / depth, in the world's units as `move`. Where the
// grid leaves the target uncovered (its edges) the rotation-only turn stays.
//
// What the hero carries (the hands, the weapon, the body) is not fixed in the
// world: moved as the world, it trailed a frame of the hero's run behind the
// eye drawn fresh and smeared (owner, 2026-10-19). Points nearer than
// reach[0] (their distance along the view, as nearPlane) move by `carry`
// instead, the world as the hero's step and the body's turn took it; farther
// than reach[1] by `move`; blended between. reach {0,0}: all by `move`.
//
// The other eye, drawn this frame (Render's `drawn`): its picture moved
// across to this eye with its own depth, over this eye's older one, so what
// moves by itself - monsters, missiles - stands where the drawn eye shows it
// (owner, 2026-10-19: moving monsters). A cell whose texels' depths part by
// more than TurnEdgePixels as seen from `apart` away lies across an edge: it
// goes as two flat layers, at its nearest depth and its farthest, each pixel
// in the one its own depth is nearer. What that eye could not see behind the
// edge, between them, keeps the older picture. Cut whole instead, such cells
// left 4-pixel steps of the older picture along every edge, stale where a
// head, a hand or a sword had moved since, and a blade thinner than a cell
// went as the ground behind it: ghosting on heads and swords (owner,
// 2026-10-20).
struct TurnDepth {
    ID3D12Resource* depth=nullptr;   // in a shader-resource state
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;   // its shader-readable format
    float scale[2]{1,1};       // the part of it the scene was drawn into: DLSS draws into a corner
    float nearPlane=0;
    std::array<float,12> move{};   // TurnMove
    math::Fov drawnFov{},nowFov{};
    std::array<float,12> carry{};   // TurnMove of the carried
    float reach[2]{0,0};
    ID3D12Resource* image=nullptr;   // the drawn eye's picture (PIXEL_SHADER_RESOURCE); null: the source's
    float apart=0;   // the drawn eye: how far it stood from this one, in the world's units
    // The drawn eye's: where this eye's own older picture, moved with its own
    // depth (Render's `depth`), shows the same surface - within `keep` pixels
    // of parallax - it stays; the drawn eye's goes only where they part: what
    // moved since, what this eye's own could not show. The game's glow round a
    // bright blade and a monster's hair have no depth of their own: moved
    // across with the depth behind them they stood where the drawn eye saw
    // them, a ghost blade with a glowing rim beside the sword, a second
    // outline of hair round a head (owner's videos, 2026-10-20). This eye's
    // own shows them from this eye. Carrying a near thing's depth over them
    // instead dragged the ground and the grass round everything near along:
    // a heat haze (owner, the same day). 0: the drawn eye's everywhere.
    float keep=0;
    // Kept everywhere, this eye was a frame behind the other all over - the
    // grass, flames and monsters' motion - which the owner liked less than
    // the ghosts. With keepNear only within keepReach target pixels of
    // something nearer by more than keepNear pixels of parallax, as either
    // eye sees it: the hands, the weapon, a monster close by, where a ghost
    // is wide enough to see. 0: everywhere.
    float keepNear=0,keepReach=0;
    // Spells, flames and Tyrael's wings are the game's effects layer, with no
    // depth of their own either: moved across with the depth behind them they
    // stood where the drawn eye saw them and the scene showed through them,
    // right only within the near things' reach round the sword (owner,
    // 2026-10-20). With keepEffects, where the layer (TurnWarp::Effects) has
    // anything from this level up - as the drawn eye drew it, round where its
    // picture comes from, or as this eye did, round here - this eye's own
    // picture stays as well. 0: off; with keepNear neither off, either keeps.
    float keepEffects=0;
};
inline constexpr float TurnEdgePixels=2;
// The grid's spacing in target pixels: an edge in depth stays within a few.
inline constexpr uint32_t TurnGridStep=4;
// TurnDepth::keepNear's blocks, target pixels square: the nearest depth in each.
inline constexpr uint32_t TurnNearBlock=16;
// The game's interface (labels over monsters, names, chat) has no depth of its
// own. Moved with the scene behind it into the eye not drawn, each label was
// misplaced and cut where a monster's edge parts the depth, a clean one every
// other frame: nameplates flickered (a player, through the owner, 2026-10-19).
// By turns it is taken out of the game's picture (hud::TakeInterface) and laid
// back over each eye (Interface): premultiplied colour, alpha the world showing
// through, as the game's own last pass lays it - so in the picture's UNORM
// encoding (UnormView), never through an sRGB view, which blends in linear
// light and lightens the label boxes.
inline constexpr std::array<float,9> TurnIdentity{1,0,0, 0,1,0, 0,0,1};

class TurnWarp {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root,depthRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline,depthPipeline,layerPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sourceHeap,targetHeap,depthHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthTarget;   // this eye's grid's own depth test, the target's size (R32 typeless: read by the next)
    Microsoft::WRL::ComPtr<ID3D12Resource> drawnDepthTarget;   // the drawn eye's grid's
    Microsoft::WRL::ComPtr<ID3D12Resource> ownTurned;   // the target as this eye's grid left it (TurnDepth::keep)
    // TurnDepth::keepNear: the nearest depth within reach of each block, the
    // drawn eye's and this eye's, and one between (R32_FLOAT).
    Microsoft::WRL::ComPtr<ID3D12RootSignature> nearRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> reducePipeline,spreadPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> nearTargets;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,3> nears;
    // TurnDepth::keepEffects: the strongest of each eye's effects layer within
    // reach of each block, as that eye was last drawn (Effects), then two
    // between (R32_FLOAT, TurnNearBlock pixels a texel).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> effectsPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> effectsTargets;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,4> effects;
    std::array<bool,2> effectsKept{};
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
public:
    // format: the eye swapchain's typed format, of the source and the target alike.
    // Any thread, for its own object: the shaders compile once for the process,
    // and an Initialize meeting that compile running waits and reuses it.
    bool Initialize(ID3D12Device*,DXGI_FORMAT format,void (*log)(const char*));
    bool Ready() const {return pipeline!=nullptr;}
    bool DepthReady() const {return depthPipeline!=nullptr;}
    bool NearReady() const {return spreadPipeline!=nullptr;}
    bool EffectsReady() const {return effectsPipeline!=nullptr&&spreadPipeline!=nullptr;}
    // The game's effects layer as the eye `slot` was drawn this frame: its two
    // targets, in `states` and left so, into that eye's blocks, each the
    // strongest of the layer within `reach` of its pixels (TurnDepth::keepEffects).
    bool Effects(ID3D12Device*,ID3D12GraphicsCommandList*,ID3D12Resource* const layer[2],const D3D12_RESOURCE_STATES states[2],int slot,float reach);
    void ForgetEffects(int slot){if(slot==0||slot==1)effectsKept[slot]=false;}
    // Render will take these: checked before a swapchain image is acquired for it.
    bool Fits(ID3D12Resource* source,ID3D12Resource* target) const;
    // source in PIXEL_SHADER_RESOURCE, target in RENDER_TARGET. slot 0 or 1, one
    // per eye: the GPU may still read the other eye's from the frame before.
    // With depth (and DepthReady) the eye is moved as well, and with `drawn`
    // the drawn eye's picture laid over it; false only when nothing was drawn.
    bool Render(ID3D12Device*,ID3D12GraphicsCommandList*,ID3D12Resource* source,ID3D12Resource* target,
                const std::array<float,9>& homography,int slot,const TurnDepth* depth=nullptr,const TurnDepth* drawn=nullptr);
    bool InterfaceReady() const {return layerPipeline!=nullptr;}
    // The interface layer (R16G16B16A16_FLOAT, PIXEL_SHADER_RESOURCE) over target
    // (RENDER_TARGET; the eye format's UnormView, or typeless), each pixel from
    // `homography` (this frame's image coordinates to the layer's; TurnIdentity
    // where it was drawn). Nothing is laid where the layer is not. at 0-3: its
    // own descriptor, two an eye, as the GPU may still read the others'.
    bool Interface(ID3D12Device*,ID3D12GraphicsCommandList*,ID3D12Resource* layer,ID3D12Resource* target,
                   const std::array<float,9>& homography,int at);
};
}
