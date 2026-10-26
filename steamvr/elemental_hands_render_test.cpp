// D3D12 WARP readback fixture for the procedural aura and the sprite looks,
// with a synthetic sprite cache. No game, headset or assets required.
// elemental-hands-render-test [preview.ppm [sprite cache folder]]
#include "elemental_hands_renderer.h"
#include "spell_sprites.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace framevr;
namespace {
constexpr unsigned W=480,H=360;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Check(HRESULT result,const char* message){Require(SUCCEEDED(result),message);}
// Full-strength masks, a warm flame ramp and cool lightning/ice/frost ramps.
std::filesystem::path WriteCache() {
    const auto folder=std::filesystem::temp_directory_path()/("elemental-hands-render-test-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    std::vector<uint8_t> d;
    auto put=[&](uint32_t v){d.insert(d.end(),reinterpret_cast<uint8_t*>(&v),reinterpret_cast<uint8_t*>(&v)+4);};
    put(0x31505353);put(1);put(elements::sprites::Count);
    for(unsigned i=0;i<elements::sprites::Count;++i) {
        const bool ramp=elements::sprites::Ramp(i);
        const uint32_t format=ramp?29:i==elements::sprites::FrozenOrb?77:80;
        put(i);put(8);put(8);put(format);put(1);
        put(uint32_t(elements::sprites::MipBytes(format,8,8)));
        for(unsigned texel=0;texel<(ramp?64u:4u);++texel) {
            if(ramp) {
                const bool warm=i==elements::sprites::FlameRamp;
                const uint8_t rgba[4]={uint8_t(warm?255:40),uint8_t(warm?96:120),uint8_t(warm?16:255),255};
                d.insert(d.end(),rgba,rgba+4);
            } else {
                const uint8_t block[8]={255,255,0,0,0,0,0,0},colour[8]={255,255,255,255,0,0,0,0};
                d.insert(d.end(),block,block+8);
                if(format==77)d.insert(d.end(),colour,colour+8);
            }
        }
    }
    std::ofstream(folder/"sprites.ssp",std::ios::binary).write(reinterpret_cast<const char*>(d.data()),std::streamsize(d.size()));
    return folder;
}
struct Pixels {
    std::vector<uint8_t> rgb=std::vector<uint8_t>(W*H*3);
    double energy=0,x=0,red=0,blue=0;
    unsigned count=0;
};
struct Shot {
    elements::Element element=elements::Element::Fire;
    int skill=-1;
    float time=1.3f,pulse=0,opacity=1,distance=.6f,background=0;
    bool typeless=false;
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
    Fixture(DXGI_FORMAT f,const std::filesystem::path& sprites):format(f),bgra(f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
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
        Require(renderer.Initialize(device.Get(),queue.Get(),format,W,H,+[](const char* text){std::puts(text);},sprites),"renderer initialize");
    }
    bool Sprites()const{return renderer.Sprites();}
    std::array<Pixels,2> Render(const Shot& shot) {
        Check(allocator->Reset(),"reset allocator");Check(list->Reset(allocator.Get(),nullptr),"reset list");
        D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=W;tex.Height=H;
        tex.DepthOrArraySize=tex.MipLevels=1;tex.SampleDesc.Count=1;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        tex.Format=shot.typeless?(bgra?DXGI_FORMAT_B8G8R8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_TYPELESS):format;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> targets[2],readback[2];
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&tex,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
        buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        elements::Scene scene;scene.time=shot.time;
        scene.hands[0]={{{0,-.04f,-shot.distance},math::Yaw(.4f)},shot.element,shot.opacity,shot.pulse,shot.skill};
        for(unsigned eye=0;eye<2;++eye) {
            heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[eye])),"target");
            auto handle=rtv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_RENDER_TARGET_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(targets[eye].Get(),&desc,handle);
            const float background[]={shot.background,shot.background,shot.background,1};list->ClearRenderTargetView(handle,background,0,nullptr);
            const interaction::Pose view{{eye==0?-.032f:.032f,0,0},{0,0,0,1}};
            Require(!renderer.Render(list.Get(),targets[eye].Get(),2,view,{-.65f,.65f,-.5f,.5f},scene),"invalid eye must not render");
            Require(!renderer.Render(list.Get(),targets[eye].Get(),eye,view,{0,0,-.5f,.5f},scene),"invalid projection must not render");
            Require(renderer.Render(list.Get(),targets[eye].Get(),eye,view,{-.65f,.65f,-.5f,.5f},scene)==(shot.opacity>0),"render visibility");
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
        const unsigned dark=unsigned(shot.background*255*3+.5f);
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
                if(energy>dark+12)++output.count;
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
// Shared checks for one effect: bounded, palette, cast, animation, typeless targets and stereo.
void Verify(Fixture& fixture,Shot shot,bool warm,float later) {
    const auto idle=fixture.Render(shot);
    auto bright=shot;bright.pulse=1;auto moved=shot;moved.time=later;auto typeless=shot;typeless.typeless=true;
    const auto cast=fixture.Render(bright),animated=fixture.Render(moved),views=fixture.Render(typeless);
    for(unsigned eye=0;eye<2;++eye) {
        Require(idle[eye].count>100&&idle[eye].count<W*H/5,"effect is visible and bounded");
        Require(warm?idle[eye].red>idle[eye].blue*2:idle[eye].blue>idle[eye].red*1.2,"element palette");
        Require(cast[eye].energy>idle[eye].energy*1.2,"confirmed cast visibly intensifies effect");
        Require(animated[eye].rgb!=idle[eye].rgb,"effect animates");
        Require(views[eye].rgb==idle[eye].rgb,"typed and typeless views render identically");
    }
    Require(idle[0].x>idle[1].x+10,"correct stereo disparity for hand position");
}
}
int main(int argc,char** argv) {try {
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    const auto synthetic=WriteCache();
    struct Cleanup {std::filesystem::path folder;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(folder,ignored);}} cleanup{synthetic};
    using elements::Element;
    const struct {Element element;int skill;bool warm;} looks[]={
        {Element::Fire,36,true},{Element::Fire,47,true},{Element::Lightning,38,false},{Element::Cold,39,false},{Element::Cold,45,false}};
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
        Fixture procedural(format,synthetic/"missing");
        Require(!procedural.Sprites(),"without a sprite cache hands keep the procedural aura");
        for(auto element:{Element::Fire,Element::Cold,Element::Lightning})Verify(procedural,{element},element==Element::Fire,1.7f);
        Fixture textured(format,synthetic);
        Require(textured.Sprites(),"the sprite cache loads");
        for(const auto& look:looks)Verify(textured,{look.element,look.skill},look.warm,2.1f);
        for(auto* fixture:{&procedural,&textured}) {
            const auto hidden=fixture->Render({Element::Fire,36,1.3f,0,0});
            Require(hidden[0].energy==0&&hidden[1].energy==0,"hidden scene leaves eye image untouched");
        }
    }
    if(argc>1) {
        // Each look at arm's length over a dark scene, both eyes.
        Fixture view(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,argc>2?std::filesystem::path(argv[2]):synthetic);
        std::vector<uint8_t> preview(2*W*3*H*std::size(looks));unsigned row=0;
        for(const auto& look:looks) {
            const auto image=view.Render({look.element,look.skill,1.3f,0,1,.35f,.04f});
            for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<3;++c)
                preview[((row*H+y)*2*W+eye*W+x)*3+c]=image[eye].rgb[(y*W+x)*3+c];
            ++row;
        }
        std::ofstream out(argv[1],std::ios::binary);out<<"P6\n"<<2*W<<" "<<std::size(looks)*H<<"\n255\n";
        out.write(reinterpret_cast<const char*>(preview.data()),std::streamsize(preview.size()));Require(bool(out),"preview write");
    }
    std::puts("Elemental hands GPU: procedural aura and five spell sprite looks; cast intensity, animation, stereo disparity, alpha preservation and four typed/typeless SDR formats passed");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
