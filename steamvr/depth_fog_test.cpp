// Compile the production fog functions with controlled scene samples and run
// them on D3D11 WARP. No game, headset, extracted assets or copied fog algorithm.
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
    std::array<float,4> colour{1,1,1,1};
    // Center/neighbour depth, depth 2..16 pixels below, blurred particle fog, depth width.
    std::array<float,4> scene{0,0,0,1024};
    // Sky, ceiling, fog, upside-down depth.
    std::array<float,4> flags{0,0,1,0};
    std::array<float,4> fog{0,0,0,1}; // RGB, strength
    // Optional exact one-pixel neighbours: right, left, down, up; -1 uses scene depth.
    std::array<float,4> edges{-1,-1,-1,-1};
};
struct Case {const char* name; Samples input; std::array<float,3> expected;};
static const char* Fixture=R"hlsl(
struct Samples { float4 colour,scene,flags,fog,edges; };
StructuredBuffer<Samples> samples : register(t0);
RWStructuredBuffer<float4> output : register(u0);
static uint fixture;
#define SkyOn (samples[fixture].flags.x != 0)
#define CeilOn (samples[fixture].flags.y != 0)
#define FogOn (samples[fixture].flags.z != 0)
#define UpsideDown (samples[fixture].flags.w != 0)
#define FogColor samples[fixture].fog.rgb
#define FogStrength samples[fixture].fog.w
#define BUFFER_RCP_WIDTH (1.0 / 1024.0)
#define BUFFER_RCP_HEIGHT (1.0 / 1024.0)
static const float NearPlane=0.2, FogStart=100, FogEnd=200, FogCurve=1, FogBlur=3;
static const bool FogFixed=true, ShowDistance=false, ShowCeilingCut=false;
static const float4 SkyProj0=float4(1,1,0,0), SkyProj1=float4(1,1,0,0);
static const float3 CamRight0=float3(1,0,0), CamRight1=float3(1,0,0);
static const float3 CamUp0=float3(0,1,0), CamUp1=float3(0,1,0);
static const float3 CamBack0=float3(0,0,1), CamBack1=float3(0,0,1);
static const float3 CeilEye0=0, CeilEye1=0;
static const float2 CeilHero0=0, CeilHero1=0;
static const float CeilScale=10, CeilHeight=20, CeilHalo=1, HudTop=0, SkyTex=0, SkyBrightness=1;
static const int FogColorSmp=0, VoidFogSmp=1, HudMaskSmp=2;
float4 Sample(int samplerId) {
    if(samplerId==FogColorSmp) return samples[fixture].colour;
    if(samplerId==VoidFogSmp) return samples[fixture].scene.zzzz;
    return 0;
}
#define tex2D(s, uv) Sample(s)
#define tex2Dlod(s, uv) Sample(s)
float DepthWidth() { return samples[fixture].scene.w; }
float4 DepthAt(float2 uv) {
    const float2 offset=(uv-float2(0.5,0.25))*1024;
    const float4 edges=samples[fixture].edges;
    if(all(abs(offset-float2(1,0))<0.01) && edges.x>=0) return edges.xxxx;
    if(all(abs(offset-float2(-1,0))<0.01) && edges.y>=0) return edges.yyyy;
    if(all(abs(offset-float2(0,1))<0.01) && edges.z>=0) return edges.zzzz;
    if(all(abs(offset-float2(0,-1))<0.01) && edges.w>=0) return edges.wwww;
    const float down=(UpsideDown ? -1 : 1)*(uv.y-0.25)*1024;
    return down>=1.5 ? samples[fixture].scene.yyyy : samples[fixture].scene.xxxx;
}
float Hash(float2 p) { return 0.5; }
float CeilUnder(float2 p) { return 20; }
bool Ceiling(float3 dir,int eye,out float3 colour,float limit) { colour=0.1; return true; }
float3 VoidGlowAt(float2 uv) { return 0; }
float3 Sky(float3 dir) { return 0.1; }
float3 PaintedSky(float3 dir) { return 0.1; }
float3 SkyGradient(float y) { return 0.1; }
)hlsl";
int main(int argc,char** argv) {try {
    if(argc!=2) throw std::runtime_error("expected D2R_DepthFog.fx path");
    std::ifstream file(argv[1]);
    if(!file) throw std::runtime_error("cannot read production fog shader");
    const std::string source{std::istreambuf_iterator<char>(file),{}};
    std::string shader=Fixture;
    shader+=Function(source,"float FogAt(");
    shader+=Function(source,"float FogAmount(");
    // Optional so running against the pre-fix shader demonstrates the regression.
    if(source.find("float ParticleFog(")!=std::string::npos) shader+=Function(source,"float ParticleFog(");
    shader+=Function(source,"float3 DepthFogWorld(");
    shader+=R"hlsl(
        [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {
            fixture=id.x;
            float2 uv=float2(0.5,UpsideDown ? 0.75 : 0.25);
            output[id.x*2+id.y]=float4(DepthFogWorld(float4(512,256,0,1),uv,id.y),1);
        })hlsl";
    std::vector<Case> cases;
    const auto add=[&](const char* name,Samples input,std::array<float,3> expected){cases.push_back({name,input,expected});};
    Samples s;
    add("near light over empty background",s,{1,1,1});
    s.scene[0]=s.scene[1]=.01f; add("near solid scenery",s,{1,1,1});
    s.scene[0]=s.scene[1]=.0005f; add("distant solid scenery still hidden",s,{0,0,0});
    s={};s.scene[2]=1;add("empty indoor background does not borrow distant ground fog",s,{1,1,1});
    s.scene[2]=.5f;add("empty indoor background does not borrow partial ground fog",s,{1,1,1});
    s={};s.colour={0,0,0,1};s.fog={.2f,.1f,.05f,1};
    add("custom background fog preserved",s,{.2f,.1f,.05f});
    s.fog[3]=.4f;add("partial background fog preserved",s,{.08f,.04f,.02f});
    s={};s.flags[2]=0;s.scene[2]=1;add("fog disabled",s,{1,1,1});
    s={};s.flags[0]=1;add("outdoor light unchanged",s,{1.1f,1.1f,1.1f});
    s.scene[2]=1;add("outdoor far effects still fade",s,{.1f,.1f,.1f});
    s={};s.flags[1]=1;add("ceiling light unchanged",s,{1.1f,1.1f,1.1f});
    s.scene[2]=1;add("ceiling far effects still fade",s,{.1f,.1f,.1f});
    s={};s.flags[0]=1;s.scene[1]=.0005f;add("outdoor nearby depth overrides blurred estimate",s,{.1f,.1f,.1f});
    // This flipped sample is below the horizon, where the sky takes FogColor.
    s.flags[3]=1;add("flipped outdoor depth uses correct downward direction",s,{0,0,0});
    s={};s.flags[0]=1;s.scene[1]=.01f;s.scene[2]=.6f;add("outdoor blurred distant fog retained",s,{.5f,.5f,.5f});
    s={};s.flags[0]=1;s.fog[3]=.4f;s.scene[1]=.0005f;add("outdoor partial fog strength",s,{.7f,.7f,.7f});
    s={};s.scene[3]=8;add("missing depth leaves colour intact",s,{1,1,1});
    for(unsigned edge=0;edge<4;++edge) {
        s={};s.scene[0]=s.scene[1]=.01f;s.edges[edge]=0;
        add("near geometry beside empty depth has no dark rim",s,{1,1,1});
        s={};s.edges[edge]=.01f;
        add("empty pixel beside near geometry has no dark rim",s,{1,1,1});
        s.scene[0]=s.scene[1]=.0005f;s.edges[edge]=0;
        add("far geometry beside empty depth still fades",s,{0,0,0});
        s={};s.edges[edge]=.0005f;
        add("empty hole in far geometry stays fogged",s,{0,0,0});
        s={};s.scene[0]=s.scene[1]=.01f;s.edges={0,0,0,0};s.edges[edge]=.0005f;
        add("far valid neighbour still suppresses dithered near speck",s,{0,0,0});
    }
    s={};s.scene[0]=s.scene[1]=.2f/150;s.edges={0,0,0,0};
    add("partly fogged silhouette retains its distance",s,{.5f,.5f,.5f});
    for(unsigned backdrop=0;backdrop<2;++backdrop) {
        s={};s.flags[backdrop]=1;s.scene[0]=s.scene[1]=.01f;s.edges={0,0,0,0};
        add("sky or ceiling does not reintroduce near silhouette rim",s,{1,1,1});
        s.scene[0]=s.scene[1]=.0005f;
        if(backdrop==0) add("outdoor distant silhouette still fades",s,{0,0,0});
        // At this ray angle the far surface is above the synthetic ceiling.
        // The existing ceiling-cut path preserves bright effects over its stone.
        else add("ceiling-cut bright effect preservation unchanged",s,{1.1f,1.1f,1.1f});
    }
    // A continuous wing crosses columns with unrelated near/far/no ground below.
    // Its native colour must match the same effect over a nearby pillar, rather
    // than acquire bands from the screen-space distance guess. Include dim colour
    // so the fix cannot merely classify saturated white pixels as special lights.
    for(float below:{0.f,.01f,.0005f})for(float estimate:{0.f,.5f,1.f})for(float flipped:{0.f,1.f}) {
        s={};s.colour={.3f,.7f,1,1};s.scene[1]=below;s.scene[2]=estimate;s.flags[3]=flipped;
        add("indoor effect colour independent of scenery in each column",s,{.3f,.7f,1});
    }
    s={};s.colour={.3f,.7f,1,1};s.scene[0]=s.scene[1]=.01f;
    add("same effect over nearby pillar has matching colour",s,{.3f,.7f,1});
    ComPtr<ID3DBlob> blob,errors;
    const auto hr=D3DCompile(shader.data(),shader.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors) std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile production fog functions");
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
    if(!passed) throw std::runtime_error("fog regression");
    std::printf("Production fog functions: %zu scenes passed for both eyes (WARP; headset validation separate)\n",cases.size());
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
