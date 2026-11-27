// render/tests/units/test_art_cache.cpp - issue 4464 (epic 0375): the art cache per art row on VP's database.
//
// 1. catalogue_rows_cached_equal: every art row of 3907's catalogue (models/evidence/0041/4354/catalogue_figures.tsv,
//    C5R_UNITS_CATALOGUE) resolved by the cached ArtResolver (twice) and by the rule (resolve_uncached): every field
//    equal, failures included; the members' .fxsxml parsed once per row over both passes.
// 2. art_cache_cost_indication: UnitRenderer::prepare of a 35-unit scene of those rows (the huge save's window drew 35,
//    unit-server-scene.md 3.4) on the CPU, the module's own pose and a fixed placement, the cache off (the rule as before
//    4464: every unit's .fxsxml read and parsed every frame) and on, interleaved; then on with a fixed pose (the pose's
//    share). Printed as ms per frame: a WORKSTATION INDICATION (COMMON 23), never a result; no timing is asserted. The
//    parse counts are asserted.
// Inputs (read only): the install (C5R_GAME_DIR; VP as DLC there, else its mod roots as extra loose roots, units_mods.h:
// C5R_UNITS_MODS, folders joined by ';', else a pool instance's MODS folders), the EXE (C5R_EXE: the unit programs), the catalogue's database copy (C5R_UNITS_DB, as
// test_placement_catalogue.cpp). A missing input skips, and build.py counts a skip as a failure.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/units.h"
#include "units_mods.h"
#include "units_testkit.h"

using namespace c5r;
using namespace c5r::units;

namespace {

std::string env(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : def;
}

std::string sourceDir() {
    std::string f = __FILE__;
    size_t s = f.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : f.substr(0, s);
}

struct Inputs {
    gamefs::GameFiles files;
    c5m::SqliteTables db;
    std::vector<std::string> rows;   // the catalogue's distinct art rows, in file order
    std::wstring exe;
    Inputs() {
        const std::string game = ::c5r::test::gameDir();
        exe = gamefs::fromUtf8(::c5r::test::exePath());
        const std::string dbPath = env("C5R_UNITS_DB", "F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db");
        const std::string catPath =
            env("C5R_UNITS_CATALOGUE", sourceDir() + "/../../../models/evidence/0041/4354/catalogue_figures.tsv");
        std::ifstream cat(gamefs::fromUtf8(catPath).c_str(), std::ios::binary);
        if (!cat) ::c5r::test::skip("catalogue text not found: " + catPath);
        std::set<std::string> seen;
        std::string line;
        while (std::getline(cat, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, 5, "unit\t") != 0) continue;
            std::vector<std::string> f;
            std::stringstream ss(line);
            std::string x;
            while (std::getline(ss, x, '\t')) f.push_back(x);
            if (f.size() > 3 && seen.insert(f[3]).second) rows.push_back(f[3]);
        }
        if (rows.empty()) ::c5r::test::skip("no unit rows in " + catPath);
        if (!c5m::SqliteTables::available()) ::c5r::test::skip("winsqlite3.dll not available");
        c5m::Error e;
        if (!db.open(dbPath, &e)) ::c5r::test::skip("database not opened: " + dbPath + ": " + e.what);
        gamefs::GameFiles::Options o;
        o.extraLooseRoots = units_test::installModRoots(game);   // C5R_UNITS_MODS, else a pool instance's MODS (0443)
        CHECK_OK(files.open(gamefs::fromUtf8(game), o));
    }
};

// Every figure alive, 12 units apart from the plot (as test_draw.cpp's): the cost of the module's placement is not
// what the cache changes, and it needs the height field.
class FixedPlacement : public PlacementSource {
public:
    UnitStatus place(const PlacementRequest& r, UnitPlacement& out) override {
        out = UnitPlacement();
        out.figures = out.living = r.art->figures();
        for (uint32_t f = 0; f < out.figures; ++f) {
            FigurePlacement p;
            p.figure = static_cast<uint16_t>(f);
            p.slot = p.figure;
            p.x = r.unit->plot_x * 100.0f + 12.0f * f - 60.0f;
            p.y = r.unit->plot_y * 100.0f;
            out.placed.push_back(p);
        }
        return UnitStatus::Ok();
    }
};

