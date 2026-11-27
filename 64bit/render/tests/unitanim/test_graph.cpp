// render/tests/unitanim/test_graph.cpp - issue 4409: the blend-DAG evaluator (c5r/unitanim/graph.h) on synthetic
// graphs built here (no game file, D83): selection by path options, the hint-5 fade through a BlockCurve, loop and
// once sources, ContinueOffset through restartFrom, an Offset from a path option, masks into the program; and every
// status bindGraph raises (UnknownNode, EcNotBound, NoProgress, BadLogicFile), each asserting code and text.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "c5r/unitanim/graph.h"
#include "testkit.h"

using namespace c5r::unitanim;
using c5m::DagNodeType;

namespace {

c5m::DagEdge edge(const char* name, int32_t from) {
    c5m::DagEdge e;
    e.name = name;
    e.input_node = from;
    return e;
}
c5m::DagNode node(DagNodeType t, const char* type_name, std::vector<c5m::DagEdge> in = {}) {
    c5m::DagNode n;
    n.type = t;
    n.type_name = std::string("TypeMarker_NodeData") + type_name;
    n.name = type_name;
    n.inputs = std::move(in);
    return n;
}
c5m::DagNode source(int32_t index, int32_t loops, bool cont, int32_t offset_from = -1) {
    c5m::DagNode n = node(DagNodeType::PoseAnimSource, "PoseAnimSource", {edge("Speed", -1), edge("Offset", offset_from)});
    n.source_index = index;
    n.loop_count = loops;
    n.clamped_loop = true;
    n.continue_offset = cont;
    n.has_continue_offset = true;
    n.data_edge_count = 1;
    return n;
}
c5m::DagNode callback(int hint, float value) {
    c5m::DagNode n = node(DagNodeType::ScalarCallback, "ScalarCallback", {edge("Input (0)", -1), edge("Input (1)", -1)});
    n.callback_hint = hint;
    n.callback_value = value;
    n.has_callback_value = true;
    return n;
}
std::vector<c5m::DagBlock> rampBlocks() {   // shape (a) of 6.3: [3, 1 s] [0, 0] [3 flipped, 0]
    c5m::DagBlock a, b, c;
    a.type = 3; a.duration = 1;
    b.type = 0;
    c.type = 3; c.flip_x = true;
    return {a, b, c};
}

// The idle -> fidget graph of 6.3 in small: Blend(Param = BlockCurve(hint 5, 0.3333 s), From = Selection(path 0) over
// idle A / idle B (looping, ContinueOffset 1), To = fidget (once)).
c5m::DagFile fadeGraph() {
    c5m::DagGraph g;
    g.source_ecs = {1000, 2000, 1040};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                // 0
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 4), edge("From", 2), edge("To", 5), edge("Mask", -1)}));     // 1
    g.nodes.push_back(node(DagNodeType::Selection, "Selection",
                           {edge("Selector", 3), edge("Pose in (0)", 6), edge("Pose in (1)", 7)}));    // 2
    g.nodes.back().default_edge = 1;
    g.nodes.push_back(callback(7, 0));                                                                 // 3
    g.nodes.push_back(node(DagNodeType::BlockCurve, "BlockCurve", {edge("Input", 8)}));                // 4
    g.nodes.back().blocks = rampBlocks();
    g.nodes.push_back(source(2, 1, false));                                                            // 5 fidget
    g.nodes.push_back(source(0, 0, true));                                                             // 6 idle A
    g.nodes.push_back(source(1, 0, true));                                                             // 7 idle B
    g.nodes.push_back(callback(5, 0.3333f));                                                           // 8
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

EcMap members() {
    EcMap m;
    m.ecs = {1000, 2000, 1040, 1100};
    m.clips = {"U_IdleA.gr2", "U_IdleB.gr2", "U_FidgetA.gr2", "U_AttackA.gr2"};
    m.durations = {6.0f, 5.0f, 3.0f, 1.0f};
    return m;
}

std::unique_ptr<GraphInstance> bind(const c5m::DagFile& f, const EcMap& m) {
    std::unique_ptr<GraphInstance> g;
    // the graphs as read (13.2): the From side runs on; the held default of MC #2117 is test_graph_hold.cpp's
    GraphOptions o;
    o.hold_from_continue = false;
    AnimStatus s = bindGraph(f, m, "fade.dge", o, g);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, s.text);
    return g;
}

