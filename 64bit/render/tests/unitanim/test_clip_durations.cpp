// render/tests/unitanim/test_clip_durations.cpp - issue 4468 (epic 0375): the controller's graph factory with clip
// durations read ahead (ClipGraphFactory::readDurations / seed / lazyReads; driver.h; unit-controller.md 18). A counting
// clip loader stands in for the files (no game file, D83): durations read ahead equal those bind() reads itself (a clip
// with no animation, an unreadable clip and a missing loader included, with their texts), a seeded factory binds without
// reading any clip, an entry the factory already holds is kept, and an unseeded clip is still read lazily (and counted).
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <cstring>

#include "c5r/unitanim/driver.h"
#include "c5r/unitanim/graph.h"
#include "pose_fixture.h"   // 0441: the synthetic model and clips of 0041's pose tests
#include "testkit.h"

using namespace c5r::unitanim;

namespace {

struct Loader {
    std::map<std::string, float> clips;   // lower-case basename -> duration (0: no animation); absent: not found
    std::map<std::string, int> reads;
    c5r::units::ClipFileLoader fn() {
        return [this](const std::string& key, c5m::FileInfo& fi, bool& missing, std::string& why) {
            reads[key]++;
            auto it = clips.find(key);
            if (it == clips.end()) {
                missing = true;
                why = key + ": not in any source";
                return false;
            }
            if (it->second > 0) {
                c5m::Animation a;
                a.duration = it->second;
                fi.animations.push_back(a);
            }
            return true;
        };
    }
    int total() const {
        int n = 0;
        for (auto& kv : reads) n += kv.second;
        return n;
    }
};

c5m::DagFile oneSource(int32_t ec) {
    c5m::DagGraph g;
    g.source_ecs = {ec};
    c5m::DagNode root;
    root.type = c5m::DagNodeType::Subgraph;
    root.type_name = "TypeMarker_NodeDataSubgraph";
    c5m::DagEdge e;
    e.name = "Pose out";
    e.input_node = 1;
    root.inputs.push_back(e);
    c5m::DagNode src;
    src.type = c5m::DagNodeType::PoseAnimSource;
    src.type_name = "TypeMarker_NodeDataPoseAnimSource";
    src.source_index = 0;
    g.nodes = {root, src};
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}

EcMap ecs() {
    EcMap m;
    m.ecs = {1000, 2000, 1040};
    m.clips = {"Art/Units/W_IdleA.gr2", "w_idleb.gr2", "W_FidgetA.GR2"};
    return m;
}

}  // namespace

C5R_TEST(read_ahead_equals_lazy_read) {
    Loader l;
    l.clips = {{"w_idlea.gr2", 6.0f}, {"w_idleb.gr2", 5.5f}, {"w_noanim.gr2", 0.0f}};
    ClipGraphFactory::Durations d;
    ClipGraphFactory::readDurations(l.fn(), {"Art/Units/W_IdleA.gr2", "w_idleb.gr2", "w_idleb.gr2", "W_NoAnim.gr2", "w_gone.gr2", ""},
                                    d);
    CHECK_EQ(d.size(), 4u);                       // keyed by lower-case basename, each once, "" skipped
    CHECK_EQ(l.total(), 4);
    CHECK(d["w_idlea.gr2"].first == 6.0f && d["w_idlea.gr2"].second.empty());
    CHECK(d["w_noanim.gr2"].first == 0.0f && d["w_noanim.gr2"].second == "w_noanim.gr2: no animation with a duration");
    CHECK(d["w_gone.gr2"].first == 0.0f && d["w_gone.gr2"].second == "w_gone.gr2: not in any source");
    // the same clips read by bind() itself give the same entries
    Loader l2;
    l2.clips = l.clips;
    ClipGraphFactory lazy(l2.fn());
    EcMap m;
    m.ecs = {1000, 2000, 3000, 4000};
    m.clips = {"Art/Units/W_IdleA.gr2", "w_idleb.gr2", "W_NoAnim.gr2", "w_gone.gr2"};
    std::unique_ptr<GraphInstance> g;
    AnimStatus st = lazy.bind(oneSource(1000), m, "one.dge", g);
    CHECK(st.ok() && g);
    CHECK_EQ(lazy.lazyReads(), 4u);
    CHECK_EQ(lazy.clipsRead(), 4u);
    ClipGraphFactory seeded(Loader().fn());
    CHECK_EQ(seeded.seed(d), 4u);
    CHECK_EQ(seeded.seed(d), 0u);                 // already held: kept
    std::unique_ptr<GraphInstance> g2;
    AnimStatus st2 = seeded.bind(oneSource(1000), m, "one.dge", g2);
    CHECK(st2.ok() && g2);
    CHECK_EQ(seeded.lazyReads(), 0u);
    // a graph whose source has no duration fails alike, with the same text, seeded or not
    std::unique_ptr<GraphInstance> g3, g4;
    AnimStatus a3 = lazy.bind(oneSource(3000), m, "noanim.dge", g3);
    AnimStatus a4 = seeded.bind(oneSource(3000), m, "noanim.dge", g4);
    CHECK(a3.code == AnimCode::EcNotBound && a4.code == AnimCode::EcNotBound && a3.text == a4.text && !g3 && !g4);
}

