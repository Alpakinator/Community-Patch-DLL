// render/tests/units/test_masks_real.cpp - issue 0617: the track masks of real unit models, resolved as stock resolves them
// (unit-controller.md 13.3, "What stock does with a mask"). Install-backed: the install (C5R_GAME_DIR; VP as DLC there,
// else its mod roots, units_mods.h) and its string databases; files read at run time, nothing from them in git (D83).
//
// One case per reason the sweep of 0617 found (render/evidence/0617/sweep_masks.out.txt):
//   - none: VP's destroyer (wickes.gr2, the unit of the ticket), dreadnought, fleet destroyer (z_class.gr2) and Yamato:
//     no bone carries GrannyTrackMasks; their edited attack and bombard-defend graphs name TrackMaskAIM_LAYER. Before
//     0617 checkTrackMasks / resolveTrackMasks refused them (TrackMaskNotBound: not drawn while those states play);
//     now the binding is unbound (stock's NULL mask) and resolves to no mask: binding "", default weight 1, no bones.
//   - all: the stock battleship (every bone carries the binding; the AIM_*_DR turret bones at 1, the rest at 0) and the
//     stock tank (AIM_DR at 1).
//   - partial: the late cargo ship (every bone but Cargo_Ship carries it): that bone takes its parent's weight, as stock.
// A missing input skips (exit 77), which build.py counts as a failure.
#include <cstdio>
#include <string>
#include <vector>

#include "c5models/logic.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/unitanim/controller_pose.h"
#include "c5r/units/art.h"
#include "c5r/units/draw.h"
#include "units_mods.h"
#include "units_testkit.h"

using namespace c5r::units;
using namespace c5r::unitanim;

