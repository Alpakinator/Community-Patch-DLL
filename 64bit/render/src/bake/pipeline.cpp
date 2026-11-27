// render/src/bake/pipeline.cpp - the bake pipeline (issue 3404): layer list -> blend vertices -> blend pass -> post
// passes -> t0 / t8. Own code, written from research/05-graphics-composition/terrain-bake-records.md, terrain-bake-
// blend.md and terrain-bake-passes.md (D83). See c5r/bake/pipeline.h.
#include "c5r/bake/pipeline.h"

#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <xmmintrin.h>   // ticket 0712: the worker takes the caller's MXCSR

#include <map>
#include <utility>

#include "c5r/bake/grid.h"
#include "c5r/bake/heightpage.h"
#include "c5r/terrain/terrain.h"   // ticket 4909: spanMeetsWrappedWindow (ExePages::heightsChanged)
#include "c5r/decal/decal.h"
#include "c5r/gamefs/dds.h"        // ticket 0698: the loader parses and creates the layer textures
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"

namespace c5r {
namespace bake {

namespace {

template <class T>
void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hr(HRESULT h, const char* what) {
    if (FAILED(h)) {
        char b[32];
        std::snprintf(b, sizeof b, "0x%08lx", static_cast<unsigned long>(h));
        return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + " failed: " + b);
    }
    return Status::Ok();
}

Status checkGridSizes(const heightfield::HeightGrid& g) {
    if (g.samplesX == 0 || g.samplesY == 0 || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "height grid: " + std::to_string(g.h.size()) + " heights for " +
                                                std::to_string(g.samplesX) + " x " + std::to_string(g.samplesY));
    return Status::Ok();
}

// ---- stand-ins

class ApproxNormals : public GridNormalSource {
public:
    const char* name() const override { return "stand-in: 3403 vertex-height central differences"; }
    Status normals(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& out) override {
        return approxGridNormals(g, w, out);
    }
};

class FlatSlopes : public SlopeClassSource {
public:
    const char* name() const override { return "stand-in: all slope classes 0"; }
    Status classes(const heightfield::HeightGrid&, const TileWindow&, SlopeClasses& out) override {
        out.clear();
        return Status::Ok();
    }
};

class FittedPages : public HeightPageSource {
public:
    ~FittedPages() override { rel(srv_); rel(tex_); }
    const char* name() const override { return "stand-in: 3403 fitted page rule, one page per tile"; }
    Status page(ID3D11Device* dev, ID3D11DeviceContext*, const heightfield::HeightGrid& g, const TileWindow& w,
                HeightPageView& out) override {
        out = HeightPageView();
        if (!dev) return Status::fail(Err::BadLayout, "height page: no device");
        std::vector<uint8_t> mip0;
        int32_t ox = 0, oy = 0;
        C5R_TRY(fittedHeightPage(g, w, mip0, ox, oy));
        // 11 mips, each a 2 x 2 box of the one above (rounded).
        std::vector<std::vector<uint8_t>> mips(1, mip0);
        for (uint32_t s = kStandInPageSize / 2; s >= 1; s /= 2) {
            const std::vector<uint8_t>& up = mips.back();
            const uint32_t us = s * 2;
            std::vector<uint8_t> m(static_cast<size_t>(s) * s);
            for (uint32_t y = 0; y < s; ++y)
                for (uint32_t x = 0; x < s; ++x) {
                    const uint32_t sum = up[(2 * y) * us + 2 * x] + up[(2 * y) * us + 2 * x + 1] +
                                         up[(2 * y + 1) * us + 2 * x] + up[(2 * y + 1) * us + 2 * x + 1];
                    m[static_cast<size_t>(y) * s + x] = static_cast<uint8_t>((sum + 2) / 4);
                }
            mips.push_back(std::move(m));
        }
        D3D11_SUBRESOURCE_DATA init[11];
        for (uint32_t i = 0; i < 11; ++i) {
            init[i].pSysMem = mips[i].data();
            init[i].SysMemPitch = kStandInPageSize >> i;
            init[i].SysMemSlicePitch = 0;
        }
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = kStandInPageSize;
        d.MipLevels = 11;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        rel(srv_);
        rel(tex_);
        C5R_TRY(hr(dev->CreateTexture2D(&d, init, &tex_), "CreateTexture2D (height page)"));
        C5R_TRY(hr(dev->CreateShaderResourceView(tex_, nullptr, &srv_), "CreateShaderResourceView (height page)"));
        out.srv = srv_;
        out.offsetX = ox;
        out.offsetY = oy;
        return Status::Ok();
    }

private:
    ID3D11Texture2D* tex_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
};

class CaptureConstants : public BakeConstantsSource {
public:
    const char* name() const override { return "stand-in: 3023 run 3 load-screen HalfVector / AuxDir"; }
    Status constants(TileId, PostConstants& k) override {
        k = PostConstants();
        return Status::Ok();
    }
};

}  // namespace

// ---------------------------------------------------------------- CPU helpers

Status gridVertexHeights(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& z) {
    z.clear();
    C5R_TRY(checkGridSizes(g));
    if (w.ex == 0 || w.ey == 0 || w.ex > kTileSamples + 1 || w.ey > kTileSamples + 1)
        return Status::fail(Err::BadLayout, "tile window extent " + std::to_string(w.ex) + " x " + std::to_string(w.ey));
    const uint32_t gw = w.ex + 2, gh = w.ey + 2;
    z.resize(static_cast<size_t>(gw) * gh);
    const float inv255 = 1.0f / 255.0f;
    for (uint32_t j = 0; j < gh; ++j)
        for (uint32_t i = 0; i < gw; ++i) {
            // unsigned wrap as the blend vertex fill (terrain-bake-blend.md 2): (samples + s) mod samples
            const uint32_t sx = static_cast<uint32_t>(static_cast<int64_t>(g.samplesX) + w.ox + i - 1) % g.samplesX;
            const uint32_t sy = static_cast<uint32_t>(static_cast<int64_t>(g.samplesY) + w.oy + j - 1) % g.samplesY;
            z[static_cast<size_t>(j) * gw + i] = (static_cast<float>(g.h[g.index(sx, sy)]) * inv255) * g.sz;
        }
    return Status::Ok();
}

Status approxGridNormals(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& out) {
    out.clear();
    std::vector<float> z;
    C5R_TRY(gridVertexHeights(g, w, z));
    const int nw = static_cast<int>(w.ex + 2), nh = static_cast<int>(w.ey + 2);
    out.resize(static_cast<size_t>(nw) * nh * 3);
    for (int j = 0; j < nh; ++j)
        for (int i = 0; i < nw; ++i) {
            const int i0 = std::max(0, i - 1), i1 = std::min(nw - 1, i + 1), j0 = std::max(0, j - 1), j1 = std::min(nh - 1, j + 1);
            const double gx = (z[j * nw + i1] - z[j * nw + i0]) / ((i1 - i0) * static_cast<double>(g.sx));
            const double gy = (z[j1 * nw + i] - z[j0 * nw + i]) / ((j1 - j0) * static_cast<double>(g.sy));
            const double len = std::sqrt(gx * gx + gy * gy + 1.0);
            out[(j * nw + i) * 3 + 0] = static_cast<float>(-gx / len);
            out[(j * nw + i) * 3 + 1] = static_cast<float>(-gy / len);
            out[(j * nw + i) * 3 + 2] = static_cast<float>(1.0 / len);
        }
    return Status::Ok();
}

Status fittedHeightPage(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<uint8_t>& page,
                        int32_t& offsetX, int32_t& offsetY) {
    page.clear();
    C5R_TRY(checkGridSizes(g));
    if (w.ox >= g.samplesX || w.oy >= g.samplesY)
        return Status::fail(Err::BadLayout, "height page: tile origin outside the grid");
    const int32_t P = static_cast<int32_t>(kStandInPageSize);
    offsetX = P / 2 - static_cast<int32_t>(w.ox) - 32;
    offsetY = -(static_cast<int32_t>(w.oy) + P / 2 + 32);
    page.assign(static_cast<size_t>(P) * P, 0);
    const int32_t W = static_cast<int32_t>(g.samplesX), H = static_cast<int32_t>(g.samplesY);
    for (int32_t row = 0; row < P; ++row) {
        // row = (-offsetY - y) mod P, so y = -offsetY - row, taken in the window of P rows around the tile
        int32_t y = -offsetY - row;
        y = std::min(std::max(y, 0), H - 1);
        for (int32_t col = 0; col < P; ++col) {
            int32_t x = col - offsetX;
            if (g.wrapX) x = ((x % W) + W) % W;
            else x = std::min(std::max(x, 0), W - 1);
            const uint32_t v = (static_cast<uint32_t>(g.h[g.index(static_cast<uint32_t>(x), static_cast<uint32_t>(y))]) + 127) / 255;
            page[static_cast<size_t>(row) * P + col] = static_cast<uint8_t>(std::min<uint32_t>(v, 255));
        }
    }
    return Status::Ok();
}

std::unique_ptr<GridNormalSource> makeApproxGridNormals() { return std::unique_ptr<GridNormalSource>(new ApproxNormals); }
std::unique_ptr<SlopeClassSource> makeFlatSlopeClasses() { return std::unique_ptr<SlopeClassSource>(new FlatSlopes); }
std::unique_ptr<HeightPageSource> makeFittedHeightPages() { return std::unique_ptr<HeightPageSource>(new FittedPages); }
std::unique_ptr<BakeConstantsSource> makeCaptureConstants() {
    return std::unique_ptr<BakeConstantsSource>(new CaptureConstants);
}

// ---------------------------------------------------------------- the EXE's rules (3405, 3406)

namespace {

TileGeom geomOf(const TileWindow& w) {
    TileGeom t;
    t.originX = static_cast<int32_t>(w.ox);
    t.originY = static_cast<int32_t>(w.oy);
    t.extentX = w.ex;
    t.extentY = w.ey;
    return t;
}

class ExeNormals : public GridNormalSource {
public:
    const char* name() const override { return "3405: gridNormals (0x728240 / 0x747580 port, D3DX normalisation)"; }
    Status normals(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& out) override {
        return gridNormals(g, geomOf(w), out);
    }
};

class ExeSlopes : public SlopeClassSource {
public:
    const char* name() const override { return "3405: tileSlopeClasses"; }
    Status classes(const heightfield::HeightGrid& g, const TileWindow& w, SlopeClasses& out) override {
        return tileSlopeClasses(g, geomOf(w), out);
    }
};

class ExePages : public HeightPageSource {
public:
    ~ExePages() override { clear(); }
    const char* name() const override { return "3406: buildHeightPage (0x74A9E0 port), pages shared by their tiles"; }
    Status page(ID3D11Device* dev, ID3D11DeviceContext*, const heightfield::HeightGrid& g, const TileWindow& w,
                HeightPageView& out) override {
        out = HeightPageView();
        if (!dev) return Status::fail(Err::BadLayout, "height page: no device");
        if (&g != grid_ || g.samplesX != sx_ || g.samplesY != sy_ || g.h.data() != data_) {
            clear();
            grid_ = &g;
            sx_ = g.samplesX;
            sy_ = g.samplesY;
            data_ = g.h.data();
        }
        TileId t;
        t.x = w.ox / kTileSamples;
        t.y = w.oy / kTileSamples;
        HeightPageIndex pi;
        int32_t off[2] = {0, 0};
        C5R_TRY(heightPageForTile(g, t, pi, off));
        const uint32_t key = pi.px + 4096 * pi.py;
        auto it = pages_.find(key);
        if (it == pages_.end()) {
            HeightPage hp;
            C5R_TRY(buildHeightPage(g, pi, kHeightPageMips, hp));
            ID3D11Texture2D* tex = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            Status st = createHeightPageTexture(dev, hp, &tex, &srv);
            if (!st.ok()) {
                rel(tex);
                rel(srv);
                return st;
            }
            it = pages_.emplace(key, std::make_pair(tex, srv)).first;
            ++built;
        }
        out.srv = it->second.second;
        out.offsetX = off[0];
        out.offsetY = off[1];
        return Status::Ok();
    }
    uint64_t built = 0;  // issue 0417: pages built so far (each is a first use of its page)
    uint64_t dropped = 0;  // ticket 4909: pages dropped by heightsChanged
    // Ticket 4909 (live-terrain.md 8.14): the pages are kept by the grid's address and sizes, so an in-place edit of the
    // same grid (a live terrain window) left them stale [C: test exe_pages_follow_an_in_place_edit]. Drops the pages whose
    // 1024-sample window (both axes wrap) holds a sample of a box; every page without boxes.
    void heightsChanged(const heightfield::HeightGrid& g, const uint32_t* boxes, size_t boxCount) override {
        if (!boxes || !boxCount || &g != grid_) {
            dropped += pages_.size();
            clear();
            return;
        }
        for (auto it = pages_.begin(); it != pages_.end();) {
            const uint32_t px = it->first % 4096, py = it->first / 4096;
            const int64_t sx0 = static_cast<int64_t>(px) * kHeightPageStep - kHeightPageMargin;
            const int64_t sy0 = static_cast<int64_t>(py) * kHeightPageStep - kHeightPageMargin;
            bool hit = false;
            for (size_t k = 0; k < boxCount && !hit; ++k) {
                const uint32_t* b = boxes + 4 * k;
                hit = terrain::spanMeetsWrappedWindow(b[0], b[2], sx0, kHeightPageSize, g.samplesX) &&
                      terrain::spanMeetsWrappedWindow(b[1], b[3], sy0, kHeightPageSize, g.samplesY);
            }
            if (hit) {
                rel(it->second.first);
                rel(it->second.second);
                it = pages_.erase(it);
                ++dropped;
            } else {
                ++it;
            }
        }
    }
    // issue 0417: drops the pages and the grid they belong to (after the warm-up's own map)
    void forget() {
        clear();
        grid_ = nullptr;
        data_ = nullptr;
        sx_ = sy_ = 0;
    }

private:
    void clear() {
        for (auto& kv : pages_) {
            rel(kv.second.first);
            rel(kv.second.second);
        }
        pages_.clear();
    }
    const heightfield::HeightGrid* grid_ = nullptr;
    const uint16_t* data_ = nullptr;
    uint32_t sx_ = 0, sy_ = 0;
    std::map<uint32_t, std::pair<ID3D11Texture2D*, ID3D11ShaderResourceView*>> pages_;
};

class StockConstants : public BakeConstantsSource {
public:
    const char* name() const override { return "3406: stockBakeLight (identity bake view, the EXE's float bits)"; }
    Status constants(TileId, PostConstants& k) override {
        k = PostConstants();
        const BakeLight L = stockBakeLight();
        for (int i = 0; i < 3; ++i) {
            k.halfVector[i] = L.halfVector[i];
            k.shadowDir[i] = L.auxDir[i];
        }
        return Status::Ok();
    }
};

// GPU time of the bakes with D3D11 timestamp queries (a disjoint query and two timestamps per bake).
// Blocking: end() waits until this bake's queries are done (the CPU waits for the GPU each bake).
// Deferred: a ring of query sets; end() flushes the context but never waits: it collects whatever earlier bakes have finished (GetData with
// DONOTFLUSH) and returns the newest finished time (0 until the first one is ready); a set still pending when its turn
// in the ring comes round again is dropped.
struct GpuTimer {
    static const int kRing = 16;
    struct Set {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* t0 = nullptr;
        ID3D11Query* t1 = nullptr;
        bool pending = false;
    };
    Set sets[kRing];
    int next = 0, current = -1;
    float latest = 0.0f;
    ~GpuTimer() {
        for (Set& s : sets) {
            rel(s.disjoint);
            rel(s.t0);
            rel(s.t1);
        }
    }
    Status init(ID3D11Device* dev) {
        for (Set& s : sets) {
            rel(s.disjoint);
            rel(s.t0);
            rel(s.t1);
            s.pending = false;
            D3D11_QUERY_DESC d = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            C5R_TRY(hr(dev->CreateQuery(&d, &s.disjoint), "CreateQuery (disjoint)"));
            d.Query = D3D11_QUERY_TIMESTAMP;
            C5R_TRY(hr(dev->CreateQuery(&d, &s.t0), "CreateQuery (timestamp)"));
            C5R_TRY(hr(dev->CreateQuery(&d, &s.t1), "CreateQuery (timestamp)"));
        }
        next = 0;
        current = -1;
        latest = 0.0f;
        return Status::Ok();
    }
    // Reads a finished set; false while the GPU has not reached it (flags 0 waits for nothing but may flush).
    bool read(ID3D11DeviceContext* ctx, Set& s, UINT flags, float& ms) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
        UINT64 a = 0, b = 0;
        if (ctx->GetData(s.disjoint, &dj, sizeof dj, flags) != S_OK) return false;
        if (ctx->GetData(s.t0, &a, sizeof a, flags) != S_OK) return false;
        if (ctx->GetData(s.t1, &b, sizeof b, flags) != S_OK) return false;
        ms = (dj.Disjoint || dj.Frequency == 0 || b < a)
                 ? 0.0f
                 : static_cast<float>(static_cast<double>(b - a) * 1000.0 / static_cast<double>(dj.Frequency));
        return true;
    }
    void collect(ID3D11DeviceContext* ctx) {
        // oldest first, so that `latest` ends on the newest finished bake
        for (int k = 0; k < kRing; ++k) {
            Set& s = sets[(next + k) % kRing];
            float ms = 0;
            if (s.pending && read(ctx, s, D3D11_ASYNC_GETDATA_DONOTFLUSH, ms)) {
                s.pending = false;
                latest = ms;
            }
        }
    }
    void begin(ID3D11DeviceContext* ctx) {
        current = next;
        next = (next + 1) % kRing;
        Set& s = sets[current];
        s.pending = false;  // a set still pending from kRing bakes ago is dropped
        ctx->Begin(s.disjoint);
        ctx->End(s.t0);
    }
    float end(ID3D11DeviceContext* ctx, bool blocking) {
        if (current < 0) return 0.0f;
        Set& s = sets[current];
        ctx->End(s.t1);
        ctx->End(s.disjoint);
        s.pending = true;
        current = -1;
        if (blocking) {
            float ms = 0;
            while (!read(ctx, s, 0, ms)) {
            }
            s.pending = false;
            latest = ms;
            return ms;
        }
        ctx->Flush();  // submit without waiting, so that earlier sets can finish
        collect(ctx);
        return latest;
    }
};

Status checkTarget(const BakeTarget& t, uint32_t sizeClass, const char* who) {
    if (!t.array) return Status::Ok();
    D3D11_TEXTURE2D_DESC d;
    t.array->GetDesc(&d);
    if (d.Format != DXGI_FORMAT_BC1_TYPELESS && d.Format != DXGI_FORMAT_BC1_UNORM && d.Format != DXGI_FORMAT_BC1_UNORM_SRGB)
        return Status::fail(Err::BadLayout, std::string(who) + " array is not BC1");
    if (d.Width != sizeClass || d.Height != sizeClass || d.MipLevels != cacheMips(sizeClass))
        return Status::fail(Err::BadLayout, std::string(who) + " array is " + std::to_string(d.Width) + " x " +
                                                std::to_string(d.Height) + " with " + std::to_string(d.MipLevels) +
                                                " mips, the bake " + std::to_string(sizeClass) + " with " +
                                                std::to_string(cacheMips(sizeClass)));
    if (t.slice >= d.ArraySize)
        return Status::fail(Err::BadLayout, std::string(who) + " slice " + std::to_string(t.slice) + " of " +
                                                std::to_string(d.ArraySize));
    return Status::Ok();
}

void copyToSlice(ID3D11DeviceContext* ctx, ID3D11Texture2D* src, const BakeTarget& t, uint32_t mips) {
    for (uint32_t m = 0; m < mips; ++m)
        ctx->CopySubresourceRegion(t.array, D3D11CalcSubresource(m, t.slice, mips), 0, 0, 0, src, m, nullptr);
}

}  // namespace

