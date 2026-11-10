// The sizes of reshade/D2R_DepthFog.fx (effect_passes.h): the rule that picks one, and the pass lists in the effect
// itself. A smaller size must be the full technique with exactly the named passes left out, in the same order, for
// both eyes - and nothing but the known readers may sample the interface mask the Lean size no longer keeps.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "effect_passes.h"
#include "fx_source.h"

using namespace framevr::effectpasses;

static void Require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }

// technique name -> each pass's text between its braces
static std::map<std::string, std::vector<std::string>> Techniques(const std::string& fx) {
    std::map<std::string, std::vector<std::string>> out;
    size_t at = 0;
    while ((at = fx.find("\ntechnique ", at)) != std::string::npos) {
        at += 11;
        const size_t nameEnd = fx.find_first_of(" \r\n<{", at);
        const std::string name = fx.substr(at, nameEnd - at);
        size_t open = fx.find("\n{", nameEnd);   // after the annotations
        Require(open != std::string::npos, "technique without a body: " + name);
        const size_t close = fx.find("\n}", open);
        Require(close != std::string::npos, "technique not closed: " + name);
        Require(!out.count(name), "technique twice: " + name);
        auto& passes = out[name];
        size_t p = open;
        while ((p = fx.find("pass {", p)) != std::string::npos && p < close) {
            const size_t end = fx.find('}', p);
            passes.push_back(fx.substr(p + 6, end - p - 6));
            p = end;
        }
        at = close;
    }
    return out;
}
static bool Has(const std::string& pass, const char* shader) {
    const std::string key = std::string("PixelShader = ") + shader + ";";
    return pass.find(key) != std::string::npos;
}
static bool HasAny(const std::string& pass, const std::vector<const char*>& shaders) {
    for (const char* s : shaders) if (Has(pass, s)) return true;
    return false;
}
static std::vector<std::string> Without(const std::vector<std::string>& passes, const std::vector<const char*>& shaders) {
    std::vector<std::string> out;
    for (const auto& p : passes) if (!HasAny(p, shaders)) out.push_back(p);
    return out;
}
static size_t Count(const std::string& text, const char* word) {
    size_t n = 0;
    for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) ++n;
    return n;
}

