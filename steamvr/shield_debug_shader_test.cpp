// Compile the production debug-draw functions with controlled scene samples and run
// them on D3D11 WARP. No game, headset, extracted assets or copied projection algorithm.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* what) {
    if(FAILED(hr)) throw std::runtime_error(what);
}
static std::string Function(const std::string& source,const char* signature) {
    const auto start=source.find(signature),body=source.find('{',start);
    if(start==std::string::npos || body==std::string::npos) throw std::runtime_error(signature);
    unsigned nesting=1;
    for(auto i=body+1;i<source.size();++i) {
        if(source[i]=='{') ++nesting;
        if(source[i]=='}' && --nesting==0) return source.substr(start,i+1-start)+"\n";
    }
    throw std::runtime_error("unterminated shader function");
}
struct Samples {
    std::array<float,4> control{1,0,-2,0}; // enabled, hit, center Z, segment-only
    std::array<float,4> pixels{665.6f,512,614.4f,512}; // left xy, right xy
};
struct Case {const char* name;Samples input;std::array<float,3> expected;};
static const char* Fixture=R"hlsl(
struct Samples {float4 control,pixels;};
StructuredBuffer<Samples> samples : register(t0);
RWStructuredBuffer<float4> output : register(u0);
static uint fixture;
#define ShieldDebugOn (samples[fixture].control.x!=0)
#define ShieldDebugHit (samples[fixture].control.y!=0)
#define ShieldDebugCenter float3(0,0,samples[fixture].control.z)
#define BUFFER_WIDTH 1024
#define BUFFER_HEIGHT 1024
static const float3 ShieldDebugRight=float3(0.5,0,0), ShieldDebugUp=float3(0,0.6,0);
static const float3 ShieldDebugNormal=float3(0,0,0.35), ShieldDebugColor=float3(0.1,1,0.2);
static const float3 ShieldDebugHitFrom=float3(-1,0,-2),ShieldDebugHitTo=float3(1,0,-2);
static const float4 SkyProj0=float4(1,1,0,0), SkyProj1=float4(1,1,0,0);
static const float3 CamRight0=float3(1,0,0),CamRight1=float3(1,0,0);
static const float3 CamUp0=float3(0,1,0),CamUp1=float3(0,1,0);
static const float3 CamBack0=float3(0,0,1),CamBack1=float3(0,0,1);
static const float3 PhantomEye0=float3(-0.1,0,0),PhantomEye1=float3(0.1,0,0);
static const bool PhantomOn=false,BoneOn=false;
static const float3 PhantomA=0,PhantomB=0,BoneO=0,BoneX=0,BoneY=0,BoneZ=0;
#define tex2D(s, uv) float4(0.02,0.03,0.04,1)
)hlsl";
int main(int argc,char** argv) {try {
    if(argc!=2)throw std::runtime_error("expected D2R_DepthFog.fx path");
    std::ifstream file(argv[1]);if(!file)throw std::runtime_error("cannot read production shader");
    const std::string source{std::istreambuf_iterator<char>(file),{}};
    std::string shader=Fixture;
    for(const char* name:{"float3 PhantomPixel(","float PhantomAhead(","float3 Segment(","float3 ShieldDebug(","float3 Phantom("})shader+=Function(source,name);
    shader+=R"hlsl(
        [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {
            fixture=id.x;
            const float2 pixel=id.y==0?samples[fixture].pixels.xy:samples[fixture].pixels.zw;
            float3 c=Phantom(float4(pixel,0,1),pixel/1024.0,id.y);
            if(samples[fixture].control.w!=0)c=Segment(float3(0.02,0.03,0.04),pixel,float3(0,0,-2),float3(0.2,0,1),id.y,ShieldDebugColor);
            output[id.x*2+id.y]=float4(c,1);
        })hlsl";
    std::vector<Case> cases;
    const auto add=[&](const char* name,Samples s,std::array<float,3> expected){cases.push_back({name,s,expected});};
    const std::array<float,3> line{.1f,1,.2f},background{.02f,.03f,.04f};Samples s;
    add("wireframe at each eye's projected edge",s,line);
    s.control[0]=0;add("toggle off preserves scene",s,background);
    s={};s.pixels={614.4f,512,665.6f,512};add("opposite eye edge must not be reused",s,background);
    s={};s.control[2]=2;add("behind camera is clipped",s,background);
    s={};s.pixels={600,512,600,512};add("outline does not fill the shield face",s,background);
    s.control[1]=1;add("native attacker direction shown on hit",s,line);
    s.control[0]=0;add("toggle hides hit feedback too",s,background);
    s={};s.pixels={600,512,600,512};s.control[3]=1;add("line crossing eye is clipped rather than dropped",s,line);
    ComPtr<ID3DBlob> blob,errors;
    const auto hr=D3DCompile(shader.data(),shader.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors) std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile production shield overlay");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"WARP device");
    ComPtr<ID3D11ComputeShader> compute;
    Check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&compute),"compute shader");
    std::vector<Samples> inputs;for(const auto& c:cases) inputs.push_back(c.input);
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(inputs.size()*sizeof(Samples));desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(Samples);
    D3D11_SUBRESOURCE_DATA initial{inputs.data(),0,0};ComPtr<ID3D11Buffer> input,output,readback;
    Check(device->CreateBuffer(&desc,&initial,&input),"fixture inputs");
    ComPtr<ID3D11ShaderResourceView> srv;Check(device->CreateShaderResourceView(input.Get(),nullptr,&srv),"fixture view");
    desc.ByteWidth=UINT(cases.size()*2*4*sizeof(float));desc.StructureByteStride=4*sizeof(float);
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Check(device->CreateBuffer(&desc,nullptr,&output),"output");
    ComPtr<ID3D11UnorderedAccessView> uav;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav),"output view");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(device->CreateBuffer(&desc,nullptr,&readback),"readback");
    auto* rawSrv=srv.Get();auto* rawUav=uav.Get();context->CSSetShader(compute.Get(),nullptr,0);
    context->CSSetShaderResources(0,1,&rawSrv);context->CSSetUnorderedAccessViews(0,1,&rawUav,nullptr);
    context->Dispatch(UINT(cases.size()),2,1);context->CopyResource(readback.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map results");
    const auto* values=static_cast<const float*>(mapped.pData);bool passed=true;
    for(size_t i=0;i<cases.size();++i)for(unsigned eye=0;eye<2;++eye)for(unsigned channel=0;channel<3;++channel) {
        const float got=values[(i*2+eye)*4+channel],want=cases[i].expected[channel];
        if(!std::isfinite(got)||std::abs(got-want)>.0001f) {
            std::fprintf(stderr,"%s eye=%u channel=%u: got %.4f expected %.4f\n",cases[i].name,eye,channel,got,want);passed=false;
        }
    }
    context->Unmap(readback.Get(),0);context->ClearState();
    if(!passed) throw std::runtime_error("shield overlay regression");
    std::printf("Production shield overlay: %zu scenes passed for both eyes (WARP; headset validation separate)\n",cases.size());
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
