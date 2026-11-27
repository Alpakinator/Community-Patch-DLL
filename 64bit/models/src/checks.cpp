// checks.cpp - geometry checks over a read file (epic 3100, issue 3101), own code, 2026-11-11.
#include "c5models/gr2.h"

#include <cmath>
#include <unordered_map>

namespace c5m {

static bool finite3(const float v[4]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

bool check_geometry(const FileInfo& f, GeometryReport& rep, Error* err) {
    rep = GeometryReport();
    // mesh -> skeleton of the model that binds it (first binding wins)
    std::unordered_map<int32_t, int32_t> mesh_skel;
    for (const auto& m : f.models)
        for (int32_t mi : m.meshes)
            if (mi >= 0 && m.skeleton >= 0) mesh_skel.emplace(mi, m.skeleton);
    auto bad = [&](const Mesh& m, const std::string& what) {
        if (rep.violations.size() < 200) rep.violations.push_back("mesh '" + m.name + "': " + what);
    };
    for (size_t mi = 0; mi < f.meshes.size(); ++mi) {
        const Mesh& m = f.meshes[mi];
        rep.meshes++;
        rep.vertices += m.vertex_count;
        rep.triangles += (uint32_t)(m.indices.size() / 3);
        // Bug 0093 V3 (issue 9025): when the bytes do not match, the per-vertex checks below are not run (they
        // would read past the buffer); the index and group checks need no vertex bytes and still run.
        bool vertex_bytes_ok = m.vertices.size() == (size_t)m.vertex_count * m.format.stride;
        if (!vertex_bytes_ok)
            bad(m, "vertex bytes " + std::to_string(m.vertices.size()) + " != count x stride");
        if (m.indices.size() % 3) bad(m, "index count " + std::to_string(m.indices.size()) + " not a multiple of 3");
        uint32_t over = 0;
        for (uint32_t ix : m.indices)
            if (ix >= m.vertex_count) ++over;
        if (over) bad(m, std::to_string(over) + " indices >= vertex count " + std::to_string(m.vertex_count));
        for (const auto& g : m.groups) {
            if ((uint64_t)(g.tri_first + (uint64_t)g.tri_count) * 3 > m.indices.size())
                bad(m, "group past the index buffer");
            if (g.material >= m.materials.size() && !m.materials.empty()) bad(m, "group material index past bindings");
        }
        bool has_pos = m.format.find("Position") != nullptr;
        bool has_nrm = m.format.find("Normal") != nullptr;
        const VertexComponent* bw = m.format.find("BoneWeights");
        const VertexComponent* bi = m.format.find("BoneIndices");
        // Bug 0093 V2 (issue 9025): weights and indices are read as bytes, up to 4 of each; they must lie inside
        // the vertex (a vertex type of 0 bytes that still names them once made this a read through a null pointer)
        for (const VertexComponent* c : {bw, bi})
            if (c && (uint64_t)c->offset + (c->count < 4 ? c->count : 4) > m.format.stride) {
                bad(m, c->name + " outside the vertex (offset " + std::to_string(c->offset) + ", stride " +
                           std::to_string(m.format.stride) + ")");
                bw = bi = nullptr;
            }
        uint32_t nonfinite_pos = 0, nonfinite_nrm = 0, weight_sum_bad = 0, bone_index_bad = 0;
        for (uint32_t v = 0; vertex_bytes_ok && v < m.vertex_count; ++v) {
            float p[4] = {0, 0, 0, 0};
            if (has_pos && m.read(v, "Position", p) && !finite3(p)) ++nonfinite_pos;
            if (has_nrm && m.read(v, "Normal", p) && !finite3(p)) ++nonfinite_nrm;
            if (bw && bi) {
                const uint8_t* w = m.vertices.data() + (size_t)v * m.format.stride + bw->offset;
                const uint8_t* ix = m.vertices.data() + (size_t)v * m.format.stride + bi->offset;
                uint32_t sum = 0;
                for (uint32_t k = 0; k < bw->count && k < 4; ++k) {
                    sum += w[k];
                    // Bug 0093 V9 (issue 4000): BoneIndices may have fewer components than BoneWeights (u8x2 against
                    // u8x4 read up to 2 B past each vertex, past the buffer at the last one); only its own are read
                    if (k < bi->count && w[k] && ix[k] >= m.bone_bindings.size()) ++bone_index_bad;
                }
                if (sum < 253 || sum > 257) ++weight_sum_bad;
            }
        }
        if (nonfinite_pos) bad(m, std::to_string(nonfinite_pos) + " vertices with a non-finite position");
        if (nonfinite_nrm) bad(m, std::to_string(nonfinite_nrm) + " vertices with a non-finite normal");
        if (weight_sum_bad) bad(m, std::to_string(weight_sum_bad) + " vertices whose bone weights do not sum to 1");
        if (bone_index_bad) bad(m, std::to_string(bone_index_bad) + " weighted bone indices past the bone bindings");
        auto it = mesh_skel.find((int32_t)mi);
        if (it != mesh_skel.end() && it->second < (int32_t)f.skeletons.size()) {
            const Skeleton& sk = f.skeletons[it->second];
            for (const auto& b : m.bone_bindings)
                if (sk.find(b.bone_name) < 0) bad(m, "bone binding '" + b.bone_name + "' not in skeleton '" + sk.name + "'");
        }
    }
    for (const auto& sk : f.skeletons)
        for (size_t i = 0; i < sk.bones.size(); ++i)
            if (sk.bones[i].parent >= (int32_t)i || sk.bones[i].parent < -1)
                rep.violations.push_back("skeleton '" + sk.name + "': bone " + std::to_string(i) +
                                         " parent index " + std::to_string(sk.bones[i].parent));
    if (!rep.violations.empty()) {
        if (err) *err = Error{Err::GeometryCheck, rep.violations[0]};
        return false;
    }
    return true;
}

}  // namespace c5m