std::unique_ptr<GridNormalSource> makeExeGridNormals() { return std::unique_ptr<GridNormalSource>(new ExeNormals); }
std::unique_ptr<SlopeClassSource> makeExeSlopeClasses() { return std::unique_ptr<SlopeClassSource>(new ExeSlopes); }
std::unique_ptr<HeightPageSource> makeExeHeightPages() { return std::unique_ptr<HeightPageSource>(new ExePages); }

// ---------------------------------------------------------------- issue 0417: step times and the warm-up report

namespace {
using Clock = std::chrono::steady_clock;
// The milliseconds since t, and t moved to now.
// This thread's CPU time, user + kernel, ms (the scheduler's granularity, about 15.6 ms).
double threadCpuMs() {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    const uint64_t kk = (static_cast<uint64_t>(k.dwHighDateTime) << 32) | k.dwLowDateTime;
    const uint64_t uu = (static_cast<uint64_t>(u.dwHighDateTime) << 32) | u.dwLowDateTime;
    return static_cast<double>(kk + uu) / 10000.0;
}
double lap(Clock::time_point& t) {
    const Clock::time_point n = Clock::now();
    const double d = std::chrono::duration<double, std::milli>(n - t).count();
    t = n;
    return d;
}
}  // namespace

std::string PipelineStepTimes::str() const {
    char b[512];
    std::snprintf(b, sizeof b,
                  "total %.1f ms: prep %.1f, page %.1f%s, own %.1f, textures %.1f (%u loaded), blend %.1f, normals %.1f, "
                  "style %.1f%s, constants %.1f, post %.1f (%u decal textures loaded), copy %.1f, gpu timer %.1f; thread CPU %.1f",
                  totalMs, prepMs, pageMs, pageBuilt ? " (built)" : "", ownMs, texturesMs, texturesLoaded, blendMs,
                  normalsMs, styleMs, styleLoaded ? " (loaded)" : "", constantsMs, postMs, decalTexturesLoaded, copyMs,
                  gpuTimerMs, threadMs);
    std::string s = b;
    if (loaderWaits) {   // ticket 0698
        std::snprintf(b, sizeof b, "; loader waits %u (%.1f ms)", loaderWaits, loaderWaitMs);
        s += b;
    }
    if (prepAhead) {   // ticket 0712
        static const char* const kHow[4] = {"inline", "hit", "waited", "miss"};
        s += "; prep ahead ";
        s += kHow[prepAhead & 3];
        if (prepAhead == 2) {
            std::snprintf(b, sizeof b, " (%.2f ms)", prepWaitMs);
            s += b;
        }
    }
    return s;
}

