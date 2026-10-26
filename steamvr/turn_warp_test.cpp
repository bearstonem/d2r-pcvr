#include "turn_warp.h"
#include "dxgi_views.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
using framevr::math::Fov;
using framevr::math::Quat;
using framevr::math::Vec;
static void Check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static void Check(HRESULT hr,const char* what,int){if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",what,(unsigned long)hr);throw std::runtime_error(what);}}
static bool Near(float a,float b,float e=1e-5f){return std::fabs(a-b)<=e;}
// where this frame's (u, v) is in the drawn image
static void Apply(const std::array<float,9>& h,float u,float v,float& x,float& y,float& w) {
    w=h[6]*u+h[7]*v+h[8];x=(h[0]*u+h[1]*v+h[2])/w;y=(h[3]*u+h[4]*v+h[5])/w;
}
static Quat Pitch(float a){return {std::sin(a*.5f),0,0,std::cos(a*.5f)};}

static void Math() {
    const Fov steam{-1.03239f,1.03239f,-1.04476f,1.04476f};   // the owner's headset, symmetric
    const Fov offset{-.95f,.80f,-.90f,.85f};                      // a runtime's own, off-centre
    const Quat level{0,0,0,1},tilted=framevr::math::Multiply(framevr::math::Yaw(.7f),Pitch(-.3f));
    for(const Fov& fov:{steam,offset})for(const Quat& head:{level,tilted}) {
        const auto same=framevr::TurnHomography(head,fov,head,fov);
        for(float u:{0.f,.25f,.5f,1.f})for(float v:{0.f,.4f,1.f}) {
            float x,y,w;Apply(same,u,v,x,y,w);
            Check(Near(x,u)&&Near(y,v)&&w>0,"the same head and FOV leave every pixel where it is");
        }
    }
    const float t=std::tan(steam.right),turn=.05f;   // about 3 degrees in a frame
    float x,y,w;
    // the head turned left: this frame's middle was left of the drawn image's
    Apply(framevr::TurnHomography(level,steam,framevr::math::Yaw(turn),steam),.5f,.5f,x,y,w);
    Check(Near(x,.5f-std::tan(turn)/(2*t))&&Near(y,.5f)&&w>0,"a turn to the left reads left of the drawn middle");
    Apply(framevr::TurnHomography(level,steam,framevr::math::Yaw(-turn),steam),.5f,.5f,x,y,w);
    Check(Near(x,.5f+std::tan(turn)/(2*t))&&Near(y,.5f),"a turn to the right reads right of it");
    // looking up: higher in the drawn image (v down)
    Apply(framevr::TurnHomography(level,steam,Pitch(turn),steam),.5f,.5f,x,y,w);
    Check(Near(x,.5f)&&Near(y,.5f-std::tan(turn)/(2*std::tan(steam.up))),"a look up reads above the drawn middle");
    // the turn is relative: the same 3 degrees from any head
    const auto from=framevr::math::Multiply(framevr::math::Yaw(1.1f),Pitch(.2f));
    Apply(framevr::TurnHomography(from,steam,framevr::math::Multiply(from,framevr::math::Yaw(turn)),steam),.5f,.5f,x,y,w);
    Check(Near(x,.5f-std::tan(turn)/(2*t),1e-4f)&&Near(y,.5f,1e-4f),"only the turn since the drawing counts");
    // a narrower FOV now reads nearer the drawn middle
    const Fov narrow{-.8f,.8f,-.8f,.8f};
    Apply(framevr::TurnHomography(level,steam,level,narrow),1.f,0.f,x,y,w);
    Check(Near(x,.5f+std::tan(.8f)/(2*t))&&Near(y,.5f-std::tan(.8f)/(2*std::tan(steam.up))),"each image's own FOV");
    // the right edge of an off-centre FOV
    Apply(framevr::TurnHomography(level,offset,level,steam),1.f,.5f,x,y,w);
    const float L=std::tan(offset.left),R=std::tan(offset.right);
    Check(Near(x,(t-L)/(R-L)),"an off-centre drawn FOV");
    // TurnMove: a point in the drawn view, through the world, into this frame's
    const Vec at{3,1.5f,-2},there{3.4f,1.6f,-2.5f};
    const Quat drawn=framevr::math::Multiply(framevr::math::Yaw(.4f),Pitch(-.2f)),now=framevr::math::Multiply(framevr::math::Yaw(.45f),Pitch(-.15f));
    const auto m=framevr::TurnMove(at,drawn,there,now);
    const Vec p{.3f,-.2f,-4};
    const Vec world=framevr::math::Rotate(drawn,p)+at,want=framevr::math::Rotate(framevr::math::Conjugate(now),world-there);
    const Vec got{m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]};
    Check(Near(got.x,want.x,1e-4f)&&Near(got.y,want.y,1e-4f)&&Near(got.z,want.z,1e-4f),"the move goes through the world");
    {   // TurnCarry: the hero walked and the body turned, the head still on it
        const Vec heroWas{10,0,20},heroNow{13,.5f,17},head{.1f,6.2f,-.3f};
        const Quat baseWas=framevr::math::Yaw(.3f),baseNow=framevr::math::Yaw(.9f),look=framevr::math::Multiply(framevr::math::Yaw(-.2f),Pitch(-.4f));
        const auto eye=[&](Vec hero,Quat base){return hero+framevr::math::Rotate(base,head);};
        const auto c=framevr::TurnCarry(eye(heroWas,baseWas),framevr::math::Multiply(baseWas,look),heroWas,baseWas,
                                        heroNow,baseNow,eye(heroNow,baseNow),framevr::math::Multiply(baseNow,look));
        const float identity[12]={1,0,0,0, 0,1,0,0, 0,0,1,0};
        bool still=true;for(int k=0;k<12;++k)still=still&&Near(c[k],identity[k],1e-4f);
        Check(still,"what the hero carries does not move while only the hero does");
        // the head turned on the body as well: the carried move is that turn alone
        const Quat lookNow=framevr::math::Multiply(framevr::math::Yaw(.1f),Pitch(-.4f));
        const auto t=framevr::TurnCarry(eye(heroWas,baseWas),framevr::math::Multiply(baseWas,look),heroWas,baseWas,
                                        heroNow,baseNow,eye(heroNow,baseNow),framevr::math::Multiply(baseNow,lookNow));
        const auto h=framevr::TurnMove(head,look,head,lookNow);   // the same head turn with no hero
        bool same=true;for(int k=0;k<12;++k)same=same&&Near(t[k],h[k],1e-4f);
        Check(same,"only the head's own move on the body is left");
    }
    std::puts("Turn homography and move passed: identity, yaw, pitch, relative turns, FOVs, world move, carried move");
}

