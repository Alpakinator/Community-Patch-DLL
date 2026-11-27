// c5r_arc_arc - tests of c5r::arc (ticket 0199): ArrowSettings.xml, the mesh, the arrow record, the end points, the
// constants and states; against 0179's recorded arc (C5R_ARC_REF, mkref_arc.py; game data, never committed); with the
// install's ArrowSettings.xml (C5R_GAME_DIR) and the EXE's ArrowShader (C5R_EXE). Own code; no game bytes.
#include "arc_testutil.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <c5r/shaders/shadersection.h>
#include <c5r/units/draw.h>
#include "testkit.h"

using namespace c5r;
using namespace c5r::arc;

namespace {

// My own XML, other values than the install's file (the parser is checked against the install in game_settings).
const char kXml[] =
    "<?xml version=\"1.0\"?>\n<!-- test -->\n<Arrow min_width = \"2.5\"\n max_width=\"6\" width2=\"1\" arc_height=\"40\"\n"
    "  outline_width=\".5\" outline_width2=\"0.25\" body_color=\"10, 20,30\" outline_color=\"1,2,3\" alpha=\".25\"\n"
    "  target_height=\"12.5\" anim_speed=\"2\" ambient_light_level=\"0.125\" />\n";

Status parse(const std::string& s, Settings& out, std::string* note = nullptr)
{
    return parseSettings(s.data(), s.size(), out, note);
}

float dist(const float* a, const float* b)
{
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

const float kStart[3] = {100.0f, -50.0f, 18.0f};
const float kEnd[3] = {36.0f, 60.0f, 20.0f};

} // namespace

// ---------------------------------------------------------------------------------------------------- settings

C5R_TEST(settings_parse_all_fields)
{
    Settings s;
    std::string note = "x";
    CHECK_OK(parse(kXml, s, &note));
    CHECK(note.empty());
    CHECK_EQ(s.minWidth, 2.5f);
    CHECK_EQ(s.maxWidth, 6.0f);
    CHECK_EQ(s.width2, 1.0f);
    CHECK_EQ(s.arcHeight, 40.0f);
    CHECK_EQ(s.outlineWidth, 0.5f);
    CHECK_EQ(s.outlineWidth2, 0.25f);
    CHECK_EQ(s.alpha, 0.25f);
    CHECK_EQ(s.targetHeight, 12.5f);
    CHECK_EQ(s.animSpeed, 2.0f);
    CHECK_EQ(s.ambient, 0.125f);
    CHECK_EQ(s.bodyColor, 0xFF1E140Au);      // bytes R 10, G 20, B 30, A 255
    CHECK_EQ(s.outlineColor, 0xFF030201u);
}

C5R_TEST(settings_missing_fields_keep_defaults)
{
    Settings s;
    std::string note;
    CHECK_OK(parse("<Arrow min_width=\"4\"/>", s, &note));
    CHECK_EQ(s.minWidth, 4.0f);
    const Settings d;
    CHECK_EQ(s.maxWidth, d.maxWidth);
    CHECK_EQ(s.bodyColor, d.bodyColor);
    CHECK(note.find("max_width") != std::string::npos);
    CHECK(note.find("body_color") != std::string::npos);
    CHECK(note.find("min_width") == std::string::npos);
}

C5R_TEST(settings_negatives)
{
    Settings s;
    s.minWidth = 123.0f;
    CHECK_ERR(parse("", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow min_width=\"3\"", s), Err::Truncated);
    CHECK_ERR(parse("<Other min_width=\"3\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow min_width=\"three\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow min_width=\"3x\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow min_width=\"1e99\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow body_color=\"255,0\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow body_color=\"255,0,256\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow body_color=\"255,0,0,0\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow anim_speed=\"0\"/>", s), Err::BadLayout);
    CHECK_ERR(parse("<Arrow width2=\"-1\"/>", s), Err::BadLayout);
    CHECK_EQ(s.minWidth, 123.0f);   // a failed parse leaves the output alone
}

// ---------------------------------------------------------------------------------------------------- the mesh

C5R_TEST(mesh_counts_and_indices)
{
    const Settings set;
    for (float t : {0.0f, 0.1f, 0.37f, 0.5f, 0.9f, 0.9999f, 1.0f, 5.0f}) {
        Mesh m;
        CHECK_OK(buildMesh(set, kStart, kEnd, t, m));
        CHECK(m.rings >= 1);
        CHECK_EQ(m.vertices.size(), static_cast<size_t>(m.rings) * kRingVertices + kHeadVertices);
        CHECK_EQ(m.indices.size(), static_cast<size_t>(m.rings - 1) * kRingJoinIndices + kHeadIndices);
        CHECK(m.vertices.size() <= kMaxVertices && m.indices.size() <= kMaxIndices);
        for (uint32_t i : m.indices) CHECK(i < m.vertices.size());
        CHECK(m.sEnd <= std::min(t, kMaxProgress) + 1e-6f);
        // the body grows with t until the early stop
        if (!m.earlyStop) CHECK(std::fabs(m.sEnd - std::min(t, kMaxProgress)) < 1e-6f);
    }
    Mesh a, b;
    CHECK_OK(buildMesh(set, kStart, kEnd, 0.0f, a));
    CHECK_EQ(a.rings, 1u);
    CHECK_OK(buildMesh(set, kStart, kEnd, 0.5f, b));
    CHECK_EQ(b.rings, 14u);   // s = 0, 0.04, ..., 0.48 and the clamped 0.5
    CHECK(std::fabs(b.sEnd - 0.5f) < 1e-6f);
}

C5R_TEST(mesh_ring_shape_and_colours)
{
    const Settings set;
    Mesh m;
    CHECK_OK(buildMesh(set, kStart, kEnd, kMaxProgress, m));
    CHECK(m.earlyStop);
    for (uint32_t r = 0; r < m.rings; ++r) {
        const Vertex* v = &m.vertices[r * kRingVertices];
        const float s = std::min(r * 0.04f, m.sEnd);
        const float w = s * (set.maxWidth - set.minWidth) + set.minWidth;
        CHECK(std::fabs(dist(v[0].pos, v[1].pos) - 2.0f * w) < 1e-3f);           // across the top face: 2 w
        CHECK(std::fabs(dist(v[2].pos, v[3].pos) - 2.0f * set.width2) < 1e-3f);   // the right face: 2 width2
        for (int i = 0; i < 8; ++i) {
            CHECK_EQ(v[i].color0, set.bodyColor);
            CHECK_EQ(v[i].color1, 0xFFFFFFFFu);
            const float* n = v[i].normal;
            CHECK(std::fabs(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) - 1.0f) < 1e-5f);
        }
        for (int i = 8; i < 16; ++i) {
            CHECK_EQ(v[i].color0, set.outlineColor);
            CHECK_EQ(v[i].color1, 0u);
            CHECK(v[i].normal[0] == 0 && v[i].normal[1] == 0 && v[i].normal[2] == 0);
            CHECK(v[i].uv[0] == 0 && v[i].uv[1] == 0);
        }
    }
    // ring 0 is centred on the start; the collar starts on the last ring
    const Vertex* v = m.vertices.data();
    for (int k = 0; k < 3; ++k) CHECK(std::fabs((v[0].pos[k] + v[4].pos[k]) * 0.5f - kStart[k]) < 1e-3f);
    const Vertex* last = &m.vertices[(m.rings - 1) * kRingVertices];
    const Vertex* collar = &m.vertices[m.rings * kRingVertices];
    CHECK(std::memcmp(last, collar, 8 * sizeof(Vertex)) == 0);
    // the head's tip lies within its own length of the end point (the early stop's rule)
    const float* tip = m.vertices[m.rings * kRingVertices + 28].pos;
    CHECK(dist(tip, kEnd) < 2.0f * std::sqrt(3.0f) * set.maxWidth + set.outlineWidth + 1.0f);
}

