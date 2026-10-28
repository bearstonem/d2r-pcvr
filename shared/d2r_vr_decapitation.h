#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace framevr::decap {

// Returns true if a model archive path represents a decapitable humanoid enemy
// Filters out player models, items, summons, act bosses, quest bosses, and non-humanoids
inline bool IsDecapitableModel(std::string_view model) {
    if (model.empty()) return false;
    std::string lo;
    lo.reserve(model.size());
    for (char c : model) lo.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));

    // 1. Never decapitate player characters, hirelings, summons, or items
    if (lo.find("/character/player/") != std::string::npos ||
        lo.find("/items/") != std::string::npos ||
        lo.find("hire") != std::string::npos ||
        lo.find("pet") != std::string::npos ||
        lo.find("valkyrie") != std::string::npos ||
        lo.find("golem") != std::string::npos ||
        lo.find("shadowmaster") != std::string::npos ||
        lo.find("shadowwarrior") != std::string::npos) {
        return false;
    }

    // 2. Never decapitate Act Bosses & major Quest Bosses (Andariel, Duriel, Mephisto, Diablo, Baal, etc.)
    if (lo.find("andariel") != std::string::npos ||
        lo.find("duriel") != std::string::npos ||
        lo.find("mephisto") != std::string::npos ||
        lo.find("diablo") != std::string::npos ||
        lo.find("baal") != std::string::npos ||
        lo.find("izual") != std::string::npos ||
        lo.find("radament") != std::string::npos ||
        lo.find("summoner") != std::string::npos ||
        lo.find("nihlathak") != std::string::npos ||
        lo.find("hephasto") != std::string::npos ||
        lo.find("ancientbarb") != std::string::npos ||
        lo.find("uberancient") != std::string::npos) {
        return false;
    }

    // 3. Never decapitate non-humanoids (worms, spiders, insects, beasts, snakes, ghosts, traps)
    if (lo.find("maggot") != std::string::npos ||
        lo.find("worm") != std::string::npos ||
        lo.find("larva") != std::string::npos ||
        lo.find("spider") != std::string::npos ||
        lo.find("arach") != std::string::npos ||
        lo.find("scarab") != std::string::npos ||
        lo.find("bug") != std::string::npos ||
        lo.find("beetle") != std::string::npos ||
        lo.find("scorpion") != std::string::npos ||
        lo.find("swarm") != std::string::npos ||
        lo.find("mosquito") != std::string::npos ||
        lo.find("clawviper") != std::string::npos ||
        lo.find("snake") != std::string::npos ||
        lo.find("vulture") != std::string::npos ||
        lo.find("bat") != std::string::npos ||
        lo.find("crow") != std::string::npos ||
        lo.find("bird") != std::string::npos ||
        lo.find("tentacle") != std::string::npos ||
        lo.find("defiler") != std::string::npos ||
        lo.find("vine") != std::string::npos ||
        lo.find("creeper") != std::string::npos ||
        lo.find("willowisp") != std::string::npos ||
        lo.find("wraith") != std::string::npos ||
        lo.find("fingermage") != std::string::npos ||
        lo.find("scimitar") != std::string::npos ||
        lo.find("door") != std::string::npos ||
        lo.find("wall") != std::string::npos ||
        lo.find("tower") != std::string::npos ||
        lo.find("catapult") != std::string::npos ||
        lo.find("trap") != std::string::npos ||
        lo.find("turret") != std::string::npos ||
        lo.find("spire") != std::string::npos ||
        lo.find("sentry") != std::string::npos ||
        lo.find("frog") != std::string::npos ||
        lo.find("chicken") != std::string::npos ||
        lo.find("bunny") != std::string::npos ||
        lo.find("pig") != std::string::npos ||
        lo.find("rat") != std::string::npos ||
        lo.find("horse") != std::string::npos ||
        lo.find("camel") != std::string::npos ||
        lo.find("cow") != std::string::npos) {
        return false;
    }

    return true;
}