class FixedPose : public PoseSource {
public:
    UnitStatus rows(const PoseRequest& r, Float4 out[kPaletteBones * 3]) override {
        for (uint32_t k = 0; k < kPaletteBones; ++k) {
            out[3 * k + 0] = {1, 0, 0, r.T[0]};
            out[3 * k + 1] = {0, 1, 0, r.T[1]};
            out[3 * k + 2] = {0, 0, 1, r.T[2]};
        }
        return UnitStatus::Ok();
    }
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : (v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]));
}

}  // namespace

C5R_TEST(catalogue_rows_cached_equal) {
    Inputs in;
    GameFilesProvider prov(in.files);
    ArtResolver cached, rule;
    CHECK_UNIT(cached.open(in.db, prov), UnitCode::Ok);
    CHECK_UNIT(rule.open(in.db, prov), UnitCode::Ok);
    size_t ok = 0;
    std::map<std::string, size_t> codes;
    for (int pass = 0; pass < 2; ++pass) {
        for (const std::string& row : in.rows) {
            UnitArt got, exp;
            const UnitStatus gs = cached.resolve(row, got);
            const UnitStatus es = rule.resolve_uncached(row, exp);
            CHECK(gs.code == es.code);
            CHECK_EQ(gs.str(), es.str());
            const std::string d = units_test::artDiff(got, exp);
            if (!d.empty()) ::c5r::test::fail(__FILE__, __LINE__, row + ": " + d);
            if (pass == 0) {
                ok += gs.ok() ? 1 : 0;
                codes[unitCodeName(gs.code)]++;
            }
        }
        if (pass == 0) CHECK_EQ(cached.fxsxml_parses(), rule.fxsxml_parses());   // first pass: one resolution each
    }
    CHECK_EQ(rule.fxsxml_parses(), 2 * cached.fxsxml_parses());   // the cache parsed nothing in the second pass
    CHECK_EQ(cached.cached_rows(), in.rows.size());
    CHECK(ok > 0);
    std::printf("catalogue: %zu art rows, %zu Ok, %llu .fxsxml parses cached over 2 passes (rule %llu); statuses:",
                in.rows.size(), ok, static_cast<unsigned long long>(cached.fxsxml_parses()),
                static_cast<unsigned long long>(rule.fxsxml_parses()));
    for (auto& c : codes) std::printf(" %s %zu", c.first.c_str(), c.second);
    std::printf("\n");
}