// A device, a queue and one list to record on, run and wait for.
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;uint64_t value=0;
    Gpu() {
        Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device",0);
        D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue",0);
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator",0);
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list",0);
        Check(list->Close(),"close",0);
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence",0);
    }
    void Record(const std::function<void()>& work) {
        Check(allocator->Reset(),"reset allocator",0);Check(list->Reset(allocator.Get(),nullptr),"reset list",0);
        work();Check(list->Close(),"close",0);
        ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        Check(queue->Signal(fence.Get(),++value),"signal",0);
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(value,event),"completion",0);
        const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);
        if(waited!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
    }
    ComPtr<ID3D12Resource> Texture(UINT w,UINT h,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=w;tex.Height=h;
        tex.DepthOrArraySize=tex.MipLevels=1;tex.Format=format;tex.SampleDesc.Count=1;tex.Flags=flags;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> r;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,state,nullptr,IID_PPV_ARGS(&r)),"texture",0);
        return r;
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=1;
        b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{};hp.Type=type;ComPtr<ID3D12Resource> r;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,state,nullptr,IID_PPV_ARGS(&r)),"buffer",0);
        return r;
    }
    // texels, row by row, into a texture left in `after`
    template<class Texel> void Upload(ID3D12Resource* texture,const std::vector<Texel>& texels,D3D12_RESOURCE_STATES after) {
        const auto desc=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&f,nullptr,nullptr,&bytes);
        auto upload=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data=nullptr;D3D12_RANGE none{0,0};Check(upload->Map(0,&none,&data),"map upload",0);
        for(UINT y=0;y<desc.Height;++y)std::memcpy(static_cast<char*>(data)+y*f.Footprint.RowPitch,&texels[y*desc.Width],desc.Width*sizeof(Texel));
        upload->Unmap(0,nullptr);
        Record([&]{
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=f;
            to.pResource=texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,after};list->ResourceBarrier(1,&b);
        });
    }
    // The warp into `target` (RENDER_TARGET), read back.
    std::vector<uint32_t> Warp(framevr::TurnWarp& warp,ID3D12Resource* source,ID3D12Resource* target,const std::array<float,9>& h,int slot,const framevr::TurnDepth* depth=nullptr,const framevr::TurnDepth* drawn=nullptr) {
        return Drawn(target,[&]{Check(warp.Render(device.Get(),list.Get(),source,target,h,slot,depth,drawn),"render");});
    }
    // What `draw` recorded into `target` (RENDER_TARGET), read back.
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
        std::vector<uint32_t> out(desc.Width*desc.Height);void* data=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)},none{0,0};
        Check(readback->Map(0,&range,&data),"map readback",0);
        for(UINT y=0;y<desc.Height;++y)std::memcpy(&out[y*desc.Width],static_cast<char*>(data)+y*f.Footprint.RowPitch,desc.Width*4);
        readback->Unmap(0,&none);return out;
    }
};
// sRGB decoded, filtered at the texel's centre and encoded again: a step at most
static bool Same(uint32_t got,uint32_t want) {
    for(int lane=0;lane<4;++lane)if(std::abs(int((got>>lane*8)&255)-int((want>>lane*8)&255))>1)return false;
    return true;
}
static constexpr std::array<float,9> Identity{1,0,0, 0,1,0, 0,0,1};

