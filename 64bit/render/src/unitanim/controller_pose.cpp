// render/src/unitanim/controller_pose.cpp - the controller's PoseSource and the member's ec map (epic 0374, issue
// 4409; c5r/unitanim/controller_pose.h, unit-controller.md 13.2). Own code.
#include "c5r/unitanim/controller_pose.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace c5r { namespace unitanim {

void ControllerPose::setProgram(int32_t player, int32_t id, uint32_t figure, const PoseProgram& program) {
    Entry& e = programs_[std::make_tuple(player, id, figure)];
    if (e.gen != gen_) ++count_;
    e.gen = gen_;
    e.program = program;   // 0441: assigned into the entry's storage (no new node, vectors and strings reused)
    e.rfile = nullptr;
}

void ControllerPose::clearPrograms() {
    // 0441: entries of figures not set in this frame stay for reuse while they are not more than the frame's own
    if (programs_.size() > 2 * count_ + 64)
        for (auto it = programs_.begin(); it != programs_.end();)
            it = it->second.gen == gen_ ? std::next(it) : programs_.erase(it);
    ++gen_;
    count_ = 0;
}

const PoseProgram* ControllerPose::program(int32_t player, int32_t id, uint32_t figure) const {
    auto it = programs_.find(std::make_tuple(player, id, figure));
    return it == programs_.end() || it->second.gen != gen_ ? nullptr : &it->second.program;
}

units::UnitStatus ControllerPose::rows(const units::PoseRequest& req, units::Float4 out[units::kPaletteBones * 3]) {
    for (uint32_t i = 0; i < units::kPaletteBones * 3; ++i) {
        out[i] = units::Float4();
        if (i % 3 == 0) out[i].x = 1;
        else if (i % 3 == 1) out[i].y = 1;
        else out[i].z = 1;
    }
    if (!req.unit)
        return units::UnitStatus::fail(units::UnitCode::BadInput, "controller pose request without a unit");
    auto it = programs_.find(std::make_tuple(req.unit->player, req.unit->id, req.figure));
    if (it == programs_.end() || it->second.gen != gen_)
        return units::UnitStatus::fail(units::UnitCode::BadInput,
                                       "no pose program for unit (" + std::to_string(req.unit->player) + ", " +
                                           std::to_string(req.unit->id) + ") figure " + std::to_string(req.figure));
    Entry& e = it->second;
    const PoseProgram* p = &e.program;
    // 4410: a mask named only by its binding takes its bones from the model (13.3); the program itself is unchanged
    bool unresolved = false;
    for (const TrackMaskWeights& m : p->masks)
        if (!m.binding.empty() && m.bones.empty()) unresolved = true;
    if (!unresolved) return module_.rowsProgram(req, *p, out);
    if (!req.model || !req.model->file || !req.mesh)
        return units::UnitStatus::fail(units::UnitCode::BadInput, "pose request without model file or mesh");
    const int32_t skel = meshSkeleton(*req.model->file, req.mesh->index);
    if (e.rfile != req.model->file.get() || e.rskel != skel) {   // 0441: resolved once per (file, skeleton) in the frame
        e.rfile = nullptr;
        e.resolved = *p;
        const AnimStatus ms = resolveTrackMasks(*req.model->file, skel, e.resolved);
        if (!ms.ok())
            return units::UnitStatus::fail(units::UnitCode::BadModel,
                                           std::string(name(ms.code)) + ": " + req.model->gr2 + " mesh " +
                                               req.mesh->name + ": " + ms.text);
        e.rfile = req.model->file.get();
        e.rskel = skel;
    }
    return module_.rowsProgram(req, e.resolved, out);
}

int32_t meshSkeleton(const c5m::FileInfo& model, uint32_t mesh) {
    if (mesh >= model.meshes.size() || model.meshes[mesh].bone_bindings.empty()) return -1;
    const c5m::Mesh& me = model.meshes[mesh];
    std::vector<int32_t> order;
    for (const c5m::ModelEntry& e : model.models)
        for (int32_t m : e.meshes)
            if (m == static_cast<int32_t>(mesh) && e.skeleton >= 0 &&
                static_cast<size_t>(e.skeleton) < model.skeletons.size())
                order.push_back(e.skeleton);
    for (size_t s = 0; s < model.skeletons.size(); ++s) order.push_back(static_cast<int32_t>(s));
    for (int32_t s : order) {
        bool all = true;
        for (const c5m::BoneBinding& bb : me.bone_bindings)
            if (model.skeletons[static_cast<size_t>(s)].find(bb.bone_name) < 0) {
                all = false;
                break;
            }
        if (all) return s;
    }
    return -1;
}

