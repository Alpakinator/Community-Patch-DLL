// render/tests/unitanim/test_controller_missing_clip.cpp - issue 4770 (epic 0042; unit-controller.md 13.4): a state
// whose graph needs a clip the member's .fxsxml names but the install does not ship (VP's Assyrian siege tower, ec 1140,
// 17.7). Over the product's graph factory (ClipGraphFactory with a clip loader that answers from a table, so the real
// bindGraph runs), hand-built logic and a fixture member (no game file, D83):
//   (1) the request keeps the figures in their state, clip and clip time: the pose programs equal byte for byte those
//       of a twin controller that never got the request, the unit stays Ok, and one LOUD line names unit, member,
//       state, ec and file;
//   (2) the same request again is a new occurrence: one more line, and the count counts both;
//   (3) an ec the .fxsxml does not name, and a clip that is there but unreadable, still fail EcNotBound as before
//       (status, no program, not flagged, nothing counted);
//   (4) a start on a missing clip (no state to keep) fails as before, with the flag.
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "anim_testkit.h"
#include "c5r/unitanim/driver.h"
#include "../../src/unitanim/missing_clip.h"

using namespace c5r::unitanim;
using namespace anim_test;
using c5m::DagNodeType;

namespace {

const double kH = 1.0 / 30.0;

// ------------------------------------------------------------------------------------------- real graphs (bindGraph)
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
    n.has_node_data = true;
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
    n.speed = 1;
    return n;
}
c5m::DagNode callback(int hint, float value) {
    c5m::DagNode n = node(DagNodeType::ScalarCallback, "ScalarCallback", {edge("Input (0)", -1), edge("Input (1)", -1)});
    n.callback_hint = hint;
    n.callback_value = value;
    n.has_callback_value = true;
    return n;
}
c5m::DagFile file(c5m::DagGraph g) {
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}
// Subgraph -> Selection(path `path`, default `def`) over two looping sources (ContinueOffset 1).
c5m::DagFile selGraph(int32_t ecA, int32_t ecB, int path, int32_t def) {
    c5m::DagGraph g;
    g.source_ecs = {ecA, ecB};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                    // 0
    g.nodes.push_back(node(DagNodeType::Selection, "Selection",
                           {edge("Selector", 2), edge("Pose in (0)", 3), edge("Pose in (1)", 4)}));       // 1
    g.nodes.back().default_edge = def;
    g.nodes.push_back(callback(7, static_cast<float>(path)));                                             // 2
    g.nodes.push_back(source(0, 0, true));                                                                // 3
    g.nodes.push_back(source(1, 0, true));                                                                // 4
    return file(g);
}
// Subgraph -> one source.
c5m::DagFile oneGraph(int32_t ec, int32_t loops) {
    c5m::DagGraph g;
    g.source_ecs = {ec};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));
    g.nodes.push_back(source(0, loops, false));
    return file(g);
}

// Every field of a program, floats by their bits: equal strings = byte-equal programs.
std::string bytes(const PoseProgram& p) {
    std::string s;
    auto put = [&](const void* d, size_t n) { s.append(static_cast<const char*>(d), n); };
    auto str = [&](const std::string& x) {
        const uint64_t n = x.size();
        put(&n, sizeof n);
        s += x;
    };
    for (const PoseNode& n : p.nodes) {
        put(&n.kind, sizeof n.kind);
        str(n.clip);
        put(&n.ec, sizeof n.ec);
        put(&n.t, sizeof n.t);
        put(&n.a, sizeof n.a);
        put(&n.b, sizeof n.b);
        put(&n.w, sizeof n.w);
        put(&n.mask, sizeof n.mask);
    }
    for (const TrackMaskWeights& m : p.masks) {
        str(m.binding);
        put(&m.default_weight, sizeof m.default_weight);
        for (const std::string& b : m.bones) str(b);
        for (float w : m.weights) put(&w, sizeof w);
        put(&m.target_weight, sizeof m.target_weight);
    }
    return s;
}

// A controller over the idle machine (anim_testkit's omni shape, no fidget roll) with the product's graph factory. The
// member "M" names warriorClips(); `missing` clips are named but not in the install (the art step's FileRef::found
// false, and the clip loader finds no file, as the product's loader over the install would); `unreadable` clips are
// found but give no animation (a malformed file); `unnamed` ecs are left out of the .fxsxml.
struct MRig {
    MemFiles files;
    FakeArt art;
    std::set<std::string> missing, unreadable;
    std::set<int> unnamed;
    std::unique_ptr<ClipGraphFactory> graphs;
    Controller c;
    ControllerOptions opt;
    std::vector<std::string> lines;   // the LOUD lines

