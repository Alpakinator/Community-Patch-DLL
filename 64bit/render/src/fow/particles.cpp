// particles.cpp - c5r::fow::ParticleSim and ParticleRenderer: the FOWLevel-3 fog particles of the server (ticket 0340), a
// look-alike of the EXE's FluidParticles. Own code, written from the behaviour described in
// research/05-graphics-composition/fog-of-war.md section 10.1 (D83: no decompiler output here). The FOWParticleShader
// program is read from the player's EXE and fx_fog_dif.dds, flowfield.dds and FluidFOWSettings.xml from the player's
// install at run time; nothing of either is in this file.
#include <c5r/fow/fow.h>

#include <windows.h>
#include <d3d11.h>
#include <xmmintrin.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "c5r/gamefs/civ5pkg.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"
#include "c5r/water/water.h"

namespace c5r {
namespace fow {

namespace {

template <class T> void rel(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const char* what)
{
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

bool finiteN(const float* m, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// Stock's constants (fog-of-war.md 10.1) [C].
const float kH = 60.0f;                                   // smoothing length
const double kPi = 3.14159265358979323846;
const float kSpiky = static_cast<float>(15.0 / (kPi * 60.0 * 60.0 * 60.0 * 60.0 * 60.0 * 60.0));   // 15 / (pi h^6)
const float kMinR = 0.001f;                               // the force pass's smallest distance
// 0692 cut B: the squared distance beyond which a neighbour pair adds exactly +-0 to the force sums of ParticleSim::step,
// so it is skipped there (fog-of-war.md 12.5; the density pass computes every pair: its body is cheaper than the
// mispredicted branch). (60 (1 + 1e-5))^2, a margin above h^2 = 3600. Why the skip changes no bit, with d2 > kSkipD2,
// so r = sqrt(d2) >= 60.0006 > h (sqrt is correctly rounded and monotonic):
//  - force: in = (r <= h) is false, so w = +0, and w dx t is +-0 when dx and t are finite (d2 finite and t finite are
//    tested; a NaN or inf t, e.g. a zero density, is computed as before and spreads its NaN exactly as before);
//  - the sums start at +0 and never become -0 (round to nearest; the step runs with the thread's default MXCSR, no
//    flush-to-zero: x - x and +0 + -0 give +0), and adding or subtracting +-0 leaves any other float unchanged.
// No FMA contraction (x64 SSE, /fp:precise), so d2 = dx dx + dy dy is the value sqrt read before.
const float kSkipD2 = 3600.072f;
const float kHeightScale = 0.2f;                          // the global height scale 0xB9A6DC
const float kHeightBias = 0.198367f;                      // spawn z: 0.2 (r HeightVariation + 0.198367) + FogHeight
const float kSizeScale = 16.0f;                           // half extent = size x MasterSize x 16
const double kTwoPi = 2.0 * kPi;
const int kSpawnTries = 10;
const uint32_t kSlotsCb = 0x90;                           // FOWParticleShader's own buffer (rows 0-3 M, 4-7 OverlayProj, 8 = 1)
const char kFogFile[] = "fx_fog_dif.dds";
const char kFlowFile[] = "flowfield.dds";
const char kSettingsFile[] = "fluidfowsettings.xml";

// a C++ stream's read of a number, as the EXE's reader (a stringstream >> float): leading blanks, then the longest number
// prefix; ".5f" -> 0.5; fails when nothing reads
bool readFloat(const std::string& s, float& out)
{
    std::istringstream is(s);
    float v = 0;
    is >> v;
    if (is.fail()) return false;
    out = v;
    return true;
}
bool readInt(const std::string& s, int32_t& out)
{
    std::istringstream is(s);
    int32_t v = 0;
    is >> v;
    if (is.fail()) return false;
    out = v;
    return true;
}

uint64_t fnv1a(const void* p, size_t n, uint64_t h = 1469598103934665603ull)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

water::World toWaterWorld(const CloudWorld& c)
{
    water::World w;
    w.minX = c.minX;
    w.minY = c.minY;
    w.maxX = c.maxX;
    w.maxY = c.maxY;
    w.width = c.maxX - c.minX;
    w.height = c.maxY - c.minY;
    w.sx = c.sx;
    w.sy = c.sy;
    w.samplesX = c.samplesX;
    w.samplesY = c.samplesY;
    w.valid = true;
    return w;
}

} // namespace

// ---- settings and the flow field ----

Status parseParticleSettings(const char* xml, size_t size, ParticleSettings& out, std::string* note)
{
    std::unique_ptr<gamefs::XmlNode> root;
    C5R_TRY(gamefs::parseXml(xml, size, root));
    if (!root) return Status::fail(Err::BadLayout, "FluidFOWSettings.xml: no root element");
    ParticleSettings s;
    std::string missed;
    auto get = [&](const char* name, float* f, int32_t* i) -> bool {
        const gamefs::XmlNode* n = root->child(name);
        const bool ok = n && (f ? readFloat(n->text, *f) : readInt(n->text, *i));
        if (!ok) missed += std::string(missed.empty() ? "" : ", ") + name;
        return ok;
    };
    // group 1: a value that does not read stops the group (fog-of-war.md 10.1)
    get("SpawnFrequency", nullptr, &s.spawnFrequency) && get("SizeMean", &s.sizeMean, nullptr) &&
        get("SizeMin", &s.sizeMin, nullptr) && get("SizeMax", &s.sizeMax, nullptr) &&
        get("SizeDeviation", &s.sizeDeviation, nullptr) && get("MassMean", &s.massMean, nullptr) &&
        get("MassMin", &s.massMin, nullptr) && get("MassMax", &s.massMax, nullptr) &&
        get("MassDeviation", &s.massDeviation, nullptr) && get("MassAndSizeCorrelation", &s.massSizeCorrelation, nullptr) &&
        get("FogHeight", &s.fogHeight, nullptr) && get("HeightVariation", &s.heightVariation, nullptr);
    // group 2
    get("FluidDensity", &s.fluidDensity, nullptr) && get("Speed", &s.speed, nullptr) && get("Drag", &s.drag, nullptr) &&
        get("MasterSize", &s.masterSize, nullptr) && get("ForceStrength", &s.forceStrength, nullptr) &&
        get("MaxRotate", &s.maxRotate, nullptr);
    if (note) *note = missed.empty() ? std::string() : "not read (defaults kept from there in its group): " + missed;
    out = s;
    return Status::Ok();
}

Status flowFieldFromDds(const uint8_t* data, size_t size, FlowField& out)
{
    out = FlowField();
    gamefs::DdsImage img;
    C5R_TRY(gamefs::parseDds(data, size, img));
    if (img.subresources.empty() || img.width == 0 || img.height == 0)
        return Status::fail(Err::BadLayout, "flowfield.dds: no image");
    int ri = -1, gi = -1;
    switch (img.format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM: ri = 2; gi = 1; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM: ri = 0; gi = 1; break;
    default:
        return Status::fail(Err::Unsupported, "flowfield.dds: format " + gamefs::dxgiFormatName(img.format) +
                                                  " (an uncompressed 32-bit RGBA or BGRA file is expected)");
    }
    const gamefs::DdsSubresource& s0 = img.subresources[0];
    const uint8_t* px = img.pixels() + s0.offset;
    out.width = img.width;
    out.height = img.height;
    out.xy.resize(static_cast<size_t>(img.width) * img.height * 2);
    for (uint32_t y = 0; y < img.height; ++y)
        for (uint32_t x = 0; x < img.width; ++x) {
            const uint8_t* t = px + static_cast<size_t>(y) * s0.rowPitch + x * 4u;
            float* o = &out.xy[(static_cast<size_t>(y) * img.width + x) * 2];
            o[0] = t[ri] / 255.0f;
            o[1] = t[gi] / 255.0f;
        }
    return Status::Ok();
}

// ---- ParticleSim ----

// 0692 cut A: the step's helper threads and the per-band lists. run(threads, n, f) calls f(0) .. f(n - 1) once each, on
// the calling thread and threads - 1 helpers (started at the first run that needs them), and returns when all n are
// done. A task is claimed with a counter tagged by the run's number, so a helper that wakes late can never take a task
// of a later run; the caller waits only for the tasks, never for an idle helper.
struct ParticleSim::StepPool {
    std::vector<std::vector<Particle>> moved;   // pass 2's output, one list a band
    std::vector<uint32_t> count;                // particles moved a band
    std::vector<std::thread> helpers;
    std::mutex mu;
    std::condition_variable wake, done;
    bool quit = false;
    uint32_t gen = 0;
    void (*fn)(void*, uint32_t) = nullptr;
    void* ctx = nullptr;
    uint32_t tasks = 0;
    unsigned int csr = 0;
    std::atomic<uint64_t> claim{0};       // (run number << 32) | the next task
    std::atomic<uint32_t> completed{0};   // tasks done in the current run

    ~StepPool()
    {
        {
            std::lock_guard<std::mutex> lk(mu);
            quit = true;
        }
        wake.notify_all();
        for (std::thread& t : helpers) t.join();
    }
    void work(uint32_t g, void (*f)(void*, uint32_t), void* c, uint32_t n)
    {
        for (;;) {
            uint64_t v = claim.load(std::memory_order_acquire);
            for (;;) {
                if (static_cast<uint32_t>(v >> 32) != g || static_cast<uint32_t>(v) >= n) return;
                if (claim.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel)) break;
            }
            f(c, static_cast<uint32_t>(v));
            if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 == n) {
                std::lock_guard<std::mutex> lk(mu);
                done.notify_all();
            }
        }
    }
    void loop()
    {
        std::unique_lock<std::mutex> lk(mu);
        uint32_t seen = gen;
        for (;;) {
            wake.wait(lk, [&] { return quit || gen != seen; });
            if (quit) return;
            seen = gen;
            void (*f)(void*, uint32_t) = fn;
            void* c = ctx;
            const uint32_t n = tasks;
            const unsigned int m = csr;
            lk.unlock();
            _mm_setcsr(m);   // the caller's rounding and denormal modes: the same arithmetic on every thread
            work(seen, f, c, n);
            lk.lock();
        }
    }
    template <class F> static void thunk(void* c, uint32_t i) { (*static_cast<F*>(c))(i); }
    template <class F> void run(uint32_t threads, uint32_t n, F& f)
    {
        if (threads <= 1 || n <= 1) {
            for (uint32_t i = 0; i < n; ++i) f(i);
            return;
        }
        while (helpers.size() + 1 < threads) helpers.emplace_back([this] { loop(); });
        uint32_t g;
        {
            std::lock_guard<std::mutex> lk(mu);
            fn = &thunk<F>;
            ctx = &f;
            tasks = n;
            csr = _mm_getcsr();
            completed.store(0, std::memory_order_relaxed);
            g = ++gen;
            claim.store(static_cast<uint64_t>(g) << 32, std::memory_order_release);
        }
        wake.notify_all();
        work(g, &thunk<F>, &f, n);
        std::unique_lock<std::mutex> lk(mu);
        done.wait(lk, [&] { return completed.load(std::memory_order_acquire) == n; });
    }
};

ParticleSim::ParticleSim() = default;
ParticleSim::~ParticleSim() = default;

void ParticleSim::setStepThreads(uint32_t n)
{
    n = std::min(std::max(n, 1u), kMaxStepThreads);
    if (n == m_threads) return;
    m_pool.reset();   // the helpers of the old count end here; the next step starts the new count's
    m_threads = n;
}

uint32_t ParticleSim::defaultStepThreads()
{
    // one step thread for every 4 logical processors, 1 to 4: the workstation (32) 4, the laptop and oldpc (8) 2, a
    // 4-thread machine 1; the step leaves at least three quarters of the machine to the render thread, the EXE and the game
    const unsigned int hc = std::thread::hardware_concurrency();
    return std::min(4u, std::max(1u, hc / 4u));
}

void ParticleSim::cellOf(float x, float y, int32_t& cx, int32_t& cy) const
{
    // stock: the integer part, clamped to one unit outside the grid, minus the origin, shifted by 6; then into the grid
    const double gx1 = static_cast<double>(m_gx) + static_cast<double>(m_cx) * kParticleCellSize;
    const double gy1 = static_cast<double>(m_gy) + static_cast<double>(m_cy) * kParticleCellSize;
    double ix = std::trunc(static_cast<double>(x)), iy = std::trunc(static_cast<double>(y));
    ix = std::min(std::max(ix, static_cast<double>(m_gx) - 1.0), gx1 + 1.0);
    iy = std::min(std::max(iy, static_cast<double>(m_gy) - 1.0), gy1 + 1.0);
    cx = static_cast<int32_t>(std::floor((ix - m_gx) / kParticleCellSize));
    cy = static_cast<int32_t>(std::floor((iy - m_gy) / kParticleCellSize));
    cx = std::min(std::max(cx, 0), m_cx - 1);
    cy = std::min(std::max(cy, 0), m_cy - 1);
}

void ParticleSim::bin(const Particle& p, std::vector<std::vector<Particle>>& cells)
{
    int32_t cx, cy;
    cellOf(p.x, p.y, cx, cy);
    std::vector<Particle>& c = cells[static_cast<size_t>(cy) * m_cx + cx];
    if (c.size() >= kParticleCellCap) {
        m_stats.lost++;
        return;
    }
    c.push_back(p);
}

Status ParticleSim::reset(float worldWidth, float worldHeight, const ParticleSettings& s, const FlowField& flow, uint32_t seed,
                          uint32_t atlasFrames)
{
    m_ready = false;
    m_cells.clear();
    m_stats = ParticleStats();
    if (!(std::isfinite(worldWidth) && worldWidth > 0 && std::isfinite(worldHeight) && worldHeight > 0))
        return Status::fail(Err::BadLayout, "ParticleSim::reset: world size not finite or not > 0");
    if (s.spawnFrequency < 1) return Status::fail(Err::BadLayout, "ParticleSim::reset: SpawnFrequency below 1");
    if (flow.width == 0 || flow.height == 0 || flow.xy.size() != static_cast<size_t>(flow.width) * flow.height * 2)
        return Status::fail(Err::BadLayout, "ParticleSim::reset: empty or inconsistent flow field");
    if (atlasFrames == 0) return Status::fail(Err::BadLayout, "ParticleSim::reset: atlas has no frame");
    const float vals[] = {s.sizeMean, s.sizeMin, s.sizeMax, s.sizeDeviation, s.massMean, s.massMin, s.massMax, s.massDeviation,
                          s.massSizeCorrelation, s.fogHeight, s.heightVariation, s.fluidDensity, s.speed, s.drag,
                          s.masterSize, s.forceStrength, s.maxRotate};
    if (!finiteN(vals, static_cast<int>(sizeof vals / sizeof vals[0])))
        return Status::fail(Err::BadLayout, "ParticleSim::reset: a setting not finite");
    // the spawn box (fog-of-war.md 10.1): nx = floor(W / 64) + 1, ny = floor(H / 64) + 21 cells, centred
    const int32_t nx = static_cast<int32_t>(worldWidth) / 64 + 1;
    const int32_t ny = static_cast<int32_t>(worldHeight) / 64 + 21;
    if (nx + 2 > 4096 || ny + 2 > 4096) return Status::fail(Err::BadLayout, "ParticleSim::reset: grid above 4096 cells a side");
    const int32_t x0 = -32 * nx, x1 = 32 * nx, y0 = -32 * ny, y1 = 32 * ny;
    m_set = s;
    m_flow = flow;
    m_flowHash = fnv1a(flow.xy.data(), flow.xy.size() * sizeof(float), fnv1a(&flow.width, 8));
    m_frames = atlasFrames;
    m_cx = nx + 2;
    m_cy = ny + 2;
    m_gx = static_cast<float>(x0 - kParticleCellSize);
    m_gy = static_cast<float>(y0 - kParticleCellSize);
    m_cells.assign(static_cast<size_t>(m_cx) * m_cy, std::vector<Particle>());
    m_rand.state = seed;
    const float sdS = std::sqrt(s.sizeDeviation), sdM = std::sqrt(s.massDeviation);
    const float c = s.massSizeCorrelation * 0.5f;
    uint32_t n = 0;
    for (int32_t x = x0; x < x1; x += s.spawnFrequency)
        for (int32_t y = y0; y < y1; y += s.spawnFrequency) {
            float size = 0, mass = 0;
            for (int tries = 0; tries < kSpawnTries; ++tries) {
                float u, v, q;
                do {
                    u = static_cast<float>(m_rand.next()) * (1.0f / 32768.0f) * 2.0f - 1.0f;
                    v = static_cast<float>(m_rand.next()) * (1.0f / 32768.0f) * 2.0f - 1.0f;
                    q = u * u + v * v;
                } while (q >= 1.0f);
                const float f = q <= 1.0e-4f ? 0.0f : static_cast<float>(std::sqrt(std::log(static_cast<double>(q)) / q * -2.0));
                size = ((1.0f - c) * u + v * c) * f * sdS + s.sizeMean;
                mass = ((1.0f - c) * v + u * c) * f * sdM + s.massMean;
                if (size > s.sizeMin && size < s.sizeMax && mass > s.massMin && mass < s.massMax) break;
            }
            Particle p;
            p.x = static_cast<float>(x);
            p.y = static_cast<float>(y);
            p.z = (static_cast<float>(m_rand.next()) * (1.0f / 32768.0f) * s.heightVariation + kHeightBias) * kHeightScale + s.fogHeight;
            const uint32_t frame = static_cast<uint32_t>(m_rand.next()) * atlasFrames / 32767u;
            ++n;
            p.id = (n << 8) | (frame & 0xFFu);
            p.mass = mass;
            p.size = size;
            bin(p, m_cells);
        }
    m_stats.particles = 0;
    for (const auto& cl : m_cells) m_stats.particles += static_cast<uint32_t>(cl.size());
    m_ready = true;
    return Status::Ok();
}

Status ParticleSim::step(const float fpMin[2], const float fpMax[2])
{
    if (!m_ready) return Status::fail(Err::NotFound, "ParticleSim::step: no reset");
    if (!finiteN(fpMin, 2) || !finiteN(fpMax, 2)) return Status::fail(Err::BadLayout, "ParticleSim::step: footprint not finite");
    const int32_t m = kParticleMargin;
    int32_t a0x, a0y, a1x, a1y;
    cellOf(fpMin[0], fpMin[1], a0x, a0y);
    cellOf(fpMax[0], fpMax[1], a1x, a1y);
    const int32_t cx0 = std::max(a0x - m, m), cy0 = std::max(a0y - m, m);
    const int32_t cx1 = std::min(a1x + m, m_cx - m), cy1 = std::min(a1y + m, m_cy - m);
    m_stats.steps++;
    m_stats.moved = 0;
    m_stats.activeCells = 0;
    if (cx1 - cx0 < 3 || cy1 - cy0 < 3) return Status::Ok();   // no inner cell
    m_stats.activeCells = static_cast<uint32_t>((cx1 - cx0) * (cy1 - cy0));
    const ParticleSettings& s = m_set;
    const float h = kH;
    auto cellAt = [&](int32_t x, int32_t y) -> std::vector<Particle>& { return m_cells[static_cast<size_t>(y) * m_cx + x]; };
    // 0692 cut A: passes 1 and 2 run in bands of cell rows, on the calling thread and the pool's helpers. Pass 1 writes
    // only each particle's own density and pressure and reads only positions and masses, which it does not change; pass
    // 2 reads what pass 1 wrote (the pool waits for all of pass 1 first) and writes only its band's own list; the lists
    // are rebinned in band order, which is the serial loop's order (y outer, x inner). The same arithmetic per particle
    // on every thread (the helpers take the caller's MXCSR), so the state is byte-identical for any thread count.
    if (!m_pool) m_pool.reset(new StepPool());
    StepPool& pool = *m_pool;
    const int32_t rows = cy1 - cy0;
    const uint32_t threads = std::min(std::max(m_threads, 1u), kMaxStepThreads);
    const uint32_t bands = threads > 1 ? static_cast<uint32_t>(std::min<int32_t>(rows, static_cast<int32_t>(threads * 4))) : 1u;
    if (pool.moved.size() < bands) {
        pool.moved.resize(bands);
        pool.count.resize(bands);
    }
    auto bandY = [&](uint32_t b) { return cy0 + static_cast<int32_t>(static_cast<int64_t>(rows) * b / bands); };
    // 1. density and pressure of every particle of the rectangle (neighbours: the 3 x 3 cells around, inside the grid)
    auto densityBand = [&](uint32_t b) {
    for (int32_t y = bandY(b); y < bandY(b + 1); ++y)
        for (int32_t x = cx0; x < cx1; ++x)
            for (Particle& p : cellAt(x, y)) {
                float rho = 0;
                for (int32_t ny = std::max(y - 1, 0); ny < std::min(y + 2, m_cy); ++ny)
                    for (int32_t nx = std::max(x - 1, 0); nx < std::min(x + 2, m_cx); ++nx)
                        for (const Particle& q : cellAt(nx, ny)) {
                            const float dx = p.x - q.x, dy = p.y - q.y;
                            // every pair (0692: skipping those beyond h here made the pass 52 % slower on oldpc: the
                            // body is cheaper than the mispredicted branch; fog-of-war.md 12.5)
                            const float r = std::sqrt(dx * dx + dy * dy);
                            const float k = h - r;
                            rho += std::max(k * k * k * kSpiky, 0.0f) * q.mass;
                        }
                p.density = rho;
                p.pressure = p.mass / (s.fluidDensity * rho * 2.0f);
            }
    };
    pool.run(threads, bands, densityBand);
    // 2. force and move inside, copy on the ring; all into new cells
    const float wx0 = static_cast<float>(cx0 + 1) * kParticleCellSize + m_gx, wy0 = static_cast<float>(cy0 + 1) * kParticleCellSize + m_gy;
    const float wx1 = static_cast<float>(cx1 - 1) * kParticleCellSize + m_gx, wy1 = static_cast<float>(cy1 - 1) * kParticleCellSize + m_gy;
    const float px = wx1 - wx0, py = wy1 - wy0;
    const float k3 = -3.0f * kSpiky;
    auto forceBand = [&](uint32_t b) {
    std::vector<Particle>& moved = pool.moved[b];
    moved.clear();
    uint32_t movedCount = 0;
    for (int32_t y = bandY(b); y < bandY(b + 1); ++y)
        for (int32_t x = cx0; x < cx1; ++x) {
            const bool ring = x < cx0 + 1 || x >= cx1 - 1 || y < cy0 + 1 || y >= cy1 - 1;
            for (const Particle& p : cellAt(x, y)) {
                if (ring) { moved.push_back(p); continue; }
                float fx = 0, fy = 0;
                for (int32_t ny = std::max(y - 1, 0); ny < std::min(y + 2, m_cy); ++ny)
                    for (int32_t nx = std::max(x - 1, 0); nx < std::min(x + 2, m_cx); ++nx)
                        for (const Particle& q : cellAt(nx, ny)) {
                            const float dx = p.x - q.x, dy = p.y - q.y;
                            const float d2 = dx * dx + dy * dy;
                            const float t = (q.density + p.density) * q.pressure;
                            // 0692 cut B: a pair beyond h adds exactly +-0 here (see kSkipD2); skipped
                            if (d2 > kSkipD2 && d2 <= FLT_MAX && std::fabs(t) <= FLT_MAX) continue;
                            float r = std::sqrt(d2);
                            const bool in = r <= h;
                            r = std::max(r, kMinR);
                            const float k = h - r;
                            const float w = in ? k * k * (1.0f / r) * k3 : 0.0f;
                            fx -= w * dx * t;
                            fy -= w * dy * t;
                        }
                // the flow field's texel: one per world unit, centred, unsigned wrap
                const uint32_t fw = m_flow.width, fh = m_flow.height;
                const uint32_t ix = (static_cast<uint32_t>(static_cast<int32_t>(p.x)) + (fw >> 1)) % fw;
                const uint32_t iy = ((fh >> 1) - static_cast<uint32_t>(static_cast<int32_t>(p.y))) % fh;
                const float* fl = &m_flow.xy[(static_cast<size_t>(iy) * fw + ix) * 2];
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
                moved.push_back(o);
                movedCount++;
            }
        }
    pool.count[b] = movedCount;
    };
    pool.run(threads, bands, forceBand);
    // 3. rebin the rectangle (at most 32 a cell; more are lost, counted), the bands' lists in band order
    for (int32_t y = cy0; y < cy1; ++y)
        for (int32_t x = cx0; x < cx1; ++x) cellAt(x, y).clear();
    for (uint32_t b = 0; b < bands; ++b) {
        for (const Particle& p : pool.moved[b]) bin(p, m_cells);
        m_stats.moved += pool.count[b];
    }
    m_stats.particles = 0;
    for (const auto& cl : m_cells) m_stats.particles += static_cast<uint32_t>(cl.size());
    return Status::Ok();
}

Status ParticleSim::emit(const float viewProj[16], const float fpMin[2], const float fpMax[2], uint32_t atlasPerRow, float du,
                         float dv, std::vector<ParticleSprite>& out)
{
    if (!m_ready) return Status::fail(Err::NotFound, "ParticleSim::emit: no reset");
    if (!viewProj || !finiteN(viewProj, 16) || !finiteN(fpMin, 2) || !finiteN(fpMax, 2) || atlasPerRow == 0 ||
        !std::isfinite(du) || !std::isfinite(dv))
        return Status::fail(Err::BadLayout, "ParticleSim::emit: a matrix, the footprint or the atlas not valid");
    int32_t ax, ay, bx, by;
    cellOf(fpMin[0], fpMin[1], ax, ay);
    cellOf(fpMax[0], fpMax[1], bx, by);
    const int32_t x0 = std::max(ax, 0), y0 = std::max(ay, 0);
    const int32_t x1 = std::min(bx + 1, m_cx - 1), y1 = std::min(by + 2, m_cy - 1);
    const float e0 = m_set.masterSize * kSizeScale;
    for (int32_t x = x0; x < x1; ++x)
        for (int32_t y = y0; y < y1; ++y)
            for (Particle& p : m_cells[static_cast<size_t>(y) * m_cx + x]) {
                if (out.size() >= kMaxParticleSprites) {
                    m_stats.spritesDropped++;
                    continue;
                }
                // rotation toward the velocity's direction, at most MaxRotate a drawn frame, the shorter way round
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
                d = std::min(std::max(d, -m_set.maxRotate), m_set.maxRotate);
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
    m_stats.sprites = static_cast<uint32_t>(out.size());
    return Status::Ok();
}

void ParticleSim::sortSprites(std::vector<ParticleSprite>& s)
{
    std::stable_sort(s.begin(), s.end(), [](const ParticleSprite& a, const ParticleSprite& b) { return a.key > b.key; });
}

// 0692 cut C: the same stable sort with the same comparison on (key, index) pairs, 8 bytes instead of 132 a sprite. With
// finite keys the comparison is a strict weak order and a stable sort's result is unique, so the permutation is
// sortSprites'. (Even with NaN keys the library's stable_sort makes the same comparisons from the keys and the count
// alone while it gets its temporary buffer, so it stays the same; tested in particles_test.)
void ParticleSim::sortOrder(const std::vector<ParticleSprite>& s, std::vector<std::pair<float, uint32_t>>& order)
{
    order.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) order[i] = std::make_pair(s[i].key, static_cast<uint32_t>(i));
    std::stable_sort(order.begin(), order.end(),
                     [](const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) { return a.first > b.first; });
}

void ParticleSim::forEach(void (*fn)(const Particle&, void*), void* ctx) const
{
    for (const auto& c : m_cells)
        for (const Particle& p : c) fn(p, ctx);
}

// The state: "C5FP" u32 version 1, settings (as stored), flow width / height / hash, frames, grid (cx, cy, gx, gy), the
// generator, the lost count and steps, then per cell u32 count and its particles (44 bytes each, little-endian floats).
std::vector<uint8_t> ParticleSim::saveState() const
{
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n) { b.insert(b.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); };
    const char magic[4] = {'C', '5', 'F', 'P'};
    const uint32_t version = 1;
    put(magic, 4);
    put(&version, 4);
    put(&m_set, sizeof m_set);
    put(&m_flow.width, 4);
    put(&m_flow.height, 4);
    put(&m_flowHash, 8);
    put(&m_frames, 4);
    put(&m_cx, 4);
    put(&m_cy, 4);
    put(&m_gx, 4);
    put(&m_gy, 4);
    put(&m_rand.state, 4);
    put(&m_stats.lost, 4);
    put(&m_stats.steps, 4);
    for (const auto& c : m_cells) {
        const uint32_t n = static_cast<uint32_t>(c.size());
        put(&n, 4);
        for (const Particle& p : c) put(&p, sizeof(Particle));
    }
    return b;
}

Status ParticleSim::loadState(const uint8_t* data, size_t size)
{
    if (!m_ready) return Status::fail(Err::NotFound, "ParticleSim::loadState: reset first (the flow field comes from the install)");
    size_t at = 0;
    auto get = [&](void* p, size_t n) -> bool {
        if (!data || at + n > size) return false;
        std::memcpy(p, data + at, n);
        at += n;
        return true;
    };
    char magic[4];
    uint32_t version = 0;
    if (!get(magic, 4) || !get(&version, 4)) return Status::fail(Err::Truncated, "particle state: header");
    if (std::memcmp(magic, "C5FP", 4) != 0) return Status::fail(Err::BadMagic, "particle state: magic");
    if (version != 1) return Status::fail(Err::BadVersion, "particle state: version " + std::to_string(version));
    ParticleSettings s;
    uint32_t fw = 0, fh = 0, frames = 0, rs = 0, lost = 0, steps = 0;
    uint64_t hash = 0;
    int32_t cx = 0, cy = 0;
    float gx = 0, gy = 0;
    if (!get(&s, sizeof s) || !get(&fw, 4) || !get(&fh, 4) || !get(&hash, 8) || !get(&frames, 4) || !get(&cx, 4) || !get(&cy, 4) ||
        !get(&gx, 4) || !get(&gy, 4) || !get(&rs, 4) || !get(&lost, 4) || !get(&steps, 4))
        return Status::fail(Err::Truncated, "particle state: fields");
    if (fw != m_flow.width || fh != m_flow.height || hash != m_flowHash)
        return Status::fail(Err::BadLayout, "particle state: another flow field than the install's");
    if (cx <= 0 || cy <= 0 || cx > 4096 || cy > 4096 || frames == 0 || !std::isfinite(gx) || !std::isfinite(gy))
        return Status::fail(Err::BadLayout, "particle state: grid not valid");
    std::vector<std::vector<Particle>> cells(static_cast<size_t>(cx) * cy);
    for (auto& c : cells) {
        uint32_t n = 0;
        if (!get(&n, 4)) return Status::fail(Err::Truncated, "particle state: cell count");
        if (n > kParticleCellCap) return Status::fail(Err::BadLayout, "particle state: a cell above 32 particles");
        c.resize(n);
        for (Particle& p : c)
            if (!get(&p, sizeof(Particle))) return Status::fail(Err::Truncated, "particle state: particle");
    }
    if (at != size) return Status::fail(Err::BadLayout, "particle state: bytes after the last cell");
    m_set = s;
    m_frames = frames;
    m_cx = cx;
    m_cy = cy;
    m_gx = gx;
    m_gy = gy;
    m_rand.state = rs;
    m_cells.swap(cells);
    m_stats = ParticleStats();
    m_stats.lost = lost;
    m_stats.steps = steps;
    for (const auto& c : m_cells) m_stats.particles += static_cast<uint32_t>(c.size());
    return Status::Ok();
}

// ---- ParticleRenderer ----

namespace {

// 0692 cut D: the draw order (as ParticleSim::sortOrder) of only the sprites that some view can show. Every view draws
// the whole list, so a sprite is dropped only when, for EVERY view, all of it lies beyond one of the view's four side
// planes of the clip volume. The VS's position is (p M).xyw with z = w / 1024 (fog-of-war.md 10.1), so in clip space a
// point is inside only if -w <= x <= w and -w <= y <= w; the four planes w - x, w + x, w - y, w + y are affine in the
// world position. A sprite is a square on z = const: centre (v0 + v2) / 2, every corner within rho = |v0 - v2| / 2 of
// it, so a plane's largest value over the sprite is at most P(centre) + rho |grad_xy P|. The sprite is dropped when
// that bound is below minus a margin of 1e-5 x the sum of the terms' magnitudes (the GPU's and this code's float
// rounding are about 1e-7 of it), with rho widened for the centre's own rounding. A dropped sprite would have been
// clipped whole and drawn no pixel; the blend has no other input from it (no depth write), and the kept sprites keep
// their relative order (a stable sort of a subsequence with finite keys). Emit is unchanged, so every particle's
// rotation still advances and the state and the stats (sprites = all emitted) are as before. A non-finite key or
// vertex keeps every sprite (the old order exactly).
void cullOrder(const std::vector<ParticleSprite>& s, const View* views, uint32_t numViews,
               std::vector<std::pair<float, uint32_t>>& order)
{
    struct Plane { float a, b, c, d, n; int axis; };   // value = a x + b y + c z + d; axis 0 = x planes, 1 = y planes
    Plane pl[kMaxViews][4];
    float absM[kMaxViews][3][4];                        // |M| columns x, y, w: rows x, y, z, 1
    for (uint32_t v = 0; v < numViews; ++v) {
        const float* m = views[v].viewProj;
        for (int k = 0; k < 4; ++k) {
            const int axis = k / 2;                     // 0: x (column 0), 1: y (column 1)
            const float sgn = (k % 2) ? 1.0f : -1.0f;   // w - x, w + x, w - y, w + y
            const double a = static_cast<double>(m[3]) + sgn * m[0 + axis], b = static_cast<double>(m[7]) + sgn * m[4 + axis];
            const double c = static_cast<double>(m[11]) + sgn * m[8 + axis], d = static_cast<double>(m[15]) + sgn * m[12 + axis];
            pl[v][k] = {static_cast<float>(a), static_cast<float>(b), static_cast<float>(c), static_cast<float>(d),
                        static_cast<float>(std::sqrt(a * a + b * b)), axis};
        }
        const int cols[3] = {0, 1, 3};
        for (int j = 0; j < 3; ++j)
            for (int r = 0; r < 4; ++r) absM[v][j][r] = std::fabs(m[r * 4 + cols[j]]);
    }
    order.clear();
    order.reserve(s.size());
    bool finite = true;
    for (size_t i = 0; i < s.size(); ++i) {
        const ParticleSprite& sp = s[i];
        if (!std::isfinite(sp.key) || !finiteN(&sp.v[0][0], 32)) {
            finite = false;
            break;
        }
        const float cx = 0.5f * (sp.v[0][0] + sp.v[2][0]), cy = 0.5f * (sp.v[0][1] + sp.v[2][1]), z = sp.v[0][2];
        const float ex = sp.v[0][0] - sp.v[2][0], ey = sp.v[0][1] - sp.v[2][1];
        const float rho = 0.5f * std::sqrt(ex * ex + ey * ey) * 1.0001f + 1e-6f * (std::fabs(cx) + std::fabs(cy)) + 1e-3f;
        const float ax = std::fabs(cx) + rho, ay = std::fabs(cy) + rho, az = std::fabs(z);
        bool shown = false;
        for (uint32_t v = 0; v < numViews && !shown; ++v) {
            float mag[3];
            for (int j = 0; j < 3; ++j) mag[j] = absM[v][j][0] * ax + absM[v][j][1] * ay + absM[v][j][2] * az + absM[v][j][3];
            bool out = false;
            for (int k = 0; k < 4 && !out; ++k) {
                const Plane& q = pl[v][k];
                const float val = q.a * cx + q.b * cy + q.c * z + q.d;
                const float margin = 1e-5f * (mag[q.axis] + mag[2]);
                out = val + rho * q.n + margin < 0.0f;
            }
            shown = !out;
        }
        if (shown) order.push_back(std::make_pair(sp.key, static_cast<uint32_t>(i)));
    }
    if (!finite) {
        ParticleSim::sortOrder(s, order);
        return;
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) { return a.first > b.first; });
}

} // namespace

