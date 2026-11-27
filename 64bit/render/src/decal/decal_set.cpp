// decal_set.cpp - the per-tile decal query, its index and the tiles of changed rectangles (epic 0166, issue 4103).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Contract: c5r/decal/decal.h ("4103" part); the choice of
// the query square and the index's cost: research/05-graphics-composition/terrain-decals.md section 10.2.
// Replaces decal_set_stub.cpp (lead-0166's stub).
#include "c5r/decal/decal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace c5r { namespace decal {

namespace {

constexpr double kMaxCoord = 1048576.0;      // 2^20 samples: beyond it a set gets no grid
constexpr int64_t kMaxGridCells = 1 << 22;   // 4 M cells (the huge map has 128 x 80 = 10,240 tiles)
constexpr int kMaxTilesPerAxis = 16384;      // tilesForRects: a world rectangle of more tiles per axis yields nothing

// One axis of the tile test: the rectangle [mn, mx] overlaps [origin + Lo, origin + Hi], inclusive. NaN: false.
inline bool axisOverlaps(float mn, float mx, int64_t origin) {
    return (double)mx >= (double)origin + kTileQueryLo && (double)mn <= (double)origin + kTileQueryHi;
}

inline bool overlapsTile(const float r[4], int64_t ox, int64_t oy) {
    return axisOverlaps(r[0], r[2], ox) && axisOverlaps(r[1], r[3], oy);
}

// The cells c (tile origin 64 c) whose query square overlaps [mn, mx] on one axis, clamped to [clampLo, clampHi]. The
// closed form gives the range; the exact predicate then corrects it by a cell at either end (float rounding), so the
// grid holds exactly the decals the scan would find. False when the range is empty or a value is not finite.
bool cellRange(float mn, float mx, int64_t clampLo, int64_t clampHi, int64_t& lo, int64_t& hi) {
    if (!std::isfinite(mn) || !std::isfinite(mx) || mn > mx)
        return false;
    double dlo = std::ceil(((double)mn - kTileQueryHi) / kTileSamples);
    double dhi = std::floor(((double)mx - kTileQueryLo) / kTileSamples);
    dlo = (std::max)(dlo, (double)clampLo - 1);
    dhi = (std::min)(dhi, (double)clampHi + 1);
    if (dlo > dhi + 2)
        return false;
    lo = (int64_t)dlo;
    hi = (int64_t)dhi;
    while (lo - 1 >= clampLo && axisOverlaps(mn, mx, (lo - 1) * kTileSamples)) --lo;
    while (lo <= hi && !axisOverlaps(mn, mx, lo * kTileSamples)) ++lo;
    while (hi + 1 <= clampHi && axisOverlaps(mn, mx, (hi + 1) * kTileSamples)) ++hi;
    while (hi >= lo && !axisOverlaps(mn, mx, hi * kTileSamples)) --hi;
    lo = (std::max)(lo, clampLo);
    hi = (std::min)(hi, clampHi);
    return lo <= hi;
}

// Draw order: ascending priority (a NaN sorts last, so the comparator stays a strict weak order); equal priorities: the
// small decals first, newest (highest listOrder) first, then the large decals in listOrder (issue 4111: the order 4106
// measured best against the EXE's caches, terrain-decals.md 13.4; the EXE collects a tile's small decals from its
// spatial index and appends the large list, 6.1 point 3); then the index in the set. A decal is large when a side is
// longer than kLargeDecal (a NaN side counts as small: such a decal is in no tile anyway).
inline float priorityKey(float p) { return std::isnan(p) ? std::numeric_limits<float>::infinity() : p; }

inline bool isLarge(const Decal& x) { return x.rect[2] - x.rect[0] > kLargeDecal || x.rect[3] - x.rect[1] > kLargeDecal; }

struct DrawLess {
    const std::vector<Decal>* d;
    bool operator()(uint32_t a, uint32_t b) const {
        const Decal &x = (*d)[a], &y = (*d)[b];
        const float pa = priorityKey(x.priority), pb = priorityKey(y.priority);
        if (pa != pb) return pa < pb;
        const bool la = isLarge(x), lb = isLarge(y);
        if (la != lb) return lb;   // small before large
        if (x.listOrder != y.listOrder) return la ? x.listOrder < y.listOrder : x.listOrder > y.listOrder;
        return a < b;
    }
};

// ---- 0366: the EXE's own selection and sort (terrain-decals.md 18; behaviour from the knowledge base, D83) ----------
// Single precision as the EXE computes (SSE): every intermediate is rounded to float.
inline float rf(float v) { volatile float r = v; return r; }

// cvttss2si: truncation; out of range or NaN gives 0x80000000
inline int32_t cvtt(float v) {
    if (!(v > -2147483648.0f && v < 2147483648.0f)) return (std::numeric_limits<int32_t>::min)();
    return (int32_t)v;
}

// trunc((q - origin) x inverse), single precision (0x724CA0 / 0x53F0C0)
inline int32_t axisCell(float q, float origin, float inverse) { return cvtt(rf(rf(q - origin) * inverse)); }

using E = ExeDrawEntry;
inline bool lt(const E& a, const E& b) { return a.priority < b.priority; }
inline void swapE(E* a, E* b) { const E t = *a; *a = *b; *b = t; }

// 32 or fewer: each next entry to the front when less than the first, else left past every entry it is less than
void insertionSort(E* first, E* last) {
    if (first == last) return;
    for (E* next = first + 1; next != last; ++next) {
        const E val = *next;
        if (lt(val, *first)) {
            for (E* p = next; p != first; --p) *p = *(p - 1);
            *first = val;
        } else {
            E* hole = next;
            while (lt(val, *(hole - 1))) { *hole = *(hole - 1); --hole; }
            *hole = val;
        }
    }
}

void med3(E* a, E* b, E* c) {
    if (lt(*b, *a)) swapE(b, a);
    if (lt(*c, *b)) swapE(c, b);
    if (lt(*b, *a)) swapE(b, a);
}

// last = the final entry; more than 41 entries: the median of three medians of three
void median(E* first, E* mid, E* last) {
    const ptrdiff_t n1 = last - first;
    if (n1 > 40) {
        const ptrdiff_t step = (n1 + 1) / 8, two = 2 * step;
        med3(first, first + step, first + two);
        med3(mid - step, mid, mid + step);
        med3(last - two, last - step, last);
        med3(first + step, mid, last - step);
    } else {
        med3(first, mid, last);
    }
}

// the fat partition: returns the run [pf, pl) of entries equal to the pivot
void partition(E* first, E* last, E*& outPf, E*& outPl) {
    E* mid = first + (last - first) / 2;
    median(first, mid, last - 1);
    E* pf = mid;
    E* pl = pf + 1;
    while (first < pf && !lt(*(pf - 1), *pf) && !lt(*pf, *(pf - 1))) --pf;
    while (pl < last && !lt(*pl, *pf) && !lt(*pf, *pl)) ++pl;
    E* gf = pl;
    E* gl = pf;
    for (;;) {
        for (; gf < last; ++gf) {
            if (lt(*pf, *gf)) continue;
            if (lt(*gf, *pf)) break;
            swapE(pl++, gf);
        }
        for (; first < gl; --gl) {
            if (lt(*(gl - 1), *pf)) continue;
            if (lt(*pf, *(gl - 1))) break;
            swapE(--pf, gl - 1);
        }
        if (gl == first && gf == last) { outPf = pf; outPl = pl; return; }
        if (gl == first) {
            if (pl != gf) swapE(pf, pl);
            ++pl;
            swapE(pf++, gf++);
        } else if (gf == last) {
            if (--gl != --pf) swapE(gl, pf);
            swapE(pf, --pl);
        } else {
            swapE(gf++, --gl);
        }
    }
}

// sift-down from `hole` over `bottom` entries (the right child unless it is less than the left), then sift `val` up
void adjustHeap(E* first, ptrdiff_t hole, ptrdiff_t bottom, E val) {
    const ptrdiff_t top = hole;
    ptrdiff_t idx = 2 * hole + 2;
    for (; idx < bottom; idx = 2 * idx + 2) {
        if (lt(first[idx], first[idx - 1])) --idx;
        first[hole] = first[idx];
        hole = idx;
    }
    if (idx == bottom) {
        first[hole] = first[bottom - 1];
        hole = bottom - 1;
    }
    for (ptrdiff_t p = (hole - 1) / 2; top < hole && lt(first[p], val); p = (hole - 1) / 2) {
        first[hole] = first[p];
        hole = p;
    }
    first[hole] = val;
}

void heapSort(E* first, E* last) {
    const ptrdiff_t bottom = last - first;
    for (ptrdiff_t hole = bottom / 2; hole > 0;) {
        --hole;
        adjustHeap(first, hole, bottom, first[hole]);
    }
    for (; last - first > 1; --last) {
        const E val = *(last - 1);
        *(last - 1) = *first;
        adjustHeap(first, 0, (last - 1) - first, val);
    }
}

void sortRec(E* first, E* last, ptrdiff_t ideal) {
    for (;;) {
        const ptrdiff_t n = last - first;
        if (n <= 32) {
            if (n > 1) insertionSort(first, last);
            return;
        }
        if (ideal <= 0) {
            heapSort(first, last);
            return;
        }
        E *pf = nullptr, *pl = nullptr;
        partition(first, last, pf, pl);
        ideal = ideal / 2 + (ideal / 2) / 2;
        if (pf - first < last - pl) {
            sortRec(first, pf, ideal);
            first = pl;
        } else {
            sortRec(pl, last, ideal);
            last = pf;
        }
    }
}

// 0366: each cell's decals by rank and the large list by rank from the per-decal fields; any inconsistency (a cell or a
// rank out of range, a rank taken twice, vectors of the wrong size) drops the walk (cols = rows = 0).
void buildWalk(DecalSet& s) {
    DecalWalk& w = s.walk;
    w.cellStart.clear();
    w.cellItems.clear();
    w.large.clear();
    if (!w.cols || !w.rows) return;
    const size_t n = s.decals.size();
    auto drop = [&]() {
        w.cols = w.rows = 0;
        w.cellStart.clear();
        w.cellItems.clear();
        w.large.clear();
    };
    if (w.cell.size() != n || w.rank.size() != n || w.point.size() != 2 * n || (uint64_t)w.cols * w.rows > (1ull << 22)) {
        drop();
        return;
    }
    const uint32_t cells = w.cols * w.rows;
    w.cellStart.assign((size_t)cells + 1, 0);
    uint32_t nLarge = 0;
    for (size_t k = 0; k < n; ++k) {
        if (w.cell[k] == kWalkLarge) nLarge++;
        else if (w.cell[k] >= cells) { drop(); return; }
        else w.cellStart[(size_t)w.cell[k] + 1]++;
    }
    for (size_t c = 1; c < w.cellStart.size(); ++c) w.cellStart[c] += w.cellStart[c - 1];
    const uint32_t kNone = 0xFFFFFFFFu;
    w.cellItems.assign(w.cellStart[cells], kNone);
    w.large.assign(nLarge, kNone);
    for (size_t k = 0; k < n; ++k) {
        const uint32_t r = w.rank[k];
        if (w.cell[k] == kWalkLarge) {
            if (r >= nLarge || w.large[r] != kNone) { drop(); return; }
            w.large[r] = (uint32_t)k;
        } else {
            const uint32_t c = w.cell[k], b = w.cellStart[c], cnt = w.cellStart[(size_t)c + 1] - b;
            if (r >= cnt || w.cellItems[(size_t)b + r] != kNone) { drop(); return; }
            w.cellItems[(size_t)b + r] = (uint32_t)k;
        }
    }
}

}  // namespace

