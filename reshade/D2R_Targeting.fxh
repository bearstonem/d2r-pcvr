// D2R_Targeting.fxh: the target outline, the landing circles (Teleport's sigil among them), the loot beams and Teleport's pillar, Outline().
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// Target outline: a glowing edge round the 3D model of the monster the target
// lock holds, coloured by range. The model is found from depth: each pixel is
// put back in the world (as the table's edge test does) and, inside a cylinder
// round the target's cell, the pixels standing above the floor are the model
// (OutlineShape: radius, lowest and highest height). A pixel whose neighbours
// fall outside that shape is on its edge, so walls and other monsters in front
// hide it. vrcam: each eye relative to the hero, in world units; OutlinePos xyz
// the cylinder's foot relative to the hero, w strength, 0 = none.
uniform bool OutlineOn < hidden = true; > = false;
uniform float3 OutlineEye0 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 OutlineEye1 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float4 OutlinePos < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float3 OutlineShape < hidden = true; > = float3(2.6, 0.15, 4.0);
uniform float3 OutlineColor < hidden = true; > = float3(1.0, 0.2, 0.12);
// A second aimed monster (the other hand's), drawn like the first in its own color.
uniform float4 Outline2Pos < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float3 Outline2Shape < hidden = true; > = float3(2.6, 0.15, 4.0);
uniform float3 Outline2Color < hidden = true; > = float3(0.2, 0.5, 1.0);
uniform float OutlineWidth < hidden = true; > = 1.0;   // how far out a neighbour is looked for, in pixels: the edge's thickness
uniform float OutlineJump < hidden = true; > = 6.0;    // a neighbour this close in 3D still belongs to the body (world units)
uniform float OutlineInner < hidden = true; > = 0.05;  // a faint tint over the whole body, a share of the strength

// The world point (relative to the hero) the eye sees at uv; ok = 0 for the sky.
float3 OutlineWorld(float2 uv, int eye, out float ok)
{
    const float d = DepthAt(UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv).x;
    ok = d > 0.0 ? 1.0 : 0.0;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    return (eye == 0 ? OutlineEye0 : OutlineEye1) + off * (NearPlane / max(d, 1e-6));
}
// Standing above the floor (between the shape's lowest and highest height).
float Raised(float3 q, float4 o, float3 shape)
{
    const float h = q.y - o.y;
    return step(shape.y, h) * step(h, shape.z);
}
// Inside the search cylinder round the target (and not sky).
float Near(float3 q, float ok, float4 o, float3 shape)
{
    return ok * step(length(q.xz - o.xz), shape.x);
}
static const float2 kOutlineDirs[8] = { float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                                       float2(0.707, 0.707), float2(-0.707, 0.707), float2(0.707, -0.707), float2(-0.707, -0.707) };
// Only height edges inside the cylinder count: its own boundary never draws.
float3 OutlineTint(float3 c, float2 uv, int eye, float3 p, float4 o, float3 shape, float3 colour)
{
    if (o.w <= 0.0 || length(p.xz - o.xz) > shape.x) return c;
    const float centre = Raised(p, o, shape);
    if (centre <= 0.0) return c;
    // The glow sits on the model's own edge pixels. A neighbour belongs to the
    // model when it is raised too and either inside the cylinder or right next
    // to this pixel in the world; the background behind a monster's head is far
    // away (a depth jump), so its silhouette edges glow, not just its feet.
    const float jump = OutlineJump;
    float edge = 0.0;
    [unroll] for (int k = 0; k < 8; ++k) {   // one ring of neighbours: a thin line
        float ok;
        const float3 q = OutlineWorld(uv + kOutlineDirs[k] * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT) * OutlineWidth, eye, ok);
        const float model = ok * Raised(q, o, shape) * max(Near(q, ok, o, shape), step(length(q - p), jump));
        edge += 1.0 - model;
    }
    const float a = max(saturate(edge / 3.0), OutlineInner) * o.w;
    return lerp(c, colour, saturate(a));   // painted over the picture: subtle at low strength
}
// Landing circles: where and when a ground skill will hit, painted on the ground
// the eye actually sees (the same world point per pixel as the outline), so
// monsters and walls standing in front hide it. One per cast or bomb in the air:
// RingPos xyz the centre relative to the hero, w its alpha (0 = none); RingFill
// how far the fill has grown from the center outward; RingCol rgb its element's
// color, w its radius (world units); RingDir a strip instead (Fire Wall): xy its
// direction on the ground, z its half-length, 0 = a circle.
uniform bool RingOn < hidden = true; > = false;
uniform float RingFloorTol < hidden = true; > = 1.0;
uniform float4 RingPos0 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingPos1 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingPos2 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingPos3 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingPos4 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingPos5 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float RingFill0 < hidden = true; > = 0.0;
uniform float RingFill1 < hidden = true; > = 0.0;
uniform float RingFill2 < hidden = true; > = 0.0;
uniform float RingFill3 < hidden = true; > = 0.0;
uniform float RingFill4 < hidden = true; > = 0.0;
uniform float RingFill5 < hidden = true; > = 0.0;
uniform float4 RingCol0 < hidden = true; > = float4(1.0, 0.45, 0.05, 5.0);
uniform float4 RingCol1 < hidden = true; > = float4(1.0, 0.45, 0.05, 5.0);
uniform float4 RingCol2 < hidden = true; > = float4(1.0, 0.45, 0.05, 5.0);
uniform float4 RingCol3 < hidden = true; > = float4(1.0, 0.45, 0.05, 5.0);
uniform float4 RingCol4 < hidden = true; > = float4(1.0, 0.45, 0.05, 5.0);
uniform float4 RingCol5 < hidden = true; > = float4(1.0, 0.45, 0.05, 5.0);
uniform float4 RingDir0 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingDir1 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingDir2 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingDir3 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingDir4 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 RingDir5 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);