struct ParticleRenderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11ShaderResourceView* sprite = nullptr;
    uint32_t perRow = 8, frames = 64;
    float du = 0.125f, dv = 0.125f;
    ParticleSettings settings;
    FlowField flow;
    ParticleSim sim;
    std::vector<ParticleSprite> sprites;
    std::vector<std::pair<float, uint32_t>> order;   // the draw order of sprites (0692 cut C)
    // The step runs on the module's own worker thread between two drawn frames, as stock's step job does (fog-of-war.md
    // 10.1): started after a frame's draw, waited for at the next frame (or any access to the state). The order of the
    // steps and draws is unchanged, so the particles stay repeatable. A thread of its own, started once (not std::async,
    // whose task goes to the C runtime's pool each frame: fog-of-war.md 10.3).
    std::thread worker;
    std::mutex mu;
    std::condition_variable cv;
    bool busy = false, quit = false;
    bool cull = true;   // 0692 cut D (C5_RENDER_PARTICLE_CULL=0: off)
    float jobLo[2] = {}, jobHi[2] = {};
    Status jobStatus;

    void loop()
    {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            cv.wait(lk, [this] { return busy || quit; });
            if (quit) return;
            const float lo[2] = {jobLo[0], jobLo[1]}, hi[2] = {jobHi[0], jobHi[1]};
            lk.unlock();
            Status st = sim.step(lo, hi);
            lk.lock();
            jobStatus = st;
            busy = false;
            cv.notify_all();
        }
    }

    void launch(const float lo[2], const float hi[2])
    {
        std::unique_lock<std::mutex> lk(mu);
        if (!worker.joinable()) worker = std::thread([this] { loop(); });
        jobLo[0] = lo[0]; jobLo[1] = lo[1]; jobHi[0] = hi[0]; jobHi[1] = hi[1];
        jobStatus = Status::Ok();
        busy = true;
        cv.notify_all();
    }

    Status join()
    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [this] { return !busy; });
        Status st = jobStatus;
        jobStatus = Status::Ok();
        return st;
    }

    ~Impl()
    {
        join();
        {
            std::lock_guard<std::mutex> lk(mu);
            quit = true;
        }
        cv.notify_all();
        if (worker.joinable()) worker.join();
        rel(vs); rel(ps); rel(layout); rel(vb); rel(ib); rel(cb); rel(blend); rel(depth); rel(raster); rel(sampler); rel(sprite);
        rel(dev);
    }
};

