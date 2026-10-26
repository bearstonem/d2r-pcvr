#include "turn_warp.h"
#include "dxgi_views.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace framevr {
using Microsoft::WRL::ComPtr;
namespace {
// The pictures turned or moved land between texels nearly always. Read
// bilinear, a half-texel step averages two, and fine detail and text lost
// contrast every other frame in each eye: softer than Both eyes (the owner,
// 2026-10-23). Catmull-Rom (TurnWarp::sharp) keeps more of it: five bilinear
// taps (the four corner ones left out), kept within the four nearest texels,
// so no ring or halo past them. On a texel's centre it is that texel.
#define TURN_SHARP R"(
float4 Sharp(Texture2D<float4> t,SamplerState s,float2 uv) {
    float w,h;t.GetDimensions(w,h);
    const float2 size=float2(w,h),at=uv*size,first=floor(at-.5)+.5,f=at-first;
    const float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
    const float2 w12=w1+w2,mid=(first+w2/w12)/size,lo=(first-1)/size,hi=(first+2)/size;
    const float4 c=(t.SampleLevel(s,float2(mid.x,lo.y),0)*(w12.x*w0.y)+t.SampleLevel(s,float2(lo.x,mid.y),0)*(w0.x*w12.y)
                   +t.SampleLevel(s,mid,0)*(w12.x*w12.y)+t.SampleLevel(s,float2(hi.x,mid.y),0)*(w3.x*w12.y)
                   +t.SampleLevel(s,float2(mid.x,hi.y),0)*(w12.x*w3.y))
                  /(w12.x*w0.y+w0.x*w12.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y);
    const int2 last=int2(size)-1,a=clamp(int2(first-.5),0,last),b=clamp(int2(first+.5),0,last);
    const float4 p=t.Load(int3(a,0)),q=t.Load(int3(b.x,a.y,0)),r=t.Load(int3(a.x,b.y,0)),u=t.Load(int3(b,0));
    return clamp(c,min(min(p,q),min(r,u)),max(max(p,q),max(r,u)));
}
)"
constexpr char Shader[]=R"(
Texture2D<float4> drawn : register(t0);
SamplerState edge : register(s0);
// TurnHomography's rows: this frame's image coordinates to the drawn image's;
// filter.x: 1 Catmull-Rom (Sharp), 0 bilinear
cbuffer Turn : register(b0) {float4 row0;float4 row1;float4 row2;float4 filter;};
)" TURN_SHARP R"(
float4 Read(float2 uv){return filter.x>0?Sharp(drawn,edge,uv):drawn.SampleLevel(edge,uv,0);}
struct Pixel {float4 position:SV_Position;float2 uv:TEXCOORD0;};
Pixel VS(uint id:SV_VertexID) {
    Pixel p;p.uv=float2(id==1?2:0,id==2?2:0);
    p.position=float4(p.uv.x*2-1,1-p.uv.y*2,0,1);return p;
}
float4 PS(Pixel p):SV_Target {
    const float3 at=float3(p.uv,1);
    const float3 h=float3(dot(row0.xyz,at),dot(row1.xyz,at),dot(row2.xyz,at));
    if(h.z<=1e-6)return float4(0,0,0,1);   // behind the drawn eye: nothing it saw
    return Read(h.xy/h.z);   // past its edge: the edge drawn out
}
// The game's interface laid over (Interface): blended as dest * a + rgb.
float4 Layer(Pixel p):SV_Target {
    const float3 at=float3(p.uv,1);
    const float3 h=float3(dot(row0.xyz,at),dot(row1.xyz,at),dot(row2.xyz,at));
    const float2 uv=h.xy/max(h.z,1e-6);
    if(h.z<=1e-6||any(uv<0)||any(uv>1))return float4(0,0,0,1);   // none of the layer here: the picture as it is
    const float4 l=Read(uv);
    return float4(l.rgb,saturate(l.a));
}
)";
constexpr UINT TurnConstants=16;
// The grid with the scene's depth (TurnDepth).
constexpr char DepthShader[]=R"(
Texture2D<float4> drawn : register(t0);
Texture2D<float> drawnDepth : register(t1);
Texture2D<float4> own : register(t2);       // the drawn eye's grid: this eye's own picture as its grid left it (TurnDepth::keep)
Texture2D<float> ownDepth : register(t3);   // and that grid's depth there, 0 where it drew nothing
Texture2D<float> nearSource : register(t4);   // the nearest depth within reach (NearShader): the drawn eye's, a block a texel
Texture2D<float> nearTarget : register(t5);   // this eye's, from that grid's depth
Texture2D<float> effectsSource : register(t6);   // the strongest of the game's effects layer round each block (Effects): the drawn eye's
Texture2D<float> effectsTarget : register(t7);   // this eye's, as it was last drawn
SamplerState edge : register(s0);
cbuffer Move : register(b0) {
    float4 move0;float4 move1;float4 move2;   // TurnMove's rows
    float4 drawnTan;   // tangents of the drawn eye's FOV: left, right, down, up
    float4 nowTan;     // this frame's
    float4 depthInfo;  // near plane, the depth's drawn part x, y
    uint4 grid;        // cells across, down; z: 1 Catmull-Rom (Sharp), 0 bilinear
    float4 carry0;float4 carry1;float4 carry2;   // the carried's rows (TurnDepth::carry)
    float4 reach;      // nearer than x: carried; farther than y: the world's; z: the drawn eye's edge scale
    float4 keep;       // x: parallax pixels a step in the depth buffer, 0 off; y: within how many the surface is the same;
                       // z: only within reach of something nearer by this many, 0 everywhere
    float4 nearInfo;   // x, y: the picture's coordinates to the near blocks; z, w: this frame's pixels to them
    float4 effects;    // x, y: the effects' blocks across, down; z: from what level they count, 0 off
};
)" TURN_SHARP R"(
// split: 0 the whole cell; a cell across an edge as two layers, each keeping
// its own pixels: > 0 the nearer, those nearer than it; < 0 the rest.
struct Pixel {float4 position:SV_Position;float2 uv:TEXCOORD0;nointerpolation float split:TEXCOORD1;};
static const float2 Corner[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
static const float Ahead=1e-3;   // nearer than this to this frame's eye: cut off
static const float EdgePixels=2;   // TurnEdgePixels
// The nearest of the texels round a point (reverse-Z: the largest), so an
// edge stays with what is in front; the sky (0) far, not infinite.
float Nearest(float2 uv) {
    const float4 d4=drawnDepth.GatherRed(edge,uv*depthInfo.yz);
    return max(max(max(d4.x,d4.y),max(d4.z,d4.w)),1e-6);
}
// The texel a point of the picture lies in. A cell across an edge sorts each
// pixel into its two layers by this one texel: by the four round it (Nearest)
// each layer, a fraction of a texel apart, could sort a texel out of both, and
// it was left a hole - the eye's own older picture through it, a stripe off
// a roof's ridge (turn captures, 2026-10-25).
float Texel(float2 uv) {
    float w,h;drawnDepth.GetDimensions(w,h);
    return drawnDepth.Load(int3(min(int2(uv*depthInfo.yz*float2(w,h)),int2(w,h)-1),0));
}
// The farthest and the nearest of them.
float2 Range(float2 uv) {
    const float4 d4=drawnDepth.GatherRed(edge,uv*depthInfo.yz);
    return max(float2(min(min(d4.x,d4.y),min(d4.z,d4.w)),max(max(d4.x,d4.y),max(d4.z,d4.w))),1e-6);
}
// layer (the instance): 0 every cell; 1 only the farther part of a cell across an edge.
Pixel VS(uint id:SV_VertexID,uint layer:SV_InstanceID) {
    const uint cell=id/6;
    const float2 at=float2(cell%grid.x,cell/grid.x),size=float2(grid.xy),corner=Corner[id%6];
    const float2 uv=(at+corner)/size;
    float d,split=0;
    if(reach.z>0) {
        // The drawn eye's picture: its texels at the cell's corners and middle.
        // Across an edge in depth the cell goes as two flat layers, the nearest
        // depth's and the farthest's, and each pixel in the one its own depth is
        // nearer: none is lost and none drawn out over the edge.
        const float2 a=Range(at/size),b=Range((at+float2(1,0))/size),c=Range((at+float2(0,1))/size),f=Range((at+1)/size),m=Range((at+.5)/size);
        const float back=min(min(min(a.x,b.x),min(c.x,f.x)),m.x),front=max(max(max(a.y,b.y),max(c.y,f.y)),m.y);
        d=corner.y>0?(corner.x>0?f.y:c.y):(corner.x>0?b.y:a.y);   // Nearest(uv)
        if((front-back)*reach.z>EdgePixels){split=(front+back)*.5;d=layer?back:front;if(layer)split=-split;}
    }
    else d=Nearest(uv);
    Pixel o;o.uv=uv;o.split=split;
    if(layer&&split==0){o.position=float4(0,0,0,-1);return o;}   // no farther part: outside the clip volume
    const float away=depthInfo.x/d;
    const float3 p=float3(lerp(drawnTan.x,drawnTan.y,uv.x),lerp(drawnTan.w,drawnTan.z,uv.y),-1)*away;
    const float3 world=float3(dot(move0.xyz,p)+move0.w,dot(move1.xyz,p)+move1.w,dot(move2.xyz,p)+move2.w);
    const float3 carried=float3(dot(carry0.xyz,p)+carry0.w,dot(carry1.xyz,p)+carry1.w,dot(carry2.xyz,p)+carry2.w);
    const float3 q=lerp(carried,world,saturate((away-reach.x)/max(reach.y-reach.x,1e-6)));
    // this frame's projection, its depth reversed: nearer is larger
    const float w=-q.z;
    o.position=float4(2*(q.x-nowTan.x*w)/(nowTan.y-nowTan.x)-w,w-2*(nowTan.w*w-q.y)/(nowTan.w-nowTan.z),Ahead,w);
    return o;
}
// Something nearer than this surface by more than keep.z pixels of parallax
// within reach: as the drawn eye saw it round here (where its glow was
// left), or as this eye did (where its glow belongs).
bool NearThing(Pixel p,float there) {
    const float source=nearSource.Load(int3(p.uv*nearInfo.xy,0)),target=nearTarget.Load(int3(p.position.xy*nearInfo.zw,0));
    return (source-Nearest(p.uv))*reach.z>keep.z||(target-there)*keep.x>keep.z;
}
// Something of the game's effects layer round here: as the drawn eye drew
// it, round where this pixel comes from (where it was left), or as this eye
// did, round here (where it belongs).
bool Effect(Pixel p) {
    const float2 here=p.position.xy*nearInfo.zw/nearInfo.xy;   // this frame's picture coordinates
    const float source=effectsSource.Load(int3(p.uv*depthInfo.yz*effects.xy,0)),target=effectsTarget.Load(int3(here*depthInfo.yz*effects.xy,0));
    return max(source,target)>=effects.z;
}
float4 PS(Pixel p):SV_Target {
    if(p.split!=0&&(Texel(p.uv)>abs(p.split))!=(p.split>0))discard;   // the other layer's pixel
    if(keep.x>0) {   // where this eye's own picture shows the same surface, it stays (round near things: keep.z; effects: effects.z)
        const int3 at=int3(p.position.xy,0);
        const float there=ownDepth.Load(at);
        const bool where=keep.z<=0&&effects.z<=0||keep.z>0&&NearThing(p,there)||effects.z>0&&Effect(p);
        if(there>0&&abs(p.position.z-there)*keep.x<=keep.y&&where)return own.Load(at);
    }
    return grid.z?Sharp(drawn,edge,p.uv):drawn.SampleLevel(edge,p.uv,0);
}
)";
constexpr UINT DepthConstants=56;
// TurnDepth::keepNear: the nearest depth in each block (Reduce), then the
// nearest within reach of each block (Spread, across then down).
constexpr char NearShader[]=R"(
Texture2D<float> input : register(t0);
SamplerState edge : register(s0);
cbuffer Near : register(b0) {
    float4 area;     // the target's width and height in pixels; the depth's drawn part x, y
    float4 spread;   // its direction x, y
    uint4 limits;    // blocks across, down; the reach in blocks
};
static const int Block=16;   // TurnNearBlock
float4 VS(uint id:SV_VertexID):SV_Position {
    const float2 uv=float2(id==1?2:0,id==2?2:0);
    return float4(uv.x*2-1,1-uv.y*2,0,1);
}
// The block's nearest depth (reverse-Z: the largest).
float Reduce(float4 position:SV_Position):SV_Target {
    const float2 first=floor(position.xy)*Block;
    float nearest=0;
    [unroll] for(int y=1;y<Block;y+=2)[unroll] for(int x=1;x<Block;x+=2) {
        const float4 d4=input.GatherRed(edge,(first+float2(x,y))/area.xy*area.zw);
        nearest=max(nearest,max(max(d4.x,d4.y),max(d4.z,d4.w)));
    }
    return nearest;
}
float Spread(float4 position:SV_Position):SV_Target {
    const int2 at=int2(position.xy),step=int2(spread.xy),size=int2(limits.xy);
    float nearest=input.Load(int3(at,0));
    for(int i=1;i<=int(limits.z);++i) {
        const int2 a=at+step*i,b=at-step*i;
        if(all(a<size))nearest=max(nearest,input.Load(int3(a,0)));
        if(all(b>=0))nearest=max(nearest,input.Load(int3(b,0)));
    }
    return nearest;
}
)";
constexpr UINT NearConstants=12;
// TurnWarp::Fill: beside anything near, the eye not drawn sees a strip the
// drawn eye could not (its grid reached nothing there), and keeps its own
// picture from a frame back there. A monster that stood there a frame ago
// and has moved since was left in it: a ghost beside it in one eye (the
// owner, 2026-10-23). Such a strip is always of what is behind - the drawn
// eye's view of it was blocked by something nearer - so where this eye's own
// picture there is nearer than the farther end of the strip along the row
// (the drawn eye's), it is something since gone: the picture beside that
// end instead (filled from what is behind, as depth-image-based rendering
// does). Where it shows what is behind, its own real texture stays. The
// depth shader's root signature: the picture at t0, the drawn eye's grid's
// depth at t1, this eye's own grid's at t2.
constexpr char HoleShader[]=R"(
Texture2D<float4> picture : register(t0);   // the eye not drawn as the grids left it (a copy)
Texture2D<float> covered : register(t1);    // the drawn eye's grid's depth: 0 where it reached nothing
Texture2D<float> ownDepth : register(t2);   // this eye's own grid's: 0 where it drew nothing
// x: how far along the row to look either side, pixels; y: parallax pixels a
// step in depth; z: within how many it shows what is behind
cbuffer Holes : register(b0) {float4 holes;};
float4 VS(uint id:SV_VertexID):SV_Position {
    const float2 uv=float2(id==1?2:0,id==2?2:0);
    return float4(uv.x*2-1,1-uv.y*2,0,1);
}
float4 PS(float4 position:SV_Position):SV_Target {
    const int2 at=int2(position.xy);
    if(covered.Load(int3(at,0))>0)discard;   // the drawn eye's picture: as it is
    const float own=ownDepth.Load(int3(at,0));
    if(own<=0)discard;   // nothing of its own with a depth here: as the turn left it
    uint width,height;covered.GetDimensions(width,height);
    float left=0,right=0;int2 l=at,r=at;   // the strip's ends: the drawn eye's nearest pixels either side
    for(int k=1;k<=int(holes.x)&&(left<=0||right<=0);++k) {
        if(left<=0&&at.x-k>=0){const float d=covered.Load(int3(at.x-k,at.y,0));if(d>0){left=d;l=int2(at.x-k,at.y);}}
        if(right<=0&&at.x+k<int(width)){const float d=covered.Load(int3(at.x+k,at.y,0));if(d>0){right=d;r=int2(at.x+k,at.y);}}
    }
    if(left<=0&&right<=0)discard;
    const bool fromLeft=right<=0||left>0&&left<=right;   // the farther end (reverse-Z: the smaller)
    if((own-(fromLeft?left:right))*holes.y<=holes.z)discard;   // its own shows what is behind: it stays
    return picture.Load(int3(fromLeft?l:r,0));
}
)";
constexpr float HoleReach=128;   // how far along the row a strip's ends are looked for: a 4096-wide eye's pixels
// TurnWarp::Rims: a depth, the void within TurnRimPixels of something taken
// as the nearest depth there (TurnRimPixels). Through the turn's root
// signature: the depth at t0.
constexpr char RimShader[]=R"(
Texture2D<float> input : register(t0);   // reverse-Z: nearer is larger, 0 the void
static const int Rim=2;   // TurnRimPixels
float4 VS(uint id:SV_VertexID):SV_Position {
    const float2 uv=float2(id==1?2:0,id==2?2:0);
    return float4(uv.x*2-1,1-uv.y*2,0,1);
}
float PS(float4 position:SV_Position):SV_Target {
    const int2 at=int2(position.xy);
    const float d=input.Load(int3(at,0));
    if(d>0)return d;   // anything with depth keeps its own
    uint width,height;input.GetDimensions(width,height);
    const int2 last=int2(width,height)-1;
    float nearest=0;
    [unroll] for(int y=-Rim;y<=Rim;++y)[unroll] for(int x=-Rim;x<=Rim;++x)
        if(x*x+y*y<=Rim*Rim+1)nearest=max(nearest,input.Load(int3(clamp(at+int2(x,y),0,last),0)));
    return nearest;
}
)";
// TurnDepth::keepEffects: the strongest channel of one of the game's effects
// targets over each block, blended as the larger into the block (Effects),
// then spread as the near blocks are (NearShader's Spread).
constexpr char EffectsShader[]=R"(
Texture2D<float4> input : register(t0);
SamplerState edge : register(s0);
cbuffer Near : register(b0) {float4 area;float4 spread;uint4 limits;};   // NearShader's
static const int Block=16;   // TurnNearBlock
// Four texels a sample, between them.
float Reduce(float4 position:SV_Position):SV_Target {
    const float2 first=floor(position.xy)*Block;
    float strongest=0;
    [unroll] for(int y=1;y<Block;y+=2)[unroll] for(int x=1;x<Block;x+=2) {
        const float4 c=input.SampleLevel(edge,(first+float2(x,y))/area.xy*area.zw,0);
        strongest=max(strongest,max(max(c.r,c.g),max(c.b,c.a)));
    }
    return strongest;
}
)";
constexpr float Ahead=1e-3f;   // the depth shader's: a grid point's depth-buffer value is Ahead / its distance
// The source heap: each eye's slot (Render's `slot`) holds its own grid's
// table (its image, its depth, four none) and the drawn eye's (its picture,
// its depth, this eye's own picture and depth as the first grid left them,
// the drawn eye's and this eye's near blocks, then effects blocks), then the
// near passes' block texture between; after both slots, two an eye for the
// interface (Interface's `at`), then four an eye for Effects (its layer's
// two targets, the two between), then a grid's table an eye for the strips
// (Fill), then two an eye for the rims (this eye's depth, the drawn eye's).
constexpr UINT GridDescriptors=8,SlotDescriptors=2*GridDescriptors+1,InterfaceDescriptors=2*SlotDescriptors,EffectsDescriptors=InterfaceDescriptors+4,
               HoleDescriptors=EffectsDescriptors+8,RimDescriptors=HoleDescriptors+2*GridDescriptors,HeapDescriptors=RimDescriptors+4;
