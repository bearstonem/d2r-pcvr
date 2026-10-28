#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "d2r_vr_state.h"

namespace framevr::gore {

enum class GoreType : uint8_t {
    None              = 0,
    SlashDecapitation = 1, // Sharp weapon lethal slash / decapitation
    BluntBoneCrush    = 2, // Blunt weapon heavy skull / bone crushing impact
    PiercingArterial  = 3, // Puncture / arrow / javelin deep strike
    CorpseExplosion   = 4, // Necromancer corpse detonation
    ColdShatter       = 5, // Frozen enemy shattered into crystalline chunks
    PoisonBurst       = 6, // Acidic / poison bile explosion
    GenericLethal     = 7  // Other fatal blow
};

enum class BloodTaxonomy : uint8_t {
    DemonIchor   = 0, // Dark black / tar-like demonic ichor
    UndeadRot    = 1, // Grey / yellowish decayed rot and bone dust
    BeastCrimson = 2, // Vivid red animal / beast blood
    InsectBile   = 3  // Acidic green / lime bile
};

struct GoreEvent {
    uint64_t timestamp = 0;       // System timestamp (GetTickCount64)
    uint32_t monsterId = 0;       // D2 monster unit ID (dwUnitId)
    uint32_t classId = 0;         // D2 monster class ID (MonStats row)
    GoreType type = GoreType::None;
    BloodTaxonomy taxonomy = BloodTaxonomy::DemonIchor;
    int hand = 0;                 // 0 = right hand, 1 = left hand
    bool lethal = false;          // True if the blow was fatal
    bool critical = false;        // True if critical strike / crushing blow
    float distanceSubTiles = 0.f; // Distance in game sub-tiles (5 sub-tiles = 1 tile)
    float worldPos[3] = {0,0,0};  // Game world coordinates
    float impactNormal[3] = {0,0,0}; // Strike impulse direction vector
};

inline bool IsCuttingWeapon(uint32_t weaponType) {
    switch (weaponType) {
        case D2RVR_TYPE_SWORD:
        case D2RVR_TYPE_AXE:
        case D2RVR_TYPE_DAGGER:
        case D2RVR_TYPE_POLEARM:
        case D2RVR_TYPE_CLAW:
            return true;
        default:
            return false;
    }
}

inline bool IsBluntWeapon(uint32_t weaponType) {
    switch (weaponType) {
        case D2RVR_TYPE_MACE:
        case D2RVR_TYPE_SCEPTER:
        case D2RVR_TYPE_STAFF:
        case D2RVR_TYPE_WAND:
        case D2RVR_TYPE_UNARMED:
            return true;
        default:
            return false;
    }
}

inline BloodTaxonomy TaxonomyForMonster(uint32_t classId) {
    // Skeletons, Zombies, Mummies, Vampires, Reanimated
    if ((classId <= 9) || (classId >= 96 && classId <= 105) || (classId >= 131 && classId <= 135) ||
        (classId >= 170 && classId <= 174) || (classId >= 274 && classId <= 277) ||
        (classId >= 575 && classId <= 579) || (classId >= 620 && classId <= 626) ||
        (classId >= 648 && classId <= 649) || (classId >= 667 && classId <= 670) ||
        (classId >= 695 && classId <= 703) || (classId >= 725 && classId <= 731)) {
        return BloodTaxonomy::UndeadRot;
    }
    // Sand Maggots, Giant Spiders, Mosquitoes, Swarms, Scarabs
    if ((classId >= 68 && classId <= 72) || (classId >= 87 && classId <= 95) ||
        (classId >= 114 && classId <= 117) || (classId >= 122 && classId <= 126) ||
        (classId >= 180 && classId <= 184) || (classId >= 190 && classId <= 194) ||
        classId == 227 || classId == 283 || (classId >= 284 && classId <= 288) ||
        (classId >= 551 && classId <= 555) || (classId >= 654 && classId <= 655) ||
        (classId >= 679 && classId <= 681) || classId == 685 || classId == 715 ||
        classId == 716 || classId == 717 || classId == 718) {
        return BloodTaxonomy::InsectBile;
    }
    // Beasts, Animals, Goatmen, Brutes, Yetis, Baboons
    if ((classId >= 10 && classId <= 14) || (classId >= 24 && classId <= 28) ||
        (classId >= 48 && classId <= 57) || (classId >= 63 && classId <= 67) ||
        (classId >= 78 && classId <= 86) || (classId >= 127 && classId <= 130) ||
        (classId >= 186 && classId <= 189) || (classId >= 501 && classId <= 505) ||
        (classId >= 580 && classId <= 589) || (classId >= 604 && classId <= 607) ||
        (classId >= 609 && classId <= 612) || (classId >= 618 && classId <= 619) ||
        (classId >= 627 && classId <= 630) || (classId >= 665 && classId <= 666)) {
        return BloodTaxonomy::BeastCrimson;
    }
    // Default to Demonic Ichor for Fallens, Shamans, Corrupt Rogues, Succubus, Doom Knights, Megademons, Overlords, Bosses
    return BloodTaxonomy::DemonIchor;
}

// Returns true if the class ID represents an Act Boss or major Quest Boss (never decapitable)
inline bool IsBossClass(uint32_t classId) {
    if (classId == 156 || classId == 707 ||                  // Andariel / Uber Andariel
        classId == 211 || classId == 708 ||                  // Duriel / Uber Duriel
        classId == 242 || classId == 333 || classId == 401 || classId == 704 || // Mephisto / Clone / Uber
        classId == 243 || classId == 705 ||                  // Diablo / Uber Diablo
        classId == 543 || classId == 544 || classId == 545 || classId == 557 || classId == 559 || classId == 570 || classId == 709 || // Baal forms
        (classId >= 562 && classId <= 566) ||                // Baal tentacles
        classId == 229 ||                                    // Radament
        classId == 250 ||                                    // The Summoner
        classId == 256 || classId == 406 || classId == 706 || // Izual / Ghost / Uber
        classId == 409 ||                                    // Hephasto the Armorer
        classId == 514 || classId == 526 ||                  // Nihlathak
        (classId >= 537 && classId <= 542)) {                // Ancients (statues + barbs)
        return true;
    }
    return false;
}

// Returns true if the class ID represents a non-humanoid creature (worms, spiders, beetles, snakes, swarms, bats, ghosts, traps)
inline bool IsNonHumanoidClass(uint32_t classId) {
    // Worms, maggots, larvae
    if ((classId >= 68 && classId <= 72) || (classId >= 180 && classId <= 184) ||
        (classId >= 190 && classId <= 194) || classId == 227 || classId == 283 ||
        (classId >= 284 && classId <= 288) || (classId >= 551 && classId <= 555) ||
        (classId >= 679 && classId <= 681) || classId == 716) return true;
    // Spiders, arachnids
    if ((classId >= 122 && classId <= 126) || classId == 685 || classId == 717) return true;
    // Scarabs, beetles, bugs
    if ((classId >= 91 && classId <= 95) || (classId >= 654 && classId <= 655) || classId == 718) return true;
    // Claw vipers, snakes
    if ((classId >= 73 && classId <= 77) || (classId >= 594 && classId <= 598)) return true;
    // Swarms, mosquitoes
    if ((classId >= 87 && classId <= 90) || (classId >= 114 && classId <= 117) || classId == 715) return true;
    // Birds, vultures, foul crows
    if ((classId >= 15 && classId <= 18) || (classId >= 110 && classId <= 113) ||
        classId == 157 || classId == 158 || (classId >= 206 && classId <= 209) ||
        (classId >= 590 && classId <= 593) || classId == 608 || classId == 738) return true;
    // Bats
    if ((classId >= 136 && classId <= 140) || classId == 159 || (classId >= 650 && classId <= 651)) return true;
    // Incorporeal ghosts, wraiths, will-o-wisps
    if ((classId >= 38 && classId <= 42) || (classId >= 118 && classId <= 121) ||
        (classId >= 631 && classId <= 633) || (classId >= 639 && classId <= 641) ||
        classId == 732 || classId == 733) return true;
    // Finger mages (spirits)
    if (classId >= 692 && classId <= 693) return true;
    // Tentacles, water watchers
    if (classId >= 258 && classId <= 263) return true;
    // Traps, siege engines, barrels, doors
    if ((classId >= 230 && classId <= 234) || classId == 273 || (classId >= 484 && classId <= 491) ||
        (classId >= 497 && classId <= 500) || (classId >= 516 && classId <= 519) ||
        (classId >= 524 && classId <= 525) || classId == 528 || classId == 574 ||
        classId == 710 || classId == 711) return true;
    // Passive town critters, animals
    if ((classId >= 149 && classId <= 151) || classId == 179 || classId == 185 ||
        classId == 556 || (classId >= 734 && classId <= 737)) return true;
    return false;
}

// Decapitation is strictly allowed only for standard humanoid / biped enemies
inline bool IsDecapitableClass(uint32_t classId) {
    return !IsBossClass(classId) && !IsNonHumanoidClass(classId);
}

inline GoreType ClassifyMeleeGore(uint32_t weaponType, bool lethal, bool critical) {
    if (!lethal && !critical) return GoreType::None;
    if (IsCuttingWeapon(weaponType)) {
        return GoreType::SlashDecapitation;
    }
    if (IsBluntWeapon(weaponType)) {
        return GoreType::BluntBoneCrush;
    }
    if (weaponType == D2RVR_TYPE_BOW || weaponType == D2RVR_TYPE_CROSSBOW ||
        weaponType == D2RVR_TYPE_SPEAR || weaponType == D2RVR_TYPE_JAVELIN) {
        return GoreType::PiercingArterial;
    }
    return lethal ? GoreType::GenericLethal : GoreType::None;
}

// Thread-safe lock-free circular queue for IPC / inter-thread event handoff
class EventQueue {
public:
    static constexpr size_t Capacity = 16;

