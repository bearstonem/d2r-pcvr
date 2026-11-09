// The game-drawn ceiling's golden writer: the source's own Granny remesher (third_party/natpoh_ceiling/granny_remesh.h, MIT)
// makes the cases steamvr/prepare_game_ceiling_test.py makes with steamvr/granny_write.py, and the test compares the bytes.
// Test-only (built by that test with g++); never part of the mod.
//   game_ceiling_golden <in.model> <out folder>
// writes <out>/<case>.mesh (the mesh's vertices then its indices) and <out>/<case>.model (the input remeshed and written).
#include "../third_party/natpoh_ceiling/granny_remesh.h"

#include <cstdio>
#include <string>

using namespace grannyremesh;

static bool ReadAll(const char* path, std::string* out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out->clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

static bool WriteAll(const std::string& path, const void* p, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(p, 1, n, f) == n;
    return std::fclose(f) == 0 && ok;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: game_ceiling_golden <in.model> <out folder>\n"); return 2; }
    std::string in;
    if (!ReadAll(argv[1], &in)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    const std::string out = argv[2];
    const Renames maps = {{"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_alb.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_ALB.texture"},
                          {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_nrm.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_NRM.texture"},
                          {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_orm.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_ORM.texture"}};
    struct Case { const char* name; Mesh mesh; int lod; };
    Case cases[] = {
        {"slab", CeilingMesh(40.0f, 6, 6, 6.0f), 2},           // the cave's rock (kind 0, salt 1)
        {"flat", CeilingMesh(40.0f, 6, 6, 0.0f), 0},           // a built room's (kind 3, salt 7)
        {"wall", WallMesh(40.0f * 6 - 40.0f * 0.5f - 6.0f, 40.0f, 96, 4, 40.0f), 4},   // the cave's rim (kind 0's wall, salt 2)
    };
    Salt(&cases[0].mesh, 1);
    Salt(&cases[1].mesh, 7);
    Salt(&cases[2].mesh, 2);
    for (Case& c : cases) {
        std::string mesh((const char*)c.mesh.v.data(), c.mesh.v.size() * sizeof(Vertex));
        mesh.append((const char*)c.mesh.idx.data(), c.mesh.idx.size() * sizeof(uint16_t));
        if (!WriteAll(out + "/" + c.name + ".mesh", mesh.data(), mesh.size())) return 3;
        File f;
        std::string why;
        if (!Read(in, &f, &why) || !Remesh(f, c.mesh, maps, c.lod, nullptr, &why)) { std::fprintf(stderr, "%s: %s\n", c.name, why.c_str()); return 4; }
        const std::string made = Write(f);
        if (!WriteAll(out + "/" + c.name + ".model", made.data(), made.size())) return 3;
    }
    return 0;
}