// The shaders' bytecode, compiled once a process: the compiles were about 106
// of the 112 ms Initialize took on the render thread at every new session
// (RTX 5090 / Ryzen 9 5950X, 2026-10-20). Copied out of the compiler's blobs,
// which must not outlive d3dcompiler_47. A shader that fails to compile stays
// failed for the process; its pass is then unavailable as before.
struct Compiled {
    bool compiler=false;
    std::vector<uint8_t> vs,ps,layer,depthVs,depthPs,nearVs,nearReduce,nearSpread,effectsReduce,holeVs,holePs,rimVs,rimPs;
    std::string errors,depthErrors,nearErrors,effectsErrors,holeErrors,rimErrors;   // the first failure's messages of each group
};
const Compiled& Shaders() {
    static std::once_flag once;static Compiled c;
    std::call_once(once,[]{
        const HMODULE module=LoadLibraryW(L"d3dcompiler_47.dll");
        const auto compile=module?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(module,"D3DCompile")):nullptr;
        if(compile)try {
            c.compiler=true;
            const auto build=[&](const char* source,size_t size,const char* name,const char* entry,const char* target,std::vector<uint8_t>& out,std::string* errors) {
                ComPtr<ID3DBlob> code,messages;
                if(SUCCEEDED(compile(source,size,name,nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&messages))) {
                    const auto* p=static_cast<const uint8_t*>(code->GetBufferPointer());out.assign(p,p+code->GetBufferSize());return;
                }
                if(errors&&errors->empty()&&messages)*errors=static_cast<const char*>(messages->GetBufferPointer());
            };
            build(Shader,sizeof(Shader)-1,"turn-warp","VS","vs_5_0",c.vs,&c.errors);
            build(Shader,sizeof(Shader)-1,"turn-warp","PS","ps_5_0",c.ps,&c.errors);
            build(Shader,sizeof(Shader)-1,"turn-warp","Layer","ps_5_0",c.layer,nullptr);
            build(DepthShader,sizeof(DepthShader)-1,"turn-depth","VS","vs_5_0",c.depthVs,&c.depthErrors);
            build(DepthShader,sizeof(DepthShader)-1,"turn-depth","PS","ps_5_0",c.depthPs,&c.depthErrors);
            build(NearShader,sizeof(NearShader)-1,"turn-near","VS","vs_5_0",c.nearVs,&c.nearErrors);
            build(NearShader,sizeof(NearShader)-1,"turn-near","Reduce","ps_5_0",c.nearReduce,&c.nearErrors);
            build(NearShader,sizeof(NearShader)-1,"turn-near","Spread","ps_5_0",c.nearSpread,&c.nearErrors);
            build(EffectsShader,sizeof(EffectsShader)-1,"turn-effects","Reduce","ps_5_0",c.effectsReduce,&c.effectsErrors);
            build(HoleShader,sizeof(HoleShader)-1,"turn-holes","VS","vs_5_0",c.holeVs,&c.holeErrors);
            build(HoleShader,sizeof(HoleShader)-1,"turn-holes","PS","ps_5_0",c.holePs,&c.holeErrors);
            build(RimShader,sizeof(RimShader)-1,"turn-rims","VS","vs_5_0",c.rimVs,&c.rimErrors);
            build(RimShader,sizeof(RimShader)-1,"turn-rims","PS","ps_5_0",c.rimPs,&c.rimErrors);
        }catch(...) {}   // out of memory: what did not finish stays unavailable
        if(module)FreeLibrary(module);
    });
    return c;
}
D3D12_SHADER_BYTECODE Code(const std::vector<uint8_t>& code){return {code.data(),code.size()};}
}
bool TurnWarp::Initialize(ID3D12Device* device,DXGI_FORMAT targetFormat,void (*log)(const char*)) {
    if(pipeline)return true;
    auto fail=[&](const char* why){*this={};if(log){char b[160];std::snprintf(b,sizeof b,"Frame OpenXR: turning the eye not drawn unavailable (%s); by turns it goes at its own pose",why);log(b);}return false;};
    const auto& shaders=Shaders();
    const auto d12=GetModuleHandleW(L"d3d12.dll");
    const auto serialize=d12?reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12,"D3D12SerializeRootSignature")):nullptr;
    if(!shaders.compiler||!serialize)return fail("no shader compiler or root serializer");
    ComPtr<ID3DBlob> errors,signature;
    if(shaders.vs.empty()||shaders.ps.empty()){if(log&&!shaders.errors.empty())log(shaders.errors.c_str());return fail("shader compile failed");}
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={1,&range};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,TurnConstants};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;sampler.MaxLOD=D3D12_FLOAT32_MAX;
    desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
    if(FAILED(serialize(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors))||
       FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root))))
        return fail("no root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature=root.Get();state.VS=Code(shaders.vs);state.PS=Code(shaders.ps);
    state.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    state.SampleMask=UINT_MAX;state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;
    state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets=1;state.RTVFormats[0]=targetFormat;state.SampleDesc.Count=1;
    if(FAILED(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&pipeline))))return fail("no pipeline for the eye format");
    // The interface pass is optional: without it By turns leaves the interface in the picture.
    if(!shaders.layer.empty()) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC layer=state;layer.PS=Code(shaders.layer);
        auto& blend=layer.BlendState.RenderTarget[0];
        blend.BlendEnable=TRUE;blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ZERO;blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE;
        layer.RTVFormats[0]=UnormView(targetFormat);
        if(FAILED(device->CreateGraphicsPipelineState(&layer,IID_PPV_ARGS(&layerPipeline))))layerPipeline.Reset();
    }
    if(!layerPipeline&&log)log("Frame OpenXR: by turns - laying the game's interface over each eye unavailable; it stays in the picture");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=HeapDescriptors;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&sourceHeap))))return fail("no descriptor heap");
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=1;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&targetHeap))))return fail("no target heap");
    format=targetFormat;
    if(log)log("Frame OpenXR: eyes by turns - the eye not drawn is turned to each frame's head on the GPU");
    // The depth pass is optional: without it By turns draws pairs while the hero moves.
    const auto depthFail=[&](const char* why){
        depthRoot.Reset();depthPipeline.Reset();depthHeap.Reset();
        if(log){char b[160];std::snprintf(b,sizeof b,"Frame OpenXR: by turns - moving the eye with the scene depth unavailable (%s)",why);log(b);}
        return true;
    };
    errors.Reset();signature.Reset();
    if(shaders.depthVs.empty()||shaders.depthPs.empty()){if(log&&!shaders.depthErrors.empty())log(shaders.depthErrors.c_str());return depthFail("shader compile failed");}
    D3D12_DESCRIPTOR_RANGE both{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,GridDescriptors,0,0,0};   // a picture, its depth; this eye's own and its depth; the near blocks; the effects blocks
    D3D12_ROOT_PARAMETER depthParameters[2]{};
    depthParameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    depthParameters[0].DescriptorTable={1,&both};depthParameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    depthParameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    depthParameters[1].Constants={0,0,DepthConstants};depthParameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC depthDesc=desc;depthDesc.pParameters=depthParameters;
    D3D12_STATIC_SAMPLER_DESC depthSampler=sampler;depthSampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;depthDesc.pStaticSamplers=&depthSampler;
    if(FAILED(serialize(&depthDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors))||
       FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&depthRoot))))
        return depthFail("no root signature");
    state.pRootSignature=depthRoot.Get();state.VS=Code(shaders.depthVs);state.PS=Code(shaders.depthPs);
    state.DepthStencilState.DepthEnable=TRUE;state.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    state.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER;state.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    if(FAILED(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&depthPipeline))))return depthFail("no pipeline");
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=2;   // this eye's grid's, the drawn eye's
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&depthHeap))))return depthFail("no depth heap");
    if(log)log("Frame OpenXR: by turns - the eye not drawn can be moved with the scene depth");
    // The strips' pass is optional: without it they keep this eye's own picture as before.
    if(!shaders.holeVs.empty()&&!shaders.holePs.empty()) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC holes=state;holes.VS=Code(shaders.holeVs);holes.PS=Code(shaders.holePs);
        holes.DepthStencilState={};holes.DSVFormat=DXGI_FORMAT_UNKNOWN;
        if(FAILED(device->CreateGraphicsPipelineState(&holes,IID_PPV_ARGS(&holePipeline))))holePipeline.Reset();
    }
    else if(log&&!shaders.holeErrors.empty())log(shaders.holeErrors.c_str());
    if(!holePipeline&&log)log("Frame OpenXR: by turns - filling the strips beside near things unavailable; they keep the eye's own picture");
    // The rims' pass is optional: without it they stay with the void.
    if(!shaders.rimVs.empty()&&!shaders.rimPs.empty()) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC rim{};
        rim.pRootSignature=root.Get();rim.VS=Code(shaders.rimVs);rim.PS=Code(shaders.rimPs);
        rim.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        rim.SampleMask=UINT_MAX;rim.RasterizerState=state.RasterizerState;
        rim.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        rim.NumRenderTargets=1;rim.RTVFormats[0]=DXGI_FORMAT_R32_FLOAT;rim.SampleDesc.Count=1;
        D3D12_DESCRIPTOR_HEAP_DESC targets{};targets.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;targets.NumDescriptors=UINT(rims.size());
        if(FAILED(device->CreateGraphicsPipelineState(&rim,IID_PPV_ARGS(&rimPipeline)))||
           FAILED(device->CreateDescriptorHeap(&targets,IID_PPV_ARGS(&rimTargets)))){rimPipeline.Reset();rimTargets.Reset();}
    }
    else if(log&&!shaders.rimErrors.empty())log(shaders.rimErrors.c_str());
    if(!rimPipeline&&log)log("Frame OpenXR: by turns - moving the rims round things against the void with them unavailable; they stay with the void");
    // The near passes are optional: without them "near things only" keeps none.
    const auto nearFail=[&](const char* why){
        nearRoot.Reset();reducePipeline.Reset();spreadPipeline.Reset();nearTargets.Reset();
        if(log){char b[192];std::snprintf(b,sizeof b,"Frame OpenXR: by turns - finding near things unavailable (%s); \"near things only\" keeps the drawn eye's picture",why);log(b);}
        return true;
    };
    errors.Reset();signature.Reset();
    if(shaders.nearVs.empty()||shaders.nearReduce.empty()||shaders.nearSpread.empty()) {
        if(log&&!shaders.nearErrors.empty())log(shaders.nearErrors.c_str());
        return nearFail("shader compile failed");
    }
    D3D12_ROOT_PARAMETER nearParameters[2]{};
    nearParameters[0]=parameters[0];nearParameters[1]=parameters[1];nearParameters[1].Constants={0,0,NearConstants};
    D3D12_ROOT_SIGNATURE_DESC nearDesc=desc;nearDesc.pParameters=nearParameters;
    if(FAILED(serialize(&nearDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors))||
       FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&nearRoot))))
        return nearFail("no root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC nearState{};
    nearState.pRootSignature=nearRoot.Get();nearState.VS=Code(shaders.nearVs);nearState.PS=Code(shaders.nearReduce);
    nearState.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    nearState.SampleMask=UINT_MAX;nearState.RasterizerState=state.RasterizerState;
    nearState.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    nearState.NumRenderTargets=1;nearState.RTVFormats[0]=DXGI_FORMAT_R32_FLOAT;nearState.SampleDesc.Count=1;
    if(FAILED(device->CreateGraphicsPipelineState(&nearState,IID_PPV_ARGS(&reducePipeline))))return nearFail("no pipeline");
    nearState.PS=Code(shaders.nearSpread);
    if(FAILED(device->CreateGraphicsPipelineState(&nearState,IID_PPV_ARGS(&spreadPipeline))))return nearFail("no pipeline");
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=UINT(nears.size());
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&nearTargets))))return nearFail("no target heap");
    // The effects pass is optional too: without it "effects" keeps none.
    if(shaders.effectsReduce.empty()) {
        if(log&&!shaders.effectsErrors.empty())log(shaders.effectsErrors.c_str());
        if(log)log("Frame OpenXR: by turns - finding the game's effects unavailable (shader compile failed)");
        return true;
    }
    nearState.PS=Code(shaders.effectsReduce);
    auto& larger=nearState.BlendState.RenderTarget[0];
    larger.BlendEnable=TRUE;larger.SrcBlend=larger.DestBlend=D3D12_BLEND_ONE;larger.BlendOp=D3D12_BLEND_OP_MAX;
    larger.SrcBlendAlpha=larger.DestBlendAlpha=D3D12_BLEND_ONE;larger.BlendOpAlpha=D3D12_BLEND_OP_MAX;
    heap.NumDescriptors=UINT(effects.size());
    if(FAILED(device->CreateGraphicsPipelineState(&nearState,IID_PPV_ARGS(&effectsPipeline)))||
       FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&effectsTargets)))) {
        effectsPipeline.Reset();effectsTargets.Reset();
        if(log)log("Frame OpenXR: by turns - finding the game's effects unavailable (no pipeline)");
    }
    return true;
}
bool TurnWarp::Effects(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12Resource* const layer[2],const D3D12_RESOURCE_STATES states[2],int slot,float reach) {
    if(slot<0||slot>1||!EffectsReady()||!layer[0]||!layer[1])return false;
    effectsKept[slot]=false;
    const auto l=layer[0]->GetDesc();
    const UINT across=UINT((l.Width+TurnNearBlock-1)/TurnNearBlock),down=(l.Height+TurnNearBlock-1)/TurnNearBlock;
    const UINT rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if(effects[0]){const auto d=effects[0]->GetDesc();if(d.Width!=across||d.Height!=down)return false;}   // one size a session
    else {
        D3D12_HEAP_PROPERTIES heapProps{};heapProps.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC blocks{};blocks.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;blocks.Width=across;blocks.Height=down;
        blocks.DepthOrArraySize=blocks.MipLevels=1;blocks.Format=DXGI_FORMAT_R32_FLOAT;blocks.SampleDesc.Count=1;blocks.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        auto rtv=effectsTargets->GetCPUDescriptorHandleForHeapStart();
        for(auto& e:effects) {
            if(FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&blocks,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&e)))){effects={};return false;}
            device->CreateRenderTargetView(e.Get(),nullptr,rtv);rtv.ptr+=rtvStep;
        }
    }
    // this eye's four: the layer's two targets, the two between
    const UINT step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=sourceHeap->GetCPUDescriptorHandleForHeapStart();auto gpu=sourceHeap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr+=SIZE_T(step)*(EffectsDescriptors+4*slot);gpu.ptr+=UINT64(step)*(EffectsDescriptors+4*slot);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    D3D12_SHADER_RESOURCE_VIEW_DESC single=srv;single.Format=DXGI_FORMAT_R32_FLOAT;
    for(int k=0;k<4;++k){auto at=cpu;at.ptr+=SIZE_T(step)*k;device->CreateShaderResourceView(k<2?layer[k]:effects[k].Get(),k<2?&srv:&single,at);}
    constexpr auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER in[2]{};UINT ins=0;
    for(int k=0;k<2;++k)if(!(states[k]&readable)) {
        auto& b=in[ins++];b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={layer[k],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,states[k],readable};
    }
    if(ins)list->ResourceBarrier(ins,in);
    const auto target=[&](int k,bool writing) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={effects[k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,writing?readable:D3D12_RESOURCE_STATE_RENDER_TARGET,writing?D3D12_RESOURCE_STATE_RENDER_TARGET:readable};
        list->ResourceBarrier(1,&b);
        auto rtv=effectsTargets->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(rtvStep)*k;
        if(writing)list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        return rtv;
    };
    const auto pass=[&](ID3D12PipelineState* pipelineState,int from,float x,float y) {
        list->SetPipelineState(pipelineState);
        auto table=gpu;table.ptr+=UINT64(step)*from;list->SetGraphicsRootDescriptorTable(0,table);
        uint32_t constants[NearConstants]{};
        const float values[8]={float(l.Width),float(l.Height),1,1,x,y,0,0};
        std::memcpy(constants,values,sizeof values);
        constants[8]=across;constants[9]=down;constants[10]=uint32_t(std::ceil(reach/TurnNearBlock));
        list->SetGraphicsRoot32BitConstants(1,NearConstants,constants,0);
        list->DrawInstanced(3,1,0,0);
    };
    list->SetGraphicsRootSignature(nearRoot.Get());
    ID3D12DescriptorHeap* heaps[]={sourceHeap.Get()};list->SetDescriptorHeaps(1,heaps);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport{0,0,float(across),float(down),0,1};const D3D12_RECT rect{0,0,LONG(across),LONG(down)};
    list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
    // both targets' strongest into the first between, the larger kept
    const float none[4]{};
    list->ClearRenderTargetView(target(2,true),none,0,nullptr);
    pass(effectsPipeline.Get(),0,0,0);pass(effectsPipeline.Get(),1,0,0);
    target(2,false);
    // spread across into the second, then down into this eye's
    target(3,true);pass(spreadPipeline.Get(),2,1,0);target(3,false);
    target(slot,true);pass(spreadPipeline.Get(),3,0,1);target(slot,false);
    for(UINT k=0;k<ins;++k)std::swap(in[k].Transition.StateBefore,in[k].Transition.StateAfter);
    if(ins)list->ResourceBarrier(ins,in);
    effectsKept[slot]=true;
    return true;
}
bool TurnWarp::Fits(ID3D12Resource* source,ID3D12Resource* target) const {
    if(!pipeline||!source||!target)return false;
    const auto s=source->GetDesc(),t=target->GetDesc();
    return RenderTargetCompatible(s.Format,format)&&RenderTargetCompatible(t.Format,format)&&s.SampleDesc.Count==1&&t.SampleDesc.Count==1;
}
bool TurnWarp::Render(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12Resource* source,ID3D12Resource* target,
                      const std::array<float,9>& homography,int slot,const TurnDepth* depth,const TurnDepth* drawn) {
    if(slot<0||slot>1||!Fits(source,target))return false;
    const auto t=target->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    auto source0=sourceHeap->GetCPUDescriptorHandleForHeapStart();
    auto sourceGpu=sourceHeap->GetGPUDescriptorHandleForHeapStart();
    const UINT step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    source0.ptr+=SIZE_T(step)*SlotDescriptors*slot;sourceGpu.ptr+=UINT64(step)*SlotDescriptors*slot;
    device->CreateShaderResourceView(source,&srv,source0);
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=format;rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto rtvHandle=targetHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target,&rtv,rtvHandle);
    list->SetGraphicsRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[]={sourceHeap.Get()};list->SetDescriptorHeaps(1,heaps);
    list->SetGraphicsRootDescriptorTable(0,sourceGpu);
    float rows[TurnConstants]{};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)rows[r*4+c]=homography[r*3+c];
    rows[12]=sharp?1.f:0.f;
    list->SetGraphicsRoot32BitConstants(1,TurnConstants,rows,0);
    list->OMSetRenderTargets(1,&rtvHandle,FALSE,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(t.Width),float(t.Height),0,1};const D3D12_RECT rect{0,0,LONG(t.Width),LONG(t.Height)};
    list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
    list->SetPipelineState(pipeline.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
    const auto usable=[](const TurnDepth* d){return d&&d->depth&&d->nearPlane>0;};
    const bool other=usable(drawn)&&drawn->image&&Fits(drawn->image,target);
    if(!depthPipeline||(!usable(depth)&&!other))return true;
    // The grids over what the turn drew: each its own depth test (this eye's
    // stays readable for the drawn eye's), and this eye's own picture as its
    // grid left it, for the drawn eye's to keep (TurnDepth::keep).
    if(depthTarget){const auto d=depthTarget->GetDesc();if(d.Width!=t.Width||d.Height!=t.Height)return true;}   // one size a session
    else {
        D3D12_HEAP_PROPERTIES heapProps{};heapProps.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=t.Width;d.Height=t.Height;
        d.DepthOrArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_R32_TYPELESS;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=0;
        D3D12_RESOURCE_DESC picture=d;picture.Format=format;picture.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depthTarget)))||
           FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&drawnDepthTarget)))||
           FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&picture,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&ownTurned)))) {
            depthTarget.Reset();drawnDepthTarget.Reset();ownTurned.Reset();return true;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D32_FLOAT;dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        auto handle=depthHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(depthTarget.Get(),&dsv,handle);
        handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        device->CreateDepthStencilView(drawnDepthTarget.Get(),&dsv,handle);
        // TurnDepth::keepNear's blocks: the drawn eye's, this eye's, one between
        if(NearReady()) {
            D3D12_RESOURCE_DESC blocks=d;blocks.Width=(t.Width+TurnNearBlock-1)/TurnNearBlock;blocks.Height=(t.Height+TurnNearBlock-1)/TurnNearBlock;
            blocks.Format=DXGI_FORMAT_R32_FLOAT;blocks.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            auto rtv=nearTargets->GetCPUDescriptorHandleForHeapStart();
            for(auto& n:nears) {
                if(FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&blocks,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&n)))){nears={};break;}
                device->CreateRenderTargetView(n.Get(),nullptr,rtv);rtv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            }
        }
    }
    const auto dsvHandle=[&](int k){auto h=depthHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV))*k;return h;};
    const auto tangents=[](const math::Fov& f,float* out){out[0]=std::tan(f.left);out[1]=std::tan(f.right);out[2]=std::tan(f.down);out[3]=std::tan(f.up);};
    const uint32_t across=(uint32_t(t.Width)+TurnGridStep-1)/TurnGridStep,down=(t.Height+TurnGridStep-1)/TurnGridStep;
    const uint32_t blocksAcross=(uint32_t(t.Width)+TurnNearBlock-1)/TurnNearBlock,blocksDown=(t.Height+TurnNearBlock-1)/TurnNearBlock;
    const auto descriptor=[&](int at){auto view=source0;view.ptr+=SIZE_T(step)*at;return view;};
    D3D12_SHADER_RESOURCE_VIEW_DESC single=srv;single.Format=DXGI_FORMAT_R32_FLOAT;
    // the drawn eye's: how many target pixels a step in its depth parts by, seen from `apart` away
    const auto edgeScale=[&](const TurnDepth& d){return d.apart>0?float(t.Width)/(std::tan(d.nowFov.right)-std::tan(d.nowFov.left))*d.apart/d.nearPlane:0.f;};
    // One grid: its picture and depth into the table at `at` (0 this eye's,
    // GridDescriptors the drawn eye's), over its own depth test `k`; `keep`:
    // this eye's own picture and depth as its grid left them, at at + 2 and
    // 3; `nearOnly`: only within reach of near things, their blocks at 4 and 5;
    // `effectsOn`: also where the game's effects are, their blocks at 6 and 7.
    const auto grid=[&](const TurnDepth& d,ID3D12Resource* image,int at,int k,bool keep,bool nearOnly,bool effectsOn) {
        if(at)device->CreateShaderResourceView(image,&srv,descriptor(at));
        D3D12_SHADER_RESOURCE_VIEW_DESC depthView=srv;depthView.Format=d.format;
        device->CreateShaderResourceView(d.depth,&depthView,descriptor(at+1));
        device->CreateShaderResourceView(keep?ownTurned.Get():nullptr,&srv,descriptor(at+2));   // none: never read
        device->CreateShaderResourceView(keep?depthTarget.Get():nullptr,&single,descriptor(at+3));
        for(int n=0;n<2;++n)device->CreateShaderResourceView(nearOnly?nears[n].Get():nullptr,&single,descriptor(at+4+n));
        for(int n=0;n<2;++n) {   // the drawn eye's, this eye's
            const int eye=n?slot:slot^1;
            device->CreateShaderResourceView(effectsOn&&effectsKept[eye]?effects[eye].Get():nullptr,&single,descriptor(at+6+n));
        }
        list->SetGraphicsRootSignature(depthRoot.Get());
        list->SetPipelineState(depthPipeline.Get());
        auto table=sourceGpu;table.ptr+=UINT64(step)*at;
        list->SetGraphicsRootDescriptorTable(0,table);
        const auto dsv=dsvHandle(k);
        list->OMSetRenderTargets(1,&rtvHandle,FALSE,&dsv);
        list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);   // each grid over the last whole
        uint32_t constants[DepthConstants]{};float values[24]{};
        std::memcpy(values,d.move.data(),sizeof(float)*12);
        tangents(d.drawnFov,values+12);tangents(d.nowFov,values+16);
        values[20]=d.nearPlane;values[21]=d.scale[0];values[22]=d.scale[1];
        std::memcpy(constants,values,sizeof values);constants[24]=across;constants[25]=down;constants[26]=sharp?1:0;
        const bool carries=d.reach[1]>d.reach[0]&&d.reach[0]>0;
        float carried[16]{};
        std::memcpy(carried,carries?d.carry.data():d.move.data(),sizeof(float)*12);
        if(carries){carried[12]=d.reach[0];carried[13]=d.reach[1];}
        if(at)carried[14]=edgeScale(d);
        std::memcpy(constants+28,carried,sizeof carried);
        // its parallax a step in the depth buffer (near plane / distance = depth * near plane / Ahead)
        const auto blocks=effectsOn?effects[0]->GetDesc():D3D12_RESOURCE_DESC{};
        const float same[12]={keep?carried[14]*d.nearPlane/Ahead:0,d.keep,nearOnly?d.keepNear:0,0,
                              float(t.Width)/TurnNearBlock,float(t.Height)/TurnNearBlock,1.f/TurnNearBlock,1.f/TurnNearBlock,
                              float(blocks.Width),float(blocks.Height),effectsOn?d.keepEffects:0,0};
        std::memcpy(constants+44,same,sizeof same);
        list->SetGraphicsRoot32BitConstants(1,DepthConstants,constants,0);
        list->DrawInstanced(6*across*down,carried[14]>0?2:1,0,0);   // the drawn eye's: the farther layers too
    };
    // The nearest depth within `reach` target pixels of each block, from
    // `input` (at in the slot) into nears[out]: a block each, spread across
    // then down through the one between.
    const auto nearBlocks=[&](int input,int out,float drawnX,float drawnY,float reach) {
        const UINT rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const auto pass=[&](ID3D12PipelineState* pipelineState,int from,int to,float x,float y) {
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={nears[to].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
            list->ResourceBarrier(1,&b);
            auto rtv=nearTargets->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(rtvStep)*to;
            list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
            list->SetPipelineState(pipelineState);
            auto table=sourceGpu;table.ptr+=UINT64(step)*from;list->SetGraphicsRootDescriptorTable(0,table);
            uint32_t constants[NearConstants]{};
            const float values[8]={float(t.Width),float(t.Height),drawnX,drawnY,x,y,0,0};
            std::memcpy(constants,values,sizeof values);
            constants[8]=blocksAcross;constants[9]=blocksDown;constants[10]=uint32_t(std::ceil(reach/TurnNearBlock));
            list->SetGraphicsRoot32BitConstants(1,NearConstants,constants,0);
            list->DrawInstanced(3,1,0,0);
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
        };
        const int between=2*GridDescriptors,outAt=GridDescriptors+4+out;   // the drawn eye's table holds nears[0], [1]
        pass(reducePipeline.Get(),input,out,0,0);
        pass(spreadPipeline.Get(),outAt,2,1,0);
        pass(spreadPipeline.Get(),between,out,0,1);
    };
    // Each depth with the void round things taken as theirs (TurnRimPixels),
    // this eye's own into rims[0], the drawn eye's into rims[1]; everything
    // after reads those. A depth of another size than the first keeps its own.
    TurnDepth ownRimmed,drawnRimmed;
    if(rimsOn&&rimPipeline) {
        const auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const UINT rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const int rims0=int(RimDescriptors+2*slot)-int(SlotDescriptors*slot);   // this eye's two, from the slot's start
        const auto rimmed=[&](const TurnDepth* d,int n,TurnDepth& out)->const TurnDepth* {
            const auto size=d->depth->GetDesc();
            auto rtv=rimTargets->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(rtvStep)*n;
            if(rims[n]){const auto r=rims[n]->GetDesc();if(r.Width!=size.Width||r.Height!=size.Height)return d;}   // one size a session
            else {
                D3D12_HEAP_PROPERTIES heapProps{};heapProps.Type=D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=size.Width;r.Height=size.Height;
                r.DepthOrArraySize=r.MipLevels=1;r.Format=DXGI_FORMAT_R32_FLOAT;r.SampleDesc.Count=1;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                if(FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&r,readable,nullptr,IID_PPV_ARGS(&rims[n])))){rims[n].Reset();return d;}
                device->CreateRenderTargetView(rims[n].Get(),nullptr,rtv);
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC depthView=srv;depthView.Format=d->format;
            device->CreateShaderResourceView(d->depth,&depthView,descriptor(rims0+n));
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={rims[n].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,readable,D3D12_RESOURCE_STATE_RENDER_TARGET};
            list->ResourceBarrier(1,&b);
            list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
            const D3D12_VIEWPORT whole{0,0,float(size.Width),float(size.Height),0,1};const D3D12_RECT wholeRect{0,0,LONG(size.Width),LONG(size.Height)};
            list->RSSetViewports(1,&whole);list->RSSetScissorRects(1,&wholeRect);
            list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(rimPipeline.Get());
            auto table=sourceGpu;table.ptr+=UINT64(step)*(rims0+n);list->SetGraphicsRootDescriptorTable(0,table);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
            out=*d;out.depth=rims[n].Get();out.format=DXGI_FORMAT_R32_FLOAT;
            return &out;
        };
        if(usable(depth))depth=rimmed(depth,0,ownRimmed);
        if(other)drawn=rimmed(drawn,1,drawnRimmed);
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
    }
    const bool own=usable(depth);
    if(own)grid(*depth,source,0,0,false,false,false);
    if(other) {
        // Where this eye's own picture shows the same surface as the drawn
        // eye's, it stays: seen from this eye, a frame old. With keepNear only
        // round near things; without the passes for that, nowhere. With
        // keepEffects (also) where the game's effects are, as far as either
        // eye's were found.
        const bool nearOnly=drawn->keepNear>0&&NearReady()&&nears[0];
        const bool effectsOn=drawn->keepEffects>0&&EffectsReady()&&effects[0]&&(effectsKept[0]||effectsKept[1]);
        const bool somewhere=drawn->keepNear<=0&&drawn->keepEffects<=0||nearOnly||effectsOn;   // everywhere, or where these say
        const bool keep=own&&drawn->keep>0&&drawn->apart>0&&somewhere;
        D3D12_RESOURCE_BARRIER b[3]{};
        for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        b[0].Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
        b[1].Transition={ownTurned.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
        b[2].Transition={depthTarget.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        if(keep) {
            list->ResourceBarrier(2,b);list->CopyResource(ownTurned.Get(),target);
            for(int k=0;k<2;++k)std::swap(b[k].Transition.StateBefore,b[k].Transition.StateAfter);
            list->ResourceBarrier(3,b);   // the target back, the copy readable, this eye's depth readable
        }
        if(keep&&nearOnly) {
            // the drawn eye's depth (its grid's table) and this eye's grid's depth as SRVs, the blocks' too
            const int at=GridDescriptors;
            D3D12_SHADER_RESOURCE_VIEW_DESC depthView=srv;depthView.Format=drawn->format;
            device->CreateShaderResourceView(drawn->depth,&depthView,descriptor(at+1));
            device->CreateShaderResourceView(depthTarget.Get(),&single,descriptor(at+3));
            for(int n=0;n<2;++n)device->CreateShaderResourceView(nears[n].Get(),&single,descriptor(at+4+n));
            device->CreateShaderResourceView(nears[2].Get(),&single,descriptor(2*GridDescriptors));
            list->SetGraphicsRootSignature(nearRoot.Get());
            const D3D12_VIEWPORT blocks{0,0,float(blocksAcross),float(blocksDown),0,1};const D3D12_RECT blockRect{0,0,LONG(blocksAcross),LONG(blocksDown)};
            list->RSSetViewports(1,&blocks);list->RSSetScissorRects(1,&blockRect);
            nearBlocks(at+1,0,drawn->scale[0],drawn->scale[1],drawn->keepReach);   // as the drawn eye saw them
            nearBlocks(at+3,1,1,1,drawn->keepReach);                               // as this eye did
            list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
        }
        grid(*drawn,drawn->image,GridDescriptors,1,keep,keep&&nearOnly,keep&&effectsOn);
        if(keep){std::swap(b[2].Transition.StateBefore,b[2].Transition.StateAfter);list->ResourceBarrier(1,b+2);}
        // The strips the drawn eye's picture did not reach: what this eye's
        // own picture left there, unless it is something nearer since gone.
        if(fill&&own&&holePipeline&&drawn->apart>0) {
            D3D12_RESOURCE_BARRIER r[4]{};
            for(auto& x:r){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
            r[0].Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
            r[1].Transition={ownTurned.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
            r[2].Transition={depthTarget.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
            r[3].Transition={drawnDepthTarget.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
            list->ResourceBarrier(2,r);list->CopyResource(ownTurned.Get(),target);   // the picture so far, to read beside the strips
            for(int k=0;k<2;++k)std::swap(r[k].Transition.StateBefore,r[k].Transition.StateAfter);
            list->ResourceBarrier(4,r);   // the target back, the copy readable, both grids' depth readable
            const int holes=int(HoleDescriptors+GridDescriptors*slot)-int(SlotDescriptors*slot);   // this eye's table, from the slot's start
            device->CreateShaderResourceView(ownTurned.Get(),&srv,descriptor(holes));
            device->CreateShaderResourceView(drawnDepthTarget.Get(),&single,descriptor(holes+1));
            device->CreateShaderResourceView(depthTarget.Get(),&single,descriptor(holes+2));
            for(int n=3;n<int(GridDescriptors);++n)device->CreateShaderResourceView(nullptr,&single,descriptor(holes+n));
            list->SetGraphicsRootSignature(depthRoot.Get());list->SetPipelineState(holePipeline.Get());
            auto table=sourceGpu;table.ptr+=UINT64(step)*holes;list->SetGraphicsRootDescriptorTable(0,table);
            const float reach[4]={std::max(16.f,HoleReach*float(t.Width)/4096),edgeScale(*drawn)*drawn->nearPlane/Ahead,TurnEdgePixels,0};
            list->SetGraphicsRoot32BitConstants(1,4,reach,0);
            list->OMSetRenderTargets(1,&rtvHandle,FALSE,nullptr);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
            for(auto& x:r)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
            list->ResourceBarrier(2,r+2);   // both grids' depth back to be written
        }
    }
    return true;
}
bool TurnWarp::Interface(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12Resource* layer,ID3D12Resource* target,
                         const std::array<float,9>& homography,int at) {
    if(!layerPipeline||!layer||!target||at<0||at>3)return false;
    const auto l=layer->GetDesc(),t=target->GetDesc();const auto view=UnormView(format);
    if(!RenderTargetCompatible(l.Format,DXGI_FORMAT_R16G16B16A16_FLOAT)||!RenderTargetCompatible(t.Format,view)||
       l.SampleDesc.Count!=1||t.SampleDesc.Count!=1)return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    const UINT step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=sourceHeap->GetCPUDescriptorHandleForHeapStart();auto gpu=sourceHeap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr+=SIZE_T(step)*(InterfaceDescriptors+at);gpu.ptr+=UINT64(step)*(InterfaceDescriptors+at);
    device->CreateShaderResourceView(layer,&srv,cpu);
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=view;rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto rtvHandle=targetHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target,&rtv,rtvHandle);
    list->SetGraphicsRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[]={sourceHeap.Get()};list->SetDescriptorHeaps(1,heaps);
    list->SetGraphicsRootDescriptorTable(0,gpu);
    float rows[TurnConstants]{};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)rows[r*4+c]=homography[r*3+c];
    rows[12]=sharp?1.f:0.f;
    list->SetGraphicsRoot32BitConstants(1,TurnConstants,rows,0);
    list->OMSetRenderTargets(1,&rtvHandle,FALSE,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(t.Width),float(t.Height),0,1};const D3D12_RECT rect{0,0,LONG(t.Width),LONG(t.Height)};
    list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
    list->SetPipelineState(layerPipeline.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
    return true;
}
}
