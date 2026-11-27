// c5r/unitanim/graph.h - one blend graph (.dge) bound to one member and running in one state of one figure (epic 0374;
// unit-controller.md 4-5). Interface by the lead at C1 so that the runtime (4403) and the evaluator (4409) code
// against one header; 4409 owns it from now on and may add members (the runtime's calls below stay).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "c5models/logic.h"
#include "c5r/unitanim/pose_program.h"
#include "c5r/unitanim/status.h"

namespace c5r { namespace unitanim {

// The path options (unit-controller.md 6.2-6.3, 4408): the machines write and test 0-6 (integers; path 4 = the
// requested action, a level); the graphs also read 7 (a continuous blend weight) and 8 (a left / centre / right
// selector), which only the EXE writes (turn and aim; the input's channel for them is pending the MC, #2016).
// Floats so that path 7 fits; integer values are exact.
constexpr int kPathOptions = 9;
struct PathOptions {
    float v[kPathOptions] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
};

// The member's ec -> animation name map (from FxsAsset::animations; one ec may name one animation).
struct EcMap {
    std::vector<int32_t> ecs;
    std::vector<std::string> clips;
    const std::string* find(int32_t ec) const;      // nullptr if the member lacks it
    // 4409 (additive): the clip's duration in seconds per entry (same length as clips; controller_pose.h makeEcMap
    // fills it from the clip cache). bindGraph needs it to wrap and clamp the sources: a source whose clip has no
    // duration > 0 is EcNotBound.
    std::vector<float> durations;
    float duration(int32_t ec) const;               // 0 if the member lacks it or none is known
};

// A sampled source that is playing (for the triggers and the oracle).
struct ActiveSource {
    int32_t ec = 0;
    std::string clip;
    float t = 0, prev_t = 0;      // clip time now and before this step (a trigger fires when its time is crossed)
    bool looped = false;          // the source wrapped during this step
    // 4409 (additive):
    float duration = 0;           // the clip's duration
    bool once = false;            // LoopCount >= 1: plays LoopCount times, then holds its last pose
    bool done = false;            // once and at its end (what ECComplete of this ec tests)
    float weight = 1;             // the source's share of the figure's pose (product of the blend weights on its path)
};

// 0470 (additive; unit-controller.md 19.7): the memory a bound graph holds, for the census (Controller::graphCensus).
// Bytes are the objects and their heap blocks as requested (vector capacity, string capacity beyond the small-string
// buffer); the heap's own overhead per block is not counted. `own` is this instance's alone; `shared` is data other
// instances may hold too, counted once per `shared_id` by a census. The parts: kGraph the graph's nodes and edges (the
// DagGraph copy, or the evaluator's own node table), kTables 0441's per-node tables (named inputs, source indexes, node
// pointers; since 0470 part of the node table, kGraph), kSources the bound sources (ec, clip, duration, start), kOther
// the rest (the object, the name, maps).
struct GraphFootprint {
    enum Part { kGraph = 0, kTables, kSources, kOther, kParts };
    size_t own[kParts] = {0, 0, 0, 0};
    size_t shared[kParts] = {0, 0, 0, 0};
    const void* shared_id = nullptr;
};

class GraphInstance {
public:
    virtual ~GraphInstance() = default;
    // Entry into a state: every source at its start (offset), loop counters reset.
    virtual void restart() = 0;
    // One step of clip time (the clock rule already applied by the caller), with the machine's options.
    virtual void advance(float dt, const PathOptions& opts) = 0;
    // The figure's pose now, flattened (Selection resolved by opts, BlockCurve / ScalarCallback reduced to w).
    virtual AnimStatus program(const PathOptions& opts, PoseProgram& out) const = 0;
    // ECComplete: every non-looping source on the selected path has reached its end.
    virtual bool complete(const PathOptions& opts) const = 0;
    // Sources on the selected path with their times (for triggers).
    virtual void active(const PathOptions& opts, std::vector<ActiveSource>& out) const = 0;
    // Time in the state since restart().
    virtual float stateTime() const = 0;
    // 4409 (additive): entry into a state taking over the clock of the previous state's sources: a source with
    // ContinueOffset 1 whose ec is in `previous` starts at that entry's clip time (unit-controller.md 6.3); every other
    // source starts as restart() starts it. Default: restart().
    virtual void restartFrom(const std::vector<ActiveSource>& previous) {
        (void)previous;
        restart();
    }
    // 4468 (additive): whether program() can return a failure for some options and time. The controller builds a
    // program for a unit it does not draw only where this is true (controller.h apply with a drawn set), so that a
    // failure is found on the same frame either way. Default true (the safe answer); bindGraph's graphs answer from
    // their structure: false when no walk can reach a missing or bad pose edge, a Selection without a usable default,
    // a node that is not a pose node, or the depth limit.
    virtual bool programMayFail() const { return true; }
    // 0441 (additive; unit-controller.md 19): the ECComplete test of the machine for this instance: some active source
    // of `ec` and complete(opts). Default: active() then complete(), as the controller asked it before; bindGraph's
    // graphs answer from one walk with no copy.
    // 0441 (additive): active() without the clip names (`clip` left empty), for the trigger walk. Default: active().
    virtual void activeTimes(const PathOptions& opts, std::vector<ActiveSource>& out) const { active(opts, out); }
    virtual bool ecComplete(int32_t ec, const PathOptions& opts) const {
        std::vector<ActiveSource> act;
        active(opts, act);
        bool has = false;
        for (const ActiveSource& a : act) has = has || a.ec == ec;
        return has && complete(opts);
    }
    // 0470 (additive): this instance's memory (GraphFootprint). Default: nothing known (every part 0).
    virtual void footprint(GraphFootprint& out) const { out = GraphFootprint(); }
};

// 4409 (additive): the BlockCurve as read (unit-controller.md 6.3, 13.2). Blocks are laid end to end over the input
// axis by their Duration (zero-duration blocks cover and change nothing); type 3 is an ease from 0 to 1 over its block (FlipX:
// from 1 to 0; FlipY: 1 minus it), type 0 holds the value the curve had at its start; before the first block the curve
// has the value of the first covering block's start, past the last its end. The output is multiplied by Scale.
// The shape of type 3's ease is not in the files: blockEase3 is the named reading (smoothstep, 3u^2 - 2u^3) [I],
// to be scored by the live fades (7.3, 4405), not tuned to one point.
float blockEase3(float u);
float evalBlockCurve(const std::vector<c5m::DagBlock>& blocks, float scale, float x);

// Binds dags[0] of a .dge to a member. Statuses: UnknownNode, EcNotBound, NoProgress, BadLogicFile.
// 4409: the checks are static, over every node of the DAG (not only the path the options select), so the pre-check
// finds them without a timeline:
//   UnknownNode   a node type the evaluator does not implement (Unknown, Layer, ScalarSelect, Callback, ...), a NULL
//                 NodeData, or a ScalarCallback hint other than 5 and 7 (6 is in no reached graph);
//   EcNotBound    a source ec the member lacks (every missing ec named), or one whose clip has no duration;
//   NoProgress    no PoseAnimSource reachable from the root;
//   BadLogicFile  no DAG, the root not a Subgraph, an edge to a node out of range or of the wrong kind (a pose where a
//                 scalar is read, ...), a cycle, a source index outside Sources, a hint-7 value outside paths 0-8, a
//                 track mask index outside TrackMasks.
AnimStatus bindGraph(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                     std::unique_ptr<GraphInstance>& out);

// 4406 round 2 (additive; premise change 4, MC #2117): readings of the evaluator held as options.
// hold_from_continue: a ContinueOffset 1 source reached through a Blend's From input (unmasked; through Selection and
// Subgraph) is held at its start, the previous state's clip time, for the fade instead of running at Speed
// (unit-controller.md 15.8: [L] one point, P01 4681; [I] in general; to be confirmed or rejected on 4405 B's
// transitions). Default true (MC #2117); false is the graphs as read (13.2), kept as a comparison line. The
// three-argument bindGraph uses these defaults. 0782 (unit-server-scene.md 19.12): only a Blend whose Param is a fade
// weight (a hint-5 callback, or a BlockCurve of one) holds its From side; under a path weight (hint 7, e.g.
// mounted_running_loop.dge's turn amount on path 7) the From side runs (was: every unmasked Blend held it, so a mounted
// unit's run stood still at its entry time through the whole walk).
struct GraphOptions {
    bool hold_from_continue = true;
};
AnimStatus bindGraph(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                     const GraphOptions& opt, std::unique_ptr<GraphInstance>& out);

// 0470 (additive; unit-controller.md 19.7): the immutable part of bound graphs, shared. A bound graph keeps what its
// walks read of the graph file (its nodes' types, numbers, curves and named inputs, the track masks) and the member's
// sources (ec, clip, duration) in one immutable shape; an instance holds only its clock and its sources' start times.
// bindGraph with a GraphShare gives every instance whose shape is equal, field for field, to one bound before through
// the same GraphShare that shape (an exact comparison, not the file's address: a file replaced or freed is never
// confused with another). Statuses, programs and every answer are those of the bindGraph without it. The shapes live
// as long as the GraphShare or an instance that holds one. One thread at a time binds through a GraphShare (the
// instances themselves are as before: one owner each).
class GraphShare {
public:
    GraphShare();
    ~GraphShare();
    GraphShare(const GraphShare&) = delete;
    GraphShare& operator=(const GraphShare&) = delete;
    size_t shapes() const;   // distinct shapes held
    size_t binds() const;    // successful binds through it
    size_t hits() const;     // of them, given a shape bound before
    size_t bytes() const;    // the shapes' bytes (GraphFootprint's rule)
    struct Impl;
    Impl* impl() { return impl_.get(); }

private:
    std::unique_ptr<Impl> impl_;
};
AnimStatus bindGraph(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                     const GraphOptions& opt, GraphShare& share, std::unique_ptr<GraphInstance>& out);

}}  // namespace c5r::unitanim
