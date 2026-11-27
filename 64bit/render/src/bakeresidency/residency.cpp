// residency.cpp - see c5r/bake/residency.h (issue 3407; moved from shell/server/residency.cpp in wave 3 and turned into
// the calls of epic 3300's TileCacheSource seam, with BC1 texture arrays).
#include "c5r/bake/residency.h"

#include <d3d11.h>
#include <windows.h>
#include <psapi.h>   // issue 0417: K32GetProcessMemoryInfo (page faults during a bake)

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "c5r/bake/post.h"      // createNearCache, nearCacheBytes (issue 0332)
#include "c5r/bake/standin.h"   // cacheBytes
#include "c5r/heightfield/heightfield.h"
#include "c5r/terrain/terrain.h"

namespace c5r {
namespace bake {

namespace {

double qpcMs() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

// issue 0417: this thread's CPU time (user + kernel, ms; the scheduler's granularity, about 15.6 ms) and the process's
// page faults, to tell a bake that ran from one that waited (a lock, I/O, the GPU, the scheduler)
double threadCpuMs() {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    const uint64_t kk = (static_cast<uint64_t>(k.dwHighDateTime) << 32) | k.dwLowDateTime;
    const uint64_t uu = (static_cast<uint64_t>(u.dwHighDateTime) << 32) | u.dwLowDateTime;
    return static_cast<double>(kk + uu) / 10000.0;
}
uint64_t pageFaults() {
    PROCESS_MEMORY_COUNTERS m = {};
    m.cb = sizeof m;
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m) ? m.PageFaultCount : 0;
}

inline uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 0x100000001b3ull;
    return h;
}

template <class T> void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

}  // namespace

int tileHeightClass(const heightfield::HeightGrid& g, uint32_t tx, uint32_t ty) {
    if (!g.samplesX || !g.samplesY || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY) return 0;
    const int X = static_cast<int>(g.samplesX), Y = static_cast<int>(g.samplesY);
    const int x0 = static_cast<int>(tx) * 64, y0 = static_cast<int>(ty) * 64;
    if (x0 >= X || y0 >= Y) return 0;
    uint16_t hmax = 0;
    for (int yy = y0 - 4; yy < y0 + 68; ++yy) {
        if (yy < 0 || yy >= Y) continue;
        const uint16_t* row = &g.h[static_cast<size_t>(yy) * X];
        for (int xx = x0 - 4; xx < x0 + 68; ++xx) {
            int sx = xx;
            if (sx < 0 || sx >= X) {
                if (!g.wrapX) continue;
                sx = (sx + X) % X;
            }
            hmax = std::max(hmax, row[sx]);
        }
    }
    const float hm = hmax / 255.0f;   // 0x7212B0: thresholds 2.0 (0x9DBBBC) and 30.0 (0x9F9134), no sz factor
    return hm < 2.0f ? 1 : hm < 30.0f ? 2 : 3;
}

// ---------------------------------------------------------------------------------------------------------------- Impl

struct Residency::Impl {
    struct Tile {
        uint8_t cls = 0;
        bool land = false, visible = false;
        bool dirty = false;           // the renderer's cache of this tile must be set (or cleared) at the next beforeDraw
        float zmin = 0, zmax = 0;
        uint64_t hash = 0;
        uint64_t gen = 1, bakedGen = 0, failedGen = 0;
        int32_t slot[2] = {-1, -1};   // array slots (t0, t8) when arrays are on
        ID3D11Texture2D* tex[2] = {}; // own textures when arrays are off
        ID3D11ShaderResourceView* srv[2] = {};
        // arrays on: the copy the renderer draws (a Texture2D from the pool), for tiles within one ring of the view: the
        // EXE's pixel shaders declare Texture2D, and reading a Texture2DArray view through them is undefined by the D3D11
        // specification (it works on the RTX 4080 of the tests, not something to ship on)
        int32_t draw[2] = {-1, -1};
        uint64_t drawGen = 0;
        uint64_t bytes = 0;
        double pendingSince = -1;
        int dist = -1;
        // issue 0371: the near pair handed to the renderer at the last setTileCache (size << 24 | entry t0 + 1; 0 = the
        // BC1 pair or nothing); a change of it marks the tile dirty
        uint32_t nearShown = 0;
        bool has() const { return srv[0] != nullptr; }
    };
    struct Chunk {
        ID3D11Texture2D* tex = nullptr;
        std::vector<ID3D11ShaderResourceView*> srv;
        uint32_t used = 0;
    };

