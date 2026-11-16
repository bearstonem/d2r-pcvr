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
    bool typeless=false,srgb=false,toss=false,ice=false,depthTest=false,sky=false,farSurface=false,invalid=false,mirror=false,reproject=false;
    for(int i=3;i<argc;++i){typeless|=!std::strcmp(argv[i],"--typeless");srgb|=!std::strcmp(argv[i],"--srgb");toss|=!std::strcmp(argv[i],"--toss");ice|=!std::strcmp(argv[i],"--ice");depthTest|=!std::strcmp(argv[i],"--ice-depth");
        sky|=!std::strcmp(argv[i],"--sky");farSurface|=!std::strcmp(argv[i],"--far");invalid|=!std::strcmp(argv[i],"--invalid");mirror|=!std::strcmp(argv[i],"--mirror");reproject|=!std::strcmp(argv[i],"--reproject");}
    ice|=depthTest||mirror;
    const auto format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr unsigned W=960,H=720;
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
    potions::Models models;if(!models.Initialize(device.Get(),queue.Get(),format,W,H,+[](const char* s){std::puts(s);},argv[1]))return 1;
    if(toss&&models.Tosses()!=5)throw std::runtime_error("thrown trap models missing");
    if(ice&&models.IcePieces()!=3)throw std::runtime_error("ice plates missing");
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
    ComPtr<ID3D12Resource> sceneDepth,depthUpload;
    if(depthTest){
        auto d=tex;d.Width=2*W;d.Height=2*H;d.Format=DXGI_FORMAT_R32_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_NONE;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&sceneDepth)),"synthetic depth");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);
        D3D12_RESOURCE_DESC buf{};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=size;buf.Height=buf.DepthOrArraySize=buf.MipLevels=1;buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES upload{};upload.Type=D3D12_HEAP_TYPE_UPLOAD;
        Check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&depthUpload)),"depth upload");
        void* mem=nullptr;Check(depthUpload->Map(0,nullptr,&mem),"depth map");
        for(unsigned y=0;y<2*H;++y)for(unsigned x=0;x<2*W;++x){
            // Actual scene occupies a quarter of this allocation, like DLSS.
            // Unused pixels are deliberately near, to catch missing UV scale.
            const float z=(x<W&&y<H)?(sky?0.f:(farSurface?.1f/2.f:((reproject?x<W/2:y<H/2)?.1f/.55f:.1f/2.f))):1.f;
            std::memcpy(static_cast<char*>(mem)+y*fp.Footprint.RowPitch+x*4,&z,4);
        }
        depthUpload->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=depthUpload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=sceneDepth.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={sceneDepth.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};list->ResourceBarrier(1,&b);
    }
    for(unsigned eye=0;eye<2;++eye) {
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[eye])),"target");
        auto handle=rtv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*stride;
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};viewDesc.Format=format;viewDesc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(targets[eye].Get(),&viewDesc,handle);
        float background[]={.018f,.022f,.027f,1};
        if(ice&&!mirror&&eye==1){background[0]=.5f;background[1]=.25f;background[2]=.12f;}
        list->ClearRenderTargetView(handle,background,0,nullptr);
        const interaction::Pose view{{mirror?0.f:eye==0?-.032f:.032f,0,0},{0,0,0,1}};
        if(ice){
            potions::Scene scene;
            if(mirror)potions::AddIce(scene,{{0,0,-.85f},{0,0,0,1}},0,.4f,.32f,{},{},0,eye==1);
            else for(int i=0;i<3;++i)potions::AddIce(scene,{{(i-1)*.29f,0,-.85f},{0,0,0,1}},i,.4f,.32f);
            potions::SceneDepth depth;
            if(depthTest){depth.resource=sceneDepth.Get();depth.format=DXGI_FORMAT_R32_FLOAT;depth.scale[0]=depth.scale[1]=.5f;depth.nearMetres=invalid?0:.1f;depth.view=view;depth.fov={-.65f,.65f,-.53f,.53f};
                if(reproject){depth.view.p.x+=.16f;depth.view.q=math::Yaw(.08f);depth.fov={-.55f,.70f,-.53f,.53f};}}
            if(!models.Render(list.Get(),targets[eye].Get(),eye,view,{-.65f,.65f,-.53f,.53f},scene,depth))throw std::runtime_error("ice render");
        }
        for(unsigned row=0;row<3&&!ice;++row) {
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
    std::vector<uint8_t> rgb(2*W*H*3);double sx[2]{},count[2]{};unsigned center[2][4]{};
    unsigned upper[2]{},lower[2]{},xmin[2]{W,W},xmax[2]{};uint8_t clearColor[2][4]{};
    for(unsigned eye=0;eye<2;++eye) {
        void* data=nullptr;D3D12_RANGE range{0,size_t(bytes)};Check(readback[eye]->Map(0,&range,&data),"map");
        const auto* background=static_cast<uint8_t*>(data);
        for(unsigned c=0;c<4;++c)clearColor[eye][c]=background[c];
        if(ice){const auto* pixel=background+(H/2)*footprint.Footprint.RowPitch+(W/2)*4;for(unsigned c=0;c<4;++c)center[eye][c]=pixel[c];}
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
            auto* p=static_cast<uint8_t*>(data)+y*footprint.Footprint.RowPitch+x*4;
            for(int c=0;c<3;++c)rgb[((y*2*W)+eye*W+x)*3+c]=p[c];
            if(int(p[0])>background[0]+15||int(p[1])>background[1]+15||int(p[2])>background[2]+15){++count[eye];sx[eye]+=x;if(y<H/2)++upper[eye];else ++lower[eye];xmin[eye]=std::min(xmin[eye],x);xmax[eye]=std::max(xmax[eye],x);}
        }
        D3D12_RANGE none{0,0};readback[eye]->Unmap(0,&none);
        if(count[eye]<(mirror?1000u:6000u))throw std::runtime_error("missing potion pixels");
    }
    // Near objects move right in the left eye, left in the right eye.
    if(!mirror&&sx[0]/count[0]-sx[1]/count[1]<15)throw std::runtime_error("incorrect stereo disparity");
    if(depthTest){
        for(unsigned eye=0;eye<2;++eye){
            std::printf("Depth occlusion eye %u: upper %u, lower %u visible pixels\n",eye,upper[eye],lower[eye]);
            if(reproject){for(unsigned c=0;c<3;++c)if(std::abs(int(center[eye][c])-int(clearColor[eye][c]))>1)throw std::runtime_error("source-eye reprojection failed to hide the middle crystal");}
            else if(!sky&&!farSurface&&!invalid){if(upper[eye]>16||lower[eye]<1000)throw std::runtime_error("near surface failed to occlude ice, or farSurface surface hid ice");}
            else if(upper[eye]<1000||lower[eye]<1000)throw std::runtime_error("sky/farSurface/invalid depth incorrectly hid ice");
        }
    }
    if(mirror){
        std::printf("Mirror silhouette: [%u,%u] vs [%u,%u]\n",xmin[0],xmax[0],xmin[1],xmax[1]);
        if(std::abs(int(xmin[0]+xmax[1])-int(W-1))>5||std::abs(int(xmax[0]+xmin[1])-int(W-1))>5)throw std::runtime_error("whole-asset mirroring failed");
    }
    if(ice&&!mirror){
        if(int(center[1][0])-int(center[0][0])<35||center[0][3]!=255||center[1][3]!=255)
            throw std::runtime_error("ice alpha blending or target-alpha preservation failed");
        std::printf("Ice GPU alpha check: center red over dark/light backgrounds %u/%u, alpha %u/%u\n",center[0][0],center[1][0],center[0][3],center[1][3]);
    }
    std::ofstream out(argv[2],std::ios::binary);out<<"P6\n"<<2*W<<" "<<H<<"\n255\n";out.write(reinterpret_cast<char*>(rgb.data()),rgb.size());
    if(!out)throw std::runtime_error("preview output failed");
    std::puts("Original potion GPU test passed: textured geometry in both eyes, correct disparity and readback");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
