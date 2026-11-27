// c5models/anim.h - pose sampler for Civilization V's unit animations (epic 0327, issue 4151; layer L2 of the
// increment 3 sheet). Own code, GPL-3.0-or-later like the rest of the repository, except the two curve decoders
// named in src/anim_curves.cpp that are translated from LSLib (MIT, notice kept there).
//
// What it does (research/05-graphics-composition/model-reader.md 6.1-6.5, unit-playback.md 1 and 5):
//   1. decode_curve()     a Curve as read_gr2() keeps it (format, degree, scalars, raw arrays) -> knots (seconds)
//                         and controls (dim floats each). Formats 1, 2, 3, 4, 5, 8, 9, 10, 11, 17, 18; the others
//                         (0, 6, 7, 12-16) are refused with AnimErr::UnsupportedFormat.
//   2. eval_curve()       de Boor of degree 0-3 on the clamped knot vector of nwn2mdk (the stored knots padded:
//                         T = [0 x (k+1), K2 .. K(n-1), K(n-1) x k]); quaternions blended by nlerp with the
//                         hemisphere fix at every step and normalised; positions and scale/shear linearly.
//   3. bind_clip()        an Animation's first track group bound to a skeleton's bones by name.
//   4. sample_local()     local 4x4 per bone at time t (bones without a track keep the skeleton's rest Transform).
//   5. world_from_local() world = parent's world x local, by ParentIndex (a parent must precede its child).
//   6. skin_rows()        World x InverseWorldTransform, the top three rows, per mesh BoneBinding.
//   7. palette_rows()     the EXE's t27 rows of a figure: [Rz(yaw) R_k | s Rz(yaw) t_k + T] (unit-playback.md 1.1).
// Matrices are column-vector convention, row-major storage: m[r*4 + c]; a Row34 is three rows of four.
//
// Language level C++14 (D15). Errors: functions return false and fill an AnimError; nothing throws.
#pragma once

#include "c5models/gr2.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace c5m {

enum class AnimErr {
    None = 0,
    UnsupportedFormat,    // a curve format the sampler does not decode (0, 6, 7, 12-16, or unknown)
    BadDegree,            // degree above 3
    MissingMember,        // a scalar or array the format needs is absent or has the wrong element type / size
    ArrayMismatch,        // array length does not fit the format (not a multiple of 4 / 2, controls not n x dim)
    DimensionMismatch,    // the curve's dimension is not the slot's (3 position, 4 orientation, 9 scale/shear)
    KnotsNotMonotonic,    // a knot below its predecessor, or a knot that is not finite
    NonFinite,            // a control or scalar that is NaN or infinite
    UnboundBone,          // a mesh BoneBinding names a bone the skeleton does not have
    ParentOrder,          // a bone's ParentIndex is not below its own index (or out of range)
    NoTrackGroup,         // the animation has no track group in the file
};
const char* anim_err_name(AnimErr e);

struct AnimError {
    AnimErr code = AnimErr::None;
    std::string what;
};

enum class CurveSlot { Position = 0, Orientation = 1, ScaleShear = 2 };
uint32_t slot_dim(CurveSlot s);   // 3, 4, 9

struct DecodedCurve {
    uint32_t format = 0, degree = 0, dim = 0;
    bool identity = false;          // DaIdentity: the slot's identity value
    std::vector<float> knots;       // seconds; size n
    std::vector<float> controls;    // n * dim (a constant curve: one control, knot 0)
};
bool decode_curve(const Curve& c, CurveSlot slot, DecodedCurve& out, AnimError* err);

// Value at t (out: dim floats). t below 0 evaluates at 0, t past the last knot at the last knot.
void eval_curve(const DecodedCurve& c, float t, bool quaternion, float* out);

// Clip time: loop = t modulo duration (in [0, duration)); otherwise clamped to [0, duration].
float clip_time(float t, float duration, bool loop);

using Mat4 = std::array<float, 16>;
using Row34 = std::array<float, 12>;

Mat4 compose_transform(const float translation[3], const float rotation_xyzw[4], const float scale_shear[9]);

struct ClipTrack {
    DecodedCurve position, orientation, scale_shear;
};
struct BoundClip {
    std::string name;
    float duration = 0;
    std::vector<ClipTrack> tracks;
    std::vector<int32_t> bone_track;     // per skeleton bone: index into tracks, -1 = rest pose
    uint32_t unbound_tracks = 0;         // tracks whose name is no bone of the skeleton (ignored, counted)
};
// Binds animation `anim` of `anim_file` (its first track group) to `skel`. Every curve is decoded here.
bool bind_clip(const FileInfo& anim_file, size_t anim, const Skeleton& skel, BoundClip& out, AnimError* err);
// As bind_clip, over EVERY track group of the animation in order, the first track of a bone name winning (epic 0041,
// issue 4353; src/anim_groups.cpp): VP's hakkapelitta clips hold a one-track group before the skeleton's
// (unit-formations.md 12.2). Only tracks that name a bone of `skel` are decoded; the others are counted in
// unbound_tracks and never refuse the clip. NoTrackGroup when the animation has no group at all.
bool bind_clip_all_groups(const FileInfo& anim_file, size_t anim, const Skeleton& skel, BoundClip& out,
                          AnimError* err);

void sample_local(const BoundClip& clip, const Skeleton& skel, float t, std::vector<Mat4>& local);
bool world_from_local(const Skeleton& skel, const std::vector<Mat4>& local, std::vector<Mat4>& world, AnimError* err);
bool skin_rows(const Skeleton& skel, const Mesh& mesh, const std::vector<Mat4>& world, std::vector<Row34>& out,
               AnimError* err);
// The figure's palette rows: R = Rz(yaw) R_k, p = s Rz(yaw) t_k + T (yaw in radians about +z).
void palette_rows(const std::vector<Row34>& skin, float scale, float yaw, const float T[3], std::vector<Row34>& out);

}  // namespace c5m