    TileBaker* baker = nullptr;
    ResidencyOptions o;
    ResidencyLog log = nullptr;
    void* logCtx = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    bool haveField = false;
    uint32_t tx = 0, ty = 0, samplesX = 0, samplesY = 0;
    bool wrap = false;
    float sx = 1, sy = 1, sz = 1, ox = 0, oy = 0;
    std::vector<Tile> t;
    std::vector<uint32_t> visible, order;
    size_t cursor = 0;
    size_t staleCursor = 0;        // issue 0365: idle()'s stale pass (ResidencyOptions::staleFirst); reset with cursor
    bool haveView = false;
    double tokens = 0, gpuTokens = 0, lastIdleMs = -1, aheadStartMs = 0;
    int radius = -1;
    bool stopped = false, visibleChanged = false;
    int failLogs = 0, traced = 0;
    int catchUpLogs = 0;           // issue 0409: catch-up draws logged (the first 16; every field's first draw is logged)
    int slowLogs = 0;              // issue 0409: slow bakes logged (the first 16)
    static constexpr double kSlowBakeMs = 500.0;   // issue 0409: a bake this long (CPU, ms) is logged and counted
    // issue 0417: sums over every bakeOne since init: wall ms of the whole call and of the baker's call, this thread's CPU
    // ms in the baker's call, the process's page faults in it (the first draw's line takes the differences)
    double sumBakeOneMs = 0, sumBakerMs = 0, sumBakerThreadMs = 0;
    uint64_t sumBakerFaults = 0;
    int64_t drawsSinceField = 0;   // issue 0409: beforeDraw calls since the last onField
    uint32_t loadSeq = 0;          // issue 0409: the last onField's loadSeq, for the first draw's log line
    uint64_t one = 0;              // bytes of one cache
    uint32_t mips = 0;
    std::vector<Chunk> chunks;     // arrays: slot = chunk * arrayChunk + slice
    std::vector<int32_t> freeSlots;
    struct DrawTex {
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };
    std::vector<DrawTex> pool;     // draw copies: index = draw id
    std::vector<int32_t> freeDraw;
    ResidencyStats s;
    // issue 0332: the near pairs' state machine and pool textures (index = pool entry, per size 64 / 128 / 256 / 512)
    bool nearOn = false;
    bool nearHold = false;   // ticket 0495: setNearHold (the offline player's settle)
    NearCaches nearM;
    std::vector<ID3D11Texture2D*> nearTex[4];
    std::vector<ID3D11ShaderResourceView*> nearSrv[4];   // issue 0371: the draw's view of each pool texture (full chain)
    std::vector<uint8_t> listed;                          // issue 0371: terrain::exeTileClass3 per tile (the EXE's list rule)
    std::vector<uint32_t> nearNext;                       // ticket 0712: the last nearFrame's NearFrameResult::next
    float vp[16] = {};
    static int nearIdx(uint32_t size) { return size == 64 ? 0 : size == 128 ? 1 : size == 256 ? 2 : size == 512 ? 3 : -1; }
    void releaseNearTextures() {
        for (auto& v : nearSrv) {
            for (auto*& x : v) safeRelease(x);
            v.clear();
        }
        for (auto& v : nearTex) {
            for (auto*& x : v) safeRelease(x);
            v.clear();
        }
        s.nearTextures = 0;
        s.nearBytes = 0;
    }
    // The pool texture of an entry, made on first use (createNearCache).
    Status nearTexture(uint32_t size, int32_t e, ID3D11Texture2D** out) {
        *out = nullptr;
        const int i = nearIdx(size);
        if (i < 0 || e < 0) return Status::fail(Err::BadLayout, "near pool entry");
        if (nearTex[i].size() <= static_cast<size_t>(e)) nearTex[i].resize(static_cast<size_t>(e) + 1, nullptr);
        if (nearSrv[i].size() <= static_cast<size_t>(e)) nearSrv[i].resize(static_cast<size_t>(e) + 1, nullptr);
        if (!nearTex[i][e]) {
            C5R_TRY(createNearCache(dev, size, &nearTex[i][e]));
            // issue 0371: the view the terrain draw binds as t0 / t8 (R8G8B8A8_UNORM, every mip)
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
            v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = nearCacheMips(size);
            const HRESULT hr = dev->CreateShaderResourceView(nearTex[i][e], &v, &nearSrv[i][e]);
            if (FAILED(hr)) {
                safeRelease(nearTex[i][e]);
                char m[96];
                std::snprintf(m, sizeof(m), "near cache view 0x%08X", static_cast<unsigned>(hr));
                return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, m);
            }
            s.nearTextures++;
            s.nearBytes += nearCacheBytes(size);
        }
        *out = nearTex[i][e];
        return Status::Ok();
    }
    // issue 0371: what the EXE's draw binds for tile i (states 2 and 5 with a pair: the near pair, else the BC1 pair):
    // the near views and the key of nearShown; false when the tile draws its BC1 pair.
    bool nearDraw(size_t i, ID3D11ShaderResourceView* v[2], uint32_t& key) const {
        v[0] = v[1] = nullptr;
        key = 0;
        if (!nearOn || i >= nearM.tileCount() || !nearM.drawsNear(static_cast<uint32_t>(i))) return false;
        const uint32_t psize = nearM.nearSize(static_cast<uint32_t>(i));
        const int k = nearIdx(psize);
        if (k < 0) return false;
        for (int j = 0; j < 2; ++j) {
            const int32_t e = nearM.entry(static_cast<uint32_t>(i), j);
            if (e >= 0 && static_cast<size_t>(e) < nearSrv[k].size()) v[j] = nearSrv[k][e];
        }
        if (!v[0]) return false;
        key = (psize << 20) | static_cast<uint32_t>(nearM.entry(static_cast<uint32_t>(i), 0) + 1);
        return true;
    }
    // One frame of the near machine (before the visible bakes); returns the frame's up-res attempts.
    uint32_t nearFrame(const ResidencyView& v) {
        std::vector<NearVisibleTile> list;
        // issue 0371: the EXE's visible list of the main view (0x7BA820 / 0x4AD810: class-3 tiles whose bounding sphere is
        // in the frustum, terrain::exeFrameLod), not this residency's own wider set: the up-res pass of the EXE walks that
        // list, so a tile the EXE does not list never takes a pair (0371's first live run: 4 of 32 pairs of 512 went to
        // tiles outside the EXE's list at the load view). The residency's set stays for its own bakes.
        std::vector<uint32_t> exeList;
        terrain::LodFrame lf;
        const bool exeOk = listed.size() == t.size() &&
                           terrain::exeFrameLod(samplesX, samplesY, sx, sy, sz, ox, oy, vp, v.width, 4, 2, lf, listed.data(),
                                                &exeList).ok();
        const std::vector<uint32_t>& src = exeOk ? exeList : visible;
        list.reserve(src.size());
        // the near plane of the view (column 2 of the row-vector ViewProj), normalised by its xyz length
        const float pl[4] = {vp[2], vp[6], vp[10], vp[14]};
        const float len = std::sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
        for (uint32_t i : src) {
            if (i >= t.size() || !t[i].land) continue;
            const uint32_t x = i % tx, y = i / tx;
            float box[6];
            terrain::exeTileBox(x * 64, y * 64, terrain::exeTileVerts(x * 64, 64, samplesX),
                                terrain::exeTileVerts(y * 64, 64, samplesY), sx, sy, sz, ox, oy, box);
            NearVisibleTile n;
            n.tile = i;
            n.projectedSize = terrain::exeTileProjectedSize(box, vp, v.width);
            const float c[3] = {0.5f * (box[0] + box[3]), 0.5f * (box[1] + box[4]), 0.5f * (box[2] + box[5])};
            n.nearDistance = len > 0 ? (pl[0] * c[0] + pl[1] * c[1] + pl[2] * c[2] + pl[3]) / len : 0.0f;
            list.push_back(n);
        }
        const NearFrameResult r = nearM.frame(list.data(), list.size(), !v.moving);
        nearNext = r.next;   // ticket 0712: the next frames' up-res (nextBakes)
        s.nearShort += r.failed;
        for (const NearUpres& u : r.upres) {
            ID3D11Texture2D* tex[2] = {nullptr, nullptr};
            Status st = nearTexture(u.size, u.entry[0], &tex[0]);
            if (st.ok() && u.entry[1] >= 0) st = nearTexture(u.size, u.entry[1], &tex[1]);
            if (st.ok()) {
                TileId id;
                id.x = u.tile % tx;
                id.y = u.tile / tx;
                st = o.nearBaker->bakeNear(ctx, id, t[u.tile].gen, tex[0], tex[1]);
            }
            if (!st.ok()) {
                nearM.upresFailed(u.tile);
                s.nearBakeFailures++;
                if (failLogs++ < 8) logf("residency: near pair of tile (%u, %u) at %u failed: %s", u.tile % tx, u.tile / tx, u.size,
                                         st.str().c_str());
                continue;
            }
            s.nearUpres++;
            t[u.tile].dirty = true;
            if (t[u.tile].visible) visibleChanged = true;
        }
        s.nearLastAttempts = r.attempts;
        return r.attempts;
    }

    Status allocDraw(int32_t& id) {
        if (freeDraw.empty()) {
            DrawTex d;
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = td.Height = o.sizeClass;
            td.MipLevels = mips;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_BC1_TYPELESS;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = dev->CreateTexture2D(&td, nullptr, &d.tex);
            if (SUCCEEDED(hr)) {
                D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
                v.Format = DXGI_FORMAT_BC1_UNORM;
                v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                v.Texture2D.MipLevels = mips;
                hr = dev->CreateShaderResourceView(d.tex, &v, &d.srv);
                if (FAILED(hr)) safeRelease(d.tex);
            }
            if (FAILED(hr)) {
                char m[96];
                std::snprintf(m, sizeof(m), "draw copy CreateTexture2D 0x%08X", static_cast<unsigned>(hr));
                return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, m);
            }
            pool.push_back(d);
            freeDraw.push_back(static_cast<int32_t>(pool.size() - 1));
            s.drawTextures++;
        }
        id = freeDraw.back();
        freeDraw.pop_back();
        s.drawInUse++;
        return Status::Ok();
    }
    void freeDraw1(int32_t& id) {
        if (id < 0) return;
        freeDraw.push_back(id);
        s.drawInUse--;
        id = -1;
    }
    void releaseDrawCopies(Tile& x) {
        if (x.draw[0] < 0 && x.draw[1] < 0) return;
        freeDraw1(x.draw[0]);
        freeDraw1(x.draw[1]);
        x.drawGen = 0;
        x.dirty = true;
    }
    // Copies a slot into a draw texture (all mips).
    void copyOut(int32_t slot, int32_t id) {
        for (uint32_t m = 0; m < mips; ++m)
            ctx->CopySubresourceRegion(pool[id].tex, D3D11CalcSubresource(m, 0, mips), 0, 0, 0, slotTex(slot),
                                       D3D11CalcSubresource(m, slotSlice(slot), mips), nullptr);
    }
    // Arrays on: the tiles within one ring of the view get a draw copy of their current bake; the others give theirs back.
    void updateDrawCopies() {
        for (size_t i = 0; i < t.size(); ++i) {
            Tile& x = t[i];
            const bool nearView = x.dist >= 0 && x.dist <= 1 && x.has();
            if (!nearView) {
                releaseDrawCopies(x);
                continue;
            }
            if (x.draw[0] >= 0 && x.drawGen == x.bakedGen) continue;
            for (int k = 0; k < 2; ++k) {
                if (x.slot[k] < 0 || !x.srv[k]) {   // no such cache (a t8 slot can be held without a t8, 0137)
                    freeDraw1(x.draw[k]);
                    continue;
                }
                if (x.draw[k] < 0) {
                    Status a = allocDraw(x.draw[k]);
                    if (!a.ok()) {
                        releaseDrawCopies(x);
                        if (a.code == Err::OutOfMemory) onOutOfMemory(a.str());
                        else if (failLogs++ < 8) logf("residency: draw copy of tile (%u, %u): %s", static_cast<unsigned>(i % tx),
                                                      static_cast<unsigned>(i / tx), a.str().c_str());
                        break;
                    }
                }
                copyOut(x.slot[k], x.draw[k]);
                s.drawCopies++;
            }
            if (x.draw[0] >= 0) {
                x.drawGen = x.bakedGen;
                x.dirty = true;
            }
        }
    }
    ID3D11ShaderResourceView* drawSrv(const Tile& x, int k) const {
        return x.draw[k] >= 0 ? pool[x.draw[k]].srv : nullptr;
    }

    void logf(const char* fmt, ...) {
        if (!log) return;
        char b[1200];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        log(logCtx, b);
    }

    // ---- array slots
    Status allocSlot(int32_t& slot) {
        if (freeSlots.empty()) {
            const double after = static_cast<double>(s.arrayBytes + one * o.arrayChunk) / 1048576.0;
            if (o.memoryLimitMb > 0 && after > o.memoryLimitMb)
                return Status::fail(Err::OutOfMemory, "array chunk beyond the memory limit of " + std::to_string(o.memoryLimitMb) + " MB");
            Chunk c;
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = d.Height = o.sizeClass;
            d.MipLevels = mips;
            d.ArraySize = o.arrayChunk;
            d.Format = DXGI_FORMAT_BC1_TYPELESS;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = dev->CreateTexture2D(&d, nullptr, &c.tex);
            if (FAILED(hr)) {
                char m[96];
                std::snprintf(m, sizeof(m), "array CreateTexture2D 0x%08X", static_cast<unsigned>(hr));
                return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, m);
            }
            c.srv.assign(o.arrayChunk, nullptr);
            for (uint32_t i = 0; i < o.arrayChunk; ++i) {
                D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
                v.Format = DXGI_FORMAT_BC1_UNORM;
                v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                v.Texture2DArray.MipLevels = mips;
                v.Texture2DArray.FirstArraySlice = i;
                v.Texture2DArray.ArraySize = 1;
                hr = dev->CreateShaderResourceView(c.tex, &v, &c.srv[i]);
                if (FAILED(hr)) {
                    for (auto*& p : c.srv) safeRelease(p);
                    safeRelease(c.tex);
                    char m[96];
                    std::snprintf(m, sizeof(m), "array slice view 0x%08X", static_cast<unsigned>(hr));
                    return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, m);
                }
            }
            // reuse an empty chunk position if one was freed
            size_t ci = chunks.size();
            for (size_t k = 0; k < chunks.size(); ++k)
                if (!chunks[k].tex) {
                    ci = k;
                    break;
                }
            if (ci == chunks.size()) chunks.push_back(Chunk());
            chunks[ci] = c;
            for (int32_t i = static_cast<int32_t>(o.arrayChunk) - 1; i >= 0; --i)
                freeSlots.push_back(static_cast<int32_t>(ci * o.arrayChunk) + i);
            s.arrays++;
            s.arrayBytes += one * o.arrayChunk;
            s.slicesTotal += o.arrayChunk;
        }
        slot = freeSlots.back();
        freeSlots.pop_back();
        chunks[slot / o.arrayChunk].used++;
        s.slicesUsed++;
        return Status::Ok();
    }
    void freeSlot(int32_t slot) {
        if (slot < 0) return;
        Chunk& c = chunks[slot / o.arrayChunk];
        c.used--;
        s.slicesUsed--;
        freeSlots.push_back(slot);
        if (c.used == 0) {   // give the chunk's memory back
            const int32_t base = static_cast<int32_t>((slot / o.arrayChunk) * o.arrayChunk);
            freeSlots.erase(std::remove_if(freeSlots.begin(), freeSlots.end(),
                                           [&](int32_t v) { return v >= base && v < base + static_cast<int32_t>(o.arrayChunk); }),
                            freeSlots.end());
            for (auto*& p : c.srv) safeRelease(p);
            c.srv.clear();
            safeRelease(c.tex);
            s.arrays--;
            s.arrayBytes -= one * o.arrayChunk;
            s.slicesTotal -= o.arrayChunk;
        }
    }
    ID3D11Texture2D* slotTex(int32_t slot) { return chunks[slot / o.arrayChunk].tex; }
    ID3D11ShaderResourceView* slotSrv(int32_t slot) { return chunks[slot / o.arrayChunk].srv[slot % o.arrayChunk]; }
    uint32_t slotSlice(int32_t slot) { return static_cast<uint32_t>(slot % o.arrayChunk); }

    void releaseAll() {
        for (size_t i = 0; i < t.size(); ++i) dropTile(i, false);
        releaseNearTextures();
        for (auto& d : pool) {
            safeRelease(d.srv);
            safeRelease(d.tex);
        }
        pool.clear();
        freeDraw.clear();
        s.drawTextures = s.drawInUse = 0;
        for (auto& c : chunks) {
            for (auto*& p : c.srv) safeRelease(p);
            safeRelease(c.tex);
        }
        chunks.clear();
        freeSlots.clear();
        s.arrays = 0;
        s.arrayBytes = 0;
        s.slicesTotal = s.slicesUsed = 0;
        s.t8Held = 0;
    }

    // Drops a tile's bake; markDirty: the renderer must forget it at the next beforeDraw. keepNear (issue 0332): a re-bake
    // replaces the BC1 pair, the near pair stays; otherwise the tile is evicted and its near pair goes back to its pool.
    void dropTile(size_t i, bool markDirty, bool keepNear = false) {
        Tile& x = t[i];
        if (nearOn && !keepNear && i < nearM.tileCount()) nearM.onEvict(static_cast<uint32_t>(i));
        if (!x.has()) return;
        releaseDrawCopies(x);
        if (x.slot[1] >= 0 && !x.srv[1]) s.t8Held--;
        for (int k = 0; k < 2; ++k) {
            if (x.slot[k] >= 0) {
                freeSlot(x.slot[k]);
                x.slot[k] = -1;
                x.srv[k] = nullptr;   // the chunk owns its views
            } else {
                safeRelease(x.srv[k]);
            }
            safeRelease(x.tex[k]);
        }
        s.cacheBytes -= x.bytes;
        s.resident--;
        x.bytes = 0;
        x.bakedGen = 0;
        if (markDirty) {
            x.dirty = true;
            if (x.visible) visibleChanged = true;
        }
    }

    bool inRadius(size_t i) const {
        if (radius < 0) return true;
        const int d = t[i].dist;
        return haveView && d >= 0 && d <= radius;
    }
    // issue 0140: one yield of idle(); spanMs = the most the waiting camera can have been held up by the slice
    void noteYield(double spanMs) {
        spanMs = std::max(0.0, spanMs);
        s.yields++;
        s.yieldWaitSumMs += spanMs;
        s.yieldWaitMaxMs = std::max(s.yieldWaitMaxMs, spanMs);
        const int b = std::min(ResidencyStats::kYieldBins - 1, static_cast<int>(spanMs / 0.25));
        s.yieldWaitHist[b]++;
    }

    bool needsBake(size_t i) const {
        const Tile& x = t[i];
        return x.land && x.bakedGen != x.gen && x.failedGen != x.gen && inRadius(i);
    }

    // One tile's field inputs (onField's loop body, shared with onFieldRegion since ticket 4909 so both give the same
    // bits): class, land, drawn z bounds, the hash of its samples and materials; same: compared with the last field (a
    // change raises the generation). True when the tile changed.
    bool scanTile(const heightfield::HeightGrid& g, uint32_t x, uint32_t y, bool same) {
        Tile& q = t[static_cast<size_t>(y) * tx + x];
        const int X = static_cast<int>(g.samplesX), Y = static_cast<int>(g.samplesY);
        const bool hasMat = !g.mat.empty();
        const int x0 = static_cast<int>(x) * 64, y0 = static_cast<int>(y) * 64;
        uint16_t hmax = 0, zmn = 0xFFFF, zmx = 0;
        uint64_t h = 0xcbf29ce484222325ull;
        for (int yy = y0 - 4; yy < y0 + 68; ++yy) {
            if (yy < 0 || yy >= Y) continue;
            const bool core = yy >= y0 - 1 && yy <= y0 + 64;
            const bool hashRow = yy >= y0 - 3 && yy < y0 + 67;
            const uint16_t* row = &g.h[static_cast<size_t>(yy) * X];
            const uint8_t* mrow = hasMat ? &g.mat[static_cast<size_t>(yy) * X] : nullptr;
            for (int xx = x0 - 4; xx < x0 + 68; ++xx) {
                int sxx = xx;
                if (sxx < 0 || sxx >= X) {
                    if (!wrap) continue;
                    sxx = (sxx + X) % X;
                }
                const uint16_t v = row[sxx];
                hmax = std::max(hmax, v);
                if (core && xx >= x0 - 1 && xx <= x0 + 64) {
                    zmn = std::min(zmn, v);
                    zmx = std::max(zmx, v);
                }
                if (hashRow && xx >= x0 - 3 && xx < x0 + 67)
                    h = mix(h, static_cast<uint64_t>(v) | (static_cast<uint64_t>(mrow ? mrow[sxx] : 0) << 16));
            }
        }
        const float hm = hmax / 255.0f;
        const uint8_t cls = hm < 2.0f ? 1 : hm < 30.0f ? 2 : 3;
        // the drawn surface's bounds: the EXE draws the high byte * sz (c5r::terrain::drawnHeightAtSample, issue
        // 0134; until 2026-11-13 u16 / 255 * sz, up to 0.25 higher). The 255-based class above is the EXE's own rule.
        q.zmin = zmn == 0xFFFF ? 0.0f : static_cast<float>(zmn >> 8) * g.sz;
        q.zmax = static_cast<float>(zmx >> 8) * g.sz;
        const bool wasLand = q.land;
        q.cls = cls;
        q.land = cls >= 2;
        bool changed = false;
        if (same && (q.hash != h || wasLand != q.land)) {
            changed = true;
            q.gen++;
            q.failedGen = 0;
            if (q.pendingSince < 0) q.pendingSince = qpcMs();
            if (nearOn && same) nearM.onContentChanged(y * tx + x);
            if (!q.land) dropTile(static_cast<size_t>(y) * tx + x, true);
        }
        q.hash = h;
        return changed;
    }

    void computeRings() {
        const size_t n = t.size();
        std::vector<uint32_t> q;
        q.reserve(n);
        for (auto& x : t) x.dist = -1;
        if (haveView) {
            for (uint32_t i : visible)
                if (t[i].dist < 0) {
                    t[i].dist = 0;
                    q.push_back(i);
                }
        } else if (n) {
            const uint32_t c = (ty / 2) * tx + tx / 2;
            t[c].dist = 0;
            q.push_back(c);
        }
        for (size_t h = 0; h < q.size(); ++h) {
            const uint32_t i = q[h];
            const int x = static_cast<int>(i % tx), y = static_cast<int>(i / tx), d = t[i].dist;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    int nx = x + dx;
                    const int ny = y + dy;
                    if (ny < 0 || ny >= static_cast<int>(ty)) continue;
                    if (nx < 0 || nx >= static_cast<int>(tx)) {
                        if (!wrap) continue;
                        nx = (nx + static_cast<int>(tx)) % static_cast<int>(tx);
                    }
                    const uint32_t j = static_cast<uint32_t>(ny) * tx + static_cast<uint32_t>(nx);
                    if (t[j].dist >= 0) continue;
                    t[j].dist = d + 1;
                    q.push_back(j);
                }
        }
        order.clear();
        for (uint32_t i : q)
            if (t[i].land) order.push_back(i);
        for (uint32_t i = 0; i < static_cast<uint32_t>(n); ++i)
            if (t[i].land && t[i].dist < 0) order.push_back(i);
        cursor = 0;
        staleCursor = 0;
    }

    void evictOutside() {
        if (radius < 0) return;
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i].has() && !inRadius(i)) {
                dropTile(i, true);
                s.evicted++;
            }
    }

    void onOutOfMemory(const std::string& why) {
        s.oomEvents++;
        const int was = radius;
        const int now = (radius < 0 || radius > 5) ? 5 : radius - 1;
        if (now < 0) {
            stopped = true;
            logf("residency: out of memory at radius 0 (%s) with %u tiles resident (%.1f MB of arrays): baking stopped; unbaked "
                 "tiles stay plain", why.c_str(), s.resident, s.arrayBytes / 1048576.0);
            return;
        }
        radius = now;
        const uint32_t before = s.resident;
        evictOutside();
        cursor = 0;
        staleCursor = 0;
        logf("residency: out of memory (%s) with %u tiles resident: radius %d -> %d, %u tiles evicted, %u resident (%.1f MB "
             "of caches, %.1f MB of arrays)", why.c_str(), before, was, now, before - s.resident, s.resident,
             s.cacheBytes / 1048576.0, s.arrayBytes / 1048576.0);
    }

    // Copies a baker's own texture into an array slot (all mips) and releases it.
    void copyIn(ID3D11Texture2D*& own, int32_t slot) {
        for (uint32_t m = 0; m < mips; ++m)
            ctx->CopySubresourceRegion(slotTex(slot), D3D11CalcSubresource(m, slotSlice(slot), mips), 0, 0, 0, own,
                                       D3D11CalcSubresource(m, 0, mips), nullptr);
        safeRelease(own);
        s.copies++;
    }

    // Arrays on (issue 0137): a tile holds two slices from its first bake to its eviction, the t8 one also when the bake
    // made no t8, and a re-bake writes into the tile's own slices. Re-bakes set areaChanged, so the EXE's rule gives every
    // re-baked tile a t8 (tile+0x300); allocating that slice then made the arrays grow with every turn's re-bakes, chunk
    // by chunk (+11 MB of server private each, 3311), until each land tile had one. Holding it from the start puts the
    // arrays at their final size after the bake-ahead (2 slices per resident tile) and keeps them there.
    bool bakeOne(size_t i) {
        const double b0 = qpcMs();
        const bool r = bakeOneTimed(i);
        sumBakeOneMs += qpcMs() - b0;
        return r;
    }
    bool bakeOneTimed(size_t i) {
        Tile& x = t[i];
        BakeRequest rq;
        rq.tile.x = static_cast<uint32_t>(i % tx);
        rq.tile.y = static_cast<uint32_t>(i / tx);
        rq.sizeClass = o.sizeClass;
        rq.contentGeneration = x.gen;
        rq.areaChanged = x.gen > 1;
        int32_t slot[2] = {-1, -1};
        const bool inPlace = o.arrays && x.has() && x.slot[0] >= 0 && x.slot[1] >= 0;
        if (inPlace) {
            slot[0] = x.slot[0];
            slot[1] = x.slot[1];
        } else if (o.arrays) {
            for (int k = 0; k < 2; ++k) {
                Status a = allocSlot(slot[k]);
                if (!a.ok()) {
                    for (int j = 0; j < k; ++j) freeSlot(slot[j]);
                    if (a.code == Err::OutOfMemory) onOutOfMemory(a.str());
                    else {
                        x.failedGen = x.gen;
                        s.failures++;
                        if (failLogs++ < 8) logf("residency: tile (%u, %u): %s", rq.tile.x, rq.tile.y, a.str().c_str());
                    }
                    return false;
                }
            }
        }
        if (o.arrays) {
            rq.t0Target.array = slotTex(slot[0]);
            rq.t0Target.slice = slotSlice(slot[0]);
            rq.t8Target.array = slotTex(slot[1]);
            rq.t8Target.slice = slotSlice(slot[1]);
        }
        BakedTile out;
        const uint64_t f0 = pageFaults();
        const double th0 = threadCpuMs();
        const double c0 = qpcMs();
        Status st = baker->bake(rq, out);
        const double cpu = qpcMs() - c0;
        const double thread = threadCpuMs() - th0;
        const uint64_t faults = pageFaults() - f0;
        sumBakerMs += cpu;
        sumBakerThreadMs += thread;
        sumBakerFaults += faults;
        // issue 0409: one bake of the first ones after a load took 9.7 s of CPU in a live run (0404's run1: 11.6 s, run3:
        // 2.7 s) while the others took 4-15 ms; name such bakes so the cause can be found. Issue 0417: "of CPU" is the wall
        // time of the baker's call; the thread's own CPU time and the process's page faults in it follow
        if (cpu >= kSlowBakeMs) {
            s.slowBakes++;
            if (slowLogs++ < 16)
                logf("residency: slow bake: tile (%u, %u) took %.1f ms of CPU (%s; %s, bake %lld since the start; thread CPU "
                     "%.1f ms, %llu page faults)",
                     rq.tile.x, rq.tile.y, cpu, x.visible ? "a visible tile" : "not visible", st.ok() ? "ok" : st.str().c_str(),
                     static_cast<long long>(s.bakes + 1), thread, static_cast<unsigned long long>(faults));
        }
        s.bakeCpuMsSum += cpu;
        s.bakeCpuMsMax = std::max(s.bakeCpuMsMax, cpu);
        const double gpu = st.ok() && out.gpuMs > 0 ? out.gpuMs : 0.0;
        s.bakeGpuMsSum += gpu;
        s.bakeGpuMsMax = std::max(s.bakeGpuMsMax, gpu);
        gpuTokens -= gpu;
        // A failed bake in place leaves the tile its slices and its last bake (both bakers check everything before they
        // write a slice: PipelineBaker copies into the targets only after every pass succeeded, the stand-in validates first).
        auto undo = [&]() {
            safeRelease(out.t0);
            safeRelease(out.t8);
            if (inPlace) return;
            freeSlot(slot[0]);
            freeSlot(slot[1]);
        };
        if (!st.ok()) {
            undo();
            if (st.code == Err::OutOfMemory) {
                onOutOfMemory(st.str());
                return false;
            }
            x.failedGen = x.gen;
            s.failures++;
            if (failLogs++ < 8)
                logf("residency: bake of tile (%u, %u) generation %llu failed: %s (plain until its content changes)", rq.tile.x,
                     rq.tile.y, static_cast<unsigned long long>(x.gen), st.str().c_str());
            return false;
        }
        ID3D11Texture2D* tex[2] = {nullptr, nullptr};
        ID3D11ShaderResourceView* srv[2] = {nullptr, nullptr};
        const bool hasT8 = out.hasT8 || out.t8 != nullptr;
        uint64_t bytes = 0;
        if (o.arrays) {
            if (out.t0) copyIn(out.t0, slot[0]);   // a baker that ignored the target
            if (out.t8) copyIn(out.t8, slot[1]);
            // without a t8 the slice stays held (0137): a re-bake gives the tile one (areaChanged)
            srv[0] = slotSrv(slot[0]);
            srv[1] = hasT8 ? slotSrv(slot[1]) : nullptr;
            bytes = one * (hasT8 ? 2 : 1);
        } else {
            tex[0] = out.t0;
            tex[1] = out.t8;
            out.t0 = out.t8 = nullptr;
            if (!tex[0]) {
                safeRelease(tex[1]);
                x.failedGen = x.gen;
                s.failures++;
                if (failLogs++ < 8) logf("residency: tile (%u, %u): the baker returned no t0", rq.tile.x, rq.tile.y);
                return false;
            }
            for (int k = 0; k < 2; ++k) {
                if (!tex[k]) continue;
                D3D11_TEXTURE2D_DESC td;
                tex[k]->GetDesc(&td);
                D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
                sd.Format = DXGI_FORMAT_BC1_UNORM;
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MipLevels = td.MipLevels;
                const HRESULT hr = dev->CreateShaderResourceView(tex[k], &sd, &srv[k]);
                if (FAILED(hr)) {
                    for (int j = 0; j < 2; ++j) {
                        safeRelease(srv[j]);
                        safeRelease(tex[j]);
                    }
                    if (hr == E_OUTOFMEMORY) {
                        onOutOfMemory("CreateShaderResourceView E_OUTOFMEMORY");
                        return false;
                    }
                    x.failedGen = x.gen;
                    s.failures++;
                    return false;
                }
                bytes += one;
            }
        }
        const bool re = x.has();
        if (inPlace) {   // the same slices: only the counters of the old bake go; its draw copies are refreshed (drawGen)
            if (x.slot[1] >= 0 && !x.srv[1]) s.t8Held--;
            s.cacheBytes -= x.bytes;
            s.resident--;
        } else {
            dropTile(i, false, true);
        }
        if (o.arrays && slot[1] >= 0 && !srv[1]) s.t8Held++;
        for (int k = 0; k < 2; ++k) {
            x.slot[k] = slot[k];
            x.tex[k] = tex[k];
            x.srv[k] = srv[k];
        }
        x.bytes = bytes;
        x.bakedGen = out.contentGeneration ? out.contentGeneration : rq.contentGeneration;
        x.dirty = true;
        s.resident++;
        s.cacheBytes += bytes;
        s.bakes++;
        if (re) s.rebakes++;
        if (nearOn) nearM.onBake(static_cast<uint32_t>(i), x.visible, srv[1] != nullptr);
        if (x.visible) visibleChanged = true;
        if (x.pendingSince >= 0 && x.bakedGen == x.gen) {
            const double lat = std::max(0.0, qpcMs() - x.pendingSince);
            s.latencyN++;
            s.latencySumMs += lat;
            s.latencyMaxMs = std::max(s.latencyMaxMs, lat);
            x.pendingSince = -1;
        }
        if (traced < o.trace) {
            traced++;
            logf("residency: bake %d tile (%u, %u) gen %llu ring %d visible %d class %d%s", traced, rq.tile.x, rq.tile.y,
                 static_cast<unsigned long long>(x.bakedGen), x.dist, static_cast<int>(x.visible), static_cast<int>(x.cls),
                 re ? " re-bake" : "");
        }
        return true;
    }

    void setView(const ResidencyView& v) {
        float m[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double a = 0;
                for (int k = 0; k < 4; ++k) a += static_cast<double>(v.view[i * 4 + k]) * v.proj[k * 4 + j];
                m[i * 4 + j] = static_cast<float>(a);
            }
        auto culled = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
            int out[6] = {};
            for (int c = 0; c < 8; ++c) {
                const float x = (c & 1) ? x1 : x0, y = (c & 2) ? y1 : y0, z = (c & 4) ? z1 : z0;
                const float cx = x * m[0] + y * m[4] + z * m[8] + m[12];
                const float cy = x * m[1] + y * m[5] + z * m[9] + m[13];
                const float cz = x * m[2] + y * m[6] + z * m[10] + m[14];
                const float cw = x * m[3] + y * m[7] + z * m[11] + m[15];
                out[0] += cx < -cw;
                out[1] += cx > cw;
                out[2] += cy < -cw;
                out[3] += cy > cw;
                out[4] += cz < 0;
                out[5] += cz > cw;
            }
            for (int k = 0; k < 6; ++k)
                if (out[k] == 8) return true;
            return false;
        };
        std::memcpy(vp, m, sizeof(vp));
        // issue 0371 (from 0169 / 4314): the copies at the EXE's wrap period (3583 on Tiny), as the renderer draws them
        const float W = terrain::wrapPeriod(samplesX, sx);
        const int copies = wrap ? 3 : 1;
        const float shift[3] = {0.0f, -W, W};
        std::vector<std::pair<float, uint32_t>> vis;
        const uint32_t B = 8;
        for (uint32_t by = 0; by < ty; by += B)
            for (uint32_t bx = 0; bx < tx; bx += B) {
                const uint32_t ex = std::min(bx + B, tx), ey = std::min(by + B, ty);
                float zmn = 1e30f, zmx = -1e30f;
                for (uint32_t y = by; y < ey; ++y)
                    for (uint32_t x = bx; x < ex; ++x) {
                        const Tile& q = t[static_cast<size_t>(y) * tx + x];
                        zmn = std::min(zmn, q.zmin);
                        zmx = std::max(zmx, q.zmax);
                    }
                for (int k = 0; k < copies; ++k) {
                    const float X0 = (bx * 64.0f - 1) * sx - ox + shift[k], X1 = (ex * 64.0f + 1) * sx - ox + shift[k];
                    const float Y0 = (by * 64.0f - 1) * sy - oy, Y1 = (ey * 64.0f + 1) * sy - oy;
                    if (culled(X0, X1, Y0, Y1, zmn - 1, zmx + 1)) continue;
                    for (uint32_t y = by; y < ey; ++y)
                        for (uint32_t x = bx; x < ex; ++x) {
                            const Tile& q = t[static_cast<size_t>(y) * tx + x];
                            const float x0 = (x * 64.0f - 1) * sx - ox + shift[k], x1 = (x * 64.0f + 65) * sx - ox + shift[k];
                            const float y0 = (y * 64.0f - 1) * sy - oy, y1 = (y * 64.0f + 65) * sy - oy;
                            if (culled(x0, x1, y0, y1, q.zmin - 1, q.zmax + 1)) continue;
                            const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, cz = (q.zmin + q.zmax) / 2;
                            const float w = cx * m[3] + cy * m[7] + cz * m[11] + m[15];
                            vis.push_back(std::make_pair(w, y * tx + x));
                        }
                }
            }
        std::sort(vis.begin(), vis.end());
        for (uint32_t i : visible) t[i].visible = false;
        std::vector<uint32_t> nv;
        nv.reserve(vis.size());
        for (auto& p : vis)
            if (!t[p.second].visible) {
                t[p.second].visible = true;
                nv.push_back(p.second);
            }
        std::vector<uint32_t> a = nv, b = visible;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        const bool changed = !haveView || a != b;
        visible.swap(nv);
        haveView = true;
        uint32_t vl = 0;
        for (uint32_t i : visible) vl += t[i].land;
        s.visible = static_cast<uint32_t>(visible.size());
        s.visibleLand = vl;
        if (changed) {
            computeRings();
            evictOutside();
        }
    }

    // issue 0409: land tiles without a bake, outside the visible set, that the renderer may still draw for the view of the
    // last setView. c5r::terrain::Renderer culls a tile's box with z bounds over every LOD level of its height page (the
    // coarse levels average up to the whole 1024-sample page and a 3 x 3 sharpen follows: terrain.cpp, setHeightField), so
    // it draws tiles beyond this residency's tight bounds (high byte * sz of the tile + 1 sample) and counts them in
    // FrameStats::landTilesWithoutCache, the frame's published unbaked count. A superset of those: the same x / y box
    // with one more sample of margin (the renderer's wrap period is 1 unit shorter, 4314) and z over the whole byte range,
    // -1 to 255 * sz + 1. Nearest first.
    void edgeUnbaked(std::vector<uint32_t>& out) {
        out.clear();
        const float* m = vp;
        auto culled = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
            int o[6] = {};
            for (int c = 0; c < 8; ++c) {
                const float x = (c & 1) ? x1 : x0, y = (c & 2) ? y1 : y0, z = (c & 4) ? z1 : z0;
                const float cx = x * m[0] + y * m[4] + z * m[8] + m[12];
                const float cy = x * m[1] + y * m[5] + z * m[9] + m[13];
                const float cz = x * m[2] + y * m[6] + z * m[10] + m[14];
                const float cw = x * m[3] + y * m[7] + z * m[11] + m[15];
                o[0] += cx < -cw;
                o[1] += cx > cw;
                o[2] += cy < -cw;
                o[3] += cy > cw;
                o[4] += cz < 0;
                o[5] += cz > cw;
            }
            for (int k = 0; k < 6; ++k)
                if (o[k] == 8) return true;
            return false;
        };
        const float z0 = -1.0f, z1 = 255.0f * sz + 1.0f;
        const float W = samplesX * sx;
        const int copies = wrap ? 3 : 1;
        const float shift[3] = {0.0f, -W, W};
        std::vector<std::pair<float, uint32_t>> found;
        const uint32_t B = 8;
        for (uint32_t by = 0; by < ty; by += B)
            for (uint32_t bx = 0; bx < tx; bx += B) {
                const uint32_t ex = std::min(bx + B, tx), ey = std::min(by + B, ty);
                for (int k = 0; k < copies; ++k) {
                    const float X0 = (bx * 64.0f - 2) * sx - ox + shift[k], X1 = (ex * 64.0f + 2) * sx - ox + shift[k];
                    const float Y0 = (by * 64.0f - 2) * sy - oy, Y1 = (ey * 64.0f + 2) * sy - oy;
                    if (culled(X0, X1, Y0, Y1, z0, z1)) continue;
                    for (uint32_t y = by; y < ey; ++y)
                        for (uint32_t x = bx; x < ex; ++x) {
                            const uint32_t i = y * tx + x;
                            const Tile& q = t[i];
                            if (q.visible || q.has() || !needsBake(i)) continue;
                            const float x0 = (x * 64.0f - 2) * sx - ox + shift[k], x1 = (x * 64.0f + 66) * sx - ox + shift[k];
                            const float y0 = (y * 64.0f - 2) * sy - oy, y1 = (y * 64.0f + 66) * sy - oy;
                            if (culled(x0, x1, y0, y1, z0, z1)) continue;
                            const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, cz = (z0 + z1) / 2;
                            found.push_back(std::make_pair(cx * m[3] + cy * m[7] + cz * m[11] + m[15], i));
                        }
                }
            }
        std::sort(found.begin(), found.end());
        for (auto& f : found)
            if (std::find(out.begin(), out.end(), f.second) == out.end()) out.push_back(f.second);
    }

    void checkAhead(double nowMs) {
        if (s.aheadDone) return;
        size_t c = cursor;
        while (c < order.size() && !needsBake(order[c])) c++;
        if (c < order.size() || !(radius < 0 || haveView)) return;
        s.aheadDone = true;
        s.aheadMs = nowMs - aheadStartMs;
        s.aheadTiles = s.resident;
        logf("residency: bake-ahead complete: %u tiles resident (%.1f MB of caches, %.1f MB of arrays) %.0f ms after the field "
             "(radius %d, %lld bakes, %lld failed, %.1f ms of baker GPU time)", s.resident, s.cacheBytes / 1048576.0,
             s.arrayBytes / 1048576.0, s.aheadMs, radius, static_cast<long long>(s.bakes), static_cast<long long>(s.failures),
             s.bakeGpuMsSum);
    }
};