C5R_TEST(mesh_negatives_and_degenerate)
{
    const Settings set;
    Mesh m;
    m.rings = 77;
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float bad[3] = {nan, 0, 0}, badInf[3] = {0, inf, 0};
    CHECK_ERR(buildMesh(set, bad, kEnd, 0.5f, m), Err::BadLayout);
    CHECK_ERR(buildMesh(set, kStart, badInf, 0.5f, m), Err::BadLayout);
    CHECK_ERR(buildMesh(set, kStart, kEnd, nan, m), Err::BadLayout);
    CHECK_ERR(buildMesh(set, kStart, kEnd, -0.1f, m), Err::BadLayout);
    CHECK_ERR(buildMesh(set, nullptr, kEnd, 0.5f, m), Err::BadLayout);
    Settings bs;
    bs.arcHeight = nan;
    CHECK_ERR(buildMesh(bs, kStart, kEnd, 0.5f, m), Err::BadLayout);
    CHECK_EQ(m.rings, 77u);   // untouched on failure
    // start equal to the end: one ring and a head, every value finite
    CHECK_OK(buildMesh(set, kStart, kStart, kMaxProgress, m));
    CHECK_EQ(m.rings, 1u);
    for (const Vertex& v : m.vertices)
        for (int k = 0; k < 3; ++k) CHECK(std::isfinite(v.pos[k]) && std::isfinite(v.normal[k]));
}

