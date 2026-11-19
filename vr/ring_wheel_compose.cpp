// Lab Ring wheel (T025, build steps R4-R5): the compose pass (vr/ring_wheel_compose.h). One pipeline, one draw: every
// instance of steamvr/ring_wheel_draw.h is a quad (a 4-vertex strip, instanced), its recipe drawn in the pixel shader
// inside that quad only. The shaders compile in Setup with the system's d3dcompiler_47 (as ui_blit.cpp and
// elemental_hands_renderer.cpp do), and Setup runs on the Lab's worker, so the frame thread never waits for it (perf R4
// item 1: about 20 ms measured).
#include "ring_wheel_compose.h"
#include "../steamvr/ring_wheel_draw.h"
#include "../steamvr/ring_wheel_shot.h"
#include "ring_wheel_shader_blob.h"   // generated at build time (steamvr/lab.cmake)
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <chrono>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
namespace framevr::ringwheel {
namespace {
const char Source[] = R"(
struct Inst { float cx, cy, w, h, rot, u0, v0, u1, v1; float4 tint; uint recipe; float param; };
StructuredBuffer<Inst> gInst : register(t0);
Texture2D gIcons : register(t1);
Texture2D gGlyphs : register(t2);
SamplerState gLinear : register(s0);
cbuffer K : register(b0) { float2 origin; float scale; float linearOut; float2 size; uint base; };   // base: the run's first instance
struct V {
    float4 pos : SV_Position; float2 local : LOCAL; float2 uv : UV;
    nointerpolation float4 tint : TINT;     // linear when the target is sRGB
    nointerpolation float4 shape : SHAPE;   // radius, param, recipe
};
float3 ToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
V VS(uint v : SV_VertexID, uint id : SV_InstanceID) {
    Inst s = gInst[id + base];
    float2 corner = float2(v & 1, v >> 1);
    float2 local = (corner * 2 - 1) * float2(s.w, s.h) * 0.5;
    float c = cos(s.rot), n = sin(s.rot);
    float2 p = float2(s.cx, s.cy) + float2(local.x * c - local.y * n, local.x * n + local.y * c);
    float2 t = (p - origin) * scale;
    V o;
    o.pos = float4(t.x / size.x * 2 - 1, 1 - t.y / size.y * 2, 0, 1);
    o.local = local;
    o.uv = s.recipe == 6 ? float2(s.u0, s.v0) : lerp(float2(s.u0, s.v0), float2(s.u1, s.v1), corner);   // recipe 6: (time, seed) per quad
    o.tint = float4(linearOut > 0.5 && s.recipe != 8 && s.recipe != 6 ? ToLinear(s.tint.rgb) : s.tint.rgb, s.tint.a);   // 8: rgb is an offset; 6: decoded after (PS)
    o.shape = float4(min(s.w, s.h) * 0.5, s.param, s.recipe, s.recipe == 6 ? s.u1 : s.u0);   // w: the arc's fill (recipe 5); the rest event's age (6)
    return o;
}
// R8 effects (L2 "Animation layers", kit wheel.js FX and hover-defaults.js): pure functions of the time t (s) and a seed,
// inside their own quad only; every loop 8 or fewer; nothing faster than 1 Hz pulses (steady brightness).
float H(float n) { return frac(sin(n * 12.9898) * 43758.5453); }
float Soft(float2 p, float2 c, float s) { return saturate(1 - length(p - c) / max(s, 0.5)); }
float Seg(float2 p, float2 a, float2 b) { float2 pa = p - a, ba = b - a; float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-4)); return length(pa - ba * h); }
float Bolt(float2 p, float2 a, float2 b, float seed, float rough) {
    float2 dir = b - a, nrm = normalize(float2(-dir.y, dir.x) + 1e-5), prev = a; float m = 1e9;
    [unroll] for (int i = 1; i <= 7; ++i) { float f = i / 7.0; float2 q = i == 7 ? b : a + dir * f + nrm * (H(seed + i * 31) - 0.5) * rough; m = min(m, Seg(p, prev, q)); prev = q; }
    return m;
}
// R8 match stage 1 (r8-match-spec sections 3 and 6.1): the kit's rest FX (wheel.js FX, called from drawIcon) per kind, scaled
// into one cap: the quad's radius R = 1.3 r, and every layer fades to 0 by R (RestCap). The kit's 'lighter' layers add up;
// the result stays premultiplied (rgb <= alpha). Orbits lap in 4 s or more, tongues are the kit's x 0.3, nothing re-shapes
// faster than 3 a second, phases seeded per icon; glints and charge orbs only in their rest event (ev: its age in ms, -1 none).
// Under the face: halos, tongues, motes, blobs, rings; over it (kit: after the rim): the lightning arcs and the steel glint.
// Per kind (RestKind order), tuned so each kind's band light stays at or under the reference's (Design/ring-wheel/r8-match/
// the design notes; the preview prints the band luma per kind and checks it per class and tree on real icons).
static const float RestGain[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
// The kit's 'lighter' (additive) layers: light added, no alpha (the ring's layer is premultiplied, so the runtime adds it to
// the world as the kit's canvas adds it to the scene); normal-blend layers (AddN) cover.
void Add(inout float4 acc, float3 col, float a) { a = saturate(a); acc.rgb += col * a; }
void AddN(inout float4 acc, float3 col, float a) { a = saturate(a); acc = float4(col * a, a) + acc * (1 - a); }   // source-over
// The kit's charge loop (FX.charge, assassin charge): bead i fades in over 300 ms, 0.9 s after the last, holds, all fade out
// over 400 ms from 4.4 s; a 5 s loop, its phase seeded per icon. Steady rest glow, outside the event gate (designer, 2026-11-07).
float BeadOn(float t, float seed, int i) {
    float cyc = fmod(t * 1000 + H(seed + 0.71) * 5000, 5000);
    return smoothstep(0, 1, saturate((cyc - i * 900) / 300)) * (1 - smoothstep(0, 1, saturate((cyc - 4400) / 400)));
}
// The kit's radial halo: a at ri, 0 at ro r (its createRadialGradient), the rest cap (1.5 r) clipping what lies past it.
// HaloK: a kit kind's S1 gain (Rest sets it), on its halos only: the figures keep the kit's alpha (designer, F1 recheck).
static float HaloK = 1;
float Halo(float d, float r, float ri, float a, float ro) { return HaloK * a * saturate((ro * r - d) / max(ro * r - ri, 1)); }
float EllD(float2 q, float2 ab) { float k0 = length(q / ab), k1 = length(q / (ab * ab)); return abs(k0 * (k0 - 1) / max(k1, 1e-4)); }   // ~px to an ellipse
float VN(float x, float s) { float i = floor(x), f = frac(x); f = f * f * (3 - 2 * f); return lerp(H(i * 7.13 + s), H((i + 1) * 7.13 + s), f); }
float Stroke(float d, float c, float w) { return saturate(w * 0.5 + 0.5 - abs(d - c)); }   // a soft line w px wide centered on c
float3 Ramp3(float u, float3 a, float3 b, float3 c, float3 e, float4 at) {   // the kit's 4-stop gradients: colors at 0, at.y, at.z, 1
    return u < at.y ? lerp(a, b, u / at.y) : u < at.z ? lerp(b, c, (u - at.y) / (at.z - at.y)) : lerp(c, e, (u - at.z) / (1 - at.z));
}
float Star(float2 q, float s) { q = abs(q); return s <= 0.05 ? 0 : saturate((sqrt(s) - sqrt(q.x) - sqrt(q.y)) * 2.5); }   // the kit's 4-point sparkle
void Sparkles(inout float4 acc, float2 p, float r, float t, float seed, float3 col, int n) {   // kit sparkle(): 1.4 s lives, a new place each
    [loop] for (int i = 0; i < 4; ++i) {
        if (i >= n) break;
        float u = t + H(seed + i) * 4.2, life = fmod(u, 2.1) / 1.4, slot = floor(u / 2.1);
        if (life > 1) continue;
        float a = H(seed + i * 7 + slot * 13) * 6.2831853, rr = r * (0.75 + 0.5 * H(seed + slot * 3 + i)), s = 5 * sin(life * 3.14159);   // the kit's
        Add(acc, col, 0.9 * sin(life * 3.14159) * Star(p - float2(cos(a), sin(a)) * rr, s));
    }
}
void RestFire(inout float4 acc, float2 p, float r, float t, float seed, float k) {
    float d = length(p);
    Add(acc, float3(0.91, 0.35, 0.10), k * Halo(d, r, 0.8 * r, 0.62, 1.5));
    [loop] for (int i = 0; i < 14; ++i) {   // tongues from the rim, leaning up, the kit's lengths (the cap clips), 620 ms lives
        float a = i / 14.0 * 6.2831853 + 0.2 * sin(t / 0.9 + i), life = frac(t / 0.62 + H(seed + i));
        float h = r * (0.45 + 0.5 * VN(t / 0.16 + i * 3.1, seed + i)) * (1 - life * 0.5), ang = a + (-1.5707963 - a) * 0.25;   // F1 recheck: the kit's length
        float2 b = float2(cos(a), sin(a)) * r * 0.96, e = b + float2(cos(ang), sin(ang)) * h * (1 + life) - float2(0, life * 4), ba = e - b, pa = p - b;
        float u = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-4)), w = r * 0.26 * (1 - life * 0.6) * (1 - u) * (1 - u) + 0.5;   // the kit's quadratic edge
        float cov = saturate((w - length(pa - ba * u)) / 2 + 0.5);   // a 2 px soft edge
        float3 c = Ramp3(u, float3(1, 0.95, 0.69), float3(1, 0.69, 0.13), float3(0.91, 0.35, 0.10), float3(0.48, 0.10, 0.04), float4(0, 0.35, 0.75, 1));
        float al = u < 0.35 ? lerp(0.85, 0.7, u / 0.35) : u < 0.75 ? lerp(0.7, 0.4, (u - 0.35) / 0.4) : lerp(0.4, 0, (u - 0.75) / 0.25);
        Add(acc, c, k * cov * al);
    }
}
void RestCold(inout float4 acc, float2 p, float r, float t, float seed, float k) {
    float d = length(p);
    Add(acc, float3(0.50, 0.78, 0.95), k * Halo(d, r, 0.85 * r, 0.62, 1.45));
    [loop] for (int i = 0; i < 7; ++i) {   // motes at 1.05-1.15 r, one turn in 4 s (vr-ux), 3 px across or more
        float a = t / 4 * 6.2831853 * (i % 2 ? 1 : -0.7) + i * 6.2831853 / 7 + H(seed + i) * 2, rr = r * (1.12 + 0.1 * sin(t / 0.7 + i));
        float s = (2.2 + 2 * H(seed + i * 5)) * 2.6, u = saturate(length(p - float2(cos(a), sin(a)) * rr) / s);   // the kit's mote gradient (s x 2.6)
        Add(acc, u < 0.4 ? lerp(float3(0.91, 0.96, 1), float3(0.50, 0.78, 0.95), u / 0.4) : float3(0.50, 0.78, 0.95), k * (u < 0.4 ? lerp(0.95, 0.6, u / 0.4) : lerp(0.6, 0, (u - 0.4) / 0.6)));
    }
}
void RestLight(inout float4 acc, float2 p, float r, float t, float seed, float k, bool over) {
    float d = length(p);
    if (!over) { Add(acc, float3(1, 0.85, 0.29), k * Halo(d, r, 0.85 * r, 0.45, 1.4)); return; }
    // the rim arcs, soft and smooth at rest (R2: no dashes, no re-shape), a lap in 4 s, 70%; the short far-side arc the other way
    float a = atan2(p.y, p.x), head = t / 4 * 6.2831853 + H(seed) * 6.2831853;
    [loop] for (int j = 0; j < 2; ++j) {
        float hd = j == 0 ? head : -head + 3.14159, span = j == 0 ? 1.9 : 0.9, rc = r + (j == 0 ? 4 : 3.5);   // the kit's r + 3-4, past our rim
        float behind = frac((hd - a) / 6.2831853) * 6.2831853, on = behind < span ? saturate(behind / 0.25) * saturate((span - behind) / 0.35) : 0;
        Add(acc, float3(1, 0.85, 0.29), k * 0.7 * on * 0.5 * saturate(1 - abs(d - rc) / 4));   // glow, a 4 px feather
        Add(acc, float3(1, 0.98, 0.88), k * 0.7 * on * Stroke(d, rc, j == 0 ? 2 : 2));       // core, 2 px
    }
}
void RestGlint(inout float4 acc, float2 p, float r, float ev) {   // kit FX.phys: a band sweeps the face once (720 ms), clipped to it
    if (ev < 0 || ev >= 720) return;
    float x = -r + ev / 720 * 2 * r; float2 a0 = float2(x - 10, -r), dv = float2(20, 2 * r);
    float u = dot(p - a0, dv) / dot(dv, dv);
    Add(acc, float3(1, 0.98, 0.92), 0.35 * saturate(1 - abs(u - 0.5) * 2) * saturate(r - length(p) + 0.5));
}
// R8 match stage 1b: a class page's own rest recipe (kit class.js WHEEL_FX_EXTRA) as its layers (RestL, generated from
// tools/ring_wheel_rest_recipes.py: A = type, flags, count, period; B = color, alpha; C = the type's numbers; D = core color
// or (wobble, its period, -), extra). Lengths are px at r 22, scaled with the icon. Flags: 1 normal blend (else the kit's
// lighter), 2 a dark under-stroke, 4 only in the icon's rest event, 8 over the face (mode 2), 16 the skill's color (tint).
// The curse marks (kit necromancer MARKS, px at size 1, canvas y down): segments (a zero-length one: a dot), per shape its
// first segment and count, and an ellipse (rx 0: none). Order: tri, star, eye, chevron, spike, mouth, hook, plus, ring, glass, arrow.
static const float4 MarkSeg[30] = {
    float4(-3.6, -2.6, 3.6, -2.6), float4(3.6, -2.6, 0, 3.6), float4(0, 3.6, -3.6, -2.6),
    float4(-5, 0, 5, 0), float4(0, -5, 0, 5),
    float4(0, 0, 0, 0),
    float4(-4.5, -3.5, 0, 0), float4(0, 0, 4.5, -3.5), float4(-4.5, 1, 0, 4.5), float4(0, 4.5, 4.5, 1),
    float4(0, -5, 0, 5), float4(-3.5, -1.5, 3.5, -1.5),
    float4(-3, -1.8, -2.121, -3.921), float4(-2.121, -3.921, 0, -4.8), float4(0, -4.8, 2.121, -3.921), float4(2.121, -3.921, 3, -1.8),
    float4(3, -1.8, 2.427, -0.037), float4(2.427, -0.037, 0.927, 1.053), float4(0.927, 1.053, 0, 2.2), float4(0, 4.6, 0, 4.6),
    float4(0, -4.5, 0, 4.5), float4(-4.5, 0, 4.5, 0),
    float4(0, 0, 0, 0),
    float4(-4, -4.5, 4, -4.5), float4(4, -4.5, -4, 4.5), float4(-4, 4.5, 4, 4.5), float4(4, 4.5, -4, -4.5),
    float4(0, -4.5, 0, 4), float4(-3.5, 0.8, 0, 4.3), float4(0, 4.3, 3.5, 0.8) };
