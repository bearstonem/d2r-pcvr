#pragma once
// Quick cast radial: the CPU painter of its two atlas regions
// (steamvr/quick_radial.h atlas). Runs on the radial's worker (vr/quick_radial.cpp) and in quick-radial-preview
// (the render sheet), never on a frame thread: the bronze band, each item's plate (its skill's or item's icon from upstream's
// icon cache, cropped as the ring wheel crops it, else its initials), the pick's rim and the center's name strips (the menu's Cinzel through GDI, Georgia when it is missing). Out: premultiplied RGBA (in
// linear light, encoded again, when the shared image is sRGB), each region exactly its size.
#include <cstdint>
#include <string>
#include <vector>
#include "quick_radial.h"

namespace framevr::quickradial::paint {
struct Icon { const uint8_t* rgba = nullptr; int w = 0, h = 0; };   // straight RGBA (upstream's icon cache); null: none
struct PEntry {
    Icon icon;
    std::string name;      // UTF-8, the game's color codes already cleaned
    int level = 0;         // shown after the name when above 0
    bool dim = false;      // the game refuses it now (not learned, not usable): drawn faint, never cast
    int count = -1;        // an item entry: how many it can use (belt and inventory), shown on its plate and after its name; -1: a skill
    std::string why;       // dim: why, after the name ("none left"); empty: "not now"
    bool empty = false;    // Fixed places (2026-11-17): an empty place, drawn as a faint socket, no plate, no name
};
struct Job {
    std::vector<PEntry> entries;   // the ring's items, in its order
    Tuning t;
    bool srgb = false;
    uint64_t key = 0;      // what was asked (the glue compares it with what it wants now)
};
struct Picture {
    std::vector<uint8_t> a, b;   // region A (atlas::AW x AH) and B (BW x BH), premultiplied RGBA
    uint64_t key = 0;
    double ms = 0;               // the paint's own time
    bool fontOk = false;         // the menu's Cinzel was used (else Georgia)
};
// The menu font from memory (the plugin's RCDATA D2RVR_FONT_CINZEL, or the preview's file): once, any thread; false keeps Georgia.
bool LoadFont(const void* data, size_t bytes);
void Paint(const Job& job, Picture& out);
// The labels' words (the glue logs and tests read them): an item's line, and the fixed labels' (Open by: Toggle or Hold).
std::string SkillLine(const PEntry& e);
const char* FixedLabel(int label, bool toggle);
}  // namespace framevr::quickradial::paint
