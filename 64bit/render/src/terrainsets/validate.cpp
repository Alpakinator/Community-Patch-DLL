// render/src/terrainsets/validate.cpp - checks a terrain-side shader-set description against the EXE's blobs
// (task 2903, epic 2900). The rules are listed in c5r/terrainsets/terrainsets.h.
#include "c5r/terrainsets/terrainsets.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"

namespace c5r { namespace terrainsets {

namespace {

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

uint32_t memberBytes(const CbMember& m) {
    if (m.count <= 1) return memberTypeSize(m.type);
    // arrays: 16-byte stride per element, the last element takes its own size
    const uint32_t elem = memberTypeSize(m.type);
    const uint32_t stride = elem < 16 ? 16 : elem;
    return stride * (m.count - 1) + elem;
}

const CbBinding* findBinding(const ShaderSetDesc& set, uint32_t slot) {
    for (const CbBinding& b : set.cbuffers)
        if (b.slot == slot) return &b;
    return nullptr;
}

const TexSlot* findTex(const std::vector<TexSlot>& v, uint32_t slot) {
    for (const TexSlot& t : v)
        if (t.slot == slot) return &t;
    return nullptr;
}

// the member covering byte `at` of the layout, nullptr if none
const CbMember* memberAt(const CbLayout& l, uint32_t at) {
    for (const CbMember& m : l.members) {
        if (at < m.offset || at >= m.offset + memberBytes(m)) continue;
        if (m.count > 1) {
            const uint32_t elem = memberTypeSize(m.type);
            const uint32_t stride = elem < 16 ? 16 : elem;
            if ((at - m.offset) % stride >= elem) continue; // in the padding after a short element
        }
        return &m;
    }
    return nullptr;
}

const std::vector<TexSlot>& texturesFor(const ShaderSetDesc& set, shaders::Stage st) {
    return st == shaders::Stage::Vertex ? set.vsTextures : set.psTextures;
}

} // namespace

const char* confTag(Conf c) {
    switch (c) {
    case Conf::Confirmed: return "C";
    case Conf::Live: return "L";
    case Conf::Inferred: return "I";
    }
    return "?";
}

uint32_t memberTypeSize(MemberType t) {
    switch (t) {
    case MemberType::Float: return 4;
    case MemberType::Float2: return 8;
    case MemberType::Float3: return 12;
    case MemberType::Float4: return 16;
    case MemberType::Float4x4: return 64;
    }
    return 0;
}

const char* memberTypeName(MemberType t) {
    switch (t) {
    case MemberType::Float: return "float";
    case MemberType::Float2: return "float2";
    case MemberType::Float3: return "float3";
    case MemberType::Float4: return "float4";
    case MemberType::Float4x4: return "float4x4";
    }
    return "?";
}

const char* texDimName(TexDim d) {
    switch (d) {
    case TexDim::Tex2D: return "2d";
    case TexDim::Tex3D: return "3d";
    case TexDim::Cube: return "cube";
    }
    return "?";
}

Status validateLayout(const CbLayout& l) {
    if (l.sizeBytes == 0 || l.sizeBytes % 16)
        return Status::fail(Err::BadLayout, std::string(l.name) + ": size " + std::to_string(l.sizeBytes) +
                                                " is not a positive multiple of 16");
    for (size_t i = 0; i < l.members.size(); ++i) {
        const CbMember& m = l.members[i];
        if (m.count == 0)
            return Status::fail(Err::BadLayout, std::string(l.name) + "." + m.name + ": count 0");
        if (m.count > 1 && memberTypeSize(m.type) < 16)
            return Status::fail(Err::BadLayout, std::string(l.name) + "." + m.name + ": arrays of " +
                                                    memberTypeName(m.type) + " are not described (use float4)");
        const uint32_t bytes = memberBytes(m);
        if (m.offset % 4 || m.offset + bytes > l.sizeBytes)
            return Status::fail(Err::BadLayout, std::string(l.name) + "." + m.name + ": offset " +
                                                    std::to_string(m.offset) + " + " + std::to_string(bytes) +
                                                    " past the size " + std::to_string(l.sizeBytes));
        // HLSL packing: a member smaller than a register never straddles a 16-byte boundary; larger ones start on one
        const uint32_t elem = memberTypeSize(m.type);
        if (elem < 16 ? (m.offset / 16 != (m.offset + elem - 1) / 16) : (m.offset % 16 != 0))
            return Status::fail(Err::BadLayout, std::string(l.name) + "." + m.name + ": straddles a 16-byte register");
        for (size_t j = 0; j < i; ++j) {
            const CbMember& o = l.members[j];
            const uint32_t ob = memberBytes(o);
            if (m.offset < o.offset + ob && o.offset < m.offset + bytes)
                return Status::fail(Err::BadLayout, std::string(l.name) + "." + m.name + " overlaps " + o.name);
        }
    }
    return Status::Ok();
}

Status validateBlob(const ShaderSetDesc& set, const shaders::ShaderBlob& blob, shaders::Stage stage, uint32_t handle,
                    bool checkHandle) {
    const std::string where = std::string(set.name) + " blob " + hex(blob.va);
    if (blob.stage != stage)
        return Status::fail(Err::BadLayout, where + " is a " + shaders::stageName(blob.stage) + " shader, the set names it as " +
                                                shaders::stageName(stage));
    if (checkHandle && blob.handleGlobal != handle)
        return Status::fail(Err::BadLayout, where + ": the EXE stores it in handle " + hex(blob.handleGlobal) +
                                                ", the set says " + hex(handle));
    shaders::ShaderReflection r;
    C5R_TRY(shaders::reflect(blob.bytes, blob.size, r));
    for (const shaders::CbDecl& d : r.cbuffers) {
        const CbBinding* b = findBinding(set, d.slot);
        if (!b)
            return Status::fail(Err::BadLayout, where + " declares cb" + std::to_string(d.slot) + " and the set has no layout there");
        if (d.vec4Count * 16 > b->layout->sizeBytes)
            return Status::fail(Err::BadLayout, where + ": cb" + std::to_string(d.slot) + "[" + std::to_string(d.vec4Count) +
                                                    "] is larger than " + b->layout->name + " (" +
                                                    std::to_string(b->layout->sizeBytes) + " bytes)");
    }
    for (const shaders::CbUse& u : r.cbUse) {
        const CbBinding* b = findBinding(set, u.slot);
        if (!b)
            return Status::fail(Err::BadLayout, where + " reads cb" + std::to_string(u.slot) + " and the set has no layout there");
        for (const shaders::CbRegUse& g : u.regs) {
            for (uint32_t c = 0; c < 4; ++c) {
                if (!(g.usedMask & (1u << c))) continue;
                if (!memberAt(*b->layout, g.reg * 16 + c * 4))
                    return Status::fail(Err::BadLayout, where + " reads cb" + std::to_string(u.slot) + "[" + std::to_string(g.reg) +
                                                            "]." + "xyzw"[c] + ", which no member of " + b->layout->name + " covers");
            }
        }
        for (uint32_t base : u.dynamicBases) {
            const CbMember* m = memberAt(*b->layout, base * 16);
            if (!m || m->count < 2)
                return Status::fail(Err::BadLayout, where + " indexes cb" + std::to_string(u.slot) + "[r + " + std::to_string(base) +
                                                        "] dynamically and no array member of " + b->layout->name + " starts there");
        }
    }
    const std::vector<TexSlot>& tex = texturesFor(set, stage);
    for (const shaders::ResourceDecl& d : r.resources) {
        if (d.uav) continue;
        const TexSlot* t = findTex(tex, d.slot);
        if (!t)
            return Status::fail(Err::BadLayout, where + " declares t" + std::to_string(d.slot) + " (" +
                                                    shaders::resourceDimName(d.dimension) + ") and the set does not describe it");
        if (static_cast<uint32_t>(t->dim) != d.dimension)
            return Status::fail(Err::BadLayout, where + ": t" + std::to_string(d.slot) + " is " + shaders::resourceDimName(d.dimension) +
                                                    ", the set says " + texDimName(t->dim));
    }
    for (const shaders::SamplerDecl& s : r.samplers)
        if (!findTex(tex, s.slot))
            return Status::fail(Err::BadLayout, where + " declares s" + std::to_string(s.slot) + " and the set has no texture slot " +
                                                    std::to_string(s.slot));
    return Status::Ok();
}

Status validateSet(const ShaderSetDesc& set, const shaders::ShaderSection& sec) {
    for (const CbBinding& b : set.cbuffers)
        C5R_TRY(validateLayout(*b.layout));
    static const shaders::Stage kStages[4] = {shaders::Stage::Vertex, shaders::Stage::Pixel, shaders::Stage::Hull,
                                              shaders::Stage::Domain};
    const bool sites = sec.createSitesApplied();
    for (const PassDesc& p : set.passes) {
        for (int lv = 0; lv < 3; ++lv) {
            for (int s = 0; s < 4; ++s) {
                uint32_t va = 0;
                if (lv == 0) va = p.sm50[s];
                else if (s < 2) va = lv == 1 ? p.sm41[s] : p.sm40[s];
                if (!va) continue;
                const shaders::ShaderBlob* b = sec.findByVa(va);
                if (!b)
                    return Status::fail(Err::NotFound, std::string(set.name) + " pass " + std::to_string(p.index) + ": no blob at " + hex(va));
                C5R_TRY(validateBlob(set, *b, kStages[s], p.handle[s], sites));
            }
        }
    }
    return Status::Ok();
}

}} // namespace c5r::terrainsets
