// test_placement.cpp - c5r::pathmodels layer 1 (ticket 0300): the instance list against 3801's recorded preview
// (f6496_reference.h), the rule's branches, every validation path. No game files.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "pathmodels_testkit.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::pathmodels;

// The recorded preview: 27 instances in the capture's order, each instance's world matrix equal to the matrix of its
// first draw (no asset of this path is animated except SelectedAni, whose instance matrix is the plain translation).
C5R_TEST(f6496_instances_match_the_capture) {
    std::vector<Instance> inst;
    PlacementStats st;
    CHECK_OK(placePath(pmt::f6496Input(), inst, &st));
    const std::vector<Asset> want = pmt::f6496Assets();
    CHECK_EQ(inst.size(), want.size());
    CHECK_EQ(inst.size() * 2, sizeof(f6496::kDraws) / sizeof(f6496::kDraws[0]));
    CHECK_EQ(st.dropped, 0u);
    CHECK_EQ(st.zeroSteps, 0u);
    if (inst.size() != want.size()) return;
    for (size_t i = 0; i < inst.size(); ++i) {
        if (inst[i].asset != want[i])
            c5r::test::fail(__FILE__, __LINE__, "instance " + std::to_string(i) + ": " + assetName(inst[i].asset) +
                                                    ", want " + assetName(want[i]));
        const f6496::Draw& d = f6496::kDraws[2 * i];
        float ref[16];
        for (int k = 0; k < 16; ++k) ref[k] = d.world[k];
        if (want[i] == Asset::SelectedAniThreeTurns) {
            // the mesh matrix carries the clip; the instance is the node's translation (draw 819's matrix: identity
            // rotation, the clip's centre mesh at rest)
            for (int k = 0; k < 12; ++k) ref[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        }
        std::string why;
        // rows 0-2 to 2e-4 (the capture prints the EXE's float products); translation to 2e-3 world units
        if (!pmt::matNear(inst[i].world.m, ref, 2e-4f, 2e-3f, &why))
            c5r::test::fail(__FILE__, __LINE__, "instance " + std::to_string(i) + " (draw " + std::to_string(d.n) +
                                                    ", " + assetName(inst[i].asset) + "): " + why);
    }
}

C5R_TEST(f6496_scales) {
    std::vector<Instance> inst;
    CHECK_OK(placePath(pmt::f6496Input(), inst));
    // node 0 unscaled; nodes 1-4: every next turn value is greater (21 -> 120 -> 121 -> 220) or last: 1.5
    CHECK_EQ(inst[0].world.m[0], 1.0f);
    for (const Instance& i : inst)
        if (i.asset == Asset::CenterThreeTurns) CHECK_EQ(i.world.m[0], kTurnEndScale);
}

C5R_TEST(mid_node_scale_and_turn_variants) {
    PathInput in;
    const int16_t turns[5] = {0, 1, 1, 2, 7};   // node 1 (1) is followed by 1: not greater -> 0.75
    for (int i = 0; i < 5; ++i) {
        PathNode n;
        n.x = static_cast<int16_t>(i);
        n.turn = turns[i];
        in.nodes.push_back(n);
        Vec3 p;
        p.x = 64.0f * i;
        in.pos.push_back(p);
    }
    std::vector<Instance> inst;
    CHECK_OK(placePath(in, inst));
    std::vector<Asset> centers;
    std::vector<float> scales;
    for (const Instance& i : inst)
        if (static_cast<uint8_t>(i.asset) < 6) {
            centers.push_back(i.asset);
            scales.push_back(i.world.m[0]);
        }
    CHECK_EQ(centers.size(), 5u);
    CHECK(centers[0] == Asset::CenterOneTurn);
    CHECK(centers[1] == Asset::CenterOneTurn);
    CHECK(centers[2] == Asset::CenterOneTurn);
    CHECK(centers[3] == Asset::CenterTwoTurns);
    CHECK(centers[4] == Asset::CenterThreeTurns);
    CHECK_EQ(scales[1], kMidScale);
    CHECK_EQ(scales[2], kTurnEndScale);
    CHECK_EQ(scales[3], kTurnEndScale);
    CHECK_EQ(scales[4], kTurnEndScale);
    // connectors follow the node's kind; the last step has 3, the others 6
    int c12 = 0, c13 = 0, c14 = 0;
    for (const Instance& i : inst) {
        c12 += i.asset == Asset::ConnectorOneTurn;
        c13 += i.asset == Asset::ConnectorTwoTurns;
        c14 += i.asset == Asset::ConnectorThreeTurns;
    }
    CHECK_EQ(c12, 12);
    CHECK_EQ(c13, 6);
    CHECK_EQ(c14, 3);
    CHECK(inst[inst.size() - 5].asset == Asset::SelectedAniThreeTurns);
}

C5R_TEST(attack_and_embark) {
    PathInput in = pmt::f6496Input();
    in.lastIsAttack = true;
    in.embark.assign(5, 0);
    in.embark[2] = 1;
    in.embark[4] = 1;   // the last node: attack wins
    std::vector<Instance> inst;
    CHECK_OK(placePath(in, inst));
    int embark = 0, attack = 0, selAttack = 0, selEmbark = 0, conEmbark = 0, conAttack = 0;
    for (const Instance& i : inst) {
        embark += i.asset == Asset::CenterEmbark;
        attack += i.asset == Asset::CenterAttack;
        selAttack += i.asset == Asset::SelectedAniAttack;
        selEmbark += i.asset == Asset::SelectedAniEmbark;
        conEmbark += i.asset == Asset::ConnectorEmbark;
        conAttack += i.asset == Asset::ConnectorAttack;
    }
    CHECK_EQ(embark, 1);
    CHECK_EQ(attack, 1);
    CHECK_EQ(selAttack, 1);
    CHECK_EQ(selEmbark, 1);   // ticket 0300 part 2: an embark node in the middle has its SelectedAni too (7.5)
    CHECK_EQ(conEmbark, 6);
    CHECK_EQ(conAttack, 3);
    // order (7.5): node 0's disc, node 1 (turn: disc, 6 chevrons), node 2 (embark: Center, then SelectedAni, 6 chevrons),
    // node 3 (turn: disc, 6), node 4 (attack: Center, then SelectedAni, 3 chevrons)
    CHECK_EQ(inst.size(), 1u + 7u + 8u + 7u + 5u);
    CHECK(inst[8].asset == Asset::CenterEmbark);
    CHECK(inst[9].asset == Asset::SelectedAniEmbark);
    CHECK_EQ(inst[9].world.m[0], 1.0f);   // the SelectedAni unscaled, the Center scaled
    CHECK(inst[8].world.m[0] != 1.0f);
    CHECK(inst[inst.size() - 5].asset == Asset::CenterAttack);
    CHECK(inst[inst.size() - 4].asset == Asset::SelectedAniAttack);
}

C5R_TEST(embark_destination) {
    // ticket 0300 part 2 (7.5): an embark destination: Center, SelectedAni, and 6 chevrons (not 3)
    PathInput in = pmt::f6496Input();
    in.embark.assign(5, 0);
    in.embark[4] = 1;
    std::vector<Instance> inst;
    CHECK_OK(placePath(in, inst));
    CHECK(inst[inst.size() - 8].asset == Asset::CenterEmbark);
    CHECK(inst[inst.size() - 7].asset == Asset::SelectedAniEmbark);
    int conEmbark = 0;
    for (const Instance& i : inst) conEmbark += i.asset == Asset::ConnectorEmbark;
    CHECK_EQ(conEmbark, 6);
}

C5R_TEST(connector_frame_and_spacing) {
    std::vector<Instance> inst;
    CHECK_OK(placePath(pmt::f6496Input(), inst));
    // the first step's six chevrons: a + j (b - a) / 7, rows side / step / up
    const PathInput in = pmt::f6496Input();
    const Vec3 a = in.pos[0], b = in.pos[1];
    for (int j = 1; j <= 6; ++j) {
        const Instance& c = inst[1 + j];
        CHECK(c.asset == Asset::ConnectorThreeTurns);
        CHECK(std::fabs(c.world.m[12] - (a.x + j * (b.x - a.x) / 7.0f)) < 1e-3f);
        CHECK(std::fabs(c.world.m[14] - (a.z + j * (b.z - a.z) / 7.0f)) < 1e-3f);
        CHECK_EQ(c.world.m[2], 0.0f);
        CHECK_EQ(c.world.m[10], 1.0f);
        const float side = c.world.m[0] * c.world.m[4] + c.world.m[1] * c.world.m[5];
        CHECK(std::fabs(side) < 1e-6f);   // side is perpendicular to the step in the ground plane
    }
}

C5R_TEST(no_path_marker_and_hide) {
    PathInput in;
    in.cursor.x = 10;
    in.cursor.y = 20;
    in.cursor.z = 3;
    std::vector<Instance> inst;
    CHECK_OK(placePath(in, inst));
    CHECK_EQ(inst.size(), 2u);
    // ticket 0300 part 2 (7.5): Center first, then the SelectedAni
    CHECK(inst[0].asset == Asset::CenterInaccessible);
    CHECK(inst[1].asset == Asset::SelectedAniInaccessible);
    CHECK_EQ(inst[0].world.m[12], 10.0f);
    in.cursorKind = CursorKind::Embark;
    CHECK_OK(placePath(in, inst));
    CHECK(inst[0].asset == Asset::CenterEmbark);
    in.cursorKind = CursorKind::Attack;
    CHECK_OK(placePath(in, inst));
    CHECK(inst[0].asset == Asset::CenterAttack);
    CHECK(inst[1].asset == Asset::SelectedAniAttack);
    in = pmt::f6496Input();
    in.show = false;
    CHECK_OK(placePath(in, inst));
    CHECK(inst.empty());
}

C5R_TEST(single_node_path) {
    PathInput in = pmt::f6496Input();
    in.nodes.resize(1);
    in.pos.resize(1);
    std::vector<Instance> inst;
    CHECK_OK(placePath(in, inst));
    CHECK_EQ(inst.size(), 1u);
    CHECK(inst[0].asset == Asset::CenterOneTurn);
}

C5R_TEST(instance_cap_and_zero_steps) {
    PathInput in;
    for (uint32_t i = 0; i < kMaxNodes; ++i) {
        PathNode n;
        n.turn = static_cast<int16_t>(100 * i);
        in.nodes.push_back(n);
        Vec3 p;
        p.x = 64.0f * (i / 2);   // every second step has zero length
        in.pos.push_back(p);
    }
    std::vector<Instance> inst;
    PlacementStats st;
    CHECK_OK(placePath(in, inst, &st));
    CHECK_EQ(st.zeroSteps, 32u);
    // 64 centres + 1 SelectedAni + 31 steps x 6 (the last non-zero step has 3: step 63 is zero) = 251
    CHECK(inst.size() <= kMaxInstances);
    for (size_t i = 0; i < in.pos.size(); ++i) in.pos[i].x = 64.0f * i;
    CHECK_OK(placePath(in, inst, &st));
    CHECK_EQ(inst.size(), static_cast<size_t>(kMaxInstances));
    CHECK_EQ(st.zeroSteps, 0u);
    CHECK_EQ(st.dropped, 64u + 1u + 62u * 6u + 3u - kMaxInstances);
}

// ---- validation paths: each refused with the named code, and the output cleared

C5R_TEST(neg_too_many_nodes) {
    PathInput in;
    in.nodes.resize(kMaxNodes + 1);
    in.pos.resize(kMaxNodes + 1);
    std::vector<Instance> inst(3);
    CHECK_ERR(placePath(in, inst), Err::BadLayout);
    CHECK(inst.empty());
}

C5R_TEST(neg_position_count) {
    PathInput in = pmt::f6496Input();
    in.pos.pop_back();
    std::vector<Instance> inst(3);
    CHECK_ERR(placePath(in, inst), Err::BadLayout);
    CHECK(inst.empty());
}

C5R_TEST(neg_embark_count) {
    PathInput in = pmt::f6496Input();
    in.embark.assign(2, 0);
    std::vector<Instance> inst;
    CHECK_ERR(placePath(in, inst), Err::BadLayout);
    CHECK(inst.empty());
}

C5R_TEST(neg_non_finite) {
    PathInput in = pmt::f6496Input();
    in.pos[3].z = std::nanf("");
    std::vector<Instance> inst;
    CHECK_ERR(placePath(in, inst), Err::BadLayout);
    CHECK(inst.empty());
    PathInput c;
    c.cursor.x = INFINITY;
    CHECK_ERR(placePath(c, inst), Err::BadLayout);
    CHECK(inst.empty());
}

// ---- nodePositions on a synthetic height field: plot centre, height, floor, fog, missing plot

namespace {
heightfield::HeightGrid grid() {
    heightfield::HeightGrid g;
    g.mapW = 4;
    g.mapH = 8;   // 4 rows + 4 margin rows
    g.samplesX = g.mapW * 64;
    g.samplesY = g.mapH * 64;
    g.sx = 1.0f;
    g.sy = 1.0f;
    g.sz = 0.25f;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>((x + y) % 200);
    // (hx, hy, 0, 1) x M: x = 64 hx + 32 hy, y = 55 hy
    g.hexToWorld[0] = 64.0f;
    g.hexToWorld[4] = 32.0f;
    g.hexToWorld[5] = 55.0f;
    g.hexToWorld[10] = 1.0f;
    g.hexToWorld[15] = 1.0f;
    return g;
}
std::vector<PathNode> nodes3() {
    std::vector<PathNode> n(3);
    n[0].x = 0, n[0].y = 0;
    n[1].x = 1, n[1].y = 1;
    n[2].x = 3, n[2].y = 3;
    return n;
}
}  // namespace

C5R_TEST(node_positions) {
    const heightfield::HeightGrid g = grid();
    std::vector<Vec3> p;
    CHECK_OK(nodePositions(g, nodes3(), {}, {}, -1e9f, p));
    CHECK_EQ(p.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        float x = 0, y = 0;
        CHECK_OK(units::plotCentre(g, nodes3()[i].x, nodes3()[i].y, x, y));
        CHECK_EQ(p[i].x, x);
        CHECK_EQ(p[i].y, y);
        CHECK_EQ(p[i].z, units::heightAtWorld(g, x, y));
    }
    CHECK_EQ(p[1].x, 96.0f);   // plot (1, 1): x = 64 + 32
    CHECK_EQ(p[1].y, 55.0f);
    // the floor: a height below it, a revealed-not-visible plot, a missing plot
    const float floorZ = 1000.0f;
    CHECK_OK(nodePositions(g, nodes3(), {}, {}, floorZ, p));
    CHECK_EQ(p[0].z, floorZ);
    std::vector<uint8_t> fog = {0, 1, 0}, missing = {0, 0, 1};
    CHECK_OK(nodePositions(g, nodes3(), fog, missing, 0.5f, p));
    CHECK_EQ(p[1].z, 0.5f);
    CHECK_EQ(p[2].z, 0.5f);
    CHECK_EQ(p[0].z, std::max(0.5f, units::heightAtWorld(g, p[0].x, p[0].y)));
}

C5R_TEST(neg_node_positions) {
    const heightfield::HeightGrid g = grid();
    std::vector<Vec3> p(2);
    std::vector<PathNode> n = nodes3();
    n[2].y = 4;   // outside the 4 map rows
    CHECK_ERR(nodePositions(g, n, {}, {}, 0.0f, p), Err::BadLayout);
    CHECK(p.empty());
    CHECK_ERR(nodePositions(g, nodes3(), {1}, {}, 0.0f, p), Err::BadLayout);
    CHECK_ERR(nodePositions(g, nodes3(), {}, {1, 0}, 0.0f, p), Err::BadLayout);
    CHECK_ERR(nodePositions(g, nodes3(), {}, {}, std::nanf(""), p), Err::BadLayout);
    heightfield::HeightGrid empty;
    CHECK_ERR(nodePositions(empty, nodes3(), {}, {}, 0.0f, p), Err::BadLayout);
}

C5R_TEST(asset_names) {
    CHECK_EQ(std::string(assetName(Asset::CenterOneTurn)), std::string("CenterOneTurn"));
    CHECK_EQ(std::string(assetName(Asset::ConnectorAttack)), std::string("ConnectorAttack"));
    CHECK_EQ(std::string(assetName(static_cast<Asset>(18))), std::string("?"));
}

C5R_TEST_MAIN