// How much the surface at p faces up: 1 floor (slopes and bumps nearly so),
// 0 a leg, wall or rock side. From its neighbours stepPx pixels right and down;
// 1 where a neighbour has no depth, so the floor's own edges still paint.
float FacingUp(float2 uv, int eye, float3 p, float stepPx)
{
    float ok1, ok2;
    const float2 px = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT) * stepPx;
    const float3 a = OutlineWorld(uv + float2(px.x, 0.0), eye, ok1) - p;
    const float3 b = OutlineWorld(uv + float2(0.0, px.y), eye, ok2) - p;
    const float3 n = cross(a, b);
    const float len = length(n);
    return (ok1 * ok2 > 0.0 && len > 1e-6) ? abs(n.y) / len : 1.0;
}
// The landing circles' look: Jeff's tuned values from the DEV window, 2026-10-20.
// Sizes are shares of the radius; strengths are 0..1. (Its height band,
// RingFloorTol, comes from vrcam.)
static const float kCircleOpacity = 0.32;       // the whole circle's strength (more transparent)
static const float kCircleRim = 0.015;          // rim width (narrower)
static const float kCircleRimStrength = 0.85;   // outline strength
static const float kCircleBase = 0.02;          // subtle floor tint
static const float kCircleBand = 0.05;          // filled band
static const float kCircleEdgeGlow = 0.25;      // edge glow
static const float kCircleGlowSharp = 20.0;     // tightness of edge glow
static const float kCircleBrighten = 0.2;       // lighten factor
static const float kCircleDot = 0.025;          // center dot
static const float kCircleDotStrength = 0.5;    // center dot strength
static const float kCircleInnerSize = 0.02;     // inner glow size
static const float kCircleInnerOpacity = 0.12;  // inner glow opacity
static const float kCircleOuterSize = 0.02;     // outer glow size
static const float kCircleOuterOpacity = 0.20;  // outer glow opacity
// One circle over color c, for the ground point p (relative to the hero). A
// strip measures from its center line instead of the center point, so the same
// rim and fill draw a rounded band. The fill is a disc growing from the center
// to the rim over the cast's time (a Guild Wars 2 style tell). up (FacingUp)
// keeps the paint on the floor: a monster's legs, a wall or a rock side inside
// the circle stay unpainted. The color is added as light.
// Teleport's landing (dir.w 1; owner, 2026-11-05: "make the teleportation circle indicator, the target ring
// more awesome looking and more noticeable"): an arcane sigil rather than a spell's area - a white-hot outer
// rim, an inner rim, twelve dashes turning between them, a ring of light rising out from the centre again and
// again, a pulsing core and light spilling past the rim. TeleLight raises a pillar over it.
float3 TeleportSigil(float3 c, float2 q, float r, float4 ring, float4 col, float up)
{
    if (r > 1.45) return c;
    const float t = Timer * 0.001;
    const float ang = atan2(q.y, q.x) / 6.2831853;
    const float outer = 1.0 - smoothstep(0.035, 0.065, abs(r - 1.0));
    const float inner = 1.0 - smoothstep(0.02, 0.04, abs(r - 0.70));
    const float band = 1.0 - smoothstep(0.03, 0.05, abs(r - 0.85));
    const float dashes = band * smoothstep(0.35, 0.45, frac(ang * 12.0 + t * 0.30)) * (1.0 - smoothstep(0.75, 0.85, frac(ang * 12.0 + t * 0.30)));
    const float runes = (1.0 - smoothstep(0.015, 0.03, abs(r - 0.42))) * step(0.5, frac(ang * 6.0 - t * 0.45));
    const float wave = frac(t * 1.1);
    const float ripple = step(r, 1.0) * exp(-abs(r - wave) * 22.0) * (1.0 - wave);
    const float core = exp(-r * r * 16.0) * (0.75 + 0.25 * sin(t * 7.0));
    const float halo = step(1.0, r) * exp(-(r - 1.0) * 7.0);
    const float disc = step(r, 1.0) * 0.14;
    const float pulse = 0.85 + 0.15 * sin(t * 4.0);
    const float a = saturate(1.2 * outer + 0.8 * inner + 0.9 * dashes + 0.7 * runes + 0.7 * ripple + 0.9 * core + 0.55 * halo + disc) *
                    pulse * ring.w * smoothstep(0.55, 0.8, up);
    const float3 tint = lerp(col.rgb, float3(1.0, 1.0, 1.0), saturate(0.55 * outer + 0.7 * core + 0.4 * ripple));   // white-hot rims and heart
    return saturate(c + tint * a * 1.35);
}
float3 RingTint(float3 c, float3 p, float4 ring, float fill, float4 col, float4 dir, float up)
{
    if (ring.w <= 0.0 || abs(p.y - ring.y) > RingFloorTol) return c;
    float2 q = p.xz - ring.xz;
    if (dir.w > 0.5) return TeleportSigil(c, q, length(q) / max(col.w, 1e-3), ring, col, up);
    if (dir.z > 0.0) q -= dir.xy * clamp(dot(q, dir.xy), -dir.z, dir.z);
    const float r = length(q) / max(col.w, 1e-3);
    if (r > 1.1 + kCircleOuterSize) return c;
    const float aa = 0.02;
    const float d = 1.0 - r;                                                    // in from the rim
    const float innerGlow = step(0.0, d) * (1.0 - smoothstep(0.0, kCircleInnerSize + 1e-3, d));
    const float outerGlow = step(0.0, -d) * (1.0 - smoothstep(0.0, kCircleOuterSize, -d));
    const float rim = 1.0 - smoothstep(kCircleRim, kCircleRim + aa, abs(r - 1.0));   // the outline
    const float inner = 1.0 - smoothstep(1.0 - aa, 1.0, r);                     // inside the circle
    const float front = fill;                                                   // fill edge, moving outward
    const float filled = inner * (1.0 - smoothstep(front, front + aa, r));      // the disc from the center to it
    const float glow = inner * exp(-abs(r - front) * kCircleGlowSharp) * step(0.001, fill);
    const float aim = 1.0 - smoothstep(kCircleDot, kCircleDot + aa, r);         // the exact aim point
    const float a = saturate(kCircleBase * inner + kCircleBand * filled + kCircleEdgeGlow * glow + kCircleRimStrength * rim +
                             kCircleDotStrength * aim + kCircleInnerOpacity * innerGlow + kCircleOuterOpacity * outerGlow) *
                    ring.w * kCircleOpacity * smoothstep(0.55, 0.8, up);
    return saturate(c + saturate(col.rgb + kCircleBrighten * glow) * a);
}
// Loot beams (vr/loot_beam_scan.cpp, steamvr/loot_beams.h): a pillar of light
// over each Rare, Set or Unique item and each high rune lying near the hero, in
// the colour the game writes its name in, and a soft glow on the floor round it.
// Each pixel's ray from the eye is tested against each beam's vertical axis; the
// beam counts only where it passes in front of what the eye sees there, so walls,
// monsters and hills in front hide it and the sky behind shows it. Drawn after
// the fog, so a beam stays visible in it; never over the game's interface left in
// the picture (UiMaskOn). vrcam: BeamPos xyz the item's spot on
// the hero's floor, relative to the hero, w its strength (0 = none); BeamCol rgb
// its colour, w a phase for its shimmer; BeamShape core radius, height and floor
// glow radius (world units), overall strength.
uniform bool BeamOn < hidden = true; > = false;
uniform float4 BeamShape < hidden = true; > = float4(0.7, 56.0, 2.8, 1.0);
uniform float BeamFloorTol < hidden = true; > = 0.6;
uniform float4 BeamPos0 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos1 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos2 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos3 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos4 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos5 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos6 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamPos7 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 BeamCol0 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol1 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol2 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol3 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol4 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol5 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol6 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 BeamCol7 < hidden = true; > = float4(1.0, 1.0, 1.0, 0.0);

