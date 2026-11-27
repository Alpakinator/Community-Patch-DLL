// mesh.cpp - c5r::arc: ArrowSettings.xml, the arc's mesh, the arrow record and its end points (ticket 0199). Own code,
// written from the behaviour described in research/02-exe-internals/arrows-and-movement-path.md sections 1.1-1.6 (D83:
// no decompiler output here). ArrowSettings.xml is read from the player's install at run time.
#include <c5r/arc/arc.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

#include "c5r/gamefs/civ5pkg.h"
#include "c5r/gamefs/gamefiles.h"

namespace c5r {
namespace arc {

namespace {

struct V3 {
    float x, y, z;
};
inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator-(V3 a) { return v3(-a.x, -a.y, -a.z); }
inline V3 operator*(float k, V3 a) { return v3(k * a.x, k * a.y, k * a.z); }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
// A zero vector stays zero (D3DXVec3Normalize's behaviour [I]).
inline V3 norm(V3 a)
{
    const float l = len(a);
    return l > 0.0f ? (1.0f / l) * a : v3(0, 0, 0);
}

bool finiteN(const float* p, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(p[i])) return false;
    return true;
}

// The rotation of v about the (normalised) axis by ang radians, as D3DXMatrixRotationAxis + D3DXVec3TransformCoord do it
// for a row vector (Rodrigues' formula).
V3 rotate(V3 v, V3 axis, float ang)
{
    const V3 a = norm(axis);
    const float c = std::cos(ang), s = std::sin(ang);
    return c * v + s * cross(a, v) + ((1.0f - c) * dot(a, v)) * a;
}

// The curve: a clamped quadratic B-spline over the four control points, knots (0, 0, 0, 1, 2, 2, 2), at u = 2 s
// (arrows-and-movement-path.md 1.6). Point and (unnormalised) tangent.
void curve(const V3 P[4], float u, V3& point, V3& tangent)
{
    if (u < 1.0f) {
        const float f = u - std::floor(u);
        point = ((1.0f - f) * (1.0f - f)) * P[0] + (2.0f * f - 1.5f * f * f) * P[1] + (0.5f * f * f) * P[2];
        tangent = (2.0f * f - 2.0f) * P[0] + (2.0f - 3.0f * f) * P[1] + f * P[2];
    } else {
        // u in [1, 2]; at u = 2 the piece's end (f = 1) is P3
        const float f = u >= 2.0f ? 1.0f : u - std::floor(u);
        point = (0.5f * (1.0f - f) * (1.0f - f)) * P[1] + (0.5f + f - 1.5f * f * f) * P[2] + (f * f) * P[3];
        tangent = (f - 1.0f) * P[1] + (1.0f - 3.0f * f) * P[2] + (2.0f * f) * P[3];
    }
}

const float kRoll = 0.549762f;        // radians per unit of the tangent's x direction
const float kRollEdge = 0.867f;
const float kStep = 0.04f;

struct Frame {
    V3 C, T, S, N;
    float w;
};

// The frame at curve parameter s: C, T, S (rolled), N and the width (1.6 "Sampling and width").
Frame frameAt(const Settings& set, const V3 P[4], float s)
{
    Frame f;
    V3 tg;
    curve(P, 2.0f * s, f.C, tg);
    f.T = norm(tg);
    const V3 S0 = norm(v3(f.T.y, -f.T.x, 0.0f));
    f.N = norm(cross(S0, f.T));
    const V3 d = norm(v3(f.T.x, f.T.y, 0.0f));
    f.S = S0;
    if (d.x > kRollEdge || d.x < -kRollEdge)
        f.S = rotate(S0, f.T, d.x * kRoll);
    else if (d.y > 0.0f)
        f.S = rotate(S0, f.T, d.x * -kRoll);
    f.w = s * (set.maxWidth - set.minWidth) + set.minWidth;
    return f;
}

Vertex vert(V3 p, V3 n, uint32_t c0, uint32_t c1)
{
    Vertex v;
    v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
    v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
    v.color0 = c0;
    v.color1 = c1;
    v.uv[0] = v.uv[1] = 0.0f;
    return v;
}

// The eight body vertices of a ring at C: a box of half-width w and half-height width2 with one normal per face.
void bodyRing(const Frame& f, float h, V3 shift, uint32_t body, std::vector<Vertex>& out)
{
    const V3 C = f.C + shift, wS = f.w * f.S, hN = h * f.N;
    const V3 pos[8] = {C - wS + hN, C + wS + hN, C + wS + hN, C + wS - hN, C + wS - hN, C - wS - hN, C - wS - hN, C - wS + hN};
    const V3 nrm[8] = {f.N, f.N, f.S, f.S, -f.N, -f.N, -f.S, -f.S};
    for (int i = 0; i < 8; ++i) out.push_back(vert(pos[i], nrm[i], body, 0xFFFFFFFFu));
}

void ring(const Settings& set, const Frame& f, std::vector<Vertex>& out)
{
    bodyRing(f, set.width2, v3(0, 0, 0), set.bodyColor, out);
    // the outline: a larger box with its winding reversed (an inverted hull), normal 0, COLOR1 0
    const float W = f.w + set.outlineWidth, H = set.width2 + set.outlineWidth2;
    const V3 WS = W * f.S, HN = H * f.N;
    const V3 o[8] = {f.C - WS + HN, f.C + WS + HN, f.C + WS + HN, f.C + WS - HN,
                     f.C + WS - HN, f.C - WS - HN, f.C - WS - HN, f.C - WS + HN};
    for (int i = 0; i < 8; ++i) out.push_back(vert(o[i], v3(0, 0, 0), set.outlineColor, 0u));
}

void joinRings(uint32_t p, uint32_t q, std::vector<uint32_t>& ix)
{
    static const uint32_t body[4][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}};
    static const uint32_t outl[4][2] = {{8, 9}, {10, 11}, {12, 13}, {14, 15}};
    for (const auto& ab : body) {
        const uint32_t a = ab[0], b = ab[1];
        const uint32_t t[6] = {p + a, q + a, p + b, p + b, q + a, q + b};
        ix.insert(ix.end(), t, t + 6);
    }
    for (const auto& ab : outl) {
        const uint32_t a = ab[0], b = ab[1];
        const uint32_t t[6] = {p + a, p + b, q + a, p + b, q + b, q + a};
        ix.insert(ix.end(), t, t + 6);
    }
}

// The head's 14 triangles over the 26-vertex layout of blocks 2 and 3.
const uint32_t kHeadTris[14][3] = {{0, 12, 1}, {1, 12, 2}, {2, 12, 3}, {4, 13, 5}, {5, 13, 14}, {6, 15, 7}, {7, 15, 8},
                                   {8, 15, 9}, {10, 16, 11}, {11, 16, 17}, {18, 22, 19}, {19, 22, 23}, {20, 24, 21},
                                   {21, 24, 25}};

// The head at the frame of s_end (1.6 "The head"): a collar, the triangular plate, its outline.
void head(const Settings& set, const Frame& f, Mesh& m)
{
    std::vector<Vertex>& V = m.vertices;
    std::vector<uint32_t>& I = m.indices;
    const float h = set.width2, O = set.outlineWidth, H = set.width2 + set.outlineWidth2;
    const float L = 2.0f * std::sqrt(3.0f) * f.w;   // the height of an equilateral triangle of side 4 w
    // block 1: the body ring at C and the same moved by O T
    uint32_t q = static_cast<uint32_t>(V.size());
    bodyRing(f, h, v3(0, 0, 0), set.bodyColor, V);
    bodyRing(f, h, O * f.T, set.bodyColor, V);
    static const uint32_t pairs[4][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}};
    for (const auto& ab : pairs) {
        const uint32_t a = q + ab[0], b = q + ab[1];
        const uint32_t t[6] = {a, a + 8, b, b, a + 8, b + 8};
        I.insert(I.end(), t, t + 6);
    }
    // block 2: the plate, base at C + O T, tip A at C + (O + L) T, thickness 2 width2
    q = static_cast<uint32_t>(V.size());
    const V3 base = f.C + O * f.T;
    const V3 Bm2 = base + (-2.0f * f.w) * f.S, Bm1 = base + (-f.w) * f.S, Bp1 = base + f.w * f.S, Bp2 = base + (2.0f * f.w) * f.S;
    const V3 A = f.C + (O + L) * f.T;
    const V3 hN = h * f.N;
    // the slanted sides' normals: the faces through B(+2), A and B(-2), A, each spanned with N
    // (fitted to 0179's capture 2026-11-16, v420 and v426 to 1e-5; arrows-and-path-line.md 12)
    const V3 M1 = norm(cross(f.N, L * f.T - (2.0f * f.w) * f.S));
    const V3 M2 = norm(cross(L * f.T + (2.0f * f.w) * f.S, f.N));
    const V3 mT = -f.T;
    const uint32_t body = set.bodyColor, white = 0xFFFFFFFFu;
    const Vertex plate[26] = {
        vert(Bm2 + hN, f.N, body, white), vert(Bm1 + hN, f.N, body, white), vert(Bp1 + hN, f.N, body, white),
        vert(Bp2 + hN, f.N, body, white), vert(Bp2 + hN, M1, body, white), vert(Bp2 - hN, M1, body, white),
        vert(Bp2 - hN, -f.N, body, white), vert(Bp1 - hN, -f.N, body, white), vert(Bm1 - hN, -f.N, body, white),
        vert(Bm2 - hN, -f.N, body, white), vert(Bm2 - hN, M2, body, white), vert(Bm2 + hN, M2, body, white),
        vert(A + hN, f.N, body, white), vert(A + hN, M1, body, white), vert(A - hN, M1, body, white),
        vert(A - hN, -f.N, body, white), vert(A - hN, M2, body, white), vert(A + hN, M2, body, white),
        vert(Bm2 - hN, mT, body, white), vert(Bm1 - hN, mT, body, white), vert(Bp1 - hN, mT, body, white),
        vert(Bp2 - hN, mT, body, white), vert(Bm2 + hN, mT, body, white), vert(Bm1 + hN, mT, body, white),
        vert(Bp1 + hN, mT, body, white), vert(Bp2 + hN, mT, body, white)};
    V.insert(V.end(), plate, plate + 26);
    for (const auto& t : kHeadTris) {
        const uint32_t x[3] = {q + t[0], q + t[1], q + t[2]};
        I.insert(I.end(), x, x + 3);
    }
    // block 3: the plate's outline, base at C (no O shift), outer corners at +-(2 w + O), tip A' = C + (L + sqrt3 O) T,
    // thickness 2 H; normal 0, COLOR0 outline, COLOR1 0; the triangles with their winding reversed
    q = static_cast<uint32_t>(V.size());
    const V3 Om2 = f.C + (-(2.0f * f.w + O)) * f.S, Om1 = f.C + (-f.w) * f.S, Op1 = f.C + f.w * f.S,
             Op2 = f.C + (2.0f * f.w + O) * f.S;
    const V3 A2 = f.C + (L + std::sqrt(3.0f) * O) * f.T;
    const V3 HN = H * f.N;
    const V3 opos[26] = {Om2 + HN, Om1 + HN, Op1 + HN, Op2 + HN, Op2 + HN, Op2 - HN, Op2 - HN, Op1 - HN, Om1 - HN,
                         Om2 - HN, Om2 - HN, Om2 + HN, A2 + HN,  A2 + HN,  A2 - HN, A2 - HN, A2 - HN, A2 + HN,
                         Om2 - HN, Om1 - HN, Op1 - HN, Op2 - HN, Om2 + HN, Om1 + HN, Op1 + HN, Op2 + HN};
    for (const auto& p : opos) V.push_back(vert(p, v3(0, 0, 0), set.outlineColor, 0u));
    for (const auto& t : kHeadTris) {
        const uint32_t x[3] = {q + t[0], q + t[2], q + t[1]};
        I.insert(I.end(), x, x + 3);
    }
}

