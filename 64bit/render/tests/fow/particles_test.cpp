// c5r::fow::ParticleSim without game files (ticket 0340; fog-of-war.md 10). Checks: the settings reader (by name, ".5f",
// a value that does not read stops its group), the flow field from a synthetic DDS, the C runtime's rand() formula, the
// lattice spawn (count, ranges, ids, repeatability, another seed), the step (only the area around the footprint moves,
// particles stay in its inner box, repulsion, the flow pushes, repeatability), the emitted sprites (cells, the rotation
// limit, the corners, the sort), the state's byte-exact round trip (a loaded state steps and emits byte-equal), and the
// negative paths (no reset, bad sizes, a truncated or foreign state, another flow field). Own code; no game bytes.
// Ticket 0692: every cut of the step and the draw list (fog-of-war.md 12.5) checked against a copy of the code before
// it (namespace ref): the state, the sprites and their order byte-equal over many steps, on synthetic states (dense,
// pairs at exactly h and just beyond, a zero-mass and a NaN-mass particle) and, when C5R_GAME_DIR is set and the scene
// library is there (C5_SCENE_LIBRARY, else C:/c5-64bit-work/scene-library/bundles), on every bundle's particles.bin with
// the install's flowfield.dds (read at run time; the only game bytes this test touches).
#include <c5r/fow/fow.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "c5r/gamefs/gamefiles.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

using namespace c5r;
using namespace c5r::fow;

