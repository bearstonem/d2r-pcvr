// D2R_Hud.fxh: the HUD kept nearer in AFR stereo, the game bar, map and labels redrawn, GameHud.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.


// The HUD nearer in AFR stereo (vrcam [stereo] ui_near). The game draws its
// HUD without a camera of its own, so it is found in the picture: in the left
// and the right eye's frames it sits on the same pixels, while the world moves
// by the eyes' parallax. Pixels equal to the previous frame (the other eye)
// in the lower part of the screen are taken for the HUD and moved apart per
// eye. Flat areas of the ground that match too move along - unseen, being flat.
uniform float HudShift < hidden = true; > = 0.0;   // uv: the left eye's HUD moves this much right, the right eye's left
uniform float HudTop < hidden = true; > = 0.70;    // the HUD is looked for below this height (uv)
uniform float HudTol < hidden = true; > = 0.02;    // colour difference still counted as "the same"
texture2D HudCurTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
texture2D HudPrevTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
sampler2D HudCurSmp { Texture = HudCurTex; MinFilter = POINT; MagFilter = POINT; };
sampler2D HudPrevSmp { Texture = HudPrevTex; MinFilter = POINT; MagFilter = POINT; };
// Where the HUD was found, held for a few frames: an animated orb or a pixel
// the eyes happen to differ on left the HUD for one frame and it flickered.
texture2D HudMaskTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
texture2D HudMaskOldTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
sampler2D HudMaskSmp { Texture = HudMaskTex; MinFilter = POINT; MagFilter = POINT; };
sampler2D HudMaskOldSmp { Texture = HudMaskOldTex; MinFilter = POINT; MagFilter = POINT; };


bool SameInBothEyes(float2 uv)
{
    if (uv.y < HudTop || uv.x < 0.0 || uv.x > 1.0) return false;
    // the pixel and the ones above and below: one equal pixel is noise
    // (written out: as a loop the compiler would not unroll it, and the whole effect failed)
    const float2 px = float2(0.0, 1.0 / BUFFER_HEIGHT);
    const float3 d0 = abs(tex2Dlod(HudCurSmp, float4(uv - px, 0, 0)).rgb - tex2Dlod(HudPrevSmp, float4(uv - px, 0, 0)).rgb);
    const float3 d1 = abs(tex2Dlod(HudCurSmp, float4(uv, 0, 0)).rgb - tex2Dlod(HudPrevSmp, float4(uv, 0, 0)).rgb);
    const float3 d2 = abs(tex2Dlod(HudCurSmp, float4(uv + px, 0, 0)).rgb - tex2Dlod(HudPrevSmp, float4(uv + px, 0, 0)).rgb);
    const float3 d = max(d0, max(d1, d2));
    return max(d.r, max(d.g, d.b)) <= HudTol;
}

// 1 where the frames agree now, else what it was, fading over three frames.
float PS_HudMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (SameInBothEyes(uv)) return 1.0;
    return max(tex2D(HudMaskOldSmp, uv).r - 0.34, 0.0);
}
float PS_HudMaskStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(HudMaskSmp, uv).r; }

bool IsHud(float2 uv)
{
    return uv.y >= HudTop && uv.x >= 0.0 && uv.x <= 1.0 && tex2Dlod(HudMaskSmp, float4(uv, 0, 0)).r > 0.0;
}

// The frame as the game drew it, before the fog: what the next frame compares with.
float4 PS_HudCopy(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(FogColorSmp, uv); }
float4 PS_HudStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(HudCurSmp, uv); }

// HUD pixels come from where the HUD was; where it left, the world beside it fills in.
float3 Hud(float2 uv, float shift)
{
    const float3 c = tex2D(FogColorSmp, uv).rgb;
    if (shift == 0.0 || uv.y < HudTop) return c;
    const float2 src = uv - float2(shift, 0.0);
    if (IsHud(src) || IsHud(uv)) return tex2D(FogColorSmp, src).rgb;
    return c;
}
float3 PS_Hud(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Hud(uv, HudShift); }
float3 PS_HudR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Hud(uv, -HudShift); }