static const int2 MarkK[11] = { int2(0, 3), int2(3, 2), int2(5, 1), int2(6, 4), int2(10, 2), int2(12, 0), int2(12, 8), int2(20, 2), int2(22, 1), int2(23, 4), int2(27, 3) };
static const float4 MarkE[11] = { float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 5, 2.8), float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0.5, 3, 4.2),
    float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 4, 4), float4(0, 0, 0, 0), float4(0, 0, 0, 0) };
void AddK(inout float4 acc, float3 col, float a, bool normal) { if (normal) AddN(acc, col, a); else Add(acc, col, a); }
float DotA(float u, float a) { return u < 0.35 ? a : u < 1 ? lerp(0.85 * a, 0, (u - 0.35) / 0.65) : 0; }   // the kit's dot(): core, 0.35 color, edge
void RestLayers(inout float4 acc, uint kind, bool over, float2 p, float r, float t, float seed, float ev, float3 tint) {
    const float TAU = 6.2831853, UP = -1.5707963;
    int2 rg = RestK[kind - RestFirstExtra];
    float s = r / 22, d = length(p), ang = atan2(p.y, p.x), ph = H(seed + 0.37) * TAU;
    [loop] for (int li = 0; li < 8; ++li) {
        if (li >= rg.y) break;
        int b = (rg.x + li) * 4;
        float4 A = RestL[b], B = RestL[b + 1], C = RestL[b + 2], D = RestL[b + 3];
        uint type = (uint)A.x, fl = (uint)A.y; int n = (int)A.z; float per = A.w;
        if (((fl & 8) != 0) != over) continue;
        float env = 1;
        if ((fl & 4) != 0) { if (ev < 0 || ev >= 1000) continue; env = sin(ev / 1000 * 3.14159); }
        float3 col = (fl & 16) != 0 ? tint : B.rgb, core = D.rgb;
        float a = B.a * env; bool nb = (fl & 1) != 0, dark = (fl & 2) != 0;
        if (type == 1) {   // a halo; C.y > 0: the kit's ground glow, an ellipse C.x r below the center, squashed to C.y, C.z r wide
            if (C.y > 0) AddK(acc, col, a * saturate(1 - length(float2(p.x, (p.y - C.x * r) / C.y)) / (C.z * r)), nb);
            else AddK(acc, col, Halo(d, r, C.z * r, a, C.w), nb);
        }
        else if (type == 2) {   // a ring on r + off, w wide; drifting off center (decoy)
            float2 c = per > 0 ? float2(sin(t / per * TAU + seed), -0.375 * cos(t / per * TAU + seed)) * C.z * s : 0;
            float dd = length(p - c), rr = r + C.x * s, w = max(2, C.y * s);
            if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.5 * a * Stroke(dd, rr, w * 2.2));
            AddK(acc, col, a * Stroke(dd, rr, w), nb);
        } else if (type == 3) {   // n arcs span long on r + off, heads lapping (or still at ang0, swaying by wobble)
            float rr = r + C.x * s, w = max(2, C.y * s), span = C.z;
            float base = (per != 0 ? ph + t / per * TAU : 0) + D.w + (D.y > 0 ? D.x * sin(t / D.y * TAU) : 0);
            [loop] for (int i = 0; i < 8; ++i) {
                if (i >= n) break;
                float head = base + i * TAU / n, behind = frac((head - ang) / TAU) * TAU;
                if (behind >= span) continue;
                float k = saturate(behind / 0.08) * saturate((span - behind) / 0.08) * (C.w > 0 ? 1 - behind / span : 1);
                if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.5 * a * k * Stroke(d, rr, w * 2.2));
                AddK(acc, col, a * k * Stroke(d, rr, w), nb);
            }
        } else if (type == 4) {   // n dots on r + off, size px, lapping (or still), evenly spread or `spacing` rad apart
            float rr = r + C.x * s, sz = max(1.5, C.y * s), base = (per != 0 ? ph + t / per * TAU : 0) + C.z;
            [loop] for (int i = 0; i < 8; ++i) {
                if (i >= n) break;
                float aa = base + (C.w > 0 ? i * C.w : i * TAU / n), u = length(p - float2(cos(aa), sin(aa)) * rr) / sz;
                if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.6 * a * saturate((sz + 1 - u * sz) + 0.5));
                AddK(acc, u < 0.35 ? lerp(core, col, u / 0.35) : col, DotA(u, a), nb);
            }
        } else if (type == 5) {   // n ripples from r + from to r + to in per s (a 150 ms fade-in), on the top `span` only when set
            [loop] for (int i = 0; i < 4; ++i) {
                if (i >= n) break;
                float q = frac(t / per + i / (float)n + H(seed)), on = (1 - q) * saturate(q / 0.05);
                float top = C.w > 0 ? (abs(frac((ang - UP) / TAU + 0.5) - 0.5) * TAU < C.w ? 1 : 0) : 1;
                Add(acc, col, a * on * top * Stroke(d, r + lerp(C.x, C.y, q) * s, C.z * s));
            }
        } else if (type == 6) {   // n motes rising off the rim (life per s), within `spread` rad of the top
            [loop] for (int i = 0; i < 6; ++i) {
                if (i >= n) break;
                float q = frac(t / per + H(seed + i * 3.1)), aa = UP + (H(seed + i * 7.3) - 0.5) * C.w;
                float2 c = float2(cos(aa), sin(aa)) * (r + C.x * s) + float2(sin(t / 0.7 + i) * 1.5 * s, -q * C.y * s);
                float sz = max(1.5, C.z * s), u = length(p - c) / sz, al = DotA(u, a * sin(q * 3.14159));
                float dr = D.w > 0 ? D.w * s : sz + 1;   // the dark under-dot's radius (kit ember: s + 2.2, smaller than the glow)
                if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.6 * a * sin(q * 3.14159) * saturate(dr + 0.5 - u * sz));
                AddK(acc, u < 0.35 ? lerp(core, col, u / 0.35) : col, al, nb);
            }
        } else if (type == 7) {   // a regular n-gon on r + off, turning one lap in per s
            float rot = ph + (per != 0 ? t / per * TAU : 0), sec = TAU / n, k = floor(frac((ang - rot) / TAU) * n);
            float na = rot + (k + 0.5) * sec, R2 = (r + C.x * s) * cos(3.14159 / n), dd = abs(dot(p, float2(cos(na), sin(na))) - R2);
            float w = max(2, C.y * s);
            if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.5 * a * saturate(w * 1.1 + 0.5 - dd));
            AddK(acc, col, a * saturate(w * 0.5 + 0.5 - dd), nb);
        } else if (type == 8) {   // n tongues on the rim (life per s), the kit's length x lenk, within `span` of `center`
            [loop] for (int i = 0; i < 12; ++i) {
                if (i >= n) break;
                float aa = C.z - C.w / 2 + (i + 0.5) / n * C.w + 0.2 * sin(t / 0.9 + i), life = frac(t / per + H(seed + i));
                float h = r * (0.45 + 0.5 * VN(t / 0.16 + i * 3.1, seed + i)) * (1 - life * 0.5) * C.x, an = aa + (UP - aa) * 0.25;
                float2 bb = float2(cos(aa), sin(aa)) * r * 0.96, e = bb + float2(cos(an), sin(an)) * h * (1 + life), ba = e - bb, pa = p - bb;
                float u = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-4)), w = r * 0.26 * C.y * (1 - life * 0.6) * (1 - u) * (1 - u) + 0.5;
                Add(acc, lerp(core, col, saturate(u * 1.6)), a * (1 - u) * saturate((w - length(pa - ba * u)) / 2 + 0.5));
            }
        } else if (type == 9) {   // the steel glint across the face (its rest event, over the face)
            float x = -r + ev / 720 * 2 * r; float2 a0 = float2(x - 10 * s, -r), dv = float2(20 * s, 2 * r);
            float u = dot(p - a0, dv) / dot(dv, dv);
            if (ev < 720) Add(acc, col, B.a * saturate(1 - abs(u - 0.5) * 2) * saturate(r - d + 0.5));
        } else if (type == 10) {   // a drip off the bottom of the rim (per s)
            float life = frac(t / per + H(seed));
            Add(acc, col, a * (1 - life) * saturate(C.x * s - length(p - float2(sin(seed) * 4 * s, r + (2 + life * 3) * s)) + 0.5));
        } else if (type == 11) Sparkles(acc, p, r, t, seed, col, n);
        else if (type == 12) {   // n smoke puffs on r + off, size px, lapping, rising up px a cycle (the kit's smoke())
            [loop] for (int i = 0; i < 6; ++i) {
                if (i >= n) break;
                float aa = ph + t / per * TAU * (i % 2 ? 1 : -0.8) + i * TAU / n;
                float2 c = float2(cos(aa), sin(aa)) * (r + C.x * s) - float2(0, frac(t / 3.4 + i / (float)n) * C.z * s);
                float u = length(p - c) / (C.y * s);
                AddK(acc, col, u < 0.55 ? lerp(a, 0.45 * a, u / 0.55) : u < 1 ? lerp(0.45 * a, 0, (u - 0.55) / 0.45) : 0, nb);
            }
        } else if (type == 13) {   // charge beads: 3 on r + off, the kit's steady 5 s loop (BeadOn), outside the event gate
            [loop] for (int i = 0; i < 3; ++i) {
                float on = BeadOn(t, seed, i);
                float aa = UP + i * TAU / 3 + t / 4 * TAU, u = length(p - float2(cos(aa), sin(aa)) * (r + C.x * s)) / (C.y * s * max(on, 0.01));
                AddN(acc, float3(0.04, 0.027, 0.02), 0.95 * on * saturate((1.25 - u) * 4));
                AddN(acc, u < 0.45 ? float3(1, 1, 1) : col, on * saturate((1 - u) * 4));
            }
        } else if (type == 14) {   // n curse marks (kit markAt), upright, at the arc heads + C.z on r + C.x, x C.y; the shape = the tint's blue
            int sh = clamp((int)round(tint.b * 255) - 1, 0, 10); int2 mk = MarkK[sh]; float4 me = MarkE[sh];
            float base = (per != 0 ? ph + t / per * TAU : 0) + C.z, sc = C.y * s, hc = max(1, 1.05 * sc), hd = max(1.8, 2.2 * sc);
            [loop] for (int i = 0; i < 4; ++i) {
                if (i >= n) break;
                float aa = base + i * TAU / n; float2 q = (p - float2(cos(aa), sin(aa)) * (r + C.x * s)) / sc;
                if (dot(q, q) > 64) continue;
                float m = me.z > 0 ? EllD(q - me.xy, me.zw) : 1e9;
                [loop] for (int j = 0; j < 8; ++j) {
                    if (j >= mk.y) break;
                    float4 sg = MarkSeg[mk.x + j];
                    m = min(m, Seg(q, sg.xy, sg.zw) - (any(sg.xy != sg.zw) ? 0 : 1));
                }
                m *= sc;
                AddN(acc, float3(0.027, 0.02, 0.016), 0.8 * a * saturate(hd + 0.5 - m));   // the kit's 4.4 px dark under-stroke
                AddN(acc, col, a * saturate(hc + 0.5 - m));                               // its 2.1 px mark (2 px at least)
            }
        } else if (type == 15) {   // n bits on a flat ellipse under the icon (C: C.x r down, half widths C.y r and C.z r), lapping;
            // D: half size px + hash x D.y; D.z 0 squares turning with their angle (kit golem rubble), 1 flat diamonds (sigil ticks); D.w lift
            float base = ph + (per != 0 ? t / per * TAU : 0);
            [loop] for (int i = 0; i < 8; ++i) {
                if (i >= n) break;
                float aa = base + i * TAU / n, sz = (D.x + H(seed + i) * D.y) * s, sq;
                float2 q = p - float2(cos(aa) * C.y * r, C.x * r + sin(aa) * C.z * r);
                if (D.z < 0.5) { float ro = aa * 2 + i, cr = cos(ro), sr = sin(ro); float2 u = abs(float2(q.x * cr + q.y * sr, q.y * cr - q.x * sr)); sq = max(u.x, u.y) - sz; }
                else sq = (abs(q.x) + abs(q.y / 0.55)) * 0.7071 - sz;   // a square turned 45 deg, squashed to 55%
                if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.8 * a * saturate(1.7 - sq));
                AddK(acc, min(1, col + D.w), a * saturate(0.5 - sq), nb);
            }
        } else if (type == 16) {   // a flat ellipse C.x r below the center, half widths C.y r and C.y r x C.z, C.w px (kit strokeDual)
            float m = EllD(p - float2(0, C.x * r), float2(C.y, C.y * C.z) * r), w = max(2, C.w * s);
            if (dark) AddN(acc, float3(0.027, 0.02, 0.016), 0.5 * a * saturate(w * 1.2 + 0.5 - m));
            AddK(acc, col, a * saturate(w * 0.5 + 0.5 - m), nb);
        }
    }
}
float4 Rest(uint kind, bool over, float2 p, float R, float t, float seed, float ev, float3 tint) {
    float r = R / 1.5, d = length(p);   // the quad's radius is the rest cap, 1.5 r (designer, 2026-11-07)
    float4 acc = 0;
    if (d >= R || (!over && d < r - 1.5)) return acc;   // perf: 44% of a rest quad ((r / 1.5 r) squared) lies under the face; only the over pass reads it
    // A kit kind's S1 gains: HaloK on its halos, fig on the whole (the figures' own where the halo is gone; the class kinds'
    // are in their tables). The halo's gain is its own: divided by fig, which multiplies everything at the end.
    const float fig = kind < 12 ? RestBuiltinFig[kind] : 1;
    HaloK = kind < 12 ? RestGain[kind] * RestBuiltinGain[kind] / max(fig, 0.01) : 1;
    if (kind >= (uint)RestFirstExtra) RestLayers(acc, kind, over, p, r, t, seed, ev, tint);
    else if (kind == 0 && !over) RestFire(acc, p, r, t, seed, 1);
    else if (kind == 1 && !over) RestCold(acc, p, r, t, seed, 1);
    else if (kind == 2) RestLight(acc, p, r, t, seed, 1, over);
    else if (kind == 3 && !over) {   // poison: 4 blobs on 1.05 r, the kit's 0.6 r each (the cap clips them), a lap in 4 s; one drip
        [loop] for (int i = 0; i < 4; ++i) {
            float a = t / 4 * 6.2831853 + i * 1.5707963, u = saturate(length(p - (float2(cos(a), sin(a) * 0.9) * r * 1.05 + float2(0, 1))) / (0.6 * r));
            Add(acc, u < 0.5 ? lerp(float3(0.78, 1, 0.42), float3(0.37, 0.75, 0.16), u * 2) : float3(0.37, 0.75, 0.16), u < 0.5 ? lerp(0.6, 0.22, u * 2) : lerp(0.22, 0, u * 2 - 1));
        }
        float life = frac(t / 1.8 + H(seed));
        Add(acc, float3(0.78, 1, 0.42), 0.8 * (1 - life) * saturate(2.2 - length(p - float2(sin(seed) * 6, r + life * 4.5)) + 0.5));
    } else if (kind == 4 && !over) {   // arcane: halo, a SOLID ring on r + 5 (R2: no dashes), 3 sparkles
        Add(acc, float3(0.55, 0.47, 1), Halo(d, r, 0.85 * r, 0.3, 1.35));
        Add(acc, float3(0.78, 0.75, 1), 0.55 * Stroke(d, r + 6, 2));   // the kit's ring on r + 6, solid (R2)
        Sparkles(acc, p, r, t, seed, float3(0.85, 0.82, 1), 3);
    } else if (kind == 5 && !over) {   // holy: halo, 4 sparkles
        Add(acc, float3(0.95, 0.79, 0.30), Halo(d, r, 0.8 * r, 0.5, 1.4));
        Sparkles(acc, p, r, t, seed, float3(1, 0.96, 0.75), 4);
    } else if (kind == 6 && !over) {   // aura (tint = the aura's color): two ripples r to 1.3 r in 2.8 s, 2 px or more, 60% at most
        [loop] for (int i = 0; i < 2; ++i) { float q = frac(t / 2.8 + i / 2.0 + H(seed)); Add(acc, float3(1, 1, 1), 0.6 * (1 - q) * Stroke(d, r * (1 + 0.3 * q), max(2, 3 * (1 - q) + 1))); }
    } else if (kind == 7 && !over) {   // shadow: 6 puffs on the rim (the kit's 0.55 r cut to 0.22 r), a lap in 6 s or more; the arc on r + 5
        [loop] for (int i = 0; i < 6; ++i) {
            float a = t / 3.6 * 6.2831853 * (i % 2 ? 0.6 : -0.5) + i * 1.0471976, rr = r * (1.05 + 0.12 * sin(t / 0.9 + i));
            float u = saturate(length(p - (float2(cos(a), sin(a)) * rr - float2(0, frac(t / 3 + i / 6.0) * 6))) / (0.55 * r));   // the kit's puffs
            AddN(acc, u < 0.6 ? lerp(float3(0.43, 0.33, 0.67), float3(0.11, 0.07, 0.19), u / 0.6) : float3(0.11, 0.07, 0.19), u < 0.6 ? lerp(0.7, 0.35, u / 0.6) : lerp(0.35, 0, (u - 0.6) / 0.4));
        }
        float a = atan2(p.y, p.x), from = t / 2, off = frac((a - from) / 6.2831853) * 6.2831853;
        Add(acc, float3(0.56, 0.49, 0.76), 0.45 * Stroke(d, r + 5, 2) * (off < 2.2 ? 1 : 0));
    } else if (kind == 8 && over) RestGlint(acc, p, r, ev);   // steel: the glint on the face only
    else if (kind == 9) {   // blood: a red halo, the steel glint
        if (!over) Add(acc, float3(0.71, 0.12, 0.12), Halo(d, r, 0.85 * r, 0.35, 1.35)); else RestGlint(acc, p, r, ev);
    } else if (kind == 10 && !over) {   // charge (tint = its color): 3 orbs on r + 4, the kit's steady 5 s loop (BeadOn)
        [loop] for (int i = 0; i < 3; ++i) {
            float on = BeadOn(t, seed, i);
            float a = -1.5707963 + i * 2.0943951 + t / 4 * 6.2831853, u = saturate(length(p - float2(cos(a), sin(a)) * (r + 7)) / (11.7 * max(on, 0.01)));   // the kit's orb: r + 7, s 4.5 x 2.6
            Add(acc, float3(1, 1, 1), on * (u < 0.35 ? lerp(0.95, 0.85, u / 0.35) : lerp(0.85, 0, (u - 0.35) / 0.65)));
        }
    } else if (kind == 11) {   // multi: fire, cold, lightning, 2 s each, the next fading in over the last 400 ms
        float q = frac(t / 6) * 3, f = frac(q); uint i = (uint)q % 3, j = (i + 1) % 3;
        float a = f > 0.8 ? 1 - (f - 0.8) / 0.2 : 1;
        [loop] for (int m = 0; m < 2; ++m) {
            uint e = m == 0 ? i : j; float w = m == 0 ? a : 1 - a;
            if (w <= 0) continue;
            if (e == 0 && !over) RestFire(acc, p, r, t, seed, w);
            else if (e == 1 && !over) RestCold(acc, p, r, t, seed, w);
            else if (e == 2) RestLight(acc, p, r, t, seed, w, over);
        }
    }
    float cap = saturate((R - d) / 2);   // RestCap: 0 at the quad's radius 1.5 r (2 px feather)
    if (kind < 12) acc.rgb *= tint;   // the kit's aura and charge colors (white for the others)
    acc.a = saturate(saturate(acc.a) * cap * fig);
    acc.rgb = min(acc.rgb * cap * fig, 1);   // the lighter layers add: rgb may pass alpha (premultiplied additive)
    return acc;
}
// R8 match stage 2 (r8-match-spec 3 "Pointed effect", chain level 4; 4a): the pointed effect's element default, the kit's
// hover-defaults.js as a function of its age t (s); kind: the rest kind 0-11 (ring_wheel_draw.h HoverKindOf); r: the pointed
// icon's radius; tint: the aura and charge rings' color. Inside the quad (90 px), a 15 px soft edge. Repeats at most 3 a
// second, each fading in over 100 ms or more (spec 4a): fire 6 embers a 2 s loop (kit 12 a 1.4 s), cold 6 shards a 2 s loop
// (kit 8 a 1.2 s), shadow 6 puffs (kit 8), blood 3 drops (kit 5), charge 3 rings a second (kit 4.8); rings solid (the kit's
// arcane dash marches), at most 60%, 2 px or more. Lightning: its bolts are their own quads (HoverBolt).
void GlowDot(inout float4 acc, float2 p, float2 c, float s, float3 col, float a) {   // kit glowDot: white core, color at 0.35, 0 at s
    float u = length(p - c) / s;
    if (u < 1) Add(acc, u < 0.35 ? lerp(float3(1, 1, 1), col, u / 0.35) : col, a * (u < 0.35 ? lerp(1, 0.9, u / 0.35) : lerp(0.9, 0, (u - 0.35) / 0.65)));
}
void HoverEl(inout float4 acc, uint k, float2 p, float r, float t, float seed, float3 tint, float w) {
    const float TAU = 6.2831853, PI = 3.14159265;
    float d = length(p), ang = atan2(p.y, p.x);
    if (k == 0) {   // fire: embers burst up off the rim
        [loop] for (int i = 0; i < 6; ++i) {
            float q = frac(t / 2 + H(seed + i)), a = -PI / 2 + (H(seed + i * 7) - 0.5) * 2.2, dd = r + q * 46;
            GlowDot(acc, p, float2(cos(a) * dd + sin(t / 0.3 + i) * 4, sin(a) * dd - q * 18), 6 * (1 - q) + 2, float3(1, 0.59, 0.16), w * 0.8 * sin(q * PI));
        }
    } else if (k == 1) {   // cold: ice shards fly out and fade
        [loop] for (int i = 0; i < 6; ++i) {
            float q = frac(t / 2 + i / 6.0), a = i * TAU / 6 + fmod(seed, 7); float2 u = float2(cos(a), sin(a)), v = p - u * (r + 4 + q * 52);
            float x = dot(v, u), y = abs(dot(v, float2(-u.y, u.x)));   // the kit's shard (9, 0) (-5, +-3), pointing out
            Add(acc, float3(0.78, 0.93, 1), w * 0.9 * sin(q * PI) * saturate(min(min(x + 5, 9 - x), 3 * (9 - x) / 14 - y) + 0.5));
        }
    } else if (k == 3) {   // poison: a green cloud swells and rolls
        // designer S2-1: at the kit's 0.45 it read too faint over a lit ground (the kit's tendrils carry it there): 0.8
        [loop] for (int i = 0; i < 6; ++i) { float q = frac(t / 2 + i / 6.0), a = i * TAU / 6 + t / 3; GlowDot(acc, p, float2(cos(a), sin(a)) * (r + 10 * q), 14 + 18 * q, float3(0.47, 0.86, 0.24), w * 0.8 * sin(q * PI)); }
    } else if (k == 4) {   // arcane: a solid rune ring opens and fades (one a 1.6 s), sparkles
        float q = frac(t / 1.6);
        Add(acc, float3(0.75, 0.67, 1), w * 0.6 * (1 - q) * saturate(q * 16) * Stroke(d, r + 6 + q * 40, 2));
        float4 sp = 0; Sparkles(sp, p, r + 10, t, seed, float3(0.88, 0.85, 1), 4); acc.rgb += sp.rgb * w;
    } else if (k == 5) {   // holy: 8 light rays turning slowly, lengths breathing
        float a = ang - t / 2.4, idx = round(a / (TAU / 8)), da = a - idx * TAU / 8, l = 26 + 10 * sin(t / 0.4 + fmod(idx + 8, 8));
        float along = d * cos(da) - r, side = abs(d * sin(da));
        Add(acc, float3(1, 0.94, 0.71), w * 0.7 * saturate(1 - along / l) * saturate(along + 0.5) * saturate(2.5 - side));
    } else if (k == 6 || k == 10) {   // aura (charge: faster, from 0.9 r): colored rings ripple out
        float per = k == 10 ? 1 : 1.5, r0 = k == 10 ? 0.9 * r : r;
        [loop] for (int i = 0; i < 3; ++i) { float q = frac(t / per + i / 3.0); Add(acc, tint, w * min(0.6, 0.85 * (1 - q)) * saturate(q * per / 0.1) * Stroke(d, r0 + 4 + q * 46, max(2, 3 * (1 - q) + 1))); }
    } else if (k == 7) {   // shadow: smoke spirals out (source-over)
        [loop] for (int i = 0; i < 6; ++i) {
            float q = frac(t / 2.2 + i / 6.0), a = i * TAU / 6 + q * 2.5, u = length(p - float2(cos(a), sin(a)) * (r + 6 + q * 30)) / 16;
            if (u < 1) AddN(acc, lerp(float3(0.35, 0.27, 0.59), float3(0.12, 0.08, 0.24), u), w * 0.6 * sin(q * PI) * (1 - u));
        }
    } else if (k == 8 || k == 9) {   // steel: two slash arcs cross the icon (blood: and drops fall)
        float q = frac(t / 1.3);
        if (q < 0.45) { float f = q / 0.45; [loop] for (int s2 = 0; s2 < 2; ++s2) { float a0 = (s2 ? 2.6 : -0.6) + f * 1.4 * (s2 ? -1 : 1), da = frac((ang - a0) / TAU) * TAU; Add(acc, float3(1, 0.97, 0.9), w * 0.8 * sin(f * PI) * Stroke(d, r + 8, 3) * (da < 0.9 ? 1 : 0)); } }
        if (k == 9) [loop] for (int i = 0; i < 3; ++i) { float q2 = frac(t / 1.3 + i / 3.0); GlowDot(acc, p, float2((H(seed + i) - 0.5) * r, r + q2 * 30), 4, float3(0.78, 0.12, 0.12), w * 0.7 * (1 - q2) * saturate(q2 * 13)); }
    }
}
float4 Hover(uint kind, float2 p, float R, float t, float seed, float r, float3 tint) {
    float4 acc = 0;
    if (kind == 11) {   // multi: fire, cold, lightning 1.2 s each, the next fading in over the last 200 ms (ring_wheel_draw.h MultiWeight)
        float q = t / 1.2, f = frac(q), fin = saturate((f - 1 + 0.2 / 1.2) / (0.2 / 1.2)); uint cur = (uint)floor(q) % 3, nx = (cur + 1) % 3;
        if (cur < 2) HoverEl(acc, cur, p, r, t, seed, tint, 1 - fin);
        if (nx < 2) HoverEl(acc, nx, p, r, t, seed, tint, fin);
    } else HoverEl(acc, kind, p, r, t, seed, tint, 1);
    return acc * saturate((R - length(p)) / PointedEdgePx);
}
// The lightning default's bolt to a neighbor (kit k.boltPath, #FFFBE0 on #FFD84A): along +x over the quad, L long; a new
// shape every 333 ms with a 100 ms crossfade (the R8 comfort cap; the kit's is 90 ms).
float4 HoverBolt(float2 p, float L, float t, float seed) {
    float st = floor(t / 0.333), x = saturate((t - st * 0.333) / 0.1);
    float4 acc = 0;
    [loop] for (int j = 0; j < 2; ++j) {
        float k = j == 0 ? x : 1 - x;
        if (k <= 0) continue;
        float m = Bolt(p, float2(-L / 2, 0), float2(L / 2, 0), seed + (st - j) * 13, 14);
        Add(acc, float3(1, 0.85, 0.29), k * 0.5 * saturate(1 - m / 5));
        Add(acc, float3(1, 0.98, 0.88), k * saturate(1.6 - m));
    }
    return acc;
}
// R7c (feedback 1 item 7): a tree mark's glyph (kit glyph()), in units of its size r, canvas y down. The fills (flame, drop,
// bolt) are the kit's paths as polygons (its beziers sampled; the flame is the designer's tree-mark flame, R7c answer 2); the rest strokes max(2, 0.12 r) px wide with round ends.
static const float2 GlyphPoly[28] = {
    float2(-0.309, 0.666), float2(-0.511, 0.467), float2(-0.600, 0.180), float2(-0.572, -0.168), float2(-0.420, -0.550),
    float2(-0.120, -0.180), float2(0.180, -0.900), float2(0.402, -0.549), float2(0.523, -0.177), float2(0.514, 0.185), float2(0.349, 0.504), float2(0.000, 0.750),   // fire (the tree-mark flame)
    float2(0.274, -0.291), float2(0.389, 0.078), float2(0.367, 0.376), float2(0.230, 0.577), float2(0.000, 0.650), float2(-0.230, 0.577),
    float2(-0.367, 0.376), float2(-0.389, 0.078), float2(-0.274, -0.291), float2(0.000, -0.700),                                         // poison
    float2(0.200, -0.750), float2(-0.300, 0.050), float2(0.050, 0.050), float2(-0.200, 0.750), float2(0.350, -0.150), float2(0.000, -0.150) };  // lightning