namespace {

// A synthetic uncompressed A8R8G8B8 DDS of w x h with texel (r, g) = f(x, y).
template <class F> std::vector<uint8_t> makeDds(uint32_t w, uint32_t h, F f)
{
    std::vector<uint8_t> b(128 + static_cast<size_t>(w) * h * 4, 0);
    auto p32 = [&](size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
    std::memcpy(&b[0], "DDS ", 4);
    p32(4, 124);
    p32(8, 0x1007);   // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    p32(12, h);
    p32(16, w);
    p32(20, w * 4);
    p32(28, 1);
    p32(76, 32);
    p32(80, 0x41);    // RGB | ALPHAPIXELS
    p32(88, 32);
    p32(92, 0x00FF0000);
    p32(96, 0x0000FF00);
    p32(100, 0x000000FF);
    p32(104, 0xFF000000);
    p32(108, 0x1000);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r, g;
            f(x, y, r, g);
            uint8_t* t = &b[128 + (static_cast<size_t>(y) * w + x) * 4];
            t[0] = 0; t[1] = g; t[2] = r; t[3] = 255;   // B, G, R, A in memory
        }
    return b;
}

FlowField zeroFlow()
{
    FlowField f;
    f.width = f.height = 64;
    f.xy.assign(64 * 64 * 2, 0.0f);
    return f;
}

struct Census { uint32_t n = 0; float minS = 1e9f, maxS = -1e9f, minM = 1e9f, maxM = -1e9f, minZ = 1e9f, maxZ = -1e9f; uint32_t maxFrame = 0; };
void census(const Particle& p, void* c)
{
    Census& s = *static_cast<Census*>(c);
    s.n++;
    s.minS = std::min(s.minS, p.size); s.maxS = std::max(s.maxS, p.size);
    s.minM = std::min(s.minM, p.mass); s.maxM = std::max(s.maxM, p.mass);
    s.minZ = std::min(s.minZ, p.z); s.maxZ = std::max(s.maxZ, p.z);
    s.maxFrame = std::max(s.maxFrame, p.id & 0xFFu);
}

const float kTop[16] = {1 / 512.0f, 0, 0, 0, 0, 1 / 512.0f, 0, 0, 0, 0, -1 / 1000.0f, 0, 0, 0, 0.5f, 1};

// ---- The reference (ticket 0692): ParticleSim's step, emit and sort exactly as they were before 0692's cuts
// (render/src/fow/particles.cpp at 8535c8787), working on a state's own bytes ("C5FP" version 1). Every cut must leave
// the state, the sprites and their order byte-equal to this. Own code (a copy of our own former code). ----
namespace ref {
const float kH = 60.0f;
const double kPi = 3.14159265358979323846;
const float kSpiky = static_cast<float>(15.0 / (kPi * 60.0 * 60.0 * 60.0 * 60.0 * 60.0 * 60.0));
const float kMinR = 0.001f;
const float kSizeScale = 16.0f;
const double kTwoPi = 2.0 * kPi;

struct Sim {
    std::vector<uint8_t> head;   // the state's bytes before the cells (lost and steps rewritten by save)
    ParticleSettings set;
    FlowField flow;
    int32_t cx = 0, cy = 0;
    float gx = 0, gy = 0;
    uint32_t lost = 0, steps = 0, moved = 0;
    std::vector<std::vector<Particle>> cells;
    static size_t headSize() { return 8 + sizeof(ParticleSettings) + 48; }
    bool load(const std::vector<uint8_t>& st, const FlowField& f)
    {
        const size_t hs = headSize();
        if (st.size() < hs) return false;
        head.assign(st.begin(), st.begin() + static_cast<std::ptrdiff_t>(hs));
        size_t at = 8;
        std::memcpy(&set, &st[at], sizeof set);
        at += sizeof set + 4 + 4 + 8 + 4;
        std::memcpy(&cx, &st[at], 4); std::memcpy(&cy, &st[at + 4], 4);
        std::memcpy(&gx, &st[at + 8], 4); std::memcpy(&gy, &st[at + 12], 4);
        std::memcpy(&lost, &st[at + 20], 4); std::memcpy(&steps, &st[at + 24], 4);
        at = hs;
        cells.assign(static_cast<size_t>(cx) * cy, std::vector<Particle>());
        for (auto& c : cells) {
            uint32_t n = 0;
            if (at + 4 > st.size()) return false;
            std::memcpy(&n, &st[at], 4);
            at += 4;
            c.resize(n);
            if (at + n * sizeof(Particle) > st.size()) return false;
            if (n) std::memcpy(c.data(), &st[at], n * sizeof(Particle));
            at += n * sizeof(Particle);
        }
        flow = f;
        return at == st.size();
    }
    std::vector<uint8_t> save() const
    {
        std::vector<uint8_t> b = head;
        std::memcpy(&b[headSize() - 8], &lost, 4);
        std::memcpy(&b[headSize() - 4], &steps, 4);
        for (const auto& c : cells) {
            const uint32_t n = static_cast<uint32_t>(c.size());
            const uint8_t* np = reinterpret_cast<const uint8_t*>(&n);
            b.insert(b.end(), np, np + 4);
            if (n) b.insert(b.end(), reinterpret_cast<const uint8_t*>(c.data()), reinterpret_cast<const uint8_t*>(c.data()) + n * sizeof(Particle));
        }
        return b;
    }
    void cellOf(float x, float y, int32_t& ox, int32_t& oy) const
    {
        const double gx1 = static_cast<double>(gx) + static_cast<double>(cx) * kParticleCellSize;
        const double gy1 = static_cast<double>(gy) + static_cast<double>(cy) * kParticleCellSize;
        double ix = std::trunc(static_cast<double>(x)), iy = std::trunc(static_cast<double>(y));
        ix = std::min(std::max(ix, static_cast<double>(gx) - 1.0), gx1 + 1.0);
        iy = std::min(std::max(iy, static_cast<double>(gy) - 1.0), gy1 + 1.0);
        ox = static_cast<int32_t>(std::floor((ix - gx) / kParticleCellSize));
        oy = static_cast<int32_t>(std::floor((iy - gy) / kParticleCellSize));
        ox = std::min(std::max(ox, 0), cx - 1);
        oy = std::min(std::max(oy, 0), cy - 1);
    }
    void bin(const Particle& p)
    {
        int32_t x, y;
        cellOf(p.x, p.y, x, y);
        std::vector<Particle>& c = cells[static_cast<size_t>(y) * cx + x];
        if (c.size() >= kParticleCellCap) { lost++; return; }
        c.push_back(p);
    }
    void step(const float fpMin[2], const float fpMax[2])
    {
        const int32_t m = kParticleMargin;
        int32_t a0x, a0y, a1x, a1y;
        cellOf(fpMin[0], fpMin[1], a0x, a0y);
        cellOf(fpMax[0], fpMax[1], a1x, a1y);
        const int32_t cx0 = std::max(a0x - m, m), cy0 = std::max(a0y - m, m);
        const int32_t cx1 = std::min(a1x + m, cx - m), cy1 = std::min(a1y + m, cy - m);
        steps++;
        moved = 0;
        if (cx1 - cx0 < 3 || cy1 - cy0 < 3) return;
        const ParticleSettings& s = set;
        const float h = kH;
        auto cellAt = [&](int32_t x, int32_t y) -> std::vector<Particle>& { return cells[static_cast<size_t>(y) * cx + x]; };
        for (int32_t y = cy0; y < cy1; ++y)
            for (int32_t x = cx0; x < cx1; ++x)
                for (Particle& p : cellAt(x, y)) {
                    float rho = 0;
                    for (int32_t ny = std::max(y - 1, 0); ny < std::min(y + 2, cy); ++ny)
                        for (int32_t nx = std::max(x - 1, 0); nx < std::min(x + 2, cx); ++nx)
                            for (const Particle& q : cellAt(nx, ny)) {
                                const float dx = p.x - q.x, dy = p.y - q.y;
                                const float r = std::sqrt(dx * dx + dy * dy);
                                const float k = h - r;
                                rho += std::max(k * k * k * kSpiky, 0.0f) * q.mass;
                            }
                    p.density = rho;
                    p.pressure = p.mass / (s.fluidDensity * rho * 2.0f);
                }
        const float wx0 = static_cast<float>(cx0 + 1) * kParticleCellSize + gx, wy0 = static_cast<float>(cy0 + 1) * kParticleCellSize + gy;
        const float wx1 = static_cast<float>(cx1 - 1) * kParticleCellSize + gx, wy1 = static_cast<float>(cy1 - 1) * kParticleCellSize + gy;
        const float px = wx1 - wx0, py = wy1 - wy0;
        std::vector<Particle> mv;
        const float k3 = -3.0f * kSpiky;
        for (int32_t y = cy0; y < cy1; ++y)
            for (int32_t x = cx0; x < cx1; ++x) {
                const bool ring = x < cx0 + 1 || x >= cx1 - 1 || y < cy0 + 1 || y >= cy1 - 1;
                for (const Particle& p : cellAt(x, y)) {
                    if (ring) { mv.push_back(p); continue; }
                    float fx = 0, fy = 0;
                    for (int32_t ny = std::max(y - 1, 0); ny < std::min(y + 2, cy); ++ny)
                        for (int32_t nx = std::max(x - 1, 0); nx < std::min(x + 2, cx); ++nx)
                            for (const Particle& q : cellAt(nx, ny)) {
                                const float dx = p.x - q.x, dy = p.y - q.y;
                                float r = std::sqrt(dx * dx + dy * dy);
                                const bool in = r <= h;
                                r = std::max(r, kMinR);
                                const float k = h - r;
                                const float w = in ? k * k * (1.0f / r) * k3 : 0.0f;
                                const float t = (q.density + p.density) * q.pressure;
                                fx -= w * dx * t;
                                fy -= w * dy * t;
                            }
                    const uint32_t fw = flow.width, fh = flow.height;
                    const uint32_t ix = (static_cast<uint32_t>(static_cast<int32_t>(p.x)) + (fw >> 1)) % fw;
                    const uint32_t iy = ((fh >> 1) - static_cast<uint32_t>(static_cast<int32_t>(p.y))) % fh;
                    const float* fl = &flow.xy[(static_cast<size_t>(iy) * fw + ix) * 2];
                    const float inv = 1.0f / p.density;
                    Particle o = p;
                    o.vx = (s.speed * (fx * inv + s.forceStrength * fl[0]) + p.vx) - s.drag * p.vx;
                    o.vy = (s.speed * (fy * inv + s.forceStrength * fl[1]) + p.vy) - s.drag * p.vy;
                    float nx = s.speed * o.vx + p.x, ny = s.speed * o.vy + p.y;
                    if (nx > wx1) nx -= px;
                    if (nx < wx0) nx += px;
                    if (ny > wy1) ny -= py;
                    if (ny < wy0) ny += py;
                    o.x = nx;
                    o.y = ny;
                    mv.push_back(o);
                    moved++;
                }
            }
        for (int32_t y = cy0; y < cy1; ++y)
            for (int32_t x = cx0; x < cx1; ++x) cellAt(x, y).clear();
        for (const Particle& p : mv) bin(p);
    }
    void emit(const float viewProj[16], const float fpMin[2], const float fpMax[2], uint32_t atlasPerRow, float du, float dv,
              std::vector<ParticleSprite>& out)
    {
        int32_t ax, ay, bx, by;
        cellOf(fpMin[0], fpMin[1], ax, ay);
        cellOf(fpMax[0], fpMax[1], bx, by);
        const int32_t x0 = std::max(ax, 0), y0 = std::max(ay, 0);
        const int32_t x1 = std::min(bx + 1, cx - 1), y1 = std::min(by + 2, cy - 1);
        const float e0 = set.masterSize * kSizeScale;
        for (int32_t x = x0; x < x1; ++x)
            for (int32_t y = y0; y < y1; ++y)
                for (Particle& p : cells[static_cast<size_t>(y) * cx + x]) {
                    if (out.size() >= kMaxParticleSprites) continue;
                    const float len = std::sqrt(p.vx * p.vx + p.vy * p.vy);
                    const float nx = len > 0 ? p.vx / len : 0.0f, ny = len > 0 ? p.vy / len : 0.0f;
                    double tgt = std::atan2(static_cast<double>(ny), static_cast<double>(nx));
                    if (tgt < 0) tgt += kTwoPi;
                    float target = static_cast<float>(tgt), cur = p.rot;
                    if (std::fabs(static_cast<double>(target) - cur) > kPi) {
                        if (target <= cur) target = static_cast<float>(tgt + kTwoPi);
                        else cur = static_cast<float>(cur + kTwoPi);
                    }
                    float d = target - cur;
                    d = std::min(std::max(d, -set.maxRotate), set.maxRotate);
                    p.rot = d + cur;
                    const float cs = std::cos(p.rot), sn = std::sin(p.rot);
                    const float e = p.size * e0;
                    const float ux = e * cs, uy = e * sn, wx = -e * sn, wy = e * cs;
                    const uint32_t frame = p.id & 0xFFu;
                    const float u0 = static_cast<float>(frame % atlasPerRow) * du, v0 = static_cast<float>(frame / atlasPerRow) * dv;
                    const float u1 = static_cast<float>(frame % atlasPerRow + 1) * du, v1 = static_cast<float>(frame / atlasPerRow + 1) * dv;
                    ParticleSprite sp = {};
                    const float pos[4][2] = {{p.x - ux + wx, p.y - uy + wy}, {p.x - ux - wx, p.y - uy - wy},
                                             {p.x + ux - wx, p.y + uy - wy}, {p.x + ux + wx, p.y + uy + wy}};
                    const float uv[4][2] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
                    for (int k = 0; k < 4; ++k) {
                        sp.v[k][0] = pos[k][0];
                        sp.v[k][1] = pos[k][1];
                        sp.v[k][2] = p.z;
                        sp.v[k][6] = uv[k][0];
                        sp.v[k][7] = uv[k][1];
                    }
                    sp.key = p.x * viewProj[3] + p.y * viewProj[7] + p.z * viewProj[11] + viewProj[15];
                    out.push_back(sp);
                }
    }
    static void sort(std::vector<ParticleSprite>& s)
    {
        std::stable_sort(s.begin(), s.end(), [](const ParticleSprite& a, const ParticleSprite& b) { return a.key > b.key; });
    }
};
} // namespace ref

// A view whose clip w varies over the map (so the sort keys differ) over a footprint.
void slantView(const float lo[2], const float hi[2], float vp[16])
{
    const float sx = 2.0f / std::max(hi[0] - lo[0], 1.0f), sy = 2.0f / std::max(hi[1] - lo[1], 1.0f);
    const float m[16] = {sx, 0, 0, 0.00031f, 0, sy, 0, 0.00017f, 0, 0, -0.001f, 0.0009f,
                         -(lo[0] + hi[0]) * 0.5f * sx, -(lo[1] + hi[1]) * 0.5f * sy, 0.5f, 7.0f};
    std::memcpy(vp, m, sizeof m);
}

// Steps, emits and sorts a state with the reference and with ParticleSim for `steps` frames over the footprints in turn;
// every state, sprite list and sorted list byte-equal. Returns the number of particles moved in total (> 0 expected).
// threads: the step's thread count (0692 cut A); 0 = a different count every step (1, 8, 2, 4, 3, ...).
struct EqStats { uint64_t moved = 0, sprites = 0; int stepsDone = 0; };
bool equalRun1(const char* what, const std::vector<uint8_t>& state, const FlowField& flow, float worldW, float worldH,
               const std::vector<std::array<float, 4>>& fps, int steps, uint32_t threads, EqStats* out)
{
    static const uint32_t kCycle[6] = {1, 8, 2, 4, 3, 16};
    ParticleSim sim;
    sim.setStepThreads(threads ? threads : 1);
    if (sim.stepThreads() != (threads ? threads : 1)) {
        std::printf("FAIL %s: setStepThreads(%u)\n", what, threads);
        ++g_fail;
        return false;
    }
    ParticleSettings any;
    any.spawnFrequency = 4000;
    if (!sim.reset(worldW, worldH, any, flow, 1).ok() || !sim.loadState(state.data(), state.size()).ok()) {
        std::printf("FAIL %s: the state does not load\n", what);
        ++g_fail;
        return false;
    }
    ref::Sim r;
    if (!r.load(state, flow) || r.save() != state) {
        std::printf("FAIL %s: the reference does not load or round-trip the state\n", what);
        ++g_fail;
        return false;
    }
    EqStats st;
    for (int i = 0; i < steps; ++i) {
        const std::array<float, 4>& f = fps[static_cast<size_t>(i) % fps.size()];
        const float lo[2] = {f[0], f[1]}, hi[2] = {f[2], f[3]};
        r.step(lo, hi);
        if (!threads) sim.setStepThreads(kCycle[i % 6]);
        if (!sim.step(lo, hi).ok()) { std::printf("FAIL %s: step %d\n", what, i); ++g_fail; return false; }
        if (sim.stats().moved != r.moved || sim.stats().lost != r.lost || sim.saveState() != r.save()) {
            std::printf("FAIL %s, %u threads: the state after step %d differs from the reference (moved %u / %u)\n", what,
                        sim.stepThreads(), i, sim.stats().moved, r.moved);
            ++g_fail;
            return false;
        }
        st.moved += r.moved;
        float vp[16];
        slantView(lo, hi, vp);
        std::vector<ParticleSprite> a, b;
        r.emit(vp, lo, hi, 8, 0.125f, 0.125f, a);
        if (!sim.emit(vp, lo, hi, 8, 0.125f, 0.125f, b).ok() || a.size() != b.size() ||
            (!a.empty() && std::memcmp(a.data(), b.data(), a.size() * sizeof(ParticleSprite)) != 0)) {
            std::printf("FAIL %s: the sprites of frame %d differ from the reference\n", what, i);
            ++g_fail;
            return false;
        }
        st.sprites += a.size();
        ref::Sim::sort(a);
        // 0692 cut C: the order of (key, index) pairs, gathered, is the old sort's
        std::vector<std::pair<float, uint32_t>> order;
        ParticleSim::sortOrder(b, order);
        std::vector<ParticleSprite> g;
        for (const auto& o : order) g.push_back(b[o.second]);
        if (g.size() != a.size() || (!a.empty() && std::memcmp(a.data(), g.data(), a.size() * sizeof(ParticleSprite)) != 0)) {
            std::printf("FAIL %s: the sprites in sortOrder's order of frame %d differ from the reference's sort\n", what, i);
            ++g_fail;
            return false;
        }
        ParticleSim::sortSprites(b);
        if (a.size() != b.size() || (!a.empty() && std::memcmp(a.data(), b.data(), a.size() * sizeof(ParticleSprite)) != 0)) {
            std::printf("FAIL %s: the sorted sprites of frame %d differ from the reference\n", what, i);
            ++g_fail;
            return false;
        }
        if (sim.saveState() != r.save()) {   // emit's rotation update
            std::printf("FAIL %s: the state after the emit of frame %d differs from the reference\n", what, i);
            ++g_fail;
            return false;
        }
        st.stepsDone++;
    }
    if (out) *out = st;
    return true;
}

// equalRun1 with the step on 1, 2, 4 and 8 threads and with a count that changes every step (0692 cut A)
bool equalRun(const char* what, const std::vector<uint8_t>& state, const FlowField& flow, float worldW, float worldH,
              const std::vector<std::array<float, 4>>& fps, int steps, EqStats* out)
{
    const uint32_t counts[5] = {1, 2, 4, 8, 0};
    for (uint32_t t : counts) {
        EqStats e;
        if (!equalRun1(what, state, flow, worldW, worldH, fps, steps, t, &e)) return false;
        if (out) *out = e;
    }
    return true;
}

// The byte offset of particle k of cell (x, y) in a saved state (cells before it counted from the state itself).
size_t particleOffset(const std::vector<uint8_t>& st, int32_t cellsX, int32_t x, int32_t y, uint32_t k)
{
    size_t at = ref::Sim::headSize();
    const size_t target = static_cast<size_t>(y) * cellsX + x;
    for (size_t c = 0; c < target; ++c) {
        uint32_t n;
        std::memcpy(&n, &st[at], 4);
        at += 4 + n * sizeof(Particle);
    }
    return at + 4 + k * sizeof(Particle);
}

void setParticle(std::vector<uint8_t>& st, size_t off, float x, float y, float mass)
{
    std::memcpy(&st[off + 0], &x, 4);
    std::memcpy(&st[off + 4], &y, 4);
    std::memcpy(&st[off + 20], &mass, 4);   // Particle: x y z vx vy mass ...
}

// 11. the 0692 cuts against the reference, synthetic states (no game files)
void checkReferenceSynthetic(const ParticleSettings& s, float W, float H)
{
    const std::vector<std::array<float, 4>> fps = {{{-200, -200, 200, 200}}, {{-500, -900, 300, 100}}, {{-600, -1100, 600, 1100}},
                                                   {{-90, 300, 420, 980}}};
    // (a) the plain lattice, no flow
    {
        ParticleSim a;
        CHECK(a.reset(W, H, s, zeroFlow(), 3).ok());
        EqStats e;
        CHECK(equalRun("lattice", a.saveState(), zeroFlow(), W, H, fps, 24, &e));
        std::printf("reference, lattice: %d steps, %llu moved, %llu sprites: equal\n", e.stepsDone,
                    static_cast<unsigned long long>(e.moved), static_cast<unsigned long long>(e.sprites));
        CHECK(e.moved > 1000);
    }
    // (b) dense (more than a cell holds: some are lost at spawn and at rebins) with a varying flow field
    {
        ParticleSettings d = s;
        d.spawnFrequency = 9;
        FlowField f;
        f.width = 64;
        f.height = 32;
        f.xy.resize(64 * 32 * 2);
        uint32_t g = 12345;
        for (float& v : f.xy) { g = g * 1664525u + 1013904223u; v = static_cast<float>(g >> 8) / 16777216.0f; }
        ParticleSim a;
        CHECK(a.reset(W, H, d, f, 4).ok());
        EqStats e;
        CHECK(equalRun("dense", a.saveState(), f, W, H, fps, 16, &e));
        std::printf("reference, dense: %d steps, %llu moved, %llu sprites, %u lost at spawn: equal\n", e.stepsDone,
                    static_cast<unsigned long long>(e.moved), static_cast<unsigned long long>(e.sprites), a.stats().lost);
        CHECK(a.stats().lost > 0 && e.moved > 10000);
    }
    // (c) pairs at exactly h (d2 = 36^2 + 48^2 = 3600), just inside the skip margin and just beyond it; a particle of
    // mass 0 alone (density 0, pressure NaN: its pairs beyond h give NaN, which the skip must not hide); a NaN mass
    {
        ParticleSim a;
        CHECK(a.reset(W, H, s, zeroFlow(), 3).ok());
        std::vector<uint8_t> st = a.saveState();
        int32_t cx, cy;
        a.cellOf(0, 0, cx, cy);
        CHECK(a.cell(cx, cy).size() >= 4 && a.cell(cx + 1, cy).size() >= 2 && a.cell(cx, cy + 1).size() >= 1);
        const float bx = a.originX() + cx * 64.0f + 8.0f, by = a.originY() + cy * 64.0f + 6.0f;   // whole numbers
        setParticle(st, particleOffset(st, a.cellsX(), cx, cy, 0), bx, by, 1.0f);
        setParticle(st, particleOffset(st, a.cellsX(), cx, cy, 1), bx + 36.0f, by + 48.0f, 1.5f);      // exactly h
        setParticle(st, particleOffset(st, a.cellsX(), cx, cy, 2), bx + 60.00049f, by, 2.0f);          // d2 below kSkipD2
        setParticle(st, particleOffset(st, a.cellsX(), cx, cy, 3), bx + 60.0008f, by, 2.0f);           // d2 above it
        setParticle(st, particleOffset(st, a.cellsX(), cx + 1, cy, 0), bx + 70.0f, by + 0.5f, 1.0f);
        setParticle(st, particleOffset(st, a.cellsX(), cx + 1, cy, 1), bx + 120.0f, by + 1.0f, 1.0f);   // beyond h of the first
        const std::vector<std::array<float, 4>> here = {{{bx - 100, by - 100, bx + 100, by + 100}}};
        CHECK(equalRun("pairs at h", st, zeroFlow(), W, H, here, 6, nullptr));
        std::vector<uint8_t> z = st;
        setParticle(z, particleOffset(z, a.cellsX(), cx, cy + 1, 0), bx + 30.0f, by + 400.0f, 0.0f);   // alone, mass 0
        CHECK(equalRun("mass 0", z, zeroFlow(), W, H, here, 6, nullptr));
        std::vector<uint8_t> n = st;
        setParticle(n, particleOffset(n, a.cellsX(), cx, cy, 2), bx + 60.00049f, by, NAN);
        CHECK(equalRun("NaN mass", n, zeroFlow(), W, H, here, 6, nullptr));
        std::printf("reference, pairs at h, mass 0, NaN mass: equal\n");
    }
    // (d) the thread count's clamp and the default rule (one for every 4 logical processors, 1 to 4)
    {
        ParticleSim a;
        CHECK(a.stepThreads() == 1);
        a.setStepThreads(0);
        CHECK(a.stepThreads() == 1);
        a.setStepThreads(1000);
        CHECK(a.stepThreads() == kMaxStepThreads);
        const uint32_t d = ParticleSim::defaultStepThreads();
        std::printf("step threads: default %u on %u logical processors\n", d, std::thread::hardware_concurrency());
        CHECK(d >= 1 && d <= 4 && d == std::min(4u, std::max(1u, std::thread::hardware_concurrency() / 4u)));
    }
}

std::wstring envW(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, v, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, v, -1, &w[0], n);
    return w;
}

