// D2R_FogWorld.fxh: DepthFogWorld, DepthFog and the fog pixel shaders: the main per-pixel shader.
// A feature header of D2R_DepthFog.fx, #included by it: a pure move out of the one effect, no line changed. The headers come after the effect's own inputs and in an order where
// every name is defined before it is used; where features interleaved in the old single file the order differs from it, which cannot change what the shader draws.

float3 DepthFogWorld(float4 pos, float2 uv, int eye)
{
    const float3 colour = tex2D(FogColorSmp, uv).rgb;
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float d = DepthAt(duv).x;

    // No depth at all (ReShade's Generic Depth off, or nothing picked): every
    // pixel read as the void, the fog covered the HUD and the sky filled every
    // dark speck of the ground. ReShade then binds a tiny empty texture, not a
    // screen-sized buffer. (Probing for empty depth low on the screen was wrong:
    // looking up, the sky is down there too, and the sky went out.)
    if (DepthWidth() < 16) return colour;

    // The pixel's four neighbours. Where the drawn distance ends, the far
    // ground has single empty pixels (depth 0): each was taken for the void,
    // got the sky, and the sky showed through the fog as dots along the
    // horizon. The void is now only where the pixel AND its neighbours are
    // empty; the fog takes the farthest drawn sample (reverse-Z: the smallest
    // positive depth), which also drowns single near pixels the game's dithered
    // transparencies leave in far ground. Empty neighbours must not turn a
    // nearby silhouette into full fog: that cut a black outline through effects
    // drawn over the pillars behind Tyrael. With no drawn sample, keep zero.
    const float2 px = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    const float d1 = DepthAt(duv + float2(px.x, 0)).x;
    const float d2 = DepthAt(duv - float2(px.x, 0)).x;
    const float d3 = DepthAt(duv + float2(0, px.y)).x;
    const float d4 = DepthAt(duv - float2(0, px.y)).x;
    const float dNear = max(max(d, d1), max(max(d2, d3), d4));
    const float dFar = min(min(d > 0.0 ? d : dNear, d1 > 0.0 ? d1 : dNear),
                           min(min(d2 > 0.0 ? d2 : dNear, d3 > 0.0 ? d3 : dNear),
                               d4 > 0.0 ? d4 : dNear));

    float3 fogColour = FogColor;
    if (SkyOn || CeilOn) {
        const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
        const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
        const float3 dir = eye == 0 ? normalize(v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0)
                                    : normalize(v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1);
        const float dither = (Hash(pos.xy) - 0.5) / 255.0;
        // What the game drew above the ceiling - the barracks' wall corners, crosses on the
        // wall tops - pierced the vault (the user's screenshot, 2026-10-07). The drawn
        // point is put back in the world by its depth (the same frame as the ceiling's:
        // the eye less the hero's feet); above the plane, the ceiling is drawn in its place.
        // A rim pixel with no depth of its own but a drawn neighbour takes the neighbour's:
        // left out, the cut shapes stayed outlined in black on the vault.
        const float dPt = d > 1e-6 ? d : dNear;
        // What the ceiling is wanted for here, and as far along the ray as: 1 a cut point, 2 a point
        // behind a wall at the floor's edge, 3 the void. ONE call of Ceiling for all three - three of
        // them inlined made the shader take 164 s to build (2026-10-07).
        int ceilMode = 0;
        float ceilLim = 1e9;
        if (CeilOn && !SkyOn && dPt > 1e-6) {
            const float3 off = (eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                         : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1) * (NearPlane / dPt);
            // A flame drawn (without depth) over a cut cross went with it: the cross stood black
            // in the fire (2026-10-07). What is fire in the pixel - warm, or bright - is laid
            // back over the vault as light; the stone of the cross, dim, is not.
            // The cut follows the stone's underside, not the plane: a cathedral's columns end
            // where the vault comes down to them (2026-10-07).
            const float3 eP = eye == 0 ? CeilEye0 : CeilEye1;
            const float2 heroP = eye == 0 ? CeilHero0 : CeilHero1;
            const float2 qPt = (eP.xz + off.xz + heroP) / CeilScale;
            // the point's height over the stone's underside there (> 0: above it)
            const float hPt = eP.y + off.y - CeilUnder(qPt);
            if (ShowCeilingCut) {
                if (frac((eP.y + off.y) / 5.0) < 0.04) return 1.0;
                // the columns the vault stands on (and the band their tops are looked for in)
                if (hPt > 0.2) return float3(1.0, 0.0, 0.0);   // cut: the vault is drawn there
                return hPt > 0.2 ? float3(1.0, 0.0, 0.0)
                                 : lerp(float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), saturate(1.0 + hPt / max(CeilHeight, 1.0)));
            }
            if (hPt > 0.2) { ceilMode = 1; ceilLim = length(off); }
        }
        // Nothing drawn here: the void, unless a HUD piece lies over it. Outdoors
        // the sky fills it; in a cave the ceiling, where this ray meets it - where
        // it does not, the void goes on as before (fogged below).
        float3 sky = float3(0.0, 0.0, 0.0);
        bool back = false;
        // A rim: no depth here, but something drawn beside it. The game's picture of
        // anything over the void is a pixel larger than its depth, its edge blended into
        // the void's black, and the sky was not painted there: a dark outline round every
        // roof, treetop and far building against the sky, which By turns moved off them in
        // every other frame of each eye (the owner, Kurast Docks: "why don't we just get rid
        // of that outline", 2026-10-25). With SkyOverRims the sky goes there too. Only at an
        // edge - at most two of the four neighbours drawn: a single empty pixel in the far
        // ground stays fogged as before (the dots along the horizon).
        bool rim = false;
        if (ceilMode == 0 && dNear > 1e-6 && d <= 1e-6 && SkyOn && SkyOverRims)
            rim = (d1 > 1e-6 ? 1 : 0) + (d2 > 1e-6 ? 1 : 0) + (d3 > 1e-6 ? 1 : 0) + (d4 > 1e-6 ? 1 : 0) <= 2;
        if (ceilMode == 0 && (dNear <= 1e-6 || rim)) {
            if (SkyOn) {
                sky = Sky(dir);
                // with the act's own fog colour the sky's low part sinks into it too:
                // the fogged land meets the sky without an edge
                if (FogOn && FogFixed) sky = lerp(sky, FogColor, 1.0 - smoothstep(-0.03, 0.2, dir.y));
                back = true;
            } else ceilMode = 3;
        }
        if (ceilMode != 0) {
            float3 cc;
            const bool got = Ceiling(dir, eye, cc, ceilLim);
            if (ceilMode == 1 && got) {
                // and bright: torch-lit stone is warm too, and a cut dome on a wall top stood
                // through the ceiling as a ghost in the catacombs (2026-10-07)
                const float peak = max(colour.r, max(colour.g, colour.b));
                const float warm = smoothstep(0.1, 0.3, colour.r - colour.b);
                const float fire = smoothstep(0.5, 0.85, peak) * max(warm, smoothstep(0.85, 1.0, peak));
                // Under it, the game's glow and flames over the void round it, filled in smoothly
                // (VoidGlow): its own lit stone must not show - the domes on the catacombs' walls
                // stood lit beside a flame, or black where 8 samples found no glow (2026-10-07).
                const float3 halo = VoidGlowAt(uv) * saturate(CeilHalo);
                // (eased in over 1.5 units it let the cut caps' outlines through - kept hard, 2026-10-07)
                return cc + dither + lerp(halo, colour, fire);
            }
            if (ceilMode == 3) { back = got; sky = cc; }
        }
        if (back) {
            const float luma = dot(colour, float3(0.2126, 0.7152, 0.0722));
            // A rim is dim as what it rims (about 0.1): the sky takes it unless it is clearly
            // light - a glow's edge beside a brazier stays, laid over the sky as below.
            const float k = rim ? 1.0 - smoothstep(0.15, 0.35, luma) : 1.0 - smoothstep(0.03, 0.08, luma);
            // The interface over the void keeps its own colour; its dark parts are the void.
            const bool hud = uv.y >= HudTop && tex2Dlod(HudMaskSmp, float4(uv, 0, 0)).r > 0.0;
            if (hud) return lerp(colour, sky + dither, k);
            // Anything else here was drawn without depth over the game's black void -
            // particles, grass, glows, the far trees of Kurast's docks. It is laid OVER
            // the sky as light, not in place of it: the dark leaves of those trees took
            // the sky's place and stood in the fog as holes in the shape of trees, and a
            // glow's dim rim, darker than the sky, ringed it in black (2026-10-05).
            // It is fogged as the first drawn surface straight BELOW it on the screen -
            // what it stands on or above: a flame gets its brazier's (clear), grass at
            // the end of the drawn ground that far ground's (gone). With nothing drawn
            // below, it is in the void: gone.
            // How much: the fog of the ground below, blurred (VoidFogTex). But ground
            // right below - 2 to 16 pixels down - wins with its own fog: the blur ran
            // the sky's none into the far ground at the horizon, and the foam along the
            // far water's edge stood out of the fog there (2026-10-05).
            float tf = 0.0;
            if (FogOn && k < 0.999) tf = ParticleFog(uv, duv);
            // Only what is clearly off black: the near-black spill the game leaves round
            // people and trees over the void, added in full, stood as a pale haze round
            // them (2026-10-05). k fades it in over the same 0.03..0.08 as the void test.
            // Over the cave ceiling the torches' own halo - a flat glow the game draws for the
            // black void - is kept at CeilHalo (1 = whole: "do not dim the halo", the user,
            // 2026-10-07); the flame itself, bright, always stays.
            float keep = 1.0, seen = 1.0 - k;
            if (!SkyOn) {
                keep = lerp(saturate(CeilHalo), 1.0, smoothstep(0.45, 0.85, max(colour.r, max(colour.g, colour.b))));
                // a halo's dim red rim is under the void's 0.03..0.08 too: over the ceiling it ended
                // short of where it ran on the wall (2026-10-07). Warm is a flame's light, not the
                // grey spill round people - it stays.
                seen = max(seen, saturate((colour.r - colour.b) * 15.0));
            }
            return sky + dither + colour * (seen * (1.0 - tf) * keep);
        }
        const float3 fogDir = normalize(float3(dir.x, clamp(dir.y, -0.1, 0.15), dir.z));
        if (SkyOn && !FogFixed) fogColour = (SkyTex > 0.5 ? PaintedSky(fogDir) : SkyGradient(fogDir.y)) * SkyBrightness + dither;
    }
    float3 litColour = colour;
    if ((GameLightsOn || HeroLightOn) && d > 1e-6) {
        const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
        if (sp.x != 0.0 && sp.y != 0.0) {
            const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
            const float3 off = (eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                         : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1) * (NearPlane / d);
            const float3 eP = eye == 0 ? CeilEye0 : CeilEye1;
            const float3 worldP = eP + off;

            // Reconstruct surface normal from screen derivatives of world position
            const float3 dX = ddx(worldP);
            const float3 dY = ddy(worldP);
            const float3 nrm = cross(dX, dY);
            const float nLen = length(nrm);
            float3 N = (nLen > 1e-5) ? (nrm / nLen) : float3(0.0, 1.0, 0.0);
            if (dot(N, off) > 0.0) N = -N;

            float3 lit = GameLightsOn ? SceneLight(worldP, N) : float3(0.0, 0.0, 0.0);
            if (HeroLightOn) lit += HeroLight(worldP, N, eP);   // the hero's own, by his Light Radius
            litColour += colour * lit * 1.2 + lit * 0.05;
        }
    }
    if (!FogOn) return litColour;

    if (!SkyOn && !CeilOn && dNear <= 1e-6) {
        // In unroofed interiors such as Tal Rasha's Chamber, zero depth belongs
        // to the background, not to Tyrael's wings drawn over it. Fogging that
        // whole pixel as infinitely far cut the wings to the pillars' outlines.
        // Keep the background's original fog and the effect's native light.
        // Borrowing distance from the ground below (ParticleFog) shaded columns
        // of the same wing differently and changed its brightness at pillars.
        // Without the effect's own depth, leave it at its authored brightness;
        // actual drawn scenery still uses its distance in the path below.
        if (ShowDistance) return 0.0;
        return fogColour * FogAmount(dFar) + litColour;
    }

    // The depth smeared: the fog of a 3x3 grid FogBlur pixels apart, averaged
    // (the fog, not the depth - an empty pixel is "far", not a zero to average).
    // 3x3, not 5x5: 25 taps per pixel in both eyes at 180 frames a second cost
    // the pair rate - 58..87 pairs/s instead of 90, and every missing pair
    // was a repeated frame in the headset.
    // Only neighbours whose fog is near this pixel's own are taken (within 0.25):
    // averaged whole, the far water beside a man got a share of his near-nothing
    // and stood round him as a dark rim FogBlur pixels wide (2026-10-05). The far
    // ground's holes and specks melt as before - their fog is the same.
    float t = FogAmount(dFar);
    if (FogBlur >= 0.5) {
        const float t0 = t;
        float sum = 0.0, wsum = 0.0;
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x) {
                const float ts = FogAmount(DepthAt(duv + float2(x, y) * px * FogBlur).x);
                const float w = 1.0 - saturate(abs(ts - t0) * 4.0);
                sum += ts * w; wsum += w;
            }
        t = (sum + t0) / (wsum + 1.0);
    }
    if (ShowDistance) return t.xxx;
    // In a cave a torch's halo is laid over the far floor too, and the fog took it away with
    // the floor: the floor's edge stood as a black cut-out in the middle of the glow
    // (2026-10-07). The halo is warm - red well over blue - and the cave's rock is not: what
    // is warm keeps its light through the fog (CeilHalo of it), as it does over the void.
    if (CeilOn && !SkyOn) {
        const float warm = smoothstep(0.05, 0.25, litColour.r - litColour.b);
        return lerp(litColour, fogColour, t) + litColour * (t * warm * saturate(CeilHalo));
    }
    return lerp(litColour, fogColour, t);
}
// The interface keeps its own colour: the fog and the sky only where the layer lets the world through.
float3 DepthFog(float4 pos, float2 uv, int eye)
{
    const float3 fogged = DepthFogWorld(pos, uv, eye);
    [branch] if (!UiMaskOn || UiMaskOff) return fogged;
    const float ui = 1.0 - saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    return lerp(fogged, tex2D(FogColorSmp, uv).rgb, ui);
}
float3 PS_DepthFog(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return DepthFog(pos, uv, 0); }
float3 PS_DepthFogR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return DepthFog(pos, uv, 1); }
