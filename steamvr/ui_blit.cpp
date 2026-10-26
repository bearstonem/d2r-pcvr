#include "ui_blit.h"
#include "dxgi_views.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <climits>
#include <cstdio>

namespace framevr {
using Microsoft::WRL::ComPtr;
bool UiBlit::Initialize(ID3D12Device* device,void (*log)(const char*)) {
    if(pipeline)return true;
    auto fail=[&](){*this={};if(log)log("Frame panels: UI conversion unavailable; native flat menu fallback retained");return false;};
    struct Library {HMODULE module;~Library(){if(module)FreeLibrary(module);}} compiler{LoadLibraryW(L"d3dcompiler_47.dll")};
    const auto compile=compiler.module?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler.module,"D3DCompile")):nullptr;
    const auto d12=GetModuleHandleW(L"d3d12.dll");
    const auto serialize=d12?reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12,"D3D12SerializeRootSignature")):nullptr;
    if(!compile||!serialize){if(log)log("Frame panels: missing shader compiler or root serializer");return fail();}
    constexpr char shader[]=R"(
Texture2D<float4> nativeUi : register(t0);
SamplerState mapSampler : register(s0);
cbuffer Options : register(b0) {uint blackBackground;uint mapSide;uint mapFlags;float bearing;float wallAlpha;float zoom;float mapSpan;float xReach;float2 xShift;};
// xReach, xShift (the full map, xReach > 0; wrist_full_map.h XSearchAt): the X's search square,
// xReach source pixels each way from the source's center moved by xShift, where the game draws
// the hero's X on its full map at the game zoom the share was drawn at; sized to that X, so other
// blue on the map (water) beside it stays out. 0: the corner map's wide search (SearchRadius,
// MarkerRadius), as before.
// mapSpan (square map, >0; Map view = Full map, steamvr/wrist_full_map.h): a square of that
// many source pixels across the whole square around the hero's X, in place of fitting the
// whole source; the arrow sized from the X at this span (ArrowRadius), the X's own pixels
// painted over in its box, up to 4x4 taps a pixel when a pixel spans several (WristTaps).
// 0: the corner map's fit, zoomed by zoom, as before.
// mapFlags (square map only):
//   1 the heading arrow over the map's centre (the hero), turned by bearing
//     (radians, clockwise from map-up);
//   2 the clear background: the map's dark backing and margins see-through,
//     only the walls drawn, at wallAlpha (unassociated alpha, as the wrist quad
//     composes it); a thin gold frame stays;
//   4 the map turned so the heading is up (with 1, the arrow points up);
//  16 (set by Render, square map): FindX ran into `hero` this frame;
//   8 top-down: the game draws its map isometric, the floor grid turned 45
//     degrees and squashed to half height; undo both so rooms are square.
//     The game's map-up becomes the up-left diagonal, so the bearing turns
//     by -45 degrees with it.
// The X: the game's own hero marker at the map's centre. The arrow is drawn
// over it on its own, with a thin dark rim so it reads over walls; no dark
// spot around it.
static const float ArrowSize=.024,RimScale=1.3;   // of the map's side; the rim's dart this much bigger
// the full map (mapSpan): the arrow from the X's size on the wrist, as wrist_full_map.h ArrowRadius
static const float ArrowMin=.016,ArrowMax=.12,ArrowOverX=1.2,TopDownXGain=2.8284271;
// a dart: tip ahead, notched tail
float Dart(float2 v,float2 dir,float2 across,float r) {
    float along=dot(v,dir),wide=abs(dot(v,across));
    return step(along,r)*step(-.65*r+.9*wide,along)*step(wide,(r-along)*.42);
}
// 2 inside the arrow, 1 on its rim, 0 elsewhere; r its length ahead of the middle
float ArrowCoverage(float2 pixel,float side,float angle,float r) {
    float2 v=pixel-side*.5;
    float2 dir=float2(sin(angle),-cos(angle)),across=float2(cos(angle),sin(angle));
    return Dart(v,dir,across,r)+Dart(v+dir*r*(RimScale-1)*.3,dir,across,r*RimScale);
}
// The hero's marker: the game's blue X, which is not at the captured map's
// centre (up-right of it on the slanted map) by an amount
// that follows the resolution. FindTiles and FindX, two passes before the map's,
// find the blue pixels nearest the centre and write their middle (map pixels)
// and count to `hero`; the map is then centred and turned on that point, under
// the arrow, and those blue pixels are painted over.
// The search square is SearchRadius of the map's longer side each way: one pixel
// scanning it alone read ~241,000 texels on the full map's 3072x1728 share, 23.6 ms
// of the GPU in one thread every frame (RTX 5090; By turns 88 -> 39 frames a second,
// owner, 2026-10-24). So FindTiles, a pixel per Tile x Tile of the square, reads each
// pixel once in parallel: its tile's nearest marker pixel and all its marker pixels'
// sum, count and box. FindX (1x1) takes the nearest, then adds up the tiles within
// MarkerRadius of it (whole tiles: the X's box rounded out to their edges). That is the
// corner map's search; the full map searches only a square sized to its X (xReach, xShift).
Texture2D<float4> hero : register(t1);
// FindTiles, a texel per tile: (nearest marker pixel xy, its squared distance (1e9 none),
// count), (sum of their middles xy, box low xy), (box high xy)
Texture2D<float4> tileNear : register(t2);
Texture2D<float4> tileSum : register(t3);
Texture2D<float4> tileHigh : register(t4);
static const float SearchRadius=.08,MarkerRadius=.02;   // of the map's longer side
static const int Tile=16,TileGrid=128;   // ui_blit.h FindTile, TileGrid
bool IsMarker(float4 c) {
    float opacity=1-saturate(c.a);
    if(opacity<.5)return false;
    float3 e=max(c.rgb,0)/opacity;   // sRGB-encoded, unassociated
    return e.b>.7&&e.b>2*e.r&&e.b>1.35*e.g;
}
struct Tiles {float4 near:SV_Target0;float4 sum:SV_Target1;float4 high:SV_Target2;};
Tiles FindTiles(float4 position:SV_Position) {
    uint w,h;nativeUi.GetDimensions(w,h);
    const float side=max(w,h);
    const int R=xReach>0?int(xReach):int(side*SearchRadius);
    const int2 c=int2(uint2(w,h)/2)+(xReach>0?int2(round(xShift)):0),from=int2(position.xy)*Tile-R;
    float best=1e9,count=0;int2 near=c,lo=int2(w,h),hi=-1;float2 sum=0;
    [loop] for(int y=from.y;y<min(from.y+Tile,R+1);++y) [loop] for(int x=from.x;x<min(from.x+Tile,R+1);++x) {
        int2 q=c+int2(x,y);
        if(any(q<0)||q.x>=int(w)||q.y>=int(h))continue;
        if(!IsMarker(nativeUi.Load(int3(q,0))))continue;
        float d=x*x+y*y;
        if(d<best){best=d;near=q;}
        sum+=q+.5;++count;lo=min(lo,q);hi=max(hi,q);
    }
    Tiles t;t.near=float4(near,best,count);t.sum=float4(sum,lo);t.high=float4(hi,0,0);
    return t;
}
float4 FindX(float4 position:SV_Position):SV_Target {
    uint w,h;nativeUi.GetDimensions(w,h);
    const float side=max(w,h);
    // the full map: the whole square, sized to the X, is the X's (xReach); the corner map: within r of the nearest
    const int R=xReach>0?int(xReach):int(side*SearchRadius),r=xReach>0?2*R:int(side*MarkerRadius)+1;
    const int2 c=int2(uint2(w,h)/2)+(xReach>0?int2(round(xShift)):0);   // as FindTiles
    const int n=min((2*R+Tile)/Tile,TileGrid);   // the tiles across the square (as many as FindTiles had)
    float best=1e9;int2 near=c;
    [loop] for(int ty=0;ty<n;++ty) [loop] for(int tx=0;tx<n;++tx) {
        const float4 t=tileNear.Load(int3(tx,ty,0));
        if(t.z<best){best=t.z;near=int2(t.xy);}
    }
    if(best>1e8)return 0;
    // the X's blue pixels: those of the tiles within r of it, every pixel counted
    const int2 first=clamp((near-r-(c-R))/Tile,0,n-1),last=clamp((near+r-(c-R))/Tile,0,n-1);
    float3 sum=0;float2 lo=near,hi=near;
    [loop] for(int y2=first.y;y2<=last.y;++y2) [loop] for(int x2=first.x;x2<=last.x;++x2) {
        const float count=tileNear.Load(int3(x2,y2,0)).w;
        if(count<=0)continue;
        const float4 t=tileSum.Load(int3(x2,y2,0));
        sum+=float3(t.xy,count);lo=min(lo,t.zw);hi=max(hi,tileHigh.Load(int3(x2,y2,0)).xy);
    }
    // w: the X's half size (map pixels), from the box of its blue pixels (the full map's arrow)
    return float4(sum.xy/max(sum.z,1),sum.z,max(hi.x-lo.x,hi.y-lo.y)*.5+.5);
}
// The X's pixels near the hero, rim too: the bright blue above, or a darker blue
// (its outline, and the sampler's blend of it with the backing).
bool IsXPixel(float4 c) {
    float opacity=1-saturate(c.a);
    if(opacity<.15)return false;
    float3 e=max(c.rgb,0)/opacity;
    return IsMarker(c)||(e.b>.2&&e.b>1.5*e.r&&e.b>1.15*e.g);
}
// The colour out, from the source's (sRGB-encoded, alpha = 1 - coverage) colour.
float4 Finish(float4 c,bool clearBack) {
    float opacity=1-saturate(c.a);
    // D2R's SDR UI shaders encode sRGB before blending into this float target.
    // XR float layers require linear light. Decode the unassociated color,
    // then premultiply again so antialiased text/borders retain their coverage.
    float3 encoded=max(c.rgb,0)/max(opacity,1e-6);
    float3 linearRgb=lerp(pow((encoded+.055)/1.055,2.4),encoded/12.92,encoded<=.04045);
    // only the walls: unassociated colour, their own coverage at wallAlpha
    // the game's black backing may be drawn as solid pixels: near-black fades out too
    const float lit=smoothstep(.05,.14,max(max(encoded.r,encoded.g),encoded.b));
    if(clearBack)return float4(linearRgb,opacity*lit*wallAlpha);
    float3 rgb=opacity>0?linearRgb*opacity:0;
    if(mapSide)rgb+=.006*(1-opacity);
    // the square map: solid, its black backing too
    return float4(rgb,mapSide?wallAlpha:blackBackground?1:opacity);
}
float4 VS(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3:-1,id==1?-3:1,0,1);
}
float4 PS(float4 position:SV_Position):SV_Target {
    float2 pixel=position.xy;
    float2 mapSize=1;
    const bool clearBack=mapSide&&(mapFlags&2);
    // 4: the map turns with you (your heading up, the arrow fixed up); else north-up, the arrow turns
    const bool turning=(mapFlags&4)!=0,topDown=(mapFlags&8)!=0,found=(mapFlags&16)!=0;
    const float heading=topDown?bearing-.78539816:bearing;
    // 16: FindX ran: centre and turn the map on the hero's marker when it was seen
    const float4 x=mapSide&&found?hero.Load(int3(0,0,0)):0;
    const bool seen=x.z>=3;
    // the arrow grown with the corner map's zoom, so it stays the same size relative to the
    // map and the game's X; on the full map, as big as the X shows at this span, within limits
    float arrowR=mapSide*ArrowSize*max(zoom,1.0);
    if(mapSpan>0)arrowR=seen?clamp(x.w*mapSide/mapSpan*(topDown?TopDownXGain:1)*ArrowOverX,mapSide*ArrowMin,mapSide*ArrowMax):mapSide*ArrowSize;
    float arrow=0;
    float2 heroAt=0;float markerReach=0;
    if(mapSide&&(mapFlags&1))arrow=ArrowCoverage(position.xy,mapSide,turning?0:heading,arrowR);
    if(arrow>1.5)return float4(1,.62,.12,1);   // the arrow: a gold dart (linear light)
    if(arrow>.5)return float4(.006,.006,.006,1);   // its dark rim
    if(mapSide) {
        // Fit the entire corner map in a square without stretching, clipping
        // exits or reading it back to the CPU. Empty margins stay dark.
        float bevel=min(1.0,mapSide/128.0);
        float edge=min(min(pixel.x,pixel.y),min(mapSide-pixel.x,mapSide-pixel.y))/bevel;
        // A narrow aged-bronze bevel and inset corner cuts. The entire native
        // map fits inside the frame, including markers at its outer edges.
        float2 corner=min(pixel,float(mapSide)-pixel)/bevel;
        if(edge<1)return float4(.012,.009,.006,1);
        if(edge<3)return float4(.24,.16,.065,1);
        if(edge<5)return float4(.055,.033,.015,1);
        if(edge<7)return float4(.012,.009,.006,1);
        if(max(corner.x,corner.y)<20&&abs(corner.x+corner.y-26)<1.4)
            return float4(.34,.24,.11,1);
        uint w,h;nativeUi.GetDimensions(w,h);
        mapSize=float2(w,h);
        if(mapSpan>0) {
            // the full map: a square of mapSpan source pixels across the whole square around
            // the X, top-down halved so its diamond is mapSpan wide, n x n taps a pixel when a
            // pixel spans several; past the game's box the dark backing
            const float2 at=seen?x.xy:mapSize*.5;
            const float k=mapSpan/mapSide;
            const int n=clamp(int(ceil(k-.001)),1,4);
            // with the arrow, the X painted over in its box, rim too
            const float mask=seen&&(mapFlags&1)?x.w*1.25+3:0;
            float4 sum=0;
            [loop] for(int j=0;j<n;++j) [loop] for(int i=0;i<n;++i) {
                float2 v=position.xy+(float2(i,j)+.5)/n-.5-mapSide*.5;
                if(turning)v=float2(v.x*cos(heading)-v.y*sin(heading),v.x*sin(heading)+v.y*cos(heading));
                if(topDown)v=float2(v.x-v.y,(v.x+v.y)*.5)*.5;
                const float2 p=v*k+at;
                float4 c=float4(0,0,0,1);   // nothing drawn: the backing
                if(all(p>=0)&&all(p<mapSize)) {
                    c=nativeUi.SampleLevel(mapSampler,p/mapSize,0);
                    if(mask>0&&all(abs(p-at)<mask)&&IsXPixel(c))c=float4(0,0,0,1);
                }
                sum+=c;
            }
            return Finish(sum/float(n*n),clearBack);
        }
        // inside upstream's bronze frame (v0.3.18)
        const float inner=mapSide-2*min(20.0,mapSide/8.0);
        float scale=inner/float(max(w,h));
        // zoom: the map and the arrow bigger about the hero (map_zoom.h), the frame as it is
        float2 v=(pixel-mapSide*.5)/max(zoom,1.0);
        if(turning)v=float2(v.x*cos(heading)-v.y*sin(heading),v.x*sin(heading)+v.y*cos(heading));
        // top-down to isometric: (x-y, (x+y)/2), sized so the whole slanted map still fits
        if(topDown)v=float2(v.x-v.y,(v.x+v.y)*.5)*((w+2.*h)/(2.*inner))*scale;
        heroAt=x.z>=3?x.xy:float2(w,h)*.5;
        markerReach=x.z>=3&&(mapFlags&1)?max(w,h)*MarkerRadius*1.6:0;
        pixel=v/scale+heroAt;
        if(any(pixel<0)||pixel.x>=w||pixel.y>=h)return clearBack?0:float4(.006,.006,.006,1);
    }
    float4 c=mapSide?nativeUi.SampleLevel(mapSampler,pixel/mapSize,0):nativeUi.Load(int3(uint2(pixel),0));
    // with the arrow, the game's X itself painted over (the backing shows)
    if(markerReach>0&&distance(pixel,heroAt)<markerReach&&IsMarker(c))return float4(.006,.006,.006,wallAlpha);
    return Finish(c,clearBack);
}
)";
    ComPtr<ID3DBlob> vs,ps,findPs,tilesPs,errors,signature;
    const auto v=compile(shader,sizeof(shader)-1,"native-ui",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors);
    const auto p=compile(shader,sizeof(shader)-1,"native-ui",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors);
    const auto f=compile(shader,sizeof(shader)-1,"native-ui",nullptr,nullptr,"FindX","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&findPs,&errors);
    const auto ft=FAILED(f)?f:compile(shader,sizeof(shader)-1,"native-ui",nullptr,nullptr,"FindTiles","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&tilesPs,&errors);
    if(FAILED(v)||FAILED(p)||FAILED(f)||FAILED(ft)){
        if(log){char b[160];std::snprintf(b,sizeof b,"Frame panels: shader compile failed VS=%08lx PS=%08lx",(unsigned long)v,(unsigned long)p);log(b);if(errors)log(static_cast<const char*>(errors->GetBufferPointer()));}
        return fail();
    }
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2+TileTargets,0,0,0};   // t0 the source, t1 the hero marker (FindX), t2.. its tiles (FindTiles)
    D3D12_ROOT_PARAMETER parameters[2]{};auto& parameter=parameters[0];parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable={1,&range};parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,10};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;sampler.MaxLOD=D3D12_FLOAT32_MAX;
    desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
    if(FAILED(serialize(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors))||
       FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)))){
        if(log)log("Frame panels: root signature unavailable");return fail();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature=root.Get();state.VS={vs->GetBufferPointer(),vs->GetBufferSize()};state.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    state.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    state.SampleMask=UINT_MAX;state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;
    state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets=1;state.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;state.SampleDesc.Count=1;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=2+TileTargets;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const auto result=device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&pipeline));
    if(FAILED(result)) {if(log){char b[128];std::snprintf(b,sizeof b,"Frame panels: pipeline failed %08lx",(unsigned long)result);log(b);}return fail();}
    state.PS={findPs->GetBufferPointer(),findPs->GetBufferSize()};state.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;
    if(FAILED(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&findPipeline)))){if(log)log("Frame panels: map marker pass unavailable");return fail();}
    state.PS={tilesPs->GetBufferPointer(),tilesPs->GetBufferSize()};state.NumRenderTargets=TileTargets;
    for(int i=0;i<TileTargets;++i){state.RTVFormats[i]=DXGI_FORMAT_R32G32B32A32_FLOAT;state.BlendState.RenderTarget[i].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
    if(FAILED(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&tilesPipeline)))){if(log)log("Frame panels: map marker pass unavailable");return fail();}
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&sourceHeap))))return fail();
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&targetHeap))))return fail();
    if(log)log("Frame panels: GPU native UI alpha conversion ready; original artwork retained");
    return true;
}
bool UiBlit::Render(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12Resource* source,ID3D12Resource* target,bool blackBackground,bool squareMap,uint32_t mapFlags,float bearing,float wallAlpha,float zoom,float mapSpan,float xReach,float xShiftRight,float xShiftDown) {
    if(!pipeline||!source||!target)return false;
    const auto s=source->GetDesc(),t=target->GetDesc();
    if(!RenderTargetCompatible(s.Format,DXGI_FORMAT_R16G16B16A16_FLOAT)||
       (squareMap?(t.Width!=t.Height||t.Width<32):(s.Width!=t.Width||s.Height!=t.Height))||
       !RenderTargetCompatible(t.Format,DXGI_FORMAT_R16G16B16A16_FLOAT)||
       s.SampleDesc.Count!=1||t.SampleDesc.Count!=1)return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(source,&srv,sourceHeap->GetCPUDescriptorHandleForHeapStart());
    const UINT srvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto heroSrv=sourceHeap->GetCPUDescriptorHandleForHeapStart();heroSrv.ptr+=srvStep;
    auto heroRtv=targetHeap->GetCPUDescriptorHandleForHeapStart();heroRtv.ptr+=rtvStep;
    auto tilesSrv=heroSrv;tilesSrv.ptr+=srvStep;   // TileTargets in a row from here
    auto tilesRtv=heroRtv;tilesRtv.ptr+=rtvStep;
    // The square map: its hero marker found first (FindTiles into `heroTiles`, then FindX into
    // the 1x1 `hero`), read by the map's pass.
    auto marker=[&](ComPtr<ID3D12Resource>& r,UINT side) {
        if(r)return;
        D3D12_HEAP_PROPERTIES heapProps{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=side;d.DepthOrArraySize=d.MipLevels=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if(FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&r))))r.Reset();
    };
    bool findHero=squareMap&&findPipeline&&tilesPipeline;
    if(findHero){marker(hero,1);for(auto& t:heroTiles)marker(t,TileGrid);}
    for(const auto& t:heroTiles)findHero=findHero&&t;
    findHero=findHero&&hero;
    D3D12_SHADER_RESOURCE_VIEW_DESC heroView=srv;heroView.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    device->CreateShaderResourceView(findHero?hero.Get():nullptr,&heroView,heroSrv);
    for(int i=0;i<TileTargets;++i){auto at=tilesSrv;at.ptr+=SIZE_T(i)*srvStep;device->CreateShaderResourceView(findHero?heroTiles[i].Get():nullptr,&heroView,at);}
    list->SetGraphicsRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[]={sourceHeap.Get()};list->SetDescriptorHeaps(1,heaps);
    list->SetGraphicsRootDescriptorTable(0,sourceHeap->GetGPUDescriptorHandleForHeapStart());
    // the X's search square, read by FindTiles and FindX before the map's own constants are set
    if(!squareMap||!(xReach>0.f)||!std::isfinite(xReach)||!std::isfinite(xShiftRight)||!std::isfinite(xShiftDown))xReach=xShiftRight=xShiftDown=0.f;
    const float search[3]={xReach,xShiftRight,xShiftDown};
    list->SetGraphicsRoot32BitConstants(1,3,search,7);
    auto barrier=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=from;b.Transition.StateAfter=to;
        list->ResourceBarrier(1,&b);
    };
    if(findHero) {
        D3D12_RENDER_TARGET_VIEW_DESC heroTarget{};heroTarget.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;heroTarget.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // a pixel per tile of the search square, then the 1x1 pass over those tiles
        const LONG n=FindTiles(UINT(s.Width),s.Height,xReach);
        for(int i=0;i<TileTargets;++i){auto at=tilesRtv;at.ptr+=SIZE_T(i)*rtvStep;device->CreateRenderTargetView(heroTiles[i].Get(),&heroTarget,at);}
        list->OMSetRenderTargets(TileTargets,&tilesRtv,TRUE,nullptr);
        const D3D12_VIEWPORT grid{0,0,float(n),float(n),0,1};const D3D12_RECT gridRect{0,0,n,n};
        list->RSSetViewports(1,&grid);list->RSSetScissorRects(1,&gridRect);
        list->SetPipelineState(tilesPipeline.Get());list->DrawInstanced(3,1,0,0);
        for(const auto& t:heroTiles)barrier(t.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        device->CreateRenderTargetView(hero.Get(),&heroTarget,heroRtv);
        list->OMSetRenderTargets(1,&heroRtv,FALSE,nullptr);
        const D3D12_VIEWPORT one{0,0,1,1,0,1};const D3D12_RECT oneRect{0,0,1,1};
        list->RSSetViewports(1,&one);list->RSSetScissorRects(1,&oneRect);
        list->SetPipelineState(findPipeline.Get());list->DrawInstanced(3,1,0,0);
        barrier(hero.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mapFlags|=16;
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};rtvDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rtvDesc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(target,&rtvDesc,targetHeap->GetCPUDescriptorHandleForHeapStart());
    auto rtv=targetHeap->GetCPUDescriptorHandleForHeapStart();list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    const D3D12_VIEWPORT vp{0,0,float(t.Width),float(t.Height),0,1};const D3D12_RECT rect{0,0,LONG(t.Width),LONG(t.Height)};
    list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&rect);
    list->SetPipelineState(pipeline.Get());
    list->SetGraphicsRoot32BitConstant(1,blackBackground?1:0,0);
    list->SetGraphicsRoot32BitConstant(1,squareMap?UINT(t.Width):0,1);
    UINT bearingBits=0;std::memcpy(&bearingBits,&bearing,sizeof bearingBits);
    list->SetGraphicsRoot32BitConstant(1,squareMap?mapFlags:0,2);
    list->SetGraphicsRoot32BitConstant(1,bearingBits,3);
    UINT alphaBits=0;std::memcpy(&alphaBits,&wallAlpha,sizeof alphaBits);
    list->SetGraphicsRoot32BitConstant(1,alphaBits,4);
    UINT zoomBits=0;std::memcpy(&zoomBits,&zoom,sizeof zoomBits);
    list->SetGraphicsRoot32BitConstant(1,zoomBits,5);
    if(!squareMap||!(mapSpan>0.f))mapSpan=0.f;   // NaN too: the fit as before
    UINT spanBits=0;std::memcpy(&spanBits,&mapSpan,sizeof spanBits);
    list->SetGraphicsRoot32BitConstant(1,spanBits,6);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
    if(findHero) {
        barrier(hero.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        for(const auto& t:heroTiles)barrier(t.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    return true;
}
}
