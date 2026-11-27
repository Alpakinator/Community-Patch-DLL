// gr2_facts.cpp - ticket 4872's look at a BuildingShader model file (not a test): models, meshes, vertex formats,
// tri groups, materials (shader set, maps, scalar ExtendedData) and the first vertices, read from the install at run
// time (C5R_GAME_DIR). Prints counts and names only; nothing of the file is written anywhere (D83).
//
//     c5r_gr2_facts <file.gr2 | file.fxsxml> [vertices to print]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5models/gr2.h"
#include "c5r/gamefs/gamefiles.h"

using namespace c5r;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: c5r_gr2_facts <file.gr2|file.fxsxml> [vertices]\n");
        return 2;
    }
    const char* game = std::getenv("C5R_GAME_DIR");
    if (!game || !*game) {
        std::fprintf(stderr, "C5R_GAME_DIR not set\n");
        return 2;
    }
    const int nv = argc > 2 ? std::atoi(argv[2]) : 2;
    gamefs::GameFiles files;
    Status s = files.open(gamefs::fromUtf8(game));
    if (!s.ok()) {
        std::fprintf(stderr, "open: %s\n", s.str().c_str());
        return 1;
    }
    std::string name = c5m::ascii_lower(argv[1]);
    std::vector<uint8_t> bytes;
    if (name.size() > 7 && name.substr(name.size() - 7) == ".fxsxml") {
        s = files.read(name, bytes);
        if (!s.ok()) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), s.str().c_str());
            return 1;
        }
        c5m::FxsAsset fx;
        c5m::Error e;
        if (!c5m::parse_fxsxml(reinterpret_cast<const char*>(bytes.data()), bytes.size(), fx, &e)) {
            std::fprintf(stderr, "fxsxml: %s\n", e.what.c_str());
            return 1;
        }
        std::printf("fxsxml %s: mesh %s, %zu animations, %zu textures\n", name.c_str(), fx.mesh.c_str(),
                    fx.animations.size(), fx.textures.size());
        name = c5m::basename_lower(fx.mesh);
    }
    std::string origin;
    s = files.read(name, bytes, &origin);
    if (!s.ok()) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), s.str().c_str());
        return 1;
    }
    c5m::FileInfo fi;
    c5m::Error e;
    c5m::Header h;
    c5m::StringDatabase strings;
    const c5m::StringDatabase* sdb = nullptr;
    if (c5m::parse_header(bytes.data(), bytes.size(), h, &e) && h.string_db_crc != 0) {
        for (const std::string& n : files.names()) {
            if (n.size() < 4 || n.substr(n.size() - 4) != ".gsd") continue;
            std::vector<uint8_t> g;
            if (files.read(n, g).ok()) strings.add_gsd(g.data(), g.size(), nullptr);
        }
        std::printf("string database crc %08x: %zu strings from the .gsd files\n", h.string_db_crc, strings.size());
        sdb = &strings;
    }
    if (!c5m::read_gr2(bytes.data(), bytes.size(), sdb, fi, &e)) {
        std::fprintf(stderr, "read_gr2: %s %s\n", c5m::err_name(e.code), e.what.c_str());
        return 1;
    }
    std::printf("%s (%s, %zu B): %zu models, %zu meshes, %zu materials, %zu skeletons, %zu animations\n", name.c_str(),
                origin.c_str(), bytes.size(), fi.models.size(), fi.meshes.size(), fi.materials.size(),
                fi.skeletons.size(), fi.animations.size());
    std::printf("basis up %g %g %g right %g %g %g back %g %g %g units/m %g\n", fi.up[0], fi.up[1], fi.up[2], fi.right[0],
                fi.right[1], fi.right[2], fi.back[0], fi.back[1], fi.back[2], fi.units_per_meter);
    for (size_t i = 0; i < fi.materials.size(); ++i) {
        const c5m::Material& m = fi.materials[i];
        std::printf("material %zu %s set '%s':", i, m.name.c_str(), m.shader_set.c_str());
        for (auto& t : m.maps) std::printf(" %s=%s", t.usage.c_str(), t.basename.c_str());
        std::printf("\n");
        for (auto& x : m.extended) std::printf("    ext %s = %s\n", x.first.c_str(), x.second.c_str());
    }
    for (size_t i = 0; i < fi.models.size(); ++i) {
        const c5m::ModelEntry& m = fi.models[i];
        std::printf("model %zu %s skeleton %d meshes:", i, m.name.c_str(), m.skeleton);
        for (int32_t k : m.meshes) std::printf(" %d", k);
        std::printf("\n");
    }
    uint32_t vsum = 0, isum = 0;
    for (size_t i = 0; i < fi.meshes.size(); ++i) {
        const c5m::Mesh& m = fi.meshes[i];
        std::printf("mesh %zu %s: v %u (start %u) i %zu (start %u) isize %u stride %u fmt %s bones %zu\n", i,
                    m.name.c_str(), m.vertex_count, vsum, m.indices.size(), isum, m.index_size, m.format.stride,
                    m.format.key().c_str(), m.bone_bindings.size());
        for (auto& g : m.groups)
            std::printf("    group material %u (%s) tri %u +%u\n", g.material,
                        g.material < m.material_names.size() ? m.material_names[g.material].c_str() : "?",
                        g.tri_first, g.tri_count);
        for (int v = 0; v < nv && v < static_cast<int>(m.vertex_count); ++v) {
            std::printf("    v%d", v);
            for (auto& c : m.format.components) {
                float f[4];
                const uint32_t n = m.read(static_cast<uint32_t>(v), c.name.c_str(), f);
                std::printf(" %s(", c.name.c_str());
                for (uint32_t k = 0; k < n; ++k) std::printf("%s%.6g", k ? "," : "", f[k]);
                std::printf(")");
            }
            std::printf("\n");
        }
        if (!m.indices.empty()) {
            std::printf("    idx");
            for (size_t k = 0; k < 9 && k < m.indices.size(); ++k) std::printf(" %u", m.indices[k]);
            std::printf("\n");
        }
        vsum += m.vertex_count;
        isum += static_cast<uint32_t>(m.indices.size());
    }
    std::printf("total v %u i %u\n", vsum, isum);
    return 0;
}
