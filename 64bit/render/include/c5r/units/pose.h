// c5r/units/pose.h - L2 of the unit module: a figure's skinning palette rows from (member model, clip name, t, scale,
// yaw, T) through models' sampler (c5models/anim.h), with a clip cache (epic 0041, issue 4353; design in
// research/05-graphics-composition/unit-renderer.md 6.3, results in its section 11).
//
// The module uses it through units.h's PoseSource: ModulePose below is the module's own pose step.
//   ModulePose pose(files);                        // once per renderer (files: the install, DLC and active mods)
//   UnitStatus s = pose.rows(req, out);            // per figure and mesh: the block's 96 bone rows
// The pieces are usable alone (the oracle, tests):
//   PoseModel pm;   buildPoseModel(modelFileInfo, animations, key, pm);                once per member model
//   ClipCache cache(clipLoaderFromProvider(files, strings));
//   poseFigure(cache, pm, clip, t, loop, scale, yaw_rad, T, meshes, rows);           per figure
//   rows[i].rows[k] = the 3 x 4 palette rows of mesh rows[i].mesh's k-th BoneBinding, [Rz(yaw) R_k | s Rz(yaw) t_k + T]
//   (unit-playback.md 1.1): the block's rows 2 + 3k .. 4 + 3k.
//
// What the layer adds to the sampler (each was a work-around in 0327's tools):
//   - every track group of a clip is bound, the first track of a bone name wins (VP's hakkapelitta clips hold a
//     one-track group before the skeleton's; unit-formations.md 12.2); tracks naming no bone are not decoded;
//   - each mesh is posed with the skeleton of the model entry that binds it (not the file's first skeleton);
//   - files written in another basis (VP's FBX-exported models: y up, back +z) are brought to the stock unit basis
//     (right +x, up +z, back -y), axes only, no unit scale: X' = M X M^-1 for rest transforms, inverse world
//     transforms and clip tracks (unit-formations.md 12.2). The mesh's vertices must take the same M (v' = M v) for
//     the rows to fit them (premise P4, 4352's repack: for y up / back +z, M takes (x, y, z) to (x, -z, y)).
// Not thread-safe: one ModulePose / ClipCache per thread, or a lock around it. Nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "c5models/anim.h"
#include "c5models/art.h"
#include "c5r/units/units.h"
#include "c5r/unitanim/pose_program.h"

