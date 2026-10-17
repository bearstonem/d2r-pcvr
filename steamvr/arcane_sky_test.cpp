// Compile and execute the production sky functions against synthetic textures.
// Covers all directions and face/pole seams without game assets or a headset.
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
int main(int argc,char** argv) {try {
    if(argc!=2)throw std::runtime_error("shader path required");
    std::ifstream inputFile(argv[1]);const std::string source{std::istreambuf_iterator<char>(inputFile),{}};
    std::string shader=R"hlsl(
Texture2D<float4> ArcaneStarsSmp : register(t0);
Texture2D<float4> ArcaneCloudsSmp : register(t1);
StructuredBuffer<float4> directions : register(t2);
SamplerState linearWrap : register(s0);
RWStructuredBuffer<float4> output : register(u0);
#define tex2Dlod(s, uv) s.SampleLevel(linearWrap, (uv).xy, (uv).w)
)hlsl";
    for(const char* fn:{"float3 ArcaneStars(","float ArcaneClouds(","float3 ArcaneSky("})shader+=Function(source,fn);
    shader+="[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) { output[id.x]=float4(ArcaneSky(normalize(directions[id.x].xyz)),1); }";
    ComPtr<ID3DBlob> blob,errors;
    auto hr=D3DCompile(shader.data(),shader.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile production Arcane shader");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"WARP device");
    ComPtr<ID3D11ComputeShader> compute;
    Check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&compute),"compute shader");
    std::array<std::array<float,4>,64*64> pixels{};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        const float v=.5f+.2f*std::sin(float(x)*6.2831853f/64)*std::cos(float(y)*6.2831853f/64);
        pixels[y*64+x]={v,v*.7f,v*.4f,1};
    }
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA texData{pixels.data(),64*16,0};ComPtr<ID3D11Texture2D> texture;
    Check(device->CreateTexture2D(&td,&texData,&texture),"fixture texture");
    ComPtr<ID3D11ShaderResourceView> texView;
    Check(device->CreateShaderResourceView(texture.Get(),nullptr,&texView),"texture view");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;Check(device->CreateSamplerState(&sd,&sampler),"sampler");
    std::vector<std::array<float,4>> directions;
    for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        std::array<float,4> dir{};dir[axis]=sign;directions.push_back(dir);
    }
    // Small moves across axis/pole and triplanar face boundaries must not
    // switch to black, a different card, or an unrelated side of the texture.
    for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f})for(float edge:{0.f,1.f,-1.f}) {
        std::array<float,4> a{},b{};a[axis]=b[axis]=sign;
        a[(axis+1)%3]=edge-.00001f;b[(axis+1)%3]=edge+.00001f;
        directions.push_back(a);directions.push_back(b);
    }
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(directions.size()*16);bd.Usage=D3D11_USAGE_IMMUTABLE;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
    D3D11_SUBRESOURCE_DATA init{directions.data(),0,0};ComPtr<ID3D11Buffer> input,output,readback;
    Check(device->CreateBuffer(&bd,&init,&input),"directions");
    ComPtr<ID3D11ShaderResourceView> inputView;Check(device->CreateShaderResourceView(input.Get(),nullptr,&inputView),"direction view");
    bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Check(device->CreateBuffer(&bd,nullptr,&output),"output");
    ComPtr<ID3D11UnorderedAccessView> outView;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&outView),"output view");
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(device->CreateBuffer(&bd,nullptr,&readback),"readback");
    ID3D11ShaderResourceView* views[]={texView.Get(),texView.Get(),inputView.Get()};auto* rawSampler=sampler.Get();auto* rawOut=outView.Get();
    context->CSSetShader(compute.Get(),nullptr,0);context->CSSetShaderResources(0,3,views);
    context->CSSetSamplers(0,1,&rawSampler);context->CSSetUnorderedAccessViews(0,1,&rawOut,nullptr);
    context->Dispatch(UINT(directions.size()),1,1);context->CopyResource(readback.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map results");
    const auto* values=static_cast<const float*>(mapped.pData);
    for(size_t i=0;i<directions.size();++i)for(unsigned c=0;c<3;++c) {
        const float value=values[i*4+c];
        if(!std::isfinite(value)||value<.02f||value>2)throw std::runtime_error("missing/nonfinite sky direction");
        if(i>=6&&(i-6)%2==1&&std::abs(value-values[(i-1)*4+c])>.002f)
            throw std::runtime_error("sky seam at face or pole");
    }
    context->Unmap(readback.Get(),0);context->ClearState();
    std::printf("Arcane sky: %zu directions, six axes and 18 seam crossings passed (WARP; headset testing separate)\n",directions.size());
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
