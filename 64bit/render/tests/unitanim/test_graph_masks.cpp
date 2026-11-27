// render/tests/unitanim/test_graph_masks.cpp - issue 4410: track masks resolved from the model (unit-controller.md
// 13.3). A .dge Trackmask names its mask only by a binding; the weights are the bones' GrannyTrackMasks members
// (c5m::Bone::track_masks). On 0041's synthetic model (pose_fixture.h, no game file, D83): meshSkeleton,
// resolveTrackMasks (full, partial, none, non-finite), checkTrackMasks (the pre-check form), and an aim-like graph
// evaluated end to end through ControllerPose, bit-identical to the same program with the mask written out by hand.
// 0617: masks built as stock builds them (parent's weight for a bone without the binding, the root 0) and a binding no
// bone carries (VP's destroyer, dreadnought, fleet destroyer, Yamato) resolved as no mask: the unmasked blend.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "c5r/unitanim/controller_pose.h"
#include "c5r/unitanim/graph.h"
#include "pose_fixture.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::units;
using namespace c5r::unitanim;
using c5m::DagNodeType;

namespace {

const char* kAim = "TrackMaskAIM_LAYER";

// The fixture's skeleton "Main" with the aim layer on Arm only (Root, Spine, Leg 0), as the stock tank carries it on
// AIM_DR (13.3); skeleton "Cart" without mask data unless `cart` is set.
c5m::FileInfo maskedModel(bool cart = false) {
    c5m::FileInfo f = posefix::makeModel(false);
    for (c5m::Bone& b : f.skeletons[0].bones) b.track_masks.push_back(std::make_pair(std::string(kAim), b.name == "Arm" ? 1.0f : 0.0f));
    if (cart)
        for (c5m::Bone& b : f.skeletons[1].bones) b.track_masks.push_back(std::make_pair(std::string(kAim), 0.0f));
    return f;
}

struct Fix {
    posefix::Loader ld;
    MemberArt member;
    UnitModel um;
    ModelMesh mesh;
    UnitInput unit;
    PoseRequest req;
    Fix() {
        member.member = "U_AIM";
        member.scale = 0.14f;
        member.files.fxsxml.name = "Unit.fxsxml";
        for (const c5m::FxsAnimation& a : posefix::anims()) {
            c5m::ResolvedAnimation ra;
            ra.file.name = a.file;
            ra.ec = a.ec;
            ra.ec_text = a.ec_text;
            ra.name = a.name;
            member.files.animations.push_back(ra);
        }
        um.gr2 = "unit.gr2";
        um.file.reset(new c5m::FileInfo(maskedModel()));
        mesh.name = "Body";
        mesh.index = 0;
        unit.player = 1;
        unit.id = 9;
        req.unit = &unit;
        req.member = &member;
        req.model = &um;
        req.mesh = &mesh;
        req.figure = 0;
        req.scale = 0.14f;
        req.yaw = 0.25f;
        const float T[3] = {100.0f, -40.0f, 12.0f};
        std::memcpy(req.T, T, sizeof T);
    }
};

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
c5m::DagNode source(int32_t index, int32_t loops) {
    c5m::DagNode n = node(DagNodeType::PoseAnimSource, "PoseAnimSource", {edge("Speed", -1), edge("Offset", -1)});
    n.source_index = index;
    n.loop_count = loops;
    n.clamped_loop = true;
    n.data_edge_count = 1;
    return n;
}

// The aim graphs of 6.3 in small: Blend(Param = ScalarSource 1, From = the body (idle A, ec 1000), To = the aim pose
// (here the fidget, ec 1040), Mask = Trackmask -> TrackMasks[0] = the aim layer's binding).
c5m::DagFile aimGraph(const char* binding = kAim) {
    c5m::DagGraph g;
    g.source_ecs = {1000, 1040};
    g.track_masks = {binding};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                 // 0
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 4), edge("From", 2), edge("To", 3), edge("Mask", 5)}));       // 1
    g.nodes.push_back(source(0, 0));                                                                    // 2 body
    g.nodes.push_back(source(1, 0));                                                                    // 3 aim
    g.nodes.push_back(node(DagNodeType::ScalarSource, "ScalarSource"));                                 // 4
    g.nodes.back().scalar = 1;
    g.nodes.push_back(node(DagNodeType::Trackmask, "Trackmask"));                                       // 5
    g.nodes.back().default_weight = 1;
    g.nodes.back().target_weight = 0;
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

