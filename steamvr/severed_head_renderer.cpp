#include "severed_head_renderer.h"
#include "dxgi_views.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace framevr::decap {

using Microsoft::WRL::ComPtr;

namespace {

struct Vertex {
    float p[3];
    float n[3];
    float flags; // 1.0 = arterial blood droplet, 2.0 = cranial bone shard, 3.0 = visceral flesh chunk
};

struct Asset {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

void Require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

void Check(HRESULT hr, const char* error) {
    Require(SUCCEEDED(hr), error);
}

inline math::Quat AxisAngle(math::Vec axis, float angle) {
    const float half = angle * 0.5f;
    const float s = std::sin(half);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

// 3D diamond (octahedron) blood droplet for arterial fountains and dripping gore
Asset BuildDropletAsset() {
    Asset a;
    constexpr float kH = 0.018f;
    constexpr float kR = 0.009f;

    auto add = [&](float x, float y, float z, float nx, float ny, float nz) {
        Vertex v{};
        v.p[0] = x; v.p[1] = y; v.p[2] = z;
        v.n[0] = nx; v.n[1] = ny; v.n[2] = nz;
        v.flags = 1.0f; // blood droplet
        a.vertices.push_back(v);
    };

    add( 0.0f,  kH,  0.0f,  0.0f,  1.0f,  0.0f); // 0: Top
    add( 0.0f, -kH,  0.0f,  0.0f, -1.0f,  0.0f); // 1: Bottom
    add(  kR,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f); // 2: Right
    add( 0.0f,  0.0f,   kR,  0.0f,  0.0f,  1.0f); // 3: Front
    add( -kR,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f); // 4: Left
    add( 0.0f,  0.0f,  -kR,  0.0f,  0.0f, -1.0f); // 5: Back

    a.indices = {
        0, 2, 3,   0, 3, 4,   0, 4, 5,   0, 5, 2,
        1, 3, 2,   1, 4, 3,   1, 5, 4,   1, 2, 5
    };
    return a;
}

// Sharp faceted 3D cranial bone shard
Asset BuildBoneShardAsset() {
    Asset a;
    auto add = [&](float x, float y, float z, float nx, float ny, float nz) {
        Vertex v{};
        v.p[0] = x; v.p[1] = y; v.p[2] = z;
        v.n[0] = nx; v.n[1] = ny; v.n[2] = nz;
        v.flags = 2.0f; // bone shard
        a.vertices.push_back(v);
    };

    // Sharp jagged cranial splinter
    add( 0.002f,  0.024f,  0.001f,  0.1f,  0.9f,  0.1f); // 0: Apex Top
    add(-0.002f, -0.016f, -0.002f, -0.1f, -0.9f, -0.1f); // 1: Apex Bottom
    add( 0.013f,  0.003f,  0.004f,  0.9f,  0.1f,  0.2f); // 2: Mid 0
    add( 0.003f,  0.007f,  0.011f,  0.2f,  0.2f,  0.9f); // 3: Mid 1
    add(-0.010f, -0.002f,  0.006f, -0.8f, -0.1f,  0.4f); // 4: Mid 2
    add(-0.007f,  0.005f, -0.009f, -0.5f,  0.2f, -0.8f); // 5: Mid 3
    add( 0.008f, -0.003f, -0.007f,  0.7f, -0.2f, -0.6f); // 6: Mid 4

    a.indices = {
        // Top cap
        0, 2, 3,   0, 3, 4,   0, 4, 5,   0, 5, 6,   0, 6, 2,
        // Bottom cap
        1, 3, 2,   1, 4, 3,   1, 5, 4,   1, 6, 5,   1, 2, 6
    };
    return a;
}

// Visceral 3D flesh & tissue chunk
Asset BuildFleshChunkAsset() {
    Asset a;
    auto add = [&](float x, float y, float z, float nx, float ny, float nz) {
        Vertex v{};
        v.p[0] = x; v.p[1] = y; v.p[2] = z;
        v.n[0] = nx; v.n[1] = ny; v.n[2] = nz;
        v.flags = 3.0f; // flesh chunk
        a.vertices.push_back(v);
    };

    // Irregular visceral meat lump
    add( 0.002f,  0.016f,  0.001f,  0.1f,  0.9f,  0.1f); // 0: Top
    add(-0.001f, -0.015f, -0.002f, -0.1f, -0.9f, -0.1f); // 1: Bottom
    add( 0.014f,  0.003f,  0.003f,  0.9f,  0.2f,  0.1f); // 2: Ring 0
    add( 0.006f, -0.005f,  0.013f,  0.4f, -0.3f,  0.8f); // 3: Ring 1
    add(-0.011f,  0.004f,  0.010f, -0.7f,  0.2f,  0.6f); // 4: Ring 2
    add(-0.014f, -0.002f, -0.003f, -0.9f, -0.1f, -0.2f); // 5: Ring 3
    add(-0.005f,  0.005f, -0.012f, -0.3f,  0.3f, -0.8f); // 6: Ring 4
    add( 0.009f, -0.004f, -0.010f,  0.6f, -0.2f, -0.7f); // 7: Ring 5

    a.indices = {
        // Upper cone
        0, 2, 3,   0, 3, 4,   0, 4, 5,   0, 5, 6,   0, 6, 7,   0, 7, 2,
        // Lower cone
        1, 3, 2,   1, 4, 3,   1, 5, 4,   1, 6, 5,   1, 7, 6,   1, 2, 7
    };
    return a;
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, const void* bytes, size_t size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&result)),
          "gore buffer allocation failed");

