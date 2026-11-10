// effect_passes.h: which size of reshade/D2R_DepthFog.fx runs. The effect was one technique of 31 passes an eye,
// nine of them for features that are off in ordinary first-person play: five wrote the whole picture back
// unchanged, four kept a mask of the interface that nothing read. The effect now has three sizes, each a technique
// per eye, and vrcam keeps on the smallest that draws everything wanted (vr/vrcam.cpp OnFinishEffects):
//
//   Lean  23 passes  the world's passes, and the frame stamp as a pass over its 48 pixels (PS_StampStrip)
//   Mask  27 passes  + PS_HudCopy, PS_HudMask, PS_HudStore, PS_HudMaskStore: the interface found in the picture
//   Full  31 passes  + PS_Hud, PS_GameHud, PS_Phantom, PS_TableKey, and PS_Stamp for the strip: the effect as it always ran
//
// The frame stamp is on by default in AFR stereo. While it needed the Full size nothing smaller ever ran there
// (headset log 2026-11-10: 31 passes an eye throughout), so the smaller sizes draw it themselves.
//
// One technique an eye, never several stacked: the order of the passes is the file's own, whatever order ReShade
// keeps its techniques in. This header is the rule and the names; effect_passes_test.cpp pins the pass lists in
// the .fx against it.
#pragma once
#include <cstdint>
#include <cstdio>

namespace framevr::effectpasses {

// What needs a pass the Lean size leaves out, as bits.
enum Use : uint32_t {
    // Each of these has a pass of its own writing the picture: the Full size.
    HudShift = 1u << 0,     // PS_Hud: [stereo] ui_near above 0 in AFR stereo (reads the mask too)
    GamePieces = 1u << 1,   // PS_GameHud: the toolbar, the map or the labels drawn back into the picture
    DebugLines = 1u << 2,   // PS_Phantom: [debug] phantom_ray, bone_axes, the developer shield guard
    TableKey = 1u << 3,     // PS_TableKey: the game on the table, F5 (reads the mask too)
    // These only read the interface mask, in the fog pass (D2R_FogWorld.fxh, the void under SkyOn or CeilOn): the Mask size.
    Sky = 1u << 5,          // the added sky, or Arcane's
    ShaderCeiling = 1u << 6,// the shader's vault (not the game-drawn ceiling)
    // Drawn at every size (PS_StampStrip in Lean and Mask, PS_Stamp in Full): named for the log only.
    FrameStamp = 1u << 4    // the frame's stamp in the bottom row, for FlatVR
};
inline constexpr uint32_t FullUses = HudShift | GamePieces | DebugLines | TableKey;
inline constexpr uint32_t MaskUses = Sky | ShaderCeiling;

enum Size { Lean = 0, Mask = 1, Full = 2, SizeCount = 3 };
inline constexpr int Passes[SizeCount] = {23, 27, 31};
inline constexpr int PassesBefore = 31;   // an eye, before the sizes: every pass, always
// [size][eye]: the left eye's (and mono), the right eye's. Full keeps the names an older plugin and ReShade's preset know.
inline constexpr const char* Technique[SizeCount][2] = {
    {"D2R_DepthFog_Lean", "D2R_DepthFog_Lean_R"},
    {"D2R_DepthFog_Mask", "D2R_DepthFog_Mask_R"},
    {"D2R_DepthFog", "D2R_DepthFog_R"},
};

// The smallest size that draws everything in `uses`.
inline constexpr Size SizeFor(uint32_t uses) { return (uses & FullUses) ? Full : (uses & MaskUses) ? Mask : Lean; }

// The size to switch on when not every technique is in the installed effect (an older .fx beside a newer plugin has
// only the Full pair): the wanted one, else the next larger there is. Full is always there once the effect is found.
inline constexpr Size Available(Size wanted, bool haveLean, bool haveMask) {
    if (wanted == Lean && haveLean) return Lean;
    if (wanted != Full && haveMask) return Mask;
    return Full;
}

// "none" or the uses by name, for the log.
inline int Describe(uint32_t uses, char* out, size_t size) {
    static constexpr struct { uint32_t bit; const char* name; } kNames[] = {
        {Sky, "interface mask for the sky"}, {ShaderCeiling, "interface mask for the shader ceiling"}, {HudShift, "HUD shift"},
        {GamePieces, "toolbar / map / labels"}, {DebugLines, "debug lines"}, {TableKey, "table key"}, {FrameStamp, "frame stamp"}};
    int len = 0;
    if (size) out[0] = 0;
    for (const auto& n : kNames) {
        if (!(uses & n.bit) || size_t(len) >= size) continue;
        const int w = std::snprintf(out + len, size - size_t(len), "%s%s", len ? ", " : "", n.name);
        if (w < 0) break;
        len += w;
    }
    if (!len) len = std::snprintf(out, size, "none");
    return len;
}

}  // namespace framevr::effectpasses