std::string WarmReport::str() const {
    char b[512];
    std::snprintf(b, sizeof b,
                  "%s in %.1f ms: %u layer textures in %.1f ms, %u styles in %.1f ms, %u throwaway bakes in %.1f ms, GPU "
                  "wait %.1f ms%s%s",
                  ran ? "done" : "not run", ms, textures, texturesMs, styles, stylesMs, bakes, bakesMs, gpuWaitMs,
                  status.ok() ? "" : "; first failure: ", status.ok() ? "" : status.str().c_str());
    return b;
}
std::unique_ptr<BakeConstantsSource> makeStockBakeConstants() {
    return std::unique_ptr<BakeConstantsSource>(new StockConstants);
}

std::string PipelineLoaderCounters::str() const {
    char b[512];
    std::snprintf(b, sizeof b,
                  "%llu layer textures (%llu missing), %llu styles, %.1f MB, reads %.1f ms in all, longest %.1f ms, create "
                  "%.1f ms, %llu failed, %llu made at adoption, %llu dropped; bakes that waited %llu (%.1f ms, longest %.1f "
                  "ms), %llu moved to the front, fallback binds %llu",
                  static_cast<unsigned long long>(layerTextures), static_cast<unsigned long long>(missing),
                  static_cast<unsigned long long>(styles), bytes / 1e6, readMs, readMsMax, createMs,
                  static_cast<unsigned long long>(failed), static_cast<unsigned long long>(createdAtAdopt),
                  static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(waits), waitMs, waitMsMax,
                  static_cast<unsigned long long>(promoted), static_cast<unsigned long long>(fallbackBinds));
    return b;
}

// ---------------------------------------------------------------- ticket 0712: prep ahead, the pure part

size_t PrepProduct::bytes() const {
    return cls.capacity() + verts.capacity() * sizeof(BlendVertex) + idx.capacity() * sizeof(uint16_t) +
           normals.capacity() * sizeof(float);
}

Status prepCpu(const heightfield::HeightGrid& g, const LayerTables& t, TileId tile, GridNormalSource& normals,
               SlopeClassSource& slopes, PrepProduct& out) {
    out = PrepProduct();
    out.tile = tile;
    Clock::time_point lapAt = Clock::now();
    if (tile.x >= g.mapW || tile.y >= g.mapH)
        return Status::fail(Err::NotFound, "tile (" + std::to_string(tile.x) + ", " + std::to_string(tile.y) +
                                               ") outside the map " + std::to_string(g.mapW) + " x " + std::to_string(g.mapH));
    // the order of bakeBody before 0712: layer list, window, slope classes, blend vertices, indices; then the normals
    C5R_TRY(buildLayerList(t, g, tile, out.list));
    C5R_TRY(tileWindow(g, tile, out.window));
    const TileWindow& w = out.window;
    BlendInputs bin;
    bin.grid = &g;
    bin.tables = &t;
    bin.stepX = g.sx;
    bin.stepY = g.sy;
    bin.stepZ = g.sz;
    const TileGeom geom = geomOf(w);
    const uint32_t gw = w.ex + 2, gh = w.ey + 2;
    C5R_TRY(slopes.classes(g, w, out.cls));
    C5R_TRY(fillBlendVertices(bin, geom, out.list, out.cls, out.verts));
    C5R_TRY(buildBlendIndices(gw, gh, out.idx));
    out.prepMs = lap(lapAt);
    C5R_TRY(normals.normals(g, w, out.normals));
    out.normalsMs = lap(lapAt);
    if (out.normals.size() != static_cast<size_t>(gw) * gh * 3)
        return Status::fail(Err::BadLayout, "grid normals: " + std::to_string(out.normals.size()) + " floats, want " +
                                                std::to_string(gw) + " x " + std::to_string(gh) + " x 3");
    return Status::Ok();
}

namespace {
template <class V>
bool sameBytes(const V& a, const V& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
}
}  // namespace

bool samePrepProduct(const PrepProduct& a, const PrepProduct& b) {
    if (a.tile.x != b.tile.x || a.tile.y != b.tile.y || a.list.count != b.list.count) return false;
    for (uint32_t i = 0; i < kMaxLayers; ++i)
        if (a.list.ids[i] != b.list.ids[i]) return false;
    if (a.window.ox != b.window.ox || a.window.oy != b.window.oy || a.window.ex != b.window.ex || a.window.ey != b.window.ey)
        return false;
    return sameBytes(a.cls, b.cls) && sameBytes(a.verts, b.verts) && sameBytes(a.idx, b.idx) && sameBytes(a.normals, b.normals);
}

std::string PrepCounters::str() const {
    char b[640];
    std::snprintf(b, sizeof b,
                  "requested %llu (replaced %llu), made %llu (failed %llu, discarded %llu); bakes: hits %llu, waits %llu "
                  "(%.1f ms, longest %.2f ms), timeouts %llu, claimed %llu, misses %llu; evicted unused %llu; invalidations "
                  "%llu (%llu waited, %.1f ms, longest %.2f ms); worker %.1f ms; cache %llu products, %.0f KB (max %.0f KB)",
                  static_cast<unsigned long long>(requested), static_cast<unsigned long long>(replaced),
                  static_cast<unsigned long long>(made), static_cast<unsigned long long>(failed),
                  static_cast<unsigned long long>(discarded), static_cast<unsigned long long>(hits),
                  static_cast<unsigned long long>(waits), waitMs, waitMsMax, static_cast<unsigned long long>(waitTimeouts),
                  static_cast<unsigned long long>(claimed), static_cast<unsigned long long>(misses),
                  static_cast<unsigned long long>(evictedUnused), static_cast<unsigned long long>(invalidations),
                  static_cast<unsigned long long>(joinWaits), joinMs, joinMsMax, workerMs,
                  static_cast<unsigned long long>(products), bytes / 1024.0, bytesMax / 1024.0);
    std::string s = b;
    if (verified) {
        std::snprintf(b, sizeof b, "; verified %llu, mismatches %llu (TEST ONLY)", static_cast<unsigned long long>(verified),
                      static_cast<unsigned long long>(verifyMismatches));
        s += b;
    }
    return s;
}

// ---------------------------------------------------------------- ticket 0698: the loader thread

namespace {

// The loader of the layer textures and the style inputs (research: 05-graphics-composition/terrain-bake-slow-first-use.md
// section 8), on the pattern of 0642's DecalPass loader. One job at a time: a layer texture is read through gamefs, parsed
// and created as BlendTextureCache::bind does on a first use; a style's three files are read and its views made as
// loadStyleInputs does. On a D3D11_CREATE_DEVICE_SINGLETHREADED device the loader only reads, and the caller makes the
// textures at adoption. `known` and `failed` belong to the caller's thread; the rest is under `m`.
struct Loader {
    ID3D11Device* dev = nullptr;              // the baker's (it outlives the thread: stop() comes first)
    const gamefs::GameFiles* fs = nullptr;
    bool multiThreaded = true;
    std::thread worker;
    bool running = false;                     // caller's view
    mutable std::mutex m;
    std::condition_variable cv;
    bool stop = false;
    struct Job {
        std::string key;                      // "t:<name>" (a layer texture) or "s:<n>" (a style)
        std::string name;
        int style = -1;
    };
    std::deque<Job> queue;
    std::string inFlight;
    struct Done {
        std::string name;
        int style = -1;
        ID3D11ShaderResourceView* srv = nullptr;   // a layer texture made here
        std::vector<uint8_t> file;                 // a layer texture's file, made at adoption (SINGLETHREADED)
        StyleFiles files;                          // a style's files, made at adoption (SINGLETHREADED)
        StyleInputs inputs;                        // a style made here
        bool created = false, missing = false, failed = false;
        Status why;
        BlendTextureCache::Load load;
        void release() {
            if (srv) srv->Release();
            srv = nullptr;
            inputs.release();
        }
    };
    std::map<std::string, Done> done;         // finished, not adopted
    std::atomic<size_t> doneCount{0};
    std::set<std::string> known;              // requested and not dropped (caller's thread)
    std::set<std::string> failed;             // failed on the loader (caller's thread, after adoption)

