// D2R_Table.fxh: the frame stamp and the table key.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

// The frame's stamp for FlatVR: when vrcam built this frame's view, and so
// which head pose it is drawn for. 48 black/white pixels at the right end of
// the bottom row - 8 magic bits (0xB2), the 32-bit stamp, 8 check bits (the
// stamp's bytes xor'ed) - which FlatVR reads from the very texture it shows
// and paints over. A stamp in shared memory ran ahead of the GPU copy of the
// picture by a frame or two, so the screen sat at the wrong head pose.
uniform bool StampOn < hidden = true; > = false;
uniform uint FrameStamp0 < hidden = true; > = 0;   // the left eye's, and the right eye's
uniform uint FrameStamp1 < hidden = true; > = 0;

float3 Stamp(float4 pos, float2 uv, uint stamp)
{
    const float3 c = tex2D(FogColorSmp, uv).rgb;
    const int x = int(pos.x), y = int(pos.y);
    const int i = x - (BUFFER_WIDTH - 48);
    if (!StampOn || y != BUFFER_HEIGHT - 1 || i < 0) return c;
    const uint sum = (stamp ^ (stamp >> 8) ^ (stamp >> 16) ^ (stamp >> 24)) & 0xFFu;
    uint bit;
    if (i < 8) bit = (0xB2u >> uint(i)) & 1u;
    else if (i < 40) bit = (stamp >> uint(i - 8)) & 1u;
    else bit = (sum >> uint(i - 40)) & 1u;
    return bit != 0u ? float3(1.0, 1.0, 1.0) : float3(0.0, 0.0, 0.0);
}
float3 PS_Stamp(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Stamp(pos, uv, FrameStamp0); }
float3 PS_StampR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Stamp(pos, uv, FrameStamp1); }