    if (bytes) {
        void* mapped = nullptr;
        D3D12_RANGE empty{0, 0};
        Check(result->Map(0, &empty, &mapped), "gore buffer map failed");
        std::memcpy(mapped, bytes, size);
        result->Unmap(0, nullptr);
    }
    return result;
}

} // namespace

struct SeveredHeadRenderer::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap;
    ComPtr<ID3D12Resource> depth[2];

    struct GpuMesh {
        ComPtr<ID3D12Resource> vb, ib;
        D3D12_VERTEX_BUFFER_VIEW vbView{};
        D3D12_INDEX_BUFFER_VIEW ibView{};
        uint32_t indexCount = 0;
    };

    GpuMesh dropletMesh;
    GpuMesh boneShardMesh;
    GpuMesh fleshChunkMesh;

    uint32_t width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t rtvStride = 0, dsvStride = 0;
    bool encodeSrgb = false;
};

SeveredHeadRenderer::SeveredHeadRenderer() = default;
SeveredHeadRenderer::~SeveredHeadRenderer() = default;
void SeveredHeadRenderer::Reset() { impl.reset(); }
bool SeveredHeadRenderer::Ready() const { return impl != nullptr; }

bool SeveredHeadRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, DXGI_FORMAT format,
                                     uint32_t w, uint32_t h, void (*log)(const char*)) {
    if (impl) return true;

    try {
        auto p = std::make_unique<Impl>();
        p->device = device;
        p->width = w;
        p->height = h;
        p->format = format;
        p->encodeSrgb = (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM);

        struct CompilerLib {
            HMODULE h = LoadLibraryW(L"d3dcompiler_47.dll");
            ~CompilerLib() { if (h) FreeLibrary(h); }
        } compiler;
        auto compile = compiler.h ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler.h, "D3DCompile")) : nullptr;
        const auto d12 = GetModuleHandleW(L"d3d12.dll");
        auto serialize = d12 ? reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12, "D3D12SerializeRootSignature")) : nullptr;
        Require(compile && serialize, "D3D shader compiler unavailable for gore renderer");

        constexpr char shaderSource[] = R"(
cbuffer GoreScene : register(b0) {
    row_major float4x4 projection;
    float4 rotation;
    float4 translation; // xyz = view position, w = scale
    float4 light;       // xyz = view light dir
    float4 options;     // x = type flag override, y = alpha intensity, z = unused, w = encode sRGB
};

struct VSInput {
    float3 p : POSITION;
    float3 n : NORMAL;
    float flags : TEXCOORD0;
};

struct PSInput {
    float4 p : SV_Position;
    float3 n : NORMAL;
    float3 view : TEXCOORD0;
    float flags : COLOR0;
};

float3 Rotate(float3 v) {
    return v + 2.0 * cross(rotation.xyz, cross(rotation.xyz, v) + rotation.w * v);
}

PSInput VS(VSInput v) {
    PSInput o;
    float3 viewPos = Rotate(v.p * translation.w) + translation.xyz;
    o.view = viewPos;
    o.p = mul(float4(viewPos, 1.0), projection);
    o.n = Rotate(v.n);
    o.flags = (options.x > 0.5) ? options.x : v.flags;
    return o;
}

