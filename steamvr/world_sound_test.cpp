// world_sound.h: the game's own reverb numbers with the echo raised, the Echo setting, the log text.
#include "world_sound.h"
#include "vr_settings.h"
#include "vr_settings_content.h"
#include "vr_settings_help.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

using namespace framevr::worldsound;
namespace st = framevr::settings;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
static bool Same(const Reverb& a, const Reverb& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// soundenviron.txt rows as the game hands them over (times / 1000).
static const Reverb Cave{5, -2000, -600, 3.4f, 0.975f, 0, 0.012f, 125, 0.1f};            // ESOUNDENVIRON_CAVE, effects
static const Reverb Cathedral{7, -1900, -500, 4.3f, 0.7f, -500, 0.1f, -2, 0.08f};        // ESOUNDENVIRON_CATHEDRAL
static const Reverb Wilderness{17, -1650, -950, 0.25f, 0.7f, -700, 0.175f, -1434, 0.1f}; // ESOUNDENVIRON_WILDERNESS
static const Reverb Arcane{7, -900, -2000, 4.0f, 0.7f, -1230, 0.0f, 350, 0.0f};          // the loudest room in the table
static const Reverb Mephisto{7, -2200, -500, 4.75f, 0.7f, -1230, 0.02f, -2, 0.029f};     // the longest but Baal's
static const Reverb Off{0, -10000, 0, 0.0f, 0.0f, -10000, 0.0f, -10000, 0.0f};           // ESOUNDENVIRON_DISABLE_REVERB
static const Reverb Kings{0, -1250, -10000, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f};               // VALLEY_OF_KINGS: a decay of 0

int main() {
    // The structure is the game's: nine 4-byte fields, levels then times as D2SoundEAXSetParams reads them.
    assert(offsetof(Reverb, room) == 4 && offsetof(Reverb, decay) == 0xc && offsetof(Reverb, reflections) == 0x14 &&
           offsetof(Reverb, reverb) == 0x1c && offsetof(Reverb, reverbDelay) == 0x20);

    // Game's own changes nothing, bit for bit.
    for (const Reverb& r : {Cave, Cathedral, Wilderness, Arcane, Off, Kings}) assert(Same(Boost(r, EchoGame), r));

    // More: Room 6 dB up (twice as loud), every other number the game's.
    {
        const Reverb m = Boost(Cave, EchoMore);
        assert(m.room == -1400 && Near(m.decay, 3.4f));
        Reverb back = m; back.room = Cave.room;
        assert(Same(back, Cave));
        assert(Boost(Cathedral, EchoMore).room == -1300 && Boost(Wilderness, EchoMore).room == -1050);
    }
    // Strong: 12 dB up, and the tail a quarter longer.
    {
        const Reverb s = Boost(Cave, EchoStrong);
        assert(s.room == -800 && Near(s.decay, 4.25f));
        assert(s.environment == Cave.environment && s.roomHf == Cave.roomHf && s.reflections == Cave.reflections &&
               s.reverb == Cave.reverb && Near(s.decayHf, Cave.decayHf) && Near(s.reflectionsDelay, Cave.reflectionsDelay) && Near(s.reverbDelay, Cave.reverbDelay));
        assert(Near(Boost(Cathedral, EchoStrong).decay, 5.375f) && Near(Boost(Mephisto, EchoStrong).decay, 5.9375f));
        assert(Near(Boost(Wilderness, EchoStrong).decay, 0.3125f));   // the moor stays short
    }
    // The echo never passes the ceiling, and never gets quieter or shorter than the game's.
    {
        assert(Boost(Arcane, EchoMore).room == RoomCeiling && Boost(Arcane, EchoStrong).room == RoomCeiling);
        Reverb loud = Cave; loud.room = -100;
        assert(Boost(loud, EchoStrong).room == -100);
        Reverb longTail = Cave; longTail.decay = 12.0f;
        assert(Near(Boost(longTail, EchoStrong).decay, 12.0f));
        Reverb nearLongest = Cave; nearLongest.decay = 7.0f;
        assert(Near(Boost(nearLongest, EchoStrong).decay, DecayLongest));
        for (int echo = 0; echo < EchoCount; ++echo)
            for (const Reverb& r : {Cave, Cathedral, Wilderness, Arcane, Mephisto, Kings}) {
                const Reverb b = Boost(r, echo);
                assert(b.room >= r.room && b.room <= 0 && b.decay >= r.decay && b.decay <= 20.0f);
                assert(Plausible(b));   // still numbers the game's setters accept
            }
        // More is never quieter than Game's own, Strong never quieter than More.
        for (const Reverb& r : {Cave, Cathedral, Wilderness, Arcane, Mephisto})
            assert(Boost(r, EchoMore).room >= Boost(r, EchoGame).room && Boost(r, EchoStrong).room >= Boost(r, EchoMore).room);
    }
    // Where the game turns the reverb off, it stays off.
    assert(Silent(Off) && Same(Boost(Off, EchoStrong), Off) && !Silent(Cave));
    // A decay the game's setter refuses (0: it keeps the preset's) is passed on as it is; the level still rises.
    {
        const Reverb k = Boost(Kings, EchoStrong);
        assert(k.decay == 0.0f && k.room == -300);
    }
    // Numbers that are not a TEAXINFO pass through untouched.
    {
        Reverb junk = Cave; junk.environment = 400;
        assert(!Plausible(junk) && Same(Boost(junk, EchoStrong), junk));
        junk = Cave; junk.room = 5;
        assert(!Plausible(junk) && Same(Boost(junk, EchoStrong), junk));
        junk = Cave; junk.decay = std::nanf("");
        assert(!Plausible(junk) && Boost(junk, EchoStrong).room == junk.room);
        junk = Cave; junk.reverbDelay = 3.0f;
        assert(!Plausible(junk));
        assert(Plausible(Cave) && Plausible(Off) && Plausible(Kings) && Plausible(Arcane));
    }

    // The setting: whole 0 - 2, anything else the default (More).
    assert(NormalizeEcho(0) == EchoGame && NormalizeEcho(1) == EchoMore && NormalizeEcho(2) == EchoStrong);
    assert(NormalizeEcho(3) == EchoDefault && NormalizeEcho(-1) == EchoDefault && NormalizeEcho(0.5f) == EchoDefault && NormalizeEcho(std::nanf("")) == EchoDefault);
    assert(EchoDefault == EchoMore && EchoGain(EchoGame) == 0 && EchoGain(EchoMore) == 600 && EchoGain(EchoStrong) == 1200);
    assert(std::string(EchoName(0)) == "Game's own" && std::string(EchoName(1)) == "More" && std::string(EchoName(2)) == "Strong");
    {
        const auto& d = st::Definitions[st::ReverbEcho];
        assert(st::ReverbEcho > st::TCrossCalibrate && st::ReverbEcho < st::Count);   // appended: saved profiles keep their keys
        assert(std::wstring(d.section) == L"audio" && std::wstring(d.key) == L"echo");
        assert(d.defaultValue == float(EchoDefault) && d.min == 0 && d.max == 2 && d.step == 1);
        assert(st::Normalize(st::ReverbEcho, 2) == 2 && st::Normalize(st::ReverbEcho, 0) == 0);
        assert(st::Normalize(st::ReverbEcho, 9) == float(EchoDefault) && st::Normalize(st::ReverbEcho, 1.5f) == float(EchoDefault));
        assert(st::HelpText(st::ReverbEcho)[0] != 0);
        const auto* row = st::content::RowOf(st::ReverbEcho);
        assert(row && row->page == st::View::Environment && row->optionCount == EchoCount && std::string(row->label) == "Echo");
        for (int i = 0; i < EchoCount; ++i) assert(std::string(row->options[i].label) == EchoName(i));   // the menu and the log use one name
    }

    // The log text.
    {
        const std::string same = Describe(Cave, Boost(Cave, EchoGame));
        assert(same == "preset 5, room -2000 mB, decay 3.40 s, tail +125 mB");
        const std::string more = Describe(Cave, Boost(Cave, EchoStrong));
        assert(more == "preset 5, room -2000 -> -800 mB, decay 3.40 -> 4.25 s, tail +125 mB");
        assert(Describe(Off, Off).find("reverb off here") != std::string::npos);
        Reverb junk = Cave; junk.environment = 400;
        assert(Describe(junk, junk).find("not understood") != std::string::npos);
    }
    // Channels: the last part of the name, the hero's footsteps apart from everyone else's.
    {
        assert(ShortChannel("sfx/ambient/footsteps-player") == "footsteps-player" && ShortChannel("master") == "master");
        assert(HeroSteps("sfx/ambient/footsteps-player") && !HeroSteps("sfx/ambient/footsteps_hd"));
        assert(OtherSteps("sfx/ambient/footsteps_hd") && OtherSteps("sfx/ambient/footsteps_sd") && !OtherSteps("sfx/ambient/footsteps-player") && !OtherSteps("sfx/monster_hd"));
        assert(ChannelName("sfx/eax/monster_neutral_hd", 26) && ChannelName("vo/eax/player-dialog-shout_hd", 29));
        assert(!ChannelName("", 0) && !ChannelName(nullptr, 3) && !ChannelName("Bad Name", 8) && !ChannelName("x\x01", 2));
        assert(CountsText({}) == "none" && CountsText({{"sfx/monster_hd", 0}}) == "none");
        assert(CountsText({{"sfx/monster_hd", 9}, {"sfx/ambient/footsteps-player", 14}, {"sfx/combat/impacts_hd", 9}}) ==
               "footsteps-player 14, impacts_hd 9, monster_hd 9");
        assert(CountsText({{"a", 5}, {"b", 4}, {"c", 3}, {"d", 2}}, 2) == "a 5, b 4 (+2 more channels, 5 sounds)");
        assert(CountsText({{"a", 5}, {"b", 1}}, 1) == "a 5 (+1 more channel, 1 sound)");
    }
    std::puts("world sound: ok");
    return 0;
}