    static void run(Loader* L) {
        using C = std::chrono::steady_clock;
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> l(L->m);
                L->cv.wait(l, [L]() { return L->stop || !L->queue.empty(); });
                if (L->stop) return;
                job = L->queue.front();
                L->queue.pop_front();
                L->inFlight = job.key;
            }
            Done d;
            d.name = job.name;
            d.style = job.style;
            d.load.name = job.style >= 0 ? "style " + std::to_string(job.style) : job.name;
            const C::time_point r0 = C::now();
            C::time_point r1 = r0;
            Status st;
            if (job.style >= 0) {
                st = readStyleFiles(*L->fs, static_cast<uint32_t>(job.style), d.files);
                r1 = C::now();
                d.load.bytes = d.files.bytes();
                if (st.ok() && L->multiThreaded) {
                    st = createStyleInputs(L->dev, d.files, d.inputs);
                    d.created = st.ok();
                    d.files = StyleFiles();
                }
            } else if (!L->fs->exists(job.name)) {
                r1 = C::now();
                d.missing = true;   // the caller records it as bind does (handle 0), or lets a bake fail as before
            } else {
                std::vector<uint8_t> bytes;
                st = L->fs->read(job.name, bytes);
                r1 = C::now();
                d.load.bytes = bytes.size();
                if (st.ok()) {
                    gamefs::DdsImage img;
                    st = gamefs::parseDds(bytes.data(), bytes.size(), img);
                    if (st.ok() && L->multiThreaded) {
                        ID3D11Resource* res = nullptr;
                        st = gamefs::createTexture(L->dev, img, &res, &d.srv);
                        if (res) res->Release();
                        d.created = st.ok();
                    } else if (st.ok()) {
                        d.file.swap(bytes);
                    }
                }
            }
            d.load.readMs = std::chrono::duration<double, std::milli>(r1 - r0).count();
            d.load.createMs = std::chrono::duration<double, std::milli>(C::now() - r1).count();
            d.load.missing = d.missing;
            d.failed = !st.ok();
            d.why = st;
            if (d.failed) d.release();
            {
                std::lock_guard<std::mutex> l(L->m);
                L->inFlight.clear();
                Done& slot = L->done[job.key];
                slot.release();   // never two of one key (known), kept safe anyway
                slot = std::move(d);
                L->doneCount.store(L->done.size());
            }
            L->cv.notify_all();
        }
    }
};

}  // namespace

// ---------------------------------------------------------------- the baker

struct PipelineBaker::Impl {
    PipelineConfig cfg;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    bool inited = false;
    BlendPass blend;
    BlendTextureCache textures;
    BakePost post;
    std::unique_ptr<decal::DecalPass> decals;  // 4105: made in init; null when its init failed
    Status decalStatus = Status::fail(Err::NotFound, "pipeline not initialised");
    decal::DecalCounters noCounters;
    GpuTimer timer;
    // the EXE's rules (3405, 3406), used where cfg leaves a source null
    std::unique_ptr<GridNormalSource> ownNormals = makeExeGridNormals();
    std::unique_ptr<SlopeClassSource> ownSlopes = makeExeSlopeClasses();
    ExePages* ownPagesExe = new ExePages;  // owned by ownPages; its counter tells a page's first use (0417)
    std::unique_ptr<HeightPageSource> ownPages{ownPagesExe};
    std::unique_ptr<BakeConstantsSource> ownConstants = makeStockBakeConstants();
    // issue 0417
    PipelineStepTimes steps;
    uint64_t slow = 0, firstUses = 0;
    int slowLogged = 0, firstUseLogged = 0;
    bool warming = false;  // the warm-up's own bakes are neither counted nor logged
    WarmReport warmRep;
    void logf(const char* fmt, ...) {
        if (!cfg.log) return;
        char b[2048];
        va_list a;
        va_start(a, fmt);
        std::vsnprintf(b, sizeof b, fmt, a);
        va_end(a);
        cfg.log(cfg.logUser, b);
    }
    Status bakeBody(const BakeRequest& req, const Overrides& ov, BakedTile& out);
    // the map
    bool haveMap = false;
    MapInputs map;
    std::vector<float> terrainLayers;
    StyleInputs styles[5];
    bool styleLoaded[5] = {};
    PipelineTileInfo last;
    // ticket 0698: the loader (LoadAhead::Thread)
    Loader loader;
    PipelineLoaderCounters lc;
    std::vector<BlendTextureCache::Load> loaderLoads;
    Clock::time_point batchStart;             // the first request since the queue was last empty
    bool batchOpen = false;                   // a request since the last "queue empty" line
    Status startLoader();
    void stopLoader();
    size_t pending() const;
    bool request(const std::string& key, const std::string& name, int style);
    size_t requestLayers(const LayerTables& t);
    size_t adopt();
    // Waits for a key queued or in flight (moved to the queue's front when queued), then adopts; false when the key is not
    // the loader's (never requested, failed, or the loader is off).
    bool waitFor(const std::string& key);
    static std::string layerKey(const std::string& name) { return "t:" + name; }
    static std::string styleKey(uint32_t s) { return "s:" + std::to_string(s); }
    // ticket 0712: prep ahead (pipeline.h). Everything below `m` is under it, except mxcsr (set before the thread starts).
    struct PrepKey {
        uint32_t x = 0, y = 0;
        uint64_t gen = 0;
        bool operator<(const PrepKey& o) const { return x != o.x ? x < o.x : y != o.y ? y < o.y : gen < o.gen; }
        bool operator==(const PrepKey& o) const { return x == o.x && y == o.y && gen == o.gen; }
    };
    struct PrepJob {
        PrepKey key;
        uint64_t epoch = 0;
        const heightfield::HeightGrid* g = nullptr;
        const LayerTables* t = nullptr;
    };
    struct PrepEntry {
        std::shared_ptr<const PrepProduct> p;
        uint64_t stamp = 0;
        size_t bytes = 0;
        bool used = false;
    };
    struct Prep {
        std::thread th;
        mutable std::mutex m;
        std::condition_variable cv, done;
        bool started = false, stop = false;
        std::deque<PrepJob> lane[2];
        bool busy = false;
        PrepKey busyKey;
        uint64_t busyEpoch = 0;
        std::map<PrepKey, PrepEntry> cache;
        uint64_t stamp = 0, epoch = 1;
        PrepCounters c;
        unsigned int mxcsr = 0;
    } prep;
    double prepInlineMs = 3.0;   // the bake thread's inline prep + normals, a running mean: the bound of a wait (0712)
    bool prepActive() const {
        return cfg.prepAhead == PrepAhead::Thread && !cfg.normals && !cfg.slopes && inited && haveMap && !warming &&
               map.grid && map.tables;
    }
    static void prepRun(Impl* P);
    void prepStart();
    void prepStopThread();
    void prepClearLocked();   // the queues and the cache (under prep.m)
    std::shared_ptr<const PrepProduct> prepTake(TileId tile, uint64_t gen, uint8_t& how, double& waitMs);
    void prepInvalidate();
    ~Impl() {
        prepStopThread();   // ticket 0712: the worker reads the map's grid and calls the own sources
        stopLoader();   // ticket 0698: the thread reads through cfg.fs and creates on dev
        for (StyleInputs& s : styles) s.release();
        decals.reset();
        post.release();
        rel(ctx);
        rel(dev);
    }
    GridNormalSource& normals() { return cfg.normals ? *cfg.normals : *ownNormals; }
    SlopeClassSource& slopes() { return cfg.slopes ? *cfg.slopes : *ownSlopes; }
    HeightPageSource& pages() { return cfg.heightPages ? *cfg.heightPages : *ownPages; }
    BakeConstantsSource& constants() { return cfg.constants ? *cfg.constants : *ownConstants; }
};

PipelineBaker::PipelineBaker(const PipelineConfig& cfg) : p_(new Impl) { p_->cfg = cfg; }
PipelineBaker::~PipelineBaker() { delete p_; }
const PipelineTileInfo& PipelineBaker::lastTile() const { return p_->last; }
const PipelineStepTimes& PipelineBaker::lastSteps() const { return p_->steps; }
uint64_t PipelineBaker::slowBakes() const { return p_->slow; }
uint64_t PipelineBaker::firstUseBakes() const { return p_->firstUses; }
const WarmReport& PipelineBaker::warmReport() const { return p_->warmRep; }
size_t PipelineBaker::texturesLoaded() const { return p_->textures.loaded(); }
const std::vector<std::string>& PipelineBaker::missingTextures() const { return p_->textures.missing(); }
decal::DecalPass* PipelineBaker::decalPass() const { return p_->decals.get(); }
const Status& PipelineBaker::decalStatus() const { return p_->decalStatus; }
const decal::DecalCounters& PipelineBaker::decalCounters() const {
    return p_->decals ? p_->decals->counters() : p_->noCounters;
}

// ---------------------------------------------------------------- ticket 0698: the loader, the caller's side

bool PipelineBaker::loaderOn() const { return p_->loader.running; }
size_t PipelineBaker::loaderPending() const { return p_->pending(); }
size_t PipelineBaker::adoptLoaded() { return p_->adopt(); }
void PipelineBaker::stopLoader() { p_->stopLoader(); }
const PipelineLoaderCounters& PipelineBaker::loaderCounters() const { return p_->lc; }
const std::vector<BlendTextureCache::Load>& PipelineBaker::loaderLoads() const { return p_->loaderLoads; }

bool PipelineBaker::waitLoader(double timeoutMs) {
    Impl& p = *p_;
    const Clock::time_point t0 = Clock::now();
    for (;;) {
        p.adopt();
        if (p.pending() == 0) return true;
        if (std::chrono::duration<double, std::milli>(Clock::now() - t0).count() >= timeoutMs) return false;
        std::unique_lock<std::mutex> l(p.loader.m);
        p.loader.cv.wait_for(l, std::chrono::milliseconds(20), [&p]() { return p.loader.doneCount.load() != 0; });
    }
}

Status PipelineBaker::Impl::startLoader() {
    if (loader.running) return Status::Ok();
    if (!inited || !dev || !cfg.fs) return Status::fail(Err::NotFound, "pipeline loader: before init");
    loader.dev = dev;
    loader.fs = cfg.fs;
    loader.multiThreaded = (dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) == 0;
    {
        std::lock_guard<std::mutex> l(loader.m);
        loader.stop = false;
    }
    loader.worker = std::thread(&Loader::run, &loader);
    loader.running = true;
    return Status::Ok();
}

void PipelineBaker::Impl::stopLoader() {
    if (!loader.running) return;
    {
        std::lock_guard<std::mutex> l(loader.m);
        loader.stop = true;
    }
    loader.cv.notify_all();
    if (loader.worker.joinable()) loader.worker.join();
    // queued and finished names are dropped: a later bake reads them itself, as before 0698
    for (const Loader::Job& j : loader.queue) loader.known.erase(j.key);
    loader.queue.clear();
    for (auto& kv : loader.done) {
        kv.second.release();
        loader.known.erase(kv.first);
    }
    loader.done.clear();
    loader.doneCount.store(0);
    loader.inFlight.clear();
    loader.running = false;
}

size_t PipelineBaker::Impl::pending() const {
    if (!loader.running) return 0;
    std::lock_guard<std::mutex> l(loader.m);
    return loader.queue.size() + (loader.inFlight.empty() ? 0 : 1) + loader.done.size();
}