bool hasWalk(const DecalSet& set) {
    const DecalWalk& w = set.walk;
    const size_t n = set.decals.size();
    return w.cols && w.rows && w.cell.size() == n && w.rank.size() == n && w.point.size() == 2 * n &&
           w.cellStart.size() == (size_t)w.cols * w.rows + 1 && w.cellItems.size() == w.cellStart.back();
}

void exeSortByPriority(ExeDrawEntry* first, ExeDrawEntry* last) {
    if (!first || last - first < 2) return;
    sortRec(first, last, last - first);
}

std::vector<ExeDrawEntry> exeSelection(const DecalSet& set, int originX, int originY, uint32_t pass) {
    std::vector<ExeDrawEntry> out;
    if (!hasWalk(set)) return out;
    const DecalWalk& w = set.walk;
    // the tile rectangle of the EXE's bake: origin - 1 .. origin + e, e = 65, 64 in the world's last column (both axes)
    const double lastCol = std::floor((double)set.worldRect[2] / kTileSamples) * kTileSamples;
    const int e = (double)originX >= lastCol ? 64 : 65;
    const float tmin[2] = {(float)(originX - 1), (float)(originY - 1)};
    const float tmax[2] = {(float)(originX + e), (float)(originY + e)};
    const float qmin[2] = {rf(tmin[0] - kLargeDecal), rf(tmin[1] - kLargeDecal)};
    const float qmax[2] = {rf(tmax[0] + kLargeDecal), rf(tmax[1] + kLargeDecal)};
    const int32_t cols = (int32_t)w.cols, rows = (int32_t)w.rows;
    int32_t c0[2], c1[2];
    for (int a = 0; a < 2; ++a) {
        c0[a] = (std::max)(0, axisCell(qmin[a], w.origin[a], w.inverse[a]));
        const int32_t hi = axisCell(qmax[a], w.origin[a], w.inverse[a]);
        int32_t end = hi == (std::numeric_limits<int32_t>::min)() ? hi : hi + 1;
        end = (std::max)(0, end);
        c1[a] = (std::min)(a == 0 ? cols : rows, end);
    }
    auto overlaps = [&](const float* r) {
        return r[0] <= tmax[0] && tmin[0] <= r[2] && r[1] <= tmax[1] && tmin[1] <= r[3];
    };
    // the walk: (c0x, c0y) first (the EXE reads it without a range check), then columns inner, rows outer
    for (int32_t x = c0[0], y = c0[1];;) {
        if (x >= 0 && y >= 0 && x < cols && y < rows) {
            const size_t c = (size_t)y * w.cols + (size_t)x;
            for (uint32_t i = w.cellStart[c]; i < w.cellStart[c + 1]; ++i) {
                const uint32_t k = w.cellItems[i];
                const float px = w.point[2 * (size_t)k], py = w.point[2 * (size_t)k + 1];
                if (!(px >= qmin[0] && px < qmax[0] && py >= qmin[1] && py < qmax[1])) continue;
                const Decal& d = set.decals[k];
                if (d.pass == pass && overlaps(d.rect)) out.push_back(ExeDrawEntry{k, d.priority});
            }
        }
        if (++x >= c1[0]) {
            x = c0[0];
            if (++y >= c1[1]) break;
        }
    }
    for (uint32_t k : w.large) {
        const Decal& d = set.decals[k];
        if (d.pass == pass && overlaps(d.rect)) out.push_back(ExeDrawEntry{k, d.priority});
    }
    return out;
}