// 0617: the same program with every Blend's mask removed (stock's blend when the mask binding is NULL)
PoseProgram unmasked(const PoseProgram& p) {
    PoseProgram u = p;
    for (PoseNode& n : u.nodes) n.mask = -1;
    u.masks.clear();
    return u;
}

PoseProgram maskOnly(const char* binding = kAim) {
    PoseProgram p;
    TrackMaskWeights tm;
    tm.binding = binding;
    tm.default_weight = 1;
    p.masks.push_back(tm);
    return p;
}

float maxDiff(const Float4* a, const Float4* b, size_t n) {
    float d = 0;
    for (size_t i = 0; i < n; ++i) {
        d = std::fmax(d, std::fabs(a[i].x - b[i].x));
        d = std::fmax(d, std::fabs(a[i].y - b[i].y));
        d = std::fmax(d, std::fabs(a[i].z - b[i].z));
        d = std::fmax(d, std::fabs(a[i].w - b[i].w));
    }
    return d;
}

bool identityRows(const Float4* r) {
    for (uint32_t i = 0; i < kPaletteBones * 3; ++i) {
        const Float4& o = r[i];
        const float want[4] = {i % 3 == 0 ? 1.0f : 0.0f, i % 3 == 1 ? 1.0f : 0.0f, i % 3 == 2 ? 1.0f : 0.0f, 0.0f};
        if (o.x != want[0] || o.y != want[1] || o.z != want[2] || o.w != want[3]) return false;
    }
    return true;
}

void expectMask(const AnimStatus& s, const char* fragment, int line) {
    std::printf("  %s: %s\n", unitanim::name(s.code), s.text.c_str());
    if (s.code != AnimCode::TrackMaskNotBound || s.text.find(fragment) == std::string::npos)
        test::fail(__FILE__, line, std::string("want TrackMaskNotBound with \"") + fragment + "\", got " +
                                       unitanim::name(s.code) + " \"" + s.text + "\"");
}

}  // namespace

C5R_TEST(mesh_skeleton_as_the_pose_step) {
    const c5m::FileInfo f = maskedModel();
    CHECK_EQ(meshSkeleton(f, 0), 0);    // Body: model entry Main
    CHECK_EQ(meshSkeleton(f, 1), 0);    // Legs
    CHECK_EQ(meshSkeleton(f, 2), 1);    // Cart: model entry Cart
    CHECK_EQ(meshSkeleton(f, 3), -1);   // Prop: no bindings
    CHECK_EQ(meshSkeleton(f, 9), -1);   // out of range
}

