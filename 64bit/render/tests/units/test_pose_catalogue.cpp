// render/tests/units/test_pose_catalogue.cpp - issue 4466 (epic 0375): the module's pose step on VP's real unit art,
// against the per-mesh path it had before 4466.
//
// catalogue_pose_equal: UnitRenderer::prepare of every art row of 3907's catalogue that draws (as test_art_cache.cpp's
// scene: FixedPlacement, the first player colour; every third unit fortified), three frames, with a PoseSource that
// varies each figure's time per frame (figures 0, 5, 10... share a time) and answers from ModulePose::rows. Each request
// is also answered by the pre-4466 path (defaultClip by name, poseFigure for the one mesh, its own ClipCache and
// PoseModel), and by ModulePose::rowsProgram for two programs (one Sample of the member's k-th animation, a Blend of the
// default clip with it, w 0.4) against poseFigureProgram. Every answer equal byte for byte (the 96 Float4) with the same
// code and message, failures included. The module's answers are hashed (FNV-1a 64 over rows, codes and messages, in
// request order) and the hash printed: built against the pre-4466 pose.cpp the same file prints the same hash (the
// cross-build check of issue 4466, server/evidence/0375/4466/). Uses only API that exists before and after 4466.
// Also printed: the time in the module's answers and in the reference's, a WORKSTATION INDICATION (COMMON 23), never a
// result; no timing is asserted.
// Inputs (read only): the install (C5R_GAME_DIR; VP as DLC there, else its mod roots, units_mods.h), the EXE (C5R_EXE), the
// catalogue's database copy (C5R_UNITS_DB), as test_art_cache.cpp. A missing input skips, and build.py counts a skip as
// a failure.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/pose.h"
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
            p.yaw = 0.25f * static_cast<float>(f);
            out.placed.push_back(p);
        }
        return UnitStatus::Ok();
    }
};

void identity96(Float4 out[kPaletteBones * 3]) {
    for (uint32_t i = 0; i < kPaletteBones * 3; ++i) {
        out[i] = Float4();
        if (i % 3 == 0) out[i].x = 1;
        else if (i % 3 == 1) out[i].y = 1;
        else out[i].z = 1;
    }
}

void to96(const std::vector<MeshPalette>& mp, Float4 out[kPaletteBones * 3]) {
    identity96(out);
    if (mp.empty()) return;
    const std::vector<c5m::Row34>& rows = mp[0].rows;
    for (size_t k = 0; k < rows.size() && k < kPaletteBones; ++k)
        for (int r = 0; r < 3; ++r) {
            Float4& o = out[k * 3 + static_cast<size_t>(r)];
            o.x = rows[k][static_cast<size_t>(r) * 4 + 0];
            o.y = rows[k][static_cast<size_t>(r) * 4 + 1];
            o.z = rows[k][static_cast<size_t>(r) * 4 + 2];
            o.w = rows[k][static_cast<size_t>(r) * 4 + 3];
        }
}

// Every request answered by the module and by the pre-4466 per-mesh path.
class ComparePose : public PoseSource {
public:
    ComparePose(c5m::FileProvider& files, const c5m::StringDatabase* strings)
        : mod_(files, strings), ref_(clipLoaderFromProvider(files, strings)) {}
    int frame = 0;
    size_t requests = 0, programs = 0, failures = 0, mismatches = 0;
    uint64_t hash = 1469598103934665603ull;
    std::string first;
    double modMs[3] = {0, 0, 0}, refMs[3] = {0, 0, 0};   // per frame

