// Tests production immunity VS/PS functions with controlled views on WARP.
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
    std::array<float,4> monster{0,-.65f,-4,1};
    std::array<float,4> control{1,10,0,0}; // on, surface depth (0=sky), UI cover, time
    std::array<float,4> sample{0,0,1,0}; // type, trail, texture value, pixel u
};
static const char* Fixture=R"hlsl(
struct Samples {float4 monster,control,sample;};
StructuredBuffer<Samples> samples:register(t0);
RWStructuredBuffer<float4> output:register(u0);
static uint fixture;
#define ImmunityOn (samples[fixture].control.x>.5)
static float4 ImmunityPos[24];
static float4 ImmunityPhase[24];
static const float4 ImmunityShape=float4(.65,.65,.16,.8);
#define Timer samples[fixture].control.w
static const float NearPlane=.1;
static const float4 SkyProj0=float4(1,1,0,0),SkyProj1=float4(1,1,0,0);
static const float3 CamRight0=float3(1,0,0),CamRight1=float3(1,0,0);
static const float3 CamUp0=float3(0,1,0),CamUp1=float3(0,1,0);
static const float3 CamBack0=float3(0,0,1),CamBack1=float3(0,0,1);
static const float3 OutlineEye0=float3(-.1,0,0),OutlineEye1=float3(.1,0,0);
#define BUFFER_RCP_WIDTH (1.0/256)
#define BUFFER_RCP_HEIGHT (1.0/256)
static const bool UpsideDown=false,UiMaskOff=false;
#define UiMaskOn (samples[fixture].control.z>0)
#define GameLayerSmp 0
#define ImmunityGlowSmp 1
#define tex2Dlod(s,uv) ((s)==0?float4(0,0,0,1-samples[fixture].control.z):samples[fixture].sample.zzzz)
float DepthWidth(){return 256;}
float4 DepthAt(float2 uv){return samples[fixture].control.y>0?NearPlane/samples[fixture].control.y:0;}
)hlsl";
int main(int argc,char** argv){try {
    if(argc!=2)throw std::runtime_error("expected production shader path");
    std::ifstream file(argv[1]);if(!file)throw std::runtime_error("cannot read production shader");
    const std::string source{std::istreambuf_iterator<char>(file),{}};
    std::string shader=Fixture;
    const auto start=source.find("struct ImmunityVertex {"),end=source.find("};",start);
    if(start==std::string::npos||end==std::string::npos)throw std::runtime_error("missing vertex structure");
    shader+=source.substr(start,end+2-start)+"\n";
    for(const char* name:{"ImmunityVertex ImmunitySprite(","float4 PS_Immunity(","ImmunityVertex VS_Immunity(","ImmunityVertex VS_ImmunityR("})shader+=Function(source,name);
    const auto compile=[&](const std::string& code,const char* entry,const char* profile,bool texture){
        ComPtr<ID3DBlob> blob,errors;
        const D3D_SHADER_MACRO defines[]={{"D2R_IMMUNITY_TEXTURE",texture?"1":"0"},{nullptr,nullptr}};
        const auto hr=D3DCompile(code.data(),code.size(),argv[1],defines,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
        if(FAILED(hr)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr,"compile production immunity shader");return blob;
    };
    for(bool texture:{false,true}){
        compile(shader,"VS_Immunity","vs_5_0",texture);
        compile(shader,"VS_ImmunityR","vs_5_0",texture);
        compile(shader,"PS_Immunity","ps_5_0",texture);
    }
    shader+=R"hlsl(
    [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){
        fixture=id.x;
        ImmunityPos[0]=samples[fixture].monster;
        const uint vertex=(uint(samples[fixture].sample.x)*3+uint(samples[fixture].sample.y))*6;
        ImmunityVertex v=ImmunitySprite(vertex,int(id.y));
        output[(id.x*2+id.y)*2]=float4(v.position.xy/v.position.w,v.depth,1);
        v.position.xy=float2(128,128);v.uv=float2(samples[fixture].sample.w+.5,.5);
        output[(id.x*2+id.y)*2+1]=PS_Immunity(v);
    })hlsl";
    std::vector<Samples> inputs;
    for(unsigned type=0;type<6;++type){Samples s;s.monster[3]=float(1u<<type);s.sample[0]=float(type);inputs.push_back(s);}
    Samples s;
    s.control[1]=1;inputs.push_back(s);  // wall in front
    s={};s.control[0]=0;inputs.push_back(s); // off
    s={};s.monster[3]=0;inputs.push_back(s); // no immunity
    s={};s.control[2]=1;inputs.push_back(s); // interface fully covers pixel
    s={};s.monster[2]=4;inputs.push_back(s); // behind camera
    s={};s.control[1]=0;inputs.push_back(s); // sky
    s={};s.sample[3]=.5f;inputs.push_back(s); // transparent card edge
    s={};s.monster[3]=63;inputs.push_back(s); // multiple types preserve physical wisp
    s={};s.sample[1]=1;inputs.push_back(s); // first trail is dimmer
    s={};s.sample[2]=0;inputs.push_back(s); // black texture
    s={};s.monster[3]=4;inputs.push_back(s); // fire immune must not draw physical wisp
    s={};s.control[2]=.5f;inputs.push_back(s); // half covered by interface
    s={};s.sample[2]=.5f;inputs.push_back(s); // real mask modulates brightness
    s={};s.monster[1]+=2;inputs.push_back(s); // monster above the player's floor
    s={};s.monster[1]-=2;inputs.push_back(s); // monster below the player's floor
    for(bool texture:{false,true}) {
        auto blob=compile(shader,"main","cs_5_0",texture);
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"WARP device");
        ComPtr<ID3D11ComputeShader> compute;Check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&compute),"compute shader");
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(inputs.size()*sizeof(Samples));desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(Samples);
        D3D11_SUBRESOURCE_DATA initial{inputs.data(),0,0};ComPtr<ID3D11Buffer> input,output,readback;
        Check(device->CreateBuffer(&desc,&initial,&input),"fixture inputs");
        ComPtr<ID3D11ShaderResourceView> srv;Check(device->CreateShaderResourceView(input.Get(),nullptr,&srv),"fixture view");
        desc.ByteWidth=UINT(inputs.size()*4*4*sizeof(float));desc.StructureByteStride=4*sizeof(float);
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        Check(device->CreateBuffer(&desc,nullptr,&output),"output");
        ComPtr<ID3D11UnorderedAccessView> uav;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav),"output view");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateBuffer(&desc,nullptr,&readback),"readback");
        auto* rawSrv=srv.Get();auto* rawUav=uav.Get();context->CSSetShader(compute.Get(),nullptr,0);
        context->CSSetShaderResources(0,1,&rawSrv);context->CSSetUnorderedAccessViews(0,1,&rawUav,nullptr);
        context->Dispatch(UINT(inputs.size()),2,1);context->CopyResource(readback.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map results");
        const float* values=static_cast<const float*>(mapped.pData);
        const auto pixel=[&](size_t scene,unsigned eye,unsigned channel){return values[(scene*4+eye*2+1)*4+channel];};
        const auto expect=[&](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
        const float colors[6][3]={{1,1,1},{.72f,.22f,1},{1,.06f,.025f},{1,.85f,.04f},{.08f,.42f,1},{.06f,1,.12f}};
        for(unsigned eye=0;eye<2;++eye) {
            for(size_t i=0;i<6;++i)for(unsigned c=0;c<3;++c)
                expect(std::abs(pixel(i,eye,c)-colors[i][c]*.8f)<.0001f,"each immunity keeps its own color in both eyes");
            for(size_t i:{6u,7u,8u,9u,10u,12u,16u})for(unsigned c=0;c<3;++c)
                expect(std::abs(pixel(i,eye,c))<.0001f,"hidden/off/inactive wisps leave scene unchanged");
            expect(std::abs(pixel(11,eye,0)-.8f)<.0001f,"visible against sky");
            expect(std::abs(pixel(13,eye,0)-.8f)<.0001f,"multiple immunities retain each color");
            expect(std::abs(pixel(14,eye,0)-.8f*.38f)<.0001f,"trail fades");
            expect(std::abs(pixel(15,eye,0)-(texture?0:.8f))<.0001f,"texture cache is sampled only when present");
            expect(std::abs(pixel(17,eye,0)-.4f)<.0001f,"interface coverage scales glow");
            expect(std::abs(pixel(18,eye,0)-(texture?.4f:.8f))<.0001f,"Feral Rage texture controls the glow");
            const auto screenY=[&](size_t scene){return values[(scene*4+eye*2)*4+1];};
            expect(std::abs(screenY(19)-screenY(0)-.5f)<.0001f,"raised monster raises its wisp in both eyes");
            expect(std::abs(screenY(20)-screenY(0)+.5f)<.0001f,"lower monster lowers its wisp in both eyes");
        }
        expect(values[0]>values[8],"each eye projects from its own position");
        context->Unmap(readback.Get(),0);context->ClearState();
    }
    std::printf("Production immunity shaders: VS/PS compile; %zu scenes in both eyes, with/without texture, passed on WARP\n",inputs.size());
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
