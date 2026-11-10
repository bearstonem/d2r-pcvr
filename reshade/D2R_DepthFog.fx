// D2R_DepthFog - distance fog and a sky for Diablo II: Resurrected under the
// vrcam plugin.
//
// Fog hides the edge where the loaded rooms end, so the render distance can
// stay modest. vrcam draws with an infinite reverse-Z projection, so the raw
// depth is d = near / z: distance = NearPlane / d, in the game's world units
// (the hero is about 7.5 tall).
//
// The sky fills the black void beyond the world: pixels with no depth at all
// that are (nearly) black. Its colour depends only on the direction the pixel
// looks in, so in stereo it sits exactly at infinity. vrcam hands over the
// camera's axes and projection per eye, and the palette of the area's biome;
// it switches the sky off underground, in caves and while a panel is open.
// With the sky on, the fog fades into the sky's horizon instead of FogColor.
//
// vrcam joins ReShade as an add-on and sets every uniform each frame from
// d2r_pcvr.ini [fog] and [sky] - the settings window's sliders. It also switches
// the technique on and off. Values typed into ReShade's own UI are overwritten
// while vrcam is running.
//
// Self-contained on purpose: FlatVR's ReShade install skips the effect
// packages, so ReShade.fxh is usually not there.
//
// The HUD is drawn over the same depth, so HUD pieces sitting over far scenery
// get fogged too. Bottom-corner orbs sit over the near ground and stay clear.
// Over the sky, only dark HUD pixels are at risk: bright ones are left alone.

uniform float NearPlane <
    hidden = true;
> = 1.5;

uniform bool FogOn <
    hidden = true;
> = true;

uniform float FogStart <
    ui_type = "slider"; ui_min = 0.0; ui_max = 2000.0; ui_step = 1.0;
    ui_label = "Fog start"; ui_tooltip = "World distance where the fog begins (set by vrcam).";
> = 150.0;

uniform float FogEnd <
    ui_type = "slider"; ui_min = 1.0; ui_max = 4000.0; ui_step = 1.0;
    ui_label = "Fog end"; ui_tooltip = "World distance where the fog is complete (set by vrcam).";
> = 600.0;

uniform float FogCurve <
    ui_type = "slider"; ui_min = 0.3; ui_max = 4.0; ui_step = 0.05;
    ui_label = "Fog curve"; ui_tooltip = "1 = linear, above 1 keeps the middle distance clearer.";
> = 1.6;

uniform float FogBlur <
    ui_type = "slider"; ui_min = 0.0; ui_max = 12.0;
    ui_label = "Fog depth blur (px)";
    ui_tooltip = "The fog is averaged over a 3x3 grid this many pixels apart: single holes and specks in the depth melt away. 0 = per pixel.";
> = 3.0;

uniform float3 FogColor <
    ui_type = "color"; ui_label = "Fog colour"; ui_tooltip = "Set by vrcam per act ([fog] color_*); without one the sky's horizon is used.";
> = float3(0.02, 0.02, 0.025);

uniform bool FogFixed < hidden = true; > = false;   // vrcam: the act has its own fog colour, the sky does not tint it
// vrcam: how much of each act's night picture shows, 0 by day .. 1 at full
// night - the game's own light (D2R_SkyNight_<act>.png, tools/gen_night_sky.py).
uniform float SkyNightMix < hidden = true; > = 0.0;
// vrcam, [sky] drift: the painted sky turns slowly round the viewer (1) or stands still (0).
// Off by default: the stars painted into a night picture went round with its clouds.
uniform float SkyDrift < hidden = true; > = 0.0;

uniform bool UpsideDown <
    ui_label = "Depth upside down"; ui_tooltip = "Tick if the fog sits on the wrong half of the picture.";
> = false;

uniform bool ShowDistance <
    ui_label = "Show distance (tuning)"; ui_tooltip = "Grey ramp of the fog amount instead of the picture.";
> = false;
uniform float CeilFineDetail <
    ui_type = "slider"; ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
    ui_label = "Ceiling fine detail";
    ui_tooltip = "A finer layer of the ceiling's own stone picture over it, near the eye - 0 = none.";
