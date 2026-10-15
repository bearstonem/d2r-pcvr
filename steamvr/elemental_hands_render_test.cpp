// Procedural D3D12 WARP readback fixture. No game, headset or assets required.
#include "elemental_hands_renderer.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace framevr;
namespace {
constexpr unsigned W=480,H=360;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Check(HRESULT result,const char* message){Require(SUCCEEDED(result),message);}
struct Pixels {
    std::vector<uint8_t> rgb=std::vector<uint8_t>(W*H*3);
    double energy=0,x=0,red=0,blue=0;
    unsigned count=0;
};
class Fixture {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12DescriptorHeap> rtv;
    ComPtr<ID3D12InfoQueue> messages;
    elements::Renderer renderer;
    DXGI_FORMAT format;
    bool bgra;
    uint64_t serial=0;
public:
    explicit Fixture(DXGI_FORMAT f):format(f),bgra(f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        ComPtr<IDXGIAdapter> warp;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP adapter");
        Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12 device");
        device.As(&messages);
        D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        Check(list->Close(),"initial close");Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=2;
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtv)),"RTVs");
        Require(renderer.Initialize(device.Get(),format,W,H,+[](const char* text){std::puts(text);}),"renderer initialize");
    }
    std::array<Pixels,2> Render(elements::Element element,float time=1.3f,float pulse=0,float opacity=1,bool typeless=false) {
        Check(allocator->Reset(),"reset allocator");Check(list->Reset(allocator.Get(),nullptr),"reset list");
        D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=W;tex.Height=H;
        tex.DepthOrArraySize=tex.MipLevels=1;tex.SampleDesc.Count=1;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        tex.Format=typeless?(bgra?DXGI_FORMAT_B8G8R8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_TYPELESS):format;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> targets[2],readback[2];
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&tex,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
        buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        elements::Scene scene;scene.time=time;
        scene.hands[0]={{{0,0,-.6f},math::Yaw(.4f)},element,opacity,pulse};
        for(unsigned eye=0;eye<2;++eye) {
            heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[eye])),"target");
            auto handle=rtv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_RENDER_TARGET_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(targets[eye].Get(),&desc,handle);
            const float background[]={0,0,0,1};list->ClearRenderTargetView(handle,background,0,nullptr);
            const interaction::Pose view{{eye==0?-.032f:.032f,0,0},{0,0,0,1}};
            Require(!renderer.Render(list.Get(),targets[eye].Get(),2,view,{-.65f,.65f,-.5f,.5f},scene),"invalid eye must not render");
            Require(!renderer.Render(list.Get(),targets[eye].Get(),eye,view,{0,0,-.5f,.5f},scene),"invalid projection must not render");
            Require(renderer.Render(list.Get(),targets[eye].Get(),eye,view,{-.65f,.65f,-.5f,.5f},scene)==(opacity>0),"render visibility");
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={targets[eye].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
            list->ResourceBarrier(1,&barrier);heap.Type=D3D12_HEAP_TYPE_READBACK;
            Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback[eye])),"readback");
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=targets[eye].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=readback[eye].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
            list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        }
        Check(list->Close(),"close");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        Check(queue->Signal(fence.Get(),++serial),"signal");
        const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"fence event");
        const HRESULT completion=fence->SetEventOnCompletion(serial,event);
        const DWORD wait=SUCCEEDED(completion)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);
        Require(wait==WAIT_OBJECT_0,"GPU completion");
        std::array<Pixels,2> result;
        for(unsigned eye=0;eye<2;++eye) {
            void* data=nullptr;D3D12_RANGE range{0,size_t(bytes)};Check(readback[eye]->Map(0,&range,&data),"map");
            auto& output=result[eye];
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
                const auto* pixel=static_cast<const uint8_t*>(data)+y*footprint.Footprint.RowPitch+x*4;
                Require(pixel[3]==255,"aura preserves target alpha");
                const uint8_t r=pixel[bgra?2:0],g=pixel[1],b=pixel[bgra?0:2];
                const unsigned offset=(y*W+x)*3;output.rgb[offset]=r;output.rgb[offset+1]=g;output.rgb[offset+2]=b;
                const unsigned energy=unsigned(r)+g+b;
                output.energy+=energy;output.x+=energy*x;output.red+=r;output.blue+=b;
                if(energy>12)++output.count;
            }
            D3D12_RANGE none{0,0};readback[eye]->Unmap(0,&none);
            if(output.energy)output.x/=output.energy;
        }
        if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T size=0;messages->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(messages->GetMessage(i,message,&size),"debug message");
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");}
        }
        if(messages)messages->ClearStoredMessages();
        return result;
    }
};
}
int main(int argc,char** argv) {try {
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    std::vector<uint8_t> preview(2*W*3*H*3);
    unsigned formatIndex=0;
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
        Fixture fixture(format);unsigned row=0;
        for(auto element:{elements::Element::Fire,elements::Element::Cold,elements::Element::Lightning}) {
            const auto idle=fixture.Render(element),bright=fixture.Render(element,1.3f,1),moved=fixture.Render(element,1.7f);
            const auto typeless=fixture.Render(element,1.3f,0,1,true);
            for(unsigned eye=0;eye<2;++eye) {
                Require(idle[eye].count>100&&idle[eye].count<W*H/5,"effect is visible and bounded");
                Require(element==elements::Element::Fire?idle[eye].red>idle[eye].blue*2:idle[eye].blue>idle[eye].red*1.2,"element palette");
                Require(bright[eye].energy>idle[eye].energy*1.2,"confirmed cast visibly intensifies effect");
                Require(moved[eye].rgb!=idle[eye].rgb,"procedural effect animates");
                Require(typeless[eye].rgb==idle[eye].rgb,"typed and typeless views render identically");
                if(!formatIndex)for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<3;++c)
                    preview[((row*H+y)*2*W+eye*W+x)*3+c]=idle[eye].rgb[(y*W+x)*3+c];
            }
            Require(idle[0].x>idle[1].x+10,"correct stereo disparity for hand position");++row;
        }
        const auto hidden=fixture.Render(elements::Element::Fire,1.3f,0,0);
        Require(hidden[0].energy==0&&hidden[1].energy==0,"hidden scene leaves eye image untouched");++formatIndex;
    }
    if(argc>1){std::ofstream out(argv[1],std::ios::binary);out<<"P6\n"<<2*W<<" "<<3*H<<"\n255\n";
        out.write(reinterpret_cast<const char*>(preview.data()),preview.size());Require(bool(out),"preview write");}
    std::puts("Elemental hands GPU: three animated effects, cast intensity, stereo disparity, alpha preservation and four typed/typeless SDR formats passed");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