bool readAll(const std::wstring& p, std::vector<uint8_t>& out)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") || !f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// 12. the 0692 cuts against the reference on every bundle's particles.bin (the install's flow field)
void checkReferenceBundles()
{
    const std::wstring game = envW("C5R_GAME_DIR");
    std::wstring lib = envW("C5_SCENE_LIBRARY");
    if (lib.empty()) lib = L"C:/c5-64bit-work/scene-library/bundles";
    const DWORD la = GetFileAttributesW(lib.c_str());
    if (game.empty() || la == INVALID_FILE_ATTRIBUTES || !(la & FILE_ATTRIBUTE_DIRECTORY)) {
        std::printf("NOTE reference on bundles not run: C5R_GAME_DIR %s, library %s\n", game.empty() ? "unset" : "set",
                    la == INVALID_FILE_ATTRIBUTES ? "absent" : "present");
        return;
    }
    gamefs::GameFiles fs;
    std::vector<uint8_t> bytes;
    FlowField flow;
    CHECK(fs.open(game).ok() && fs.read("flowfield.dds", bytes).ok() && flowFieldFromDds(bytes.data(), bytes.size(), flow).ok());
    if (flow.xy.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((lib + L"/*").c_str(), &fd);
    int checked = 0, refused = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            std::vector<uint8_t> pb;
            if (!readAll(lib + L"/" + fd.cFileName + L"/particles.bin", pb) || pb.size() < 40) continue;
            uint32_t flags, sbytes;
            float pw, ph;
            std::memcpy(&flags, &pb[16], 4);
            std::memcpy(&pw, &pb[24], 4);
            std::memcpy(&ph, &pb[28], 4);
            std::memcpy(&sbytes, &pb[32], 4);
            if (!(flags & 1) || sbytes + 40 != pb.size()) continue;
            const std::vector<uint8_t> st(pb.begin() + 40, pb.end());
            ParticleSim probe;
            ParticleSettings any;
            any.spawnFrequency = 4000;
            if (!probe.reset(pw, ph, any, flow, 1).ok() || !probe.loadState(st.data(), st.size()).ok()) {
                std::printf("NOTE bundle %ls: the state does not load with this install's flow field\n", fd.cFileName);
                ++refused;
                continue;
            }
            // footprints: a 20 x 14-cell box in the middle, one on the grid's low corner (the clamps), a wide thin one
            const float gx = probe.originX(), gy = probe.originY(), ex = probe.cellsX() * 64.0f, ey = probe.cellsY() * 64.0f;
            const float mx = gx + ex * 0.5f, my = gy + ey * 0.5f;
            const std::vector<std::array<float, 4>> fps = {{{mx - 640, my - 448, mx + 640, my + 448}},
                                                           {{gx - 50, gy - 50, gx + 900, gy + 700}},
                                                           {{gx + ex * 0.1f, my - 100, gx + ex * 0.9f, my + 150}}};
            EqStats e;
            char what[200];
            std::snprintf(what, sizeof what, "bundle %ls", fd.cFileName);
            if (equalRun(what, st, flow, pw, ph, fps, 9, &e)) {
                std::printf("reference, %s (%u particles): %d steps, %llu moved, %llu sprites: equal\n", what, probe.stats().particles,
                            e.stepsDone, static_cast<unsigned long long>(e.moved), static_cast<unsigned long long>(e.sprites));
                CHECK(e.moved > 0);
            }
            ++checked;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::printf("reference on bundles: %d checked, %d not loadable\n", checked, refused);
    CHECK(checked > 0);
}

} // namespace

