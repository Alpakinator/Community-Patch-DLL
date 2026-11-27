// render/src/units/pose.cpp - L2 of the unit module: pose models, clip look-up, the figure's palette rows and the
// module's PoseSource (epic 0041, issue 4353; c5r/units/pose.h, unit-renderer.md 6.3 and 11).
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include "c5r/units/pose.h"

namespace c5r { namespace units {

const char* poseErrName(PoseErr e) {
    switch (e) {
        case PoseErr::Ok: return "Ok";
        case PoseErr::ClipNotFound: return "ClipNotFound";
        case PoseErr::CurveUnsupported: return "CurveUnsupported";
        case PoseErr::ModelNotFound: return "ModelNotFound";
        case PoseErr::BadModel: return "BadModel";
        case PoseErr::BadInput: return "BadInput";
    }
    return "?";
}

UnitCode poseErrUnitCode(PoseErr e) {
    switch (e) {
        case PoseErr::Ok: return UnitCode::Ok;
        case PoseErr::ClipNotFound: return UnitCode::ClipNotFound;
        case PoseErr::CurveUnsupported: return UnitCode::CurveUnsupported;
        case PoseErr::ModelNotFound: return UnitCode::ModelNotFound;
        case PoseErr::BadModel: return UnitCode::BadModel;
        case PoseErr::BadInput: return UnitCode::BadInput;
    }
    return UnitCode::BadModel;
}

std::string PoseStatus::str() const { return std::string(poseErrName(code)) + (msg.empty() ? "" : ": " + msg); }

namespace {

PoseStatus fail(PoseErr c, std::string msg) {
    PoseStatus s;
    s.code = c;
    s.msg = std::move(msg);
    return s;
}

using c5m::Mat4;

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

Mat4 fromBasis(const float m[9]) {
    Mat4 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 4 + j] = m[i * 3 + j];
    r[15] = 1;
    return r;
}

// Inverse of a 3 x 3 (row-major); false if singular.
bool inv3(const double a[9], double out[9]) {
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                       a[2] * (a[3] * a[7] - a[4] * a[6]);
    if (!(std::fabs(det) > 1e-9)) return false;
    const double id = 1.0 / det;
    out[0] = (a[4] * a[8] - a[5] * a[7]) * id;
    out[1] = (a[2] * a[7] - a[1] * a[8]) * id;
    out[2] = (a[1] * a[5] - a[2] * a[4]) * id;
    out[3] = (a[5] * a[6] - a[3] * a[8]) * id;
    out[4] = (a[0] * a[8] - a[2] * a[6]) * id;
    out[5] = (a[2] * a[3] - a[0] * a[5]) * id;
    out[6] = (a[3] * a[7] - a[4] * a[6]) * id;
    out[7] = (a[1] * a[6] - a[0] * a[7]) * id;
    out[8] = (a[0] * a[4] - a[1] * a[3]) * id;
    return true;
}

struct Conj {
    bool identity = true;
    Mat4 m{}, mi{};
};
Conj conjOf(const Basis& b) {
    Conj c;
    if (b.identity) return c;
    double a[9], ai[9];
    for (int i = 0; i < 9; ++i) a[i] = b.m[i];
    inv3(a, ai);   // fileBasis() made sure it is invertible
    float fi[9];
    for (int i = 0; i < 9; ++i) fi[i] = static_cast<float>(ai[i]);
    c.identity = false;
    c.m = fromBasis(b.m);
    c.mi = fromBasis(fi);
    return c;
}
Mat4 apply(const Conj& c, const Mat4& x) { return c.identity ? x : mul(mul(c.m, x), c.mi); }

bool finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// "Warrior_IdleA.gr2", "Assets\\Units\\Warrior_IdleA.gr2" -> "warrior_idlea"
std::string stem(const std::string& path) {
    std::string b = c5m::basename_lower(path);
    if (b.size() > 4 && b.compare(b.size() - 4, 4, ".gr2") == 0) b.resize(b.size() - 4);
    return b;
}

bool parseEc(const std::string& s, int& ec) {
    std::string d = s;
    if (d.size() > 3 && c5m::ascii_lower(d.substr(0, 3)) == "ec:") d = d.substr(3);
    if (d.empty() || d.size() > 9) return false;
    for (char ch : d)
        if (ch < '0' || ch > '9') return false;
    ec = std::atoi(d.c_str());
    return true;
}

}  // namespace

Basis fileBasis(const c5m::FileInfo& f, PoseStatus* st) {
    if (st) *st = PoseStatus();
    Basis b;
    bool any = false;
    for (int i = 0; i < 3; ++i) any = any || f.right[i] != 0 || f.up[i] != 0 || f.back[i] != 0;
    if (!any) return b;   // no ArtToolInfo axes: used as written
    // src columns: the file's right, up, back; dst columns: the stock unit files' (+x, +z, -y). A = dst * src^-1.
    double src[9] = {f.right[0], f.up[0], f.back[0], f.right[1], f.up[1], f.back[1], f.right[2], f.up[2], f.back[2]};
    const double dst[9] = {1, 0, 0, 0, 0, -1, 0, 1, 0};
    double si[9];
    if (!inv3(src, si)) {
        if (st) *st = fail(PoseErr::BadModel, "the file's ArtToolInfo axes are not invertible");
        return b;
    }
    bool ident = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double v = 0;
            for (int k = 0; k < 3; ++k) v += dst[i * 3 + k] * si[k * 3 + j];
            b.m[i * 3 + j] = static_cast<float>(v);
            if (std::fabs(v - (i == j ? 1.0 : 0.0)) > 1e-6) ident = false;
        }
    if (ident) return Basis();
    b.identity = false;
    return b;
}

