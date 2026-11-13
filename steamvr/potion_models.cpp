#include "potion_models.h"
#include "dxgi_views.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <climits>
#include <stdexcept>

namespace framevr::potions {
using Microsoft::WRL::ComPtr;
namespace {
constexpr const char* Codes[]={"hp1","hp2","hp3","hp4","hp5","mp1","mp2","mp3","mp4","mp5","rvs","rvl","yps","wms","vps"};
constexpr unsigned Count=15;   // the potions, all required; then the extras, each optional
// The arrow (the bow's), then the traps and bombs thrown by hand (#224, toss::Model),
// then the gas and oil potions, which fly as their own bottles when thrown (#292).
// Last, Bone Armor's three pieces (bn1..bn3, 1 m long, scaled when drawn; bone_graft.h).
constexpr const char* Extras[]={"arw","tfb","tsw","tbs","tst","twk","gps","gpm","gpl","ops","opm","opl","bn1","bn2","bn3"};
constexpr float ExtraExtent[]={.45f,.1f,.1f,.1f,.1f,.1f,.2f,.2f,.2f,.2f,.2f,.2f,.75f,.75f,.75f};
constexpr unsigned ExtraCount=15,TossExtras=5,FirstBone=12;
using Vertex=ice::Vertex;
struct Asset {std::vector<Vertex> vertices;std::vector<uint32_t> indices;unsigned w,h,format;std::vector<std::vector<uint8_t>> mips;};
void Require(bool condition,const char* error){if(!condition)throw std::runtime_error(error);}
void Check(HRESULT hr,const char* error){Require(SUCCEEDED(hr),error);}
// extent: the furthest a vertex may lie from the origin (metres).
Asset Load(const std::filesystem::path& file,float extent=.2f) {
    std::ifstream input(file,std::ios::binary|std::ios::ate);
    Require(bool(input),"potion cache missing; run prepare_potions.py");
    const auto size=input.tellg();Require(size>32&&size<8*1024*1024,"invalid potion cache size");
    input.seekg(0);std::vector<uint8_t> data(static_cast<size_t>(size));input.read(reinterpret_cast<char*>(data.data()),size);
    Require(bool(input),"truncated potion cache");size_t offset=0;
    auto read=[&](void* out,size_t bytes){Require(offset<=data.size()&&bytes<=data.size()-offset,"truncated potion payload");std::memcpy(out,data.data()+offset,bytes);offset+=bytes;};
    uint32_t header[8];read(header,sizeof header);
    Require(header[0]==0x31525650&&header[1]==1&&header[2]>0&&header[2]<=32768&&header[3]>0&&header[3]<=131072&&header[3]%3==0,"invalid potion geometry");
    Asset a;a.w=header[4];a.h=header[5];a.format=header[6];
    Require(a.w&&a.h&&a.w<=4096&&a.h<=4096&&(a.format==72||a.format==78||a.format==29)&&header[7]>0&&header[7]<=14,"invalid potion texture");
    a.vertices.resize(header[2]);a.indices.resize(header[3]);read(a.vertices.data(),a.vertices.size()*sizeof(Vertex));read(a.indices.data(),a.indices.size()*4);
    for(const auto& v:a.vertices) {
        for(float x:v.p)Require(std::isfinite(x)&&std::fabs(x)<=extent,"invalid potion position");
        for(float x:v.n)Require(std::isfinite(x)&&std::fabs(x)<=1.01f,"invalid potion normal");
        for(float x:v.uv)Require(std::isfinite(x)&&std::fabs(x)<=16,"invalid potion UV");
        Require(v.glass==0||v.glass==1,"invalid potion material");
    }
    for(auto i:a.indices)Require(i<a.vertices.size(),"potion index out of range");
    for(unsigned i=0;i<header[7];++i) {
        uint32_t bytes;read(&bytes,4);const auto w=std::max(1u,a.w>>i),h=std::max(1u,a.h>>i);
        const auto expected=a.format==29?w*h*4:((w+3)/4)*((h+3)/4)*(a.format==72?8:16);
        Require(bytes==expected,"invalid potion mip size");a.mips.emplace_back(bytes);read(a.mips.back().data(),bytes);
    }
    Require(offset==data.size(),"unexpected potion cache data");return a;
}
std::filesystem::path Directory() {
    HMODULE module=nullptr;wchar_t path[32768]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&module);
    Require(module&&GetModuleFileNameW(module,path,32768),"potion module path unavailable");
    return std::filesystem::path(path).parent_path()/L"d2r_vr_potions";
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,const void* bytes,size_t size) {
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;
    desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> result;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result)),"potion buffer allocation failed");
    if(bytes){void* mapped=nullptr;D3D12_RANGE empty{0,0};Check(result->Map(0,&empty,&mapped),"potion buffer map failed");std::memcpy(mapped,bytes,size);result->Unmap(0,nullptr);}
    return result;
}
}
struct Models::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline,icePipeline;
    ComPtr<ID3D12DescriptorHeap> srv,rtv,dsv;
    ComPtr<ID3D12Resource> depth[2];
    struct Mesh {ComPtr<ID3D12Resource> vertices,indices,texture;D3D12_VERTEX_BUFFER_VIEW vb{};D3D12_INDEX_BUFFER_VIEW ib{};uint32_t code=0,count=0;float radius=0;};
    Mesh meshes[Count+ExtraCount+ice::Count];unsigned meshCount=Count;
    bool arrow=false;unsigned tosses=0,bones=0,ice=0;
    uint32_t width=0,height=0,srvStride=0,rtvStride=0,dsvStride=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;bool encode=false;
};
Models::Models()=default;Models::~Models()=default;
void Models::Reset(){impl.reset();}
bool Models::Ready() const {return impl!=nullptr;}
bool Models::HasArrow() const {return impl&&impl->arrow;}
unsigned Models::Tosses() const {return impl?impl->tosses:0;}
unsigned Models::Bones() const {return impl?impl->bones:0;}
unsigned Models::IcePieces() const {return impl?impl->ice:0;}
float Models::Radius(uint32_t code) const {
    if(!impl)return 0;
    code=(code&0xffffff)|(uint32_t(' ')<<24);
    for(unsigned i=0;i<impl->meshCount;++i)if(impl->meshes[i].code==code)return impl->meshes[i].radius;
    return 0;
}
bool Models::Initialize(ID3D12Device* device,ID3D12CommandQueue* queue,DXGI_FORMAT format,uint32_t w,uint32_t h,void (*log)(const char*),const std::filesystem::path& directory) {
    if(impl)return true;
    try {
        auto p=std::make_unique<Impl>();p->device=device;p->width=w;p->height=h;p->format=format;
        Require(format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,"potion renderer requires SDR eye target");
        p->encode=format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM;
        const auto folder=directory.empty()?Directory():directory;
        std::vector<Asset> assets(Count);std::vector<uint32_t> codes;
        const auto code=[](const char* c){return interaction::Code(c[0],c[1],c[2]);};
        for(unsigned i=0;i<Count;++i){assets[i]=Load(folder/(std::string(Codes[i])+".pvr"));codes.push_back(code(Codes[i]));}
        // The extras are optional: an older cache, or one without them, keeps the
        // bow's draw guide and throws traps with nothing in the hand.
        std::string arrowNote;
        for(unsigned i=0;i<ExtraCount;++i) {
            try{assets.push_back(Load(folder/(std::string(Extras[i])+".pvr"),ExtraExtent[i]));codes.push_back(code(Extras[i]));if(!i)p->arrow=true;else if(i<=TossExtras)++p->tosses;else if(i>=FirstBone)++p->bones;}
            catch(const std::exception& e){if(!i)arrowNote=e.what();}
        }
        // Authored ice assets are optional; never substitute Necromancer bones.
        const auto firstIce=assets.size();
        try {
            for(unsigned i=0;i<ice::Count;++i){
                assets.push_back(Load(folder/("ic"+std::to_string(i+1)+".pvr"),.75f));
                codes.push_back(ice::Code(int(i)));
            }
            p->ice=ice::Count;
        } catch(const std::exception& e){
            assets.resize(firstIce);codes.resize(firstIce);
            if(log)log(e.what());
        }
        p->meshCount=unsigned(assets.size());
        struct Lib {HMODULE h=LoadLibraryW(L"d3dcompiler_47.dll");~Lib(){if(h)FreeLibrary(h);}} compiler;
        auto compile=compiler.h?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler.h,"D3DCompile")):nullptr;
        const auto d12=GetModuleHandleW(L"d3d12.dll");
        auto serialize=d12?reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12,"D3D12SerializeRootSignature")):nullptr;
        Require(compile&&serialize,"potion shader compiler unavailable");
        constexpr char source[]=R"(