float4 PS(PSInput v) : SV_Target {
    float3 n = normalize(v.n);
    float3 eye = normalize(-v.view);
    float3 l = normalize(light.xyz);
    float3 h = normalize(l + eye);

    float diff = saturate(dot(n, l));
    float3 base;
    float3 specColor;
    float spec;
    float fresnel = pow(1.0 - saturate(dot(n, eye)), 3.0);

    float var = options.z;
    if (v.flags > 2.5) {
        // 3.0: Torn flesh: deep maroon meat with wet, blood-slick highlights
        base = lerp(float3(0.10, 0.012, 0.010), float3(0.22, 0.035, 0.030), var);
        spec = pow(saturate(dot(n, h)), 48.0) * 0.55;
        specColor = float3(0.55, 0.10, 0.08);
    } else if (v.flags > 1.5) {
        // 2.0: Skull fragment: off-white bone, inner face soaked in blood
        float stain = saturate(0.35 + 0.65 * (0.5 - 0.5 * n.y) + 0.3 * (var - 0.5));
        float3 bone = lerp(float3(0.42, 0.36, 0.28), float3(0.30, 0.24, 0.18), var);
        base = lerp(bone, float3(0.12, 0.012, 0.010), stain);
        spec = pow(saturate(dot(n, h)), 32.0) * lerp(0.12, 0.45, stain);
        specColor = lerp(float3(0.35, 0.32, 0.28), float3(0.50, 0.08, 0.06), stain);
    } else {
        // 1.0: Fresh arterial blood: dark, saturated and glossy
        base = lerp(float3(0.075, 0.004, 0.004), float3(0.16, 0.010, 0.008), var);
        spec = pow(saturate(dot(n, h)), 80.0) * 0.85;
        specColor = float3(0.60, 0.12, 0.10);
    }

    float3 color = base * (0.18 + 0.82 * diff) + spec * specColor + fresnel * (base * 0.8);

    if (options.w > 0.5) {
        color = lerp(1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055, color * 12.92, color <= 0.0031308);
    }
    return float4(color, 1.0);
}
)";

        ComPtr<ID3DBlob> vs, ps, errors, signature;
        Check(compile(shaderSource, sizeof(shaderSource) - 1, "severed-head", nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &errors),
              "gore VS compilation failed");
        Check(compile(shaderSource, sizeof(shaderSource) - 1, "severed-head", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors),
              "gore PS compilation failed");

        D3D12_ROOT_PARAMETER params[1]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants = {0, 0, 32};
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{1, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        Check(serialize(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "gore root serialization failed");
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p->root)),
              "gore root creation failed");

        const D3D12_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = p->root.Get();
        psoDesc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        psoDesc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        psoDesc.InputLayout = {elements, 3};
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL; // Reverse-Z matching D2R
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = format;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&p->pipeline)),
              "gore pipeline creation failed");

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 2;
        Check(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&p->rtvHeap)), "gore RTV heap failed");
        p->rtvStride = device->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 2;
        Check(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&p->dsvHeap)), "gore DSV heap failed");
        p->dsvStride = device->GetDescriptorHandleIncrementSize(dsvHeapDesc.Type);

        D3D12_HEAP_PROPERTIES gpuHeap{};
        gpuHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = w;
        depthDesc.Height = h;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = depthDesc.Format;
        depthClear.DepthStencil = {0.0f, 0}; // Reverse-Z: 0 is far plane

        for (unsigned i = 0; i < 2; ++i) {
            Check(device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                                 D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                                 IID_PPV_ARGS(&p->depth[i])),
                  "gore depth buffer creation failed");
            auto dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
            dsv.ptr += i * p->dsvStride;
            device->CreateDepthStencilView(p->depth[i].Get(), nullptr, dsv);
        }

        auto uploadMesh = [&](const Asset& a, Impl::GpuMesh& out) {
            out.vb = Buffer(device, a.vertices.data(), a.vertices.size() * sizeof(Vertex));
            out.ib = Buffer(device, a.indices.data(), a.indices.size() * 4);
            out.vbView = {out.vb->GetGPUVirtualAddress(), UINT(a.vertices.size() * sizeof(Vertex)), sizeof(Vertex)};
            out.ibView = {out.ib->GetGPUVirtualAddress(), UINT(a.indices.size() * 4), DXGI_FORMAT_R32_UINT};
            out.indexCount = UINT(a.indices.size());
        };

        uploadMesh(BuildDropletAsset(), p->dropletMesh);
        uploadMesh(BuildBoneShardAsset(), p->boneShardMesh);
        uploadMesh(BuildFleshChunkAsset(), p->fleshChunkMesh);

        impl = std::move(p);
        if (log) {
            log("Frame gore: Cranial burst & arterial geyser renderer initialized (Option 1: Clean stump + bone shards & dark visceral gore)");
        }
        return true;
    } catch (const std::exception& e) {
        if (log) {
            std::string msg = "Frame gore: severed head renderer init failed: ";
            msg += e.what();
            log(msg.c_str());
        }
        return false;
    }
}