// The transport builds a throwaway TurnWarp on a worker while a session
// starts; the render thread's own Initialize may meet that compile still
// running, and every later session reuses its code.
static void Precompiled(Gpu& gpu) {
    bool warmed=false;
    std::thread worker([&]{framevr::TurnWarp warm;warmed=warm.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr);});
    framevr::TurnWarp first;
    const bool built=first.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr);
    worker.join();
    Check(warmed&&built&&first.DepthReady()&&first.NearReady()&&first.EffectsReady()&&first.InterfaceReady()&&first.RimReady(),"every pass is built beside a running compile");
    framevr::TurnWarp next;
    Check(next.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr)&&next.DepthReady()&&next.NearReady()&&
          next.EffectsReady()&&next.InterfaceReady()&&next.RimReady(),"a later session builds every pass from the compiled code");
}
// The pass on the GPU: an 8x8 picture, the target typed or typeless as the runtime's may be.
static void Turn(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,+[](const char* s){std::puts(s);}),"pipeline");
    Check(warp.DepthReady(),"the depth pass is built");
    constexpr UINT side=8;
    auto source=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    // every pixel its own: x and y in red and green, a fixed blue, opaque
    const auto texel=[](UINT x,UINT y){return uint32_t(20+x*29)|uint32_t(20+y*29)<<8|0x80u<<16|0xffu<<24;};
    std::vector<uint32_t> texels(side*side);for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x)texels[y*side+x]=texel(x,y);
    gpu.Upload(source.Get(),texels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // what each case should give: a source pixel (x, y), or black when behind
    struct Case {const char* what;std::array<float,9> h;int dx,dy;bool behind;};
    const Case cases[]={
        {"the same head copies every pixel",Identity,0,0,false},
        {"a turn reads two pixels to the right, the edge drawn out",{1,0,2.f/side, 0,1,0, 0,0,1},2,0,false},
        {"homogeneous: a scale of the whole matrix changes nothing",{3,0,0, 0,3,-3.f/side, 0,0,3},0,-1,false},
        {"slot 1 and a look down: one pixel lower",{1,0,0, 0,1,1.f/side, 0,0,1},0,1,false},
        {"behind the drawn eye stays black",{1,0,0, 0,1,0, 0,0,-1},0,0,true},
    };
    for(const auto& c:cases) {
        const auto out=gpu.Warp(warp,source.Get(),target.Get(),c.h,int(&c-cases)&1);
        bool same=true;
        for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x) {
            const UINT sx=UINT(std::clamp(int(x)+c.dx,0,int(side)-1)),sy=UINT(std::clamp(int(y)+c.dy,0,int(side)-1));
            const uint32_t want=c.behind?0xff000000u:texel(sx,sy);
            if(!Same(out[y*side+x],want)){same=false;std::printf("%s: pixel %u,%u = %08x, expected %08x\n",c.what,x,y,out[y*side+x],want);}
        }
        Check(same,c.what);
    }
    std::printf("Turn warp GPU pass passed (%s target): copy, turn, edge, scale, slots, behind\n",typeless?"typeless":"typed");
}

