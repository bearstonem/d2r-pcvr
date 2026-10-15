#include "elemental_hands_renderer.h"
#include "dxgi_views.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <climits>
#include <stdexcept>
#include <string>

namespace framevr::elements {
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Check(HRESULT result,const char* message){Require(SUCCEEDED(result),message);}
// Original procedural geometry and shading; no external art or native VFX.
constexpr char Source[]=R"(
cbuffer HandScene:register(b0) {
    row_major float4x4 projection;
    float4 rotation;
    float4 translation; // w = hand index
    float4 options;     // time, element, opacity, cast pulse
    float4 up;          // room up in eye space; w = encode sRGB
};
float hash(float n){return frac(sin(n*127.1+311.7)*43758.5453);}
float3 rotate(float3 p){return p+2*cross(rotation.xyz,cross(rotation.xyz,p)+rotation.w*p);}
float3 arcPoint(float arc,float step,float tick) {
    float seed=arc*31+translation.w*137;
    float angle=arc*1.5707963+step*.23;
    float radius=.048+hash(seed+step*7+tick)*.014;
    return float3(cos(angle)*radius,(step/8-.5)*.12,sin(angle)*radius-.025);
}
struct Pixel {float4 position:SV_Position;float2 uv:TEXCOORD0;float life:TEXCOORD1;};
Pixel VS(uint vertex:SV_VertexID,uint particle:SV_InstanceID) {
    const float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
    float2 corner=corners[vertex];
    float seed=particle+translation.w*53;
    float phase=frac(options.x*.8+hash(seed));
    float angle=hash(seed+8)*6.2831853;
    float3 p;
    Pixel o;o.uv=corner;o.life=1;
    if(options.y>2.5) {
        float arc=floor(particle/8),segment=particle%8,tick=floor(options.x*12);
        float3 a=rotate(arcPoint(arc,segment,tick))+translation.xyz;
        float3 b=rotate(arcPoint(arc,segment+1,tick))+translation.xyz;
        float2 direction=b.xy-a.xy;
        float2 normal=float2(-direction.y,direction.x)/max(length(direction),.00001);
        p=lerp(a,b,corner.y*.5+.5);
        p.xy+=normal*corner.x*.0035;
        o.life=.45+.55*hash(arc*19+tick+translation.w*7);
    } else {
        float radius=.045+hash(seed+3)*.025;
        float3 local=float3(cos(angle)*radius,(hash(seed+14)-.5)*.09,sin(angle)*radius-.025);
        p=rotate(local)+translation.xyz;
        float2 size;
        if(options.y<1.5) {
            p+=up.xyz*(phase*.085);
            p+=rotate(float3(sin(options.x*6+seed)*.006,0,0));
            size=float2(.014,.026)*(1-phase*.55);
            if(particle%4==0)size*=.3;
            o.life=sin(phase*3.14159265);
        } else {
            p+=rotate(float3(sin(options.x*2+seed),cos(options.x*2+seed),0))*.009;
            size=float2(.024,.018);
            o.life=.4+.6*sin(phase*3.14159265);
        }
        p.xy+=corner*size;
    }
    o.position=mul(float4(p,1),projection);
    return o;
}
float3 srgb(float3 c){return lerp(12.92*c,1.055*pow(max(c,0),1/2.4)-.055,step(.0031308,c));}
float4 PS(Pixel i):SV_Target {
    float shape;float3 color;
    if(options.y<1.5) {
        float width=1-(i.uv.y*.5+.5)*.65;
        float2 uv=float2(i.uv.x/width,i.uv.y);
        shape=pow(saturate(1-dot(uv,uv)),2);
        color=lerp(float3(1,.12,.008),float3(1,.7,.08),saturate(-i.uv.y));
    } else if(options.y<2.5) {
        shape=pow(saturate(1-dot(i.uv,i.uv)),3);
        color=float3(.22,.64,1);
    } else {
        shape=pow(saturate(1-abs(i.uv.x)),2);
        color=lerp(float3(.18,.3,1),float3(.7,.85,1),shape);
    }
    float alpha=shape*i.life*options.z*(.24+.30*options.w);
    if(up.w>.5)color=srgb(color);
    return float4(color,alpha);
}
)";
}
struct Renderer::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> rtv;
    uint32_t width=0,height=0,stride=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool encode=false;
};
Renderer::Renderer()=default;Renderer::~Renderer()=default;
void Renderer::Reset(){impl.reset();}
bool Renderer::Ready()const{return impl!=nullptr;}
bool Renderer::Initialize(ID3D12Device* device,DXGI_FORMAT format,uint32_t width,uint32_t height,void (*log)(const char*)) {
    if(impl)return true;
    try {
        Require(device&&width&&height,"invalid eye target");
        Require(format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
            format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,"requires an SDR eye target");
        auto p=std::make_unique<Impl>();p->device=device;p->format=format;p->width=width;p->height=height;
        p->encode=format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM;
        struct Library {HMODULE h=LoadLibraryW(L"d3dcompiler_47.dll");~Library(){if(h)FreeLibrary(h);}} library;
        const auto compile=library.h?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(library.h,"D3DCompile")):nullptr;
        const auto d12=GetModuleHandleW(L"d3d12.dll");
        const auto serialize=d12?reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12,"D3D12SerializeRootSignature")):nullptr;
        Require(compile&&serialize,"shader compiler unavailable");
        ComPtr<ID3DBlob> vs,ps,errors,signature;
        auto shader=[&](const char* entry,const char* target,ComPtr<ID3DBlob>& out){
            errors.Reset();
            const auto result=compile(Source,sizeof Source,"elemental-hands",nullptr,nullptr,entry,target,
                D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&out,&errors);
            if(FAILED(result)&&errors&&log)log(static_cast<const char*>(errors->GetBufferPointer()));
            Check(result,"shader compilation failed");
        };
        shader("VS","vs_5_0",vs);shader("PS","ps_5_0",ps);
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants={0,0,32};parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC root{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
        errors.Reset();Check(serialize(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"root serialization failed");
        Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&p->root)),"root creation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=p->root.Get();
        state.VS={vs->GetBufferPointer(),vs->GetBufferSize()};state.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        auto& blend=state.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
        blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ZERO;blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        state.SampleMask=UINT_MAX;state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;
        state.DepthStencilState.DepthEnable=FALSE;state.DepthStencilState.StencilEnable=FALSE;
        state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;state.NumRenderTargets=1;
        state.RTVFormats[0]=format;state.SampleDesc.Count=1;
        Check(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&p->pipeline)),"pipeline creation failed");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=2;
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p->rtv)),"target descriptors failed");
        p->stride=device->GetDescriptorHandleIncrementSize(heap.Type);impl=std::move(p);
        if(log)log("Frame elemental hands: procedural stereo aura renderer ready (prototype overlay)");
        return true;
    } catch(const std::exception& e) {
        if(log){const std::string message=std::string("Frame elemental hands: ")+e.what()+"; effects unavailable";log(message.c_str());}
        return false;
    }
}
bool Renderer::Render(ID3D12GraphicsCommandList* list,ID3D12Resource* target,unsigned eye,
                      interaction::Pose view,math::Fov fov,const Scene& scene) {
    if(!impl||!list||!target||eye>1||!scene.Visible()||!interaction::Valid(view)||!std::isfinite(scene.time))return false;
    if(!std::isfinite(fov.left+fov.right+fov.down+fov.up)||fov.left>=fov.right||fov.down>=fov.up||
        std::max({std::fabs(fov.left),std::fabs(fov.right),std::fabs(fov.down),std::fabs(fov.up)})>=1.57f)return false;
    auto& p=*impl;const auto desc=target->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.DepthOrArraySize!=1||desc.Width!=p.width||
       desc.Height!=p.height||desc.SampleDesc.Count!=1||!(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)||
       !RenderTargetCompatible(desc.Format,p.format))return false;
    auto rtv=p.rtv->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=eye*p.stride;
    D3D12_RENDER_TARGET_VIEW_DESC targetView{};targetView.Format=p.format;targetView.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    p.device->CreateRenderTargetView(target,&targetView,rtv);list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(p.width),float(p.height),0,1};
    const D3D12_RECT scissor{0,0,LONG(p.width),LONG(p.height)};
    list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
    list->SetGraphicsRootSignature(p.root.Get());list->SetPipelineState(p.pipeline.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const auto inverse=math::Conjugate(view.q);
    const auto up=math::Rotate(inverse,{0,1,0});
    bool drawn=false;
    for(unsigned h=0;h<2;++h) {
        const auto& hand=scene.hands[h];
        if(hand.element==Element::None||unsigned(hand.element)>unsigned(Element::Lightning)||!interaction::Valid(hand.pose)||
           !std::isfinite(hand.opacity+hand.pulse)||hand.opacity<=0)continue;
        float constants[32]{};math::Projection(fov.left,fov.right,fov.down,fov.up,.02f,constants);
        const auto rotation=math::Multiply(inverse,hand.pose.q);
        const auto translation=math::Rotate(inverse,hand.pose.p-view.p);
        constants[16]=rotation.x;constants[17]=rotation.y;constants[18]=rotation.z;constants[19]=rotation.w;
        constants[20]=translation.x;constants[21]=translation.y;constants[22]=translation.z;constants[23]=float(h);
        constants[24]=scene.time;constants[25]=float(hand.element);constants[26]=std::clamp(hand.opacity,0.f,1.f);constants[27]=std::clamp(hand.pulse,0.f,1.f);
        constants[28]=up.x;constants[29]=up.y;constants[30]=up.z;constants[31]=p.encode?1:0;
        list->SetGraphicsRoot32BitConstants(0,32,constants,0);list->DrawInstanced(6,32,0,0);drawn=true;
    }
    return drawn;
}
}