int main()
{
    // 1. settings
    {
        const char xml[] = "<?xml version=\"1.0\"?><FluidFOWSettings><Speed>2.5</Speed><SpawnFrequency>30</SpawnFrequency>"
                           "<MassAndSizeCorrelation>.25f</MassAndSizeCorrelation><SizeMean>2</SizeMean><SizeMin>0.5</SizeMin>"
                           "<SizeMax>3</SizeMax><SizeDeviation>0.5</SizeDeviation><MassMean>2</MassMean><MassMin>.5</MassMin>"
                           "<MassMax>3</MassMax><MassDeviation>2</MassDeviation><FogHeight>7</FogHeight>"
                           "<HeightVariation>10.0f</HeightVariation><FluidDensity>40</FluidDensity><Drag>x</Drag>"
                           "<MasterSize>0.5</MasterSize></FluidFOWSettings>";
        ParticleSettings s;
        std::string note;
        Status st = parseParticleSettings(xml, sizeof xml - 1, s, &note);
        std::printf("settings: %s; %s\n", st.str().c_str(), note.c_str());
        CHECK(st.ok());
        CHECK(s.spawnFrequency == 30 && s.sizeMean == 2.0f && s.massSizeCorrelation == 0.25f && s.fogHeight == 7.0f &&
              s.heightVariation == 10.0f);
        // group 2: FluidDensity read, Speed read, Drag does not read -> MasterSize keeps its default
        CHECK(s.fluidDensity == 40.0f && s.speed == 2.5f && s.drag == 0.001f && s.masterSize == 1.0f);
        CHECK(note.find("Drag") != std::string::npos);
        // a missing value in group 1 stops it: SizeMin missing -> SizeMax .. HeightVariation keep defaults
        const char xml2[] = "<R><SpawnFrequency>10</SpawnFrequency><SizeMean>3</SizeMean><SizeMax>9</SizeMax><FogHeight>5</FogHeight>"
                            "<FluidDensity>1</FluidDensity></R>";
        ParticleSettings t;
        CHECK(parseParticleSettings(xml2, sizeof xml2 - 1, t).ok());
        CHECK(t.spawnFrequency == 10 && t.sizeMean == 3.0f && t.sizeMax == 4.0f && t.fogHeight == 0.0f && t.fluidDensity == 1.0f &&
              t.speed == 5.0f);
        ParticleSettings u;
        CHECK(!parseParticleSettings("not xml <", 9, u).ok());
    }
    // 2. flow field
    {
        const std::vector<uint8_t> dds = makeDds(8, 4, [](uint32_t x, uint32_t y, uint8_t& r, uint8_t& g) {
            r = static_cast<uint8_t>(x * 30);
            g = static_cast<uint8_t>(y * 60);
        });
        FlowField f;
        Status st = flowFieldFromDds(dds.data(), dds.size(), f);
        std::printf("flow: %s %u x %u\n", st.str().c_str(), f.width, f.height);
        CHECK(st.ok() && f.width == 8 && f.height == 4);
        CHECK(std::fabs(f.xy[(2 * 8 + 3) * 2] - 90 / 255.0f) < 1e-6f && std::fabs(f.xy[(2 * 8 + 3) * 2 + 1] - 120 / 255.0f) < 1e-6f);
        FlowField g;
        CHECK(!flowFieldFromDds(dds.data(), 100, g).ok());   // truncated
    }
    // 3. rand
    {
        CrtRand r;
        const int a = r.next(), b = r.next(), c = r.next();
        std::printf("rand: %d %d %d\n", a, b, c);
        CHECK(a == 41 && b == 18467 && c == 6334);   // the C runtime's first three values with seed 1
    }
    ParticleSettings s;   // the EXE's defaults
    s.spawnFrequency = 22;
    s.sizeMean = 2.5f; s.sizeMin = 1.0f; s.sizeMax = 4.0f;
    s.massMin = 0.25f; s.massMax = 4.0f; s.massSizeCorrelation = 0.5f;
    s.fogHeight = 11.0f; s.heightVariation = 50.0f;
    s.fluidDensity = 45.0f; s.speed = 3.5f; s.drag = 0.004f; s.masterSize = 0.8f; s.forceStrength = 3.5e-5f; s.maxRotate = 0.0025f;
    const float W = 1000.0f, H = 700.0f;   // nx = 16, ny = 31 cells: spawn box 1024 x 1984
    // 4. negatives before reset
    {
        ParticleSim q;
        const float a[2] = {0, 0};
        std::vector<ParticleSprite> out;
        CHECK(q.step(a, a).code == Err::NotFound);
        CHECK(q.emit(kTop, a, a, 8, 0.125f, 0.125f, out).code == Err::NotFound);
        CHECK(q.reset(-1, H, s, zeroFlow(), 1).code == Err::BadLayout);
        CHECK(q.reset(W, H, s, FlowField(), 1).code == Err::BadLayout);
        ParticleSettings bad = s;
        bad.spawnFrequency = 0;
        CHECK(q.reset(W, H, bad, zeroFlow(), 1).code == Err::BadLayout);
        CHECK(q.loadState(nullptr, 0).code == Err::NotFound);
    }
    // 5. spawn
    ParticleSim sim;
    CHECK(sim.reset(W, H, s, zeroFlow(), 1).ok());
    {
        Census c;
        sim.forEach(census, &c);
        const uint32_t lx = (1024 + 21) / 22, ly = (1984 + 21) / 22;   // lattice points per axis
        std::printf("spawn: %u particles (lattice %u), grid %d x %d, origin (%.0f, %.0f), size %.3f..%.3f mass %.3f..%.3f z %.2f..%.2f, "
                    "lost %u\n", c.n, lx * ly, sim.cellsX(), sim.cellsY(), sim.originX(), sim.originY(), c.minS, c.maxS, c.minM, c.maxM,
                    c.minZ, c.maxZ, sim.stats().lost);
        CHECK(sim.cellsX() == 18 && sim.cellsY() == 33);
        CHECK(sim.originX() == -576.0f && sim.originY() == -1056.0f);
        CHECK(c.n + sim.stats().lost == lx * ly && sim.stats().lost == 0);
        CHECK(c.minZ >= 11.0f && c.maxZ <= 11.0f + 0.2f * (50.0f + 0.2f));
        CHECK(c.maxFrame <= 64);
        // sizes and masses outside (min, max) only when 10 draws all missed: rare
        CHECK(c.minS > 0.0f && c.maxS < 8.0f);
        ParticleSim same, other;
        CHECK(same.reset(W, H, s, zeroFlow(), 1).ok() && other.reset(W, H, s, zeroFlow(), 7).ok());
        CHECK(same.saveState() == sim.saveState());
        CHECK(other.saveState() != sim.saveState());
    }
    // 6. step: footprint around (0, 0); far cells stand still; the inner box holds the moved particles
    {
        ParticleSim a, b;
        CHECK(a.reset(W, H, s, zeroFlow(), 3).ok() && b.reset(W, H, s, zeroFlow(), 3).ok());
        const float f0[2] = {-200, -200}, f1[2] = {200, 200};
        int32_t c0x, c0y, c1x, c1y;
        a.cellOf(f0[0], f0[1], c0x, c0y);
        a.cellOf(f1[0], f1[1], c1x, c1y);
        const std::vector<Particle> farBefore = a.cell(0, 0);
        for (int i = 0; i < 20; ++i) {
            CHECK(a.step(f0, f1).ok());
            CHECK(b.step(f0, f1).ok());
        }
        std::printf("step: moved %u of %u, active cells %u, lost %u\n", a.stats().moved, a.stats().particles, a.stats().activeCells,
                    a.stats().lost);
        CHECK(a.stats().moved > 0 && a.stats().activeCells > 0);
        CHECK(a.saveState() == b.saveState());   // repeatable
        const std::vector<Particle>& farAfter = a.cell(0, 0);
        CHECK(farAfter.size() == farBefore.size() &&
              (farAfter.empty() || std::memcmp(farAfter.data(), farBefore.data(), farAfter.size() * sizeof(Particle)) == 0));
        // inner box: cells (c0 - 3 + 1) .. (c1 + 3 - 1)
        const float wx0 = (c0x - 3 + 1) * 64.0f + a.originX(), wx1 = (c1x + 3 - 1) * 64.0f + a.originX();
        bool inside = true, anyVel = false;
        for (int32_t y = c0y - 2; y < c1y + 2; ++y)
            for (int32_t x = c0x - 2; x < c1x + 2; ++x)
                for (const Particle& p : a.cell(x, y)) {
                    inside = inside && p.x >= wx0 && p.x <= wx1;
                    anyVel = anyVel || p.vx != 0 || p.vy != 0;
                }
        CHECK(inside && anyVel);
    }
    // 7. repulsion of two particles and the flow's push (a one-particle world with a uniform flow)
    {
        ParticleSettings one = s;
        one.spawnFrequency = 4000;   // one lattice point: (x0, y0)
        FlowField push = zeroFlow();
        for (size_t i = 0; i < push.xy.size(); i += 2) { push.xy[i] = 1.0f; push.xy[i + 1] = 0.0f; }
        ParticleSim q;
        CHECK(q.reset(4000, 4000, one, push, 1).ok());
        CHECK(q.stats().particles == 4);   // lattice 2 x 2: the spawn box is 4032 x 5312
        // the particle sits at the spawn box's corner; its cell's footprint
        const float f[2] = {q.originX() + 64 * 5.0f, q.originY() + 64 * 5.0f};
        Census c0;
        q.forEach(census, &c0);
        CHECK(q.step(f, f).ok());
        // the particle is at the box's corner, outside a rectangle around cell 5: not moved
        CHECK(q.stats().moved == 0);
    }
    // 8. emit: sprites of the footprint's cells, rotation limit, corners, sort
    {
        ParticleSim a;
        CHECK(a.reset(W, H, s, zeroFlow(), 5).ok());
        const float f0[2] = {-200, -200}, f1[2] = {200, 200};
        for (int i = 0; i < 5; ++i) CHECK(a.step(f0, f1).ok());
        std::vector<ParticleSprite> sp;
        CHECK(a.emit(kTop, f0, f1, 8, 0.125f, 0.125f, sp).ok());
        int32_t ax, ay, bx, by;
        a.cellOf(f0[0], f0[1], ax, ay);
        a.cellOf(f1[0], f1[1], bx, by);
        uint32_t expect = 0;
        float maxRot = 0;
        for (int32_t x = ax; x < bx + 1; ++x)
            for (int32_t y = ay; y < by + 2; ++y)
                for (const Particle& p : a.cell(x, y)) { ++expect; maxRot = std::max(maxRot, std::min(std::fabs(p.rot), std::fabs(6.2831853f - p.rot))); }
        std::printf("emit: %zu sprites (expected %u), largest |rotation| %.5f after one emit\n", sp.size(), expect, maxRot);
        CHECK(sp.size() == expect && expect > 0);
        CHECK(maxRot <= s.maxRotate + 1e-5f);
        // a sprite is a square of side 2 e sqrt(2)-rotated: |v0 - v2| = 2 sqrt(2) e
        const ParticleSprite& q = sp[0];
        const float d02 = std::hypot(q.v[0][0] - q.v[2][0], q.v[0][1] - q.v[2][1]);
        CHECK(d02 > 2.0f * 1.41421f * 1.0f * 0.8f * 16.0f * 0.99f);   // size > SizeMin = 1
        CHECK(q.v[0][6] == q.v[1][6] && q.v[2][6] == q.v[3][6] && q.v[1][7] == q.v[2][7] && q.v[0][7] == q.v[3][7]);
        // sort: descending keys
        std::vector<ParticleSprite> k(3);
        k[0].key = 1; k[1].key = 3; k[2].key = 2;
        ParticleSim::sortSprites(k);
        CHECK(k[0].key == 3 && k[1].key == 2 && k[2].key == 1);
        // 0692 cut C: sortOrder against sortSprites on keys with many ties (+0 and -0 among them), and with NaN keys
        for (int withNan = 0; withNan < 2; ++withNan)
            for (uint32_t n : {0u, 1u, 31u, 33u, 500u, 6179u, 10240u}) {
                std::vector<ParticleSprite> v(n);
                uint32_t g = 777u + n;
                const float pool[7] = {0.0f, -0.0f, 1.0f, 2.5f, -3.0f, 1e30f, -1e-30f};
                for (uint32_t i = 0; i < n; ++i) {
                    g = g * 1664525u + 1013904223u;
                    v[i] = ParticleSprite();
                    v[i].key = (g >> 28) < 7 ? pool[g >> 28] : static_cast<float>(g >> 9) * 1e-3f;
                    if (withNan && (g & 0xFF) == 3) v[i].key = NAN;
                    v[i].v[0][0] = static_cast<float>(i);   // the identity of each sprite
                }
                std::vector<std::pair<float, uint32_t>> order;
                ParticleSim::sortOrder(v, order);
                std::vector<ParticleSprite> sorted = v;
                ParticleSim::sortSprites(sorted);
                bool same = order.size() == n;
                for (uint32_t i = 0; same && i < n; ++i) same = std::memcmp(&sorted[i], &v[order[i].second], sizeof(ParticleSprite)) == 0;
                if (!same) std::printf("FAIL sortOrder != sortSprites: n %u, NaN keys %d\n", n, withNan);
                CHECK(same);
            }
    }
    // 9. the state round trip: load the state of step 10 into a fresh sim; both go on byte-equal
    {
        ParticleSim a, b;
        CHECK(a.reset(W, H, s, zeroFlow(), 9).ok() && b.reset(W, H, s, zeroFlow(), 1).ok());
        const float f0[2] = {-150, -100}, f1[2] = {250, 300};
        for (int i = 0; i < 10; ++i) CHECK(a.step(f0, f1).ok());
        const std::vector<uint8_t> st = a.saveState();
        Status ls = b.loadState(st.data(), st.size());
        std::printf("state: %zu bytes, load %s\n", st.size(), ls.str().c_str());
        CHECK(ls.ok());
        CHECK(b.saveState() == st);
        for (int i = 0; i < 10; ++i) {
            CHECK(a.step(f0, f1).ok());
            CHECK(b.step(f0, f1).ok());
        }
        std::vector<ParticleSprite> sa, sb;
        CHECK(a.emit(kTop, f0, f1, 8, 0.125f, 0.125f, sa).ok() && b.emit(kTop, f0, f1, 8, 0.125f, 0.125f, sb).ok());
        CHECK(sa.size() == sb.size() && !sa.empty() && std::memcmp(sa.data(), sb.data(), sa.size() * sizeof(ParticleSprite)) == 0);
        CHECK(a.saveState() == b.saveState());
        // negatives: truncated, bad magic, bytes after the end, another flow field
        CHECK(b.loadState(st.data(), st.size() - 1).code == Err::Truncated);
        std::vector<uint8_t> bm = st;
        bm[0] = 'X';
        CHECK(b.loadState(bm.data(), bm.size()).code == Err::BadMagic);
        std::vector<uint8_t> extra = st;
        extra.push_back(0);
        CHECK(b.loadState(extra.data(), extra.size()).code == Err::BadLayout);
        FlowField other = zeroFlow();
        other.xy[0] = 1.0f;
        ParticleSim c;
        CHECK(c.reset(W, H, s, other, 1).ok());
        CHECK(c.loadState(st.data(), st.size()).code == Err::BadLayout);
        CHECK(a.saveState() == b.saveState());   // the failed loads left b as it was
    }
    // 10. non-finite inputs
    {
        const float nan2[2] = {NAN, 0}, ok2[2] = {0, 0};
        std::vector<ParticleSprite> out;
        CHECK(sim.step(nan2, ok2).code == Err::BadLayout);
        float bad[16];
        std::memcpy(bad, kTop, sizeof bad);
        bad[5] = INFINITY;
        CHECK(sim.emit(bad, ok2, ok2, 8, 0.125f, 0.125f, out).code == Err::BadLayout);
    }
    // 11-12. ticket 0692: the cuts against the code before them
    checkReferenceSynthetic(s, W, H);
    checkReferenceBundles();
    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