// Checks if a bone name represents a head joint across common D2R and Biped rigs
inline bool IsHeadBoneName(std::string_view name) {
    if (name.empty()) return false;
    std::string lo;
    lo.reserve(name.size());
    for (char c : name) lo.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));

    // Exclusions for sub-parts or unrelated equipment
    if (lo.find("overhead") != std::string::npos ||
        lo.find("arrowhead") != std::string::npos ||
        lo.find("headband") != std::string::npos ||
        lo.find("necklace") != std::string::npos) {
        return false;
    }

    // High-priority joint names
    if (lo == "head_m_jnt" || lo == "head_bind_jnt" || lo == "c_neckhead_head" ||
        lo == "bip01 head" || lo == "bip01_head" || lo == "bip_head" ||
        lo == "head" || lo == "def_head" || lo == "j_head") {
        return true;
    }

    if (lo.find("head_") != std::string::npos ||
        lo.find("_head") != std::string::npos ||
        lo.find("bip01 head") != std::string::npos) {
        return true;
    }

    return false;
}

// Checks if a bone name represents a neck joint
inline bool IsNeckBoneName(std::string_view name) {
    if (name.empty()) return false;
    std::string lo;
    lo.reserve(name.size());
    for (char c : name) lo.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));

    if (lo.find("necklace") != std::string::npos) return false;

    if (lo == "neck" || lo == "neck_m_jnt" || lo == "neck_bind_jnt" ||
        lo == "bip01 neck" || lo == "bip01_neck" || lo == "bip_neck" ||
        lo == "c_neck_neck1" || lo == "c_neck_neck" || lo == "neck_01" ||
        lo == "def_neck" || lo == "j_neck") {
        return true;
    }

    if (lo.find("neck") != std::string::npos) return true;

    return false;
}

// Checks if a bone name represents a head or facial joint (jaw, chin, teeth, horns, eyes, snout)
inline bool IsHeadOrFacialBoneName(std::string_view name) {
    if (name.empty()) return false;
    std::string lo;
    lo.reserve(name.size());
    for (char c : name) lo.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));

    if (lo.find("necklace") != std::string::npos) return false;

    if (lo.find("jaw") != std::string::npos || lo.find("chin") != std::string::npos ||
        lo.find("teeth") != std::string::npos || lo.find("tooth") != std::string::npos ||
        lo.find("mouth") != std::string::npos || lo.find("tongue") != std::string::npos) {
        return true;
    }

    if (lo.find("horn") != std::string::npos || lo.find("ear") != std::string::npos ||
        lo.find("eye") != std::string::npos || lo.find("snout") != std::string::npos ||
        lo.find("face") != std::string::npos || lo.find("beard") != std::string::npos ||
        lo.find("hair") != std::string::npos || lo.find("helmet") != std::string::npos) {
        return true;
    }

    if (IsHeadBoneName(name)) return true;

    return false;
}

// Finds the primary head bone index in a bone hierarchy
inline int FindHeadBone(const std::vector<std::string>& names, const std::vector<int32_t>& parents) {
    (void)parents;
    const int count = static_cast<int>(names.size());

    // Pass 1: exact high-priority joint matches (prioritize parent joint over leaf dummies)
    for (int i = 0; i < count; ++i) {
        std::string lo;
        lo.reserve(names[i].size());
        for (char c : names[i]) lo.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
        if (lo == "head_m_jnt" || lo == "head_bind_jnt" || lo == "c_neckhead_head" ||
            lo == "bip01 head" || lo == "bip01_head" || lo == "bip_head") {
            return i;
        }
    }

    // Pass 2: exact "head"
    for (int i = 0; i < count; ++i) {
        std::string lo;
        lo.reserve(names[i].size());
        for (char c : names[i]) lo.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
        if (lo == "head") return i;
    }

    // Pass 3: general head names
    for (int i = 0; i < count; ++i) {
        if (IsHeadBoneName(names[i])) return i;
    }

    return -1;
}

