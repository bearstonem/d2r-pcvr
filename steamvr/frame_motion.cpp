#include "frame_motion.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace framevr::framemotion {
namespace {
using Microsoft::WRL::ComPtr;
// Constants (frame_motion.h) as the cbuffer packs them, a float4 at a time.
constexpr char Shader[]=R"(
Texture2D<float> depth : register(t0);
Texture2D<float2> game : register(t1);   // the game's vectors (info.w > 0)
SamplerState nearest : register(s0);
cbuffer Motion : register(b0) {
    float4 back0;float4 back1;float4 back2;      // this view to the same eye's a frame back
    float4 carry0;float4 carry1;float4 carry2;   // the same for what the hero carries
    float4 was0;float4 was1;float4 was2;         // this view to the game's render before
    float4 nowTan;float4 backTan;float4 wasTan;  // tangents: left, right, down, up
    float4 info;    // near plane; the depth's drawn part x, y; renders of each point's own motion (0: none)
    float4 reach;   // carried nearer than x, the world's farther than y; own motion from z out, w pixels and up
    float4 size;    // the drawn part's texels x, y; z: one surface's vectors agree within this many pixels
};
struct Pixel {float4 position:SV_Position;float2 uv:TEXCOORD0;};
Pixel VS(uint id:SV_VertexID) {
    Pixel o;o.uv=float2((id<<1)&2,id&2);
    o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);
    return o;
}
float3 Apply(float4 r0,float4 r1,float4 r2,float3 p,float w) {
    return float3(dot(r0.xyz,p)+r0.w*w,dot(r1.xyz,p)+r1.w*w,dot(r2.xyz,p)+r2.w*w);
}
float2 Project(float3 p,float4 t) {return float2((-p.x/p.z-t.x)/(t.y-t.x),(t.w+p.y/p.z)/(t.w-t.z));}
// Where the point under this texel was in the same eye's image a frame back,
// less where it is now (uv): the nearest of the depth's four texels here
// (reverse-Z: the largest), so an edge goes with what is in front.
float2 PS(Pixel i):SV_Target {
    const float2 uv=i.uv,at=uv*info.yz;
    const float4 d4=depth.GatherRed(nearest,at);
    float4 gx=0,gy=0;
    if(info.w>0){gx=game.GatherRed(nearest,at);gy=game.GatherGreen(nearest,at);}
    int pick=0;
    [unroll] for(int j=1;j<4;++j)if(d4[j]>d4[pick])pick=j;
    const float n=d4[pick];
    const float2 g=float2(gx[pick],gy[pick]);
    float2 lo=g,hi=g;
    [unroll] for(int k=0;k<4;++k)if(d4[k]>=n*.98){lo=min(lo,float2(gx[k],gy[k]));hi=max(hi,float2(gx[k],gy[k]));}
    const bool agree=max((hi.x-lo.x)*size.x,(hi.y-lo.y)*size.y)<size.z;
    const float3 ray=float3(lerp(nowTan.x,nowTan.y,uv.x),lerp(nowTan.w,nowTan.z,uv.y),-1);
    if(n<=0) {   // the sky: turned only
        const float3 q=Apply(back0,back1,back2,ray,0);
        return q.z<0?Project(q,backTan)-uv:float2(0,0);
    }
    const float away=info.x/n;
    const float3 p=ray*away;
    const float free=saturate((away-reach.x)/max(reach.y-reach.x,1e-6));
    const float3 q=lerp(Apply(carry0,carry1,carry2,p,1),Apply(back0,back1,back2,p,1),free);
    if(q.z>=0)return float2(0,0);
    float2 m=Project(q,backTan)-uv;
    if(info.w>0&&away>reach.z&&agree) {   // its own motion, the camera's part taken out
        const float3 b=Apply(was0,was1,was2,p,1);
        if(b.z<0) {
            const float2 own=g-(uv-Project(b,wasTan));
            if(length(own*size.xy)>=reach.w)m-=own*info.w*free;
        }
    }
    return m;
}
)";
// Compiled once a process, as the turn warp's shaders: copied out of the
// compiler's blobs, which must not outlive d3dcompiler_47.
struct Compiled {bool compiler=false;std::vector<uint8_t> vs,ps;std::string errors;};
const Compiled& Shaders() {
    static std::once_flag once;static Compiled c;
    std::call_once(once,[]{
        const HMODULE module=LoadLibraryW(L"d3dcompiler_47.dll");
        const auto compile=module?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(module,"D3DCompile")):nullptr;
        if(compile)try {
            c.compiler=true;
            const auto build=[&](const char* entry,const char* target,std::vector<uint8_t>& out) {
                ComPtr<ID3DBlob> code,messages;
                if(SUCCEEDED(compile(Shader,sizeof(Shader)-1,"frame-motion",nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&messages))) {
                    const auto* p=static_cast<const uint8_t*>(code->GetBufferPointer());out.assign(p,p+code->GetBufferSize());return;
                }
                if(c.errors.empty()&&messages)c.errors=static_cast<const char*>(messages->GetBufferPointer());
            };
            build("VS","vs_5_0",c.vs);build("PS","ps_5_0",c.ps);
        }catch(...) {}
        if(module)FreeLibrary(module);
    });
    return c;
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;
    b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=before;b.Transition.StateAfter=after;
    return b;
}
constexpr auto Readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}
bool ShadersCompile(std::string* errors) {
    const auto& c=Shaders();
    if(errors)*errors=c.errors;
    return c.compiler&&!c.vs.empty()&&!c.ps.empty();
}
bool Pass::Initialize(ID3D12Device* device,void (*log)(const char*)) {
    if(pipeline)return true;
    auto fail=[&](const char* why){*this={};if(log){char b[160];std::snprintf(b,sizeof b,"Frame generation: game motion unavailable (%s); optical flow instead",why);log(b);}return false;};
    const auto& shaders=Shaders();
    const auto d12=GetModuleHandleW(L"d3d12.dll");
    const auto serialize=d12?reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12,"D3D12SerializeRootSignature")):nullptr;
    if(!shaders.compiler||!serialize)return fail("no shader compiler or root serializer");
    if(shaders.vs.empty()||shaders.ps.empty()){if(log&&!shaders.errors.empty())log(shaders.errors.c_str());return fail("shader compile failed");}
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={1,&range};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,ConstantCount};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
    ComPtr<ID3DBlob> signature,errors;
    if(FAILED(serialize(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors))||
       FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root))))return fail("root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=root.Get();pso.VS={shaders.vs.data(),shaders.vs.size()};pso.PS={shaders.ps.data(),shaders.ps.size()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN;
    pso.SampleMask=UINT_MAX;pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable=TRUE;pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets=1;pso.RTVFormats[0]=DXGI_FORMAT_R16G16_FLOAT;pso.SampleDesc.Count=1;
    if(FAILED(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipeline))))return fail("pipeline");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=4;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_DESCRIPTOR_HEAP_DESC rtv{};rtv.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rtv.NumDescriptors=2;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&sources)))||FAILED(device->CreateDescriptorHeap(&rtv,IID_PPV_ARGS(&targets))))return fail("descriptor heaps");
    return true;
}
bool Pass::Render(ID3D12Device* device,ID3D12GraphicsCommandList* list,int eye,ID3D12Resource* depth,DXGI_FORMAT depthFormat,D3D12_RESOURCE_STATES depthState,
                  ID3D12Resource* game,D3D12_RESOURCE_STATES gameState,const Constants& c) {
    if(!pipeline||!depth||(eye!=0&&eye!=1))return false;
    const auto d=depth->GetDesc();
    const uint32_t w=std::max(1u,uint32_t(std::ceil(float(d.Width)*c.scale[0]/2))),h=std::max(1u,uint32_t(std::ceil(float(d.Height)*c.scale[1]/2)));
    auto& target=vectors[size_t(eye)];
    if(!target||width[size_t(eye)]!=w||height[size_t(eye)]!=h) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;r.DepthOrArraySize=1;r.MipLevels=1;
        r.Format=DXGI_FORMAT_R16G16_FLOAT;r.SampleDesc.Count=1;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        target.Reset();
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,Readable,nullptr,IID_PPV_ARGS(&target))))return false;
        width[size_t(eye)]=w;height[size_t(eye)]=h;
    }
    const UINT step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=sources->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(eye)*2*step;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=1;srv.Format=depthFormat;
    device->CreateShaderResourceView(depth,&srv,cpu);
    cpu.ptr+=step;srv.Format=DXGI_FORMAT_R16G16_FLOAT;
    const bool vectorsIn=game&&c.renders>0;
    device->CreateShaderResourceView(vectorsIn?game:nullptr,&srv,cpu);
    auto rtv=targets->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr+=SIZE_T(eye)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(target.Get(),nullptr,rtv);
    D3D12_RESOURCE_BARRIER before[3]{};UINT n=0;
    before[n++]=Transition(target.Get(),Readable,D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(!(depthState&Readable))before[n++]=Transition(depth,depthState,Readable);
    if(vectorsIn&&!(gameState&Readable))before[n++]=Transition(game,gameState,Readable);
    list->ResourceBarrier(n,before);
    ID3D12DescriptorHeap* heaps[]={sources.Get()};list->SetDescriptorHeaps(1,heaps);
    list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(pipeline.Get());
    auto gpu=sources->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(eye)*2*step;
    list->SetGraphicsRootDescriptorTable(0,gpu);
    list->SetGraphicsRoot32BitConstants(1,ConstantCount,&c,0);
    const D3D12_VIEWPORT viewport{0,0,float(w),float(h),0,1};const D3D12_RECT scissor{0,0,LONG(w),LONG(h)};
    list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
    list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3,1,0,0);
    for(UINT i=0;i<n;++i)std::swap(before[i].Transition.StateBefore,before[i].Transition.StateAfter);
    list->ResourceBarrier(n,before);
    return true;
}
bool Pass::Clear(ID3D12Device* device,ID3D12GraphicsCommandList* list,int eye) {
    if(!pipeline||(eye!=0&&eye!=1)||!vectors[size_t(eye)])return false;
    auto* target=vectors[size_t(eye)].Get();
    auto rtv=targets->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr+=SIZE_T(eye)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(target,nullptr,rtv);
    auto b=Transition(target,Readable,D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(1,&b);
    const float none[4]{0,0,0,0};list->ClearRenderTargetView(rtv,none,0,nullptr);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    return true;
}
}
