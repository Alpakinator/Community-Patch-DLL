// anim_curves.cpp - curve decoders and de Boor evaluation of the pose sampler (c5models/anim.h). Epic 0327, issue
// 4151, 2026-11-15. Own code written from the knowledge base's description of the formats
// (research/05-graphics-composition/model-reader.md 6.2-6.3) and checked against the Python reference
// research/05-graphics-composition/evidence/3100/playback/curves.py (tests/test_anim.cpp), except the two decoders
// marked below.
//
// Formats 3 (DaConstant32f) and 17 (D3I1K16uC16u) are translated from LSLib:
//   origin: https://github.com/Norbyte/lslib, commit 6f5f6987d10d8485876b8368431c948e4dea4f83,
//           files LSLib/Granny/Model/CurveData/DaConstant32f.cs and LSLib/Granny/Model/CurveData/D3I1K16uC16u.cs
//   (the knot scale of format 17 also follows LSLib/Granny/Model/CurveData/AnimationCurveData.cs,
//   ConvertOneOverKnotScaleTrunc). Their licence notice, kept as the MIT licence requires:
//
//   The MIT License (MIT)
//
//   Copyright (c) 2015 Norbyte
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
#include "c5models/anim.h"

#include <cmath>
#include <cstring>

namespace c5m {

const char* anim_err_name(AnimErr e) {
    switch (e) {
    case AnimErr::None: return "None";
    case AnimErr::UnsupportedFormat: return "UnsupportedFormat";
    case AnimErr::BadDegree: return "BadDegree";
    case AnimErr::MissingMember: return "MissingMember";
    case AnimErr::ArrayMismatch: return "ArrayMismatch";
    case AnimErr::DimensionMismatch: return "DimensionMismatch";
    case AnimErr::KnotsNotMonotonic: return "KnotsNotMonotonic";
    case AnimErr::NonFinite: return "NonFinite";
    case AnimErr::UnboundBone: return "UnboundBone";
    case AnimErr::ParentOrder: return "ParentOrder";
    case AnimErr::NoTrackGroup: return "NoTrackGroup";
    }
    return "?";
}

uint32_t slot_dim(CurveSlot s) {
    return s == CurveSlot::Position ? 3u : s == CurveSlot::Orientation ? 4u : 9u;
}

namespace {

bool fail(AnimError* err, AnimErr code, const std::string& what) {
    if (err) {
        err->code = code;
        err->what = what;
    }
    return false;
}

// D4n scale / offset tables (model-reader.md 6.2; the same 16 values in both open readers)
const float kScale[16] = {1.4142135f, 0.70710677f, 0.35355338f, 0.35355338f, 0.35355338f, 0.17677669f, 0.17677669f,
                          0.17677669f, -1.4142135f, -0.70710677f, -0.35355338f, -0.35355338f, -0.35355338f,
                          -0.17677669f, -0.17677669f, -0.17677669f};
const float kOffset[16] = {-0.70710677f, -0.35355338f, -0.53033006f, -0.17677669f, 0.17677669f, -0.17677669f,
                           -0.088388346f, 0.0f, 0.70710677f, 0.35355338f, 0.53033006f, 0.17677669f, -0.17677669f,
                           0.17677669f, 0.088388346f, -0.0f};

const std::vector<float>* scalar(const Curve& c, const char* name, size_t n) {
    for (const auto& s : c.scalars)
        if (s.first == name) return s.second.size() == n ? &s.second : nullptr;
    return nullptr;
}

const CurveArray* array(const Curve& c, const char* name, MemberType type) {
    for (const auto& a : c.arrays)
        if (a.name == name) {
            if (a.type != type || a.data.size() != (size_t)a.count * member_type_size(type)) return nullptr;
            return &a;
        }
    return nullptr;
}

uint32_t element(const CurveArray& a, size_t i) {
    if (a.type == MemberType::UInt8) return a.data[i];
    uint16_t v;
    std::memcpy(&v, &a.data[2 * i], 2);
    return v;
}

float real(const CurveArray& a, size_t i) {
    float v;
    std::memcpy(&v, &a.data[4 * i], 4);
    return v;
}

// OneOverKnotScaleTrunc: the high 16 bits of a float (model-reader.md 6.2)
float trunc_to_float(uint32_t u16) {
    uint32_t bits = u16 << 16;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

bool finite_all(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

// formats 10, 11, 17, 18: knots and 3-vectors with per-component scale and offset
bool decode_d3(const Curve& c, DecodedCurve& o, AnimError* err) {
    const bool k16 = c.format == 10 || c.format == 17;
    const bool one = c.format == 17 || c.format == 18;   // one value per knot for all three components
    const auto* tr = scalar(c, "OneOverKnotScaleTrunc", 1);
    const auto* sc = scalar(c, "ControlScales", 3);
    const auto* of = scalar(c, "ControlOffsets", 3);
    const CurveArray* kc = array(c, "KnotsControls", k16 ? MemberType::UInt16 : MemberType::UInt8);
    if (!tr || !sc || !of || !kc) return fail(err, AnimErr::MissingMember, c.format_name + ": scalars or KnotsControls");
    const size_t per = one ? 2 : 4;
    if (kc->count == 0 || kc->count % per) return fail(err, AnimErr::ArrayMismatch, c.format_name + ": KnotsControls length");
    const size_t n = kc->count / per;
    const float inv = trunc_to_float((uint32_t)(*tr)[0]);
    if (!(inv > 0) || !std::isfinite(inv)) return fail(err, AnimErr::NonFinite, c.format_name + ": knot scale");
    o.knots.resize(n);
    o.controls.resize(n * 3);
    for (size_t i = 0; i < n; ++i) {
        o.knots[i] = (float)element(*kc, i) / inv;
        for (size_t j = 0; j < 3; ++j) {
            // format 17: LSLib D3I1K16uC16u.cs GetPoints (one word per knot, scaled per component); 18 likewise
            const float q = (float)element(*kc, one ? n + i : n + 3 * i + j);
            o.controls[3 * i + j] = q * (*sc)[j] + (*of)[j];
        }
    }
    return true;
}

// formats 8, 9: quaternions from three 15-bit or 7-bit values per knot (model-reader.md 6.2, "D4n encoding")
bool decode_d4n(const Curve& c, DecodedCurve& o, AnimError* err) {
    const bool k16 = c.format == 8;
    const auto* sel = scalar(c, "ScaleOffsetTableEntries", 1);
    const auto* inv = scalar(c, "OneOverKnotScale", 1);
    const CurveArray* kc = array(c, "KnotsControls", k16 ? MemberType::UInt16 : MemberType::UInt8);
    if (!sel || !inv || !kc) return fail(err, AnimErr::MissingMember, c.format_name + ": scalars or KnotsControls");
    if (kc->count == 0 || kc->count % 4) return fail(err, AnimErr::ArrayMismatch, c.format_name + ": KnotsControls length");
    if (!((*inv)[0] > 0) || !std::isfinite((*inv)[0])) return fail(err, AnimErr::NonFinite, c.format_name + ": knot scale");
    const size_t n = kc->count / 4;
    const uint32_t s = (uint32_t)(*sel)[0];
    const float q = k16 ? 0.000030518509f : 0.0078740157f;   // 1/32767, 1/127
    float sc[4], of[4];
    for (int i = 0; i < 4; ++i) {
        sc[i] = kScale[(s >> (4 * i)) & 15] * q;
        of[i] = kOffset[(s >> (4 * i)) & 15];
    }
    const uint32_t top = k16 ? 0x8000u : 0x80u, low = k16 ? 0x7fffu : 0x7fu, sh = k16 ? 15u : 7u;
    o.knots.resize(n);
    o.controls.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        o.knots[i] = (float)element(*kc, i) / (*inv)[0];
        const uint32_t a = element(*kc, n + 3 * i), b = element(*kc, n + 3 * i + 1), cc = element(*kc, n + 3 * i + 2);
        const uint32_t s1 = (((b & top) >> sh) << 1) | ((cc & top) >> sh);
        const uint32_t s2 = (s1 + 1) & 3, s3 = (s1 + 2) & 3, s4 = (s1 + 3) & 3;
        const float da = (float)(a & low) * sc[s2] + of[s2];
        const float db = (float)(b & low) * sc[s3] + of[s3];
        const float dc = (float)(cc & low) * sc[s4] + of[s4];
        const float r = 1.0f - (da * da + db * db + dc * dc);
        float dd = std::sqrt(r > 0 ? r : 0.0f);
        if (a & top) dd = -dd;
        float* qv = &o.controls[4 * i];
        qv[s2] = da;
        qv[s3] = db;
        qv[s4] = dc;
        qv[s1] = dd;
    }
    return true;
}

}  // namespace

namespace {
bool decode_impl(const Curve& c, CurveSlot slot, DecodedCurve& o, AnimError* err) {
    o = DecodedCurve();
    const uint32_t dim = slot_dim(slot);
    if (!c.present) return fail(err, AnimErr::MissingMember, "curve not present");
    o.format = c.format;
    o.degree = c.degree;
    o.dim = dim;
    if (c.degree > 3) return fail(err, AnimErr::BadDegree, c.format_name + ": degree " + std::to_string(c.degree));
    bool ok = true;
    switch (c.format) {
    case 1: {   // DaK32fC32f: float knots, n x dim float controls
        const CurveArray* k = array(c, "Knots", MemberType::Real32);
        const CurveArray* p = array(c, "Controls", MemberType::Real32);
        if (!k || !p) return fail(err, AnimErr::MissingMember, c.format_name + ": Knots or Controls");
        if (k->count == 0 || p->count % k->count) return fail(err, AnimErr::ArrayMismatch, c.format_name + ": Controls not n x dim");
        if (p->count / k->count != dim) return fail(err, AnimErr::DimensionMismatch, c.format_name + ": dimension");
        o.knots.resize(k->count);
        o.controls.resize(p->count);
        for (size_t i = 0; i < k->count; ++i) o.knots[i] = real(*k, i);
        for (size_t i = 0; i < p->count; ++i) o.controls[i] = real(*p, i);
        break;
    }
    case 2:   // DaIdentity
        o.identity = true;
        o.degree = 0;
        o.knots.assign(1, 0.0f);
        o.controls.assign(dim, 0.0f);
        if (slot == CurveSlot::Orientation) o.controls[3] = 1.0f;
        if (slot == CurveSlot::ScaleShear) o.controls[0] = o.controls[4] = o.controls[8] = 1.0f;
        break;
    case 3: {   // DaConstant32f: one control of any dimension (translated from LSLib DaConstant32f.cs, see above)
        const CurveArray* p = array(c, "Controls", MemberType::Real32);
        if (!p) return fail(err, AnimErr::MissingMember, c.format_name + ": Controls");
        if (p->count != dim) return fail(err, AnimErr::DimensionMismatch, c.format_name + ": dimension");
        o.knots.assign(1, 0.0f);
        o.controls.resize(dim);
        for (size_t i = 0; i < dim; ++i) o.controls[i] = real(*p, i);
        break;
    }
    case 4:
    case 5: {   // D3Constant32f / D4Constant32f: Controls[3] / [4] as scalars
        const uint32_t d = c.format == 4 ? 3u : 4u;
        if (d != dim) return fail(err, AnimErr::DimensionMismatch, c.format_name + ": dimension");
        const auto* v = scalar(c, "Controls", d);
        if (!v) return fail(err, AnimErr::MissingMember, c.format_name + ": Controls");
        o.knots.assign(1, 0.0f);
        o.controls = *v;
        break;
    }
    case 8:
    case 9:
        if (dim != 4) return fail(err, AnimErr::DimensionMismatch, c.format_name + ": dimension");
        ok = decode_d4n(c, o, err);
        break;
    case 10:
    case 11:
    case 17:
    case 18:
        if (dim != 3) return fail(err, AnimErr::DimensionMismatch, c.format_name + ": dimension");
        ok = decode_d3(c, o, err);
        break;
    default:
        return fail(err, AnimErr::UnsupportedFormat,
                    "curve format " + std::to_string(c.format) + " (" + c.format_name + ") not supported");
    }
    if (!ok) return false;
    if (!finite_all(o.controls)) return fail(err, AnimErr::NonFinite, c.format_name + ": control not finite");
    for (size_t i = 0; i < o.knots.size(); ++i)
        if (!std::isfinite(o.knots[i]) || (i && o.knots[i] < o.knots[i - 1]))
            return fail(err, AnimErr::KnotsNotMonotonic, c.format_name + ": knot " + std::to_string(i));
    return true;
}
}  // namespace

bool decode_curve(const Curve& c, CurveSlot slot, DecodedCurve& out, AnimError* err) {
    if (decode_impl(c, slot, out, err)) return true;
    out = DecodedCurve();   // nothing half-decoded is left behind
    return false;
}

float clip_time(float t, float duration, bool loop) {
    if (!(duration > 0) || !std::isfinite(t)) return 0.0f;
    if (loop) {
        float r = std::fmod(t, duration);
        if (r < 0) r += duration;
        return r >= duration ? 0.0f : r;
    }
    return t < 0 ? 0.0f : (t > duration ? duration : t);
}

namespace {

void nlerp4(const float* a, const float* b, float t, float* out) {
    float bb[4] = {b[0], b[1], b[2], b[3]};
    if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0)
        for (float& x : bb) x = -x;
    float q[4];
    for (int i = 0; i < 4; ++i) q[i] = a[i] + (bb[i] - a[i]) * t;
    float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(n > 0)) n = 1.0f;
    for (int i = 0; i < 4; ++i) out[i] = q[i] / n;
}

}  // namespace