// Finds the neck bone index supporting the given head bone
inline int FindNeckBone(const std::vector<std::string>& names, const std::vector<int32_t>& parents, int headBone) {
    const int count = static_cast<int>(names.size());
    if (headBone < 0 || headBone >= count) return -1;

    // Check immediate ancestors of headBone first (the actual supporting neck)
    for (int p = parents[headBone]; p >= 0 && p < count; p = parents[p]) {
        if (IsNeckBoneName(names[p])) return p;
    }

    // Pass 2: any bone containing neck that is not the head itself
    for (int i = 0; i < count; ++i) {
        if (i != headBone && IsNeckBoneName(names[i])) return i;
    }

    // Fallback: parent of headBone (spine/chest/clavicle)
    if (headBone >= 0 && headBone < count) {
        return parents[headBone];
    }

    return -1;
}

// Generates a boolean mask marking the root bone and all its descendants.
// In Granny 3D hierarchies, parent bones always precede their children (parent[i] < i).
inline std::vector<uint8_t> BuildDescendantMask(int count, const std::vector<int32_t>& parents, int rootBone) {
    std::vector<uint8_t> mask(count, 0);
    if (rootBone >= 0 && rootBone < count) {
        mask[rootBone] = 1;
        for (int i = rootBone + 1; i < count; ++i) {
            const int p = parents[i];
            if (p >= 0 && p < count && mask[p]) {
                mask[i] = 1;
            }
        }
    }
    return mask;
}

// Generates a boolean mask marking headBone, all its descendants, and facial features under neckBone.
// This prevents facial features (jaw, horns, chin, eyes) from remaining tethered to the neck stump.
inline std::vector<uint8_t> BuildHeadMask(int count, const std::vector<std::string>& names,
                                          const std::vector<int32_t>& parents, int headBone, int neckBone) {
    std::vector<uint8_t> mask(count, 0);
    if (headBone >= 0 && headBone < count) {
        mask[headBone] = 1;
        for (int i = 0; i < count; ++i) {
            for (int p = parents[i]; p >= 0 && p < count; p = parents[p]) {
                if (p == headBone) {
                    mask[i] = 1;
                    break;
                }
            }
        }

        for (int i = 0; i < count; ++i) {
            if (i == neckBone) continue;
            if (!mask[i] && i < static_cast<int>(names.size()) && IsHeadOrFacialBoneName(names[i])) {
                for (int p = parents[i]; p >= 0 && p < count; p = parents[p]) {
                    if (p == neckBone || p == headBone) {
                        mask[i] = 1;
                        break;
                    }
                }
            }
        }
    }
    return mask;
}

// 4x4 matrix helpers for exact rigid kinematics (Granny 3D row-vector convention: v' = v * M)
struct Mat4 {
    float m[16] = {};

    static Mat4 Identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

inline Mat4 Mul4(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 4; ++k) {
            for (int j = 0; j < 4; ++j) {
                r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
        }
    }
    return r;
}

// Rigid inverse of 4x4 affine row-vector matrix: M^-1 = [ R^T, 0 ; -T * R^T, 1 ]
inline Mat4 RigidInverse(const Mat4& a) {
    Mat4 inv{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            inv.m[r * 4 + c] = a.m[c * 4 + r];
        }
    }
    inv.m[15] = 1.0f;
    const float tx = a.m[12], ty = a.m[13], tz = a.m[14];
    inv.m[12] = -(tx * inv.m[0] + ty * inv.m[4] + tz * inv.m[8]);
    inv.m[13] = -(tx * inv.m[1] + ty * inv.m[5] + tz * inv.m[9]);
    inv.m[14] = -(tx * inv.m[2] + ty * inv.m[6] + tz * inv.m[10]);
    return inv;
}

