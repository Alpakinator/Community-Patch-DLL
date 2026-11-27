// render/src/unitanim/graph.cpp - the blend-DAG evaluator (epic 0374, issue 4409): a .dge bound to a member, run in
// one state of one figure, flattened to a pose program (c5r/unitanim/graph.h, pose_program.h). Behaviour of the
// graphs as read: research/05-graphics-composition/unit-controller.md 6.3; this code: 13.2. Own code.
// 0470 (19.7): a bound graph is an immutable Shape (what the walks read of the file, and the member's sources), shared
// through a GraphShare between equal binds, and an instance's own clock and sources' start times.
//
// Readings this code makes, each named in 13.2 [I unless marked]:
//   - ScalarCallback hint 7 returns path option CallbackValue (0-8) [C by structure, 6.3]; hint 5 returns the fade
//     weight min(1, time in the state / CallbackValue);
//   - Selection passes input int(Selector + 0.5); out of range: input DefaultEdge;
//   - PoseAnimSource: clip time = start + Speed x (time in the state) + Offset (Offset input connected: its scalar,
//     in seconds); LoopCount 0 loops, n >= 1 plays n times and holds the last pose; start = 0, or with ContinueOffset
//     1 the clip time the same ec had in the previous state (restartFrom);
//   - Blend: From x (1 - w) + To x w per bone, w = Param clamped to [0, 1]; without a mask w <= 0 is From alone and
//     w >= 1 is To alone (so a finished fade is a single source); the mask goes into the program by its binding;
//     an input with no edge gives no pose, so the blend is its connected input alone (0439).
#include "c5r/unitanim/graph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

