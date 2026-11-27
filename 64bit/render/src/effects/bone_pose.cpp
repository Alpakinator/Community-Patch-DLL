// render/src/effects/bone_pose.cpp - a figure's trigger bone in world units (epic 0042, ticket 4728; c5r/effects/bone_pose.h).
// Own code, GPL-3.0-or-later. Written from the knowledge base only (D83).
#include "c5r/effects/bone_pose.h"

#include <cctype>
#include <cmath>
#include <vector>

#include "c5models/anim.h"
#include "c5models/gr2.h"
#include "c5r/effects/projectile.h"

namespace c5r { namespace effects {

namespace {

using c5m::Mat4;

Mat4 mul4(const Mat4& a, const Mat4& b) {
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

bool sameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// The Sample node a program is posed with: from the last node, at each blend the side of the larger weight.
const unitanim::PoseNode* dominant(const unitanim::PoseProgram& prog, bool& blended) {
    blended = false;
    if (prog.nodes.empty()) return nullptr;
    int32_t i = static_cast<int32_t>(prog.nodes.size()) - 1;
    for (size_t guard = 0; guard <= prog.nodes.size(); ++guard) {
        const unitanim::PoseNode& n = prog.nodes[static_cast<size_t>(i)];
        if (n.kind == unitanim::PoseNode::Sample) return &n;
        blended = true;
        const int32_t next = n.w < 0.5f ? n.a : n.b;
        if (next < 0 || next >= i) return nullptr;   // a node names an earlier node only
        i = next;
    }
    return nullptr;
}

}  // namespace

BonePoser::BonePoser(c5m::FileProvider& files, const c5m::StringDatabase* strings)
    : files_(files), strings_(strings), cache_(units::clipLoaderFromProvider(files, strings)) {}

const units::PoseModel* BonePoser::model(const units::MemberArt& m, std::string& why) {
    const std::string gr2 = c5m::basename_lower(m.files.mesh.name);
    const std::string key = gr2 + "|" + c5m::basename_lower(m.files.fxsxml.name);
    auto it = models_.find(key);
    if (it != models_.end()) return it->second.get();
    auto f = failed_.find(key);
    if (f != failed_.end()) {
        why = f->second;
        return nullptr;
    }
    ++counts_.modelFails;   // taken back below on success
    c5m::Bytes b;
    if (gr2.empty() || !files_.read(gr2, b, nullptr)) {
        why = failed_[key] = key + ": the member's .gr2 is in no source";
        return nullptr;
    }
    c5m::FileInfo info;
    c5m::Error e;
    if (!c5m::read_gr2(b.data(), b.size(), strings_, info, &e)) {
        why = failed_[key] = key + ": " + e.what;
        return nullptr;
    }
    std::vector<c5m::FxsAnimation> anims;
    for (const c5m::ResolvedAnimation& ra : m.files.animations) {
        c5m::FxsAnimation fa;
        fa.file = ra.file.name;
        fa.ec_text = ra.ec_text;
        fa.ec = ra.ec;
        fa.name = ra.name;
        anims.push_back(std::move(fa));
    }
    std::unique_ptr<units::PoseModel> pm(new units::PoseModel());
    const units::PoseStatus st = units::buildPoseModel(info, anims, key, *pm);
    if (!st.ok()) {
        why = failed_[key] = st.str();
        return nullptr;
    }
    --counts_.modelFails;
    ++counts_.models;
    const units::PoseModel* out = pm.get();
    models_[key] = std::move(pm);
    return out;
}

bool BonePoser::boneWorld(const units::MemberArt& m, const unitanim::PoseProgram& prog, const std::string& bone,
                          const FigureFrame& f, float pos[3], float orient[4], std::string* why) {
    ++counts_.asks;
    std::string w;
    auto refuse = [&](const std::string& text) {
        if (why) *why = text;
        return false;
    };
    if (!std::isfinite(f.x) || !std::isfinite(f.y) || !std::isfinite(f.z) || !std::isfinite(f.yaw))
        return refuse("bone " + bone + ": the figure's frame is not finite");
    bool blended = false;
    const unitanim::PoseNode* n = dominant(prog, blended);
    if (!n || !std::isfinite(n->t)) return refuse("bone " + bone + ": an empty or broken pose program");
    if (blended) ++counts_.blends;
    const units::PoseModel* pm = model(m, w);
    if (!pm) return refuse(w);
    // the skeleton that has the bone (the first, in file order)
    int32_t s = -1, bi = -1;
    for (size_t k = 0; k < pm->skeletons.size() && bi < 0; ++k)
        for (size_t j = 0; j < pm->skeletons[k].bones.size(); ++j)
            if (sameNoCase(pm->skeletons[k].bones[j].name, bone)) {
                s = static_cast<int32_t>(k);
                bi = static_cast<int32_t>(j);
                break;
            }
    if (bi < 0) {
        ++counts_.noBone;
        return refuse(pm->key + ": no bone " + bone);
    }
    const units::CachedClip* cc = nullptr;
    const units::PoseStatus cs = cache_.get(*pm, n->clip, static_cast<uint32_t>(s), cc);
    if (!cs.ok() || !cc) {
        ++counts_.clipFails;
        return refuse(cs.str());
    }
    // the unit module's sampling (pose.cpp sampleWorld): tracks in the clip's basis brought to the stock unit basis; the
    // controller has wrapped or clamped t already (a program's Sample is clamped, as rowsProgram does)
    const c5m::Skeleton& sk = pm->skeletons[static_cast<size_t>(s)];
    bool conj = !cc->basis.identity;
    Mat4 cm{}, cmi{};
    if (conj) {
        double a[9], ai[9];
        for (int i = 0; i < 9; ++i) a[i] = cc->basis.m[i];
        if (!inv3(a, ai)) {
            ++counts_.clipFails;
            return refuse(pm->key + ": clip " + n->clip + ": its basis is not invertible");
        }
        float fi[9];
        for (int i = 0; i < 9; ++i) fi[i] = static_cast<float>(ai[i]);
        cm = fromBasis(cc->basis.m);
        cmi = fromBasis(fi);
    }
    const float tc = c5m::clip_time(n->t, cc->duration, false);
    std::vector<Mat4> local(sk.bones.size()), world;
    for (size_t b = 0; b < sk.bones.size(); ++b) {
        const int32_t ti = b < cc->bound.bone_track.size() ? cc->bound.bone_track[b] : -1;
        if (ti < 0) {
            local[b] = pm->rest[static_cast<size_t>(s)][b];
            continue;
        }
        const c5m::ClipTrack& tr = cc->bound.tracks[static_cast<size_t>(ti)];
        float p[3], q[4], ss[9];
        c5m::eval_curve(tr.position, tc, false, p);
        c5m::eval_curve(tr.orientation, tc, true, q);
        c5m::eval_curve(tr.scale_shear, tc, false, ss);
        const Mat4 l = c5m::compose_transform(p, q, ss);
        local[b] = conj ? mul4(mul4(cm, l), cmi) : l;
    }
    c5m::AnimError ae;
    if (!c5m::world_from_local(sk, local, world, &ae) || static_cast<size_t>(bi) >= world.size()) {
        ++counts_.clipFails;
        return refuse(pm->key + ": " + c5m::anim_err_name(ae.code) + ": " + ae.what);
    }
    const Mat4& W = world[static_cast<size_t>(bi)];   // column convention, row-major: W[r * 4 + c]
    const float sc = std::isfinite(f.scale) && f.scale > 0 ? f.scale : 1.0f;
    const float c = std::cos(f.yaw), sn = std::sin(f.yaw);
    const float Rz[9] = {c, -sn, 0, sn, c, 0, 0, 0, 1};
    // the point: s Rz p + T (the palette's rule)
    for (int r = 0; r < 3; ++r) {
        float v = 0;
        for (int j = 0; j < 3; ++j) v += Rz[r * 3 + j] * W[j * 4 + 3];
        pos[r] = sc * v;
    }
    pos[0] += f.x;
    pos[1] += f.y;
    pos[2] += f.z;
    // the orientation: Rz R with R's columns normalised
    float R[9];
    for (int col = 0; col < 3; ++col) {
        float l = 0;
        for (int r = 0; r < 3; ++r) l += W[r * 4 + col] * W[r * 4 + col];
        l = std::sqrt(l);
        for (int r = 0; r < 3; ++r) R[r * 3 + col] = l > 1e-12f ? W[r * 4 + col] / l : (r == col ? 1.0f : 0.0f);
    }
    float G[9];
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) {
            float v = 0;
            for (int j = 0; j < 3; ++j) v += Rz[r * 3 + j] * R[j * 3 + col];
            G[r * 3 + col] = v;
        }
    // rotationQuaternion takes the row-vector form (rows = the axes): M = G^T
    M4 M;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) M.m[i * 4 + j] = G[j * 3 + i];
    rotationQuaternion(M, orient);
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(pos[k])) return refuse(pm->key + ": bone " + bone + ": not finite");
    for (int k = 0; k < 4; ++k)
        if (!std::isfinite(orient[k])) return refuse(pm->key + ": bone " + bone + ": not finite");
    ++counts_.ok;
    return true;
}

}}  // namespace c5r::effects