ParticleRenderer::~ParticleRenderer() { delete m_impl; }

ParticleSim& ParticleRenderer::sim()
{
    static ParticleSim none;
    if (!m_impl) return none;
    m_impl->join();   // a step in flight finishes first (its status is lost here; frame() reports a failed step)
    return m_impl->sim;
}

const ParticleSettings& ParticleRenderer::settings() const
{
    static const ParticleSettings none;
    return m_impl ? m_impl->settings : none;
}

Status ParticleRenderer::init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir)
{
    delete m_impl;
    m_impl = nullptr;
    if (!dev) return Status::fail(Err::BadLayout, "ParticleRenderer::init: no device");
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("FOWParticleShader");
    if (!set) return Status::fail(Err::NotFound, "terrainsets has no FOWParticleShader set");
    const terrainsets::PassDesc* pass = nullptr;
    for (const auto& pd : set->passes)
        if (pd.index == 0) pass = &pd;
    if (!pass) return Status::fail(Err::NotFound, "FOWParticleShader pass 0 not described");
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::unique_ptr<Impl> p(new Impl());
    // 0692 cut A: the step's threads, ParticleSim::defaultStepThreads(); C5_RENDER_PARTICLE_THREADS (1..16) overrides it
    // for a diagnosis or a bench (the state is the same for any count)
    uint32_t threads = ParticleSim::defaultStepThreads();
    if (const char* e = std::getenv("C5_RENDER_PARTICLE_THREADS")) {
        const int v = std::atoi(e);
        if (v >= 1 && v <= static_cast<int>(kMaxStepThreads)) threads = static_cast<uint32_t>(v);
    }
    p->sim.setStepThreads(threads);
    // 0692 cut D: C5_RENDER_PARTICLE_CULL=0 draws every emitted sprite again (a diagnosis; the picture is the same)
    if (const char* e = std::getenv("C5_RENDER_PARTICLE_CULL")) p->cull = std::strcmp(e, "0") != 0;
    p->dev = dev;
    dev->AddRef();
    auto blob = [&](uint32_t va, shaders::Stage st, const char* what, const shaders::ShaderBlob** out) -> Status {
        char v[16];
        std::snprintf(v, sizeof v, "0x%08x", va);
        const shaders::ShaderBlob* b = va ? sec.findByVa(va) : nullptr;
        if (!b) return Status::fail(Err::NotFound, std::string(what) + " blob " + v + " not in the .shader section");
        if (b->stage != st) return Status::fail(Err::BadLayout, std::string(what) + " blob " + v + " has another stage");
        *out = b;
        return Status::Ok();
    };
    const shaders::ShaderBlob *bv = nullptr, *bp = nullptr;
    C5R_TRY(blob(pass->sm50[0], shaders::Stage::Vertex, "FOWParticleShader VS", &bv));
    C5R_TRY(blob(pass->sm50[1], shaders::Stage::Pixel, "FOWParticleShader PS", &bp));
    C5R_TRY(hrOk(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (FOWParticleShader)"));
    C5R_TRY(hrOk(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (FOWParticleShader)"));
    // the sprite vertex (fog-of-war.md 10.1): POSITION float3, NORMAL float3, TEXCOORD float2; stride 32
    const D3D11_INPUT_ELEMENT_DESC el[3] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    C5R_TRY(hrOk(dev->CreateInputLayout(el, 3, bv->bytes, bv->size, &p->layout), "CreateInputLayout (FOWParticleShader)"));
    {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = kMaxParticleSprites * 4u * 32u;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &p->vb), "CreateBuffer (sprite vertices)"));
        std::vector<uint16_t> ix(static_cast<size_t>(kMaxParticleSprites) * 6);
        for (uint32_t i = 0; i < kMaxParticleSprites; ++i) {
            const uint16_t b = static_cast<uint16_t>(i * 4u);
            const uint16_t t[6] = {b, static_cast<uint16_t>(b + 1), static_cast<uint16_t>(b + 2), b, static_cast<uint16_t>(b + 2),
                                   static_cast<uint16_t>(b + 3)};
            std::memcpy(&ix[static_cast<size_t>(i) * 6], t, sizeof t);
        }
        D3D11_BUFFER_DESC di = {};
        di.ByteWidth = static_cast<UINT>(ix.size() * sizeof(uint16_t));
        di.Usage = D3D11_USAGE_IMMUTABLE;
        di.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA s = {ix.data(), 0, 0};
        C5R_TRY(hrOk(dev->CreateBuffer(&di, &s, &p->ib), "CreateBuffer (sprite indices)"));
        D3D11_BUFFER_DESC dc = {};
        dc.ByteWidth = kSlotsCb;
        dc.Usage = D3D11_USAGE_DYNAMIC;
        dc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(dev->CreateBuffer(&dc, nullptr, &p->cb), "CreateBuffer (FOWParticleShader constants)"));
    }
    {
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        C5R_TRY(hrOk(dev->CreateBlendState(&b, &p->blend), "CreateBlendState (particles)"));
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = TRUE;
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        d.BackFace = d.FrontFace;
        C5R_TRY(hrOk(dev->CreateDepthStencilState(&d, &p->depth), "CreateDepthStencilState (particles)"));
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_NONE;   // [I: stock's rasterizer state for this draw was not read]
        r.DepthClipEnable = TRUE;
        C5R_TRY(hrOk(dev->CreateRasterizerState(&r, &p->raster), "CreateRasterizerState (particles)"));
        D3D11_SAMPLER_DESC s = {};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        s.MaxAnisotropy = 1;
        s.ComparisonFunc = D3D11_COMPARISON_NEVER;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        C5R_TRY(hrOk(dev->CreateSamplerState(&s, &p->sampler), "CreateSamplerState (particles)"));
    }
    {
        gamefs::GameFiles fs;
        C5R_TRY(fs.open(gameDir));
        std::vector<uint8_t> bytes;
        Status r = fs.read(kFogFile, bytes);
        if (!r.ok()) return Status::fail(Err::NotFound, std::string(kFogFile) + " not readable from the install: " + r.str());
        gamefs::DdsImage img;
        Status pr = gamefs::parseDds(bytes.data(), bytes.size(), img);
        if (!pr.ok()) return Status::fail(pr.code, std::string(kFogFile) + ": " + pr.msg);
        if (img.width < 64 || img.height < 64) return Status::fail(Err::BadLayout, std::string(kFogFile) + ": smaller than one 64 x 64 frame");
        ID3D11Resource* tex = nullptr;
        Status cr = gamefs::createTexture(dev, img, &tex, &p->sprite);
        rel(tex);
        if (!cr.ok()) return Status::fail(cr.code, std::string(kFogFile) + ": " + cr.msg);
        p->perRow = img.width / 64u;
        p->frames = (img.width / 64u) * (img.height / 64u);
        p->du = 64.0f / static_cast<float>(img.width);
        p->dv = 64.0f / static_cast<float>(img.height);
        bytes.clear();
        r = fs.read(kFlowFile, bytes);
        if (!r.ok()) return Status::fail(Err::NotFound, std::string(kFlowFile) + " not readable from the install: " + r.str());
        Status fr = flowFieldFromDds(bytes.data(), bytes.size(), p->flow);
        if (!fr.ok()) return Status::fail(fr.code, std::string(kFlowFile) + ": " + fr.msg);
        bytes.clear();
        r = fs.read(kSettingsFile, bytes);
        if (!r.ok()) return Status::fail(Err::NotFound, std::string(kSettingsFile) + " not readable from the install: " + r.str());
        Status sr = parseParticleSettings(reinterpret_cast<const char*>(bytes.data()), bytes.size(), p->settings);
        if (!sr.ok()) return Status::fail(sr.code, std::string(kSettingsFile) + ": " + sr.msg);
    }
    m_impl = p.release();
    return Status::Ok();
}