TileDecalList decalsForTile(const DecalSet& set, int originX, int originY) {
    TileDecalList list;
    list.set = &set;
    if (hasWalk(set) && originX % kTileSamples == 0 && originY % kTileSamples == 0) {   // 0366: the EXE's order
        std::vector<ExeDrawEntry> p0 = exeSelection(set, originX, originY, 0), p1 = exeSelection(set, originX, originY, 1);
        exeSortByPriority(p0.data(), p0.data() + p0.size());
        exeSortByPriority(p1.data(), p1.data() + p1.size());
        list.indices.reserve(p0.size() + p1.size());
        size_t i = 0, j = 0;
        while (i < p0.size() || j < p1.size()) {
            if (j >= p1.size() || (i < p0.size() && !(p1[j].priority < p0[i].priority))) list.indices.push_back(p0[i++].decal);
            else list.indices.push_back(p1[j++].decal);
        }
        return list;
    }
    const DecalGrid& g = set.grid;
    if (g.cols > 0 && g.rows > 0 && originX % kTileSamples == 0 && originY % kTileSamples == 0 &&
        g.start.size() == (size_t)g.cols * g.rows + 1) {
        const int64_t cx = originX / kTileSamples - (int64_t)g.cellX0, cy = originY / kTileSamples - (int64_t)g.cellY0;
        if (cx < 0 || cy < 0 || cx >= g.cols || cy >= g.rows)
            return list;   // no decal reaches a tile outside the grid (the grid spans every decal's cells)
        const size_t cell = (size_t)cy * g.cols + (size_t)cx;
        const uint32_t b = g.start[cell], e = g.start[cell + 1];
        list.indices.reserve(e - b);
        for (uint32_t k = b; k < e && k < g.items.size(); ++k) {
            const uint32_t i = g.items[k];
            if (i < set.decals.size() && overlapsTile(set.decals[i].rect, originX, originY))
                list.indices.push_back(i);
        }
        return list;
    }
    for (uint32_t i = 0; i < (uint32_t)set.decals.size(); ++i)
        if (overlapsTile(set.decals[i].rect, originX, originY))
            list.indices.push_back(i);
    std::sort(list.indices.begin(), list.indices.end(), DrawLess{&set.decals});
    return list;
}