    void open(int figures, bool defaultIdle = false) {
        c5r::units::UnitArt a;
        a.art_row = "ART_W";
        std::vector<std::pair<std::string, int>> clips;
        for (auto& cl : warriorClips())
            if (!unnamed.count(cl.second)) clips.push_back(cl);
        c5r::units::MemberArt m = memberArt("M", figures, "omni.fsmxml", clips);
        for (auto& an : m.files.animations)
            if (missing.count(an.file.name)) an.file.found = false;
        a.members.push_back(m);
        art.rows["W"] = a;
        const std::set<std::string> gone = missing, bad = unreadable;
        graphs.reset(new ClipGraphFactory([gone, bad](const std::string& key, c5m::FileInfo& out, bool& miss,
                                                      std::string& why) {
            out = c5m::FileInfo();
            miss = false;
            if (gone.count(key)) {
                miss = true;
                why = key + ": not in the install";
                return false;
            }
            if (!bad.count(key)) {
                c5m::Animation an;
                an.name = key;
                an.duration = key == "w_attacka.gr2" ? 1.0f : key == "w_fidgeta.gr2" ? 2.0f : 6.0f;
                out.animations.push_back(an);
            }
            return true;
        }));
        c.open(files, art, *graphs, opt);
        setMissingClipSink(c, [this](const std::string& l) { lines.push_back(l); });
        c5m::StateMachine sm = idleMachine(0.0f);
        if (defaultIdle) sm.default_state = 1;   // a default state with a graph: the start is from == SIZE_MAX
        CHECK_ANIM(c.logic()->putMachine("omni.fsmxml", sm), AnimCode::Ok);
        c.logic()->putGraph("idle.dge", selGraph(1000, 2000, 0, 0));
        c.logic()->putGraph("idlecont.dge", selGraph(1000, 2000, 0, 0));
        c.logic()->putGraph("fidget.dge", oneGraph(1040, 1));
        c.logic()->putGraph("fortify.dge", oneGraph(1520, 0));
        c.logic()->putGraph("attack.dge", oneGraph(1100, 1));
    }
    const FigureProgram* prog(int player, int id, uint32_t figure) const {
        for (auto& p : c.programs())
            if (p.player == player && p.id == id && p.figure == figure) return &p;
        return nullptr;
    }
    size_t programsOf(int player, int id) const {
        size_t n = 0;
        for (auto& p : c.programs())
            if (p.player == player && p.id == id) ++n;
        return n;
    }
};

TimelineEvent ev(EventKind k, int player = 0, int id = 1) {
    TimelineEvent e;
    e.kind = k;
    e.player = player;
    e.id = id;
    return e;
}
TimelineEvent createEv(int player, int id) {
    TimelineEvent e = ev(EventKind::Create, player, id);
    e.unit = Rig::unit(player, id, "W");
    return e;
}
TimelineEvent attackEv() {
    TimelineEvent e = ev(EventKind::Action);
    e.action = ActionKind::Attack;   // BasicBridge: path 4 = 1100 -> idle 1 -> attack 9 (attack.dge, ec 1100)
    return e;
}
TimelineFrame frame(uint64_t seq, double dt) {
    TimelineFrame f;
    f.seq = seq;
    f.dt = dt;
    return f;
}
const AnimStatus& unitStatus(const Controller& c, int player = 0, int id = 1) {
    static const AnimStatus none = AnimStatus::Fail(AnimCode::UnitFailed, "the unit is not held");
    const AnimStatus* s = c.status(player, id);
    return s ? *s : none;
}

// Applies `f` to both rigs (the request only to `a`'s copy when `request`) and checks that every figure's program is
// byte-equal and in state 1 (idle) in both.
void stepBoth(MRig& a, MRig& b, uint64_t seq, bool request, uint32_t figures) {
    TimelineFrame fa = frame(seq, kH), fb = frame(seq, kH);
    if (request) fa.events.push_back(attackEv());
    CHECK_ANIM(a.c.apply(fa), AnimCode::Ok);
    CHECK_ANIM(b.c.apply(fb), AnimCode::Ok);
    CHECK_ANIM(unitStatus(a.c), AnimCode::Ok);
    CHECK_ANIM(unitStatus(b.c), AnimCode::Ok);
    for (uint32_t k = 0; k < figures; ++k) {
        const FigureProgram* pa = a.prog(0, 1, k);
        const FigureProgram* pb = b.prog(0, 1, k);
        CHECK(pa && pb);
        if (!pa || !pb) return;
        CHECK_EQ(pa->state, 1);
        CHECK_EQ(pb->state, 1);
        CHECK(bytes(pa->program) == bytes(pb->program));
    }
}

}  // namespace

