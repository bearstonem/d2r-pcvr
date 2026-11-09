// The painted sky's band goes once round the circle (3072 x 1024), and our generated
// skies, 1536 px pictures laid twice side by side, look exactly as they did when the
// 1536 px band was wrapped twice. Runs the production PaintedSky on D3D11 WARP against
// synthetic textures, and reads the shipped placeholders (reshade/sky) with WIC.
// No game, headset or extracted assets.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "fx_source.h"
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
    throw std::runtime_error("unterminated sky function");
}
constexpr unsigned W=3072,H=1024,Half=W/2;
static constexpr float Pi=3.14159265f;

// The 12 band textures (6 acts, day and night) are declared 3072 x 1024: a picture of
// another size would be stretched by ReShade, so the placeholders must be that size.
static void CheckDeclarations(const std::string& source) {
    unsigned count=0;
    for(size_t at=source.find("texture2D Sky");at!=std::string::npos;at=source.find("texture2D Sky",at+1)) {
        const size_t eol=source.find('\n',at);
        const std::string line=source.substr(at,eol==std::string::npos?std::string::npos:eol-at);
        const std::string name=line.substr(10,line.find(' ',10)-10);
        if(name.rfind("SkyBand",0)!=0&&name.rfind("SkyNBand",0)!=0)continue;   // the caps
        ++count;
        if(line.find("Width = 3072; Height = 1024;")==std::string::npos)throw std::runtime_error(name+" is not declared 3072 x 1024");
    }
    if(count!=12)throw std::runtime_error("expected 12 sky band textures, found "+std::to_string(count));
}

// Each shipped band (day and night) is 3072 x 1024 and its two halves are the same
// picture: the old 1536 px band laid twice, so nothing changes until real art arrives.
static unsigned CheckPlaceholders(const std::wstring& folder) {
    ComPtr<IWICImagingFactory> wic;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC factory");
    unsigned checked=0;
    for(const wchar_t* kind:{L"D2R_Sky_",L"D2R_SkyNight_"})for(const wchar_t* act:{L"act1",L"act2",L"act3",L"act4",L"act5",L"snow"}) {
        const std::wstring path=folder+kind+act+L".png";
        const std::string name(path.begin(),path.end());
        ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
        Check(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),name.c_str());
        Check(decoder->GetFrame(0,&frame),"frame");
        UINT w=0,h=0;Check(frame->GetSize(&w,&h),"size");
        if(w!=W||h!=H)throw std::runtime_error(name+" is "+std::to_string(w)+"x"+std::to_string(h)+", not 3072x1024");
        Check(wic->CreateFormatConverter(&converter),"converter");
        Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"convert");
        std::vector<uint32_t> px(size_t(W)*H);
        Check(converter->CopyPixels(nullptr,W*4,UINT(px.size()*4),reinterpret_cast<BYTE*>(px.data())),"pixels");
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Half;++x)
            if(px[size_t(y)*W+x]!=px[size_t(y)*W+x+Half])throw std::runtime_error(name+": its two halves differ");
        ++checked;
    }
    return checked;
}