void expectStatus(const c5m::DagFile& f, const EcMap& m, AnimCode code, const char* fragment, int line) {
    std::unique_ptr<GraphInstance> g;
    AnimStatus s = bindGraph(f, m, "bad.dge", g);
    // codes by number: name() is status.cpp's (4403), not on this branch
    std::printf("  code %d: %s\n", static_cast<int>(s.code), s.text.c_str());
    if (s.code != code || s.text.find(fragment) == std::string::npos || g)
        c5r::test::fail(__FILE__, line, "want code " + std::to_string(static_cast<int>(code)) + " with \"" +
                                            fragment + "\", got \"" + s.text + "\"");
}

bool near(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol; }

}  // namespace

C5R_TEST(block_curve_as_read) {
    CHECK(near(blockEase3(0), 0) && near(blockEase3(1), 1) && near(blockEase3(0.5f), 0.5f));
    CHECK(near(blockEase3(0.6f), 0.648f));
    std::vector<c5m::DagBlock> a = rampBlocks();
    CHECK(near(evalBlockCurve(a, 1, -0.5f), 0));
    CHECK(near(evalBlockCurve(a, 1, 0.25f), blockEase3(0.25f)));
    CHECK(near(evalBlockCurve(a, 1, 1.0f), 1));
    CHECK(near(evalBlockCurve(a, 1, 3.0f), 1));
    CHECK(near(evalBlockCurve(a, 0.25f, 3.0f), 0.25f));
    // shape (c): [3, 0] [0, 0] [3 flipped, 1 s]: a fall from 1 to 0
    std::vector<c5m::DagBlock> c = rampBlocks();
    c[0].duration = 0;
    c[2].duration = 1;
    CHECK(near(evalBlockCurve(c, 1, 0.0f), 1));
    CHECK(near(evalBlockCurve(c, 1, 0.25f), 1 - blockEase3(0.25f)));
    CHECK(near(evalBlockCurve(c, 1, 2.0f), 0));
}

C5R_TEST(fade_selection_and_sources) {
    std::unique_ptr<GraphInstance> g = bind(fadeGraph(), members());
    if (!g) return;
    PathOptions o;
    PoseProgram p;
    // entry from an idle A at 5.4 s (ContinueOffset 1 takes it over); the fidget starts at 0
    ActiveSource prev;
    prev.ec = 1000;
    prev.t = 5.4f;
    g->restartFrom({prev});
    CHECK(g->program(o, p).ok());
    CHECK(p.single());                       // w = 0: the From side alone
    CHECK_EQ(p.nodes[0].ec, 1000);
    CHECK(near(p.nodes[0].t, 5.4f));
    g->advance(0.2f, o);
    CHECK(g->program(o, p).ok());
    CHECK_EQ(p.nodes.size(), static_cast<size_t>(3));
    CHECK_EQ(p.nodes[0].clip, std::string("U_IdleA.gr2"));
    CHECK(near(p.nodes[0].t, 5.6f));
    CHECK_EQ(p.nodes[1].ec, 1040);
    CHECK(near(p.nodes[1].t, 0.2f));
    CHECK(p.nodes[2].kind == PoseNode::Blend && p.nodes[2].a == 0 && p.nodes[2].b == 1);
    const float w = p.nodes[2].w;
    // 7.3's one live point (P01, idle A -> fidget A): w = 0.680 at 0.2 s after the fidget's start. The reading here
    // (smoothstep over the graph's 0.3333 s) gives 0.648: reported, not tuned (unit-controller.md 13.2).
    std::printf("  fade weight at 0.2 s: %.4f (7.3 measured 0.680; linear over 0.3333 s would give %.4f)\n", w,
                0.2f / 0.3333f);
    CHECK(near(w, blockEase3(0.2f / 0.3333f)));
    std::vector<ActiveSource> act;
    g->active(o, act);
    CHECK_EQ(act.size(), static_cast<size_t>(2));
    CHECK(near(act[0].weight, 1 - w) && near(act[1].weight, w));
    CHECK(act[1].once && !act[1].done);
    CHECK(!g->complete(o));
    // idle A wraps at 6 s while fading: looped this step
    g->advance(0.5f, o);   // 0.7 s: fade done, fidget alone
    CHECK(g->program(o, p).ok());
    CHECK(p.single());
    CHECK_EQ(p.nodes[0].ec, 1040);
    CHECK(near(p.nodes[0].t, 0.7f));
    g->advance(2.5f, o);   // 3.2 s: the fidget (3 s, once) holds its last pose
    CHECK(g->program(o, p).ok());
    CHECK(near(p.nodes[0].t, 3.0f));
    CHECK(g->complete(o));
    CHECK(near(g->stateTime(), 3.2f));
    // path 0 = 1 selects idle B on the From side; out of range -> DefaultEdge 1 (idle B)
    g->restart();
    o.v[0] = 1;
    CHECK(g->program(o, p).ok());
    CHECK_EQ(p.nodes[0].ec, 2000);
    CHECK(near(p.nodes[0].t, 0));   // restart(): no take-over
    o.v[0] = 7;
    CHECK(g->program(o, p).ok());
    CHECK_EQ(p.nodes[0].ec, 2000);
    // a looping source wraps and reports it
    o.v[0] = 0;
    prev.t = 5.9f;
    g->restartFrom({prev});
    g->advance(0.2f, o);   // idle A 5.9 -> 6.1 = 0.1 (wrapped) while the fade runs
    g->active(o, act);
    CHECK_EQ(act.size(), static_cast<size_t>(2));
    CHECK(act[0].ec == 1000 && act[0].looped && near(act[0].t, 0.1f, 1e-4f) && near(act[0].prev_t, 5.9f, 1e-4f));
}

