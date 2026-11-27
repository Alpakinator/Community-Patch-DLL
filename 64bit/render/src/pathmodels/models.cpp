// models.cpp - layer 2 of c5r::pathmodels: the 18 path assets read from the install with 0041's model reader
// (c5models, used read-only), and the draw list (ticket 0300). Own code from the knowledge base's description (D83):
// arrows-and-movement-path.md 2.1 and 2.4, path-models.md 2-4.
#include "c5r/pathmodels/pathmodels.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"

namespace c5r { namespace pathmodels {

namespace {

Status bad(Err e, const std::string& asset, const std::string& what) {
    return Status::fail(e, asset + ": " + what);
}

const char* const kMaps[3] = {"BaseTextureMap", "SREFMap", "IrradianceCubeMap"};

bool component(const c5m::Mesh& m, const char* name, uint32_t minCount, uint32_t maxCount) {
    const c5m::VertexComponent* c = m.format.find(name);
    return c && c->type == c5m::MemberType::Real32 && c->count >= minCount && c->count <= maxCount &&
           c->offset + c->count * 4 <= m.format.stride;
}

}  // namespace

Status repackMesh(const c5m::FileInfo& fi, const c5m::Mesh& m, PathMesh& out) {
    out = PathMesh();
    const std::string asset = "mesh repack";
    if (!component(m, "Position", 3, 3) || !component(m, "Normal", 3, 3) ||
        !component(m, "TextureCoordinates0", 2, 4))
        return bad(Err::Unsupported, asset, "mesh " + m.name +
                                                " lacks Real32 Position:3, Normal:3 or TextureCoordinates0 (2-4)");
    if (m.vertex_count == 0 || m.vertex_count > 65536)
        return bad(Err::Unsupported, asset, "mesh " + m.name + " has " + std::to_string(m.vertex_count) +
                                                " vertices (1 .. 65,536 fit 16-bit indices)");
    if (m.vertices.size() != static_cast<size_t>(m.vertex_count) * m.format.stride)
        return bad(Err::BadLayout, asset, "mesh " + m.name + "'s vertex bytes do not match count x stride");
    if (m.indices.empty() || m.indices.size() % 3 != 0)
        return bad(Err::Unsupported, asset, "mesh " + m.name + " has " + std::to_string(m.indices.size()) +
                                                " indices (a non-empty triangle list)");
    if (m.groups.size() != 1)
        return bad(Err::Unsupported, asset, "mesh " + m.name + " has " + std::to_string(m.groups.size()) +
                                                " material groups (one draw per mesh needs one)");
    const c5m::TriGroup& g = m.groups[0];
    if (g.material >= m.materials.size() || m.materials[g.material] < 0 ||
        static_cast<size_t>(m.materials[g.material]) >= fi.materials.size())
        return bad(Err::Unsupported, asset, "mesh " + m.name + "'s material is not in the file");
    const c5m::Material& mat = fi.materials[static_cast<size_t>(m.materials[g.material])];
    if (mat.shader_set != "UIShader")
        return bad(Err::Unsupported, asset, "mesh " + m.name + "'s material " + mat.name + " has shader set '" +
                                                mat.shader_set + "', not UIShader");
    for (int k = 0; k < 3; ++k) {
        std::string found;
        for (const c5m::TextureMap& tm : mat.maps)
            if (tm.usage == kMaps[k]) {
                found = tm.basename;
                break;
            }
        // SelectedAniAttack's centre material has no SREFMap (path-models.md 2.2): t1 stays empty and the renderer binds
        // its stand-in (black, alpha 1) [I: what the EXE binds there is not captured]
        if (found.empty() && k == 1) continue;
        if (found.empty())
            return bad(Err::Unsupported, asset, "material " + mat.name + " has no " + kMaps[k]);
        out.texture[k] = found;
    }
    out.name = m.name;
    out.vertexCount = m.vertex_count;
    out.vertices.assign(static_cast<size_t>(m.vertex_count) * kVertexStride, 0);
    for (uint32_t v = 0; v < m.vertex_count; ++v) {
        float p[4], n[4], t[4];
        m.read(v, "Position", p);
        m.read(v, "Normal", n);
        m.read(v, "TextureCoordinates0", t);
        const float f[8] = {p[0], p[1], p[2], n[0], n[1], n[2], t[0], t[1]};
        std::memcpy(&out.vertices[static_cast<size_t>(v) * kVertexStride], f, sizeof f);
    }
    out.indices.resize(m.indices.size());
    for (size_t i = 0; i < m.indices.size(); ++i) {
        if (m.indices[i] >= m.vertex_count)
            return bad(Err::Unsupported, asset, "mesh " + m.name + ": index " + std::to_string(m.indices[i]) +
                                                    " out of range");
        out.indices[i] = static_cast<uint16_t>(m.indices[i]);
    }
    return Status::Ok();
}

namespace {

Status readOne(const FileReader& read, const std::string& name, std::vector<uint8_t>& out) {
    Status s = read(c5m::ascii_lower(name), out);
    if (!s.ok()) return Status::fail(s.code == Err::Ok ? Err::Io : s.code, name + ": " + s.msg);
    return Status::Ok();
}

Status loadAsset(const FileReader& read, Asset a, PathAsset& out) {
    const std::string name = assetName(a);
    std::vector<uint8_t> bytes;
    C5R_TRY(readOne(read, name + ".fxsxml", bytes));
    c5m::FxsAsset fx;
    c5m::Error e;
    if (!c5m::parse_fxsxml(reinterpret_cast<const char*>(bytes.data()), bytes.size(), fx, &e))
        return bad(Err::BadLayout, name, std::string(".fxsxml: ") + c5m::err_name(e.code) + " " + e.what);
    if (fx.mesh_count != 1 || fx.mesh.empty())
        return bad(Err::BadLayout, name, ".fxsxml names " + std::to_string(fx.mesh_count) + " meshes, not one");
    C5R_TRY(readOne(read, c5m::basename_lower(fx.mesh), bytes));
    c5m::FileInfo fi;
    if (!c5m::read_gr2(bytes.data(), bytes.size(), nullptr, fi, &e))
        return bad(Err::BadLayout, name, fx.mesh + ": " + c5m::err_name(e.code) + " " + e.what);
    out = PathAsset();
    out.name = name;
    out.skeletons = fi.skeletons;
    out.sourceMeshes = fi.meshes;
    std::vector<bool> used(fi.meshes.size(), false);
    for (const c5m::ModelEntry& model : fi.models) {
        for (int32_t mi : model.meshes) {
            if (mi < 0 || static_cast<size_t>(mi) >= fi.meshes.size()) continue;
            const c5m::Mesh& m = fi.meshes[static_cast<size_t>(mi)];
            PathMesh pm;
            Status rs = repackMesh(fi, m, pm);
            if (!rs.ok()) return Status::fail(rs.code, name + ": " + rs.msg);
            if (model.skeleton < 0 || static_cast<size_t>(model.skeleton) >= fi.skeletons.size())
                return bad(Err::BadLayout, name, "mesh " + m.name + "'s model has no skeleton");
            const c5m::Skeleton& sk = fi.skeletons[static_cast<size_t>(model.skeleton)];
            if (m.bone_bindings.empty() || sk.find(m.bone_bindings[0].bone_name) < 0)
                return bad(Err::BadLayout, name, "mesh " + m.name + "'s first bone binding is no bone of skeleton " +
                                                     sk.name);
            pm.skeleton = model.skeleton;
            pm.bone = sk.find(m.bone_bindings[0].bone_name);
            used[static_cast<size_t>(mi)] = true;
            out.meshes.push_back(std::move(pm));
        }
    }
    for (size_t i = 0; i < used.size(); ++i)
        if (!used[i]) return bad(Err::BadLayout, name, "mesh " + fi.meshes[i].name + " is bound by no model");
    if (out.meshes.empty()) return bad(Err::BadLayout, name, "no mesh");
    if (!fx.animations.empty()) {
        C5R_TRY(readOne(read, c5m::basename_lower(fx.animations[0].file), bytes));
        c5m::FileInfo af;
        if (!c5m::read_gr2(bytes.data(), bytes.size(), nullptr, af, &e))
            return bad(Err::BadLayout, name, fx.animations[0].file + ": " + c5m::err_name(e.code) + " " + e.what);
        if (af.animations.empty()) return bad(Err::Unsupported, name, fx.animations[0].file + " holds no animation");
        out.animated = true;
        out.duration = af.animations[0].duration;
        if (!(out.duration > 0.0f) || !std::isfinite(out.duration))
            return bad(Err::Unsupported, name, "the clip's duration is not positive");
        for (const c5m::Skeleton& sk : out.skeletons) {
            c5m::BoundClip clip;
            c5m::AnimError ae;
            if (!c5m::bind_clip_all_groups(af, 0, sk, clip, &ae))
                return bad(Err::Unsupported, name, "clip on skeleton " + sk.name + ": " +
                                                       c5m::anim_err_name(ae.code) + " " + ae.what);
            out.clips.push_back(std::move(clip));
        }
    }
    return Status::Ok();
}

}  // namespace

Status PathModels::load(const FileReader& read) {
    assets_.clear();
    std::vector<PathAsset> all(kAssetCount);
    for (uint32_t i = 0; i < kAssetCount; ++i) C5R_TRY(loadAsset(read, static_cast<Asset>(i), all[i]));
    assets_.swap(all);
    return Status::Ok();
}

FileReader gameFilesReader(const gamefs::GameFiles& files) {
    const gamefs::GameFiles* f = &files;
    return [f](const std::string& name, std::vector<uint8_t>& out) -> Status {
        if (!f->exists(name)) return Status::fail(Err::NotFound, "not in the game's files");
        return f->read(name, out);
    };
}

Status PathModels::load(const gamefs::GameFiles& files) { return load(gameFilesReader(files)); }

Status PathModels::meshMatrix(Asset a, uint32_t mesh, float t, Mat4& out) const {
    out = Mat4();
    const size_t ai = static_cast<size_t>(a);
    if (ai >= assets_.size()) return Status::fail(Err::BadLayout, "asset index out of range");
    const PathAsset& pa = assets_[ai];
    if (mesh >= pa.meshes.size()) return Status::fail(Err::BadLayout, pa.name + ": mesh index out of range");
    if (!pa.animated) return Status::Ok();
    const PathMesh& pm = pa.meshes[mesh];
    const c5m::Skeleton& sk = pa.skeletons[static_cast<size_t>(pm.skeleton)];
    const c5m::BoundClip& clip = pa.clips[static_cast<size_t>(pm.skeleton)];
    std::vector<c5m::Mat4> local, world;
    c5m::sample_local(clip, sk, c5m::clip_time(t, pa.duration, true), local);
    c5m::AnimError ae;
    if (!c5m::world_from_local(sk, local, world, &ae))
        return Status::fail(Err::BadLayout, pa.name + ": " + c5m::anim_err_name(ae.code) + " " + ae.what);
    // The rigid skin row of the mesh's bone (World x InverseWorldTransform, column vectors), transposed to row vectors.
    c5m::Mat4 iw;
    const float* s = sk.bones[static_cast<size_t>(pm.bone)].inverse_world;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) iw[r * 4 + c] = s[c * 4 + r];
    const c5m::Mat4& w = world[static_cast<size_t>(pm.bone)];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) v += w[i * 4 + k] * iw[k * 4 + j];
            out.m[j * 4 + i] = v;   // transpose: column-vector (i, j) -> row-vector (j, i)
        }
    return Status::Ok();
}