> = 0.7;
uniform bool ShowCeilingCut <
    ui_label = "Show ceiling cut (tuning)";
    ui_tooltip = "Under a cave ceiling: what the game drew, by its height over the hero's floor -\n"
                 "blue low to green at the ceiling, red above it (cut), a white line every 5 units.";
> = false;

texture2D FogColorTex : COLOR;
// Private depth channel adapted from natpoh/diablo2-vr-port 4b6284c (MIT, BodyWalkVR).
texture2D FogDepthTex : D2RVR_DEPTH;
texture2D FogDepthRsTex : DEPTH;
sampler2D FogColorSmp { Texture = FogColorTex; };
sampler2D FogDepthSmp { Texture = FogDepthTex; MinFilter = POINT; MagFilter = POINT; };
sampler2D FogDepthRsSmp { Texture = FogDepthRsTex; MinFilter = POINT; MagFilter = POINT; };
uniform bool OwnDepth < hidden = true; > = false;
// The part of the depth buffer the game draws the scene into (vrcam, from the game's
// viewport): 1 without an upscaler; DLSS draws at 0.5 - 0.67 of the screen into the top
// left of a screen-size buffer, so every read of the depth goes through DepthAt (2026-10-08).
uniform float2 DepthScale < hidden = true; > = float2(1.0, 1.0);
float4 DepthAt(float2 duv) {
    const float4 c = float4(duv * DepthScale, 0, 0);
    return OwnDepth ? tex2Dlod(FogDepthSmp, c) : tex2Dlod(FogDepthRsSmp, c);
}
// under 16: no depth bound
int DepthWidth() { return OwnDepth ? tex2Dsize(FogDepthSmp, 0).x : tex2Dsize(FogDepthRsSmp, 0).x; }
float Hash(float2 p)
{
    // Dave Hoskins' hash12: no diagonal banding on integer cells
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1.0, 0.0)), f.x),
                lerp(Hash(i + float2(0.0, 1.0)), Hash(i + float2(1.0, 1.0)), f.x), f.y);
}

