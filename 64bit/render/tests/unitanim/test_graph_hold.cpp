// render/tests/unitanim/test_graph_hold.cpp - issue 4406 round 2 (epic 0374): GraphOptions::hold_from_continue
// (graph.h; unit-controller.md 15.8) on synthetic graphs (no game file, D83). Held: a ContinueOffset source under a
// Blend's From input stays at the previous state's clip time while the fade runs; the To side and a ContinueOffset
// source that is not on a From side run on; the option is on by default (MC #2117); off is the graphs as read
// (test_graph.cpp binds with it off).
#include <cmath>
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
c5m::DagNode source(int32_t index, int32_t loops, bool cont) {
    c5m::DagNode n = node(DagNodeType::PoseAnimSource, "PoseAnimSource", {edge("Speed", -1), edge("Offset", -1)});
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

// Blend(Param = hint 5 over 0.3333 s, From = Selection(path 0) over idle A / idle B (looping, ContinueOffset 1),
// To = fidget (once)): the idle -> fidget shape of 6.3, without the BlockCurve.
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
    g.nodes.push_back(callback(5, 0.3333f));                                                           // 4
    g.nodes.push_back(source(2, 1, false));                                                            // 5 fidget
    g.nodes.push_back(source(0, 0, true));                                                             // 6 idle A
    g.nodes.push_back(source(1, 0, true));                                                             // 7 idle B
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

// One ContinueOffset idle source at the root (no Blend): not a From side.
c5m::DagFile loneGraph() {
    c5m::DagGraph g;
    g.source_ecs = {1000};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));
    g.nodes.push_back(source(0, 0, true));
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

// 0782: mounted_running_loop.dge's shape (unit-server-scene.md 19.12): Blend(Param = path 7 (hint 7), From = the run
// (looping, ContinueOffset 1), To = a turn clip (looping, ContinueOffset 1)). Ecs 1000 / 2000 stand in for 1400 / 1480.
// With curve = true the Param is a BlockCurve over a hint-5 fade instead: a fade, so the From side is held as before.
c5m::DagFile pathBlendGraph(bool curve) {
    c5m::DagGraph g;
    g.source_ecs = {1000, 2000};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                // 0
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 2), edge("From", 3), edge("To", 4), edge("Mask", -1)}));     // 1
    if (curve) {
        c5m::DagNode c = node(DagNodeType::BlockCurve, "BlockCurve", {edge("Input", 5)});            // 2
        c5m::DagBlock b;
        b.type = 3;
        b.duration = 1.0f;
        c.blocks = {b};
        c.scale = 1.0f;
        g.nodes.push_back(c);
    } else {
        g.nodes.push_back(callback(7, 7));                                                             // 2 path 7
    }
    g.nodes.push_back(source(0, 0, true));                                                             // 3 run
    g.nodes.push_back(source(1, 0, true));                                                             // 4 turn
    if (curve) g.nodes.push_back(callback(5, 0.5f));                                                   // 5 fade
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

EcMap members() {
    EcMap m;
    m.ecs = {1000, 2000, 1040};
    m.clips = {"U_IdleA.gr2", "U_IdleB.gr2", "U_FidgetA.gr2"};
    m.durations = {6.0f, 5.0f, 3.0f};
    return m;
}

std::unique_ptr<GraphInstance> bind(const c5m::DagFile& f, bool hold) {
    std::unique_ptr<GraphInstance> g;
    GraphOptions o;
    o.hold_from_continue = hold;
    AnimStatus s = bindGraph(f, members(), "hold.dge", o, g);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, s.text);
    return g;
}

bool near(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol; }

}  // namespace

C5R_TEST(from_side_continue_source_held_with_the_option) {
    for (int hold = 0; hold < 2; ++hold) {
        std::unique_ptr<GraphInstance> g = bind(fadeGraph(), hold != 0);
        if (!g) return;
        PathOptions o;
        PoseProgram p;
        ActiveSource prev;
        prev.ec = 1000;
        prev.t = 5.434f;
        g->restartFrom({prev});
        CHECK(g->program(o, p).ok());
        CHECK(p.single());
        CHECK(near(p.nodes[0].t, 5.434f));       // the hand-over is the same either way
        g->advance(0.2f, o);
        CHECK(g->program(o, p).ok());
        CHECK_EQ(p.nodes.size(), static_cast<size_t>(3));
        CHECK_EQ(p.nodes[0].ec, 1000);
        CHECK(near(p.nodes[0].t, hold ? 5.434f : 5.634f));   // held at the entry time / running at Speed 1
        CHECK_EQ(p.nodes[1].ec, 1040);
        CHECK(near(p.nodes[1].t, 0.2f));                     // the To side runs either way
        CHECK(near(p.nodes[2].w, 0.2f / 0.3333f, 1e-4f));    // the weight is not touched
        std::vector<ActiveSource> act;
        g->active(o, act);
        CHECK_EQ(act.size(), static_cast<size_t>(2));
        CHECK(near(act[0].t, hold ? 5.434f : 5.634f));
        CHECK(near(act[0].prev_t, hold ? 5.434f : 5.434f));
        g->advance(0.2f, o);                                 // the fade is over: the fidget alone
        CHECK(g->program(o, p).ok());
        CHECK(p.single());
        CHECK_EQ(p.nodes[0].ec, 1040);
        CHECK(near(p.nodes[0].t, 0.4f));
    }
}