PoseStatus buildPoseModel(const c5m::FileInfo& model, const std::vector<c5m::FxsAnimation>& animations,
                          const std::string& key, PoseModel& out) {
    out = PoseModel();
    PoseModel pm;
    pm.key = key;
    PoseStatus bs;
    pm.basis = fileBasis(model, &bs);
    if (!bs.ok()) return fail(PoseErr::BadModel, key + ": " + bs.msg);
    const Conj cj = conjOf(pm.basis);
    pm.skeletons = model.skeletons;
    for (size_t s = 0; s < pm.skeletons.size(); ++s) {
        const c5m::Skeleton& sk = pm.skeletons[s];
        std::vector<Mat4> rest(sk.bones.size()), iw(sk.bones.size());
        for (size_t b = 0; b < sk.bones.size(); ++b) {
            const c5m::Bone& bone = sk.bones[b];
            if (bone.parent >= static_cast<int32_t>(b))
                return fail(PoseErr::BadModel, key + ": skeleton " + sk.name + ": bone " + bone.name + " has parent " +
                                                   std::to_string(bone.parent));
            rest[b] = apply(cj, c5m::compose_transform(bone.local.translation, bone.local.rotation,
                                                       bone.local.scale_shear));
            Mat4 t{};   // stored for row vectors: transposed to column convention
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) t[r * 4 + c] = bone.inverse_world[c * 4 + r];
            iw[b] = apply(cj, t);
        }
        pm.rest.push_back(std::move(rest));
        pm.inverse_world.push_back(std::move(iw));
    }
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const c5m::Mesh& mesh = model.meshes[mi];
        PoseModel::MeshBind mb;
        mb.name = mesh.name;
        if (!mesh.bone_bindings.empty()) {
            // The skeleton of the model entry that binds this mesh first, then any skeleton with every bound bone.
            std::vector<int32_t> order;
            for (const c5m::ModelEntry& me : model.models)
                for (int32_t m : me.meshes)
                    if (m == static_cast<int32_t>(mi) && me.skeleton >= 0 &&
                        static_cast<size_t>(me.skeleton) < pm.skeletons.size())
                        order.push_back(me.skeleton);
            for (size_t s = 0; s < pm.skeletons.size(); ++s) order.push_back(static_cast<int32_t>(s));
            for (int32_t s : order) {
                std::vector<int32_t> bones;
                for (const c5m::BoneBinding& bb : mesh.bone_bindings) {
                    const int32_t bi = pm.skeletons[static_cast<size_t>(s)].find(bb.bone_name);
                    if (bi < 0) break;
                    bones.push_back(bi);
                }
                if (bones.size() == mesh.bone_bindings.size()) {
                    mb.skeleton = s;
                    mb.bones = std::move(bones);
                    break;
                }
            }
            if (mb.skeleton < 0)
                mb.unresolved = "mesh " + mesh.name + " binds bones that no skeleton of the file has all of (first: " +
                                mesh.bone_bindings[0].bone_name + ")";
        }
        pm.meshes.push_back(std::move(mb));
    }
    pm.animations = animations;
    pm.idle = findAnimation(pm, "ec:1000");     // 4466: defaultClip's two look-ups, once per model
    pm.fortify = findAnimation(pm, "ec:1520");
    out = std::move(pm);
    return PoseStatus();
}

int32_t findAnimation(const PoseModel& pm, const std::string& clip) {
    if (clip.empty()) return -1;
    const std::string lc = c5m::ascii_lower(clip);
    for (size_t i = 0; i < pm.animations.size(); ++i)
        if (!pm.animations[i].name.empty() && c5m::ascii_lower(pm.animations[i].name) == lc)
            return static_cast<int32_t>(i);
    const std::string st = stem(clip);
    for (size_t i = 0; i < pm.animations.size(); ++i)
        if (stem(pm.animations[i].file) == st) return static_cast<int32_t>(i);
    int ec = 0;
    if (parseEc(clip, ec))
        for (size_t i = 0; i < pm.animations.size(); ++i)
            for (int e : pm.animations[i].ec)
                if (e == ec) return static_cast<int32_t>(i);
    return -1;
}