float Fbm(float2 p)
{
    float v = 0.0, a = 0.5;
    [unroll] for (int i = 0; i < 5; ++i) { v += a * Noise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return v;
}

// The game's interface left over this frame (labels over monsters, names, chat), as vrcam
// copies it out of the game's interface layer: premultiplied colour, alpha = how much of the
// world shows through. From above it is taken out and drawn back on the labels' plane
// (LabelsOn); otherwise it stays in the picture and only tells the fog where not to lie
// (UiMaskOn): labels over the void were fogged with the particles (2026-10-04).
texture2D GameLayerTex : D2R_GAME_LAYER;
sampler2D GameLayerSmp { Texture = GameLayerTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
uniform bool UiMaskOn < hidden = true; > = false;
uniform bool UiMaskOff <
    ui_label = "Ignore the interface mask (test)";
    ui_tooltip = "Fog and sky over everything, the game's interface layer not looked at - to see whether a straight line "
                 "across the picture comes from it (act 2 town, 2026-10-07).";
> = false;


// The feature headers: each one is a block of this effect moved out of this file unchanged (the visible settings, the inputs and shared helpers above, and
// the vertex shader and techniques at the end stay here). They are listed in an order where every name is defined before it is used.
#include "D2R_Sky.fxh"
#include "D2R_Fog.fxh"
#include "D2R_CeilingBase.fxh"
#include "D2R_Lights.fxh"
#include "D2R_Ceiling.fxh"
#include "D2R_Hud.fxh"
#include "D2R_FogWorld.fxh"
#include "D2R_Debug.fxh"
#include "D2R_Targeting.fxh"
#include "D2R_Table.fxh"
#include "D2R_Immunity.fxh"
#include "D2R_TeleportArc.fxh"


void VS_Fullscreen(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}


// Three sizes of the one effect, each for the left eye's frames (and mono) and for the right eye's. vrcam keeps exactly
// one of the six on: before each present, the one of the eye that present is for, at the smallest size that draws
// everything wanted in it (steamvr/effect_passes.h; its test pins these lists).
//
//   D2R_DepthFog_Lean  the world's passes only: fog, sky, ceiling, lights, outlines, circles, beams, immunity, the arc.
//   D2R_DepthFog_Mask  those and the four that find the interface in the picture (HudCur/HudPrev/HudMask/HudMaskOld).
//                      The mask is read by the fog pass where the sky or the shader's ceiling fills the void
//                      (D2R_FogWorld.fxh), by the table key and by the HUD shift - nothing else.
//   D2R_DepthFog       every pass, as the effect always ran: with the HUD shift ([stereo] ui_near), the toolbar / map /
//                      labels drawn back (PS_GameHud), the debug lines (PS_Phantom), the table key and the frame stamp.
//                      Each of those five passes writes the picture back unchanged while its feature is off, which is
//                      why the smaller sizes can leave them out.
//
// The passes of a smaller size are the full one's with those left out, in the same order. The full techniques keep
// their names: an older plugin, and ReShade's preset, know only them.
technique D2R_DepthFog <
    ui_tooltip = "Distance fog and sky for D2R with the vrcam plugin (reads raw reverse-Z depth). Left eye / mono. Every pass.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudCopy; RenderTarget = HudCurTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMask; RenderTarget = HudMaskTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFog; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCands; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeights<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFog; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Outline; }
    pass { VertexShader = VS_Immunity; PixelShader = PS_Immunity; VertexCount = 2592; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_TeleportArc; PixelShader = PS_TeleportArc; VertexCount = 90; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Hud; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_GameHud; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Phantom; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TableKey; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Stamp; }
}

technique D2R_DepthFog_Mask <
    ui_tooltip = "The same without the passes of the HUD shift, the toolbar / map / labels, the debug lines, the table key and the frame stamp. Left eye / mono.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudCopy; RenderTarget = HudCurTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMask; RenderTarget = HudMaskTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFog; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCands; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeights<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFog; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Outline; }
    pass { VertexShader = VS_Immunity; PixelShader = PS_Immunity; VertexCount = 2592; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_TeleportArc; PixelShader = PS_TeleportArc; VertexCount = 90; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
}

technique D2R_DepthFog_Lean <
    ui_tooltip = "The same without those and without the interface mask: the world's passes only. Left eye / mono.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFog; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCands; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeights<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFog; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Outline; }
    pass { VertexShader = VS_Immunity; PixelShader = PS_Immunity; VertexCount = 2592; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_TeleportArc; PixelShader = PS_TeleportArc; VertexCount = 90; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
}

technique D2R_DepthFog_R <
    ui_tooltip = "The same for the right eye's frames in AFR stereo; vrcam switches between the two.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudCopy; RenderTarget = HudCurTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMask; RenderTarget = HudMaskTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogR; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCandsR; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeightsR<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFogR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_OutlineR; }
    pass { VertexShader = VS_ImmunityR; PixelShader = PS_Immunity; VertexCount = 2592; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_TeleportArcR; PixelShader = PS_TeleportArc; VertexCount = 90; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_GameHudR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_PhantomR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TableKeyR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_StampR; }
}

technique D2R_DepthFog_Mask_R <
    ui_tooltip = "D2R_DepthFog_Mask for the right eye's frames.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudCopy; RenderTarget = HudCurTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMask; RenderTarget = HudMaskTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogR; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCandsR; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeightsR<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFogR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_OutlineR; }
    pass { VertexShader = VS_ImmunityR; PixelShader = PS_Immunity; VertexCount = 2592; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_TeleportArcR; PixelShader = PS_TeleportArc; VertexCount = 90; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
}

technique D2R_DepthFog_Lean_R <
    ui_tooltip = "D2R_DepthFog_Lean for the right eye's frames.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogR; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCandsR; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeightsR<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFogR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_OutlineR; }
    pass { VertexShader = VS_ImmunityR; PixelShader = PS_Immunity; VertexCount = 2592; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
    pass { VertexShader = VS_TeleportArcR; PixelShader = PS_TeleportArc; VertexCount = 90; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; ColorWriteMask = 7; }
}