Status ParticleRenderer::reset(float worldWidth, float worldHeight, uint32_t seed)
{
    if (!m_impl) return Status::fail(Err::NotFound, "ParticleRenderer::reset: not initialised");
    m_impl->join();
    return m_impl->sim.reset(worldWidth, worldHeight, m_impl->settings, m_impl->flow, seed, m_impl->frames);
}

Status ParticleRenderer::frame(ID3D11DeviceContext* ctx, const ParticleInput& in, ParticleStats* stats)
{
    Impl* p = m_impl;
    if (!p || !p->sim.ready()) return Status::fail(Err::NotFound, "ParticleRenderer::frame: not initialised or not reset");
    if (!ctx || !in.colour || !in.depth || !in.mask)
        return Status::fail(Err::BadLayout, "ParticleRenderer::frame: no context, target, depth or mask");
    if (in.width == 0 || in.height == 0) return Status::fail(Err::BadLayout, "ParticleRenderer::frame: viewport 0");
    if (!in.views || in.numViews == 0 || in.numViews > kMaxViews)
        return Status::fail(Err::BadLayout, "ParticleRenderer::frame: views 0 or above kMaxViews");
    const CloudWorld& w = in.world;
    if (!(std::isfinite(w.minX) && std::isfinite(w.maxX) && std::isfinite(w.minY) && std::isfinite(w.maxY) && w.maxX > w.minX &&
          w.maxY > w.minY && w.samplesX && w.samplesY && std::isfinite(w.sx) && std::isfinite(w.sy)))
        return Status::fail(Err::BadLayout, "ParticleRenderer::frame: world not valid");
    for (uint32_t v = 0; v < in.numViews; ++v)
        if (!finiteN(in.views[v].viewProj, 16) || !finiteN(in.views[v].overlayProj, 16))
            return Status::fail(Err::BadLayout, "ParticleRenderer::frame: a matrix of view " + std::to_string(v) + " not finite");
    // the previous frame's step (on its worker thread) first
    C5R_TRY(p->join());
    // footprints, then every view's sprites into one list, sorted back to front (fog-of-war.md 10.1)
    const water::World ww = toWaterWorld(w);
    water::Footprint fp[kMaxViews];
    for (uint32_t v = 0; v < in.numViews; ++v) C5R_TRY(water::viewFootprint(in.views[v].viewProj, ww, fp[v]));
    p->sprites.clear();
    for (uint32_t v = 0; v < in.numViews; ++v) {
        const float a[2] = {fp[v].minX, fp[v].minY}, b[2] = {fp[v].maxX, fp[v].maxY};
        C5R_TRY(p->sim.emit(in.views[v].viewProj, a, b, p->perRow, p->du, p->dv, p->sprites));
    }
    // back to front: the order of sortSprites, the sprites written in that order straight into the buffer (0692 cut C),
    // without the sprites that no view can show (0692 cut D)
    if (p->cull) cullOrder(p->sprites, in.views, in.numViews, p->order);
    else ParticleSim::sortOrder(p->sprites, p->order);
    const uint32_t n = static_cast<uint32_t>(p->order.size());
    if (n) {
        D3D11_MAPPED_SUBRESOURCE m = {};
        C5R_TRY(hrOk(ctx->Map(p->vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (sprite vertices)"));
        uint8_t* dst = static_cast<uint8_t*>(m.pData);
        for (uint32_t i = 0; i < n; ++i) std::memcpy(dst + static_cast<size_t>(i) * 128, p->sprites[p->order[i].second].v, 128);
        ctx->Unmap(p->vb, 0);
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(in.width), static_cast<float>(in.height), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(p->raster);
        ctx->OMSetRenderTargets(1, &in.colour, in.depth);
        const float bf[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(p->blend, bf, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(p->depth, 0);
        const UINT stride = 32, offset = 0;
        ctx->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
        ctx->IASetIndexBuffer(p->ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(p->layout);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->VSSetShader(p->vs, nullptr, 0);
        ctx->PSSetShader(p->ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &p->cb);
        ID3D11SamplerState* samp[2] = {p->sampler, p->sampler};
        ctx->PSSetSamplers(0, 2, samp);
        ID3D11ShaderResourceView* srv[2] = {p->sprite, in.mask};
        ctx->PSSetShaderResources(0, 2, srv);
        for (uint32_t v = 0; v < in.numViews; ++v) {
            float c[kSlotsCb / 4] = {};
            std::memcpy(c, in.views[v].viewProj, 64);
            std::memcpy(c + 16, in.views[v].overlayProj, 64);
            c[32] = c[33] = c[34] = c[35] = 1.0f;
            D3D11_MAPPED_SUBRESOURCE mc = {};
            Status ms = hrOk(ctx->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc), "Map (FOWParticleShader constants)");
            if (!ms.ok()) {
                ID3D11ShaderResourceView* none[2] = {};
                ctx->PSSetShaderResources(0, 2, none);
                return ms;
            }
            std::memcpy(mc.pData, c, kSlotsCb);
            ctx->Unmap(p->cb, 0);
            ctx->DrawIndexed(n * 6u, 0, 0);
        }
        ID3D11ShaderResourceView* none[2] = {};
        ctx->PSSetShaderResources(0, 2, none);
    }
    // one step for the next frame, in view 0's footprint (stock: the step job started after the draw)
    const float a[2] = {fp[0].minX, fp[0].minY}, b[2] = {fp[0].maxX, fp[0].maxY};
    if (stats) *stats = p->sim.stats();   // the step of the previous frame and this frame's sprites
    p->launch(a, b);
    return Status::Ok();
}

} // namespace fow
} // namespace c5r
