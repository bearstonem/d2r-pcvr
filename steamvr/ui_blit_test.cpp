#include "ui_blit.h"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* what){if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",what,(unsigned long)hr);throw std::runtime_error(what);}}
static float Half(uint16_t x){const int e=(x>>10)&31,m=x&1023;return (x&0x8000?-1.f:1.f)*std::ldexp(e?1.f+m/1024.f:m/1024.f,e?e-15:-14);}
int main(int argc,char** argv){try {
    bool blackBackground=false,typeless=false,squareMap=false,portrait=false,undim=false;
    float mapZoom=1.f;
    for(int i=1;i<argc;++i) {if(!std::strcmp(argv[i],"--typeless"))typeless=true;
        else if(!std::strcmp(argv[i],"--square-map"))squareMap=true;
        else if(!std::strcmp(argv[i],"--zoom-out")){squareMap=true;mapZoom=.5f;}
        else if(!std::strcmp(argv[i],"--undim"))undim=true;
        else if(!std::strcmp(argv[i],"--portrait"))portrait=true;else blackBackground=true;}
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    framevr::UiBlit blit;if(!blit.Initialize(device.Get(),+[](const char* s){std::puts(s);}))return 1;
    D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=4;tex.Height=2;
    tex.DepthOrArraySize=tex.MipLevels=1;tex.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;tex.SampleDesc.Count=1;
    if(portrait){tex.Width=2;tex.Height=4;}
    const UINT sourceWidth=UINT(tex.Width),sourceHeight=tex.Height;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 sourceBytes=0;
    device->GetCopyableFootprints(&tex,0,1,0,&footprint,nullptr,nullptr,&sourceBytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> source,target,upload,readback;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)),"source");
    tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if(squareMap)tex.Width=tex.Height=32;
    if(typeless)tex.Format=DXGI_FORMAT_R16G16B16A16_TYPELESS;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)),"target");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outputFootprint{};UINT64 bytes=0;
    device->GetCopyableFootprints(&tex,0,1,0,&outputFootprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes>sourceBytes?bytes:sourceBytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type=D3D12_HEAP_TYPE_UPLOAD;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    hp.Type=D3D12_HEAP_TYPE_READBACK;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
    // Native SDR colors: empty, opaque text, 50% border, 75% icon.
    // At 50% coverage a .5 sRGB border must give .10702 linear, not .25
    // (washed out) or .050876 (decoding premultiplied color directly).
    uint16_t pixels[8][4]={{0,0,0,0x3c00},{0x3c00,0x3800,0x3400,0},{0x3400,0x3000,0,0x3800},{0x3600,0,0x3200,0x3400},
        {0,0,0,0x4000},{0x3800,0,0,0xbc00},{0x3c00,0x3c00,0x3c00,0},{0,0,0,0}};
    // Out-of-range alpha probes test the unfiltered menu path. Real map
    // pixels have valid transmittance before hardware filtering.
    if(squareMap){pixels[4][3]=0x3c00;pixels[5][3]=0;}
    float expected[8][4]={{0,0,0,0},{1,.21404114f,.05087609f,1},{.10702057f,.02543804f,0,.5f},{.16053085f,0,.03815707f,.75f},
        {0,0,0,0},{.21404114f,0,0,1},{1,1,1,1},{0,0,0,1}};
    // --undim (the leather): a menu over the game's 50% black backdrop, which covers the whole
    // 4x2 band. Its corners and side middles are the backdrop alone and come out empty; the
    // text, the 50% border and the 75% icon drawn over it convert as they do without it
    // (above), and a panel's faint black drop shadow over it (30%) comes out empty too. The
    // top-right corner is clear (the game's dim does not reach every corner): it must not
    // count as the backdrop, and stays empty.
    const uint16_t dimmed[8][4]={{0,0,0,0x3800},{0x3c00,0x3800,0x3400,0},{0x3400,0x3000,0,0x3400},{0,0,0,0x3c00},
        {0,0,0,0x3800},{0x3600,0,0x3200,0x3000},{0,0,0,0x359a},{0,0,0,0x3800}};
    const float undimmed[8][4]={{0,0,0,0},{1,.21404114f,.05087609f,1},{.10702057f,.02543804f,0,.5f},{0,0,0,0},
        {0,0,0,0},{.16053085f,0,.03815707f,.75f},{0,0,0,0},{0,0,0,0}};
    if(undim){std::memcpy(pixels,dimmed,sizeof pixels);std::memcpy(expected,undimmed,sizeof expected);}
    const int32_t band[4]={0,0,int32_t(sourceWidth),int32_t(sourceHeight)};
    void* data=nullptr;D3D12_RANGE empty{0,0};Check(upload->Map(0,&empty,&data),"map upload");
    for(UINT y=0;y<sourceHeight;++y)std::memcpy(static_cast<char*>(data)+y*footprint.Footprint.RowPitch,pixels[y*sourceWidth],sourceWidth*8);
    upload->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC q{};ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"commands");
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=source.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    commands->ResourceBarrier(1,&barrier);
    if(!blit.Render(device.Get(),commands.Get(),source.Get(),target.Get(),blackBackground,squareMap,0,0,1.f,mapZoom,0.f,0.f,0.f,0.f,undim?band:nullptr))throw std::runtime_error("render");
    barrier.Transition={target.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
    from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;
    to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=outputFootprint;commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Check(commands->Close(),"close");ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");Check(queue->Signal(fence.Get(),1),"signal");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(1,event),"completion");
    const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
    D3D12_RANGE range{0,SIZE_T(bytes)};Check(readback->Map(0,&range,&data),"map readback");
    bool correct=true;for(UINT y=0;y<tex.Height;++y)for(UINT x=0;x<tex.Width;++x){
        auto* out=reinterpret_cast<uint16_t*>(static_cast<char*>(data)+y*outputFootprint.Footprint.RowPitch)+x*4;
        float want[4]{};
        if(!squareMap)std::memcpy(want,expected[y*sourceWidth+x],sizeof want);
        else {
            const float edge=std::min({x+.5f,y+.5f,31.5f-x,31.5f-y});
            const float cx=std::min(x+.5f,31.5f-x)*4,cy=std::min(y+.5f,31.5f-y)*4;
            if(edge*4<1){want[0]=.012f;want[1]=.009f;want[2]=.006f;}
            else if(edge*4<3){want[0]=.24f;want[1]=.16f;want[2]=.065f;}
            else if(edge*4<5){want[0]=.055f;want[1]=.033f;want[2]=.015f;}
            else if(edge*4<7){want[0]=.012f;want[1]=.009f;want[2]=.006f;}
            else if(std::max(cx,cy)<20&&std::abs(cx+cy-26)<1.4f){want[0]=.34f;want[1]=.24f;want[2]=.11f;}
            else {
                // A 4:2 source occupies 24x12 pixels (or 12x24 when tall).
                const float scale=6*mapZoom,left=16-sourceWidth*scale/2,top=16-sourceHeight*scale/2;
                if(x+.5f>=left&&y+.5f>=top&&x+.5f<left+sourceWidth*scale&&y+.5f<top+sourceHeight*scale) {
                    const float sx=(x+.5f-left)/scale-.5f,sy=(y+.5f-top)/scale-.5f;
                    const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
                    const float fx=sx-ix,fy=sy-iy;
                    float native[4]{};
                    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                        const int index=std::clamp(iy+dy,0,int(sourceHeight)-1)*sourceWidth+std::clamp(ix+dx,0,int(sourceWidth)-1);
                        for(int c=0;c<4;++c)native[c]+=Half(pixels[index][c])*(dx?fx:1-fx)*(dy?fy:1-fy);
                    }
                    const float opacity=1-std::clamp(native[3],0.f,1.f);
                    for(int c=0;c<3;++c) {
                        const float encoded=std::max(native[c],0.f)/std::max(opacity,1e-6f);
                        const float linear=encoded<=.04045f?encoded/12.92f:std::pow((encoded+.055f)/1.055f,2.4f);
                        want[c]=(opacity>0?linear*opacity:0)+.006f*(1-opacity);
                    }
                } else want[0]=want[1]=want[2]=.006f;
            }
            want[3]=1;
        }
        if(blackBackground)want[3]=1;
        // Hardware texture filtering rounds weights and half-float samples.
        // Allow two 8-bit color steps after gamma conversion for filtered RGB;
        // alpha, framing and the unfiltered menu retain the strict tolerance.
        for(int c=0;c<4;++c)if(std::abs(Half(out[c])-want[c])>(squareMap&&c<3?2.f/255:.0006f)){correct=false;std::printf("pixel %u,%u lane %d = %f expected %f\n",x,y,c,Half(out[c]),want[c]);}}
    readback->Unmap(0,&empty);if(!correct)throw std::runtime_error("converted pixels differ");
    std::puts(undim?"Native UI GPU conversion passed: the menu's backdrop taken out, what is drawn over it unchanged":
              blackBackground?"Native UI GPU conversion passed: opaque black backing, unchanged artwork colors":"Native UI GPU conversion passed: orientation, SDR color, translucent edges, inverse alpha and clamping");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