// The depth pass: a 64x64 picture and its depth, a 90-degree view, the eye
// moved half a world unit to the right. A point d away goes 0.5 / d of the
// half-width (32 px) to the left: 4 px at 4 units, 2 at 8, 8 at 2.
static void Move(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr)&&warp.DepthReady(),"pipelines");
    constexpr UINT side=64;constexpr float nearPlane=1;
    auto source=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto texel=[](UINT x,UINT y){return uint32_t(x*4)|uint32_t(y*4)<<8|0x80u<<16|0xffu<<24;};
    std::vector<uint32_t> texels(side*side);for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x)texels[y*side+x]=texel(x,y);
    gpu.Upload(source.Get(),texels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const float q=std::atan(1.f);const Fov square{-q,q,-q,q};
    framevr::TurnDepth moved;moved.format=DXGI_FORMAT_R32_FLOAT;moved.nearPlane=nearPlane;
    moved.drawnFov=moved.nowFov=square;moved.move=framevr::TurnMove({0,0,0},{0,0,0,1},{.5f,0,0},{0,0,0,1});
    const auto distances=[&](const std::function<float(UINT)>& at){   // depth = near / distance, by column; 0 the sky
        std::vector<uint32_t> d(side*side);
        for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){const float v=at(x)>0?nearPlane/at(x):0.f;std::memcpy(&d[y*side+x],&v,4);}
        auto texture=gpu.Texture(side,side,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(texture.Get(),d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return texture;
    };
    // what this frame shows at column x, read from column x + shift of the drawn picture
    const auto expect=[&](const std::vector<uint32_t>& out,UINT from,UINT to,int shift,const char* what) {
        bool same=true;
        for(UINT y=0;y<side;++y)for(UINT x=from;x<to;++x)if(!Same(out[y*side+x],texel(x+shift,y))) {
            if(same)std::printf("%s: pixel %u,%u = %08x, expected %08x\n",what,x,y,out[y*side+x],texel(x+shift,y));same=false;
        }
        Check(same,what);
    };
    int slot=0;
    {   // a wall 4 units away: all of it 4 px to the left; the edge it leaves keeps the turn alone
        const auto depth=distances([](UINT){return 4.f;});moved.depth=depth.Get();
        const auto out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&moved);
        expect(out,0,56,4,"a wall 4 units away moves 4 px");
        expect(out,61,64,0,"the edge the wall leaves keeps the turn alone");
    }
    {   // the sky (depth 0) does not move with the eye
        const auto depth=distances([](UINT){return 0.f;});moved.depth=depth.Get();
        expect(gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&moved),0,64,0,"the sky stays where it is");
    }
    {   // a post 2 units away (columns 24-39) before a wall at 8: the post goes 8 px, over the wall's 2
        const auto depth=distances([](UINT x){return x>=24&&x<40?2.f:8.f;});moved.depth=depth.Get();
        const auto out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&moved);
        expect(out,18,30,8,"the post moves 8 px and covers the wall behind it");
        expect(out,2,14,2,"the wall left of it moves 2 px");
        expect(out,46,56,2,"the wall right of it moves 2 px");
    }
    {   // carried: the hero took the eye half a unit right. A hand 2 units away
        // (columns 24-39) went along and stays; the wall at 8 still moves 2 px
        framevr::TurnDepth carried=moved;carried.carry=framevr::TurnMove({0,0,0},{0,0,0,1},{0,0,0},{0,0,0,1});
        carried.reach[0]=3;carried.reach[1]=3.5f;
        const auto depth=distances([](UINT x){return x>=24&&x<40?2.f:8.f;});carried.depth=depth.Get();
        const auto out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&carried);
        expect(out,26,38,0,"what the hero carries stays with the eye");
        expect(out,2,18,2,"the wall left of it still moves 2 px");
        expect(out,46,56,2,"the wall right of it still moves 2 px");
        carried.reach[0]=carried.reach[1]=0;
        expect(gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&carried),18,30,8,"no reach: all moves with the world");
    }
    {   // the drawn eye's picture over this eye's own (turned only, unmoved): a
        // post 2 units away (columns 24-39) before a wall at 4, seen from half a
        // unit to the left. The post goes 8 px, the wall 4; the 4 px the post
        // uncovers on its right, hidden from the drawn eye, keep this eye's own
        const auto other=[](UINT x,UINT y){return uint32_t(x*4)|uint32_t(y*4)<<8|0x30u<<16|0xffu<<24;};
        std::vector<uint32_t> pixels(side*side);for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x)pixels[y*side+x]=other(x,y);
        auto picture=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(picture.Get(),pixels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const auto depth=distances([](UINT x){return x>=24&&x<40?2.f:4.f;});
        framevr::TurnDepth drawn=moved;drawn.depth=depth.Get();drawn.image=picture.Get();drawn.apart=.5f;
        auto out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,nullptr,&drawn);
        const auto from=[&](UINT a,UINT b,int shift,bool drawnEye,const char* what) {
            bool same=true;
            for(UINT y=0;y<side;++y)for(UINT x=a;x<b;++x) {
                const uint32_t want=drawnEye?other(x+shift,y):texel(x,y);
                if(!Same(out[y*side+x],want)){if(same)std::printf("%s: pixel %u,%u = %08x, expected %08x\n",what,x,y,out[y*side+x],want);same=false;}
            }
            Check(same,what);
        };
        // each pixel by its own depth, the cells across the post's edges too:
        // the post whole to its edges, this eye's own exactly where it hid the wall
        from(2,15,4,true,"the wall from the drawn eye, moved 4 px");
        from(16,32,8,true,"the post from the drawn eye, moved 8 px to both its edges");
        from(32,36,0,false,"what the post hid from the drawn eye keeps this eye's own");
        from(36,56,4,true,"the wall past it from the drawn eye again, from its edge on");
        drawn.apart=0;   // no edge test: the post's edge drawn out over the gap instead
        const auto stretched=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,nullptr,&drawn);
        Check(!Same(stretched[10*side+35],texel(35,10)),"without the cut the gap is the drawn eye's, drawn out");
        // a blade two texels wide (columns 33-34) inside one cell, 2 units away
        // before the wall at 4: it moves 8 px, not with the wall behind it
        const auto blade=distances([](UINT x){return x==33||x==34?2.f:4.f;});
        drawn.depth=blade.Get();drawn.apart=.5f;
        out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,nullptr,&drawn);
        from(2,24,4,true,"the wall left of the blade moved 4 px");
        from(25,27,8,true,"the blade inside one cell moved 8 px");
        from(29,31,0,false,"what the blade hid from the drawn eye keeps this eye's own");
        from(31,56,4,true,"the wall right of the blade moved 4 px");
        // This eye's own picture, moved with its own depth (the head still: in
        // place), showed the wall alone; the drawn eye's shows the post moved
        // in front of it since. Where both show the wall, this eye's own stays
        // (as a glow or hair would, seen from this eye); the post is the drawn
        // eye's; what it hid is this eye's own as before.
        const auto post=distances([](UINT x){return x>=24&&x<40?2.f:4.f;}),wall=distances([](UINT){return 4.f;});
        drawn.depth=post.Get();drawn.keep=framevr::TurnEdgePixels;
        framevr::TurnDepth still=moved;still.depth=wall.Get();still.move=framevr::TurnMove({0,0,0},{0,0,0,1},{0,0,0},{0,0,0,1});
        out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&still,&drawn);
        from(2,15,0,false,"where both show the wall this eye's own stays");
        from(16,32,8,true,"the post moved in from the drawn eye");
        from(32,56,0,false,"what it hid and the wall past it: this eye's own");
        drawn.keep=0;
        out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&still,&drawn);
        from(2,15,4,true,"keep off: the drawn eye's wall everywhere it reaches");
        from(32,36,0,false,"its own wall in the strip the drawn eye cannot see stays");
        // This eye's own picture still showed the post where it stood a frame
        // ago (columns 28-43): in the strip the drawn eye cannot see (32-35)
        // it is nearer than the wall behind - a ghost. What is behind beside
        // the strip goes there instead: the drawn eye's wall at 36 (its 40).
        const auto old=distances([](UINT x){return x>=28&&x<44?2.f:4.f;});
        framevr::TurnDepth stale=still;stale.depth=old.Get();
        out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&stale,&drawn);
        bool filled=true;
        for(UINT y=0;y<side;++y)for(UINT x=32;x<36;++x)if(!Same(out[y*side+x],other(40,y))) {
            if(filled)std::printf("a frame-old post in the strip: pixel %u,%u = %08x, expected %08x\n",x,y,out[y*side+x],other(40,y));filled=false;
        }
        Check(filled,"a frame-old post in the strip: what is behind beside it instead");
        from(16,32,8,true,"the post itself from the drawn eye still");
        warp.Fill(false);
        out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&stale,&drawn);
        from(32,36,0,false,"fill off: the frame-old post stays, as before");
        warp.Fill(true);
    }
    {   // DLSS: the scene drawn into the top-left quarter of a depth twice the size
        auto big=gpu.Texture(side*2,side*2,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        std::vector<uint32_t> d(side*side*4,0);const float v=nearPlane/4;
        for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x)std::memcpy(&d[y*side*2+x],&v,4);   // the rest: the sky
        gpu.Upload(big.Get(),d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        framevr::TurnDepth corner=moved;corner.depth=big.Get();corner.scale[0]=corner.scale[1]=.5f;
        expect(gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&corner),0,56,4,"the depth read from its drawn corner");
    }
    std::printf("Turn depth GPU pass passed (%s target): wall, sky, occlusion, edge, carried, the drawn eye's picture to the pixel, a thin blade, this eye's own where the surface is the same, a frame-old post in the strip filled, drawn corner\n",typeless?"typeless":"typed");
}