C5R_TEST(resolve_full_partial_none) {
    c5m::FileInfo f = maskedModel();
    PoseProgram p = maskOnly();
    CHECK(resolveTrackMasks(f, 0, p).ok());
    CHECK_EQ(p.masks[0].bones.size(), static_cast<size_t>(4));
    for (size_t i = 0; i < p.masks[0].bones.size(); ++i)
        CHECK(p.masks[0].weights[i] == (p.masks[0].bones[i] == "Arm" ? 1.0f : 0.0f));
    // resolved masks and masks with explicit bones are left as they are
    PoseProgram again = p;
    CHECK(resolveTrackMasks(f, 1, again).ok());
    CHECK(again.masks[0].bones == p.masks[0].bones && again.masks[0].weights == p.masks[0].weights);
    // 0617, partial as stock builds it: a bone without the member takes its parent's weight, the root 0; every bone is
    // listed (before 0617: not listed, the node's DefaultWeight 1)
    {
        c5m::FileInfo g = maskedModel();
        g.skeletons[0].bones[0].track_masks.clear();                 // Root (no parent): 0
        g.skeletons[0].bones[1].track_masks[0].second = 1.0f;        // Spine 1
        g.skeletons[0].bones[2].track_masks.clear();                 // Arm, child of Spine: 1
        g.skeletons[0].bones[3].track_masks.clear();                 // Leg, child of Root: 0
        PoseProgram q = maskOnly();
        CHECK(resolveTrackMasks(g, 0, q).ok());
        CHECK(q.masks[0].binding == kAim);
        CHECK(q.masks[0].bones == std::vector<std::string>({"Root", "Spine", "Arm", "Leg"}));
        CHECK(q.masks[0].weights == std::vector<float>({0.0f, 1.0f, 1.0f, 0.0f}));
    }
    // 0617, none (skeleton Cart has no mask data, as VP's four edited ships): stock binds no mask and blends unmasked;
    // resolved as no mask: binding "", default weight 1, no bones (before 0617: TrackMaskNotBound, the unit not drawn)
    PoseProgram n = maskOnly();
    CHECK(trackMaskUnbound(f.skeletons[1], kAim));
    CHECK(resolveTrackMasks(f, 1, n).ok());
    CHECK(n.masks[0].binding.empty() && n.masks[0].bones.empty() && n.masks[0].weights.empty());
    CHECK(n.masks[0].default_weight == 1.0f);
    // another binding than the bones carry: no mask too
    PoseProgram o = maskOnly("TrackMaskGlobalLayer");
    CHECK(trackMaskUnbound(f.skeletons[0], "TrackMaskGlobalLayer"));
    CHECK(resolveTrackMasks(f, 0, o).ok());
    CHECK(o.masks[0].binding.empty() && o.masks[0].bones.empty());
    // a binding on later bones but not on the first bone that carries mask data: stock takes the names from that bone
    {
        c5m::FileInfo g = maskedModel();
        g.skeletons[0].bones[0].track_masks = {std::make_pair(std::string("TrackMaskOther"), 1.0f)};
        CHECK(trackMaskUnbound(g.skeletons[0], kAim));
        PoseProgram u = maskOnly();
        CHECK(resolveTrackMasks(g, 0, u).ok());
        CHECK(u.masks[0].binding.empty());
        CHECK(!trackMaskUnbound(maskedModel().skeletons[0], kAim));
    }
    // no skeleton, a non-finite weight
    PoseProgram s = maskOnly();
    expectMask(resolveTrackMasks(f, -1, s), "the mesh has no skeleton", __LINE__);
    f.skeletons[0].bones[2].track_masks[0].second = NAN;
    PoseProgram w = maskOnly();
    expectMask(resolveTrackMasks(f, 0, w), "bone Arm of skeleton Main: weight not finite", __LINE__);
    // a program without binding-only masks is unchanged
    PoseProgram e;
    CHECK(resolveTrackMasks(f, 5, e).ok());
}

C5R_TEST(precheck_form) {
    const c5m::DagFile g = aimGraph();
    c5m::DagFile plain;
    plain.dags.push_back(c5m::DagGraph());   // a graph without track masks
    std::vector<const c5m::DagFile*> graphs = {nullptr, &plain, &g};
    std::vector<std::string> names = {"", "plain.dge", "aim_idle.dge"};
    // 0617: skeleton Cart (mesh 2) has no mask data: no mask there, passes (before 0617: refused)
    CHECK(checkTrackMasks(maskedModel(false), graphs, names).ok());
    CHECK(checkTrackMasks(maskedModel(true), graphs, names).ok());
    // a weight that is not finite stays refused, naming the graph
    c5m::FileInfo nan = maskedModel(true);
    nan.skeletons[0].bones[1].track_masks[0].second = NAN;
    expectMask(checkTrackMasks(nan, graphs, names), "aim_idle.dge: track mask TrackMaskAIM_LAYER: bone Spine of skeleton Main: weight not finite", __LINE__);
    // graphs without masks need nothing from the model
    std::vector<const c5m::DagFile*> none = {nullptr, &plain};
    CHECK(checkTrackMasks(posefix::makeModel(false), none, names).ok());
    // a model without skinned meshes cannot carry a mask
    c5m::FileInfo bare = maskedModel(true);
    for (c5m::Mesh& m : bare.meshes) m.bone_bindings.clear();
    expectMask(checkTrackMasks(bare, graphs, names), "the model has no skinned mesh", __LINE__);
}