// ---------------------------------------------------------------------------------------------------- the record

C5R_TEST(arrow_growth_clock)
{
    Settings set;   // anim_speed 0.5 s
    Arrow a(set);
    CHECK(!a.visible());
    CHECK_OK(a.update(0.1f));   // hidden: nothing
    CHECK_EQ(a.generation(), 0u);
    const float endXY[2] = {kEnd[0], kEnd[1]};
    CHECK_OK(a.spawn(kStart, endXY));
    CHECK_EQ(a.end()[2], set.targetHeight);
    CHECK(a.growing());
    CHECK_OK(a.update(0.125f));   // rebuild at 0, then progress 0.25 and rebuild again
    CHECK(a.visible());
    CHECK_EQ(a.generation(), 2u);
    CHECK(std::fabs(a.progress() - 0.25f) < 1e-6f);
    CHECK(std::fabs(a.mesh().sEnd - 0.25f) < 1e-6f);
    CHECK_OK(a.update(10.0f));
    CHECK(!a.growing());
    const uint64_t g = a.generation();
    CHECK_OK(a.update(0.016f));   // still: no rebuild
    CHECK_EQ(a.generation(), g);
    a.removeAll();
    CHECK(!a.visible());
    CHECK_OK(a.spawn(kStart, endXY));   // the next spawn reuses the record and starts again
    CHECK_OK(a.update(0.0f));
    CHECK(a.visible());
    CHECK_EQ(a.mesh().rings, 1u);
    CHECK_ERR(a.update(-1.0f), Err::BadLayout);
    CHECK_ERR(a.update(std::numeric_limits<float>::quiet_NaN()), Err::BadLayout);
    const float bad[2] = {std::numeric_limits<float>::infinity(), 0};
    CHECK_ERR(a.spawn(kStart, bad), Err::BadLayout);
    CHECK_EQ(a.end()[0], kEnd[0]);   // unchanged by the refused spawn
}

// ---------------------------------------------------------------------------------------------------- end points

namespace {

// A synthetic 4 x 4 plot grid with the textbook placement and a height ramp.
heightfield::HeightGrid smallGrid()
{
    heightfield::HeightGrid g;
    g.mapW = 4;
    g.mapH = 8;
    g.samplesX = 256;
    g.samplesY = 512;
    g.wrapX = true;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = 128.0f;
    g.oy = 200.0f;
    const float m[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 0, 32, 128, 0, 1};
    std::memcpy(g.hexToWorld, m, sizeof m);
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(x * 100 + y);
    return g;
}

} // namespace