// Near things only (TurnDepth::keepNear): a 256-pixel wall 4 units away, a
// post 2 units away in columns 96-127, the drawn eye 0.125 units to the left
// (the post goes 8 px, the wall 4). This eye's own picture is kept where it
// shows the same wall within reach (32 px, two 16-px blocks) of the post -
// as the drawn eye saw it, then as this eye did - and the drawn eye's
// picture is taken everywhere else.
static void KeepNear(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr)&&warp.DepthReady()&&warp.NearReady(),"pipelines");
    constexpr UINT side=256;constexpr float nearPlane=1;
    const auto texel=[](UINT x,UINT y){return x|(y&255)<<8|0x80u<<16|0xffu<<24;};
    const auto other=[](UINT x,UINT y){return x|(y&255)<<8|0x30u<<16|0xffu<<24;};
    std::vector<uint32_t> own(side*side),pixels(side*side);
    for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){own[y*side+x]=texel(x,y);pixels[y*side+x]=other(x,y);}
    auto source=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto picture=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    gpu.Upload(source.Get(),own,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.Upload(picture.Get(),pixels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const auto distances=[&](bool post){
        std::vector<uint32_t> d(side*side);
        for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){const float v=nearPlane/(post&&x>=96&&x<128?2.f:4.f);std::memcpy(&d[y*side+x],&v,4);}
        auto texture=gpu.Texture(side,side,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(texture.Get(),d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return texture;
    };
    const auto withPost=distances(true),wallOnly=distances(false);
    const float q=std::atan(1.f);const Fov square{-q,q,-q,q};
    framevr::TurnDepth still;still.format=DXGI_FORMAT_R32_FLOAT;still.nearPlane=nearPlane;still.drawnFov=still.nowFov=square;
    still.move=framevr::TurnMove({0,0,0},{0,0,0,1},{0,0,0},{0,0,0,1});
    framevr::TurnDepth drawn=still;drawn.image=picture.Get();drawn.apart=.125f;
    drawn.move=framevr::TurnMove({0,0,0},{0,0,0,1},{.125f,0,0},{0,0,0,1});
    drawn.keep=framevr::TurnEdgePixels;drawn.keepNear=2;drawn.keepReach=32;   // the post parts from the wall by 4 px
    std::vector<uint32_t> out;int slot=0;
    const auto from=[&](UINT a,UINT b,int shift,bool drawnEye,const char* what) {
        bool same=true;
        for(UINT y=0;y<side;y+=7)for(UINT x=a;x<b;++x) {
            const uint32_t want=drawnEye?other(x+shift,y):texel(x,y);
            if(!Same(out[y*side+x],want)){if(same)std::printf("%s: pixel %u,%u = %08x, expected %08x\n",what,x,y,out[y*side+x],want);same=false;}
        }
        Check(same,what);
    };
    // The post moved in: the drawn eye shows it, this eye's own the wall alone.
    // Round where the drawn eye saw it (columns 64-159) the wall stays this eye's.
    drawn.depth=withPost.Get();still.depth=wallOnly.Get();
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&still,&drawn);
    from(2,56,4,true,"far from the post the drawn eye's wall");
    from(62,86,0,false,"within reach of the post, as the drawn eye saw it, this eye's own wall");
    from(90,118,8,true,"the post from the drawn eye");
    from(126,154,0,false,"within reach right of it, this eye's own wall");
    from(160,250,4,true,"past the reach the drawn eye's wall again");
    // The post moved away: this eye's own showed it, the drawn eye the wall
    // alone. Round where this eye saw it the wall stays its own: its grid
    // draws the post's edges out over a cell, into the blocks either side,
    // so from those (columns 48-175).
    drawn.depth=wallOnly.Get();still.depth=withPost.Get();
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,slot^=1,&still,&drawn);
    from(2,44,4,true,"far from it the drawn eye's wall");
    from(50,92,0,false,"within reach of the post, as this eye saw it, its own wall");
    from(98,126,4,true,"where the post was: the drawn eye's wall");
    from(134,174,0,false,"within reach right of it, its own wall");
    from(180,250,4,true,"past the reach the drawn eye's wall again");
    std::printf("Near things GPU pass passed (%s target): this eye's own kept within reach of a near thing as either eye saw it, the drawn eye's elsewhere\n",typeless?"typeless":"typed");
}

