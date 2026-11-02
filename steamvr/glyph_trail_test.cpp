#include "glyph_trail.h"
#include <cstdio>
#include <string>
using namespace framevr;
using namespace framevr::glyphtrail;
static int failures = 0;
static void Check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; } }
static const uint8_t* At(const std::vector<uint8_t>& rgba, int x, int y) { return &rgba[(size_t(y) * Size + size_t(x)) * 4]; }

int main() {
    // A horizontal stroke 30 cm long from the middle to the right.
    Job job;
    job.stroke = {{0, 0}, {.15f, 0}, {.3f, 0}};
    job.colour = Fire;
    auto img = Paint(job);
    Check(img.size() == size_t(Size) * Size * 4, "the image is Size x Size RGBA");
    const auto mid = Pixel({.15f, 0});
    const uint8_t* on = At(img, int(mid.x), int(mid.y));
    Check(on[3] > 240, "on the line: opaque");
    Check(on[0] > on[2], "fire: more red than blue");
    const uint8_t* near = At(img, int(mid.x), int(mid.y) + 12);
    Check(near[3] > 20 && near[3] < on[3], "beside the line: the glow, fainter");
    Check(At(img, 20, 20)[3] == 0, "far from the line: transparent");
    Check(At(img, Size / 2 - 60, Size / 2)[3] == 0, "behind the stroke's start: transparent");

    // The fade scales the alpha.
    job.alpha = .5f;
    auto half = Paint(job);
    Check(std::abs(int(At(half, int(mid.x), int(mid.y))[3]) - 128) <= 2, "half faded: half alpha on the line");
    job.alpha = 0;
    auto gone = Paint(job);
    bool any = false;
    for (size_t i = 3; i < gone.size(); i += 4) any = any || gone[i];
    Check(!any, "fully faded: nothing");

    // The flash whitens the core.
    job.alpha = 1; job.flare = 1;
    auto flash = Paint(job);
    Check(At(flash, int(mid.x), int(mid.y))[2] > on[2], "the flash: whiter core");

    // A stroke off the image is clipped, not written out of bounds.
    Job edge; edge.stroke = {{0, 0}, {.9f, .9f}};
    auto clipped = Paint(edge);
    Check(clipped.size() == size_t(Size) * Size * 4, "a stroke past the edge is clipped");

    // Painted as it is drawn (the worker keeps the coverage, adding the new segments) or at once: the same.
    {
        auto work = Painter::Incremental();
        Job j; j.strokeId = 7; j.colour = Cold;
        const std::vector<glyphs::Point> pts{{0, 0}, {.1f, .05f}, {.2f, -.05f}, {.25f, .1f}, {.12f, .2f}};
        Image last;
        for (size_t n = 1; n <= pts.size(); ++n) { j.stroke.assign(pts.begin(), pts.begin() + long(n)); last = work(j); }
        Check(last.pixels == Paint(j), "painted a segment at a time: the same picture as at once");
        j.alpha = .3f; j.flare = .5f;
        Check(work(j).pixels == Paint(j), "a fade re-shades the same coverage");
        j.strokeId = 8; j.stroke = {{0, 0}, {.1f, 0}};
        Check(work(j).pixels == Paint(j), "a new stroke starts over");
    }

    // Element colours.
    Check(ElementColour(40) == Cold && ElementColour(52) == Fire && ElementColour(57) == Lightning, "sorceress armors and buffs");
    Check(ElementColour(149) == War && ElementColour(68) == Bone && ElementColour(278) == Poison, "warcries, bone, venom");
    Check(ElementColour(99) == Holy && ElementColour(102) == Fire && ElementColour(114) == Cold, "paladin auras by element, others holy");
    Check(ElementColour(9999) == Drawing, "an unknown skill: pale arcane");

    if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
    std::puts("glyph-trail-test: ok");
    return 0;
}
