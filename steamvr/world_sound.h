#pragma once
// World sound (vr/world_sound.cpp): the game's own room echo, made easier to hear in VR.
//
// D2R already plays an echo for the place you stand in. Every level names a sound
// environment (levels.txt SoundEnv -> soundenviron.txt), each with EAX reverb numbers for
// effects and for voices: a cave rings for 3.4 s, the Cathedral for 4.3 s, the moor for a
// quarter of a second. The game's channel tree (soundchannels.json) puts nearly every world
// sound under the two channels that carry the reverb, "sfx/eax" and "vo/eax": monsters,
// combat, footsteps, objects, items, skills, speech. Only the interface and music stay dry.
//
// What the game's numbers lack in a headset is level: the echo sits 12 to 29 dB under the
// sound that made it (Room -1250 to -2900 mB), tuned for speakers across a room. Echo
// raises the reverb's level (Room, the master of the early reflections and the tail) and,
// at Strong, lets the tail ring a quarter longer. Every other number stays the game's, so
// each place keeps its own character.
//
// This header is the arithmetic and the log text; nothing here touches the game.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace framevr::worldsound {

// D2SoundEAXSetParams' argument (TEAXINFO), as the game fills it from a soundenviron.txt
// row: levels in millibels, times in seconds (the table's milliseconds / 1000).
struct Reverb {
    int32_t environment;       // EAX preset the other numbers start from, 0 - 26
    int32_t room;              // the reverb's level, -10000 (silent) - 0 mB
    int32_t roomHf;            // its treble, -10000 - 0 mB
    float   decay;             // seconds, 0.1 - 20
    float   decayHf;           // treble decay ratio, 0.1 - 2
    int32_t reflections;       // early reflections against room, -10000 - 1000 mB
    float   reflectionsDelay;  // seconds, 0 - 0.3
    int32_t reverb;            // the tail against room, -10000 - 2000 mB
    float   reverbDelay;       // seconds, 0 - 0.1
};
static_assert(sizeof(Reverb) == 0x24, "the game's TEAXINFO is nine 4-byte fields");

// [audio] echo, VR Settings > Graphics > Environment > Echo.
enum Echo : int { EchoGame = 0, EchoMore = 1, EchoStrong = 2, EchoCount = 3 };
inline constexpr int EchoDefault = EchoMore;
inline int NormalizeEcho(float value) {
    if (!std::isfinite(value) || value != std::round(value) || value < 0 || value >= EchoCount) return EchoDefault;
    return int(value);
}
inline const char* EchoName(int echo) {
    return echo == EchoMore ? "More" : echo == EchoStrong ? "Strong" : "Game's own";
}
// Millibels added to Room, and what the decay time is multiplied by.
inline int   EchoGain(int echo)  { return echo == EchoMore ? 600 : echo == EchoStrong ? 1200 : 0; }
inline float EchoDecay(int echo) { return echo == EchoStrong ? 1.25f : 1.0f; }
// Room never goes above this: at 0 mB the echo is as loud as the sound itself.
inline constexpr int   RoomCeiling = -300;
// Under this the game means "no reverb here" (ESOUNDENVIRON_DISABLE_REVERB and the guild
// hall have -10000): left alone.
inline constexpr int   RoomSilent = -9000;
inline constexpr float DecayLongest = 8.0f;

// Numbers the game could have sent. Anything else (another build's structure, a bad
// pointer's bytes) passes through untouched.
inline bool Plausible(const Reverb& r) {
    const auto level = [](int32_t v, int32_t top) { return v >= -10000 && v <= top; };
    const auto time = [](float v, float top) { return std::isfinite(v) && v >= 0.0f && v <= top; };
    return r.environment >= 0 && r.environment <= 26 && level(r.room, 0) && level(r.roomHf, 0) &&
           level(r.reflections, 1000) && level(r.reverb, 2000) && time(r.decay, 20.0f) &&
           time(r.decayHf, 2.0f) && time(r.reflectionsDelay, 0.3f) && time(r.reverbDelay, 0.1f);
}
inline bool Silent(const Reverb& r) { return r.room <= RoomSilent; }

// The game's numbers with the echo raised. Game's own, implausible numbers and a silenced
// reverb come back as they are; a level never drops and never passes the ceiling.
inline Reverb Boost(const Reverb& game, int echo) {
    Reverb out = game;
    const int gain = EchoGain(echo);
    if (gain <= 0 || !Plausible(game) || Silent(game)) return out;
    out.room = std::max(game.room, std::min(game.room + gain, RoomCeiling));
    // The game's setter refuses a decay under 0.1 s (it keeps the preset's): left as it is.
    if (game.decay >= 0.1f) out.decay = std::max(game.decay, std::min(game.decay * EchoDecay(echo), DecayLongest));
    return out;
}

// "preset 5, room -2000 -> -1400 mB, decay 3.40 -> 3.40 s, tail +125 mB"
inline std::string Describe(const Reverb& game, const Reverb& sent) {
    char b[160];
    if (!Plausible(game)) { std::snprintf(b, sizeof b, "numbers not understood (preset %d, room %d), passed on untouched", game.environment, game.room); return b; }
    if (Silent(game)) { std::snprintf(b, sizeof b, "preset %d, reverb off here (room %d mB)", game.environment, game.room); return b; }
    if (game.room == sent.room && game.decay == sent.decay)
        std::snprintf(b, sizeof b, "preset %d, room %d mB, decay %.2f s, tail %+d mB", game.environment, game.room, game.decay, game.reverb);
    else
        std::snprintf(b, sizeof b, "preset %d, room %d -> %d mB, decay %.2f -> %.2f s, tail %+d mB", game.environment, game.room, sent.room, game.decay, sent.decay, game.reverb);
    return b;
}

// ---- which sounds the game started: a count per channel, for the log ----

// "sfx/ambient/footsteps-player" -> "footsteps-player": the last part of a channel's name
// says what it is, and the log line stays short.
inline std::string ShortChannel(const std::string& name) {
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}
// A channel name out of the game's table: lower-case path of letters, digits, / - _ .
inline bool ChannelName(const char* text, size_t length) {
    if (!text || length == 0 || length > 63) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}
// The hero's own footsteps, and everyone else's.
inline bool HeroSteps(const std::string& name) { return ShortChannel(name) == "footsteps-player"; }
inline bool OtherSteps(const std::string& name) { return ShortChannel(name).rfind("footsteps_", 0) == 0; }

// "footsteps-player 14, monster_hd 9, impacts_hd 4 (+2 more channels, 3 sounds)": the
// busiest channels first, ties by name, at most `most` of them.
inline std::string CountsText(std::vector<std::pair<std::string, int>> counts, size_t most = 8) {
    counts.erase(std::remove_if(counts.begin(), counts.end(), [](const auto& c) { return c.second <= 0; }), counts.end());
    std::sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    std::string out;
    int restChannels = 0, restSounds = 0;
    for (size_t i = 0; i < counts.size(); ++i) {
        if (i >= most) { ++restChannels; restSounds += counts[i].second; continue; }
        if (!out.empty()) out += ", ";
        out += ShortChannel(counts[i].first) + " " + std::to_string(counts[i].second);
    }
    if (restChannels) out += " (+" + std::to_string(restChannels) + " more channel" + (restChannels == 1 ? "" : "s") + ", " + std::to_string(restSounds) + " sound" + (restSounds == 1 ? "" : "s") + ")";
    return out.empty() ? "none" : out;
}

}   // namespace framevr::worldsound
