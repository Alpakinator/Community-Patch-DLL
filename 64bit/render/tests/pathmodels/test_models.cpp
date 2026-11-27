// test_models.cpp - c5r::pathmodels layer 2 (ticket 0300): the 18 assets read from the install (C5R_GAME_DIR) with
// 0041's model reader, and the draw list against 3801's recorded draws (f6496_reference.h): index counts, vertex and
// index buffer digests, texture digests, world matrices, the SelectedAni clip. Plus the validation paths of the
// loader and the mesh repack on synthetic inputs (no game files).
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/draw.h"   // units::sha256Hex
#include "pathmodels_testkit.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::pathmodels;

namespace {

std::string sha(const void* p, size_t n) { return units::sha256Hex(static_cast<const uint8_t*>(p), n); }

struct Install {
    gamefs::GameFiles files;
    PathModels models;
    bool ok = false;
};

Install& install() {
    static Install in;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const std::string dir = c5r::test::gameDir();
        if (!dir.empty() && in.files.open(gamefs::fromUtf8(dir)).ok()) {
            Status s = in.models.load(in.files);
            if (!s.ok()) std::printf("PathModels::load: %s\n", s.str().c_str());
            in.ok = s.ok();
        }
    }
    if (c5r::test::gameDir().empty()) c5r::test::skip("C5R_GAME_DIR not set");
    return in;
}

// The SHA-256 of a texture as the EXE uploads it: the DDS payload (legacy 128-byte header) with red and blue swapped
// (B8G8R8A8 in the file, R8G8B8A8 on the GPU; path-models.md 2.2).
std::string uploadedDigest(const std::vector<uint8_t>& dds) {
    if (dds.size() < 128 || std::memcmp(dds.data() + 84, "DX10", 4) == 0) return "not a legacy DDS";
    std::vector<uint8_t> p(dds.begin() + 128, dds.end());
    for (size_t i = 0; i + 3 < p.size(); i += 4) std::swap(p[i], p[i + 2]);
    return sha(p.data(), p.size());
}

}  // namespace

C5R_TEST(load_from_install) {
    Install& in = install();
    CHECK(in.ok);
    if (!in.ok) return;
    for (uint32_t a = 0; a < kAssetCount; ++a) {
        const PathAsset& pa = in.models.asset(static_cast<Asset>(a));
        CHECK_EQ(pa.meshes.size(), 2u);   // every path asset is two rigid meshes (path-models.md 2.1)
        CHECK_EQ(pa.animated, a >= 6 && a <= 11);
        for (const PathMesh& m : pa.meshes) {
            CHECK_EQ(m.vertices.size(), static_cast<size_t>(m.vertexCount) * kVertexStride);
            CHECK(m.texture[2] == "cubelight_generic_32.dds");
        }
    }
    const std::vector<std::string> tex = textureNames(in.models);
    CHECK(tex.size() >= 8u);
}

