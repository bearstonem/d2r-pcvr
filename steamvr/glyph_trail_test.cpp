#include "glyph_trail.h"
#include <cstdio>
#include <string>
using namespace framevr;
using namespace framevr::glyphtrail;
static int failures = 0;
static void Check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; } }
static const uint8_t* At(const std::vector<uint8_t>& rgba, int x, int y) { return &rgba[(size_t(y) * Size + size_t(x)) * 4]; }
// The alpha summed over rows y0..y1 (inclusive) across the image.
static long Rows(const std::vector<uint8_t>& rgba, int y0, int y1) {
    long sum = 0;
    for (int y = std::max(0, y0); y <= std::min(Size - 1, y1); ++y)
        for (int x = 0; x < Size; ++x) sum += At(rgba, x, y)[3];
    return sum;
}
static long Total(const std::vector<uint8_t>& rgba) { return Rows(rgba, 0, Size - 1); }

int main() {
    // A horizontal stroke 30 cm long from the middle to the right, as it is drawn.
    Job job;
    job.stroke = {{0, 0}, {.15f, 0}, {.3f, 0}};
    job.strokeId = 1;
    auto img = Paint(job);
    Check(img.size() == size_t(Size) * Size * 4, "the image is Size x Size RGBA");
    const auto mid = Pixel({.15f, 0});
    const int mx = int(mid.x), my = int(mid.y);
    const uint8_t* on = At(img, mx, my);
    Check(on[3] > 240, "on the line: opaque");
    Check(on[2] >= on[0], "drawing: pale arcane, no warmer than blue");
    const uint8_t* near = At(img, mx, my + 12);
    Check(near[3] > 20 && near[3] < on[3], "beside the line: the glow, fainter");
    Check(At(img, 20, 20)[3] == 0, "far from the line: transparent");
    Check(At(img, Size / 2 - 60, Size / 2)[3] == 0, "behind the stroke's start: transparent");
    Check(Paint(job) == img, "the same job paints the same picture");

    // The overall fade scales the alpha.
    job.alpha = .5f;
    Check(std::abs(int(At(Paint(job), mx, my)[3]) - 128) <= 2, "half faded: half alpha on the line");
    job.alpha = 0;
    Check(Total(Paint(job)) == 0, "fully faded: nothing");
    job.alpha = 1;

    // Each element's look: painted, and gone after its time.
    struct Named { Look look; const char* name; };
    for (const auto& h : {Named{Look::Fire, "fire"}, Named{Look::Cold, "cold"}, Named{Look::Lightning, "lightning"}, Named{Look::Poison, "poison"},
                          Named{Look::Holy, "holy"}, Named{Look::Magic, "magic"}, Named{Look::War, "war"}, Named{Look::Nature, "nature"},
                          Named{Look::Bone, "bone"}, Named{Look::Curse, "curse"}, Named{Look::Shadow, "shadow"}, Named{Look::Steel, "steel"},
                          Named{Look::Refused, "refused"}}) {
        Job j = job; j.look = h.look; j.t = h.look == Look::Refused ? .04f : .6f;
        Check(Total(Paint(j)) > Total(img) / 2, std::string(h.name) + ": painted");
        j.t = Duration(h.look) + .01f;
        Check(Total(Paint(j)) == 0, std::string(h.name) + ": gone after its time");
    }
    auto shade = [&](Look look, float t) { Job j = job; j.look = look; j.t = t; return Paint(j); };
    const auto fire = shade(Look::Fire, .6f), cold = shade(Look::Cold, .6f), poison = shade(Look::Poison, .6f);
    // The glow beside the line carries the colour (the core runs white hot).
    Check(At(fire, mx, my + 10)[0] > At(fire, mx, my + 10)[2] + 60, "fire: red over blue");
    Check(At(cold, mx, my + 10)[2] > At(cold, mx, my + 10)[0] + 60, "cold: blue over red");
    Check(At(poison, mx, my + 10)[1] > At(poison, mx, my + 10)[0] && At(poison, mx, my + 10)[1] > At(poison, mx, my + 10)[2], "poison: green");
    // Fire rises off the line, poison falls from it.
    Check(Rows(fire, my - 70, my - 20) > 2 * Rows(fire, my + 20, my + 70), "fire: flames and embers above the line");
    Check(Rows(poison, my + 20, my + 70) > 2 * Rows(poison, my - 70, my - 20), "poison: mist and drops below the line");
    // The flash on recognition runs whiter than the effect after it.
    Check(At(shade(Look::Fire, .03f), mx, my)[2] > At(shade(Look::Fire, 1.0f), mx, my)[2], "the flash: a whiter core");
    // Lightning crackles: its arcs jump between frames.
    Check(shade(Look::Lightning, .5f) != shade(Look::Lightning, .56f), "lightning: the arcs move");
    // Curse sinks: its mist under the line.
    const auto curse = shade(Look::Curse, .6f);
    Check(Rows(curse, my + 20, my + 70) > Rows(curse, my - 70, my - 20), "curse: sinking below the line");
    // War rings out: an echo stands off the line where the plain glow has nearly gone.
    Check(At(shade(Look::War, .3f), mx, my - 20)[3] > At(img, mx, my - 20)[3] + 60, "war: an echo off the line");
    // Each leaves: well into its exit less is left, and a refused glyph goes sooner.
    Check(Total(shade(Look::Fire, 2.0f)) < Total(fire) / 2 && Total(shade(Look::Cold, 2.0f)) < Total(cold) / 2, "the exit: most of it gone");
    Check(Total(shade(Look::Refused, .9f)) == 0 && Total(shade(Look::Holy, .9f)) > 0, "refused goes sooner");

    // Painted as it is drawn (the worker keeps the coverage, adding the new segments) or at once: the same.
    {
        auto work = Painter::Incremental();
        Job j; j.strokeId = 7; j.look = Look::Fire; j.t = .6f;
        const std::vector<glyphs::Point> pts{{0, 0}, {.1f, .05f}, {.2f, -.05f}, {.25f, .1f}, {.12f, .2f}};
        Image last;
        for (size_t n = 1; n <= pts.size(); ++n) { j.stroke.assign(pts.begin(), pts.begin() + long(n)); last = work(j); }
        Check(last.pixels == Paint(j), "painted a segment at a time: the same picture as at once");
        j.t = 1.4f;
        Check(work(j).pixels == Paint(j), "a later frame re-shades the same coverage");
        j.strokeId = 8; j.stroke = {{0, 0}, {.1f, 0}};
        Check(work(j).pixels == Paint(j), "a new stroke starts over");
    }

    // A stroke off the image is clipped, not written out of bounds.
    Job edge; edge.stroke = {{0, 0}, {.9f, .9f}}; edge.look = Look::Fire; edge.t = .6f;
    Check(Paint(edge).size() == size_t(Size) * Size * 4, "a stroke past the edge is clipped");

    // Element colours and looks.
    Check(ElementColour(40) == Cold && ElementColour(52) == Fire && ElementColour(57) == Lightning, "sorceress armors and buffs");
    Check(ElementColour(149) == War && ElementColour(68) == Bone && ElementColour(278) == Poison, "warcries, bone, venom");
    Check(ElementColour(99) == Holy && ElementColour(102) == Fire && ElementColour(114) == Cold, "paladin auras by element, others holy");
    Check(ElementColour(9999) == Drawing, "an unknown skill: pale arcane");
    Check(ElementColour(359) == Magic && ElementColour(220) == Magic && ElementColour(219) == Magic, "Town Portal: arcane, as Teleport");
    Check(ElementColour(87) == Curse && ElementColour(85) == Curse && ElementColour(70) == Bone && ElementColour(90) == Steel, "necromancer: curses, blood, bone, iron");
    Check(ElementColour(98) == War && ElementColour(123) == Magic && ElementColour(109) == Nature && ElementColour(119) == Holy, "paladin auras: might, conviction, cleansing, sanctuary");
    Check(ElementColour(264) == Shadow && ElementColour(277) == Steel && ElementColour(271) == Lightning && ElementColour(262) == Fire, "assassin: shadow, blades, traps");
    Check(ElementColour(373) == Curse && ElementColour(388) == Shadow && ElementColour(402) == Fire && ElementColour(403) == Drawing, "warlock: demon, eldritch, chaos");
    Check(ElementColour(223) == Nature && ElementColour(244) == Fire && ElementColour(59) == Cold && ElementColour(54) == Magic, "druid forms and fire; blizzard, teleport");
    Check(ElementLook(40) == Look::Cold && ElementLook(149) == Look::War && ElementLook(9999) == Look::Drawing && ElementLook(42) == Look::Lightning,
          "each element's look");

    if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
    std::puts("glyph-trail-test: ok");
    return 0;
}