bool PipelineBaker::Impl::request(const std::string& key, const std::string& name, int style) {
    if (!loader.running || loader.known.count(key)) return false;
    if (!batchOpen && pending() == 0) {
        batchStart = Clock::now();
        batchOpen = true;
    }
    {
        std::lock_guard<std::mutex> l(loader.m);
        Loader::Job j;
        j.key = key;
        j.name = name;
        j.style = style;
        loader.queue.push_back(j);
    }
    loader.known.insert(key);
    ++lc.requested;
    loader.cv.notify_all();
    return true;
}

size_t PipelineBaker::Impl::requestLayers(const LayerTables& t) {
    size_t n = 0;
    for (uint32_t id = 0; id < t.layerCount && id < t.setOfLayer.size(); ++id) {
        if (t.setOfLayer[id] >= t.sets.size()) continue;
        const TextureSet& s = t.sets[t.setOfLayer[id]];
        const std::string* names[3] = {&s.diffuse, &s.heightmap, &s.specmap};
        for (int k = 0; k < 3; ++k) {
            const std::string& nm = *names[k];
            if (nm.empty() || (k == 2 && nm == "NULL") || textures.has(nm)) continue;
            if (request(layerKey(nm), nm, -1)) ++n;
        }
    }
    return n;
}

size_t PipelineBaker::Impl::adopt() {
    if (loader.doneCount.load() == 0) return 0;   // cheap when nothing is finished
    std::map<std::string, Loader::Done> got;
    {
        std::lock_guard<std::mutex> l(loader.m);
        got.swap(loader.done);
        loader.doneCount.store(0);
    }
    size_t n = 0;
    for (auto& kv : got) {
        Loader::Done& d = kv.second;
        Status st = d.why;
        if (d.style >= 0) {
            const uint32_t s = static_cast<uint32_t>(d.style);
            if (st.ok() && !d.created) {   // a SINGLETHREADED device: made here, on the caller's thread
                st = createStyleInputs(dev, d.files, d.inputs);
                if (st.ok()) ++lc.createdAtAdopt;
            }
            if (!st.ok()) {
                d.release();
                loader.failed.insert(kv.first);
                ++lc.failed;
                continue;
            }
            if (styleLoaded[s]) {   // a bake loaded it meanwhile
                d.release();
                ++lc.dropped;
            } else {
                styles[s] = d.inputs;
                d.inputs = StyleInputs();
                styleLoaded[s] = true;
                ++lc.styles;
            }
        } else if (d.missing) {
            if (!textures.provideMissing(d.name)) {   // MissingTexture::Fail: a bake fails NotFound as before
                loader.failed.insert(kv.first);
                ++lc.failed;
                continue;
            }
            ++lc.missing;
        } else {
            if (st.ok() && !d.created) {
                gamefs::DdsImage img;
                st = gamefs::parseDds(d.file.data(), d.file.size(), img);
                ID3D11Resource* res = nullptr;
                if (st.ok()) st = gamefs::createTexture(dev, img, &res, &d.srv);
                if (res) res->Release();
                if (st.ok()) ++lc.createdAtAdopt;
            }
            if (!st.ok()) {
                d.release();
                loader.failed.insert(kv.first);
                ++lc.failed;
                continue;
            }
            if (textures.provide(d.name, d.srv)) ++lc.layerTextures;
            else ++lc.dropped;
            d.release();   // the cache holds its own reference
        }
        lc.bytes += d.load.bytes;
        lc.readMs += d.load.readMs;
        lc.readMsMax = std::max(lc.readMsMax, d.load.readMs);
        lc.createMs += d.load.createMs;
        loaderLoads.push_back(d.load);
        ++n;
    }
    if (!got.empty() && batchOpen && pending() == 0) {
        batchOpen = false;
        logf("pipeline: loader: queue empty %.1f ms after its first request: %s",
             std::chrono::duration<double, std::milli>(Clock::now() - batchStart).count(), lc.str().c_str());
    }
    return n;
}

bool PipelineBaker::Impl::waitFor(const std::string& key) {
    if (!loader.running || !loader.known.count(key) || loader.failed.count(key)) return false;
    const Clock::time_point w0 = Clock::now();
    {
        std::unique_lock<std::mutex> l(loader.m);
        if (!loader.done.count(key) && loader.inFlight != key) {
            bool queued = false;
            for (auto it = loader.queue.begin(); it != loader.queue.end(); ++it)
                if (it->key == key) {
                    queued = true;
                    if (it != loader.queue.begin()) {
                        Loader::Job j = *it;
                        loader.queue.erase(it);
                        loader.queue.push_front(j);
                        ++lc.promoted;
                    }
                    break;
                }
            if (!queued) return false;   // taken already (nothing to wait for)
        }
        loader.cv.wait(l, [this, &key]() { return loader.stop || loader.done.count(key) != 0; });
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - w0).count();
    ++steps.loaderWaits;
    steps.loaderWaitMs += ms;
    adopt();
    return true;
}

// ---------------------------------------------------------------- ticket 0712: prep ahead, the worker and the cache

// The worker: one job at a time, lane 0 first. It reads the job's grid and tables in place (the writers join it first:
// prepInvalidate) and calls the pipeline's own sources, which keep no state (prepActive allows no other source).
void PipelineBaker::Impl::prepRun(Impl* P) {
    Prep& q = P->prep;
    _mm_setcsr(q.mxcsr);   // the caller's floating-point control (denormals, rounding), as the inline computation has it
    std::unique_lock<std::mutex> l(q.m);
    for (;;) {
        q.cv.wait(l, [&q]() { return q.stop || !q.lane[0].empty() || !q.lane[1].empty(); });
        if (q.stop) break;
        std::deque<PrepJob>& d = !q.lane[0].empty() ? q.lane[0] : q.lane[1];
        const PrepJob j = d.front();
        d.pop_front();
        if (j.epoch != q.epoch || q.cache.count(j.key)) continue;
        q.busy = true;
        q.busyKey = j.key;
        q.busyEpoch = j.epoch;
        l.unlock();
        std::shared_ptr<PrepProduct> prod = std::make_shared<PrepProduct>();
        TileId tile;
        tile.x = j.key.x;
        tile.y = j.key.y;
        const Clock::time_point t0 = Clock::now();
        const Status st = prepCpu(*j.g, *j.t, tile, *P->ownNormals, *P->ownSlopes, *prod);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        l.lock();
        q.busy = false;
        q.c.workerMs += ms;
        if (!st.ok()) {
            q.c.failed++;
        } else if (j.epoch != q.epoch) {
            q.c.discarded++;
        } else {
            PrepEntry e;
            e.bytes = prod->bytes();
            e.p = std::move(prod);
            e.stamp = ++q.stamp;
            q.cache[j.key] = std::move(e);
            q.c.made++;
            q.c.bytes += q.cache[j.key].bytes;
            const size_t cap = std::max<uint32_t>(1, P->cfg.prepCacheProducts);
            while (q.cache.size() > cap) {   // the least recently made or used
                auto old = q.cache.begin();
                for (auto it = q.cache.begin(); it != q.cache.end(); ++it)
                    if (it->second.stamp < old->second.stamp) old = it;
                if (!old->second.used) q.c.evictedUnused++;
                q.c.bytes -= old->second.bytes;
                q.cache.erase(old);
            }
            q.c.products = q.cache.size();
            q.c.bytesMax = std::max(q.c.bytesMax, q.c.bytes);
        }
        q.done.notify_all();
    }
}

void PipelineBaker::Impl::prepStart() {
    if (prep.started) return;
    prep.mxcsr = _mm_getcsr();
    prep.stop = false;
    prep.th = std::thread(&Impl::prepRun, this);
    prep.started = true;
}

void PipelineBaker::Impl::prepClearLocked() {
    prep.lane[0].clear();
    prep.lane[1].clear();
    prep.cache.clear();
    prep.c.products = 0;
    prep.c.bytes = 0;
}

void PipelineBaker::Impl::prepStopThread() {
    if (!prep.started) return;
    {
        std::lock_guard<std::mutex> l(prep.m);
        prep.stop = true;
        prep.epoch++;
        prepClearLocked();
    }
    prep.cv.notify_all();
    if (prep.th.joinable()) prep.th.join();
    prep.started = false;
    prep.stop = false;
}

void PipelineBaker::Impl::prepInvalidate() {
    if (!prep.started) return;   // nothing was ever requested: nothing reads, nothing is cached
    std::unique_lock<std::mutex> l(prep.m);
    prep.c.invalidations++;
    prep.epoch++;
    prepClearLocked();
    if (prep.busy) {
        // R3 of the frame-jobs note: always joined, no timeout; a job is one prepCpu (a few ms)
        const Clock::time_point t0 = Clock::now();
        prep.done.wait(l, [this]() { return !prep.busy; });
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        prep.c.joinWaits++;
        prep.c.joinMs += ms;
        prep.c.joinMsMax = std::max(prep.c.joinMsMax, ms);
        prepClearLocked();   // the job's product was discarded (its epoch ended); kept clear anyway
    }
}

// The bake's side: a cached product (how 1), one in flight waited for (2, at most about the time of computing it), else
// null (3: the bake computes inline; a queued job of the same key is taken off the queue). 0 when prep ahead is off.
std::shared_ptr<const PrepProduct> PipelineBaker::Impl::prepTake(TileId tile, uint64_t gen, uint8_t& how, double& waitMs) {
    how = 0;
    waitMs = 0;
    if (!prepActive()) return nullptr;
    std::unique_lock<std::mutex> l(prep.m);
    if (!prep.started) {   // on, nothing requested yet
        prep.c.misses++;
        how = 3;
        return nullptr;
    }
    PrepKey k;
    k.x = tile.x;
    k.y = tile.y;
    k.gen = gen;
    auto it = prep.cache.find(k);
    if (it != prep.cache.end()) {
        it->second.used = true;
        it->second.stamp = ++prep.stamp;
        prep.c.hits++;
        how = 1;
        return it->second.p;
    }
    if (prep.busy && prep.busyKey == k && prep.busyEpoch == prep.epoch) {
        const double boundMs = std::min(20.0, std::max(0.5, prepInlineMs));
        const Clock::time_point t0 = Clock::now();
        prep.done.wait_until(l, t0 + std::chrono::microseconds(static_cast<int64_t>(boundMs * 1000.0)),
                             [this, &k]() { return !(prep.busy && prep.busyKey == k); });
        waitMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        prep.c.waitMs += waitMs;
        prep.c.waitMsMax = std::max(prep.c.waitMsMax, waitMs);
        it = prep.cache.find(k);
        if (it != prep.cache.end()) {
            it->second.used = true;
            it->second.stamp = ++prep.stamp;
            prep.c.waits++;
            how = 2;
            return it->second.p;
        }
        if (prep.busy && prep.busyKey == k) prep.c.waitTimeouts++;
        prep.c.misses++;
        how = 3;
        return nullptr;
    }
    bool claimed = false;
    for (std::deque<PrepJob>& d : prep.lane)
        for (auto j = d.begin(); j != d.end();) {
            if (j->key == k) {
                j = d.erase(j);
                claimed = true;
            } else {
                ++j;
            }
        }
    if (claimed) prep.c.claimed++;
    prep.c.misses++;
    how = 3;
    return nullptr;
}