void eval_curve(const DecodedCurve& c, float t, bool quaternion, float* out) {
    const size_t n = c.knots.size(), k = c.degree, dim = c.dim;
    if (n == 0 || c.controls.size() < dim) {
        for (size_t j = 0; j < dim; ++j) out[j] = 0.0f;
        return;
    }
    if (n == 1 || n < k + 1 || k == 0) {   // constant: P[0] (curves.py de_boor, nwn2mdk export_gr2.cpp)
        for (size_t j = 0; j < dim; ++j) out[j] = c.controls[j];
    } else {
        // clamp t to the knot range (model-reader.md 6.3 rule 1: Trunc knot scales put the last knot up to 0.6 %
        // past Duration)
        if (t < 0) t = 0;
        if (t > c.knots[n - 1]) t = c.knots[n - 1];
        // padded knot vector T = [0, K0 .. K(n-1)] with T[1..k] = 0 and K(n-1) appended k times
        float T[256 + 8];
        std::vector<float> big;
        float* tv = T;
        const size_t len = n + k + 1;
        if (len > sizeof(T) / sizeof(T[0])) {
            big.resize(len);
            tv = big.data();
        }
        tv[0] = 0.0f;
        for (size_t j = 0; j < n; ++j) tv[j + 1] = c.knots[j];
        for (size_t j = 1; j <= k; ++j) tv[j] = 0.0f;
        for (size_t j = 0; j < k; ++j) tv[n + 1 + j] = c.knots[n - 1];
        // span: the last i with T[i] <= t, searched from k, at most len - k - 2 (= n - 1)
        size_t lo = k, hi = len - k - 1;   // first index with T[i] > t lies in [lo, hi]
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (tv[mid] <= t) lo = mid + 1;
            else hi = mid;
        }
        const size_t i = lo - 1;
        float d[4][9];
        for (size_t j = 0; j <= k; ++j)
            for (size_t m = 0; m < dim; ++m) d[j][m] = c.controls[(j + i - k) * dim + m];
        for (size_t r = 1; r <= k; ++r)
            for (size_t j = k; j >= r; --j) {
                const float den = tv[j + 1 + i - r] - tv[j + i - k];
                const float al = den != 0 ? (t - tv[j + i - k]) / den : (quaternion ? 1.0f : 0.0f);
                if (quaternion) {
                    nlerp4(d[j - 1], d[j], al, d[j]);
                } else {
                    for (size_t m = 0; m < dim; ++m) d[j][m] = d[j - 1][m] + (d[j][m] - d[j - 1][m]) * al;
                }
            }
        for (size_t m = 0; m < dim; ++m) out[m] = d[k][m];
    }
    if (quaternion && dim == 4) {
        float nn = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (!(nn > 0)) {
            out[0] = out[1] = out[2] = 0.0f;
            out[3] = 1.0f;
        } else {
            for (int m = 0; m < 4; ++m) out[m] /= nn;
        }
    }
}

}  // namespace c5m