bool SeveredHeadRenderer::Render(ID3D12GraphicsCommandList* list, ID3D12Resource* target, unsigned eye,
                                 math::Vec viewPos, math::Quat viewRot, math::Fov fov,
                                 const std::vector<SeveredHeadProp>& heads,
                                 const interaction::Stage& stage, float upm) {
    if (!impl || eye > 1 || heads.empty() || !stage.known || !target) return false;

    auto& p = *impl;
    const auto desc = target->GetDesc();
    if (desc.Width != p.width || desc.Height != p.height || desc.SampleDesc.Count != 1 ||
        !RenderTargetCompatible(desc.Format, p.format)) {
        return false;
    }

    // Set render target and clear private stereo depth buffer
    auto rtv = p.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += eye * p.rtvStride;
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = p.format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    p.device->CreateRenderTargetView(target, &rtvDesc, rtv);

    auto dsv = p.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += eye * p.dsvStride;
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const D3D12_VIEWPORT vp{0.0f, 0.0f, float(p.width), float(p.height), 0.0f, 1.0f};
    const D3D12_RECT rect{0, 0, LONG(p.width), LONG(p.height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &rect);

    list->SetGraphicsRootSignature(p.root.Get());
    list->SetPipelineState(p.pipeline.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const float scaleFactor = (upm > 0.5f && upm < 15.0f) ? (1.0f / upm) : 1.0f;
    const auto inverse = math::Conjugate(viewRot);

    for (const auto& head : heads) {
        if (head.elapsedSeconds >= 4.0f) continue;

        // Origin at the severed neck stump
        math::Vec stumpWorld = {
            head.neckStumpPos[0] * scaleFactor,
            head.neckStumpPos[1] * scaleFactor,
            head.neckStumpPos[2] * scaleFactor
        };
        const float stumpDistSq = stumpWorld.x * stumpWorld.x + stumpWorld.y * stumpWorld.y + stumpWorld.z * stumpWorld.z;
        if (stumpDistSq < 0.0001f) {
            stumpWorld = {
                head.worldPos[0] * scaleFactor,
                head.worldPos[1] * scaleFactor,
                head.worldPos[2] * scaleFactor
            };
        }
        math::Vec stumpRoom = stage.Room(stumpWorld);

        const auto light = math::Rotate(inverse, math::Normalized({-0.4f, 0.8f, 0.3f}));

        float constants[32]{};
        math::Projection(fov.left, fov.right, fov.down, fov.up, 0.02f, constants);
        constants[24] = light.x;
        constants[25] = light.y;
        constants[26] = light.z;
        constants[31] = p.encodeSrgb ? 1.0f : 0.0f;

        const float t = head.elapsedSeconds;
        const math::Vec impulse = {head.velocity[0], head.velocity[1], head.velocity[2]};
        const float bodyScale = std::clamp(head.scale, 0.5f, 2.0f);

        // The neck sits roughly 1.1 m above the floor for a humanoid of scale 1.
        // Everything lands on this plane instead of freezing in mid-air.
        const float neckHeight = 1.1f * bodyScale;
        const float floorY = stumpRoom.y - neckHeight;

        // The corpse collapses during its death animation, so the stump (the
        // emitter) follows it down instead of spurting from an empty point in space.
        auto stumpAt = [&](float time) {
            const float k = std::clamp((time - 0.25f) / 0.65f, 0.0f, 1.0f);
            const float ease = k * k * (3.0f - 2.0f * k);
            math::Vec s = stumpRoom;
            s.y = stumpRoom.y - ease * neckHeight * 0.78f;
            s.x += impulse.x * 0.08f * ease;
            s.z += impulse.z * 0.08f * ease;
            return s;
        };

        // Fade-out shrink for anything still on screen near the end of the effect.
        const float lifeFade = std::clamp((4.0f - t) / 0.9f, 0.0f, 1.0f);

        auto draw = [&](const Impl::GpuMesh& mesh, float flag, math::Vec room, math::Quat rot,
                        float scale, float variation) {
            if (scale <= 0.001f) return;
            const math::Vec v = math::Rotate(inverse, room - viewPos);
            const math::Quat r = math::Multiply(inverse, rot);
            constants[16] = r.x; constants[17] = r.y; constants[18] = r.z; constants[19] = r.w;
            constants[20] = v.x; constants[21] = v.y; constants[22] = v.z;
            constants[23] = scale;
            constants[28] = flag;
            constants[30] = variation;
            list->IASetVertexBuffers(0, 1, &mesh.vbView);
            list->IASetIndexBuffer(&mesh.ibView);
            list->SetGraphicsRoot32BitConstants(0, 32, constants, 0);
            list->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        };

        // Ballistic particle with air drag, floor contact and an optional bounce.
        // Returns false while the particle has not been emitted yet.
        struct Ballistic { math::Vec pos; float age; bool landed; float landedFor; };
        auto simulate = [&](math::Vec origin, math::Vec v0, float emitTime, float drag,
                            float bounce, Ballistic& out) {
            const float age = t - emitTime;
            if (age < 0.0f) return false;
            constexpr float g = 9.8f;
            const float h = std::max(0.0f, origin.y - floorY);
            const float hit = (v0.y + std::sqrt(v0.y * v0.y + 2.0f * g * h)) / g;
            auto horiz = [&](float a) { return drag > 0.0f ? (1.0f - std::exp(-drag * a)) / drag : a; };
            if (age < hit) {
                const float hx = horiz(age);
                out = {{origin.x + v0.x * hx, origin.y + v0.y * age - 0.5f * g * age * age, origin.z + v0.z * hx},
                       age, false, 0.0f};
                return true;
            }
            const float hx = horiz(hit);
            math::Vec land = {origin.x + v0.x * hx, floorY, origin.z + v0.z * hx};
            float after = age - hit;
            if (bounce > 0.0f) {
                // One damped hop, then slide to rest.
                const float vy = (g * hit - v0.y) * bounce;
                const float hop = 2.0f * vy / g;
                const float decay = drag > 0.0f ? std::exp(-drag * hit) : 1.0f;
                const float slide = std::min(after, hop + 0.25f);
                const float sh = (1.0f - std::exp(-6.0f * slide)) / 6.0f;
                land.x += v0.x * decay * bounce * sh;
                land.z += v0.z * decay * bounce * sh;
                if (after < hop) land.y = floorY + vy * after - 0.5f * g * after * after;
            }
            out = {land, age, true, after};
            return true;
        };

        auto hash = [](int k, int salt) {
            const float s = std::sin(float(k) * 12.9898f + float(salt) * 78.233f) * 43758.5453f;
            return s - std::floor(s);
        };

        // 1. Cranial burst: bone shards fly out with the strike, bounce once and rest.
        for (int k = 0; k < 14; ++k) {
            const float phi = hash(k, 1) * 6.2831853f;
            const float spd = 0.9f + 1.4f * hash(k, 2);
            const math::Vec v0 = {impulse.x * 0.55f + std::cos(phi) * spd,
                                  1.0f + 1.6f * hash(k, 3) + impulse.y * 0.3f,
                                  impulse.z * 0.55f + std::sin(phi) * spd};
            Ballistic b{};
            if (!simulate(stumpRoom, v0, 0.0f, 1.2f, 0.28f, b)) continue;
            const math::Vec axis = math::Normalized({hash(k, 4) - 0.5f, 0.7f, hash(k, 5) - 0.5f});
            // Spin slows to a stop once the shard settles.
            const float spinTime = b.landed ? (b.age - b.landedFor) + std::min(b.landedFor, 0.4f) * 0.5f : b.age;
            const math::Quat q = AxisAngle(axis, spinTime * (9.0f + 8.0f * hash(k, 6)) + hash(k, 7) * 6.28f);
            const float size = (0.45f + 0.55f * hash(k, 8)) * bodyScale;
            draw(p.boneShardMesh, 2.0f, b.pos, q, size * lifeFade, hash(k, 9));
        }

        // 2. Flesh chunks: heavier, no bounce, they splat and stay.
        for (int k = 0; k < 9; ++k) {
            const float phi = hash(k, 11) * 6.2831853f;
            const float spd = 0.6f + 1.0f * hash(k, 12);
            const math::Vec v0 = {impulse.x * 0.45f + std::cos(phi) * spd,
                                  0.8f + 1.2f * hash(k, 13) + impulse.y * 0.25f,
                                  impulse.z * 0.45f + std::sin(phi) * spd};
            Ballistic b{};
            if (!simulate(stumpRoom, v0, 0.0f, 0.9f, 0.0f, b)) continue;
            const math::Vec axis = math::Normalized({0.6f, hash(k, 14) - 0.5f, hash(k, 15) - 0.5f});
            const float spin = b.landed ? b.age - b.landedFor : b.age;
            const math::Quat q = AxisAngle(axis, spin * 7.0f + hash(k, 16) * 6.28f);
            const float size = (0.55f + 0.6f * hash(k, 17)) * bodyScale;
            draw(p.fleshChunkMesh, 3.0f, b.pos + math::Vec{0.0f, b.landed ? 0.006f : 0.0f, 0.0f}, q,
                 size * lifeFade, hash(k, 18));
        }

        // 3. Arterial jets: every droplet is emitted once from the (falling) stump,
        // with cardiac pulses and pressure that drops off quickly. No looping.
        constexpr int kJet = 48;
        constexpr float kJetWindow = 1.6f;
        for (int k = 0; k < kJet; ++k) {
            const float emit = kJetWindow * float(k) / float(kJet) + 0.02f * hash(k, 21);
            const float pressure = std::exp(-emit / 0.55f);
            const float pulse = 0.55f + 0.45f * std::max(0.0f, std::sin(emit * 11.0f));
            const float phi = hash(k, 22) * 6.2831853f;
            const float cone = 0.25f + 0.35f * hash(k, 23);
            const float up = (1.1f + 1.3f * pulse) * pressure + 0.15f;
            const math::Vec v0 = {std::cos(phi) * cone * pressure + impulse.x * 0.15f * pressure,
                                  up,
                                  std::sin(phi) * cone * pressure + impulse.z * 0.15f * pressure};
            Ballistic b{};
            if (!simulate(stumpAt(emit), v0, emit, 0.6f, 0.0f, b)) continue;
            // Airborne drops are elongated by size; pooled drops spread a little and fade.
            const float pool = b.landed ? std::min(1.0f, b.landedFor / 0.25f) : 0.0f;
            const float size = (0.7f + 0.6f * hash(k, 24)) * (1.0f + 0.6f * pool) * bodyScale;
            draw(p.dropletMesh, 1.0f, b.pos + math::Vec{0.0f, b.landed ? -0.004f : 0.0f, 0.0f},
                 math::Quat{0, 0, 0, 1}, size * lifeFade, hash(k, 25));
        }

        // 4. Heavy drips running off the stump once the jets weaken.
        for (int k = 0; k < 18; ++k) {
            const float emit = 0.5f + 2.0f * float(k) / 18.0f + 0.05f * hash(k, 31);
            const float spread = 0.04f * bodyScale;
            const math::Vec origin = stumpAt(emit) + math::Vec{(hash(k, 32) - 0.5f) * spread, 0.0f,
                                                               (hash(k, 33) - 0.5f) * spread};
            Ballistic b{};
            if (!simulate(origin, {0.0f, -0.1f, 0.0f}, emit, 0.0f, 0.0f, b)) continue;
            const float size = (0.6f + 0.5f * hash(k, 34)) * bodyScale;
            draw(p.dropletMesh, 1.0f, b.pos, math::Quat{0, 0, 0, 1}, size * lifeFade, hash(k, 35));
        }

        // 5. Strike spray: fine droplets flung along the blade path in the first instant.
        for (int k = 0; k < 22; ++k) {
            const float phi = hash(k, 41) * 6.2831853f;
            const float spd = 0.5f + 0.9f * hash(k, 42);
            const math::Vec v0 = {impulse.x * (0.8f + 0.4f * hash(k, 43)) + std::cos(phi) * spd,
                                  0.6f + 1.0f * hash(k, 44) + impulse.y * 0.4f,
                                  impulse.z * (0.8f + 0.4f * hash(k, 43)) + std::sin(phi) * spd};
            Ballistic b{};
            if (!simulate(stumpRoom, v0, 0.0f, 1.8f, 0.0f, b)) continue;
            const float size = (0.35f + 0.4f * hash(k, 45)) * bodyScale;
            draw(p.dropletMesh, 1.0f, b.pos, math::Quat{0, 0, 0, 1}, size * lifeFade, hash(k, 46));
        }
    }

    return true;
}

} // namespace framevr::decap