bool PipelineBaker::prepOn() const { return p_->prepActive(); }

size_t PipelineBaker::prepRequest(int lane, const PrepHint* hints, size_t n) {
    Impl& p = *p_;
    if (!p.prepActive() || lane < 0 || lane > 1 || (!hints && n)) return 0;
    p.prepStart();
    const size_t cap = std::max<uint32_t>(1, p.cfg.prepCacheProducts);
    size_t queued = 0;
    {
        std::lock_guard<std::mutex> l(p.prep.m);
        std::deque<Impl::PrepJob> next;
        for (size_t i = 0; i < n && next.size() < cap; ++i) {
            Impl::PrepJob j;
            j.key.x = hints[i].tile.x;
            j.key.y = hints[i].tile.y;
            j.key.gen = hints[i].contentGeneration;
            j.epoch = p.prep.epoch;
            j.g = p.map.grid;
            j.t = p.map.tables;
            if (j.key.x >= p.map.grid->mapW || j.key.y >= p.map.grid->mapH) continue;
            if (p.prep.cache.count(j.key)) continue;
            if (p.prep.busy && p.prep.busyKey == j.key && p.prep.busyEpoch == p.prep.epoch) continue;
            bool dup = false;
            for (const Impl::PrepJob& o : next) dup = dup || o.key == j.key;
            for (const Impl::PrepJob& o : p.prep.lane[1 - lane]) dup = dup || o.key == j.key;
            if (dup) continue;
            next.push_back(j);
        }
        for (const Impl::PrepJob& o : p.prep.lane[lane]) {
            bool kept = false;
            for (const Impl::PrepJob& j : next) kept = kept || o.key == j.key;
            if (!kept) p.prep.c.replaced++;
        }
        for (const Impl::PrepJob& j : next) {
            bool had = false;
            for (const Impl::PrepJob& o : p.prep.lane[lane]) had = had || o.key == j.key;
            if (!had) {
                p.prep.c.requested++;
                queued++;
            }
        }
        p.prep.lane[lane].swap(next);
    }
    p.prep.cv.notify_one();
    return queued;
}

void PipelineBaker::prepInvalidate() { p_->prepInvalidate(); }
void PipelineBaker::prepStop() { p_->prepStopThread(); }

PrepCounters PipelineBaker::prepCounters() const {
    std::lock_guard<std::mutex> l(p_->prep.m);
    return p_->prep.c;
}

Status PipelineBaker::init(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    Impl& p = *p_;
    p.prepStopThread();   // ticket 0712: the worker reads the old map
    p.stopLoader();   // ticket 0698: the thread reads through the old device
    p.inited = false;
    if (!dev || !ctx) return Status::fail(Err::BadLayout, "pipeline init: no device or context");
    if (!p.cfg.fs) return Status::fail(Err::BadLayout, "pipeline init: no install view (PipelineConfig::fs)");
    if (!p.cfg.exe) return Status::fail(Err::BadLayout, "pipeline init: no shader section (PipelineConfig::exe)");
    if (!cacheMips(p.cfg.largestCacheSize))
        return Status::fail(Err::BadLayout, "pipeline init: largest cache size " + std::to_string(p.cfg.largestCacheSize));
    rel(p.ctx);
    rel(p.dev);
    p.dev = dev;
    p.dev->AddRef();
    p.ctx = ctx;
    p.ctx->AddRef();
    C5R_TRY(p.blend.init(dev, *p.cfg.exe));
    C5R_TRY(p.post.init(dev, *p.cfg.exe));
    // 4105: the decal pass beside the post passes. GameFiles' reads are const; DecalPass::init takes a plain pointer.
    p.decals.reset(new decal::DecalPass);
    p.decalStatus = p.decals->init(dev, *p.cfg.exe, const_cast<gamefs::GameFiles*>(p.cfg.decalFs ? p.cfg.decalFs : p.cfg.fs),
                                   p.cfg.decalVas ? *p.cfg.decalVas : decal::DecalShaderVas());
    if (!p.decalStatus.ok()) {
        p.decals.reset();
        if (p.cfg.decals) return Status::fail(p.decalStatus.code, "pipeline init: decal pass: " + p.decalStatus.msg);
    }
    if (p.cfg.gpuTiming != GpuTiming::Off) C5R_TRY(p.timer.init(dev));
    p.inited = true;
    p.warmRep = WarmReport();
    if (p.cfg.warm == WarmUp::AtInit) {
        // issue 0417: never fails init; a failure is in the report and the log, and a later bake fails as it would
        const Status w = warm(p.cfg.warmTables, p.cfg.warmSizeClass);
        p.logf("pipeline: warm-up at init %s", p.warmRep.str().c_str());
        (void)w;
    }
    if (p.cfg.loadAhead == LoadAhead::Thread) {
        // ticket 0698: the loader; the styles now (the layer textures at setMap). Never fails init: without it the bakes
        // read as before.
        const Status ls = p.startLoader();
        if (ls.ok()) {
            size_t n = 0;
            for (uint32_t s = 0; s < 5; ++s)
                if (!p.styleLoaded[s] && p.request(Impl::styleKey(s), std::string(), static_cast<int>(s))) ++n;
            p.logf("pipeline: loader: started (%s); %zu styles queued, the layer textures at setMap (ticket 0698)",
                   p.loader.multiThreaded ? "free-threaded device: read and made on the loader thread"
                                          : "single-threaded device: read on the loader thread, made at adoption",
                   n);
        } else {
            p.logf("pipeline: loader: did not start (%s): the bakes read the layer textures and styles themselves",
                   ls.str().c_str());
        }
    }
    return Status::Ok();
}

Status PipelineBaker::setMap(const MapInputs& in) {
    Impl& p = *p_;
    p.prepInvalidate();   // ticket 0712: the worker reads the map's grid and tables; the products are of the old map
    p.haveMap = false;
    p.map = MapInputs();
    if (!p.inited) return Status::fail(Err::NotFound, "pipeline setMap before init");
    if (!in.grid || !in.tables) return Status::fail(Err::BadLayout, "pipeline setMap: grid or tables missing");
    C5R_TRY(checkGrid(*in.grid));
    if (in.tables->layerCount == 0 || in.tables->layerCount > kMaxLayerIds)
        return Status::fail(Err::BadLayout, "pipeline setMap: layer count " + std::to_string(in.tables->layerCount));
    if (in.style >= 5) return Status::fail(Err::BadLayout, "pipeline setMap: style " + std::to_string(in.style));
    if (in.plotArtTypes && (in.plotsX != in.grid->mapW || in.plotsY == 0 || in.plotsY > in.grid->mapH))
        return Status::fail(Err::BadLayout, "pipeline setMap: plot art types " + std::to_string(in.plotsX) + " x " +
                                                std::to_string(in.plotsY) + " for a grid of " +
                                                std::to_string(in.grid->mapW) + " x " + std::to_string(in.grid->mapH) +
                                                " tiles");
    C5R_TRY(fillTerrainLayers(*in.tables, in.grid->mapW, p.terrainLayers));
    p.map = in;
    p.haveMap = true;
    if (p.loader.running && !p.warming) {
        // ticket 0698: every layer texture of the map's tables, ahead of the bakes (names in the cache or queued are
        // skipped, so a new field of the same tables queues nothing)
        p.adopt();
        const size_t n = p.requestLayers(*in.tables);
        if (n) p.logf("pipeline: loader: map set: %zu layer texture names queued (%zu in the cache)", n, p.textures.loaded());
    }
    return Status::Ok();
}

void PipelineBaker::heightsChanged(const heightfield::HeightGrid& g, const uint32_t* boxes, size_t boxCount) {
    p_->prepInvalidate();   // ticket 0712: the products read the old samples (the writer joined before it wrote)
    p_->pages().heightsChanged(g, boxes, boxCount);
}

uint64_t PipelineBaker::pagesBuilt() const { return p_->cfg.heightPages ? 0 : p_->ownPagesExe->built; }

// Ticket 0714: each tile's page through the same source and call as bakeBody's page step (tileWindow, then page()), so a
// page this makes is the one the tile's bake would have made; the source keeps it per page key.
Status PipelineBaker::warmPages(const TileId* tiles, size_t count, PageWarmReport* report) {
    Impl& p = *p_;
    PageWarmReport r;
    r.tiles = static_cast<uint32_t>(count);
    const Clock::time_point t0 = Clock::now();
    const double th0 = threadCpuMs();
    Status first = Status::Ok();
    if (!p.inited)
        first = Status::fail(Err::NotFound, "pipeline warmPages before init");
    else if (!p.haveMap)
        first = Status::fail(Err::NotFound, "pipeline warmPages before setMap");
    else if (!tiles && count)
        first = Status::fail(Err::BadLayout, "pipeline warmPages: no tiles");
    else {
        const heightfield::HeightGrid& g = *p.map.grid;
        const uint64_t built0 = p.ownPagesExe->built;
        std::set<uint32_t> keys;
        for (size_t k = 0; k < count; ++k) {
            const TileId t = tiles[k];
            if (t.x >= g.mapW || t.y >= g.mapH) {
                ++r.skipped;
                continue;
            }
            HeightPageIndex pi;
            int32_t off[2] = {0, 0};
            TileWindow w;
            Status st = heightPageForTile(g, t, pi, off);
            if (st.ok()) st = tileWindow(g, t, w);
            if (st.ok() && keys.insert(pi.px + 4096 * pi.py).second) {
                HeightPageView view;
                st = p.pages().page(p.dev, p.ctx, g, w, view);
            }
            if (!st.ok() && first.ok()) first = st;
        }
        r.pages = static_cast<uint32_t>(keys.size());
        r.built = static_cast<uint32_t>(p.ownPagesExe->built - built0);
    }
    r.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    r.threadMs = threadCpuMs() - th0;
    if (report) *report = r;
    return first;
}