// Collapses head and descendant bones into the neck pivot point (clean stump cut).
// Matrix16 requires float m[16] row-vector layout (standard Granny 3D world pose).
template <typename Matrix16>
inline void CollapseHeadBones(Matrix16* worldPoses, int boneCount,
                              const std::vector<uint8_t>& inHead,
                              int headBone, int neckBone,
                              float outPivot[3] = nullptr) {
    if (!worldPoses || headBone < 0 || headBone >= boneCount) return;

    // Determine pivot point: neck bone position if valid, else head bone position
    const int pivotBone = (neckBone >= 0 && neckBone < boneCount) ? neckBone : headBone;
    const float px = worldPoses[pivotBone].m[12];
    const float py = worldPoses[pivotBone].m[13];
    const float pz = worldPoses[pivotBone].m[14];

    if (outPivot) {
        outPivot[0] = px;
        outPivot[1] = py;
        outPivot[2] = pz;
    }

    const size_t n = std::min<size_t>(static_cast<size_t>(boneCount), inHead.size());
    for (size_t i = 0; i < n; ++i) {
        if (inHead[i]) {
            auto& mat = worldPoses[i];
            for (int k = 0; k < 16; ++k) mat.m[k] = 0.0f;
            mat.m[0]  = 0.0001f;
            mat.m[5]  = 0.0001f;
            mat.m[10] = 0.0001f;
            mat.m[15] = 1.0f;

            mat.m[12] = px;
            mat.m[13] = py;
            mat.m[14] = pz;
        }
    }
}

// Simulates 3D ballistic trajectory, multi-phase floor bounces, horizontal drag, surface roll, and tumbling rotation.
inline void SimulateHeadPhysics(float t, const float p0[3], const float v0[3],
                                float floorY, const float spinAxis[3], float spinSpeed,
                                float outPos[3], float outRot[9]) {
    constexpr float g = 9.81f;
    constexpr float r = 0.12f; // approximate head radius
    const float groundY = floorY + r;

    // Time to first ground impact: 1/2 g t^2 - v0_y t - h0 = 0
    const float h0 = std::max(0.01f, p0[1] - groundY);
    const float disc1 = v0[1] * v0[1] + 2.0f * g * h0;
    const float t1 = (v0[1] + std::sqrt(std::max(0.0f, disc1))) / g;

    // First bounce: restitution e1 = 0.42
    constexpr float e1 = 0.42f;
    const float vImp1 = std::sqrt(std::max(0.0f, disc1)); // downward velocity at impact
    const float vBounce1 = std::max(0.0f, vImp1 * e1);
    const float dtBounce1 = (2.0f * vBounce1) / g;
    const float t2 = t1 + dtBounce1;

    // Second bounce: restitution e2 = 0.35
    constexpr float e2 = 0.35f;
    const float vBounce2 = vBounce1 * e2;
    const float dtBounce2 = (2.0f * vBounce2) / g;
    const float t3 = t2 + dtBounce2;

    // Vertical position calculation
    float py = groundY;
    if (t < t1) {
        py = p0[1] + v0[1] * t - 0.5f * g * t * t;
    } else if (t < t2) {
        const float tb = t - t1;
        py = groundY + vBounce1 * tb - 0.5f * g * tb * tb;
    } else if (t < t3) {
        const float tb = t - t2;
        py = groundY + vBounce2 * tb - 0.5f * g * tb * tb;
    } else {
        py = groundY;
    }
    py = std::max(groundY, py);

    // Multi-phase horizontal trajectory:
    // Phase 1 (in air: t < t1): very low air drag (mu_air = 0.25)
    constexpr float muAir = 0.25f;
    const float dist1 = (std::fabs(muAir) > 1e-4f) ? (1.0f - std::exp(-muAir * t1)) / muAir : t1;
    const float v1x = v0[0] * std::exp(-muAir * t1) * 0.72f; // ground friction at impact 1
    const float v1z = v0[2] * std::exp(-muAir * t1) * 0.72f;

    // Phase 2 (hop 1: t1 <= t < t2)
    const float dt1 = t2 - t1;
    const float dist2 = (std::fabs(muAir) > 1e-4f) ? (1.0f - std::exp(-muAir * dt1)) / muAir : dt1;
    const float v2x = v1x * std::exp(-muAir * dt1) * 0.65f; // ground friction at impact 2
    const float v2z = v1z * std::exp(-muAir * dt1) * 0.65f;

    // Phase 3 (hop 2: t2 <= t < t3)
    const float dt2 = t3 - t2;
    const float dist3 = (std::fabs(muAir) > 1e-4f) ? (1.0f - std::exp(-muAir * dt2)) / muAir : dt2;
    const float v3x = v2x * std::exp(-muAir * dt2) * 0.60f; // velocity entering ground roll
    const float v3z = v2z * std::exp(-muAir * dt2) * 0.60f;

    float px = p0[0];
    float pz = p0[2];

    if (t < t1) {
        const float f = (1.0f - std::exp(-muAir * t)) / muAir;
        px += v0[0] * f;
        pz += v0[2] * f;
    } else if (t < t2) {
        const float tb = t - t1;
        const float f = (1.0f - std::exp(-muAir * tb)) / muAir;
        px += v0[0] * dist1 + v1x * f;
        pz += v0[2] * dist1 + v1z * f;
    } else if (t < t3) {
        const float tb = t - t2;
        const float f = (1.0f - std::exp(-muAir * tb)) / muAir;
        px += v0[0] * dist1 + v1x * dist2 + v2x * f;
        pz += v0[2] * dist1 + v1z * dist2 + v2z * f;
    } else {
        // Phase 4 (ground rolling until natural rest: mu_roll = 1.6)
        constexpr float muRoll = 1.6f;
        const float tb = t - t3;
        const float f = (1.0f - std::exp(-muRoll * tb)) / muRoll;
        px += v0[0] * dist1 + v1x * dist2 + v2x * dist3 + v3x * f;
        pz += v0[2] * dist1 + v1z * dist2 + v2z * dist3 + v3z * f;
    }

    outPos[0] = px;
    outPos[1] = py;
    outPos[2] = pz;

    // Dynamic angular rotation:
    // In air: free tumble (spinSpeed * t)
    // On ground: rolling smoothly matching floor travel
    float totalAngle = 0.0f;
    if (t < t3) {
        totalAngle = spinSpeed * t;
    } else {
        const float rollTime = (1.0f - std::exp(-1.6f * (t - t3))) / 1.6f;
        const float rollSpeed = std::hypot(v3x, v3z);
        const float rollDist = rollSpeed * rollTime;
        totalAngle = spinSpeed * t3 + (rollDist / r);
    }

    const float c = std::cos(totalAngle);
    const float s = std::sin(totalAngle);
    const float k = 1.0f - c;
    const float x = spinAxis[0], y = spinAxis[1], z = spinAxis[2];

    outRot[0] = c + x * x * k;
    outRot[1] = x * y * k - z * s;
    outRot[2] = x * z * k + y * s;

    outRot[3] = y * x * k + z * s;
    outRot[4] = c + y * y * k;
    outRot[5] = y * z * k - x * s;

    outRot[6] = z * x * k - y * s;
    outRot[7] = z * y * k + x * s;
    outRot[8] = c + z * z * k;
}

