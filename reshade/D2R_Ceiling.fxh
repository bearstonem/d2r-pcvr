// D2R_Ceiling.fxh: the cave ceiling: height map, peaks and Ceiling().
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// The far wall (2026-10-07, the user: "paint the void in the distance - the cave's
// texture round the hero, 200 away"): past the floor's edge the void stayed black under
// the ceiling. A cylinder of the same stone CeilWallDist round the hero closes the cave:
// what the ceiling's ray does not meet first (looking level or down, or the ceiling past
// the wall) meets the wall, sunk in the fog by its distance like the rest. It goes with
// the hero - far off in the fog that does not show. 0 = none.
uniform float CeilWallDist < hidden = true; > = 200.0;   // [ceiling] wall_distance
// And a floor under the real one (the user, 2026-10-07): a ray going down past the drawn
// floor's edge meets stone CeilFloorDepth under the hero's floor before the wall. 0 = none.
uniform float CeilFloorDepth < hidden = true; > = 5.0;  // [ceiling] floor_depth

// A dome over what stands higher than the ceiling (the user, 2026-10-07: the cathedral's
// altar canopy rose through it - "bend the ceiling there, evenly, as a sphere"). Every
// frame a compute pass puts each drawn pixel back in the world and keeps the highest
// point per tile in a 64 x 64 map over the wrap (HBits, atomic max, in 1/16 units); the
// map keeps the most ever seen until the area changes (CeilMapGen, vrcam). Over a tile
// whose top is above the ceiling, the ceiling rises as a sphere CeilDomeRadius round,
// CeilDomeClear over that top, by CeilDomeMax at most; elsewhere it stays CeilHeight.
uniform bool CeilDome < hidden = true; > = false;           // [ceiling] dome_<biome>
uniform float CeilDomeRadius < hidden = true; > = 30.0;     // [ceiling] dome_radius_<biome>
uniform float CeilDomeMax < hidden = true; > = 30.0;        // [ceiling] dome_max_<biome>
uniform float CeilDomeClear < hidden = true; > = 3.0;       // world units over the top it covers
// Only what reaches over this gets a dome (the user, 2026-10-07: "a slider to find where to round
// it" - over the altar's canopy, not the columns); under the ceiling's height it is that height.
uniform float CeilDomeFind < hidden = true; > = 0.0;        // [ceiling] dome_find_<biome>
float DomeLine() { return max(CeilHeight, CeilDomeFind); }
uniform float CeilMapGen < hidden = true; > = 0.0;          // vrcam: another area - the map starts again
texture2D HBitsTex { Width = 64; Height = 64; Format = R32U; };
storage2D<uint> HBitsSt { Texture = HBitsTex; };
sampler2D<uint> HBitsSmp { Texture = HBitsTex; };
texture2D SupHiTex { Width = 64; Height = 64; Format = R32U; };   // this frame's points over the dome's line
storage2D<uint> SupHiSt { Texture = SupHiTex; };
sampler2D<uint> SupHiSmp { Texture = SupHiTex; };
texture2D SupTex { Width = 64; Height = 64; Format = RGBA32F; };      // a: those points, kept (not one bat - many)
sampler2D SupSmp { Texture = SupTex; MinFilter = POINT; MagFilter = POINT; };
texture2D SupOldTex { Width = 64; Height = 64; Format = RGBA32F; };
sampler2D SupOldSmp { Texture = SupOldTex; MinFilter = POINT; MagFilter = POINT; };
texture2D HMapTex { Width = 64; Height = 64; Format = R32F; };      // the highest point per tile, kept
sampler2D HMapSmp { Texture = HMapTex; AddressU = WRAP; AddressV = WRAP; MinFilter = POINT; MagFilter = POINT; };
texture2D HMapOldTex { Width = 64; Height = 64; Format = R32F; };
sampler2D HMapOldSmp { Texture = HMapOldTex; MinFilter = POINT; MagFilter = POINT; };
texture2D HMapGenTex { Width = 1; Height = 1; Format = R32F; };     // the CeilMapGen HMapOld belongs to
sampler2D HMapGenSmp { Texture = HMapGenTex; MinFilter = POINT; MagFilter = POINT; };