C5R_TEST(plot_to_world_and_spawn_points)
{
    heightfield::HeightGrid g = smallGrid();
    float a[2], b[2], sa[2];
    plotToWorldXY(g, 2, 2, a, sa);
    plotToWorldXY(g, 1, 4, b);
    // hex (1, 2): sample (64 + 64 + 32, 128 + 128) = (160, 256)
    CHECK_EQ(sa[0], 160.0f);
    CHECK_EQ(sa[1], 256.0f);
    CHECK_EQ(a[0], 160.0f - 128.0f);
    CHECK(std::fabs(a[1] - (256.0f * 0.866f - 200.0f)) < 1e-4f);
    // two rows up and half a plot left: (-64, 2 x 64 x 0.866)
    CHECK(std::fabs((b[0] - a[0]) + 64.0f) < 1e-3f);
    CHECK(std::fabs((b[1] - a[1]) - 128.0f * 0.866f) < 1e-3f);
    CHECK_EQ(worldWidth(g), 255.0f);
    float st[3], en[2];
    CHECK_OK(spawnPoints(g, 2, 2, 1, 4, nullptr, st, en));
    CHECK_EQ(st[0], a[0]);
    CHECK_EQ(st[1], a[1]);
    CHECK(std::fabs(st[2] - (160.0f * 100 + 256) / 255.0f * 0.2f) < 1e-5f);   // the sample under the centre, truncated
    CHECK_EQ(en[0], b[0]);
    CHECK_EQ(en[1], b[1]);
    // a fractional centre takes the sample below it, not the nearest
    g.hexToWorld[12] = 32.75f;
    g.hexToWorld[13] = 128.75f;
    CHECK_OK(spawnPoints(g, 2, 2, 1, 4, nullptr, st, en));
    CHECK(std::fabs(st[2] - (160.0f * 100 + 256) / 255.0f * 0.2f) < 1e-5f);
    // negatives
    heightfield::HeightGrid e;
    CHECK_ERR(spawnPoints(e, 2, 2, 1, 4, nullptr, st, en), Err::BadLayout);
    heightfield::HeightGrid n = smallGrid();
    n.hexToWorld[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_ERR(spawnPoints(n, 2, 2, 1, 4, nullptr, st, en), Err::BadLayout);
    heightfield::HeightGrid s = smallGrid();
    s.h.pop_back();
    CHECK_ERR(spawnPoints(s, 2, 2, 1, 4, nullptr, st, en), Err::BadLayout);
    CHECK_ERR(spawnPoints(g, 2, 8, 1, 4, nullptr, st, en), Err::BadLayout);
    CHECK_ERR(spawnPoints(g, 2, -1, 1, 4, nullptr, st, en), Err::BadLayout);
    CHECK_ERR(spawnPoints(g, 2, 2, 1, 4, nullptr, nullptr, en), Err::BadLayout);
}

C5R_TEST(frustum_and_wrap)
{
    // an orthographic view of x in [-100, 100], y in [-100, 100], z in [0, 100] (row vectors)
    float vp[16] = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 1};
    float pl[6][4];
    CHECK_OK(frustumPlanes(vp, pl));
    const float W = 1000.0f;
    // start inside: kept. Stock's quirk: x1 - W is the nearest, yet x1 stays
    float s2[3] = {50, 0, 10}, e2[3] = {50 + W - 40, 0, 20};
    CHECK_OK(chooseWrap(pl, W, s2, e2));
    CHECK_EQ(s2[0], 50.0f);
    CHECK_EQ(e2[0], 50 + W - 40);
    // x1 + W the nearest: chosen
    float s3[3] = {50, 0, 10}, e3[3] = {60 - W, 0, 20};
    CHECK_OK(chooseWrap(pl, W, s3, e3));
    CHECK_EQ(e3[0], 60.0f);
    // start outside by more than 64, its copy one period left inside: the copy is taken
    float s4[3] = {W + 20, 0, 10}, e4[3] = {30, 0, 20};
    CHECK_OK(chooseWrap(pl, W, s4, e4));
    CHECK_EQ(s4[0], 20.0f);
    CHECK_EQ(e4[0], 30.0f);
    // within the 64 margin: kept
    float s5[3] = {150, 0, 10}, e5[3] = {140, 0, 20};
    CHECK_OK(chooseWrap(pl, W, s5, e5));
    CHECK_EQ(s5[0], 150.0f);
    // no copy inside: x kept; W = 0 changes nothing
    float s6[3] = {500, 0, 10}, e6[3] = {480, 0, 20};
    CHECK_OK(chooseWrap(pl, W, s6, e6));
    CHECK_EQ(s6[0], 500.0f);
    CHECK_OK(chooseWrap(pl, 0.0f, s6, e6));
    CHECK_EQ(s6[0], 500.0f);
    // negatives
    float nanvp[16] = {std::numeric_limits<float>::quiet_NaN()};
    CHECK_ERR(frustumPlanes(nanvp, pl), Err::BadLayout);
    float zero[16] = {};
    CHECK_ERR(frustumPlanes(zero, pl), Err::BadLayout);
    float sb[3] = {std::numeric_limits<float>::infinity(), 0, 0};
    CHECK_ERR(chooseWrap(pl, W, sb, e6), Err::BadLayout);
    CHECK_ERR(chooseWrap(pl, std::numeric_limits<float>::quiet_NaN(), s6, e6), Err::BadLayout);
}