// The eye (relative to the hero) and the direction its pixel at uv looks along,
// scaled so that t = 1 is one world unit of view depth (as OutlineWorld).
void BeamRay(float2 uv, int eye, out float3 e, out float3 dir)
{
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
    dir = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                   : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    e = eye == 0 ? OutlineEye0 : OutlineEye1;
}
// One beam's light at this pixel. tSurf: how far (view depth) the eye sees here,
// huge over the sky. The core narrows towards the top, where it fades out; a
// slow shimmer climbs it. The floor point p (ok = 1) round the foot is lit too.
float3 BeamLight(float3 e, float3 dir, float tSurf, float3 p, float ok, float4 b, float4 col)
{
    if (b.w <= 0.0) return 0.0;
    float3 light = 0.0;
    const float2 dxz = dir.xz;
    const float t = dot(b.xz - e.xz, dxz) / max(dot(dxz, dxz), 1e-6);   // nearest the axis, across the ground
    if (t > 0.0) {
        const float h = (e.y + t * dir.y - b.y) / max(BeamShape.y, 1e-3);   // 0 the floor, 1 the top
        const float r = BeamShape.x * lerp(1.0, 0.3, saturate(h));
        const float d = length(e.xz + t * dxz - b.xz) / max(r, 1e-3);
        const float front = smoothstep(-1.0, 0.5, tSurf - t);             // in front of the scene there
        const float along = smoothstep(0.0, 0.015, h) * (1.0 - smoothstep(0.2, 1.0, h));
        const float shimmer = 0.82 + 0.18 * sin(h * 55.0 - Timer * 0.0025 + col.w);
        const float core = exp(-d * d), halo = exp(-d * d / 9.0);
        const float a = along * front * shimmer;
        light += col.rgb * (0.75 * core + 0.25 * halo) * a + core * core * core * a * 0.08;   // a little white at the very heart
    }
    if (ok > 0.0 && abs(p.y - b.y) < BeamFloorTol) {   // the glow on the floor round its foot
        const float g = length(p.xz - b.xz) / max(BeamShape.z, 1e-3);
        light += col.rgb * exp(-g * g * 2.5) * (0.5 + 0.1 * sin(Timer * 0.002 + col.w));
    }
    return light * b.w * BeamShape.w;
}
// Teleport's pillar over its sigil (vrcam: TelePos xyz the landing on the hero's floor relative to the hero, w its
// strength; TeleCol rgb its colour; TeleShape core radius and height in world units): narrower and shorter than a
// loot beam, brightest at its foot, light spiralling up it. Like the beams, only in front of what the eye sees.
uniform bool TeleOn < hidden = true; > = false;
uniform float4 TelePos < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 TeleCol < hidden = true; > = float4(0.62, 0.55, 1.0, 0.0);
uniform float2 TeleShape < hidden = true; > = float2(0.6, 14.0);
float3 TeleLight(float3 e, float3 dir, float tSurf)
{
    if (TelePos.w <= 0.0) return 0.0;
    const float2 dxz = dir.xz;
    const float t = dot(TelePos.xz - e.xz, dxz) / max(dot(dxz, dxz), 1e-6);
    if (t <= 0.0) return 0.0;
    const float h = (e.y + t * dir.y - TelePos.y) / max(TeleShape.y, 1e-3);   // 0 the floor, 1 the top
    const float3 at = e + t * dir;
    const float2 off = at.xz - TelePos.xz;
    const float d = length(off) / max(TeleShape.x * lerp(1.0, 0.45, saturate(h)), 1e-3);
    const float front = smoothstep(-1.0, 0.5, tSurf - t);
    const float along = smoothstep(0.0, 0.02, h) * (1.0 - smoothstep(0.15, 1.0, h));
    const float spiral = 0.7 + 0.3 * sin(h * 40.0 + atan2(off.y, off.x) * 2.0 - Timer * 0.009);
    const float core = exp(-d * d), halo = exp(-d * d / 6.0);
    const float a = along * front * spiral * (1.0 - 0.5 * saturate(h));
    return (TeleCol.rgb * (0.7 * core + 0.3 * halo) + core * core * 0.25) * a * TelePos.w;
}
float3 Beams(float3 c, float2 uv, int eye, float3 p, float ok)
{
    float3 e, dir;
    BeamRay(uv, eye, e, dir);
    const float tSurf = ok > 0.0 ? dot(p - e, dir) / max(dot(dir, dir), 1e-6) : 1e7;
    float3 light = BeamLight(e, dir, tSurf, p, ok, BeamPos0, BeamCol0);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos1, BeamCol1);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos2, BeamCol2);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos3, BeamCol3);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos4, BeamCol4);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos5, BeamCol5);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos6, BeamCol6);
    light += BeamLight(e, dir, tSurf, p, ok, BeamPos7, BeamCol7);
    // Not over the game's interface left in the picture (names, labels, chat), as the fog.
    if (UiMaskOn && !UiMaskOff) light *= saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    return saturate(c + light);
}
// Teleport's pillar, its own pass beside the beams (loot-beams-shader-test compiles Beams alone).
float3 TelePillar(float3 c, float2 uv, int eye, float3 p, float ok)
{
    float3 e, dir;
    BeamRay(uv, eye, e, dir);
    const float tSurf = ok > 0.0 ? dot(p - e, dir) / max(dot(dir, dir), 1e-6) : 1e7;
    float3 light = TeleLight(e, dir, tSurf);
    if (UiMaskOn && !UiMaskOff) light *= saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    return saturate(c + light);
}
// The campfire's sparks (vrcam: a click on the fire, [campfire] sparks): a burst of embers out of the
// fire, rising, drifting and dying out over two seconds, each a small glowing streak along its flight.
// Drawn into the picture as light (added), hidden by whatever stands nearer than the spark at that
// pixel (the depth), and over the sky as well. SparkPos: the fire relative to the hero (world units),
// SparkTime: seconds since the click, SparkSeed: a different burst each click.
uniform bool SparkOn < hidden = true; > = false;
uniform float3 SparkPos < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float SparkTime < hidden = true; > = 0.0;
uniform float SparkSeed < hidden = true; > = 0.0;
#define SPARK_COUNT 44
float SparkHash(float n) { return frac(sin(n * 12.9898 + SparkSeed * 78.233) * 43758.5453); }
// Where spark i is t seconds after the click (relative to the hero), and how bright (0 once dead).
float3 SparkAt(int i, float t, out float life)
{
    const float fi = float(i);
    const float a = SparkHash(fi) * 6.2831853, r = SparkHash(fi + 11.0);
    const float delay = SparkHash(fi + 23.0) * 0.35;           // not all at once: a flurry
    const float span = 0.7 + SparkHash(fi + 37.0) * 1.3;       // how long it glows
    const float tt = max(t - delay, 0.0);
    life = (t > delay) ? saturate(1.0 - tt / span) : 0.0;
    const float3 v = float3(cos(a) * (1.5 + 4.0 * r), 9.0 + 13.0 * SparkHash(fi + 51.0), sin(a) * (1.5 + 4.0 * r));
    const float3 start = SparkPos + float3(cos(a) * 1.5 * r, 1.0 + SparkHash(fi + 63.0) * 2.0, sin(a) * 1.5 * r);
    // rising fast, slowed by the air, a wander from side to side, at the end sinking a little
    const float drag = (1.0 - exp(-tt * 1.6)) / 1.6;
    const float3 wander = float3(sin(tt * 5.0 + fi) , 0.0, cos(tt * 4.0 + fi * 1.7)) * 0.8 * tt;
    return start + v * drag + wander + float3(0.0, -2.5 * tt * tt, 0.0);
}
float3 Sparks(float3 c, float2 uv, int eye, float ok, float3 p)
{
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    const float3 e = eye == 0 ? OutlineEye0 : OutlineEye1;
    const float3 dir = normalize(off);
    const float scene = ok > 0.0 ? length(p - e) : 1e9;
    // the whole burst inside a ball over the fire: rays missing it are left alone
    const float3 ball = SparkPos + float3(0.0, 14.0, 0.0) - e;
    const float along = dot(ball, dir);
    if (length(ball - dir * along) > 22.0) return c;
    float3 glow = 0.0;
    const float t = SparkTime;
    [loop] for (int i = 0; i < SPARK_COUNT; ++i) {
        float life;
        const float3 q0 = SparkAt(i, t, life);
        if (life <= 0.0) continue;
        float lifeB;
        const float3 q1 = SparkAt(i, max(t - 0.045, 0.0), lifeB);   // its streak: where it was a moment ago
        // nearest approach of the pixel's ray to the streak, sampled along it
        float best = 1e9, depth = 0.0;
        [unroll] for (int k = 0; k < 3; ++k) {
            const float3 q = lerp(q0, q1, k * 0.5) - e;
            const float s = dot(q, dir);
            const float d = length(q - dir * s) / max(s, 1.0);   // as an angle: the same size near and far
            if (s > 0.0 && d < best) { best = d; depth = s; }
        }
        if (depth <= 0.0 || depth > scene + 0.5) continue;   // behind something
        const float size = 0.0012 * (0.6 + SparkHash(float(i) + 71.0));
        const float flick = 0.75 + 0.25 * sin(t * 40.0 + float(i) * 3.1);
        const float g = exp(-(best * best) / (size * size)) * life * flick;
        // white-hot when young, orange, then a deep red ember
        const float3 hot = lerp(float3(1.0, 0.25, 0.05), lerp(float3(1.0, 0.6, 0.15), float3(1.0, 0.95, 0.75), saturate(life * 2.0 - 1.0)), saturate(life * 1.6));
        glow += hot * g * 3.0;
    }
    return c + glow;
}
float3 Outline(float2 uv, int eye)
{
    float3 c = tex2D(FogColorSmp, uv).rgb;
    if (!OutlineOn && !RingOn && !BeamOn && !TeleOn && !SparkOn) return c;
    float ok;
    const float3 p = OutlineWorld(uv, eye, ok);
    if (BeamOn) c = Beams(c, uv, eye, p, ok);   // over the sky as well
    if (SparkOn) c = Sparks(c, uv, eye, ok, p);   // over the sky as well
    if (TeleOn) c = TelePillar(c, uv, eye, p, ok);
    if (ok <= 0.0) return c;
    if (RingOn) {
        const float up = FacingUp(uv, eye, p, 3.0);   // two more depth taps, only while a circle shows
        c = RingTint(c, p, RingPos0, RingFill0, RingCol0, RingDir0, up);
        c = RingTint(c, p, RingPos1, RingFill1, RingCol1, RingDir1, up);
        c = RingTint(c, p, RingPos2, RingFill2, RingCol2, RingDir2, up);
        c = RingTint(c, p, RingPos3, RingFill3, RingCol3, RingDir3, up);
        c = RingTint(c, p, RingPos4, RingFill4, RingCol4, RingDir4, up);
        c = RingTint(c, p, RingPos5, RingFill5, RingCol5, RingDir5, up);
    }
    if (OutlineOn) c = OutlineTint(OutlineTint(c, uv, eye, p, OutlinePos, OutlineShape, OutlineColor), uv, eye, p, Outline2Pos, Outline2Shape, Outline2Color);
    return c;
}
float3 PS_Outline(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Outline(uv, 0); }
float3 PS_OutlineR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Outline(uv, 1); }

