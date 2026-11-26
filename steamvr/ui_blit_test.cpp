#include "ui_blit.h"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* what){if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",what,(unsigned long)hr);throw std::runtime_error(what);}}
static float Half(uint16_t x){const int e=(x>>10)&31,m=x&1023;return (x&0x8000?-1.f:1.f)*std::ldexp(e?1.f+m/1024.f:m/1024.f,e?e-15:-14);}
int main(int argc,char** argv){try {
    bool ground=false;bool blackBackground=false,typeless=false,squareMap=false,portrait=false,undim=false,seeThrough=false;
    float back=1;   // --see-through <percent>: the floating map's backing, 0 see-through to 100 solid
    float frameShown=1;   // --frame <percent>: the floating map's frame, 0 hidden (not pointed at) to 100 shown (pointed at)
    for(int i=1;i<argc;++i) {if(!std::strcmp(argv[i],"--typeless"))typeless=true;
        else if(!std::strcmp(argv[i],"--square-map"))squareMap=true;
        else if(!std::strcmp(argv[i],"--see-through")&&i+1<argc){squareMap=seeThrough=true;back=float(std::atof(argv[++i]))/100;}
        else if(!std::strcmp(argv[i],"--ground-map")){ground=true;squareMap=seeThrough=true;}
        else if(!std::strcmp(argv[i],"--frame")&&i+1<argc)frameShown=float(std::atof(argv[++i]))/100;
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
    // --see-through: the map's surface as transport's (the solid square at the top, the floating copy's own square
    // SeeThroughTop rows down, 8 between them as panelatlas::Gap), at 32 wide; that square drawn only when it differs
    constexpr UINT SeeThroughTop=40;
    const bool copyDrawn=seeThrough&&(back<1||frameShown<1);
    if(seeThrough)tex.Height=(ground?2:1)*SeeThroughTop+32;
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
    if(!blit.Render(device.Get(),commands.Get(),source.Get(),target.Get(),blackBackground,squareMap,0,0,1.f,1.f,0.f,0.f,0.f,0.f,undim?band:nullptr,
                    seeThrough?SeeThroughTop:0,back,frameShown,ground?2*SeeThroughTop:0,0))throw std::runtime_error("render");
    barrier.Transition={target.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
    from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;
    to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=outputFootprint;commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    // --see-through: the wrist's square as the slider at 100% (upstream's one solid square) draws it, by a blit of its own,
    // to compare bit for bit with the top square drawn beside the see-through copy
    framevr::UiBlit wristBlit;ComPtr<ID3D12Resource> wristTarget,wristReadback;
    if(seeThrough) {
        if(!wristBlit.Initialize(device.Get(),nullptr))throw std::runtime_error("wrist blit");
        hp.Type=D3D12_HEAP_TYPE_DEFAULT;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&wristTarget)),"wrist target");
        hp.Type=D3D12_HEAP_TYPE_READBACK;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&wristReadback)),"wrist readback");
        if(!wristBlit.Render(device.Get(),commands.Get(),source.Get(),wristTarget.Get(),false,true))throw std::runtime_error("wrist render");
        barrier.Transition={wristTarget.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        from.pResource=wristTarget.Get();to.pResource=wristReadback.Get();commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
    Check(commands->Close(),"close");ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");Check(queue->Signal(fence.Get(),1),"signal");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(1,event),"completion");
    const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
    D3D12_RANGE range{0,SIZE_T(bytes)};Check(readback->Map(0,&range,&data),"map readback");
    // The square map's pixel x on its own row y, its backing at `backing` (1: solid) and its frame at `frame` (1 shown,
    // 0 hidden), unassociated; filtered: a sample of the source (else frame or margin); fore: what the game drew lit
    // there, its coverage; inFrame: on the frame (the bevel or a corner cut).
    auto squarePixel=[&](UINT x,int y,float backing,float frame,float want[4],bool& filtered,float& fore,bool& inFrame,float nudgeX=0,float nudgeY=0) {
        filtered=false;fore=0;inFrame=true;
        auto set=[&](float r,float g,float b,float a){want[0]=r;want[1]=g;want[2]=b;want[3]=a;};
        const float edge=std::min({x+.5f,y+.5f,31.5f-x,31.5f-y});
        const float cx=std::min(x+.5f,31.5f-x)*4,cy=std::min(y+.5f,31.5f-y)*4;
        // the frame: the gold line and corner cuts solid, its dark bands the backing
        float border[4]{};
        auto setBorder=[&](float r,float g,float b,float a){border[0]=r;border[1]=g;border[2]=b;border[3]=a;};
        if(edge*4<1)setBorder(.012f,.009f,.006f,backing);
        else if(edge*4<3)setBorder(.24f,.16f,.065f,1);
        else if(edge*4<5)setBorder(.055f,.033f,.015f,backing);
        else if(edge*4<7)setBorder(.012f,.009f,.006f,backing);
        else if(std::max(cx,cy)<20&&std::abs(cx+cy-26)<1.4f)setBorder(.34f,.24f,.11f,1);
        else inFrame=false;
        if(inFrame&&frame>=1){set(border[0],border[1],border[2],border[3]);return;}
        {   // the map (beneath a frame below 1)
            // A 4:2 source occupies 24x12 pixels (or 12x24 when tall).
            const int left=portrait?10:4,top=portrait?4:10;
            if(int(x)>=left&&y>=top&&x<left+sourceWidth*6&&y<int(top+sourceHeight*6)) {
                filtered=true;
                const float sx=(x+.5f-left)/6-.5f,sy=(y+.5f-top)/6-.5f;
                const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
                const float fx=std::clamp(sx-ix+nudgeX,0.f,1.f),fy=std::clamp(sy-iy+nudgeY,0.f,1.f);   // nudge: a filter weight step (below)
                float native[4]{};
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                    const int index=std::clamp(iy+dy,0,int(sourceHeight)-1)*sourceWidth+std::clamp(ix+dx,0,int(sourceWidth)-1);
                    for(int c=0;c<4;++c)native[c]+=Half(pixels[index][c])*(dx?fx:1-fx)*(dy?fy:1-fy);
                }
                const float opacity=1-std::clamp(native[3],0.f,1.f);
                float linear[3],brightest=0;
                for(int c=0;c<3;++c) {
                    const float encoded=std::max(native[c],0.f)/std::max(opacity,1e-6f);
                    brightest=std::max(brightest,encoded);
                    linear[c]=encoded<=.04045f?encoded/12.92f:std::pow((encoded+.055f)/1.055f,2.4f);
                }
                // lit: the shader's smoothstep(.05, .14) of the brightest channel; near black is backing too
                const float t=std::clamp((brightest-.05f)/(.14f-.05f),0.f,1.f),lit=t*t*(3-2*t);
                fore=opacity*lit;
                const float a=fore+backing*(1-fore);
                for(int c=0;c<3;++c) {
                    const float solid=(opacity>0?linear[c]*opacity:0)+.006f*(1-opacity);
                    want[c]=backing>=1?solid:a>0?(linear[c]*fore+backing*(solid-linear[c]*fore))/a:0;
                }
                want[3]=backing>=1?1:a;
            } else set(.006f,.006f,.006f,backing);
        }
        // a frame fading: over the map beneath it, premultiplied, out unassociated
        if(inFrame&&frame>0) {
            const float a=want[3]*(1-frame)+border[3]*frame;
            for(int c=0;c<3;++c)want[c]=a>0?(want[c]*want[3]*(1-frame)+border[c]*border[3]*frame)/a:0;
            want[3]=a;
        }
    };
    bool correct=true;int opaqueFore=0,clearBacking=0,frameFaded=0;
    for(UINT y=0;y<tex.Height;++y)for(UINT x=0;x<tex.Width;++x){
        auto* out=reinterpret_cast<uint16_t*>(static_cast<char*>(data)+y*outputFootprint.Footprint.RowPitch)+x*4;
        float want[4]{};bool filtered=false,premultiplied=false,inFrame=false;float fore=0;
        if(!squareMap)std::memcpy(want,expected[y*sourceWidth+x],sizeof want);
        else if(!seeThrough||y<=32)squarePixel(x,int(y),1,1,want,filtered,fore,inFrame);   // the solid square, and the row under it
        else if(ground&&y>=2*SeeThroughTop-1){
            // the floor map: never a frame, whatever the copy's (--frame 100 too), its edge faded out
            int row=int(y)-int(2*SeeThroughTop);squarePixel(x,row,back,0,want,filtered,fore,inFrame);
            float r=std::hypot((x+.5f)/16-1,(row+.5f)/16-1),q=std::clamp((r-.62f)/.38f,0.f,1.f);
            float cover=.55f*(1-q*q*(3-2*q));
            want[3]*=cover;for(int lane=0;lane<3;++lane)want[lane]*=want[3];
            for(int lane=0;lane<4;++lane)if(std::abs(Half(out[lane])-want[lane])>(filtered?.026f:.0008f)){correct=false;std::printf("floor %u,%d lane %d mismatch\n",x,row,lane);}
            continue;
        } else if(y<SeeThroughTop-1||!copyDrawn||(ground&&y>SeeThroughTop+32)) {
            // between the squares, and all below when the copy is the wrist's (100%, its frame shown): never drawn (a
            // committed resource starts zeroed)
            for(int c=0;c<4;++c)if(Half(out[c])!=0){correct=false;std::printf("pixel %u,%u lane %d = %f: drawn where nothing is\n",x,y,c,Half(out[c]));}
            continue;
        } else {   // the floating copy's own square, and the row over it
            squarePixel(x,int(y)-int(SeeThroughTop),back,frameShown,want,filtered,fore,inFrame);
            premultiplied=filtered;
            if(filtered&&fore>=1)++opaqueFore;
            if(!filtered&&!inFrame&&want[3]==back&&back<1)++clearBacking;
            if(inFrame&&frameShown<1)++frameFaded;
        }
        if(blackBackground)want[3]=1;
        // Hardware texture filtering rounds weights and half-float samples.
        // Allow two 8-bit color steps after gamma conversion for filtered RGB;
        // alpha, framing and the unfiltered menu retain the strict tolerance.
        // The see-through copy's filtered pixels: their color as composed (times alpha) and their alpha within two
        // steps; what the game drew fully lit stays strictly opaque.
        // Partly lit there, the shader's lit (a smoothstep over 0.09 of brightness) turns the hardware's 8-bit filter
        // weights into up to ~0.03 of alpha: those pixels pass within two steps of the range their weights one 1/256
        // step either way give.
        float low[4],high[4];
        for(int c=0;c<4;++c)low[c]=high[c]=premultiplied&&c<3?want[c]*want[3]:want[c];
        if(premultiplied&&fore<1)for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i) {
            float nudged[4];bool nearFiltered=false,nearFrame=false;float nearFore=0;
            squarePixel(x,int(y)-int(SeeThroughTop),back,frameShown,nudged,nearFiltered,nearFore,nearFrame,i/256.f,j/256.f);
            for(int c=0;c<4;++c){const float v=c<3?nudged[c]*nudged[3]:nudged[c];low[c]=std::min(low[c],v);high[c]=std::max(high[c],v);}
        }
        for(int c=0;c<4;++c) {
            const float got=premultiplied&&c<3?Half(out[c])*Half(out[3]):Half(out[c]);
            const float tolerance=premultiplied&&c==3&&fore<1?2.f/255:squareMap&&c<3?2.f/255:.0006f;
            if(got<low[c]-tolerance||got>high[c]+tolerance){correct=false;std::printf("pixel %u,%u lane %d = %f expected %f to %f\n",x,y,c,got,low[c],high[c]);}
        }
    }
    if(seeThrough) {   // the wrist's square (and the row under it) bit for bit as the one solid square at 100% draws it
        void* wrist=nullptr;Check(wristReadback->Map(0,&range,&wrist),"map wrist readback");
        for(UINT y=0;y<=32;++y)if(std::memcmp(static_cast<char*>(data)+y*outputFootprint.Footprint.RowPitch,static_cast<char*>(wrist)+y*outputFootprint.Footprint.RowPitch,32*8)) {
            correct=false;std::printf("row %u of the wrist's square differs from the solid square alone\n",y);
        }
        wristReadback->Unmap(0,&empty);
    }
    readback->Unmap(0,&empty);if(!correct)throw std::runtime_error("converted pixels differ");
    if(copyDrawn&&(!opaqueFore||(back<1&&!clearBacking)||(frameShown<1&&!frameFaded)))
        throw std::runtime_error("the floating copy's square had no fully drawn, backing or frame pixel to test");
    if(seeThrough){std::printf("Native UI GPU conversion passed: the floating map's backing at %.0f%%, its frame at %.0f%%; %s; the solid square unchanged\n",back*100,frameShown*100,
        !copyDrawn?"no square of its own drawn (the wrist's)":back<1?"what the game drew lit stays opaque, the backing at that alpha, the frame over the map beneath at its own":
        "what the game drew stays as solid as the wrist's, the frame over the map beneath at its own");
        std::puts("the wrist's square identical, bit for bit, to the one solid square drawn alone");return 0;}
    std::puts(undim?"Native UI GPU conversion passed: the menu's backdrop taken out, what is drawn over it unchanged":
              blackBackground?"Native UI GPU conversion passed: opaque black backing, unchanged artwork colors":"Native UI GPU conversion passed: orientation, SDR color, translucent edges, inverse alpha and clamping");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
