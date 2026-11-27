// draw_check.cpp - cb1 from a pass camera, and the comparisons of the module's draw inputs with a capture's, one
// function per check (epic 0041, issue 4352, DoD B; the oracle run 4355 calls them). Own code.
#include "c5r/units/draw.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace c5r { namespace units {

Cb1 makeCb1(const PassCamera& c) {
    Cb1 o;
    for (int i = 0; i < 3; ++i) o.r[i] = c.rotation[i];
    o.r[3] = {c.eye[0], c.eye[1], c.eye[2], 1.0f};
    for (int i = 0; i < 4; ++i) o.r[4 + i] = c.viewProj[i];
    o.r[8] = {c.threshold, 0, 0, 0};
    o.r[9] = c.fog;
    o.r[10] = c.fogColour;
    o.r[11] = {c.eye[0], c.eye[1], c.eye[2], 0};
    return o;
}

PassCamera cameraFromCb1(const Cb1& c) {
    PassCamera p;
    for (int i = 0; i < 3; ++i) p.rotation[i] = c.r[i];
    p.eye[0] = c.r[3].x;
    p.eye[1] = c.r[3].y;
    p.eye[2] = c.r[3].z;
    for (int i = 0; i < 4; ++i) p.viewProj[i] = c.r[4 + i];
    p.threshold = c.r[8].x;
    p.fog = c.r[9];
    p.fogColour = c.r[10];
    return p;
}

namespace {
bool same(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }
bool same4(const Float4& a, const Float4& b) { return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z) && same(a.w, b.w); }
}  // namespace

bool cb1ReadLanesEqual(const Cb1& a, const Cb1& b, std::string* why) {
    auto bad = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (!same(a.r[3].x, b.r[3].x) || !same(a.r[3].y, b.r[3].y) || !same(a.r[3].z, b.r[3].z)) return bad("r3 (eye)");
    for (int i = 4; i < 8; ++i)
        if (!same4(a.r[i], b.r[i])) return bad("r4-r7 (view-projection)");
    if (!same(a.r[8].x, b.r[8].x)) return bad("r8.x (threshold)");
    if (!same4(a.r[9], b.r[9])) return bad("r9 (fog)");
    if (!same4(a.r[10], b.r[10])) return bad("r10 (fog colour)");
    if (!same(a.r[11].x, b.r[11].x) || !same(a.r[11].y, b.r[11].y) || !same(a.r[11].z, b.r[11].z))
        return bad("r11 (fog eye)");
    return true;
}

static BufferCompare compareElements(const uint8_t* ours, size_t ourBytes, const uint8_t* cap, size_t capBytes,
                                     uint32_t stride) {
    BufferCompare c;
    c.sizeMatch = ourBytes == capBytes;
    c.elements = static_cast<uint32_t>(ourBytes / stride);
    if (!c.sizeMatch || !cap) return c;
    for (uint32_t i = 0; i < c.elements; ++i) {
        if (std::memcmp(ours + static_cast<size_t>(i) * stride, cap + static_cast<size_t>(i) * stride, stride) == 0)
            ++c.equal;
        else if (c.differ.size() < 64)
            c.differ.push_back(i);
    }
    return c;
}

BufferCompare compareVertices(const ModelMesh& ours, const uint8_t* captured, size_t bytes) {
    return compareElements(ours.vertices.data(), ours.vertices.size(), captured, bytes, ours.stride ? ours.stride : 1);
}

BufferCompare compareIndices(const ModelMesh& ours, const uint8_t* captured, size_t bytes) {
    return compareElements(reinterpret_cast<const uint8_t*>(ours.indices.data()), ours.indices.size() * 2, captured,
                           bytes, 2);
}

bool headerRowsEqual(const Float4 ours[2], const float cap[8], float* maxDiff) {
    float d0 = 0;
    const float o0[4] = {ours[0].x, ours[0].y, ours[0].z, ours[0].w};
    for (int i = 0; i < 4; ++i) d0 = std::max(d0, std::fabs(o0[i] - cap[i]));
    const float o1[3] = {ours[1].x, ours[1].y, ours[1].z};
    float d1 = 0;
    for (int i = 0; i < 3; ++i) d1 = std::max(d1, std::fabs(o1[i] - cap[4 + i]));
    if (maxDiff) *maxDiff = std::max(d0, d1);
    return d0 <= 1.0f / 255.0f + 1e-6f && d1 == 0.0f && o0[3] == cap[3];
}

bool idsFigureMajor(const std::vector<std::vector<uint32_t>>& perMesh, uint32_t figures, std::string* why) {
    std::set<uint32_t> base;
    for (size_t j = 0; j < perMesh.size(); ++j) {
        if (perMesh[j].size() != figures) {
            if (why) *why = "mesh " + std::to_string(j) + " has " + std::to_string(perMesh[j].size()) + " instances, " +
                            std::to_string(figures) + " figures";
            return false;
        }
        std::set<uint32_t> s;
        for (uint32_t id : perMesh[j]) {
            if (id < j) {
                if (why) *why = "id below the mesh index";
                return false;
            }
            s.insert(id - static_cast<uint32_t>(j));
        }
        if (s.size() != figures || (j > 0 && s != base)) {
            if (why) *why = "mesh " + std::to_string(j) + "'s ids are not mesh 0's plus " + std::to_string(j);
            return false;
        }
        if (j == 0) base = s;
    }
    return true;
}

}}  // namespace c5r::units
