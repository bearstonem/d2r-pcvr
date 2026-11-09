// D2R_CeilingBase.fxh: the cave ceiling: uniforms, noise, relief and the ceiling pictures.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// The cave ceiling (docs/plan_cave_ceiling.md), set by vrcam ([ceiling]) in the
// biomes on its list, from inside only: in the void, where the sky is outdoors,
// each eye's ray meets a plane CeilHeight over the hero's floor - so it stands
// at its true depth in stereo. Uses the sky's per-eye axes and projection.
// First step, the test: a grey plane with a grid (stereo and height in the headset).
uniform bool CeilOn < hidden = true; > = false;
uniform float CeilHeight < hidden = true; > = 30.0;     // over the hero's floor, world units
uniform float CeilScale < hidden = true; > = 10.0;      // world units per tile (the grid's cell)
uniform float CeilBrightness < hidden = true; > = 1.0;
uniform float3 CeilEye0 < hidden = true; > = float3(0.0, 7.0, 0.0);   // each eye less the hero (his feet)
uniform float3 CeilEye1 < hidden = true; > = float3(0.0, 7.0, 0.0);
uniform float2 CeilHero0 < hidden = true; > = float2(0.0, 0.0);       // the hero's x, z wrapped on 64 tiles
uniform float2 CeilHero1 < hidden = true; > = float2(0.0, 0.0);

// Value noise that repeats every `per` lattice cells: the ceiling's world x, z
// come wrapped on 64 tiles (vrcam), so everything on it must repeat with them.
float HashP(float2 i, float per) { return Hash(i - per * floor(i / per)); }
float NoiseP(float2 p, float per)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(HashP(i, per), HashP(i + float2(1.0, 0.0), per), f.x),
                lerp(HashP(i + float2(0.0, 1.0), per), HashP(i + float2(1.0, 1.0), per), f.x), f.y);
}

// How far the rock hangs down at q (tiles: world x, z / CeilScale), 0 = up at the
// plane .. 1 = CeilRelief below it. Lumpy stone: four octaves, 2 tiles and finer.
// (Stalactites as blunt cones in half the 2x2-tile cells were taken out: they read
// as little pyramids hanging in rows - the user, 2026-10-06.)
float CeilRockNoise(float2 q)
{
    float v = 0.0, a = 0.5, f = 0.5;
    [unroll] for (int i = 0; i < 4; ++i) { v += a * NoiseP(q * f, 64.0 * f); f *= 2.0; a *= 0.5; }
    return saturate(0.55 * v);
}

uniform float CeilRelief < hidden = true; > = 6.0;   // world units the rock hangs down, 0 = flat

// The cave ceiling along this eye's ray (dir: unit, in the world); false where the
// ray never meets it - looking level or down, or the eye above it - and the void
// stays as it was. Rock hanging from a plane CeilHeight over the hero's floor,
// CeilRelief deep: the ray is marched through that band (CeilSteps steps, then
// a secant between the last two). Lit from the hero, fading with the distance
// from him, and dimly from one fixed side; the hollows darker; the fog by its
// distance from the eye, as the walls get.
uniform int CeilSteps < hidden = true; > = 12;
uniform float CeilLightRadius < hidden = true; > = 25.0;   // world units from the hero where his light is down to half
// The stone's pictures, one per kind of dungeon (vrcam [ceiling] texture_<biome>; drawn by
// tools/casc_extract/gen_ceiling_from_ref.py): 4 slots, D2R_CEILING_1..4, the area's in
// CeilTexSlot - the act 1 caves' rock, the crypts' brick (2026-10-07). One picture spans
// 4 tiles: 64, the wrap, is a whole number of them. Without a file (CeilTexOn off) the
// stone is drawn here. A slot vrcam has no file for keeps one that is there, never sampled.
#ifndef D2R_CEILING_1
 // Optional pictures are supplied by the plugin after checking they exist.
 // Empty slots use procedural stone and must not fail the initial reload.
 #define D2R_CEILING_1 ""
#endif
#ifndef D2R_CEILING_2
 #define D2R_CEILING_2 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_3
 #define D2R_CEILING_3 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_4
 #define D2R_CEILING_4 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_5
 #define D2R_CEILING_5 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_6
 #define D2R_CEILING_6 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_7
 #define D2R_CEILING_7 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_8
 #define D2R_CEILING_8 D2R_CEILING_1