// ---------------------------------------------------------------------------------------------------------------- API

Residency::Residency() : p_(new Impl()) {}
Residency::~Residency() {
    p_->releaseAll();
    delete p_;
}

Status Residency::init(TileBaker* baker, const ResidencyOptions& o, ResidencyLog log, void* logCtx) {
    if (!baker) return Status::fail(Err::BadLayout, "residency: no baker");
    if (o.radius < -1 || o.radius > 128) return Status::fail(Err::BadLayout, "residency: radius outside -1..128");
    if (!(o.rate > 0) || !std::isfinite(o.rate)) return Status::fail(Err::BadLayout, "residency: rate must be > 0");
    if (o.burst < 0) return Status::fail(Err::BadLayout, "residency: burst < 0");
    if (!(o.gpuMsPerS >= 0) || !std::isfinite(o.gpuMsPerS)) return Status::fail(Err::BadLayout, "residency: gpu ms per s < 0");
    if (!(o.memoryLimitMb >= 0) || !std::isfinite(o.memoryLimitMb)) return Status::fail(Err::BadLayout, "residency: memory limit < 0");
    if (o.arrayChunk == 0 || o.arrayChunk > 2048) return Status::fail(Err::BadLayout, "residency: array chunk outside 1..2048");
    if (!cacheMips(o.sizeClass)) return Status::fail(Err::BadLayout, "residency: size class " + std::to_string(o.sizeClass));
    if (o.nearCaches) {
        if (!o.nearBaker) return Status::fail(Err::BadLayout, "residency: near caches on without a near baker");
        NearCaches probe;
        C5R_TRY(probe.init(1, o.nearOptions));
    }
    Impl& p = *p_;
    p.releaseAll();
    p.baker = baker;
    p.o = o;
    p.log = log;
    p.logCtx = logCtx;
    p.radius = o.radius;
    p.s.radiusInForce = o.radius;
    p.nearOn = o.nearCaches;
    p.one = cacheBytes(o.sizeClass);
    p.mips = cacheMips(o.sizeClass);
    return Status::Ok();
}