    void Publish(const GoreEvent& ev) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        buffer[h % Capacity] = ev;
        head.store(h + 1, std::memory_order_release);
    }

    bool Take(GoreEvent& out) {
        uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t h = head.load(std::memory_order_acquire);
        if (t == h) return false;
        // In case of ring overflow, advance tail to latest available window
        if (h - t > Capacity) {
            t = h - Capacity;
        }
        out = buffer[t % Capacity];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }

    bool Peek(GoreEvent& out) const {
        uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t h = head.load(std::memory_order_acquire);
        if (t == h) return false;
        if (h - t > Capacity) t = h - Capacity;
        out = buffer[t % Capacity];
        return true;
    }

    // Finds the most recent event for a given hand within maxAgeMs
    bool LatestForHand(int hand, uint64_t now, uint64_t maxAgeMs, GoreEvent& out) {
        const uint32_t h = head.load(std::memory_order_acquire);
        uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == h) return false;
        if (h - t > Capacity) t = h - Capacity;

        for (uint32_t i = h; i > t; --i) {
            const auto& candidate = buffer[(i - 1) % Capacity];
            if (candidate.hand == hand && candidate.timestamp <= now && (now - candidate.timestamp) <= maxAgeMs) {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    // Finds the most recent decapitation event within maxAgeMs and maxDist of (x, z)
    bool FindRecentDecapitation(uint64_t now, uint64_t maxAgeMs, float x, float z, float maxDist, GoreEvent& out) const {
        const uint32_t h = head.load(std::memory_order_acquire);
        uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == h) return false;
        if (h - t > Capacity) t = h - Capacity;

        for (uint32_t i = h; i > t; --i) {
            const auto& candidate = buffer[(i - 1) % Capacity];
            if (candidate.type == GoreType::SlashDecapitation &&
                candidate.timestamp <= now && (now - candidate.timestamp) <= maxAgeMs) {
                const float dx = candidate.worldPos[0] - x;
                const float dz = candidate.worldPos[2] - z;
                if (std::hypot(dx, dz) <= maxDist) {
                    out = candidate;
                    return true;
                }
            }
        }
        return false;
    }

    // Finds and consumes the most recent decapitation event within maxAgeMs and maxDist of (x, z).
    // Consuming ensures only ONE monster skeleton claims the decapitation, preventing nearby living monsters from losing heads.
    bool ConsumeRecentDecapitation(uint64_t now, uint64_t maxAgeMs, float x, float z, float maxDist, GoreEvent& out) {
        const uint32_t h = head.load(std::memory_order_acquire);
        uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == h) return false;
        if (h - t > Capacity) t = h - Capacity;

        for (uint32_t i = h; i > t; --i) {
            auto& candidate = buffer[(i - 1) % Capacity];
            if (candidate.type == GoreType::SlashDecapitation &&
                candidate.timestamp <= now && (now - candidate.timestamp) <= maxAgeMs) {
                const float dx = candidate.worldPos[0] - x;
                const float dz = candidate.worldPos[2] - z;
                if (std::hypot(dx, dz) <= maxDist) {
                    out = candidate;
                    candidate.type = GoreType::None; // Atomically consumed
                    return true;
                }
            }
        }
        return false;
    }

    uint32_t Count() const {
        const uint32_t h = head.load(std::memory_order_acquire);
        const uint32_t t = tail.load(std::memory_order_relaxed);
        if (h <= t) return 0;
        return std::min<uint32_t>(h - t, Capacity);
    }

    void Clear() {
        const uint32_t h = head.load(std::memory_order_relaxed);
        tail.store(h, std::memory_order_release);
    }

private:
    std::array<GoreEvent, Capacity> buffer{};
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};
};

inline EventQueue& Queue() {
    static EventQueue s_queue;
    return s_queue;
}

inline void Publish(const GoreEvent& ev) {
    Queue().Publish(ev);
}

inline bool Take(GoreEvent& out) {
    return Queue().Take(out);
}

inline bool LatestForHand(int hand, uint64_t now, uint64_t maxAgeMs, GoreEvent& out) {
    return Queue().LatestForHand(hand, now, maxAgeMs, out);
}

inline void Reset() {
    Queue().Clear();
}

} // namespace framevr::gore