C5R_TEST(aim_graph_end_to_end) {
    Fix f;
    ModulePose pose(f.ld.fn());
    ControllerPose cp(pose);
    EcMap em;
    CHECK(makeEcMap(pose, f.member, f.um, em).ok());
    std::unique_ptr<GraphInstance> gi;
    const c5m::DagFile g = aimGraph();
    AnimStatus bs = bindGraph(g, em, "aim_idle.dge", gi);
    CHECK(bs.ok());
    if (!gi) return;
    PathOptions o;
    gi->restart();
    gi->advance(1.25f, o);
    PoseProgram p;
    CHECK(gi->program(o, p).ok());
    CHECK_EQ(p.masks.size(), static_cast<size_t>(1));
    CHECK(p.masks[0].bones.empty());   // the graph names the binding only
    cp.setProgram(f.unit.player, f.unit.id, f.req.figure, p);
    Float4 got[kPaletteBones * 3], want[kPaletteBones * 3], body[kPaletteBones * 3], aim[kPaletteBones * 3];
    const UnitStatus s = cp.rows(f.req, got);
    if (!s.ok()) std::printf("  rows: %s\n", s.text.c_str());
    CHECK(s.ok());
    // the same program with the mask written out by hand (4409's path) is bit-identical
    PoseProgram hand = p;
    hand.masks[0].bones = {"Root", "Spine", "Arm", "Leg"};
    hand.masks[0].weights = {0, 0, 1, 0};
    CHECK(pose.rowsProgram(f.req, hand, want).ok());
    CHECK(std::memcmp(got, want, sizeof got) == 0);
    // Root (slot 0) keeps the body, Arm (slot 2) takes the aim pose: it differs from the body alone
    PoseProgram pb, pa;
    pb.nodes.push_back(p.nodes[0]);
    pa.nodes.push_back(p.nodes[1]);
    CHECK(pose.rowsProgram(f.req, pb, body).ok());
    CHECK(pose.rowsProgram(f.req, pa, aim).ok());
    CHECK(maxDiff(got, body, 3) <= 2e-4f);
    CHECK(maxDiff(got + 6, body + 6, 3) > 1e-3f);
    CHECK(maxDiff(got, aim, 3) > 1e-3f);
    std::printf("  aim layer: Root block %.2g from body, Arm block %.3g from body\n", maxDiff(got, body, 3),
                maxDiff(got + 6, body + 6, 3));
    // the program kept by the controller is not changed by the resolution
    CHECK(cp.program(f.unit.player, f.unit.id, f.req.figure)->masks[0].bones.empty());
    // 0617: a model without mask data (VP's destroyer): drawn as stock draws it, the unmasked blend: bit-identical to
    // the same program with the Blend's mask removed (before 0617: BadModel TrackMaskNotBound, identity rows, not drawn)
    f.um.file.reset(new c5m::FileInfo(posefix::makeModel(false)));
    Float4 r[kPaletteBones * 3], un[kPaletteBones * 3];
    const UnitStatus n = cp.rows(f.req, r);
    if (!n.ok()) std::printf("  rows: %s\n", n.text.c_str());
    CHECK(n.ok());
    CHECK(!identityRows(r));
    CHECK(pose.rowsProgram(f.req, unmasked(p), un).ok());
    CHECK(std::memcmp(r, un, sizeof r) == 0);
    // Param 1 unmasked: every bone takes the aim pose (Root too, which the masked model keeps from the body)
    CHECK(maxDiff(r, aim, 3) <= 2e-4f);
    CHECK(maxDiff(r, body, 3) > 1e-3f);
}