bool areaChangedAt(const DecalSet& set, int tileX, int tileY, bool& known) {
    known = false;
    if (!set.areaTilesX || !set.areaTilesY || tileX < 0 || tileY < 0 || (uint32_t)tileX >= set.areaTilesX ||
        (uint32_t)tileY >= set.areaTilesY || set.areaChanged.size() != (size_t)set.areaTilesX * set.areaTilesY)
        return false;
    known = true;
    return set.areaChanged[(size_t)tileY * set.areaTilesX + (size_t)tileX] != 0;
}

namespace {
void buildGrid(DecalSet& set);
}

void buildIndex(DecalSet& set) {
    buildWalk(set);   // 0366
    buildGrid(set);
}

// 0365: the index from the previous generation's (decal.h). Exactness: every cell of buildIndex holds, in DrawLess
// order (a strict total order), the decals whose query square overlaps it; a kept decal has prev's rectangle, so it sits
// in prev's cells; an added decal's cells come from cellRange as in buildGrid; the span is prev's whenever no removed
// decal reached its edge and every added decal lies inside it (the span is the bounding box of every decal's cells).
bool buildIndexFrom(DecalSet& set, const DecalSet& prev, const std::vector<uint32_t>& newOf,
                    const std::vector<uint32_t>& added, std::string* why, IndexDeltaStats* stats) {
    buildWalk(set);
    IndexDeltaStats st;
    auto whole = [&](const char* w) {
        if (why) *why = w;
        buildGrid(set);
        if (stats) *stats = IndexDeltaStats();
        return false;
    };
    const DecalGrid& g0 = prev.grid;
    const size_t n = set.decals.size(), m = prev.decals.size();
    if (g0.cols <= 0 || g0.rows <= 0 || g0.start.size() != (size_t)g0.cols * g0.rows + 1 || g0.items.empty() ||
        g0.start.back() != g0.items.size())
        return whole("the previous set has no grid");
    if (newOf.size() != m) return whole("the mapping does not cover the previous set");
    std::vector<uint8_t> hit(n, 0);
    for (size_t j = 0; j < m; ++j) {
        const uint32_t v = newOf[j];
        if (v == kDeltaGone) continue;
        if (v >= n || hit[v]) return whole("the mapping is not one-to-one");
        hit[v] = 1;
        if (std::memcmp(prev.decals[j].rect, set.decals[v].rect, sizeof(float) * 4) != 0)
            return whole("a kept decal's rectangle differs");
    }
    for (uint32_t a : added) {
        if (a >= n || hit[a]) return whole("an added decal is also kept, twice listed or outside the set");
        hit[a] = 1;
    }
    for (size_t k = 0; k < n; ++k)
        if (!hit[k]) return whole("a decal is neither kept nor added");
    const int64_t gx0 = g0.cellX0, gy0 = g0.cellY0, gx1 = gx0 + g0.cols - 1, gy1 = gy0 + g0.rows - 1;
    const int64_t kLim = (int64_t)(kMaxCoord / kTileSamples) + 2;
    for (size_t j = 0; j < m; ++j) {
        if (newOf[j] != kDeltaGone) continue;
        const float* r = prev.decals[j].rect;
        int64_t c[4];
        if (!cellRange(r[0], r[2], -kLim, kLim, c[0], c[2]) || !cellRange(r[1], r[3], -kLim, kLim, c[1], c[3]))
            continue;   // in no cell
        if (c[0] <= gx0 || c[2] >= gx1 || c[1] <= gy0 || c[3] >= gy1)
            return whole("a removed decal's cells reach the grid's edge");
    }
    std::vector<std::pair<uint32_t, uint32_t>> adds;   // (cell, decal)
    const int64_t cols = g0.cols;
    for (uint32_t a : added) {
        const float* r = set.decals[a].rect;
        for (int k = 0; k < 4; ++k)
            if (!std::isfinite(r[k]) || std::fabs(r[k]) > kMaxCoord) return whole("an added decal's rectangle is not finite or too far");
        if (r[0] > r[2] || r[1] > r[3]) return whole("an added decal's rectangle is inverted");
        int64_t c[4];
        if (!cellRange(r[0], r[2], -kLim, kLim, c[0], c[2]) || !cellRange(r[1], r[3], -kLim, kLim, c[1], c[3]))
            continue;
        if (c[0] < gx0 || c[2] > gx1 || c[1] < gy0 || c[3] > gy1) return whole("an added decal's cells reach past the grid");
        for (int64_t y = c[1]; y <= c[3]; ++y)
            for (int64_t x = c[0]; x <= c[2]; ++x)
                adds.push_back(std::make_pair((uint32_t)((y - gy0) * cols + (x - gx0)), a));
    }
    std::sort(adds.begin(), adds.end());
    const size_t cells = (size_t)g0.cols * g0.rows;
    if ((uint64_t)g0.items.size() + adds.size() > 0xFFFFFFF0ull) return whole("too many cell entries");
    DecalGrid g;
    g.cellX0 = g0.cellX0;
    g.cellY0 = g0.cellY0;
    g.cols = g0.cols;
    g.rows = g0.rows;
    g.start.assign(cells + 1, 0);
    g.items.reserve(g0.items.size() + adds.size());
    const DrawLess less{&set.decals};
    size_t ai = 0;
    for (size_t c = 0; c < cells; ++c) {
        const size_t b = g.items.size();
        g.start[c] = (uint32_t)b;
        for (uint32_t k = g0.start[c]; k < g0.start[c + 1]; ++k) {
            const uint32_t j = g0.items[k];
            if (j >= m) return whole("the previous grid names a decal outside its set");
            if (newOf[j] != kDeltaGone) g.items.push_back(newOf[j]);
        }
        bool joined = false;
        for (; ai < adds.size() && adds[ai].first == c; ++ai) {
            g.items.push_back(adds[ai].second);
            joined = true;
            st.entriesAdded++;
        }
        if (joined || !std::is_sorted(g.items.begin() + (ptrdiff_t)b, g.items.end(), less)) {
            std::sort(g.items.begin() + (ptrdiff_t)b, g.items.end(), less);
            st.cellsSorted++;
        }
    }
    g.start[cells] = (uint32_t)g.items.size();
    set.grid = std::move(g);
    if (why) why->clear();
    if (stats) *stats = st;
    return true;
}