// Every one of the 54 draws: the instance list of the recorded nodes, one draw per mesh, against the capture.
C5R_TEST(f6496_draws_match_the_capture) {
    Install& in = install();
    if (!in.ok) {
        CHECK(in.ok);
        return;
    }
    std::vector<Instance> inst;
    CHECK_OK(placePath(pmt::f6496Input(), inst));
    // The clip time of the capture is unknown: find the t at which SelectedAni's ring (draw 820) has the captured
    // rotation (a scan at the clip's own step, 1/60 s, then 1/6000 s around the best).
    const PathAsset& sel = in.models.asset(Asset::SelectedAniThreeTurns);
    const f6496::Draw& ring = pmt::draw(820);
    float bestT = 0, best = 1e9f;
    auto errAt = [&](float t) {
        Mat4 m;
        if (!in.models.meshMatrix(Asset::SelectedAniThreeTurns, 1, t, m).ok()) return 1e9f;
        float e = 0;
        for (int k = 0; k < 12; ++k) e = std::fmax(e, std::fabs(m.m[k] - ring.world[k]));
        return e;
    };
    for (float t = 0; t < sel.duration; t += 1.0f / 60.0f) {
        const float e = errAt(t);
        if (e < best) best = e, bestT = t;
    }
    const float c = bestT;
    for (float t = c - 1.0f / 60.0f; t < c + 1.0f / 60.0f; t += 1.0f / 6000.0f) {
        const float e = errAt(t);
        if (e < best) best = e, bestT = t;
    }
    std::printf("SelectedAni clip: duration %.4f s, best t %.4f s, max rotation difference %.3g\n", sel.duration, bestT,
                best);
    CHECK(best < 2e-3f);
    std::vector<DrawItem> draws;
    CHECK_OK(buildDraws(in.models, inst, bestT, draws));
    const size_t n = sizeof(f6496::kDraws) / sizeof(f6496::kDraws[0]);
    CHECK_EQ(draws.size(), n);
    if (draws.size() != n) return;
    std::map<std::string, std::string> texDigest;
    for (size_t i = 0; i < n; ++i) {
        const f6496::Draw& ref = f6496::kDraws[i];
        const DrawItem& d = draws[i];
        const PathMesh& m = in.models.asset(d.asset).meshes[d.mesh];
        const std::string where = "draw " + std::to_string(ref.n) + " (" + assetName(d.asset) + " mesh " +
                                  std::to_string(d.mesh) + " " + m.name + ")";
        if (d.indexCount != ref.indices) c5r::test::fail(__FILE__, __LINE__, where + ": index count");
        if (sha(m.vertices.data(), m.vertices.size()) != ref.vb)
            c5r::test::fail(__FILE__, __LINE__, where + ": vertex buffer differs from the capture's");
        if (sha(m.indices.data(), m.indices.size() * 2) != ref.ib)
            c5r::test::fail(__FILE__, __LINE__, where + ": index buffer differs from the capture's");
        for (int k = 0; k < 3; ++k) {
            const std::string& name = m.texture[k];
            if (!texDigest.count(name)) {
                std::vector<uint8_t> bytes;
                texDigest[name] = in.files.read(name, bytes).ok() ? uploadedDigest(bytes) : "unreadable";
            }
            if (texDigest[name] != ref.t[k])
                c5r::test::fail(__FILE__, __LINE__, where + ": t" + std::to_string(k) + " " + name +
                                                        " is not the captured texture");
        }
        std::string why;
        if (!pmt::matNear(d.world.m, ref.world, 2e-3f, 2e-3f, &why))
            c5r::test::fail(__FILE__, __LINE__, where + ": world matrix: " + why);
    }
}

C5R_TEST(mesh_matrix_static_is_identity) {
    Install& in = install();
    if (!in.ok) {
        CHECK(in.ok);
        return;
    }
    Mat4 m;
    CHECK_OK(in.models.meshMatrix(Asset::CenterOneTurn, 0, 3.0f, m));
    const Mat4 id;
    CHECK_EQ(pmt::maxDiff(m.m, id.m, 16), 0.0f);
    CHECK_ERR(in.models.meshMatrix(Asset::CenterOneTurn, 2, 0.0f, m), Err::BadLayout);
    CHECK_ERR(in.models.meshMatrix(static_cast<Asset>(18), 0, 0.0f, m), Err::BadLayout);
    // the clip loops: t and t + duration give the same matrix
    const float d = in.models.asset(Asset::SelectedAniOneTurn).duration;
    Mat4 a, b;
    CHECK_OK(in.models.meshMatrix(Asset::SelectedAniOneTurn, 1, 1.25f, a));
    CHECK_OK(in.models.meshMatrix(Asset::SelectedAniOneTurn, 1, 1.25f + d, b));
    CHECK(pmt::maxDiff(a.m, b.m, 16) < 1e-4f);
}

// ---- loader validation paths (synthetic reader, no game files)

namespace {
FileReader mapReader(std::map<std::string, std::string> files) {
    return [files](const std::string& name, std::vector<uint8_t>& out) -> Status {
        auto it = files.find(name);
        if (it == files.end()) return Status::fail(Err::NotFound, "no such file");
        out.assign(it->second.begin(), it->second.end());
        return Status::Ok();
    };
}
}  // namespace

C5R_TEST(neg_load_missing_file) {
    PathModels m;
    CHECK_ERR(m.load(mapReader({})), Err::NotFound);
    CHECK(!m.loaded());
    std::vector<DrawItem> d;
    CHECK_ERR(buildDraws(m, {}, 0.0f, d), Err::NotFound);
}

C5R_TEST(neg_load_bad_fxsxml) {
    PathModels m;
    CHECK_ERR(m.load(mapReader({{"centeroneturn.fxsxml", "<Asset><Mesh file=\"a.gr2\"></Asset"}})), Err::BadLayout);
    CHECK_ERR(m.load(mapReader({{"centeroneturn.fxsxml", "<Asset></Asset>"}})), Err::BadLayout);
    CHECK_ERR(m.load(mapReader({{"centeroneturn.fxsxml",
                                 "<Asset><Mesh file=\"a.gr2\"/><Mesh file=\"b.gr2\"/></Asset>"}})),
              Err::BadLayout);
    CHECK(!m.loaded());
}