C5R_TEST(offset_from_path_and_mask) {
    c5m::DagGraph g;
    g.source_ecs = {1000, 1100};
    g.track_masks = {"MASK_LAYER"};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                 // 0
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 4), edge("From", 2), edge("To", 3), edge("Mask", 5)}));       // 1
    g.nodes.push_back(source(0, 0, false, 6));                                                          // 2 offset <- 6
    g.nodes.push_back(source(1, 1, false));                                                             // 3
    g.nodes.push_back(node(DagNodeType::ScalarSource, "ScalarSource"));                                 // 4
    g.nodes.back().scalar = 1;
    g.nodes.push_back(node(DagNodeType::Trackmask, "Trackmask"));                                       // 5
    g.nodes.back().default_weight = 1;
    g.nodes.back().target_weight = 0;
    g.nodes.push_back(callback(7, 6));                                                                  // 6
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    std::unique_ptr<GraphInstance> gi = bind(f, members());
    if (!gi) return;
    PathOptions o;
    o.v[6] = 1;
    PoseProgram p;
    CHECK(gi->program(o, p).ok());
    CHECK_EQ(p.nodes.size(), static_cast<size_t>(3));
    CHECK(near(p.nodes[0].t, 1.0f));            // Offset = path 6 = 1 (seconds, the reading of 6.3)
    CHECK(p.nodes[2].kind == PoseNode::Blend && near(p.nodes[2].w, 1) && p.nodes[2].mask == 0);
    CHECK_EQ(p.masks.size(), static_cast<size_t>(1));
    CHECK_EQ(p.masks[0].binding, std::string("MASK_LAYER"));
    CHECK(near(p.masks[0].default_weight, 1) && near(p.masks[0].target_weight, 0) && p.masks[0].bones.empty());
}

