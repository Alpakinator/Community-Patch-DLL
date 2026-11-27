// model.cpp - L1 of the unit module: a member's .gr2 to the EXE's vertex streams, indices and textures, kept in a
// model cache with their GPU buffers (epic 0041, issue 4352; research/05-graphics-composition/unit-renderer.md 6.2
// and 10.2; unit-playback.md 3 and 6). Own code; the vertex rule is models' repack_unit_mesh with the EXE's basis
// conversion (unit-renderer.md 10.9, ticket 0447).
#include "c5r/units/draw.h"

#include <cmath>
#include <cstring>
#include <algorithm>

#include "c5models/unit_mesh.h"
#include "c5r/gamefs/dds.h"
#include "c5r/units/pose.h"

namespace c5r { namespace units {

// model_params.cpp (4356)
bool materialTextureParams(const uint8_t* data, size_t size, const c5m::StringDatabase* strings,
                           std::vector<std::vector<std::pair<std::string, std::string>>>& out, std::string* why);

namespace {

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

const c5m::VertexComponent* need(const c5m::Mesh& m, const char* name, c5m::MemberType t, uint32_t n) {
    const c5m::VertexComponent* c = m.format.find(name);
    if (!c || c->type != t || c->count < n) return nullptr;
    return c;
}

// A Real32 or Real16 component of at least n elements (the load rule reads both, unit-renderer.md 10.9).
const c5m::VertexComponent* real(const c5m::Mesh& m, const char* name, uint32_t n) {
    const c5m::VertexComponent* c = m.format.find(name);
    if (!c || c->count < n || (c->type != c5m::MemberType::Real32 && c->type != c5m::MemberType::Real16)) return nullptr;
    return c;
}

}  // namespace

// applyBasis, widenReal16 and positiveZeros were the load path's steps before ticket 0447 (the FBX turn, Real16
// sources widened, a Real16 -0 made +0); the EXE's basis conversion (unit-renderer.md 10.9, models' unit_basis and
// repack_unit_mesh) does all three, and ModelCache no longer calls them. Kept as tools (draw.h).
// Position and Normal times a 3 x 3 matrix (row-major, column vectors), in float32 (premise P4, unit-renderer.md 10.2).
bool applyBasis(c5m::Mesh& m, const float M[9]) {
    const c5m::VertexComponent* pos = need(m, "Position", c5m::MemberType::Real32, 3);
    const c5m::VertexComponent* nrm = need(m, "Normal", c5m::MemberType::Real32, 3);
    if (!pos) return false;
    const uint32_t stride = m.format.stride;
    if (static_cast<uint64_t>(m.vertex_count) * stride != m.vertices.size()) return false;
    for (uint32_t v = 0; v < m.vertex_count; ++v) {
        for (const c5m::VertexComponent* c : {pos, nrm}) {
            if (!c || c->offset + 12u > stride) continue;
            uint8_t* p = m.vertices.data() + static_cast<size_t>(v) * stride + c->offset;
            float f[3];
            std::memcpy(f, p, 12);
            float r[3];
            for (int i = 0; i < 3; ++i) r[i] = M[3 * i] * f[0] + M[3 * i + 1] * f[1] + M[3 * i + 2] * f[2];
            std::memcpy(p, r, 12);
        }
    }
    return true;
}

// A mesh whose Normal or TextureCoordinates0 are Real16 (several DLC unique units; unit-renderer.md 10.2) as the
// same mesh with Real32 Position[3], BoneWeights, BoneIndices, Normal[3], TextureCoordinates0[2]: the halves widened
// exactly. False when a component is missing or of another type.
bool widenReal16(const c5m::Mesh& in, c5m::Mesh& out) {
    const c5m::VertexComponent* pos = need(in, "Position", c5m::MemberType::Real32, 3);
    const c5m::VertexComponent* bw = in.format.find("BoneWeights");
    const c5m::VertexComponent* bi = in.format.find("BoneIndices");
    const c5m::VertexComponent* nrm = in.format.find("Normal");
    const c5m::VertexComponent* uv = in.format.find("TextureCoordinates0");
    auto real = [](const c5m::VertexComponent* c, uint32_t n) {
        return c && c->count >= n && (c->type == c5m::MemberType::Real32 || c->type == c5m::MemberType::Real16);
    };
    if (!pos || !real(nrm, 3) || !real(uv, 2)) return false;
    if ((bw && (bw->type != c5m::MemberType::NormalUInt8 || bw->count < 4)) ||
        (bi && (bi->type != c5m::MemberType::UInt8 || bi->count < 4)) || (!bw != !bi))
        return false;
    const uint32_t stride = in.format.stride;
    if (static_cast<uint64_t>(in.vertex_count) * stride != in.vertices.size()) return false;
    auto end = [&](const c5m::VertexComponent* c, uint32_t n) {
        return c->offset + n * (c->type == c5m::MemberType::Real16 ? 2u : 4u) <= stride;
    };
    if (!end(pos, 3) || !end(nrm, 3) || !end(uv, 2) || (bw && bw->offset + 4u > stride) || (bi && bi->offset + 4u > stride))
        return false;
    out = in;
    out.format = c5m::VertexFormat();
    auto add = [&](c5m::MemberType t, const char* name, uint32_t count, uint32_t size) {
        c5m::VertexComponent c;
        c.type = t;
        c.name = name;
        c.count = count;
        c.offset = out.format.stride;
        out.format.components.push_back(c);
        out.format.stride += size;
    };
    add(c5m::MemberType::Real32, "Position", 3, 12);
    if (bw) {
        add(c5m::MemberType::NormalUInt8, "BoneWeights", 4, 4);
        add(c5m::MemberType::UInt8, "BoneIndices", 4, 4);
    }
    add(c5m::MemberType::Real32, "Normal", 3, 12);
    add(c5m::MemberType::Real32, "TextureCoordinates0", 2, 8);
    const uint32_t os = out.format.stride;
    out.vertices.assign(static_cast<size_t>(in.vertex_count) * os, 0);
    auto get = [](const uint8_t* p, const c5m::VertexComponent* c, uint32_t k) {
        if (c->type == c5m::MemberType::Real32) {
            float f;
            std::memcpy(&f, p + c->offset + 4 * k, 4);
            return f;
        }
        uint16_t h;
        std::memcpy(&h, p + c->offset + 2 * k, 2);
        return static_cast<float>(c5m::double_from_half(h));
    };
    for (uint32_t v = 0; v < in.vertex_count; ++v) {
        const uint8_t* s = in.vertices.data() + static_cast<size_t>(v) * stride;
        uint8_t* d = out.vertices.data() + static_cast<size_t>(v) * os;
        std::memcpy(d, s + pos->offset, 12);
        size_t o = 12;
        if (bw) {
            std::memcpy(d + o, s + bw->offset, 4);
            std::memcpy(d + o + 4, s + bi->offset, 4);
            o += 8;
        }
        for (uint32_t k = 0; k < 3; ++k) {
            const float f = get(s, nrm, k);
            std::memcpy(d + o + 4 * k, &f, 4);
        }
        for (uint32_t k = 0; k < 2; ++k) {
            const float f = get(s, uv, k);
            std::memcpy(d + o + 12 + 4 * k, &f, 4);
        }
    }
    return true;
}

// -0.0 in Position and Normal becomes +0.0. Applied to Real16-sourced meshes only: there the EXE stores a -0 normal
// as +0 (0x0000, not 0x8000) in all 9 catalogue cases, while Real32 meshes keep -0 (applying it to them broke 2
// byte-exact meshes) [L, unit-renderer.md 10.2]. In place.
void positiveZeros(c5m::Mesh& m) {
    const uint32_t stride = m.format.stride;
    if (static_cast<uint64_t>(m.vertex_count) * stride != m.vertices.size()) return;
    for (const char* name : {"Position", "Normal"}) {
        const c5m::VertexComponent* c = need(m, name, c5m::MemberType::Real32, 3);
        if (!c || c->offset + 12u > stride) continue;
        for (uint32_t v = 0; v < m.vertex_count; ++v)
            for (uint32_t k = 0; k < 3; ++k) {
                uint8_t* p = m.vertices.data() + static_cast<size_t>(v) * stride + c->offset + 4 * k;
                uint32_t bits;
                std::memcpy(&bits, p, 4);
                if (bits == 0x80000000u) std::memset(p, 0, 4);
            }
    }
}

namespace {

std::string stem(const std::string& n) {
    const size_t dot = n.rfind('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}

// The EXE's load rule (unit-renderer.md 10.9, ticket 0447): models' repack_unit_mesh with the file's basis, into the
// 28-byte stream (skinned) or the 20-byte one. Since 0447 the rule equals the EXE's vertex buffer on all 515 drawn
// meshes of the catalogue, the former weight ties included, so ModelMesh::tie_vertices stays empty.
UnitStatus repackWith(const c5m::Mesh& mesh, const c5m::UnitBasis& basis, bool skinned, ModelMesh& out) {
    if (skinned && mesh.bone_bindings.size() > kPaletteBones)
        return UnitStatus::fail(UnitCode::TooManyBones, "mesh " + mesh.name + " binds " +
                                std::to_string(mesh.bone_bindings.size()) + " bones (palette block: 32)");
    c5m::UnitMesh um;
    std::string err;
    if (!c5m::repack_unit_mesh(mesh, basis, skinned, um, &err)) {
        const bool layout = err.find("needs") != std::string::npos;
        return UnitStatus::fail(layout ? UnitCode::UnknownLayout : UnitCode::BadModel, err);
    }
    out.name = mesh.name;
    out.stride = skinned ? kSkinnedStride : kStaticStride;
    out.vertex_count = um.vertex_count;
    out.vertices = std::move(um.vertices);
    out.indices = std::move(um.indices);
    out.bones = um.bones;
    out.tie_vertices.clear();
    return UnitStatus::Ok();
}

// The basis of a file in the stock 3ds Max ArtToolInfo, or VP's FBX one when y_up (the two kinds of unit files).
UnitStatus basisOf(const c5m::Mesh& mesh, float scale, bool y_up, c5m::UnitBasis& b) {
    std::string err;
    const bool ok = y_up ? c5m::unit_basis_fbx(scale, b, &err) : c5m::unit_basis_stock(scale, b, &err);
    if (!ok) return UnitStatus::fail(UnitCode::BadModel, "mesh " + mesh.name + ": " + err);
    return UnitStatus::Ok();
}

}  // namespace

// The stream follows the bone bindings as the EXE's does (two or more: the 28-byte stream, unit-renderer.md 10.9 step
// 2; in the catalogue the same split as "has BoneWeights" [L]); a mesh with two or more bindings but no NormalUInt8
// BoneWeights / UInt8 BoneIndices is refused (what the EXE draws for it is not seen [I]).
UnitStatus meshMaterial(const c5m::FileInfo& f, const c5m::Mesh& mesh, Material& out) {
    const bool skinned = c5m::unit_mesh_skinned(mesh);
    const bool base = real(mesh, "Position", 3) && real(mesh, "Normal", 3) && real(mesh, "TextureCoordinates0", 2);
    if (!skinned) {
        if (!base)
            return UnitStatus::fail(UnitCode::UnknownLayout, "mesh " + mesh.name + ": layout " + mesh.format.key() +
                                    " is neither the 28-byte nor the 20-byte stream's source");
        out = Material::Static;
        return UnitStatus::Ok();
    }
    if (!base || !need(mesh, "BoneWeights", c5m::MemberType::NormalUInt8, 4) ||
        !need(mesh, "BoneIndices", c5m::MemberType::UInt8, 4))
        return UnitStatus::fail(UnitCode::UnknownLayout, "mesh " + mesh.name + ": skinned layout " + mesh.format.key() +
                                " is not the 28-byte stream's source");
    out = Material::Skinned;
    for (int32_t mi : mesh.materials) {
        if (mi < 0 || static_cast<size_t>(mi) >= f.materials.size()) continue;
        const c5m::Material& mat = f.materials[static_cast<size_t>(mi)];
        if (lower(mat.shader_set).find("alpha") != std::string::npos) out = Material::SkinnedAlpha;
    }
    return UnitStatus::Ok();
}

UnitStatus repackSkinned(const c5m::Mesh& mesh, float scale, bool y_up, ModelMesh& out) {
    if (mesh.bone_bindings.size() > kPaletteBones)
        return UnitStatus::fail(UnitCode::TooManyBones, "mesh " + mesh.name + " binds " +
                                std::to_string(mesh.bone_bindings.size()) + " bones (palette block: 32)");
    c5m::UnitBasis b;
    UnitStatus st = basisOf(mesh, scale, y_up, b);
    if (!st.ok()) return st;
    return repackWith(mesh, b, true, out);
}

UnitStatus repackStatic(const c5m::Mesh& mesh, float scale, bool y_up, ModelMesh& out) {
    c5m::UnitBasis b;
    UnitStatus st = basisOf(mesh, scale, y_up, b);
    if (!st.ok()) return st;
    return repackWith(mesh, b, false, out);
}

UnitStatus loadStringDatabases(const std::vector<std::string>& names, c5m::FileProvider& files,
                               c5m::StringDatabase& out) {
    for (auto& n : names) {
        if (n.size() < 4 || lower(n.substr(n.size() - 4)) != ".gsd") continue;
        c5m::Bytes b;
        std::string origin;
        if (!files.read(lower(n), b, &origin)) return UnitStatus::fail(UnitCode::BadModel, "string database " + n + " unreadable");
        c5m::Error e;
        if (!out.add_gsd(b.data(), b.size(), &e))
            return UnitStatus::fail(UnitCode::BadModel, "string database " + n + " (" + origin + "): " + e.what);
    }
    return UnitStatus::Ok();
}

// ---------------------------------------------------------------------------------------------- the cache
ModelCache::~ModelCache() { clear(); }

void ModelCache::clear() {
    for (auto& kv : models_)
        for (auto& m : kv.second->meshes) {
            if (m.vb) m.vb->Release();
            if (m.ib) m.ib->Release();
            m.vb = nullptr;
            m.ib = nullptr;
        }
    for (auto& kv : textures_) {
        if (kv.second->srv) kv.second->srv->Release();
        if (kv.second->resource) kv.second->resource->Release();
    }
    models_.clear();
    failed_.clear();
    textures_.clear();
}

UnitStatus ModelCache::texture(const std::string& name, const UnitTexture*& out) {
    out = nullptr;
    const std::string key = c5m::basename_lower(name);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        out = it->second.get();
        return UnitStatus::Ok();
    }
    std::unique_ptr<UnitTexture> t(new UnitTexture());
    t->name = key;
    c5m::Bytes b;
    std::string origin;
    std::string tried = key;
    if (!files_.read(key, b, &origin)) {
        // art names a source file (.tga, .psd) where the archives hold the .dds of that stem
        size_t dot = key.rfind('.');
        std::string dds = (dot == std::string::npos ? key : key.substr(0, dot)) + ".dds";
        tried += dds != key ? " or " + dds : "";
        if (dds == key || !files_.read(dds, b, &origin))
            return UnitStatus::fail(UnitCode::TextureNotFound, "texture " + tried + " not found");
        t->name = dds;
    }
    t->file.assign(b.begin(), b.end());
    gamefs::DdsImage img;
    Status s = gamefs::parseDds(t->file.data(), t->file.size(), img);
    if (!s.ok()) return UnitStatus::fail(UnitCode::TextureNotFound, "texture " + t->name + " (" + origin + "): " + s.str());
    t->width = img.width;
    t->height = img.height;
    t->mips = img.mipLevels;
    t->faces = img.arraySize;
    t->format = img.format;
    for (auto& sr : img.subresources)
        t->payload.insert(t->payload.end(), img.pixels() + sr.offset, img.pixels() + sr.offset + sr.size);
    if (img.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        // the EXE uploads A8R8G8B8 files as R8G8B8A8 (unit_irradiance.dds, fogramp.dds; unit-renderer.md 10.3)
        for (size_t i = 0; i + 3 < t->payload.size(); i += 4) std::swap(t->payload[i], t->payload[i + 2]);
        t->format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    out = t.get();
    textures_[key] = std::move(t);
    return UnitStatus::Ok();
}

UnitStatus ModelCache::build(const MemberArt& member, UnitModel& m) {
    const std::string gr2 = c5m::basename_lower(member.files.mesh.name);
    m.gr2 = gr2;
    m.scale = member.scale;
    c5m::Bytes b;
    std::string origin;
    if (gr2.empty() || !files_.read(gr2, b, &origin))
        return UnitStatus::fail(UnitCode::ModelNotFound, "model " + (gr2.empty() ? member.model : gr2) + " not found");
    m.file.reset(new c5m::FileInfo());
    c5m::Error e;
    if (!c5m::read_gr2(b.data(), b.size(), strings_, *m.file, &e))
        return UnitStatus::fail(UnitCode::BadModel, gr2 + " (" + origin + "): " + std::string(c5m::err_name(e.code)) +
                                ": " + e.what);
    std::vector<std::vector<std::pair<std::string, std::string>>> params;
    std::string pwhy;
    if (!materialTextureParams(b.data(), b.size(), strings_, params, &pwhy)) params.clear();   // then maps decide
    // The EXE's basis conversion of the file at this member's Scale (unit-renderer.md 10.9, ticket 0447): the file's
    // ArtToolInfo axes and UnitsPerMeter give L (the FBX files' turn (x, y, z) -> (x, -z, y) included) and its
    // cofactor inverse for the normals. The pose layer turns the skeletons by pose.h's fileBasis, axes only (P4);
    // y_up names the files whose axes are not the stock ones.
    PoseStatus bst;
    const Basis axes = fileBasis(*m.file, &bst);
    if (!bst.ok()) return UnitStatus::fail(UnitCode::BadModel, gr2 + ": " + bst.str());
    m.y_up = !axes.identity;
    c5m::UnitBasis basis;
    {
        std::string berr;
        if (!c5m::unit_basis(*m.file, member.scale, basis, &berr))
            return UnitStatus::fail(UnitCode::BadModel, gr2 + ": " + berr);
    }
    // Mesh order (issue 4355, 2026-11-15): the file's first Model's MeshBindings order, then any mesh it does not bind
    // in file order. The EXE numbers a figure's blocks (and issues its draws) in that order: VP's FBX cavalry files
    // list the rider mesh first in Meshes but bind the horse first (or the other way round), and the chariot archer
    // binds driver, archer, chariot while its Meshes list starts with the chariot (unit-renderer.md 13).
    std::vector<size_t> order;
    {
        std::vector<bool> taken(m.file->meshes.size(), false);
        if (!m.file->models.empty())
            for (int32_t mi : m.file->models[0].meshes)
                if (mi >= 0 && static_cast<size_t>(mi) < taken.size() && !taken[static_cast<size_t>(mi)]) {
                    taken[static_cast<size_t>(mi)] = true;
                    order.push_back(static_cast<size_t>(mi));
                }
        for (size_t i = 0; i < taken.size(); ++i)
            if (!taken[i]) order.push_back(i);
    }
    for (size_t i : order) {
        const c5m::Mesh& src = m.file->meshes[i];   // Real16 normals and coordinates are read as they are (10.9)
        const std::string ln = lower(src.name);
        if (ln.find("dmg") != std::string::npos || ln.find("_damaged") != std::string::npos ||
            (ln.size() > 4 && ln.compare(ln.size() - 3, 3, "dam") == 0 && (ln[ln.size() - 4] == '_' || ln[ln.size() - 4] == ' '))) {
            m.skipped.push_back(src.name + ": damage state");
            continue;
        }
        if (src.vertex_count == 0 || src.indices.empty()) {
            m.skipped.push_back(src.name + ": no vertices or indices");
            continue;
        }
        ModelMesh mm;
        UnitStatus st = meshMaterial(*m.file, src, mm.material);
        if (!st.ok()) return UnitStatus::fail(st.code, gr2 + ": " + st.text);
        st = repackWith(src, basis, mm.material != Material::Static, mm);
        if (!st.ok()) return UnitStatus::fail(st.code, gr2 + ": " + st.text);
        mm.index = static_cast<uint32_t>(i);
        // textures (unit-renderer.md 10.8): the material's FireGrafix UnitShaderTextures parameters (BaseTextureMap
        // -> t0, SREFMap -> t1); else the 3ds Max maps of those usages; else the .fxsxml's texture of the role ("sref"
        // in the name -> t1, the first other -> t0)
        std::vector<std::string> fxs;
        std::string fxsBase, fxsSref;
        for (auto& ft : member.files.textures) {
            const std::string name = stem(c5m::basename_lower(ft.name));
            fxs.push_back(name);
            if (name.find("sref") != std::string::npos) {
                if (fxsSref.empty()) fxsSref = c5m::basename_lower(ft.name);
            } else if (fxsBase.empty()) {
                fxsBase = c5m::basename_lower(ft.name);
            }
        }
        for (int32_t mi : src.materials) {
            if (mi < 0 || static_cast<size_t>(mi) >= m.file->materials.size()) continue;
            for (auto& map : m.file->materials[static_cast<size_t>(mi)].maps) {
                const std::string u = lower(map.usage);
                const std::string name = !map.basename.empty() ? map.basename : c5m::basename_lower(map.file);
                if (name.empty()) continue;
                if ((u == "basetexturemap" || u == "diffuse") && mm.base_texture.empty()) mm.base_texture = name;
                if ((u == "sreftexturemap" || u == "srefmap" || u == "sref") && mm.sref_texture.empty())
                    mm.sref_texture = name;
            }
        }
        auto listed = [&](const std::string& n) {
            return fxs.empty() || std::find(fxs.begin(), fxs.end(), stem(n)) != fxs.end();
        };
        // the material's FireGrafix UnitShaderTextures parameters decide when they name a texture (4356, 10.8)
        std::string pBase, pSref;
        for (int32_t mi : src.materials)
            if (mi >= 0 && static_cast<size_t>(mi) < params.size())
                for (auto& p : params[static_cast<size_t>(mi)]) {
                    const std::string name = c5m::basename_lower(p.second);
                    if (name.empty()) continue;
                    if (p.first == "BaseTextureMap" && pBase.empty()) pBase = name;
                    if (p.first == "SREFMap" && pSref.empty()) pSref = name;
                }
        // order: the parameters, else the 3ds Max maps, else the .fxsxml's texture of the role (no catalogue mesh
        // reaches the last two: every unit material carries the parameters [L, 10.8])
        (void)listed;
        if (!pBase.empty()) mm.base_texture = pBase;
        else if (mm.base_texture.empty() && !fxsBase.empty()) mm.base_texture = fxsBase;
        if (!pSref.empty()) mm.sref_texture = pSref;
        else if (mm.sref_texture.empty() && !fxsSref.empty()) mm.sref_texture = fxsSref;
        if (mm.base_texture.empty()) {
            std::string usages;
            for (int32_t mi : src.materials) {
                if (mi < 0 || static_cast<size_t>(mi) >= m.file->materials.size()) continue;
                const c5m::Material& mat = m.file->materials[static_cast<size_t>(mi)];
                usages += " [" + mat.name + " set '" + mat.shader_set + "' tex '" + mat.texture_file + "'";
                for (auto& map : mat.maps) usages += " " + map.usage + "=" + map.file;
                usages += "]";
            }
            return UnitStatus::fail(UnitCode::TextureNotFound, gr2 + ": mesh " + src.name +
                                    " names no BaseTextureMap; materials:" + usages + "; fxsxml textures " +
                                    std::to_string(member.files.textures.size()));
        }
        // a texture in no file binds the EXE's fallback (named in m.fallbacks); one that exists but does not parse
        // is TextureNotFound
        auto bind = [&](const std::string& name, const UnitTexture*& slot) -> UnitStatus {
            UnitStatus ts = texture(name, slot);
            if (ts.ok()) return ts;
            const size_t dot = name.rfind('.');
            const std::string dds = (dot == std::string::npos ? name : name.substr(0, dot)) + ".dds";
            std::string o;
            if (files_.exists(c5m::basename_lower(name), &o) || files_.exists(c5m::basename_lower(dds), &o)) return ts;
            slot = fallbackTexture();
            m.fallbacks.push_back("TextureFallback: " + gr2 + " " + src.name + ": " + name);
            return UnitStatus::Ok();
        };
        st = bind(mm.base_texture, mm.t0);
        if (!st.ok()) return UnitStatus::fail(st.code, gr2 + ": mesh " + src.name + ": " + st.text);
        if (!mm.sref_texture.empty()) {
            st = bind(mm.sref_texture, mm.t1);
            if (!st.ok()) return UnitStatus::fail(st.code, gr2 + ": mesh " + src.name + ": " + st.text);
        }
        m.meshes.push_back(std::move(mm));
    }
    if (m.meshes.empty()) return UnitStatus::fail(UnitCode::BadModel, gr2 + ": no mesh to draw");
    return UnitStatus::Ok();
}

UnitStatus ModelCache::get(const MemberArt& member, const UnitModel*& out) {
    out = nullptr;
    uint32_t bits;
    std::memcpy(&bits, &member.scale, 4);
    const std::string key = c5m::basename_lower(member.files.mesh.name) + "|" + std::to_string(bits);
    auto f = failed_.find(key);
    if (f != failed_.end()) return f->second;
    auto it = models_.find(key);
    if (it != models_.end()) {
        out = it->second.get();
        return UnitStatus::Ok();
    }
    std::unique_ptr<UnitModel> m(new UnitModel());
    UnitStatus st = build(member, *m);
    if (!st.ok()) {
        failed_[key] = st;
        return st;
    }
    out = m.get();
    models_[key] = std::move(m);
    return UnitStatus::Ok();
}

const UnitTexture* ModelCache::fallbackTexture() {
    auto it = textures_.find(kFallbackTexture);
    if (it != textures_.end()) return it->second.get();
    std::unique_ptr<UnitTexture> t(new UnitTexture());
    t->name = kFallbackTexture;
    t->width = t->height = 8;
    t->mips = 1;
    t->format = DXGI_FORMAT_R8G8B8A8_UNORM;
    t->payload.assign(8 * 8 * 4, 0);
    t->payload[0] = 255;   // texel 0 = (255, 0, 0, 255), the rest 0 (unit-renderer.md 10.8)
    t->payload[3] = 255;
    const UnitTexture* p = t.get();
    textures_[kFallbackTexture] = std::move(t);
    return p;
}

Status ModelCache::upload(ID3D11Device* dev) {
    if (!dev) return Status::fail(Err::Device, "no device");
    for (auto& kv : textures_) {
        UnitTexture& t = *kv.second;
        if (t.srv) continue;
        if (t.file.empty()) {   // made in code (the fallback): payload as R8G8B8A8, one mip
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = t.width;
            d.Height = t.height;
            d.MipLevels = d.ArraySize = 1;
            d.Format = t.format;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_IMMUTABLE;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init = {t.payload.data(), t.width * 4, 0};
            ID3D11Texture2D* tex = nullptr;
            HRESULT hr = dev->CreateTexture2D(&d, &init, &tex);
            if (FAILED(hr)) return Status::fail(Err::Device, "fallback texture: " + std::to_string(hr));
            t.resource = tex;
            hr = dev->CreateShaderResourceView(tex, nullptr, &t.srv);
            if (FAILED(hr)) return Status::fail(Err::Device, "fallback texture view: " + std::to_string(hr));
            continue;
        }
        gamefs::DdsImage img;
        C5R_TRY(gamefs::parseDds(t.file.data(), t.file.size(), img));
        C5R_TRY(gamefs::createTexture(dev, img, &t.resource, &t.srv));
    }
    for (auto& kv : models_)
        for (auto& m : kv.second->meshes) {
            if (m.vb) continue;
            D3D11_BUFFER_DESC d = {};
            d.Usage = D3D11_USAGE_IMMUTABLE;
            d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            d.ByteWidth = static_cast<UINT>(m.vertices.size());
            D3D11_SUBRESOURCE_DATA init = {m.vertices.data(), 0, 0};
            HRESULT hr = dev->CreateBuffer(&d, &init, &m.vb);
            if (FAILED(hr)) return Status::fail(Err::Device, "vertex buffer of " + m.name + ": " + std::to_string(hr));
            d.BindFlags = D3D11_BIND_INDEX_BUFFER;
            d.ByteWidth = static_cast<UINT>(m.indices.size() * 2);
            init.pSysMem = m.indices.data();
            hr = dev->CreateBuffer(&d, &init, &m.ib);
            if (FAILED(hr)) return Status::fail(Err::Device, "index buffer of " + m.name + ": " + std::to_string(hr));
        }
    return Status::Ok();
}

}}  // namespace c5r::units