// IEEE half for the interface's texels: 0 and normal values only.
static uint16_t Half(float f) {
    if(f==0)return 0;
    uint32_t b;std::memcpy(&b,&f,4);
    return uint16_t(((b>>16)&0x8000u)|((((b>>23)&0xffu)-112u)<<10)|((b>>13)&0x3ffu));
}
// The game's effects (TurnDepth::keepEffects): a 256-pixel wall 4 units away
// in both eyes, the drawn eye 0.125 units to the left (the wall goes 4 px).
// The drawn eye's effects layer has something in columns 96-127 (its first
// target), this eye's in 160-191 (its second, left by the game to be copied
// from). This eye's own picture is kept where the pixel comes from the drawn
// eye's effect, and where this eye's own is; the drawn eye's elsewhere.
static void KeepEffects(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr)&&warp.DepthReady()&&warp.EffectsReady(),"pipelines");
    constexpr UINT side=256;constexpr float nearPlane=1;
    const auto texel=[](UINT x,UINT y){return x|(y&255)<<8|0x80u<<16|0xffu<<24;};
    const auto other=[](UINT x,UINT y){return x|(y&255)<<8|0x30u<<16|0xffu<<24;};
    std::vector<uint32_t> own(side*side),pixels(side*side);
    for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){own[y*side+x]=texel(x,y);pixels[y*side+x]=other(x,y);}
    auto source=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto picture=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    gpu.Upload(source.Get(),own,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.Upload(picture.Get(),pixels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    std::vector<uint32_t> d(side*side);const float v=nearPlane/4;for(auto& t:d)std::memcpy(&t,&v,4);
    auto wall=gpu.Texture(side,side,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.Upload(wall.Get(),d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    using Texel=std::array<uint16_t,4>;
    const auto layer=[&](UINT from,UINT to,D3D12_RESOURCE_STATES after) {   // an effect over these columns, nothing elsewhere
        std::vector<Texel> texels(side*side,Texel{0,0,0,0});
        for(UINT y=0;y<side;++y)for(UINT x=from;x<to;++x)texels[y*side+x]=Texel{Half(1),Half(.5f),Half(.25f),Half(1)};
        auto texture=gpu.Texture(side,side,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(texture.Get(),texels,after);return texture;
    };
    const auto ps=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,copied=D3D12_RESOURCE_STATE_COPY_SOURCE;
    const auto drawnEffect=layer(96,128,ps),drawnNone=layer(0,0,ps),ownNone=layer(0,0,copied),ownEffect=layer(160,192,copied);
    const auto effects=[&](ID3D12Resource* a,ID3D12Resource* b,D3D12_RESOURCE_STATES state,int slot) {
        ID3D12Resource* const pair[2]={a,b};const D3D12_RESOURCE_STATES states[2]={state,state};
        bool kept=false;gpu.Record([&]{kept=warp.Effects(gpu.device.Get(),gpu.list.Get(),pair,states,slot,0);});
        Check(kept,"the effects layer read into its eye's blocks");
    };
    effects(drawnEffect.Get(),drawnNone.Get(),ps,1);     // the drawn eye: slot 1
    effects(ownNone.Get(),ownEffect.Get(),copied,0);     // this eye: slot 0, its layer left as the game left it
    const float q=std::atan(1.f);const Fov square{-q,q,-q,q};
    framevr::TurnDepth still;still.format=DXGI_FORMAT_R32_FLOAT;still.nearPlane=nearPlane;still.drawnFov=still.nowFov=square;
    still.move=framevr::TurnMove({0,0,0},{0,0,0,1},{0,0,0},{0,0,0,1});still.depth=wall.Get();
    framevr::TurnDepth drawn=still;drawn.image=picture.Get();drawn.apart=.125f;
    drawn.move=framevr::TurnMove({0,0,0},{0,0,0,1},{.125f,0,0},{0,0,0,1});
    drawn.keep=framevr::TurnEdgePixels;drawn.keepEffects=.5f;
    std::vector<uint32_t> out;
    const auto from=[&](UINT a,UINT b,int shift,bool drawnEye,const char* what) {
        bool same=true;
        for(UINT y=0;y<side;y+=7)for(UINT x=a;x<b;++x) {
            const uint32_t want=drawnEye?other(x+shift,y):texel(x,y);
            if(!Same(out[y*side+x],want)){if(same)std::printf("%s: pixel %u,%u = %08x, expected %08x\n",what,x,y,out[y*side+x],want);same=false;}
        }
        Check(same,what);
    };
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,0,&still,&drawn);
    from(2,88,4,true,"away from the effects the drawn eye's picture");
    from(94,122,0,false,"where the drawn eye's effect would land, this eye's own");
    from(128,156,4,true,"between them the drawn eye's again");
    from(162,190,0,false,"where this eye's own effect is, its own");
    from(196,250,4,true,"past them the drawn eye's again");
    drawn.keepEffects=1.5f;   // above the effects' level: none count
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,0,&still,&drawn);
    from(2,250,4,true,"effects below the level keep nothing");
    drawn.keepEffects=.5f;warp.ForgetEffects(0);warp.ForgetEffects(1);
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,0,&still,&drawn);
    from(2,250,4,true,"with no effects found, effects only keeps nothing - not everything");
    std::printf("Effects GPU pass passed (%s target): this eye's own kept where either eye's effects layer has something, the drawn eye's elsewhere\n",typeless?"typeless":"typed");
}

// Rims (TurnRimPixels): a 64-pixel picture, a post 2 units away in columns
// 24-39 against the void (depth 0), the drawn eye half a unit to the left (the
// post goes 8 px, the void none). The two columns either side of the post,
// 22-23 and 40-41, are its rim in the picture but have the void's depth. They
// go with the post; left with the void, the right one stood 8 px from it.
static void Rims(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr)&&warp.DepthReady()&&warp.RimReady(),"pipelines");
    constexpr UINT side=64;constexpr float nearPlane=1;
    const auto texel=[](UINT x,UINT y){return uint32_t(x*4)|uint32_t(y*4)<<8|0x80u<<16|0xffu<<24;};
    const auto other=[](UINT x,UINT y){return uint32_t(x*4)|uint32_t(y*4)<<8|0x30u<<16|0xffu<<24;};
    std::vector<uint32_t> own(side*side),pixels(side*side),d(side*side);
    for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x) {
        own[y*side+x]=texel(x,y);pixels[y*side+x]=other(x,y);
        const float v=x>=24&&x<40?nearPlane/2:0.f;std::memcpy(&d[y*side+x],&v,4);
    }
    auto source=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto picture=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto depth=gpu.Texture(side,side,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.Upload(source.Get(),own,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.Upload(picture.Get(),pixels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.Upload(depth.Get(),d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const float q=std::atan(1.f);const Fov square{-q,q,-q,q};
    framevr::TurnDepth drawn;drawn.format=DXGI_FORMAT_R32_FLOAT;drawn.nearPlane=nearPlane;drawn.drawnFov=drawn.nowFov=square;
    drawn.move=framevr::TurnMove({0,0,0},{0,0,0,1},{.5f,0,0},{0,0,0,1});drawn.depth=depth.Get();drawn.image=picture.Get();drawn.apart=.5f;
    std::vector<uint32_t> out;
    const auto from=[&](UINT a,UINT b,int shift,bool drawnEye,const char* what) {
        bool same=true;
        for(UINT y=0;y<side;++y)for(UINT x=a;x<b;++x) {
            const uint32_t want=drawnEye?other(x+shift,y):texel(x,y);
            if(!Same(out[y*side+x],want)){if(same)std::printf("%s: pixel %u,%u = %08x, expected %08x\n",what,x,y,out[y*side+x],want);same=false;}
        }
        Check(same,what);
    };
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,0,nullptr,&drawn);
    from(2,13,0,true,"the void left of the post stays");
    from(16,32,8,true,"the post moves 8 px");
    from(14,16,8,true,"its left rim with it");
    from(32,34,8,true,"its right rim with it, beside it still");
    from(34,42,0,false,"what the post and its rim hid from the drawn eye: this eye's own");
    from(42,60,0,true,"the void right of it stays");
    {   // a beam 2 units away along the bottom (rows 40-63) under the void, the drawn eye
        // also a hair lower: its rows go a thirtieth of a pixel up, the void's not at all.
        // Sorted by the four texels round each point, the row above the beam's rim fell
        // out of both layers - the eye's own older picture through a hole along the ridge.
        std::vector<uint32_t> beam(side*side);
        for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){const float v=y>=40?nearPlane/2:0.f;std::memcpy(&beam[y*side+x],&v,4);}
        auto beamDepth=gpu.Texture(side,side,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(beamDepth.Get(),beam,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        framevr::TurnDepth low=drawn;low.depth=beamDepth.Get();low.move=framevr::TurnMove({0,0,0},{0,0,0,1},{.5f,.002f,0},{0,0,0,1});
        out=gpu.Warp(warp,source.Get(),target.Get(),Identity,1,nullptr,&low);
        bool covered=true;
        for(UINT y=30;y<50;++y)for(UINT x=10;x<50;++x)if(Same(out[y*side+x],texel(x,y))) {
            if(covered)std::printf("a hole along the beam: pixel %u,%u is this eye's own (%08x)\n",x,y,out[y*side+x]);covered=false;
        }
        Check(covered,"no hole along a level edge against the void a hair apart");
    }
    warp.Rims(false);   // as before: the rim with the void
    out=gpu.Warp(warp,source.Get(),target.Get(),Identity,1,nullptr,&drawn);
    from(16,32,8,true,"rims off: the post moves 8 px");
    from(32,40,0,false,"rims off: this eye's own beside it");
    from(40,42,0,true,"rims off: the right rim left 8 px from the post");
    warp.Rims(true);
    std::printf("Rims GPU pass passed (%s target): the rim round a post against the void goes with it, or with the void when off\n",typeless?"typeless":"typed");
}

// The sharper read (TurnWarp::Sharpen): a 64-pixel picture of vertical
// stripes, 4 px a period, turned half a pixel. Bilinear averages each two
// neighbours; Catmull-Rom keeps more of the stripes' contrast. A whole-pixel
// turn reads every texel as it is either way, and a hard step from 64 to 192
// neither undershoots nor overshoots (kept within the nearest texels).
static void Sharpness(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr),"pipeline");
    constexpr UINT side=64;
    auto source=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto step=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto grey=[](uint32_t v){return v|v<<8|v<<16|0xffu<<24;};
    constexpr uint32_t stripe[4]={40,140,240,140};
    std::vector<uint32_t> stripes(side*side),steps(side*side);
    for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){stripes[y*side+x]=grey(stripe[x%4]);steps[y*side+x]=grey(x<32?64:192);}
    gpu.Upload(source.Get(),stripes,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.Upload(step.Get(),steps,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const std::array<float,9> half{1,0,.5f/side, 0,1,0, 0,0,1},two{1,0,2.f/side, 0,1,0, 0,0,1};
    const auto contrast=[&](const std::vector<uint32_t>& out){   // the red lane's range over the middle of row 32
        int lo=255,hi=0;for(UINT x=16;x<48;++x){const int r=out[32*side+x]&255;lo=std::min(lo,r);hi=std::max(hi,r);}
        return hi-lo;
    };
    int slot=0;
    warp.Sharpen(false);const int soft=contrast(gpu.Warp(warp,source.Get(),target.Get(),half,slot^=1));
    warp.Sharpen(true);const int sharp=contrast(gpu.Warp(warp,source.Get(),target.Get(),half,slot^=1));
    std::printf("half a pixel: stripes' contrast %d bilinear, %d Catmull-Rom\n",soft,sharp);
    Check(sharp>=soft+soft/8,"Catmull-Rom keeps more of the stripes than bilinear");
    const auto out=gpu.Warp(warp,source.Get(),target.Get(),two,slot^=1);
    bool same=true;
    for(UINT y=0;y<side;++y)for(UINT x=0;x<side-2;++x)if(!Same(out[y*side+x],grey(stripe[(x+2)%4]))){
        if(same)std::printf("whole pixels: pixel %u,%u = %08x, expected %08x\n",x,y,out[y*side+x],grey(stripe[(x+2)%4]));same=false;
    }
    Check(same,"a whole-pixel turn reads every texel as it is");
    const auto edge=gpu.Warp(warp,step.Get(),target.Get(),half,slot^=1);
    bool within=true;
    for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){const int r=edge[y*side+x]&255;if(r<63||r>193){if(within)std::printf("step: pixel %u,%u red %d\n",x,y,r);within=false;}}
    Check(within,"a hard step neither undershoots nor overshoots");
    std::printf("Sharper read passed (%s target): more contrast at half a pixel, whole pixels exact, no ring at a step\n",typeless?"typeless":"typed");
}

// The game's interface laid back over a picture (TurnWarp::Interface), as the
// game's last pass lays it: picture * alpha + colour, in the picture's UNORM
// encoding, its alpha kept; nothing laid where the layer is not.
static void Lay(Gpu& gpu,bool typeless) {
    framevr::TurnWarp warp;
    Check(warp.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr)&&warp.InterfaceReady(),"the interface pass is built");
    Check(framevr::UnormView(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)==DXGI_FORMAT_R8G8B8A8_UNORM&&
          framevr::TypelessFamily(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)==DXGI_FORMAT_B8G8R8A8_TYPELESS&&
          framevr::TypelessFamily(DXGI_FORMAT_R16G16B16A16_FLOAT)==DXGI_FORMAT_UNKNOWN,"the interface's views");
    constexpr UINT side=16;
    const uint32_t scene=0x80c06030u;   // r 0x30, g 0x60, b 0xc0; alpha 0x80, a marker the pass keeps
    using Texel=std::array<uint16_t,4>;
    std::vector<Texel> texels(side*side,Texel{0,0,0,Half(1)});   // the world through everywhere
    for(UINT x=4;x<8;++x)texels[6*side+x]=Texel{Half(.5f),Half(.25f),0,Half(.5f)};   // a label: premultiplied, half the world through
    auto layer=gpu.Texture(side,side,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.Upload(layer.Get(),texels,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const auto lane=[](uint32_t c,int k){return float((c>>k*8)&255)/255;};
    const uint32_t label=[&]{
        const float rgb[3]={.5f,.25f,0};uint32_t c=scene&0xff000000u;
        for(int k=0;k<3;++k)c|=uint32_t(std::lround((lane(scene,k)*.5f+rgb[k])*255))<<k*8;
        return c;
    }();
    int at=0;
    const auto lay=[&](const std::array<float,9>& h) {
        auto target=gpu.Texture(side,side,typeless?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
        gpu.Upload(target.Get(),std::vector<uint32_t>(side*side,scene),D3D12_RESOURCE_STATE_RENDER_TARGET);
        const int slot=at;at=(at+1)%4;
        return gpu.Drawn(target.Get(),[&]{Check(warp.Interface(gpu.device.Get(),gpu.list.Get(),layer.Get(),target.Get(),h,slot),"interface");});
    };
    const auto expect=[&](const std::vector<uint32_t>& out,int from,int to,const char* what) {
        bool same=true;
        for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x) {
            const uint32_t want=y==6&&int(x)>=from&&int(x)<to?label:scene;
            if(!Same(out[y*side+x],want)||(out[y*side+x]>>24)!=(scene>>24)) {
                if(same)std::printf("%s: pixel %u,%u = %08x, expected %08x\n",what,x,y,out[y*side+x],want);same=false;
            }
        }
        Check(same,what);
    };
    expect(lay(framevr::TurnIdentity),4,8,"the label over the picture as the game lays it; the rest and the alpha as they were");
    expect(lay({1,0,2.f/side, 0,1,0, 0,0,1}),2,6,"turned: each pixel from where the homography reads the layer");
    expect(lay({1,0,.5f, 0,1,0, 0,0,1}),0,0,"nothing laid where the layer is not");
    // An sRGB view would blend in linear light: never taken.
    auto srgb=gpu.Texture(side,side,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    bool laid=true;gpu.Record([&]{laid=warp.Interface(gpu.device.Get(),gpu.list.Get(),layer.Get(),srgb.Get(),framevr::TurnIdentity,0);});
    Check(!laid,"an sRGB-typed target is refused");
    std::printf("Interface GPU pass passed (%s target): blend, turned, outside the layer, sRGB refused\n",typeless?"typeless":"typed");
}

int main(int argc,char** argv) {try {
    const bool typeless=argc>1&&!std::strcmp(argv[1],"--typeless");
    Math();Gpu gpu;Precompiled(gpu);Turn(gpu,typeless);Move(gpu,typeless);KeepNear(gpu,typeless);KeepEffects(gpu,typeless);Rims(gpu,typeless);Sharpness(gpu,typeless);Lay(gpu,typeless);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
