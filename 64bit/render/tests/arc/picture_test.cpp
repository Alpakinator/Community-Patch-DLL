// c5r_arc_picture - the arc drawn by c5r at 0179's cameras (ticket 0199): stand-alone pictures for the comparison with
// stock (render/evidence/0199/picture_0199.py; DIAGNOSTIC pictures, D112: c5r's arc drawn by a test program, not the full
// shipped system).
// Inputs: C5R_EXE (ArrowShader), C5R_ARC_REF (mkref_arc.py with --stock3804: frame2846's mesh and constants, the Tiny
// map's grid and height field, 3804's cameras a1 and a2 with their stock pictures). The arcs' end points come from the
// pairs' plots through the map (spawnPoints), the mesh from c5r (Arrow grown to the end). Outputs under C5R_WORK/arc
// (game data, never committed):
//   f2846_alone.rgba, f2846_stockmesh.rgba   frame2846's camera: c5r's arc and stock's captured mesh through the same
//                                            draws, on (0, 0, 0, 0) with a cleared depth (the mesh's own effect)
//   NAME_alone.rgba, NAME_over_off.rgba      per camera a1, a2: c5r's arc alone, and drawn over stock's picture without
//                                            the arc (depth cleared: nothing in front of the arc)
//   picture.txt                              the figures of the frame2846 pair
// Own code; no game bytes.
#include "arc_testutil.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "testkit.h"

using namespace c5r;
using namespace c5r::arc;