namespace c5r { namespace unitanim {

const std::string* EcMap::find(int32_t ec) const {
    for (size_t i = 0; i < ecs.size() && i < clips.size(); ++i)
        if (ecs[i] == ec) return &clips[i];
    return nullptr;
}

float EcMap::duration(int32_t ec) const {
    for (size_t i = 0; i < ecs.size() && i < clips.size(); ++i)
        if (ecs[i] == ec) return i < durations.size() ? durations[i] : 0.0f;
    return 0.0f;
}

float blockEase3(float u) {
    u = u < 0 ? 0 : (u > 1 ? 1 : u);
    return u * u * (3.0f - 2.0f * u);
}

float evalBlockCurve(const std::vector<c5m::DagBlock>& blocks, float scale, float x) {
    // value of block k at local position u in [0, 1], given the value at its start
    auto at = [](const c5m::DagBlock& b, float start, float u) {
        float v;
        if (b.type == 3) v = blockEase3(b.flip_x ? 1.0f - u : u);
        else v = start;   // type 0 (and any other): hold
        if (b.flip_y && b.type == 3) v = 1.0f - v;
        return v;
    };
    float level = 0, pos = 0;
    bool any = false;
    float first_start = 0;
    for (const c5m::DagBlock& b : blocks) {
        if (!(b.duration > 0)) continue;   // zero width: covers nothing and changes nothing
        const float start_val = at(b, level, 0.0f);
        if (!any) {
            any = true;
            first_start = start_val;
            if (x < pos) return first_start * scale;
        }
        if (x < pos + b.duration) return at(b, level, (x - pos) / b.duration) * scale;
        level = at(b, level, 1.0f);
        pos += b.duration;
    }
    return level * scale;
}

namespace {

using c5m::DagNode;
using c5m::DagNodeType;

bool isScalarNode(DagNodeType t) {
    return t == DagNodeType::ScalarCallback || t == DagNodeType::ScalarSource || t == DagNodeType::BlockCurve;
}
bool isPoseNode(DagNodeType t) {
    return t == DagNodeType::PoseAnimSource || t == DagNodeType::Selection || t == DagNodeType::Blend ||
           t == DagNodeType::Subgraph;
}

// 0470: heap bytes of a string and a vector as requested (GraphFootprint): a string's buffer beyond the small-string
// one (capacity + 1 when it does not fit the object), a vector's capacity.
size_t heapOf(const std::string& s) {
    const std::string empty;
    return s.capacity() > empty.capacity() ? s.capacity() + 1 : 0;
}
template <class T> size_t heapOf(const std::vector<T>& v) { return v.capacity() * sizeof(T); }

// The input edge named `name`, or nullptr.
const c5m::DagEdge* input(const DagNode& n, const char* name) {
    for (const c5m::DagEdge& e : n.inputs)
        if (e.name == name) return &e;
    return nullptr;
}

// 0441: a node's named inputs, looked up once at bind time (input()'s rule: the first edge of that name; -1 when the
// edge is absent or not connected, which build() treated alike), so that the walks below compare no strings and build
// no "Pose in (k)" names per call.
struct NodeEdges {
    int32_t pose_out = -1, offset = -1, selector = -1, param = -1, from = -1, to = -1, mask = -1, input = -1;
    std::vector<int32_t> pose_in;   // "Pose in (0)", "Pose in (1)", ... up to the first missing one
};

int32_t edgeNode(const DagNode& n, const char* name) {
    const c5m::DagEdge* e = input(n, name);
    return e ? e->input_node : -1;
}

// 0470: what the walks read of one node, copied at bind time from the DagNode (the fields of 4409's evaluator and
// 0441's named inputs); nothing else of the DagGraph is kept.
struct EvalNode {
    DagNodeType type = DagNodeType::Unknown;
    int32_t loop_count = 0;          // PoseAnimSource
    float speed = 1, offset = 0;
    bool continue_offset = false;
    int32_t default_edge = 0;        // Selection
    float scale = 1;                 // BlockCurve (and blocks)
    int32_t callback_hint = 0;       // ScalarCallback
    float callback_value = 0, scalar = 0;
    int32_t binding_index = 0;       // Trackmask
    float default_weight = 1, target_weight = 0;
    std::vector<c5m::DagBlock> blocks;
    NodeEdges e;                     // 0441: the named inputs
    int32_t src = -1;                // 0441: the node's index in Shape::sources (-1: none)
};

// A bound source: the graph's PoseAnimSource node and the member's clip for its ec.
struct SourceDef {
    int32_t node = -1;
    int32_t ec = 0;
    std::string clip;
    float duration = 0;
};

// 0470 (unit-controller.md 19.7): the immutable part of a graph bound to a member: one graph file's evaluated nodes and
// the member's sources. Shared (GraphShare) between every instance bound to an equal shape; an instance keeps only its
// clock and its sources' start times.
struct Shape {
    std::string name;                        // the file name, for the failure texts
    int32_t root = -1;
    std::vector<EvalNode> nodes;
    std::vector<std::string> track_masks;    // TrackMasks[k].BindingName
    std::vector<SourceDef> sources;
    bool may_fail = true;                    // 4468: programMayFail()
};

// 0441: what a walk of the graph hands on. The walk (Graph::walk) is build()'s traversal, its failures and its float
// operations unchanged; a sink takes the sources it reaches and, for a program, the nodes. ProgramSink is program()'s
// output, ActiveSink active()'s, EcSink the ECComplete test's (no copy of a clip name, no node).
struct ProgramSink {
    static const bool kActive = false;
    PoseProgram& out;
    void source(const SourceDef&, const EvalNode&, float, float, bool, bool, float) {}
    int32_t sample(const SourceDef& s, float t) {
        PoseNode pn;
        pn.kind = PoseNode::Sample;
        pn.clip = s.clip;
        pn.ec = s.ec;
        pn.t = t;
        out.nodes.push_back(pn);
        return static_cast<int32_t>(out.nodes.size()) - 1;
    }
    int32_t blend(int32_t a, int32_t b, float w, const EvalNode* mask, const std::vector<std::string>& bindings) {
        PoseNode bn;
        bn.kind = PoseNode::Blend;
        bn.a = a;
        bn.b = b;
        bn.w = w;
        if (mask) {
            TrackMaskWeights tm;
            tm.binding = bindings[static_cast<size_t>(mask->binding_index)];
            tm.default_weight = mask->default_weight;
            tm.target_weight = mask->target_weight;
            bn.mask = static_cast<int32_t>(out.masks.size());
            out.masks.push_back(tm);
        }
        out.nodes.push_back(bn);
        return static_cast<int32_t>(out.nodes.size()) - 1;
    }
};

struct ActiveSink {
    static const bool kActive = true;
    std::vector<ActiveSource>& act;
    bool clip = true;   // false: activeTimes() (no clip name copied)
    void source(const SourceDef& s, const EvalNode& n, float t, float prev_t, bool looped, bool done, float weight) {
        ActiveSource a;
        a.ec = s.ec;
        if (clip) a.clip = s.clip;
        a.t = t;
        a.prev_t = prev_t;
        a.looped = looped;
        a.duration = s.duration;
        a.once = n.loop_count >= 1;
        a.done = done;
        a.weight = weight;
        act.push_back(a);
    }
    int32_t sample(const SourceDef&, float) { return -1; }
    int32_t blend(int32_t, int32_t, float, const EvalNode*, const std::vector<std::string>&) { return -1; }
};

// complete(): some once source and every once source done; ecComplete(ec): also some source of `ec`
struct EcSink {
    static const bool kActive = false;
    int32_t ec = 0;
    bool has = false, any_once = false, all_done = true;
    void source(const SourceDef& s, const EvalNode& n, float, float, bool, bool done, float) {
        if (s.ec == ec) has = true;
        if (n.loop_count >= 1) {
            any_once = true;
            if (!done) all_done = false;
        }
    }
    int32_t sample(const SourceDef&, float) { return -1; }
    int32_t blend(int32_t, int32_t, float, const EvalNode*, const std::vector<std::string>&) { return -1; }
};

// 4468: whether build() can fail from node i at `depth` for some options and time. It follows build()'s pose edges
// (every input a selector or a weight could choose) and its failures: a node out of range, the depth limit, a
// Selection whose default edge is out of range (a selector value may be out of range too), a node that is not a pose
// node, a source node without a bound source. deepest[i]: the greatest depth node i was walked from without a
// failure (walking it again from no deeper cannot fail either). 0470: over the shape's nodes (the same edges: 0441's
// named inputs follow input()'s rule).
bool mayFail(const Shape& sh, int32_t i, int depth, std::vector<int>& deepest) {
    if (i < 0 || static_cast<size_t>(i) >= sh.nodes.size() || depth > 64) return true;
    if (deepest[static_cast<size_t>(i)] >= depth) return false;
    const EvalNode& n = sh.nodes[static_cast<size_t>(i)];
    bool fails = false;
    switch (n.type) {
        case DagNodeType::Subgraph:
            fails = mayFail(sh, n.e.pose_out, depth + 1, deepest);
            break;
        case DagNodeType::PoseAnimSource:
            fails = n.src < 0;
            break;
        case DagNodeType::Selection: {
            const std::vector<int32_t>& poses = n.e.pose_in;
            fails = n.default_edge < 0 || static_cast<size_t>(n.default_edge) >= poses.size();
            for (size_t k = 0; k < poses.size() && !fails; ++k) fails = mayFail(sh, poses[k], depth + 1, deepest);
            break;
        }
        case DagNodeType::Blend: {
            const int32_t fromNode = n.e.from, toNode = n.e.to;
            if (toNode < 0 && fromNode >= 0) fails = mayFail(sh, fromNode, depth + 1, deepest);   // 0439 (build)
            else if (fromNode < 0 && toNode >= 0) fails = mayFail(sh, toNode, depth + 1, deepest);
            else fails = mayFail(sh, fromNode, depth + 1, deepest) || mayFail(sh, toNode, depth + 1, deepest);
            break;
        }
        default:
            fails = true;
            break;
    }
    if (!fails) deepest[static_cast<size_t>(i)] = depth;
    return fails;
}

// 0470: the shape's bytes as requested (GraphFootprint): kGraph the evaluated nodes, the track masks and the name;
// kSources the bound sources.
void shapeBytes(const Shape& s, size_t& graph, size_t& sources) {
    graph = sizeof(Shape) + heapOf(s.name) + heapOf(s.nodes) + heapOf(s.track_masks);
    for (const EvalNode& n : s.nodes) graph += heapOf(n.blocks) + heapOf(n.e.pose_in);
    for (const std::string& t : s.track_masks) graph += heapOf(t);
    sources = heapOf(s.sources);
    for (const SourceDef& d : s.sources) sources += heapOf(d.clip);
}

// 0470: the shape as bytes, every field the walks read (floats by their bits), for GraphShare's exact comparison.
struct KeyWriter {
    std::string& out;
    template <class T> void pod(const T& v) { out.append(reinterpret_cast<const char*>(&v), sizeof v); }
    void str(const std::string& s) {
        pod(static_cast<uint64_t>(s.size()));
        out.append(s);
    }
};
void shapeKey(const Shape& s, std::string& out) {
    out.clear();
    KeyWriter w{out};
    w.str(s.name);
    w.pod(s.root);
    w.pod(static_cast<uint64_t>(s.nodes.size()));
    for (const EvalNode& n : s.nodes) {
        w.pod(static_cast<int32_t>(n.type));
        w.pod(n.loop_count);
        w.pod(n.speed);
        w.pod(n.offset);
        w.pod(static_cast<uint8_t>(n.continue_offset ? 1 : 0));
        w.pod(n.default_edge);
        w.pod(n.scale);
        w.pod(n.callback_hint);
        w.pod(n.callback_value);
        w.pod(n.scalar);
        w.pod(n.binding_index);
        w.pod(n.default_weight);
        w.pod(n.target_weight);
        w.pod(static_cast<uint64_t>(n.blocks.size()));
        for (const c5m::DagBlock& b : n.blocks) {
            w.pod(b.type);
            w.pod(static_cast<uint8_t>((b.flip_x ? 1 : 0) | (b.flip_y ? 2 : 0)));
            w.pod(b.duration);
        }
        const NodeEdges& e = n.e;
        const int32_t named[8] = {e.pose_out, e.offset, e.selector, e.param, e.from, e.to, e.mask, e.input};
        w.pod(named);
        w.pod(static_cast<uint64_t>(e.pose_in.size()));
        for (int32_t p : e.pose_in) w.pod(p);
        w.pod(n.src);
    }
    w.pod(static_cast<uint64_t>(s.track_masks.size()));
    for (const std::string& t : s.track_masks) w.str(t);
    w.pod(static_cast<uint64_t>(s.sources.size()));
    for (const SourceDef& d : s.sources) {
        w.pod(d.node);
        w.pod(d.ec);
        w.str(d.clip);
        w.pod(d.duration);
    }
}

uint64_t fnv64(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

class Graph : public GraphInstance {
public:
    Graph(std::shared_ptr<const Shape> shape, GraphOptions opt = GraphOptions())
        : s_(std::move(shape)), start_(s_->sources.size(), 0.0f), opt_(opt) {}
    bool programMayFail() const override { return s_->may_fail; }
    void restart() override {
        elapsed_ = 0;
        prev_elapsed_ = 0;
        std::fill(start_.begin(), start_.end(), 0.0f);
    }
    void restartFrom(const std::vector<ActiveSource>& previous) override {
        restart();
        for (size_t k = 0; k < s_->sources.size(); ++k) {
            const SourceDef& s = s_->sources[k];
            const EvalNode& n = s_->nodes[static_cast<size_t>(s.node)];
            if (!n.continue_offset) continue;
            for (const ActiveSource& a : previous)
                if (a.ec == s.ec) {
                    start_[k] = a.t;
                    break;
                }
        }
    }
    void advance(float dt, const PathOptions& opts) override {
        (void)opts;
        prev_elapsed_ = elapsed_;
        if (dt > 0 && std::isfinite(dt)) elapsed_ += dt;
    }
    AnimStatus program(const PathOptions& opts, PoseProgram& out) const override {
        out.clear();
        ProgramSink sink{out};
        int32_t last = -1;
        AnimStatus st = walk(s_->root, opts, 1.0f, sink, last, 0, false);
        if (!st.ok()) out.clear();
        return st;
    }
    bool complete(const PathOptions& opts) const override {
        EcSink sink;
        int32_t last = -1;
        walk(s_->root, opts, 1.0f, sink, last, 0, false);
        return sink.any_once && sink.all_done;
    }
    // 0441: one walk, no ActiveSource copied (the default's active() + complete() is two walks)
    bool ecComplete(int32_t ec, const PathOptions& opts) const override {
        EcSink sink;
        sink.ec = ec;
        int32_t last = -1;
        walk(s_->root, opts, 1.0f, sink, last, 0, false);
        return sink.has && sink.any_once && sink.all_done;
    }
    void active(const PathOptions& opts, std::vector<ActiveSource>& out) const override {
        out.clear();
        ActiveSink sink{out};
        int32_t last = -1;
        walk(s_->root, opts, 1.0f, sink, last, 0, false);
    }
    void activeTimes(const PathOptions& opts, std::vector<ActiveSource>& out) const override {
        out.clear();
        ActiveSink sink{out, false};
        int32_t last = -1;
        walk(s_->root, opts, 1.0f, sink, last, 0, false);
    }
    float stateTime() const override { return elapsed_; }
    // 0470: own = this object and its start times; shared = the shape (once per shape in a census)
    void footprint(GraphFootprint& out) const override {
        out = GraphFootprint();
        out.own[GraphFootprint::kOther] = sizeof(*this);
        out.own[GraphFootprint::kSources] = heapOf(start_);
        shapeBytes(*s_, out.shared[GraphFootprint::kGraph], out.shared[GraphFootprint::kSources]);
        out.shared_id = s_.get();
    }

private:
    // Clip time of source k at `elapsed` (offset added), wrapped or clamped by LoopCount; `done` for held sources.
    float clipTime(size_t k, const EvalNode& n, float elapsed, float offset, bool& done, long& cycle) const {
        const SourceDef& s = s_->sources[k];
        const float raw = start_[k] + n.speed * elapsed + offset;
        done = false;
        if (!(s.duration > 0)) { cycle = 0; return 0; }
        const float c = std::floor(raw / s.duration);
        cycle = static_cast<long>(c);
        if (n.loop_count >= 1 && raw >= s.duration * static_cast<float>(n.loop_count)) {
            done = true;
            return s.duration;
        }
        float t = raw - c * s.duration;
        if (t < 0) t = 0;
        if (t >= s.duration) t = 0;
        return raw < 0 ? 0.0f : t;
    }
    float scalar(int32_t i, const PathOptions& opts, int depth) const {
        if (i < 0 || depth > 64) return 0;
        const EvalNode& n = s_->nodes[static_cast<size_t>(i)];
        switch (n.type) {
            case DagNodeType::ScalarCallback: {
                if (n.callback_hint == 7) return opts.v[static_cast<int>(n.callback_value)];
                // hint 5: the fade weight over CallbackValue seconds
                if (!(n.callback_value > 0)) return 1;
                const float w = elapsed_ / n.callback_value;
                return w < 0 ? 0 : (w > 1 ? 1 : w);
            }
            case DagNodeType::ScalarSource: return n.scalar;
            case DagNodeType::BlockCurve:
                return evalBlockCurve(n.blocks, n.scale, scalar(n.e.input, opts, depth + 1));
            default: return 0;
        }
    }
    // 0782: whether scalar node i is a fade weight (a hint-5 callback, or a BlockCurve of one): the only Blend weight
    // under which hold_from_continue holds the From side. A path weight (hint 7: mounted_running_loop.dge's turn
    // amount, path 7) is not a fade: its From side (the run) runs (unit-server-scene.md 19.12).
    bool fadeWeight(int32_t i, int depth) const {
        if (i < 0 || depth > 64) return false;
        const EvalNode& n = s_->nodes[static_cast<size_t>(i)];
        if (n.type == DagNodeType::ScalarCallback) return n.callback_hint == 5;
        if (n.type == DagNodeType::BlockCurve) return fadeWeight(n.e.input, depth + 1);
        return false;
    }
    // build() of 4409 as a walk over a sink (0441): the same nodes in the same order, the same failures and texts, the
    // same float operations; what each node gives is the sink's.
    template <class Sink>
    AnimStatus walk(int32_t i, const PathOptions& opts, float weight, Sink& sink, int32_t& last, int depth,
                    bool from) const {
        const Shape& sh = *s_;
        if (i < 0 || static_cast<size_t>(i) >= sh.nodes.size() || depth > 64)
            return AnimStatus::Fail(AnimCode::BadLogicFile, sh.name + ": pose edge to node " + std::to_string(i));
        const EvalNode& n = sh.nodes[static_cast<size_t>(i)];
        const NodeEdges& ed = n.e;
        switch (n.type) {
            case DagNodeType::Subgraph:
                return walk(ed.pose_out, opts, weight, sink, last, depth + 1, from);
            case DagNodeType::PoseAnimSource: {
                // every PoseAnimSource of a bound graph has its source (bindGraph fails EcNotBound otherwise); before
                // 0470 a missing one threw (std::map::at), unreachable as now
                if (n.src < 0)
                    return AnimStatus::Fail(AnimCode::BadLogicFile, sh.name + ": node " + std::to_string(i) +
                                                                        ": no bound source");
                const size_t k = static_cast<size_t>(n.src);
                const SourceDef& s = sh.sources[k];
                const float off = ed.offset >= 0 ? scalar(ed.offset, opts, depth + 1) : n.offset;
                bool done = false, pdone = false;
                long cyc = 0, pcyc = 0;
                // 4406 r2: a held From-side ContinueOffset source stays at its start (GraphOptions)
                const bool hold = opt_.hold_from_continue && from && n.continue_offset;
                const float t = clipTime(k, n, hold ? 0.0f : elapsed_, off, done, cyc);
                float prev_t = 0;
                if (Sink::kActive) prev_t = clipTime(k, n, hold ? 0.0f : prev_elapsed_, off, pdone, pcyc);
                sink.source(s, n, t, prev_t, n.loop_count == 0 && cyc != pcyc, done, weight);
                last = sink.sample(s, t);
                return AnimStatus::Ok();
            }
            case DagNodeType::Selection: {
                const float sv = scalar(ed.selector, opts, depth + 1);
                const std::vector<int32_t>& poses = ed.pose_in;
                long idx = std::lround(sv);
                if (idx < 0 || static_cast<size_t>(idx) >= poses.size()) idx = n.default_edge;
                if (idx < 0 || static_cast<size_t>(idx) >= poses.size())
                    return AnimStatus::Fail(AnimCode::BadLogicFile, sh.name + ": node " + std::to_string(i) +
                                                                        ": selector and default edge out of range");
                return walk(poses[static_cast<size_t>(idx)], opts, weight, sink, last, depth + 1, from);
            }
            case DagNodeType::Blend: {
                // 0439: a pose input with no edge (VP's edited_aim_ms_idle_graph.dge: the masked aim blend's To) gives
                // no pose: the blend passes its connected input whole (13.2 [I]: what the EXE's blend does with an
                // unconnected input is not read; the EXE draws those ships)
                const int32_t fromNode = ed.from, toNode = ed.to;
                if (toNode < 0 && fromNode >= 0) return walk(fromNode, opts, weight, sink, last, depth + 1, true);
                if (fromNode < 0 && toNode >= 0) return walk(toNode, opts, weight, sink, last, depth + 1, from);
                float w = scalar(ed.param, opts, depth + 1);
                w = std::isfinite(w) ? (w < 0 ? 0 : (w > 1 ? 1 : w)) : 0.0f;
                const bool masked = ed.mask >= 0;
                // 0782: the From side is a held side only under a fade weight (was: under any unmasked Blend)
                const bool fromHeld = from || (!masked && fadeWeight(ed.param, depth + 1));
                if (!masked && w <= 0) return walk(fromNode, opts, weight, sink, last, depth + 1, fromHeld);
                if (!masked && w >= 1) return walk(toNode, opts, weight, sink, last, depth + 1, from);
                int32_t a = -1, b = -1;
                AnimStatus st = walk(fromNode, opts, weight * (masked ? 1.0f : 1.0f - w), sink, a, depth + 1,
                                     fromHeld);
                if (!st.ok()) return st;
                st = walk(toNode, opts, weight * w, sink, b, depth + 1, from);
                if (!st.ok()) return st;
                last = sink.blend(a, b, w, masked ? &sh.nodes[static_cast<size_t>(ed.mask)] : nullptr, sh.track_masks);
                return AnimStatus::Ok();
            }
            default:
                return AnimStatus::Fail(AnimCode::BadLogicFile, sh.name + ": node " + std::to_string(i) +
                                                                    " is not a pose node");
        }
    }

    std::shared_ptr<const Shape> s_;   // 0470: shared, immutable
    std::vector<float> start_;         // per source of s_: its start (restart, restartFrom)
    float elapsed_ = 0, prev_elapsed_ = 0;
    GraphOptions opt_;
};

// 0470: the shape of a validated graph bound to a member's sources (bindGraph's step 5).
std::shared_ptr<Shape> makeShape(const c5m::DagGraph& g, std::vector<SourceDef> sources, const std::string& file_name) {
    std::shared_ptr<Shape> sh = std::make_shared<Shape>();
    sh->name = file_name;
    sh->root = g.root;
    sh->track_masks = g.track_masks;
    sh->sources = std::move(sources);
    sh->nodes.resize(g.nodes.size());
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        const DagNode& d = g.nodes[i];
        EvalNode& n = sh->nodes[i];
        n.type = d.type;
        n.loop_count = d.loop_count;
        n.speed = d.speed;
        n.offset = d.offset;
        n.continue_offset = d.continue_offset;
        n.default_edge = d.default_edge;
        n.scale = d.scale;
        n.callback_hint = d.callback_hint;
        n.callback_value = d.callback_value;
        n.scalar = d.scalar;
        n.binding_index = d.binding_index;
        n.default_weight = d.default_weight;
        n.target_weight = d.target_weight;
        n.blocks = d.blocks;
        NodeEdges& e = n.e;
        e.pose_out = edgeNode(d, "Pose out");
        e.offset = edgeNode(d, "Offset");
        e.selector = edgeNode(d, "Selector");
        e.param = edgeNode(d, "Param");
        e.from = edgeNode(d, "From");
        e.to = edgeNode(d, "To");
        e.mask = edgeNode(d, "Mask");
        e.input = edgeNode(d, "Input");
        if (d.type == DagNodeType::Selection)
            for (size_t k = 0;; ++k) {
                const c5m::DagEdge* pe = input(d, ("Pose in (" + std::to_string(k) + ")").c_str());
                if (!pe) break;
                e.pose_in.push_back(pe->input_node);
            }
    }
    // as 4409's std::map by node (the last source of a node; bindGraph makes one per PoseAnimSource node)
    for (size_t k = 0; k < sh->sources.size(); ++k) {
        const int32_t node = sh->sources[k].node;
        if (node >= 0 && static_cast<size_t>(node) < sh->nodes.size())
            sh->nodes[static_cast<size_t>(node)].src = static_cast<int32_t>(k);
    }
    std::vector<int> deepest(sh->nodes.size(), -1);
    sh->may_fail = mayFail(*sh, sh->root, 0, deepest);
    return sh;
}

std::string nodeText(const std::string& file, size_t i, const DagNode& n) {
    return file + ": node " + std::to_string(i) + " (" + n.name + ", " +
           (n.type_name.empty() ? std::string("NULL NodeData") : n.type_name) + ")";
}

AnimStatus bindShared(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                      const GraphOptions& opt, GraphShare* share, std::unique_ptr<GraphInstance>& out);

}  // namespace

// 0470: the shapes by the hash of their key; a hit is an exact comparison of the keys (shapeKey), so two shapes are
// shared only when every field the walks read is equal.
struct GraphShare::Impl {
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<const Shape>>> by_hash;
    size_t binds = 0, hits = 0, shapes = 0, bytes = 0;
    std::string key, other;   // reused buffers
    std::shared_ptr<const Shape> intern(std::shared_ptr<const Shape> s) {
        ++binds;
        shapeKey(*s, key);
        std::vector<std::shared_ptr<const Shape>>& bucket = by_hash[fnv64(key)];
        for (const std::shared_ptr<const Shape>& c : bucket) {
            shapeKey(*c, other);
            if (other == key) {
                ++hits;
                return c;
            }
        }
        bucket.push_back(s);
        ++shapes;
        size_t g = 0, src = 0;
        shapeBytes(*s, g, src);
        bytes += g + src;
        return s;
    }
};

GraphShare::GraphShare() : impl_(new Impl()) {}
GraphShare::~GraphShare() {}
size_t GraphShare::shapes() const { return impl_->shapes; }
size_t GraphShare::binds() const { return impl_->binds; }
size_t GraphShare::hits() const { return impl_->hits; }
size_t GraphShare::bytes() const { return impl_->bytes; }

AnimStatus bindGraph(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                     std::unique_ptr<GraphInstance>& out) {
    return bindShared(file, ecs, file_name, GraphOptions(), nullptr, out);
}

AnimStatus bindGraph(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                     const GraphOptions& opt, std::unique_ptr<GraphInstance>& out) {
    return bindShared(file, ecs, file_name, opt, nullptr, out);
}

AnimStatus bindGraph(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                     const GraphOptions& opt, GraphShare& share, std::unique_ptr<GraphInstance>& out) {
    return bindShared(file, ecs, file_name, opt, &share, out);
}

namespace {

AnimStatus bindShared(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                      const GraphOptions& opt, GraphShare* share, std::unique_ptr<GraphInstance>& out) {
    out.reset();
    if (file.dags.empty()) return AnimStatus::Fail(AnimCode::BadLogicFile, file_name + ": no DAG");
    const c5m::DagGraph& g = file.dags[0];
    const size_t n = g.nodes.size();
    // 1. node types and their own data
    for (size_t i = 0; i < n; ++i) {
        const DagNode& d = g.nodes[i];
        const bool known = d.has_node_data && (isScalarNode(d.type) || isPoseNode(d.type) ||
                                               d.type == DagNodeType::Trackmask);
        if (!known) return AnimStatus::Fail(AnimCode::UnknownNode, nodeText(file_name, i, d));
        if (d.type == DagNodeType::ScalarCallback) {
            if (d.callback_hint != 5 && d.callback_hint != 7)
                return AnimStatus::Fail(AnimCode::UnknownNode, nodeText(file_name, i, d) + ": callback hint " +
                                                                   std::to_string(d.callback_hint));
            if (d.callback_hint == 7 && (d.callback_value != std::floor(d.callback_value) || d.callback_value < 0 ||
                                         d.callback_value >= static_cast<float>(kPathOptions)))
                return AnimStatus::Fail(AnimCode::BadLogicFile, nodeText(file_name, i, d) +
                                                                    ": hint 7 value outside paths 0-8");
        }
        if (d.type == DagNodeType::Trackmask &&
            (d.binding_index < 0 || static_cast<size_t>(d.binding_index) >= g.track_masks.size()))
            return AnimStatus::Fail(AnimCode::BadLogicFile, nodeText(file_name, i, d) + ": track mask index " +
                                                                std::to_string(d.binding_index));
        if (d.type == DagNodeType::PoseAnimSource &&
            (d.source_index < 0 || static_cast<size_t>(d.source_index) >= g.source_ecs.size()))
            return AnimStatus::Fail(AnimCode::BadLogicFile, nodeText(file_name, i, d) + ": source index " +
                                                                std::to_string(d.source_index));
    }
    // 2. edges: in range and of the right kind
    for (size_t i = 0; i < n; ++i) {
        const DagNode& d = g.nodes[i];
        for (const c5m::DagEdge& e : d.inputs) {
            if (e.input_node < 0) continue;
            if (static_cast<size_t>(e.input_node) >= n)
                return AnimStatus::Fail(AnimCode::BadLogicFile, nodeText(file_name, i, d) + ": edge " + e.name +
                                                                    " to node " + std::to_string(e.input_node));
            const DagNodeType src = g.nodes[static_cast<size_t>(e.input_node)].type;
            bool ok;
            if (e.name == "Mask") ok = src == DagNodeType::Trackmask;
            else if (e.name == "From" || e.name == "To" || e.name == "Pose out" || e.name.compare(0, 7, "Pose in") == 0)
                ok = isPoseNode(src);
            else ok = isScalarNode(src);   // Selector, Param, Input, Offset, Speed, Input (k)
            if (!ok)
                return AnimStatus::Fail(AnimCode::BadLogicFile, nodeText(file_name, i, d) + ": edge " + e.name +
                                                                    " from a node of the wrong kind");
        }
    }
    if (g.root < 0 || static_cast<size_t>(g.root) >= n || g.nodes[static_cast<size_t>(g.root)].type != DagNodeType::Subgraph)
        return AnimStatus::Fail(AnimCode::BadLogicFile, file_name + ": root " + std::to_string(g.root) +
                                                            " is not a Subgraph");
    // 3. cycles and reachable sources (depth-first from the root over every input)
    std::vector<int> state(n, 0);   // 0 new, 1 on the stack, 2 done
    std::vector<size_t> reached;
    bool cycle = false;
    std::function<void(size_t)> visit = [&](size_t i) {
        if (state[i] == 2 || cycle) return;
        if (state[i] == 1) { cycle = true; return; }
        state[i] = 1;
        for (const c5m::DagEdge& e : g.nodes[i].inputs)
            if (e.input_node >= 0) visit(static_cast<size_t>(e.input_node));
        state[i] = 2;
        reached.push_back(i);
    };
    visit(static_cast<size_t>(g.root));
    if (cycle) return AnimStatus::Fail(AnimCode::BadLogicFile, file_name + ": the graph has a cycle");
    // 4. sources: every PoseAnimSource of the DAG must bind (static check); NoProgress if none is reachable
    std::vector<SourceDef> sources;
    std::string missing;
    for (size_t i = 0; i < n; ++i) {
        const DagNode& d = g.nodes[i];
        if (d.type != DagNodeType::PoseAnimSource) continue;
        SourceDef s;
        s.node = static_cast<int32_t>(i);
        s.ec = g.source_ecs[static_cast<size_t>(d.source_index)];
        const std::string* clip = ecs.find(s.ec);
        s.duration = ecs.duration(s.ec);
        if (!clip) {
            missing += (missing.empty() ? "" : ", ") + std::to_string(s.ec);
            continue;
        }
        if (!(s.duration > 0)) {
            missing += (missing.empty() ? "" : ", ") + std::to_string(s.ec) + " (" + *clip + ": no duration)";
            continue;
        }
        s.clip = *clip;
        sources.push_back(s);
    }
    if (!missing.empty())
        return AnimStatus::Fail(AnimCode::EcNotBound, file_name + ": source ec the member lacks: " + missing);
    bool progress = false;
    for (size_t i : reached)
        if (g.nodes[i].type == DagNodeType::PoseAnimSource) progress = true;
    if (!progress)
        return AnimStatus::Fail(AnimCode::NoProgress, file_name + ": no animation source reachable from the root");
    // 5. 0470: the shape (what the walks read), shared through `share` with every equal one bound before
    std::shared_ptr<const Shape> shape = makeShape(g, std::move(sources), file_name);
    if (share) shape = share->impl()->intern(std::move(shape));
    std::unique_ptr<Graph> gi(new Graph(std::move(shape), opt));
    gi->restart();
    out = std::move(gi);
    return AnimStatus::Ok();
}

}  // namespace

}}  // namespace c5r::unitanim