C5R_TEST(seeded_factory_reads_only_what_it_lacks) {
    Loader l;
    l.clips = {{"w_idlea.gr2", 6.0f}, {"w_idleb.gr2", 5.5f}, {"w_fidgeta.gr2", 2.0f}};
    ClipGraphFactory f(l.fn());
    ClipGraphFactory::Durations d;
    Loader ahead;
    ahead.clips = l.clips;
    ClipGraphFactory::readDurations(ahead.fn(), {"w_idlea.gr2", "w_idleb.gr2"}, d);
    CHECK_EQ(f.seed(d), 2u);
    std::unique_ptr<GraphInstance> g;
    CHECK(f.bind(oneSource(1000), ecs(), "idle.dge", g).ok());
    CHECK_EQ(f.lazyReads(), 1u);                  // the fidget clip only, read on the binding thread
    CHECK_EQ(l.reads["w_fidgeta.gr2"], 1);
    CHECK_EQ(l.total(), 1);
    CHECK(f.bind(oneSource(1040), ecs(), "fidget.dge", g).ok());
    CHECK_EQ(f.lazyReads(), 1u);                  // cached since
    // a seed after a lazy read keeps the factory's own entry
    ClipGraphFactory::Durations other;
    other["w_fidgeta.gr2"] = std::make_pair(9.0f, std::string());
    CHECK_EQ(f.seed(other), 0u);
}

// A factory with no loader (the negative path): every clip is "no clip loader", read ahead or lazily alike.
C5R_TEST(no_loader) {
    ClipGraphFactory::Durations d;
    ClipGraphFactory::readDurations(c5r::units::ClipFileLoader(), {"w_idlea.gr2"}, d);
    CHECK(d.size() == 1 && d["w_idlea.gr2"].first == 0.0f && d["w_idlea.gr2"].second == "w_idlea.gr2: no clip loader");
    ClipGraphFactory f((c5r::units::ClipFileLoader()));
    std::unique_ptr<GraphInstance> g;
    EcMap m;
    m.ecs = {1000};
    m.clips = {"w_idlea.gr2"};
    AnimStatus st = f.bind(oneSource(1000), m, "one.dge", g);
    CHECK(st.code == AnimCode::EcNotBound && st.text.find("w_idlea.gr2: no clip loader") != std::string::npos);
}

// 0441 (unit-controller.md 19): the durations a pose clip cache already read (the server's pre-check binds every clip
// of every row it checks) equal a second read of the same clips entry for entry, by bits and text: taken from the cache
// for the clips that bound, read for the rest (one that did not bind, one missing, one unreadable), nothing for a clip
// the cache never saw. Mutation: dropping the "status ok" filter of ClipCache::durations takes unit_bad.gr2's
// failed binding (duration 0) and fails the count checks below.
C5R_TEST(durations_from_the_clip_cache_equal_a_second_read) {
    using namespace c5r::units;
    posefix::Loader binder;
    ClipCache cache(binder.fn());
    PoseModel pm;
    CHECK(buildPoseModel(posefix::makeModel(false), posefix::anims(), "unit.gr2|unit.fxsxml", pm).ok());
    std::vector<std::string> clips;
    for (const c5m::FxsAnimation& a : posefix::anims()) {
        clips.push_back(a.file);
        if (a.file == "Unit_FidgetA.gr2") continue;   // a clip the cache never binds: read
        for (uint32_t sk = 0; sk < 2; ++sk) {
            const CachedClip* cc = nullptr;
            cache.get(pm, a.file, sk, cc);
        }
    }
    std::map<std::string, float> held;
    CHECK_EQ(cache.durations(held), 4u);   // idleA, fortify, yup, front bound; bad, missing, broken failed; fidget unseen
    CHECK_EQ(cache.durations(held), 0u);   // kept
    posefix::Loader again, rest;
    ClipGraphFactory::Durations full, mine;
    ClipGraphFactory::readDurations(again.fn(), clips, full);
    CHECK_EQ(ClipGraphFactory::takeFromCache(cache, clips, mine), 4u);
    CHECK_EQ(ClipGraphFactory::takeFromCache(cache, clips, mine), 0u);   // entries already in `out` are kept
    ClipGraphFactory::readDurations(rest.fn(), clips, mine);
    CHECK_EQ(rest.calls, 4);               // fidget, bad, missing, broken: only what the cache lacks is read
    CHECK_EQ(full.size(), 8u);
    CHECK_EQ(mine.size(), full.size());
    for (const auto& kv : full) {
        auto it = mine.find(kv.first);
        CHECK(it != mine.end());
        if (it == mine.end()) continue;
        CHECK(std::memcmp(&it->second.first, &kv.second.first, sizeof(float)) == 0);
        CHECK(it->second.second == kv.second.second);
    }
    CHECK(mine["unit_bad.gr2"].first == 2.0f && mine["unit_bad.gr2"].second.empty());   // read: its file has a duration
    CHECK(mine["unit_missing.gr2"].first == 0.0f && mine["unit_missing.gr2"].second == "unit_missing.gr2: in no source");
    CHECK(mine["unit_broken.gr2"].first == 0.0f &&
          mine["unit_broken.gr2"].second == "unit_broken.gr2: BadMagic: not a granny file");
}

C5R_TEST_MAIN