Status PipelineBaker::bake(const BakeRequest& req, BakedTile& out) { return bakeWith(req, Overrides(), out); }

Status PipelineBaker::bakeWith(const BakeRequest& req, const Overrides& ov, BakedTile& out) {
    Impl& p = *p_;
    p.steps = PipelineStepTimes();
    const uint64_t decalTex0 = p.decals ? p.decals->counters().texturesLoaded : 0;
    const size_t loads0 = p.textures.loads().size(), decalLoads0 = p.decals ? p.decals->loads().size() : 0;
    const Clock::time_point t0 = Clock::now();
    const double th0 = threadCpuMs();
    const Status st = p.bakeBody(req, ov, out);
    p.steps.totalMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    p.steps.threadMs = threadCpuMs() - th0;
    const uint64_t decalTex1 = p.decals ? p.decals->counters().texturesLoaded : 0;
    p.steps.decalTexturesLoaded = static_cast<uint32_t>(decalTex1 - decalTex0);
    if (p.steps.loaderWaits) {   // ticket 0698
        ++p.lc.waits;
        p.lc.waitMs += p.steps.loaderWaitMs;
        p.lc.waitMsMax = std::max(p.lc.waitMsMax, p.steps.loaderWaitMs);
    }
    if (p.warming) return st;
    const bool slow = p.steps.totalMs >= p.cfg.slowBakeMs;
    const bool first = p.steps.firstUse();
    if (slow) ++p.slow;
    if (first) ++p.firstUses;
    if ((slow && p.slowLogged < 32) || (first && p.firstUseLogged < p.cfg.firstUseLogs)) {
        if (slow) ++p.slowLogged;
        if (first) ++p.firstUseLogged;
        const PipelineTileInfo& i = p.last;
        const Clock::time_point l0 = Clock::now();
        // the files this bake read (layer and decal textures; the style's files are in its style step), the slowest 6
        struct F {
            const std::string* name;
            uint64_t bytes;
            double readMs, createMs;
        };
        std::vector<F> files;
        for (size_t k = loads0; k < p.textures.loads().size(); ++k) {
            const BlendTextureCache::Load& l = p.textures.loads()[k];
            files.push_back(F{&l.name, l.bytes, l.readMs, l.createMs});
        }
        if (p.decals)
            for (size_t k = decalLoads0; k < p.decals->loads().size(); ++k) {
                const decal::DecalPass::TextureLoad& l = p.decals->loads()[k];
                files.push_back(F{&l.name, l.bytes, l.readMs, l.createMs});
            }
        std::string fl;
        if (!files.empty()) {
            double rs = 0, cs = 0;
            uint64_t bs = 0;
            for (const F& f : files) {
                rs += f.readMs;
                cs += f.createMs;
                bs += f.bytes;
            }
            std::sort(files.begin(), files.end(),
                      [](const F& a, const F& b) { return a.readMs + a.createMs > b.readMs + b.createMs; });
            char b[160];
            std::snprintf(b, sizeof b, "; files %zu, %.0f KB, read %.1f ms, create %.1f ms:", files.size(), bs / 1024.0, rs, cs);
            fl = b;
            for (size_t k = 0; k < files.size() && k < 6; ++k) {
                std::snprintf(b, sizeof b, " %s %.0f KB %.1f+%.1f ms,", files[k].name->c_str(), files[k].bytes / 1024.0,
                              files[k].readMs, files[k].createMs);
                fl += b;
            }
            if (files.size() > 6) fl += " ...";
        }
        // p.last is this tile's only when the bake got that far; a failed bake names its status instead
        p.logf("pipeline: %s: tile (%u, %u) size %u, %s%s%s, %s%s", slow ? "slow bake" : "first use", req.tile.x, req.tile.y,
               req.sizeClass, st.ok() ? "" : "failed: ", st.ok() ? "" : st.str().c_str(),
               st.ok() ? (std::string("style ") + std::to_string(i.style) + ", " + std::to_string(i.list.count) +
                          " layers, t8 " + (i.needSecond ? "1" : "0") + ", decals " +
                          std::to_string(req.decals ? req.decals->indices.size() : 0))
                             .c_str()
                       : "",
               p.steps.str().c_str(), fl.c_str());
        p.steps.logMs = std::chrono::duration<double, std::milli>(Clock::now() - l0).count();
    }
    return st;
}

Status PipelineBaker::Impl::bakeBody(const BakeRequest& req, const Overrides& ov, BakedTile& out) {
    out = BakedTile();
    Impl& p = *this;
    Clock::time_point lapAt = Clock::now();
    if (ov.needSecond < -1 || ov.needSecond > 1 || ov.style < -1 || ov.style > 4)
        return Status::fail(Err::BadLayout, "bake overrides: needSecond " + std::to_string(ov.needSecond) + ", style " +
                                                std::to_string(ov.style));
    if (!p.inited) return Status::fail(Err::NotFound, "pipeline bake before init");
    if (!p.haveMap) return Status::fail(Err::NotFound, "pipeline bake before setMap");
    const uint32_t mips = cacheMips(req.sizeClass);
    if (!mips) return Status::fail(Err::BadLayout, "size class " + std::to_string(req.sizeClass) + " is not 64/128/256/512");
    const heightfield::HeightGrid& g = *p.map.grid;
    const LayerTables& t = *p.map.tables;
    if (req.tile.x >= g.mapW || req.tile.y >= g.mapH)
        return Status::fail(Err::NotFound, "tile (" + std::to_string(req.tile.x) + ", " + std::to_string(req.tile.y) +
                                               ") outside the map " + std::to_string(g.mapW) + " x " + std::to_string(g.mapH));
    C5R_TRY(checkTarget(req.t0Target, req.sizeClass, "t0 target"));
    C5R_TRY(checkTarget(req.t8Target, req.sizeClass, "t8 target"));
    PipelineTileInfo info;
    // ticket 0712: the CPU products (layer list, window, slope classes, blend vertices and indices, grid normals): a
    // product of this tile and generation made ahead by the worker, or computed here; prepCpu is the one code path of both
    PrepProduct mine;
    std::shared_ptr<const PrepProduct> ahead = p.prepTake(req.tile, req.contentGeneration, p.steps.prepAhead, p.steps.prepWaitMs);
    if (ahead && p.cfg.prepVerify) {   // TEST ONLY: the product against the inline computation
        const Status vs = prepCpu(g, t, req.tile, p.normals(), p.slopes(), mine);
        const bool same = vs.ok() && samePrepProduct(*ahead, mine);
        {
            std::lock_guard<std::mutex> l(p.prep.m);
            p.prep.c.verified++;
            if (!same) p.prep.c.verifyMismatches++;
        }
        if (!same) {
            p.logf("LOUD: pipeline: prep ahead: the product of tile (%u, %u) gen %llu differs from the inline computation "
                   "(%s); the inline products are used (TEST ONLY check, ticket 0712)",
                   req.tile.x, req.tile.y, static_cast<unsigned long long>(req.contentGeneration),
                   vs.ok() ? "bytes" : vs.str().c_str());
            C5R_TRY(vs);
            ahead.reset();
        }
    } else if (!ahead) {
        C5R_TRY(prepCpu(g, t, req.tile, p.normals(), p.slopes(), mine));
        p.prepInlineMs = 0.8 * p.prepInlineMs + 0.2 * (mine.prepMs + mine.normalsMs);
    }
    const PrepProduct& pp = ahead ? *ahead : mine;
    {
        const double d = lap(lapAt);
        p.steps.normalsMs = ahead ? 0.0 : mine.normalsMs;
        p.steps.prepMs = std::max(0.0, d - p.steps.normalsMs);   // the checks, and the look-up or the computation
    }
    info.list = pp.list;
    info.window = pp.window;
    const TileWindow& w = info.window;
    info.needSecondByList = needsSecondCache(t, info.list, p.cfg.specMaps, false);
    // tile+0x2FC: a listed layer's set has a spec map, or tile+0x300 (area changed) with the spec switch on
    info.needSecond = ov.needSecond >= 0 ? ov.needSecond == 1
                                         : needsSecondCache(t, info.list, p.cfg.specMaps, req.areaChanged);
    info.normals = p.normals().name();
    info.slopes = p.slopes().name();
    info.heightPage = p.pages().name();
    info.constants = p.constants().name();
    if (ov.style >= 0) {
        info.style = static_cast<uint32_t>(ov.style);
        info.styleSource = "override";
    } else if (p.cfg.styles) {
        info.style = p.cfg.styles->style(req.tile);
        info.styleSource = p.cfg.styles->name();
    } else if (p.map.plotArtTypes) {
        // 3406's tileStyle over the DLL's plots (the grid's mapH - 4 rows are the DLL's map)
        info.style = tileStyle(p.map.plotArtTypes, p.map.plotsX, p.map.plotsY, p.map.wrapX, req.tile);
        info.styleSource = "MapInputs::plotArtTypes (tileStyle)";
    } else {
        info.style = p.map.style;
        info.styleSource = "MapInputs::style";
    }
    if (info.style >= 5) return Status::fail(Err::BadLayout, "style " + std::to_string(info.style) + " of the style source");

    if (p.cfg.gpuTiming != GpuTiming::Off) p.timer.begin(p.ctx);
    // 1. The blend pass (3402), on the tile's (ex + 2) x (ey + 2) grid (its vertices and indices: the products above).
    const uint32_t gw = w.ex + 2, gh = w.ey + 2;
    p.steps.prepMs += lap(lapAt);
    HeightPageView page;
    const uint64_t built0 = p.ownPagesExe->built;
    C5R_TRY(p.pages().page(p.dev, p.ctx, g, w, page));
    p.steps.pageBuilt = p.ownPagesExe->built != built0;
    p.steps.pageMs = lap(lapAt);
    info.pageOffsetX = page.offsetX;
    info.pageOffsetY = page.offsetY;
    std::vector<uint8_t> own;
    C5R_TRY(fillOwnBlock(t, g.mapW, info.list, p.cfg.largestCacheSize, page.offsetX, page.offsetY, own));
    p.steps.ownMs = lap(lapAt);
    BlendTextures tex;
    if (p.loader.running) {
        // ticket 0698: what the loader finished is taken; a name it has queued or in flight is waited for (moved to the
        // front of its queue); a name it never had, or failed, is left to bind, which reads it as before
        p.adopt();
        bool all = true;
        for (uint32_t i = 0; i < info.list.count && i < kMaxLayers; ++i) {
            const uint32_t id = info.list.ids[i];
            if (id >= t.layerCount || id >= t.setOfLayer.size() || t.setOfLayer[id] >= t.sets.size()) continue;   // bind fails it
            const TextureSet& s = t.sets[t.setOfLayer[id]];
            const std::string* names[3] = {&s.diffuse, &s.heightmap, &s.specmap};
            for (int k = 0; k < 3; ++k) {
                const std::string& nm = *names[k];
                if (k == 2 && (nm.empty() || nm == "NULL")) continue;
                if (p.textures.has(nm)) continue;
                p.waitFor(Impl::layerKey(nm));
                if (!p.textures.has(nm)) all = false;
            }
        }
        if (!all) ++p.lc.fallbackBinds;
    }
    // the bake's own reads: bind records each file it read or name it found missing in loads() (the loader's are not)
    const size_t loaded0 = p.textures.loads().size();
    const Status bound = p.textures.bind(p.dev, *p.cfg.fs, t, info.list, p.blend.dummySrv(), tex);
    p.steps.texturesLoaded = static_cast<uint32_t>(p.textures.loads().size() - loaded0);
    p.steps.texturesMs = lap(lapAt);
    C5R_TRY(bound);
    C5R_TRY(p.blend.run(p.ctx, pp.verts, pp.idx, own, p.terrainLayers, tex));
    p.steps.blendMs = lap(lapAt);

    // 2. The passes after the blend (3403), on the same grid (its normals: the products above, size checked by prepCpu).
    const std::vector<float>& normals = pp.normals;
    if (!p.styleLoaded[info.style] && p.loader.running) {
        // ticket 0698: the loader's style, waited for when queued or in flight
        p.adopt();
        if (!p.styleLoaded[info.style]) p.waitFor(Impl::styleKey(info.style));
        p.steps.styleMs = lap(lapAt);
    }
    if (!p.styleLoaded[info.style]) {
        p.steps.styleLoaded = true;
        const Status ls = loadStyleInputs(p.dev, *p.cfg.fs, info.style, p.styles[info.style]);
        p.steps.styleMs += lap(lapAt);
        C5R_TRY(ls);
        p.styleLoaded[info.style] = true;
    }
    PostInputs pin;
    for (int i = 0; i < 3; ++i) pin.blend[i] = p.blend.target(i);
    pin.blendMipsReady = true;  // BlendPass::run generates them
    pin.heightPage = page.srv;
    pin.originX = static_cast<int>(w.ox);
    pin.originY = static_cast<int>(w.oy);
    pin.gridNormals = normals.data();
    pin.gridNormalCount = gw * gh;
    pin.gridW = gw;
    pin.gridH = gh;
    pin.sunCube = p.styles[info.style].sunCube;
    pin.shadowCube = p.styles[info.style].shadowCube;
    pin.colorKey = p.styles[info.style].colorKey;
    pin.sizeClass = req.sizeClass;
    pin.needSecond = info.needSecond;
    // 4105: the tile's decals (post.h: null or empty draws nothing and changes nothing).
    pin.decals = req.decals;
    pin.decalPass = p.decals.get();
    // 0371: the near pair of the EXE's state-5 bake (null: unchanged)
    pin.nearCache[0] = req.nearCache[0];
    pin.nearCache[1] = req.nearCache[1];
    lap(lapAt);
    C5R_TRY(p.constants().constants(req.tile, pin.k));
    p.steps.constantsMs = lap(lapAt);
    pin.k.heightOffset[0] = static_cast<float>(page.offsetX);
    pin.k.heightOffset[1] = static_cast<float>(page.offsetY);
    pin.k.hexScale[0] = g.sx;
    pin.k.hexScale[1] = g.sy;
    pin.k.hexScale[2] = g.sz;
    PostOutputs po;
    const Status posted = p.post.run(p.ctx, pin, po);
    p.steps.postMs = lap(lapAt);
    C5R_TRY(posted);

    // 3. Into the caller's array slices, or returned.
    if (req.t0Target.array) {
        copyToSlice(p.ctx, po.t0, req.t0Target, mips);
        rel(po.t0);
    }
    if (po.t8 && req.t8Target.array) {
        copyToSlice(p.ctx, po.t8, req.t8Target, mips);
        rel(po.t8);
    }
    p.steps.copyMs = lap(lapAt);
    out.tile = req.tile;
    out.sizeClass = req.sizeClass;
    out.contentGeneration = req.contentGeneration;
    out.t0 = po.t0;
    out.t8 = po.t8;
    out.hasT8 = info.needSecond;
    out.gpuMs = p.cfg.gpuTiming == GpuTiming::Off ? 0.0f : p.timer.end(p.ctx, p.cfg.gpuTiming == GpuTiming::Blocking);
    p.steps.gpuTimerMs = lap(lapAt);
    p.last = info;
    return Status::Ok();
}

