// The hero light (reshade/D2R_Lights.fxh HeroLight, #440): the production function on D3D11 WARP. Dark
// within 4 units of the eye (the face, the hands), lit on the floor round the hero, gone at twice its reach,
// and a wider reach (more Light Radius) visibly lights farther. No game, headset or extracted assets.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "fx_source.h"
#include "light_radius.h"
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* what) {if(FAILED(hr))throw std::runtime_error(what);}
static std::string Function(const std::string& source,const char* signature) {
    const auto start=source.find(signature),body=source.find('{',start);
    if(start==std::string::npos||body==std::string::npos)throw std::runtime_error(signature);
    unsigned nesting=1;
    for(auto i=body+1;i<source.size();++i) {
        if(source[i]=='{')++nesting;
        if(source[i]=='}'&&--nesting==0)return source.substr(start,i+1-start)+"\n";
    }
    throw std::runtime_error("unterminated shader function");
}
static std::string Statement(const std::string& source,const char* start) {
    const auto at=source.find(start),end=source.find(';',at);
    if(at==std::string::npos||end==std::string::npos)throw std::runtime_error(start);
    return source.substr(at,end+1-at)+"\n";
}
struct Case {std::array<float,4> light;std::array<float,4> point;std::array<float,4> normal;std::array<float,4> eye;};   // xyz+radius, xyz+strength

int main(int argc,char** argv) {try {
    if(argc!=2)throw std::runtime_error("expected D2R_DepthFog.fx path");
    const std::string source=fxsource::ReadWithIncludes(argv[1]);   // the effect with its feature headers (D2R_Lights.fxh ...) pasted in
    std::string shader=R"hlsl(
struct Case { float4 light, spot, normal, eye; };
StructuredBuffer<Case> cases : register(t0);
RWStructuredBuffer<float4> output : register(u0);
static float3 HeroLightPos;
static float HeroLightRadius, HeroLightStrength;
)hlsl";
    shader+=Statement(source,"static const float kHeroLightNear");
    shader+=Statement(source,"static const float3 kHeroLightColour");
    shader+=Function(source,"float Falloff(");
    shader+=Function(source,"float3 HeroLight(");
    shader+=R"hlsl(
[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {
    const Case c=cases[id.x];
    HeroLightPos=c.light.xyz;HeroLightRadius=c.light.w;HeroLightStrength=c.spot.w;
    output[id.x]=float4(HeroLight(c.spot.xyz,normalize(c.normal.xyz),c.eye.xyz),1);
})hlsl";
    ComPtr<ID3DBlob> blob,errors;
    const HRESULT hr=D3DCompile(shader.data(),shader.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile production HeroLight");

    // The hero stands at the origin on the floor (y 0), his eyes 6.5 up; the light at his shoulder (light_radius.h).
    const float eyeY=6.5f;
    const auto at=framevr::lightradius::HeroLightAt(0,eyeY,0,0);
    const float tuned=20.0f,strength=.25f,wider=tuned*framevr::lightradius::Scale(3,true,{});   // +3 Light Radius
    const auto make=[&](float radius,float x,float y,float z,float s,float ny=1){
        return Case{{at.x,at.y,at.z,radius},{x,y,z,s},{0,ny,ny==1?0.f:-1.f,0},{0,eyeY,0,0}};};
    std::vector<Case> cases={
        make(tuned,0,6.0f,1.5f,strength,0),     // 0 a hand before the face
        make(tuned,.4f,4.0f,.3f,strength,0),    // 1 the chest, under the light
        make(tuned,0,6.5f,3.9f,strength,0),     // 2 3.9 units ahead of the eyes
        make(tuned,0,0,0,strength),             // 3 the floor at his feet
        make(tuned,0,0,15,strength),            // 4 the floor 15 ahead
        make(tuned,0,0,30,strength),            // 5 the floor 30 ahead
        make(wider,0,0,30,strength),            // 6 the same with +3 Light Radius
        make(tuned,0,0,45,strength),            // 7 past twice the reach
        make(wider,0,0,45,strength),            // 8 lit with +3 Light Radius
        make(tuned,0,0,15,0),                   // 9 strength 0
    };
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"WARP device");
    ComPtr<ID3D11ComputeShader> compute;
    Check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&compute),"compute shader");
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(cases.size()*sizeof(Case));bd.Usage=D3D11_USAGE_IMMUTABLE;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=sizeof(Case);
    D3D11_SUBRESOURCE_DATA init{cases.data(),0,0};ComPtr<ID3D11Buffer> input,output,readback;
    Check(device->CreateBuffer(&bd,&init,&input),"cases");
    ComPtr<ID3D11ShaderResourceView> inputView;Check(device->CreateShaderResourceView(input.Get(),nullptr,&inputView),"case view");
    bd.ByteWidth=UINT(cases.size()*16);bd.StructureByteStride=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Check(device->CreateBuffer(&bd,nullptr,&output),"output");
    ComPtr<ID3D11UnorderedAccessView> outView;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&outView),"output view");
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(device->CreateBuffer(&bd,nullptr,&readback),"readback");
    auto* rawIn=inputView.Get();auto* rawOut=outView.Get();
    context->CSSetShader(compute.Get(),nullptr,0);context->CSSetShaderResources(0,1,&rawIn);
    context->CSSetUnorderedAccessViews(0,1,&rawOut,nullptr);
    context->Dispatch(UINT(cases.size()),1,1);context->CopyResource(readback.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map results");
    std::vector<std::array<float,4>> v(cases.size());std::memcpy(v.data(),mapped.pData,v.size()*16);
    context->Unmap(readback.Get(),0);
    const auto lum=[&](size_t i){const auto& c=v[i];if(!std::isfinite(c[0]+c[1]+c[2]))throw std::runtime_error("not finite");return .2126f*c[0]+.7152f*c[1]+.0722f*c[2];};
    if(lum(0)!=0||lum(1)!=0||lum(2)!=0)throw std::runtime_error("the hero light lights within 4 units of the eye (face, chest, hands)");
    if(!(lum(3)>.05f&&lum(3)<.5f))throw std::runtime_error("the floor at his feet: "+std::to_string(lum(3)));
    if(!(lum(4)>.03f))throw std::runtime_error("the floor 15 ahead is not lit");
    if(!(lum(5)>0&&lum(5)<lum(4)))throw std::runtime_error("the light does not fall off with distance");
    if(!(lum(6)>2*lum(5)))throw std::runtime_error("+3 Light Radius does not visibly widen it: "+std::to_string(lum(5))+" -> "+std::to_string(lum(6)));
    if(lum(7)!=0)throw std::runtime_error("lit past twice its reach");
    if(!(lum(8)>0))throw std::runtime_error("+3 Light Radius does not reach farther");
    if(lum(9)!=0)throw std::runtime_error("strength 0 still lights");
    std::printf("Hero light: dark within 4 units of the eye; feet %.3f, 15 ahead %.3f, 30 ahead %.3f -> %.3f with +3 Light Radius (reach %.0f -> %.0f), "
                "45 ahead 0 -> %.3f (WARP; headset testing separate)\n",lum(3),lum(4),lum(5),lum(6),tuned,wider,lum(8));
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