cbuffer Scene:register(b0) {row_major float4x4 projection;float4 rotation;float4 translation;float4 light;float4 options;float4 occA;float4 occB;float4 depthRotation;float4 depthTranslation;float4 depthFov;float4 depthOptions;};
Texture2D<float4> albedo:register(t0);Texture2D<float> sceneDepth:register(t1);SamplerState texSampler:register(s0);
float3 Rotate(float3 v){return v+2*cross(rotation.xyz,cross(rotation.xyz,v)+rotation.w*v);}
struct In {float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float glass:TEXCOORD1;};
struct Out {float4 p:SV_Position;float3 view:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float glass:TEXCOORD3;};
Out VS(In v){Out o;if(light.w<0){v.p.x=-v.p.x;v.n.x=-v.n.x;}float s=translation.w>0?translation.w:1,w=occB.w>0?occB.w:s;o.view=Rotate(v.p*float3(w,s,w))+translation.xyz;float3 m=v.n/float3(w,s,w);v.n=m*rsqrt(max(dot(m,m),1e-12));o.p=mul(float4(o.view,1),projection);o.n=Rotate(v.n);o.uv=v.uv;o.glass=v.glass;return o;}
// The first hit of a ray from the eye (view space origin) with a capsule, -1 for none.
float Capsule(float3 rd,float3 pa,float3 pb,float r){
    float3 ba=pb-pa,oa=-pa;float baba=dot(ba,ba),bard=dot(ba,rd),baoa=dot(ba,oa),rdoa=dot(rd,oa),oaoa=dot(oa,oa);
    float a=baba-bard*bard,b=baba*rdoa-baoa*bard,c=baba*oaoa-baoa*baoa-r*r*baba,h=b*b-a*c;
    if(h>=0&&a>1e-8){float t=(-b-sqrt(h))/a,y=baoa+t*bard;if(y>0&&y<baba)return t;
        float3 oc=y<=0?oa:-pb;b=dot(rd,oc);c=dot(oc,oc)-r*r;h=b*b-c;if(h>0)return -b-sqrt(h);}
    return -1;
}
float4 PS(Out v):SV_Target {
    if(options.w>0&&depthOptions.z>0){
        float3 p=v.view+2*cross(depthRotation.xyz,cross(depthRotation.xyz,v.view)+depthRotation.w*v.view)+depthTranslation.xyz;
        float z=-p.z;
        if(z>0){
            float2 uv=float2((p.x/z-depthFov.x)/(depthFov.y-depthFov.x),(depthFov.w-p.y/z)/(depthFov.w-depthFov.z));
            if(all(uv>=0)&&all(uv<=1)){
                uint w,h;sceneDepth.GetDimensions(w,h);
                uint2 xy=min(uint2(uv*depthOptions.xy*float2(w,h)),uint2(w-1,h-1));
                float d=sceneDepth.Load(int3(xy,0));
                // Reverse-Z: zero is sky/no surface. Use the native near plane,
                // not this renderer's 2 cm plane; tolerate 1 cm of pose/jitter.
                if(d>0&&d<=1&&depthOptions.z/d+depthOptions.w<z)discard;
            }
        }
    }
    // A piece behind its forearm (occA/occB: wrist, elbow; occA.w the radius) is not drawn there.
    if(occA.w>0){float d=length(v.view);float t=Capsule(v.view/d,occA.xyz,occB.xyz,occA.w);if(t>0&&t<d-.002)discard;}
    float3 n=normalize(v.n),eye=normalize(-v.view);
    float4 texel=albedo.Sample(texSampler,v.uv);float3 base=texel.rgb;
    if(options.x>0)base=lerp(base,dot(base,float3(.2126,.7152,.0722))*.28,v.glass>.5?.85:.2);
    float diffuse=saturate(dot(n,light.xyz));
    float3 c=base*(.38+.62*diffuse)+pow(saturate(dot(n,normalize(light.xyz+eye))),32)*lerp(.12,.38,v.glass);
    c+=options.y*float3(.14,.08,.02)*pow(1-saturate(dot(n,eye)),2);
    float iceAlpha=texel.a;
    if(options.w>0){
        // Convex crystals: keep the visible facets only, avoiding accumulated
        // alpha from rear faces and preserving clear interiors.
        if(dot(n,eye)<=0)discard;
        float rim=pow(1-saturate(dot(n,eye)),3);
        c=base*(.62+.30*abs(dot(n,light.xyz)))+float3(.12,.16,.18)*rim;
        c+=float3(.18,.20,.22)*pow(saturate(dot(n,normalize(light.xyz+eye))),48);
        iceAlpha=saturate(texel.a*(.75+rim*.65));
    }
    if(options.z>0)c=lerp(1.055*pow(max(c,0),1/2.4)-.055,c*12.92,c<=.0031308);
    return float4(c,options.w>0?iceAlpha:1);
})";
        ComPtr<ID3DBlob> vs,ps,errors,signature;
        Check(compile(source,sizeof(source)-1,"potion-models",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors),"potion vertex shader failed");
        Check(compile(source,sizeof(source)-1,"potion-models",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors),"potion pixel shader failed");
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},depthRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,1,0,0};
        D3D12_ROOT_PARAMETER params[3]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,56};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[2].DescriptorTable={1,&depthRange};params[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_ANISOTROPIC;sampler.MaxAnisotropy=4;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root{3,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        Check(serialize(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"potion root serialization failed");
        Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&p->root)),"potion root creation failed");
        const D3D12_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=p->root.Get();state.VS={vs->GetBufferPointer(),vs->GetBufferSize()};state.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        state.InputLayout={elements,4};state.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;state.SampleMask=UINT_MAX;
        state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;
        state.DepthStencilState.DepthEnable=TRUE;state.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;state.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;state.NumRenderTargets=1;state.RTVFormats[0]=format;state.DSVFormat=DXGI_FORMAT_D32_FLOAT;state.SampleDesc.Count=1;
        Check(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&p->pipeline)),"potion pipeline creation failed");
        // Ice alone blends after opaque items and does not write depth/target alpha.
        auto& blend=state.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
        blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ZERO;blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE;
        state.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        Check(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&p->icePipeline)),"ice plate pipeline creation failed");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=Count+ExtraCount+ice::Count+2;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p->srv)),"potion texture descriptors failed");p->srvStride=device->GetDescriptorHandleIncrementSize(heap.Type);
        heap.NumDescriptors=2;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p->rtv)),"potion target descriptors failed");p->rtvStride=device->GetDescriptorHandleIncrementSize(heap.Type);
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p->dsv)),"potion depth descriptors failed");p->dsvStride=device->GetDescriptorHandleIncrementSize(heap.Type);
        D3D12_HEAP_PROPERTIES gpu{};gpu.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC depth{};depth.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;depth.Width=w;depth.Height=h;depth.DepthOrArraySize=depth.MipLevels=1;
        depth.Format=DXGI_FORMAT_D32_FLOAT;depth.SampleDesc.Count=1;depth.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=depth.Format;clear.DepthStencil={0,0};
        for(unsigned i=0;i<2;++i){Check(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_NONE,&depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&p->depth[i])),"potion depth allocation failed");auto handle=p->dsv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=i*p->dsvStride;device->CreateDepthStencilView(p->depth[i].Get(),nullptr,handle);}
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"potion upload allocator failed");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"potion upload list failed");
        std::vector<ComPtr<ID3D12Resource>> uploads;
        for(unsigned i=0;i<p->meshCount;++i) {
            const auto& a=assets[i];auto& m=p->meshes[i];m.code=codes[i];m.count=unsigned(a.indices.size());
            for(const auto& v:a.vertices)m.radius=std::max(m.radius,std::hypot(v.p[0],v.p[2])); // about the model's +Y
            m.vertices=Buffer(device,a.vertices.data(),a.vertices.size()*sizeof(Vertex));m.indices=Buffer(device,a.indices.data(),a.indices.size()*4);
            m.vb={m.vertices->GetGPUVirtualAddress(),UINT(a.vertices.size()*sizeof(Vertex)),sizeof(Vertex)};m.ib={m.indices->GetGPUVirtualAddress(),UINT(a.indices.size()*4),DXGI_FORMAT_R32_UINT};
            D3D12_RESOURCE_DESC tex=depth;tex.Width=a.w;tex.Height=a.h;tex.MipLevels=UINT16(a.mips.size());tex.Format=DXGI_FORMAT(a.format);tex.Flags=D3D12_RESOURCE_FLAG_NONE;
            Check(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&m.texture)),"potion texture allocation failed");
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(a.mips.size());std::vector<UINT> rows(a.mips.size());std::vector<UINT64> rowBytes(a.mips.size());UINT64 bytes=0;
            device->GetCopyableFootprints(&tex,0,tex.MipLevels,0,footprints.data(),rows.data(),rowBytes.data(),&bytes);
            auto upload=Buffer(device,nullptr,size_t(bytes));void* mapped=nullptr;D3D12_RANGE empty{0,0};Check(upload->Map(0,&empty,&mapped),"potion texture map failed");
            for(unsigned j=0;j<a.mips.size();++j) {
                Require(rowBytes[j]*rows[j]==a.mips[j].size(),"potion mip footprint mismatch");
                for(unsigned row=0;row<rows[j];++row)std::memcpy(static_cast<uint8_t*>(mapped)+footprints[j].Offset+row*footprints[j].Footprint.RowPitch,a.mips[j].data()+row*rowBytes[j],size_t(rowBytes[j]));
                D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprints[j];to.pResource=m.texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=j;
                list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            }
            upload->Unmap(0,nullptr);uploads.push_back(upload);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={m.texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};list->ResourceBarrier(1,&barrier);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=tex.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=tex.MipLevels;
            auto handle=p->srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=i*p->srvStride;device->CreateShaderResourceView(m.texture.Get(),&srv,handle);
        }
        Check(list->Close(),"potion upload close failed");
        ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"potion upload fence failed");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"potion fence event failed");
        ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        const auto signal=queue->Signal(fence.Get(),1);
        const auto hr=SUCCEEDED(signal)?fence->SetEventOnCompletion(1,event):signal;
        const auto wait=SUCCEEDED(hr)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);
        // Device removal abandons all work. A healthy timed-out queue must
        // retain submitted uploads/resources until completion (never free in flight).
        if(wait!=WAIT_OBJECT_0&&SUCCEEDED(device->GetDeviceRemovedReason())) {
            static std::vector<ComPtr<IUnknown>> retired;
            for(auto& u:uploads)retired.push_back(u);retired.push_back(list);retired.push_back(allocator);retired.push_back(fence);
            for(auto& m:p->meshes)retired.push_back(m.texture);
        }
        Require(wait==WAIT_OBJECT_0,"potion upload timeout");impl=std::move(p);
        if(log) {
            std::string text="Frame potions: 15 original 3D potion models ready; stereo belt and grip rendering active";
            text+=HasArrow()?"; the game's arrow model ready for the bow":"; arrow model unavailable ("+arrowNote+"), the bow keeps its draw guide";
            text+="; "+std::to_string(Tosses())+" of 5 thrown trap models";
            text+="; "+std::to_string(Bones())+" of 3 Bone Armor pieces";
            text+="; 3 original translucent blue ice plates ready";
            log(text.c_str());
        }
        return true;
    } catch(const std::exception& e) {if(log){std::string text="Frame potions: ";text+=e.what();text+="; retaining icon fallback";log(text.c_str());}return false;}
}
bool Models::Render(ID3D12GraphicsCommandList* list,ID3D12Resource* target,unsigned eye,interaction::Pose view,math::Fov f,const Scene& scene,const SceneDepth& depth) {
    if(!impl||eye>1||!scene.count||scene.count>scene.items.size()||!interaction::Valid(view))return false;
    auto& p=*impl;const auto desc=target->GetDesc();
    if(desc.Width!=p.width||desc.Height!=p.height||desc.SampleDesc.Count!=1||!RenderTargetCompatible(desc.Format,p.format))return false;
    auto rtv=p.rtv->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=eye*p.rtvStride;
    D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};viewDesc.Format=p.format;viewDesc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    p.device->CreateRenderTargetView(target,&viewDesc,rtv);auto dsv=p.dsv->GetCPUDescriptorHandleForHeapStart();dsv.ptr+=eye*p.dsvStride;
    list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);list->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    const D3D12_VIEWPORT vp{0,0,float(p.width),float(p.height),0,1};const D3D12_RECT rect{0,0,LONG(p.width),LONG(p.height)};
    list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&rect);list->SetGraphicsRootSignature(p.root.Get());list->SetPipelineState(p.pipeline.Get());
    ID3D12DescriptorHeap* heaps[]={p.srv.Get()};list->SetDescriptorHeaps(1,heaps);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // One depth descriptor per eye, updated only after the caller's copy fence.
    const bool depthFormat=depth.format==DXGI_FORMAT_R32_FLOAT||depth.format==DXGI_FORMAT_R16_UNORM||
        depth.format==DXGI_FORMAT_R24_UNORM_X8_TYPELESS||depth.format==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    bool useDepth=depth.resource&&depthFormat&&interaction::Valid(depth.view)&&
        std::isfinite(depth.nearMetres)&&depth.nearMetres>0&&depth.nearMetres<1&&
        std::isfinite(depth.scale[0]+depth.scale[1])&&depth.scale[0]>0&&depth.scale[0]<=1&&depth.scale[1]>0&&depth.scale[1]<=1&&
        std::isfinite(depth.fov.left+depth.fov.right+depth.fov.down+depth.fov.up)&&
        depth.fov.left<depth.fov.right&&depth.fov.down<depth.fov.up;
    if(useDepth){const auto d=depth.resource->GetDesc();useDepth=d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.SampleDesc.Count==1&&d.DepthOrArraySize==1;}
    auto depthCpu=p.srv->GetCPUDescriptorHandleForHeapStart();depthCpu.ptr+=(Count+ExtraCount+ice::Count+eye)*p.srvStride;
    D3D12_SHADER_RESOURCE_VIEW_DESC depthView{};depthView.Format=useDepth?depth.format:DXGI_FORMAT_R32_FLOAT;
    depthView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;depthView.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;depthView.Texture2D.MipLevels=1;
    p.device->CreateShaderResourceView(useDepth?depth.resource:nullptr,&depthView,depthCpu);
    auto depthGpu=p.srv->GetGPUDescriptorHandleForHeapStart();depthGpu.ptr+=(Count+ExtraCount+ice::Count+eye)*p.srvStride;
    list->SetGraphicsRootDescriptorTable(2,depthGpu);
    auto draw=[&](unsigned i) {
        const auto& item=scene.items[i];if(!interaction::Valid(item.pose))return;
        const auto code=(item.code&0xffffff)|(uint32_t(' ')<<24);unsigned mesh=0;while(mesh<p.meshCount&&p.meshes[mesh].code!=code)++mesh;if(mesh==p.meshCount)return;
        const bool icy=ice::IsCode(code);list->SetPipelineState(icy?p.icePipeline.Get():p.pipeline.Get());
        auto& m=p.meshes[mesh];float constants[56]{};math::Projection(f.left,f.right,f.down,f.up,.02f,constants);
        const auto inverse=math::Conjugate(view.q),rotation=math::Multiply(inverse,item.pose.q);
        const auto translation=math::Rotate(inverse,item.pose.p-view.p),light=math::Rotate(inverse,math::Normalized({-.4f,.8f,.3f}));
        constants[16]=rotation.x;constants[17]=rotation.y;constants[18]=rotation.z;constants[19]=rotation.w;
        constants[20]=translation.x;constants[21]=translation.y;constants[22]=translation.z;
        constants[23]=item.scale>0&&item.scale<=4?item.scale:0;   // the shader's size multiplier (0 = 1)
        constants[24]=light.x;constants[25]=light.y;constants[26]=light.z;
        constants[28]=item.empty?1:0;constants[29]=item.highlight?1:0;constants[30]=p.encode?1:0;
        constants[31]=icy?1:0;constants[27]=icy&&item.mirrorX?-1:0;
        if(icy&&useDepth){
            const auto inverseDepth=math::Conjugate(depth.view.q);
            const auto dq=math::Multiply(inverseDepth,view.q);const auto dt=math::Rotate(inverseDepth,view.p-depth.view.p);
            constants[40]=dq.x;constants[41]=dq.y;constants[42]=dq.z;constants[43]=dq.w;
            constants[44]=dt.x;constants[45]=dt.y;constants[46]=dt.z;
            constants[48]=std::tan(depth.fov.left);constants[49]=std::tan(depth.fov.right);
            constants[50]=std::tan(depth.fov.down);constants[51]=std::tan(depth.fov.up);
            constants[52]=depth.scale[0];constants[53]=depth.scale[1];constants[54]=depth.nearMetres;constants[55]=.01f;
        }
        if(item.occR>0) {   // the forearm capsule in view space (the shader hides pixels behind it)
            const auto a=math::Rotate(inverse,item.occA-view.p),b=math::Rotate(inverse,item.occB-view.p);
            constants[32]=a.x;constants[33]=a.y;constants[34]=a.z;constants[35]=item.occR;
            constants[36]=b.x;constants[37]=b.y;constants[38]=b.z;
        }
        constants[39]=item.width>0&&item.width<=4?item.width:0;   // the size across X and Z (0 = the scale)
        list->SetGraphicsRoot32BitConstants(0,56,constants,0);auto srv=p.srv->GetGPUDescriptorHandleForHeapStart();srv.ptr+=mesh*p.srvStride;list->SetGraphicsRootDescriptorTable(1,srv);
        list->IASetVertexBuffers(0,1,&m.vb);list->IASetIndexBuffer(&m.ib);list->DrawIndexedInstanced(m.count,1,0,0,0);
    };
    std::array<unsigned,9+bottles::MaxBottles+BoneItems> transparent{};unsigned n=0;
    for(unsigned i=0;i<scene.count;++i)if(ice::IsCode(scene.items[i].code))transparent[n++]=i;else draw(i);
    const auto inverse=math::Conjugate(view.q);
    std::sort(transparent.begin(),transparent.begin()+n,[&](unsigned a,unsigned b){
        return math::Rotate(inverse,scene.items[a].pose.p-view.p).z<math::Rotate(inverse,scene.items[b].pose.p-view.p).z;});
    for(unsigned i=0;i<n;++i)draw(transparent[i]);
    return true;
}
}
