// D2R_Lights.fxh: torch, game and scene light, the fire and torch passes, the void glow.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// Torches light the ceiling (the user, 2026-10-06), found in the picture: fire is
// bright and warm. Each frame the brightest fire of each of 8 x 4 screen cells is
// placed in the world by the depth at it (TorchCandTex), and kept in a list of 16
// lights in the world (TorchTex) - matched to what is there already, new ones into
// free places - for CeilTorchMemory seconds after it was last seen, so the light
// stays when the torch leaves the view (looking straight up at the ceiling over it).
// Positions: the world's x, z wrapped on 64 tiles as the ceiling's; y over the hero's floor.
uniform bool CeilTorches < hidden = true; > = true;
uniform float CeilTorchRadius < hidden = true; > = 20.0;    // [ceiling] torch_radius: world units where a torch's light is down to half
uniform float CeilTorchBright < hidden = true; > = 0.6;     // [ceiling] torch_brightness
uniform float CeilTorchMemory < hidden = true; > = 8.0;     // seconds a torch out of sight still lights
uniform float CeilTorchWarm < hidden = true; > = 0.6;      // [ceiling] torch_warmth: the game's pale torch colour toward fire's orange
uniform float CeilHalo < hidden = true; > = 1.0;            // [ceiling] torch_halo: how much of the game's glow round a flame shows in a cave (1 = all)
uniform float FrameTime < source = "frametime"; >;
static const int kFireW = BUFFER_WIDTH / 16;
static const int kFireH = BUFFER_HEIGHT / 16;
texture2D FireTex { Width = BUFFER_WIDTH / 16; Height = BUFFER_HEIGHT / 16; Format = RGBA32F; };
sampler2D FireSmp { Texture = FireTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
texture2D TorchCandTex { Width = 32; Height = 2; Format = RGBA32F; };   // row 0: place, fire; row 1: colour
sampler2D TorchCandSmp { Texture = TorchCandTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
texture2D TorchTex { Width = 16; Height = 2; Format = RGBA32F; };       // row 0: place, strength; row 1: colour, life
sampler2D TorchSmp { Texture = TorchTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
texture2D TorchOldTex { Width = 16; Height = 2; Format = RGBA32F; };
sampler2D TorchOldSmp { Texture = TorchOldTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };

float4 Texel(sampler2D s, int i, int row, float w, float h) { return tex2Dlod(s, float4((i + 0.5) / w, (row + 0.5) / h, 0, 0)); }
// a to b, the shortest way round the wrap
float3 WrapDelta(float3 a, float3 b)
{
    const float P = 64.0 * CeilScale;
    float3 d = b - a;
    d.xz -= P * round(d.xz / P);
    return d;
}

// The walls are wet rock: a glint where a light's half-way vector meets the normal
// (Blinn), on top of the diffuse. The ceiling had none and read as dry mud beside them.
uniform float CeilWet < hidden = true; > = 0.35;       // [ceiling] wet: how much the rock shines
uniform float CeilBump < hidden = true; > = 0.8;       // [ceiling] detail: world units the picture's light parts stand out
uniform float CeilContrast < hidden = true; > = 1.6;   // [ceiling] contrast: the picture's fine detail times this
// A light's falloff with k = distance / its radius: a bright spot (1.78 at the light), down
// to 1 at the radius, gone at twice it - a quick fall-off, "a bright spot" as the user asked.
// The old 1 / (1 + k^2) was so flat that a torch 9 units under the ceiling lit it almost as
// much 13 units aside as straight above: an even wash, not a spot (the user, 2026-10-07).
float Falloff(float k) { const float x = saturate(1.0 - 0.25 * k * k); return x * x * 1.78; }
float Glint(float3 nrm, float3 toLight, float3 toEye) { return pow(saturate(dot(nrm, normalize(toLight + toEye))), 40.0); }

// The torches' light on a point of the ceiling (wh: world, wrapped; nrm: into the air;
// v: toward the eye). x: diffuse, the colour; the glint in w.
float4 TorchLight(float3 wh, float3 nrm, float3 v)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    float glint = 0.0;
    [loop] for (int i = 0; i < 16; ++i) {
        const float4 L = Texel(TorchSmp, i, 0, 16.0, 2.0);
        if (L.w <= 0.0) continue;
        const float4 C = Texel(TorchSmp, i, 1, 16.0, 2.0);
        const float3 dv = WrapDelta(wh, L.xyz);
        const float dist = length(dv);
        const float3 l = dv / max(dist, 1e-3);
        const float k = dist / max(CeilTorchRadius, 1.0);
        const float fall = Falloff(k) * L.w * saturate(C.w * 3.0);
        sum += C.rgb * fall * (0.35 + 0.65 * saturate(dot(nrm, l)));
        glint += fall * Glint(nrm, l, v);
    }
    return float4(sum, glint);
}

// The lit objects the game itself has near the hero (vrcam reads them from the game's
// rooms; their light from its Objects table): xyz less the hero (his feet), w the
// game's light radius (Lit: a torch 19). With them the fire in the picture is not
// looked for: the picture's depth at a flame was the wall behind it.
uniform bool GameLightsOn < hidden = true; > = false;
uniform float4 GameLightPos[16] < hidden = true; >;
uniform float4 GameLightCol[16] < hidden = true; >;

// Their light on a point of the ceiling (p: less the hero), as TorchLight; a torch's
// reach is CeilTorchRadius, the others' by their radius against a torch's 19. A slow
// flicker of a few percent, each its own, as a flame's light breathes.
float4 GameLight(float3 p, float3 nrm, float3 v)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    float glint = 0.0;
    [loop] for (int i = 0; i < 16; ++i) {
        const float4 L = GameLightPos[i];
        if (L.w <= 0.0) continue;
        const float3 dv = L.xyz - p;
        const float dist = length(dv);
        const float3 l = dv / max(dist, 1e-3);
        const float k = dist / max(CeilTorchRadius * L.w / 19.0, 1.0);
        const float flicker = 0.93 + 0.07 * sin(Timer * 0.011 + i * 2.3) * sin(Timer * 0.0037 + i * 1.1);
        // gone at 2 radii (Falloff), and with the light's distance from the hero (GameLightCol.w, vrcam [ceiling] torch_distance)
        const float fall = Falloff(k) * flicker * GameLightCol[i].w;
        // the game's light colours are pale (a torch's 255/236/176): on grey stone they only
        // brightened it - "not yellow enough" (the user, 2026-10-07); fire's orange mixed in
        const float3 col = lerp(GameLightCol[i].rgb, float3(1.0, 0.58, 0.22), saturate(CeilTorchWarm));
        sum += col * fall * (0.35 + 0.65 * saturate(dot(nrm, l)));
        glint += fall * Glint(nrm, l, v);
    }
    return float4(sum, glint);
}

// Their light on drawn world points (p: less the hero / floorY)
float3 SceneLight(float3 p, float3 nrm)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    [loop] for (int i = 0; i < 16; ++i) {
        const float4 L = GameLightPos[i];
        if (L.w <= 0.0) continue;
        const float3 dv = L.xyz - p;
        const float dist = length(dv);
        const float rad = max(CeilTorchRadius * L.w / 19.0, 1.0);
        if (dist >= rad * 2.0) continue;
        const float k = dist / rad;
        const float3 l = dv / max(dist, 1e-3);
        const float NdotL = saturate(dot(nrm, l));
        const float flicker = 0.93 + 0.07 * sin(Timer * 0.011 + i * 2.3) * sin(Timer * 0.0037 + i * 1.1);
        // Inverse-square quadratic falloff tuned for 1.5x-2x wider reach and distinct bright focal spot
        const float atten = (1.0 / (1.0 + 0.12 * dist + 0.008 * dist * dist)) * Falloff(k);
        const float fall = atten * flicker * GameLightCol[i].w * (0.35 + 0.65 * NdotL);
        const float3 col = lerp(GameLightCol[i].rgb, float3(1.0, 0.58, 0.22), saturate(CeilTorchWarm));
        sum += col * fall * 2.6;
    }
        return sum * CeilTorchBright;
}
float3 SceneLight(float3 p) { return SceneLight(p, float3(0.0, 1.0, 0.0)); }

