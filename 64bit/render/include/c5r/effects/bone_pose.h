// c5r/effects/bone_pose.h - a figure's trigger bone in world units, for the effects the server starts and moves on a bone
// (epic 0042, ticket 4728; route S stage A, D210 / D211). Own code, GPL-3.0-or-later. Written from the knowledge base
// (unit-playback.md 1.1: the palette rows [Rz(yaw) R_k | s Rz(yaw) t_k + T]; unit-renderer.md 6.3 the pose step;
// effects-formats.md 8.1: an effect started on a bone takes the bone's world matrix). No decompiler output, nothing of the
// game's files (D83): the member's .gr2 and clips are read from the player's install at run time.
//
// The skeleton is posed exactly as the unit module's pose step poses it (c5r/units/pose.h: PoseModel, ClipCache, the
// clip's file basis brought to the stock unit basis, world = parent's world x local), then placed as the palette rows
// place a figure: the bone's point p goes to s Rz(yaw) p + T (the member's Scale scales the model's positions, not its
// rotations) and its rotation R to Rz(yaw) R (columns normalised: a bone's scale-shear is not an orientation).
// A blended program (two clips at once) is posed with the clip of the larger weight at each blend [I: one bone point
// for an effect start; the drawn figure blends], counted.
//
// Own PoseModel and ClipCache (one per poser; the fighting members' clips only). Not thread-safe. Nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "c5models/art.h"
#include "c5r/unitanim/pose_program.h"
#include "c5r/units/art.h"
#include "c5r/units/pose.h"

namespace c5r { namespace effects {

// Where a figure stands: its model origin T (world units, z up), its yaw about +z (radians, the palette's) and its
// member's Scale (ArtDefine_UnitMemberInfos.Scale, as UnitModel::scale; <= 0 or not finite: 1).
struct FigureFrame {
    float x = 0, y = 0, z = 0;
    float yaw = 0;
    float scale = 1;
};

struct BonePoseCounts {
    size_t asks = 0, ok = 0;
    size_t blends = 0;            // programs with a blend: posed with one clip
    size_t noBone = 0;            // the bone is in no skeleton of the member's model
    size_t modelFails = 0;        // the .gr2 could not be read or prepared (once per model, then answered from the cache)
    size_t clipFails = 0;         // the clip could not be bound or sampled
    size_t models = 0;            // models prepared
};

class BonePoser {
public:
    BonePoser(c5m::FileProvider& files, const c5m::StringDatabase* strings);
    // The bone `bone` (its name as the trigger file writes it; ASCII case ignored) of a figure of member `m` posed by
    // `prog` and standing at `f`: its world point and its orientation as a unit quaternion (x, y, z, w) in the
    // convention of rotationQuaternion (projectile.h; 4722's StartSpec::orient). False with why: no bone of that name,
    // the model or the clip refused, an empty program, a non-finite input.
    bool boneWorld(const units::MemberArt& m, const unitanim::PoseProgram& prog, const std::string& bone,
                   const FigureFrame& f, float pos[3], float orient[4], std::string* why = nullptr);
    const BonePoseCounts& counts() const { return counts_; }

private:
    const units::PoseModel* model(const units::MemberArt& m, std::string& why);
    c5m::FileProvider& files_;
    const c5m::StringDatabase* strings_;
    units::ClipCache cache_;
    std::map<std::string, std::unique_ptr<units::PoseModel>> models_;
    std::map<std::string, std::string> failed_;                 // model key -> why
    BonePoseCounts counts_;
};

}}  // namespace c5r::effects