namespace {

const char* kAim = "TrackMaskAIM_LAYER";

struct Install {
    c5r::gamefs::GameFiles gf;
    std::unique_ptr<GameFilesProvider> files;
    c5m::StringDatabase strings;
    Install() {
        const std::string dir = c5r::test::gameDir();
        c5r::gamefs::GameFiles::Options o;
        o.extraLooseRoots = units_test::installModRoots(dir);
        CHECK_OK(gf.open(c5r::gamefs::fromUtf8(dir), o));
        files.reset(new GameFilesProvider(gf));
        CHECK_UNIT(loadStringDatabases(gf.names(), *files, strings), UnitCode::Ok);
    }
    bool model(const char* name, c5m::FileInfo& fi) {
        c5m::Bytes b;
        std::string origin;
        if (!files->read(name, b, &origin)) {
            c5r::test::skip(std::string(name) + " not in the install");
            return false;
        }
        c5m::Error e;
        const bool ok = c5m::read_gr2(b.data(), b.size(), &strings, fi, &e);
        CHECK(ok);
        std::printf("%s (%s): %zu skeleton(s)\n", name, origin.c_str(), fi.skeletons.size());
        return ok;
    }
    bool graph(const char* name, c5m::DagFile& d) {
        c5m::Bytes b;
        if (!files->read(name, b, nullptr)) {
            c5r::test::skip(std::string(name) + " not in the install");
            return false;
        }
        c5m::Error e;
        const bool ok = c5m::parse_dge(b.data(), b.size(), &strings, d, &e);
        CHECK(ok);
        return ok;
    }
};

PoseProgram maskOnly() {
    PoseProgram p;
    TrackMaskWeights tm;
    tm.binding = kAim;
    tm.default_weight = 1;
    p.masks.push_back(tm);
    return p;
}

// the skeletons the model's skinned meshes use (the pose step's rule)
std::vector<int32_t> meshSkeletons(const c5m::FileInfo& fi) {
    std::vector<int32_t> out;
    for (uint32_t m = 0; m < fi.meshes.size(); ++m) {
        const int32_t s = meshSkeleton(fi, m);
        bool seen = false;
        for (int32_t x : out) seen = seen || x == s;
        if (s >= 0 && !seen) out.push_back(s);
    }
    return out;
}

C5R_TEST(vp_edited_ships_unbound_mask_is_no_mask) {
    Install in;
    c5m::DagFile attacks, bombard;
    if (!in.graph("edited_aim_ms_modern_ship_attacks.dge", attacks)) return;
    if (!in.graph("edited_aim_ms_bombard_defend_loop_graph.dge", bombard)) return;
    CHECK(!attacks.dags.empty() && attacks.dags[0].track_masks == std::vector<std::string>({kAim}));
    CHECK(!bombard.dags.empty() && bombard.dags[0].track_masks == std::vector<std::string>({kAim}));
    const std::vector<const c5m::DagFile*> graphs = {&attacks, &bombard};
    const std::vector<std::string> names = {"edited_aim_ms_modern_ship_attacks.dge",
                                            "edited_aim_ms_bombard_defend_loop_graph.dge"};
    for (const char* m : {"wickes.gr2", "dreadnought_generic.gr2", "z_class.gr2", "yamato_class.gr2"}) {
        c5m::FileInfo fi;
        if (!in.model(m, fi)) continue;
        const std::vector<int32_t> skels = meshSkeletons(fi);
        CHECK(!skels.empty());
        for (int32_t s : skels) {
            const c5m::Skeleton& sk = fi.skeletons[static_cast<size_t>(s)];
            size_t with = 0;
            for (const c5m::Bone& b : sk.bones) with += b.track_masks.size();
            CHECK_EQ(with, 0u);   // no bone carries mask data
            CHECK(trackMaskUnbound(sk, kAim));
            PoseProgram p = maskOnly();
            const AnimStatus st = resolveTrackMasks(fi, s, p);
            CHECK(st.ok());
            CHECK(p.masks[0].binding.empty() && p.masks[0].bones.empty() && p.masks[0].default_weight == 1.0f);
            std::printf("  %s skeleton %s: %zu bones, no mask data: unbound, resolved as no mask\n", m, sk.name.c_str(),
                        sk.bones.size());
        }
        // the pre-check's form (before 0617: TrackMaskNotBound "no bone of skeleton World_Center ... carries it")
        const AnimStatus pc = checkTrackMasks(fi, graphs, names);
        if (!pc.ok()) std::printf("  %s: %s\n", c5r::unitanim::name(pc.code), pc.text.c_str());
        CHECK(pc.ok());
    }
}

C5R_TEST(stock_full_masks) {
    Install in;
    struct Want { const char* model; size_t at_one_min; const char* prefix; };
    for (const Want& w : {Want{"battleship.gr2", 2, "AIM_"}, Want{"tank.gr2", 1, "AIM_DR"}}) {
        c5m::FileInfo fi;
        if (!in.model(w.model, fi)) continue;
        for (int32_t s : meshSkeletons(fi)) {
            const c5m::Skeleton& sk = fi.skeletons[static_cast<size_t>(s)];
            CHECK(!trackMaskUnbound(sk, kAim));
            PoseProgram p = maskOnly();
            CHECK(resolveTrackMasks(fi, s, p).ok());
            const TrackMaskWeights& m = p.masks[0];
            CHECK(m.binding == kAim);
            CHECK_EQ(m.bones.size(), sk.bones.size());
            size_t ones = 0;
            for (size_t i = 0; i < m.bones.size(); ++i) {
                CHECK(m.weights[i] == 0.0f || m.weights[i] == 1.0f);
                float own = -1;
                CHECK(sk.bones[i].track_mask(kAim, own) && own == m.weights[i]);   // every bone carries its own
                if (m.weights[i] == 1.0f) {
                    ++ones;
                    CHECK(m.bones[i].compare(0, std::string(w.prefix).size(), w.prefix) == 0);
                }
            }
            CHECK(ones >= w.at_one_min);
            std::printf("  %s skeleton %s: %zu bones, %zu at weight 1\n", w.model, sk.name.c_str(), sk.bones.size(), ones);
        }
    }
}

C5R_TEST(stock_partial_mask_takes_the_parents_weight) {
    Install in;
    c5m::FileInfo fi;
    if (!in.model("cargo_ship_late.gr2", fi)) return;
    size_t missing = 0;
    for (int32_t s : meshSkeletons(fi)) {
        const c5m::Skeleton& sk = fi.skeletons[static_cast<size_t>(s)];
        CHECK(!trackMaskUnbound(sk, kAim));
        PoseProgram p = maskOnly();
        CHECK(resolveTrackMasks(fi, s, p).ok());
        const TrackMaskWeights& m = p.masks[0];
        CHECK_EQ(m.bones.size(), sk.bones.size());
        for (size_t i = 0; i < sk.bones.size(); ++i) {
            float own = -1;
            if (sk.bones[i].track_mask(kAim, own)) {
                CHECK(m.weights[i] == own);
                continue;
            }
            ++missing;
            const int32_t par = sk.bones[i].parent;
            const float want = par >= 0 ? m.weights[static_cast<size_t>(par)] : 0.0f;
            CHECK(m.weights[i] == want);
            std::printf("  cargo_ship_late.gr2 bone %s without the binding: parent %d, weight %g (before 0617: 1)\n",
                        sk.bones[i].name.c_str(), par, m.weights[i]);
        }
    }
    CHECK_EQ(missing, 1u);
}

}  // namespace

C5R_TEST_MAIN