#endif
uniform bool CeilTexOn < hidden = true; > = true;
uniform float CeilTexSlot < hidden = true; > = 1.0;
texture2D CeilTex1 < source = D2R_CEILING_1; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex2 < source = D2R_CEILING_2; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex3 < source = D2R_CEILING_3; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex4 < source = D2R_CEILING_4; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex5 < source = D2R_CEILING_5; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex6 < source = D2R_CEILING_6; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex7 < source = D2R_CEILING_7; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex8 < source = D2R_CEILING_8; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
sampler2D CeilSmp1 { Texture = CeilTex1; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp2 { Texture = CeilTex2; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp3 { Texture = CeilTex3; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp4 { Texture = CeilTex4; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp5 { Texture = CeilTex5; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp6 { Texture = CeilTex6; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp7 { Texture = CeilTex7; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp8 { Texture = CeilTex8; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
static const float kCeilTexTiles = 4.0;
// World units one picture spans - its own per dungeon (vrcam [ceiling] tex_size_<biome>; 0 = 4 tiles):
// the sewers' brick wanted its own size, the tile grid (the height map) stays (the user, 2026-10-07)
uniform float CeilTexSize < hidden = true; > = 0.0;
float TexSpan() { return CeilTexSize > 0.5 ? CeilTexSize : kCeilTexTiles * CeilScale; }
float3 CeilPic(float2 uv, float lod)
{
    const int slot = (int)(CeilTexSlot + 0.5);
    if (slot == 2) return tex2Dlod(CeilSmp2, float4(uv, 0, lod)).rgb;
    if (slot == 3) return tex2Dlod(CeilSmp3, float4(uv, 0, lod)).rgb;
    if (slot == 4) return tex2Dlod(CeilSmp4, float4(uv, 0, lod)).rgb;
    if (slot == 5) return tex2Dlod(CeilSmp5, float4(uv, 0, lod)).rgb;
    if (slot == 6) return tex2Dlod(CeilSmp6, float4(uv, 0, lod)).rgb;
    if (slot == 7) return tex2Dlod(CeilSmp7, float4(uv, 0, lod)).rgb;
    if (slot == 8) return tex2Dlod(CeilSmp8, float4(uv, 0, lod)).rgb;
    return tex2Dlod(CeilSmp1, float4(uv, 0, lod)).rgb;
}

// The relief from the picture itself, not noise (the user, 2026-10-07: the sewers' bricks were flat
// paint): what stands out of the vault by CeilRelief - the light parts (1), the dark (-1), or the
// coloured (2: bricks in grey mortar). Against the picture's own blur, so the whole stays level.
// vrcam [ceiling] relief_pic_<biome>; 0 = the old lumpy noise.
uniform float CeilReliefPic < hidden = true; > = 0.0;
float PicRock(float2 q)
{
    const float2 uv = q * CeilScale / TexSpan();
    const float3 c = CeilPic(uv, 1.0), m = CeilPic(uv, 6.0);
    const int mode = (int)round(CeilReliefPic);
    float v, a;
    if (mode == 2) {
        v = (max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b))) / max(max(c.r, max(c.g, c.b)), 1e-3);
        a = (max(m.r, max(m.g, m.b)) - min(m.r, min(m.g, m.b))) / max(max(m.r, max(m.g, m.b)), 1e-3);
    } else {
        v = dot(c, float3(0.2126, 0.7152, 0.0722)); a = dot(m, float3(0.2126, 0.7152, 0.0722));
        if (mode < 0) { v = 1.0 - v; a = 1.0 - a; }
    }
    // the raised parts at 1 (hanging down the most), the joints at 0; bevelled edges, not cliffs
    // (at x5 the bricks' sides stood as lit icicles - 2026-10-07)
    return smoothstep(0.0, 1.0, saturate(0.5 + (v - a) * 2.5));
}
bool PicRelief() { return CeilTexOn && abs(CeilReliefPic) > 0.5; }
float CeilRock(float2 q) { return PicRelief() ? PicRock(q) : CeilRockNoise(q); }

