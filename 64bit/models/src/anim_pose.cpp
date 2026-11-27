// anim_pose.cpp - clip binding, local and world poses, skinning rows and the EXE's palette rows (c5models/anim.h).
// Epic 0327, issue 4151, 2026-11-15. Own code, from model-reader.md 6.1 / 6.5 and unit-playback.md 1.1.
#include "c5models/anim.h"

#include <cmath>
#include <unordered_map>

namespace c5m {

namespace {

bool fail(AnimError* err, AnimErr code, const std::string& what) {
    if (err) {
        err->code = code;
        err->what = what;
    }
    return false;
}

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    return r;
}

}  // namespace

Mat4 compose_transform(const float p[3], const float q_in[4], const float ss[9]) {
    // v' = R(q) * SS^T * v + P: the stored ScaleShear is for row vectors, its transpose acts on columns
    float x = q_in[0], y = q_in[1], z = q_in[2], w = q_in[3];
    float n = std::sqrt(x * x + y * y + z * z + w * w);
    if (!(n > 0)) n = 1.0f;
    x /= n; y /= n; z /= n; w /= n;
    const float R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                        2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    Mat4 m{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += R[r * 3 + k] * ss[c * 3 + k];
            m[r * 4 + c] = s;
        }
        m[r * 4 + 3] = p[r];
    }
    m[15] = 1.0f;
    return m;
}

bool bind_clip(const FileInfo& f, size_t anim, const Skeleton& skel, BoundClip& out, AnimError* err) {
    out = BoundClip();
    if (anim >= f.animations.size()) return fail(err, AnimErr::NoTrackGroup, "no animation " + std::to_string(anim));
    const Animation& a = f.animations[anim];
    out.name = a.name;
    out.duration = a.duration;
    if (a.track_groups.empty() || a.track_groups[0] < 0 || (size_t)a.track_groups[0] >= f.track_groups.size())
        return fail(err, AnimErr::NoTrackGroup, a.name + ": no track group");
    const TrackGroup& tg = f.track_groups[(size_t)a.track_groups[0]];
    std::unordered_map<std::string, int32_t> bone_index;
    for (size_t i = 0; i < skel.bones.size(); ++i) bone_index.emplace(skel.bones[i].name, (int32_t)i);
    out.bone_track.assign(skel.bones.size(), -1);
    for (const TransformTrack& tt : tg.transform_tracks) {
        ClipTrack ct;
        AnimError e;
        if (!decode_curve(tt.position, CurveSlot::Position, ct.position, &e) ||
            !decode_curve(tt.orientation, CurveSlot::Orientation, ct.orientation, &e) ||
            !decode_curve(tt.scale_shear, CurveSlot::ScaleShear, ct.scale_shear, &e))
        {
            out = BoundClip();
            return fail(err, e.code, a.name + ": track " + tt.name + ": " + e.what);
        }
        auto it = bone_index.find(tt.name);
        if (it == bone_index.end()) {
            ++out.unbound_tracks;
            continue;
        }
        if (out.bone_track[(size_t)it->second] >= 0) continue;   // a second track of one bone: the first wins
        out.bone_track[(size_t)it->second] = (int32_t)out.tracks.size();
        out.tracks.push_back(ct);
    }
    return true;
}

void sample_local(const BoundClip& clip, const Skeleton& skel, float t, std::vector<Mat4>& local) {
    local.resize(skel.bones.size());
    for (size_t b = 0; b < skel.bones.size(); ++b) {
        const int32_t ti = b < clip.bone_track.size() ? clip.bone_track[b] : -1;
        if (ti < 0) {
            const Transform& r = skel.bones[b].local;
            local[b] = compose_transform(r.translation, r.rotation, r.scale_shear);
            continue;
        }
        const ClipTrack& ct = clip.tracks[(size_t)ti];
        float p[3], q[4], ss[9];
        eval_curve(ct.position, t, false, p);
        eval_curve(ct.orientation, t, true, q);
        eval_curve(ct.scale_shear, t, false, ss);
        local[b] = compose_transform(p, q, ss);
    }
}

bool world_from_local(const Skeleton& skel, const std::vector<Mat4>& local, std::vector<Mat4>& world, AnimError* err) {
    const size_t n = skel.bones.size();
    if (local.size() != n) return fail(err, AnimErr::ArrayMismatch, "local pose size");
    world.resize(n);
    for (size_t b = 0; b < n; ++b) {
        const int32_t p = skel.bones[b].parent;
        if (p < 0) {
            world[b] = local[b];
        } else if ((size_t)p >= b) {
            world.clear();
            return fail(err, AnimErr::ParentOrder,
                        "bone " + std::to_string(b) + " (" + skel.bones[b].name + ") has parent " + std::to_string(p));
        } else {
            world[b] = mul(world[(size_t)p], local[b]);
        }
    }
    return true;
}

bool skin_rows(const Skeleton& skel, const Mesh& mesh, const std::vector<Mat4>& world, std::vector<Row34>& out,
               AnimError* err) {
    out.clear();
    if (world.size() != skel.bones.size()) return fail(err, AnimErr::ArrayMismatch, "world pose size");
    for (const BoneBinding& bb : mesh.bone_bindings) {
        const int32_t i = skel.find(bb.bone_name);
        if (i < 0) {
            out.clear();
            return fail(err, AnimErr::UnboundBone, mesh.name + ": bone " + bb.bone_name + " not in the skeleton");
        }
        Mat4 iw;   // stored for row vectors: transpose for column vectors
        const float* s = skel.bones[(size_t)i].inverse_world;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) iw[r * 4 + c] = s[c * 4 + r];
        const Mat4 m = mul(world[(size_t)i], iw);
        Row34 row;
        for (int k = 0; k < 12; ++k) row[k] = m[k];
        out.push_back(row);
    }
    return true;
}

void palette_rows(const std::vector<Row34>& skin, float scale, float yaw, const float T[3], std::vector<Row34>& out) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float Rz[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
    out.resize(skin.size());
    for (size_t k = 0; k < skin.size(); ++k) {
        const Row34& m = skin[k];
        Row34& o = out[k];
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                float v = 0;
                for (int j = 0; j < 3; ++j) v += Rz[r * 3 + j] * m[j * 4 + col];
                o[r * 4 + col] = v;
            }
            float tv = 0;
            for (int j = 0; j < 3; ++j) tv += Rz[r * 3 + j] * m[j * 4 + 3];
            o[r * 4 + 3] = scale * tv + T[r];
        }
    }
}

}  // namespace c5m