// 0441 (unit-controller.md 19): the walks without copies answer as the walk with them. Over a time sweep (the fade, the
// once fidget running, done and held, idle wraps) and every selector value (0, 1 and out of range: the default edge),
// for the graph as read and with the held From side (MC #2117): ecComplete(ec) equals the default answer
// (GraphInstance::ecComplete: active() then complete()) for every ec the member has and one it lacks, and activeTimes()
// equals active() field for field except the clip name, which it leaves empty.
C5R_TEST(walks_without_copies_answer_alike) {
    size_t checks = 0, completes = 0;
    for (int held = 0; held < 2; ++held) {
        std::unique_ptr<GraphInstance> g;
        GraphOptions go;
        go.hold_from_continue = held != 0;
        CHECK(bindGraph(fadeGraph(), members(), "fade.dge", go, g).ok());
        if (!g) return;
        for (int sel = 0; sel < 3; ++sel) {
            PathOptions o;
            o.v[0] = sel == 2 ? 7.0f : static_cast<float>(sel);
            ActiveSource prev;
            prev.ec = 1000;
            prev.t = 5.1f;
            g->restartFrom({prev});
            for (int k = 0; k < 40; ++k) {
                g->advance(k == 0 ? 0.0f : 0.11f, o);
                for (int ec : {1000, 2000, 1040, 1100, 4242}) {
                    const bool fast = g->ecComplete(ec, o);
                    const bool slow = g->GraphInstance::ecComplete(ec, o);
                    CHECK(fast == slow);
                    completes += fast ? 1 : 0;
                    ++checks;
                }
                std::vector<ActiveSource> a, t;
                g->active(o, a);
                g->activeTimes(o, t);
                CHECK_EQ(a.size(), t.size());
                for (size_t i = 0; i < a.size() && i < t.size(); ++i) {
                    CHECK(t[i].clip.empty() && !a[i].clip.empty());
                    CHECK(a[i].ec == t[i].ec && a[i].t == t[i].t && a[i].prev_t == t[i].prev_t &&
                          a[i].looped == t[i].looped && a[i].duration == t[i].duration && a[i].once == t[i].once &&
                          a[i].done == t[i].done && a[i].weight == t[i].weight);
                }
            }
        }
    }
    std::printf("  %zu ecComplete answers compared, %zu of them true\n", checks, completes);
    CHECK(completes > 0 && completes < checks);   // both answers occur
}

// ---------------------------------------------------------------------------------------------- statuses