namespace {
void buildGrid(DecalSet& set) {
    DecalGrid g;
    const std::vector<Decal>& d = set.decals;
    const uint32_t n = (uint32_t)d.size();
    // per decal: its cell range; the grid spans all of them
    std::vector<int64_t> range((size_t)n * 4);
    int64_t gx0 = 0, gy0 = 0, gx1 = -1, gy1 = -1;
    bool any = false;
    const int64_t kLim = (int64_t)(kMaxCoord / kTileSamples) + 2;
    for (uint32_t i = 0; i < n; ++i) {
        const float* r = d[i].rect;
        for (int k = 0; k < 4; ++k)
            if (!std::isfinite(r[k]) || std::fabs(r[k]) > kMaxCoord) {
                set.grid = DecalGrid();
                return;   // no grid: decalsForTile scans
            }
        if (r[0] > r[2] || r[1] > r[3]) {
            set.grid = DecalGrid();
            return;
        }
        int64_t* c = &range[(size_t)i * 4];
        if (!cellRange(r[0], r[2], -kLim, kLim, c[0], c[2]) || !cellRange(r[1], r[3], -kLim, kLim, c[1], c[3])) {
            c[0] = 1; c[2] = 0;   // in no cell
            continue;
        }
        if (!any) { gx0 = c[0]; gx1 = c[2]; gy0 = c[1]; gy1 = c[3]; any = true; }
        gx0 = (std::min)(gx0, c[0]); gx1 = (std::max)(gx1, c[2]);
        gy0 = (std::min)(gy0, c[1]); gy1 = (std::max)(gy1, c[3]);
    }
    if (!any) {   // no decal in any cell: a 1 x 1 empty grid answers every aligned query with nothing
        g.cols = g.rows = 1;
        g.start.assign(2, 0);
        set.grid = std::move(g);
        return;
    }
    const int64_t cols = gx1 - gx0 + 1, rows = gy1 - gy0 + 1;
    if (cols * rows > kMaxGridCells) {
        set.grid = DecalGrid();
        return;
    }
    std::vector<uint32_t> order(n);
    for (uint32_t i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), DrawLess{&d});
    g.cellX0 = (int)gx0; g.cellY0 = (int)gy0; g.cols = (int)cols; g.rows = (int)rows;
    g.start.assign((size_t)(cols * rows) + 1, 0);
    uint64_t total = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const int64_t* c = &range[(size_t)i * 4];
        if (c[0] > c[2]) continue;
        for (int64_t y = c[1]; y <= c[3]; ++y)
            for (int64_t x = c[0]; x <= c[2]; ++x)
                g.start[(size_t)((y - gy0) * cols + (x - gx0)) + 1]++;
        total += (uint64_t)(c[2] - c[0] + 1) * (uint64_t)(c[3] - c[1] + 1);
    }
    if (total > 0xFFFFFFF0ull) {
        set.grid = DecalGrid();
        return;
    }
    for (size_t k = 1; k < g.start.size(); ++k) g.start[k] += g.start[k - 1];
    g.items.resize((size_t)total);
    std::vector<uint32_t> fill(g.start.begin(), g.start.end() - 1);
    for (uint32_t i : order) {   // in draw order, so every cell's list is in draw order
        const int64_t* c = &range[(size_t)i * 4];
        if (c[0] > c[2]) continue;
        for (int64_t y = c[1]; y <= c[3]; ++y)
            for (int64_t x = c[0]; x <= c[2]; ++x)
                g.items[fill[(size_t)((y - gy0) * cols + (x - gx0))]++] = i;
    }
    set.grid = std::move(g);
}
}  // namespace