// State of an active decapitated monster instance
struct DecapInstance {
    uintptr_t skeletonInstance = 0;
    uint32_t ecsEntity = 0; // Versioned ECS entity owning this skeleton instance
    uint32_t monsterId = 0;
    uint8_t taxonomy = 0; // 0 = Undead, 1 = Demon, 2 = Beast
    uint64_t decapitatedAt = 0;
    uint64_t expiresAt = 0;
    int headBone = -1;
    int neckBone = -1;
    std::vector<uint8_t> inHead;
    float neckPivot[3] = {};
    float headInitialPos[3] = {};
    Mat4 headInitialMat{};
    std::vector<Mat4> childLocalTransforms;
    float velocity[3] = {};
    float spinAxis[3] = {0.0f, 1.0f, 0.0f};
    float spinSpeed = 0.0f;
    float floorY = 0.0f;
    bool initialized = false;
    float monsterWorldPos[3] = {};
    float currentHeadWorldPos[3] = {};
    float currentHeadRot[9] = {};
    bool isResting = false;
};

// Decoupled 3D physical severed head / skull prop published for independent rendering
struct SeveredHeadProp {
    uint32_t monsterId = 0;
    uint8_t taxonomy = 0; // 0 = Undead, 1 = Demon, 2 = Beast
    float worldPos[3] = {};
    float worldRot[9] = {};
    float neckStumpPos[3] = {};
    float velocity[3] = {};
    float elapsedSeconds = 0.0f;
    bool isResting = false;
    float scale = 1.0f;
};