// The game's toolbar and corner map drawn back at a size of their own, from
// above and from behind (D2R VR Settings > Interface: toolbar / map size in the
// picture). vrcam takes the piece out of the game's interface layer and binds
// its copy here: premultiplied colour, alpha = how much of the world shows
// through, the way the game's own last pass lays the layer over the world.
// After the fog, so neither gets fogged; moved apart per eye like the HUD.
uniform bool GameBarOn < hidden = true; > = false;
uniform float4 GameBarBox < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);   // uv: left, top, right, bottom
uniform float GameBarShift < hidden = true; > = 0.0;   // uv, the toolbar alone: + nearer, - farther (added to HudShift)
uniform bool GameMapOn < hidden = true; > = false;
uniform float4 GameMapBox < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float GameMapShift < hidden = true; > = 0.0;   // uv, the map alone: + nearer, - farther (F3 [hud_floor] map_near)
texture2D GameBarTex : D2R_GAME_BAR;
texture2D GameMapTex : D2R_GAME_MAP;
sampler2D GameBarSmp { Texture = GameBarTex; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D GameMapSmp { Texture = GameMapTex; AddressU = CLAMP; AddressV = CLAMP; };

// From above in stereo: the rest of the interface (labels over monsters, the
// target's name, chat) taken out whole and laid on a plane tilted like the
// ground, so a label near the bottom of the screen stands as near as its
// monster. Per eye the row y moves by LabelBase + LabelTilt y (uv; the top row
// stays where the game puts it, the bottom moves the most;
// left eye right = nearer). The toolbar's strip and the map's box (LabelKeep0/1,
// where they are in the game's picture) stay flat and move like the HUD.
// On the floor (VR F3, vrcam [hud_floor] labels_alpha) the same layer comes out
// where the game puts it (no base, no tilt) only to be faded: LabelAlpha - only
// while vrcam cannot fade the label boxes through the game's own code.
uniform bool LabelsOn < hidden = true; > = false;
uniform float LabelBase < hidden = true; > = 0.0;
uniform float LabelTilt < hidden = true; > = 0.0;
uniform float LabelAlpha < hidden = true; > = 1.0;
uniform float4 LabelKeep0 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 LabelKeep1 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
// vrcam [hud] monster_alpha: the name plate over the target monster (the game's
// MonsterHealth panel, uv box) faded as a whole - box, frame and name.
uniform float4 PlateBox < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float PlateAlpha < hidden = true; > = 1.0;

bool InBox(float2 uv, float4 box) { return uv.x >= box.x && uv.y >= box.y && uv.x < box.z && uv.y < box.w; }
bool Kept(float2 uv) { return InBox(uv, LabelKeep0) || InBox(uv, LabelKeep1); }

// A label's pixel faded by LabelAlpha: its cover (1 - a) is cut, the more the darker
// the colour under it - the boxes ({0,0,0,0.6} in the game) fade, the names keep
// their cover and colour. The colour, premultiplied, stays: the box adds none.
float4 FadedLabel(float4 l)
{
    const float cover = 1.0 - saturate(l.a);
    const float lit = max(l.r, max(l.g, l.b)) / max(cover, 1e-3);   // the colour under the cover
    const float k = lerp(LabelAlpha, 1.0, smoothstep(0.1, 0.3, lit));
    return float4(l.rgb, 1.0 - cover * k);
}

float4 GameLabels(float2 uv, float eyeSign)
{
    const float2 flat = uv - float2(eyeSign * HudShift, 0.0);
    if (Kept(flat)) return tex2Dlod(GameLayerSmp, float4(flat, 0, 0));
    const float2 src = uv - float2(eyeSign * (LabelBase + LabelTilt * uv.y), 0.0);
    if (src.x < 0.0 || src.x > 1.0 || Kept(src)) return float4(0.0, 0.0, 0.0, 1.0);
    float4 l = tex2Dlod(GameLayerSmp, float4(src, 0, 0));
    if (LabelAlpha < 0.999) l = FadedLabel(l);
    if (PlateAlpha < 0.999 && InBox(src, PlateBox)) l = float4(l.rgb * PlateAlpha, 1.0 - (1.0 - saturate(l.a)) * PlateAlpha);
    return l;
}

// The interface's own bright pixels (names, numbers, icons) in this frame's layer -
// out (LabelsOn) or left in the picture (UiMaskOn) - for the floor's key (TableKey),
// which must never take them for the void. A box alone is dark: not one of them.
bool InterfaceLit(float2 uv, float eyeSign)
{
    if (!LabelsOn && !UiMaskOn) return false;
    float4 l = tex2Dlod(GameLayerSmp, float4(uv, 0, 0));
    if (LabelsOn) l = GameLabels(uv, eyeSign);
    return l.a < 0.5 && max(l.r, max(l.g, l.b)) > 0.04;
}

float4 GamePiece(sampler2D s, float4 box, float2 uv)
{
    const float2 t = (uv - box.xy) / (box.zw - box.xy);
    if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0) return float4(0.0, 0.0, 0.0, 1.0);
    return tex2Dlod(s, float4(t, 0, 0));
}

// The toolbar and the map drawn back, as one premultiplied layer (alpha = the world through).
// GameBarBox may be anywhere on the screen: [hud_*] bar_x / bar_y move it.
float4 GamePieces(float2 uv, float eyeSign)
{
    const float2 src = uv - float2(eyeSign * HudShift, 0.0);
    float4 p = float4(0.0, 0.0, 0.0, 1.0);
    [branch] if (GameBarOn) p = GamePiece(GameBarSmp, GameBarBox, src - float2(eyeSign * GameBarShift, 0.0));
    [branch] if (GameMapOn)
    {
        const float4 m = GamePiece(GameMapSmp, GameMapBox, src - float2(eyeSign * GameMapShift, 0.0));   // the map over the toolbar where they meet
        p = float4(m.rgb + p.rgb * m.a, p.a * m.a);
    }
    return p;
}

// eyeSign: +1 the left eye (moves right = nearer), -1 the right one
float3 GameHud(float2 uv, float eyeSign)
{
    float3 c = tex2D(FogColorSmp, uv).rgb;
    [branch] if (LabelsOn)
    {
        const float4 l = GameLabels(uv, eyeSign);
        c = c * saturate(l.a) + l.rgb;
    }
    const float4 p = GamePieces(uv, eyeSign);
    return c * saturate(p.a) + p.rgb;
}
float3 PS_GameHud(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return GameHud(uv, 1.0); }
float3 PS_GameHudR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return GameHud(uv, -1.0); }

