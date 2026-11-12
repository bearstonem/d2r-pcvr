// game_ceiling_test - shared/game_ceiling.h, shared/ceiling_measures.h and vr/engine_held_units.h: [ceiling] game_ceiling's slab of the game's
// own lit rock (adapted from natpoh/diablo2-vr-port's cleanroom/tests/game_ceiling_test.cpp, MIT, see THIRD_PARTY.md; its mesh, beams,
// domes and INI-upgrade parts left out with what they test):
//   1. shown only ticked, in VR F4, where a ceiling is wanted, the hero known - each missing one named; who draws it (the game's while it is
//      the game's, laid or not - never the shader's vault in between);
//   2. a tile's span, the area's floor (the lowest stood since the area changed), the slab's and the wall's matrices, and when a move must
//      carry no motion (Jumped);
//   3. its units: one root and a model, naming our model's path and its kind's three maps; every kind its own paths, never another's;
//   4. the installer's receipt naming a file, whichever way its keys are written;
//   5. [ceiling] height_from_walls: a preset's placements, a wall piece's top, the median, the areas' table, the measures file written and
//      read back, the height chosen (its own, else the walls', else 18), a room's floor maps;
//   6. letting go: the live entities kept, in order; no placing between the world's teardown and a menu; the log line.
#include "../shared/ceiling_measures.h"
#include "../shared/game_ceiling.h"
#include "../vr/engine_held_units.h"
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace gameceiling;