Status Residency::onField(ID3D11Device* dev, const heightfield::HeightGrid& g, uint32_t tilePlots, uint32_t loadSeq,
                          const MapInputs* map) {
    Impl& p = *p_;
    if (!p.baker) return Status::fail(Err::NotFound, "residency: init not done");
    if (!dev) return Status::fail(Err::BadLayout, "residency: no device");
    if (tilePlots != 1) return Status::fail(Err::Unsupported, "residency: tile plots " + std::to_string(tilePlots) + " (the bake's tile is 1 plot)");
    if (!g.samplesX || !g.samplesY || g.samplesX % 64 || g.samplesY % 64 || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY ||
        (!g.mat.empty() && g.mat.size() != g.h.size()))
        return Status::fail(Err::BadLayout, "residency: field sizes inconsistent");
    MapInputs mi;
    if (map) mi = *map;
    mi.grid = &g;
    Status st = p.baker->setMap(mi);
    if (!st.ok()) {
        p.logf("residency: baker setMap failed: %s (nothing baked for this field)", st.str().c_str());
        p.haveField = false;
        return st;
    }
    const double t0 = qpcMs();
    const uint32_t tx = g.samplesX / 64, ty = g.samplesY / 64;
    const bool same = p.haveField && tx == p.tx && ty == p.ty && dev == p.dev;
    if (!same) {
        p.releaseAll();
        p.t.assign(static_cast<size_t>(tx) * ty, Impl::Tile());
        p.s.resident = 0;
        p.s.cacheBytes = 0;
        p.tx = tx;
        p.ty = ty;
    }
    p.dev = dev;
    p.samplesX = g.samplesX;
    p.samplesY = g.samplesY;
    p.wrap = g.wrapX;
    p.sx = g.sx;
    p.sy = g.sy;
    p.sz = g.sz;
    p.ox = g.ox;
    p.oy = g.oy;
    uint32_t c1 = 0, c2 = 0, c3 = 0, changed = 0;
    for (uint32_t y = 0; y < ty; ++y)
        for (uint32_t x = 0; x < tx; ++x) {
            Impl::Tile& q = p.t[static_cast<size_t>(y) * tx + x];
            if (p.scanTile(g, x, y, same)) changed++;
            if (q.cls == 1) c1++;
            else if (q.cls == 2) c2++;
            else c3++;
            if (same && q.has()) q.dirty = true;   // the renderer dropped its caches in setHeightField
        }
    if (p.nearOn && (!same || p.nearM.tileCount() != tx * ty)) C5R_TRY(p.nearM.init(tx * ty, p.o.nearOptions));
    if (p.nearOn) {   // issue 0371: the EXE's list rule (height class 3 by its own window) for the up-res pass's visible list
        p.listed.assign(static_cast<size_t>(tx) * ty, 0);
        for (uint32_t y = 0; y < ty; ++y)
            for (uint32_t x = 0; x < tx; ++x) p.listed[static_cast<size_t>(y) * tx + x] = terrain::exeTileClass3(g, x, y) ? 1 : 0;
    }
    p.s.tilesX = tx;
    p.s.tilesY = ty;
    p.s.class1 = c1;
    p.s.class2 = c2;
    p.s.class3 = c3;
    p.s.land = c2 + c3;
    p.s.fields++;
    p.s.fieldChangedTiles += changed;
    p.haveField = true;
    p.nearNext.clear();   // ticket 0712: of the last field
    p.aheadStartMs = qpcMs();
    p.s.aheadDone = false;
    p.s.aheadMs = -1;
    p.s.aheadTiles = 0;
    p.drawsSinceField = 0;
    p.loadSeq = loadSeq;
    p.s.firstDrawUnbaked = -1;
    p.s.firstDrawBakes = -1;
    p.s.firstDrawMs = -1;
    p.tokens = 0;
    p.gpuTokens = 0;
    p.lastIdleMs = -1;
    if (!p.haveView || !same) {
        p.haveView = false;
        p.visible.clear();
        for (auto& q : p.t) q.visible = false;
    }
    p.computeRings();
    p.logf("residency: field %ux%u tiles (%s, load %u): class 1 %u, class 2 %u, class 3 %u -> %u land tiles to bake (%.1f MB of "
           "BC1 at %u); %u tiles changed; classified in %.1f ms; radius %d, rate %.0f/s, burst %d, gpu %.0f ms/s, arrays %d",
           tx, ty, same ? "same size: compared" : "new", loadSeq, c1, c2, c3, c2 + c3, (c2 + c3) * 2.0 * p.one / 1048576.0,
           p.o.sizeClass, changed, qpcMs() - t0, p.radius, p.o.rate, p.o.burst, p.o.gpuMsPerS, static_cast<int>(p.o.arrays));
    return Status::Ok();
}