std::string defaultClip(const PoseModel& pm, bool fortified) {
    if (fortified) {
        const int32_t i = findAnimation(pm, "ec:1520");
        if (i >= 0) return pm.animations[static_cast<size_t>(i)].file;
    }
    const int32_t i = findAnimation(pm, "ec:1000");
    return i >= 0 ? pm.animations[static_cast<size_t>(i)].file : std::string();
}

int32_t defaultClipIndex(const PoseModel& pm, bool fortified) {
    const int32_t i = fortified && pm.fortify >= 0 ? pm.fortify : pm.idle;
    return i >= 0 && static_cast<size_t>(i) < pm.animations.size() ? i : -1;
}

// 4466: the steps of poseFigure / poseFigureProgram as functions, so that ModulePose's memo runs the same float
// operations in the same order (its rows are byte-equal to these functions', tested in test_pose.cpp).
namespace {

// One mesh of the request: in range, resolved, at most 32 bound bones.
PoseStatus checkMesh(const PoseModel& pm, uint32_t mi) {
    if (mi >= pm.meshes.size())
        return fail(PoseErr::BadInput, pm.key + ": mesh " + std::to_string(mi) + " of " +
                                           std::to_string(pm.meshes.size()));
    const PoseModel::MeshBind& mb = pm.meshes[mi];
    if (!mb.unresolved.empty()) return fail(PoseErr::BadModel, pm.key + ": " + mb.unresolved);
    if (mb.bones.size() > kPaletteBones)
        return fail(PoseErr::BadModel, pm.key + ": mesh " + mb.name + " binds " + std::to_string(mb.bones.size()) +
                                           " bones (palette slots: 32)");
    return PoseStatus();
}

// Skeleton s's world transforms with clip cc at t (wrapped or clamped by `loop`).
PoseStatus sampleWorld(const PoseModel& pm, int32_t s, const CachedClip& cc, float t, bool loop,
                       std::vector<Mat4>& world) {
    const c5m::Skeleton& sk = pm.skeletons[static_cast<size_t>(s)];
    const Conj cj = conjOf(cc.basis);
    const float tc = c5m::clip_time(t, cc.duration, loop);
    std::vector<Mat4> local(sk.bones.size());
    for (size_t b = 0; b < sk.bones.size(); ++b) {
        const int32_t ti = b < cc.bound.bone_track.size() ? cc.bound.bone_track[b] : -1;
        if (ti < 0) {
            local[b] = pm.rest[static_cast<size_t>(s)][b];
            continue;
        }
        const c5m::ClipTrack& tr = cc.bound.tracks[static_cast<size_t>(ti)];
        float p[3], q[4], ss[9];
        c5m::eval_curve(tr.position, tc, false, p);
        c5m::eval_curve(tr.orientation, tc, true, q);
        c5m::eval_curve(tr.scale_shear, tc, false, ss);
        local[b] = apply(cj, c5m::compose_transform(p, q, ss));
    }
    c5m::AnimError ae;
    if (!c5m::world_from_local(sk, local, world, &ae))
        return fail(PoseErr::BadModel, pm.key + ": " + c5m::anim_err_name(ae.code) + ": " + ae.what);
    return PoseStatus();
}

// A mesh's palette rows from its skeleton's world transforms.
void paletteOf(const PoseModel& pm, const PoseModel::MeshBind& mb, const std::vector<Mat4>& w, float scale,
               float yaw_rad, const float T[3], std::vector<c5m::Row34>& rows) {
    const std::vector<Mat4>& iw = pm.inverse_world[static_cast<size_t>(mb.skeleton)];
    std::vector<c5m::Row34> skin;
    skin.reserve(mb.bones.size());
    for (int32_t bi : mb.bones) {
        const Mat4 m = mul(w[static_cast<size_t>(bi)], iw[static_cast<size_t>(bi)]);
        c5m::Row34 r;
        for (int k = 0; k < 12; ++k) r[k] = m[k];
        skin.push_back(r);
    }
    c5m::palette_rows(skin, scale, yaw_rad, T, rows);
}

}  // namespace