// 0617: the destroyer's case in the shape of VP's edited_aim_ms_modern_ship_attacks / _bombard_defend_loop_graph: a masked
// Blend of the body (From) and the aim pose (To) whose Param is a fade below 1, on a model whose bones carry no mask data.
// Stock binds no mask and lerps every bone by Param; the controller's rows equal the unmasked program's bit for bit, for
// several Param values, and are never identity (the unit is drawn). With mask data the same graph differs (Root keeps
// the body), so the unbound case is not the masked one.
C5R_TEST(destroyer_unbound_mask_blends_unmasked) {
    Fix f;
    f.um.file.reset(new c5m::FileInfo(posefix::makeModel(false)));   // no GrannyTrackMasks on any bone (wickes.gr2)
    ModulePose pose(f.ld.fn());
    EcMap em;
    CHECK(makeEcMap(pose, f.member, f.um, em).ok());
    const float params[4] = {0.0f, 0.03f, 0.35f, 1.0f};
    for (float w : params) {
        c5m::DagFile g = aimGraph();
        g.dags[0].nodes[4].scalar = w;
        std::unique_ptr<GraphInstance> gi;
        CHECK(bindGraph(g, em, "edited_aim_ms_modern_ship_attacks.dge", gi).ok());
        if (!gi) return;
        PathOptions o;
        gi->restart();
        gi->advance(0.8f, o);
        PoseProgram p;
        CHECK(gi->program(o, p).ok());
        ControllerPose cp(pose);
        cp.setProgram(f.unit.player, f.unit.id, f.req.figure, p);
        Float4 got[kPaletteBones * 3], want[kPaletteBones * 3];
        const UnitStatus s = cp.rows(f.req, got);
        if (!s.ok()) std::printf("  rows: %s\n", s.text.c_str());
        CHECK(s.ok());
        CHECK(!identityRows(got));
        CHECK(pose.rowsProgram(f.req, unmasked(p), want).ok());
        CHECK(std::memcmp(got, want, sizeof got) == 0);
        if (w > 0.1f) {   // with mask data on the bones the same graph gives other rows
            f.um.file.reset(new c5m::FileInfo(maskedModel()));
            ControllerPose cm(pose);
            cm.setProgram(f.unit.player, f.unit.id, f.req.figure, p);
            Float4 masked[kPaletteBones * 3];
            CHECK(cm.rows(f.req, masked).ok());
            CHECK(maxDiff(got, masked, 3) > 1e-3f);
            f.um.file.reset(new c5m::FileInfo(posefix::makeModel(false)));
        }
        std::printf("  Param %.2f: unbound mask = unmasked blend, bit-identical\n", w);
    }
}

