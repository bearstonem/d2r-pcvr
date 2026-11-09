// D2R_Sky.fxh: the sky: its uniforms, pictures, Arcane Sanctuary sky, Sky().
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// Sky, all set by vrcam.
//
// Both eyes' values, each present: which eye a frame is for comes from the
// technique (D2R_DepthFog = left, D2R_DepthFog_R = right), never from a
// uniform. Under D3D12 ReShade keeps one constant buffer per effect and
// rewrites it in place at every present; with a pair drawn per game frame the
// right eye's present wrote it before the GPU had run the left eye's effect,
// and the left eye drew the right eye's sky.
uniform bool SkyOn < hidden = true; > = false;
uniform bool ArcaneSkyOn < hidden = true; > = false;
// vrcam, [sky] cover_rims: the sky goes over the dark rim the game leaves round
// anything against the void (DepthFogWorld).
uniform bool SkyOverRims < hidden = true; > = true;

// These are prepared from the player's game, never distributed. Without a
// complete cache the original native weather cards remain installed.
#ifndef D2R_ARCANE_SKY_CACHE
 #define D2R_ARCANE_SKY_CACHE 0
#endif
#if D2R_ARCANE_SKY_CACHE
texture2D ArcaneStarsTex < source = "PCVR_Arcane/stars.png"; > { Width = 1024; Height = 512; Format = RGBA8; };
sampler2D ArcaneStarsSmp { Texture = ArcaneStarsTex; AddressU = WRAP; AddressV = WRAP; };
texture2D ArcaneCloudsTex < source = "PCVR_Arcane/clouds.png"; > { Width = 512; Height = 512; Format = RGBA8; };
sampler2D ArcaneCloudsSmp { Texture = ArcaneCloudsTex; AddressU = WRAP; AddressV = WRAP; };

// World-direction triplanar sampling covers all six directions, including
// below the floating platforms. No flat card edge, longitude seam or pole.
// Only direction matters: head translation cannot make a nearby star screen.
float3 ArcaneStars(float3 p, float3 weights)
{
    return tex2Dlod(ArcaneStarsSmp, float4(p.yz, 0, 0)).rgb * weights.x
         + tex2Dlod(ArcaneStarsSmp, float4(p.zx, 0, 0)).rgb * weights.y
         + tex2Dlod(ArcaneStarsSmp, float4(p.xy, 0, 0)).rgb * weights.z;
}
float ArcaneClouds(float3 p, float3 weights)
{
    return tex2Dlod(ArcaneCloudsSmp, float4(p.yz, 0, 0)).r * weights.x
         + tex2Dlod(ArcaneCloudsSmp, float4(p.zx, 0, 0)).r * weights.y
         + tex2Dlod(ArcaneCloudsSmp, float4(p.xy, 0, 0)).r * weights.z;
}
float3 ArcaneSky(float3 dir)
{
    float3 weights = pow(abs(dir), 8.0);
    weights /= max(dot(weights, float3(1, 1, 1)), 0.0001);
    const float3 maps = ArcaneStars(dir * 1.8 + float3(0.13, 0.37, 0.61), weights);
    const float stars = dot(maps, float3(0.38, 0.55, 0.8));
    const float broad = ArcaneClouds(dir * 0.7 + float3(0.1, 0.3, 0.7), weights);
    const float detail = ArcaneClouds(dir * 2.4 + float3(0.6, 0.8, 0.2), weights);
    const float density = smoothstep(0.18, 0.78, broad) * (0.25 + 0.75 * detail);
    const float tint = smoothstep(-0.8, 0.8, dot(dir, normalize(float3(0.6, 0.2, -0.7))));
    const float3 nebula = lerp(float3(0.025, 0.05, 0.17), float3(0.015, 0.14, 0.11), tint);
    return float3(0.001, 0.002, 0.006) + nebula * density + stars * float3(0.8, 0.9, 1.0);
}
#endif
// The camera's axes in the world (y up); it looks along -CamBack. 0 = left eye, 1 = right.
uniform float3 CamRight0 < hidden = true; > = float3(1.0, 0.0, 0.0);
uniform float3 CamUp0 < hidden = true; > = float3(0.0, 1.0, 0.0);
uniform float3 CamBack0 < hidden = true; > = float3(0.0, 0.0, 1.0);
uniform float3 CamRight1 < hidden = true; > = float3(1.0, 0.0, 0.0);
uniform float3 CamUp1 < hidden = true; > = float3(0.0, 1.0, 0.0);
uniform float3 CamBack1 < hidden = true; > = float3(0.0, 0.0, 1.0);
// Each eye's projection: M[0], M[5], M[8], M[9].
uniform float4 SkyProj0 < hidden = true; > = float4(0.5625, 1.0, 0.0, 0.0);
uniform float4 SkyProj1 < hidden = true; > = float4(0.5625, 1.0, 0.0, 0.0);
uniform float3 SkyZenith < hidden = true; > = float3(0.10, 0.13, 0.18);
uniform float3 SkyHorizon < hidden = true; > = float3(0.35, 0.30, 0.28);
uniform float3 SunDir < hidden = true; > = float3(0.0, 0.2, 1.0);
uniform float SkySun < hidden = true; > = 0.5;          // 0 = no sun
uniform float SkyClouds < hidden = true; > = 0.7;       // cover 0..1
uniform float SkyStars < hidden = true; > = 0.0;
uniform float SkyBrightness < hidden = true; > = 1.0;
uniform float SkyTex < hidden = true; > = 0.0;        // 0 = drawn here, 1..6 = painted: act 1-5, act 5 snow
uniform bool SkyCapOn < hidden = true; > = true;       // the painted sky has its zenith picture
uniform float Timer < source = "timer"; >;

