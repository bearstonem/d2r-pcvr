// Adapted from natpoh/diablo2-vr-port (MIT, Copyright (c) 2026 natpoh) at 91cb1e8; see THIRD_PARTY.md.
// shared/game_ceiling.h there; the beams, the vaults and the INI upgrade batches left out.
#pragma once
// game_ceiling.h - [ceiling] game_ceiling: the dungeon ceiling drawn by the game itself. A slab of the game's own lit cave rock - act 1's
// rocks03 model (StandardMaterial) with the slab's mesh in it and each area's own tiling terrain as its maps (the installer makes it from
// the player's game: steamvr/prepare_game_ceiling.py) - loaded by the game's own unit loader and laid over the hero every frame by the DLL
// (vr/engine_units.cpp). The game draws it in its own frame: both eyes, its torches' and the hero's light on it, its fog over it, fire
// and spells in their own order. The pure parts, for the DLL and the installer's tests; tested in steamvr/game_ceiling_test.cpp.
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace gameceiling {

// The slab as made: kTilesHalf tiles of kTile units each way from its middle, kCells cells a tile, its rock hanging down kRelief from its
// top. One tile = one picture of the floor's rock.
constexpr float kTile = 40.0f;
constexpr int kTilesHalf = 6;
constexpr int kCells = 6;
constexpr float kRelief = 6.0f;
// Its mesh's index count, every LOD: two triangles a cell, both windings
constexpr unsigned kIndexCount = 12u * (2u * kTilesHalf * kCells) * (2u * kTilesHalf * kCells);

// Where the game takes it from. The game makes a model only from a path it knows (its own files' names): one of its developers' test
// models no unit or preset names and nothing in play shows. Of all the game's JSON (the installed game, 2026-10-27) only its LOD table
// (data/hd/model_lod_desc.json) names this one. The unit (ours) and the textures (the game's) load from their own paths.
constexpr const char* kModelDir = "data/hd/env/model/Tests/test_detailnormal/";
constexpr const char* kModelFile = "cliffside01";   // + "_lod<0-4>.model"; the unit names it without the LOD
constexpr const char* kModel = "data/hd/env/model/Tests/test_detailnormal/cliffside01.model";
constexpr const char* kUnit = "data/hd/env/model/d2r_vr/ceiling/cave_ceiling_unit.json";
// The game's model it is made from (its file read for every LOD), and its maps renamed to the floor's tiling rock (act 1's caves' terrain)
constexpr const char* kSource = "data/hd/env/model/act1/caves/act1_caves_rocks/rocks03_lod1.model";
struct Map { const char* from; const char* to; };
constexpr Map kMaps[] = {
    {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_alb.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_ALB.texture"},
    {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_nrm.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_NRM.texture"},
    {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_orm.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_ORM.texture"},
};

// [ceiling] game_ceiling_wall: the slab's rim bent down - a ring of the same rock (WallMesh), its own model and unit, laid with the slab:
// the black void past the slab's edge closed by a wall. Its middle at the hero (not on whole tiles: it never pokes out from under the
// slab, which snaps), its top at the slab's top, its reach down to [ceiling] floor_depth under the area's floor. Caves only (rooms have
// walls of their own); off by default.
constexpr float kWallRadius = kTile * kTilesHalf - kTile * 0.5f - kRelief;   // under the slab wherever its middle is (it snaps up to half a tile)
constexpr float kWallHeight = kTile;      // the model's own height (one picture), scaled to the reach
constexpr int kWallSegments = 96;         // round it
constexpr int kWallRows = 4;              // down it
constexpr unsigned kWallIndexCount = 12u * kWallSegments * kWallRows;
constexpr const char* kWallModelFile = "decal01";   // in kModelDir
constexpr const char* kWallModel = "data/hd/env/model/Tests/test_detailnormal/decal01.model";
constexpr const char* kWallUnit = "data/hd/env/model/d2r_vr/ceiling/cave_ceiling_wall_unit.json";

// ---- every room its own: the kinds of ceiling ----
// The same slab, made the same way from the same lit rock model (kSource), each kind with a tiling terrain set of the game's own (ALB / NRM /
// ORM): the caves' rock, the crypt's tall stone slabs, the cathedral's smooth light stone, the barracks' and catacombs' smooth dark stone,
// the palace's ornamented squares, and elsewhere the room's own floor. Caves keep their rock relief and the wall at the rim; built rooms
// are flat and have walls of their own. A room gets the game's ceiling only once its kind is made; every other room keeps the shader's
// vault with its own settings - never nothing.
// Each kind is its own model at its own free path (its maps are baked in): the game's developers' test models no unit or preset of play
// names (2026-10-27 scan: only the LOD table). Kind 0 is the first one, at its first paths and units.
struct Kind {
    const char* name;      // for the log and its units' names
    const char* maps;      // the terrain set's path without "_ALB.texture" / "_NRM.texture" / "_ORM.texture"
    float relief;          // its rock's relief, model units; 0 flat
    const char* slab;      // its model, under data/hd/env/model/Tests/ ("@...": under data/hd/env/model/), without "_lod<n>.model"
    const char* wall;      // the wall at its rim's model, or nullptr (rooms: their own walls)
};
constexpr Kind kKinds[] = {
    {"cave", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01", kRelief, "test_detailnormal/cliffside01", "test_detailnormal/decal01"},
    // the other caves: their own act's rock
    {"jungle_cave", "data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01", kRelief, "test_cloth/cloth02", "test_cloth/cloth04"},
    {"ice_cave", "data/hd/env/texture/terrain/expansion/icecave/Act5_Rocky_Floor", kRelief, "test_cloth/rope_test01/rope_test01", "testy"},
    // the crypt's tall stone slabs: act 4's tall stone tiles in columns
    {"crypt", "data/hd/env/texture/terrain/act4/mesa/act4_mesa_stonetile01", 0.0f, "vertblend_house", nullptr},
    // smooth dark stone (the barracks', the catacombs', the palace cellar's, the tombs'): the Arcane Sanctuary's dark grunge
    {"smooth_dark", "data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge01", 0.0f, "test_terrainblending/cliff01", nullptr},
    // smooth light stone (the cathedral's): its lighter grunge
    {"smooth_light", "data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge02", 0.0f, "test_terrainblending/ramp01", nullptr},
    // the palace cells' ornamented marble
    {"palace", "data/hd/env/texture/terrain/act2/palace/act2_palace_cell_marble_tile01", 0.0f, "TextureExportTesting/TestCube", nullptr},
    // their own floor's (FloorMaps): at test models only the developers' test presets name (no level loads them)
    {"sewer", "data/hd/env/texture/terrain/act2/sewer/act2_sewer_cobblestone01", 0.0f, "test_cube_tex", nullptr},
    {"harem", "data/hd/env/texture/terrain/act2/palace/act2_palace_marble01", 0.0f, "test_cube_foliage", nullptr},
    {"barracks_court", "data/hd/env/texture/terrain/act1/barracks/act1_barracks_cobblestone03", 0.0f, "@expansion/siege/expansion_siege_craters/test_asset", nullptr},
    {"act3_sewer", "data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01", 0.0f, "@act1/catacomb/act1_catacomb_props/_pit02", nullptr},
    {"temple", "data/hd/env/texture/terrain/act3/temple/act3_temple_stone01", 0.0f, "test_cube", nullptr},
    {"durance", "data/hd/env/texture/terrain/act3/travincal/act3_travincal_stone01", 0.0f, "test_cloth/hangingRug", nullptr},
    {"wildtemple", "data/hd/env/texture/terrain/expansion/wildtemple/expansion_wildtemple_caverock", 0.0f, "test_decal/steppe01", nullptr},
    {"baallair", "data/hd/env/texture/terrain/expansion/baallair/expansion_baallair_floor01", 0.0f, "test_light/test_material_spheres01", nullptr},
};
// The kinds whose maps are their room's own floor (the biome they were taken from): the installer's test reads its first terrain layer
struct FloorKind { const char* biome; const char* kind; };
constexpr FloorKind kFloorKinds[] = {{"act2_sewer", "sewer"}, {"act2_palace", "harem"},
                                     {"act1_court_to_barracks", "barracks_court"}, {"act3_sewer", "act3_sewer"},
                                     {"act3_temple", "temple"}, {"act3_travincal", "durance"}, {"expansion_wildtemple", "wildtemple"},
                                     {"expansion_baallair", "baallair"}, {"expansion_baallair_throneroom", "baallair"}};
// The floor's stone among a biome's terrain layers (their ALB paths, in its order): the first whose name says a solid floor - stone,
// cobblestone, marble, tile, floor, brick, rock - and nothing loose or wet (dirt, mud, sand, water, puddle, sewage, blood, moss, pebbles,
// grass, damaged, bridge); its maps without "_ALB.texture". None: "" - the cave rock then (kRockMaps), laid flat.
constexpr const char* kRockMaps = "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01";
inline std::string FloorMaps(const std::vector<std::string>& albedos) {
    for (const std::string& a : albedos) {
        std::string n = a.substr(a.find_last_of('/') + 1);
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        bool solid = false, loose = false;
        for (const char* w : {"stone", "cobble", "marble", "tile", "floor", "brick", "rock"}) solid = solid || n.find(w) != std::string::npos;
        for (const char* w : {"dirt", "mud", "sand", "water", "puddle", "sewage", "blood", "moss", "pebble", "grass", "damaged", "bridge"})
            loose = loose || n.find(w) != std::string::npos;
        const std::string tail = "_alb.texture";
        if (solid && !loose && n.size() > tail.size() && n.compare(n.size() - tail.size(), tail.size(), tail) == 0)
            return a.substr(0, a.size() - tail.size());
    }
    return std::string();
}
// Each piece's mesh its own bytes (the game shares a model it has by what it holds - every flat kind's slab is the same mesh): kind k's
// slab 1 + 2k, its wall 2 + 2k
inline unsigned SaltOf(int kind, bool wall) { return 1u + 2u * (unsigned)kind + (wall ? 1u : 0u); }
constexpr int kKindCount = (int)(sizeof kKinds / sizeof kKinds[0]);
// Which kind each area gets (the game's biome names); an area not here keeps the shader's vault (or none, as before)
struct BiomeKind { const char* biome; int kind; };
constexpr BiomeKind kBiomeKinds[] = {
    {"act1_caves", 0}, {"act2_maggot", 1}, {"act3_spider", 1}, {"act3_jungle_dungeon", 1}, {"expansion_icecave", 2},
    {"act1_crypt", 3}, {"act1_barracks", 4}, {"act1_catacombs", 4}, {"act1_cathedral", 5}, {"act2_palace_clean", 6},
    {"act2_sewer", 7}, {"act2_palace", 8},
    // smooth stone, not the floor's tiles or the cave rock: the catacombs' smooth dark stone in the palace cellar and every tomb
    {"act2_palace_cells", 4}, {"act2_tomb", 4},
    {"act1_court_to_barracks", 9}, {"act3_sewer", 10}, {"act3_temple", 11}, {"act3_travincal", 12},
    {"expansion_wildtemple", 13}, {"expansion_baallair", 14}, {"expansion_baallair_throneroom", 14},
};
// The default of [ceiling] game_ceiling_biomes: every area of the table
inline std::string AllBiomes() {
    std::string s;
    for (const BiomeKind& b : kBiomeKinds) s += (s.empty() ? "" : ",") + std::string(b.biome);
    return s;
}
// The area's kind, or -1 (no fitting model: the shader's vault there, if it has one)
inline int KindOf(const std::string& biome) {
    for (const BiomeKind& b : kBiomeKinds) if (biome == b.biome) return b.kind;
    return -1;
}
inline std::string MapPath(const Kind& k, const char* which) { return std::string(k.maps) + "_" + which + ".texture"; }   // which: ALB, NRM, ORM
// A piece's model path the unit names (no LOD), one LOD's file, and its unit's path (kind 0 at its first ones)
inline std::string ModelBase(const char* file) {   // (under Tests/; "@...": under data/hd/env/model/ itself)
    return file[0] == '@' ? std::string("data/hd/env/model/") + (file + 1) : std::string("data/hd/env/model/Tests/") + file;
}
inline std::string ModelPath(const char* file) { return ModelBase(file) + ".model"; }
inline std::string LodPath(const char* file, int lod) { return ModelBase(file) + "_lod" + std::to_string(lod) + ".model"; }
constexpr int kLods = 5;   // every piece's _lod0 .. _lod4 files, the same mesh in each
inline std::string UnitPath(int kind, bool wall) {
    if (kind == 0) return wall ? kWallUnit : kUnit;
    return std::string("data/hd/env/model/d2r_vr/ceiling/") + kKinds[kind].name + (wall ? "_wall_unit.json" : "_ceiling_unit.json");
}

struct In {
    bool ticked = false;   // [ceiling] game_ceiling
    bool f4 = false;       // VR first person
    bool wanted = false;   // a ceiling wanted here: [ceiling] enabled, first person, in a world, an area on game_ceiling_biomes, no menu
    bool heroOk = false;   // the hero's place is known
};

// Why the slab is not shown now ("" = shown)
inline const char* WhyNot(const In& in) {
    if (!in.ticked) return "[ceiling] game_ceiling=0";
    if (!in.f4) return "not VR F4";
    if (!in.wanted) return "no game ceiling wanted here ([ceiling] enabled, first person, an area on [ceiling] game_ceiling_biomes with a kind of its own, no menu)";
    if (!in.heroOk) return "no hero yet";
    return "";
}

// The slab given up on: its unit refused or missing (tried, no definition), or its placings run out (PlaceInstance giving nothing, or 5
// that made no model)
inline bool GivenUp(bool loadTried, bool haveDef, int tries) { return (loadTried && !haveDef) || tries > 4; }
// The ceiling is the game's: ticked, VR F4, a ceiling of the game's wanted here ([ceiling] game_ceiling_biomes, a kind), the slab not given
// up on, our hook on the game not failed - whether or not it is laid yet
inline bool ByGame(bool ticked, bool f4, bool caveWanted, bool givenUp, bool hookFailed) {
    return ticked && f4 && caveWanted && !givenUp && !hookFailed;
}
// Who draws it this frame: 0 nobody (no ceiling wanted here, or the sky's area), 1 the shader's vault (`wanted`: an area on [ceiling]
// biomes, with its own settings), 2 the game's - its slab not laid yet, nothing drawn meanwhile (never the old vault for a moment), 3 the
// game's, laid.
inline int Drawer(bool skyHere, bool wanted, bool byGame, bool laid) { return skyHere ? 0 : byGame ? (laid ? 3 : 2) : wanted ? 1 : 0; }

// World units one tile spans: the shader ceiling's picture, 4 of its tiles ([ceiling] scale)
inline float TileWorld(float scale) { return 4.0f * (scale > 0.0f ? scale : 10.0f); }

// The area's floor: the lowest the hero has stood since the area changed (the shader ceiling's rule: on a ledge the ceiling rose with
// him). `gen` the area's generation now; `seen` the one `floor` was taken in.
inline float Floor(float* floor, unsigned* seen, unsigned gen, float heroY) {
    if (*seen != gen) { *seen = gen; *floor = heroY; }
    if (heroY < *floor) *floor = heroY;
    return *floor;
}

// Its local matrix (row vectors, the place in [12..14]): shown - its top `height` over `floorY`, scaled to `tileWorld` units a tile, its
// middle on the whole tile nearest the hero (every tile is the same: walking, the rock stays where it is in the world); put away - a
// thousandth of its size, 5000 units under the hero.
inline void Matrix(const float hero[3], float floorY, float height, float tileWorld, bool shown, float m[16]) {
    const float k = shown ? tileWorld / kTile : 0.001f;
    float x = hero[0], y = hero[1] - 5000.0f, z = hero[2];
    if (shown) {
        x = (float)(std::floor((double)hero[0] / tileWorld + 0.5) * tileWorld);
        z = (float)(std::floor((double)hero[2] / tileWorld + 0.5) * tileWorld);
        y = floorY + height;
    }
    const float out[16] = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, x, y, z, 1};
    std::memcpy(m, out, sizeof out);
}

// The wall's reach: from the slab's top down to `floorDepth` ([ceiling] floor_depth, the shader ceiling's stone under the floor) below the
// area's floor - so it closes the void down to wherever the drawn floor ends
inline float WallReach(float height, float floorDepth) { return height + (floorDepth > 0.0f ? floorDepth : 0.0f); }

// The wall's local matrix: shown - its middle at the hero, its top `height` over `floorY` (the slab's), scaled round to `tileWorld` units a
// tile as the slab and down to `reach`; put away as the slab.
inline void WallMatrix(const float hero[3], float floorY, float height, float tileWorld, float reach, bool shown, float m[16]) {
    const float k = shown ? tileWorld / kTile : 0.001f, ky = shown ? reach / kWallHeight : 0.001f;
    const float out[16] = {k, 0, 0, 0, 0, ky, 0, 0, 0, 0, k, 0, hero[0], shown ? floorY + height : hero[1] - 5000.0f, hero[2], 1};
    std::memcpy(m, out, sizeof out);
}

// A matrix laid now moved off the last one (another tile, the floor, parked or shown): the renderer must not take it for motion (DLSS's
// motion vectors, frame generation) - its world is written over the previous one as well. A slab never moves smoothly: it stands still
// between snaps. The wall (`followsHero`) goes across with the hero every frame - real motion, kept - so only its other moves count.
inline bool Jumped(const float last[16], const float now[16], bool followsHero = false) {
    for (int i = 0; i < 16; ++i) {
        if (followsHero && (i == 12 || i == 14)) continue;
        if (std::fabs(last[i] - now[i]) > 1e-4f * (1.0f + std::fabs(now[i]))) return true;
    }
    return false;
}

// A room's height: its own height_<biome> once set (> 0) wins; not set (0, or no key), [ceiling] height_from_walls puts it on the room's
// walls' top where they are measured (> 0) - a rock that hangs `relief` model units (scaled by `k`, the slab's) rests its lowest on the
// walls; else kHeightAlone.
constexpr float kHeightAlone = 18.0f;
inline float HeightOf(float own, bool fromWalls, float wallsTop, float relief, float k) {
    return own > 0.0f ? own : fromWalls && wallsTop > 0.0f ? wallsTop + relief * k : kHeightAlone;
}

// A unit's file (ours: written by the installer beside the models): `name` (d2r_vr_<name>), its model and its three maps (`maps` without
// "_ALB.texture" ...). lightMask 19 and shadowMask 0 as the game's own cave pieces that cast none (act1/caves presets): its lights reach
// it, it throws no shadow.
inline std::string UnitJsonOf(const std::string& name, const std::string& model, const std::string& maps) {
    return R"({"dependencies":{"particles":[],"models":[{"path":")" + model + R"("}],)"
           R"("skeletons":[],"animations":[],"textures":[{"path":")" + maps + "_ALB.texture" + R"("},)"
           R"({"path":")" + maps + "_NRM.texture" + R"("},)"
           R"({"path":")" + maps + "_ORM.texture" + R"("}],"physics":[],"json":[],"variantdata":[],)"
           R"("objecteffects":[],"other":[]},"type":"UnitDefinition","name":"d2r_vr_)" + name + R"(","entities":[{"type":"Entity",)"
           R"("name":"entity_root","id":1000000003,"components":[{"type":"UnitRootComponent","name":"component_root",)"
           R"("state_machine_filename":"","doNotInheritRotation":false,"rotationOverride":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},)"
           R"("doNotUseHDHeight":true,"hideAllMeshWhenInOpenedMode":false,"onCreateEventName":"","animations":[]},)"
           R"({"type":"TransformDefinitionComponent","name":"entity_root_TransformDefinition","position":{"x":0.0,"y":0.0,"z":0.0},)"
           R"("orientation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},"scale":{"x":1.0,"y":1.0,"z":1.0},"inheritOnlyPosition":false}]},)"
           R"({"type":"Entity","name":"entity_model","id":1000000004,"components":[{"type":"ModelDefinitionComponent",)"
           R"("name":"component_model_)" + name + R"(","filename":")" + model + R"(",)"
           R"("visibleLayers":1,"lightMask":19,"shadowMask":0,"ghostShadows":false,"floorModel":false,"terrainBlendEnableYUpBlend":false,)"
           R"("terrainBlendMode":1}]}]})";
}
// A kind's slab, or its wall, naming its own model and its maps
inline std::string UnitJson(int kind, bool wall) {
    const Kind& k = kKinds[kind];
    const std::string name = kind == 0 ? (wall ? "cave_ceiling_wall" : "cave_ceiling") : std::string(k.name) + (wall ? "_wall" : "_ceiling");
    return UnitJsonOf(name, ModelPath(wall ? k.wall : k.slab), k.maps);
}
inline std::string UnitJson(bool wall = false) { return UnitJson(0, wall); }   // (the act 1 cave's)

