// cpumesh.cpp - the CPU-built meshes' store and resolve (ticket 4879, epic 0561; c5r/landmarks/cpumesh.h,
// research/05-graphics-composition/city-renderer.md 13). Written 2026-11-18 for this repository (GPL-3.0-or-later) from
// the knowledge base's description (D83; no EXE code).
#include "c5r/landmarks/cpumesh.h"

#include "shell_model_tap.h"

namespace c5r { namespace landmarks {

const char* geomKindName(GeomKind k) { return k == GeomKind::Index32 ? "index32" : "vertex"; }

uint64_t geometryHash(GeomKind k, const void* data, size_t bytes) {
    if (!data) return 0;
    if (k == GeomKind::Index32) return bytes >= 8 ? VPModelHash64(static_cast<const uint8_t*>(data) + 4, bytes - 4) : 0;
    return bytes ? VPModelHash64(data, bytes) : 0;
}

Status CpuMeshStore::add(GeomKind kind, uint64_t hash, uint32_t stride, const uint8_t* data, size_t bytes) {
    if (has(kind, hash)) return Status::Ok();
    if (bytes > kMaxBytes) {
        ++counts_.refusedOversize;
        return Status::fail(Err::Unsupported, std::string(geomKindName(kind)) + " buffer of " + std::to_string(bytes) +
                                                  " bytes over the " + std::to_string(kMaxBytes) + "-byte bound (oversize)");
    }
    const bool layoutOk = kind == GeomKind::Index32 ? (bytes >= 8 && bytes % 4 == 0)
                                                    : ((stride == 20 || stride == 32) && bytes && bytes % stride == 0);
    if (!data || !layoutOk) {
        ++counts_.refusedLayout;
        return Status::fail(Err::BadLayout, std::string(geomKindName(kind)) + " buffer: " + std::to_string(bytes) +
                                                " bytes, stride " + std::to_string(stride) + " not a layout this store takes");
    }
    if (!hash || geometryHash(kind, data, bytes) != hash) {
        ++counts_.refusedHash;
        return Status::fail(Err::BadLayout, std::string(geomKindName(kind)) +
                                                " buffer: its content differs from its hash (written after the copy)");
    }
    CpuBuffer& b = held_[{static_cast<uint8_t>(kind), hash}];
    b.kind = kind;
    b.hash = hash;
    b.stride = kind == GeomKind::Index32 ? 4u : stride;
    b.bytes.assign(data, data + bytes);
    bytes_ += bytes;
    ++generation_;
    ++counts_.added;
    return Status::Ok();
}

const CpuBuffer* CpuMeshStore::get(GeomKind kind, uint64_t hash) const {
    auto it = held_.find({static_cast<uint8_t>(kind), hash});
    return it == held_.end() ? nullptr : &it->second;
}

bool CpuMeshStore::remove(GeomKind kind, uint64_t hash) {
    auto it = held_.find({static_cast<uint8_t>(kind), hash});
    if (it == held_.end()) return false;
    bytes_ -= it->second.bytes.size();
    held_.erase(it);
    ++generation_;
    ++counts_.removed;
    return true;
}

void CpuMeshStore::clear() {
    if (held_.empty()) return;
    counts_.removed += held_.size();
    held_.clear();
    bytes_ = 0;
    ++generation_;
}

std::vector<std::pair<GeomKind, uint64_t>> CpuMeshStore::list() const {
    std::vector<std::pair<GeomKind, uint64_t>> out;
    out.reserve(held_.size());
    for (const auto& kv : held_) out.emplace_back(kv.second.kind, kv.second.hash);
    return out;
}

uint32_t addCpuTextureNames(WireIndex& index) {
    uint32_t n = 0;
    for (const char* t : {"bridges_diff.dds", "bridges_sref.dds"}) n += index.textureId(t) ? 1u : 0u;
    return n;
}

bool cpuMaterialFits(Material m, uint32_t vertexStride) {
    return (m == Material::Blocks && vertexStride == 32) || (m == Material::Single && vertexStride == 20);
}

Status resolveCpu(const CpuMeshStore& store, const WireIndex& index, const TapFrame& f, ModelScene& out,
                  CpuResolveReport* report) {
    CpuResolveReport rep;
    C5R_TRY(checkFrame(f));
    out.cpuItems.clear();
    std::vector<uint32_t> keepUnknown;
    std::vector<bool> done(f.draws.size(), false), byFile(f.draws.size(), false);
    for (const DrawItem& it : out.items)   // a file holds it (an R32 file layout): resolve() drew it already
        if (it.tapDraw < byFile.size()) byFile[it.tapDraw] = true;
    for (uint32_t i = 0; i < f.draws.size(); ++i) {
        const TapDraw& d = f.draws[i];
        if (!(d.key.flags & kWireIndex32) || (d.material != Material::Blocks && d.material != Material::Single)) continue;
        if (byFile[i]) continue;
        ++rep.r32;
        const CpuBuffer* ib = store.get(GeomKind::Index32, d.key.ib);
        const CpuBuffer* vb = store.get(GeomKind::Vertex, d.key.vb);
        if (!ib || !vb) {
            ++rep.noGeometry;
            continue;
        }
        if (!cpuMaterialFits(d.material, vb->stride)) {
            ++rep.badStride;
            continue;
        }
        const uint64_t end = static_cast<uint64_t>(d.key.startIndex) + d.key.indexCount;
        if (!d.key.indexCount || end > ib->bytes.size() / 4)
            return Status::fail(Err::BadLayout, "tap draw " + std::to_string(i) + ": its range ends at index " +
                                                    std::to_string(end) + ", past its held index buffer of " +
                                                    std::to_string(ib->bytes.size() / 4));
        CpuDrawItem it;
        it.t0 = index.textureName(d.key.t0);
        it.t1 = index.textureName(d.key.t1);
        if ((d.key.t0 && it.t0.empty()) || (d.key.t1 && it.t1.empty())) {
            ++rep.textureUnnamed;
            continue;
        }
        it.key = d.key;
        it.material = d.material;
        it.target = d.target;
        it.instanceCount = d.instanceCount;
        it.instRow = d.instRow;
        it.vsCb1 = d.vsCb1;
        it.psCb1 = d.psCb1;
        for (int k = 0; k < 3; ++k) it.env[k] = d.env[k];
        it.tapDraw = i;
        it.vertexStride = vb->stride;
        out.cpuItems.push_back(it);
        done[i] = true;
        ++rep.resolved;
    }
    for (uint32_t u : out.unknown)
        if (u >= done.size() || !done[u]) keepUnknown.push_back(u);
    out.unknownDraws -= static_cast<uint32_t>(out.unknown.size() - keepUnknown.size());
    out.unknown.swap(keepUnknown);
    if (report) *report = rep;
    return Status::Ok();
}

}}  // namespace c5r::landmarks
