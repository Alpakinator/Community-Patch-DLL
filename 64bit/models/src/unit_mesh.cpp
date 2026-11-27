// unit_mesh.cpp - see include/c5models/unit_mesh.h (issue 4152, premise K1; the EXE's load rule since ticket 0447,
// research/05-graphics-composition/unit-renderer.md 10.9). Own code from the knowledge base's description.
#include "c5models/unit_mesh.h"

#include <cmath>
#include <cstring>
#include <limits>

// Every product and sum below is one float32 operation in the order the rule gives: no contraction into fused
// multiply-adds (the bytes depend on each rounding).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace c5m {

namespace {

uint16_t to_half(double x, bool away) {
    uint64_t b;
    std::memcpy(&b, &x, 8);
    const uint16_t sign = static_cast<uint16_t>((b >> 48) & 0x8000u);
    if (std::isnan(x)) return static_cast<uint16_t>(sign | 0x7E00u);
    const double a = std::fabs(x);
    if (a >= 65520.0) return static_cast<uint16_t>(sign | 0x7C00u);   // rounds to infinity (65504 + half an ulp)
    if (a < std::ldexp(1.0, -25)) {
        // below half the smallest subnormal: 0; exactly half of it: ties to even (0)
        return sign;
    }
    int e = 0;
    std::frexp(a, &e);   // a = m * 2^e, m in [0.5, 1)
    // the half's unit in the last place at this magnitude: 2^(e - 11) for normals (exponent e - 1 >= -14),
    // 2^-24 for subnormals
    const int ulp_exp = (e - 1 >= -14) ? e - 11 : -24;
    const double q = std::ldexp(a, -ulp_exp);       // exact: a power-of-two scaling
    double r = std::floor(q);
    const double frac = q - r;                      // exact
    if (frac > 0.5 || (frac == 0.5 && (away || std::fmod(r, 2.0) != 0.0))) r += 1.0;
    // value = r * 2^ulp_exp; build the bit pattern
    const double v = std::ldexp(r, ulp_exp);
    if (v >= 65520.0) return static_cast<uint16_t>(sign | 0x7C00u);
    if (v < std::ldexp(1.0, -14)) {                 // subnormal (r may have carried to the smallest normal)
        return static_cast<uint16_t>(sign | static_cast<uint16_t>(r));
    }
    int e2 = 0;
    const double m2 = std::frexp(v, &e2);           // v = m2 * 2^e2, m2 in [0.5, 1)
    const uint32_t exp_bits = static_cast<uint32_t>(e2 - 1 + 15);
    const uint32_t mant = static_cast<uint32_t>(std::ldexp(m2, 11)) - 1024u;   // 11 significant bits, top one implicit
    return static_cast<uint16_t>(sign | (exp_bits << 10) | mant);
}

uint32_t bits_of(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

float float_of(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

bool finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

}  // namespace

uint16_t half_from_double(double x) { return to_half(x, false); }
uint16_t half_from_float_away(float x) { return to_half(static_cast<double>(x), true); }

uint16_t half_from_float_granny(float x) {
    const uint32_t b = bits_of(x);
    const uint32_t sgn = (b >> 16) & 0x8000u;
    const uint32_t e = (b >> 23) & 0xFFu;
    const uint32_t m = b & 0x7FFFFFu;
    const int32_t ie = static_cast<int32_t>(e) - 112;   // the half's biased exponent
    if (ie > 30) {                                      // 2^16 and above, infinity, NaN
        if (e == 0xFFu && m != 0) {
            uint32_t r = m >> 13;                       // a NaN keeps its top ten mantissa bits (1 if all are zero)
            if (r == 0) r = 1;
            return static_cast<uint16_t>(r | sgn | 0x7C00u);
        }
        return static_cast<uint16_t>(sgn | 0x7C00u);
    }
    if (ie < -10) return static_cast<uint16_t>(sgn);    // below the smallest subnormal's range: signed zero
    if (ie < 1) {                                       // subnormal: shift the mantissa with its hidden bit, then round
        const uint32_t sh = static_cast<uint32_t>(1 - ie);
        return static_cast<uint16_t>(((((m | 0x800000u) >> sh) + 0x1000u) >> 13) | sgn);
    }
    uint32_t m2 = m + 0x1000u;                          // ties away from zero
    int32_t ie2 = ie;
    if (m2 & 0x800000u) {                               // the carry raises the exponent (31: infinity)
        m2 = 0;
        ie2 = ie + 1;
    }
    return static_cast<uint16_t>((m2 >> 13) | (static_cast<uint32_t>(ie2) << 10) | sgn);
}

double double_from_half(uint16_t h) {
    const double s = (h & 0x8000u) ? -1.0 : 1.0;
    const int e = (h >> 10) & 0x1F;
    const int m = h & 0x3FF;
    if (e == 0) return s * std::ldexp(static_cast<double>(m), -24);
    if (e == 31) return m ? std::numeric_limits<double>::quiet_NaN() : s * std::numeric_limits<double>::infinity();
    return s * std::ldexp(static_cast<double>(m + 1024), e - 25);
}

void unit_weights(uint8_t w[4]) {
    const float w0 = static_cast<float>(w[0]), w1 = static_cast<float>(w[1]), w2 = static_cast<float>(w[2]);
    const float s = (w2 + w1) + w0;
    if (!(s > 0.0f)) return;
    const float k = 255.0f / s;
    const float p0 = k * w0;
    const float p1 = k * w1;
    // + 0.5 is exact in double (as unit-renderer.md 10.9's check does); floor of a value in 0..255.5
    const float a0 = static_cast<float>(std::floor(static_cast<double>(p0) + 0.5));
    const float a1 = static_cast<float>(std::floor(static_cast<double>(p1) + 0.5));
    const float a2 = 255.0f - (a1 + a0);
    // truncated to an integer, its low byte stored (a2 is -1 only for weights such as (1, 1, 0): 255 [I])
    w[0] = static_cast<uint8_t>(static_cast<int32_t>(a0) & 0xFF);
    w[1] = static_cast<uint8_t>(static_cast<int32_t>(a1) & 0xFF);
    w[2] = static_cast<uint8_t>(static_cast<int32_t>(a2) & 0xFF);
    w[3] = 0;
}

// ---------------------------------------------------------------------------------------------- step 1
bool unit_basis(float u, const float fR[3], const float fU[3], const float fB[3], const float origin[3], float scale,
                UnitBasis& out, std::string* err) {
    out = UnitBasis();
    if (!(std::isfinite(scale) && scale > 0)) {
        if (err) *err = "scale must be finite and > 0";
        return false;
    }
    if (!(std::isfinite(u) && u > 0)) {
        if (err) *err = "ArtToolInfo UnitsPerMeter must be finite and > 0";
        return false;
    }
    if (!finite3(fR) || !finite3(fU) || !finite3(fB) || !finite3(origin)) {
        if (err) *err = "ArtToolInfo axes or origin not finite";
        return false;
    }
    // the desired axes: right dR, up dU, back dB (e, p6, p5 below name them in the order the products are taken)
    const float dR[3] = {1, 0, 0}, dU[3] = {0, 0, 1}, dB[3] = {0, -1, 0};
    const float* e = dB;
    const float* p6 = dU;
    const float* p5 = dR;
    float M[9];
    M[0] = (e[0] * fB[0] + p6[0] * fU[0]) + fR[0] * p5[0];
    M[1] = (fU[1] * p6[0] + e[0] * fB[1]) + fR[1] * p5[0];
    M[2] = (fB[2] * e[0] + fU[2] * p6[0]) + fR[2] * p5[0];
    M[3] = (e[1] * fB[0] + p6[1] * fU[0]) + p5[1] * fR[0];
    M[4] = (fU[1] * p6[1] + fR[1] * p5[1]) + e[1] * fB[1];
    M[5] = (fB[2] * e[1] + fU[2] * p6[1]) + fR[2] * p5[1];
    M[6] = (e[2] * fB[0] + p6[2] * fU[0]) + p5[2] * fR[0];
    M[7] = (fU[1] * p6[2] + fR[1] * p5[2]) + e[2] * fB[1];
    M[8] = (fB[2] * e[2] + fU[2] * p6[2]) + fR[2] * p5[2];
    const float desired = u * scale;
    const float k = desired / u;
    float m[9];
    for (int i = 0; i < 9; ++i) m[i] = M[i] * k;
    float det = ((m[4] * m[8] - m[5] * m[7]) * m[0]) - ((m[3] * m[8] - m[6] * m[5]) * m[1]);
    det = det + ((m[3] * m[7] - m[6] * m[4]) * m[2]);
    if (det == 0.0f || !std::isfinite(det)) {
        if (err) *err = "the ArtToolInfo axes give a singular basis";
        return false;
    }
    const float inv = 1.0f / det;
    float I[9];
    I[0] = (m[4] * m[8] - m[7] * m[5]) * inv;
    I[3] = -((m[8] * m[3] - m[6] * m[5]) * inv);
    I[6] = (m[7] * m[3] - m[6] * m[4]) * inv;
    I[1] = -((m[1] * m[8] - m[7] * m[2]) * inv);
    I[4] = (m[8] * m[0] - m[6] * m[2]) * inv;
    I[7] = -((m[7] * m[0] - m[6] * m[1]) * inv);
    I[2] = (m[1] * m[5] - m[4] * m[2]) * inv;
    I[5] = -((m[5] * m[0] - m[2] * m[3]) * inv);
    I[8] = (m[4] * m[0] - m[1] * m[3]) * inv;
    out.k = k;
    std::memcpy(out.L, m, sizeof m);
    std::memcpy(out.I, I, sizeof I);
    for (int i = 0; i < 3; ++i) out.A[i] = 0.0f - origin[i];
    return true;
}

bool unit_basis(const FileInfo& f, float scale, UnitBasis& out, std::string* err) {
    bool axes = false;
    for (int i = 0; i < 3; ++i) axes = axes || f.right[i] != 0 || f.up[i] != 0 || f.back[i] != 0;
    if (!axes && f.units_per_meter == 0) {   // no ArtToolInfo [I]
        const float r[3] = {1, 0, 0}, up[3] = {0, 0, 1}, back[3] = {0, -1, 0}, o[3] = {0, 0, 0};
        return unit_basis(kUnitDefaultUnitsPerMeter, r, up, back, o, scale, out, err);
    }
    return unit_basis(f.units_per_meter, f.right, f.up, f.back, f.origin, scale, out, err);
}

bool unit_basis_stock(float scale, UnitBasis& out, std::string* err) {
    const float r[3] = {1, 0, 0}, up[3] = {0, 0, 1}, back[3] = {0, -1, 0}, o[3] = {0, 0, 0};
    return unit_basis(39.370079f, r, up, back, o, scale, out, err);
}

bool unit_basis_fbx(float scale, UnitBasis& out, std::string* err) {
    const float r[3] = {1, 0, 0}, up[3] = {0, 1, 0}, back[3] = {0, 0, 1}, o[3] = {0, 0, 0};
    return unit_basis(1.0f, r, up, back, o, scale, out, err);
}

namespace {

void put16(uint8_t* p, uint16_t h) {
    p[0] = static_cast<uint8_t>(h & 0xFF);
    p[1] = static_cast<uint8_t>(h >> 8);
}

bool fail(std::string* err, UnitMesh& out, const std::string& m) {
    if (err) *err = m;
    out = UnitMesh();
    return false;
}

const VertexComponent* need(const Mesh& m, const char* name, MemberType t, uint32_t min_count) {
    const VertexComponent* c = m.format.find(name);
    if (!c || c->type != t || c->count < min_count) return nullptr;
    return c;
}

// A Real32 or Real16 component of at least min_count elements.
const VertexComponent* real(const Mesh& m, const char* name, uint32_t min_count) {
    const VertexComponent* c = m.format.find(name);
    if (!c || c->count < min_count || (c->type != MemberType::Real32 && c->type != MemberType::Real16)) return nullptr;
    return c;
}

uint32_t elem_size(const VertexComponent* c) { return c->type == MemberType::Real16 ? 2u : 4u; }

float get(const uint8_t* v, const VertexComponent* c, uint32_t k) {
    if (c->type == MemberType::Real32) {
        float f;
        std::memcpy(&f, v + c->offset + 4 * k, 4);
        return f;
    }
    uint16_t h;
    std::memcpy(&h, v + c->offset + 2 * k, 2);
    return static_cast<float>(double_from_half(h));   // exact
}

}  // namespace

bool repack_unit_mesh(const Mesh& mesh, const UnitBasis& B, bool skinned, UnitMesh& out, std::string* err) {
    out = UnitMesh();
    const VertexComponent* pos = real(mesh, "Position", 3);
    const VertexComponent* nrm = real(mesh, "Normal", 3);
    const VertexComponent* uv = real(mesh, "TextureCoordinates0", 2);
    const VertexComponent* bw = skinned ? need(mesh, "BoneWeights", MemberType::NormalUInt8, 4) : nullptr;
    const VertexComponent* bi = skinned ? need(mesh, "BoneIndices", MemberType::UInt8, 4) : nullptr;
    if (!pos || !nrm || !uv || (skinned && (!bw || !bi)))
        return fail(err, out, "mesh " + mesh.name + ": needs Position[3], Normal[3], TextureCoordinates0[2+] (Real32 "
                              "or Real16)" + (skinned ? ", NormalUInt8 BoneWeights[4], UInt8 BoneIndices[4]" : ""));
    const uint32_t stride = mesh.format.stride;
    const uint32_t n = mesh.vertex_count;
    if (static_cast<uint64_t>(pos->offset) + 3 * elem_size(pos) > stride ||
        static_cast<uint64_t>(nrm->offset) + 3 * elem_size(nrm) > stride ||
        static_cast<uint64_t>(uv->offset) + 2 * elem_size(uv) > stride ||
        (bw && static_cast<uint64_t>(bw->offset) + 4 > stride) || (bi && static_cast<uint64_t>(bi->offset) + 4 > stride))
        return fail(err, out, "mesh " + mesh.name + ": a component lies outside the vertex");
    if (static_cast<uint64_t>(n) * stride != mesh.vertices.size())
        return fail(err, out, "mesh " + mesh.name + ": vertex bytes != count x stride");
    if (n > 65536) return fail(err, out, "mesh " + mesh.name + ": more than 65,536 vertices need a 32-bit index buffer");
    const uint32_t bones = static_cast<uint32_t>(mesh.bone_bindings.size());
    if (skinned && bones > kUnitPaletteBones)
        return fail(err, out, "mesh " + mesh.name + ": " + std::to_string(bones) + " bones, more than one palette block "
                              "holds (" + std::to_string(kUnitPaletteBones) + "; split into several draws, K8)");
    for (uint32_t i : mesh.indices)
        if (i >= n) return fail(err, out, "mesh " + mesh.name + ": an index is not below the vertex count");
    const uint32_t os = skinned ? kUnitVertexStride : kUnitStaticStride;
    const uint32_t nOff = skinned ? 16u : 8u, tOff = skinned ? 24u : 16u;
    const float* L = B.L;
    const float* I = B.I;
    out.vertices.assign(static_cast<size_t>(n) * os, 0);
    for (uint32_t v = 0; v < n; ++v) {
        const uint8_t* src = mesh.vertices.data() + static_cast<size_t>(v) * stride;
        uint8_t* dst = out.vertices.data() + static_cast<size_t>(v) * os;
        // step 2, positions
        const float x = get(src, pos, 0), y = get(src, pos, 1), z = get(src, pos, 2);
        float P[3];
        if (std::isnan(x) || std::isnan(y) || std::isnan(z)) {
            // every output coordinate takes the first NaN operand, quieted (0 x NaN is NaN in the off-diagonal terms;
            // ethiopiacompbow.gr2, unit-renderer.md 10.9)
            const float q = std::isnan(x) ? x : std::isnan(y) ? y : z;
            P[0] = P[1] = P[2] = float_of(bits_of(q) | 0x400000u);
        } else {
            P[0] = B.A[0] + ((L[0] * x + L[1] * y) + L[2] * z);
            P[1] = B.A[1] + ((L[3] * x + L[4] * y) + L[5] * z);
            P[2] = B.A[2] + ((L[6] * x + L[7] * y) + L[8] * z);
        }
        // step 2, normals (the transpose of I)
        const float nx = get(src, nrm, 0), ny = get(src, nrm, 1), nz = get(src, nrm, 2);
        const float N[3] = {(ny * I[3] + nz * I[6]) + nx * I[0], (ny * I[4] + nz * I[7]) + I[1] * nx,
                            (nz * I[8] + I[2] * nx) + I[5] * ny};
        for (int c = 0; c < 3; ++c) put16(dst + 2 * c, half_from_float_granny(P[c]));
        for (int c = 0; c < 3; ++c) put16(dst + nOff + 2 * c, half_from_float_granny(N[c]));
        for (uint32_t c = 0; c < 2; ++c) {
            if (uv->type == MemberType::Real16) {
                std::memcpy(dst + tOff + 2 * c, src + uv->offset + 2 * c, 2);   // copied as it is
            } else {
                float t;
                std::memcpy(&t, src + uv->offset + 4 * c, 4);
                put16(dst + tOff + 2 * c, half_from_float_granny(t));
            }
        }
        if (!skinned) continue;
        uint8_t w[4], b[4];
        std::memcpy(w, src + bw->offset, 4);
        std::memcpy(b, src + bi->offset, 4);
        uint8_t keep[4];
        std::memcpy(keep, w, 4);
        unit_weights(w);   // step 4, every vertex
        if (std::memcmp(keep, w, 4) != 0) ++out.renormalised;
        for (int c = 0; c < 4; ++c)
            if (b[c] >= bones && (c == 0 || w[c] != 0))
                return fail(err, out, "mesh " + mesh.name + ": vertex " + std::to_string(v) + " names bone " +
                                      std::to_string(b[c]) + " of " + std::to_string(bones));
        std::memcpy(dst + 8, w, 4);
        std::memcpy(dst + 12, b, 4);
    }
    out.indices.reserve(mesh.indices.size());
    for (uint32_t i : mesh.indices) out.indices.push_back(static_cast<uint16_t>(i));
    out.vertex_count = n;
    out.stride = os;
    out.bones = bones;
    return true;
}

bool repack_unit_mesh(const Mesh& mesh, float scale, UnitMesh& out, std::string* err) {
    UnitBasis b;
    if (!unit_basis_stock(scale, b, err)) {
        out = UnitMesh();
        return false;
    }
    return repack_unit_mesh(mesh, b, true, out, err);
}

uint64_t fnv1a64(const uint8_t* data, size_t size) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace c5m
