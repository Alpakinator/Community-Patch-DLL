// render/tests/terrain/region_core.h - epic 0465, ticket 4909 (research/05-graphics-composition/live-terrain.md 8.14):
// today's whole-field path against the region path, for the terrain renderer, the water module and the residency.
// Written 2026-11-19 by w8-4909 for this repository (GPL-3.0-or-later).
//
// For one field before and after a live window (rects: the samples the window wrote), each module is set to `before`,
// then brought to `after` twice: once by its whole-field call (setHeightField / onField, today's live path) and once by
// its region call (updateHeightRegion / onFieldRegion), each on its own instance; the digests of the two must be equal
// (pages and material textures read back from the GPU, tile state, residency tiles, bake order). Times are the median of
// reps runs of each call (the module set back to `before` between runs, untimed).
#pragma once
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include "c5r/bake/heightpage.h"
#include "c5r/bake/residency.h"
#include "c5r/bake/standin.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/terrain/terrain.h"
#include "c5r/water/water.h"

namespace c5r { namespace regiontest {

struct PartResult {
    bool equal = false;
    std::vector<uint64_t> whole, region;
    std::vector<double> wholeMs, regionMs;
    std::string error;
    double median(const std::vector<double>& v) const {
        if (v.empty()) return -1;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[s.size() / 2];
    }
};

struct Report {
    PartResult terrain, terrainBuilt, water, residency;
    uint32_t pages = 0;
    std::vector<double> pageBuildMs;   // bake::buildHeightPage of the pages the rects touch, all of them, per rep
    terrain::RegionUpdateStats terrainStats, terrainBuiltStats, waterStats;
    bake::RegionFieldStats resStats;
};

inline double msSince(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

inline bool digestsEqual(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) { return !a.empty() && a == b; }

// reps >= 1. exe: CivilizationV_DX11.exe (the TerrainShader programs), gameDir: may be empty (the water module's river
// art then stays its own).
inline Report runRegionCompare(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::wstring& exe, const std::wstring& gameDir,
                               const heightfield::HeightGrid& before, const heightfield::HeightGrid& after,
                               const std::vector<terrain::SampleRect>& rects, uint32_t tilePlots, int reps, bool doWater = true,
                               bool doResidency = true) {
    Report rep;
    typedef std::chrono::steady_clock C;
    // the pages a worker would build
    std::vector<bake::HeightPageIndex> idx;
    Status ps = terrain::pagesForRects(after, rects, idx);
    rep.pages = static_cast<uint32_t>(idx.size());
    std::vector<bake::HeightPage> built(idx.size());
    for (int k = 0; k < reps && ps.ok(); ++k) {
        const C::time_point t0 = C::now();
        for (size_t i = 0; i < idx.size() && ps.ok(); ++i) ps = bake::buildHeightPage(after, idx[i], bake::kHeightPageMips, built[i]);
        rep.pageBuildMs.push_back(msSince(t0));
    }
    // terrain: whole, region, region with built pages
    {
        terrain::Renderer a, b, c;
        Status st = a.init(dev, exe, L"");
        if (st.ok()) st = b.init(dev, exe, L"");
        if (st.ok()) st = c.init(dev, exe, L"");
        for (int k = 0; k < reps && st.ok(); ++k) {
            st = a.setHeightField(before, tilePlots);
            const C::time_point t0 = C::now();
            if (st.ok()) st = a.setHeightField(after, tilePlots);
            rep.terrain.wholeMs.push_back(msSince(t0));
            if (st.ok()) st = b.setHeightField(before, tilePlots);
            const C::time_point t1 = C::now();
            if (st.ok()) st = b.updateHeightRegion(after, rects, nullptr, &rep.terrainStats);
            rep.terrain.regionMs.push_back(msSince(t1));
            if (st.ok()) st = c.setHeightField(before, tilePlots);
            const C::time_point t2 = C::now();
            if (st.ok() && ps.ok()) st = c.updateHeightRegion(after, rects, &built, &rep.terrainBuiltStats);
            rep.terrainBuilt.regionMs.push_back(msSince(t2));
        }
        if (st.ok()) st = a.fieldDigest(rep.terrain.whole);
        if (st.ok()) st = b.fieldDigest(rep.terrain.region);
        if (st.ok()) st = c.fieldDigest(rep.terrainBuilt.region);
        rep.terrainBuilt.whole = rep.terrain.whole;
        rep.terrainBuilt.wholeMs = rep.terrain.wholeMs;
        if (!st.ok()) rep.terrain.error = rep.terrainBuilt.error = st.str();
        rep.terrain.equal = st.ok() && digestsEqual(rep.terrain.whole, rep.terrain.region);
        rep.terrainBuilt.equal = st.ok() && ps.ok() && digestsEqual(rep.terrainBuilt.whole, rep.terrainBuilt.region);
    }
    if (doWater) {
        water::Renderer a, b;
        Status st = a.init(dev, exe, gameDir);
        if (st.ok()) st = b.init(dev, exe, gameDir);
        for (int k = 0; k < reps && st.ok(); ++k) {
            st = a.setHeightField(before);
            const C::time_point t0 = C::now();
            if (st.ok()) st = a.setHeightField(after);
            rep.water.wholeMs.push_back(msSince(t0));
            if (st.ok()) st = b.setHeightField(before);
            const C::time_point t1 = C::now();
            if (st.ok()) st = b.updateHeightRegion(after, rects, nullptr, &rep.waterStats);
            rep.water.regionMs.push_back(msSince(t1));
        }
        if (st.ok()) st = a.fieldDigest(rep.water.whole);
        if (st.ok()) st = b.fieldDigest(rep.water.region);
        if (!st.ok()) rep.water.error = st.str();
        rep.water.equal = st.ok() && digestsEqual(rep.water.whole, rep.water.region);
    }
    if (doResidency) {
        // the residency against the stand-in baker; one change per instance (generations count): a fresh pair per rep
        Status st = Status::Ok();
        for (int k = 0; k < reps && st.ok(); ++k) {
            bake::StandinBaker ba{bake::StandinFaults()}, bb{bake::StandinFaults()};
            bake::Residency ra, rb;
            bake::ResidencyOptions o;
            st = ba.init(dev, ctx);
            if (st.ok()) st = bb.init(dev, ctx);
            if (st.ok()) st = ra.init(&ba, o);
            if (st.ok()) st = rb.init(&bb, o);
            if (st.ok()) st = ra.onField(dev, before, 1, 1);
            if (st.ok()) st = rb.onField(dev, before, 1, 1);
            const C::time_point t0 = C::now();
            if (st.ok()) st = ra.onField(dev, after, 1, 1);
            rep.residency.wholeMs.push_back(msSince(t0));
            const C::time_point t1 = C::now();
            if (st.ok()) st = rb.onFieldRegion(dev, after, rects, nullptr, &rep.resStats);
            rep.residency.regionMs.push_back(msSince(t1));
            if (k == reps - 1) {
                if (st.ok()) st = ra.fieldDigest(rep.residency.whole);
                if (st.ok()) st = rb.fieldDigest(rep.residency.region);
            }
        }
        if (!st.ok()) rep.residency.error = st.str();
        rep.residency.equal = st.ok() && digestsEqual(rep.residency.whole, rep.residency.region);
    }
    return rep;
}

// The rects where two fields of one layout differ (heights or materials), as rows of runs merged into one bounding
// rect per connected band: here simply the bounding box of every differing sample (empty when equal).
inline std::vector<terrain::SampleRect> diffBox(const heightfield::HeightGrid& a, const heightfield::HeightGrid& b) {
    std::vector<terrain::SampleRect> out;
    uint32_t x0 = UINT32_MAX, y0 = UINT32_MAX, x1 = 0, y1 = 0;
    for (uint32_t y = 0; y < a.samplesY; ++y)
        for (uint32_t x = 0; x < a.samplesX; ++x) {
            const size_t i = a.index(x, y);
            if (a.h[i] != b.h[i] || (!a.mat.empty() && a.mat[i] != b.mat[i])) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1);
                y1 = std::max(y1, y + 1);
            }
        }
    if (x1 > x0) {
        terrain::SampleRect r;
        r.x0 = x0;
        r.y0 = y0;
        r.x1 = x1;
        r.y1 = y1;
        out.push_back(r);
    }
    return out;
}

}} // namespace c5r::regiontest
