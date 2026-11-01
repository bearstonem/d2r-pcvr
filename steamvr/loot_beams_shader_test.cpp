// Compile the production loot-beam functions (D2R_DepthFog.fx Beams) with controlled
// views and run them on D3D11 WARP. No game, headset or copied beam algorithm: the
// checks are what a beam must and must not light.
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
// One view: the eye at the hero's origin looking down -z (identity axes, 90 degree
// projection). beam: its foot relative to the hero, w strength; colour: rgb, w phase;
// view: the pixel's ndc x, y, how far the scene is along it (view depth, < 0 = sky)
// and how much of the game's interface covers the pixel (0 = none).
struct Samples {
    std::array<float,4> beam{0,-2,-10,1};
    std::array<float,4> colour{1,168.f/255,0,0};
    std::array<float,4> view{0,0,-1,0};
};
static const char* Fixture=R"hlsl(
struct Samples {float4 beam,colour,view;};
StructuredBuffer<Samples> samples : register(t0);
RWStructuredBuffer<float4> output : register(u0);
static uint fixture;
#define BeamPos0 samples[fixture].beam
#define BeamCol0 samples[fixture].colour
static const float4 BeamPos1=0,BeamPos2=0,BeamPos3=0,BeamPos4=0,BeamPos5=0,BeamPos6=0,BeamPos7=0;
static const float4 BeamCol1=1,BeamCol2=1,BeamCol3=1,BeamCol4=1,BeamCol5=1,BeamCol6=1,BeamCol7=1;
static const float4 BeamShape=float4(0.7,56,2.8,1);
static const float BeamFloorTol=0.6, Timer=0;
#define UiMaskOn (samples[fixture].view.w > 0)
static const bool UiMaskOff=false;
#define GameLayerSmp 0
#define tex2Dlod(s, p) float4(0, 0, 0, 1.0 - samples[fixture].view.w)
static const float4 SkyProj0=float4(1,1,0,0), SkyProj1=float4(1,1,0,0);
static const float3 CamRight0=float3(1,0,0),CamRight1=float3(1,0,0);
static const float3 CamUp0=float3(0,1,0),CamUp1=float3(0,1,0);
static const float3 CamBack0=float3(0,0,1),CamBack1=float3(0,0,1);
static const float3 OutlineEye0=0,OutlineEye1=0;
)hlsl";
static const std::array<float,3> Background{.1f,.1f,.1f};
int main(int argc,char** argv) {try {
    if(argc!=2)throw std::runtime_error("expected D2R_DepthFog.fx path");
    std::ifstream file(argv[1]);if(!file)throw std::runtime_error("cannot read production shader");
    const std::string source{std::istreambuf_iterator<char>(file),{}};
    std::string shader=Fixture;
    for(const char* name:{"void BeamRay(","float3 BeamLight(","float3 Beams("})shader+=Function(source,name);
    shader+=R"hlsl(
        [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {
            fixture=id.x;
            const float4 v=samples[fixture].view;
            const float2 uv=float2(v.x*0.5+0.5,0.5-v.y*0.5);
            const float3 dir=float3(v.x,v.y,-1);
            const float ok=v.z>=0?1:0;
            output[id.x]=float4(Beams(float3(0.1,0.1,0.1),uv,0,dir*max(v.z,0),ok),1);
        })hlsl";
    std::vector<Samples> inputs;std::vector<const char*> names;
    const auto add=[&](const char* name,Samples s){inputs.push_back(s);names.push_back(name);};
    Samples s;
    add("0 straight at the beam, sky behind",s);
    s.view[2]=5;add("1 a wall in front of the beam",s);
    s.view[2]=12;add("2 a wall just behind the beam",s);
    s.view={.5f,0,-1,0};add("3 looking well beside the beam",s);
    s={};s.beam[3]=0;add("4 no beam (strength 0)",s);
    s={};s.view={0,-.2f,10,0};add("5 the floor at the beam's foot",s);
    s={};s.beam[1]=-80;add("6 above the beam's top",s);
    s={};s.beam[2]=10;add("7 the beam behind the eye",s);
    s={};s.colour={0,252.f/255,0,0};add("8 a set beam is green",s);
    s={};s.view={.5f,-.2f,10,0};add("9 the floor 5 units from the foot",s);
    s={};s.view={0,-.2f,10,0};s.beam[3]=.5f;add("10 half strength floor glow",s);
    s={};s.view={0,0,-1,1};add("11 a label over the beam",s);
    s={};s.view={0,-.2f,10,.5f};add("12 half a label over the floor glow",s);

    ComPtr<ID3DBlob> blob,errors;
    const auto hr=D3DCompile(shader.data(),shader.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors) std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile production loot beams");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"WARP device");
    ComPtr<ID3D11ComputeShader> compute;
    Check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&compute),"compute shader");
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(inputs.size()*sizeof(Samples));desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(Samples);
    D3D11_SUBRESOURCE_DATA initial{inputs.data(),0,0};ComPtr<ID3D11Buffer> input,output,readback;
    Check(device->CreateBuffer(&desc,&initial,&input),"fixture inputs");
    ComPtr<ID3D11ShaderResourceView> srv;Check(device->CreateShaderResourceView(input.Get(),nullptr,&srv),"fixture view");
    desc.ByteWidth=UINT(inputs.size()*4*sizeof(float));desc.StructureByteStride=4*sizeof(float);
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Check(device->CreateBuffer(&desc,nullptr,&output),"output");
    ComPtr<ID3D11UnorderedAccessView> uav;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav),"output view");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(device->CreateBuffer(&desc,nullptr,&readback),"readback");
    auto* rawSrv=srv.Get();auto* rawUav=uav.Get();context->CSSetShader(compute.Get(),nullptr,0);
    context->CSSetShaderResources(0,1,&rawSrv);context->CSSetUnorderedAccessViews(0,1,&rawUav,nullptr);
    context->Dispatch(UINT(inputs.size()),1,1);context->CopyResource(readback.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map results");
    std::vector<std::array<float,3>> got(inputs.size());
    const auto* values=static_cast<const float*>(mapped.pData);
    for(size_t i=0;i<inputs.size();++i)for(int c=0;c<3;++c)got[i][size_t(c)]=values[i*4+size_t(c)];
    context->Unmap(readback.Get(),0);context->ClearState();

    bool passed=true;
    const auto expect=[&](bool ok,size_t i,const char* what){
        if(!ok){std::fprintf(stderr,"%s: %s (got %.4f %.4f %.4f)\n",names[i],what,got[i][0],got[i][1],got[i][2]);passed=false;}
    };
    const auto unchanged=[&](size_t i){
        bool same=true;for(int c=0;c<3;++c)same=same&&std::isfinite(got[i][size_t(c)])&&std::abs(got[i][size_t(c)]-Background[size_t(c)])<.0005f;
        expect(same,i,"must leave the picture alone");
    };
    const auto gain=[&](size_t i,int c){return got[i][size_t(c)]-Background[size_t(c)];};
    // Seen against the sky: bright, orange (red over green, no blue added).
    // Not washed out to yellow or white: green well under red, little blue.
    expect(gain(0,0)>.6f&&gain(0,1)>.3f&&gain(0,1)<gain(0,0)*.85f&&gain(0,2)<.2f,0,"an orange beam over the sky");
    unchanged(1);                                                     // hidden by the wall
    expect(gain(2,0)>.6f,2,"a beam in front of a wall shows");
    expect(gain(3,0)<.01f&&gain(3,1)<.01f,3,"no light far beside the beam");
    unchanged(4);
    // Its foot on the floor: the floor glow, in the beam's colour (more red than green, no blue).
    expect(gain(5,0)>.3f&&gain(5,1)>.15f&&gain(5,0)>gain(5,1)&&std::abs(gain(5,2))<.0005f,5,"the floor glows round the foot");
    unchanged(6);
    unchanged(7);
    expect(gain(8,1)>.6f&&gain(8,0)<gain(8,1),8,"a green beam");
    expect(gain(9,0)<.01f,9,"the floor glow is gone 5 units out");
    expect(std::abs(gain(10,0)*2-gain(5,0))<.01f,10,"half strength, half the light");
    unchanged(11);                                                    // the game's interface stays readable
    expect(std::abs(gain(12,0)*2-gain(5,0))<.01f,12,"half covered, half the light");
    if(!passed) throw std::runtime_error("loot beam regression");
    std::printf("Production loot beams: %zu views passed (WARP; headset validation separate)\n",inputs.size());
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