    UnitStatus rows(const PoseRequest& r0, Float4 out[kPaletteBones * 3]) override {
        PoseRequest r = r0;
        r.t = 0.13f * static_cast<float>(frame) + 0.07f * static_cast<float>(r.figure % 5);
        Float4 a[kPaletteBones * 3], b[kPaletteBones * 3];
        auto t0 = std::chrono::steady_clock::now();
        const UnitStatus got = mod_.rows(r, a);
        auto t1 = std::chrono::steady_clock::now();
        const PoseModel* pm = refModel(r);
        PoseStatus want;
        if (!pm) {
            want.code = PoseErr::BadModel;   // the module refuses it too (compared by code and text below)
            want.msg = got.text;
            identity96(b);
        } else {
            want = refRows(*pm, r, b);
        }
        auto t2 = std::chrono::steady_clock::now();
        modMs[frame % 3] += std::chrono::duration<double, std::milli>(t1 - t0).count();
        refMs[frame % 3] += std::chrono::duration<double, std::milli>(t2 - t1).count();
        compare(got, want, a, b, r, "rows");
        if (pm && !pm->animations.empty()) {
            const size_t k = (r.figure + static_cast<size_t>(frame)) % pm->animations.size();
            unitanim::PoseProgram one;
            unitanim::PoseNode n;
            n.clip = pm->animations[k].file;
            n.t = r.t + 0.05f;
            one.nodes.push_back(n);
            unitanim::PoseProgram blend;
            unitanim::PoseNode d = n, c;
            d.clip = defaultClip(*pm, false);
            d.t = r.t;
            c.kind = unitanim::PoseNode::Blend;
            c.a = 0;
            c.b = 1;
            c.w = 0.4f;
            blend.nodes = {d, n, c};
            const unitanim::PoseProgram* ps[] = {&one, &blend};
            for (const unitanim::PoseProgram* p : ps) {
                Float4 pa[kPaletteBones * 3], pb[kPaletteBones * 3];
                const UnitStatus pg = mod_.rowsProgram(r, *p, pa);
                identity96(pb);
                std::vector<MeshPalette> mp;
                const PoseStatus pw = poseFigureProgram(ref_, *pm, *p, r.scale, r.yaw, r.T,
                                                        std::vector<uint32_t>(1, r.mesh->index), mp);
                if (pw.ok()) to96(mp, pb);
                compare(pg, pw, pa, pb, r, p == &one ? "program sample" : "program blend");
                ++programs;
            }
        }
        std::memcpy(out, a, sizeof a);
        return got;
    }

private:
    ModulePose mod_;
    ClipCache ref_;
    std::map<std::string, std::unique_ptr<PoseModel>> models_;
    std::set<std::string> failed_;

    // the PoseModel the module builds for the request (ModulePose::model before 4466), nullptr when it refuses one
    const PoseModel* refModel(const PoseRequest& r) {
        const std::string key = r.model->gr2 + "|" + c5m::basename_lower(r.member->files.fxsxml.name);
        auto it = models_.find(key);
        if (it != models_.end()) return it->second.get();
        if (failed_.count(key) || !r.model->file) return nullptr;
        std::vector<c5m::FxsAnimation> anims;
        for (const c5m::ResolvedAnimation& ra : r.member->files.animations) {
            c5m::FxsAnimation fa;
            fa.file = ra.file.name;
            fa.ec_text = ra.ec_text;
            fa.ec = ra.ec;
            fa.name = ra.name;
            anims.push_back(fa);
        }
        std::unique_ptr<PoseModel> pm(new PoseModel());
        if (!buildPoseModel(*r.model->file, anims, key, *pm).ok()) {
            failed_.insert(key);
            return nullptr;
        }
        const PoseModel* p = pm.get();
        models_[key] = std::move(pm);
        return p;
    }

    PoseStatus refRows(const PoseModel& pm, const PoseRequest& r, Float4 out[kPaletteBones * 3]) {
        identity96(out);
        std::string clip = r.clip;
        if (clip.empty()) {
            const bool fortified = r.unit && (r.unit->flags & kFlagFortified);
            clip = defaultClip(pm, fortified);
            if (clip.empty()) {
                PoseStatus s;
                s.code = PoseErr::ClipNotFound;
                s.msg = pm.key + ": no default clip (ec 1000" + std::string(fortified ? " or 1520" : "") + ")";
                return s;
            }
        }
        std::vector<MeshPalette> mp;
        PoseStatus st = poseFigure(ref_, pm, clip, r.t, true, r.scale, r.yaw, r.T,
                                   std::vector<uint32_t>(1, r.mesh->index), mp);
        if (st.ok()) to96(mp, out);
        return st;
    }