PoseStatus poseFigure(ClipCache& cache, const PoseModel& pm, const std::string& clip, float t, bool loop, float scale,
                      float yaw_rad, const float T[3], const std::vector<uint32_t>& meshes,
                      std::vector<MeshPalette>& out) {
    out.clear();
    if (!std::isfinite(t) || !std::isfinite(scale) || !std::isfinite(yaw_rad) || !T || !finite3(T))
        return fail(PoseErr::BadInput, pm.key + ": t, scale, yaw or T not finite");
    std::vector<uint32_t> list = meshes;
    if (list.empty())
        for (uint32_t i = 0; i < pm.meshes.size(); ++i) list.push_back(i);
    std::set<int32_t> skels;
    for (uint32_t mi : list) {
        PoseStatus ms = checkMesh(pm, mi);
        if (!ms.ok()) return ms;
        if (pm.meshes[mi].skeleton >= 0) skels.insert(pm.meshes[mi].skeleton);
    }
    std::map<int32_t, std::vector<Mat4>> worlds;
    for (int32_t s : skels) {
        const CachedClip* cc = nullptr;
        PoseStatus st = cache.get(pm, clip, static_cast<uint32_t>(s), cc);
        if (!st.ok()) return st;
        std::vector<Mat4> world;
        st = sampleWorld(pm, s, *cc, t, loop, world);
        if (!st.ok()) return st;
        worlds[s] = std::move(world);
    }
    std::vector<MeshPalette> res;
    for (uint32_t mi : list) {
        const PoseModel::MeshBind& mb = pm.meshes[mi];
        MeshPalette mp;
        mp.mesh = mi;
        if (mb.skeleton >= 0) paletteOf(pm, mb, worlds[mb.skeleton], scale, yaw_rad, T, mp.rows);
        res.push_back(std::move(mp));
    }
    out = std::move(res);
    return PoseStatus();
}

// ---------------------------------------------------------------------------------------------- pose programs (4409)