float PolyD(float2 p, int o, int N) {   // signed distance to a closed polygon, negative inside
    float d = 1e9, s = 1;
    for (int i = 0; i < N; ++i) {
        int j = i == 0 ? N - 1 : i - 1;
        float2 vi = GlyphPoly[o + i], vj = GlyphPoly[o + j], e = vj - vi, w = p - vi, b = w - e * saturate(dot(w, e) / dot(e, e));
        d = min(d, dot(b, b));
        bool c1 = p.y >= vi.y, c2 = p.y < vj.y, c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * sqrt(d);
}
// kind: ringwheel::MarkGlyph (0 fire, 1 cold, 2 lightning, 3 poison, 4 arcane, 5 holy, 6 aura, 7 shadow, 9 bone, else steel: the
// kit's default).
float TreeGlyph(uint kind, float2 p, float r, float fw) {
    float2 q = p / max(r, 1); float lw = max(2.0, r * 0.12) * 0.5 / max(r, 1), sd;   // sd: signed distance in r units
    if (kind == 0) sd = PolyD(q, 0, 12);
    else if (kind == 3) sd = PolyD(q, 12, 10);
    else if (kind == 2) sd = PolyD(q, 22, 6);
    else if (kind == 1) { float m = 1e9; [unroll] for (int k = 0; k < 3; ++k) { float a = k * 1.0471976; float2 v = float2(cos(a), sin(a)) * 0.7; m = min(m, Seg(q, -v, v)); } sd = m - lw; }
    else if (kind == 4) { float m = abs(length(q) - 0.55); [unroll] for (int k = 0; k < 4; ++k) { float a = k * 1.5707963 + 0.7853982; m = min(m, Seg(q, float2(0, 0), float2(cos(a), sin(a)) * 0.55)); } sd = m - lw; }
    else if (kind == 5) sd = min(Seg(q, float2(0, -0.7), float2(0, 0.7)), Seg(q, float2(-0.45, -0.2), float2(0.45, -0.2))) - lw;
    else if (kind == 7) sd = max(length(q) - 0.6, -(length(q - float2(0.28, -0.15)) - 0.52));
    else if (kind == 6) { float m = 1e9; [unroll] for (int k = 0; k < 8; ++k) { float a = k * 0.7853982; float2 v = float2(cos(a), sin(a)); m = min(m, Seg(q, v * 0.35, v * 0.7)); } sd = min(m - lw, length(q) - 0.22); }
    else if (kind == 9) {   // bone (R7c TS3, kit 'bone'): a shaft max(2 px, 0.22 r) wide, two 0.2 r knobs at each end (+-0.113 r on both axes, 0.1 r out)
        float sw = max(2.0, r * 0.22) * 0.5 / max(r, 1), m = Seg(q, float2(-0.5, 0.5), float2(0.5, -0.5)) - sw;
        [unroll] for (int k = -1; k <= 1; k += 2) {
            m = min(m, length(q - float2(-0.6 + k * 0.1131, 0.6 + k * 0.1131)) - 0.2);
            m = min(m, length(q - float2(0.6 + k * 0.1131, -0.6 + k * 0.1131)) - 0.2);
        }
        sd = m;
    }
    else sd = min(Seg(q, float2(-0.55, 0.55), float2(0.55, -0.55)), Seg(q, float2(-0.15, -0.55), float2(-0.55, -0.15))) - lw;
    return saturate(0.5 - sd * r / fw);
}
// R7c: the rune band (kit runeRing): n runes from angle 0 (canvas +x), each turned by its angle + 90 deg, 8 px tall in 2 px
// strokes (TS4), one of three by the kit's hash of (i x 31 + seed) (ringwheel::RuneOf): a stem and slash, a caret, a Z.
// Static. On R - 6.5: the middle of the gap between our rings (they stroke inward from their radius: 78-80 and 67-69 at
// rest; the kit centers its strokes, so its r - 5.5 is the same middle).
float KitHash(uint n) { n = (n ^ (n >> 16)) * 0x45d9f3bu; n = (n ^ (n >> 16)) * 0x45d9f3bu; return (n ^ (n >> 16)) / 4294967296.0; }
float Runes(float2 p, float R, uint n, uint seed, float fw) {
    float rr = R - 6.5;
    if (abs(length(p) - rr) > 5 || n == 0) return 0;
    float step = 6.2831853 / n, a = atan2(p.y, p.x);
    if (a < 0) a += 6.2831853;
    uint i = (uint)floor(a / step + 0.5) % n;
    float ac = i * step, c = cos(ac + 1.5707963), s = sin(ac + 1.5707963);
    float2 o = p - float2(cos(ac), sin(ac)) * rr, q = float2(o.x * c + o.y * s, -o.x * s + o.y * c);   // the rune's own frame
    float h = KitHash(i * 31 + seed), m;
    if (h < 0.33) m = min(Seg(q, float2(0, -4), float2(0, 4)), Seg(q, float2(-2.5, -1), float2(2.5, 1)));
    else if (h < 0.66) m = min(Seg(q, float2(-2.5, 4), float2(0, -4)), Seg(q, float2(0, -4), float2(2.5, 4)));
    else m = min(min(Seg(q, float2(-2.5, -4), float2(2.5, -4)), Seg(q, float2(2.5, -4), float2(-2.5, 4))), Seg(q, float2(-2.5, 4), float2(2.5, 4)));
    return saturate((1.0 - m) / fw + 0.5);
}
// Feedback 1 item 3: a bound-button badge (kit drawQuestBadge), premultiplied, in gamma colors. kind 0 a dark iron disc,
// 1 the grip layer's gold coin, 2 the trigger's dark rounded square (corner cr); each with a 2 px gold light rim inside its
// edge, over a 7 px dark halo (70% at the body's radius to 0). The fill is the kit's radial gradient from a 1 px circle at
// (-0.4 Rb, -0.4 Rb) to the body circle: t solves |p - c(t)| = r(t) (canvas two-circle gradient), clamped 0-1.
float4 Badge(float2 p, float d, float Rb, float cr, uint kind, float fw) {
    float sd;
    if (kind == 2) { float2 q = abs(p) - (Rb - cr); sd = length(max(q, 0)) + min(max(q.x, q.y), 0) - cr; }
    else sd = d - Rb;
    float2 pp = p + 0.4 * Rb, dc = 0.4 * Rb;
    float dr = Rb - 1, a = dot(dc, dc) - dr * dr, b = dot(pp, dc) + dr, c = dot(pp, pp) - 1;
    float t = abs(a) > 1e-4 ? (b - sqrt(max(b * b - a * c, 0))) / a : c / (2 * b);
    t = saturate(t);
    float3 fill = kind == 1 ? (t < 0.6 ? lerp(float3(1, 0.941, 0.769), float3(0.910, 0.780, 0.494), t / 0.6)
                                       : lerp(float3(0.910, 0.780, 0.494), float3(0.788, 0.635, 0.369), (t - 0.6) / 0.4))
                            : lerp(float3(0.290, 0.220, 0.125), float3(0.078, 0.067, 0.051), t);
    float rim = saturate((sd + 2) / fw + 0.5);
    float3 col = lerp(fill, float3(0.910, 0.780, 0.494), rim);
    if (linearOut > 0.5) col = ToLinear(col);
    float body = saturate(-sd / fw + 0.5), halo = 0.7 * saturate(1 - max(d - Rb, 0) / 7);
    return float4(col * body, body + halo * (1 - body));
}
// T036: our own dark disc under a clean icon (the game's plate keyed out on the atlas worker, ring_wheel_unbake.h): the mockup's
// radial gradient, #2C241B up left to #100C08 at the edge, opaque. A keyed icon shows its glyph on it; an opaque picture (an icon
// the key skips) covers it whole. p: from the face's center, r: the face's radius.
float4 UnderDisc(float2 p, float r) {
    float t = saturate(length(p + float2(0.3, 0.35) * r) / (1.35 * r));
    float3 col = lerp(float3(0.173, 0.141, 0.106), float3(0.063, 0.047, 0.031), t);
    if (linearOut > 0.5) col = ToLinear(col);
    return float4(col, 1);
}
float4 OverDisc(float4 art, float2 p, float r) { return art + UnderDisc(p, r) * (1 - art.a); }
// T036: the third holding p (0 Glyphs upper left, 1 Gestures upper right, 2 Quick cast bottom; canvas y down), ringwheel::ThirdAt
// without the dead bands.
uint ThirdOfPoint(float2 p) { float a = degrees(atan2(p.y, p.x)); return a >= -90 && a < 30 ? 1 : a >= 30 && a < 150 ? 2 : 0; }
// T036 Element frame (the mockup's layerFrame): kind = ringwheel::Element; r0 the band's inner radius, b its width; coverage and
// a 0-1 shade (0 the base color, 1 its light) in the band only. Static: no time.
float2 Frame(uint kind, float2 p, float r0, float b, float fw) {
    float d = length(p), a = atan2(p.y, p.x), t = (d - r0) / max(b, 1), cov = 0, shade = 0;
    float base = saturate((1.2 - abs(d - r0)) / fw + 0.5);   // a thin ring at the band's foot
    if (kind == 1) {   // cold: 10 shards, long and short, from the foot out
        float st = 6.2831853 / 10, k = round((a + 1.5707963) / st), da = a + 1.5707963 - k * st, L = (fmod(abs(k), 2) > 0.5) ? 0.62 : 1;
        float w = radians(fmod(abs(k), 2) > 0.5 ? 6 : 8) * r0, half = w * (1 - t / L), e = abs(da) * d;
        cov = t >= 0 && t <= L ? saturate((half - e) / fw + 0.5) : 0; shade = 1 - abs(da) * d / max(w, 1);
    } else if (kind == 0) {   // fire: 12 tongues leaning one way, their light at the foot
        float st = 6.2831853 / 12, k = round((a + 1.5707963) / st), da = a + 1.5707963 - k * st - radians(fmod(abs(k), 2) > 0.5 ? 4 : -4);
        float L = 0.55 + 0.45 * frac(sin(k * 13 + 5) * 43758.5453), lean = radians(7) * saturate(t / L), half = radians(9) * r0 * pow(saturate(1 - t / L), 0.7);
        cov = t >= 0 && t <= L ? saturate((half - abs(da - lean) * d) / fw + 0.5) : 0; shade = 1 - saturate(t / L);
    } else if (kind == 2) {   // lightning: three zigzags of 90 deg, each 9 legs between the band's foot and its 75%
        float m = 1e9;
        [unroll] for (int s = 0; s < 3; ++s) {
            float a0 = radians(s * 120 - 80), u = (a - a0) / radians(10);
            u = u < -1 ? u + 36 : u;
            float j = clamp(floor(u), 0, 8);
            float aa = a0 + j * radians(10), ab = aa + radians(10);
            float ra = r0 + b * (fmod(j, 2) > 0.5 ? 0.75 : 0.2), rb = r0 + b * (fmod(j, 2) > 0.5 ? 0.2 : 0.75);
            m = min(m, Seg(p, float2(cos(aa), sin(aa)) * ra, float2(cos(ab), sin(ab)) * rb));
        }
        cov = saturate((1.1 - m) / fw + 0.5); shade = 0.5; base = 0;
    } else if (kind == 3) {   // poison: 8 bubbles at hashed heights and sizes, each with a darker rim
        float st = 6.2831853 / 8, k = round((a + 1.2217305) / st), ak = k * st - 1.2217305, h0 = frac(sin(k * 7 + 3) * 43758.5453), h1 = frac(sin(k * 11 + 1) * 43758.5453);
        float2 q = float2(cos(ak), sin(ak)) * (r0 + b * (0.3 + 0.4 * h0));
        float rr = 1.6 + 1.8 * h1, dd = length(p - q);
        cov = saturate((rr - dd) / fw + 0.5); shade = 1 - saturate((dd - rr + 1.2) / 1.2);
    } else {   // arcane and the rest: 12 rune ticks mid band (a caret, a stem, a slash)
        float st = 6.2831853 / 12, k = round((a + 1.5707963) / st), ak = k * st - 1.5707963, s = b * 0.38;
        float2 o = p - float2(cos(ak), sin(ak)) * (r0 + b * 0.5), q = float2(o.x * -sin(ak) + o.y * cos(ak), o.x * cos(ak) + o.y * sin(ak));
        uint v = (uint)fmod(abs(k), 3);
        float m = v == 0 ? min(Seg(q, float2(-s, s), float2(0, -s)), Seg(q, float2(0, -s), float2(s, s))) : v == 1 ? Seg(q, float2(0, -s), float2(0, s)) : Seg(q, float2(-s, -s), float2(s, s));
        cov = saturate((0.9 - m) / fw + 0.5); shade = 0.5; base *= 0.7;
    }
    return float2(max(cov, base * 0.75), shade * (cov > base * 0.75 ? 1 : 0));
}
float4 PS(V i) : SV_Target {
    float R = i.shape.x, d = length(i.local);
    float fw = max(fwidth(d), 0.75);
    float2 dx = ddx(i.uv), dy = ddy(i.uv);
    uint recipe = (uint)i.shape.z;
    float4 c;   // premultiplied, before the opacity
    if (recipe == 8) {   // R8 icon art motion: a face copy moved by (dx, dy) = tint.rg, cut to its seat's circle (kit: clipped to the icon)
        float4 art = gIcons.SampleGrad(gLinear, i.uv, dx, dy);
        if (linearOut > 0.5) art.rgb = ToLinear(art.rgb / max(art.a, 1.0 / 255)) * art.a;
        art = OverDisc(art, i.local, R);   // T036: the copy carries its disc (the seat under it draws only its shade and rings)
        c = art * saturate((R - d) / fw + 0.5) * saturate((R - length(i.local + i.tint.rg)) / fw + 0.5);
    } else if (recipe == 0) {
        float4 art = gIcons.SampleGrad(gLinear, i.uv, dx, dy);   // premultiplied in gamma, read through a UNORM view
        // An sRGB target blends in linear light: undo the alpha, decode, premultiply again (review R5 K4: a _SRGB view
        // would decode the premultiplied value and darken every soft edge).
        if (linearOut > 0.5) art.rgb = ToLinear(art.rgb / max(art.a, 1.0 / 255)) * art.a;
        if (i.shape.y < 0) art.rgb = dot(art.rgb, float3(0.299, 0.587, 0.114));   // a negative rim: the art in gray (an Off gesture, R7 RS1)
        float pw = abs(i.shape.y);
        if (pw >= 16) { pw -= 16; art = OverDisc(art, i.local, R); }   // T036: +-(rim + 16): a clean icon on our dark disc
        float rim = saturate((d - (R - pw)) / fw + 0.5);
        c = lerp(art, float4(i.tint.rgb, 1), rim) * saturate((R - d) / fw + 0.5);
    } else if (recipe == 7) {   // (recipe 6, the effects: PSFx)   // the lightning face crawl: two bolts across the face, new every 70 ms (uv.x = the step), clipped to it
        float st = i.uv.x, a0 = H(st) * 6.2831853, a1 = a0 + 3.14159 * (0.7 + 0.5 * H(st * 3));
        float2 p0 = float2(cos(a0), sin(a0)) * R * 0.9, p1 = float2(cos(a1), sin(a1)) * R * 0.9, m0 = float2(cos(a0), sin(a0)) * R * 0.2;
        float b = min(Bolt(i.local, p0, p1, st * 11, R * 0.55), Bolt(i.local, m0, float2(cos(a1 + 1), sin(a1 + 1)) * R * 0.85, st * 17, R * 0.4));
        float core = saturate(1.6 - b), glow = saturate(1 - b / 5) * 0.5;
        c = float4(lerp(float3(1, 0.85, 0.3) * glow, float3(1, 0.98, 0.88), core), max(core, glow)) * saturate((R - d) / fw + 0.5);
    } else if (recipe == 11) {   // R8 match stage 1: a seat's icon as the kit's drawIcon (art to r, rim shade, dark line, rim outside)
        // T036 layers: param = +-(width + 16 x flags), 1 no art (the Icon layer Off: no disc, art or shade), 2 no ring (Color ring Off:
        // no dark line or rim). The art sits on our dark disc (a clean icon).
        float pv = abs(i.shape.y);
        uint flags = (uint)(pv / 16);
        float r = R - 4.5, w = pv - flags * 16, face = saturate((r - d) / fw + 0.5);
        bool artOn = (flags & 1) == 0, ringOn = (flags & 2) == 0;
        float4 art = 0;
        if (i.shape.y > 0 && artOn) {
            art = gIcons.SampleGrad(gLinear, i.uv, dx, dy);
            if (linearOut > 0.5) art.rgb = ToLinear(art.rgb / max(art.a, 1.0 / 255)) * art.a;
            art = OverDisc(art, i.local, r);
        }
        float shade = artOn ? 0.55 * saturate((d - 0.6 * r) / (0.4 * r)) * face : 0;   // kit rimShade: 0 at 0.6 r, 55% black at r
        c = art * face;
        c = float4(c.rgb * (1 - shade), c.a + (1 - c.a) * shade);
        float dark = ringOn ? saturate((1.5 - abs(d - (r + 1))) / fw + 0.5) : 0, rim = ringOn ? saturate((w * 0.5 - abs(d - (r + 1.5 + w * 0.5))) / fw + 0.5) : 0;
        c = float4(0.027, 0.020, 0.016, 1) * dark + c * (1 - dark);   // 3 px #070504 on r + 1
        c = float4(i.tint.rgb, 1) * rim + c * (1 - rim);               // the rim (or the hover ring)
    } else if (recipe == 15) {   // T025 a tree mark's painted square (the art is borderless full-bleed rust, so the plate's frame is drawn here): the cell as a
        // rounded square (half side R - 4.5), a bevel (a lit upper left edge, a shaded lower right, 2 px wide) over it, a 3 px #070504 line, the rim in the tint
        float r = R - 4.5, cr = 0.14 * r;
        float2 q = abs(i.local) - (r - cr);
        float sd = length(max(q, 0)) + min(max(q.x, q.y), 0) - cr;   // the face's signed distance: inside < 0
        float4 art = gIcons.SampleGrad(gLinear, i.uv, dx, dy);
        if (linearOut > 0.5) art.rgb = ToLinear(art.rgb / max(art.a, 1.0 / 255)) * art.a;
        art = OverDisc(art, i.local, r);
        c = art * saturate(0.5 - sd / fw);
        float edge = saturate(1 + sd / 2.5) * saturate(0.5 - sd / fw);   // 0 inside, 1 on the outer 2.5 px
        float lit = saturate(dot(normalize(i.local + 1e-4), float2(-0.7071, -0.7071)));   // toward the upper left
        float3 bev = lerp(float3(0, 0, 0), float3(1, 0.86, 0.66), lit) ;
        c.rgb = lerp(c.rgb, bev * c.a, edge * lerp(0.45, 0.30, lit));   // dark toward the lower right, a warm light toward the upper left
        float w = abs(i.shape.y);
        float dark = saturate((1.5 - abs(sd - 1)) / fw + 0.5), rim = saturate((w * 0.5 - abs(sd - (1.5 + w * 0.5))) / fw + 0.5);
        c = float4(0.027, 0.020, 0.016, 1) * dark + c * (1 - dark);
        c = float4(i.tint.rgb, 1) * rim + c * (1 - rim);
    } else if (recipe == 13) {   // T036 a third of the center: param = the third (+ 4: solid, the backing; + 8: its fill, a light
        // ramp outward from 70%; else its 2.5 px outline), uv.x = its inner radius
        uint p = (uint)(i.shape.y + 0.5), t = p & 3;
        float r0 = i.uv.x, inSlice = ThirdOfPoint(i.local) == t ? 1 : 0;
        float cover = inSlice * saturate((R - d) / fw + 0.5) * saturate((d - r0) / fw + 0.5);
        if (p >= 8) c = float4(i.tint.rgb * lerp(0.7, 1, saturate((d - r0) / max(R - r0, 1))), 1) * cover;
        else if (p >= 4) c = float4(i.tint.rgb, 1) * cover;
        else {   // the outline: the nearest of its outer arc, inner arc and two spokes (a third starts at 150, -90 or 30 deg)
            float a0 = t == 0 ? 150 : t == 1 ? -90 : 30, rel = fmod(degrees(atan2(i.local.y, i.local.x)) - a0 + 720, 360);
            float e = min(min(R - d, d - r0), d * sin(radians(clamp(min(rel, 120 - rel), 0, 90))));
            c = float4(i.tint.rgb, 1) * cover * saturate((2.5 - e) / fw + 0.5);
        }
    } else if (recipe == 14) {   // T036 Element frame: param = the element, uv = (inner radius, width)
        float2 f = Frame((uint)(i.shape.y + 0.5), i.local, i.uv.x, i.uv.y, fw);
        float3 col = lerp(i.tint.rgb, lerp(i.tint.rgb, float3(1, 0.97, 0.88), 0.55), f.y);
        c = float4(col, 1) * f.x;
    } else if (recipe == 9) {   // R7c the rune band: uv = (the hash seed, the dimmed half: 1 top, 2 bottom)
        // T036: 4 + third + 3 x percent: the thirds, that third full and the others at the percent
        uint dim = (uint)(i.uv.y + 0.5);
        float k = (dim == 1 && i.local.y < 0) || (dim == 2 && i.local.y > 0) ? 0.55 : 1;
        if (dim >= 4) k = ThirdOfPoint(i.local) == (dim - 4) % 3 ? 1 : ((dim - 4) / 3) / 100.0;
        c = float4(i.tint.rgb, 1) * (Runes(i.local, R, (uint)(i.shape.y + 0.5), (uint)(i.uv.x + 0.5), fw) * k);
    } else if (recipe == 10) {   // R7c a tree mark's glyph: param = its kind, u0 = its size r
        c = float4(i.tint.rgb, 1) * TreeGlyph((uint)(i.shape.y + 0.5), i.local, i.shape.w, fw);
    } else if (recipe == 12) {   // feedback 1 item 3 a bound-button badge: param = its kind, uv = (body radius, the square's corner)
        c = Badge(i.local, d, i.uv.x, i.uv.y, (uint)(i.shape.y + 0.5), fw);
    } else if (recipe == 4) {
        float2 g = gGlyphs.SampleGrad(gLinear, i.uv, dx, dy).rg;   // fill, outline
        c = float4(i.tint.rgb * g.r, i.shape.y > 0.5 ? g.r : max(g.r, g.g));   // param 1: the fill only (the badge coin's letter)
    } else {
        float cover = 1;
        if (recipe == 1 || recipe == 5) cover = saturate((R - d) / fw + 0.5) * saturate((d - (R - i.shape.y)) / fw + 0.5);
        if (recipe == 5) {   // the hold ring: clockwise from the top to the fill (canvas y down)
            float a = atan2(i.local.x, -i.local.y) / 6.2831853;
            cover *= (a < 0 ? a + 1 : a) <= i.shape.w ? 1 : 0;
        }
        else if (recipe == 1 && i.shape.w > 0) {   // dashed (R7 RS5): u0 = the period in px along the ring, 60% drawn
            float a = atan2(i.local.x, -i.local.y) + 3.14159265;
            cover *= frac(a * (R - i.shape.y * 0.5) / i.shape.w) < 0.6 ? 1 : 0;
        }
        else if (recipe == 2) cover = saturate((R - d) / max(i.shape.y, fw));
        c = float4(i.tint.rgb, 1) * cover;
    }
    c = saturate(c) * i.tint.a;
    clip(max(c.a, max(c.r, max(c.g, c.b))) - 1.0 / 255);   // keep additive light (R8 match 1b: rgb with no alpha)
    return c;
}
// R8 match stage 3a (r8-match-spec 'Stage 3a plan'): the kit's level 3 (hover-game.js), with the game's own effect
// textures from upstream's spell sprite cache (d2r_vr_spell_sprites, read at run time on the worker, never shipped): one R8
// atlas (FxTexPx = 1024), white art whose value is its alpha. Cells (uv): the fire plates 03 and 04, the smoke atlas (2 x 2
// puffs: the kit's chunky cloud stands in), the lightning arc flipbook (4 x 2 frames along x: the kit's single-arc atlas
// stands in) and the soft glow card.
Texture2D<float> gFxTex : register(t3);
static const float4 FxFire03 = float4(0, 0, 0.5, 0.5), FxFire04 = float4(0.5, 0, 0.5, 0.5), FxSmoke = float4(0, 0.5, 0.5, 0.5),
    FxArcs = float4(0.5, 0.5, 0.5, 0.125), FxGlow = float4(0.5, 0.625, 0.25, 0.25);
float FxT(float4 cell, float2 uv, float pxSpan) {   // uv 0-1 over the cell; pxSpan: the screen px the cell's width spans (the mip)
    float lod = max(0, log2(cell.z * 1024 / max(pxSpan, 1)));
    return gFxTex.SampleLevel(gLinear, cell.xy + clamp(uv, 0.002, 0.998) * cell.zw, lod);
}
// kit flames(): the fire plate 1.6 x the pad tall, scrolled up a pad per `speed` ms, through the glow card, gain 4, tinted.
void Flames(inout float4 acc, float2 p, float ageMs, float x, float y, float w, float h, float3 col, float speed, float seed, float k) {
    float2 q = (p - float2(x, y)) / float2(w, h);
    if (any(q < 0) || any(q > 1)) return;
    float f = FxT(FxFire03, float2(q.x, frac(q.y / 1.6 + frac(ageMs / speed + H(seed)))), w);
    float2 m = (q - float2(-0.1, 0.05)) / float2(1.2, 1.35);
    float mask = all(m >= 0) && all(m <= 1) ? FxT(FxGlow, m, w * 1.2) : 0;
    Add(acc, col, k * saturate(f * mask * 4));
}
float4 HoverGame(uint kind, float2 p, float R, float t, float seed, float r) {
    const float TAU = 6.2831853, PI = 3.14159265;
    float4 acc = 0; float ageMs = t * 1000;
    if (kind == 0) {   // fire: the game's flames lick up from the bottom of the icon, fading in over 300 ms
        float k = min(1, ageMs / 300);
        Flames(acc, p, ageMs, -r * 1.25, -r * 0.2, r * 2.5, r * 1.6, float3(0.855, 0.518, 0.114), 1400, seed, k);
        Flames(acc, p, ageMs + 500, -r * 0.8, r * 0.2, r * 1.6, r * 1.1, float3(1, 0.918, 0.533), 1100, seed + 3, k);
    } else if (kind == 3) {   // poison: 5 game cloud puffs swell and roll round the icon (2.3 a second, under the cap)
        [loop] for (int i = 0; i < 5; ++i) {
            float q = frac(ageMs / 2200 + i / 5.0), a = i * TAU / 5 + ageMs / 3200, s = r * (0.9 + 1.1 * q);
            float2 uv = (p - float2(cos(a), sin(a)) * (r + 8 * q)) / s + 0.5;
            if (any(uv < 0) || any(uv > 1)) continue;
            float4 cell = float4(FxSmoke.xy + float2(i % 2, (i / 2) % 2) * 0.25, 0.25, 0.25);
            // Designer S3a FIX 3: the stand-in's puffs fill ~60% of their cell (the kit's chunky cloud ~90%) and are thinner:
            // the inner 80% of the cell at gain 2.5 covers as the kit's cloud at its gain 2 (0.33 vs 0.31 of the cell).
            Add(acc, float3(0.47, 0.863, 0.235), 0.55 * sin(q * PI) * saturate(FxT(cell, 0.1 + uv * 0.8, s / 0.8) * 2.5));
        }
    }
    return acc * saturate((R - length(p)) / PointedEdgePx);
}
// kit arcTo(): a game arc frame stretched along the bolt (+x, L long), a 46 px yellow glow and a 22 px white core, gain 2; a
// new frame every 333 ms with a 100 ms crossfade (the R8 cap; the kit's 90 ms).
float4 HoverBoltTex(float2 p, float L, float t, float seed) {
    float st = floor(t / 0.333), x = saturate((t - st * 0.333) / 0.1), u = p.x / L + 0.5;
    float4 acc = 0;
    if (u < 0 || u > 1) return acc;
    [loop] for (int j = 0; j < 2; ++j) {
        float k = j == 0 ? x : 1 - x;
        if (k <= 0) continue;
        float fr = fmod(st - j + floor(H(seed) * 4), 4);   // the top row's 4 frames (the strong arcs)
        float4 cell = float4(FxArcs.xy + float2(fr * 0.125, 0), 0.125, 0.0625);
        float g = p.y / 46 + 0.5, c = p.y / 22 + 0.5;
        if (g >= 0 && g <= 1) Add(acc, float3(1, 0.784, 0.235), k * 0.9 * saturate(FxT(cell, float2(u, g), L) * 2));
        if (c >= 0 && c <= 1) Add(acc, float3(1, 0.973, 0.847), k * saturate(FxT(cell, float2(u, c), L) * 2));
    }
    return acc;
}
// R8 match stage 2 (Boss, review S1): the effects (recipe 6) in their own pixel shader, so each shader keeps its slot
// budget as the recipes grow; Draw switches pipelines per run of instances (the list's order, so the layering, is kept).
// param = kind + 256 x mode (0 rest, 1 pointed, 2 rest over the face, 3 a pointed bolt, 4 a pointed game-texture default,
// 5 a game-texture bolt) + 4096 for a pointed default's part over its own face; uv = (time s, seed).
float4 PSFx(V i) : SV_Target {
    uint p = (uint)i.shape.y, mode = (p >> 8) & 15;
    // Designer S3a FIX: the kit draws the pointed effect after the face (the flames lick up over its lower third). The over
    // pass repeats it on a quad the size of the face, cut to the face (a = 4096); the under pass keeps the rest (spec 4a).
    bool face = p >= 4096;
    float R = face ? PointedReachPx : i.shape.x;   // the effect's own reach, not the small quad's (review N1: from the C++ constant)
    float4 c;
    if (mode == 1) c = Hover(p & 255, i.local, R, i.uv.x, i.uv.y, i.shape.w, i.tint.rgb);
    else if (mode == 3) c = HoverBolt(i.local, i.shape.w, i.uv.x, i.uv.y);
    else if (mode == 4) c = HoverGame(p & 255, i.local, R, i.uv.x, i.uv.y, i.shape.w);
    else if (mode == 5) c = HoverBoltTex(i.local, i.shape.w, i.uv.x, i.uv.y);
    else c = Rest(p & 255, mode == 2, i.local, i.shape.x, i.uv.x, i.uv.y, i.shape.w, i.tint.rgb);
    if (face) { float d = length(i.local); c *= saturate((i.shape.w - d) / max(fwidth(d), 0.75) + 0.5); }
    if (linearOut > 0.5) c.rgb = ToLinear(saturate(c.rgb));   // gamma light to the sRGB target's linear (additive: no alpha to divide by)
    c = saturate(c) * i.tint.a;
    clip(max(c.a, max(c.r, max(c.g, c.b))) - 1.0 / 255);
    return c;
}
// Perf (the design notes "R8 stage 1b check" finding 1): with no offline blob, the effects draw nothing until the worker has
// compiled PSFx (the slow one); icons, rims and text (PS) show at once.
float4 PSFxNone(V i) : SV_Target { clip(-1); return 0; }
)";
constexpr size_t SliceBytes = 1 << 20;   // frame-budget section 2: at most 1 MB a frame
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void Check(HRESULT hr, const char* what) { Require(SUCCEEDED(hr), what); }
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    return b;
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, D3D12_HEAP_TYPE type, UINT64 bytes, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> out;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&out)), "buffer creation failed");
    return out;
}
D3D12_RESOURCE_DESC TextureDesc(AtlasKind k) {
    D3D12_RESOURCE_DESC d{};
    const UINT px = k == AtlasKind::FxTex ? FxTexPx : AtlasTexPx;
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = px; d.Height = px; d.DepthOrArraySize = 1;
    d.MipLevels = UINT16(k == AtlasKind::Icons ? IconMips : k == AtlasKind::FxTex ? FxTexMips : 1);
    d.Format = k == AtlasKind::Icons ? DXGI_FORMAT_R8G8B8A8_TYPELESS : k == AtlasKind::FxTex ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8_UNORM;
    d.SampleDesc.Count = 1;
    return d;
}
double Now() { LARGE_INTEGER f, t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return double(t.QuadPart) * 1000.0 / double(f.QuadPart); }
}  // namespace