// ---------------------------------------------------------------------------------------------------- constants, states

C5R_TEST(constants_layout)
{
    float vp[16];
    for (int i = 0; i < 16; ++i) vp[i] = static_cast<float>(i) + 0.5f;
    Settings s;
    s.alpha = 0.3f;
    s.ambient = 0.6f;
    float cb[24];
    std::memset(cb, 0xCD, sizeof cb);
    packConstants(vp, s, cb);
    CHECK(std::memcmp(cb, vp, sizeof vp) == 0);
    CHECK_EQ(cb[16], 0.3f);
    CHECK_EQ(cb[20], 0.6f);
    for (int i : {17, 18, 19, 21, 22, 23}) CHECK_EQ(cb[i], 0.0f);
    static_assert(kConstantBytes == sizeof cb, "96 bytes");
}

C5R_TEST(pass_states)
{
    const PassState a = passState(0), b = passState(1);
    CHECK(!a.blendEnable && b.blendEnable);
    CHECK_EQ(a.writeMask, static_cast<uint32_t>(D3D11_COLOR_WRITE_ENABLE_ALPHA));
    CHECK_EQ(b.writeMask, static_cast<uint32_t>(D3D11_COLOR_WRITE_ENABLE_ALL));
    CHECK_EQ(b.srcBlend, static_cast<uint32_t>(D3D11_BLEND_SRC_ALPHA));
    CHECK_EQ(b.destBlend, static_cast<uint32_t>(D3D11_BLEND_INV_SRC_ALPHA));
    CHECK_EQ(b.destBlendAlpha, static_cast<uint32_t>(D3D11_BLEND_ONE));
    for (const PassState& p : {a, b}) {
        CHECK(p.depthEnable && !p.stencilEnable && p.frontCounterClockwise && p.depthClipEnable);
        CHECK_EQ(p.depthFunc, static_cast<uint32_t>(D3D11_COMPARISON_LESS_EQUAL));
        CHECK_EQ(p.cullMode, static_cast<uint32_t>(D3D11_CULL_FRONT));
    }
    CHECK(passState(7).blendEnable && passState(7).writeMask == b.writeMask);
}

// ---------------------------------------------------------------------------------------------------- 0179's recorded arc

namespace {

bool loadRef(arctest::Ref& r)
{
    std::string why;
    if (!r.load(arctest::refRoot(), why)) {
        test::fail(__FILE__, __LINE__, "0179's recorded arc not found: " + why);
        return false;
    }
    return true;
}

} // namespace

C5R_TEST(ref_spawn_points_from_the_map)
{
    // the capture's plots through the map's own grid and height field give the capture's end points
    arctest::Ref r;
    if (!loadRef(r)) return;
    heightfield::HeightGrid g;
    std::string why;
    if (!r.grid(g, why)) {
        test::fail(__FILE__, __LINE__, why);
        return;
    }
    float st[3], en[2];
    CHECK_OK(spawnPoints(g, static_cast<int32_t>(r.num("plots", 0)), static_cast<int32_t>(r.num("plots", 1)),
                         static_cast<int32_t>(r.num("plots", 2)), static_cast<int32_t>(r.num("plots", 3)), nullptr, st, en));
    std::printf("spawn points from the map: start (%.6f, %.6f, %.6f) end (%.6f, %.6f); the capture's ring 0 (%.6f, %.6f, %.6f), "
                "fitted end (%.6f, %.6f)\n",
                st[0], st[1], st[2], en[0], en[1], r.num("start", 0), r.num("start", 1), r.num("start", 2), r.num("end", 0),
                r.num("end", 1));
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(st[k] - r.num("start", static_cast<size_t>(k))) < 1e-3);
    for (int k = 0; k < 2; ++k) CHECK(std::fabs(en[k] - r.num("end", static_cast<size_t>(k))) < 1e-3);
    CHECK_EQ(worldWidth(g), 3583.0f);   // 3804's grid.json wrap_period
}

