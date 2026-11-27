// bench_frame.cpp - the controller's frame and the drawn figures' pose rows on a recorded timeline, offline (issue 0441,
// epic 0375 follow-up F3; research/05-graphics-composition/unit-controller.md 19). Own code, 2026-11-16.
//
// What the server's render thread does per drawn frame for the units with render_units_anim=1 (srv_units_anim.cpp
// AnimRunner::apply, then UnitRenderer::prepare's pose step through ControllerPose), on the inputs of the driver
// (`unitanim play`): a timeline file (unitfeed's timeline.c5anim of a recorded bundle), the install, the database and
// the mods. After the timeline's frames it plays --frames more frames of --dt s with no event; from frame --warm on,
// each frame's time is measured in two parts:
//   apply  Controller::apply(frame, drawn) + ControllerPose::clearPrograms / setProgram for every program (AnimRunner)
//   pose   ControllerPose::rows for every figure program x every mesh that needs a pose (prepare's rule), as prepare
//          asks it (yaw and T fixed per figure: the placement is not this tool's part)
// The drawn set: --drawn K units of the held set, spread evenly over the units in key order (deterministic).
// --dump FILE writes one line per frame with the FNV-1a 64 hash of every output of the frame (the programs bit for bit,
// the triggers, every status with its text, the rows' bytes and each rows() status) and a last line with the hash of
// all lines: two builds whose dumps are equal gave byte-equal outputs on every frame.
// Timing is an indication on the machine that runs it (COMMON 23); the acceptance numbers come from the live series.
// --census (0470, unit-controller.md 19.7): after the timeline and at the end, the controller's graph census
// (Controller::graphCensus: figures, bound graph instances, their bytes by part, own and shared) and the process's
// private bytes.
//
//   c5r_unitanim_bench_frame --timeline FILE --game DIR --db FILE --exe FILE [--mods "DIR;DIR"] [--frames N]
//       [--dt S] [--warm N] [--drawn K] [--dump FILE] [--no-pose] [--census]
//   c5r_unitanim_bench_frame --durations --game DIR --db FILE --exe FILE [--mods "DIR;DIR"]
// --durations: the server's clip-duration read-ahead (srv_units_scene.cpp PrecheckRun::readDurations) both ways on the
// database: the pre-check (UnitRenderer::precheck, timed), the clips of every art row it reached, then (a) every clip
// read again (ClipGraphFactory::readDurations, 4468's read-ahead, timed) and (b) the durations taken from the
// pre-check's clip cache (ClipGraphFactory::takeFromCache) with only the rest read (0441, timed); (a) and (b) must be
// equal entry for entry (duration bits and refusal text), else exit 7.
// Exit: 0 ok, 2 usage, 3 the timeline refused, 4 an input unavailable, 5 the dump not written, 6 a frame refused,
// 7 the durations differ.
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/controller_pose.h"
#include "c5r/unitanim/driver.h"
#include "c5r/unitanim/timeline.h"
#include "c5r/units/art.h"
#include "c5r/units/draw.h"
#include "c5r/units/pose.h"
#include "c5r/units/units.h"

using namespace c5r;
using namespace c5r::unitanim;

namespace {

struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    template <class T> void pod(const T& v) { bytes(&v, sizeof v); }
    void str(const std::string& s) {
        pod(static_cast<uint64_t>(s.size()));
        bytes(s.data(), s.size());
    }
};

void hashProgram(Fnv& f, const PoseProgram& p) {
    f.pod(static_cast<uint64_t>(p.nodes.size()));
    for (const PoseNode& n : p.nodes) {
        f.pod(static_cast<int>(n.kind));
        f.str(n.clip);
        f.pod(n.ec);
        f.pod(n.t);
        f.pod(n.a);
        f.pod(n.b);
        f.pod(n.w);
        f.pod(n.mask);
    }
    f.pod(static_cast<uint64_t>(p.masks.size()));
    for (const TrackMaskWeights& m : p.masks) {
        f.str(m.binding);
        f.pod(m.default_weight);
        f.pod(m.target_weight);
        f.pod(static_cast<uint64_t>(m.bones.size()));
        for (const std::string& b : m.bones) f.str(b);
        for (float w : m.weights) f.pod(w);
    }
}

double nowMs() {
    LARGE_INTEGER c, fq;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&fq);
    return 1000.0 * static_cast<double>(c.QuadPart) / static_cast<double>(fq.QuadPart);
}