C5R_TEST(missing_clip_flag_text) {
    CHECK(missingInInstall(AnimStatus::Fail(AnimCode::EcNotBound, std::string(kMissingInInstall) + "ec 1 clip x.gr2")));
    CHECK(!missingInInstall(AnimStatus::Fail(AnimCode::EcNotBound, "attack.dge: source ec the member lacks: 1100")));
    CHECK(!missingInInstall(AnimStatus::Fail(AnimCode::BadEvent, std::string(kMissingInInstall) + "x")));
    CHECK(!missingInInstall(AnimStatus::Ok()));
}

// The control: with the attack clip in the install, the same request enters the attack state (so the request does
// reach the transition the missing clip stops).
C5R_TEST(present_clip_enters_attack) {
    MRig r;
    r.open(3);
    TimelineFrame f = frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    TimelineFrame k = frame(2, kH);
    k.events.push_back(attackEv());
    CHECK_ANIM(r.c.apply(k), AnimCode::Ok);
    CHECK_ANIM(unitStatus(r.c), AnimCode::Ok);
    for (uint32_t i = 0; i < 3; ++i) {
        CHECK(r.prog(0, 1, i) != nullptr);
        if (r.prog(0, 1, i)) CHECK_EQ(r.prog(0, 1, i)->state, 9);
    }
    CHECK(r.lines.empty());
    CHECK_EQ(missingClipReverts(r.c), size_t(0));
}

// (1) and (2): the figures keep state, clip and clip time (byte-equal to a twin without the request), the unit stays
// Ok, one LOUD line per occurrence with unit, member, state, ec and file; a second request logs once more; the count
// counts both.
C5R_TEST(missing_clip_keeps_state_and_logs_per_occurrence) {
    MRig a, b;
    a.missing.insert("w_attacka.gr2");
    b.missing.insert("w_attacka.gr2");
    a.open(3);
    b.open(3);
    TimelineFrame fa = frame(1, kH), fb = frame(1, kH);
    fa.events.push_back(createEv(0, 1));
    fb.events.push_back(createEv(0, 1));
    CHECK_ANIM(a.c.apply(fa), AnimCode::Ok);
    CHECK_ANIM(b.c.apply(fb), AnimCode::Ok);
    for (uint64_t s = 2; s <= 4; ++s) stepBoth(a, b, s, false, 3);
    const std::string before = bytes(a.prog(0, 1, 0)->program);
    const std::string clipBefore = a.prog(0, 1, 0)->program.nodes.back().clip;

    // the request: kept out of state 9, one line, counted once
    stepBoth(a, b, 5, true, 3);
    CHECK_EQ(a.lines.size(), size_t(1));
    CHECK_EQ(missingClipReverts(a.c), size_t(1));
    CHECK(bytes(a.prog(0, 1, 0)->program) != before);                          // the clip time went on by one step
    CHECK_EQ(a.prog(0, 1, 0)->program.nodes.back().clip, clipBefore);          // the same clip
    CHECK_NEAR(a.prog(0, 1, 0)->program.nodes.back().t - kH, 4 * kH, 1e-5);    // and its time, h on from frame 4
    if (!a.lines.empty()) {
        const std::string& l = a.lines[0];
        CHECK(l.compare(0, 15, "LOUD unitanim: ") == 0);
        CHECK(l.find(kMissingInInstall) != std::string::npos);
        CHECK(l.find("unit (0, 1) W") != std::string::npos);
        CHECK(l.find("member M (M.fxsxml)") != std::string::npos);
        CHECK(l.find("omni.fsmxml state 1 -> 9 (attack.dge)") != std::string::npos);
        CHECK(l.find("ec 1100 clip w_attacka.gr2") != std::string::npos);
        CHECK(l.find("the figures keep state 1") != std::string::npos);
    }
    CHECK(b.lines.empty());

    // the request stands (path 4 = 1100): the state is kept out again every update, silently
    for (uint64_t s = 6; s <= 40; ++s) stepBoth(a, b, s, false, 3);
    CHECK_EQ(a.lines.size(), size_t(1));
    CHECK_EQ(missingClipReverts(a.c), size_t(1));

    // (2) the same request again: a new occurrence
    stepBoth(a, b, 41, true, 3);
    CHECK_EQ(a.lines.size(), size_t(2));
    CHECK_EQ(missingClipReverts(a.c), size_t(2));
    if (a.lines.size() == 2) CHECK(a.lines[1] == a.lines[0]);
    for (uint64_t s = 42; s <= 50; ++s) stepBoth(a, b, s, false, 3);
    CHECK_EQ(a.lines.size(), size_t(2));
    CHECK_EQ(missingClipReverts(a.c), size_t(2));

    // the request answered by another (fortify, 1500): the figures go on as the twin's
    TimelineFrame ka = frame(51, kH), kb = frame(51, kH);
    TimelineEvent fo = ev(EventKind::Fortify);
    fo.a = 1;
    ka.events.push_back(fo);
    kb.events.push_back(fo);
    CHECK_ANIM(a.c.apply(ka), AnimCode::Ok);
    CHECK_ANIM(b.c.apply(kb), AnimCode::Ok);
    for (uint32_t k = 0; k < 3; ++k) {
        CHECK_EQ(a.prog(0, 1, k)->state, 7);
        CHECK(bytes(a.prog(0, 1, k)->program) == bytes(b.prog(0, 1, k)->program));
    }
    CHECK_EQ(a.lines.size(), size_t(2));
    // open() starts the count again
    a.c.open(a.files, a.art, *a.graphs, a.opt);
    CHECK_EQ(missingClipReverts(a.c), size_t(0));
}

