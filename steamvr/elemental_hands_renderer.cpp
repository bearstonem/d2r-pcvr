#include "elemental_hands_renderer.h"
#include "dxgi_views.h"
#include "spell_sprites.h"
#include <algorithm>
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
// The particles (elemental_particles.h) with the game's effect textures (spell_sprites.h), one quad each,
// drawn in one pass per eye. Each kind's material follows the game's: a mask or flipbook frame, distorted
// for flames, its brightness turned to colour through the spell's own gradient. Premultiplied: a particle
// with blend 0 adds light (flames, sparks, arcs, glows), 1 covers what is behind (smoke, mist).
constexpr char ParticleSource[]=R"(
cbuffer View:register(b0) {
    row_major float4x4 projection;
    float4 inverse;    // the eye's rotation, conjugated: the room into the eye
    float4 position;   // the eye in the room; w = encode sRGB
    float4 options;    // time
};
struct Particle {float3 p;float size;float3 v;float life;float aspect,rotation,intensity,blend;uint kind,frame;float seed,stretch;};
StructuredBuffer<Particle> particles:register(t21);   // after the textures' t0..t20
Texture2D<float> firePlate:register(t0);
Texture2D<float> fireballPlate:register(t1);
Texture2D<float> softFlame:register(t2);
Texture2D<float4> flameRamp:register(t3);
Texture2D<float> flash:register(t4);
Texture2D<float> glowCard:register(t5);
Texture2D<float4> lightningRamp:register(t6);
Texture2D<float> glint:register(t7);
Texture2D<float4> iceRamp:register(t8);
Texture2D<float4> frozenOrb:register(t9);
Texture2D<float4> frostRamp:register(t10);
Texture2D<float> fireFlipbook:register(t11);
Texture2D<float> smoke:register(t12);
Texture2D<float> wisp:register(t13);
Texture2D<float> arcs:register(t14);
Texture2D<float> bolts:register(t15);
Texture2D<float4> flakes:register(t16);
Texture2D<float4> iceCaustic:register(t17);
Texture2D<float> glowDot:register(t18);
Texture2D<float> star:register(t19);
Texture2D<float2> distortion:register(t20);
SamplerState tiled:register(s0);
SamplerState edge:register(s1);
static const float Tau=6.2831853;
// The kinds (elemental_hands.h Kind).
static const uint Flame=0,Ember=1,Glow=2,Smoke=3,Arc=4,Bolt=5,Spark=6,Mist=7,Flake=8,Chunk=9,Orb=10,Flare=11,Core=12,Plasma=13;
float3 rotate(float4 q,float3 p){return p+2*cross(q.xyz,cross(q.xyz,p)+q.w*p);}
struct Pixel {float4 position:SV_Position;float2 uv:TEXCOORD0;nointerpolation uint index:TEXCOORD1;};
Pixel VS(uint vertex:SV_VertexID,uint instance:SV_InstanceID) {
    const float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
    const float2 c=corners[vertex];
    const Particle q=particles[instance];
    Pixel o;o.uv=c*float2(.5,-.5)+.5;o.index=instance;
    const float3 centre=rotate(inverse,q.p-position.xyz);
    const float3 view=dot(centre,centre)>1e-8?normalize(centre):float3(0,0,-1);
    const float3 roomUp=rotate(inverse,float3(0,1,0));
    float halfHeight=q.size,halfWidth=q.size*q.aspect;
    float3 up=roomUp-view*dot(roomUp,view);
    bool turned=true;
    if(q.kind==Arc||q.kind==Bolt) {   // along its axis
        const float3 axis=rotate(inverse,q.v);up=axis-view*dot(axis,view);turned=false;
    } else if(q.stretch>0) {          // along its velocity
        const float3 velocity=rotate(inverse,q.v);
        const float3 along=velocity-view*dot(velocity,view);
        if(dot(along,along)>1e-8){up=along;halfHeight=q.size+length(along)*q.stretch*.5;turned=false;}
    } else if(q.kind==Flame)turned=false;   // upright in the room
    up=dot(up,up)>1e-8?normalize(up):float3(0,1,0);
    float3 right=normalize(cross(view,up));up=cross(right,view);
    if(turned){const float s=sin(q.rotation),k=cos(q.rotation);const float3 r=right*k+up*s;up=up*k-right*s;right=r;}
    const float3 p=centre+right*c.x*halfWidth+up*c.y*halfHeight;
    o.position=mul(float4(p,1),projection);
    return o;
}
float3 srgb(float3 c){return lerp(12.92*c,1.055*pow(max(c,0),1/2.4)-.055,step(.0031308,c));}
float2 spin(float2 uv,float angle){const float s=sin(angle),c=cos(angle);uv-=.5;return float2(c*uv.x-s*uv.y,s*uv.x+c*uv.y)+.5;}
// A frame of an atlas of cols x rows, row by row from the top left; the quad's uv inset a little so a
// neighbouring frame never bleeds in.
float2 cell(float2 uv,uint frame,uint cols,uint rows){uv=clamp(uv,.01,.99);return (float2(frame%cols,(frame/cols)%rows)+uv)/float2(cols,rows);}
float3 ramp(uint which,float v) {
    const float2 at=float2(saturate(v),.5);
    return which==0?flameRamp.Sample(edge,at).rgb:which==1?lightningRamp.Sample(edge,at).rgb:which==2?iceRamp.Sample(edge,at).rgb:frostRamp.Sample(edge,at).rgb;
}
float4 PS(Pixel i):SV_Target {
    const Particle q=particles[i.index];
    const float t=options.x,life=q.life;
    const float fade=saturate(life*8)*saturate((1-life)*2.5);   // in quickly, out over the last part
    const float2 uv=i.uv;
    float v=0,alpha=0;float3 color=0;
    if(q.kind==Flame) {   // the game's fire flipbook, its 64 frames over the flame's life, licked by Fire Bolt's distortion
        const float2 d=distortion.Sample(tiled,uv*.55+float2(q.seed,-t*.8)).xy*2-1;
        const float2 at=uv+d*.035*(1-uv.y);
        const float f=min(q.frame+life*57,63.0),f0=floor(f);
        const float a=fireFlipbook.Sample(edge,cell(at,uint(f0),8,8)),b=fireFlipbook.Sample(edge,cell(at,uint(min(f0+1,63.0)),8,8));
        v=lerp(a,b,f-f0)*q.intensity*fade;
        color=ramp(0,v*1.1);alpha=saturate(v*1.6);
    } else if(q.kind==Ember||q.kind==Spark) {
        v=glowDot.Sample(edge,uv)*q.intensity*fade;
        color=ramp(q.kind==Ember?0:1,.55+.45*saturate(v));alpha=saturate(v);
    } else if(q.kind==Glow) {
        v=glowDot.Sample(edge,uv)*q.intensity*fade;
        color=ramp(q.frame,v*.85);alpha=saturate(v);
    } else if(q.kind==Smoke) {   // dark, warm while young
        v=smoke.Sample(edge,cell(uv,q.frame,2,2));
        alpha=saturate(v*q.intensity*fade*.30);
        color=lerp(float3(.05,.045,.04),float3(.30,.12,.04),saturate(1-life*2.5));
    } else if(q.kind==Arc||q.kind==Bolt) {   // the arcs lie across their frames: turned along the axis
        v=(q.kind==Arc?arcs.Sample(edge,cell(uv.yx,q.frame,4,2)):bolts.Sample(edge,cell(uv,q.frame,4,2)))*q.intensity*fade;
        color=ramp(1,v);alpha=saturate(v*1.2);
    } else if(q.kind==Mist) {
        v=wisp.Sample(edge,cell(uv,q.frame,2,2));
        alpha=saturate(v*q.intensity*fade*.40);
        color=ramp(3,.25+.45*v)*.95;
    } else if(q.kind==Flake||q.kind==Chunk) {   // snowflakes in the left column, ice chunks in the right
        const float twinkle=.7+.3*sin(t*9+q.seed*7);
        v=flakes.Sample(edge,cell(uv,q.frame*2+(q.kind==Chunk?1:0),2,4)).a*q.intensity*fade*twinkle;
        color=ramp(q.kind==Flake?2:3,.45+.55*saturate(v));alpha=saturate(v);
    } else if(q.kind==Orb) {   // the Frozen Orb's shards turning, an ice shimmer over them
        const float shards=frozenOrb.Sample(tiled,spin(uv,q.rotation)).r;
        const float3 caustic=iceCaustic.Sample(tiled,uv*.8+float2(t*.05,t*.03)).rgb;
        v=saturate(glowCard.Sample(edge,uv)*(.35+.9*shards+.35*dot(caustic,1.0/3)))*q.intensity;
        color=ramp(3,v);alpha=saturate(v*1.3);
    } else if(q.kind==Flare) {
        v=star.Sample(edge,uv)*q.intensity*fade;
        color=ramp(q.frame,.5+.5*saturate(v));alpha=saturate(v);
    } else if(q.kind==Core) {   // the fireball's plate turning, licked by the distortion, round
        const float2 d=distortion.Sample(tiled,uv*.7+float2(t*.3,-t*.5)).xy*2-1;
        const float plate=fireballPlate.Sample(tiled,spin(uv,q.rotation)*.6+float2(t*.05,0)+d*.06);
        v=saturate(glowCard.Sample(edge,uv)*(.25+1.2*plate))*q.intensity;
        color=ramp(0,v);alpha=saturate(v*1.4);
    } else {   // Plasma: Charged Bolt's own flash, two layers turning against each other
        const float a=flash.Sample(edge,spin(uv,q.rotation)),b=flash.Sample(edge,spin(uv,1.3-q.rotation*1.7));
        v=(a*.7+b*.5)*q.intensity;
        color=ramp(1,.35+.5*saturate(v));alpha=saturate(v);
    }
    if(position.w>.5)color=srgb(color);
    return float4(color*alpha,alpha*q.blend);
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
// The particles' upload ring: each Render takes the next of RingSlots, so a pass still on the GPU (the
// other eye's, a frame or two before) is never written over.
constexpr unsigned RingSlots=16;
constexpr size_t SlotBytes=MaxDrawn*sizeof(Particle);
struct Renderer::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root,spriteRoot;
    ComPtr<ID3D12PipelineState> pipeline,spritePipeline;
    ComPtr<ID3D12DescriptorHeap> rtv,srv;
    ComPtr<ID3D12Resource> ring;uint8_t* mapped=nullptr;unsigned next=0;
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
            shader(ParticleSource,sizeof ParticleSource,"VS","vs_5_0",spriteVs);shader(ParticleSource,sizeof ParticleSource,"PS","ps_5_0",spritePs);
            D3D12_ROOT_PARAMETER particles[3]{};
            particles[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;particles[0].Constants={0,0,28};
            particles[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
            particles[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;particles[1].Descriptor={sprites::Count,0};   // t21, after the textures
            particles[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
            D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,sprites::Count,0,0,0};
            particles[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;particles[2].DescriptorTable={1,&range};
            particles[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_STATIC_SAMPLER_DESC samplers[2]{};
            for(unsigned i=0;i<2;++i) {
                auto& s=samplers[i];s.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.ShaderRegister=i;s.MaxLOD=D3D12_FLOAT32_MAX;
                s.AddressU=s.AddressV=s.AddressW=i?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            }
            rootSignature({3,particles,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_NONE},p->spriteRoot);
            pipeline(p->spriteRoot.Get(),spriteVs,spritePs,true,p->spritePipeline);
            p->ring=UploadBuffer(device,RingSlots*SlotBytes);
            D3D12_RANGE none{0,0};void* mapped=nullptr;Check(p->ring->Map(0,&none,&mapped),"particle ring map failed");
            p->mapped=static_cast<uint8_t*>(mapped);
            UploadSprites(device,queue,textures,p->srv,p->textures);
        } catch(const std::exception& e) {
            unavailable=e.what();p->spritePipeline.Reset();p->spriteRoot.Reset();p->srv.Reset();p->textures={};p->ring.Reset();p->mapped=nullptr;
        }
        impl=std::move(p);
        if(log) {
            const std::string message=unavailable.empty()?
                "Frame elemental hands: particle effects ready, with "+std::to_string(sprites::Count)+" of your game's effect textures":
                "Frame elemental hands: spell textures unavailable ("+unavailable+"); procedural aura ready";
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
    if(p.spritePipeline) {   // the particles (elemental_particles.h), both hands in one draw
        const unsigned count=std::min(scene.count,MaxDrawn);
        if(!count||!p.mapped)return false;
        // Those that cover what is behind first, the farthest first; then those that add light (any order).
        struct Order {float key;unsigned index;};
        std::array<Order,MaxDrawn> order;
        for(unsigned i=0;i<count;++i) {
            const auto& q=scene.drawn[i];const auto d=q.p-view.p;
            order[i]={q.blend>.5f?-math::Dot(d,d):1e9f,i};
        }
        std::sort(order.begin(),order.begin()+count,[](const Order& a,const Order& b){return a.key<b.key;});
        const unsigned slot=p.next++%RingSlots;
        auto* out=reinterpret_cast<Particle*>(p.mapped+slot*SlotBytes);
        for(unsigned i=0;i<count;++i)out[i]=scene.drawn[order[i].index];
        float constants[28]{};math::Projection(fov.left,fov.right,fov.down,fov.up,.02f,constants);
        const auto inverse=math::Conjugate(view.q);
        constants[16]=inverse.x;constants[17]=inverse.y;constants[18]=inverse.z;constants[19]=inverse.w;
        constants[20]=view.p.x;constants[21]=view.p.y;constants[22]=view.p.z;constants[23]=p.encode?1:0;
        constants[24]=scene.time;
        list->SetGraphicsRootSignature(p.spriteRoot.Get());list->SetPipelineState(p.spritePipeline.Get());
        ID3D12DescriptorHeap* heaps[]={p.srv.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetGraphicsRoot32BitConstants(0,28,constants,0);
        list->SetGraphicsRootShaderResourceView(1,p.ring->GetGPUVirtualAddress()+slot*SlotBytes);
        list->SetGraphicsRootDescriptorTable(2,p.srv->GetGPUDescriptorHandleForHeapStart());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(6,count,0,0);
        return true;
    }
    // Without the game's textures: the original procedural aura, per hand.
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
        constants[24]=scene.time;constants[25]=float(hand.element);
        constants[26]=std::clamp(hand.opacity,0.f,1.f);constants[27]=std::clamp(hand.pulse,0.f,1.f);
        constants[28]=up.x;constants[29]=up.y;constants[30]=up.z;constants[31]=p.encode?1:0;
        list->SetGraphicsRoot32BitConstants(0,32,constants,0);list->DrawInstanced(6,32,0,0);drawn=true;
    }
    return drawn;
}
}