namespace c5r { namespace units {

enum class PoseErr {
    Ok = 0,
    ClipNotFound,       // the member's .fxsxml names no animation by that name, file or ec code
    CurveUnsupported,   // a curve format (or degree) the sampler does not decode, on a track of a skeleton bone
    ModelNotFound,      // a clip file the .fxsxml names is in neither install nor mods
    BadModel,           // the reader refused a file, a mesh binds a bone no skeleton has, a parent after its child,
                        // a curve whose data is broken, a basis that is not invertible, more than 32 bound bones
    BadInput,           // a non-finite t, scale, yaw or T, a mesh index out of range, no model in the request
};
const char* poseErrName(PoseErr e);
UnitCode poseErrUnitCode(PoseErr e);   // the UnitStatus code of the same name (BadInput -> BadInput)

struct PoseStatus {
    PoseErr code = PoseErr::Ok;
    std::string msg;   // names the model, clip file, track or bone
    bool ok() const { return code == PoseErr::Ok; }
    std::string str() const;
};

// Reads a clip .gr2 by its lower-case basename. Returns false with `why` set; `missing` true when the file is in no
// source (-> ModelNotFound), false when it was found but could not be read (-> BadModel).
using ClipFileLoader = std::function<bool(const std::string& basename_lower, c5m::FileInfo& out, bool& missing,
                                          std::string& why)>;
ClipFileLoader clipLoaderFromProvider(c5m::FileProvider& files, const c5m::StringDatabase* strings);

// 3 x 3 row-major map from a file's axes to the stock unit basis (right +x, up +z, back -y), no unit scale.
struct Basis {
    bool identity = true;
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
// From the file's ArtToolInfo; identity when the file has none (all axes zero) or its axes are the stock ones
// (within 1e-6). A non-invertible set of axes gives BadModel in `st`.
Basis fileBasis(const c5m::FileInfo& f, PoseStatus* st);

// One member model prepared for posing (once per model and .fxsxml; copies what it needs, keeps no pointer).
struct PoseModel {
    std::string key;                                   // cache key (the module uses "<gr2>|<fxsxml>")
    Basis basis;                                       // the model file's basis (vertices take the same map)
    std::vector<c5m::Skeleton> skeletons;              // as read (bone names, parents)
    std::vector<std::vector<c5m::Mat4>> rest;          // per skeleton, per bone: rest local, basis-converted
    std::vector<std::vector<c5m::Mat4>> inverse_world; // per skeleton, per bone: column convention, basis-converted
    struct MeshBind {
        std::string name;
        int32_t skeleton = -1;                         // index into skeletons; -1 = no bindings (or unresolved)
        std::vector<int32_t> bones;                    // per BoneBinding: bone index in that skeleton
        std::string unresolved;                        // why a mesh with bindings has no skeleton ("" = fine)
    };
    std::vector<MeshBind> meshes;                      // per mesh of the file, in file order
    std::vector<c5m::FxsAnimation> animations;         // the member's .fxsxml animation list, in file order
    // 4466: the default clips' indexes into `animations`, found once by buildPoseModel (findAnimation "ec:1000" /
    // "ec:1520"); -1 = none. defaultClipIndex() reads them; defaultClip() still looks up by name.
    int32_t idle = -1;
    int32_t fortify = -1;
};
// BadModel: an invalid basis, a skeleton with a parent at or after its child.
PoseStatus buildPoseModel(const c5m::FileInfo& model, const std::vector<c5m::FxsAnimation>& animations,
                          const std::string& key, PoseModel& out);

// The .fxsxml animation `clip` names: matched (ASCII case ignored) against an animation's `name` attribute, then its
// file with or without directory and ".gr2", then "ec:<code>" or a plain integer against its ec codes. -1 = none.
int32_t findAnimation(const PoseModel& pm, const std::string& clip);
// The default clip of a figure with no FigureInput (unit-renderer.md 6.2, decision 6): the idle A (ec 1000), or with
// `fortified` the fortify idle (ec 1520), falling back to the idle A; "" if the member has neither. Returns the
// animation's file as the .fxsxml writes it.
std::string defaultClip(const PoseModel& pm, bool fortified);
// 4466: the same choice as an index into pm.animations from the indexes buildPoseModel stored (pm.fortify with
// `fortified` when it is >= 0, else pm.idle); -1 = none. For a model built by buildPoseModel, defaultClip(pm, f) ==
// pm.animations[defaultClipIndex(pm, f)].file (or "" with -1).
int32_t defaultClipIndex(const PoseModel& pm, bool fortified);

// A clip bound to one skeleton of one model, with its decoded control points.
struct CachedClip {
    PoseStatus status;              // a failed bind is cached too: the next frame gets the same answer at no cost
    std::string file;               // lower-case basename of the clip file
    float duration = 0;
    Basis basis;                    // the clip file's basis
    c5m::BoundClip bound;
    size_t bytes = 0;               // decoded knots and controls and the track tables, bytes
};

class ClipCache {
public:
    explicit ClipCache(ClipFileLoader loader);
    // Clip `clip` of model `pm`, bound to skeleton `skeleton`: from the cache, else looked up in the .fxsxml list,
    // read and bound (every curve of a skeleton bone decoded) and kept. `out` points into the cache (valid until
    // clear()). ClipNotFound is not cached (a name look-up only).
    PoseStatus get(const PoseModel& pm, const std::string& clip, uint32_t skeleton, const CachedClip*& out);
    struct Stats {
        size_t entries = 0, failed_entries = 0;  // kept bindings (failed ones included)
        size_t hits = 0, binds = 0;              // look-ups served from the cache / that read and bound a file
        size_t bytes = 0;                        // decoded curve data kept
        double bind_ms = 0;                      // wall time spent reading and binding (an indication, COMMON 23)
    };
    const Stats& stats() const { return stats_; }
    void clear();
    // 0441 (additive): the duration of every clip file bound here without a failure and with a positive finite
    // duration (animation 0's, as read from the file), by the file's lower-case basename (CachedClip::file); entries
    // already in `out` are kept. Returns how many it added.
    size_t durations(std::map<std::string, float>& out) const;
private:
    ClipFileLoader loader_;
    std::map<std::string, CachedClip> map_;      // key: model key | animation index | skeleton
    Stats stats_;
};

struct MeshPalette {
    uint32_t mesh = 0;                 // index into the model file's meshes
    std::vector<c5m::Row34> rows;      // one per BoneBinding of the mesh, in binding order
};

// The figure's palette rows. t is wrapped into the clip (loop) or clamped (c5m::clip_time). `meshes` empty = every
// mesh of the model file (a mesh without bindings gets no rows). On failure `out` is empty.
PoseStatus poseFigure(ClipCache& cache, const PoseModel& pm, const std::string& clip, float t, bool loop, float scale,
                      float yaw_rad, const float T[3], const std::vector<uint32_t>& meshes,
                      std::vector<MeshPalette>& out);

// 4409 (additive, epic 0374): the figure's palette rows from a pose program (c5r/unitanim/pose_program.h): every
// Sample node sampled at its t (clamped to the clip: the controller has already wrapped it), Blend nodes per bone in
// local space as blendfit.py does (positions and scale-shear lerped, orientations nlerped after aligning b to a's
// hemisphere; bone weight w x m with m the mask's weight for the bone, the default weight for bones it does not
// name), then 0041's world, skinning and palette steps unchanged. A program with one Sample at t < duration goes
// through poseFigure() itself: its rows are bit-identical to poseFigure(clip, t, loop) and to ModulePose::rows
// (MC condition (1) of #2000). Codes as poseFigure, plus BadInput for an empty program, a node index that is not an
// earlier node, a non-finite weight, a mask index out of range, and a mask with a binding but no bones (which bones a
// .dge's track mask binding names is not read yet: unit-controller.md 13.2); BadModel when two blended sources or a
// source and a rest bone are in different file bases.
PoseStatus poseFigureProgram(ClipCache& cache, const PoseModel& pm, const unitanim::PoseProgram& prog, float scale,
                             float yaw_rad, const float T[3], const std::vector<uint32_t>& meshes,
                             std::vector<MeshPalette>& out);

// The module's pose step (units.h's PoseSource). Builds a PoseModel per (req.model->gr2, member .fxsxml) from
// req.model->file and req.member->files.animations on first use; clip "" = defaultClip(fortified flag); t wraps
// (every clip treated as looping until the controller, U5, says otherwise). Rows of slots the mesh does not bind are
// identity. Codes: ClipNotFound, CurveUnsupported, ModelNotFound, BadModel, BadInput.
class ModulePose : public PoseSource {
public:
    explicit ModulePose(c5m::FileProvider& files, const c5m::StringDatabase* strings = nullptr);
    explicit ModulePose(ClipFileLoader loader);
    UnitStatus rows(const PoseRequest& req, Float4 out[kPaletteBones * 3]) override;
    // Binds every clip of the member's .fxsxml to every skeleton its meshes use (the pre-check's clip binding).
    UnitStatus bindAll(const MemberArt& member, const UnitModel& model);
    // 4409 (additive): the request's rows from a pose program instead of (clip, t) (req.clip and req.t unused).
    UnitStatus rowsProgram(const PoseRequest& req, const unitanim::PoseProgram& prog, Float4 out[kPaletteBones * 3]);
    // 4409 (additive): the duration of a clip of the member (any skeleton its meshes use; the first that binds).
    UnitStatus clipDuration(const MemberArt& member, const UnitModel& model, const std::string& clip, float& out);
    const ClipCache& cache() const { return cache_; }
    size_t models() const { return models_.size(); }
    // Sampling time spent in rows(), an indication (COMMON 23).
    double sampleMs() const { return sample_ms_; }
    size_t samples() const { return samples_; }
    // 4466: requests served from the world memo (a skeleton's world transforms of an earlier request with the same
    // model, clip, clip time and loop rule, or the same pose program) / that sampled the clip.
    size_t worldHits() const { return world_hits_; }
    size_t worldSamples() const { return world_samples_; }
private:
    UnitStatus model(const MemberArt& member, const UnitModel& m, const PoseModel*& out);
    // 4466: one mesh's rows by the memo; the same checks, codes, messages and float operations as poseFigure /
    // poseFigureProgram with that one mesh (render/tests/units/test_pose.cpp compares them byte for byte).
    UnitStatus memoRows(const PoseModel& pm, const PoseRequest& req, const std::string* clip, float t, bool loop,
                        const unitanim::PoseProgram* prog, Float4 out[kPaletteBones * 3]);
    ClipCache cache_;
    std::map<std::string, std::unique_ptr<PoseModel>> models_;
    std::map<std::string, PoseStatus> failed_;
    double sample_ms_ = 0;
    size_t samples_ = 0;
    // 4466: the last member / model pair model() resolved (checked by name before use: the pointers alone could be
    // reused by other art) and the world memo, a ring of the last kWorldMemo skeleton poses. The memo holds pointers
    // into models_ and cache_, which this object never frees while it lives.
    const MemberArt* last_member_ = nullptr;
    const UnitModel* last_model_ = nullptr;
    std::string last_gr2_, last_fxs_;
    const PoseModel* last_pm_ = nullptr;
    struct WorldMemo {
        const PoseModel* pm = nullptr;
        int32_t skeleton = -1;
        const CachedClip* clip = nullptr;       // a (clip, t, loop) entry; nullptr for a program entry
        uint32_t t_bits = 0;
        bool loop = false;
        bool program = false;
        unitanim::PoseProgram prog;              // a program entry: the program it was sampled from
        std::vector<c5m::Mat4> world;
    };
    static const size_t kWorldMemo = 8;
    WorldMemo memo_[kWorldMemo];
    size_t memo_next_ = 0;
    size_t world_hits_ = 0, world_samples_ = 0;
    // 4466: (model, skeleton, clip name) -> the ClipCache entry cache_.get gave for it (failed bindings included; a
    // ClipNotFound has no entry and is asked again). Looked up without building a string (transparent comparator).
    struct ClipKey {
        const PoseModel* pm;
        uint32_t skeleton;
        std::string clip;
    };
    struct ClipRef {
        const PoseModel* pm;
        uint32_t skeleton;
        const std::string* clip;
    };
    struct ClipLess {
        using is_transparent = void;
        static bool less(const PoseModel* a, uint32_t as, const std::string& ac, const PoseModel* b, uint32_t bs,
                         const std::string& bc) {
            if (a != b) return std::less<const PoseModel*>()(a, b);
            if (as != bs) return as < bs;
            return ac < bc;
        }
        bool operator()(const ClipKey& a, const ClipKey& b) const { return less(a.pm, a.skeleton, a.clip, b.pm, b.skeleton, b.clip); }
        bool operator()(const ClipKey& a, const ClipRef& b) const { return less(a.pm, a.skeleton, a.clip, b.pm, b.skeleton, *b.clip); }
        bool operator()(const ClipRef& a, const ClipKey& b) const { return less(a.pm, a.skeleton, *a.clip, b.pm, b.skeleton, b.clip); }
    };
    std::map<ClipKey, const CachedClip*, ClipLess> clips_;
    PoseStatus clipFor(const PoseModel& pm, const std::string& clip, uint32_t skeleton, const CachedClip*& out);
};

}}  // namespace c5r::units