// 0441 (unit-controller.md 19): ControllerPose keeps its entries across frames and the resolved masks per figure for
// the frame. Every rows() answer equals the one a fresh ControllerPose gives for the same single program (the per-call
// resolve of 4410), bit for bit, across: two meshes on two skeletons (Body on Main, Cart on Cart) asked in turn, a new
// frame with a changed program (time moved), a frame in which the figure has no program (refused, named), the figure
// back, and a model file replaced within a frame (re-resolved: a model without mask data is refused). The entries of
// figures not set in recent frames are dropped when they outnumber the frame's own (programCount and program()).
C5R_TEST(entries_across_frames_equal_a_fresh_source) {
    Fix f;
    f.um.file.reset(new c5m::FileInfo(maskedModel(true)));
    ModulePose pose(f.ld.fn());
    ControllerPose cp(pose);
    EcMap em;
    CHECK(makeEcMap(pose, f.member, f.um, em).ok());
    std::unique_ptr<GraphInstance> gi;
    const c5m::DagFile g = aimGraph();
    CHECK(bindGraph(g, em, "aim_idle.dge", gi).ok());
    if (!gi) return;
    PathOptions o;
    gi->restart();
    ModelMesh cart;
    cart.name = "Cart";
    cart.index = 2;
    size_t compared = 0;
    // the rows a fresh source gives for program p (its own ModulePose: no memo shared with cp's)
    auto fresh = [&](const PoseProgram& p, const ModelMesh& mesh, Float4* out) {
        posefix::Loader ld;
        ModulePose mp(ld.fn());
        ControllerPose one(mp);
        one.setProgram(f.unit.player, f.unit.id, f.req.figure, p);
        PoseRequest rq = f.req;
        rq.mesh = &mesh;
        return one.rows(rq, out);
    };
    auto same = [&](const PoseProgram& p, const ModelMesh& mesh) {
        Float4 got[kPaletteBones * 3], want[kPaletteBones * 3];
        PoseRequest rq = f.req;
        rq.mesh = &mesh;
        const UnitStatus a = cp.rows(rq, got);
        const UnitStatus b = fresh(p, mesh, want);
        CHECK(a.code == b.code && a.text == b.text);
        CHECK(std::memcmp(got, want, sizeof got) == 0);
        ++compared;
    };
    for (int frame = 0; frame < 6; ++frame) {
        gi->advance(0.37f, o);
        PoseProgram p;
        CHECK(gi->program(o, p).ok());
        cp.clearPrograms();
        if (frame == 3) {   // the figure is not drawn this frame: refused, named
            Float4 r[kPaletteBones * 3];
            const UnitStatus n = cp.rows(f.req, r);
            CHECK(n.code == UnitCode::BadInput && n.text == "no pose program for unit (1, 9) figure 0");
            CHECK(identityRows(r));
            CHECK(cp.program(f.unit.player, f.unit.id, f.req.figure) == nullptr);
            CHECK_EQ(cp.programCount(), 0u);
            continue;
        }
        cp.setProgram(f.unit.player, f.unit.id, f.req.figure, p);
        CHECK_EQ(cp.programCount(), 1u);
        same(p, f.mesh);
        same(p, cart);
        same(p, f.mesh);
        CHECK(cp.program(f.unit.player, f.unit.id, f.req.figure)->masks[0].bones.empty());
        if (frame == 5) {   // the model file replaced within the frame: resolved again (0617: without mask data, the
                            // unmasked blend; before 0617 refused)
            f.um.file.reset(new c5m::FileInfo(posefix::makeModel(false)));
            Float4 r[kPaletteBones * 3], u[kPaletteBones * 3];
            const UnitStatus n = cp.rows(f.req, r);
            CHECK(n.ok());
            CHECK(pose.rowsProgram(f.req, unmasked(p), u).ok());
            CHECK(std::memcmp(r, u, sizeof r) == 0);
            same(p, f.mesh);
        }
    }
    CHECK_EQ(compared, 16u);
    // many figures in one frame, then one: the old entries are not found, and dropped at the next frame's start
    cp.clearPrograms();
    PoseProgram one;
    CHECK(gi->program(o, one).ok());
    for (uint32_t k = 0; k < 200; ++k) cp.setProgram(2, 7, k, one);
    CHECK_EQ(cp.programCount(), 200u);
    cp.clearPrograms();
    cp.setProgram(2, 7, 5, one);
    CHECK_EQ(cp.programCount(), 1u);
    CHECK(cp.program(2, 7, 4) == nullptr && cp.program(2, 7, 5) != nullptr);
    cp.clearPrograms();
    CHECK(cp.program(2, 7, 5) == nullptr);
    CHECK_EQ(cp.programCount(), 0u);
}

C5R_TEST_MAIN
