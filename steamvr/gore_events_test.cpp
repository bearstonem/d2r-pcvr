#include "../shared/d2r_vr_gore.h"
#include "haptic_strength.h"
#include <cassert>
#include <cstdio>
#include <vector>

namespace {

void TestWeaponClassification() {
    using namespace framevr::gore;

    // Cutting weapons
    assert(IsCuttingWeapon(D2RVR_TYPE_SWORD));
    assert(IsCuttingWeapon(D2RVR_TYPE_AXE));
    assert(IsCuttingWeapon(D2RVR_TYPE_DAGGER));
    assert(IsCuttingWeapon(D2RVR_TYPE_POLEARM));
    assert(IsCuttingWeapon(D2RVR_TYPE_CLAW));
    assert(!IsCuttingWeapon(D2RVR_TYPE_MACE));
    assert(!IsCuttingWeapon(D2RVR_TYPE_BOW));
    assert(!IsCuttingWeapon(D2RVR_TYPE_UNARMED));

    // Blunt weapons
    assert(IsBluntWeapon(D2RVR_TYPE_MACE));
    assert(IsBluntWeapon(D2RVR_TYPE_SCEPTER));
    assert(IsBluntWeapon(D2RVR_TYPE_STAFF));
    assert(IsBluntWeapon(D2RVR_TYPE_WAND));
    assert(IsBluntWeapon(D2RVR_TYPE_UNARMED));
    assert(!IsBluntWeapon(D2RVR_TYPE_SWORD));
    assert(!IsBluntWeapon(D2RVR_TYPE_AXE));
    assert(!IsBluntWeapon(D2RVR_TYPE_CROSSBOW));

    std::puts("TestWeaponClassification: PASS");
}

void TestMonsterTaxonomy() {
    using namespace framevr::gore;

    // Undead (Skeletons 0..4, Zombies 5..9, Mummies 96..100)
    assert(TaxonomyForMonster(0) == BloodTaxonomy::UndeadRot);  // skeleton1
    assert(TaxonomyForMonster(4) == BloodTaxonomy::UndeadRot);  // skeleton5
    assert(TaxonomyForMonster(5) == BloodTaxonomy::UndeadRot);  // zombie1
    assert(TaxonomyForMonster(9) == BloodTaxonomy::UndeadRot);  // zombie5
    assert(TaxonomyForMonster(96) == BloodTaxonomy::UndeadRot); // mummy1

    // Insects / Vermin (Sand Maggots 68..72, Scarabs 91..95, Spiders 122..126)
    assert(TaxonomyForMonster(68) == BloodTaxonomy::InsectBile);  // sandmaggot1
    assert(TaxonomyForMonster(91) == BloodTaxonomy::InsectBile);  // scarab1
    assert(TaxonomyForMonster(122) == BloodTaxonomy::InsectBile); // arach1

    // Animals / Beasts (Brutes 24..28, Goatmen 53..57, Quill Rats 63..67)
    assert(TaxonomyForMonster(25) == BloodTaxonomy::BeastCrimson); // brute2
    assert(TaxonomyForMonster(53) == BloodTaxonomy::BeastCrimson); // goatman1
    assert(TaxonomyForMonster(63) == BloodTaxonomy::BeastCrimson); // quillrat1

    // Demons (Fallens 19..23, Shamans 58..62, Corrupt Rogues 43..47, 160..169)
    assert(TaxonomyForMonster(19) == BloodTaxonomy::DemonIchor);  // fallen1
    assert(TaxonomyForMonster(58) == BloodTaxonomy::DemonIchor);  // fallenshaman1
    assert(TaxonomyForMonster(43) == BloodTaxonomy::DemonIchor);  // corruptrogue1
    assert(TaxonomyForMonster(160) == BloodTaxonomy::DemonIchor); // cr_archer1
    assert(TaxonomyForMonster(999) == BloodTaxonomy::DemonIchor); // Default

    std::puts("TestMonsterTaxonomy: PASS");
}

void TestGoreClassification() {
    using namespace framevr::gore;

    // Non-lethal and non-critical should yield None
    assert(ClassifyMeleeGore(D2RVR_TYPE_SWORD, false, false) == GoreType::None);
    assert(ClassifyMeleeGore(D2RVR_TYPE_MACE, false, false) == GoreType::None);

    // Lethal slash with cutting weapons
    assert(ClassifyMeleeGore(D2RVR_TYPE_SWORD, true, false) == GoreType::SlashDecapitation);
    assert(ClassifyMeleeGore(D2RVR_TYPE_AXE, true, false) == GoreType::SlashDecapitation);
    assert(ClassifyMeleeGore(D2RVR_TYPE_DAGGER, true, false) == GoreType::SlashDecapitation);

    // Lethal crush with blunt weapons
    assert(ClassifyMeleeGore(D2RVR_TYPE_MACE, true, false) == GoreType::BluntBoneCrush);
    assert(ClassifyMeleeGore(D2RVR_TYPE_STAFF, true, false) == GoreType::BluntBoneCrush);
    assert(ClassifyMeleeGore(D2RVR_TYPE_UNARMED, true, false) == GoreType::BluntBoneCrush);

    // Piercing weapons
    assert(ClassifyMeleeGore(D2RVR_TYPE_SPEAR, true, false) == GoreType::PiercingArterial);
    assert(ClassifyMeleeGore(D2RVR_TYPE_JAVELIN, true, false) == GoreType::PiercingArterial);

    // Other lethal
    assert(ClassifyMeleeGore(D2RVR_TYPE_ORB, true, false) == GoreType::GenericLethal);

    // Critical non-lethal strike
    assert(ClassifyMeleeGore(D2RVR_TYPE_SWORD, false, true) == GoreType::SlashDecapitation);
    assert(ClassifyMeleeGore(D2RVR_TYPE_MACE, false, true) == GoreType::BluntBoneCrush);

    std::puts("TestGoreClassification: PASS");
}

void TestEventQueue() {
    using namespace framevr::gore;

    EventQueue q;
    assert(q.Count() == 0);

    GoreEvent ev1{};
    ev1.timestamp = 1000;
    ev1.monsterId = 42;
    ev1.hand = 0;
    ev1.type = GoreType::SlashDecapitation;
    ev1.taxonomy = BloodTaxonomy::DemonIchor;
    ev1.lethal = true;

    q.Publish(ev1);
    assert(q.Count() == 1);

    GoreEvent peeked{};
    assert(q.Peek(peeked));
    assert(peeked.monsterId == 42);
    assert(peeked.type == GoreType::SlashDecapitation);
    assert(q.Count() == 1);

    GoreEvent taken{};
    assert(q.Take(taken));
    assert(taken.monsterId == 42);
    assert(q.Count() == 0);
    assert(!q.Take(taken));

    // Test ring buffer wrap-around
    for (uint32_t i = 0; i < 25; ++i) {
        GoreEvent ev{};
        ev.timestamp = 2000 + i;
        ev.monsterId = i;
        ev.hand = int(i % 2);
        ev.type = (i % 2 == 0) ? GoreType::SlashDecapitation : GoreType::BluntBoneCrush;
        q.Publish(ev);
    }

    assert(q.Count() == EventQueue::Capacity);

    // LatestForHand checks
    GoreEvent latestRight{};
    assert(q.LatestForHand(0, 2024, 150, latestRight));
    assert(latestRight.hand == 0);
    assert(latestRight.monsterId == 24);
    assert(latestRight.type == GoreType::SlashDecapitation);

    GoreEvent latestLeft{};
    assert(q.LatestForHand(1, 2024, 150, latestLeft));
    assert(latestLeft.hand == 1);
    assert(latestLeft.monsterId == 23);
    assert(latestLeft.type == GoreType::BluntBoneCrush);

    // Stale check
    GoreEvent stale{};
    assert(!q.LatestForHand(0, 3000, 100, stale)); // Timestamp 2024 is older than 3000 - 100

    q.Clear();
    assert(q.Count() == 0);

    std::puts("TestEventQueue: PASS");
}

void TestGoreHaptics() {
    using namespace framevr::gore;
    using namespace framevr::haptics;

    const auto base = SampleFor(Category::Melee);
    const GoreType types[] = {
        GoreType::SlashDecapitation,
        GoreType::BluntBoneCrush,
        GoreType::PiercingArterial,
        GoreType::CorpseExplosion,
        GoreType::ColdShatter,
        GoreType::GenericLethal
    };

    for (const auto type : types) {
        const auto pulse = GoreSampleFor(type);
        assert(pulse.milliseconds > base.milliseconds);
        assert(pulse.milliseconds == base.milliseconds * 2);
        assert(pulse.frequency == base.frequency);
        assert(pulse.amplitude >= base.amplitude);

        const auto shortBase = SampleFor(Category::Melee, true, 0);
        const auto shortPulse = GoreSampleFor(type, shortBase);
        assert(shortPulse.milliseconds > shortBase.milliseconds);
        assert(shortPulse.milliseconds == shortBase.milliseconds * 2);
        assert(shortPulse.frequency == shortBase.frequency);

        const auto leftBase = SampleFor(Category::Melee, false, 1);
        const auto leftPulse = GoreSampleFor(type, leftBase);
        assert(leftPulse.milliseconds > leftBase.milliseconds);
        assert(leftPulse.milliseconds == leftBase.milliseconds * 2);
        assert(leftPulse.frequency == leftBase.frequency);
    }

    const auto nonePulse = GoreSampleFor(GoreType::None, base);
    assert(nonePulse.milliseconds == base.milliseconds);
    assert(nonePulse.frequency == base.frequency);

    std::puts("TestGoreHaptics: PASS");
}

} // namespace

int main() {
    std::puts("Running gore-events-test...");
    TestWeaponClassification();
    TestMonsterTaxonomy();
    TestGoreClassification();
    TestEventQueue();
    TestGoreHaptics();
    std::puts("All gore-events-test checks PASSED.");
    return 0;
}