// 0470: the process's private bytes (commit), as the live series reads the server's
double privateMb() {
    PROCESS_MEMORY_COUNTERS_EX pm;
    std::memset(&pm, 0, sizeof pm);
    pm.cb = sizeof pm;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof pm)) return -1;
    return static_cast<double>(pm.PrivateUsage) / (1024.0 * 1024.0);
}

// 0470: the controller's graph census (Controller::graphCensus) with the process's private bytes
void printCensus(const Controller& c, const char* when) {
    const Controller::GraphCensus g = c.graphCensus();
    static const char* parts[] = {"graph", "tables", "sources", "other"};
    size_t own = 0, shared = 0;
    for (int p = 0; p < GraphFootprint::kParts; ++p) {
        own += g.own[p];
        shared += g.shared[p];
    }
    std::printf("bench_frame: census %s: figures %zu, graph instances %zu, member-states %zu, files %zu, shared blocks "
                "%zu; private %.1f MB\n",
                when, g.figures, g.instances, g.member_states, g.files, g.shared_blocks, privateMb());
    std::printf("bench_frame: census %s: own %zu B (%.1f per instance), shared %zu B, total %.2f MB\n", when, own,
                g.instances ? static_cast<double>(own) / static_cast<double>(g.instances) : 0.0, shared,
                static_cast<double>(own + shared) / (1024.0 * 1024.0));
    for (int p = 0; p < GraphFootprint::kParts; ++p)
        std::printf("bench_frame: census %s:   %-7s own %zu B (%.1f per instance), shared %zu B\n", when, parts[p],
                    g.own[p], g.instances ? static_cast<double>(g.own[p]) / static_cast<double>(g.instances) : 0.0,
                    g.shared[p]);
}

struct Stats {
    double mean = 0, sd = 0, median = 0, p95 = 0, min = 0, max = 0;
};
Stats statsOf(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    double sum = 0;
    for (double x : v) sum += x;
    s.mean = sum / static_cast<double>(v.size());
    double q = 0;
    for (double x : v) q += (x - s.mean) * (x - s.mean);
    s.sd = v.size() > 1 ? std::sqrt(q / static_cast<double>(v.size() - 1)) : 0;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.p95 = v[std::min(v.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(v.size())))];
    s.min = v.front();
    s.max = v.back();
    return s;
}

int fail(int code, const std::string& msg) {
    std::printf("bench_frame: FAILED (%d): %s\n", code, msg.c_str());
    return code;
}

std::wstring widen(const std::string& s) { return gamefs::fromUtf8(s); }

}  // namespace

