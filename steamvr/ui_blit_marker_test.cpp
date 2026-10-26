// The square map's hero marker search (FindTiles + FindX) on the GPU: the nearest blue X to
// the share's centre, its middle, count and half size - every pixel counted, so they hold
// still as the hero moves - on a corner-map-sized share and on the full map's 3072x1728,
// with a farther decoy, at each of the X's offsets against the tiles in a 16-pixel walk.
// Prints the GPU time of the whole Render (marker passes and the map's).
#include "ui_blit.h"
#include "wrist_full_map.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* what){if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",what,(unsigned long)hr);throw std::runtime_error(what);}}
static void Expect(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static uint16_t HalfOf(float f){   // the few values this test writes: 0, 1 and blues in [0.1, 1]
    if(f==0)return 0;
    int e=0;const float m=std::frexp(f,&e);   // f = m * 2^e, m in [0.5, 1)
    return uint16_t(((e-1+15)<<10)|(int(std::lround((m*2-1)*1024))&1023));
}
struct Blob {int x,y,half;};   // a blue square of side 2*half+1 at (x, y), offsets from the share's centre
static void Run(ID3D12Device* device,ID3D12CommandQueue* queue,UINT w,UINT h,Blob x,Blob decoy,float tolerance,framevr::fullmap::XSearch search={}) {
    D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=w;tex.Height=h;
    tex.DepthOrArraySize=tex.MipLevels=1;tex.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;tex.SampleDesc.Count=1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 sourceBytes=0;
    device->GetCopyableFootprints(&tex,0,1,0,&footprint,nullptr,nullptr,&sourceBytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> source,target,upload,readback;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)),"source");
    tex.Width=tex.Height=512;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)),"target");
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=sourceBytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;
    buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type=D3D12_HEAP_TYPE_UPLOAD;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    buffer.Width=256;
    hp.Type=D3D12_HEAP_TYPE_READBACK;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
    // nothing drawn: transmittance 1; the X and the decoy: opaque blue (sRGB-encoded, alpha 0)
    const uint16_t empty[4]={0,0,0,HalfOf(1)},blue[4]={HalfOf(.1f),HalfOf(.2f),HalfOf(.9f),0};
    void* data=nullptr;D3D12_RANGE none{0,0};Check(upload->Map(0,&none,&data),"map upload");
    const int cx=int(w/2),cy=int(h/2);
    auto inside=[&](const Blob& b,int px,int py){return std::abs(px-cx-b.x)<=b.half&&std::abs(py-cy-b.y)<=b.half;};
    for(UINT y=0;y<h;++y){
        auto* row=reinterpret_cast<uint16_t*>(static_cast<char*>(data)+y*footprint.Footprint.RowPitch);
        for(UINT i=0;i<w;++i)std::memcpy(row+i*4,inside(x,int(i),int(y))||inside(decoy,int(i),int(y))?blue:empty,8);
    }
    upload->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"commands");
    D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=2;ComPtr<ID3D12QueryHeap> queries;
    Check(device->CreateQueryHeap(&qd,IID_PPV_ARGS(&queries)),"queries");
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=source.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    commands->ResourceBarrier(1,&barrier);
    framevr::UiBlit blit;Expect(blit.Initialize(device,nullptr),"initialize");
    commands->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
    // the full map's flags and span: the arrow, top-down, turning; a square of the share's width
    Expect(blit.Render(device,commands.Get(),source.Get(),target.Get(),false,true,1|4|8,0,1,1,float(w),search.reach,search.right,search.down),"render");
    commands->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
    commands->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,readback.Get(),0);
    Expect(blit.Marker()!=nullptr,"marker texture");
    barrier.Transition={blit.Marker(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    commands->ResourceBarrier(1,&barrier);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT one{};one.Offset=0;one.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,1,1,1,D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
    ComPtr<ID3D12Resource> markerBack;buffer.Width=D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&markerBack)),"marker readback");
    from={};from.pResource=blit.Marker();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to={};to.pResource=markerBack.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=one;
    commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commands->ResourceBarrier(1,&barrier);
    Check(commands->Close(),"close");ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");Check(queue->Signal(fence.Get(),1),"signal");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(1,event),"completion");
    const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);Expect(wait==WAIT_OBJECT_0,"GPU timeout");
    UINT64 frequency=0;Check(queue->GetTimestampFrequency(&frequency),"timestamp frequency");
    D3D12_RANGE stamps{0,16};Check(readback->Map(0,&stamps,&data),"map stamps");
    const auto* t=static_cast<const UINT64*>(data);const double ms=double(t[1]-t[0])*1000/double(frequency);readback->Unmap(0,&none);
    D3D12_RANGE marker{0,16};Check(markerBack->Map(0,&marker,&data),"map marker");
    float m[4];std::memcpy(m,data,sizeof m);markerBack->Unmap(0,&none);
    const float wantX=float(cx+x.x)+.5f,wantY=float(cy+x.y)+.5f,wantCount=float((2*x.half+1)*(2*x.half+1)),wantHalf=float(x.half)+.5f;
    std::printf("%ux%u share%s: marker at %.2f,%.2f (want %.2f,%.2f), count %.0f (%.0f), half size %.2f (%.2f); Render %.3f ms on the GPU\n",
                w,h,search.reach>0?" (full map search)":"",m[0],m[1],wantX,wantY,m[2],wantCount,m[3],wantHalf,ms);
    Expect(std::fabs(m[0]-wantX)<=tolerance&&std::fabs(m[1]-wantY)<=tolerance,"the X's middle, not the decoy's");
    Expect(m[2]>=3&&std::fabs(m[2]-wantCount)<=wantCount*.5f,"its pixel count, enough to be seen");
    Expect(std::fabs(m[3]-wantHalf)<=tolerance,"its half size");
}
int main(){try {
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    D3D12_COMMAND_QUEUE_DESC q{};ComPtr<ID3D12CommandQueue> queue;Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
    Expect(framevr::UiBlit::FindTiles(3072,1728)>=(2*int(3072*.08f)+framevr::UiBlit::FindTile)/framevr::UiBlit::FindTile&&
           framevr::UiBlit::FindTiles(640,400)>=(2*int(640*.08f)+framevr::UiBlit::FindTile)/framevr::UiBlit::FindTile&&
           framevr::UiBlit::FindTiles(1u<<20,1)==framevr::UiBlit::TileGrid,"the tiles cover the shader's search square, within the grid");
    // a corner map, then the full map's share on a 3072-px layer: X up-right of the centre,
    // a decoy farther out; the X walked across a tile both ways (the hero moving)
    for(int step=0;step<16;step+=5) {
        Run(device.Get(),queue.Get(),640,400,{9+step,-6-step,3},{40,30,3},.01f);
        Run(device.Get(),queue.Get(),3072,1728,{37+step,-23-step,4},{150,100,4},.01f);
    }
    // the full map's search where the game draws the X, sized to it (wrist_full_map.h XSearchAt), on a
    // 2112x1188 share as in the headset trace of 2026-10-24: at game zoom 0.125 a small X with water
    // 30 px off (the wide search took it in) and a speck 17 px off; at 1.625 and 2.0 the big X risen
    // above the center with water 80 px below the center (which a square at the center would take in)
    const auto at=[](float zoom){return framevr::fullmap::XSearchAt(zoom,1188);};
    for(int step=0;step<6;step+=5) {
        Run(device.Get(),queue.Get(),2112,1188,{1+step/5,-4+step/5,4},{40,20,10},.01f,at(.125f));
        Run(device.Get(),queue.Get(),2112,1188,{1,-3,4},{20,0,3},.01f,at(.125f));
        Run(device.Get(),queue.Get(),2112,1188,{6,-25+step,50},{0,90,10},.01f,at(1.625f));
        Run(device.Get(),queue.Get(),2112,1188,{7,-31+step,67},{0,95,8},.01f,at(2.f));
    }
    std::puts("Map marker search passed: the nearest X, its middle, count and size, on a corner map, the full map's 3072x1728 share, and the full map's own search on 2112x1188");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