C5R_TEST(ref_mesh_vertex_for_vertex)
{
    arctest::Ref r;
    if (!loadRef(r)) return;
    const Settings set;   // the install's values (game_settings_from_install checks them)
    CHECK(std::fabs(r.num("zfit", 0) - set.targetHeight) < 1e-3);   // the end's z is target_height
    // the inputs: the plots through the map (the server's route); without the grid, the capture's own fitted points
    float start[3] = {static_cast<float>(r.num("start", 0)), static_cast<float>(r.num("start", 1)), static_cast<float>(r.num("start", 2))};
    float end[3] = {static_cast<float>(r.num("end", 0)), static_cast<float>(r.num("end", 1)), set.targetHeight};
    heightfield::HeightGrid g;
    std::string why;
    if (r.grid(g, why)) {
        float exy[2];
        CHECK_OK(spawnPoints(g, static_cast<int32_t>(r.num("plots", 0)), static_cast<int32_t>(r.num("plots", 1)),
                             static_cast<int32_t>(r.num("plots", 2)), static_cast<int32_t>(r.num("plots", 3)), nullptr, start, exy));
        end[0] = exy[0];
        end[1] = exy[1];
        std::printf("inputs from the map's grid\n");
    } else {
        test::fail(__FILE__, __LINE__, why);
    }
    Mesh m;
    CHECK_OK(buildMesh(set, start, end, kMaxProgress, m));
    const size_t nv = static_cast<size_t>(r.num("counts", 0)), ni = static_cast<size_t>(r.num("counts", 1));
    CHECK_EQ(nv, static_cast<size_t>(468));
    CHECK_EQ(ni, static_cast<size_t>(1260));
    CHECK_EQ(m.vertices.size(), nv);
    CHECK_EQ(m.indices.size(), ni);
    CHECK(m.earlyStop);
    CHECK_EQ(m.rings, 25u);
    if (m.vertices.size() != nv || m.indices.size() != ni || r.vb.size() < nv * 40 || r.ib.size() < ni * 4) return;
    CHECK(std::memcmp(m.indices.data(), r.ib.data(), ni * 4) == 0);
    double maxPos = 0, maxNrm = 0, sumPos = 0;
    size_t worst = 0, colourBad = 0, posExact = 0, nrmExact = 0;
    for (size_t i = 0; i < nv; ++i) {
        Vertex rv;
        std::memcpy(&rv, &r.vb[i * 40], 40);
        const Vertex& v = m.vertices[i];
        for (int k = 0; k < 3; ++k) {
            const double dp = std::fabs(static_cast<double>(v.pos[k]) - rv.pos[k]);
            const double dn = std::fabs(static_cast<double>(v.normal[k]) - rv.normal[k]);
            sumPos += dp;
            if (dp > maxPos) {
                maxPos = dp;
                worst = i;
            }
            maxNrm = std::max(maxNrm, dn);
        }
        if (v.color0 != rv.color0 || v.color1 != rv.color1) ++colourBad;
        if (std::memcmp(v.pos, rv.pos, 12) == 0) ++posExact;
        if (std::memcmp(v.normal, rv.normal, 12) == 0) ++nrmExact;
    }
    std::printf("ref mesh: %zu vertices, %zu indices equal: %s; colours differing %zu; position max |d| %.6g (v%zu), mean %.3g, "
                "bit-equal %zu; normal max |d| %.6g, bit-equal %zu\n",
                nv, ni, std::memcmp(m.indices.data(), r.ib.data(), ni * 4) == 0 ? "yes" : "NO", colourBad, maxPos, worst,
                sumPos / (3.0 * nv), posExact, maxNrm, nrmExact);
    CHECK_EQ(colourBad, static_cast<size_t>(0));
    CHECK(maxPos < 1e-3);   // world units; float32 at |x| ~1400 is 1.2e-4 per step
    CHECK(maxNrm < 1e-4);
}