namespace {

struct BonePose {
    bool rest = true;
    float p[3] = {0, 0, 0};
    float q[4] = {0, 0, 0, 1};
    float ss[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

bool sameBasis(const Basis& a, const Basis& b) {
    if (a.identity || b.identity) return a.identity == b.identity;
    for (int i = 0; i < 9; ++i)
        if (a.m[i] != b.m[i]) return false;
    return true;
}

void restOf(const c5m::Bone& b, BonePose& o) {
    o.rest = false;
    for (int i = 0; i < 3; ++i) o.p[i] = b.local.translation[i];
    for (int i = 0; i < 4; ++i) o.q[i] = b.local.rotation[i];
    for (int i = 0; i < 9; ++i) o.ss[i] = b.local.scale_shear[i];
}

// blendfit.py's per-bone blend: lerp positions and scale-shear, nlerp orientations with b on a's hemisphere.
void blendBone(const BonePose& a, const BonePose& b, float w, BonePose& o) {
    o.rest = false;
    for (int i = 0; i < 3; ++i) o.p[i] = a.p[i] + (b.p[i] - a.p[i]) * w;
    for (int i = 0; i < 9; ++i) o.ss[i] = a.ss[i] + (b.ss[i] - a.ss[i]) * w;
    const float d = a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3];
    const float sb = d < 0 ? -1.0f : 1.0f;
    float q[4], n = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = a.q[i] * (1.0f - w) + sb * b.q[i] * w;
        n += q[i] * q[i];
    }
    n = std::sqrt(n);
    for (int i = 0; i < 4; ++i) o.q[i] = n > 0 ? q[i] / n : a.q[i];
}

// 4466: a program of more than one node: scale, yaw and T finite, every node well formed (poseFigureProgram's checks).
PoseStatus checkProgram(const PoseModel& pm, const unitanim::PoseProgram& prog, float scale, float yaw_rad,
                        const float T[3]) {
    if (!std::isfinite(scale) || !std::isfinite(yaw_rad) || !T || !finite3(T))
        return fail(PoseErr::BadInput, pm.key + ": scale, yaw or T not finite");
    for (size_t k = 0; k < prog.nodes.size(); ++k) {
        const unitanim::PoseNode& n = prog.nodes[k];
        // 0441: the node's name only on a failure (it was built for every node of every call)
        auto at = [&]() { return pm.key + ": program node " + std::to_string(k); };
        if (n.kind == unitanim::PoseNode::Sample) {
            if (!std::isfinite(n.t)) return fail(PoseErr::BadInput, at() + ": t not finite");
        } else {
            if (n.a < 0 || n.b < 0 || static_cast<size_t>(n.a) >= k || static_cast<size_t>(n.b) >= k)
                return fail(PoseErr::BadInput, at() + ": blend of nodes that are not earlier nodes");
            if (!std::isfinite(n.w)) return fail(PoseErr::BadInput, at() + ": weight not finite");
            if (n.mask >= 0) {
                if (static_cast<size_t>(n.mask) >= prog.masks.size())
                    return fail(PoseErr::BadInput, at() + ": mask " + std::to_string(n.mask) + " out of range");
                const unitanim::TrackMaskWeights& m = prog.masks[static_cast<size_t>(n.mask)];
                if (!m.binding.empty() && m.bones.empty())
                    return fail(PoseErr::BadInput, at() + ": track mask " + m.binding + " not resolved to bones");
                if (m.bones.size() != m.weights.size())
                    return fail(PoseErr::BadInput, at() + ": track mask bones and weights differ in count");
            }
        }
    }
    return PoseStatus();
}

// 4466: skeleton s's world transforms from a checked program; `get(clip, skeleton, cc)` binds a Sample node's clip.
template <class GetClip>
PoseStatus programWorld(GetClip get, const PoseModel& pm, const unitanim::PoseProgram& prog, int32_t s,
                        std::vector<Mat4>& world) {
    const c5m::Skeleton& sk = pm.skeletons[static_cast<size_t>(s)];
    const size_t nb = sk.bones.size();
    std::vector<std::vector<BonePose>> poses(prog.nodes.size());
    std::vector<Basis> bases(prog.nodes.size());
    for (size_t k = 0; k < prog.nodes.size(); ++k) {
        const unitanim::PoseNode& n = prog.nodes[k];
        std::vector<BonePose>& P = poses[k];
        P.resize(nb);
        if (n.kind == unitanim::PoseNode::Sample) {
            const CachedClip* cc = nullptr;
            PoseStatus st = get(n.clip, static_cast<uint32_t>(s), cc);
            if (!st.ok()) return st;
            bases[k] = cc->basis;
            const float tc = c5m::clip_time(n.t, cc->duration, false);
            for (size_t b = 0; b < nb; ++b) {
                const int32_t ti = b < cc->bound.bone_track.size() ? cc->bound.bone_track[b] : -1;
                if (ti < 0) continue;   // rest
                const c5m::ClipTrack& tr = cc->bound.tracks[static_cast<size_t>(ti)];
                P[b].rest = false;
                c5m::eval_curve(tr.position, tc, false, P[b].p);
                c5m::eval_curve(tr.orientation, tc, true, P[b].q);
                c5m::eval_curve(tr.scale_shear, tc, false, P[b].ss);
            }
            continue;
        }
        const std::vector<BonePose>& A = poses[static_cast<size_t>(n.a)];
        const std::vector<BonePose>& B = poses[static_cast<size_t>(n.b)];
        if (!sameBasis(bases[static_cast<size_t>(n.a)], bases[static_cast<size_t>(n.b)]))
            return fail(PoseErr::BadModel, pm.key + ": program node " + std::to_string(k) +
                                               ": blend of clips in different file bases");
        bases[k] = bases[static_cast<size_t>(n.a)];
        const unitanim::TrackMaskWeights* mask = n.mask >= 0 ? &prog.masks[static_cast<size_t>(n.mask)] : nullptr;
        for (size_t b = 0; b < nb; ++b) {
            if (A[b].rest && B[b].rest) continue;
            float m = 1;
            if (mask) {
                m = mask->default_weight;
                for (size_t j = 0; j < mask->bones.size(); ++j)
                    if (mask->bones[j] == sk.bones[b].name) {
                        m = mask->weights[j];
                        break;
                    }
            }
            BonePose ra, rb;
            const BonePose* pa = &A[b];
            const BonePose* pb = &B[b];
            if (pa->rest || pb->rest) {
                if (!sameBasis(pm.basis, bases[k]))
                    return fail(PoseErr::BadModel, pm.key + ": program node " + std::to_string(k) +
                                                       ": a rest bone and a clip in different file bases");
                if (pa->rest) { restOf(sk.bones[b], ra); pa = &ra; }
                if (pb->rest) { restOf(sk.bones[b], rb); pb = &rb; }
            }
            blendBone(*pa, *pb, n.w * m, P[b]);
        }
    }
    const std::vector<BonePose>& F = poses.back();
    const Conj cj = conjOf(bases.back());
    std::vector<Mat4> local(nb);
    for (size_t b = 0; b < nb; ++b)
        local[b] = F[b].rest ? pm.rest[static_cast<size_t>(s)][b]
                             : apply(cj, c5m::compose_transform(F[b].p, F[b].q, F[b].ss));
    c5m::AnimError ae;
    if (!c5m::world_from_local(sk, local, world, &ae))
        return fail(PoseErr::BadModel, pm.key + ": " + c5m::anim_err_name(ae.code) + ": " + ae.what);
    return PoseStatus();
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// 4466: two programs that give the same poses (every field the sampling reads, floats by bits).
bool samePrograms(const unitanim::PoseProgram& a, const unitanim::PoseProgram& b) {
    if (a.nodes.size() != b.nodes.size() || a.masks.size() != b.masks.size()) return false;
    for (size_t k = 0; k < a.nodes.size(); ++k) {
        const unitanim::PoseNode& x = a.nodes[k];
        const unitanim::PoseNode& y = b.nodes[k];
        if (x.kind != y.kind || x.clip != y.clip || x.ec != y.ec || !sameBits(x.t, y.t) || x.a != y.a || x.b != y.b ||
            !sameBits(x.w, y.w) || x.mask != y.mask)
            return false;
    }
    for (size_t k = 0; k < a.masks.size(); ++k) {
        const unitanim::TrackMaskWeights& x = a.masks[k];
        const unitanim::TrackMaskWeights& y = b.masks[k];
        if (x.binding != y.binding || !sameBits(x.default_weight, y.default_weight) || x.bones != y.bones ||
            x.weights.size() != y.weights.size() || !sameBits(x.target_weight, y.target_weight))
            return false;
        for (size_t j = 0; j < x.weights.size(); ++j)
            if (!sameBits(x.weights[j], y.weights[j])) return false;
    }
    return true;
}

}  // namespace

PoseStatus poseFigureProgram(ClipCache& cache, const PoseModel& pm, const unitanim::PoseProgram& prog, float scale,
                             float yaw_rad, const float T[3], const std::vector<uint32_t>& meshes,
                             std::vector<MeshPalette>& out) {
    out.clear();
    if (prog.nodes.empty()) return fail(PoseErr::BadInput, pm.key + ": empty pose program");
    if (prog.single())   // exactly 0041's path (t already wrapped or clamped by the controller)
        return poseFigure(cache, pm, prog.nodes[0].clip, prog.nodes[0].t, false, scale, yaw_rad, T, meshes, out);
    PoseStatus ps = checkProgram(pm, prog, scale, yaw_rad, T);
    if (!ps.ok()) return ps;
    std::vector<uint32_t> list = meshes;
    if (list.empty())
        for (uint32_t i = 0; i < pm.meshes.size(); ++i) list.push_back(i);
    std::set<int32_t> skels;
    for (uint32_t mi : list) {
        PoseStatus ms = checkMesh(pm, mi);
        if (!ms.ok()) return ms;
        if (pm.meshes[mi].skeleton >= 0) skels.insert(pm.meshes[mi].skeleton);
    }
    auto get = [&](const std::string& clip, uint32_t s, const CachedClip*& cc) { return cache.get(pm, clip, s, cc); };
    std::map<int32_t, std::vector<Mat4>> worlds;
    for (int32_t s : skels) {
        std::vector<Mat4> world;
        PoseStatus st = programWorld(get, pm, prog, s, world);
        if (!st.ok()) return st;
        worlds[s] = std::move(world);
    }
    std::vector<MeshPalette> res;
    for (uint32_t mi : list) {
        const PoseModel::MeshBind& mb = pm.meshes[mi];
        MeshPalette mp;
        mp.mesh = mi;
        if (mb.skeleton >= 0) paletteOf(pm, mb, worlds[mb.skeleton], scale, yaw_rad, T, mp.rows);
        res.push_back(std::move(mp));
    }
    out = std::move(res);
    return PoseStatus();
}

// ---------------------------------------------------------------------------------------------- ModulePose

namespace {

void identityRows(Float4 out[kPaletteBones * 3]) {
    for (uint32_t i = 0; i < kPaletteBones * 3; ++i) {
        out[i] = Float4();
        if (i % 3 == 0) out[i].x = 1;
        else if (i % 3 == 1) out[i].y = 1;
        else out[i].z = 1;
    }
}

void copyRows(const std::vector<c5m::Row34>& rows, Float4 out[kPaletteBones * 3]) {
    for (size_t k = 0; k < rows.size() && k < kPaletteBones; ++k)
        for (int r = 0; r < 3; ++r) {
            Float4& o = out[k * 3 + static_cast<size_t>(r)];
            o.x = rows[k][static_cast<size_t>(r) * 4 + 0];
            o.y = rows[k][static_cast<size_t>(r) * 4 + 1];
            o.z = rows[k][static_cast<size_t>(r) * 4 + 2];
            o.w = rows[k][static_cast<size_t>(r) * 4 + 3];
        }
}

}  // namespace

ModulePose::ModulePose(c5m::FileProvider& files, const c5m::StringDatabase* strings)
    : cache_(clipLoaderFromProvider(files, strings)) {}

ModulePose::ModulePose(ClipFileLoader loader) : cache_(std::move(loader)) {}

UnitStatus ModulePose::model(const MemberArt& member, const UnitModel& m, const PoseModel*& out) {
    out = nullptr;
    // 4466: the pair of the last request (every mesh of a figure asks again): the same art by name, no key built
    if (last_pm_ && &member == last_member_ && &m == last_model_ && m.gr2 == last_gr2_ &&
        member.files.fxsxml.name == last_fxs_) {
        out = last_pm_;
        return UnitStatus::Ok();
    }
    const std::string key = m.gr2 + "|" + c5m::basename_lower(member.files.fxsxml.name);
    auto it = models_.find(key);
    if (it != models_.end()) {
        out = it->second.get();
    } else {
        auto f = failed_.find(key);
        if (f != failed_.end()) return UnitStatus::fail(poseErrUnitCode(f->second.code), f->second.msg);
        if (!m.file) {
            failed_[key] = fail(PoseErr::BadModel, key + ": the model has no parsed .gr2");
            return UnitStatus::fail(UnitCode::BadModel, failed_[key].msg);
        }
        std::vector<c5m::FxsAnimation> anims;
        for (const c5m::ResolvedAnimation& ra : member.files.animations) {
            c5m::FxsAnimation fa;
            fa.file = ra.file.name;
            fa.ec_text = ra.ec_text;
            fa.ec = ra.ec;
            fa.name = ra.name;
            anims.push_back(std::move(fa));
        }
        std::unique_ptr<PoseModel> pm(new PoseModel());
        PoseStatus st = buildPoseModel(*m.file, anims, key, *pm);
        if (!st.ok()) {
            failed_[key] = st;
            return UnitStatus::fail(poseErrUnitCode(st.code), st.msg);
        }
        out = pm.get();
        models_[key] = std::move(pm);
    }
    last_member_ = &member;
    last_model_ = &m;
    last_gr2_ = m.gr2;
    last_fxs_ = member.files.fxsxml.name;
    last_pm_ = out;
    return UnitStatus::Ok();
}

PoseStatus ModulePose::clipFor(const PoseModel& pm, const std::string& clip, uint32_t skeleton,
                               const CachedClip*& out) {
    out = nullptr;
    const ClipRef ref{&pm, skeleton, &clip};
    auto it = clips_.find(ref);
    if (it != clips_.end()) {
        out = it->second;
        return out->status;
    }
    PoseStatus st = cache_.get(pm, clip, skeleton, out);
    if (out) clips_.emplace(ClipKey{&pm, skeleton, clip}, out);   // a binding (or a failed one) cache_ keeps
    return st;
}

UnitStatus ModulePose::memoRows(const PoseModel& pm, const PoseRequest& req, const std::string* clip, float t,
                                bool loop, const unitanim::PoseProgram* prog, Float4 out[kPaletteBones * 3]) {
    // the checks of poseFigure (clip) or poseFigureProgram (prog, more than one node), in their order
    PoseStatus st;
    if (prog) {
        st = checkProgram(pm, *prog, req.scale, req.yaw, req.T);
    } else if (!std::isfinite(t) || !std::isfinite(req.scale) || !std::isfinite(req.yaw) || !finite3(req.T)) {
        st = fail(PoseErr::BadInput, pm.key + ": t, scale, yaw or T not finite");
    }
    if (st.ok()) st = checkMesh(pm, req.mesh->index);
    if (!st.ok()) return UnitStatus::fail(poseErrUnitCode(st.code), st.msg);
    const PoseModel::MeshBind& mb = pm.meshes[req.mesh->index];
    if (mb.skeleton < 0) return UnitStatus::Ok();   // no bindings: identity rows, no clip asked (as poseFigure)
    // the skeleton's world transforms: from the memo, else sampled and kept
    const CachedClip* cc = nullptr;
    if (!prog) {
        st = clipFor(pm, *clip, static_cast<uint32_t>(mb.skeleton), cc);
        if (!st.ok()) return UnitStatus::fail(poseErrUnitCode(st.code), st.msg);
    }
    uint32_t tb = 0;
    std::memcpy(&tb, &t, sizeof tb);
    const std::vector<Mat4>* world = nullptr;
    for (const WorldMemo& m : memo_) {
        if (m.pm != &pm || m.skeleton != mb.skeleton || m.program != (prog != nullptr)) continue;
        if (prog ? samePrograms(m.prog, *prog) : (m.clip == cc && m.t_bits == tb && m.loop == loop)) {
            world = &m.world;
            break;
        }
    }
    if (world) {
        ++world_hits_;
    } else {
        std::vector<Mat4> w;
        if (prog) {
            auto get = [&](const std::string& c, uint32_t s, const CachedClip*& o) { return clipFor(pm, c, s, o); };
            st = programWorld(get, pm, *prog, mb.skeleton, w);
        } else {
            st = sampleWorld(pm, mb.skeleton, *cc, t, loop, w);
        }
        if (!st.ok()) return UnitStatus::fail(poseErrUnitCode(st.code), st.msg);
        ++world_samples_;
        WorldMemo& m = memo_[memo_next_];
        memo_next_ = (memo_next_ + 1) % kWorldMemo;
        m.pm = &pm;
        m.skeleton = mb.skeleton;
        m.clip = prog ? nullptr : cc;
        m.t_bits = prog ? 0 : tb;
        m.loop = prog ? false : loop;
        m.program = prog != nullptr;
        if (prog) m.prog = *prog;
        else m.prog.clear();
        m.world = std::move(w);
        world = &m.world;
    }
    std::vector<c5m::Row34> rows;
    paletteOf(pm, mb, *world, req.scale, req.yaw, req.T, rows);
    copyRows(rows, out);
    return UnitStatus::Ok();
}

UnitStatus ModulePose::rows(const PoseRequest& req, Float4 out[kPaletteBones * 3]) {
    identityRows(out);
    if (!req.member || !req.model || !req.mesh)
        return UnitStatus::fail(UnitCode::BadInput, "pose request without member, model or mesh");
    const auto t0 = std::chrono::steady_clock::now();
    const PoseModel* pm = nullptr;
    UnitStatus us = model(*req.member, *req.model, pm);
    if (!us.ok()) return us;
    const std::string* clip = &req.clip;
    if (clip->empty()) {
        const bool fortified = req.unit && (req.unit->flags & kFlagFortified);
        const int32_t ai = defaultClipIndex(*pm, fortified);   // 4466: found once per model, not by name per mesh
        if (ai < 0 || pm->animations[static_cast<size_t>(ai)].file.empty())
            return UnitStatus::fail(UnitCode::ClipNotFound, pm->key + ": no default clip (ec 1000" +
                                                                std::string(fortified ? " or 1520" : "") + ")");
        clip = &pm->animations[static_cast<size_t>(ai)].file;
    }
    us = memoRows(*pm, req, clip, req.t, true, nullptr, out);
    if (!us.ok()) {
        identityRows(out);
        return us;
    }
    sample_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++samples_;
    return UnitStatus::Ok();
}

UnitStatus ModulePose::bindAll(const MemberArt& member, const UnitModel& m) {
    const PoseModel* pm = nullptr;
    UnitStatus us = model(member, m, pm);
    if (!us.ok()) return us;
    std::set<int32_t> skels;
    for (const PoseModel::MeshBind& mb : pm->meshes) {
        if (!mb.unresolved.empty()) return UnitStatus::fail(UnitCode::BadModel, pm->key + ": " + mb.unresolved);
        if (mb.skeleton >= 0) skels.insert(mb.skeleton);
    }
    for (size_t a = 0; a < pm->animations.size(); ++a)
        for (int32_t s : skels) {
            const CachedClip* cc = nullptr;
            PoseStatus st = cache_.get(*pm, pm->animations[a].file, static_cast<uint32_t>(s), cc);
            if (!st.ok()) return UnitStatus::fail(poseErrUnitCode(st.code), st.msg);
        }
    return UnitStatus::Ok();
}

UnitStatus ModulePose::rowsProgram(const PoseRequest& req, const unitanim::PoseProgram& prog,
                                   Float4 out[kPaletteBones * 3]) {
    identityRows(out);
    if (!req.member || !req.model || !req.mesh)
        return UnitStatus::fail(UnitCode::BadInput, "pose request without member, model or mesh");
    const auto t0 = std::chrono::steady_clock::now();
    const PoseModel* pm = nullptr;
    UnitStatus us = model(*req.member, *req.model, pm);
    if (!us.ok()) return us;
    if (prog.nodes.empty()) return UnitStatus::fail(UnitCode::BadInput, pm->key + ": empty pose program");
    // one Sample: poseFigure's path (clamped, the controller has wrapped t); else the program's
    us = prog.single() ? memoRows(*pm, req, &prog.nodes[0].clip, prog.nodes[0].t, false, nullptr, out)
                       : memoRows(*pm, req, nullptr, 0, false, &prog, out);
    if (!us.ok()) {
        identityRows(out);
        return us;
    }
    sample_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++samples_;
    return UnitStatus::Ok();
}

UnitStatus ModulePose::clipDuration(const MemberArt& member, const UnitModel& m, const std::string& clip, float& out) {
    out = 0;
    const PoseModel* pm = nullptr;
    UnitStatus us = model(member, m, pm);
    if (!us.ok()) return us;
    for (const PoseModel::MeshBind& mb : pm->meshes) {
        if (mb.skeleton < 0) continue;
        const CachedClip* cc = nullptr;
        PoseStatus st = cache_.get(*pm, clip, static_cast<uint32_t>(mb.skeleton), cc);
        if (!st.ok()) return UnitStatus::fail(poseErrUnitCode(st.code), st.msg);
        out = cc->duration;
        return UnitStatus::Ok();
    }
    return UnitStatus::fail(UnitCode::BadModel, pm->key + ": no mesh with a skeleton");
}

}}  // namespace c5r::units
