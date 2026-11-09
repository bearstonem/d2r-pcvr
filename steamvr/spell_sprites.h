#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// The hand spells' effect textures, cached from the installed game by
// prepare_spell_sprites.py (sprites.ssp, version 2). Slots follow its TEXTURES order: the spells' own
// eleven, then the game's fire flipbook (8x8), smoke and wisp atlases (2x2), lightning arc and bolt
// flipbooks (4x2), snowflakes and ice chunks (2 columns x 4 rows), an ice caustic, a glow dot, a star
// flare and Fire Bolt's flame distortion (two channels).
namespace framevr::elements::sprites {
enum Slot : unsigned {
    FirePlate, FireballPlate, SoftFlame, FlameRamp, Flash, Glow, LightningRamp,
    Glint, IceRamp, FrozenOrb, FrostRamp,
    FireFlipbook, Smoke, Wisp, Arcs, Bolts, Flakes, IceCaustic, Dot, Star, Distortion, Count
};
constexpr uint32_t Version=2;
inline bool Ramp(unsigned slot) {return slot==FlameRamp||slot==LightningRamp||slot==IceRamp||slot==FrostRamp;}
struct Texture {
    uint32_t width=0,height=0,format=0; // DXGI: BC4_UNORM 80, BC3_UNORM 77, BC5_UNORM 83, R8G8B8A8_UNORM_SRGB 29
    std::vector<std::vector<uint8_t>> mips;
};
inline uint64_t MipBytes(uint32_t format,uint32_t width,uint32_t height) {
    return format==29?uint64_t(width)*height*4:uint64_t((width+3)/4)*((height+3)/4)*(format==80?8:16);
}
inline std::vector<Texture> Parse(const std::vector<uint8_t>& data) {
    size_t offset=0;
    auto read=[&](void* out,size_t bytes) {
        if(offset>data.size()||bytes>data.size()-offset)throw std::runtime_error("truncated spell sprite cache");
        std::memcpy(out,data.data()+offset,bytes);offset+=bytes;
    };
    uint32_t header[3];read(header,sizeof header);
    if(header[0]!=0x31505353||header[1]!=Version||header[2]!=Count)
        throw std::runtime_error(header[0]==0x31505353&&header[1]<Version?"spell sprite cache from an older install; Update / repair prepares it again":"unsupported spell sprite cache");
    std::vector<Texture> result(Count);
    for(unsigned i=0;i<Count;++i) {
        uint32_t fields[5];read(fields,sizeof fields);
        auto& t=result[i];t.width=fields[1];t.height=fields[2];t.format=fields[3];
        const bool format=Ramp(i)?t.format==29:i==Distortion?t.format==83:t.format==80||t.format==77;
        // Block-compressed textures need whole blocks at their top level.
        if(fields[0]!=i||!format||t.width<4||t.height<4||t.width>2048||t.height>2048||
           (t.format!=29&&(t.width%4||t.height%4))||!fields[4]||fields[4]>14)
            throw std::runtime_error("invalid spell sprite texture");
        for(unsigned m=0;m<fields[4];++m) {
            uint32_t bytes=0;read(&bytes,sizeof bytes);
            if(bytes!=MipBytes(t.format,std::max(1u,t.width>>m),std::max(1u,t.height>>m)))
                throw std::runtime_error("invalid spell sprite mip");
            t.mips.emplace_back(bytes);read(t.mips.back().data(),bytes);
        }
    }
    if(offset!=data.size())throw std::runtime_error("unexpected spell sprite cache data");
    return result;
}
}