struct Compose::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool linearOut = false;
    void (*say)(const char*) = nullptr;
    Compose::ShaderStats stats;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline, pipelineFx;   // PS; PSFx (recipe 6, the effects), or PSFxNone until FinishEffects
    ComPtr<ID3D12PipelineState> pipelineFxLate;         // the fallback's PSFx, compiled by FinishEffects on the worker
    std::atomic<ID3D12PipelineState*> fxLive{nullptr};  // the effects pipeline Draw uses (swapped once, by FinishEffects)
    std::atomic<bool> fxPending{false};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC fxState{};       // FinishEffects' copy of the pipeline description (its PS replaced)
    std::string source;                                 // the shader source (FinishEffects compiles PSFx from it)
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;            // the compiled VS fxState points at (alive until FinishEffects)
    ComPtr<ID3D12Fence> fence;
    uint64_t value = 0;
    HANDLE event = nullptr;
    unsigned preciseWaits = 0;        // WaitIdle waits ended by the high-resolution timer path (the test's proof)
    // A screenshot's resources the GPU still held when its wait ended (item 4): freed once the fence passes `at`.
    struct ShotParked { ComPtr<ID3D12Resource> target, buffer; ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list; uint64_t at = 0; };
    std::vector<ShotParked> parked;
    bool shotDraw = false;            // Draw is a screenshot's: kept out of the 10 s cost window (TakeWindow)
    ComPtr<ID3D12DescriptorHeap> rtv, srv;
    UINT rtvStride = 0, srvStride = 0;
    struct Slot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> instances;
        void* mapped = nullptr;
        uint64_t fence = 0;   // the work it last held (0: never)
        bool timed = false;   // a compose with timestamps (a Pump has none)
    } slots[3];
    SlotRing ring;
    ComPtr<ID3D12QueryHeap> timestamps;
    ComPtr<ID3D12Resource> readback;
    const uint64_t* stamps = nullptr;   // the readback, mapped once
    double tickMs = 0;                  // ms per timestamp tick (0: no timestamps)
    uint64_t gpuRead = 0;               // the newest compose whose timestamps were read
    CostWindow cpu, gpu;
    // The atlases, two of each kind (front and back), made once; footprints and one staging for the larger.
    ComPtr<ID3D12Resource> tex[3][2];   // icons, glyphs, the game effect textures (stage 3a)
    bool copyDest[3][2] = {{true, true}, {true, true}, {true, true}};
    int front[3] = {-1, -1, -1};
    unsigned flips[3] = {0, 0, 0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot[3][IconMips > FxTexMips ? IconMips : FxTexMips]{};
    ComPtr<ID3D12Resource> staging;
    uint8_t* stagingMem = nullptr;
    AtlasKind pendingKind = AtlasKind::Icons;   // written by the worker before its release store
    struct Upload { int kind = 0, target = 0, mip = 0; UINT row = 0; bool done = false; uint64_t fence = 0; } up;
    size_t maxSlice = 0;
    ~Impl() { if (event) CloseHandle(event); }
    int Mips(int kind) const { return kind == int(AtlasKind::Icons) ? IconMips : kind == int(AtlasKind::FxTex) ? FxTexMips : 1; }
    // Reads every compose whose timestamps completed and are not read yet, oldest first (perf R4 item 4).
    void ReadGpu() {
        if (tickMs <= 0) return;
        const uint64_t done = fence->GetCompletedValue();
        for (;;) {
            int best = -1;
            for (int i = 0; i < 3; ++i)
                if (slots[i].timed && slots[i].fence > gpuRead && slots[i].fence <= done && (best < 0 || slots[i].fence < slots[best].fence)) best = i;
            if (best < 0) return;
            gpuRead = slots[best].fence;
            const uint64_t t0 = stamps[best * 2], t1 = stamps[best * 2 + 1];
            if (t1 >= t0) gpu.Add(double(t1 - t0) * tickMs);
        }
    }
    D3D12_GPU_DESCRIPTOR_HANDLE Table() const {
        D3D12_GPU_DESCRIPTOR_HANDLE h = srv->GetGPUDescriptorHandleForHeapStart();
        h.ptr += UINT64(((front[0] + 1) * 3 + (front[1] + 1)) * 2) * srvStride;
        return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE FxTable() const {   // t3: the game texture atlas (none, 0, 1 after the 18 pairs)
        D3D12_GPU_DESCRIPTOR_HANDLE h = srv->GetGPUDescriptorHandleForHeapStart();
        h.ptr += UINT64(18 + front[2] + 1) * srvStride;
        return h;
    }
    bool Begin(Slot& s, ID3D12PipelineState* pso) { return SUCCEEDED(s.allocator->Reset()) && SUCCEEDED(s.list->Reset(s.allocator.Get(), pso)); }
    bool Submit(int slot, bool timed) {
        auto& s = slots[slot];
        if (FAILED(s.list->Close())) return false;
        ID3D12CommandList* lists[] = {s.list.Get()};
        queue->ExecuteCommandLists(1, lists);
        // Marked before the Signal (review R4): a failed Signal (a removed device) leaves the slot busy for good, so its
        // allocator is never reset while the GPU may hold it.
        s.fence = ++value; s.timed = timed;
        ring.Submitted(slot, value);
        return SUCCEEDED(queue->Signal(fence.Get(), value));
    }
};

Compose::Compose() = default;
// Review R4 J2: the destructor makes no D3D12 call and logs nothing, since it may run at DLL unload (the loader lock, the
// device and the logger may be gone). What Release did not free (the Lab's OnUiReset frees it after the GPU is idle) is
// left allocated, never freed while the GPU may still read it.
Compose::~Compose() { (void)impl.release(); }
uint64_t Compose::Submitted() const { return Ready() ? impl->value : 0; }
uint64_t Compose::Completed() const { return Ready() ? impl->fence->GetCompletedValue() : 0; }
unsigned Compose::SkippedBusy() const { return Ready() ? impl->ring.skippedBusy : 0; }
bool Compose::HasAtlas(AtlasKind k) const { return Ready() && impl->front[int(k)] >= 0; }
unsigned Compose::Flips(AtlasKind k) const { return Ready() ? impl->flips[int(k)] : 0; }
size_t Compose::MaxSliceBytes() const { return Ready() ? impl->maxSlice : 0; }
Compose::ShaderStats Compose::Stats() const { return Ready() ? impl->stats : ShaderStats{}; }
Compose::Window Compose::TakeWindow() {
    Window w;
    if (!Ready()) return w;
    auto& p = *impl;
    p.ReadGpu();
    w.composes = p.cpu.n; w.cpuMean = p.cpu.Mean(); w.cpuMax = p.cpu.max; w.cpuP95 = p.cpu.P95();
    w.gpuSamples = p.gpu.n; w.gpuMean = p.gpu.Mean(); w.gpuMax = p.gpu.max; w.gpuP95 = p.gpu.P95();
    p.cpu.Reset(); p.gpu.Reset();
    return w;
}

unsigned long long ShaderSourceHash(const std::string& s) {   // FNV-1a 64, as tools/gen_ring_wheel_shader.py
    unsigned long long h = 0xcbf29ce484222325ull;
    for (const char c : s) { h ^= uint8_t(c); h *= 0x100000001b3ull; }
    return h;
}
bool ComposeForceCompile = false;
// Review N1: the HLSL's copies of the C++ constants, written ahead of the source (tools/gen_ring_wheel_shader.py writes the same
// line from ring_wheel_draw.h; the source hash check fails the preview if the two ever differ).
static_assert(PointedReachPx == float(int(PointedReachPx)) && PointedEdgePx == float(int(PointedEdgePx)), "whole px: the line prints them as integers");
std::string ShaderConstants() {
    return "static const float PointedReachPx = " + std::to_string(int(PointedReachPx)) + ", PointedEdgePx = " + std::to_string(int(PointedEdgePx)) + ";\n";
}
std::string ComposeShaderSource() { return ShaderConstants() + RestLayersHlsl + Source; }
// Perf (the design notes "R8 stage 1b check" finding 2): one tiny draw with each pipeline into a 1 x 1 target on the worker,
// waited on its fence, so a driver's lazy compile at first use lands here and not on the frame thread's first compose.
// Uses slot 0 before Setup publishes (no Draw can run yet). The ms, or -1 when it could not run (the first compose pays).
double Warm(Compose::Impl& p);
bool Compose::Setup(ID3D12Device* device, ID3D12CommandQueue* queue, int format, void (*say)(const char*)) {
    if (Ready()) return true;
    if (impl) return false;   // set up, not published yet (a failed setup leaves impl null: a later call retries)
    try {
        const auto f = DXGI_FORMAT(format);
        Require(device && queue, "no device");
        Require(f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM ||
            f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, "unsupported target format");
        auto p = std::make_unique<Impl>();
        p->device = device; p->queue = queue; p->format = f; p->say = say;
        p->linearOut = f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        struct Library { HMODULE h = LoadLibraryW(L"d3dcompiler_47.dll"); ~Library() { if (h) FreeLibrary(h); } } library;
        const auto compile = library.h ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(library.h, "D3DCompile")) : nullptr;
        const auto d12 = GetModuleHandleW(L"d3d12.dll");
        const auto serialize = d12 ? reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12, "D3D12SerializeRootSignature")) : nullptr;
        Require(compile && serialize, "shader compiler unavailable");
        ComPtr<ID3DBlob> vs, ps, psFx, errors;
        const std::string source = ComposeShaderSource();   // the constants (N1), then the class recipes' layer table (stage 1b)
        // Perf: the bytecode compiled offline (tools/gen_ring_wheel_shader.py) when it
        // was made from this exact source: no compile on the On edge. A stale blob: compile here (15-20 s) and say so.
        const auto t0 = std::chrono::steady_clock::now();
        p->stats.embedded = ShaderBlobOk && ShaderSourceHash(source) == ShaderBlobHash && !ComposeForceCompile;
        D3D12_SHADER_BYTECODE vsCode{ShaderBlobVs, sizeof ShaderBlobVs}, psCode{ShaderBlobPs, sizeof ShaderBlobPs}, fxCode{ShaderBlobPsFx, sizeof ShaderBlobPsFx};
        if (!p->stats.embedded) {
            auto shader = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
                errors.Reset();
                const HRESULT hr = compile(source.data(), source.size(), "ring-wheel-compose", nullptr, nullptr, entry, target,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
                if (FAILED(hr) && errors && say) say(static_cast<const char*>(errors->GetBufferPointer()));
                Check(hr, "shader compilation failed");
            };
            shader("VS", "vs_5_0", vs); shader("PS", "ps_5_0", ps); shader("PSFxNone", "ps_5_0", psFx);   // PSFx later (FinishEffects)
            p->fxPending.store(true, std::memory_order_relaxed);
            vsCode = {vs->GetBufferPointer(), vs->GetBufferSize()}; psCode = {ps->GetBufferPointer(), ps->GetBufferSize()};
            fxCode = {psFx->GetBufferPointer(), psFx->GetBufferSize()};
        }
        if (const auto reflect = reinterpret_cast<decltype(&D3DReflect)>(GetProcAddress(library.h, "D3DReflect"))) {
            ComPtr<ID3D12ShaderReflection> r;
            D3D12_SHADER_DESC desc{};
            if (SUCCEEDED(reflect(psCode.pShaderBytecode, psCode.BytecodeLength, IID_PPV_ARGS(&r))) && SUCCEEDED(r->GetDesc(&desc))) p->stats.psInstructions = desc.InstructionCount;
            r.Reset(); desc = {};
            if (SUCCEEDED(reflect(fxCode.pShaderBytecode, fxCode.BytecodeLength, IID_PPV_ARGS(&r))) && SUCCEEDED(r->GetDesc(&desc))) p->stats.fxInstructions = desc.InstructionCount;
        }
        D3D12_ROOT_PARAMETER params[4]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[0].Constants = {0, 0, 7};   // 6: the run's base instance
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; params[1].Descriptor = {0, 0};
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0};   // t1 icons, t2 glyphs
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[2].DescriptorTable = {1, &range};
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE fxRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 0};   // t3 the game texture atlas (stage 3a)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[3].DescriptorTable = {1, &fxRange};
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // trilinear: no shimmer when an icon floats (plan E4)
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ComPtr<ID3DBlob> signature;
        const D3D12_ROOT_SIGNATURE_DESC rootDesc{4, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        Check(serialize(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "root serialization failed");
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p->root)), "root creation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
        state.pRootSignature = p->root.Get();
        state.VS = vsCode; state.PS = psCode;
        auto& blend = state.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;   // premultiplied: color and alpha both ONE, INV_SRC_ALPHA
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        state.SampleMask = UINT_MAX;
        state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        state.RasterizerState.DepthClipEnable = TRUE;
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        state.NumRenderTargets = 1; state.RTVFormats[0] = f; state.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&p->pipeline)), "pipeline creation failed");
        state.PS = fxCode;
        Check(device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&p->pipelineFx)), "effects pipeline creation failed");
        p->fxLive.store(p->pipelineFx.Get(), std::memory_order_relaxed);
        p->fxState = state; p->fxState.PS = {}; p->source = source; p->vsBlob = vs;   // fxState.VS points into vs when compiled here
        // perf 1b finding 4: the shaders' load or compile and both pipelines' creation (the driver's DXBC translation)
        p->stats.compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p->fence)), "fence creation failed");
        p->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(p->event != nullptr, "event creation failed");
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = 3;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&p->rtv)), "target descriptors failed");
        p->rtvStride = device->GetDescriptorHandleIncrementSize(heap.Type);
        for (auto& s : p->slots) {
            Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.allocator)), "allocator creation failed");
            Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.allocator.Get(), nullptr, IID_PPV_ARGS(&s.list)), "list creation failed");
            Check(s.list->Close(), "list close failed");
            s.instances = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, sizeof(Instance) * MaxDraw, D3D12_RESOURCE_STATE_GENERIC_READ);
            Check(s.instances->Map(0, nullptr, &s.mapped), "instance map failed");
        }
        // The atlases: two of each kind, their footprints, one staging for the larger, all mapped once.
        D3D12_HEAP_PROPERTIES def{}; def.Type = D3D12_HEAP_TYPE_DEFAULT;
        UINT64 stagingBytes = 0;
        for (int k = 0; k < 3; ++k) {
            const auto d = TextureDesc(AtlasKind(k));
            for (auto& t : p->tex[k])
                Check(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t)), "atlas creation failed");
            UINT64 bytes = 0;
            device->GetCopyableFootprints(&d, 0, d.MipLevels, 0, p->foot[k], nullptr, nullptr, &bytes);
            stagingBytes = std::max(stagingBytes, bytes);
        }
        p->staging = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, stagingBytes, D3D12_RESOURCE_STATE_GENERIC_READ);
        Check(p->staging->Map(0, nullptr, reinterpret_cast<void**>(&p->stagingMem)), "staging map failed");
        // Descriptor pairs (icons, glyphs) for each front: none, 0, 1 of each, so a flip only picks another table.
        heap = {}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = 21; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&p->srv)), "atlas descriptors failed");
        p->srvStride = device->GetDescriptorHandleIncrementSize(heap.Type);
        for (int fi = -1; fi < 2; ++fi) for (int fg = -1; fg < 2; ++fg) {
            D3D12_CPU_DESCRIPTOR_HANDLE h = p->srv->GetCPUDescriptorHandleForHeapStart();
            h.ptr += size_t(((fi + 1) * 3 + (fg + 1)) * 2) * p->srvStride;
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.Texture2D.MipLevels = IconMips;   // always UNORM: the shader decodes (K4)
            device->CreateShaderResourceView(fi >= 0 ? p->tex[0][fi].Get() : nullptr, &d, h);   // a null view reads 0
            h.ptr += p->srvStride;
            d.Format = DXGI_FORMAT_R8G8_UNORM; d.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(fg >= 0 ? p->tex[1][fg].Get() : nullptr, &d, h);
        }
        for (int ff = -1; ff < 2; ++ff) {   // stage 3a: the game texture atlas's views (none: reads 0), after the 18 pairs
            D3D12_CPU_DESCRIPTOR_HANDLE h = p->srv->GetCPUDescriptorHandleForHeapStart();
            h.ptr += size_t(18 + ff + 1) * p->srvStride;
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Format = DXGI_FORMAT_R8_UNORM; d.Texture2D.MipLevels = FxTexMips;
            device->CreateShaderResourceView(ff >= 0 ? p->tex[2][ff].Get() : nullptr, &d, h);
        }
        UINT64 frequency = 0;
        if (SUCCEEDED(queue->GetTimestampFrequency(&frequency)) && frequency) {
            D3D12_QUERY_HEAP_DESC q{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 6, 0};
            if (SUCCEEDED(device->CreateQueryHeap(&q, IID_PPV_ARGS(&p->timestamps)))) {
                p->readback = Buffer(device, D3D12_HEAP_TYPE_READBACK, 6 * sizeof(uint64_t), D3D12_RESOURCE_STATE_COPY_DEST);
                void* m = nullptr;
                if (SUCCEEDED(p->readback->Map(0, nullptr, &m))) { p->stamps = static_cast<const uint64_t*>(m); p->tickMs = 1000.0 / double(frequency); }
            }
        }
        p->stats.warmMs = Warm(*p);
        if (say) {
            char line[240];
            std::snprintf(line, sizeof line, "Frame ring wheel: compose shaders %s, pipelines in %.0f ms, warmed in %.0f ms (pixel shaders %u + effects %u instructions)%s",
                p->stats.embedded ? "loaded (built offline)" : "compiled", p->stats.compileMs, p->stats.warmMs, p->stats.psInstructions, p->stats.fxInstructions,
                p->stats.embedded ? "" : ShaderBlobOk ? "; the build's blob is stale (a build regenerates it); effects after their compile" : "; built without fxc; effects after their compile");
            say(line);
        }
        impl = std::move(p);
        stage.store(0, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& e) {
        if (say) say((std::string("Frame ring wheel: compose setup failed: ") + e.what() + "; the wheel stays upstream's").c_str());
        return false;
    }
}

