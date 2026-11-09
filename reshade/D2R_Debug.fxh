// D2R_Debug.fxh: the phantom lines, the bone frame and the shield debug drawing.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// vrcam [debug] phantom_ray: the crossbow's shot line (where the bolt goes and the left
// hand takes it), from the hand 1.5 m on, as a red line - the model must lie on it.
// The ends are relative to the eyes' middle, in the world; each eye projects them.
uniform bool PhantomOn < hidden = true; > = false;
uniform float3 PhantomA < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 PhantomB < hidden = true; > = float3(0.0, 0.0, 1.0);
// and the crossbow bone's own axes from its origin: X green, Y blue, Z yellow
uniform bool BoneOn < hidden = true; > = false;
uniform float3 BoneO < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 BoneX < hidden = true; > = float3(1.0, 0.0, 0.0);
uniform float3 BoneY < hidden = true; > = float3(0.0, 1.0, 0.0);
uniform float3 BoneZ < hidden = true; > = float3(0.0, 0.0, 1.0);
// Those points are relative to where the hands hang (between the eyes); this is each eye's
// camera from there, taken off before projecting - without it both eyes saw the lines from
// the middle, with no parallax, and they floated far behind the hand (2026-10-06).
uniform float3 PhantomEye0 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 PhantomEye1 < hidden = true; > = float3(0.0, 0.0, 0.0);

// Developer shield guard inspection. The CPU supplies the exact guard pose;
// amber denotes a fallback reference plane, cyan a measured item face.
uniform bool ShieldDebugOn < hidden = true; > = false;
uniform bool ShieldDebugHit < hidden = true; > = false;
uniform float3 ShieldDebugCenter < hidden = true; > = float3(0,0,0);
uniform float3 ShieldDebugRight < hidden = true; > = float3(0,0,0);
uniform float3 ShieldDebugUp < hidden = true; > = float3(0,0,0);
uniform float3 ShieldDebugNormal < hidden = true; > = float3(0,0,0);
uniform float3 ShieldDebugColor < hidden = true; > = float3(0,1,1);
uniform float3 ShieldDebugHitFrom < hidden = true; > = float3(0,0,0);
uniform float3 ShieldDebugHitTo < hidden = true; > = float3(0,0,0);

// A point relative to the eye -> its pixel; z < 0 behind the eye.
float3 PhantomPixel(float3 v, int eye)
{
    const float3 R = eye == 0 ? CamRight0 : CamRight1, U = eye == 0 ? CamUp0 : CamUp1, B = eye == 0 ? CamBack0 : CamBack1;
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    v -= eye == 0 ? PhantomEye0 : PhantomEye1;
    const float3 c = float3(dot(v, R), dot(v, U), dot(v, B));
    const float w = max(-c.z, 1e-4);
    const float2 ndc = float2(c.x / w * sp.x - sp.z, c.y / w * sp.y - sp.w);
    return float3((ndc.x * 0.5 + 0.5) * BUFFER_WIDTH, (0.5 - ndc.y * 0.5) * BUFFER_HEIGHT, -c.z);
}
// How far ahead of this eye a point is (world units; < 0 behind it).
float PhantomAhead(float3 v, int eye)
{
    v -= eye == 0 ? PhantomEye0 : PhantomEye1;
    return -dot(v, eye == 0 ? CamBack0 : CamBack1);
}
float3 Segment(float3 c, float2 p, float3 a3, float3 b3, int eye, float3 col)
{
    // A segment running past the eye is cut where it passes it, not dropped whole: a long
    // grab line along an axe went behind the camera and was never drawn (2026-10-06).
    const float near = 0.05, za = PhantomAhead(a3, eye), zb = PhantomAhead(b3, eye);
    if (za <= near && zb <= near) return c;
    if (za < near) a3 = lerp(a3, b3, (near - za) / (zb - za));
    if (zb < near) b3 = lerp(b3, a3, (near - zb) / (za - zb));
    const float3 a = PhantomPixel(a3, eye), b = PhantomPixel(b3, eye);
    if (a.z <= 0.01 || b.z <= 0.01) return c;
    const float2 ab = b.xy - a.xy;
    const float t = saturate(dot(p - a.xy, ab) / max(dot(ab, ab), 1e-4));
    const float d = length(p - (a.xy + ab * t));
    return lerp(c, col, 1.0 - smoothstep(1.0, 2.5, d));
}
float3 ShieldDebug(float3 c, float2 pixel, int eye)
{
    if (!ShieldDebugOn) return c;
    const float3 o = ShieldDebugCenter, r = ShieldDebugRight, u = ShieldDebugUp;
    const float3 n = ShieldDebugNormal, colour = ShieldDebugColor;
    c = Segment(c, pixel, o-r-u, o+r-u, eye, colour);
    c = Segment(c, pixel, o+r-u, o+r+u, eye, colour);
    c = Segment(c, pixel, o+r+u, o-r+u, eye, colour);
    c = Segment(c, pixel, o-r+u, o-r-u, eye, colour);
    c = Segment(c, pixel, o-r*0.15, o+r*0.15, eye, colour);
    c = Segment(c, pixel, o-u*0.15, o+u*0.15, eye, colour);
    // Both face directions participate in coverage, so show both normals.
    c = Segment(c, pixel, o-n, o+n, eye, colour);
    c = Segment(c, pixel, o+n, o+n*0.75+r*0.15, eye, colour);
    c = Segment(c, pixel, o+n, o+n*0.75-r*0.15, eye, colour);
    // Native combat gives a horizontal attacker direction, not an impact point.
    if (ShieldDebugHit) c = Segment(c, pixel, ShieldDebugHitFrom, ShieldDebugHitTo, eye, colour);
    return c;
}
float3 Phantom(float4 pos, float2 uv, int eye)
{
    float3 c = tex2D(FogColorSmp, uv).rgb;
    if (PhantomOn) c = Segment(c, pos.xy, PhantomA, PhantomB, eye, float3(1.0, 0.05, 0.05));
    if (BoneOn) {
        c = Segment(c, pos.xy, BoneO, BoneX, eye, float3(0.1, 1.0, 0.1));
        c = Segment(c, pos.xy, BoneO, BoneY, eye, float3(0.2, 0.4, 1.0));
        c = Segment(c, pos.xy, BoneO, BoneZ, eye, float3(1.0, 0.9, 0.1));
    }
    return ShieldDebug(c, pos.xy, eye);
}
float3 PS_Phantom(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Phantom(pos, uv, 0); }
float3 PS_PhantomR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Phantom(pos, uv, 1); }