void RecordHeights(uint3 id, int eye)
{
    if (!CeilOn || !CeilDome || id.x >= BUFFER_WIDTH || id.y >= BUFFER_HEIGHT) return;
    const float2 uv = (id.xy + 0.5) / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    const float d = DepthAt(UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv).x;
    if (d <= 1e-6) return;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0) * (NearPlane / d);
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    const float P = 64.0 * CeilScale;
    if (abs(off.x) > 0.45 * P || abs(off.z) > 0.45 * P) return;   // a tile once round the wrap is another place
    const float3 e = eye == 0 ? CeilEye0 : CeilEye1;
    const float h = e.y + off.y;
    if (h < 1.0) return;   // the floor
    const float2 w = (e.xz + off.xz + (eye == 0 ? CeilHero0 : CeilHero1)) / CeilScale;
    const int2 cell = int2(w - 64.0 * floor(w / 64.0)) & 63;
    atomicMax(HBitsSt, cell, (uint)(min(h, 2000.0) * 16.0));
    if (h > DomeLine()) atomicAdd(SupHiSt, cell, 1u);
}
void CS_RecordHeights(uint3 id : SV_DispatchThreadID) { RecordHeights(id, 0); }
void CS_RecordHeightsR(uint3 id : SV_DispatchThreadID) { RecordHeights(id, 1); }
void CS_ClearHeights(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(HBitsSt, int2(id.xy), 0u);
    tex2Dstore(SupHiSt, int2(id.xy), 0u);
}
// the points over the dome's line, kept (none kept from another area)
float4 PS_SupMerge(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const int2 c = int2(pos.xy);
    const bool same = abs(tex2Dfetch(HMapGenSmp, int2(0, 0)).x - CeilMapGen) < 0.5;
    const float old = same ? tex2Dfetch(SupOldSmp, c).a : 0.0;
    return float4(0.0, 0.0, 0.0, min(old + (float)tex2Dfetch(SupHiSmp, c).x, 1e6));
}
float4 PS_SupStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2Dfetch(SupSmp, int2(pos.xy)); }
// this frame's highest points on top of what was kept (none kept from another area)
float PS_HMapMerge(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const int2 c = int2(pos.xy);
    const float now = tex2Dfetch(HBitsSmp, c).x / 16.0;
    const bool same = abs(tex2Dfetch(HMapGenSmp, int2(0, 0)).x - CeilMapGen) < 0.5;
    return max(now, same ? tex2Dfetch(HMapOldSmp, c).x : 0.0);
}
float PS_HMapStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2Dfetch(HMapSmp, int2(pos.xy)).x; }
float PS_HMapGenStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return CeilMapGen; }
// One smooth dome over each thing that stands over the ceiling (the user, 2026-10-07: "a flat
// ceiling, and over the church one even round dome" - a sphere per tile made it lumpy). A peak
// is a tile drawn over the ceiling often (not one bat) and the highest such within
// CeilDomeRadius; its dome stands over the middle of the tiles over the ceiling round it
// (weighted by how far they reach over), CeilDomeClear over the peak's top, an even half
// ellipsoid CeilDomeRadius round down to the ceiling. Each tile keeps its nearest peak, and a
// point takes the domes of the 3 x 3 tiles round it - all found in parallel, every frame.
texture2D PeakTex { Width = 64; Height = 64; Format = RGBA32F; };       // rg: the dome's middle (tiles, wrapped), b: its top, a: 1 = one
sampler2D PeakSmp { Texture = PeakTex; MinFilter = POINT; MagFilter = POINT; };
texture2D DomeSeedTex { Width = 64; Height = 64; Format = RGBA32F; };   // the nearest peak, as PeakTex
sampler2D DomeSeedSmp { Texture = DomeSeedTex; MinFilter = POINT; MagFilter = POINT; };
bool DomeTall(int2 c) { return tex2Dfetch(SupSmp, c & 63).a >= 100.0 && tex2Dfetch(HMapSmp, c & 63).x > DomeLine() + 0.5; }
float2 WrapTiles(float2 d) { return d - 64.0 * round(d / 64.0); }
float4 PS_Peaks(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilDome) return 0.0;
    const int2 c = int2(pos.xy);
    if (!DomeTall(c)) return 0.0;
    const float me = tex2Dfetch(HMapSmp, c).x;
    const float Rt = max(CeilDomeRadius, CeilScale) / CeilScale;   // tiles
    const int n = min((int)ceil(Rt), 8);
    float w = 0.0;
    float2 sum = 0.0;
    [loop] for (int y = -8; y <= 8; ++y) {
        if (abs(y) > n) continue;
        [loop] for (int x = -8; x <= 8; ++x) {
            if (abs(x) > n || length(float2(x, y)) > Rt) continue;
            const int2 cn = c + int2(x, y);
            if (!DomeTall(cn)) continue;
            const float h = tex2Dfetch(HMapSmp, cn & 63).x;
            // a higher one near (or as high, before this one): that one is the peak
            if (h > me || (h == me && (y < 0 || (y == 0 && x < 0)))) return 0.0;
            const float wi = h - DomeLine();
            w += wi; sum += wi * float2(x, y);
        }
    }
    const float2 mid = c + 0.5 + sum / max(w, 1e-3);
    return float4(mid - 64.0 * floor(mid / 64.0), me, 1.0);
}
float4 PS_DomeSeeds(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilDome) return 0.0;
    const int2 c = int2(pos.xy);
    const float Rt = max(CeilDomeRadius, CeilScale) / CeilScale;
    const int n = min((int)ceil(Rt) + 1, 9);
    float best = 1e6;
    float4 at = 0.0;
    [loop] for (int y = -9; y <= 9; ++y) {
        if (abs(y) > n) continue;
        [loop] for (int x = -9; x <= 9; ++x) {
            if (abs(x) > n) continue;
            const float4 pk = tex2Dfetch(PeakSmp, (c + int2(x, y)) & 63);
            if (pk.a < 0.5) continue;
            const float d = length(WrapTiles(pk.rg - (c + 0.5)));
            if (d < best) { best = d; at = pk; }
        }
    }
    return at;
}
// The ceiling's plane at q (tiles, wrapped): its own height, or a dome's
float CeilTopAt(float2 q)
{
    if (!CeilDome) return CeilHeight;
    float top = CeilHeight;
    const float R = max(CeilDomeRadius, CeilScale);
    const int2 c = int2(floor(q));
    [loop] for (int y = -1; y <= 1; ++y)
        [loop] for (int x = -1; x <= 1; ++x) {
            const float4 d = tex2Dfetch(DomeSeedSmp, (c + int2(x, y)) & 63);
            if (d.a < 0.5) continue;
            const float r = length(WrapTiles(q - d.rg)) * CeilScale / R;
            if (r >= 1.0) continue;
            top = max(top, CeilHeight + (d.b + CeilDomeClear - CeilHeight) * sqrt(1.0 - r * r));
        }
    return min(top, CeilHeight + max(CeilDomeMax, 0.0));
}