double Warm(Compose::Impl& p) {
    try {
        const auto t0 = std::chrono::steady_clock::now();
        D3D12_HEAP_PROPERTIES def{}; def.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = 1; d.Height = 1; d.DepthOrArraySize = 1;
        d.MipLevels = 1; d.Format = p.format; d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> target;
        Check(p.device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)), "warm target");
        auto& s = p.slots[0];
        Instance two[2]{};   // a clear rect (PS) and a rest effect (PSFx), both 1 px and transparent
        two[0] = {0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, RecipeRect, 0};
        two[1] = {0, 0, 1, 1, 0, 0, 0, -1, 0, 1, 1, 1, 0, RecipeFx, 0};
        std::memcpy(s.mapped, two, sizeof two);
        Require(p.Begin(s, p.pipeline.Get()), "warm list");
        auto* cl = s.list.Get();
        D3D12_RENDER_TARGET_VIEW_DESC rd{}; rd.Format = p.format; rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = p.rtv->GetCPUDescriptorHandleForHeapStart();
        p.device->CreateRenderTargetView(target.Get(), &rd, rtv);
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT vp{0, 0, 1, 1, 0, 1}; const D3D12_RECT rect{0, 0, 1, 1};
        cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &rect);
        cl->SetGraphicsRootSignature(p.root.Get());
        ID3D12DescriptorHeap* heaps[] = {p.srv.Get()};
        cl->SetDescriptorHeaps(1, heaps);
        const float k[6] = {0, 0, 1, p.linearOut ? 1.f : 0.f, 1, 1};
        cl->SetGraphicsRoot32BitConstants(0, 6, k, 0);
        cl->SetGraphicsRootShaderResourceView(1, s.instances->GetGPUVirtualAddress());
        cl->SetGraphicsRootDescriptorTable(2, p.Table());
        cl->SetGraphicsRootDescriptorTable(3, p.FxTable());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        for (UINT i = 0; i < 2; ++i) {
            if (i) cl->SetPipelineState(p.fxLive.load(std::memory_order_relaxed));
            cl->SetGraphicsRoot32BitConstant(0, i, 6);
            cl->DrawInstanced(4, 1, 0, 0);
        }
        Check(cl->Close(), "warm close");
        ID3D12CommandList* lists[] = {cl};
        p.queue->ExecuteCommandLists(1, lists);
        s.fence = ++p.value;
        p.ring.Submitted(0, p.value);   // review S1: the ring knows slot 0 is busy (a failed wait must not let Draw reset it)
        Check(p.queue->Signal(p.fence.Get(), p.value), "warm signal");
        if (p.fence->GetCompletedValue() < p.value) {
            Check(p.fence->SetEventOnCompletion(p.value, p.event), "warm event");
            Require(WaitForSingleObject(p.event, 5000) == WAIT_OBJECT_0, "warm wait");
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    } catch (const std::exception&) { return -1; }
}