struct Gpu {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ComputeShader> compute;ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11ShaderResourceView> Texture(const std::vector<float>& rgba,unsigned w,unsigned h) {
        D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{rgba.data(),w*16,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
        Check(device->CreateTexture2D(&td,&data,&texture),"texture");
        Check(device->CreateShaderResourceView(texture.Get(),nullptr,&view),"texture view");
        return view;
    }
    // rays: xyz direction, w the timer (ms); two results per ray: PaintedSky, the old band
    std::vector<std::array<float,4>> Run(const std::vector<std::array<float,4>>& rays,ID3D11ShaderResourceView* now,ID3D11ShaderResourceView* old) {
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(rays.size()*16);bd.Usage=D3D11_USAGE_IMMUTABLE;
        bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
        D3D11_SUBRESOURCE_DATA init{rays.data(),0,0};ComPtr<ID3D11Buffer> input,output,readback;
        Check(device->CreateBuffer(&bd,&init,&input),"rays");
        ComPtr<ID3D11ShaderResourceView> inputView;Check(device->CreateShaderResourceView(input.Get(),nullptr,&inputView),"ray view");
        bd.ByteWidth*=2;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        Check(device->CreateBuffer(&bd,nullptr,&output),"output");
        ComPtr<ID3D11UnorderedAccessView> outView;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&outView),"output view");
        bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateBuffer(&bd,nullptr,&readback),"readback");
        ID3D11ShaderResourceView* views[]={now,old,inputView.Get()};auto* rawSampler=sampler.Get();auto* rawOut=outView.Get();
        context->CSSetShader(compute.Get(),nullptr,0);context->CSSetShaderResources(0,3,views);
        context->CSSetSamplers(0,1,&rawSampler);context->CSSetUnorderedAccessViews(0,1,&rawOut,nullptr);
        context->Dispatch(UINT(rays.size()),1,1);context->CopyResource(readback.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map results");
        std::vector<std::array<float,4>> out(rays.size()*2);
        std::memcpy(out.data(),mapped.pData,out.size()*16);
        context->Unmap(readback.Get(),0);context->ClearState();
        return out;
    }
};

static std::array<float,4> Ray(float headingDeg,float elevationDeg,float timer) {
    const float h=headingDeg*Pi/180,e=elevationDeg*Pi/180;
    return {std::sin(h)*std::cos(e),std::sin(e),std::cos(h)*std::cos(e),timer};
}