// A unit's file as read back, checked before the game is given it ("" when fine): the game asserts on a unit with no root or two
// (vr/sky_dome.h UnitFileProblem there).
inline const char* UnitFileProblem(const std::string& text) {
    if (text.empty()) return "the file is not there (or empty)";
    if (text.find("\"type\":\"UnitDefinition\"") == std::string::npos) return "not a UnitDefinition";
    size_t roots = 0;
    for (size_t at = text.find("\"UnitRootComponent\""); at != std::string::npos; at = text.find("\"UnitRootComponent\"", at + 1)) ++roots;
    if (roots == 0) return "no entity with a UnitRootComponent (the game asserts, then crashes)";
    if (roots > 1) return "more than one UnitRootComponent (the game asserts)";
    if (text.find("\"ModelDefinitionComponent\"") == std::string::npos) return "no ModelDefinitionComponent (nothing to draw)";
    return nullptr;
}

// The installer's receipt (steamvr/prepare_game_ceiling.py: pcvr-game-ceiling.json beside the mod's .mpq folder) names a file: its
// "files" keys, as relative paths - from the mod's folder ("D2R-PCVR.mpq/data/...") or from the .mpq folder ("data/...") - with either
// slash (a backslash written escaped in the JSON, "\\", counts once). `rel` from the .mpq folder ("data/hd/..."). Case is ignored (the
// game's paths are not case-sensitive).
inline bool ReceiptNames(const std::string& receipt, const std::string& rel) {
    auto norm = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\') {
                while (i + 1 < s.size() && s[i + 1] == '\\') ++i;
                out += '/';
            } else {
                out += (char)std::tolower((unsigned char)s[i]);
            }
        }
        return out;
    };
    const std::string text = norm(receipt), want = norm(rel);
    for (size_t at = text.find(want); at != std::string::npos; at = text.find(want, at + 1)) {
        const bool start = at > 0 && (text[at - 1] == '"' || (at >= 5 && text.compare(at - 5, 5, ".mpq/") == 0));
        const bool end = at + want.size() < text.size() && text[at + want.size()] == '"';
        if (start && end) return true;
    }
    return false;
}

}  // namespace gameceiling