bool Compose::EffectsPending() const { return Ready() && impl->fxPending.load(std::memory_order_relaxed); }
bool Compose::FinishEffects(void (*say)(const char*)) {
    if (!EffectsPending()) return false;
    auto& p = *impl;
    p.fxPending.store(false, std::memory_order_relaxed);
    try {
        struct Library { HMODULE h = LoadLibraryW(L"d3dcompiler_47.dll"); ~Library() { if (h) FreeLibrary(h); } } library;
        const auto compile = library.h ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(library.h, "D3DCompile")) : nullptr;
        Require(compile != nullptr, "shader compiler unavailable");
        const auto t0 = std::chrono::steady_clock::now();
        ComPtr<ID3DBlob> ps, errors;
        const HRESULT hr = compile(p.source.data(), p.source.size(), "ring-wheel-compose", nullptr, nullptr, "PSFx", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors);
        if (FAILED(hr) && errors && say) say(static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr, "effects shader compilation failed");
        auto state = p.fxState;
        state.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        Check(p.device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&p.pipelineFxLate)), "effects pipeline creation failed");
        p.fxLive.store(p.pipelineFxLate.Get(), std::memory_order_release);   // the placeholder stays alive (in-flight lists)
        // Not warmed: the frame thread owns the slots now; a lazy driver compile lands on the first compose with effects.
        if (say) {
            char line[160];
            std::snprintf(line, sizeof line, "Frame ring wheel: effects shader compiled in %.0f ms on the worker (no offline blob); effects on",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            say(line);
        }
        return true;
    } catch (const std::exception& e) {
        if (say) say((std::string("Frame ring wheel: effects shader failed: ") + e.what() + "; the ring shows without effects").c_str());
        return false;
    }
}