C5R_TEST(continue_source_off_the_from_side_runs_on) {
    std::unique_ptr<GraphInstance> g = bind(loneGraph(), true);
    if (!g) return;
    PathOptions o;
    PoseProgram p;
    ActiveSource prev;
    prev.ec = 1000;
    prev.t = 2.0f;
    g->restartFrom({prev});
    g->advance(0.5f, o);
    CHECK(g->program(o, p).ok());
    CHECK(p.single());
    CHECK(near(p.nodes[0].t, 2.5f));
}

C5R_TEST(default_options_hold_the_from_side) {   // MC #2117: held by default, also through the 3-argument bindGraph
    GraphOptions o;
    CHECK(o.hold_from_continue);
    std::unique_ptr<GraphInstance> a, b;
    CHECK(bindGraph(fadeGraph(), members(), "a.dge", a).ok());
    CHECK(bindGraph(fadeGraph(), members(), "b.dge", o, b).ok());
    ActiveSource prev;
    prev.ec = 1000;
    prev.t = 1.0f;
    a->restartFrom({prev});
    b->restartFrom({prev});
    PathOptions po;
    a->advance(0.1f, po);
    b->advance(0.1f, po);
    PoseProgram pa, pb;
    CHECK(a->program(po, pa).ok() && b->program(po, pb).ok());
    CHECK_EQ(pa.nodes.size(), pb.nodes.size());
    CHECK(near(pa.nodes[0].t, 1.0f) && near(pb.nodes[0].t, 1.0f));
}

// 0782: under a path weight (not a fade) the From side runs, at weight 0 (the server never steers: path 7 = 0) and
// inside a turn (0 < w < 1); the To side runs as before. Before 0782 the run stood at its entry time: the mounted
// unit's legs still through the whole walk.
C5R_TEST(from_side_under_a_path_weight_runs) {
    for (float w7 : {0.0f, 0.5f}) {
        std::unique_ptr<GraphInstance> g = bind(pathBlendGraph(false), true);
        if (!g) return;
        PathOptions o;
        o.v[7] = w7;
        ActiveSource prev;
        prev.ec = 1000;
        prev.t = 0.2f;
        g->restartFrom({prev});
        g->advance(0.3f, o);
        PoseProgram p;
        CHECK(g->program(o, p).ok());
        CHECK_EQ(p.nodes[0].ec, 1000);
        CHECK(near(p.nodes[0].t, 0.5f));          // 0.2 + 0.3: running (held would be 0.2)
        if (w7 > 0) {
            CHECK_EQ(p.nodes.size(), static_cast<size_t>(3));
            CHECK_EQ(p.nodes[1].ec, 2000);
            CHECK(near(p.nodes[1].t, 0.3f));
            CHECK(near(p.nodes[2].w, 0.5f));
        } else {
            CHECK(p.single());
        }
    }
}

// 0782: a BlockCurve over a hint-5 fade is a fade: the From side is held as before 0782.
C5R_TEST(from_side_under_a_curve_fade_still_held) {
    std::unique_ptr<GraphInstance> g = bind(pathBlendGraph(true), true);
    if (!g) return;
    PathOptions o;
    ActiveSource prev;
    prev.ec = 1000;
    prev.t = 0.2f;
    g->restartFrom({prev});
    g->advance(0.1f, o);                          // fade 0.1 / 0.5 = 0.2 through the ease: 0 < w < 1
    PoseProgram p;
    CHECK(g->program(o, p).ok());
    CHECK_EQ(p.nodes.size(), static_cast<size_t>(3));
    CHECK_EQ(p.nodes[0].ec, 1000);
    CHECK(near(p.nodes[0].t, 0.2f));              // held at the entry time
}

C5R_TEST_MAIN