    void mix(const void* p, size_t n) {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            hash ^= c[i];
            hash *= 1099511628211ull;
        }
    }

    void compare(const UnitStatus& got, const PoseStatus& want, const Float4* a, const Float4* b, const PoseRequest& r,
                 const char* what) {
        ++requests;
        if (!got.ok()) ++failures;
        const int code = static_cast<int>(got.code);
        mix(&code, sizeof code);
        mix(got.text.data(), got.text.size());
        mix(a, sizeof(Float4) * kPaletteBones * 3);
        const bool same = got.code == poseErrUnitCode(want.code) && got.text == want.msg &&
                          std::memcmp(a, b, sizeof(Float4) * kPaletteBones * 3) == 0;
        if (!same) {
            ++mismatches;
            if (first.empty())
                first = std::string(what) + " " + r.model->gr2 + " mesh " + r.mesh->name + " figure " +
                        std::to_string(r.figure) + ": module " + got.text + " / reference " + want.str();
        }
    }
};

}  // namespace

C5R_TEST(catalogue_pose_equal) {
    Inputs in;
    UnitRenderer ur;
    CHECK_OK(ur.open(nullptr, in.files, in.db, in.exe));
    FixedPlacement place;
    ur.setPlacementSource(&place);
    GameFilesProvider prov(in.files);
    c5m::StringDatabase strings;
    CHECK_UNIT(loadStringDatabases(in.files.names(), prov, strings), UnitCode::Ok);
    ComparePose cmp(prov, &strings);
    ur.setPoseSource(&cmp);
    std::vector<c5m::DbRow> pc;
    c5m::Error e;
    CHECK(in.db.rows("PlayerColors", {"Type"}, pc, &e));
    UnitScene sc;
    PlayerInput p;
    p.player = 0;
    float rgba[4];
    for (auto& r : pc)
        if (!r.empty() && !r[0].null && ur.art().team_colour(r[0].text, rgba).ok()) {
            p.colour = r[0].text;
            break;
        }
    CHECK(!p.colour.empty());
    sc.players.push_back(p);
    for (size_t i = 0; i < in.rows.size(); ++i) {
        UnitInput u;
        u.player = 0;
        u.id = static_cast<int32_t>(i);
        u.unit_type = "UNIT_CATALOGUE";
        u.art_row = in.rows[i];
        u.plot_x = static_cast<int32_t>(i % 40);
        u.plot_y = static_cast<int32_t>(i / 40);
        u.flags = static_cast<uint8_t>((i % 3 == 2) ? kFlagFortified : 0u);
        sc.units.push_back(u);
    }
    size_t drawn = 0;
    for (int f = 0; f < 3; ++f) {
        cmp.frame = f;
        FramePlan plan;
        CHECK_OK(ur.prepare(sc, nullptr, plan));
        drawn += plan.drawn();
    }
    std::printf("catalogue pose: %zu art rows, %zu units drawn over 3 frames; %zu answers compared (%zu from programs, "
                "%zu failures), %zu differ%s%s\n",
                in.rows.size(), drawn, cmp.requests, cmp.programs, cmp.failures, cmp.mismatches,
                cmp.first.empty() ? "" : "; first: ", cmp.first.c_str());
    std::printf("module answer hash (FNV-1a 64, rows + codes + messages in request order): %016llx\n",
                static_cast<unsigned long long>(cmp.hash));
    std::printf("INDICATION (workstation, COMMON 23; not a result): ModulePose::rows / the pre-4466 per-mesh path, ms per "
                "frame (frame 0 binds the clips): %.3f / %.3f, %.3f / %.3f, %.3f / %.3f, over %zu row requests\n",
                cmp.modMs[0], cmp.refMs[0], cmp.modMs[1], cmp.refMs[1], cmp.modMs[2], cmp.refMs[2],
                cmp.requests - cmp.programs);
    CHECK(drawn > 0u);
    CHECK(cmp.requests > 1000u);
    CHECK_EQ(cmp.mismatches, static_cast<size_t>(0));
}

C5R_TEST_MAIN