C5R_TEST(ref_states_and_constants)
{
    arctest::Ref r;
    if (!loadRef(r)) return;
    for (uint32_t p = 0; p < 2; ++p) {
        const char* key = p == 0 ? "pass 0" : "pass 1";
        CHECK(r.has(key));
        const PassState s = passState(p);
        const uint32_t ours[19] = {s.blendEnable, s.srcBlend, s.destBlend, s.blendOp, s.srcBlendAlpha, s.destBlendAlpha,
                                   s.blendOpAlpha, s.writeMask, s.depthEnable, s.depthWriteMask, s.depthFunc, s.stencilEnable,
                                   s.fillMode, s.cullMode, s.frontCounterClockwise, s.depthClipEnable, s.scissorEnable,
                                   s.multisampleEnable, s.topology};
        for (int i = 0; i < 19; ++i) {
            const uint32_t want = static_cast<uint32_t>(r.num(key, static_cast<size_t>(i)));
            if (ours[i] != want) {
                test::fail(__FILE__, __LINE__, std::string(key) + " field " + std::to_string(i) + ": ours " +
                                                   std::to_string(ours[i]) + ", stock " + std::to_string(want));
            }
        }
    }
    CHECK_EQ(r.cb.size(), static_cast<size_t>(96));
    if (r.cb.size() != 96) return;
    float ref[24], ours[24];
    std::memcpy(ref, r.cb.data(), 96);
    packConstants(ref, Settings(), ours);   // the view-projection is the frame's; alpha and ambient from the settings
    CHECK(std::memcmp(ours, ref, 64) == 0);
    CHECK(std::memcmp(&ours[16], &ref[16], 4) == 0);   // alpha 0.5
    CHECK(std::memcmp(&ours[20], &ref[20], 4) == 0);   // ambient 0.7
    for (int i : {19, 21, 22, 23}) CHECK(std::memcmp(&ours[i], &ref[i], 4) == 0);
    // 17 and 18 hold values that are not floats in stock's block (arrows-and-path-line.md 3): 0 here, not compared
}

// ---------------------------------------------------------------------------------------------------- the install and the EXE

C5R_TEST(game_settings_from_install)
{
    const std::string g = test::gameDir();
    Settings s;
    s.minWidth = -1.0f;
    std::string origin;
    CHECK_OK(loadSettings(std::wstring(g.begin(), g.end()), s, &origin));
    std::printf("ArrowSettings.xml from %s\n", origin.c_str());
    const Settings d;
    CHECK(std::memcmp(&s, &d, sizeof s) == 0);   // the defaults are the install's values
    CHECK_ERR(loadSettings(L"C:/nonexistent-0199-install", s), Err::Io);
}