int main(int argc, char** argv) {
    std::string timeline, game, db, exe, mods, dump;
    size_t frames = 1200, warm = 120, drawnK = 32;
    double dt = 1.0 / 115.0;
    bool doPose = true, durationsMode = false, census = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& v) {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::string v;
        bool ok = true;
        if (a == "--timeline") ok = next(timeline);
        else if (a == "--game") ok = next(game);
        else if (a == "--db") ok = next(db);
        else if (a == "--exe") ok = next(exe);
        else if (a == "--mods") ok = next(mods);
        else if (a == "--dump") ok = next(dump);
        else if (a == "--no-pose") doPose = false;
        else if (a == "--durations") durationsMode = true;
        else if (a == "--census") census = true;
        else if (a == "--frames" && (ok = next(v))) frames = static_cast<size_t>(std::strtoull(v.c_str(), nullptr, 10));
        else if (a == "--warm" && (ok = next(v))) warm = static_cast<size_t>(std::strtoull(v.c_str(), nullptr, 10));
        else if (a == "--drawn" && (ok = next(v))) drawnK = static_cast<size_t>(std::strtoull(v.c_str(), nullptr, 10));
        else if (a == "--dt" && (ok = next(v))) dt = std::strtod(v.c_str(), nullptr);
        else return fail(2, "unknown argument " + a);
        if (!ok) return fail(2, a + " needs a value");
    }
    if ((timeline.empty() && !durationsMode) || game.empty() || db.empty() || exe.empty())
        return fail(2, "needs --timeline (or --durations), --game, --db and --exe");
    if (!(dt > 0) || !std::isfinite(dt) || warm >= frames) return fail(2, "--dt must be > 0 and --warm below --frames");

    Timeline tl;
    units::UnitScene start;
    if (!durationsMode) {
        TimelineStatus ts = readTimelineFile(widen(timeline), tl, start);
        if (!ts.ok()) return fail(3, timeline + ": " + ts.str());
    }

    gamefs::GameFiles gf;
    gamefs::GameFiles::Options gopt;
    size_t at = 0;
    while (!mods.empty() && at <= mods.size()) {
        const size_t sc = mods.find(';', at);
        const std::string d = mods.substr(at, sc == std::string::npos ? std::string::npos : sc - at);
        if (!d.empty()) gopt.extraLooseRoots.push_back(widen(d));
        if (sc == std::string::npos) break;
        at = sc + 1;
    }
    Status s = gf.open(widen(game), gopt);
    if (!s.ok()) return fail(4, "game files " + game + ": " + s.str());
    c5m::SqliteTables tables;
    c5m::Error e;
    if (!c5m::SqliteTables::available() || !tables.open(db, &e)) return fail(4, "database " + db + ": " + e.what);
    units::GameFilesProvider provider(gf);
    units::ArtResolver resolver;
    units::UnitStatus us = resolver.open(tables, provider);
    if (!us.ok()) return fail(4, "art tables: " + us.str());
    c5m::StringDatabase strings;
    us = units::loadStringDatabases(gf.names(), provider, strings);
    if (!us.ok()) return fail(4, "string databases: " + us.str());
    BridgeTables bt;
    AnimStatus as = loadBridgeTables(tables, bt);
    if (!as.ok()) return fail(4, as.text);
    units::UnitRenderer renderer;   // CPU only: the models of the drawn units (loadUnit)
    s = renderer.open(nullptr, gf, tables, widen(exe), widen(game));
    if (!s.ok()) return fail(4, "unit renderer: " + s.str());

    if (durationsMode) {
        const double p0 = nowMs();
        std::vector<units::UnitTypeStatus> out;
        s = renderer.precheck(out);
        const double p1 = nowMs();
        if (!s.ok()) return fail(4, "pre-check: " + s.str());
        std::set<std::string> rowSet;
        for (const units::UnitTypeStatus& t : out)
            if (!t.art_row.empty()) rowSet.insert(t.art_row);
        std::vector<std::string> clips;
        size_t rowsN = 0;
        for (const std::string& row : rowSet) {
            units::UnitArt ua;
            if (!renderer.art().resolve(row, ua).ok()) continue;
            ++rowsN;
            for (const units::MemberArt& m : ua.members)
                for (const c5m::ResolvedAnimation& ra : m.files.animations) clips.push_back(ra.file.name);
        }
        const units::ClipFileLoader loader = units::clipLoaderFromProvider(provider, &strings);
        ClipGraphFactory::Durations full, mine;
        const double r0 = nowMs();
        ClipGraphFactory::readDurations(loader, clips, full);
        const double r1 = nowMs();
        size_t cached = 0;
        const units::ModulePose* mp = dynamic_cast<const units::ModulePose*>(renderer.poseSource());
        if (mp) cached = ClipGraphFactory::takeFromCache(mp->cache(), clips, mine);
        ClipGraphFactory::readDurations(loader, clips, mine);
        const double r2 = nowMs();
        size_t differ = 0, failed = 0;
        std::string first;
        for (const auto& kv : full) {
            failed += kv.second.first > 0 ? 0 : 1;
            auto it = mine.find(kv.first);
            const bool same = it != mine.end() &&
                              std::memcmp(&it->second.first, &kv.second.first, sizeof(float)) == 0 &&
                              it->second.second == kv.second.second;
            if (!same && !differ++) first = kv.first;
        }
        if (mine.size() != full.size() && !differ++) first = "(the maps' sizes)";
        std::printf("bench_frame: durations: pre-check %.1f ms (%zu entries, %s), %zu art rows, %zu clip names, %zu "
                    "clips (%zu without a duration)\n",
                    p1 - p0, out.size(), mp ? "module pose" : "no module pose", rowsN, clips.size(), full.size(), failed);
        std::printf("bench_frame: durations: read again %.1f ms; from the pre-check's cache %zu + read %zu: %.1f ms\n",
                    r1 - r0, cached, mine.size() - cached, r2 - r1);
        std::printf("bench_frame: durations: %zu of %zu differ%s%s\n", differ, full.size(), differ ? ", first " : "",
                    first.c_str());
        if (differ) return fail(7, "the durations differ");
        std::printf("bench_frame: OK\n");
        return 0;
    }

    // AnimRunner::open's controller
    units::ModulePose module(provider, &strings);
    ControllerPose pose(module);
    ResolverArtSource art(resolver);
    ClipGraphFactory graphs(units::clipLoaderFromProvider(provider, &strings));
    graphs.options.hold_from_continue = true;
    ActionBridge bridge(bt);
    bridge.remove_fallen = true;
    ControllerOptions co;
    co.seed = tl.seed;
    co.forced = tl.forced;
    co.bridge = &bridge;
    co.strings = &strings;
    Controller c;
    c.open(provider, art, graphs, co);
    c.addPlayers(tl.players);
    c.start(start);
    std::map<int32_t, units::PlayerInput> players;
    for (auto& p : start.players) players[p.player] = p;
    for (auto& p : tl.players) players[p.player] = p;
    std::map<Controller::UnitKey, units::UnitInput> inputs;
    for (auto& u : start.units) inputs[std::make_pair(u.player, u.id)] = u;

    FILE* df = nullptr;
    if (!dump.empty() && !(df = std::fopen(dump.c_str(), "wb"))) return fail(5, "cannot write " + dump);
    Fnv all;
    std::set<Controller::UnitKey> drawn;
    std::vector<double> applyMs, poseMs;
    size_t rowsCalls = 0, rowsFailed = 0, programsDrawn = 0, blends = 0;
    struct Loaded {
        units::UnitStatus st;
        units::UnitArt art;
        std::vector<const units::UnitModel*> models;
    };
    std::map<Controller::UnitKey, Loaded> loaded;
    const size_t total = tl.frames.size() + frames;
    for (size_t fi = 0; fi < total; ++fi) {
        TimelineFrame extra;
        const TimelineFrame* fr = nullptr;
        if (fi < tl.frames.size()) {
            fr = &tl.frames[fi];
            for (const TimelineEvent& ev : fr->events) {
                const auto key = std::make_pair(ev.player, ev.id);
                if (ev.kind == EventKind::Create) inputs[key] = ev.unit;
                else if (ev.kind == EventKind::Remove) inputs.erase(key);
            }
        } else {
            extra.seq = (tl.frames.empty() ? 0 : tl.frames.back().seq) + (fi - tl.frames.size()) + 1;
            extra.dt = dt;
            fr = &extra;
        }
        if (fi == tl.frames.size()) {   // the drawn set, after the timeline: K units spread over the held ones
            if (census) printCensus(c, "after the timeline");
            std::vector<Controller::UnitKey> keys;
            for (auto& kv : inputs)
                if (c.status(kv.first.first, kv.first.second)) keys.push_back(kv.first);
            // 0470: K above the held units draws every unit (the loop ran past `keys` before)
            for (size_t k = 0; k < std::min(drawnK, keys.size()); ++k)
                drawn.insert(keys[(k * keys.size()) / std::min(drawnK, keys.size())]);
            std::printf("bench_frame: %zu units held, %zu drawn, %zu timeline frames, %zu more of %.6f s\n", keys.size(),
                        drawn.size(), tl.frames.size(), frames, dt);
        }
        const bool measured = fi >= tl.frames.size() + warm;
        const double t0 = nowMs();
        AnimStatus st = c.apply(*fr, fi < tl.frames.size() ? nullptr : &drawn);
        if (!st.ok()) return fail(6, "frame " + std::to_string(fi) + ": " + st.text);
        pose.clearPrograms();
        for (const FigureProgram& p : c.programs()) pose.setProgram(p.player, p.id, p.figure, p.program);
        const double t1 = nowMs();
        Fnv fh;
        for (const FigureProgram& p : c.programs()) {
            fh.pod(p.player);
            fh.pod(p.id);
            fh.pod(p.figure);
            fh.pod(p.member);
            fh.pod(p.state);
            hashProgram(fh, p.program);
        }
        for (const TriggerEvent& t : c.triggers()) {
            fh.pod(t.player);
            fh.pod(t.id);
            fh.pod(t.figure);
            fh.pod(static_cast<int>(t.type));
            fh.pod(t.ec);
            fh.pod(t.trigger);
            fh.pod(t.time);
            fh.pod(t.offset);
            fh.str(t.event);
            fh.str(t.bone);
        }
        for (const UnitAnimStatus& u : c.statuses()) {
            fh.pod(u.player);
            fh.pod(u.id);
            fh.pod(static_cast<int>(u.status.code));
            fh.str(u.status.text);
        }
        double t2 = nowMs(), t3 = t2;
        if (doPose && fi >= tl.frames.size()) {
            // the requests prepare makes for the drawn units' figures (draw.cpp: per figure, per mesh needing a pose)
            std::vector<units::PoseRequest> reqs;
            for (const FigureProgram& p : c.programs()) {
                const auto key = std::make_pair(p.player, p.id);
                auto in = inputs.find(key);
                if (in == inputs.end()) continue;
                auto it = loaded.find(key);
                if (it == loaded.end()) {
                    Loaded l;
                    auto po = players.find(p.player);
                    l.st = renderer.loadUnit(in->second, po == players.end() ? nullptr : &po->second, l.art, l.models);
                    it = loaded.emplace(key, std::move(l)).first;
                }
                const Loaded& l = it->second;
                if (!l.st.ok() || p.member >= l.models.size() || !l.models[p.member]) continue;
                const units::UnitModel* model = l.models[p.member];
                for (const units::ModelMesh& mesh : model->meshes) {
                    if (mesh.material == units::Material::Static && mesh.bones == 0) continue;
                    units::PoseRequest rq;
                    rq.unit = &in->second;
                    rq.member = &l.art.members[p.member];
                    rq.model = model;
                    rq.mesh = &mesh;
                    rq.figure = p.figure;
                    rq.scale = l.art.members[p.member].scale;
                    rq.yaw = 0.25f + 0.125f * static_cast<float>(p.figure % 8);
                    rq.T[0] = 10.0f * static_cast<float>(p.figure);
                    rq.T[1] = 3.0f;
                    rq.T[2] = 1.0f;
                    reqs.push_back(rq);
                }
            }
            units::Float4 rows[units::kPaletteBones * 3];
            std::vector<units::UnitStatus> rs(reqs.size());
            std::vector<units::Float4> out(reqs.size() * units::kPaletteBones * 3);
            t2 = nowMs();
            for (size_t k = 0; k < reqs.size(); ++k) {
                rs[k] = pose.rows(reqs[k], rows);
                std::memcpy(&out[k * units::kPaletteBones * 3], rows, sizeof rows);
            }
            t3 = nowMs();
            for (size_t k = 0; k < reqs.size(); ++k) {
                fh.pod(static_cast<int>(rs[k].code));
                fh.str(rs[k].text);
                rowsFailed += rs[k].ok() ? 0 : 1;
            }
            if (!out.empty()) fh.bytes(out.data(), out.size() * sizeof(units::Float4));
            if (measured) rowsCalls += reqs.size();
        }
        if (measured) {
            applyMs.push_back(t1 - t0);
            if (doPose) poseMs.push_back(t3 - t2);
            programsDrawn += c.programs().size();
            for (const FigureProgram& p : c.programs()) blends += p.program.single() ? 0 : 1;
        }
        if (df) std::fprintf(df, "%zu %016llx\n", fi, static_cast<unsigned long long>(fh.h));
        all.pod(fh.h);
    }
    if (census) printCensus(c, "at the end");
    const size_t n = applyMs.size();
    const Stats a = statsOf(applyMs), p = statsOf(poseMs);
    std::printf("bench_frame: apply ms per frame mean %.4f sd %.4f median %.4f p95 %.4f min %.4f max %.4f (n %zu)\n", a.mean,
                a.sd, a.median, a.p95, a.min, a.max, n);
    if (doPose)
        std::printf("bench_frame: pose ms per frame mean %.4f sd %.4f median %.4f p95 %.4f min %.4f max %.4f (n %zu)\n",
                    p.mean, p.sd, p.median, p.p95, p.min, p.max, poseMs.size());
    std::printf("bench_frame: programs per frame %.1f (blends %.1f), rows calls per frame %.1f, rows failed %zu, "
                "units %zu\n",
                n ? static_cast<double>(programsDrawn) / static_cast<double>(n) : 0.0,
                n ? static_cast<double>(blends) / static_cast<double>(n) : 0.0,
                n ? static_cast<double>(rowsCalls) / static_cast<double>(n) : 0.0, rowsFailed, c.units());
    std::printf("bench_frame: world memo hits %zu samples %zu\n", module.worldHits(), module.worldSamples());
    std::printf("bench_frame: outputs hash %016llx over %zu frames\n", static_cast<unsigned long long>(all.h), total);
    if (df) {
        std::fprintf(df, "all %016llx\n", static_cast<unsigned long long>(all.h));
        if (std::fclose(df) != 0) return fail(5, "cannot close " + dump);
    }
    std::printf("bench_frame: OK\n");
    return 0;
}