C5R_TEST(refuse_unknown_node) {
    c5m::DagFile f = fadeGraph();
    f.dags[0].nodes[3].type = DagNodeType::Unknown;
    f.dags[0].nodes[3].type_name = "TypeMarker_NodeDataSomethingNew";
    expectStatus(f, members(), AnimCode::UnknownNode, "node 3 (ScalarCallback, TypeMarker_NodeDataSomethingNew)", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[3].type = DagNodeType::Layer;
    expectStatus(f, members(), AnimCode::UnknownNode, "node 3", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[3].has_node_data = false;
    f.dags[0].nodes[3].type_name.clear();
    expectStatus(f, members(), AnimCode::UnknownNode, "NULL NodeData", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[8].callback_hint = 6;
    expectStatus(f, members(), AnimCode::UnknownNode, "callback hint 6", __LINE__);
}

C5R_TEST(refuse_ec_not_bound) {
    EcMap m = members();
    m.ecs[1] = 2001;   // the member lacks 2000
    m.ecs[2] = 1041;   // and 1040
    expectStatus(fadeGraph(), m, AnimCode::EcNotBound, "source ec the member lacks: 1040, 2000", __LINE__);
    m = members();
    m.durations[2] = 0;
    expectStatus(fadeGraph(), m, AnimCode::EcNotBound, "1040 (U_FidgetA.gr2: no duration)", __LINE__);
    m.durations.clear();
    expectStatus(fadeGraph(), m, AnimCode::EcNotBound, "no duration", __LINE__);
}

C5R_TEST(refuse_no_progress) {
    c5m::DagFile f = fadeGraph();
    f.dags[0].nodes[0].inputs[0].input_node = -1;   // nothing reaches the output
    expectStatus(f, members(), AnimCode::NoProgress, "no animation source reachable", __LINE__);
}

C5R_TEST(refuse_bad_logic_file) {
    c5m::DagFile empty;
    expectStatus(empty, members(), AnimCode::BadLogicFile, "no DAG", __LINE__);
    c5m::DagFile f = fadeGraph();
    f.dags[0].root = 1;
    expectStatus(f, members(), AnimCode::BadLogicFile, "is not a Subgraph", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[1].inputs[1].input_node = 40;
    expectStatus(f, members(), AnimCode::BadLogicFile, "edge From to node 40", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[1].inputs[0].input_node = 5;   // Param from a pose node
    expectStatus(f, members(), AnimCode::BadLogicFile, "edge Param from a node of the wrong kind", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[2].inputs[1].input_node = 1;   // Selection <- Blend <- Selection: a cycle
    expectStatus(f, members(), AnimCode::BadLogicFile, "cycle", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[5].source_index = 3;
    expectStatus(f, members(), AnimCode::BadLogicFile, "source index 3", __LINE__);
    f = fadeGraph();
    f.dags[0].nodes[3].callback_value = 9;
    expectStatus(f, members(), AnimCode::BadLogicFile, "hint 7 value outside paths 0-8", __LINE__);
    f = fadeGraph();
    c5m::DagNode tm = node(DagNodeType::Trackmask, "Trackmask");
    tm.binding_index = 2;
    f.dags[0].nodes.push_back(tm);
    f.dags[0].nodes[1].inputs[3].input_node = 9;
    expectStatus(f, members(), AnimCode::BadLogicFile, "track mask index 2", __LINE__);
}

// ------------------------------------------------------------------------------------------- 0470: shared shapes
namespace {

// offset_from_path_and_mask's graph: an Offset from path 6, a masked blend, a ScalarSource weight
c5m::DagFile maskGraph() {
    c5m::DagGraph g;
    g.source_ecs = {1000, 1100};
    g.track_masks = {"MASK_LAYER"};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                 // 0
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 4), edge("From", 2), edge("To", 3), edge("Mask", 5)}));       // 1
    g.nodes.push_back(source(0, 0, false, 6));                                                          // 2 offset <- 6
    g.nodes.push_back(source(1, 1, false));                                                             // 3
    g.nodes.push_back(node(DagNodeType::ScalarSource, "ScalarSource"));                                 // 4
    g.nodes.back().scalar = 0.4f;
    g.nodes.push_back(node(DagNodeType::Trackmask, "Trackmask"));                                       // 5
    g.nodes.back().default_weight = 1;
    g.nodes.back().target_weight = 0.25f;
    g.nodes.push_back(callback(7, 6));                                                                  // 6
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

std::string fbits(float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    char b[16];
    std::snprintf(b, sizeof b, "%08x", u);
    return b;
}

// Every answer of one instance at one point, as text (floats by their bits).
std::string answers(const GraphInstance& g, const PathOptions& o) {
    std::string s;
    PoseProgram p;
    const AnimStatus st = g.program(o, p);
    s += std::to_string(static_cast<int>(st.code)) + st.text + "|";
    for (const PoseNode& n : p.nodes)
        s += std::to_string(static_cast<int>(n.kind)) + "/" + n.clip + "/" + std::to_string(n.ec) + "/" + fbits(n.t) + "/" +
             std::to_string(n.a) + "/" + std::to_string(n.b) + "/" + fbits(n.w) + "/" + std::to_string(n.mask) + ";";
    for (const TrackMaskWeights& m : p.masks) s += "m" + m.binding + fbits(m.default_weight) + fbits(m.target_weight) + ";";
    std::vector<ActiveSource> a, t;
    g.active(o, a);
    for (const ActiveSource& x : a)
        s += "a" + std::to_string(x.ec) + x.clip + fbits(x.t) + fbits(x.prev_t) + (x.looped ? "L" : "l") +
             fbits(x.duration) + (x.once ? "O" : "o") + (x.done ? "D" : "d") + fbits(x.weight) + ";";
    g.activeTimes(o, t);
    for (const ActiveSource& x : t) s += "t" + std::to_string(x.ec) + x.clip + fbits(x.t) + fbits(x.prev_t) + ";";
    s += g.complete(o) ? "C" : "c";
    for (int ec : {1000, 2000, 1040, 1100, 4242}) s += g.ecComplete(ec, o) ? "E" : "e";
    s += fbits(g.stateTime()) + (g.programMayFail() ? "F" : "f");
    return s;
}

// Step k of a run by `seed`: the entry (restartFrom with the previous state's idle at a seed's time), then steps of a
// seed's length over every selector value (0, 1 and out of range) and two offsets.
std::string step(GraphInstance& g, int seed, int k) {
    PathOptions o;
    o.v[0] = static_cast<float>((k + seed) % 3 == 2 ? 7 : (k + seed) % 3);
    o.v[6] = static_cast<float>((k / 5 + seed) % 2);
    if (k == 0) {
        ActiveSource prev;
        prev.ec = 1000;
        prev.t = 0.7f * static_cast<float>(seed);
        g.restartFrom({prev});
    }
    g.advance(k == 0 ? 0.0f : 0.05f + 0.031f * static_cast<float>(seed), o);
    return answers(g, o);
}

const void* sharedId(const GraphInstance& g) {
    GraphFootprint fp;
    g.footprint(fp);
    return fp.shared_id;
}

std::unique_ptr<GraphInstance> bindWith(const c5m::DagFile& f, const EcMap& m, const char* name, bool held,
                                        GraphShare* share) {
    std::unique_ptr<GraphInstance> g;
    GraphOptions o;
    o.hold_from_continue = held;
    AnimStatus s = share ? bindGraph(f, m, name, o, *share, g) : bindGraph(f, m, name, o, g);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, s.text);
    return g;
}

}  // namespace

// 0470 (unit-controller.md 19.7): two instances bound through one GraphShare, one of them from a second DagFile object
// of the same content, hold one shape; each answers, step by step and interleaved with the other's different run, as an
// instance bound on its own taking the same run: the shape is shared, the clock and the sources' starts are not.
C5R_TEST(shared_shapes_answer_alike) {
    size_t compared = 0, apart = 0;
    for (int which = 0; which < 2; ++which)
        for (int held = 0; held < 2; ++held) {
            const c5m::DagFile f = which ? maskGraph() : fadeGraph();
            const c5m::DagFile copy = f;
            GraphShare share;
            std::unique_ptr<GraphInstance> p1 = bindWith(f, members(), "g.dge", held != 0, nullptr);
            std::unique_ptr<GraphInstance> p2 = bindWith(f, members(), "g.dge", held != 0, nullptr);
            std::unique_ptr<GraphInstance> a = bindWith(f, members(), "g.dge", held != 0, &share);
            std::unique_ptr<GraphInstance> b = bindWith(copy, members(), "g.dge", held != 0, &share);
            if (!p1 || !p2 || !a || !b) return;
            CHECK_EQ(share.shapes(), 1u);
            CHECK_EQ(share.binds(), 2u);
            CHECK_EQ(share.hits(), 1u);
            CHECK(share.bytes() > 0);
            CHECK(sharedId(*a) != nullptr && sharedId(*a) == sharedId(*b));
            CHECK(sharedId(*p1) != sharedId(*p2) && sharedId(*p1) != sharedId(*a));
            for (int k = 0; k < 60; ++k) {
                const std::string ra = step(*a, 1, k), rb = step(*b, 2, k);
                CHECK_EQ(ra, step(*p1, 1, k));
                CHECK_EQ(rb, step(*p2, 2, k));
                compared += 2;
                apart += ra != rb ? 1 : 0;
            }
        }
    std::printf("  %zu points compared, the two shared instances apart on %zu\n", compared, apart);
    CHECK(apart > 100);   // the two runs differ: the instances' own state is their own
}

// A shape is shared only when every field the walks read is equal: each change below (a number, a curve, an edge, a
// track mask, the file name, a clip, a duration) makes a new shape that answers as its own bind; a change to a field
// the walks do not read (a node's name) shares.
C5R_TEST(shared_shapes_differ_by_content) {
    GraphShare share;
    struct Variant {
        const char* what;
        c5m::DagFile f;
        EcMap m;
        const char* name;
        bool shares;
    };
    std::vector<Variant> vs;
    vs.push_back({"the original", fadeGraph(), members(), "fade.dge", false});
    vs.push_back({"the original again", fadeGraph(), members(), "fade.dge", true});
    {
        Variant v{"a source's speed", fadeGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[6].speed = 1.5f;
        vs.push_back(v);
    }
    {
        Variant v{"a block's duration", fadeGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[4].blocks[0].duration = 2;
        vs.push_back(v);
    }
    {
        Variant v{"a block's flip", fadeGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[4].blocks[2].flip_y = true;
        vs.push_back(v);
    }
    {
        Variant v{"the selection's inputs", fadeGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[2].inputs[1].input_node = 7;
        v.f.dags[0].nodes[2].inputs[2].input_node = 6;
        vs.push_back(v);
    }
    {
        Variant v{"a callback's value", fadeGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[8].callback_value = 0.5f;
        vs.push_back(v);
    }
    {
        Variant v{"the file name", fadeGraph(), members(), "fade2.dge", false};
        vs.push_back(v);
    }
    {
        Variant v{"a clip", fadeGraph(), members(), "fade.dge", false};
        v.m.clips[0] = "U_IdleA2.gr2";
        vs.push_back(v);
    }
    {
        Variant v{"a duration", fadeGraph(), members(), "fade.dge", false};
        v.m.durations[1] = 5.5f;
        vs.push_back(v);
    }
    {
        Variant v{"a node's name (not read)", fadeGraph(), members(), "fade.dge", true};
        v.f.dags[0].nodes[6].name = "Renamed";
        vs.push_back(v);
    }
    vs.push_back({"the mask graph", maskGraph(), members(), "fade.dge", false});
    {
        Variant v{"its track mask's name", maskGraph(), members(), "fade.dge", false};
        v.f.dags[0].track_masks[0] = "MASK_OTHER";
        vs.push_back(v);
    }
    {
        Variant v{"its mask's weight", maskGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[5].target_weight = 0.5f;
        vs.push_back(v);
    }
    {
        Variant v{"its scalar", maskGraph(), members(), "fade.dge", false};
        v.f.dags[0].nodes[4].scalar = 0.6f;
        vs.push_back(v);
    }
    std::vector<std::unique_ptr<GraphInstance>> kept;
    size_t shapes = 0;
    for (const Variant& v : vs) {
        std::unique_ptr<GraphInstance> s = bindWith(v.f, v.m, v.name, true, &share);
        std::unique_ptr<GraphInstance> p = bindWith(v.f, v.m, v.name, true, nullptr);
        if (!s || !p) return;
        if (!v.shares) ++shapes;
        std::printf("  %-28s shapes %zu, hits %zu\n", v.what, share.shapes(), share.hits());
        if (share.shapes() != shapes) c5r::test::fail(__FILE__, __LINE__, std::string("shapes after ") + v.what);
        for (int k = 0; k < 40; ++k) CHECK_EQ(step(*s, 3, k), step(*p, 3, k));
        kept.push_back(std::move(s));
    }
    CHECK_EQ(share.binds(), vs.size());
    CHECK_EQ(share.hits(), 2u);
}

// Every refusal through a GraphShare is the refusal without it (code and text), gives no instance and adds no shape and
// no bind; a bind after them still shares.
C5R_TEST(shared_bind_refusals_alike) {
    GraphShare share;
    std::vector<std::pair<c5m::DagFile, EcMap>> bad;
    c5m::DagFile f = fadeGraph();
    f.dags[0].nodes[8].type = DagNodeType::Unknown;
    bad.push_back({f, members()});
    EcMap m = members();
    m.ecs[2] = 9999;
    bad.push_back({fadeGraph(), m});
    m = members();
    m.durations.clear();
    bad.push_back({fadeGraph(), m});
    f = fadeGraph();
    f.dags[0].nodes[0].inputs[0].input_node = -1;
    bad.push_back({f, members()});
    bad.push_back({c5m::DagFile(), members()});
    f = fadeGraph();
    f.dags[0].root = 1;
    bad.push_back({f, members()});
    f = fadeGraph();
    f.dags[0].nodes[2].inputs[1].input_node = 1;
    bad.push_back({f, members()});
    f = fadeGraph();
    f.dags[0].nodes[3].callback_value = 9;
    bad.push_back({f, members()});
    std::unique_ptr<GraphInstance> first = bindWith(fadeGraph(), members(), "bad.dge", true, &share);
    for (auto& b : bad) {
        std::unique_ptr<GraphInstance> g1, g2;
        GraphOptions o;
        const AnimStatus s1 = bindGraph(b.first, b.second, "bad.dge", o, g1);
        const AnimStatus s2 = bindGraph(b.first, b.second, "bad.dge", o, share, g2);
        std::printf("  code %d: %s\n", static_cast<int>(s2.code), s2.text.c_str());
        CHECK(!s1.ok() && s1.code == s2.code && s1.text == s2.text && !g1 && !g2);
    }
    CHECK_EQ(share.shapes(), 1u);
    CHECK_EQ(share.binds(), 1u);
    std::unique_ptr<GraphInstance> again = bindWith(fadeGraph(), members(), "bad.dge", true, &share);
    CHECK(first && again && sharedId(*first) == sharedId(*again));
    CHECK_EQ(share.hits(), 1u);
}

C5R_TEST_MAIN