// The stamp alone, for the Lean and Mask sizes of the effect (steamvr/effect_passes.h): a small triangle over the
// bottom right corner in place of a pass over the whole picture, so the stamp - on by default in AFR stereo - no
// longer needs the Full size. It reads nothing and writes only the 48 stamp pixels; off, it writes nothing.
void VS_StampStrip(in uint id : SV_VertexID, out float4 pos : SV_Position)
{
    const float2 px = id == 0 ? float2(BUFFER_WIDTH - 192, BUFFER_HEIGHT) : id == 1 ? float2(BUFFER_WIDTH, BUFFER_HEIGHT) : float2(BUFFER_WIDTH, BUFFER_HEIGHT - 4);
    pos = float4(px.x / BUFFER_WIDTH * 2.0 - 1.0, 1.0 - px.y / BUFFER_HEIGHT * 2.0, 0.0, 1.0);
}
float4 StampStrip(float4 pos, uint stamp)
{
    const int x = int(pos.x), y = int(pos.y);
    const int i = x - (BUFFER_WIDTH - 48);
    if (!StampOn || y != BUFFER_HEIGHT - 1 || i < 0) discard;
    const uint sum = (stamp ^ (stamp >> 8) ^ (stamp >> 16) ^ (stamp >> 24)) & 0xFFu;
    uint bit;
    if (i < 8) bit = (0xB2u >> uint(i)) & 1u;
    else if (i < 40) bit = (stamp >> uint(i - 8)) & 1u;
    else bit = (sum >> uint(i - 40)) & 1u;
    return bit != 0u ? float4(1.0, 1.0, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}
float4 PS_StampStrip(float4 pos : SV_Position) : SV_Target { return StampStrip(pos, FrameStamp0); }
float4 PS_StampStripR(float4 pos : SV_Position) : SV_Target { return StampStrip(pos, FrameStamp1); }

// The table (vrcam's VR view F5): the game's view from above in a Meta Quest
// over passthrough. The headset keys by brightness (the bridge's Blend
// passthrough: what is darker than a threshold, ~0.012, shows the room) -
// brightness, not a colour key: the video keeps it at full resolution, a
// colour at half (4:2:0) and with a fringe. So the void round the world is
// made exactly black, and everything of the game is lifted to at least
// TableFloor, so its own shadows and night are never cut. The void is the
// pixel and its neighbours two pixels out with no depth AND dark - what the
// interface draws over the void is bright and stays.
uniform bool TableKeyOn < hidden = true; > = false;
uniform float TableFloor < hidden = true; > = 0.05;   // vrcam, [table] floor: how far off black the game is lifted
// vrcam, [table] background: what the void is filled with - black for the bridge's
// brightness key, or a chroma key colour (pure green...) for Virtual Desktop's own.
uniform float3 TableBackground < hidden = true; > = float3(0.0, 0.0, 0.0);

// The diorama's edge: a pixel whose ground point - straight down from what it
// shows, so a wall at the edge stays whole - is outside the game's own view
// (times TableBounds) is the void too. vrcam: this eye's ray (SkyProj, Cam*),
// its eye less the hero, the game's view x projection by columns, the hero.
uniform bool TableBoundsOn < hidden = true; > = false;
uniform float TableBounds < hidden = true; > = 1.0;
uniform float3 TableEye0 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 TableEye1 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float4 TableGameX < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 TableGameY < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 TableGameW < hidden = true; > = float4(0.0, 0.0, 0.0, 1.0);
uniform float3 TableHero < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float2 TableTurn < hidden = true; > = float2(1.0, 0.0);   // [table] turn as cos, sin: the world turned under the board

bool OutsideTable(float2 uv, float d, int eye)
{
    if (!TableBoundsOn || d <= 0.0) return false;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    // reverse-Z with an infinite far plane: depth = near / distance along the view
    float3 p = (eye == 0 ? TableEye0 : TableEye1) + off * (NearPlane / d);
    p.y = 0.0;   // the ground under it, at the hero's feet
    // the board stays where it was put while the world turns under it: undo the turn
    p = float3(p.x * TableTurn.x - p.z * TableTurn.y, 0.0, p.x * TableTurn.y + p.z * TableTurn.x);
    const float4 w = float4(p + TableHero, 1.0);
    const float cw = dot(w, TableGameW);
    if (cw <= 1e-5) return true;
    const float2 g = float2(dot(w, TableGameX), dot(w, TableGameY)) / cw;
    return max(abs(g.x), abs(g.y)) > TableBounds;
}

float3 TableKey(float2 uv, int eye)
{
    const float3 c = tex2D(FogColorSmp, uv).rgb;
    if (!TableKeyOn || DepthWidth() < 16) return c;
    // The toolbar and the map drawn back (PS_GameHud, [hud_floor]) are never the void,
    // wherever bar_x / bar_y put them - over the room or past the board's edge: kept,
    // their dark lifted like the game's so the headset's key does not cut them.
    [branch] if (GameBarOn || GameMapOn)
    {
        if (GamePieces(uv, eye == 0 ? 1.0 : -1.0).a < 0.5)
            return max(c, float3(TableFloor, TableFloor, TableFloor));
    }
    // The names over items and monsters likewise ([hud_floor] labels_*), taken out to be
    // faded or left in the picture: their letters are never the void, wherever the board's
    // edge or no depth below would key them. A box alone keys like any dark (2026-10-06).
    if (InterfaceLit(uv, eye == 0 ? 1.0 : -1.0))
        return max(c, float3(TableFloor, TableFloor, TableFloor));
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float2 px = 2.0 * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    float d = DepthAt(duv).x;
    d = max(d, DepthAt(duv + float2(px.x, 0)).x);
    d = max(d, DepthAt(duv - float2(px.x, 0)).x);
    d = max(d, DepthAt(duv + float2(0, px.y)).x);
    d = max(d, DepthAt(duv - float2(0, px.y)).x);
    const float top = max(c.r, max(c.g, c.b));
    // Nothing drawn here. Dark: the void. Bright: the interface, or something drawn
    // without depth - rain (kept: "it does not get in the way") and grass at the end of
    // the drawn ground, which stood in the room as a strip past the board's edge
    // (2026-10-05). The grass stands on ground a few pixels below it, and that ground
    // is off the board; rain over the void has nothing near below it.
    if (d <= 0.0) {
        // (the interface's letters were kept above; its dark - a label's box - keys as before)
        if (IsHud(uv)) return c;
        if (top < 0.08) return TableBackground;
        const float down = UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT;
        // the nearest drawn pixel 2, 4 ... 64 pixels down (no return inside: the loop must unroll)
        float hitOff = 0.0, hitD = 0.0;
        [unroll] for (int k = 1; k <= 6; ++k) {
            const float off = exp2((float)k);
            const float dd = DepthAt(duv + float2(0.0, down * off)).x;
            const bool take = hitD <= 0.0 && dd > 0.0;
            hitOff = take ? off : hitOff;
            hitD = take ? dd : hitD;
        }
        if (hitD > 0.0 && OutsideTable(uv + float2(0.0, BUFFER_RCP_HEIGHT * hitOff), hitD, eye)) return TableBackground;
        return c;
    }
    // A pixel with no depth of its own beside one that has it (the foam and glints
    // along the far water's edge) is tested at its neighbour's depth: at its own,
    // zero, it was never outside and stood on the black at the horizon (2026-10-05).
    const float dc = DepthAt(duv).x;
    if (OutsideTable(uv, dc > 0.0 ? dc : d, eye)) return TableBackground;
    return top < TableFloor ? max(c, float3(TableFloor, TableFloor, TableFloor)) : c;
}
float3 PS_TableKey(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TableKey(uv, 0); }
float3 PS_TableKeyR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TableKey(uv, 1); }