std::vector<TileOrigin> tilesForRects(const DecalRect* rects, size_t count, const float worldRect[4]) {
    std::vector<TileOrigin> out;
    if (!rects || !worldRect)
        return out;
    for (int k = 0; k < 4; ++k)
        if (!std::isfinite(worldRect[k]) || std::fabs(worldRect[k]) > kMaxCoord)
            return out;
    if (worldRect[0] > worldRect[2] || worldRect[1] > worldRect[3])
        return out;
    const int64_t tx0 = (int64_t)std::floor(worldRect[0] / kTileSamples), tx1 = (int64_t)std::floor(worldRect[2] / kTileSamples);
    const int64_t ty0 = (int64_t)std::floor(worldRect[1] / kTileSamples), ty1 = (int64_t)std::floor(worldRect[3] / kTileSamples);
    if (tx1 - tx0 + 1 > kMaxTilesPerAxis || ty1 - ty0 + 1 > kMaxTilesPerAxis)
        return out;
    const int64_t w = tx1 - tx0 + 1, h = ty1 - ty0 + 1;
    std::vector<uint8_t> mark((size_t)(w * h), 0);
    for (size_t i = 0; i < count; ++i) {
        const float* r = rects[i].v;
        // an infinite edge is fine (clamped to the world's tiles); a NaN or an inverted rectangle is skipped
        float mn[2] = {r[0], r[1]}, mx[2] = {r[2], r[3]};
        bool bad = false;
        for (int a = 0; a < 2; ++a) {
            if (std::isnan(mn[a]) || std::isnan(mx[a]) || mn[a] > mx[a]) bad = true;
            const float big = (float)(kMaxCoord * 2);
            mn[a] = (std::max)(-big, (std::min)(big, mn[a]));
            mx[a] = (std::max)(-big, (std::min)(big, mx[a]));
        }
        int64_t x0, x1, y0, y1;
        if (bad || !cellRange(mn[0], mx[0], tx0, tx1, x0, x1) || !cellRange(mn[1], mx[1], ty0, ty1, y0, y1))
            continue;
        for (int64_t y = y0; y <= y1; ++y)
            for (int64_t x = x0; x <= x1; ++x)
                mark[(size_t)((y - ty0) * w + (x - tx0))] = 1;
    }
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x)
            if (mark[(size_t)(y * w + x)]) {
                TileOrigin t;
                t.x = (int)((tx0 + x) * kTileSamples);
                t.y = (int)((ty0 + y) * kTileSamples);
                out.push_back(t);
            }
    return out;
}