// The hero's own light in dungeons whose ceiling the game draws (#440: Light Radius "doesnt seem to expand
// the light radius at all"). The game's light round him is left as it is; this modest one of ours reaches
// HeroLightRadius, which vrcam scales by his Light Radius (steamvr/light_radius.h), so a +Light Radius item
// or skill widens it. At his shoulder (HeroLightPos: less the hero / over the area's floor, as GameLightPos).
// None within kHeroLightNear units of the eye, full from twice that: a hand light at full strength lit the
// hero's own face (owner, 2026-11-06).
uniform bool HeroLightOn < hidden = true; > = false;
uniform float3 HeroLightPos < hidden = true; > = float3(0.0, 5.2, 0.0);
uniform float HeroLightRadius < hidden = true; > = 20.0;    // [light] hero_reach x Light Radius: Falloff's 1 here, gone at twice
uniform float HeroLightStrength < hidden = true; > = 0.25; // [light] hero_strength
static const float kHeroLightNear = 4.0;
static const float3 kHeroLightColour = float3(1.0, 0.88, 0.70);   // a lamp's warm white, not a torch's orange
float3 HeroLight(float3 p, float3 nrm, float3 eyeP)
{
    const float3 dv = HeroLightPos - p;
    const float dist = length(dv);
    const float rad = max(HeroLightRadius, 1.0);
    if (dist >= rad * 2.0) return 0.0;
    const float3 l = dv / max(dist, 1e-3);
    const float fromEye = smoothstep(kHeroLightNear, kHeroLightNear * 2.0, length(p - eyeP));
    return kHeroLightColour * (Falloff(dist / rad) * HeroLightStrength * fromEye * (0.35 + 0.65 * saturate(dot(nrm, l))));
}

