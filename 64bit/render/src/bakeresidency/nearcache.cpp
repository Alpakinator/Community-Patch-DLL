// nearcache.cpp - the near-camera RGBA8 pair's pools and state machine (issue 0332); see c5r/bake/residency.h. Behaviour as
// research/05-graphics-composition/terrain-bake-passes.md 3.5.1 and 3.5.3 describe the EXE's cull 0x7BA820, its sort
// 0x5E7010 and the allocation 0x5D78A0 / 0x6AD280 (D83: written from that description, no decompiler output).
#include "c5r/bake/residency.h"

#include <algorithm>
#include <string>

#include "c5r/bake/heightpage.h"   // sizeClassForProjectedSize (0x4B2B40)
#include "c5r/bake/post.h"         // nearCacheBytes

namespace c5r {
namespace bake {

namespace {
const uint32_t kSizes[4] = {64, 128, 256, 512};
}

uint32_t NearPoolCounts::count(uint32_t size) const {
    switch (size) {
    case 64: return n64;
    case 128: return n128;
    case 256: return n256;
    case 512: return n512;
    default: return 0;
    }
}

uint64_t nearPoolBytes(const NearPoolCounts& c) {
    uint64_t b = 0;
    for (uint32_t s : kSizes) b += static_cast<uint64_t>(c.count(s)) * nearCacheBytes(s);
    return b;
}

int NearCaches::sizeIndex(uint32_t size) {
    switch (size) {
    case 64: return 0;
    case 128: return 1;
    case 256: return 2;
    case 512: return 3;
    default: return -1;
    }
}

Status NearCaches::init(uint32_t tileCount, const NearCacheOptions& o) {
    if (tileCount == 0 || tileCount > (1u << 22)) return Status::fail(Err::BadLayout, "near caches: tile count " + std::to_string(tileCount));
    for (uint32_t s : kSizes)
        if (o.pools.count(s) > 4096)
            return Status::fail(Err::BadLayout, "near caches: pool of " + std::to_string(s) + " above 4096 textures");
    if (sizeIndex(o.minClass) < 0) return Status::fail(Err::BadLayout, "near caches: min class " + std::to_string(o.minClass));
    if (o.upresSettled == 0 || o.upresSettled > 64 || o.perFrameMoving == 0 || o.perFrameMoving > 64)
        return Status::fail(Err::BadLayout, "near caches: up-res per frame outside 1..64");
    if (o.budgetSettled == 0 || o.budgetSettled > 256) return Status::fail(Err::BadLayout, "near caches: settled budget outside 1..256");
    o_ = o;
    t_.assign(tileCount, T());
    for (int k = 0; k < 4; ++k) {
        free_[k].clear();
        const uint32_t n = o.pools.count(kSizes[k]);
        // the free list is popped from its end (0x6AD280: array + count); entry 0 is taken first
        for (uint32_t e = n; e-- > 0;) free_[k].push_back(static_cast<int32_t>(e));
    }
    upres_ = upresFailed_ = released_ = frames_ = 0;
    return Status::Ok();
}

bool NearCaches::take(uint32_t size, int n, int32_t out[2], uint32_t& gotSize) {
    // 0x5D78A0 / 0x6AD280: one texture per cache; in array mode only the asked size, else the next smaller pool when the
    // asked one is empty; the two caches must end up the same size, or everything goes back.
    out[0] = out[1] = -1;
    uint32_t got[2] = {0, 0};
    for (int k = 0; k < n; ++k) {
        int i = sizeIndex(size);
        while (i >= 0 && free_[i].empty() && !o_.arrayMode) --i;
        if (i < 0 || free_[i].empty()) break;
        out[k] = free_[i].back();
        free_[i].pop_back();
        got[k] = kSizes[i];
    }
    const bool ok = out[0] >= 0 && (n < 2 || (out[1] >= 0 && got[0] == got[1]));
    if (!ok) {
        for (int k = 0; k < 2; ++k)
            if (out[k] >= 0) free_[sizeIndex(got[k])].push_back(out[k]);
        out[0] = out[1] = -1;
        gotSize = 0;
        return false;
    }
    gotSize = got[0];
    return true;
}

void NearCaches::give(uint32_t size, const int32_t e[2]) {
    const int i = sizeIndex(size);
    if (i < 0) return;
    for (int k = 0; k < 2; ++k)
        if (e[k] >= 0) free_[i].push_back(e[k]);
}

void NearCaches::release(T& x) {
    if (x.entry[0] < 0) return;
    give(x.nearSize, x.entry);
    x.entry[0] = x.entry[1] = -1;
    x.nearSize = 0;
    released_++;
}

void NearCaches::onBake(uint32_t tile, bool forView, bool needSecond) {
    if (tile >= t_.size()) return;
    T& x = t_[tile];
    x.second = needSecond;
    if ((x.state == kNearHeld || x.state == kNearRebake) && x.entry[0] >= 0) return;
    const uint8_t s = (forView || x.state == kNearView) ? kNearView : kNearAhead;
    x.state = x.req = s;
}

void NearCaches::onBcOnlyBake(uint32_t tile, bool forView) {
    if (tile >= t_.size()) return;
    T& x = t_[tile];
    release(x);
    x.state = x.req = forView ? kNearView : kNearAhead;
}

void NearCaches::onContentChanged(uint32_t tile) {
    if (tile < t_.size()) t_[tile].gen++;
}

void NearCaches::onEvict(uint32_t tile) {
    if (tile >= t_.size()) return;
    T& x = t_[tile];
    release(x);
    x.state = x.req = kNearNone;
}

NearFrameResult NearCaches::frame(const NearVisibleTile* vis, size_t n, bool settled) {
    NearFrameResult r;
    frames_++;
    lastUpres_.clear();
    if (t_.empty() || (!vis && n)) return r;
    // The visible list, each tile once, with its sort keys.
    struct E { uint32_t tile; float dist; uint32_t key; size_t order; };
    std::vector<E> list;
    list.reserve(n);
    std::vector<uint8_t> seen;   // sparse use: only the listed tiles are marked and cleared
    seen.assign(t_.size(), 0);
    for (size_t i = 0; i < n; ++i) {
        const NearVisibleTile& v = vis[i];
        if (v.tile >= t_.size() || seen[v.tile]) continue;
        seen[v.tile] = 1;
        list.push_back(E{v.tile, v.nearDistance, v.key1BC, list.size()});
        // 0x7BA820's switch on the current state (table 0x7BAF50), after tile+0x180 = 0x4B2B40
        T& x = t_[v.tile];
        x.wanted = sizeClassForProjectedSize(v.projectedSize, o_.minClass);
        switch (x.state) {
        case kNearAhead: x.req = kNearView; break;
        case kNearPending: x.req = kNearHeld; break;
        case kNearHeld: {
            bool ask = false;
            if (x.bakedGen < x.gen) ask = true;   // content generation ahead: layer list rebuilt, request 2
            if (x.nearSize != x.wanted && freeCount(x.wanted) > 1) ask = true;
            if (ask) {
                x.req = kNearRebake;
                r.requests2++;
            }
            break;
        }
        default: break;   // 0: the host's BC1 slot and bake; 2 and 3: nothing
        }
    }
    // 0x5E7010: requested ascending, current ascending, +0x1BC descending, +0x1A4 descending (ties: list order [I]).
    std::stable_sort(list.begin(), list.end(), [&](const E& a, const E& b) {
        const T& x = t_[a.tile];
        const T& y = t_[b.tile];
        if (x.req != y.req) return x.req < y.req;
        if (x.state != y.state) return x.state < y.state;
        if (a.key != b.key) return a.key > b.key;
        if (a.dist != b.dist) return a.dist > b.dist;
        return a.order < b.order;
    });
    // The up-res pass.
    const uint32_t cap = settled ? o_.upresSettled : o_.perFrameMoving;
    const uint32_t budget = settled ? o_.budgetSettled : o_.perFrameMoving;
    std::vector<uint32_t> cand;
    for (const E& e : list) {
        const T& x = t_[e.tile];
        if (x.state == kNearHeld && x.req == kNearHeld) continue;
        if (x.state == kNearPending && x.req == kNearHeld) continue;
        if (x.state == kNearNone) continue;
        if (cand.size() < cap) {
            cand.push_back(e.tile);
        } else {   // ticket 0712: the tiles the pass would take next (a hint only)
            if (r.next.size() >= NearFrameResult::kNearNext) break;
            r.next.push_back(e.tile);
        }
    }
    r.candidates = static_cast<uint32_t>(cand.size());
    for (uint32_t tile : cand) {
        if (r.attempts >= budget) break;
        r.attempts++;
        T& x = t_[tile];
        int32_t e[2];
        const uint32_t want = x.wanted;
        uint32_t got = 0;
        if (!take(want, x.second ? 2 : 1, e, got)) {
            // Ticket 0667 (our rule, not the EXE's): a held pair whose content changed (a live terrain change, a road)
            // must not go on drawing what it held when no new pair can be had (array mode with the wanted pool empty:
            // the usual case at a close camera, where the 512 pool is full). The EXE keeps such a pair as it is
            // (terrain-bake-passes.md 3.5.1); here it is baked again into its own textures at its own size, or, when
            // they cannot hold what the tile needs now (a second texture), given back, so that the tile draws its BC1
            // pair, which the residency bakes again from the new content.
            const bool held = (x.state == kNearHeld || x.state == kNearRebake) && x.entry[0] >= 0;
            if (held && x.bakedGen < x.gen) {
                if (!x.second || x.entry[1] >= 0) {
                    x.prevState = x.state;
                    x.prevReq = x.req;
                    x.state = x.req = kNearHeld;
                    x.bakedGen = x.gen;
                    upres_++;
                    NearUpres u;
                    u.tile = tile;
                    u.size = x.nearSize;
                    u.entry[0] = x.entry[0];
                    u.entry[1] = x.entry[1];
                    r.upres.push_back(u);
                    r.inPlace++;
                    lastUpres_.push_back(tile);
                    continue;
                }
                release(x);
                x.state = x.req = kNearView;
                r.dropped++;
            }
            r.failed++;
            upresFailed_++;
            continue;
        }
        x.prevState = x.state;
        x.prevReq = x.req;
        release(x);
        x.entry[0] = e[0];
        x.entry[1] = e[1];
        x.nearSize = got;
        x.state = x.req = kNearHeld;
        x.bakedGen = x.gen;
        upres_++;
        NearUpres u;
        u.tile = tile;
        u.size = got;
        u.entry[0] = e[0];
        u.entry[1] = e[1];
        r.upres.push_back(u);
        lastUpres_.push_back(tile);
    }
    r.budgetUsed = r.attempts;
    // The dirty list's wrapper for the visible tiles requested 3 from state 1: no bake, state 3.
    for (const E& e : list) {
        T& x = t_[e.tile];
        if (x.state == kNearAhead && x.req == kNearView) x.state = kNearView;
    }
    return r;
}

void NearCaches::upresFailed(uint32_t tile) {
    if (tile >= t_.size()) return;
    T& x = t_[tile];
    if (std::find(lastUpres_.begin(), lastUpres_.end(), tile) == lastUpres_.end()) return;
    release(x);
    // the old pair went back when the new one was taken: the tile draws its BC1 pair now
    const uint8_t s = (x.prevState == kNearAhead) ? kNearAhead : kNearView;
    x.state = x.req = s;
    upresFailed_++;
    upres_--;
}

uint32_t NearCaches::tileCount() const { return static_cast<uint32_t>(t_.size()); }
NearState NearCaches::state(uint32_t tile) const { return tile < t_.size() ? static_cast<NearState>(t_[tile].state) : kNearNone; }
NearState NearCaches::requested(uint32_t tile) const { return tile < t_.size() ? static_cast<NearState>(t_[tile].req) : kNearNone; }
uint32_t NearCaches::nearSize(uint32_t tile) const { return tile < t_.size() ? t_[tile].nearSize : 0; }
uint32_t NearCaches::wanted(uint32_t tile) const { return tile < t_.size() ? t_[tile].wanted : 0; }
int32_t NearCaches::entry(uint32_t tile, int k) const {
    return tile < t_.size() && (k == 0 || k == 1) ? t_[tile].entry[k] : -1;
}
bool NearCaches::drawsNear(uint32_t tile) const {
    if (tile >= t_.size()) return false;
    const T& x = t_[tile];
    return (x.state == kNearHeld || x.state == kNearRebake) && x.entry[0] >= 0;
}
uint32_t NearCaches::freeCount(uint32_t size) const {
    const int i = sizeIndex(size);
    return i < 0 ? 0 : static_cast<uint32_t>(free_[i].size());
}
NearCacheCounts NearCaches::counts() const {
    NearCacheCounts c;
    for (const T& x : t_) {
        if (x.state < 6) c.state[x.state]++;
        const int i = sizeIndex(x.nearSize);
        if (x.entry[0] >= 0 && i >= 0) c.held[i]++;
    }
    for (int k = 0; k < 4; ++k) c.freeTex[k] = static_cast<uint32_t>(free_[k].size());
    c.upres = upres_;
    c.upresFailed = upresFailed_;
    c.released = released_;
    c.frames = frames_;
    return c;
}
const NearCacheOptions& NearCaches::options() const { return o_; }

}  // namespace bake
}  // namespace c5r