int main() {
    // 1. when
    In ok;
    ok.ticked = ok.f4 = ok.wanted = ok.heroOk = true;
    assert(std::strcmp(WhyNot(ok), "") == 0);
    {
        In in = ok; in.ticked = false; assert(std::strstr(WhyNot(in), "game_ceiling=0") != nullptr);
        in = ok; in.f4 = false; assert(std::strstr(WhyNot(in), "F4") != nullptr);
        in = ok; in.wanted = false; assert(std::strstr(WhyNot(in), "no game ceiling wanted") != nullptr);
        in = ok; in.heroOk = false; assert(std::strstr(WhyNot(in), "hero") != nullptr);
    }
    // who draws it: the game's while ticked in VR F4 and not given up on - laid or not; the shader's vault only when it is not the game's
    // at all; while the slab loads, nobody
    assert(ByGame(true, true, true, false, false));
    assert(!ByGame(false, true, true, false, false) && !ByGame(true, false, true, false, false) && !ByGame(true, true, false, false, false) &&
           !ByGame(true, true, true, true, false) && !ByGame(true, true, true, false, true));
    assert(!GivenUp(false, false, 0));   // not tried yet: not given up - the area's first frame is the game's already
    assert(GivenUp(true, false, 0) && !GivenUp(true, true, 4) && GivenUp(true, true, 5));
    assert(Drawer(false, true, true, false) == 2);   // the first frames in the area, the slab not laid: nothing drawn, NOT the vault
    assert(Drawer(false, true, true, true) == 3);
    assert(Drawer(false, false, true, true) == 3);   // an area on game_ceiling_biomes only (not on [ceiling] biomes): the game's still
    assert(Drawer(false, true, false, false) == 1);  // game_ceiling off (or given up, or not F4): the vault as before
    assert(Drawer(false, false, false, false) == 0 && Drawer(true, true, false, false) == 0);
    for (int laid = 0; laid < 2; ++laid)
        assert(Drawer(false, true, ByGame(true, true, true, GivenUp(false, false, 0), false), laid != 0) != 1);
    // 2. where
    assert(TileWorld(10.0f) == 40.0f && TileWorld(0.0f) == 40.0f && TileWorld(2.5f) == 10.0f);
    {
        float floor = 0.0f;
        unsigned seen = ~0u;
        assert(Floor(&floor, &seen, 7, 100.0f) == 100.0f && seen == 7);
        assert(Floor(&floor, &seen, 7, 96.0f) == 96.0f);    // down a step: the floor with him
        assert(Floor(&floor, &seen, 7, 104.0f) == 96.0f);   // up on a ledge: the floor stays
        assert(Floor(&floor, &seen, 8, 104.0f) == 104.0f);  // another area: its own
    }
    {
        const float hero[3] = {57.0f, 96.0f, -21.0f};
        float m[16];
        Matrix(hero, 90.0f, 23.0f, 40.0f, true, m);
        assert(m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f && m[15] == 1.0f && m[1] == 0.0f && m[4] == 0.0f);
        assert(m[12] == 40.0f && m[14] == -40.0f && m[13] == 113.0f);   // the nearest whole tiles (57 -> 40, -21 -> -40), the floor + 23
        const float on[3] = {61.0f, 96.0f, -19.0f};
        float n[16];
        Matrix(on, 90.0f, 23.0f, 40.0f, true, n);
        assert(n[12] == 80.0f && n[14] == 0.0f);
        assert(Jumped(m, n) && !Jumped(m, m));                          // another tile: no motion taken from it
        const float within[3] = {58.0f, 96.0f, -22.0f};
        Matrix(within, 90.0f, 23.0f, 40.0f, true, n);
        assert(!Jumped(m, n));                                          // walking within a tile: the slab stands still
        Matrix(hero, 90.0f, 23.0f, 20.0f, true, m);                     // a tile of 20 units: half the model's
        assert(m[0] == 0.5f && m[12] == 60.0f && m[14] == -20.0f);
        const float far[3] = {123456.0f, 0.0f, -654321.0f};             // far from the origin: still a whole number of tiles
        Matrix(far, 0.0f, 23.0f, 40.0f, true, m);
        assert(std::fmod((double)m[12], 40.0) == 0.0 && std::fabs(m[12] - far[0]) <= 20.0f && std::fabs(m[14] - far[2]) <= 20.0f);
        Matrix(hero, 90.0f, 23.0f, 40.0f, false, m);
        assert(m[0] == 0.001f && m[12] == 57.0f && m[13] == 96.0f - 5000.0f && m[14] == -21.0f);
        Matrix(hero, 90.0f, 23.0f, 40.0f, true, n);
        assert(Jumped(m, n));                                           // shown again after being put away: no motion either
    }
    // the rim bent down
    assert(WallReach(23.0f, 20.0f) == 43.0f && WallReach(23.0f, 0.0f) == 23.0f && WallReach(23.0f, -5.0f) == 23.0f);
    {
        const float hero[3] = {57.0f, 96.0f, -21.0f};
        float w[16], v[16];
        WallMatrix(hero, 90.0f, 23.0f, 40.0f, 43.0f, true, w);
        assert(w[12] == 57.0f && w[14] == -21.0f && w[13] == 113.0f);   // at the hero (not snapped), its top the slab's
        assert(w[0] == 1.0f && w[10] == 1.0f && std::fabs(w[5] - 43.0f / kWallHeight) < 1e-6f && w[1] == 0.0f && w[4] == 0.0f);
        const float next[3] = {58.5f, 96.0f, -20.0f};
        WallMatrix(next, 90.0f, 23.0f, 40.0f, 43.0f, true, v);
        assert(Jumped(w, v) && !Jumped(w, v, true));                    // going across with the hero: real motion, kept
        WallMatrix(next, 80.0f, 23.0f, 40.0f, 43.0f, true, v);
        assert(Jumped(w, v, true));                                     // the floor dropped: a jump
        WallMatrix(hero, 90.0f, 23.0f, 40.0f, 43.0f, false, w);
        assert(w[0] == 0.001f && w[5] == 0.001f && w[13] == 96.0f - 5000.0f);
        WallMatrix(hero, 90.0f, 23.0f, 40.0f, WallReach(23.0f, 20.0f), true, w);
        assert(std::fabs(w[13] - kWallHeight * w[5] - (90.0f - 20.0f)) < 1e-3f);   // its bottom: floor_depth under the floor
    }
    // under the slab wherever the slab snaps (its middle up to half a tile from the hero)
    assert(kWallRadius + kTile * 0.5f <= kTile * kTilesHalf && kWallRadius > kTile * (kTilesHalf - 1));
    assert(kIndexCount == 12u * 72u * 72u);
    // 3. its units
    const std::string unit = UnitJson();
    assert(UnitFileProblem(unit) == nullptr);
    assert(unit.find(std::string("\"filename\":\"") + kModel + "\"") != std::string::npos);
    for (const Map& m : kMaps) assert(unit.find(m.to) != std::string::npos);
    assert(unit.find("\"lightMask\":19") != std::string::npos && unit.find("\"shadowMask\":0") != std::string::npos);
    assert(std::string(kModel) == std::string(kModelDir) + kModelFile + ".model");
    assert(UnitFileProblem("") && UnitFileProblem("{\"type\":\"Other\"}") && UnitFileProblem(R"({"type":"UnitDefinition"})") &&
           UnitFileProblem(R"({"type":"UnitDefinition","a":"UnitRootComponent","b":"UnitRootComponent","c":"ModelDefinitionComponent"})") &&
           UnitFileProblem(R"({"type":"UnitDefinition","a":"UnitRootComponent"})"));
    const std::string wallUnit = UnitJson(true);
    assert(UnitFileProblem(wallUnit) == nullptr);
    assert(wallUnit.find(std::string("\"filename\":\"") + kWallModel + "\"") != std::string::npos && wallUnit.find(kModel) == std::string::npos);
    assert(std::string(kWallModel) == std::string(kModelDir) + kWallModelFile + ".model" && std::string(kWallModel) != kModel);
    assert(std::string(kWallUnit) != kUnit);
    assert(UnitPath(0, false) == kUnit && UnitPath(0, true) == kWallUnit && UnitJson(0, false) == UnitJson() && UnitJson(0, true) == UnitJson(true));
    assert(ModelPath(kKinds[0].slab) == kModel && ModelPath(kKinds[0].wall) == kWallModel && MapPath(kKinds[0], "ALB") == kMaps[0].to);
    {
        std::vector<std::string> paths, units;
        for (int k = 0; k < kKindCount; ++k)
            for (const bool wall : {false, true}) {
                const char* file = wall ? kKinds[k].wall : kKinds[k].slab;
                if (!file) continue;
                paths.push_back(ModelPath(file));
                units.push_back(UnitPath(k, wall));
                const std::string u = UnitJson(k, wall);
                assert(UnitFileProblem(u) == nullptr && u.find(ModelPath(file)) != std::string::npos);
                for (const char* m : {"ALB", "NRM", "ORM"}) assert(u.find(MapPath(kKinds[k], m)) != std::string::npos);
                assert(LodPath(file, 3) == ModelPath(file).substr(0, ModelPath(file).size() - 6) + "_lod3.model");
            }
        for (size_t i = 0; i < paths.size(); ++i)
            for (size_t j = i + 1; j < paths.size(); ++j) assert(paths[i] != paths[j] && units[i] != units[j]);
        for (int k = 0; k < kKindCount; ++k) assert((kKinds[k].wall != nullptr) == (kKinds[k].relief == kRelief));   // walls: the caves'
    }
    assert(KindOf("act1_caves") == 0 && KindOf("act1_barracks") == KindOf("act1_catacombs") &&
           std::string(kKinds[KindOf("act1_barracks")].maps).find("grunge01") != std::string::npos);
    assert(KindOf("act4_diab") == -1 && KindOf("act1_outdoors") == -1 && KindOf("") == -1);   // (the Chaos Sanctuary keeps its void)
    for (const BiomeKind& b : kBiomeKinds) assert(b.kind >= 0 && b.kind < kKindCount && AllBiomes().find(b.biome) != std::string::npos);
    for (const char* room : {"act1_crypt", "act1_barracks", "act1_cathedral", "act1_catacombs", "act2_palace_clean", "act2_sewer"})
        assert(KindOf(room) >= 0);   // every area of the shipped [ceiling] biomes has a kind
    for (const FloorKind& f : kFloorKinds) {
        const int k = KindOf(f.biome);
        assert(k >= 0 && std::string(kKinds[k].name) == f.kind && kKinds[k].relief == 0.0f && kKinds[k].wall == nullptr);
    }
    for (int a = 0; a < kKindCount; ++a)
        for (const bool wa : {false, true})
            for (int b = 0; b < kKindCount; ++b)
                for (const bool wb : {false, true}) assert((a == b && wa == wb) || SaltOf(a, wa) != SaltOf(b, wb));
    assert(ModelPath("@a/b/test_asset") == "data/hd/env/model/a/b/test_asset.model" && LodPath("@a/x", 2) == "data/hd/env/model/a/x_lod2.model" &&
           ModelPath("testy") == "data/hd/env/model/Tests/testy.model");
    // 4. the installer's receipt names a file: from the mod's folder or the .mpq folder, either slash, any case - and only the whole path
    {
        const std::string receipt = R"({"game_build":"x","files":{"D2R-PCVR.mpq/data/hd/env/model/d2r_vr/ceiling/cave_ceiling_unit.json":"ab",)"
                                    R"("data\\hd\\env\\model\\Tests\\test_detailnormal\\cliffside01_lod0.model":"cd"}})";
        assert(ReceiptNames(receipt, kUnit));
        assert(ReceiptNames(receipt, LodPath(kKinds[0].slab, 0)));
        assert(ReceiptNames(receipt, "DATA/hd/env/model/tests/test_detailnormal/CLIFFSIDE01_lod0.model"));
        assert(!ReceiptNames(receipt, LodPath(kKinds[0].slab, 1)) && !ReceiptNames(receipt, "model/d2r_vr/ceiling/cave_ceiling_unit.json") &&
               !ReceiptNames(receipt, "data/hd/env/model/d2r_vr/ceiling/cave_ceiling_unit.jso") && !ReceiptNames("", kUnit));
    }

    // 5. [ceiling] height_from_walls: the walls' top from the presets
    {
        const char* preset = R"({"name":"barE1","entities":[{"type":"Entity","name":"wall","components":[
            {"type":"TransformDefinitionComponent","name":"t","position":{"x":3.0,"y":0.5,"z":-2.0},"scale":{"x":1.0,"y":1.0,"z":1.0}},
            {"type":"ModelDefinitionComponent","name":"m","filename":"data/hd/env/model/act1/barracks/Wall01.model","visibleLayers":1}]},
          {"type":"Entity","name":"pillar","components":[{"type":"ModelDefinitionComponent","filename":"data/hd/env/model/act1/barracks/pillar02.model"},
            {"type":"TransformDefinitionComponent","position":{"y":-1e0},"scale":{"y":2}}]},
          {"type":"Entity","name":"no model","components":[{"type":"TransformDefinitionComponent","position":{"y":9}}]},
          {"type":"Entity","name":"nested","children":[{"components":[{"type":"ModelDefinitionComponent","filename":"x/crate.model"}]}],
           "note":"a \"quoted\" \\ string","flag":true,"none":null,"list":[1,-2.5e-1,[]]}]})";
        std::vector<ceilmeasure::Placement> placed;
        assert(ceilmeasure::Placements(preset, &placed) && placed.size() == 3);
        assert(placed[0].model == "data/hd/env/model/act1/barracks/wall01.model" && placed[0].y == 0.5f && placed[0].scaleY == 1.0f);
        assert(placed[1].model.find("pillar02") != std::string::npos && placed[1].y == -1.0f && placed[1].scaleY == 2.0f);   // (its transform after it)
        assert(placed[2].model == "x/crate.model" && placed[2].y == 0.0f && placed[2].scaleY == 1.0f);   // (no transform: at 0, scale 1)
        assert(std::fabs(ceilmeasure::WallTop(placed[0], 0.0f, 14.4f) - 14.9f) < 1e-4f);   // its height + its top
        assert(std::fabs(ceilmeasure::WallTop(placed[1], 0.0f, 8.0f) - 15.0f) < 1e-4f);    // scaled: 8 x 2 - 1
        assert(ceilmeasure::WallTop(placed[2], 0.0f, 20.0f) < 0.0f);                      // a crate is not a wall
        assert(ceilmeasure::WallTop(placed[0], 10.0f, 15.0f) < 0.0f);                     // a cap 5 tall is not a wall
        std::vector<ceilmeasure::Placement> none;
        assert(!ceilmeasure::Placements("{\"a\":[1,2", &none) && !ceilmeasure::Placements("{} x", &none));   // cut short; trailing bytes
        assert(ceilmeasure::IsWallPiece("data/hd/env/model/act1/caves/act1_caves_cliffs/CLIFF03.model") && ceilmeasure::IsWallPiece("column_a") &&
               !ceilmeasure::IsWallPiece("wood_ceiling_frame01"));
        assert(ceilmeasure::Median({}) == 0.0f && ceilmeasure::Median({3.0f, 1.0f, 2.0f}) == 2.0f && ceilmeasure::Median({4.0f, 1.0f, 2.0f, 3.0f}) == 2.5f);
        // every area named once, indoors only, each its own presets' folder; every area with a kind can be measured
        for (size_t i = 0; i < sizeof ceilmeasure::kAreas / sizeof ceilmeasure::kAreas[0]; ++i) {
            const std::string b = ceilmeasure::kAreas[i].biome, p = ceilmeasure::kAreas[i].presets;
            assert(p.compare(0, 3, b.substr(0, 3)) == 0 || b.rfind("expansion", 0) == 0);
            assert(b != "act4_diab" && b != "act4_fort" && b.find("outdoors") == std::string::npos);
            for (size_t j = i + 1; j < sizeof ceilmeasure::kAreas / sizeof ceilmeasure::kAreas[0]; ++j) assert(b != ceilmeasure::kAreas[j].biome);
        }
        for (const BiomeKind& k : kBiomeKinds) {
            bool in = false;
            for (const ceilmeasure::Area& a : ceilmeasure::kAreas) in = in || k.biome == std::string(a.biome);
            assert(in);
        }
        // the file: written, read back; another version or none taken as nothing
        ceilmeasure::Measures m, back;
        m.walls = {{"act1_barracks", 14.92f}, {"act1_crypt", 14.1f}};
        assert(ceilmeasure::Parse(ceilmeasure::Format(m), &back) && back.walls.size() == 2);
        assert(std::fabs(ceilmeasure::WallsTopOf(back, "act1_crypt") - 14.1f) < 1e-3f && ceilmeasure::WallsTopOf(back, "act1_caves") == 0.0f);
        std::string other = ceilmeasure::Format(m);
        const std::string cur = "version " + std::to_string(ceilmeasure::kVersion);
        other.replace(other.find(cur), cur.size(), "version 1");
        assert(!ceilmeasure::Parse(other, &back) && back.walls.empty());
        assert(!ceilmeasure::Parse("", &back) && !ceilmeasure::Parse("walls act1_crypt 14\n", &back) && back.walls.empty());
        assert(ceilmeasure::Parse("version 5\r\nwalls a 0\r\nwalls b 7.5\r\n", &back) && back.walls.size() == 1 && ceilmeasure::WallsTopOf(back, "b") == 7.5f);
        // the height: its own once set; else on the walls' top when ticked and measured (a hanging rock resting its lowest on them); else 18
        assert(HeightOf(15.1f, true, 14.9f, 0.0f, 1.0f) == 15.1f);
        assert(HeightOf(0.0f, true, 14.9f, 0.0f, 1.0f) == 14.9f);
        assert(HeightOf(0.0f, true, 10.4f, kRelief, 0.5f) == 10.4f + 3.0f);   // the cave rock: 6 model units at half a tile's scale
        assert(HeightOf(0.0f, true, 0.0f, 0.0f, 1.0f) == kHeightAlone && HeightOf(0.0f, false, 14.9f, 0.0f, 1.0f) == kHeightAlone);
        assert(kHeightAlone == 18.0f && HeightOf(-3.0f, false, 0.0f, 0.0f, 1.0f) == kHeightAlone);
        // a room's floor: the first layer of solid stone among its terrain's, nothing loose or wet; none - ""
        assert(FloorMaps({"t/act3_docktown_base_dirt_ALB.texture", "t/act3_temple_stone01_ALB.texture"}) == "t/act3_temple_stone01");
        assert(FloorMaps({"t/expansion_wildtemple_puddle01_ALB.texture", "t/expansion_wildtemple_caverock_ALB.texture"}) == "t/expansion_wildtemple_caverock");
        assert(FloorMaps({"t/act2_tomb_sand02_ALB.texture", "t/act2_tomb_mud01_ALB.texture", "t/act2_tomb_sand_hard01_ALB.texture"}).empty());
        assert(FloorMaps({}).empty() && std::string(kRockMaps).find("act1_caves_rock01") != std::string::npos);
    }

    // 6. letting go (vr/engine_held_units.h)
    {
        const uint32_t ents[] = {0x00000000u, 0x00100001u, 0x00000002u, 0x00300003u};   // the registry's entities by index (the version above)
        assert(heldunits::Live(ents, 4, 0x00100001u) && heldunits::Live(ents, 4, 0x00000002u));
        assert(!heldunits::Live(ents, 4, 0x00200001u) && !heldunits::Live(ents, 4, 0x00000004u) && !heldunits::Live(nullptr, 0, 0));
        uint32_t list[] = {0x00100001u, 0x00200001u, 0x00300003u, 0x00000009u};
        const size_t k = heldunits::KeepLive(list, 4, [&](uint32_t e) { return heldunits::Live(ents, 4, e); });
        assert(k == 2 && list[0] == 0x00100001u && list[1] == 0x00300003u);   // the live ones, in their order
        heldunits::Life life;
        assert(life.MayPlace(true) && !life.MayPlace(false));
        life.Teardown();
        assert(!life.MayPlace(true));   // the frames the game still draws while it leaves: nothing placed
        life.World(true);
        assert(!life.MayPlace(true));
        life.World(false);              // a menu: the next game places afresh
        assert(life.MayPlace(true));
        heldunits::Tally t;
        t.instances = 2; t.stale = 1; t.definitions = 2;
        assert(heldunits::Line("game left", t).find("2 placed instances removed (1 with entities") != std::string::npos);
        t.kept = 1;
        assert(heldunits::Line("game left", t).find("KEPT") != std::string::npos);
    }
    return 0;
}