// ---- The torches' light: finding fire, placing it, keeping it --------------------------

// How much a colour looks like fire: bright, red well over blue.
float Fireness(float3 c) { return saturate((c.r - 0.65) * 4.0) * saturate((c.r - c.b - 0.3) * 3.0); }

// Each texel: the most fire-like of 4 x 4 pixels of its 16 x 16 block (x), where (yz),
// and how much fire the block holds (w: the sum over the 16). The interface is left
// out: the bottom of the screen, and the game's own layer's letters.
float4 PS_Fire(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || !CeilTorches || GameLightsOn) return 0.0;
    const float2 corner = floor(pos.xy) * 16.0;
    float best = 0.0, sum = 0.0;
    float2 at = float2(0.0, 0.0);
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x) {
            const float2 p = (corner + float2(x, y) * 4.0 + 2.0) * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
            float s = p.y < 0.88 ? Fireness(tex2Dlod(FogColorSmp, float4(p, 0, 0)).rgb) : 0.0;
            if (UiMaskOn) s *= saturate(tex2Dlod(GameLayerSmp, float4(p, 0, 0)).a);
            sum += s;
            if (s > best) { best = s; at = p; }
        }
    return float4(best, at, sum);
}

// Each of the 32 candidates: the brightest fire of its screen cell (8 x 4), put in the
// world by the depth there. A flame is often drawn without depth: what it stands on
// right under it (the brazier) is nearer than what is behind it, and is taken instead.
float4 TorchCands(float4 pos, int eye)
{
    if (!CeilOn || !CeilTorches || GameLightsOn) return 0.0;
    const int idx = (int)pos.x, row = (int)pos.y;
    const int cw = (kFireW + 7) / 8, ch = (kFireH + 3) / 4;
    const int gx = idx % 8, gy = idx / 8;
    float best = 0.0;
    float2 at = float2(0.0, 0.0);
    int2 bt = int2(0, 0);
    [loop] for (int y = 0; y < ch; ++y)
        [loop] for (int x = 0; x < cw; ++x) {
            const int2 t = int2(gx * cw + x, gy * ch + y);
            if (t.x >= kFireW || t.y >= kFireH) continue;
            const float4 f = Texel(FireSmp, t.x, t.y, (float)kFireW, (float)kFireH);
            if (f.x > best) { best = f.x; at = f.yz; bt = t; }
        }
    if (best < 0.05) return 0.0;
    // the fire round the brightest point, 5 x 5 blocks (80 px): how big the flame is on the screen
    float area = 0.0;
    [unroll] for (int ay = -2; ay <= 2; ++ay)
        [unroll] for (int ax = -2; ax <= 2; ++ax) {
            const int2 t = bt + int2(ax, ay);
            if (t.x >= 0 && t.y >= 0 && t.x < kFireW && t.y < kFireH) area += Texel(FireSmp, t.x, t.y, (float)kFireW, (float)kFireH).w;
        }
    const float2 duv = UpsideDown ? float2(at.x, 1.0 - at.y) : at;
    const float own = DepthAt(duv).x;
    const float2 below = FirstDepthBelowAt(duv, UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT);
    float d = own;
    if (below.y > 0.0 && below.y <= 64.0 && below.x > own * 1.1) d = below.x;
    if (d <= 1e-6) return 0.0;
    const float z = NearPlane / d;                      // along the view
    if (FogOn && z * FogSlant(at, eye) > FogEnd) return 0.0;   // past the fog's end, from the eye
    const float2 ndc = float2(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0) * z;
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    const float3 e = eye == 0 ? CeilEye0 : CeilEye1;
    const float2 hero = eye == 0 ? CeilHero0 : CeilHero1;
    const float P = 64.0 * CeilScale;
    float3 w = float3(e.x + off.x + hero.x, e.y + off.y, e.z + off.z + hero.y);
    w.xz -= P * floor(w.xz / P);
    // The light by the flame's size in the world: its area on the screen grows as the
    // square of nearness, so it is taken back by the distance (30 units = as seen).
    // A torch's flame ~ 1; a candle's or an ember's a small part - a small fire, a dim light.
    const float res = BUFFER_HEIGHT / 1350.0;   // more pixels to the same flame on a taller screen
    const float size = area * (z / 30.0) * (z / 30.0) / (res * res);
    const float strength = saturate(size / 40.0);
    if (strength < 0.02) return 0.0;
    if (row == 0) return float4(w, strength);
    const float3 col = tex2Dlod(FogColorSmp, float4(at, 0, 0)).rgb;
    return float4(col / max(col.r, 1e-3), 1.0);
}
float4 PS_TorchCands(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TorchCands(pos, 0); }
float4 PS_TorchCandsR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TorchCands(pos, 1); }

