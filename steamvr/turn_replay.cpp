// Development tool: one frame of a By turns capture (capture_pair; Frame-turns-N-F)
// through TurnWarp again, offline, to see which pass makes what: it found the
// holes the four-texel sort left along a roof's ridge (2026-10-25). The
// capture's files go in as raw ones (tools/turn_replay.py writes them and runs
// this): params.txt (the sidecar's warp), drawn.rgba and own.rgba (sRGB bytes),
// drawn.depth and own.depth (float32, reverse-Z), W x H. Writes out.rgba beside them.
//   turn-replay <folder> <rims 0|1> <fill 0|1> <sharp 0|1> [out name]
#include "turn_warp.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

static void Check(HRESULT hr,const char* what){if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",what,(unsigned long)hr);throw std::runtime_error(what);}}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;uint64_t value=0;
    Gpu() {
        Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
        D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        Check(list->Close(),"close");Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    }
    void Record(const std::function<void()>& work) {
        Check(allocator->Reset(),"reset allocator");Check(list->Reset(allocator.Get(),nullptr),"reset list");
        work();Check(list->Close(),"close");
        ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        Check(queue->Signal(fence.Get(),++value),"signal");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(value,event),"completion");
        const auto waited=WaitForSingleObject(event,30000);CloseHandle(event);
        if(waited!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
    }
    ComPtr<ID3D12Resource> Texture(UINT w,UINT h,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=w;tex.Height=h;
        tex.DepthOrArraySize=tex.MipLevels=1;tex.Format=format;tex.SampleDesc.Count=1;tex.Flags=flags;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> r;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,state,nullptr,IID_PPV_ARGS(&r)),"texture");
        return r;
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=1;
        b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{};hp.Type=type;ComPtr<ID3D12Resource> r;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,state,nullptr,IID_PPV_ARGS(&r)),"buffer");
        return r;
    }
    void Upload(ID3D12Resource* texture,const std::vector<uint32_t>& texels,D3D12_RESOURCE_STATES after) {
        const auto desc=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&f,nullptr,nullptr,&bytes);
        auto upload=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data=nullptr;D3D12_RANGE none{0,0};Check(upload->Map(0,&none,&data),"map upload");
        for(UINT y=0;y<desc.Height;++y)std::memcpy(static_cast<char*>(data)+y*f.Footprint.RowPitch,&texels[size_t(y)*desc.Width],desc.Width*4);
        upload->Unmap(0,nullptr);
        Record([&]{
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=f;
            to.pResource=texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,after};list->ResourceBarrier(1,&b);
        });
    }
    std::vector<uint32_t> Drawn(ID3D12Resource* target,const std::function<void()>& draw) {
        const auto desc=target->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&f,nullptr,nullptr,&bytes);
        auto readback=Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        Record([&]{
            draw();
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=f;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
        });
        std::vector<uint32_t> out(size_t(desc.Width)*desc.Height);void* data=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)},none{0,0};
        Check(readback->Map(0,&range,&data),"map readback");
        for(UINT y=0;y<desc.Height;++y)std::memcpy(&out[size_t(y)*desc.Width],static_cast<char*>(data)+y*f.Footprint.RowPitch,desc.Width*4);
        readback->Unmap(0,&none);return out;
    }
};
static std::vector<uint32_t> Raw(const std::string& path,size_t count) {
    std::ifstream in(path,std::ios::binary);std::vector<uint32_t> v(count);
    if(!in.read(reinterpret_cast<char*>(v.data()),std::streamsize(count*4)))throw std::runtime_error("short file: "+path);
    return v;
}
int main(int argc,char** argv) {try {
    if(argc<5){std::fprintf(stderr,"turn-replay <folder> <rims 0|1> <fill 0|1> <sharp 0|1> [out name]\n");return 2;}
    const std::string dir=argv[1];const std::string outName=argc>5?argv[5]:"out.rgba";
    std::map<std::string,std::vector<float>> p;
    {std::ifstream in(dir+"/params.txt");std::string line;
     while(std::getline(in,line)){std::istringstream s(line);std::string key;s>>key;float v;while(s>>v)p[key].push_back(v);}}
    const auto need=[&](const char* key,size_t n)->const std::vector<float>&{auto& v=p[key];if(v.size()<n)throw std::runtime_error(std::string("params: ")+key);return v;};
    const UINT w=UINT(need("size",2)[0]),h=UINT(need("size",2)[1]);
    const auto fov=[&](const char* key){const auto& v=need(key,4);return framevr::math::Fov{v[0],v[1],v[2],v[3]};};
    const auto twelve=[&](const char* key){std::array<float,12> a{};const auto& v=need(key,12);for(int k=0;k<12;++k)a[k]=v[k];return a;};
    Gpu gpu;framevr::TurnWarp warp;
    if(!warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,+[](const char* s){std::puts(s);})||!warp.DepthReady())throw std::runtime_error("warp");
    warp.Rims(argv[2][0]=='1');warp.Fill(argv[3][0]=='1');warp.Sharpen(argv[4][0]=='1');
    const size_t n=size_t(w)*h;
    const auto image=[&](const char* name){auto t=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(t.Get(),Raw(dir+"/"+name,n),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);return t;};
    const auto depthTexture=[&](const char* name){auto t=gpu.Texture(w,h,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(t.Get(),Raw(dir+"/"+name,n),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);return t;};
    auto drawnImage=image("drawn.rgba"),ownImage=image("own.rgba");auto drawnDepth=depthTexture("drawn.depth"),ownDepth=depthTexture("own.depth");
    auto target=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    framevr::TurnDepth own,drawn;
    own.depth=ownDepth.Get();own.format=DXGI_FORMAT_R32_FLOAT;own.nearPlane=need("own.near",1)[0];
    own.move=twelve("own.move");own.carry=twelve("own.carry");own.reach[0]=need("own.reach",2)[0];own.reach[1]=need("own.reach",2)[1];
    own.drawnFov=fov("own.drawnFov");own.nowFov=fov("own.nowFov");
    drawn.depth=drawnDepth.Get();drawn.format=DXGI_FORMAT_R32_FLOAT;drawn.image=drawnImage.Get();drawn.nearPlane=need("drawn.near",1)[0];
    drawn.move=twelve("drawn.move");drawn.drawnFov=fov("drawn.drawnFov");drawn.nowFov=fov("drawn.nowFov");drawn.apart=need("drawn.apart",1)[0];
    const auto& keep=need("drawn.keep",4);drawn.keep=keep[0];drawn.keepNear=keep[1];drawn.keepReach=keep[2];drawn.keepEffects=keep[3];
    std::array<float,9> turn{};const auto& hv=need("homography",9);for(int k=0;k<9;++k)turn[k]=hv[k];
    const auto out=gpu.Drawn(target.Get(),[&]{if(!warp.Render(gpu.device.Get(),gpu.list.Get(),ownImage.Get(),target.Get(),turn,1,&own,&drawn))throw std::runtime_error("render");});
    std::ofstream(dir+"/"+outName,std::ios::binary).write(reinterpret_cast<const char*>(out.data()),std::streamsize(out.size()*4));
    std::printf("replayed %ux%u rims %c fill %c sharp %c -> %s\n",w,h,argv[2][0],argv[3][0],argv[4][0],(dir+"/"+outName).c_str());
    return 0;
} catch(const std::exception& e){std::fprintf(stderr,"turn-replay: %s\n",e.what());return 1;}}