// ticket 4909 (live-terrain.md 8.14)
Status Residency::onFieldRegion(ID3D11Device* dev, const heightfield::HeightGrid& g, const std::vector<terrain::SampleRect>& rects,
                                const MapInputs* map, RegionFieldStats* out) {
    Impl& p = *p_;
    const double t0 = qpcMs();
    if (!p.baker) return Status::fail(Err::NotFound, "residency: init not done");
    if (!p.haveField) return Status::fail(Err::NotFound, "residency: onFieldRegion before a field");
    if (!dev || dev != p.dev) return Status::fail(Err::BadLayout, "residency: onFieldRegion on another device");
    const uint32_t tx = g.samplesX / 64, ty = g.samplesY / 64;
    if (g.samplesX != p.samplesX || g.samplesY != p.samplesY || tx != p.tx || ty != p.ty || g.wrapX != p.wrap || g.sx != p.sx ||
        g.sy != p.sy || g.sz != p.sz || g.ox != p.ox || g.oy != p.oy || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY ||
        (!g.mat.empty() && g.mat.size() != g.h.size()))
        return Status::fail(Err::BadLayout, "residency: onFieldRegion: not the current field's layout");
    for (const terrain::SampleRect& r : rects)
        if (r.x0 >= r.x1 || r.y0 >= r.y1 || r.x1 > g.samplesX || r.y1 > g.samplesY)
            return Status::fail(Err::BadLayout, "residency: onFieldRegion: a rect is empty or outside the field");
    MapInputs mi;
    if (map) mi = *map;
    mi.grid = &g;
    Status st = p.baker->setMap(mi);
    if (!st.ok()) {
        p.logf("residency: baker setMap failed: %s (nothing baked for this field)", st.str().c_str());
        p.haveField = false;
        return st;
    }
    // a tile reads samples x0 - 4 .. x0 + 67 (columns wrapping with the field), rows y0 - 4 .. y0 + 67 (rows outside skipped)
    RegionFieldStats rs;
    uint32_t changed = 0;
    bool landMoved = false;
    for (uint32_t y = 0; y < ty; ++y) {
        const int64_t ya = static_cast<int64_t>(y) * 64 - 4, yb = ya + 72;
        bool rowHit = false;
        for (const terrain::SampleRect& r : rects)
            if (static_cast<int64_t>(r.y1) > ya && static_cast<int64_t>(r.y0) < yb) rowHit = true;
        if (!rowHit) continue;
        for (uint32_t x = 0; x < tx; ++x) {
            const int64_t xa = static_cast<int64_t>(x) * 64 - 4;
            bool hit = false;
            for (const terrain::SampleRect& r : rects) {
                if (static_cast<int64_t>(r.y1) <= ya || static_cast<int64_t>(r.y0) >= yb) continue;
                if (p.wrap ? terrain::spanMeetsWrappedWindow(r.x0, r.x1, xa, 72, g.samplesX)
                           : (static_cast<int64_t>(r.x1) > xa && static_cast<int64_t>(r.x0) < xa + 72)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) continue;
            Impl::Tile& q = p.t[static_cast<size_t>(y) * tx + x];
            const bool wasLand = q.land;
            if (p.scanTile(g, x, y, true)) changed++;
            landMoved = landMoved || wasLand != q.land;
            rs.tilesScanned++;
        }
    }
    if (p.nearOn) {   // the EXE's list rule (exeTileClass3 wraps rows and columns): its 63 x 63 window from + 4
        if (p.listed.size() != static_cast<size_t>(tx) * ty) p.listed.assign(static_cast<size_t>(tx) * ty, 0);
        for (uint32_t y = 0; y < ty; ++y)
            for (uint32_t x = 0; x < tx; ++x) {
                bool hit = false;
                for (const terrain::SampleRect& r : rects)
                    if (terrain::spanMeetsWrappedWindow(r.x0, r.x1, static_cast<int64_t>(x) * 64 + 4, 63, g.samplesX) &&
                        terrain::spanMeetsWrappedWindow(r.y0, r.y1, static_cast<int64_t>(y) * 64 + 4, 63, g.samplesY)) {
                        hit = true;
                        break;
                    }
                if (hit) p.listed[static_cast<size_t>(y) * tx + x] = terrain::exeTileClass3(g, x, y) ? 1 : 0;
            }
    }
    uint32_t c1 = 0, c2 = 0, c3 = 0;
    for (const Impl::Tile& q : p.t) {
        if (q.cls == 1) c1++;
        else if (q.cls == 2) c2++;
        else c3++;
    }
    p.s.class1 = c1;
    p.s.class2 = c2;
    p.s.class3 = c3;
    p.s.land = c2 + c3;
    p.s.fields++;
    p.s.fieldChangedTiles += changed;
    // The scheduling state (tokens, bake-ahead, first-draw counters) is kept: the same load's field, edited (onField
    // restarts them for a load). The rings and the bake order follow the land classes, as onField's.
    p.computeRings();
    rs.tilesChanged = changed;
    rs.landChanged = landMoved;
    rs.ms = qpcMs() - t0;
    if (out) *out = rs;
    p.logf("residency: field region (%zu rects, load %u): %u tiles compared, %u changed; class 1 %u, class 2 %u, class 3 %u; in %.1f ms",
           rects.size(), p.loadSeq, rs.tilesScanned, changed, c1, c2, c3, rs.ms);
    return Status::Ok();
}

Status Residency::fieldDigest(std::vector<uint64_t>& out) const {
    out.clear();
    const Impl& p = *p_;
    if (!p.haveField) return Status::fail(Err::NotFound, "residency: fieldDigest before a field");
    uint64_t h = 0xcbf29ce484222325ull;
    auto fold = [&](const void* d, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(d);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 0x100000001b3ull;
        }
    };
    for (const Impl::Tile& q : p.t) {
        fold(&q.cls, 1);
        fold(&q.land, 1);
        fold(&q.zmin, 4);
        fold(&q.zmax, 4);
        fold(&q.hash, 8);
        fold(&q.gen, 8);
        fold(&q.failedGen, 8);
        const bool has = q.has();
        fold(&has, 1);
    }
    out.push_back(h);   // [0] tiles
    h = 0xcbf29ce484222325ull;
    fold(p.listed.data(), p.listed.size());
    fold(&p.s.class1, sizeof p.s.class1);
    fold(&p.s.class2, sizeof p.s.class2);
    fold(&p.s.class3, sizeof p.s.class3);
    fold(&p.s.land, sizeof p.s.land);
    fold(&p.s.fieldChangedTiles, sizeof p.s.fieldChangedTiles);
    out.push_back(h);   // [1] the EXE list, the class counts, the changed-tile total
    h = 0xcbf29ce484222325ull;
    fold(p.order.data(), p.order.size() * sizeof(uint32_t));
    out.push_back(h);   // [2] the bake order
    return Status::Ok();
}

bool Residency::raiseTile(uint32_t x, uint32_t y, double signalMs) {
    Impl& p = *p_;
    if (!p.haveField || x >= p.tx || y >= p.ty) return false;
    Impl::Tile& q = p.t[static_cast<size_t>(y) * p.tx + x];
    q.gen++;
    q.failedGen = 0;
    if (q.pendingSince < 0) q.pendingSince = signalMs >= 0 ? signalMs : qpcMs();
    if (p.nearOn) p.nearM.onContentChanged(y * p.tx + x);
    p.s.raises++;
    p.cursor = 0;
    p.staleCursor = 0;
    return true;
}

void Residency::onPlotsChanged(const uint32_t* plotIndices, size_t count, double signalMs) {
    Impl& p = *p_;
    if (!p.haveField || !plotIndices) return;
    const uint32_t plotsX = p.tx, plotsY = p.ty >= 4 ? p.ty - 4 : 0;   // the grid has 2 + 2 margin rows [I]
    for (size_t k = 0; k < count; ++k) {
        const uint32_t pi = plotIndices[k];
        if (!plotsX || pi >= plotsX * plotsY) continue;
        p.s.plotsChanged++;
        const int px = static_cast<int>(pi % plotsX), py = static_cast<int>(pi / plotsX) + 2;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                int x = px + dx;
                const int y = py + dy;
                if (y < 0 || y >= static_cast<int>(p.ty)) continue;
                if (x < 0 || x >= static_cast<int>(p.tx)) {
                    if (!p.wrap) continue;
                    x = (x + static_cast<int>(p.tx)) % static_cast<int>(p.tx);
                }
                raiseTile(static_cast<uint32_t>(x), static_cast<uint32_t>(y), signalMs);
            }
    }
}