// 0393: the tiles whose draw list changes with the tie rule (decal.h). Written 2026-11-16.
namespace {
// the grid holds the tile's decals (the 4111 square, a superset of the walk's selection); true when it cannot say
bool mayHaveDecals(const DecalSet& s, int64_t tx, int64_t ty) {
    const DecalGrid& g = s.grid;
    if (g.cols <= 0 || g.rows <= 0 || g.start.size() != (size_t)g.cols * g.rows + 1) return true;
    const int64_t cx = tx - g.cellX0, cy = ty - g.cellY0;
    if (cx < 0 || cy < 0 || cx >= g.cols || cy >= g.rows) return false;   // the grid spans every decal's cells
    const size_t c = (size_t)cy * g.cols + (size_t)cx;
    return g.start[c + 1] > g.start[c];
}

bool sameDecal(const Decal& a, const Decal& b) {
    return a.hash == b.hash && std::memcmp(a.rect, b.rect, sizeof(a.rect)) == 0 &&
           std::memcmp(&a.priority, &b.priority, sizeof(float)) == 0 && a.pass == b.pass && a.vertexCount == b.vertexCount;
}
}  // namespace

std::vector<TileOrigin> tieRuleTiles(const DecalSet& prev, const DecalSet& next, TieRuleStats* stats) {
    std::vector<TileOrigin> out;
    TieRuleStats st;
    if (hasWalk(prev) == hasWalk(next)) {
        if (stats) *stats = st;
        return out;
    }
    const float* wr = next.worldRect;
    for (int k = 0; k < 4; ++k)
        if (!std::isfinite(wr[k]) || std::fabs(wr[k]) > kMaxCoord) {
            if (stats) *stats = st;
            return out;
        }
    if (wr[0] > wr[2] || wr[1] > wr[3]) {
        if (stats) *stats = st;
        return out;
    }
    const int64_t tx0 = (int64_t)std::floor(wr[0] / kTileSamples), tx1 = (int64_t)std::floor(wr[2] / kTileSamples);
    const int64_t ty0 = (int64_t)std::floor(wr[1] / kTileSamples), ty1 = (int64_t)std::floor(wr[3] / kTileSamples);
    if (tx1 - tx0 + 1 > kMaxTilesPerAxis || ty1 - ty0 + 1 > kMaxTilesPerAxis) {
        if (stats) *stats = st;
        return out;
    }
    for (int64_t ty = ty0; ty <= ty1; ++ty)
        for (int64_t tx = tx0; tx <= tx1; ++tx) {
            if (!mayHaveDecals(prev, tx, ty) && !mayHaveDecals(next, tx, ty)) continue;
            st.candidates++;
            const int ox = (int)(tx * kTileSamples), oy = (int)(ty * kTileSamples);
            const TileDecalList a = decalsForTile(prev, ox, oy), b = decalsForTile(next, ox, oy);
            st.decalsCompared += (uint32_t)(a.indices.size() + b.indices.size());
            // each pass on its own: the decal pass draws pass 0 into the blend's targets and pass 1 into the macro-shadow
            // target, each in the list's order, so the interleaving of the two passes in the list draws nothing different
            // (a decal of another pass is drawn by neither and not compared)
            bool differ = false;
            for (uint32_t pass = 0; !differ && pass < 2; ++pass) {
                size_t i = 0, j = 0;
                for (;;) {
                    while (i < a.indices.size() && prev.decals[a.indices[i]].pass != pass) ++i;
                    while (j < b.indices.size() && next.decals[b.indices[j]].pass != pass) ++j;
                    if (i == a.indices.size() || j == b.indices.size()) {
                        differ = i != a.indices.size() || j != b.indices.size();
                        break;
                    }
                    if (!sameDecal(prev.decals[a.indices[i]], next.decals[b.indices[j]])) {
                        differ = true;
                        break;
                    }
                    ++i;
                    ++j;
                }
            }
            if (!differ) continue;
            st.differ++;
            TileOrigin t;
            t.x = ox;
            t.y = oy;
            out.push_back(t);
        }
    if (stats) *stats = st;
    return out;
}

}} // namespace c5r::decal