int main(int argc,char** argv) {try {
    if(argc!=2)throw std::runtime_error("expected D2R_DepthFog.fx path");
    const std::string source=fxsource::ReadWithIncludes(argv[1]);   // the effect with its feature headers (D2R_Sky.fxh ...) pasted in
    CheckDeclarations(source);
    const std::string fx=argv[1];
    const std::string dir=fx.substr(0,fx.find_last_of("/\\")+1);
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const unsigned pictures=CheckPlaceholders(std::wstring(dir.begin(),dir.end())+L"sky\\");

    std::string shader=R"hlsl(
Texture2D<float4> BandNow : register(t0);    // the 3072 px band
Texture2D<float4> BandOld : register(t1);    // the 1536 px band it was made from
StructuredBuffer<float4> rays : register(t2);
SamplerState wrapClamp : register(s0);       // as SkyBand*Smp: AddressU WRAP, AddressV CLAMP, linear
RWStructuredBuffer<float4> output : register(u0);
static float Timer;
static const float SkyDrift=1, SkyTex=1, SkyNightMix=0;
static const bool SkyCapOn=false;
static const float3 SkyZenith=0;
float3 BandAt(int slot, float4 t, bool night) { return BandNow.SampleLevel(wrapClamp, t.xy, t.w).rgb; }
float3 CapAt(int slot, float4 t, bool night) { return 0; }
// the band as it was: 1536 px wrapped twice round the circle
float3 OldBand(float3 dir) {
    const float el = degrees(asin(clamp(dir.y, -1.0, 1.0)));
    const float u = atan2(dir.x, dir.z) / 3.14159265 + Timer * 0.000001 * SkyDrift;
    return BandOld.SampleLevel(wrapClamp, float2(u, (65.0 - el) / 73.0), 0).rgb;
}
)hlsl";
    shader+=Function(source,"float3 PaintedSky(");
    shader+=R"hlsl(
[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {
    Timer=rays[id.x].w;
    const float3 d=normalize(rays[id.x].xyz);
    output[id.x*2]=float4(PaintedSky(d),1);
    output[id.x*2+1]=float4(OldBand(d),1);
})hlsl";
    ComPtr<ID3DBlob> blob,errors;
    const HRESULT hr=D3DCompile(shader.data(),shader.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile production PaintedSky");
    Gpu gpu;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&gpu.device,nullptr,&gpu.context),"WARP device");
    Check(gpu.device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&gpu.compute),"compute shader");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=D3D11_TEXTURE_ADDRESS_WRAP;sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    Check(gpu.device->CreateSamplerState(&sd,&gpu.sampler),"sampler");

    // 1. A noisy 1536 px picture and the same laid twice: the new band gives the old one's colour everywhere.
    std::vector<float> old(size_t(Half)*H*4),twice(size_t(W)*H*4);
    uint32_t seed=12345;
    for(size_t i=0;i<old.size();++i){seed=seed*1664525u+1013904223u;old[i]=i%4==3?1.f:float(seed>>8)/16777216.f;}
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<4;++c)
        twice[(size_t(y)*W+x)*4+c]=old[(size_t(y)*Half+x%Half)*4+c];
    const auto oldView=gpu.Texture(old,Half,H),twiceView=gpu.Texture(twice,W,H);
    std::vector<std::array<float,4>> rays;
    for(float timer:{0.f,1234567.f})for(float el:{-7.f,-1.f,0.f,12.5f,33.f,39.f})for(int i=0;i<1440;++i)
        rays.push_back(Ray(i*0.25f+0.013f,el,timer));
    const auto same=gpu.Run(rays,twiceView.Get(),oldView.Get());
    float worst=0;
    for(size_t i=0;i<rays.size();++i)for(unsigned c=0;c<3;++c)worst=std::max(worst,std::abs(same[i*2][c]-same[i*2+1][c]));
    // one 1/256 sub-texel step between two random texels at most
    if(worst>.004f)throw std::runtime_error("the twice-laid band differs from the old twice-wrapped band by "+std::to_string(worst));

    // 2. A landmark painted once in the 3072 px band is seen once round the circle, at its place.
    std::vector<float> mark(size_t(W)*H*4,0.f);
    const unsigned at=1000;
    for(unsigned y=0;y<H;++y)for(unsigned x=at;x<at+8;++x)for(unsigned c=0;c<4;++c)mark[(size_t(y)*W+x)*4+c]=1;
    const auto markView=gpu.Texture(mark,W,H);
    rays.clear();
    for(int i=0;i<3600;++i)rays.push_back(Ray(i*0.1f,10,0));
    const auto seen=gpu.Run(rays,markView.Get(),oldView.Get());
    unsigned landmarks=0,lit=0;float sum=0;
    for(size_t i=0;i<rays.size();++i) {
        const bool on=seen[i*2][0]>.5f,before=seen[((i+rays.size()-1)%rays.size())*2][0]>.5f;
        if(on&&!before)++landmarks;
        if(on){++lit;sum+=i*0.1f;}
    }
    const float where=lit?sum/lit:-1;   // its middle
    // u = heading / 360 (heading 0 = +z, the band's left edge), so column 1000 of 3072 is ~117 degrees round
    const float expected=(at+4)*360.f/W;
    if(landmarks!=1)throw std::runtime_error("a landmark painted once is seen "+std::to_string(landmarks)+" times round the circle");
    if(std::abs(where-expected)>.5f)throw std::runtime_error("the landmark is at "+std::to_string(where)+" degrees, not "+std::to_string(expected));

    // 3. The drift keeps its old speed: after 1,000,000 ms the band has turned 180 degrees (as the old u + 1 did).
    rays={Ray(where,10,0),Ray(where-180,10,1000000.f)};
    const auto drift=gpu.Run(rays,markView.Get(),oldView.Get());
    if(drift[0][0]<.5f||drift[2][0]<.5f)throw std::runtime_error("the drift does not keep its old speed");
    if(SUCCEEDED(co))CoUninitialize();
    std::printf("Sky band: 12 bands declared 3072x1024; %u shipped bands 3072x1024 with equal halves; %zu rays match the old "
                "twice-wrapped band (worst %.5f); a landmark seen once at %.1f degrees; drift speed kept (WARP; headset testing separate)\n",
                pictures,size_t(2*6*1440),worst,where);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