// Painted skies (tools/gen_sky.py, or any picture), one pair per act. vrcam sets
// the file names as preprocessor definitions from d2r_pcvr.ini [sky] texture_* /
// cap_* (bare names are looked for in reshade-shaders\Textures); a slot with no
// picture keeps the act 1 files here and is never sampled. Pictures of another
// size are scaled to the declared one.
// The band goes round the viewer, wrapped twice round the circle: its bottom
// edge 8 degrees under the horizon, its top 65 degrees up. Above 40 degrees it
// hands over to the cap, a square picture of the clouds seen straight up, laid
// flat over the viewer (a band's top rows would only pinch into a point there).
#ifndef D2R_SKY_ACT1
 #define D2R_SKY_ACT1 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT1
 #define D2R_SKYCAP_ACT1 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT2
 #define D2R_SKY_ACT2 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT2
 #define D2R_SKYCAP_ACT2 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT3
 #define D2R_SKY_ACT3 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT3
 #define D2R_SKYCAP_ACT3 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT4
 #define D2R_SKY_ACT4 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT4
 #define D2R_SKYCAP_ACT4 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT5
 #define D2R_SKY_ACT5 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT5
 #define D2R_SKYCAP_ACT5 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_SNOW
 #define D2R_SKY_SNOW "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_SNOW
 #define D2R_SKYCAP_SNOW "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif

// Each act's night picture, blended in by SkyNightMix; an act without one keeps
// the act 1 day file here and is never sampled.
#ifndef D2R_SKYN_ACT1
 #define D2R_SKYN_ACT1 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT1
 #define D2R_SKYCAPN_ACT1 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT2
 #define D2R_SKYN_ACT2 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT2
 #define D2R_SKYCAPN_ACT2 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT3
 #define D2R_SKYN_ACT3 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT3
 #define D2R_SKYCAPN_ACT3 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT4
 #define D2R_SKYN_ACT4 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT4
 #define D2R_SKYCAPN_ACT4 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT5
 #define D2R_SKYN_ACT5 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT5
 #define D2R_SKYCAPN_ACT5 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_SNOW
 #define D2R_SKYN_SNOW "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_SNOW
 #define D2R_SKYCAPN_SNOW "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
