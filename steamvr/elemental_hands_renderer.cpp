#include "elemental_hands_renderer.h"
#include "dxgi_views.h"
#include "spell_sprites.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <climits>
#include <fstream>
#include <stdexcept>
#include <string>

namespace framevr::elements {
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Check(HRESULT result,const char* message){Require(SUCCEEDED(result),message);}
// Original procedural geometry and shading; no external art or native VFX.
// Hands use it when the spell sprite cache is unavailable.
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
        float forwardOffset=(options.y>4.5)?0.0:-.025;
        float3 local=float3(cos(angle)*radius,(hash(seed+14)-.5)*.09,sin(angle)*radius+forwardOffset);
        p=rotate(local)+translation.xyz;
        float2 size;
        if(options.y>4.5) {
            size=float2(.055,.100)*(1.0-phase*.35);
            p=translation.xyz+up.xyz*(size.y*.55);
            p+=up.xyz*(phase*.12);
            p+=rotate(float3(sin(options.x*5+seed)*.010,0,0));
            if(particle%3==0)size*=.45;
            o.life=sin(phase*3.14159265);
        } else if(options.y<1.5) {
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
    if(options.y<1.5||options.y>4.5) {
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
// A few small sprites per hand with the spells' own effect textures, from the
// installed game (spell_sprites.h), shaded through their own colour ramps.
constexpr unsigned SpritesPerHand=8;
constexpr char SpriteSource[]=R"(
cbuffer HandScene:register(b0) {
    row_major float4x4 projection;
    float4 rotation;
    float4 translation; // w = hand index
    float4 options;     // time, look, opacity, cast pulse
    float4 up;          // room up in eye space; w = encode sRGB
};
Texture2D<float> firePlate:register(t0);
Texture2D<float> fireballPlate:register(t1);
Texture2D<float> softFlame:register(t2);
Texture2D<float4> flameRamp:register(t3);
Texture2D<float> flash:register(t4);
Texture2D<float> glow:register(t5);
Texture2D<float4> lightningRamp:register(t6);
Texture2D<float> glint:register(t7);
Texture2D<float4> iceRamp:register(t8);
Texture2D<float4> frozenOrb:register(t9);
Texture2D<float4> frostRamp:register(t10);
SamplerState tiled:register(s0);
SamplerState edge:register(s1);
static const float Tau=6.2831853;
float hash(float n){return frac(sin(n*127.1+311.7)*43758.5453);}
float3 rotate(float3 p){return p+2*cross(rotation.xyz,cross(rotation.xyz,p)+rotation.w*p);}
// A point on a ring around the hand/emitter, centred on grip (bare hand) or on origin (torch).
float3 around(float angle,float radius,float height){return rotate(float3(cos(angle)*radius,height,sin(angle)*radius+((options.y>4.5)?0.0:-.03)))+translation.xyz;}
// Modes: 0 flame core, 1 ember or wisp, 2 glow, 3 lightning flash, 4 ice glint,
// 5 frozen orb. data: x ramp or texture choice, y intensity, z texture spin.
struct Pixel {float4 position:SV_Position;float2 uv:TEXCOORD0;float4 data:TEXCOORD1;nointerpolation uint mode:TEXCOORD2;};
Pixel VS(uint vertex:SV_VertexID,uint sprite:SV_InstanceID) {
    const float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
    const float2 corner=corners[vertex];
    const float t=options.x,look=options.y,pulse=options.w,seed=sprite*7.31+translation.w*53;
    Pixel o;o.uv=corner*float2(.5,-.5)+.5;o.data=float4(0,1+pulse,0,0);o.mode=1;
    float3 p=around(0,0,0);float2 size=0;
    if(look>4.5) { // Torch flame & rising embers
        const float3 viewDir=dot(translation.xyz,translation.xyz)>1e-4?normalize(translation.xyz):float3(0,0,-1);
        const float3 sDir=cross(viewDir,up.xyz);
        const float3 h1=dot(sDir,sDir)>1e-6?normalize(sDir):float3(1,0,0);
        const float3 h2=cross(up.xyz,h1);

        if(sprite==0) { // Core roaring flame pillar rooted right on the club head
            o.mode=0;o.data.x=0;o.data.z=t*1.20;
            o.data.y=1.45+pulse*.4;
            size=float2(.070,.125);
            p=translation.xyz+up.xyz*(size.y*.45);
        } else if(sprite==1) { // 3D swirling flame tongue (front/right)
            o.mode=0;o.data.x=1;o.data.z=t*1.55+seed;
            o.data.y=1.20+pulse*.3;
            size=float2(.060,.110);
            const float a=t*3.2+seed;
            p=translation.xyz+up.xyz*(size.y*.50)+(h1*cos(a)+h2*sin(a))*.010;
        } else if(sprite==2) { // 3D counter-swirling flame tongue (back/left)
            o.mode=0;o.data.x=0;o.data.z=-t*1.10+seed;
            o.data.y=1.15+pulse*.3;
            size=float2(.056,.105);
            const float a=-t*2.8+seed+3.14159;
            p=translation.xyz+up.xyz*(size.y*.50)+(h1*cos(a)+h2*sin(a))*.010;
        } else if(sprite==3) { // Leaping high flame tip
            o.mode=0;o.data.x=1;o.data.z=t*2.20+seed;
            o.data.y=(1.0+pulse*.3)*(.85+.15*sin(t*11.0+seed));
            size=float2(.042,.085);
            const float a=t*4.5+seed;
            p=translation.xyz+up.xyz*(.060+sin(t*10.0+seed)*.010)+(h1*cos(a)+h2*sin(a))*.006;
        } else if(sprite==4) { // Bright combustion core right at the head
            o.mode=1;
            o.data.y=(1.10+pulse*.4)*(.92+.08*sin(t*7.0+seed));
            size=float2(.080,.085);
            p=translation.xyz+up.xyz*(size.y*.15);
        } else { // Rising embers & fiery sparks
            const float phase=frac(t*.75+hash(seed));
            const float a=hash(seed+1.0)*Tau+phase*3.0;
            const float r=(.006+.020*hash(seed+2.0))*(1.0+phase*.8);
            size=float2(1,1)*.011*(1.0-phase*.6);
            p=translation.xyz+up.xyz*(.01+phase*.26)+(h1*cos(a)+h2*sin(a))*r;
            o.mode=1;
            o.data.y=(1.0-phase*.75)*2.5;
        }
    } else if(look<.5||look<1.5&&sprite>3) { // Fire Bolt, and Fireball's embers
        if(sprite==0){o.mode=0;p+=up.xyz*.008;size=float2(.018,.026);}
        else {
            const float phase=frac(t*.55+hash(seed));
            p=around(hash(seed+1)*Tau,.012*hash(seed+2),0)+up.xyz*(.005+phase*.06);
            size=.0045*(1-phase*.6);o.data.y*=1-phase;
        }
    } else if(look<1.5) { // Fireball
        if(sprite==0){o.mode=0;o.data.x=1;o.data.z=t*.9;size=.022;}
        else {p=around(t*1.4+sprite*Tau/3,.046,sin(t*2.1+sprite)*.008);size=.009;o.data.y*=.8;}
    } else if(look<2.5) { // Charged Bolt
        if(sprite==0){o.mode=2;o.data.x=1;size=.02;o.data.y*=.65;}
        else if(sprite<7) {
            const float tick=floor(t*13+hash(seed)*5);
            p=around(t*(sprite%2?2.4:-1.9)+sprite*1.05+hash(tick+seed)*.6,.035+.012*hash(seed+tick*.3),(hash(seed+tick)-.5)*.05);
            size=.017*(.55+.6*hash(tick*3.1+seed))*step(.3,hash(tick*1.7+seed));
            o.mode=3;o.data.y*=1.25;o.data.z=hash(tick*5.3+seed)*Tau;
        }
    } else { // Ice Bolt, Ice Blast
        const bool blast=look>3.5;
        if(sprite==0) {
            if(blast){o.mode=5;o.data.z=t*.6;size=.021;}
            else {o.mode=2;o.data.x=2;size=.02;o.data.y*=.55;}
        } else if(sprite<(blast?5u:6u)) {
            const float count=blast?4:5,twinkle=.55+.45*sin(t*4.5+hash(seed)*Tau);
            p=around(t*.7+sprite*Tau/count,blast?.045:.042,sin(t*1.3+sprite*1.7)*.012);
            size=.014*twinkle;o.mode=4;o.data.x=blast?1:0;o.data.y*=.7+.5*twinkle;o.data.z=t*.4+sprite;
        }
    }
    size*=1+.35*pulse;
    if(size.x<=0){o.position=float4(0,0,-1,1);return o;} // unused or dark this tick: clipped
    // Face the eye, upright to the room.
    const float3 view=normalize(p),side=cross(view,up.xyz);
    const float3 right=dot(side,side)>1e-6?normalize(side):float3(1,0,0),upward=cross(right,view);
    p+=right*corner.x*size.x+upward*corner.y*size.y;
    o.position=mul(float4(p,1),projection);
    return o;
}
float3 srgb(float3 c){return lerp(12.92*c,1.055*pow(max(c,0),1/2.4)-.055,step(.0031308,c));}
float2 spin(float2 uv,float angle){const float s=sin(angle),c=cos(angle);uv-=.5;return float2(c*uv.x-s*uv.y,s*uv.x+c*uv.y)+.5;}
float4 PS(Pixel i):SV_Target {
    const float t=options.x;
    float v;float3 color;
    if(i.mode==0) {
        const float shape=softFlame.Sample(edge,i.uv)*lerp(.6,1,i.uv.y);
        const float plate=i.data.x<.5?firePlate.Sample(tiled,i.uv*float2(.55,.45)+float2(0,t*.55)):
            fireballPlate.Sample(tiled,spin(i.uv,i.data.z)*.6+float2(t*.05,0));
        v=saturate(shape*(.2+1.15*plate))*i.data.y;
        color=flameRamp.Sample(edge,float2(saturate(v),.5)).rgb;
    } else if(i.mode==1) {
        v=softFlame.Sample(edge,i.uv)*i.data.y;
        color=flameRamp.Sample(edge,float2(saturate(v*.9+.1),.5)).rgb;
    } else if(i.mode==2) {
        v=glow.Sample(edge,i.uv)*i.data.y;
        color=i.data.x<1.5?lightningRamp.Sample(edge,float2(saturate(v),.5)).rgb:iceRamp.Sample(edge,float2(saturate(v*.7),.5)).rgb;
    } else if(i.mode==3) {
        v=flash.Sample(edge,spin(i.uv,i.data.z))*i.data.y;
        color=lightningRamp.Sample(edge,float2(saturate(v),.5)).rgb;
    } else if(i.mode==4) {
        v=glint.Sample(edge,spin(i.uv,i.data.z))*i.data.y;
        color=i.data.x<.5?iceRamp.Sample(edge,float2(saturate(v*.8),.5)).rgb:frostRamp.Sample(edge,float2(saturate(v),.5)).rgb;
    } else {
        const float shards=frozenOrb.Sample(tiled,spin(i.uv,i.data.z)).r;
        v=saturate(glow.Sample(edge,i.uv)*(.35+.9*shards))*i.data.y;
        color=frostRamp.Sample(edge,float2(saturate(v),.5)).rgb;
    }
    const float alpha=saturate(v*1.4)*options.z;
    if(up.w>.5)color=srgb(color);
    return float4(color*alpha,alpha*.55); // premultiplied: partly additive glow
}
)";
std::filesystem::path Directory() {
    HMODULE module=nullptr;wchar_t path[32768]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&module);
    Require(module&&GetModuleFileNameW(module,path,32768),"plugin path unavailable");
    return std::filesystem::path(path).parent_path()/L"d2r_vr_spell_sprites";
}
std::vector<uint8_t> ReadCache(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary|std::ios::ate);
    Require(bool(input),"spell sprite cache missing; the installer prepares it from your game");
    const auto size=input.tellg();Require(size>12&&size<32*1024*1024,"invalid spell sprite cache size");
    std::vector<uint8_t> data(static_cast<size_t>(size));input.seekg(0);input.read(reinterpret_cast<char*>(data.data()),size);
    Require(bool(input),"truncated spell sprite cache");return data;
}
ComPtr<ID3D12Resource> UploadBuffer(ID3D12Device* device,size_t size) {
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;
    desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result)),"sprite upload allocation failed");
    return result;
}
using Textures=std::array<ComPtr<ID3D12Resource>,sprites::Count>;
// Uploads every cached texture and waits; on success the sprite pipeline may draw.
void UploadSprites(ID3D12Device* device,ID3D12CommandQueue* queue,const std::vector<sprites::Texture>& textures,
                   ComPtr<ID3D12DescriptorHeap>& srv,Textures& resources) {
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=sprites::Count;
    heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srv)),"sprite descriptors failed");
    const auto srvStride=device->GetDescriptorHandleIncrementSize(heap.Type);
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"sprite upload allocator failed");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"sprite upload list failed");
    std::vector<ComPtr<ID3D12Resource>> uploads;
    D3D12_HEAP_PROPERTIES gpu{};gpu.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(unsigned i=0;i<sprites::Count;++i) {
        const auto& t=textures[i];
        D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=t.width;tex.Height=t.height;
        tex.DepthOrArraySize=1;tex.MipLevels=UINT16(t.mips.size());tex.Format=DXGI_FORMAT(t.format);tex.SampleDesc.Count=1;
        Check(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resources[i])),"sprite texture allocation failed");
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(t.mips.size());std::vector<UINT> rows(t.mips.size());
        std::vector<UINT64> rowBytes(t.mips.size());UINT64 bytes=0;
        device->GetCopyableFootprints(&tex,0,tex.MipLevels,0,footprints.data(),rows.data(),rowBytes.data(),&bytes);
        auto upload=UploadBuffer(device,size_t(bytes));void* mapped=nullptr;D3D12_RANGE empty{0,0};
        Check(upload->Map(0,&empty,&mapped),"sprite upload map failed");
        for(unsigned j=0;j<t.mips.size();++j) {
            Require(rowBytes[j]*rows[j]==t.mips[j].size(),"sprite mip footprint mismatch");
            for(unsigned row=0;row<rows[j];++row)
                std::memcpy(static_cast<uint8_t*>(mapped)+footprints[j].Offset+row*footprints[j].Footprint.RowPitch,t.mips[j].data()+row*rowBytes[j],size_t(rowBytes[j]));
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint=footprints[j];to.pResource=resources[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=j;
            list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        }
        upload->Unmap(0,nullptr);uploads.push_back(upload);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={resources[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        list->ResourceBarrier(1,&barrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=tex.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=tex.MipLevels;
        auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=i*srvStride;
        device->CreateShaderResourceView(resources[i].Get(),&view,handle);
    }
    Check(list->Close(),"sprite upload close failed");
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"sprite upload fence failed");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"sprite fence event failed");
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    const auto signal=queue->Signal(fence.Get(),1);
    const auto hr=SUCCEEDED(signal)?fence->SetEventOnCompletion(1,event):signal;
    const auto wait=SUCCEEDED(hr)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);
    // As for the potions: a healthy queue that timed out keeps its submitted
    // uploads and textures alive until it completes, never freed in flight.
    if(wait!=WAIT_OBJECT_0&&SUCCEEDED(device->GetDeviceRemovedReason())) {
        static std::vector<ComPtr<IUnknown>> retired;
        for(auto& u:uploads)retired.push_back(u);retired.push_back(list);retired.push_back(allocator);retired.push_back(fence);
        for(auto& t:resources)retired.push_back(t);
    }
    Require(wait==WAIT_OBJECT_0,"sprite upload timeout");
}
}
struct Renderer::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root,spriteRoot;
    ComPtr<ID3D12PipelineState> pipeline,spritePipeline;
    ComPtr<ID3D12DescriptorHeap> rtv,srv;
    Textures textures;
    uint32_t width=0,height=0,stride=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool encode=false;
};
Renderer::Renderer()=default;Renderer::~Renderer()=default;
void Renderer::Reset(){impl.reset();}
bool Renderer::Ready()const{return impl!=nullptr;}
bool Renderer::Sprites()const{return impl&&impl->spritePipeline;}
bool Renderer::Initialize(ID3D12Device* device,ID3D12CommandQueue* queue,DXGI_FORMAT format,uint32_t width,uint32_t height,
                          void (*log)(const char*),const std::filesystem::path& directory) {
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
        ComPtr<ID3DBlob> errors;
        auto shader=[&](const char* source,size_t size,const char* entry,const char* target,ComPtr<ID3DBlob>& out){
            errors.Reset();
            const auto result=compile(source,size,"elemental-hands",nullptr,nullptr,entry,target,
                D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&out,&errors);
            if(FAILED(result)&&errors&&log)log(static_cast<const char*>(errors->GetBufferPointer()));
            Check(result,"shader compilation failed");
        };
        auto rootSignature=[&](const D3D12_ROOT_SIGNATURE_DESC& desc,ComPtr<ID3D12RootSignature>& out){
            ComPtr<ID3DBlob> signature;errors.Reset();
            Check(serialize(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"root serialization failed");
            Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&out)),"root creation failed");
        };
        auto pipeline=[&](ID3D12RootSignature* root,const ComPtr<ID3DBlob>& vs,const ComPtr<ID3DBlob>& ps,bool premultiplied,ComPtr<ID3D12PipelineState>& out){
            D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=root;
            state.VS={vs->GetBufferPointer(),vs->GetBufferSize()};state.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
            auto& blend=state.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
            blend.SrcBlend=premultiplied?D3D12_BLEND_ONE:D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha=D3D12_BLEND_ZERO;blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
            state.SampleMask=UINT_MAX;state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
            state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;
            state.DepthStencilState.DepthEnable=FALSE;state.DepthStencilState.StencilEnable=FALSE;
            state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;state.NumRenderTargets=1;
            state.RTVFormats[0]=format;state.SampleDesc.Count=1;
            Check(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&out)),"pipeline creation failed");
        };
        ComPtr<ID3DBlob> vs,ps;
        shader(Source,sizeof Source,"VS","vs_5_0",vs);shader(Source,sizeof Source,"PS","ps_5_0",ps);
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants={0,0,32};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        rootSignature({1,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE},p->root);
        pipeline(p->root.Get(),vs,ps,false,p->pipeline);
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=2;
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p->rtv)),"target descriptors failed");
        p->stride=device->GetDescriptorHandleIncrementSize(heap.Type);
        std::string unavailable;
        try {
            Require(queue!=nullptr,"no upload queue");
            const auto textures=sprites::Parse(ReadCache((directory.empty()?Directory():directory)/L"sprites.ssp"));
            ComPtr<ID3DBlob> spriteVs,spritePs;
            shader(SpriteSource,sizeof SpriteSource,"VS","vs_5_0",spriteVs);shader(SpriteSource,sizeof SpriteSource,"PS","ps_5_0",spritePs);
            D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,sprites::Count,0,0,0};
            parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[1].DescriptorTable={1,&range};
            parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_STATIC_SAMPLER_DESC samplers[2]{};
            for(unsigned i=0;i<2;++i) {
                auto& s=samplers[i];s.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.ShaderRegister=i;s.MaxLOD=D3D12_FLOAT32_MAX;
                s.AddressU=s.AddressV=s.AddressW=i?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            }
            rootSignature({2,parameters,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_NONE},p->spriteRoot);
            pipeline(p->spriteRoot.Get(),spriteVs,spritePs,true,p->spritePipeline);
            UploadSprites(device,queue,textures,p->srv,p->textures);
        } catch(const std::exception& e) {
            unavailable=e.what();p->spritePipeline.Reset();p->spriteRoot.Reset();p->srv.Reset();p->textures={};
        }
        impl=std::move(p);
        if(log) {
            const std::string message=unavailable.empty()?
                "Frame elemental hands: the spells' own effect textures from your game are ready (prototype overlay)":
                "Frame elemental hands: spell textures unavailable ("+unavailable+"); procedural aura ready (prototype overlay)";
            log(message.c_str());
        }
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
    const bool textured=p.spritePipeline!=nullptr;
    if(textured) {
        list->SetGraphicsRootSignature(p.spriteRoot.Get());list->SetPipelineState(p.spritePipeline.Get());
        ID3D12DescriptorHeap* heaps[]={p.srv.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetGraphicsRootDescriptorTable(1,p.srv->GetGPUDescriptorHandleForHeapStart());
    } else {
        list->SetGraphicsRootSignature(p.root.Get());list->SetPipelineState(p.pipeline.Get());
    }
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
        constants[24]=scene.time;constants[25]=textured?float(SpellLook(hand.skill,hand.element)):float(hand.element);
        constants[26]=std::clamp(hand.opacity,0.f,1.f);constants[27]=std::clamp(hand.pulse,0.f,1.f);
        constants[28]=up.x;constants[29]=up.y;constants[30]=up.z;constants[31]=p.encode?1:0;
        list->SetGraphicsRoot32BitConstants(0,32,constants,0);list->DrawInstanced(6,textured?SpritesPerHand:32,0,0);drawn=true;
    }
    return drawn;
}
}
