// D2R_Fog.fxh: the fog curve, the void fog and the particle fog.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// How much fog a depth value gets, 0..1. Empty depth (the void) is far: full fog.
// How much the full fog covers, 0..1 (vrcam [fog] caves_strength in caves, else 1): below 1 the far end shows through.
uniform float FogStrength < hidden = true; > = 1.0;
// Eased in over its first 40 units: under 1 the curve leaves the start with no limit to its slope,
// and the clear ground round the hero ended in a hard ring (the owner, 2026-10-28: "a circle of
// clarity around the player ... with a very sharp line defining its border").
float FogAt(float dist)
{
    const float ease = smoothstep(FogStart, FogStart + 40.0, dist);
    return pow(saturate((dist - FogStart) / max(FogEnd - FogStart, 1.0)), FogCurve) * ease * FogStrength;
}
// Each eye's projection, as SkyProj0/1 (x, y the scales, z, w the off-centre shifts), set whenever the fog is on;
// zero until the plugin knows it.
uniform float4 FogProj0 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 FogProj1 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
// The fog is measured from the eye, not along the view: depth gives a point's distance along the view's axis,
// and the same thing 50 degrees off it is 1.56 times as far along its own ray. Measured along the axis, scenery
// lost fog as it slid to the edge of the view and gained it when looked at (a thing 100 units off: 43% of the
// fog straight ahead, 30% at 50 degrees), so the fog shifted with every turn of the head (code review,
// 2026-11-09). FogSlant: the ray's length per unit along the axis at a screen point (uv as the screen's, y down);
// 1, the old measure, while the projection is not known.
float FogSlant(float2 uv, int eye)
{
    const float4 sp = eye == 0 ? FogProj0 : FogProj1;
    if (sp.x == 0.0 || sp.y == 0.0) return 1.0;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return length(float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, 1.0));
}
float FogAmount(float depth, float slant)
{
    return FogAt(NearPlane / max(depth, 1e-7) * slant);    // reverse-Z infinite: d = near / z, along the view's axis
}
// The depth of the first drawn surface straight below duv on the screen (down =
// one screen pixel down, in the depth's uv), 0 when nothing is drawn within
// 1024 pixels: doublings to the first hit (2, 4 ... 1024 pixels), then halvings
// between the last miss and that hit, to the pixel. The first hit alone moved
// in steps from row to row, and a big glow (a fire) over the void came out in
// bands of fog (2026-10-04).
// x: that depth, y: how many pixels down it was found (0 with nothing found).
float2 FirstDepthBelowAt(float2 duv, float down)
{
    float miss = 0.0, hit = 0.0;
    [unroll] for (int s = 1; s <= 10; ++s) {
        const float off = exp2((float)s);
        const bool drawn = DepthAt(duv + float2(0.0, down * off)).x > 1e-6;
        hit = hit > 0.0 ? hit : (drawn ? off : 0.0);
        miss = hit > 0.0 ? miss : off;
    }
    [unroll] for (int b = 0; b < 9; ++b) {
        const float mid = 0.5 * (miss + hit);
        const bool drawn = hit > 0.0 && DepthAt(duv + float2(0.0, down * mid)).x > 1e-6;
        hit = drawn ? mid : hit;
        miss = drawn ? miss : mid;
    }
    return hit > 0.0 ? float2(DepthAt(duv + float2(0.0, down * (hit + 1.0))).x, hit) : float2(0.0, 0.0);
}