// The list of 16 torches: a torch already there is moved toward the candidate seen
// near it and lives on; one not seen fades over CeilTorchMemory seconds; a place
// that was free takes the next candidate no torch is near (the n-th free place the
// n-th such candidate, so every place picks a different one). Two torches that came
// together keep the first.
float4 PS_TorchMerge(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || !CeilTorches || GameLightsOn) return 0.0;
    const int i = (int)pos.x, row = (int)pos.y;
    const float near = 4.0;   // world units: the same torch
    const float4 p0 = Texel(TorchOldSmp, i, 0, 16.0, 2.0), c0 = Texel(TorchOldSmp, i, 1, 16.0, 2.0);
    if (c0.w > 0.0) {
        [loop] for (int k = 0; k < 16; ++k) {
            if (k >= i) break;
            if (Texel(TorchOldSmp, k, 1, 16.0, 2.0).w > 0.0 && length(WrapDelta(p0.xyz, Texel(TorchOldSmp, k, 0, 16.0, 2.0).xyz)) < near)
                return 0.0;
        }
        float bestD = near;
        int bestJ = -1;
        [loop] for (int j = 0; j < 32; ++j) {
            const float4 q = Texel(TorchCandSmp, j, 0, 32.0, 2.0);
            if (q.w < 0.05) continue;
            const float dj = length(WrapDelta(p0.xyz, q.xyz));
            if (dj < bestD) { bestD = dj; bestJ = j; }
        }
        if (bestJ >= 0) {
            const float4 q = Texel(TorchCandSmp, bestJ, 0, 32.0, 2.0);
            if (row == 0) return float4(p0.xyz + WrapDelta(p0.xyz, q.xyz) * 0.3, lerp(p0.w, q.w, 0.3));
            return float4(lerp(c0.rgb, Texel(TorchCandSmp, bestJ, 1, 32.0, 2.0).rgb, 0.3), 1.0);
        }
        const float life = c0.w - FrameTime * 0.001 / max(CeilTorchMemory, 0.1);
        if (life <= 0.0) return 0.0;
        return row == 0 ? p0 : float4(c0.rgb, life);
    }
    int rank = 0;   // free places before this one
    [loop] for (int k2 = 0; k2 < 16; ++k2) {
        if (k2 >= i) break;
        if (Texel(TorchOldSmp, k2, 1, 16.0, 2.0).w <= 0.0) ++rank;
    }
    int n = 0;
    [loop] for (int j2 = 0; j2 < 32; ++j2) {
        const float4 q = Texel(TorchCandSmp, j2, 0, 32.0, 2.0);
        if (q.w < 0.05) continue;
        bool taken = false;
        [loop] for (int k3 = 0; k3 < 16; ++k3)
            if (Texel(TorchOldSmp, k3, 1, 16.0, 2.0).w > 0.0 && length(WrapDelta(q.xyz, Texel(TorchOldSmp, k3, 0, 16.0, 2.0).xyz)) < near) { taken = true; break; }
        [loop] for (int k4 = 0; k4 < 32; ++k4) {
            if (taken || k4 >= j2) break;
            const float4 o = Texel(TorchCandSmp, k4, 0, 32.0, 2.0);
            if (o.w >= 0.05 && length(WrapDelta(q.xyz, o.xyz)) < near) taken = true;
        }
        if (taken) continue;
        if (n == rank) return row == 0 ? q : float4(Texel(TorchCandSmp, j2, 1, 32.0, 2.0).rgb, 1.0);
        ++n;
    }
    return 0.0;
}
float4 PS_TorchStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(TorchSmp, uv); }

