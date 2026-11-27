// c5r/unitanim/controller_pose.h - the controller's PoseSource (epic 0374, issue 4409; unit-controller.md 4, 13.2).
// The controller sets one pose program per figure for the frame; the module asks rows() per figure and mesh, and
// ControllerPose answers through ModulePose::rowsProgram (pose.h). units.h and PoseRequest are unchanged: the program
// is looked up by (req.unit->player, req.unit->id, req.figure).
//
// A figure without a program is refused (BadInput, named): no default clip is substituted (D118, MC condition (4)).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "c5r/unitanim/graph.h"
#include "c5r/unitanim/pose_program.h"
#include "c5r/units/pose.h"

namespace c5r { namespace unitanim {

class ControllerPose : public units::PoseSource {
public:
    explicit ControllerPose(units::ModulePose& module) : module_(module) {}
    // 0441: a frame's programs are kept in entries that live across frames (clearPrograms starts a new frame; an entry
    // not set in the frame is not found), so that setting a program reuses its entry's storage; entries of figures not
    // set in the last frame are dropped when they outnumber the frame's. A program's resolved track masks (rows()) are
    // kept with it for the frame, per (model file, skeleton): one resolve per figure, not per mesh.
    void setProgram(int32_t player, int32_t id, uint32_t figure, const PoseProgram& program);
    void clearPrograms();
    const PoseProgram* program(int32_t player, int32_t id, uint32_t figure) const;
    size_t programCount() const { return count_; }
    units::UnitStatus rows(const units::PoseRequest& req, units::Float4 out[units::kPaletteBones * 3]) override;
    units::UnitStatus checkClips(const units::MemberArt& member, const units::UnitModel& model) override {
        return module_.bindAll(member, model);
    }

private:
    units::ModulePose& module_;
    struct Entry {
        PoseProgram program;
        uint64_t gen = 0;                          // the frame it was set in
        const c5m::FileInfo* rfile = nullptr;      // `resolved` is the program with its masks resolved for (rfile,
        int32_t rskel = -1;                        // rskel); rfile null: none this frame
        PoseProgram resolved;
    };
    std::map<std::tuple<int32_t, int32_t, uint32_t>, Entry> programs_;
    uint64_t gen_ = 1;
    size_t count_ = 0;
};

// The member's ec map for bindGraph: every animation of its .fxsxml by each of its ec codes (the first animation
// wins for an ec named twice), with the clip's duration from the module's clip cache. A clip that does not bind
// (ModelNotFound, BadModel, CurveUnsupported) is left out of the map with its reason in `notes`, so a graph that
// uses its ec fails with EcNotBound naming it.
AnimStatus makeEcMap(units::ModulePose& module, const units::MemberArt& member, const units::UnitModel& model,
                     EcMap& out, std::string* notes = nullptr);

// 4410 (additive): track masks (unit-controller.md 13.3). A .dge Trackmask names its mask only by a binding name
// (TrackMaskAIM_LAYER); the weights are in the model, one per bone, as the member of that name in the bone's
// ExtendedData.GrannyTrackMasks struct (c5m::Bone::track_masks) [C for the data].
//
// The skeleton the pose step uses for mesh `mesh` of `model` (pose.cpp's rule: the skeleton of the model entry that
// binds the mesh, else the first skeleton that has every bone the mesh binds); -1 for a mesh without bindings.
int32_t meshSkeleton(const c5m::FileInfo& model, uint32_t mesh);
// 0617: true when stock binds `binding` as no mask on this skeleton: the first bone that carries mask data does not name
// it (stock takes the mask names from that bone), or no bone carries mask data at all (VP's four edited modern ships).
bool trackMaskUnbound(const c5m::Skeleton& sk, const std::string& binding);
// Fills every mask of `prog` that has a binding and no bones from skeleton `skeleton`, as stock builds its masks (0617,
// 13.3): bones = every bone of the skeleton in order, weight = the bone's own value, else its parent's weight, else 0
// (the root); the Trackmask node's DefaultWeight is not used. An unbound binding (trackMaskUnbound) becomes no mask:
// binding "", default weight 1, no bones, which pose.cpp blends as the unmasked blend stock then runs. Before 0617 an
// unbound binding was TrackMaskNotBound (the unit not drawn) and bones without the binding took DefaultWeight 1.
// TrackMaskNotBound stays for a weight that is not finite and an invalid skeleton index. A program without
// binding-only masks is left unchanged.
AnimStatus resolveTrackMasks(const c5m::FileInfo& model, int32_t skeleton, PoseProgram& prog);
// The pre-check's form: every TrackMasks binding of the graphs against every skeleton the model's meshes use.
// TrackMaskNotBound names the graph, binding and skeleton. Graph pointers may be null (states without a graph).
AnimStatus checkTrackMasks(const c5m::FileInfo& model, const std::vector<const c5m::DagFile*>& graphs,
                           const std::vector<std::string>& graph_names);

}}  // namespace c5r::unitanim