C5R_TEST(art_cache_cost_indication) {
    Inputs in;
    UnitRenderer on, off;
    CHECK_OK(on.open(nullptr, in.files, in.db, in.exe));
    CHECK_OK(off.open(nullptr, in.files, in.db, in.exe));
    off.setArtCache(false);
    FixedPlacement place;
    on.setPlacementSource(&place);
    off.setPlacementSource(&place);
    // the player: the first PlayerColors Type with a colour
    std::vector<c5m::DbRow> pc;
    c5m::Error e;
    CHECK(in.db.rows("PlayerColors", {"Type"}, pc, &e));
    UnitScene sc;
    PlayerInput p;
    p.player = 0;
    float rgba[4];
    for (auto& r : pc)
        if (!r.empty() && !r[0].null && on.art().team_colour(r[0].text, rgba).ok()) {
            p.colour = r[0].text;
            break;
        }
    CHECK(!p.colour.empty());
    sc.players.push_back(p);
    // 35 units of rows that draw (Ok through prepare with the module's pose), as the huge window's 35
    {
        UnitScene probe;
        probe.players = sc.players;
        for (size_t i = 0; i < in.rows.size(); ++i) {
            UnitInput u;
            u.player = 0;
            u.id = static_cast<int32_t>(i);
            u.unit_type = "UNIT_CATALOGUE";
            u.art_row = in.rows[i];
            u.plot_x = static_cast<int32_t>(i % 40);
            u.plot_y = static_cast<int32_t>(i / 40);
            probe.units.push_back(u);
        }
        FramePlan plan;
        CHECK_OK(on.prepare(probe, nullptr, plan));
        for (size_t i = 0; i < plan.units.size() && sc.units.size() < 35; ++i)
            if (plan.units[i].status.ok()) sc.units.push_back(probe.units[i]);
    }
    CHECK_EQ(sc.units.size(), static_cast<size_t>(35));
    FramePlan warm;
    CHECK_OK(off.prepare(sc, nullptr, warm));   // the model cache of the off arm filled (models are cached either way)
    CHECK_EQ(warm.drawn(), static_cast<size_t>(35));
    const uint64_t onParses = on.art().fxsxml_parses();
    const uint64_t offPerFrame0 = off.art().fxsxml_parses();
    const int kBlocks = 6, kFrames = 10;
    std::vector<double> msOff, msOn, msOnFixedPose;
    FixedPose fixed;
    auto run = [&](UnitRenderer& r, std::vector<double>& out) {
        FramePlan plan;
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < kFrames; ++f) CHECK_OK(r.prepare(sc, nullptr, plan));
        const auto t1 = std::chrono::steady_clock::now();
        out.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / kFrames);
        CHECK_EQ(plan.drawn(), static_cast<size_t>(35));
    };
    for (int b = 0; b < kBlocks; ++b) {   // interleaved: off, on, on with a fixed pose; the order turns each block
        const int k = b % 3;
        for (int j = 0; j < 3; ++j) {
            const int arm = (k + j) % 3;
            if (arm == 0) run(off, msOff);
            if (arm == 1) run(on, msOn);
            if (arm == 2) {
                on.setPoseSource(&fixed);
                run(on, msOnFixedPose);
                on.setPoseSource(nullptr);
            }
        }
    }
    // parses: the cache none after the probe; the rule every member of every unit of every frame
    CHECK_EQ(on.art().fxsxml_parses(), onParses);
    const uint64_t offFrames = static_cast<uint64_t>(kBlocks) * kFrames;
    const uint64_t perFrame = offPerFrame0;   // the warm-up frame's parses
    CHECK(perFrame >= 35);
    CHECK_EQ(off.art().fxsxml_parses(), offPerFrame0 + offFrames * perFrame);
    std::printf("INDICATION (workstation, COMMON 23; not a result): prepare() of 35 catalogue units, CPU only, %d blocks x "
                "%d frames interleaved: cache off median %.3f ms/frame (min %.3f, max %.3f); cache on %.3f (min %.3f, max "
                "%.3f); cache on with a fixed pose %.3f (min %.3f, max %.3f); .fxsxml parses per frame: off %llu, on 0\n",
                kBlocks, kFrames, median(msOff), *std::min_element(msOff.begin(), msOff.end()),
                *std::max_element(msOff.begin(), msOff.end()), median(msOn), *std::min_element(msOn.begin(), msOn.end()),
                *std::max_element(msOn.begin(), msOn.end()), median(msOnFixedPose),
                *std::min_element(msOnFixedPose.begin(), msOnFixedPose.end()),
                *std::max_element(msOnFixedPose.begin(), msOnFixedPose.end()), static_cast<unsigned long long>(perFrame));
    std::printf("per block (off / on / on fixed pose):");
    for (int b = 0; b < kBlocks; ++b) std::printf(" %.3f/%.3f/%.3f", msOff[b], msOn[b], msOnFixedPose[b]);
    std::printf("\n");
}

C5R_TEST_MAIN