namespace {

bool render(arctest::Device& d, Renderer& rr, const float vp[16], uint32_t W, uint32_t H, const Mesh& m,
            const uint8_t* background, std::vector<uint8_t>& out)
{
    arctest::Target t;
    if (!t.create(d.dev, W, H, background)) return false;
    if (!background) {
        const float clear[4] = {0, 0, 0, 0};
        t.clear(d.ctx, clear);
    } else {
        d.ctx->ClearDepthStencilView(t.dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
    DrawInput in;
    std::memcpy(in.viewProj, vp, 64);
    in.colour = t.rtv;
    in.depth = t.dsv;
    in.width = W;
    in.height = H;
    DrawStats st;
    Status s = rr.drawMesh(d.ctx, in, m, Settings(), &st);
    if (!s.ok()) {
        test::fail(__FILE__, __LINE__, "drawMesh: " + s.str());
        return false;
    }
    return t.readColour(d.dev, d.ctx, out);
}

// The arc of a pair of plots as the server will make it: spawnPoints, then the record grown to its end.
bool arcOf(const heightfield::HeightGrid& g, const int32_t p[4], Mesh& out)
{
    float st[3], en[2];
    Status s = spawnPoints(g, p[0], p[1], p[2], p[3], nullptr, st, en);
    if (!s.ok()) {
        test::fail(__FILE__, __LINE__, "spawnPoints: " + s.str());
        return false;
    }
    Arrow a;
    CHECK_OK(a.spawn(st, en));
    CHECK_OK(a.update(1.0f));   // longer than anim_speed: fully grown
    CHECK(!a.growing() && a.visible());
    out = a.mesh();
    return true;
}

} // namespace

C5R_TEST(pictures_at_0179_cameras)
{
    const std::string exe = test::exePath();
    arctest::Ref r;
    std::string why;
    if (!r.load(arctest::refRoot(), why)) {
        test::fail(__FILE__, __LINE__, "0179's recorded arc not found: " + why);
        return;
    }
    heightfield::HeightGrid g;
    if (!r.grid(g, why) || !r.has("pics")) {
        test::fail(__FILE__, __LINE__, "the reference has no grid or pictures (mkref_arc.py --stock3804): " + why);
        return;
    }
    const uint32_t W = static_cast<uint32_t>(r.num("pics", 0)), H = static_cast<uint32_t>(r.num("pics", 1));
    arctest::Device d;
    CHECK(d.create());
    Renderer rr;
    CHECK_OK(rr.init(d.dev, std::wstring(exe.begin(), exe.end())));
    if (!rr.ready()) return;
    const std::string dir = test::workDir() + "/arc";
    CreateDirectoryA(test::workDir().c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);

    // frame2846's camera: c5r's arc against stock's captured mesh, through the same draws
    float vp[16];
    std::memcpy(vp, r.cb.data(), 64);
    const int32_t plots[4] = {static_cast<int32_t>(r.num("plots", 0)), static_cast<int32_t>(r.num("plots", 1)),
                              static_cast<int32_t>(r.num("plots", 2)), static_cast<int32_t>(r.num("plots", 3))};
    Mesh ours;
    if (!arcOf(g, plots, ours)) return;
    Mesh stock;
    const size_t nv = static_cast<size_t>(r.num("counts", 0)), ni = static_cast<size_t>(r.num("counts", 1));
    CHECK(r.vb.size() >= nv * 40 && r.ib.size() >= ni * 4);
    stock.vertices.resize(nv);
    stock.indices.resize(ni);
    std::memcpy(stock.vertices.data(), r.vb.data(), nv * 40);
    std::memcpy(stock.indices.data(), r.ib.data(), ni * 4);
    std::vector<uint8_t> a, b;
    CHECK(render(d, rr, vp, W, H, ours, nullptr, a));
    CHECK(render(d, rr, vp, W, H, stock, nullptr, b));
    if (a.size() != b.size() || a.size() != static_cast<size_t>(W) * H * 4u) return;
    size_t covA = 0, covB = 0, onlyA = 0, onlyB = 0, differ = 0, over8 = 0;
    int maxd = 0;
    for (size_t i = 0; i < a.size(); i += 4) {
        const bool ca = a[i + 3] != 0, cb = b[i + 3] != 0;
        covA += ca;
        covB += cb;
        onlyA += ca && !cb;
        onlyB += cb && !ca;
        int m = 0;
        for (int k = 0; k < 4; ++k) m = std::max(m, std::abs(static_cast<int>(a[i + k]) - static_cast<int>(b[i + k])));
        differ += m != 0;
        over8 += m > 8;
        maxd = std::max(maxd, m);
    }
    char line[512];
    std::snprintf(line, sizeof line,
                  "frame2846 camera %ux%u: covered c5r %zu stock-mesh %zu, only c5r %zu, only stock-mesh %zu; pixels differing "
                  "%zu (> 8: %zu), max channel difference %d\n",
                  W, H, covA, covB, onlyA, onlyB, differ, over8, maxd);
    std::printf("%s", line);
    CHECK(arctest::writeFile(dir + "/f2846_alone.rgba", a.data(), a.size()));
    CHECK(arctest::writeFile(dir + "/f2846_stockmesh.rgba", b.data(), b.size()));
    CHECK(arctest::writeFile(dir + "/picture.txt", line, std::strlen(line)));
    CHECK(covA > 20000);
    CHECK(onlyA + onlyB <= covB / 1000);
    CHECK(over8 <= covB / 1000);

    // 3804's cameras a1 and a2: c5r's arc over stock's picture without it
    for (const char* name : {"a1", "a2"}) {
        int32_t p[4];
        double offDiff = 0;
        float cvp[16];
        if (!r.cam(name, p, cvp, offDiff)) {
            test::fail(__FILE__, __LINE__, std::string("no camera ") + name + " in the reference");
            continue;
        }
        std::vector<uint8_t> off;
        if (!arctest::readFile(r.dir + "/" + name + "_off.rgba", off) || off.size() != static_cast<size_t>(W) * H * 4u) {
            test::fail(__FILE__, __LINE__, std::string(name) + "_off.rgba missing or of another size");
            continue;
        }
        Mesh m;
        if (!arcOf(g, p, m)) continue;
        std::vector<uint8_t> alone, over;
        CHECK(render(d, rr, cvp, W, H, m, nullptr, alone));
        CHECK(render(d, rr, cvp, W, H, m, off.data(), over));
        size_t cov = 0;
        for (size_t i = 3; i < alone.size(); i += 4) cov += alone[i] != 0;
        std::printf("camera %s: pair (%d, %d) -> (%d, %d), %zu vertices, %zu pixels covered; stock's on/off camera "
                    "difference %.4g\n",
                    name, p[0], p[1], p[2], p[3], m.vertices.size(), cov, offDiff);
        CHECK(cov > 10000);
        CHECK(arctest::writeFile(dir + "/" + name + "_alone.rgba", alone.data(), alone.size()));
        CHECK(arctest::writeFile(dir + "/" + name + "_over_off.rgba", over.data(), over.size()));
    }
    std::printf("wrote the pictures into %s\n", dir.c_str());
}

C5R_TEST_MAIN