// The stone's underside at q (tiles, wrapped), over the hero's floor
float CeilUnder(float2 q)
{
    const float R = max(CeilRelief, 0.0);
    return CeilTopAt(q) - (R > 1e-3 ? R * CeilRock(q) : 0.0);
}

bool Ceiling(float3 dir, int eye, out float3 c, float tMax)
{
    c = float3(0.0, 0.0, 0.0);
    const float3 e = eye == 0 ? CeilEye0 : CeilEye1;
    const float2 hero = eye == 0 ? CeilHero0 : CeilHero1;   // the hero in the world (wrapped)
    const float R = max(CeilRelief, 0.0);
    const float3 back = eye == 0 ? CamBack0 : CamBack1;
    const float ahead = max(dot(dir, -back), 0.0);          // view depth per unit along the ray
    // the wall: where the ray leaves the cylinder (the eye is inside it)
    const float W = CeilWallDist;
    float tw = 1e9;
    if (W > 1.0) {
        const float qa = dot(dir.xz, dir.xz), qb = dot(e.xz, dir.xz), qc = dot(e.xz, e.xz) - W * W;
        if (qa > 1e-6) tw = (-qb + sqrt(max(qb * qb - qa * qc, 0.0))) / qa;
    }
    const bool ceilRay = CeilHeight - e.y > 0.0 && dir.y > 1e-3;
    // the floor under the floor: a plane, nearer than the wall
    float tf = 1e9;
    if (CeilFloorDepth > 0.0 && dir.y < -1e-3) tf = (-CeilFloorDepth - e.y) / dir.y;
    const bool onFloor = tf < tw;
    if (onFloor) tw = tf;
    if (!ceilRay && tw > 1e8) return false;
    float t = tw, h = 0.5;
    bool onWall = true;
    if (ceilRay) {
        // the band the rock hangs in: from its lowest reach to the plane (a dome's highest)
        const float topHi = CeilHeight + (CeilDome ? max(CeilDomeMax, 0.0) : 0.0);
        const float t0 = max(CeilHeight - R - e.y, 0.0) / dir.y;
        float t1 = min((topHi - e.y) / dir.y, tMax);
        if (t0 < tw) {
            // past the full fog nothing shows: the fog's colour, no march
            if (FogOn && FogStrength >= 0.999 && t0 * ahead > FogEnd) { c = FogColor; return true; }
            t = t1;
            if (R > 1e-3 || CeilDome) {
                if (FogOn) t1 = min(t1, FogEnd / max(ahead, 0.05));
                // the vault's thin ribs want finer steps than lumpy rock; a dome's band is tall
                const int n = clamp(CeilDome || PicRelief() ? max(CeilSteps, 32) : CeilSteps, 1, 64);
                const float dt = (t1 - t0) / n;
                float tPrev = t0, gPrev = -1.0;   // g: the ray's height above the rock's underside (> 0 = in the rock)
                [loop] for (int i = 0; i <= n; ++i) {
                    const float ti = t0 + dt * i;
                    const float2 q = (e.xz + dir.xz * ti + hero) / CeilScale;
                    const float g = e.y + dir.y * ti - CeilUnder(q);
                    if (g >= 0.0) {
                        t = ti;
                        if (i > 0) {
                            // halved 5 times between the last step outside and the first inside: the
                            // vault's edges against the far stone stood in steps (2026-10-07)
                            float ta = tPrev, tb = ti;
                            [loop] for (int b = 0; b < 5; ++b) {
                                const float tm = 0.5 * (ta + tb);
                                const float2 qm = (e.xz + dir.xz * tm + hero) / CeilScale;
                                if (e.y + dir.y * tm - CeilUnder(qm) >= 0.0) tb = tm; else ta = tm;
                            }
                            t = 0.5 * (ta + tb);
                        }
                        break;
                    }
                    tPrev = ti; gPrev = g;
                }
            }
            onWall = t > tw;   // the ceiling past the wall: the wall
            if (onWall) t = tw;
        }
    }
    const float3 hit = float3(e.x, e.y, e.z) + dir * t;     // less the hero (his feet)
    // Where on the stone, and its axes: on the ceiling the world's x, z (tiles); on the wall
    // round it and down (the angle round the hero times the radius, the height)
    float2 q;
    float3 nrm, tanU, tanV;
    if (!onWall) {
        q = (hit.xz + hero) / CeilScale;
        h = R > 1e-3 ? CeilRock(q) : 0.0;
        // the normal into the air (the rock is above): -(R dh/dx, 1, R dh/dz), and a dome's slope
        const float k = PicRelief() ? 0.02 : 0.08;   // tiles (a brick's joint is fine)
        const float ux = (CeilUnder(q + float2(k, 0.0)) - CeilUnder(q - float2(k, 0.0))) / (2.0 * k * CeilScale);
        const float uz = (CeilUnder(q + float2(0.0, k)) - CeilUnder(q - float2(0.0, k))) / (2.0 * k * CeilScale);
        nrm = normalize(float3(ux, -1.0, uz));
        tanU = float3(1.0, 0.0, 0.0); tanV = float3(0.0, 0.0, 1.0);
        if (abs(nrm.y) < 0.5) {
            // a raised wall: the picture along it and up it, not smeared down it from above
            const float2 side = normalize(nrm.xz);
            const float2 along = float2(-side.y, side.x);
            q = float2(dot(hit.xz + hero, along), -hit.y) / CeilScale;
            nrm = float3(side.x, 0.0, side.y);
            tanU = float3(along.x, 0.0, along.y); tanV = float3(0.0, -1.0, 0.0);
            h = 0.5;
        }
    } else if (onFloor) {
        q = (hit.xz + hero) / CeilScale;
        nrm = float3(0.0, 1.0, 0.0);
        tanU = float3(1.0, 0.0, 0.0); tanV = float3(0.0, 0.0, 1.0);
    } else {
        const float2 radial = hit.xz / max(length(hit.xz), 1e-3);
        q = float2(atan2(radial.y, radial.x) * W, -hit.y) / CeilScale;
        nrm = float3(-radial.x, 0.0, -radial.y);   // toward the hero
        tanU = float3(-radial.y, 0.0, radial.x); tanV = float3(0.0, -1.0, 0.0);
    }
    // The stone: the picture (its mip by one pixel's footprint, stretched where the ray
    // grazes), or dark brown-grey mottled. Its fine detail is pushed up (the picture less
    // its blur three mips up, times CeilContrast), and its light parts stand out of the
    // rock by CeilBump: the normal tilts with the picture's slope, so every grain and crack
    // catches the light the way the walls' normal maps do.
    float3 stone;
    if (CeilTexOn) {
        const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
        const float foot = t * 2.0 / (abs(sp.y) * BUFFER_HEIGHT) / max(-dot(dir, nrm), 0.05);   // world units
        const float lod = log2(max(foot / TexSpan() * 1024.0, 1.0));
        const float2 uvT = q * CeilScale / TexSpan();
        const float3 s0 = CeilPic(uvT, lod);
        const float3 blur = CeilPic(uvT, lod + 3.0);
        stone = max(blur + (s0 - blur) * CeilContrast, 0.0) * 0.55;
        // A finer layer: the same picture 5.3 times smaller, only its fine detail - one 1024 picture
        // over 40 units stood soft beside the game's walls (2026-10-07). Its own mips put it out far off.
        if (CeilFineDetail > 0.0) {
            const float k = 5.3, lodF = lod + log2(k);
            const float3 f0 = CeilPic(uvT * k + 0.37, lodF), f1 = CeilPic(uvT * k + 0.37, lodF + 2.0);
            stone = max(stone + (f0 - f1) * 0.55 * CeilFineDetail, 0.0);
        }
        const float du = exp2(lod) * 1.5 / 1024.0;   // a texel and a half at that mip, in uv
        const float3 lw = float3(0.2126, 0.7152, 0.0722);
        const float lx = dot(CeilPic(uvT + float2(du, 0.0), lod) - CeilPic(uvT - float2(du, 0.0), lod), lw);
        const float lz = dot(CeilPic(uvT + float2(0.0, du), lod) - CeilPic(uvT - float2(0.0, du), lod), lw);
        const float perWorld = 1.0 / (2.0 * du * TexSpan());   // the slope per world unit
        nrm = normalize(nrm - CeilBump * perWorld * (lx * tanU + lz * tanV));
    } else stone = lerp(float3(0.16, 0.13, 0.10), float3(0.30, 0.25, 0.20), NoiseP(q * 6.0, 384.0));
    // the hero's light (as from a torch at his shoulder), dying with the distance from
    // him - lit over his head, dark toward the walls, which hides where their tops meet
    // the ceiling - and a dim fixed fill from one side, so the far rock keeps a shape
    const float3 toEye = -dir;
    const float3 fromHero = float3(0.0, 0.7 * e.y, 0.0) - hit;
    const float3 lHero = fromHero / max(length(fromHero), 1e-3);
    const float dHero = length(fromHero) / max(CeilLightRadius, 1.0);
    const float fall = Falloff(dHero);
    // (the fill and the ambient kept low: with more, the rock went flat and grey - 2026-10-07)
    const float lit = 1.0 * fall * saturate(dot(nrm, lHero))
                    + 0.18 * saturate(dot(nrm, normalize(float3(0.5, -1.0, 0.3)))) + 0.04;
    float3 light = lit.xxx;
    float glint = 0.8 * fall * Glint(nrm, lHero, toEye);
    if (CeilTorches) {
        const float4 tl = CeilTorchBright * (GameLightsOn ? GameLight(hit, nrm, toEye)
                                                          : TorchLight(float3(hit.x + hero.x, hit.y, hit.z + hero.y), nrm, toEye));
        light += tl.rgb;
        glint += tl.w;
    }
    // the hollows (up at the plane) darker, the tips lighter; the glint is the light's own colour, not the stone's
    c = (stone * light + CeilWet * glint * float3(1.0, 0.9, 0.75)) * lerp(0.55, 1.0, h) * CeilBrightness;
    if (FogOn) c = lerp(c, FogColor, FogAt(t * ahead));
    return true;
}