void Residency::beforeDraw(ID3D11DeviceContext* ctx, terrain::Renderer& r, const ResidencyView& v, double budgetMs) {
    Impl& p = *p_;
    if (!p.haveField || !ctx) return;
    p.ctx = ctx;
    p.setView(v);
    // issue 0332: the up-res pass runs first in the EXE's cull and its attempts count against the frame's bake budget
    const int nearUsed = (p.nearOn && !p.nearHold && !p.stopped && p.nearM.tileCount()) ? static_cast<int>(p.nearFrame(v)) : 0;
    const bool firstDraw = p.drawsSinceField++ == 0;
    uint32_t unbakedBefore = 0;   // visible land tiles without any bake that this frame could bake (issue 0409)
    for (uint32_t i : p.visible)
        if (!p.t[i].has() && p.needsBake(i)) unbakedBefore++;
    const double d0 = qpcMs();
    const int64_t bakes0 = p.s.bakes;
    // issue 0417: where the first draw's time went (the sums' differences over this call)
    const double one0 = p.sumBakeOneMs, baker0 = p.sumBakerMs, thread0 = p.sumBakerThreadMs, drawThread0 = threadCpuMs();
    const uint64_t faults0 = p.sumBakerFaults, drawFaults0 = pageFaults();
    bool caughtUp = false;
    uint32_t edge = 0;            // issue 0409: tiles at the view's edge baked by the catch-up (Impl::edgeUnbaked)
    if (!p.stopped) {
        const double c0 = qpcMs();
        int n = 0;
        const int burst = p.o.burst - nearUsed;
        // issue 0409: more tiles without any bake than the burst (a load's first draw, a jump): bake every one of them now,
        // without burst or budgetMs, so the frame has none in the fallback colour, then the ones at the view's edge that
        // the renderer's wider bounds draw too; the stale ones keep the burst below
        if (p.o.catchUpVisible && static_cast<int64_t>(unbakedBefore) > static_cast<int64_t>(burst)) {
            caughtUp = true;
            for (uint32_t i : p.visible) {
                if (p.stopped) break;
                if (p.t[i].has() || !p.needsBake(i)) continue;
                if (p.bakeOne(i)) p.s.predrawBakes++;
                n++;
            }
            std::vector<uint32_t> more;
            if (!p.stopped) p.edgeUnbaked(more);
            for (uint32_t i : more) {
                if (p.stopped) break;
                if (p.t[i].has() || !p.needsBake(i)) continue;
                if (p.bakeOne(i)) p.s.predrawBakes++;
                n++;
                edge++;
            }
        }
        for (uint32_t i : p.visible) {
            if (n >= burst || p.stopped) break;
            if (!p.needsBake(i)) continue;
            if (!p.o.staleBeforeDraw && p.t[i].has()) {   // 0140: drawn with its old bake; idle() re-bakes it first
                p.s.staleDeferred++;
                continue;
            }
            const bool re = p.t[i].has();
            if (p.bakeOne(i)) {
                p.s.predrawBakes++;
                if (re) p.s.predrawRebakes++;
            }
            n++;
            if (qpcMs() - c0 > budgetMs) break;
        }
        if (n) p.s.predrawMaxMs = std::max(p.s.predrawMaxMs, qpcMs() - c0);
    }
    if (p.o.arrays) p.updateDrawCopies();
    uint32_t nearDrawn[2] = {0, 0};
    for (size_t i = 0; i < p.t.size(); ++i) {
        Impl::Tile& q = p.t[i];
        // issue 0371: a tile in state 2 or 5 with a pair is drawn from its near pair (the EXE's draw objects +0x158 /
        // +0x160), every other one from its BC1 pair; a change between the two (up-res, release, eviction) sets it again
        ID3D11ShaderResourceView* nv[2] = {nullptr, nullptr};
        uint32_t key = 0;
        const bool nearNow = p.nearOn && p.nearDraw(i, nv, key);
        if (nearNow && q.visible) nearDrawn[p.nearM.nearSize(static_cast<uint32_t>(i)) == 512 ? 1 : 0]++;
        if (key != q.nearShown) q.dirty = true;
        if (!q.dirty) continue;
        q.dirty = false;
        q.nearShown = key;
        terrain::TileCache c;
        c.color = nearNow ? nv[0] : p.o.arrays ? p.drawSrv(q, 0) : q.srv[0];
        c.second = nearNow ? nv[1] : p.o.arrays ? p.drawSrv(q, 1) : q.srv[1];
        c.generation = static_cast<uint32_t>(q.bakedGen);
        Status st = r.setTileCache(static_cast<uint32_t>(i % p.tx), static_cast<uint32_t>(i / p.tx), c);
        if (!st.ok()) {
            p.s.sinkErrors++;
            if (p.failLogs++ < 8) p.logf("residency: setTileCache(%u, %u) failed: %s", static_cast<unsigned>(i % p.tx),
                                         static_cast<unsigned>(i / p.tx), st.str().c_str());
        }
    }
    p.s.nearDrawn256 = nearDrawn[0];
    p.s.nearDrawn512 = nearDrawn[1];
    p.visibleChanged = false;
    uint32_t gray = 0, stale = 0;
    for (uint32_t i : p.visible) {
        const Impl::Tile& q = p.t[i];
        if (!q.land) continue;
        if (!q.has()) gray++;
        else if (q.bakedGen != q.gen) stale++;
    }
    p.s.framesCounted++;
    if (gray) {
        p.s.grayFrames++;
        p.s.grayTileFrames += gray;
        if (p.s.aheadDone) p.s.grayFramesAfterAhead++;
    }
    if (stale) p.s.staleFrames++;
    // issue 0409: the catch-up's counters and log lines; the field's first draw is logged whether it caught up or not
    const double ms = qpcMs() - d0;
    const int64_t made = p.s.bakes - bakes0;
    if (caughtUp) {
        p.s.catchUpDraws++;
        p.s.catchUpBakes += made;
        p.s.catchUpEdgeBakes += edge;
        p.s.catchUpMaxTiles = std::max(p.s.catchUpMaxTiles, unbakedBefore);
        p.s.catchUpMaxMs = std::max(p.s.catchUpMaxMs, ms);
    }
    if (firstDraw) {
        p.s.firstDrawUnbaked = static_cast<int32_t>(unbakedBefore);
        p.s.firstDrawBakes = static_cast<int32_t>(made);
        p.s.firstDrawMs = ms;
        p.logf("residency: first draw of load %u (frame %lld): %u visible land tiles, %u without a bake; %lld baked before "
               "the draw (%u at the view's edge) in %.1f ms (%s); %u still without a bake",
               p.loadSeq, static_cast<long long>(v.frameId), p.s.visibleLand, unbakedBefore, static_cast<long long>(made), edge,
               ms, caughtUp ? "catch-up: every one" : p.o.catchUpVisible ? "within the burst" : "burst, catch-up off", gray);
        // issue 0417: the same draw split: the baker's calls (wall, this thread's CPU, page faults), the rest of the bakes
        // (slots, arrays, bookkeeping), the rest of the call; and the call's own thread CPU and page faults
        p.logf("residency: first draw of load %u split: baker calls %.1f ms (thread CPU %.1f ms, %llu page faults), the rest of "
               "the bakes %.1f ms, the rest of the draw %.1f ms; the draw's thread CPU %.1f ms, %llu page faults",
               p.loadSeq, p.sumBakerMs - baker0, p.sumBakerThreadMs - thread0,
               static_cast<unsigned long long>(p.sumBakerFaults - faults0), (p.sumBakeOneMs - one0) - (p.sumBakerMs - baker0),
               ms - (p.sumBakeOneMs - one0), threadCpuMs() - drawThread0,
               static_cast<unsigned long long>(pageFaults() - drawFaults0));
    } else if (caughtUp && p.catchUpLogs++ < 16) {
        p.logf("residency: catch-up draw (frame %lld): %u visible land tiles without a bake > burst %d; %lld baked before "
               "the draw (%u at the view's edge) in %.1f ms; %u still without a bake",
               static_cast<long long>(v.frameId), unbakedBefore, p.o.burst - nearUsed, static_cast<long long>(made), edge, ms,
               gray);
    }
}

void Residency::idle(ID3D11DeviceContext* ctx, double budgetMs) {
    Impl& p = *p_;
    if (!p.haveField || p.stopped || !ctx) return;
    p.ctx = ctx;
    const double now = qpcMs();
    if (p.lastIdleMs < 0) p.lastIdleMs = now;
    const double dt = now - p.lastIdleMs;
    p.lastIdleMs = now;
    p.tokens = std::min(p.tokens + dt * p.o.rate / 1000.0, std::max(1.0, static_cast<double>(p.o.burst)));
    if (p.o.gpuMsPerS > 0) p.gpuTokens = std::min(p.gpuTokens + dt * p.o.gpuMsPerS / 1000.0, p.o.gpuMsPerS / 4.0);
    if (budgetMs > 0) {
        // issue 0140: the yield check before each bake; lastCheck = the newest check that found no camera waiting
        double lastCheck = now;
        int baked = 0;
        bool yielded = false;
        while (p.tokens >= 1.0 && !p.stopped && (p.o.gpuMsPerS <= 0 || p.gpuTokens > 0)) {
            // issue 0365: stale tiles (a bake older than their content) first, then the tiles without one
            size_t pick = p.order.size();
            if (p.o.staleFirst) {
                while (p.staleCursor < p.order.size() &&
                       !(p.needsBake(p.order[p.staleCursor]) && p.t[p.order[p.staleCursor]].has()))
                    p.staleCursor++;
                pick = p.staleCursor;
            }
            const bool stalePass = pick < p.order.size();
            if (!stalePass) {
                while (p.cursor < p.order.size() && !p.needsBake(p.order[p.cursor])) p.cursor++;
                if (p.cursor >= p.order.size()) break;
                pick = p.cursor;
            }
            if (p.o.yield) {
                const double c = qpcMs();
                if (p.o.yield(p.o.yieldCtx)) {
                    yielded = true;
                    p.noteYield(c - lastCheck);   // at the slice's first check: the time since the call began
                    break;
                }
                lastCheck = c;
            }
            p.bakeOne(p.order[pick]);
            if (stalePass) p.s.staleFirstBakes++;
            baked++;
            p.tokens -= 1.0;
            if (qpcMs() - now > budgetMs) break;
        }
        if (baked) {
            const double end = qpcMs();
            // a camera that came during the last bake is held up by it too: the end check counts it
            if (p.o.yield && !yielded && p.o.yield(p.o.yieldCtx)) p.noteYield(end - lastCheck);
            p.s.idleSlices++;
            p.s.idleSliceMaxMs = std::max(p.s.idleSliceMaxMs, end - now);
        }
    }
    p.checkAhead(qpcMs());
}

double ResidencyStats::yieldWaitP99Ms() const {
    int64_t n = 0;
    for (int b = 0; b < kYieldBins; ++b) n += yieldWaitHist[b];
    if (!n) return 0;
    const int64_t want = (n * 99 + 99) / 100;   // ceil(0.99 n)
    int64_t c = 0;
    for (int b = 0; b < kYieldBins; ++b) {
        c += yieldWaitHist[b];
        if (c >= want) return b + 1 < kYieldBins ? 0.25 * (b + 1) : yieldWaitMaxMs;
    }
    return yieldWaitMaxMs;
}

bool Residency::takeVisibleChanged() {
    const bool v = p_->visibleChanged;
    p_->visibleChanged = false;
    return v;
}

// Ticket 0712 (residency.h): a read of the orders beforeDraw and idle() follow; nothing changes.
void Residency::nextBakes(std::vector<Hint>& nearHints, std::vector<Hint>& ahead, size_t maxAhead) const {
    const Impl& p = *p_;
    nearHints.clear();
    ahead.clear();
    if (!p.haveField || p.stopped || !p.tx) return;
    auto hint = [&p](uint32_t i) {
        Hint h;
        h.tile.x = i % p.tx;
        h.tile.y = i / p.tx;
        h.contentGeneration = p.t[i].gen;
        return h;
    };
    if (p.nearOn && !p.nearHold)
        for (uint32_t i : p.nearNext)
            if (i < p.t.size() && p.t[i].land) nearHints.push_back(hint(i));
    auto add = [&](uint32_t i) {
        for (const Hint& h : ahead)
            if (h.tile.x == i % p.tx && h.tile.y == i / p.tx) return;
        ahead.push_back(hint(i));
    };
    for (uint32_t i : p.visible) {
        if (ahead.size() >= maxAhead) return;
        if (i < p.t.size() && p.needsBake(i)) add(i);
    }
    if (p.o.staleFirst)
        for (size_t c = p.staleCursor; c < p.order.size() && ahead.size() < maxAhead; ++c) {
            const uint32_t i = p.order[c];
            if (p.needsBake(i) && p.t[i].has()) add(i);
        }
    for (size_t c = p.cursor; c < p.order.size() && ahead.size() < maxAhead; ++c)
        if (p.needsBake(p.order[c])) add(p.order[c]);
}