// (3) an ec the member's .fxsxml does not name: EcNotBound as before, not flagged, no program, nothing counted.
C5R_TEST(unnamed_ec_fails_as_before) {
    MRig r;
    r.unnamed.insert(1100);
    r.open(3);
    TimelineFrame f = frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_ANIM(unitStatus(r.c), AnimCode::Ok);
    TimelineFrame k = frame(2, kH);
    k.events.push_back(attackEv());
    CHECK_ANIM(r.c.apply(k), AnimCode::Ok);
    const AnimStatus& st = unitStatus(r.c);
    CHECK_ANIM(st, AnimCode::EcNotBound);
    CHECK(!missingInInstall(st));
    CHECK(st.text.find("1100") != std::string::npos);
    CHECK_EQ(r.programsOf(0, 1), size_t(0));
    CHECK(r.lines.empty());
    CHECK_EQ(missingClipReverts(r.c), size_t(0));
}

// (3, D118) a clip the install has but that gives no animation (malformed): EcNotBound as before, not flagged.
C5R_TEST(unreadable_clip_fails_as_before) {
    MRig r;
    r.unreadable.insert("w_attacka.gr2");
    r.open(3);
    TimelineFrame f = frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    TimelineFrame k = frame(2, kH);
    k.events.push_back(attackEv());
    CHECK_ANIM(r.c.apply(k), AnimCode::Ok);
    const AnimStatus& st = unitStatus(r.c);
    CHECK_ANIM(st, AnimCode::EcNotBound);
    CHECK(!missingInInstall(st));
    CHECK(st.text.find("w_attacka.gr2") != std::string::npos);
    CHECK_EQ(r.programsOf(0, 1), size_t(0));
    CHECK(r.lines.empty());
    CHECK_EQ(missingClipReverts(r.c), size_t(0));
}

// (4) a start on a missing clip, from the default state itself (from == SIZE_MAX): fails as before, flagged.
C5R_TEST(start_on_missing_clip_fails_flagged) {
    MRig r;
    r.missing.insert("w_idlea.gr2");
    r.open(3, true);
    TimelineFrame f = frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    const AnimStatus& st = unitStatus(r.c);
    CHECK_ANIM(st, AnimCode::EcNotBound);
    CHECK(missingInInstall(st));
    CHECK(st.text.find("ec 1000 clip w_idlea.gr2") != std::string::npos);
    CHECK_EQ(r.programsOf(0, 1), size_t(0));
    CHECK(r.lines.empty());
    CHECK_EQ(missingClipReverts(r.c), size_t(0));
}

// (4) the same through the default chooser (graphless 2 -> idle 1 before any graph): no state to keep either.
C5R_TEST(start_through_chooser_on_missing_clip_fails_flagged) {
    MRig r;
    r.missing.insert("w_idleb.gr2");
    r.open(3);
    TimelineFrame f = frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    const AnimStatus& st = unitStatus(r.c);
    CHECK_ANIM(st, AnimCode::EcNotBound);
    CHECK(missingInInstall(st));
    CHECK(st.text.find("ec 2000 clip w_idleb.gr2") != std::string::npos);
    CHECK_EQ(r.programsOf(0, 1), size_t(0));
    CHECK(r.lines.empty());
    CHECK_EQ(missingClipReverts(r.c), size_t(0));
}

C5R_TEST_MAIN