bool Compose::Stage(const AtlasImage& image) {
    if (!StagingFree()) return false;
    const int k = int(image.kind);
    const int bpp = image.kind == AtlasKind::Icons ? 4 : image.kind == AtlasKind::FxTex ? 1 : 2, mips = impl->Mips(k);
    const int px = image.kind == AtlasKind::FxTex ? FxTexPx : AtlasTexPx;
    if (image.px != px || image.bpp != bpp || image.mips != mips || image.data.size() < image.MipOffset(mips)) return false;
    auto& p = *impl;
    for (int mip = 0; mip < mips; ++mip) {
        const auto& f = p.foot[k][mip];
        const size_t row = size_t(f.Footprint.Width) * size_t(bpp);
        const uint8_t* src = image.data.data() + image.MipOffset(mip);
        for (UINT y = 0; y < f.Footprint.Height; ++y) std::memcpy(p.stagingMem + f.Offset + size_t(y) * f.Footprint.RowPitch, src + size_t(y) * row, row);
    }
    p.pendingKind = image.kind;
    stage.store(1, std::memory_order_release);
    return true;
}

bool Compose::CanDraw() const {
    if (!Ready()) return false;
    auto& p = *impl;
    SlotRing probe = p.ring;   // Acquire without counting a skip
    return probe.Acquire(p.fence->GetCompletedValue()) >= 0;
}
size_t Compose::Pump() {
    if (!Ready()) return 0;
    auto& p = *impl;
    int st = stage.load(std::memory_order_acquire);
    if (st == 0) return 0;   // nothing staged: no fence read (perf R5 should-fix)
    const uint64_t done = p.fence->GetCompletedValue();
    if (st == 2 && p.up.done) {   // the last slice completed: the staging is the worker's again
        if (done >= p.up.fence) stage.store(0, std::memory_order_release);
        return 0;
    }
    if (st == 1) {
        p.up = {};
        p.up.kind = int(p.pendingKind);
        p.up.target = p.front[p.up.kind] < 0 ? 0 : 1 - p.front[p.up.kind];
        stage.store(2, std::memory_order_relaxed);
        st = 2;
    }
    if (st != 2) return 0;
    p.ReadGpu();
    const int slot = p.ring.Acquire(done);
    if (slot < 0) return 0;
    auto& s = p.slots[slot];
    if (!p.Begin(s, nullptr)) return 0;
    auto& u = p.up;
    ID3D12Resource* tex = p.tex[u.kind][u.target].Get();
    if (!p.copyDest[u.kind][u.target]) {   // the old front (draws before this on the queue have read it)
        const auto b = Transition(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        s.list->ResourceBarrier(1, &b);
        p.copyDest[u.kind][u.target] = true;
    }
    size_t copied = 0;
    const int mips = p.Mips(u.kind);
    while (!u.done) {
        const auto& f = p.foot[u.kind][u.mip];
        const size_t rowBytes = f.Footprint.RowPitch;
        UINT rows = UINT(std::min<size_t>(f.Footprint.Height - u.row, (SliceBytes - copied) / rowBytes));
        if (!rows) break;
        D3D12_TEXTURE_COPY_LOCATION to{tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}}; to.SubresourceIndex = UINT(u.mip);
        D3D12_TEXTURE_COPY_LOCATION from{p.staging.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}}; from.PlacedFootprint = f;
        if (u.kind == int(AtlasKind::Icons)) from.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        const D3D12_BOX box{0, u.row, 0, f.Footprint.Width, u.row + rows, 1};
        s.list->CopyTextureRegion(&to, 0, u.row, 0, &from, &box);
        copied += rows * rowBytes;
        u.row += rows;
        if (u.row >= f.Footprint.Height) { u.row = 0; if (++u.mip >= mips) u.done = true; }
    }
    if (u.done) {
        const auto b = Transition(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        s.list->ResourceBarrier(1, &b);
        p.copyDest[u.kind][u.target] = false;
    }
    if (!p.Submit(slot, false)) return 0;
    if (u.done) { u.fence = p.value; p.front[u.kind] = u.target; ++p.flips[u.kind]; }   // draws after this on the queue see it
    p.maxSlice = std::max(p.maxSlice, copied);
    return copied;
}