C5R_TEST(neg_load_bad_gr2) {
    PathModels m;
    CHECK_ERR(m.load(mapReader({{"centeroneturn.fxsxml", "<Asset><Mesh file=\"CenterOneTurn.gr2\"/></Asset>"},
                                {"centeroneturn.gr2", std::string(200, 'x')}})),
              Err::BadLayout);
    CHECK_ERR(m.load(mapReader({{"centeroneturn.fxsxml", "<Asset><Mesh file=\"CenterOneTurn.gr2\"/></Asset>"}})),
              Err::NotFound);
    CHECK(!m.loaded());
}

// ---- repackMesh on a synthetic mesh

namespace {
struct Synth {
    c5m::FileInfo fi;
    c5m::Mesh mesh;
};
Synth synth() {
    Synth s;
    c5m::Material mat;
    mat.name = "m";
    mat.shader_set = "UIShader";
    const char* usage[3] = {"BaseTextureMap", "SREFMap", "IrradianceCubeMap"};
    const char* file[3] = {"a.dds", "b.dds", "cube.dds"};
    for (int k = 0; k < 3; ++k) {
        c5m::TextureMap t;
        t.usage = usage[k];
        t.basename = file[k];
        mat.maps.push_back(t);
    }
    s.fi.materials.push_back(mat);
    c5m::Mesh& m = s.mesh;
    m.name = "tri";
    const char* comp[3] = {"Position", "Normal", "TextureCoordinates0"};
    for (int k = 0; k < 3; ++k) {
        c5m::VertexComponent c;
        c.type = c5m::MemberType::Real32;
        c.name = comp[k];
        c.count = 3;
        c.offset = 12u * k;
        m.format.components.push_back(c);
    }
    m.format.stride = 36;
    m.vertex_count = 3;
    m.vertices.assign(3 * 36, 0);
    for (int v = 0; v < 3; ++v)
        for (int f = 0; f < 9; ++f) {
            const float x = static_cast<float>(v * 10 + f);
            std::memcpy(&m.vertices[v * 36 + f * 4], &x, 4);
        }
    m.index_size = 2;
    m.indices = {0, 1, 2};
    c5m::TriGroup g;
    g.tri_count = 1;
    m.groups.push_back(g);
    m.materials.push_back(0);
    return s;
}
}  // namespace

C5R_TEST(repack_synthetic) {
    Synth s = synth();
    PathMesh pm;
    CHECK_OK(repackMesh(s.fi, s.mesh, pm));
    CHECK_EQ(pm.vertexCount, 3u);
    CHECK_EQ(pm.vertices.size(), 96u);
    float v1[8];
    std::memcpy(v1, &pm.vertices[32], 32);
    // vertex 1: position 10 11 12, normal 13 14 15, uv 16 17 (the third texture coordinate dropped)
    for (int k = 0; k < 8; ++k) CHECK_EQ(v1[k], static_cast<float>(10 + k));
    CHECK(pm.texture[0] == "a.dds" && pm.texture[1] == "b.dds" && pm.texture[2] == "cube.dds");
    // no SREFMap (SelectedAniAttack's centre): accepted, t1 empty
    s.fi.materials[0].maps.erase(s.fi.materials[0].maps.begin() + 1);
    CHECK_OK(repackMesh(s.fi, s.mesh, pm));
    CHECK(pm.texture[1].empty() && pm.texture[2] == "cube.dds");
}

C5R_TEST(neg_repack) {
    PathMesh pm;
    {
        Synth s = synth();
        s.mesh.format.components.erase(s.mesh.format.components.begin() + 1);   // no Normal
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.mesh.format.components[0].type = c5m::MemberType::Real16;
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.fi.materials[0].shader_set = "Building";
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.fi.materials[0].maps.erase(s.fi.materials[0].maps.begin());   // no BaseTextureMap
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.fi.materials[0].maps.pop_back();   // no IrradianceCubeMap
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.mesh.groups.push_back(s.mesh.groups[0]);
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.mesh.indices[2] = 3;
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.mesh.indices.clear();
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.mesh.vertices.resize(50);
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::BadLayout);
    }
    {
        Synth s = synth();
        s.mesh.materials[0] = 5;
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
    {
        Synth s = synth();
        s.mesh.vertex_count = 70000;
        CHECK_ERR(repackMesh(s.fi, s.mesh, pm), Err::Unsupported);
    }
}

C5R_TEST_MAIN