// ---------------------------------------------------------------- issue 0417: the warm-up

Status PipelineBaker::warm(const LayerTables* tables, uint32_t sizeClass) {
    Impl& p = *p_;
    p.warmRep = WarmReport();
    WarmReport& r = p.warmRep;
    if (!p.inited) {
        r.status = Status::fail(Err::NotFound, "pipeline warm before init");
        return r.status;
    }
    if (!cacheMips(sizeClass)) {
        r.status = Status::fail(Err::BadLayout, "warm: size class " + std::to_string(sizeClass) + " is not 64/128/256/512");
        return r.status;
    }
    r.ran = true;
    p.prepInvalidate();   // ticket 0712: the map set before warm() is dropped
    p.warming = true;
    p.haveMap = false;
    p.map = MapInputs();
    const Clock::time_point all = Clock::now();
    Clock::time_point at = all;
    auto note = [&](const Status& s) {
        if (!s.ok() && r.status.ok()) r.status = s;
    };
    // 1. Every layer texture of the tables, eight layers per bind (a layer list holds eight).
    if (tables) {
        const size_t before = p.textures.loaded() + p.textures.missing().size();
        for (uint32_t id = 0; id < tables->layerCount; id += kMaxLayers) {
            LayerList list;
            list.count = std::min<uint32_t>(kMaxLayers, tables->layerCount - id);
            for (uint32_t i = 0; i < list.count; ++i) list.ids[i] = id + i;
            BlendTextures tex;
            note(p.textures.bind(p.dev, *p.cfg.fs, *tables, list, p.blend.dummySrv(), tex));
        }
        r.textures = static_cast<uint32_t>(p.textures.loaded() + p.textures.missing().size() - before);
    }
    r.texturesMs = lap(at);
    // 2. The five styles' cubes and LUT.
    for (uint32_t s = 0; s < 5; ++s) {
        if (p.styleLoaded[s]) continue;
        const Status ls = loadStyleInputs(p.dev, *p.cfg.fs, s, p.styles[s]);
        note(ls);
        if (ls.ok()) {
            p.styleLoaded[s] = true;
            ++r.styles;
        }
    }
    r.stylesMs = lap(at);
    // 3. Throwaway bakes on a 3 x 3 map of its own: material 0 on the left, 1 on the right (two layers in the middle
    // column), every style with and without t8, then one with a decal (the decal pass's shaders and states).
    if (tables) {
        heightfield::HeightGrid g;
        g.mapW = g.mapH = 3;
        g.samplesX = g.samplesY = 3 * kTileSamples;
        g.wrapX = true;
        g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
        g.mat.resize(g.h.size());
        for (uint32_t y = 0; y < g.samplesY; ++y)
            for (uint32_t x = 0; x < g.samplesX; ++x) {
                g.h[g.index(x, y)] = static_cast<uint16_t>(12000 + 40 * ((x + 2 * y) % 64));
                g.mat[g.index(x, y)] = x < 100 ? 0 : 1;
            }
        MapInputs mi;
        mi.grid = &g;
        mi.tables = tables;
        Status sm = setMap(mi);
        note(sm);
        if (sm.ok()) {
            BakeRequest req;
            req.tile.x = 1;
            req.tile.y = 1;
            req.sizeClass = sizeClass;
            auto one = [&](const Overrides& ov, const decal::TileDecalList* decals) {
                BakeRequest q = req;
                q.decals = decals;
                BakedTile o;
                note(bakeWith(q, ov, o));
                if (o.t0) o.t0->Release();
                if (o.t8) o.t8->Release();
                ++r.bakes;
            };
            for (int s = 0; s < 5; ++s)
                for (int ns = 0; ns < 2; ++ns) {
                    Overrides ov;
                    ov.style = s;
                    ov.needSecond = ns;
                    one(ov, nullptr);
                }
            if (p.decals) {
                // one quad over the middle tile, pass 0, the default texture (no file read)
                decal::DecalSet set;
                set.worldRect[2] = static_cast<float>(g.samplesX);
                set.worldRect[3] = static_cast<float>(g.samplesY);
                decal::Decal d;
                d.rect[0] = 70;
                d.rect[1] = 70;
                d.rect[2] = 120;
                d.rect[3] = 110;
                d.priority = 0.5f;
                d.vertexCount = 6;
                const uint16_t h0 = 0, h1 = 0x3C00;  // half 0 and 1
                const uint16_t q[6][2] = {{h0, h0}, {h1, h0}, {h0, h1}, {h0, h1}, {h1, h0}, {h1, h1}};
                for (const auto& v : q) set.vertices.push_back(decal::DecalVertex{v[0], v[1], v[0], v[1]});
                set.decals.push_back(d);
                decal::buildIndex(set);
                const decal::TileDecalList list = decal::decalsForTile(set, 64, 64);
                Overrides ov;
                ov.needSecond = 1;
                if (!list.indices.empty()) one(ov, &list);
            }
        }
        p.haveMap = false;
        p.map = MapInputs();
        p.ownPagesExe->forget();
    }
    r.bakesMs = lap(at);
    // 4. Wait for the GPU to finish them (the driver's work on first use happens by then), at most 10 s.
    if (r.bakes) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
        ID3D11Query* q = nullptr;
        if (SUCCEEDED(p.dev->CreateQuery(&qd, &q))) {
            p.ctx->End(q);
            BOOL done = FALSE;
            while (p.ctx->GetData(q, &done, sizeof done, 0) != S_OK || !done) {
                if (std::chrono::duration<double, std::milli>(Clock::now() - at).count() > 10000.0) {
                    note(Status::fail(Err::Device, "warm: the GPU did not finish the throwaway bakes in 10 s"));
                    break;
                }
                std::this_thread::yield();
            }
            q->Release();
        }
        r.gpuWaitMs = lap(at);
    }
    r.ms = std::chrono::duration<double, std::milli>(Clock::now() - all).count();
    p.warming = false;
    return r.status;
}

}  // namespace bake
}  // namespace c5r