Compose::Result Compose::Draw(ID3D12Resource* target, const ContentRect& content, const Instance* list, int n) {
    if (!Ready() || !target || content.w <= 0 || content.h <= 0) return Result::NotReady;
    auto& p = *impl;
    const double t0 = Now();
    p.ReadGpu();
    const int slot = p.ring.Acquire(p.fence->GetCompletedValue());
    if (slot < 0) return Result::Busy;
    auto& s = p.slots[slot];
    n = std::clamp(n, 0, MaxDraw);
    if (n) std::memcpy(s.mapped, list, sizeof(Instance) * size_t(n));
    const D3D12_RESOURCE_DESC td = target->GetDesc();
    const LONG w = std::min<LONG>(content.w, LONG(td.Width)), h = std::min<LONG>(content.h, LONG(td.Height));
    if (!p.Begin(s, p.pipeline.Get())) return Result::NotReady;
    auto* cl = s.list.Get();
    D3D12_RENDER_TARGET_VIEW_DESC rd{}; rd.Format = p.format; rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p.rtv->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += size_t(slot) * p.rtvStride;
    p.device->CreateRenderTargetView(target, &rd, rtv);
    const bool timed = p.tickMs > 0 && !p.shotDraw;
    if (timed) cl->EndQuery(p.timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(slot * 2));
    const D3D12_RECT rect{0, 0, w, h};
    const float clear[4] = {0, 0, 0, 0};
    cl->ClearRenderTargetView(rtv, clear, 1, &rect);
    if (n) {
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT vp{0, 0, float(content.w), float(content.h), 0, 1};
        cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &rect);
        cl->SetGraphicsRootSignature(p.root.Get());
        ID3D12DescriptorHeap* heaps[] = {p.srv.Get()};
        cl->SetDescriptorHeaps(1, heaps);
        const float k[6] = {content.x0, content.y0, content.canvasW > 0 ? float(content.w) / content.canvasW : 1.f, p.linearOut ? 1.f : 0.f,
            float(content.w), float(content.h)};
        cl->SetGraphicsRoot32BitConstants(0, 6, k, 0);
        cl->SetGraphicsRootShaderResourceView(1, s.instances->GetGPUVirtualAddress());
        cl->SetGraphicsRootDescriptorTable(2, p.Table());
        cl->SetGraphicsRootDescriptorTable(3, p.FxTable());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        // One draw per run of instances on the same pixel shader (effects: PSFx), in the list's order (the layering).
        // SV_InstanceID starts at 0 in every draw, so the run's first instance goes in the root constant `base`.
        ID3D12PipelineState* fx = p.fxLive.load(std::memory_order_acquire);
        bool fxNow = list[0].recipe == RecipeFx;
        if (fxNow) cl->SetPipelineState(fx);
        for (int start = 0; start < n;) {
            int end = start + 1;
            while (end < n && (list[end].recipe == RecipeFx) == fxNow) ++end;
            cl->SetGraphicsRoot32BitConstant(0, UINT(start), 6);
            cl->DrawInstanced(4, UINT(end - start), 0, 0);
            if (end < n) { fxNow = !fxNow; cl->SetPipelineState(fxNow ? fx : p.pipeline.Get()); }
            start = end;
        }
    }
    if (timed) {
        cl->EndQuery(p.timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(slot * 2 + 1));
        cl->ResolveQueryData(p.timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(slot * 2), 2, p.readback.Get(), UINT64(slot) * 16);
    }
    if (!p.Submit(slot, timed)) return Result::NotReady;
    if (!p.shotDraw) p.cpu.Add(Now() - t0);
    return Result::Composed;
}

void ShotRows::Drop() { if (buffer) buffer->Release(); buffer = nullptr; }
bool ShotRows::Read(shot::Raw& out) {
    out = {};
    if (!buffer) return false;
    const size_t count = shot::Raw::Bytes(rowPitch, w, h);
    void* data = nullptr;
    const D3D12_RANGE read{SIZE_T(offset), SIZE_T(offset + count)};
    bool copied = count && offset + count <= bytes && SUCCEEDED(buffer->Map(0, &read, &data)) && data;
    if (copied) {
        const uint8_t* rows = static_cast<const uint8_t*>(data) + offset;
        try { out.rows.assign(rows, rows + count); } catch (...) { copied = false; }
        const D3D12_RANGE none{0, 0};
        buffer->Unmap(0, &none);
    }
    if (copied) { out.rowPitch = rowPitch; out.w = w; out.h = h; out.bgra = bgra; out.srgb = srgb; }
    else out = {};
    Drop();
    return copied;
}
const char* Compose::Shot(const ContentRect& content, const Instance* list, int n, unsigned waitMs, ShotRows& out) {
    out.Drop();
    if (!Ready() || content.w <= 0 || content.h <= 0) return "the compose pass is not ready";
    auto& p = *impl;
    const uint64_t done = p.fence->GetCompletedValue();
    p.parked.erase(std::remove_if(p.parked.begin(), p.parked.end(), [&](const Impl::ShotParked& s) { return s.at <= done; }), p.parked.end());
    if (!WaitIdle(waitMs)) return "the GPU is still drawing the wheel";   // every slot free: this Draw is never Busy, and no slot in flight is touched
    Impl::ShotParked s;
    D3D12_HEAP_PROPERTIES gpuHeap{D3D12_HEAP_TYPE_DEFAULT}, cpuHeap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = UINT64(content.w); d.Height = UINT(content.h); d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.Format = p.format; d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot{};
    UINT64 bytes = 0;
    p.device->GetCopyableFootprints(&d, 0, 1, 0, &foot, nullptr, nullptr, &bytes);
    D3D12_RESOURCE_DESC b{};
    b.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; b.Width = bytes; b.Height = 1; b.DepthOrArraySize = 1; b.MipLevels = 1;
    b.SampleDesc.Count = 1; b.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!bytes || FAILED(p.device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&s.target))) ||
        FAILED(p.device->CreateCommittedResource(&cpuHeap, D3D12_HEAP_FLAG_NONE, &b, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s.buffer))) ||
        FAILED(p.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.allocator))) ||
        FAILED(p.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.allocator.Get(), nullptr, IID_PPV_ARGS(&s.list)))) return "no readback target";
    p.shotDraw = true;
    const Result drawn = Draw(s.target.Get(), content, list, n);
    p.shotDraw = false;
    if (drawn != Result::Composed) { s.at = p.value; p.parked.push_back(std::move(s)); return "the draw failed"; }   // a failed Submit: the GPU may hold the target
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {s.target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    s.list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION to{s.buffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}}, from{s.target.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    to.PlacedFootprint = foot; from.SubresourceIndex = 0;
    s.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (FAILED(s.list->Close())) { s.at = p.value; p.parked.push_back(std::move(s)); return "the readback list failed"; }   // the Draw is already submitted: the GPU may hold the target
    ID3D12CommandList* lists[] = {s.list.Get()};
    p.queue->ExecuteCommandLists(1, lists);
    s.at = ++p.value;   // the Lab fence: Release and the Off edge wait for this copy too
    if (FAILED(p.queue->Signal(p.fence.Get(), s.at))) { p.parked.push_back(std::move(s)); return "the GPU was lost"; }
    if (p.fence->GetCompletedValue() < s.at) {
        const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event && SUCCEEDED(p.fence->SetEventOnCompletion(s.at, event))) WaitForSingleObject(event, waitMs);
        if (event) CloseHandle(event);
        if (p.fence->GetCompletedValue() < s.at) { p.parked.push_back(std::move(s)); return "the GPU did not finish"; }
    }
    // Job B: the GPU is done with the buffer, so it is the save's from here (the target, the allocator and the list go
    // now, as before). Its rows are read on the worker (ShotRows::Read): mapping and copying 7 MB is not the frame's work.
    out.buffer = s.buffer.Detach();
    out.offset = foot.Offset; out.bytes = bytes; out.rowPitch = foot.Footprint.RowPitch; out.w = content.w; out.h = content.h;
    out.bgra = p.format == DXGI_FORMAT_B8G8R8A8_UNORM || p.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    out.srgb = p.linearOut;
    return nullptr;
}
unsigned Compose::PreciseWaits() const { return impl ? impl->preciseWaits : 0; }
ID3D12Fence* Compose::MarkLast(uint64_t& value) const {
    value = 0;
    if (!Ready() || !impl->fence) return nullptr;
    value = impl->value;
    impl->fence->AddRef();
    return impl->fence.Get();
}
bool Compose::WaitIdle(unsigned waitMs) {
    if (!Ready()) return true;   // never drew: nothing in flight
    auto& p = *impl;
    if (p.fence->GetCompletedValue() >= p.value) return true;
    // The fence's own event. A wait the timer ends leaves it armed for this value; nothing is drawn after the Off edge, so
    // Release (the worker, next) waits for the same value and skips its wait once it is reached. Round 2026-11-08 (review W1):
    // a screenshot waits here during play, so an earlier wait's leftover (the event set, or armed for an older value) must not
    // end this one early: cleared first, and a wake before the fence reached the value waits on.
    if (!waitMs) return p.fence->GetCompletedValue() >= p.value;
    ResetEvent(p.event);
    if (FAILED(p.fence->SetEventOnCompletion(p.value, p.event))) return p.fence->GetCompletedValue() >= p.value;
    const HANDLE done = p.event;
    // A plain timeout rounds up to the system timer tick (measured 24 ms for 16 on WARP, up to about 31): a high-resolution
    // waitable timer (Windows 10 1803+) ends the wait on time without timeBeginPeriod (the game's timer is not ours to change).
    // No such timer: the plain timeout, rounded.
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due; due.QuadPart = -LONGLONG(waitMs) * 10000;   // relative, 100 ns units
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        ++p.preciseWaits;
        const HANDLE both[2] = {done, timer};
        while (WaitForMultipleObjects(2, both, FALSE, waitMs + 50) == WAIT_OBJECT_0 && p.fence->GetCompletedValue() < p.value) {}   // the timer ends it; the extra 50 ms only if the timer never fires
    } else {
        WaitForSingleObject(done, waitMs);
    }
    if (timer) CloseHandle(timer);
    return p.fence->GetCompletedValue() >= p.value;
}
bool Compose::Release(unsigned waitMs) {
    if (!impl) return true;
    auto& p = *impl;
    if (p.fence && p.fence->GetCompletedValue() < p.value) {
        if (SUCCEEDED(p.fence->SetEventOnCompletion(p.value, p.event))) WaitForSingleObject(p.event, waitMs);
        if (p.fence->GetCompletedValue() < p.value) return false;
    }
    ready.store(false, std::memory_order_release);
    stage.store(0, std::memory_order_relaxed);
    impl.reset();
    return true;
}
double NowMs() {
    static const double perMs = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart) / 1000.0; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / perMs;
}
}  // namespace framevr::ringwheel