// The glow and the flames the game draws over the void (no depth there), warm only (not the grey
// spill round people), averaged into 1/8 and 1/32 of the screen with how much of each block is
// void: a cut thing's place is filled from the void round it, smoothly, whatever its size.
texture2D VoidGlow8Tex { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = RGBA16F; };
sampler2D VoidGlow8Smp { Texture = VoidGlow8Tex; AddressU = CLAMP; AddressV = CLAMP; };
texture2D VoidGlow32Tex { Width = BUFFER_WIDTH / 32; Height = BUFFER_HEIGHT / 32; Format = RGBA16F; };
sampler2D VoidGlow32Smp { Texture = VoidGlow32Tex; AddressU = CLAMP; AddressV = CLAMP; };
float4 PS_VoidGlow8(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || SkyOn) return 0.0;
    const int2 base = int2(pos.xy) * 8;
    float4 sum = 0.0;
    [loop] for (int y = 0; y < 8; y += 2)
        [loop] for (int x = 0; x < 8; x += 2) {
            const float2 u = (base + int2(x, y) + 0.5) / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
            if (DepthAt(UpsideDown ? float2(u.x, 1.0 - u.y) : u).x > 1e-6) continue;
            const float3 c = tex2Dlod(FogColorSmp, float4(u, 0, 0)).rgb;
            sum += float4(c * saturate((c.r - c.b) * 15.0), 1.0);
        }
    return sum / 16.0;   // premultiplied: rgb = glow x the void's share, a = the void's share
}
float4 PS_VoidGlow32(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || SkyOn) return 0.0;
    const int2 base = int2(pos.xy) * 4;
    float4 sum = 0.0;
    [loop] for (int y = 0; y < 4; ++y)
        [loop] for (int x = 0; x < 4; ++x)
            sum += tex2Dfetch(VoidGlow8Smp, base + int2(x, y));
    return sum / 16.0;
}
float3 VoidGlowAt(float2 uv)
{
    const float4 g8 = tex2Dlod(VoidGlow8Smp, float4(uv, 0, 0));
    const float4 g32 = tex2Dlod(VoidGlow32Smp, float4(uv, 0, 0));
    const float3 wide = g32.a > 1e-3 ? g32.rgb / g32.a : 0.0;
    const float3 close = g8.a > 1e-3 ? g8.rgb / g8.a : wide;
    return lerp(wide, close, saturate(g8.a * 4.0));
}