C5R_TEST(game_renderer_draws)
{
    const std::string exe = test::exePath();
    const std::wstring wexe(exe.begin(), exe.end());
    arctest::Device d;
    CHECK(d.create());
    if (!d.dev) return;
    Renderer rr;
    DrawInput in;
    Mesh m;
    const Settings set;
    CHECK_ERR(rr.drawMesh(d.ctx, in, m, set), Err::NotFound);
    CHECK_ERR(rr.init(nullptr, wexe), Err::BadLayout);
    CHECK_ERR(rr.init(d.dev, L"C:/nonexistent-0199/CivilizationV_DX11.exe"), Err::Io);
    CHECK(!rr.ready());
    CHECK_OK(rr.init(d.dev, wexe));
    if (!rr.ready()) return;
    // the blobs equal the ones bound in stock's capture
    {
        shaders::ShaderSection sec;
        CHECK_OK(sec.open(wexe));
        const shaders::ShaderBlob* vs = sec.findByVa(kArrowVsVa);
        const shaders::ShaderBlob* ps = sec.findByVa(kArrowPsVa);
        CHECK(vs && ps);
        arctest::Ref r;
        std::string why;
        if (vs && ps && r.load(arctest::refRoot(), why)) {
            CHECK_EQ(units::sha256Hex(vs->bytes, vs->size), r.rec["vs"].at(0));
            CHECK_EQ(units::sha256Hex(ps->bytes, ps->size), r.rec["ps"].at(0));
        } else if (vs && ps) {
            test::fail(__FILE__, __LINE__, "0179's recorded arc not found: " + why);
        }
    }
    // a top-down view of an arc from (0, 0, 0) to (100, 0, 20): x in [-20, 120], y in [-70, 70], depth 0.5 - 0.004 z
    arctest::Target t;
    CHECK(t.create(d.dev, 256, 256));
    const float black[4] = {0, 0, 0, 0};
    t.clear(d.ctx, black);
    const float vp[16] = {1.0f / 70, 0, 0, 0, 0, 1.0f / 70, 0, 0, 0, 0, -0.004f, 0, -50.0f / 70, 0, 0.5f, 1};
    std::memcpy(in.viewProj, vp, sizeof vp);
    in.colour = t.rtv;
    in.depth = t.dsv;
    in.width = in.height = 256;
    const float s0[3] = {0, 0, 0}, e0[3] = {100, 0, 20};
    CHECK_OK(buildMesh(set, s0, e0, kMaxProgress, m));
    DrawStats st;
    CHECK_OK(rr.drawMesh(d.ctx, in, m, set, &st));
    CHECK_EQ(st.draws, 2u);
    CHECK_EQ(st.indices, 2u * static_cast<uint32_t>(m.indices.size()));
    std::vector<uint8_t> px;
    CHECK(t.readColour(d.dev, d.ctx, px));
    if (px.size() != 256u * 256u * 4u) return;
    // the arc's middle, world (50, 0) -> pixel (128, 128): the body, red at half alpha over black
    const uint8_t* c = &px[(128u * 256u + 128u) * 4u];
    std::printf("arc middle pixel %u %u %u %u\n", c[0], c[1], c[2], c[3]);
    CHECK(c[0] > 60 && c[0] > c[1] + 40 && c[0] > c[2] + 40);
    CHECK(c[3] >= 250);   // pre-pass alpha 0.5, then 0.5 + 0.5 (alpha ONE / ONE): 254 in UNORM8
    const uint8_t* corner = &px[(20u * 256u + 20u) * 4u];
    CHECK(corner[0] == 0 && corner[1] == 0 && corner[2] == 0 && corner[3] == 0);
    // the arrow record: uploads once per generation, nothing when hidden
    Arrow a(set);
    const float exy[2] = {100, 0};
    CHECK_OK(a.spawn(s0, exy));
    CHECK_OK(rr.draw(d.ctx, in, a, &st));   // not built yet: invisible
    CHECK_EQ(st.draws, 0u);
    CHECK_OK(a.update(1.0f));
    CHECK_OK(rr.draw(d.ctx, in, a, &st));
    CHECK_EQ(st.uploads, 1u);
    CHECK_OK(rr.draw(d.ctx, in, a, &st));
    CHECK_EQ(st.uploads, 0u);
    CHECK_EQ(st.draws, 2u);
    a.removeAll();
    CHECK_OK(rr.draw(d.ctx, in, a, &st));
    CHECK_EQ(st.draws, 0u);
    // negatives
    DrawInput bad = in;
    CHECK_ERR(rr.drawMesh(nullptr, in, m, set), Err::BadLayout);
    bad.colour = nullptr;
    CHECK_ERR(rr.drawMesh(d.ctx, bad, m, set), Err::BadLayout);
    bad = in;
    bad.depth = nullptr;
    CHECK_ERR(rr.drawMesh(d.ctx, bad, m, set), Err::BadLayout);
    bad = in;
    bad.width = 0;
    CHECK_ERR(rr.drawMesh(d.ctx, bad, m, set), Err::BadLayout);
    bad = in;
    bad.viewProj[5] = std::numeric_limits<float>::infinity();
    CHECK_ERR(rr.drawMesh(d.ctx, bad, m, set), Err::BadLayout);
    Mesh big = m;
    big.vertices.resize(kMaxVertices + 1);
    CHECK_ERR(rr.drawMesh(d.ctx, in, big, set), Err::BadLayout);
    Mesh badIdx = m;
    badIdx.indices[5] = static_cast<uint32_t>(badIdx.vertices.size());
    CHECK_ERR(rr.drawMesh(d.ctx, in, badIdx, set), Err::BadLayout);
    Mesh odd = m;
    odd.indices.pop_back();
    CHECK_ERR(rr.drawMesh(d.ctx, in, odd, set), Err::BadLayout);
    // after the refusals a good draw still works
    CHECK_OK(rr.drawMesh(d.ctx, in, m, set, &st));
    CHECK_EQ(st.draws, 2u);
}

C5R_TEST_MAIN