texture2D SkyNBand1Tex < source = D2R_SKYN_ACT1; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand1Smp { Texture = SkyNBand1Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap1Tex < source = D2R_SKYCAPN_ACT1; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap1Smp { Texture = SkyNCap1Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand2Tex < source = D2R_SKYN_ACT2; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand2Smp { Texture = SkyNBand2Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap2Tex < source = D2R_SKYCAPN_ACT2; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap2Smp { Texture = SkyNCap2Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand3Tex < source = D2R_SKYN_ACT3; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand3Smp { Texture = SkyNBand3Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap3Tex < source = D2R_SKYCAPN_ACT3; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap3Smp { Texture = SkyNCap3Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand4Tex < source = D2R_SKYN_ACT4; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand4Smp { Texture = SkyNBand4Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap4Tex < source = D2R_SKYCAPN_ACT4; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap4Smp { Texture = SkyNCap4Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand5Tex < source = D2R_SKYN_ACT5; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand5Smp { Texture = SkyNBand5Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap5Tex < source = D2R_SKYCAPN_ACT5; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap5Smp { Texture = SkyNCap5Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand6Tex < source = D2R_SKYN_SNOW; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand6Smp { Texture = SkyNBand6Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap6Tex < source = D2R_SKYCAPN_SNOW; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap6Smp { Texture = SkyNCap6Tex; AddressU = WRAP; AddressV = WRAP; };

texture2D SkyBand1Tex < source = D2R_SKY_ACT1; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand1Smp { Texture = SkyBand1Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap1Tex < source = D2R_SKYCAP_ACT1; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap1Smp { Texture = SkyCap1Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand2Tex < source = D2R_SKY_ACT2; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand2Smp { Texture = SkyBand2Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap2Tex < source = D2R_SKYCAP_ACT2; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap2Smp { Texture = SkyCap2Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand3Tex < source = D2R_SKY_ACT3; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand3Smp { Texture = SkyBand3Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap3Tex < source = D2R_SKYCAP_ACT3; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap3Smp { Texture = SkyCap3Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand4Tex < source = D2R_SKY_ACT4; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand4Smp { Texture = SkyBand4Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap4Tex < source = D2R_SKYCAP_ACT4; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap4Smp { Texture = SkyCap4Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand5Tex < source = D2R_SKY_ACT5; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand5Smp { Texture = SkyBand5Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap5Tex < source = D2R_SKYCAP_ACT5; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap5Smp { Texture = SkyCap5Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand6Tex < source = D2R_SKY_SNOW; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand6Smp { Texture = SkyBand6Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap6Tex < source = D2R_SKYCAP_SNOW; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap6Smp { Texture = SkyCap6Tex; AddressU = WRAP; AddressV = WRAP; };

// Zenith-to-horizon gradient; below the horizon the haze darkens toward the ground.
float3 SkyGradient(float h)
{
    if (h < 0.0) return lerp(SkyHorizon, SkyHorizon * 0.55, saturate(-h * 3.0));
    return lerp(SkyHorizon, SkyZenith, pow(saturate(h), 0.45));
}

float3 BandAt(int slot, float4 t, bool night)
{
    if (night) {
        if (slot == 1) return tex2Dlod(SkyNBand1Smp, t).rgb;
        if (slot == 2) return tex2Dlod(SkyNBand2Smp, t).rgb;
        if (slot == 3) return tex2Dlod(SkyNBand3Smp, t).rgb;
        if (slot == 4) return tex2Dlod(SkyNBand4Smp, t).rgb;
        if (slot == 5) return tex2Dlod(SkyNBand5Smp, t).rgb;
        return tex2Dlod(SkyNBand6Smp, t).rgb;
    }
    if (slot == 1) return tex2Dlod(SkyBand1Smp, t).rgb;
    if (slot == 2) return tex2Dlod(SkyBand2Smp, t).rgb;
    if (slot == 3) return tex2Dlod(SkyBand3Smp, t).rgb;
    if (slot == 4) return tex2Dlod(SkyBand4Smp, t).rgb;
    if (slot == 5) return tex2Dlod(SkyBand5Smp, t).rgb;
    return tex2Dlod(SkyBand6Smp, t).rgb;
}
float3 CapAt(int slot, float4 t, bool night)
{
    if (night) {
        if (slot == 1) return tex2Dlod(SkyNCap1Smp, t).rgb;
        if (slot == 2) return tex2Dlod(SkyNCap2Smp, t).rgb;
        if (slot == 3) return tex2Dlod(SkyNCap3Smp, t).rgb;
        if (slot == 4) return tex2Dlod(SkyNCap4Smp, t).rgb;
        if (slot == 5) return tex2Dlod(SkyNCap5Smp, t).rgb;
        return tex2Dlod(SkyNCap6Smp, t).rgb;
    }
    if (slot == 1) return tex2Dlod(SkyCap1Smp, t).rgb;
    if (slot == 2) return tex2Dlod(SkyCap2Smp, t).rgb;
    if (slot == 3) return tex2Dlod(SkyCap3Smp, t).rgb;
    if (slot == 4) return tex2Dlod(SkyCap4Smp, t).rgb;
    if (slot == 5) return tex2Dlod(SkyCap5Smp, t).rgb;
    return tex2Dlod(SkyCap6Smp, t).rgb;
}

float3 PaintedSky(float3 dir)
{
    const float el = degrees(asin(clamp(dir.y, -1.0, 1.0)));
    // two turns round the circle, drifting slowly so the clouds move
    const float u = atan2(dir.x, dir.z) / 3.14159265 + Timer * 0.000001 * SkyDrift;
    const float v = (65.0 - el) / 73.0;
    const float w = smoothstep(40.0, 62.0, el);
    const int slot = (int)(SkyTex + 0.5);
    const float n = saturate(SkyNightMix);   // the night picture's share: the same clouds, at night
    float3 c = float3(0.0, 0.0, 0.0);
    if (w < 1.0) {
        const float4 t = float4(u, v, 0, 0);
        c = BandAt(slot, t, false);
        if (n > 0.0) c = lerp(c, BandAt(slot, t, true), n);
    }
    if (w > 0.0) {
        float3 z = SkyZenith;   // no zenith picture: the palette's colour
        if (SkyCapOn) {
            // the cloud layer straight overhead; one tile spans the whole cap
            const float2 p = dir.xz / max(dir.y, 0.2) * 0.4 + Timer * 0.0000004 * SkyDrift * float2(1.0, 0.3);
            const float4 t = float4(p, 0, 0);
            z = CapAt(slot, t, false);
            if (n > 0.0) z = lerp(z, CapAt(slot, t, true), n);
        }
        c = lerp(c, z, w);
    }
    return c;
}

float3 Sky(float3 dir)
{
#if D2R_ARCANE_SKY_CACHE
    if (ArcaneSkyOn) return ArcaneSky(dir);
#endif
    if (SkyTex > 0.5) return PaintedSky(dir) * SkyBrightness;
    const float h = dir.y;
    float3 c = SkyGradient(h);
    const float cs = saturate(dot(dir, normalize(SunDir)));
    const float3 sunTint = float3(1.0, 0.85, 0.6);
    c += SkySun * (pow(cs, 600.0) * 6.0 + pow(cs, 12.0) * 0.35) * sunTint;
    if (h > 0.0) {
        if (SkyStars > 0.0) {
            const float2 g = dir.xz / (h + 0.25) * 220.0;
            const float star = step(0.995, Hash(floor(g))) * smoothstep(0.35, 0.0, length(frac(g) - 0.5));
            c += SkyStars * star * saturate(h * 4.0);
        }
        if (SkyClouds > 0.0) {
            // A flat layer overhead: the direction meets it at dir.xz / dir.y.
            const float2 p = dir.xz / (h + 0.12) * 1.6 + Timer * 0.00001 * float2(1.0, 0.4);
            const float n = Fbm(p);
            const float edge = lerp(0.75, 0.3, saturate(SkyClouds));
            const float dens = smoothstep(edge, edge + 0.25, n) * smoothstep(0.0, 0.15, h);
            const float3 cloud = SkyHorizon * (0.75 + 0.6 * n) + SkySun * 0.15 * sunTint * pow(cs, 4.0);
            c = lerp(c, cloud, dens * 0.9);
        }
    }
    return c * SkyBrightness;
}