// The fog of what is drawn without depth over the void, worked out at an eighth
// of the screen and blurred, so it changes smoothly across the picture. Worked out
// per pixel, from the ground straight below each one, neighbouring columns met
// different things below - tree edges, people, gaps - and rain, glows and the far
// trees of Kurast's docks came out cut into vertical bands (2026-10-05).
// The amount: that ground's fog - what stands over far ground goes with it, what
// stands over near ground (a torch's flame, the rain round the hero) stays clear.
texture2D VoidFogTex { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = R16F; };
texture2D VoidFogTmpTex { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = R16F; };
sampler2D VoidFogSmp { Texture = VoidFogTex; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D VoidFogTmpSmp { Texture = VoidFogTmpTex; AddressU = CLAMP; AddressV = CLAMP; };

float VoidFog(float2 uv, int eye)
{
    if (!FogOn || DepthWidth() < 16) return 0.0;
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float down = UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT;
    const float2 hit = FirstDepthBelowAt(duv, down);
    // As much fog as the thing stands on that ground: what hangs higher over it
    // on the screen keeps more of its light (rain over the hero, sparks), fading
    // out from 64 to 512 pixels up - at 32..256 far smoke over the far ground
    // shone through the fog (2026-10-05). Nothing drawn below: no fog from here.
    // (One column only: the nearest of nine cut a torch's glow into bars.)
    // (its distance from the eye: taken where that ground is on the screen, hit.y pixels down)
    return hit.y > 0.0 ? FogAmount(hit.x, FogSlant(uv + float2(0.0, (hit.y + 1.0) * BUFFER_RCP_HEIGHT), eye)) * (1.0 - smoothstep(64.0, 512.0, hit.y)) : 0.0;
}
float PS_VoidFog(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return VoidFog(uv, 0); }
float PS_VoidFogR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return VoidFog(uv, 1); }
// 9 taps two small texels apart (+-64 screen pixels), binomial weights; across, then down.
// Across only left a torch's glow in bars; the far water's edge the down blur
// thinned is taken in the main pass (the ground right below wins, see there).
float PS_VoidFogBlurH(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 st = float2(16.0 * BUFFER_RCP_WIDTH, 0.0);
    float t = 70.0 * tex2Dlod(VoidFogSmp, float4(uv, 0, 0)).x;
    t += 56.0 * (tex2Dlod(VoidFogSmp, float4(uv + st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - st, 0, 0)).x);
    t += 28.0 * (tex2Dlod(VoidFogSmp, float4(uv + 2.0 * st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - 2.0 * st, 0, 0)).x);
    t += 8.0 * (tex2Dlod(VoidFogSmp, float4(uv + 3.0 * st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - 3.0 * st, 0, 0)).x);
    t += tex2Dlod(VoidFogSmp, float4(uv + 4.0 * st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - 4.0 * st, 0, 0)).x;
    return t / 256.0;
}
float PS_VoidFogBlurV(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 st = float2(0.0, 16.0 * BUFFER_RCP_HEIGHT);
    float t = 70.0 * tex2Dlod(VoidFogTmpSmp, float4(uv, 0, 0)).x;
    t += 56.0 * (tex2Dlod(VoidFogTmpSmp, float4(uv + st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - st, 0, 0)).x);
    t += 28.0 * (tex2Dlod(VoidFogTmpSmp, float4(uv + 2.0 * st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - 2.0 * st, 0, 0)).x);
    t += 8.0 * (tex2Dlod(VoidFogTmpSmp, float4(uv + 3.0 * st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - 3.0 * st, 0, 0)).x);
    t += tex2Dlod(VoidFogTmpSmp, float4(uv + 4.0 * st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - 4.0 * st, 0, 0)).x;
    return t / 256.0;
}


// Sky and added-ceiling composition estimate transparent effects' fog from
// nearby scenery below them. This is not the effects' own depth.
float ParticleFog(float2 uv, float2 duv, int eye)
{
    float t = saturate(tex2Dlod(VoidFogSmp, float4(uv, 0, 0)).x);
    const float down = UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT;
    float dn = 0.0, at = 0.0;   // at: how many pixels down that ground was found
    [unroll] for (int s = 1; s <= 4; ++s) {
        const float d = DepthAt(duv + float2(0.0, down * exp2((float)s))).x;
        at = dn > 0.0 ? at : exp2((float)s);
        dn = dn > 0.0 ? dn : d;
    }
    if (dn > 1e-6) t = max(t, FogAmount(dn, FogSlant(uv + float2(0.0, at * BUFFER_RCP_HEIGHT), eye)));
    return t;
}