// 0617 (unit-controller.md 13.3, "What stock does with a mask"): stock takes the mask names from the first bone that
// carries GrannyTrackMasks, gives each bone its own value, else its parent's weight, else 0 (the root), and binds a name
// no bone carries as no mask: the blend then runs unmasked. Here: an unbound mask becomes binding "" with default weight
// 1 and no bones (pose.cpp's m = 1 for every bone, its unmasked blend); a bound one lists every bone of the skeleton.
bool trackMaskUnbound(const c5m::Skeleton& sk, const std::string& binding) {
    for (const c5m::Bone& b : sk.bones) {
        if (b.track_masks.empty()) continue;   // the first bone that carries mask data names the masks
        for (const auto& tm : b.track_masks)
            if (tm.first == binding) return false;
        return true;
    }
    return true;
}

AnimStatus resolveTrackMasks(const c5m::FileInfo& model, int32_t skeleton, PoseProgram& prog) {
    for (TrackMaskWeights& m : prog.masks) {
        if (m.binding.empty() || !m.bones.empty()) continue;
        if (skeleton < 0 || static_cast<size_t>(skeleton) >= model.skeletons.size())
            return AnimStatus::Fail(AnimCode::TrackMaskNotBound,
                                    "track mask " + m.binding + ": the mesh has no skeleton (" +
                                        std::to_string(skeleton) + ")");
        const c5m::Skeleton& sk = model.skeletons[static_cast<size_t>(skeleton)];
        if (trackMaskUnbound(sk, m.binding)) {   // 0617: stock's NULL mask, an unmasked blend
            m.binding.clear();
            m.default_weight = 1.0f;
            m.weights.clear();
            continue;
        }
        std::vector<std::string> bones;
        std::vector<float> weights(sk.bones.size(), 0.0f);
        for (size_t i = 0; i < sk.bones.size(); ++i) {
            const c5m::Bone& b = sk.bones[i];
            float w = 0;
            if (b.track_mask(m.binding, w)) {
                if (!std::isfinite(w))
                    return AnimStatus::Fail(AnimCode::TrackMaskNotBound, "track mask " + m.binding + ": bone " +
                                                                             b.name + " of skeleton " + sk.name +
                                                                             ": weight not finite");
                weights[i] = w;
            } else if (b.parent >= 0 && static_cast<size_t>(b.parent) < sk.bones.size()) {
                weights[i] = weights[static_cast<size_t>(b.parent)];   // 0617: the parent's weight, as stock
            }
            bones.push_back(b.name);
        }
        m.bones = std::move(bones);
        m.weights = std::move(weights);
    }
    return AnimStatus::Ok();
}

AnimStatus checkTrackMasks(const c5m::FileInfo& model, const std::vector<const c5m::DagFile*>& graphs,
                           const std::vector<std::string>& graph_names) {
    std::vector<int32_t> skels;
    for (uint32_t mi = 0; mi < model.meshes.size(); ++mi) {
        const int32_t s = meshSkeleton(model, mi);
        if (s >= 0 && std::find(skels.begin(), skels.end(), s) == skels.end()) skels.push_back(s);
    }
    for (size_t g = 0; g < graphs.size(); ++g) {
        if (!graphs[g]) continue;
        const std::string gname = g < graph_names.size() ? graph_names[g] : ("graph " + std::to_string(g));
        for (const c5m::DagGraph& dag : graphs[g]->dags)
            for (const std::string& binding : dag.track_masks) {
                if (binding.empty()) continue;
                if (skels.empty())
                    return AnimStatus::Fail(AnimCode::TrackMaskNotBound,
                                            gname + ": track mask " + binding + ": the model has no skinned mesh");
                for (int32_t s : skels) {
                    PoseProgram probe;
                    TrackMaskWeights tm;
                    tm.binding = binding;
                    probe.masks.push_back(tm);
                    const AnimStatus st = resolveTrackMasks(model, s, probe);
                    if (!st.ok()) return AnimStatus::Fail(st.code, gname + ": " + st.text);
                }
            }
    }
    return AnimStatus::Ok();
}

AnimStatus makeEcMap(units::ModulePose& module, const units::MemberArt& member, const units::UnitModel& model,
                     EcMap& out, std::string* notes) {
    out = EcMap();
    if (notes) notes->clear();
    for (const c5m::ResolvedAnimation& a : member.files.animations) {
        float d = 0;
        units::UnitStatus st = module.clipDuration(member, model, a.file.name, d);
        if (!st.ok()) {
            if (notes) *notes += (notes->empty() ? "" : "; ") + a.file.name + ": " + st.text;
            continue;
        }
        for (int ec : a.ec) {
            if (out.find(ec)) continue;
            out.ecs.push_back(ec);
            out.clips.push_back(a.file.name);
            out.durations.push_back(d);
        }
    }
    return AnimStatus::Ok();
}

}}  // namespace c5r::unitanim