void Residency::release() {
    p_->releaseAll();
    p_->haveField = false;
    p_->haveView = false;
}

const ResidencyStats& Residency::stats() {
    Impl& p = *p_;
    uint32_t pend = 0;
    for (size_t i = 0; i < p.t.size(); ++i) pend += p.needsBake(i);
    p.s.pending = pend;
    p.s.radiusInForce = p.radius;
    if (p.nearOn && p.nearM.tileCount()) {
        const NearCacheCounts c = p.nearM.counts();
        p.s.nearHeld256 = c.held[2];
        p.s.nearHeld512 = c.held[3];
        p.s.nearFree256 = c.freeTex[2];
        p.s.nearFree512 = c.freeTex[3];
    }
    p.s.stopped = p.stopped;
    return p.s;
}

void Residency::statsLine(char* out, size_t n) {
    if (!out || !n) return;
    const ResidencyStats& r = stats();
    std::snprintf(out, n,
                  "land=%u resident=%u cache_mb=%.1f array_mb=%.1f arrays=%u slices=%u/%u t8_held=%u copies=%lld draw_tex=%u draw_used=%u "
                  "draw_copies=%lld pending=%u visible=%u "
                  "visible_land=%u bakes=%lld rebakes=%lld failed=%lld oom=%lld evicted=%lld sink_err=%lld radius=%d stopped=%d "
                  "ahead_done=%d ahead_ms=%.0f frames=%lld gray_frames=%lld gray_after_ahead=%lld gray_tile_frames=%lld "
                  "stale_frames=%lld raises=%lld plots_changed=%lld lat_n=%lld lat_max_ms=%.1f lat_mean_ms=%.1f "
                  "bake_cpu_mean_ms=%.3f bake_cpu_max_ms=%.3f bake_gpu_ms_total=%.1f bake_gpu_max_ms=%.3f "
                  "idle_slices=%lld idle_slice_max_ms=%.3f yields=%lld yield_wait_max_ms=%.3f yield_wait_mean_ms=%.3f "
                  "yield_wait_p99_ms=%.2f predraw_bakes=%lld predraw_rebakes=%lld predraw_max_ms=%.3f stale_deferred=%lld "
                  "stale_before_draw=%d yield=%d stale_first=%d stale_first_bakes=%lld catch_up=%d catch_up_draws=%lld catch_up_bakes=%lld "
                  "catch_up_edge_bakes=%lld catch_up_max_tiles=%u catch_up_max_ms=%.1f first_draw_unbaked=%d first_draw_bakes=%d first_draw_ms=%.1f slow_bakes=%lld",
                  r.land, r.resident, r.cacheBytes / 1048576.0, r.arrayBytes / 1048576.0, r.arrays, r.slicesUsed, r.slicesTotal,
                  r.t8Held, static_cast<long long>(r.copies), r.drawTextures, r.drawInUse, static_cast<long long>(r.drawCopies), r.pending, r.visible, r.visibleLand, static_cast<long long>(r.bakes),
                  static_cast<long long>(r.rebakes), static_cast<long long>(r.failures), static_cast<long long>(r.oomEvents),
                  static_cast<long long>(r.evicted), static_cast<long long>(r.sinkErrors), r.radiusInForce,
                  static_cast<int>(r.stopped), static_cast<int>(r.aheadDone), r.aheadMs, static_cast<long long>(r.framesCounted),
                  static_cast<long long>(r.grayFrames), static_cast<long long>(r.grayFramesAfterAhead),
                  static_cast<long long>(r.grayTileFrames), static_cast<long long>(r.staleFrames),
                  static_cast<long long>(r.raises), static_cast<long long>(r.plotsChanged), static_cast<long long>(r.latencyN),
                  r.latencyMaxMs, r.latencyN ? r.latencySumMs / r.latencyN : -1.0, r.bakes ? r.bakeCpuMsSum / r.bakes : -1.0,
                  r.bakeCpuMsMax, r.bakeGpuMsSum, r.bakeGpuMsMax, static_cast<long long>(r.idleSlices), r.idleSliceMaxMs,
                  static_cast<long long>(r.yields), r.yieldWaitMaxMs, r.yields ? r.yieldWaitSumMs / r.yields : -1.0,
                  r.yieldWaitP99Ms(), static_cast<long long>(r.predrawBakes), static_cast<long long>(r.predrawRebakes),
                  r.predrawMaxMs, static_cast<long long>(r.staleDeferred), static_cast<int>(p_->o.staleBeforeDraw),
                  static_cast<int>(p_->o.yield != nullptr), static_cast<int>(p_->o.staleFirst),
                  static_cast<long long>(r.staleFirstBakes), static_cast<int>(p_->o.catchUpVisible),
                  static_cast<long long>(r.catchUpDraws), static_cast<long long>(r.catchUpBakes),
                  static_cast<long long>(r.catchUpEdgeBakes), r.catchUpMaxTiles, r.catchUpMaxMs,
                  static_cast<int>(r.firstDrawUnbaked), static_cast<int>(r.firstDrawBakes), r.firstDrawMs,
                  static_cast<long long>(r.slowBakes));
    // issue 0371: the near pairs are not in this line (the server's per-second terrain line holds it in a buffer it was
    // already filling: 22 more fields cut its end, and every test reading the line failed); ResidencyStats has them and
    // the server writes them on a line of their own ("near 1s:", srv_render_scene.cpp)
}

bool Residency::hasField() const { return p_->haveField; }
uint32_t Residency::tilesX() const { return p_->tx; }
uint32_t Residency::tilesY() const { return p_->ty; }
int Residency::tileClass(uint32_t x, uint32_t y) const {
    if (!p_->haveField || x >= p_->tx || y >= p_->ty) return 0;
    return p_->t[static_cast<size_t>(y) * p_->tx + x].cls;
}
bool Residency::nearPair(uint32_t x, uint32_t y, ID3D11Texture2D** t0, ID3D11Texture2D** t8, uint32_t* size) const {
    const Impl& p = *p_;
    if (t0) *t0 = nullptr;
    if (t8) *t8 = nullptr;
    if (size) *size = 0;
    if (!p.nearOn || !p.haveField || x >= p.tx || y >= p.ty || !p.nearM.tileCount()) return false;
    const uint32_t i = y * p.tx + x;
    if (!p.nearM.drawsNear(i)) return false;
    const uint32_t sz = p.nearM.nearSize(i);
    const int k = Impl::nearIdx(sz);
    for (int j = 0; j < 2; ++j) {
        const int32_t e = p.nearM.entry(i, j);
        ID3D11Texture2D* tex = (k >= 0 && e >= 0 && static_cast<size_t>(e) < p.nearTex[k].size()) ? p.nearTex[k][e] : nullptr;
        if (j == 0 && !tex) return false;
        if (j == 0 && t0) *t0 = tex;
        if (j == 1 && t8) *t8 = tex;
    }
    if (size) *size = sz;
    return true;
}

const NearCaches* Residency::nearCaches() const {
    return p_->nearOn && p_->nearM.tileCount() ? &p_->nearM : nullptr;
}

// ---- ticket 0495: the near state for the offline scene harness (residency.h) --------------------------------------------
bool Residency::nearState(std::vector<uint8_t>& out) const {
    out.clear();
    if (!p_->nearOn || !p_->haveField || !p_->nearM.tileCount()) return false;
    p_->nearM.saveState(out);
    return true;
}

Status Residency::nearStaleImages(ID3D11DeviceContext* ctx, std::vector<NearPairImage>& out) const {
    const Impl& p = *p_;
    out.clear();
    if (!p.nearOn || !p.haveField || !p.nearM.tileCount()) return Status::fail(Err::NotFound, "near pairs off or no field");
    if (!ctx || !p.dev) return Status::fail(Err::NotFound, "no device or context");
    for (uint32_t i = 0; i < p.nearM.tileCount(); ++i) {
        if (!p.nearM.drawsNear(i) || !p.nearM.contentBehind(i)) continue;
        const uint32_t sz = p.nearM.nearSize(i);
        const int k = Impl::nearIdx(sz);
        for (uint32_t j = 0; j < 2; ++j) {
            const int32_t e = p.nearM.entry(i, static_cast<int>(j));
            if (e < 0) continue;
            ID3D11Texture2D* tex = (k >= 0 && static_cast<size_t>(e) < p.nearTex[k].size()) ? p.nearTex[k][e] : nullptr;
            if (!tex) return Status::fail(Err::NotFound, "near pair texture of tile " + std::to_string(i) + " not made");
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            d.MiscFlags = 0;
            ID3D11Texture2D* st = nullptr;
            HRESULT hr = p.dev->CreateTexture2D(&d, nullptr, &st);
            if (FAILED(hr)) return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, "near readback texture");
            ctx->CopyResource(st, tex);
            NearPairImage img;
            img.tile = i;
            img.size = sz;
            img.second = j;
            img.texels.reserve(static_cast<size_t>(nearCacheBytes(sz)));
            Status rs = Status::Ok();
            for (uint32_t m = 0; m < d.MipLevels && rs.ok(); ++m) {
                const uint32_t w = std::max(1u, sz >> m), h = std::max(1u, sz >> m);
                D3D11_MAPPED_SUBRESOURCE ms;
                hr = ctx->Map(st, m, D3D11_MAP_READ, 0, &ms);
                if (FAILED(hr)) {
                    rs = Status::fail(Err::Device, "near readback Map");
                    break;
                }
                for (uint32_t r = 0; r < h; ++r) {
                    const uint8_t* row = static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(r) * ms.RowPitch;
                    img.texels.insert(img.texels.end(), row, row + static_cast<size_t>(w) * 4);
                }
                ctx->Unmap(st, m);
            }
            st->Release();
            C5R_TRY(rs);
            out.push_back(std::move(img));
        }
    }
    return Status::Ok();
}

Status Residency::restoreNear(ID3D11DeviceContext* ctx, const uint8_t* data, size_t size, uint32_t* pairs,
                              const std::vector<NearPairImage>* images) {
    Impl& p = *p_;
    if (pairs) *pairs = 0;
    if (!p.nearOn || !p.haveField || !p.nearM.tileCount()) return Status::fail(Err::NotFound, "near pairs off or no field");
    if (!ctx) return Status::fail(Err::NotFound, "no device context");
    if (!p.o.nearBaker) return Status::fail(Err::NotFound, "no near baker");
    NearCaches loaded = p.nearM;
    C5R_TRY(loaded.loadState(data, size));
    // the recorded texels of pairs whose content was behind (checked against the loaded state before anything changes)
    std::vector<const NearPairImage*> img(static_cast<size_t>(loaded.tileCount()) * 2, nullptr);
    if (images)
        for (const NearPairImage& m : *images) {
            const bool ok = m.tile < loaded.tileCount() && m.second < 2 && loaded.drawsNear(m.tile) && loaded.nearSize(m.tile) == m.size &&
                            loaded.entry(m.tile, static_cast<int>(m.second)) >= 0 && m.texels.size() == nearCacheBytes(m.size) &&
                            !img[static_cast<size_t>(m.tile) * 2 + m.second];
            if (!ok)
                return Status::fail(Err::BadLayout, "near pair image of tile " + std::to_string(m.tile) + " (" + std::to_string(m.size) +
                                                        ", texture " + std::to_string(m.second) +
                                                        "): no such pair in the state, another size, a wrong length or twice");
            img[static_cast<size_t>(m.tile) * 2 + m.second] = &m;
        }
    p.nearM = loaded;
    p.ctx = ctx;
    // every pair the state holds: its recorded texels when it has them, else baked with the tile's current inputs as an
    // up-res would (Impl::nearFrame)
    for (uint32_t i = 0; i < p.nearM.tileCount() && i < p.t.size(); ++i) {
        if (!p.nearM.drawsNear(i)) continue;
        const uint32_t size2 = p.nearM.nearSize(i);
        ID3D11Texture2D* tex[2] = {nullptr, nullptr};
        Status st = p.nearTexture(size2, p.nearM.entry(i, 0), &tex[0]);
        if (st.ok() && p.nearM.entry(i, 1) >= 0) st = p.nearTexture(size2, p.nearM.entry(i, 1), &tex[1]);
        const NearPairImage* const* mine = &img[static_cast<size_t>(i) * 2];
        if (st.ok() && (mine[0] || mine[1])) {
            for (int j = 0; j < 2; ++j) {
                if (!tex[j]) continue;
                if (!mine[j]) {
                    st = Status::fail(Err::BadLayout, "an image for one of the pair's two textures only");
                    break;
                }
                const uint8_t* src = mine[j]->texels.data();
                for (uint32_t m = 0; m < nearCacheMips(size2); ++m) {
                    const uint32_t w = std::max(1u, size2 >> m);
                    ctx->UpdateSubresource(tex[j], m, nullptr, src, w * 4, 0);
                    src += static_cast<size_t>(w) * w * 4;
                }
            }
        } else if (st.ok()) {
            TileId id;
            id.x = i % p.tx;
            id.y = i / p.tx;
            st = p.o.nearBaker->bakeNear(ctx, id, p.t[i].gen, tex[0], tex[1]);
        }
        if (!st.ok()) {
            char m[96];
            std::snprintf(m, sizeof(m), "near pair of tile (%u, %u) at %u: ", i % p.tx, i / p.tx, size2);
            return Status::fail(st.code, m + st.msg);
        }
        p.t[i].dirty = true;
        if (p.t[i].visible) p.visibleChanged = true;
        if (pairs) (*pairs)++;
    }
    return Status::Ok();
}

