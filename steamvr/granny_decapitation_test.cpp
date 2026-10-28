#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "d2r_vr_gore.h"
#include "d2r_vr_decapitation.h"

namespace {

struct MockMatrix {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    static MockMatrix Translation(float x, float y, float z) {
        MockMatrix mat;
        mat.m[12] = x;
        mat.m[13] = y;
        mat.m[14] = z;
        return mat;
    }
};

void TestBoneIdentification() {
    printf("[granny-decapitation-test] Suite 1: Bone Identification & Hierarchy Parsing\n");

    // 1. Act I Fallen / Fallen Shaman rig
    {
        std::vector<std::string> names = {
            "root", "spine", "chest", "neck_M_JNT", "head_M_JNT", "jaw", "horn_L", "horn_R"
        };
        std::vector<int32_t> parents = {-1, 0, 1, 2, 3, 4, 4, 4};
        int head = framevr::decap::FindHeadBone(names, parents);
        int neck = framevr::decap::FindNeckBone(names, parents, head);
        assert(head == 4);
        assert(neck == 3);
        assert(names[head] == "head_M_JNT");
        assert(names[neck] == "neck_M_JNT");
    }

    // 2. Act I Zombie / Ghoul Biped rig
    {
        std::vector<std::string> names = {
            "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Neck", "Bip01 Head", "Head_End"
        };
        std::vector<int32_t> parents = {-1, 0, 1, 2, 3, 4};
        int head = framevr::decap::FindHeadBone(names, parents);
        int neck = framevr::decap::FindNeckBone(names, parents, head);
        assert(head == 4);
        assert(neck == 3);
        assert(names[head] == "Bip01 Head");
        assert(names[neck] == "Bip01 Neck");
    }

    // 3. Act I Skeleton warrior rig
    {
        std::vector<std::string> names = {
            "root", "spine_01", "neck", "head", "teeth"
        };
        std::vector<int32_t> parents = {-1, 0, 1, 2, 3};
        int head = framevr::decap::FindHeadBone(names, parents);
        int neck = framevr::decap::FindNeckBone(names, parents, head);
        assert(head == 3);
        assert(neck == 2);
        assert(names[head] == "head");
        assert(names[neck] == "neck");
    }

    // 4. Act I Corrupt Rogue rig (Hero-style Amazon bind joints)
    {
        std::vector<std::string> names = {
            "root", "spine_01_bind_jnt", "neck_bind_jnt", "head_bind_jnt", "hair_01", "hair_02"
        };
        std::vector<int32_t> parents = {-1, 0, 1, 2, 3, 4};
        int head = framevr::decap::FindHeadBone(names, parents);
        int neck = framevr::decap::FindNeckBone(names, parents, head);
        assert(head == 3);
        assert(neck == 2);
        assert(names[head] == "head_bind_jnt");
        assert(names[neck] == "neck_bind_jnt");
    }

    // 5. Werewolf / Werebear enemy rig
    {
        std::vector<std::string> names = {
            "root", "C_spine1", "C_Neck_neck1", "C_NeckHead_head", "snout"
        };
        std::vector<int32_t> parents = {-1, 0, 1, 2, 3};
        int head = framevr::decap::FindHeadBone(names, parents);
        int neck = framevr::decap::FindNeckBone(names, parents, head);
        assert(head == 3);
        assert(neck == 2);
        assert(names[head] == "C_NeckHead_head");
        assert(names[neck] == "C_Neck_neck1");
    }

    // 6. Assassin rig edge-case: joint "head_M_JNT" with a leaf dummy "head"
    {
        std::vector<std::string> names = {
            "root", "spine", "neck_main_00_M_JNT", "head_M_JNT", "head"
        };
        std::vector<int32_t> parents = {-1, 0, 1, 2, 3};
        int head = framevr::decap::FindHeadBone(names, parents);
        int neck = framevr::decap::FindNeckBone(names, parents, head);
        // Must select the main joint head_M_JNT (index 3), not the leaf dummy (index 4)
        assert(head == 3);
        assert(neck == 2);
    }

    // 7. Non-humanoid / headless entity (e.g. object or spider)
    {
        std::vector<std::string> names = {"root", "body", "leg1", "leg2"};
        std::vector<int32_t> parents = {-1, 0, 1, 1};
        int head = framevr::decap::FindHeadBone(names, parents);
        assert(head == -1);
    }
}

void TestDescendantHierarchyMask() {
    printf("[granny-decapitation-test] Suite 2: Descendant Hierarchy Mask Generation\n");

    // Full humanoid rig hierarchy:
    // 0: root
    // 1: spine (parent 0)
    // 2: chest (parent 1)
    // 3: neck (parent 2)
    // 4: head (parent 3)
    // 5: jaw (parent 4)
    // 6: horn_L (parent 4)
    // 7: horn_R (parent 4)
    // 8: eye_L (parent 4)
    // 9: shoulder_L (parent 2)
    // 10: arm_L (parent 9)
    // 11: hand_L (parent 10)
    // 12: shoulder_R (parent 2)
    // 13: arm_R (parent 12)
    // 14: hand_R (parent 13)
    // 15: pelvis (parent 0)
    // 16: leg_L (parent 15)
    // 17: leg_R (parent 15)
    const int count = 18;
    std::vector<int32_t> parents = {
        -1, 0, 1, 2, 3, 4, 4, 4, 4, 2, 9, 10, 2, 12, 13, 0, 15, 15
    };

    auto mask = framevr::decap::BuildDescendantMask(count, parents, 4); // root at head (4)

    assert(mask.size() == count);
    // Head and descendants must be 1
    assert(mask[4] == 1); // head
    assert(mask[5] == 1); // jaw
    assert(mask[6] == 1); // horn_L
    assert(mask[7] == 1); // horn_R
    assert(mask[8] == 1); // eye_L

    // Non-head bones must be 0
    assert(mask[0] == 0);  // root
    assert(mask[1] == 0);  // spine
    assert(mask[2] == 0);  // chest
    assert(mask[3] == 0);  // neck
    assert(mask[9] == 0);  // shoulder_L
    assert(mask[10] == 0); // arm_L
    assert(mask[11] == 0); // hand_L
    assert(mask[12] == 0); // shoulder_R
    assert(mask[13] == 0); // arm_R
    assert(mask[14] == 0); // hand_R
    assert(mask[15] == 0); // pelvis
    assert(mask[16] == 0); // leg_L
    assert(mask[17] == 0); // leg_R

    // Test BuildHeadMask where jaw / horns are direct children of neck (bone 3)
    {
        std::vector<std::string> rigNames = {
            "root", "spine", "chest", "neck_M_JNT", "head_M_JNT", "jaw", "horn_L", "horn_R", "shoulder_L"
        };
        // Note: jaw (5) and horn_L (6) have parent neck (3), while horn_R (7) has parent head (4)
        std::vector<int32_t> rigParents = {
            -1, 0, 1, 2, 3, 3, 3, 4, 2
        };
        auto headMask = framevr::decap::BuildHeadMask(9, rigNames, rigParents, 4, 3);
        assert(headMask[4] == 1); // head
        assert(headMask[5] == 1); // jaw (under neck, must be included in severed head)
        assert(headMask[6] == 1); // horn_L (under neck, must be included in severed head)
        assert(headMask[7] == 1); // horn_R (under head, included)
        assert(headMask[3] == 0); // neck stays on torso
        assert(headMask[2] == 0); // chest stays on torso
        assert(headMask[8] == 0); // shoulder stays on torso
    }
}

void TestMatrixCollapseMath() {
    printf("[granny-decapitation-test] Suite 3: Matrix Collapse Math\n");

    constexpr int count = 6;
    std::vector<MockMatrix> poses(count);

    // Bone 0: Root at (10, 0, 20)
    poses[0] = MockMatrix::Translation(10.0f, 0.0f, 20.0f);
    // Bone 1: Spine at (10, 1, 20)
    poses[1] = MockMatrix::Translation(10.0f, 1.0f, 20.0f);
    // Bone 2: Neck at (10, 1.5, 20)
    poses[2] = MockMatrix::Translation(10.0f, 1.5f, 20.0f);
    // Bone 3: Head at (10, 1.8, 20)
    poses[3] = MockMatrix::Translation(10.0f, 1.8f, 20.0f);
    // Bone 4: Horn at (10.2, 2.1, 20.1)
    poses[4] = MockMatrix::Translation(10.2f, 2.1f, 20.1f);
    // Bone 5: Arm at (10.5, 1.2, 20.0)
    poses[5] = MockMatrix::Translation(10.5f, 1.2f, 20.0f);

    std::vector<uint8_t> inHead(count, 0);
    inHead[3] = 1; // head
    inHead[4] = 1; // horn

    float outPivot[3] = {};
    framevr::decap::CollapseHeadBones(poses.data(), count, inHead, 3, 2, outPivot);

    // 1. Pivot must match neck translation
    assert(std::fabs(outPivot[0] - 10.0f) < 1e-4f);
    assert(std::fabs(outPivot[1] - 1.5f) < 1e-4f);
    assert(std::fabs(outPivot[2] - 20.0f) < 1e-4f);

    // 2. Neck bone must be completely untouched
    assert(std::fabs(poses[2].m[12] - 10.0f) < 1e-4f);
    assert(std::fabs(poses[2].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(poses[2].m[14] - 20.0f) < 1e-4f);
    assert(std::fabs(poses[2].m[0] - 1.0f) < 1e-4f);
    assert(std::fabs(poses[2].m[5] - 1.0f) < 1e-4f);
    assert(std::fabs(poses[2].m[10] - 1.0f) < 1e-4f);

    // 3. Head bone must have translation pinned to neck pivot and 3x3 scaled down to 0.0001
    assert(std::fabs(poses[3].m[12] - 10.0f) < 1e-4f);
    assert(std::fabs(poses[3].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(poses[3].m[14] - 20.0f) < 1e-4f);
    assert(std::fabs(poses[3].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[3].m[5] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[3].m[10] - 0.0001f) < 1e-6f);

    // 4. Horn bone must also be collapsed to neck pivot and scaled down
    assert(std::fabs(poses[4].m[12] - 10.0f) < 1e-4f);
    assert(std::fabs(poses[4].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(poses[4].m[14] - 20.0f) < 1e-4f);
    assert(std::fabs(poses[4].m[0] - 0.0001f) < 1e-6f);

    // 5. Spine and Arm must be completely untouched
    assert(std::fabs(poses[1].m[13] - 1.0f) < 1e-4f);
    assert(std::fabs(poses[1].m[0] - 1.0f) < 1e-4f);
    assert(std::fabs(poses[5].m[12] - 10.5f) < 1e-4f);
    assert(std::fabs(poses[5].m[0] - 1.0f) < 1e-4f);
}

void TestDecapitationManagerLifecycle() {
    printf("[granny-decapitation-test] Suite 4: Decapitation Manager Lifecycle & TTL\n");

    auto& mgr = framevr::decap::Manager();
    mgr.Clear();
    assert(mgr.Count() == 0);

    const uintptr_t skelA = 0x1000A0;
    const uintptr_t skelB = 0x1000B0;
    const uint64_t t0 = 10000;

    std::vector<uint8_t> inHead = {0, 0, 0, 1, 1}; // 5 bones, 3 & 4 in head

    // Register instance A
    bool reg = mgr.RegisterDecapitation(skelA, 42, 3, 2, inHead, t0, 60000);
    assert(reg);
    assert(mgr.Count() == 1);

    // At t0 + 1000ms: active
    assert(mgr.IsDecapitated(skelA, t0 + 1000));
    assert(!mgr.IsDecapitated(skelB, t0 + 1000));

    // Posing collapse test
    std::vector<MockMatrix> poses = {
        MockMatrix::Translation(0, 0, 0),
        MockMatrix::Translation(0, 1, 0),
        MockMatrix::Translation(0, 1.5f, 0),
        MockMatrix::Translation(0, 1.8f, 0),
        MockMatrix::Translation(0, 2.0f, 0)
    };
    float pivot[3] = {};
    bool applied = mgr.ApplyCollapse(skelA, poses.data(), 5, t0 + 5000, pivot);
    assert(applied);
    assert(std::fabs(pivot[1] - 1.5f) < 1e-4f);
    // Option C: Monster corpse has its head bone cleanly collapsed to 0.0001f scale at neck pivot (1.5f)
    assert(std::fabs(poses[3].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[3].m[13] - 1.5f) < 1e-4f);

    // And the decoupled severed head prop has completed ballistic flight and rests at ground level
    std::vector<framevr::decap::SeveredHeadProp> activeHeads;
    mgr.GetActiveSeveredHeads(activeHeads, t0 + 5000);
    assert(activeHeads.size() == 1);
    assert(activeHeads[0].worldPos[1] < 0.25f);
    assert(activeHeads[0].isResting);

    // At t0 + 30000ms: still active
    assert(mgr.IsDecapitated(skelA, t0 + 30000));

    // At t0 + 60001ms: expired
    assert(!mgr.IsDecapitated(skelA, t0 + 60001));

    // Prune
    mgr.PruneExpired(t0 + 60001);
    assert(mgr.Count() == 0);

    // Recycled skeleton address check: if a freed corpse's skeleton address is reused by a
    // newly spawned monster (with a different ECS entity or incremented entity version),
    // IsDecapitated(self, currentEntity, now) must reject it and drop the stale entry.
    const uint32_t corpseEntity = 0x0010002Au; // version 1, index 0x2A
    const uint32_t reusedSlotEntity = 0x0020002Au; // version 2, index 0x2A
    assert(mgr.RegisterDecapitation(skelA, corpseEntity, 42, 3, 2, inHead, t0, 0.0f, nullptr));
    uint32_t trackedEntity = 0;
    assert(mgr.IsDecapitated(skelA, t0 + 500, &trackedEntity));
    assert(trackedEntity == corpseEntity);
    assert(mgr.IsDecapitated(skelA, corpseEntity, t0 + 500));
    assert(mgr.Count() == 1);

    // Same skeleton address, but now owned by a new ECS entity: rejected and dropped
    assert(!mgr.IsDecapitated(skelA, reusedSlotEntity, t0 + 600));
    assert(mgr.Count() == 0);
    assert(!mgr.IsDecapitated(skelA, t0 + 600));

    mgr.Clear();
}

void TestEventQueueDecapitationMatching() {
    printf("[granny-decapitation-test] Suite 5: Event Queue Spatial Decapitation Matching\n");

    framevr::gore::EventQueue queue;
    queue.Clear();

    const uint64_t t0 = 5000;

    framevr::gore::GoreEvent ev{};
    ev.timestamp = t0;
    ev.monsterId = 101;
    ev.type = framevr::gore::GoreType::SlashDecapitation;
    ev.worldPos[0] = 20.0f;
    ev.worldPos[1] = 0.0f;
    ev.worldPos[2] = 30.0f;
    queue.Publish(ev);

    assert(queue.Count() == 1);

    // 1. Exact / nearby match (0.5m away) within 500ms
    framevr::gore::GoreEvent matched{};
    bool found = queue.FindRecentDecapitation(t0 + 500, 1500, 20.3f, 30.4f, 2.5f, matched);
    assert(found);
    assert(matched.monsterId == 101);

    // 2. Distant entity (10m away) must not match
    framevr::gore::GoreEvent distant{};
    bool foundDistant = queue.FindRecentDecapitation(t0 + 500, 1500, 30.0f, 30.0f, 2.5f, distant);
    assert(!foundDistant);

    // 3. Stale event (> 1500ms) must not match
    framevr::gore::GoreEvent stale{};
    bool foundStale = queue.FindRecentDecapitation(t0 + 2000, 1500, 20.1f, 30.1f, 2.5f, stale);
    assert(!foundStale);

    // 4. ConsumeRecentDecapitation: consumes the event so a second nearby monster does NOT match
    framevr::gore::GoreEvent consumed{};
    bool foundConsumed = queue.ConsumeRecentDecapitation(t0 + 500, 1500, 20.3f, 30.4f, 2.5f, consumed);
    assert(foundConsumed);
    assert(consumed.monsterId == 101);

    // Second monster evaluation immediately fails because the event was consumed
    framevr::gore::GoreEvent secondMonster{};
    bool secondMatch = queue.ConsumeRecentDecapitation(t0 + 500, 1500, 20.3f, 30.4f, 2.5f, secondMonster);
    assert(!secondMatch);
}


void TestSeveredHeadPhysicsTrajectory() {
    printf("[granny-decapitation-test] Suite 6: Severed Head Physics Trajectory & Floor Bounce\n");

    const float p0[3] = {10.0f, 1.8f, 20.0f};
    const float v0[3] = {1.2f, 1.5f, 0.0f};
    const float floorY = 0.0f;
    const float spinAxis[3] = {0.0f, 1.0f, 0.0f};
    const float spinSpeed = 6.0f;

    // 1. At t = 0: initial position
    {
        float pos[3] = {};
        float rot[9] = {};
        framevr::decap::SimulateHeadPhysics(0.0f, p0, v0, floorY, spinAxis, spinSpeed, pos, rot);
        assert(std::fabs(pos[0] - 10.0f) < 1e-4f);
        assert(std::fabs(pos[1] - 1.8f) < 1e-4f);
        assert(std::fabs(pos[2] - 20.0f) < 1e-4f);
    }

    // 2. At t = 0.15s: in flight, rising
    {
        float pos[3] = {};
        float rot[9] = {};
        framevr::decap::SimulateHeadPhysics(0.15f, p0, v0, floorY, spinAxis, spinSpeed, pos, rot);
        assert(pos[1] > 1.8f);
        assert(pos[0] > 10.0f);
    }

    // 3. At t = 5.0s: settled at floor + head radius
    {
        float pos[3] = {};
        float rot[9] = {};
        framevr::decap::SimulateHeadPhysics(5.0f, p0, v0, floorY, spinAxis, spinSpeed, pos, rot);
        assert(std::fabs(pos[1] - 0.12f) < 1e-3f);
        // Verify rotation matrix orthonormality (length of row 0 ~ 1.0)
        float lenSq = rot[0]*rot[0] + rot[1]*rot[1] + rot[2]*rot[2];
        assert(std::fabs(lenSq - 1.0f) < 1e-4f);
    }
}

void TestRigidHeadAndChildBonePreservation() {
    printf("[granny-decapitation-test] Suite 7: Rigid Head Rotation & Zero Child Bone Deformation\n");

    auto& mgr = framevr::decap::Manager();
    mgr.Clear();

    const uintptr_t skelPhys = 0x2000A0;
    const uintptr_t skelStump = 0x2000B0;
    const uint64_t t0 = 10000;
    const std::vector<uint8_t> inHead = {0, 0, 0, 1, 1}; // 5 bones: 0=root, 1=spine, 2=neck, 3=head, 4=jaw

    const float impulse[3] = {1.0f, 1.2f, 0.0f};

    mgr.RegisterDecapitation(skelPhys, 501, 3, 2, inHead, t0, 0.0f, impulse);
    mgr.RegisterDecapitation(skelStump, 502, 3, 2, inHead, t0, 0.0f, impulse);

    std::vector<MockMatrix> posesA = {
        MockMatrix::Translation(0, 0, 0),        // Root
        MockMatrix::Translation(0, 1.0f, 0),     // Spine
        MockMatrix::Translation(0, 1.5f, 0),     // Neck
        MockMatrix::Translation(0, 1.8f, 0),     // Head
        MockMatrix::Translation(0.15f, 1.7f, 0)  // Jaw: offset (0.15, -0.1, 0)
    };
    std::vector<MockMatrix> posesB = posesA;


    // First frame initialization at t0
    float pA[3] = {}, pB[3] = {};
    mgr.ApplyCollapse(skelPhys, posesA.data(), 5, t0, pA);
    mgr.ApplyCollapse(skelStump, posesB.data(), 5, t0, pB);

    // Advance 500 ms in physics flight
    mgr.ApplyCollapse(skelPhys, posesA.data(), 5, t0 + 500, pA);
    mgr.ApplyCollapse(skelStump, posesB.data(), 5, t0 + 500, pB);

    // Option C Verification:
    // 1. On the monster corpse skeleton: BOTH instances have head/jaw collapsed cleanly to 0.0001f scale
    // at the neck pivot (1.5m). Zero bones moving away = ZERO mesh stretching, ZERO rubber webbing!
    assert(std::fabs(posesA[3].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(posesA[3].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(posesA[4].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(posesA[4].m[13] - 1.5f) < 1e-4f);

    assert(std::fabs(posesB[3].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(posesB[3].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(posesB[4].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(posesB[4].m[13] - 1.5f) < 1e-4f);

    // 2. On the decoupled severed head 3D prop:
    // Published independently with realistic 3D ballistic trajectory and tumbling rotation
    std::vector<framevr::decap::SeveredHeadProp> activeHeads;
    mgr.GetActiveSeveredHeads(activeHeads, t0 + 500);
    assert(activeHeads.size() == 2);
    assert(activeHeads[0].worldPos[0] > 0.05f); // horizontal ballistic movement
    assert(activeHeads[0].worldPos[1] > 0.0f);  // in air above floor
    const float rotRow0Len = activeHeads[0].worldRot[0]*activeHeads[0].worldRot[0] +
                             activeHeads[0].worldRot[1]*activeHeads[0].worldRot[1] +
                             activeHeads[0].worldRot[2]*activeHeads[0].worldRot[2];
    assert(std::fabs(rotRow0Len - 1.0f) < 1e-4f); // orthonormal 3D tumbling rotation
    assert(activeHeads[0].scale == 1.0f);

    mgr.Clear();
}

void TestCleanStumpBoneCollapsing() {
    printf("[granny-decapitation-test] Suite 8: Clean Stump Bone Collapsing & Zero Mesh Deformation\n");

    auto& mgr = framevr::decap::Manager();
    mgr.Clear();

    const uintptr_t skel = 0x2000A0;
    const uint64_t t0 = 10000;
    // 5 bones: 0=root, 1=spine, 2=neck, 3=head, 4=jaw/horn (child of head)
    const std::vector<uint8_t> inHead = {0, 0, 0, 1, 1};

    mgr.RegisterDecapitation(skel, 501, 3, 2, inHead, t0, 0.0f, nullptr);

    std::vector<MockMatrix> poses = {
        MockMatrix::Translation(0, 0, 0),       // Root
        MockMatrix::Translation(0, 1.0f, 0),    // Spine
        MockMatrix::Translation(0, 1.5f, 0),    // Neck
        MockMatrix::Translation(0, 1.8f, 0),    // Head
        MockMatrix::Translation(0.2f, 2.0f, 0)  // Jaw/Horn
    };

    float outPivot[3] = {};
    bool applied = mgr.ApplyCollapse(skel, poses.data(), 5, t0 + 1000, outPivot);
    assert(applied);

    // 1. Pivot must be at neck stump
    assert(std::fabs(outPivot[0] - 0.0f) < 1e-4f);
    assert(std::fabs(outPivot[1] - 1.5f) < 1e-4f);
    assert(std::fabs(outPivot[2] - 0.0f) < 1e-4f);

    // 2. Unaffected bones (root, spine, neck) retain full uncollapsed identity/scale
    for (int i = 0; i <= 2; ++i) {
        assert(std::fabs(poses[i].m[0] - 1.0f) < 1e-4f);
        assert(std::fabs(poses[i].m[5] - 1.0f) < 1e-4f);
        assert(std::fabs(poses[i].m[10] - 1.0f) < 1e-4f);
    }
    assert(std::fabs(poses[2].m[13] - 1.5f) < 1e-4f); // Neck unchanged

    // 3. Head (bone 3) and Jaw/Horn (bone 4) must be collapsed to 0.0001f scale
    // and pinned exactly at the neck pivot to prevent skin mesh stretching
    assert(std::fabs(poses[3].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[3].m[5] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[3].m[10] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[3].m[12] - 0.0f) < 1e-4f);
    assert(std::fabs(poses[3].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(poses[3].m[14] - 0.0f) < 1e-4f);

    assert(std::fabs(poses[4].m[0] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[4].m[5] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[4].m[10] - 0.0001f) < 1e-6f);
    assert(std::fabs(poses[4].m[12] - 0.0f) < 1e-4f);
    assert(std::fabs(poses[4].m[13] - 1.5f) < 1e-4f);
    assert(std::fabs(poses[4].m[14] - 0.0f) < 1e-4f);

    mgr.Clear();
}

void TestMultipleConcurrentDecapitations() {
    printf("[granny-decapitation-test] Suite 9: Multiple Concurrent Decapitations & Capacity\n");

    auto& mgr = framevr::decap::Manager();
    mgr.Clear();

    const uint64_t t0 = 20000;
    const std::vector<uint8_t> inHead = {0, 0, 0, 1};

    // Register 16 decapitations (max capacity)
    for (int i = 0; i < 16; ++i) {
        uintptr_t skel = 0x300000 + (i * 0x100);
        bool reg = mgr.RegisterDecapitation(skel, 1000 + i, 3, 2, inHead, t0 + (i * 10));
        assert(reg);
    }
    assert(mgr.Count() == 16);

    // Register 17th
    uintptr_t skelNew = 0x399999;
    bool regNew = mgr.RegisterDecapitation(skelNew, 9999, 3, 2, inHead, t0 + 500);
    assert(regNew);
    assert(mgr.Count() == 17);
    assert(mgr.IsDecapitated(skelNew, t0 + 600));

    // Prune expired
    mgr.PruneExpired(t0 + 70000);
    assert(mgr.Count() == 0);

    mgr.Clear();
}

void TestHumanoidDecapitationFilteringAndTaxonomy() {
    printf("[granny-decapitation-test] Suite 10: Humanoid Decapitation Filtering & Taxonomy Verification\n");

    // 1. Humanoids that MUST decapitate:
    // Fallen Shamans (58..62, 645..647)
    for (uint32_t id : {58u, 59u, 60u, 61u, 62u, 645u, 646u, 647u}) {
        assert(framevr::gore::IsDecapitableClass(id));
        assert(framevr::gore::TaxonomyForMonster(id) == framevr::gore::BloodTaxonomy::DemonIchor);
    }
    // Corrupt Rogues / Archers / Lancers (43..47, 160..169, 613..617, 721..723)
    for (uint32_t id : {43u, 44u, 45u, 46u, 47u, 160u, 161u, 162u, 163u, 164u, 165u, 166u, 167u, 168u, 169u, 613u, 615u, 721u, 722u, 723u}) {
        assert(framevr::gore::IsDecapitableClass(id));
        assert(framevr::gore::TaxonomyForMonster(id) == framevr::gore::BloodTaxonomy::DemonIchor);
    }
    // Skeletons and Zombies
    for (uint32_t id : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 170u, 648u}) {
        assert(framevr::gore::IsDecapitableClass(id));
        assert(framevr::gore::TaxonomyForMonster(id) == framevr::gore::BloodTaxonomy::UndeadRot);
    }

    // 2. Bosses that must NEVER decapitate:
    for (uint32_t id : {156u, 707u, 211u, 708u, 242u, 704u, 243u, 705u, 543u, 544u, 545u, 570u, 709u, 229u, 250u, 256u, 409u, 514u, 526u, 537u, 540u, 562u}) {
        assert(!framevr::gore::IsDecapitableClass(id));
        assert(framevr::gore::IsBossClass(id));
    }

    // 3. Non-humanoids that must NEVER decapitate:
    for (uint32_t id : {68u, 70u, 180u, 190u, 283u, 122u, 685u, 91u, 73u, 87u, 114u, 15u, 110u, 136u, 38u, 118u, 258u}) {
        assert(!framevr::gore::IsDecapitableClass(id));
        assert(framevr::gore::IsNonHumanoidClass(id));
    }

    // 4. Model filtering
    assert(framevr::decap::IsDecapitableModel("data/hd/character/enemy/corruptrogue1/torso_med_lod0.model"));
    assert(framevr::decap::IsDecapitableModel("data/hd/character/enemy/cr_archer1/torso_med_lod0.model"));
    assert(framevr::decap::IsDecapitableModel("data/hd/character/enemy/fallenshaman1/torso_lod0.model"));
    assert(framevr::decap::IsDecapitableModel("data/hd/character/enemy/skeleton1/torso_lod0.model"));
    assert(framevr::decap::IsDecapitableModel("data/hd/character/enemy/zombie1/torso_lit_lod0.model"));
    assert(!framevr::decap::IsDecapitableModel("data/hd/character/enemy/andariel/torso_lod0.model"));
    assert(!framevr::decap::IsDecapitableModel("data/hd/character/enemy/duriel/torso_lod0.model"));
    assert(!framevr::decap::IsDecapitableModel("data/hd/character/player/barbarian/torso_lod0.model"));
    assert(!framevr::decap::IsDecapitableModel("data/hd/character/enemy/sandmaggot1/torso_lod0.model"));
    assert(!framevr::decap::IsDecapitableModel("data/hd/character/enemy/arach1/torso_lod0.model"));
}

} // namespace

int main() {
    printf("=== D2R VR Extra Gore - Granny 3D Decapitation Test Suite ===\n");
    TestBoneIdentification();
    TestDescendantHierarchyMask();
    TestMatrixCollapseMath();
    TestDecapitationManagerLifecycle();
    TestEventQueueDecapitationMatching();
    TestSeveredHeadPhysicsTrajectory();
    TestRigidHeadAndChildBonePreservation();
    TestCleanStumpBoneCollapsing();
    TestMultipleConcurrentDecapitations();
    TestHumanoidDecapitationFilteringAndTaxonomy();
    printf("=== ALL GRANNY DECAPITATION TESTS PASSED (10/10 SUITES) ===\n");
    return 0;
}

