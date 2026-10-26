#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace framevr {
// Convert D2R's SDR sRGB-premultiplied RGB + world-transmittance channel to
// linear premultiplied RGBA for XR. Caller owns synchronization/transitions.
class UiBlit {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline,findPipeline,tilesPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sourceHeap,targetHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> hero;   // 1x1: the map's hero marker, found each frame (square map)
public:
    // The marker search's tiles, as the shader's Tile: TileGrid of them across covers a
    // source up to TileGrid*Tile/(2*SearchRadius) pixels on its longer side (12,800).
    // TileTargets: FindTiles' outputs (nearest, sum and box low, box high).
    static constexpr int FindTile=16,TileGrid=128,TileTargets=3;
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> heroTiles[TileTargets];   // FindTiles: up to TileGrid x TileGrid tiles of its search square
public:
    // The tiles across the search square of a w x h source, as FindX counts them (+1 to spare).
    static int FindTiles(uint32_t w,uint32_t h,float xReach=0.f) {
        const int R=xReach>0.f?int(xReach):int(float(w>h?w:h)*.08f);
        const int n=(2*R+FindTile)/FindTile+1;
        return n<1?1:n>TileGrid?TileGrid:n;
    }
    // The 1x1 marker the last square map found (x, y in source pixels, count, half size; 0 none),
    // in the render-target state; null before one. For tests.
    ID3D12Resource* Marker() const {return hero.Get();}
    bool Initialize(ID3D12Device* device,void (*log)(const char*));
    // mapFlags (square map only): 1 heading arrow at bearing (radians, clockwise from map-up), 2 clear background, walls at wallAlpha, 4 the map turns with you
    // zoom (square map only, >= 1): the map that much bigger about the hero; 1 fits it whole
    // mapSpan (square map only, > 0; Map view = Full map): a square of that many source pixels across the whole square around the hero's X, the arrow sized from the X; 0: the fit, zoomed
    // xReach (square map only, > 0; the full map): the X searched only that many source pixels each way from the source's center moved by xShiftRight, xShiftDown (wrist_full_map.h XSearchAt); 0: the corner map's wide search
    bool Render(ID3D12Device*,ID3D12GraphicsCommandList*,ID3D12Resource* source,ID3D12Resource* target,bool blackBackground=false,bool squareMap=false,uint32_t mapFlags=0,float bearing=0,float wallAlpha=1.f,float zoom=1.f,float mapSpan=0.f,
                float xReach=0.f,float xShiftRight=0.f,float xShiftDown=0.f);
};
}