// Thread-safe manager for tracked decapitations in Granny 3D
class DecapitationManager {
public:
    static constexpr uint64_t kDefaultTtlMs = 60000; // 60 seconds (covers death anim + corpse persistence)
    static constexpr size_t kMaxInstances = 512;

    bool RegisterDecapitation(uintptr_t self, uint32_t ecsEntity, uint32_t monsterId, int headBone, int neckBone,
                              const std::vector<uint8_t>& inHead, uint64_t now,
                              float floorY, const float* impulse,
                              uint8_t taxonomy = 0,
                              const float* monsterWorldPos = nullptr,
                              uint64_t ttlMs = kDefaultTtlMs) {
        if (!self || headBone < 0) return false;
        std::unique_lock lock(mutex_);

        if (instances_.size() >= kMaxInstances) {
            PruneExpiredLocked(now);
            if (instances_.size() >= kMaxInstances) {
                instances_.clear();
            }
        }

        DecapInstance inst{};
        inst.skeletonInstance = self;
        inst.ecsEntity = ecsEntity;
        inst.monsterId = monsterId;
        inst.taxonomy = taxonomy;
        inst.decapitatedAt = now;
        inst.expiresAt = now + ttlMs;
        inst.headBone = headBone;
        inst.neckBone = neckBone;
        inst.inHead = inHead;
        inst.floorY = floorY;

        if (monsterWorldPos) {
            inst.monsterWorldPos[0] = monsterWorldPos[0];
            inst.monsterWorldPos[1] = monsterWorldPos[1];
            inst.monsterWorldPos[2] = monsterWorldPos[2];
        }

        if (impulse && (std::fabs(impulse[0]) > 1e-4f || std::fabs(impulse[2]) > 1e-4f)) {
            inst.velocity[0] = impulse[0];
            inst.velocity[1] = (impulse[1] > 0.5f) ? impulse[1] : 1.8f;
            inst.velocity[2] = impulse[2];
        } else {
            inst.velocity[0] = 1.6f;
            inst.velocity[1] = 1.8f;
            inst.velocity[2] = 1.6f;
        }

        const float vLenSq = inst.velocity[0] * inst.velocity[0] + inst.velocity[2] * inst.velocity[2];
        if (vLenSq > 1e-4f) {
            const float vLen = std::sqrt(vLenSq);
            inst.spinAxis[0] = -inst.velocity[2] / vLen;
            inst.spinAxis[1] = 0.25f;
            inst.spinAxis[2] = inst.velocity[0] / vLen;
            const float sLen = std::hypot(inst.spinAxis[0], std::hypot(inst.spinAxis[1], inst.spinAxis[2]));
            if (sLen > 1e-4f) {
                inst.spinAxis[0] /= sLen;
                inst.spinAxis[1] /= sLen;
                inst.spinAxis[2] /= sLen;
            }
        } else {
            inst.spinAxis[0] = 0.0f; inst.spinAxis[1] = 1.0f; inst.spinAxis[2] = 0.0f;
        }
        inst.spinSpeed = 8.5f;

        instances_[self] = std::move(inst);
        return true;
    }

    template <typename TaxType>
    bool RegisterDecapitation(uintptr_t self, uint32_t ecsEntity, uint32_t monsterId, int headBone, int neckBone,
                              const std::vector<uint8_t>& inHead, uint64_t now,
                              float floorY, const float* impulse,
                              TaxType taxonomy,
                              const float* monsterWorldPos = nullptr,
                              uint64_t ttlMs = kDefaultTtlMs) {
        return RegisterDecapitation(self, ecsEntity, monsterId, headBone, neckBone, inHead, now, floorY, impulse, static_cast<uint8_t>(taxonomy), monsterWorldPos, ttlMs);
    }

