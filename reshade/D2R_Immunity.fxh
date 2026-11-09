// D2R_Immunity.fxh: the monster immunity sprites (#421).
// A feature header of D2R_DepthFog.fx, #included by it after the others, so every name it uses (the eye
// positions and camera axes, the depth and the game layer) is defined before it.

// Monster immunity sprites. The artwork is Feral Rage's own gritty glow,
// prepared locally from the player's installation. Motion and tint are ours:
// never apply Feral Rage's gameplay state or recolor the player's skill.
// BEGIN MONSTER IMMUNITY SHADER (also compiled by the standalone shader test)
#ifndef D2R_IMMUNITY_TEXTURE
 #define D2R_IMMUNITY_TEXTURE 0
#endif
#if D2R_IMMUNITY_TEXTURE
texture2D ImmunityGlowTex < source = "PCVR_Immunity/feral_glow.png"; > { Width = 256; Height = 256; Format = RGBA8; };
sampler2D ImmunityGlowSmp { Texture = ImmunityGlowTex; AddressU = CLAMP; AddressV = CLAMP; };
#endif
uniform bool ImmunityOn < hidden = true; > = false;
uniform float4 ImmunityPos[24] < hidden = true; >;   // hero-relative foot xyz, six-bit mask
uniform float4 ImmunityPhase[24] < hidden = true; >; // stable unit phase in x
uniform float4 ImmunityShape < hidden = true; > = float4(3,3,.8,.8); // radius, height, sprite size, strength
struct ImmunityVertex {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float depth : TEXCOORD2;
};
ImmunityVertex ImmunitySprite(uint vertex, int eye)
{
    ImmunityVertex o;
    o.position = float4(-2,-2,0,1); o.uv = 0; o.color = 0; o.depth = 0;
    const uint sprite = vertex / 6, monster = sprite / 18, type = (sprite / 3) % 6, trail = sprite % 3;
    if (!ImmunityOn || monster >= 24) return o;
    const float4 m = ImmunityPos[monster];
    if ((uint(m.w) & (1u << type)) == 0u) return o;
    const float2 corners[6] = {float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
    const float3 colors[6] = {float3(1,1,1),float3(.72,.22,1),float3(1,.06,.025),float3(1,.85,.04),float3(.08,.42,1),float3(.06,1,.12)};
    const float angle = Timer * .0016 + ImmunityPhase[monster].x + type * 1.04719755 - trail * .13;
    const float3 p = m.xyz + float3(cos(angle)*ImmunityShape.x,
        ImmunityShape.y * (1 + .18*sin(angle*2 + type)), sin(angle)*ImmunityShape.x);
    const float3 v = p - (eye == 0 ? OutlineEye0 : OutlineEye1);
    const float3 R = eye == 0 ? CamRight0 : CamRight1, U = eye == 0 ? CamUp0 : CamUp1, B = eye == 0 ? CamBack0 : CamBack1;
    const float4 projection = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 camera = float3(dot(v,R),dot(v,U),dot(v,B));
    const float ahead = -camera.z;
    if (ahead <= NearPlane) return o;
    const float2 corner = corners[vertex % 6];
    const float size = ImmunityShape.z * (1 - trail * .18);
    const float2 clip = camera.xy * projection.xy - projection.zw * ahead + corner * size * projection.xy;
    o.position = float4(clip,0,ahead);
    o.uv = corner * float2(.5,-.5) + .5;
    o.color = float4(colors[type], ImmunityShape.w * (trail == 0 ? 1 : trail == 1 ? .38 : .14));
    o.depth = ahead;
    return o;
}
ImmunityVertex VS_Immunity(uint vertex : SV_VertexID) { return ImmunitySprite(vertex,0); }
ImmunityVertex VS_ImmunityR(uint vertex : SV_VertexID) { return ImmunitySprite(vertex,1); }
float4 PS_Immunity(ImmunityVertex i) : SV_Target
{
    if (!ImmunityOn || DepthWidth() < 16) return 0;
    const float2 uv = i.position.xy * float2(BUFFER_RCP_WIDTH,BUFFER_RCP_HEIGHT);
    const float d = DepthAt(UpsideDown ? float2(uv.x,1-uv.y) : uv).x;
    const float scene = d > 0 ? NearPlane / max(d,1e-6) : 1e7;
    float fade = saturate((scene-i.depth) / max(ImmunityShape.z*.25,1e-3));
    if (UiMaskOn && !UiMaskOff) fade *= saturate(tex2Dlod(GameLayerSmp,float4(uv,0,0)).a);
    const float2 q = i.uv * 2 - 1;
    // Fade the card edge even with a texture: no square bounds on a bright background.
    float shape = pow(saturate(1-dot(q,q)),2);
#if D2R_IMMUNITY_TEXTURE
    shape *= tex2Dlod(ImmunityGlowSmp,float4(i.uv,0,0)).r;
#endif
    return float4(i.color.rgb * (shape * fade * i.color.a),0);
}
// END MONSTER IMMUNITY SHADER
