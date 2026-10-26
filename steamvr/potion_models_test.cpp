// Asset-backed D3D12 readback test. No game or saves touched.
#include "potion_models.h"
#include <wrl/client.h>
#include <fstream>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
using namespace framevr;
static void Check(HRESULT h,const char* s){if(FAILED(h)){std::fprintf(stderr,"%s %08lx\n",s,(unsigned long)h);throw std::runtime_error(s);}}
int main(int argc,char** argv){try {
    if(argc<3)return 2;
    bool typeless=false,srgb=false,toss=false;   // --toss: the thrown trap models in the middle row (#224)
    for(int i=3;i<argc;++i){typeless|=!std::strcmp(argv[i],"--typeless");srgb|=!std::strcmp(argv[i],"--srgb");toss|=!std::strcmp(argv[i],"--toss");}
    const auto format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr unsigned W=960,H=720;
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
    potions::Models models;if(!models.Initialize(device.Get(),queue.Get(),format,W,H,+[](const char* s){std::puts(s);},argv[1]))return 1;
    if(toss&&models.Tosses()!=5)throw std::runtime_error("thrown trap models missing");
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=W;tex.Height=H;tex.DepthOrArraySize=tex.MipLevels=1;tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    tex.Format=typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:format;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> targets[2],readback[2];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;device->GetCopyableFootprints(&tex,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_DESCRIPTOR_HEAP_DESC rh{};rh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rh.NumDescriptors=2;ComPtr<ID3D12DescriptorHeap> rtv;Check(device->CreateDescriptorHeap(&rh,IID_PPV_ARGS(&rtv)),"RTV");
    const auto stride=device->GetDescriptorHandleIncrementSize(rh.Type);
    const char* codes[]={"hp1","hp2","hp3","hp4","hp5","mp1","mp2","mp3","mp4","mp5","rvs","rvl","yps","wms","vps"};
    const char* tossCodes[]={"tfb","tsw","tbs","tst","twk"};
    for(unsigned eye=0;eye<2;++eye) {
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[eye])),"target");
        auto handle=rtv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*stride;
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};viewDesc.Format=format;viewDesc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(targets[eye].Get(),&viewDesc,handle);
        float background[]={.018f,.022f,.027f,1};list->ClearRenderTargetView(handle,background,0,nullptr);
        const interaction::Pose view{{eye==0?-.032f:.032f,0,0},{0,0,0,1}};
        for(unsigned row=0;row<3;++row) {
            potions::Scene scene;scene.count=5;
            for(unsigned col=0;col<5;++col) {
                auto& item=scene.items[col];const char* code=toss&&row==1?tossCodes[col]:codes[row*5+col];item.code=interaction::Code(code[0],code[1],code[2]);
                item.pose={{(float(col)-2)*.19f,(1-float(row))*.255f,-.85f},math::Yaw(.55f)};
                item.highlight=col==0;
            }
            // Under the potions, the cached arrow pointing right (along its +Y).
            if(row==2&&models.HasArrow())potions::AddArrow(scene,{{0,-.47f,-.85f},math::BeamOrientation({1,0,0},{0,0,1})});
            if(!models.Render(list.Get(),targets[eye].Get(),eye,view,{-.65f,.65f,-.53f,.53f},scene))throw std::runtime_error("render");
        }
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={targets[eye].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type=D3D12_HEAP_TYPE_READBACK;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback[eye])),"readback");
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=targets[eye].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=readback[eye].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
    Check(list->Close(),"close");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");Check(queue->Signal(fence.Get(),1),"signal");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(1,event),"completion");const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
    std::vector<uint8_t> rgb(2*W*H*3);double sx[2]{},count[2]{};
    for(unsigned eye=0;eye<2;++eye) {
        void* data=nullptr;D3D12_RANGE range{0,size_t(bytes)};Check(readback[eye]->Map(0,&range,&data),"map");
        const auto* background=static_cast<uint8_t*>(data);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
            auto* p=static_cast<uint8_t*>(data)+y*footprint.Footprint.RowPitch+x*4;
            for(int c=0;c<3;++c)rgb[((y*2*W)+eye*W+x)*3+c]=p[c];
            if(int(p[0])>background[0]+15||int(p[1])>background[1]+15||int(p[2])>background[2]+15){++count[eye];sx[eye]+=x;}
        }
        D3D12_RANGE none{0,0};readback[eye]->Unmap(0,&none);
        if(count[eye]<10000)throw std::runtime_error("missing potion pixels");
    }
    // Near objects move right in the left eye, left in the right eye.
    if(sx[0]/count[0]-sx[1]/count[1]<15)throw std::runtime_error("incorrect stereo disparity");
    std::ofstream out(argv[2],std::ios::binary);out<<"P6\n"<<2*W<<" "<<H<<"\n255\n";out.write(reinterpret_cast<char*>(rgb.data()),rgb.size());
    if(!out)throw std::runtime_error("preview output failed");
    std::puts("Original potion GPU test passed: textured geometry in both eyes, correct disparity and readback");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
