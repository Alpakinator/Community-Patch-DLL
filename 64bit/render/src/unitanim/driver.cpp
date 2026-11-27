// driver.cpp - the offline driver of the unit animation controller (epic 0374, issue 4404 part B; driver.h;
// unit-controller.md 4 and 14).
#include "c5r/unitanim/driver.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include <direct.h>
#include <errno.h>

#include "c5models/art.h"
#include "c5models/gr2.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/units/art.h"
#include "c5r/units/draw.h"
#include "c5r/units/scene_file.h"

namespace c5r { namespace unitanim {

const char* driverExitName(DriverExit e) {
    switch (e) {
    case DriverExit::Ok: return "Ok";
    case DriverExit::UnitsFailed: return "UnitsFailed";
    case DriverExit::Usage: return "Usage";
    case DriverExit::TimelineRefused: return "TimelineRefused";
    case DriverExit::InputsUnavailable: return "InputsUnavailable";
    case DriverExit::OutputFailed: return "OutputFailed";
    case DriverExit::FrameRefused: return "FrameRefused";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------- graph factory
std::pair<float, std::string> ClipGraphFactory::readOne(const units::ClipFileLoader& loader, const std::string& key) {
    c5m::FileInfo fi;
    bool missing = false;
    std::string w;
    float d = 0;
    if (!loader || !loader(key, fi, missing, w)) {
        if (w.empty()) w = key + ": no clip loader";
    } else if (fi.animations.empty() || !(fi.animations[0].duration > 0)) {
        w = key + ": no animation with a duration";
    } else {
        d = fi.animations[0].duration;
    }
    return std::make_pair(d, w);
}

void ClipGraphFactory::readDurations(const units::ClipFileLoader& loader, const std::vector<std::string>& clips,
                                     Durations& out) {
    for (const std::string& c : clips) {
        const std::string key = c5m::basename_lower(c);
        if (key.empty() || out.count(key)) continue;
        out.emplace(key, readOne(loader, key));
    }
}

size_t ClipGraphFactory::takeFromCache(const units::ClipCache& cache, const std::vector<std::string>& clips,
                                       Durations& out) {
    std::map<std::string, float> cached;
    cache.durations(cached);
    size_t n = 0;
    for (const std::string& c : clips) {
        const std::string key = c5m::basename_lower(c);
        if (key.empty() || out.count(key)) continue;
        auto it = cached.find(key);
        if (it == cached.end()) continue;
        out.emplace(key, std::make_pair(it->second, std::string()));
        ++n;
    }
    return n;
}

size_t ClipGraphFactory::seed(const Durations& d) {
    size_t n = 0;
    for (const auto& kv : d)
        if (durations_.emplace(kv.first, kv.second).second) ++n;
    return n;
}

float ClipGraphFactory::clipDuration(const std::string& clip) {
    const std::string key = c5m::basename_lower(clip);
    auto it = durations_.find(key);
    if (it == durations_.end()) {
        ++lazy_reads_;
        it = durations_.emplace(key, readOne(loader_, key)).first;
    }
    return it->second.first > 0 ? it->second.first : 0.0f;
}

AnimStatus ClipGraphFactory::bind(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                                  std::unique_ptr<GraphInstance>& out) {
    EcMap m = ecs;
    m.durations.assign(m.clips.size(), 0.f);
    std::string why;
    for (size_t i = 0; i < m.clips.size(); ++i) {
        const std::string key = c5m::basename_lower(m.clips[i]);
        auto it = durations_.find(key);
        if (it == durations_.end()) {
            ++lazy_reads_;
            it = durations_.emplace(key, readOne(loader_, key)).first;
        }
        m.durations[i] = it->second.first;
        if (!(it->second.first > 0)) why += (why.empty() ? "" : "; ") + it->second.second;
    }
    AnimStatus st = bindGraph(file, m, file_name, options, share_, out);   // 0470: shared shapes
    if (!st.ok() && !why.empty()) st.text += " (clips without a duration: " + why + ")";
    return st;
}

namespace {

std::wstring widen(const std::string& s) { return gamefs::fromUtf8(s); }
std::string narrow(const std::wstring& s) { return gamefs::toUtf8(s); }

// Creates `dir` and its missing parents. False when it cannot.
bool makeDirs(const std::wstring& dir) {
    if (dir.empty()) return false;
    std::wstring p;
    for (size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == L'/' || dir[i] == L'\\') {
            if (!p.empty() && !(p.size() == 2 && p[1] == L':')) {
                if (_wmkdir(p.c_str()) != 0 && errno != EEXIST) return false;
            }
        }
        if (i < dir.size()) p += dir[i];
    }
    return true;
}

std::string num(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

std::string text(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string q = "\"";
    for (char c : s) {
        if (c == '"') q += '"';
        q += c;
    }
    return q + "\"";
}

class Csv {
public:
    bool open(const std::wstring& path, const char* header) {
        f_ = _wfopen(path.c_str(), L"wb");
        if (!f_) return false;
        return put(std::string(header) + "\n");
    }
    bool put(const std::string& line) {
        if (!f_ || !ok_) return false;
        ok_ = std::fwrite(line.data(), 1, line.size(), f_) == line.size();
        return ok_;
    }
    bool close() {
        if (!f_) return false;
        const bool c = std::fclose(f_) == 0;
        f_ = nullptr;
        return ok_ && c;
    }
    ~Csv() {
        if (f_) std::fclose(f_);
    }

private:
    std::FILE* f_ = nullptr;
    bool ok_ = true;
};

std::string seqName(uint64_t seq) {
    char b[32];
    std::snprintf(b, sizeof b, "%010llu", static_cast<unsigned long long>(seq));
    return b;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- play
DriverExit playTimeline(const Timeline& tl, const units::UnitScene& start, const std::wstring& out_dir,
                        c5m::FileProvider& files, ArtSource& art, GraphFactory& graphs, const PlayOptions& opt,
                        PlaySummary& sum, std::string& msg) {
    sum = PlaySummary();
    msg.clear();
    const std::wstring sep = L"/";
    if (!makeDirs(out_dir) || (opt.c5units && !makeDirs(out_dir + sep + L"frames"))) {
        msg = "cannot create " + narrow(out_dir);
        return DriverExit::OutputFailed;
    }
    Csv progs, trigs, stats, masks;   // masks.csv: 4406 r2 (additive), the program's track masks by index
    if (!progs.open(out_dir + sep + L"programs.csv",
                    "frame,seq,present_time,player,id,figure,node,kind,clip,ec,t,a,b,w,mask,state") ||
        !trigs.open(out_dir + sep + L"triggers.csv",
                    "frame,seq,present_time,player,id,figure,type,ec,trigger,time,offset,event,bone") ||
        !stats.open(out_dir + sep + L"status.csv", "frame,seq,player,id,status,text") ||
        !masks.open(out_dir + sep + L"masks.csv",
                    "frame,seq,player,id,figure,mask,binding,default_weight,target_weight,bones")) {
        msg = "cannot write the CSV files in " + narrow(out_dir);
        return DriverExit::OutputFailed;
    }
    ControllerOptions co = opt.controller;
    co.seed = tl.seed;
    co.forced = tl.forced;
    Controller c;
    c.open(files, art, graphs, co);
    if (opt.prepare) opt.prepare(c);
    c.addPlayers(tl.players);
    c.start(start);
    std::vector<units::PlayerInput> players = start.players;
    for (auto& p : tl.players) players.push_back(p);

    std::set<std::pair<int32_t, int32_t>> failed;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t fi = 0; fi < tl.frames.size(); ++fi) {
        const TimelineFrame& fr = tl.frames[fi];
        AnimStatus st = c.apply(fr);
        if (!st.ok()) {
            msg = "frame " + std::to_string(fi) + " (seq " + std::to_string(fr.seq) + "): " + name(st.code) + ": " +
                  st.text;
            return DriverExit::FrameRefused;
        }
        ++sum.frames;
        const std::string head = std::to_string(fi) + "," + std::to_string(fr.seq) + "," +
                                 (fr.has_present ? num(fr.present_time) : std::string("-"));
        bool ok = true;
        for (auto& p : c.programs()) {
            for (size_t n = 0; n < p.program.nodes.size(); ++n) {
                const PoseNode& nd = p.program.nodes[n];
                std::string l = head + "," + std::to_string(p.player) + "," + std::to_string(p.id) + "," +
                                std::to_string(p.figure) + "," + std::to_string(n) + ",";
                if (nd.kind == PoseNode::Sample)
                    l += "sample," + text(nd.clip) + "," + std::to_string(nd.ec) + "," + num(nd.t) + ",-,-,-,-,";
                else
                    l += "blend,-,-,-," + std::to_string(nd.a) + "," + std::to_string(nd.b) + "," + num(nd.w) + "," +
                         (nd.mask < 0 ? std::string("-") : std::to_string(nd.mask)) + ",";
                l += std::to_string(p.state) + "\n";
                ok = ok && progs.put(l);
                ++sum.program_rows;
            }
            for (size_t m = 0; m < p.program.masks.size(); ++m) {
                const TrackMaskWeights& mk = p.program.masks[m];
                ok = ok && masks.put(std::to_string(fi) + "," + std::to_string(fr.seq) + "," +
                                     std::to_string(p.player) + "," + std::to_string(p.id) + "," +
                                     std::to_string(p.figure) + "," + std::to_string(m) + "," + text(mk.binding) +
                                     "," + num(mk.default_weight) + "," + num(mk.target_weight) + "," +
                                     std::to_string(mk.bones.size()) + "\n");
            }
        }
        for (auto& t : c.triggers()) {
            ok = ok && trigs.put(head + "," + std::to_string(t.player) + "," + std::to_string(t.id) + "," +
                                 std::to_string(t.figure) + "," + c5m::trigger_type_name(t.type) + "," +
                                 std::to_string(t.ec) + "," + std::to_string(t.trigger) + "," + num(t.time) + "," +
                                 num(t.offset) + "," + text(t.event) + "," + text(t.bone) + "\n");
            ++sum.trigger_rows;
        }
        bool allOk = true;
        for (auto& s : c.statuses()) {
            ok = ok && stats.put(std::to_string(fi) + "," + std::to_string(fr.seq) + "," + std::to_string(s.player) +
                                 "," + std::to_string(s.id) + "," + name(s.status.code) + "," +
                                 text(s.status.text) + "\n");
            ++sum.status_rows;
            if (!s.status.ok()) {
                allOk = false;
                if (failed.insert(std::make_pair(s.player, s.id)).second && sum.first_failure.empty())
                    sum.first_failure = std::to_string(s.player) + "/" + std::to_string(s.id) + " " +
                                        name(s.status.code) + ": " + s.status.text;
            }
        }
        if (c.statuses().size() > sum.units_max) sum.units_max = c.statuses().size();
        if (!ok) {
            msg = "cannot write the CSV files in " + narrow(out_dir);
            return DriverExit::OutputFailed;
        }
        // .c5units: every unit Ok, every figure with one source, every unit with at least one figure program
        if (opt.c5units && allOk) {
            units::UnitScene sc;
            sc.format = start.format;
            sc.position_source = start.position_source;
            sc.water_level = start.water_level;
            sc.players = players;
            std::map<std::pair<int32_t, int32_t>, size_t> at;
            bool single = true;
            for (auto& s : c.statuses()) {
                const units::UnitInput* in = c.unitInput(s.player, s.id);
                if (!in) {
                    single = false;
                    break;
                }
                units::UnitInput u = *in;
                u.figures.clear();
                at[std::make_pair(s.player, s.id)] = sc.units.size();
                sc.units.push_back(u);
            }
            for (auto& p : c.programs()) {
                if (!single) break;
                auto it = at.find(std::make_pair(p.player, p.id));
                if (it == at.end() || !p.program.single()) {
                    single = false;
                    break;
                }
                units::FigureInput f;
                f.figure = static_cast<uint16_t>(p.figure);
                f.clip = p.program.nodes[0].clip;
                f.t = p.program.nodes[0].t;
                sc.units[it->second].figures.push_back(f);
            }
            for (auto& u : sc.units) single = single && !u.figures.empty();
            if (single) {
                units::SceneFileStatus ws =
                    units::writeSceneFile(out_dir + sep + L"frames" + sep + widen(seqName(fr.seq)) + L".c5units", sc);
                if (!ws.ok()) {
                    msg = "frame " + std::to_string(fi) + ": .c5units: " + ws.str();
                    return DriverExit::OutputFailed;
                }
                ++sum.c5units_frames;
            }
        }
    }
    sum.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sum.failed_units = failed.size();
    if (!progs.close() || !trigs.close() || !stats.close()) {
        msg = "cannot write the CSV files in " + narrow(out_dir);
        return DriverExit::OutputFailed;
    }
    if (!failed.empty()) {
        msg = std::to_string(failed.size()) + " unit(s) failed; first " + sum.first_failure;
        return DriverExit::UnitsFailed;
    }
    return DriverExit::Ok;
}

// ---------------------------------------------------------------------------------------------- the tool
namespace {

const char* const kUsage =
    "usage: unitanim play --timeline FILE.c5anim --game INSTALL --db DB --out DIR [--mods \"A;B\"]\n"
    "                     [--work-table FILE] [--no-c5units] [--fade MACHINE:FROM:TO:SECONDS]...\n"
    "                     [--skip SEQ:PLAYER:ID:FIGURE|*:STEPS]... [--clock steps|continuous]\n"
    "                     [--run-from]   (GraphOptions::hold_from_continue off, 4406 r2; default held)\n"
    "                     [--action-skip none|order|next]   (ActionSkip, 4406 r2; default none)\n"
    "                     [--damage-death]   (ActionBridge::remove_fallen off: 1200 on damage, comparison only;\n"
    "                                         default: the fallen are removed, MC #2147)\n"
    "exit: 0 Ok, 1 UnitsFailed, 2 Usage, 3 TimelineRefused, 4 InputsUnavailable, 5 OutputFailed, 6 FrameRefused\n";

int fail(DriverExit e, const std::string& why) {
    if (e == DriverExit::Usage) std::fprintf(stderr, "unitanim: %s\n%s", why.c_str(), kUsage);
    else std::fprintf(stderr, "unitanim: %s: %s\n", driverExitName(e), why.c_str());
    return static_cast<int>(e);
}

bool readText(const std::string& path, std::string& out) {
    std::FILE* f = _wfopen(widen(path).c_str(), L"rb");
    if (!f) return false;
    out.clear();
    char b[65536];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

// "<machine>:<from>:<to>:<seconds>" (an X1 rule as data, ControllerOptions::fades; 4406).
bool parseFade(const std::string& v, CrossFadeRule& r) {
    const size_t a = v.find(':'), b = v.find(':', a + 1), c = v.find(':', b + 1);
    if (a == std::string::npos || b == std::string::npos || c == std::string::npos || a == 0) return false;
    char* e = nullptr;
    r.machine = v.substr(0, a);
    const std::string f = v.substr(a + 1, b - a - 1), t = v.substr(b + 1, c - b - 1), d = v.substr(c + 1);
    r.from_state = static_cast<int32_t>(std::strtol(f.c_str(), &e, 10));
    if (f.empty() || *e) return false;
    r.to_state = static_cast<int32_t>(std::strtol(t.c_str(), &e, 10));
    if (t.empty() || *e) return false;
    r.duration = std::strtof(d.c_str(), &e);
    return !d.empty() && !*e && r.duration > 0;
}

// "<seq>:<player>:<id>:<figure or *>:<steps>" (a K6 skip as data, ControllerOptions::skips; 4406).
bool parseSkip(const std::string& v, SkipRule& r) {
    std::vector<std::string> f;
    size_t at = 0;
    while (true) {
        const size_t c = v.find(':', at);
        f.push_back(v.substr(at, c == std::string::npos ? std::string::npos : c - at));
        if (c == std::string::npos) break;
        at = c + 1;
    }
    if (f.size() != 5) return false;
    for (size_t i = 0; i < 5; ++i)
        if (f[i].empty()) return false;
    char* e = nullptr;
    r.seq = std::strtoull(f[0].c_str(), &e, 10);
    if (*e) return false;
    r.player = static_cast<int32_t>(std::strtol(f[1].c_str(), &e, 10));
    if (*e) return false;
    r.id = static_cast<int32_t>(std::strtol(f[2].c_str(), &e, 10));
    if (*e) return false;
    if (f[3] == "*") r.figure = -1;
    else {
        r.figure = static_cast<int32_t>(std::strtol(f[3].c_str(), &e, 10));
        if (*e || r.figure < 0) return false;
    }
    r.steps = static_cast<int32_t>(std::strtol(f[4].c_str(), &e, 10));
    return !*e && r.steps >= 0;
}

}  // namespace

int driverMain(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "play") != 0)
        return fail(DriverExit::Usage, argc < 2 ? "no command" : std::string("unknown command ") + argv[1]);
    std::string timeline, game, db, out, mods, workTable;
    bool c5units = true;
    std::vector<CrossFadeRule> fades;
    std::vector<SkipRule> skips;
    ClockMode clockMode = ClockMode::Continuous;   // MC #2089
    bool runFrom = false;                          // 4406 r2: GraphOptions::hold_from_continue off (comparison)
    ActionSkip actionSkip = ActionSkip::None;      // 4406 r2: --action-skip
    bool damageRemove = true;                      // 4406 r3; the default since MC #2147 (premise change 5)
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& v) {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        bool okArg = true;
        if (a == "--timeline") okArg = next(timeline);
        else if (a == "--game") okArg = next(game);
        else if (a == "--db") okArg = next(db);
        else if (a == "--out") okArg = next(out);
        else if (a == "--mods") okArg = next(mods);
        else if (a == "--work-table") okArg = next(workTable);
        else if (a == "--no-c5units") c5units = false;
        else if (a == "--run-from") runFrom = true;
        else if (a == "--damage-remove") damageRemove = true;   // 4406 r3: ActionBridge::remove_fallen (the default)
        else if (a == "--damage-death") damageRemove = false;   // MC #2147: 4404's 1200 reading, comparison only
        else if (a == "--action-skip") {   // 4406 r2: ActionSkip as data (clock.h)
            std::string v;
            okArg = next(v);
            if (okArg && v == "none") actionSkip = ActionSkip::None;
            else if (okArg && v == "order") actionSkip = ActionSkip::OrderFrame;
            else if (okArg && v == "next") actionSkip = ActionSkip::NextFrame;
            else if (okArg) return fail(DriverExit::Usage, "bad --action-skip " + v);
        }
        else if (a == "--clock") {
            std::string v;
            okArg = next(v);
            if (okArg && v == "continuous") clockMode = ClockMode::Continuous;
            else if (okArg && v == "steps") clockMode = ClockMode::Steps;   // a comparison only (MC #2089)
            else if (okArg) return fail(DriverExit::Usage, "bad --clock " + v);
        } else if (a == "--fade" || a == "--skip") {
            std::string v;
            okArg = next(v);
            if (okArg && a == "--fade") {
                CrossFadeRule r;
                if (!parseFade(v, r)) return fail(DriverExit::Usage, "bad --fade " + v);
                fades.push_back(r);
            } else if (okArg) {
                SkipRule r;
                if (!parseSkip(v, r)) return fail(DriverExit::Usage, "bad --skip " + v);
                skips.push_back(r);
            }
        }
        else return fail(DriverExit::Usage, "unknown argument " + a);
        if (!okArg) return fail(DriverExit::Usage, a + " needs a value");
    }
    if (timeline.empty() || game.empty() || db.empty() || out.empty())
        return fail(DriverExit::Usage, "play needs --timeline, --game, --db and --out");

    // 1. the timeline and its start scene (before any game file: a bad file is refused without an install)
    Timeline tl;
    units::UnitScene start;
    TimelineStatus ts = readTimelineFile(widen(timeline), tl, start);
    if (!ts.ok()) return fail(DriverExit::TimelineRefused, timeline + ": " + ts.str());

    // 2. the game's files, the database, the art, the string databases, the bridge's tables
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
    if (!s.ok()) return fail(DriverExit::InputsUnavailable, "game files " + game + ": " + s.str());
    c5m::SqliteTables tables;
    c5m::Error e;
    if (!c5m::SqliteTables::available() || !tables.open(db, &e))
        return fail(DriverExit::InputsUnavailable, "database " + db + ": " + e.what);
    units::GameFilesProvider provider(gf);
    units::ArtResolver resolver;
    units::UnitStatus us = resolver.open(tables, provider);
    if (!us.ok()) return fail(DriverExit::InputsUnavailable, "art tables: " + us.str());
    c5m::StringDatabase strings;
    us = units::loadStringDatabases(gf.names(), provider, strings);
    if (!us.ok()) return fail(DriverExit::InputsUnavailable, "string databases: " + us.str());
    BridgeTables bt;
    AnimStatus as = loadBridgeTables(tables, bt);
    if (!as.ok()) return fail(DriverExit::InputsUnavailable, as.text);
    if (!workTable.empty()) {
        std::string wt;
        if (!readText(workTable, wt)) return fail(DriverExit::InputsUnavailable, "cannot read " + workTable);
        as = parseWorkTable(wt, bt.work_codes);
        if (!as.ok()) return fail(DriverExit::InputsUnavailable, workTable + ": " + as.text);
    }

    // 3. play
    ResolverArtSource art(resolver);
    ClipGraphFactory graphs(units::clipLoaderFromProvider(provider, &strings));
    graphs.options.hold_from_continue = !runFrom;   // default held (MC #2117)
    ActionBridge bridge(bt);
    bridge.remove_fallen = damageRemove;
    PlayOptions po;
    po.controller.bridge = &bridge;
    po.controller.fades = fades;    // default: none (defaultCrossFades() is empty since 4406)
    po.controller.skips = skips;
    po.controller.action_skip = actionSkip;
    po.controller.clock.mode = clockMode;
    po.controller.strings = &strings;   // 4410: the pre-check reads the aim members' track mask bones
    po.c5units = c5units;
    PlaySummary sum;
    std::string msg;
    const DriverExit r = playTimeline(tl, start, widen(out), provider, art, graphs, po, sum, msg);
    std::printf("unitanim %s frames %zu units_max %zu program_rows %zu trigger_rows %zu status_rows %zu c5units_frames "
                "%zu failed_units %zu clips_read %zu seconds %.3f\n",
                driverExitName(r), sum.frames, sum.units_max, sum.program_rows, sum.trigger_rows, sum.status_rows,
                sum.c5units_frames, sum.failed_units, graphs.clipsRead(), sum.seconds);
    if (r != DriverExit::Ok) return fail(r, msg);
    return 0;
}

}}  // namespace c5r::unitanim