int main(int argc, char** argv) { try {
    Require(argc == 2, "expected D2R_DepthFog.fx path");

    // The rule.
    Require(SizeFor(0) == Lean, "nothing optional wanted is the Lean size");
    Require(SizeFor(Sky) == Mask && SizeFor(ShaderCeiling) == Mask && SizeFor(Sky | ShaderCeiling) == Mask, "the sky and the shader ceiling read the mask");
    for (const uint32_t use : {uint32_t(HudShift), uint32_t(GamePieces), uint32_t(DebugLines), uint32_t(TableKey)}) {
        Require(SizeFor(use) == Full, "a pass of its own needs the Full size");
        Require(SizeFor(use | Sky) == Full, "... with the sky too");
    }
    Require(SizeFor(FrameStamp) == Lean && SizeFor(FrameStamp | Sky) == Mask, "the frame stamp is drawn at every size: it asks for none");
    Require(Available(Lean, true, true) == Lean && Available(Mask, true, true) == Mask && Available(Full, true, true) == Full, "all sizes there");
    Require(Available(Lean, false, false) == Full && Available(Mask, false, false) == Full, "an older effect has only the Full pair");
    Require(Available(Lean, false, true) == Mask && Available(Mask, true, false) == Full, "the next larger size there is");
    char text[200];
    Describe(0, text, sizeof text);
    Require(!strcmp(text, "none"), "nothing optional reads none");
    Describe(Sky | HudShift | TableKey, text, sizeof text);
    Require(!strcmp(text, "interface mask for the sky, HUD shift, table key"), text);
    char tiny[8];
    Describe(~0u, tiny, sizeof tiny);   // never past the buffer
    Require(strlen(tiny) < sizeof tiny, "a short buffer is not overrun");

    // The effect: the six techniques and nothing else.
    std::ifstream file(argv[1], std::ios::binary);
    Require(bool(file), "cannot read the effect");
    const auto noCr = [](std::string text) { text.erase(std::remove(text.begin(), text.end(), '\r'), text.end()); return text; };   // either line ending
    const std::string fx = noCr(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
    const auto techniques = Techniques(fx);
    Require(techniques.size() == size_t(SizeCount) * 2, "the effect has " + std::to_string(techniques.size()) + " techniques, not six");
    const std::vector<const char*> maskPasses = {"PS_HudCopy", "PS_HudMask", "PS_HudStore", "PS_HudMaskStore"};
    const std::vector<const char*> fullPasses[2] = {{"PS_Hud", "PS_GameHud", "PS_Phantom", "PS_TableKey", "PS_Stamp"},
                                                    {"PS_HudR", "PS_GameHudR", "PS_PhantomR", "PS_TableKeyR", "PS_StampR"}};
    for (int eye = 0; eye < 2; ++eye) {
        for (int size = 0; size < SizeCount; ++size) {
            const auto found = techniques.find(Technique[size][eye]);
            Require(found != techniques.end(), std::string("no technique ") + Technique[size][eye]);
            Require(int(found->second.size()) == Passes[size],
                    std::string(Technique[size][eye]) + " has " + std::to_string(found->second.size()) + " passes, effect_passes.h says " + std::to_string(Passes[size]));
        }
        const auto& full = techniques.at(Technique[Full][eye]);
        Require(Passes[Full] == PassesBefore, "the Full size is the effect as it was");
        size_t mask = 0, own = 0;
        for (const auto& p : full) { mask += HasAny(p, maskPasses); own += HasAny(p, fullPasses[eye]); }
        Require(mask == 4 && own == 5, "the Full size has each optional pass once");
        // The always-on passes: Lean is Full less the nine, Mask is Full less the five, order kept.
        // Each smaller size ends with the stamp's own small pass, in place of the Full size's pass over the picture.
        auto maskSize = Without(full, fullPasses[eye]);
        auto leanSize = Without(maskSize, maskPasses);
        for (const int size : {int(Mask), int(Lean)}) {
            const auto& list = techniques.at(Technique[size][eye]);
            const std::string strip = std::string("PixelShader = PS_StampStrip") + (eye ? "R;" : ";");
            Require(!list.empty() && list.back().find("VertexShader = VS_StampStrip;") != std::string::npos && list.back().find(strip) != std::string::npos &&
                    list.back().find("VertexCount = 3;") != std::string::npos && list.back().find("RenderTarget") == std::string::npos,
                    std::string(Technique[size][eye]) + " does not end with the stamp strip, three vertices into the picture");
            (size == Mask ? maskSize : leanSize).push_back(list.back());
        }
        Require(!HasAny(full.back(), {"PS_StampStrip"}) && Count(full.back(), "PS_Stamp") == 1, "the Full size keeps its own stamp pass, last");
        Require(techniques.at(Technique[Mask][eye]) == maskSize, std::string(Technique[Mask][eye]) + " is not the full technique less the five feature passes");
        Require(techniques.at(Technique[Lean][eye]) == leanSize, std::string(Technique[Lean][eye]) + " is not the full technique less the nine optional passes");
    }
    // The right eye's lists are the left eye's with its own entry points.
    {
        auto left = techniques.at(Technique[Full][0]);
        const auto& right = techniques.at(Technique[Full][1]);
        const char* const kOwn[] = {"PS_VoidFog", "PS_TorchCands", "CS_RecordHeights", "PS_DepthFog", "PS_Outline", "VS_Immunity", "VS_TeleportArc",
                                    "PS_Hud", "PS_GameHud", "PS_Phantom", "PS_TableKey", "PS_Stamp"};
        for (auto& p : left)
            for (const char* name : kOwn)
                for (const char* end : {";", "<"}) {
                    const std::string from = std::string("= ") + name + end, to = std::string("= ") + name + "R" + end;
                    if (const size_t at = p.find(from); at != std::string::npos) p.replace(at, from.size(), to);
                }
        Require(left == right, "the right eye's full technique differs from the left eye's by more than its entry points");
    }

    // The proof the Lean size rests on: who reads what the four mask passes write. A new reader anywhere must be
    // given a Use bit (and a line in vrcam's OnFinishEffects) before this count is changed.
    const std::string all = noCr(fxsource::ReadWithIncludes(argv[1]));
    Require(Count(all, "HudMaskSmp") == 4, "HudMaskSmp: its sampler, PS_HudMaskStore, IsHud and the fog pass's void - " + std::to_string(Count(all, "HudMaskSmp")) + " found");
    Require(Count(all, "IsHud(") == 4, "IsHud: itself, twice in Hud (the HUD shift) and once in TableKey - " + std::to_string(Count(all, "IsHud(")) + " found");
    Require(Count(all, "HudCurSmp") == 5 && Count(all, "HudPrevSmp") == 4 && Count(all, "HudMaskOldSmp") == 2, "the mask's own textures are read by the mask passes only");
    Require(Count(all, "SameInBothEyes(") == 2, "SameInBothEyes is PS_HudMask's alone");
    {   // the fog pass reads the mask only inside its sky / ceiling block
        const size_t block = all.find("if (SkyOn || CeilOn) {"), read = all.find("tex2Dlod(HudMaskSmp, float4(uv, 0, 0)).r > 0.0;\n            if (hud)");
        const size_t after = all.find("float3 litColour = colour;");
        Require(block != std::string::npos && read != std::string::npos && after != std::string::npos && block < read && read < after,
                "the fog pass's read of the interface mask is no longer inside its SkyOn || CeilOn block");
    }
    // Each pass the smaller sizes leave out writes the picture back unchanged while its feature is off.
    Require(all.find("if (shift == 0.0 || uv.y < HudTop) return c;") != std::string::npos, "Hud passes through without a shift");
    Require(all.find("if (!TableKeyOn || DepthWidth() < 16) return c;") != std::string::npos, "TableKey passes through when off");
    Require(all.find("if (!StampOn || y != BUFFER_HEIGHT - 1 || i < 0) return c;") != std::string::npos, "Stamp passes through when off");
    Require(all.find("if (!StampOn || y != BUFFER_HEIGHT - 1 || i < 0) discard;") != std::string::npos, "the stamp strip writes nothing when off or outside its 48 pixels");
    Require(all.find("if (!ShieldDebugOn) return c;") != std::string::npos && all.find("if (PhantomOn) c = Segment(") != std::string::npos &&
            all.find("if (BoneOn) {") != std::string::npos, "Phantom draws only under its three switches");
    Require(all.find("[branch] if (LabelsOn)") != std::string::npos && all.find("[branch] if (GameBarOn) p = ") != std::string::npos &&
            all.find("[branch] if (GameMapOn)") != std::string::npos, "GameHud draws only under its three switches");

    printf("Effect sizes: Lean %d, Mask %d, Full %d passes an eye (was %d); pass lists and mask readers pinned\n", Passes[Lean], Passes[Mask], Passes[Full], PassesBefore);
    return 0;
} catch (const std::exception& e) { fprintf(stderr, "effect-passes-test: %s\n", e.what()); return 1; } }