    bool RegisterDecapitation(uintptr_t self, uint32_t monsterId, int headBone, int neckBone,
                              const std::vector<uint8_t>& inHead, uint64_t now,
                              float floorY, const float* impulse,
                              uint8_t taxonomy = 0,
                              const float* monsterWorldPos = nullptr,
                              uint64_t ttlMs = kDefaultTtlMs) {
        return RegisterDecapitation(self, 0, monsterId, headBone, neckBone, inHead, now, floorY, impulse, taxonomy, monsterWorldPos, ttlMs);
    }

    bool RegisterDecapitation(uintptr_t self, uint32_t monsterId, int headBone, int neckBone,
                              const std::vector<uint8_t>& inHead, uint64_t now,
                              uint64_t ttlMs = kDefaultTtlMs) {
        return RegisterDecapitation(self, 0, monsterId, headBone, neckBone, inHead, now, 0.0f, nullptr, 0, nullptr, ttlMs);
    }

    bool IsDecapitated(uintptr_t self, uint64_t now, uint32_t* outEntity = nullptr) const {
        if (!self) return false;
        std::shared_lock lock(mutex_);
        auto it = instances_.find(self);
        if (it != instances_.end() && now < it->second.expiresAt) {
            if (outEntity) *outEntity = it->second.ecsEntity;
            return true;
        }
        return false;
    }

    // Verifies that the skeleton address still belongs to the same versioned ECS entity;
    // drops the entry and returns false if the corpse was freed and a new monster reused `self`.
    bool IsDecapitated(uintptr_t self, uint32_t currentEntity, uint64_t now) {
        if (!self) return false;
        std::unique_lock lock(mutex_);
        auto it = instances_.find(self);
        if (it == instances_.end()) return false;
        if (now >= it->second.expiresAt || (it->second.ecsEntity != 0 && it->second.ecsEntity != currentEntity)) {
            instances_.erase(it);
            return false;
        }
        return true;
    }

    template <typename Matrix16>
    bool ApplyCollapse(uintptr_t self, Matrix16* worldPoses, int boneCount, uint64_t now, float outPivot[3] = nullptr) {
        if (!self || !worldPoses || boneCount <= 0) return false;

        std::unique_lock lock(mutex_);
        auto it = instances_.find(self);
        if (it == instances_.end() || now >= it->second.expiresAt) {
            return false;
        }

        auto& inst = it->second;

        // Initialize initial pivot and capture relative child poses on first frame
        if (!inst.initialized) {
            const int neck = (inst.neckBone >= 0 && inst.neckBone < boneCount) ? inst.neckBone : inst.headBone;
            if (neck >= 0 && neck < boneCount) {
                inst.neckPivot[0] = worldPoses[neck].m[12];
                inst.neckPivot[1] = worldPoses[neck].m[13];
                inst.neckPivot[2] = worldPoses[neck].m[14];
            }

            const int head = (inst.headBone >= 0 && inst.headBone < boneCount) ? inst.headBone : inst.neckBone;
            if (head >= 0 && head < boneCount) {
                inst.headInitialPos[0] = worldPoses[head].m[12];
                inst.headInitialPos[1] = worldPoses[head].m[13];
                inst.headInitialPos[2] = worldPoses[head].m[14];

                for (int k = 0; k < 16; ++k) {
                    inst.headInitialMat.m[k] = worldPoses[head].m[k];
                }

                Mat4 invHead = RigidInverse(inst.headInitialMat);
                inst.childLocalTransforms.resize(boneCount);

                const size_t n = std::min<size_t>(static_cast<size_t>(boneCount), inst.inHead.size());
                for (size_t i = 0; i < n; ++i) {
                    if (inst.inHead[i] && int(i) != head) {
                        Mat4 childMat;
                        for (int k = 0; k < 16; ++k) childMat.m[k] = worldPoses[i].m[k];
                        inst.childLocalTransforms[i] = Mul4(childMat, invHead);
                    }
                }
            }

            // In model space, ground floor is at Y = 0.0f
            inst.floorY = 0.0f;
            inst.initialized = true;
        }

        if (outPivot) {
            outPivot[0] = inst.neckPivot[0];
            outPivot[1] = inst.neckPivot[1];
            outPivot[2] = inst.neckPivot[2];
        }

        // Option C: ALWAYS execute Clean Stump on the monster's skeleton!
        // All head and facial bones are collapsed to the neck pivot with 0.0001f scale.
        // This guarantees ZERO mesh stretching, ZERO rubber webbing, and ZERO deformation on the corpse.
        CollapseHeadBones(worldPoses, boneCount, inst.inHead, inst.headBone, inst.neckBone, nullptr);

        // Simulate cranial burst origin / trajectory
        const float t = (now > inst.decapitatedAt) ? float(now - inst.decapitatedAt) / 1000.0f : 0.0f;
        float headPos[3] = {};
        float headRot[9] = {};
        SimulateHeadPhysics(t, inst.headInitialPos, inst.velocity, inst.floorY,
                            inst.spinAxis, inst.spinSpeed, headPos, headRot);

        inst.currentHeadWorldPos[0] = inst.monsterWorldPos[0] + headPos[0];
        inst.currentHeadWorldPos[1] = inst.monsterWorldPos[1] + headPos[1];
        inst.currentHeadWorldPos[2] = inst.monsterWorldPos[2] + headPos[2];
        std::memcpy(inst.currentHeadRot, headRot, sizeof(headRot));
        inst.isResting = (t > 2.5f);

        return true;
    }