std::vector<std::string> textureNames(const PathModels& m) {
    std::set<std::string> s;
    if (m.loaded())
        for (uint32_t a = 0; a < kAssetCount; ++a)
            for (const PathMesh& pm : m.asset(static_cast<Asset>(a)).meshes)
                for (int k = 0; k < 3; ++k)
                    if (!pm.texture[k].empty()) s.insert(pm.texture[k]);
    return std::vector<std::string>(s.begin(), s.end());
}

Status buildDraws(const PathModels& m, const std::vector<Instance>& instances, float animTime,
                  std::vector<DrawItem>& out) {
    out.clear();
    if (!m.loaded()) return Status::fail(Err::NotFound, "path models not loaded");
    if (!std::isfinite(animTime)) return Status::fail(Err::BadLayout, "the animation time is not finite");
    std::vector<DrawItem> v;
    for (const Instance& in : instances) {
        if (static_cast<uint32_t>(in.asset) >= kAssetCount)
            return Status::fail(Err::BadLayout, "instance asset out of range");
        const PathAsset& pa = m.asset(in.asset);
        for (uint32_t k = 0; k < pa.meshes.size(); ++k) {
            DrawItem d;
            d.asset = in.asset;
            d.mesh = k;
            d.indexCount = static_cast<uint32_t>(pa.meshes[k].indices.size());
            Mat4 mm;
            C5R_TRY(m.meshMatrix(in.asset, k, animTime, mm));
            d.world = mul(mm, in.world);
            v.push_back(d);
        }
    }
    out.swap(v);
    return Status::Ok();
}

}}  // namespace c5r::pathmodels