void Residency::setNearHold(bool hold) { p_->nearHold = hold; }

bool Residency::tileBaked(uint32_t x, uint32_t y) const {
    if (!p_->haveField || x >= p_->tx || y >= p_->ty) return false;
    const Impl::Tile& q = p_->t[static_cast<size_t>(y) * p_->tx + x];
    return q.has() && q.bakedGen == q.gen;
}

bool Residency::tileHasBake(uint32_t x, uint32_t y) const {
    if (!p_->haveField || x >= p_->tx || y >= p_->ty) return false;
    return p_->t[static_cast<size_t>(y) * p_->tx + x].has();
}

// ---- ticket 0495: NearCaches' state as bytes (residency.h; here, beside the residency's restore, not in nearcache.cpp) ----------------------------------------------------------------------
namespace {
const uint32_t kStateSizes[4] = {64, 128, 256, 512};
const uint32_t kStateMagic = 0x434E3543u;   // "C5NC"
const uint32_t kStateVersion = 1;

void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& o, uint64_t v) {
    for (int i = 0; i < 8; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool get32(uint32_t& v) {
        if (n - at < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[at + i]) << (8 * i);
        at += 4;
        return true;
    }
    bool get64(uint64_t& v) {
        if (n - at < 8) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[at + i]) << (8 * i);
        at += 8;
        return true;
    }
};
bool validSize(uint32_t s) { return s == 0 || s == 64 || s == 128 || s == 256 || s == 512; }
}  // namespace

bool NearCaches::contentBehind(uint32_t tile) const {
    if (tile >= t_.size()) return false;
    const T& x = t_[tile];
    return (x.state == kNearHeld || x.state == kNearRebake) && x.entry[0] >= 0 && x.bakedGen < x.gen;
}

void NearCaches::saveState(std::vector<uint8_t>& out) const {
    out.clear();
    put32(out, kStateMagic);
    put32(out, kStateVersion);
    put32(out, static_cast<uint32_t>(t_.size()));
    put32(out, o_.pools.n64);
    put32(out, o_.pools.n128);
    put32(out, o_.pools.n256);
    put32(out, o_.pools.n512);
    put32(out, o_.minClass);
    put32(out, o_.upresSettled);
    put32(out, o_.perFrameMoving);
    put32(out, o_.budgetSettled);
    put32(out, o_.arrayMode ? 1u : 0u);
    put64(out, static_cast<uint64_t>(upres_));
    put64(out, static_cast<uint64_t>(upresFailed_));
    put64(out, static_cast<uint64_t>(released_));
    put64(out, static_cast<uint64_t>(frames_));
    for (int k = 0; k < 4; ++k) {
        put32(out, static_cast<uint32_t>(free_[k].size()));
        for (int32_t e : free_[k]) put32(out, static_cast<uint32_t>(e));
    }
    put32(out, static_cast<uint32_t>(lastUpres_.size()));
    for (uint32_t x : lastUpres_) put32(out, x);
    const T init;
    uint32_t n = 0;
    const size_t countAt = out.size();
    put32(out, 0);
    for (size_t i = 0; i < t_.size(); ++i) {
        const T& x = t_[i];
        if (x.state == init.state && x.req == init.req && x.prevState == init.prevState && x.prevReq == init.prevReq &&
            x.second == init.second && x.nearSize == init.nearSize && x.wanted == init.wanted && x.prevSize == init.prevSize &&
            x.entry[0] == init.entry[0] && x.entry[1] == init.entry[1] && x.prevEntry[0] == init.prevEntry[0] &&
            x.prevEntry[1] == init.prevEntry[1] && x.gen == init.gen && x.bakedGen == init.bakedGen)
            continue;
        n++;
        put32(out, static_cast<uint32_t>(i));
        put32(out, static_cast<uint32_t>(x.state) | static_cast<uint32_t>(x.req) << 8 | static_cast<uint32_t>(x.prevState) << 16 |
                       static_cast<uint32_t>(x.prevReq) << 24);
        put32(out, x.second ? 1u : 0u);
        put32(out, x.nearSize);
        put32(out, x.wanted);
        put32(out, x.prevSize);
        put32(out, static_cast<uint32_t>(x.entry[0]));
        put32(out, static_cast<uint32_t>(x.entry[1]));
        put32(out, static_cast<uint32_t>(x.prevEntry[0]));
        put32(out, static_cast<uint32_t>(x.prevEntry[1]));
        put64(out, x.gen);
        put64(out, x.bakedGen);
    }
    for (int i = 0; i < 4; ++i) out[countAt + i] = static_cast<uint8_t>(n >> (8 * i));
}

Status NearCaches::loadState(const uint8_t* data, size_t size) {
    if (!data && size) return Status::fail(Err::BadLayout, "near state: no data");
    Reader r{data, size};
    auto trunc = [&](const char* what) { return Status::fail(Err::Truncated, std::string("near state: short at ") + what); };
    auto bad = [&](const std::string& what) { return Status::fail(Err::BadLayout, "near state: " + what); };
    uint32_t magic = 0, version = 0, tiles = 0;
    if (!r.get32(magic) || !r.get32(version) || !r.get32(tiles)) return trunc("the header");
    if (magic != kStateMagic) return bad("magic is not C5NC");
    if (version != kStateVersion) return bad("version " + std::to_string(version) + " (1 is read)");
    if (tiles != t_.size())
        return bad("tile count " + std::to_string(tiles) + ", the machine has " + std::to_string(t_.size()) + " (another field?)");
    uint32_t opt[9];
    for (uint32_t& v : opt)
        if (!r.get32(v)) return trunc("the options");
    const uint32_t mine[9] = {o_.pools.n64, o_.pools.n128, o_.pools.n256, o_.pools.n512, o_.minClass,
                              o_.upresSettled, o_.perFrameMoving, o_.budgetSettled, o_.arrayMode ? 1u : 0u};
    static const char* const kOpt[9] = {"pool 64", "pool 128", "pool 256", "pool 512", "minClass", "upresSettled", "perFrameMoving",
                                        "budgetSettled", "arrayMode"};
    for (int i = 0; i < 9; ++i)
        if (opt[i] != mine[i])
            return bad(std::string(kOpt[i]) + " " + std::to_string(opt[i]) + ", the machine's is " + std::to_string(mine[i]));
    uint64_t cnt[4];
    for (uint64_t& v : cnt)
        if (!r.get64(v)) return trunc("the counters");
    std::vector<int32_t> fl[4];
    std::vector<uint8_t> used[4];
    for (int k = 0; k < 4; ++k) used[k].assign(o_.pools.count(kStateSizes[k]), 0);
    for (int k = 0; k < 4; ++k) {
        uint32_t m = 0;
        if (!r.get32(m)) return trunc("a free list");
        if (m > used[k].size()) return bad("free list of " + std::to_string(kStateSizes[k]) + " holds " + std::to_string(m) + " entries, the pool " +
                                           std::to_string(used[k].size()));
        for (uint32_t j = 0; j < m; ++j) {
            uint32_t e = 0;
            if (!r.get32(e)) return trunc("a free list");
            if (e >= used[k].size() || used[k][e]) return bad("free entry " + std::to_string(e) + " of " + std::to_string(kStateSizes[k]) +
                                                              " out of its pool or listed twice");
            used[k][e] = 1;
            fl[k].push_back(static_cast<int32_t>(e));
        }
    }
    uint32_t lastN = 0;
    if (!r.get32(lastN)) return trunc("the last up-res list");
    if (lastN > tiles) return bad("last up-res list of " + std::to_string(lastN));
    std::vector<uint32_t> last(lastN);
    for (uint32_t& x : last) {
        if (!r.get32(x)) return trunc("the last up-res list");
        if (x >= tiles) return bad("last up-res tile " + std::to_string(x) + " out of range");
    }
    uint32_t n = 0;
    if (!r.get32(n)) return trunc("the tile count");
    if (n > tiles) return bad("tile records " + std::to_string(n) + " above the tile count");
    std::vector<T> t(tiles, T());
    int64_t prev = -1;
    for (uint32_t j = 0; j < n; ++j) {
        uint32_t idx = 0, states = 0, second = 0, w[7];
        uint64_t gen = 0, bakedGen = 0;
        if (!r.get32(idx) || !r.get32(states) || !r.get32(second)) return trunc("a tile");
        for (uint32_t& v : w)
            if (!r.get32(v)) return trunc("a tile");
        if (!r.get64(gen) || !r.get64(bakedGen)) return trunc("a tile");
        if (idx >= tiles || static_cast<int64_t>(idx) <= prev) return bad("tile index " + std::to_string(idx) + " out of range or not ascending");
        prev = idx;
        T& x = t[idx];
        x.state = static_cast<uint8_t>(states & 0xFF);
        x.req = static_cast<uint8_t>(states >> 8 & 0xFF);
        x.prevState = static_cast<uint8_t>(states >> 16 & 0xFF);
        x.prevReq = static_cast<uint8_t>(states >> 24 & 0xFF);
        if (x.state > kNearHeld || x.req > kNearHeld || x.prevState > kNearHeld || x.prevReq > kNearHeld)
            return bad("tile " + std::to_string(idx) + ": a state above 5");
        if (second > 1) return bad("tile " + std::to_string(idx) + ": second " + std::to_string(second));
        x.second = second != 0;
        x.nearSize = w[0];
        x.wanted = w[1];
        x.prevSize = w[2];
        if (!validSize(x.nearSize) || !validSize(x.wanted) || !validSize(x.prevSize))
            return bad("tile " + std::to_string(idx) + ": a size not 0/64/128/256/512");
        x.entry[0] = static_cast<int32_t>(w[3]);
        x.entry[1] = static_cast<int32_t>(w[4]);
        x.prevEntry[0] = static_cast<int32_t>(w[5]);
        x.prevEntry[1] = static_cast<int32_t>(w[6]);
        x.gen = gen;
        x.bakedGen = bakedGen;
        if (x.entry[0] < 0 && x.entry[1] >= 0) return bad("tile " + std::to_string(idx) + ": a second entry without a first");
        if (x.entry[0] >= 0) {
            const int k = sizeIndex(x.nearSize);
            if (k < 0) return bad("tile " + std::to_string(idx) + ": a held pair without a size");
            for (int q = 0; q < 2; ++q) {
                const int32_t e = x.entry[q];
                if (e < 0) continue;
                if (static_cast<size_t>(e) >= used[k].size() || used[k][e])
                    return bad("tile " + std::to_string(idx) + ": entry " + std::to_string(e) + " of " + std::to_string(x.nearSize) +
                               " out of its pool or held twice");
                used[k][e] = 1;
            }
        }
    }
    if (r.at != r.n) return Status::fail(Err::Truncated, "near state: " + std::to_string(r.n - r.at) + " trailing bytes");
    for (int k = 0; k < 4; ++k)
        for (size_t e = 0; e < used[k].size(); ++e)
            if (!used[k][e]) return bad("entry " + std::to_string(e) + " of " + std::to_string(kStateSizes[k]) + " neither free nor held");
    t_.swap(t);
    for (int k = 0; k < 4; ++k) free_[k].swap(fl[k]);
    lastUpres_.swap(last);
    upres_ = static_cast<int64_t>(cnt[0]);
    upresFailed_ = static_cast<int64_t>(cnt[1]);
    released_ = static_cast<int64_t>(cnt[2]);
    frames_ = static_cast<int64_t>(cnt[3]);
    return Status::Ok();
}

}  // namespace bake
}  // namespace c5r