    void GetActiveSeveredHeads(std::vector<SeveredHeadProp>& out, uint64_t now) const {
        std::shared_lock lock(mutex_);
        out.clear();
        for (const auto& pair : instances_) {
            const auto& inst = pair.second;
            if (now < inst.expiresAt && inst.initialized) {
                SeveredHeadProp prop{};
                prop.monsterId = inst.monsterId;
                prop.taxonomy = inst.taxonomy;
                prop.worldPos[0] = inst.currentHeadWorldPos[0];
                prop.worldPos[1] = inst.currentHeadWorldPos[1];
                prop.worldPos[2] = inst.currentHeadWorldPos[2];
                std::memcpy(prop.worldRot, inst.currentHeadRot, sizeof(prop.worldRot));
                prop.neckStumpPos[0] = inst.monsterWorldPos[0] + inst.neckPivot[0];
                prop.neckStumpPos[1] = inst.monsterWorldPos[1] + inst.neckPivot[1];
                prop.neckStumpPos[2] = inst.monsterWorldPos[2] + inst.neckPivot[2];
                prop.velocity[0] = inst.velocity[0];
                prop.velocity[1] = inst.velocity[1];
                prop.velocity[2] = inst.velocity[2];
                prop.elapsedSeconds = (now > inst.decapitatedAt) ? float(now - inst.decapitatedAt) / 1000.0f : 0.0f;
                prop.isResting = inst.isResting;
                prop.scale = 1.0f;
                out.push_back(prop);
            }
        }
    }

    void PruneExpired(uint64_t now) {
        std::unique_lock lock(mutex_);
        PruneExpiredLocked(now);
    }

    void Clear() {
        std::unique_lock lock(mutex_);
        instances_.clear();
    }

    size_t Count() const {
        std::shared_lock lock(mutex_);
        return instances_.size();
    }

    bool GetInstance(uintptr_t self, DecapInstance& out) const {
        std::shared_lock lock(mutex_);
        auto it = instances_.find(self);
        if (it != instances_.end()) {
            out = it->second;
            return true;
        }
        return false;
    }

private:
    void PruneExpiredLocked(uint64_t now) {
        for (auto it = instances_.begin(); it != instances_.end();) {
            if (now >= it->second.expiresAt) {
                it = instances_.erase(it);
            } else {
                ++it;
            }
        }
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<uintptr_t, DecapInstance> instances_;
};

inline DecapitationManager& Manager() {
    static DecapitationManager s_manager;
    return s_manager;
}

} // namespace framevr::decap