bool readNumber(const std::string& text, float& out)
{
    std::istringstream is(text);
    is.imbue(std::locale::classic());
    double d = 0;
    is >> d;
    if (!is) return false;
    is >> std::ws;
    if (!is.eof()) return false;
    out = static_cast<float>(d);
    return std::isfinite(out);
}

bool readColour(const std::string& text, uint32_t& out)
{
    int c[3] = {0, 0, 0};
    const char* p = text.c_str();
    for (int i = 0; i < 3; ++i) {
        while (*p == ' ' || *p == '\t') ++p;
        char* e = nullptr;
        const long v = std::strtol(p, &e, 10);
        if (e == p || v < 0 || v > 255) return false;
        c[i] = static_cast<int>(v);
        p = e;
        while (*p == ' ' || *p == '\t') ++p;
        if (i < 2) {
            if (*p != ',') return false;
            ++p;
        }
    }
    if (*p != '\0') return false;
    // R8G8B8A8_UNORM bytes R, G, B, A: the dword 0xAABBGGRR (arrows-and-movement-path.md 1.5)
    out = 0xFF000000u | (static_cast<uint32_t>(c[2]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | static_cast<uint32_t>(c[0]);
    return true;
}

} // namespace

// ---- settings ----

Status parseSettings(const char* xml, size_t size, Settings& out, std::string* note)
{
    std::unique_ptr<gamefs::XmlNode> root;
    C5R_TRY(gamefs::parseXml(xml, size, root));
    if (!root || root->name != "Arrow") return Status::fail(Err::BadLayout, "ArrowSettings.xml: no <Arrow> root element");
    Settings s;
    std::string missed;
    struct F { const char* name; float* v; };
    const F fl[] = {{"min_width", &s.minWidth},        {"max_width", &s.maxWidth},       {"width2", &s.width2},
                    {"outline_width", &s.outlineWidth}, {"outline_width2", &s.outlineWidth2}, {"arc_height", &s.arcHeight},
                    {"target_height", &s.targetHeight}, {"anim_speed", &s.animSpeed},      {"alpha", &s.alpha},
                    {"ambient_light_level", &s.ambient}};
    for (const F& f : fl) {
        auto it = root->attrs.find(f.name);
        if (it == root->attrs.end()) {
            missed += std::string(missed.empty() ? "" : ", ") + f.name;
            continue;
        }
        if (!readNumber(it->second, *f.v))
            return Status::fail(Err::BadLayout, std::string("ArrowSettings.xml: ") + f.name + " = \"" + it->second + "\" is not a number");
    }
    struct C { const char* name; uint32_t* v; };
    const C cl[] = {{"body_color", &s.bodyColor}, {"outline_color", &s.outlineColor}};
    for (const C& c : cl) {
        auto it = root->attrs.find(c.name);
        if (it == root->attrs.end()) {
            missed += std::string(missed.empty() ? "" : ", ") + c.name;
            continue;
        }
        if (!readColour(it->second, *c.v))
            return Status::fail(Err::BadLayout, std::string("ArrowSettings.xml: ") + c.name + " = \"" + it->second + "\" is not R,G,B");
    }
    if (!(s.animSpeed > 0.0f)) return Status::fail(Err::BadLayout, "ArrowSettings.xml: anim_speed must be > 0");
    if (s.minWidth < 0 || s.maxWidth < 0 || s.width2 < 0 || s.outlineWidth < 0 || s.outlineWidth2 < 0)
        return Status::fail(Err::BadLayout, "ArrowSettings.xml: a width is negative");
    if (note) *note = missed.empty() ? std::string() : "missing (defaults kept): " + missed;
    out = s;
    return Status::Ok();
}

Status loadSettings(const std::wstring& gameDir, Settings& out, std::string* origin)
{
    gamefs::GameFiles fs;
    C5R_TRY(fs.open(gameDir));
    std::vector<uint8_t> bytes;
    Status r = fs.read(kSettingsFile, bytes, origin);
    if (!r.ok()) return Status::fail(Err::NotFound, std::string(kSettingsFile) + " not readable from the install: " + r.str());
    return parseSettings(reinterpret_cast<const char*>(bytes.data()), bytes.size(), out);
}

// ---- the mesh ----

Status buildMesh(const Settings& set, const float start[3], const float end[3], float progress, Mesh& out)
{
    if (!start || !end || !finiteN(start, 3) || !finiteN(end, 3) || !std::isfinite(progress) || progress < 0.0f)
        return Status::fail(Err::BadLayout, "buildMesh: an end point or the progress is not finite (or progress < 0)");
    static_assert(offsetof(Settings, bodyColor) == 10 * sizeof(float), "Settings starts with its ten floats");
    const float* sf = &set.minWidth;
    if (!finiteN(sf, 10)) return Status::fail(Err::BadLayout, "buildMesh: a setting is not finite");
    const float t = progress < kMaxProgress ? progress : kMaxProgress;
    Mesh m;
    const V3 P0 = v3(start[0], start[1], start[2]), P3 = v3(end[0], end[1], end[2]);
    V3 P[4] = {P0, 0.75f * P0 + 0.25f * P3, 0.25f * P0 + 0.75f * P3, P3};
    P[1].z += set.arcHeight;
    P[2].z += set.arcHeight;
    const float stopK = 2.0f * std::sqrt(3.0f);
    Frame f = frameAt(set, P, 0.0f);
    float s = 0.0f;
    for (;;) {
        const float ss = s < t ? s : t;
        f = frameAt(set, P, ss);
        const uint32_t q = static_cast<uint32_t>(m.vertices.size());
        ring(set, f, m.vertices);
        if (m.rings > 0) joinRings(q - kRingVertices, q, m.indices);
        ++m.rings;
        m.sEnd = ss;
        if (len(P3 - f.C) <= stopK * f.w + set.outlineWidth) {
            m.earlyStop = true;
            break;
        }
        s += kStep;
        if (!(s < t + kStep - 0.001f)) break;
    }
    head(set, f, m);
    out = std::move(m);
    return Status::Ok();
}

// ---- the arrow record ----

Status Arrow::spawn(const float start[3], const float endXY[2])
{
    if (!start || !endXY || !finiteN(start, 3) || !finiteN(endXY, 2))
        return Status::fail(Err::BadLayout, "Arrow::spawn: an end point is not finite");
    std::memcpy(m_start, start, sizeof m_start);
    m_end[0] = endXY[0];
    m_end[1] = endXY[1];
    m_end[2] = m_set.targetHeight;
    m_progress = 0.0f;
    m_hidden = false;
    m_growing = true;
    m_rebuild = true;
    return Status::Ok();
}

Status Arrow::rebuild()
{
    C5R_TRY(buildMesh(m_set, m_start, m_end, m_progress, m_mesh));
    m_built = true;
    ++m_gen;
    if (m_mesh.earlyStop) m_growing = false;
    return Status::Ok();
}

Status Arrow::update(float dt)
{
    if (!std::isfinite(dt) || dt < 0.0f) return Status::fail(Err::BadLayout, "Arrow::update: dt not finite or negative");
    if (m_hidden) return Status::Ok();
    if (m_rebuild) {
        m_rebuild = false;
        C5R_TRY(rebuild());
    }
    if (m_growing) {
        const float rate = 1.0f / m_set.animSpeed;
        if (m_progress < kMaxProgress) {
            m_progress += rate * dt;
            if (m_progress > kMaxProgress) m_progress = kMaxProgress;
        }
        if (m_progress >= kMaxProgress) m_growing = false;
        C5R_TRY(rebuild());
    }
    return Status::Ok();
}

// ---- end points ----

void plotToWorldXY(const heightfield::HeightGrid& g, int32_t gx, int32_t gy, float out[2], float sampleOut[2])
{
    const float hx = static_cast<float>(gx - (gy >> 1)), hy = static_cast<float>(gy);
    const float* m = g.hexToWorld;
    const float x = hx * m[0] + hy * m[4] + m[12];
    const float y = hx * m[1] + hy * m[5] + m[13];
    out[0] = x * g.sx - g.ox;
    out[1] = y * g.sy - g.oy;
    if (sampleOut) {
        sampleOut[0] = x;
        sampleOut[1] = y;
    }
}

float worldWidth(const heightfield::HeightGrid& g)
{
    return (g.wrapX && g.samplesX > 0) ? static_cast<float>(g.samplesX - 1) * g.sx : 0.0f;
}

Status spawnPoints(const heightfield::HeightGrid& g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const float (*planes)[4],
                   float start[3], float endXY[2])
{
    if (!start || !endXY) return Status::fail(Err::BadLayout, "spawnPoints: no output");
    if (g.samplesX == 0 || g.samplesY == 0 || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "spawnPoints: the height grid is empty or inconsistent");
    if (!finiteN(g.hexToWorld, 16) || !std::isfinite(g.sx) || !std::isfinite(g.sy) || !std::isfinite(g.ox) ||
        !std::isfinite(g.oy) || !std::isfinite(g.sz))
        return Status::fail(Err::BadLayout, "spawnPoints: the grid's placement is not finite");
    const int32_t rows = static_cast<int32_t>(g.mapH);
    if (y0 < 0 || y1 < 0 || (rows > 0 && (y0 >= rows || y1 >= rows)))
        return Status::fail(Err::BadLayout, "spawnPoints: a plot outside the grid's rows");
    float s[2], e[2], smp[2];
    plotToWorldXY(g, x0, y0, s, smp);
    plotToWorldXY(g, x1, y1, e);
    float st[3] = {s[0], s[1], heightfield::heightAtSample(g, static_cast<int>(std::floor(smp[0])), static_cast<int>(std::floor(smp[1])))};
    float en[3] = {e[0], e[1], 0.0f};
    if (planes) C5R_TRY(chooseWrap(planes, worldWidth(g), st, en));
    std::memcpy(start, st, sizeof st);
    endXY[0] = en[0];
    endXY[1] = en[1];
    return Status::Ok();
}

Status frustumPlanes(const float vp[16], float planes[6][4])
{
    if (!vp || !finiteN(vp, 16)) return Status::fail(Err::BadLayout, "frustumPlanes: the matrix is not finite");
    // row vectors: clip = (x, y, z, 1) M, so clip.k = sum_i p_i M[i][k]; the planes are columns combined
    auto col = [&](int k, float c[4]) { for (int i = 0; i < 4; ++i) c[i] = vp[i * 4 + k]; };
    float c0[4], c1[4], c2[4], c3[4];
    col(0, c0); col(1, c1); col(2, c2); col(3, c3);
    for (int i = 0; i < 4; ++i) {
        planes[0][i] = c3[i] + c0[i];   // left
        planes[1][i] = c3[i] - c0[i];   // right
        planes[2][i] = c3[i] + c1[i];   // bottom
        planes[3][i] = c3[i] - c1[i];   // top
        planes[4][i] = c2[i];           // near (D3D: 0 <= z)
        planes[5][i] = c3[i] - c2[i];   // far
    }
    for (int p = 0; p < 6; ++p) {
        const float l = std::sqrt(planes[p][0] * planes[p][0] + planes[p][1] * planes[p][1] + planes[p][2] * planes[p][2]);
        if (!(l > 0.0f) || !std::isfinite(l)) return Status::fail(Err::BadLayout, "frustumPlanes: a zero plane");
        for (int i = 0; i < 4; ++i) planes[p][i] /= l;
    }
    return Status::Ok();
}

Status chooseWrap(const float planes[6][4], float W, float start[3], float end[3])
{
    if (!planes || !start || !end || !finiteN(&planes[0][0], 24) || !std::isfinite(W) || !finiteN(start, 3) || !finiteN(end, 3))
        return Status::fail(Err::BadLayout, "chooseWrap: a value is not finite");
    if (!(W > 0.0f)) return Status::Ok();
    auto inside = [&](float x) {
        for (int p = 0; p < 6; ++p)
            if (planes[p][0] * x + planes[p][1] * start[1] + planes[p][2] * start[2] + planes[p][3] < -64.0f) return false;
        return true;
    };
    float sx = start[0];
    const float cand[3] = {start[0] - W, start[0], start[0] + W};
    for (float c : cand)
        if (inside(c)) sx = c;   // the last candidate inside wins
    start[0] = sx;
    auto d2 = [&](float x) {
        const float dx = x - start[0], dy = end[1] - start[1];
        return dx * dx + dy * dy;
    };
    // stock's order: x1 if nearer than x1 - W, then x1 + W if nearer still; x1 - W itself is never chosen
    float ex = end[0];
    const float dm = d2(end[0] - W), d0 = d2(end[0]), dp = d2(end[0] + W);
    // [I: the running best starts at x1 - W's distance while the choice starts at x1; so x1 + W wins only when it is
    // nearer than both others]
    float best = dm;
    if (d0 < best) best = d0;
    if (dp < best) ex = end[0] + W;
    end[0] = ex;
    return Status::Ok();
}

} // namespace arc
} // namespace c5r
