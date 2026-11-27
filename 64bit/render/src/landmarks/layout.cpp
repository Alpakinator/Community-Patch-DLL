// layout.cpp - layers 1 and 2 of c5r::landmarks: the mesh key, half floats, one .gr2 laid out and repacked as the
// EXE uploads it (ticket 4872, epic 0561). Own code from the knowledge base's description (D83): city-renderer.md 2.2,
// landmark-renderer.md 2 (the layout read from the 3907/3910 captures' buffers).
#include "c5r/landmarks/library.h"

#include <cmath>
#include <cstring>

#include "c5models/unit_mesh.h"

namespace c5r { namespace landmarks {

const char* className(ModelClass c) {
    switch (c) {
    case ModelClass::CityBlock: return "CityBlock";
    case ModelClass::CityPiece: return "CityPiece";
    case ModelClass::Landmark: return "Landmark";
    default: return "Unknown";
    }
}

bool MeshKey::operator<(const MeshKey& o) const {
    if (indexHash != o.indexHash) return indexHash < o.indexHash;
    if (vertexHash != o.vertexHash) return vertexHash < o.vertexHash;
    if (firstIndex != o.firstIndex) return firstIndex < o.firstIndex;
    if (indexCount != o.indexCount) return indexCount < o.indexCount;
    return baseVertex < o.baseVertex;
}

size_t MeshKeyHash::operator()(const MeshKey& k) const {
    uint64_t h = k.indexHash ^ (k.vertexHash * kFnvPrime);
    h = (h ^ k.firstIndex) * kFnvPrime;
    h = (h ^ k.indexCount) * kFnvPrime;
    h = (h ^ static_cast<uint32_t>(k.baseVertex)) * kFnvPrime;
    return static_cast<size_t>(h);
}

uint64_t fnv1a64(const void* data, size_t bytes, uint64_t h) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}

Status meshKey(const void* indices, size_t count, uint32_t indexBytes, uint32_t hashFrom, const void* vertices,
               size_t vertexBytes, uint32_t firstIndex, uint32_t indexCount, int32_t baseVertex, MeshKey& out) {
    out = MeshKey();
    if (indexBytes != 2 && indexBytes != 4)
        return Status::fail(Err::BadLayout, "index size " + std::to_string(indexBytes) + " (2 or 4)");
    if (indexCount == 0) return Status::fail(Err::BadLayout, "a draw of 0 indices");
    if (!indices || static_cast<uint64_t>(firstIndex) + indexCount > count)
        return Status::fail(Err::BadLayout, "range " + std::to_string(firstIndex) + " + " +
                                                std::to_string(indexCount) + " past the buffer's " +
                                                std::to_string(count) + " indices");
    if (hashFrom > count) return Status::fail(Err::BadLayout, "hash start past the index buffer");
    if (!vertices || vertexBytes == 0) return Status::fail(Err::BadLayout, "no vertex bytes");
    out.indexHash = fnv1a64(static_cast<const uint8_t*>(indices) + static_cast<size_t>(hashFrom) * indexBytes,
                            (count - hashFrom) * indexBytes);
    out.vertexHash = fnv1a64(vertices, vertexBytes);
    out.firstIndex = firstIndex;
    out.indexCount = indexCount;
    out.baseVertex = baseVertex;
    return Status::Ok();
}

// ---------------------------------------------------------------------------------------------- layout
uint32_t vertexStride(VertexForm f) {
    switch (f) {
    case VertexForm::Float32: return 32;
    case VertexForm::Half20: return 20;
    case VertexForm::Skinned28: return 28;
    }
    return 0;
}

const char* formName(VertexForm f) {
    switch (f) {
    case VertexForm::Float32: return "Float32";
    case VertexForm::Half20: return "Half20";
    case VertexForm::Skinned28: return "Skinned28";
    }
    return "?";
}

namespace {

bool real32(const c5m::Mesh& m, const char* name, uint32_t lo, uint32_t hi) {
    const c5m::VertexComponent* c = m.format.find(name);
    return c && c->type == c5m::MemberType::Real32 && c->count >= lo && c->count <= hi &&
           c->offset + c->count * 4 <= m.format.stride;
}

const c5m::Material* groupMaterial(const c5m::FileInfo& fi, const c5m::Mesh& m, const c5m::TriGroup& g) {
    if (g.material >= m.materials.size() || m.materials[g.material] < 0 ||
        static_cast<size_t>(m.materials[g.material]) >= fi.materials.size())
        return nullptr;
    return &fi.materials[static_cast<size_t>(m.materials[g.material])];
}

}  // namespace

bool packedMesh(const c5m::FileInfo& fi, const c5m::Mesh& m) {
    if (m.groups.empty()) return false;
    for (const c5m::TriGroup& g : m.groups) {
        const c5m::Material* mat = groupMaterial(fi, m, g);
        if (!mat || mat->shader_set.compare(0, 14, "BuildingShader") != 0) return false;
    }
    return true;
}


namespace {

bool skinnedMesh(const c5m::FileInfo& fi, const c5m::Mesh& m) {
    for (const c5m::TriGroup& g : m.groups) {
        const c5m::Material* mat = groupMaterial(fi, m, g);
        if (mat && mat->shader_set == "BuildingShader_Skinned") return true;
    }
    return false;
}

}  // namespace

std::vector<uint32_t> meshOrder(const c5m::FileInfo& fi) {
    std::vector<uint32_t> order;
    std::vector<bool> seen(fi.meshes.size(), false);
    for (const c5m::ModelEntry& me : fi.models)
        for (int32_t k : me.meshes)
            if (k >= 0 && static_cast<size_t>(k) < fi.meshes.size() && !seen[static_cast<size_t>(k)]) {
                seen[static_cast<size_t>(k)] = true;
                order.push_back(static_cast<uint32_t>(k));
            }
    for (uint32_t k = 0; k < fi.meshes.size(); ++k)
        if (!seen[k]) order.push_back(k);
    return order;
}

Status layoutFile(const c5m::FileInfo& fi, bool cityBlock, float scale, FileLayout& out) {
    out = FileLayout();
    if (!(scale > 0.0f) || !std::isfinite(scale)) return Status::fail(Err::BadLayout, "scale not finite and > 0");
    FileLayout lay;
    lay.scale = scale;
    lay.form = cityBlock ? VertexForm::Float32 : VertexForm::Half20;
    uint64_t rigid = 0, skin = 0, i0 = kLeadIndices;
    for (uint32_t mi : meshOrder(fi)) {
        const c5m::Mesh& m = fi.meshes[mi];
        if (!packedMesh(fi, m)) {
            lay.unpacked.push_back(m.name);
            continue;
        }
        const std::string who = "mesh " + m.name;
        const bool sk = skinnedMesh(fi, m);
        if (sk && cityBlock) return Status::fail(Err::Unsupported, who + ": a skinned mesh in a city set");
        if (cityBlock &&
            (!real32(m, "Position", 3, 3) || !real32(m, "Normal", 3, 3) || !real32(m, "TextureCoordinates0", 2, 4)))
            return Status::fail(Err::Unsupported, who + " lacks Real32 Position:3, Normal:3 or TextureCoordinates0");
        if (m.indices.empty() || m.indices.size() % 3 != 0)
            return Status::fail(Err::Unsupported, who + " has " + std::to_string(m.indices.size()) +
                                                      " indices (a non-empty triangle list)");
        for (uint32_t ix : m.indices)
            if (ix >= m.vertex_count)
                return Status::fail(Err::Unsupported, who + ": index " + std::to_string(ix) + " out of range");
        // The index size is the EXE's, not the file's (ticket 4878, city-renderer.md 12.2 / 12.4 [C, 3910 Lanc18-21]): a
        // mesh stored with 32-bit indices ("Indices") whose every index fits 16 bits is drawn with R16 indices; R32
        // only when an index needs it (schloss_hb's mesh of more than 65,536 vertices, drawn R32 in Lanc20, which
        // repack_unit_mesh refuses anyway). Was: R32 for every file with a 32-bit mesh (38 files, 151 draws missed).
        for (uint32_t ix : m.indices)
            if (ix > 0xFFFFu) lay.indexBytes = 4;
        const uint64_t v0 = sk ? skin : rigid;
        for (uint32_t gi = 0; gi < m.groups.size(); ++gi) {
            const c5m::TriGroup& g = m.groups[gi];
            if (g.tri_count == 0 || (static_cast<uint64_t>(g.tri_first) + g.tri_count) * 3 > m.indices.size())
                return Status::fail(Err::Unsupported, who + ": tri group " + std::to_string(gi) +
                                                          " empty or past the mesh's indices");
            const c5m::Material* mat = groupMaterial(fi, m, g);
            if (!mat) return Status::fail(Err::BadLayout, who + "'s material is not in the file");
            Part p;
            p.fileMesh = mi;
            p.group = gi;
            p.skinned = sk;
            p.mesh = m.name;
            p.material = mat->name;
            p.materialIndex = static_cast<uint32_t>(m.materials[g.material]);
            p.shaderSet = mat->shader_set;
            for (const c5m::TextureMap& t : mat->maps) {
                p.textures.emplace_back(t.usage, t.basename);
                if (t.usage == "Diffuse" && p.t0.empty()) p.t0 = t.basename;
                if (t.usage == "BuildingSREF" && p.t1.empty()) p.t1 = t.basename;
            }
            p.vertexCount = m.vertex_count;
            p.key.firstIndex = static_cast<uint32_t>(i0 + static_cast<uint64_t>(g.tri_first) * 3);
            p.key.indexCount = g.tri_count * 3;
            p.key.baseVertex = static_cast<int32_t>(v0);
            lay.parts.push_back(p);
        }
        (sk ? skin : rigid) += m.vertex_count;
        i0 += m.indices.size();
        if (rigid > 0x7FFFFFFFu || skin > 0x7FFFFFFFu || i0 > 0xFFFFFFFFu)
            return Status::fail(Err::Unsupported, "file too large");
    }
    if (lay.parts.empty()) return Status::fail(Err::Unsupported, "no BuildingShader mesh in the file");
    lay.vertexCount = static_cast<uint32_t>(rigid);
    lay.skinnedVertexCount = static_cast<uint32_t>(skin);
    lay.indexCount = static_cast<uint32_t>(i0);
    out = lay;
    return Status::Ok();
}

namespace {

// Float32: the file's basis at scale 1 (repack_unit_mesh's step 2 in float32), no halves.
void repackFloat32(const c5m::Mesh& m, const c5m::UnitBasis& b, uint8_t* out) {
    for (uint32_t v = 0; v < m.vertex_count; ++v) {
        float p[4], n[4], t[4];
        m.read(v, "Position", p);
        m.read(v, "Normal", n);
        m.read(v, "TextureCoordinates0", t);
        const float* L = b.L;
        const float* I = b.I;
        float f[8];
        f[0] = b.A[0] + ((L[0] * p[0] + L[1] * p[1]) + L[2] * p[2]);
        f[1] = b.A[1] + ((L[3] * p[0] + L[4] * p[1]) + L[5] * p[2]);
        f[2] = b.A[2] + ((L[6] * p[0] + L[7] * p[1]) + L[8] * p[2]);
        f[3] = (n[1] * I[3] + n[2] * I[6]) + n[0] * I[0];
        f[4] = (n[1] * I[4] + n[2] * I[7]) + I[1] * n[0];
        f[5] = (n[2] * I[8] + I[2] * n[0]) + I[5] * n[1];
        f[6] = t[0];
        f[7] = t[1];
        std::memcpy(out + static_cast<size_t>(v) * 32, f, sizeof f);
    }
}

}  // namespace

Status repackFile(const c5m::FileInfo& fi, const FileLayout& lay, FileBuffers& out) {
    out = FileBuffers();
    const uint32_t stride = vertexStride(lay.form);
    if (lay.form == VertexForm::Skinned28 || stride == 0)
        return Status::fail(Err::BadLayout, "the rigid set's form must be Float32 or Half20");
    c5m::UnitBasis basis;
    std::string err;
    if (!c5m::unit_basis(fi, lay.form == VertexForm::Float32 ? 1.0f : lay.scale, basis, &err))
        return Status::fail(Err::BadLayout, "basis: " + err);
    FileBuffers b;
    b.vertices.assign(static_cast<size_t>(lay.vertexCount) * stride, 0);
    b.skinnedVertices.assign(static_cast<size_t>(lay.skinnedVertexCount) * 28, 0);
    b.indices.assign(static_cast<size_t>(lay.indexCount) * lay.indexBytes, 0);
    size_t rigid = 0, skin = 0, i0 = kLeadIndices;
    int32_t lastMesh = -1;
    for (const Part& p : lay.parts) {
        if (static_cast<int32_t>(p.fileMesh) == lastMesh) continue;   // one copy per mesh, every group's range in it
        lastMesh = static_cast<int32_t>(p.fileMesh);
        if (p.fileMesh >= fi.meshes.size()) return Status::fail(Err::BadLayout, "layout does not fit the file");
        const c5m::Mesh& m = fi.meshes[p.fileMesh];
        size_t& v0 = p.skinned ? skin : rigid;
        const size_t cap = p.skinned ? lay.skinnedVertexCount : lay.vertexCount;
        if (v0 + m.vertex_count > cap || i0 + m.indices.size() > lay.indexCount ||
            static_cast<int32_t>(v0) != p.key.baseVertex)
            return Status::fail(Err::BadLayout, "layout does not fit mesh " + m.name);
        if (lay.form == VertexForm::Float32 && !p.skinned) {
            repackFloat32(m, basis, &b.vertices[v0 * 32]);
        } else {
            c5m::UnitMesh um;
            if (!c5m::repack_unit_mesh(m, basis, p.skinned, um, &err))
                return Status::fail(Err::Unsupported, "mesh " + m.name + ": " + err);
            const size_t s = p.skinned ? 28u : 20u;
            if (um.vertex_count != m.vertex_count || um.vertices.size() != static_cast<size_t>(m.vertex_count) * s)
                return Status::fail(Err::BadLayout, "mesh " + m.name + ": the unit repack's size differs");
            std::memcpy(p.skinned ? &b.skinnedVertices[v0 * 28] : &b.vertices[v0 * 20], um.vertices.data(),
                        um.vertices.size());
        }
        for (size_t i = 0; i < m.indices.size(); ++i) {
            const uint32_t ix = m.indices[i];
            if (lay.indexBytes == 2) {
                const uint16_t s16 = static_cast<uint16_t>(ix);
                std::memcpy(&b.indices[(i0 + i) * 2], &s16, 2);
            } else {
                std::memcpy(&b.indices[(i0 + i) * 4], &ix, 4);
            }
        }
        v0 += m.vertex_count;
        i0 += m.indices.size();
    }
    if (rigid != lay.vertexCount || skin != lay.skinnedVertexCount || i0 != lay.indexCount)
        return Status::fail(Err::BadLayout, "layout counts differ");
    out = std::move(b);
    return Status::Ok();
}

}}  // namespace c5r::landmarks
