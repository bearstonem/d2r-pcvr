// D2R_TeleportArc.fxh: Teleport's arc from the right hand to its sigil (teleport_arc.h; owner, 2026-11-09).
// A feature header of D2R_DepthFog.fx, #included by it after the others, so every name it uses (the eye
// positions and camera axes, the depth and the game layer) is defined before it.

// vrcam: ArcPts xyz each point relative to the hero in render units (from the hand, 0, to the landing, 15),
// w how far along (0..1); ArcShape x the ribbon's half-width in render units, y its strength.
uniform bool ArcOn < hidden = true; > = false;
uniform float4 ArcPts[16] < hidden = true; >;
uniform float4 ArcShape < hidden = true; > = float4(1, 1, 0, 0);
struct ArcVertex {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;      // x along the segment, y across the ribbon (-1..1)
    float along : TEXCOORD1;    // 0 at the hand, 1 at the landing
    float depth : TEXCOORD2;
};
// Six vertices a segment, 15 segments: each a quad between two points, turned to face this eye.
ArcVertex ArcRibbon(uint vertex, int eye)
{
    ArcVertex o;
    o.position = float4(-2, -2, 0, 1); o.uv = 0; o.along = 0; o.depth = 0;
    const uint segment = vertex / 6;
    if (!ArcOn || segment >= 15) return o;
    const float2 corners[6] = {float2(0,-1), float2(0,1), float2(1,1), float2(0,-1), float2(1,1), float2(1,-1)};
    const float2 corner = corners[vertex % 6];
    const float4 a = ArcPts[segment], b = ArcPts[segment + 1];
    const float3 eyeAt = eye == 0 ? OutlineEye0 : OutlineEye1;
    float3 p = lerp(a.xyz, b.xyz, corner.x);
    const float3 along = b.xyz - a.xyz;
    const float3 side = cross(along, eyeAt - p);
    const float sideLength = length(side);
    if (sideLength < 1e-5) return o;
    p += side / sideLength * ArcShape.x * corner.y;
    const float3 v = p - eyeAt;
    const float3 R = eye == 0 ? CamRight0 : CamRight1, U = eye == 0 ? CamUp0 : CamUp1, B = eye == 0 ? CamBack0 : CamBack1;
    const float4 projection = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 camera = float3(dot(v, R), dot(v, U), dot(v, B));
    const float ahead = -camera.z;
    if (ahead <= NearPlane) return o;
    o.position = float4(camera.xy * projection.xy - projection.zw * ahead, 0, ahead);
    o.uv = corner;
    o.along = lerp(a.w, b.w, corner.x);
    o.depth = ahead;
    return o;
}
ArcVertex VS_TeleportArc(uint vertex : SV_VertexID) { return ArcRibbon(vertex, 0); }
ArcVertex VS_TeleportArcR(uint vertex : SV_VertexID) { return ArcRibbon(vertex, 1); }
// Teleport's arcane violet (the sigil's, vrcam kElement[5]): a soft core with a glow, a pulse running toward the
// landing, faded in from the hand; behind a wall or a monster a third as bright, so the way still shows.
float4 PS_TeleportArc(ArcVertex i) : SV_Target
{
    if (!ArcOn || DepthWidth() < 16) return 0;
    const float2 uv = i.position.xy * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    const float d = DepthAt(UpsideDown ? float2(uv.x, 1 - uv.y) : uv).x;
    const float scene = d > 0 ? NearPlane / max(d, 1e-6) : 1e7;
    const float seen = scene + ArcShape.x * 4 >= i.depth ? 1.0 : 0.33;
    float shade = smoothstep(0.0, 0.12, i.along) * seen * ArcShape.y;
    if (UiMaskOn && !UiMaskOff) shade *= saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    const float across = abs(i.uv.y);
    const float core = pow(saturate(1 - across), 3), glow = pow(saturate(1 - across), 1.2) * 0.35;
    const float pulse = 0.75 + 0.25 * sin(i.along * 40.0 - Timer * 0.006);
    return float4(float3(0.62, 0.55, 1.0) * (core + glow) * pulse * shade, 0);
}
