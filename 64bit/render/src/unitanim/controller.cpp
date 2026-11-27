// controller.cpp - the unit animation controller (epic 0374, issue 4403; controller.h; unit-controller.md 1, 4, 5,
// 9, 13.1).
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/controller_pose.h"   // 4410: checkTrackMasks in the pre-check
#include "c5r/units/placement.h"          // 0439: livingFigures for the revival
#include "missing_clip.h"                 // 4770: a state's clip missing from the install (unit-controller.md 13.4)

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <set>

namespace c5r { namespace unitanim {

using units::UnitInput;

// ---------------------------------------------------------------------------------------------- data
std::vector<CrossFadeRule> defaultCrossFades() {
    // Empty (lead-0374's decision, 2026-11-15 22:59, from 4404's double-fade finding, unit-controller.md 14.2): the
    // one rule this list held (omni 6 -> 3, 0.3 s, from 4407's P01 point) faded on top of the fidget graph's own fade,
    // which alone explains P01 frame 4681 (15). The mechanism stays as data (ControllerOptions::fades).
    return std::vector<CrossFadeRule>();
}

AnimStatus ResolverArtSource::resolve(const UnitInput& u, const units::PlayerInput* owner, units::UnitArt& out) {
    std::string row = u.art_row;
    if (row.empty()) {
        if (!owner)
            return AnimStatus::Fail(AnimCode::UnitFailed, u.unit_type + ": no player record for its owner " +
                                                              std::to_string(u.player));
        units::UnitStatus s = r_.art_row(u.unit_type, owner->art_style, owner->era, row);
        if (!s.ok()) return AnimStatus::Fail(AnimCode::UnitFailed, u.unit_type + ": " + s.str());
    }
    units::UnitStatus s = r_.resolve(row, out);
    if (!s.ok()) return AnimStatus::Fail(AnimCode::UnitFailed, row + ": " + s.str());
    out.unit_type = u.unit_type;
    return AnimStatus::Ok();
}

AnimStatus BasicBridge::requests(const TimelineEvent& ev, const UnitInput& unit, std::vector<Request>& out) {
    out.clear();
    Request r;
    switch (ev.kind) {
    case EventKind::Fortify: r.value = ev.a ? 1500 : 1000; break;
    case EventKind::Move: r.value = 1400; break;
    case EventKind::Arrive: r.value = 1000; break;
    case EventKind::Destroyed: r.value = 1200; break;
    case EventKind::Kill: r.value = 1200; r.figures = ev.figures; break;
    case EventKind::Action:
        r.figures = ev.figures;
        switch (ev.action) {
        case ActionKind::Attack: r.value = 1100; break;
        case ActionKind::AttackCity: r.value = 1160; break;
        case ActionKind::Bombard: r.value = 1180; break;
        case ActionKind::Charge: r.value = 1120; break;
        case ActionKind::AttackCharge: r.value = 1140; break;
        case ActionKind::Victory: r.value = 1800; break;
        case ActionKind::BombardDefend: r.value = 1280; break;
        case ActionKind::Activate: r.value = 1990; break;
        case ActionKind::Captured: r.value = 1195; break;
        case ActionKind::Run: r.value = 1400; break;           // 4775: the choreography's approach
        case ActionKind::CombatReady: r.value = 1600; break;   // 4775
        case ActionKind::Idle: r.value = 1000; break;          // 4775: the reface
        case ActionKind::Walk: r.value = 1450; break;          // 4750: the defender's phase-2 stance
        default:
            return AnimStatus::Fail(AnimCode::BadEvent, std::string("action ") + actionKindName(ev.action) +
                                                            " on " + unit.unit_type +
                                                            ": no request code in the files (the bridge, 4404)");
        }
        break;
    default:
        return AnimStatus::Ok();   // no request: turn, damage, embark, garrison, visibility
    }
    out.push_back(r);
    return AnimStatus::Ok();
}

// ---------------------------------------------------------------------------------------------- runtime state
struct Controller::Member {
    std::string key;
    const BoundMachine* machine = nullptr;
    std::vector<const c5m::DagFile*> graph;    // per state index (nullptr: no asset)
    std::vector<std::string> graph_name;       // per state index
    std::vector<const c5m::TriggerSystem*> triggers;
    std::vector<std::string> trigger_files;    // 4722 (additive): the file of each entry of `triggers`
    EcMap ecs;
    // 4770: per entry of `ecs`, whether the art step found the clip in the install (c5m::FileRef::found); false =
    // named by the .fxsxml, not shipped (unit-controller.md 13.4)
    std::vector<bool> ec_found;
    std::set<int32_t> p4;                      // the values its machine reads on path 4 (method 1 or 6; 0439)
    // 4738: per entry of `ecs`, its clip's duration (GraphFactory::clipDuration), the threshold of the EXE's ECComplete;
    // empty when the factory knows no durations (then ECComplete is the walk of the current graph, as before)
    std::vector<float> ec_duration;
};

namespace controller_detail {

struct Fig {
    uint32_t member = 0, index = 0;
    bool alive = true;
    MachineState ms;
    std::vector<std::unique_ptr<GraphInstance>> graphs;        // per state index (bound on first entry)
    GraphInstance* cur = nullptr;
    size_t cur_state = SIZE_MAX;
    bool fading = false;
    PoseProgram frozen;
    double fade_t = 0, fade_d = 0;
    float start_t = 0;            // a figure line's start time, applied when the figure first has a graph
    // 4406 round 2: a request on path 4 applied once the default chooser has written its own (0 = none): an air-domain
    // figure is created parked (1000, landed) although the airplane machine's default writes S4=1400 (15.8, [I]).
    int32_t start_p4 = 0;
    uint64_t instance = 0;        // 4782: the clip instance: a new id each time the figure enters a state with a graph
};

}  // namespace controller_detail

namespace {

using controller_detail::Fig;

const size_t kMaxChain = 16;   // graphless states taken in one update

bool crossed(float time, const ActiveSource& s) {
    if (!s.looped) return time >= s.prev_t && time < s.t;
    return time >= s.prev_t || time < s.t;
}

std::string pointName(const char* kind, const std::string& machine, size_t socket) {
    return std::string(kind) + ":" + machine + ":" + std::to_string(socket);
}

// 4770 (missing_clip.h): per controller, its LOUD sink and its count of reverted occurrences. Kept here, not in the
// Controller, because the public header is not changed by 4770 (unit-controller.md 13.4 names the fields).
struct MissingClipEntry {
    LoudSink sink;
    size_t reverts = 0;
};
std::mutex& missingClipMutex() {
    static std::mutex m;
    return m;
}
std::map<const Controller*, MissingClipEntry>& missingClipRegistry() {
    static std::map<const Controller*, MissingClipEntry> r;
    return r;
}
void missingClipLoud(const Controller& c, const std::string& line) {
    LoudSink sink;
    {
        std::lock_guard<std::mutex> lock(missingClipMutex());
        MissingClipEntry& e = missingClipRegistry()[&c];
        ++e.reverts;
        sink = e.sink;
    }
    if (sink) sink(line);
    else std::fprintf(stderr, "%s\n", line.c_str());
}

}  // namespace

const char kMissingInInstall[] = "missing in install: ";

bool missingInInstall(const AnimStatus& s) {
    return s.code == AnimCode::EcNotBound && s.text.compare(0, sizeof(kMissingInInstall) - 1, kMissingInInstall) == 0;
}

void setMissingClipSink(const Controller& c, LoudSink sink) {
    std::lock_guard<std::mutex> lock(missingClipMutex());
    missingClipRegistry()[&c].sink = std::move(sink);
}

size_t missingClipReverts(const Controller& c) {
    std::lock_guard<std::mutex> lock(missingClipMutex());
    auto it = missingClipRegistry().find(&c);
    return it == missingClipRegistry().end() ? 0 : it->second.reverts;
}

struct Controller::Unit {
    UnitInput in;
    units::UnitArt art;
    AnimStatus st;
    UnitClock clock;
    Rng rng;
    ForcedChoices forced;
    std::vector<const Member*> members;
    std::vector<controller_detail::Fig> figs;
    // 4406 r2: ActionSkip (clock.h): armed by an action event on frame action_seq, consumed by one step
    bool action_armed = false;
    uint64_t action_seq = 0;
    // 0441: settle() has work: a new unit (its start), revived figures, or a figure left in a state without a graph
    // (step's chain ends there when no socket holds). Otherwise settle() would change nothing and is skipped.
    bool settle_pending = true;
    // 4770 (unit-controller.md 13.4): the (member index, state index) pairs whose clip is missing from the install that
    // the unit's figures were kept out of in the current occurrence: logged once, then kept out again silently and
    // without a bind; cleared when an event's request reaches a figure of the unit (a new occurrence)
    std::vector<std::pair<uint32_t, size_t>> missing;
    uint64_t* instances = nullptr;   // 4782: the controller's clip-instance counter (instance ids unique per controller)
};

namespace {

class FigHost : public MachineHost {
public:
    FigHost(Controller::Unit& u, Fig& f, const BoundMachine& m, bool forced) : u_(u), f_(f), m_(m), forced_(forced) {}
    bool ecComplete(int32_t ec, float fraction) override {
        (void)fraction;   // 4738: the EXE reads and keeps the condition's value but its test never uses it
        // 4738 (W42's reading of build 4390913, unit-controller.md 13.1 as corrected): ECComplete(ec) holds when the time
        // in the current state is above the duration of the member's clip for that ec (0 when the member has none): no
        // graph, selection or playing source is looked at. So the omni ENTER_BOMBARD_DEFEND (state 15) is left after
        // the 1280 clip's length also when its graph plays the 1285 loop (entered from fortify or combat, path 6 = 1),
        // where the walk below never completed and froze the figure (the 12 s limit fights of forms B and the MG's).
        const Controller::Member& mem = *u_.members[f_.member];
        if (!mem.ec_duration.empty()) {
            float d = 0;
            for (size_t i = 0; i < mem.ecs.ecs.size(); ++i)
                if (mem.ecs.ecs[i] == ec) {
                    d = mem.ec_duration[i];   // the first entry naming it, as EcMap::find binds the graphs
                    break;
                }
            return f_.ms.time > d;
        }
        if (!f_.cur || f_.cur_state != f_.ms.state) return false;
        return f_.cur->ecComplete(ec, f_.ms.opts);   // 0441: one walk (graph.h)
    }
    AnimStatus roll(size_t socket, float chance, bool& fires) override {
        int32_t v = 0;
        // 0441: the point's name only when a choice is queued (the free game queues none: no string per roll)
        if (!u_.forced.empty() && u_.forced.take(static_cast<int32_t>(f_.index), pointName("roll", m_.file, socket), v)) {
            fires = v != 0;
            return AnimStatus::Ok();
        }
        // forced mode without a record: the roll does not fire (a "no" is every update's default; the recordings
        // name the firings only; 13.1)
        fires = forced_ ? false : u_.rng.roll(chance);
        return AnimStatus::Ok();
    }
    AnimStatus option(size_t socket, const c5m::FsmCondition& c, int32_t& value) override {
        // 0441: the point's name only when it is needed (a queued choice or forced mode's refusal)
        const std::string p = (!u_.forced.empty() || forced_) ? pointName("option", m_.file, socket) : std::string();
        if (!u_.forced.empty() && u_.forced.take(static_cast<int32_t>(f_.index), p, value)) {
            if (!optionAllowed(c, value))
                return AnimStatus::Fail(AnimCode::BadEvent, "choice " + std::to_string(value) + " at " + p +
                                                                " (figure " + std::to_string(f_.index) +
                                                                ") is not a value the option can take");
            return AnimStatus::Ok();
        }
        if (forced_)
            return AnimStatus::Fail(AnimCode::ForcedChoiceMissing,
                                    p + " (figure " + std::to_string(f_.index) + "): no choice record");
        value = drawOption(c, u_.rng.uniform());
        return AnimStatus::Ok();
    }
private:
    Controller::Unit& u_;
    Fig& f_;
    const BoundMachine& m_;
    bool forced_;
};

}  // namespace

Controller::Controller() {}
Controller::~Controller() {
    std::lock_guard<std::mutex> lock(missingClipMutex());   // 4770
    missingClipRegistry().erase(this);
}

void Controller::open(c5m::FileProvider& files, ArtSource& art, GraphFactory& graphs, const ControllerOptions& opt) {
    {
        std::lock_guard<std::mutex> lock(missingClipMutex());   // 4770: the count starts again, the sink is kept
        auto it = missingClipRegistry().find(this);
        if (it != missingClipRegistry().end()) it->second.reverts = 0;
    }
    files_ = &files;
    art_ = &art;
    graphs_ = &graphs;
    opt_ = opt;
    cache_.reset(new LogicCache(files));
    members_.clear();
    units_.clear();
    programs_.clear();
    triggers_.clear();
    statuses_.clear();
}

void Controller::addPlayers(const std::vector<units::PlayerInput>& players) {
    for (auto& p : players) players_[p.player] = p;
}

size_t Controller::units() const { return units_.size(); }

Controller::GraphCensus Controller::graphCensus() const {
    GraphCensus c;
    std::set<const void*> blocks;
    std::set<std::pair<const Member*, size_t>> states;
    std::set<const c5m::DagFile*> files;
    for (const auto& kv : units_) {
        const Unit& u = *kv.second;
        for (const Fig& f : u.figs) {
            ++c.figures;
            const Member* m = f.member < u.members.size() ? u.members[f.member] : nullptr;
            for (size_t s = 0; s < f.graphs.size(); ++s) {
                if (!f.graphs[s]) continue;
                ++c.instances;
                GraphFootprint fp;
                f.graphs[s]->footprint(fp);
                for (int p = 0; p < GraphFootprint::kParts; ++p) c.own[p] += fp.own[p];
                if (fp.shared_id && blocks.insert(fp.shared_id).second)
                    for (int p = 0; p < GraphFootprint::kParts; ++p) c.shared[p] += fp.shared[p];
                if (m) {
                    states.insert(std::make_pair(m, s));
                    if (s < m->graph.size() && m->graph[s]) files.insert(m->graph[s]);
                }
            }
        }
    }
    c.shared_blocks = blocks.size();
    c.member_states = states.size();
    c.files = files.size();
    return c;
}

const AnimStatus* Controller::status(int32_t player, int32_t id) const {
    auto it = units_.find(std::make_pair(player, id));
    return it == units_.end() ? nullptr : &it->second->st;
}

const units::UnitInput* Controller::unitInput(int32_t player, int32_t id) const {
    auto it = units_.find(std::make_pair(player, id));
    return it == units_.end() ? nullptr : &it->second->in;
}

AnimStatus Controller::member(const units::MemberArt& ma, const Member*& out) {
    out = nullptr;
    const auto& files = ma.files;
    const std::string key = ma.member + "|" + logicName(files.fxsxml.name);
    auto it = members_.find(key);
    if (it != members_.end()) {
        out = it->second.get();
        return AnimStatus::Ok();
    }
    if (files.state_machines.empty())
        return AnimStatus::Fail(AnimCode::MachineNotFound, ma.member + ": " + files.fxsxml.name +
                                                               " names no state machine");
    std::unique_ptr<Member> m(new Member());
    m->key = key;
    AnimStatus st = cache_->machine(files.state_machines[0].name, m->machine);
    if (!st.ok()) return AnimStatus::Fail(st.code, ma.member + ": " + st.text);
    const auto& fsm = m->machine->fsm;
    m->graph.assign(fsm.states.size(), nullptr);
    m->graph_name.assign(fsm.states.size(), std::string());
    for (size_t i = 0; i < fsm.states.size(); ++i) {
        if (fsm.states[i].asset.empty()) continue;
        st = cache_->graph(fsm.states[i].asset, m->graph[i]);
        if (!st.ok()) return AnimStatus::Fail(st.code, ma.member + " state " + std::to_string(fsm.states[i].id) +
                                                           ": " + st.text);
        m->graph_name[i] = logicName(fsm.states[i].asset);
    }
    for (auto& t : files.timed_triggers) {
        const c5m::TriggerSystem* ts = nullptr;
        st = cache_->triggers(t.file.name, ts);
        if (!st.ok()) return AnimStatus::Fail(st.code, ma.member + ": " + st.text);
        m->triggers.push_back(ts);
        m->trigger_files.push_back(t.file.name);   // 4722
    }
    for (auto& a : files.animations)
        for (int ec : a.ec) {
            m->ecs.ecs.push_back(ec);
            m->ecs.clips.push_back(a.file.name);
            m->ec_found.push_back(a.file.found);   // 4770
        }
    // 4738: the ECComplete thresholds, read once when the member is bound (the EXE binds them when the model's machine is
    // attached to the figure's animation instance)
    m->ec_duration.reserve(m->ecs.clips.size());
    for (const std::string& clip : m->ecs.clips) {
        const float d = graphs_->clipDuration(clip);
        if (d < 0) {
            m->ec_duration.clear();
            break;
        }
        m->ec_duration.push_back(d);
    }
    for (auto& so : fsm.sockets)
        for (auto& c : so.conditions)
            // 0439: a not-equal test reads the value too (civilian_state_01 leaves its fidget on P4!=1000)
            if (c.type == c5m::CondType::CheckPathOption && c.path == 4 && (c.method == 1 || c.method == 6))
                m->p4.insert(c.value);
    out = m.get();
    members_[key] = std::move(m);
    return AnimStatus::Ok();
}

namespace {

// 4770 (unit-controller.md 13.4): whether graph `g`'s EcNotBound is a missing-in-install one: every source ec of the
// graph is named by the member's .fxsxml, and at least one, by the entry bindGraph uses (the first that names it,
// EcMap::find), is a clip the art step did not find. `list` names those (ec and clip). An ec the .fxsxml does not name
// makes it any other EcNotBound (false), as does a graph whose named clips were all found (a clip that is there but
// gives no duration).
bool missingInInstallClips(const c5m::DagFile& g, const Controller::Member& m, std::string& list) {
    list.clear();
    if (g.dags.empty()) return false;
    bool any = false;
    for (int32_t ec : g.dags[0].source_ecs) {
        size_t i = 0;
        while (i < m.ecs.ecs.size() && i < m.ecs.clips.size() && m.ecs.ecs[i] != ec) ++i;
        if (i >= m.ecs.ecs.size() || i >= m.ecs.clips.size()) return false;   // not named: not this case
        if (i < m.ec_found.size() && !m.ec_found[i]) {
            list += (list.empty() ? "" : ", ") + std::string("ec ") + std::to_string(ec) + " clip " + m.ecs.clips[i];
            any = true;
        }
    }
    return any;
}

// Entering f.ms.state (after a transition from `from`, or a start when from == SIZE_MAX). `prior` is the figure's
// machine state before the update that made the transition (for a start: the state itself).
AnimStatus enterState(const Controller& c, Controller::Unit& u, Fig& f, const Controller::Member& m, GraphFactory& gf,
                      const std::vector<CrossFadeRule>& fades, size_t from, const MachineState& prior) {
    const BoundMachine& bm = *m.machine;
    const std::vector<const c5m::DagFile*>& graph = m.graph;
    const PathOptions& before = prior.opts;
    const size_t to = f.ms.state;
    const c5m::DagFile* g = graph[to];
    if (!g) return AnimStatus::Ok();   // a state without a graph holds the previous one (13.1)
    if (f.graphs.size() < graph.size()) f.graphs.resize(graph.size());
    std::unique_ptr<GraphInstance>& inst = f.graphs[to];
    if (!inst) {
        // 4770: a state this occurrence already kept the figure out of: kept out again, silently, without a bind
        const bool keep = f.cur != nullptr && from != SIZE_MAX;
        const std::pair<uint32_t, size_t> key(f.member, to);
        if (keep && std::find(u.missing.begin(), u.missing.end(), key) != u.missing.end()) {
            f.ms = prior;
            return AnimStatus::Ok();
        }
        AnimStatus st = gf.bind(*g, m.ecs, m.graph_name[to], inst);
        if (!st.ok()) {
            std::string list;
            if (st.code == AnimCode::EcNotBound && missingInInstallClips(*g, m, list)) {
                if (!keep) {
                    // a start: no state to keep; fails as before, flagged (13.4)
                    st.text = kMissingInInstall + list + ": " + st.text;
                    return st;
                }
                // the figure keeps its state, graph, clip and clip time; one LOUD line per occurrence (per member and
                // state of the unit), counted
                inst.reset();
                const int32_t fromId = bm.fsm.states[from].id, toId = bm.fsm.states[to].id;
                const units::MemberArt* ma = f.member < u.art.members.size() ? &u.art.members[f.member] : nullptr;
                missingClipLoud(c, std::string("LOUD unitanim: ") + kMissingInInstall + "unit (" +
                                       std::to_string(u.in.player) + ", " + std::to_string(u.in.id) + ") " +
                                       u.in.unit_type + ", member " +
                                       (ma ? ma->member + " (" + ma->files.fxsxml.name + ")" : std::string("?")) +
                                       " from figure " + std::to_string(f.index) + ", " + bm.file + " state " +
                                       std::to_string(fromId) + " -> " + std::to_string(toId) + " (" +
                                       m.graph_name[to] + "): " + list + ": the figures keep state " +
                                       std::to_string(fromId) + " (" + st.text + ")");
                u.missing.push_back(key);
                f.ms = prior;
                return AnimStatus::Ok();
            }
            return st;
        }
        if (!inst) return AnimStatus::Fail(AnimCode::BadLogicFile, m.graph_name[to] + ": the graph factory gave none");
    }
    const CrossFadeRule* rule = nullptr;
    if (from != SIZE_MAX)
        for (auto& r : fades)
            if (r.machine == bm.file && r.from_state == bm.fsm.states[from].id && r.to_state == bm.fsm.states[to].id &&
                r.duration > 0)
                rule = &r;
    f.fading = false;
    f.frozen.clear();
    if (rule && f.cur) {
        PoseProgram p;
        if (f.cur->program(before, p).ok() && !p.nodes.empty()) {
            f.frozen = p;
            f.fading = true;
            f.fade_t = 0;
            f.fade_d = rule->duration;
        }
    }
    // 4406 round 2: the previous state's sources (collected before f.cur switches) hand their clip times to the new
    // state's ContinueOffset sources (unit-controller.md 6.3, 15.8); a start (no previous graph) restarts from 0.
    std::vector<ActiveSource> previous;
    if (f.cur) f.cur->active(before, previous);
    inst->restartFrom(previous);
    f.cur = inst.get();
    f.cur_state = to;
    if (u.instances) f.instance = ++*u.instances;   // 4782: a new clip instance
    return AnimStatus::Ok();
}

}  // namespace

void Controller::create(const UnitInput& in) {
    std::unique_ptr<Unit> u(new Unit());
    u->in = in;
    u->instances = &instances_;   // 4782
    u->rng.reseed(unitSeed(opt_.seed, in.player, in.id));
    auto po = players_.find(in.player);
    u->st = art_->resolve(in, po == players_.end() ? nullptr : &po->second, u->art);
    if (u->st.ok()) {
        for (auto& ma : u->art.members) {
            const Member* m = nullptr;
            AnimStatus st = member(ma, m);
            if (!st.ok()) {
                u->st = st;
                break;
            }
            u->members.push_back(m);
        }
    }
    if (u->st.ok()) {
        uint32_t total = u->art.figures();
        uint32_t alive = total;
        if (in.flags & units::kFlagDead) alive = 0;
        else if (in.alive >= 0) alive = std::min<uint32_t>(total, static_cast<uint32_t>(in.alive));
        uint32_t index = 0;
        for (uint32_t mi = 0; mi < u->art.members.size(); ++mi)
            for (int k = 0; k < u->art.members[mi].count; ++k, ++index) {
                if (index >= alive) continue;
                Fig f;
                f.member = mi;
                f.index = index;
                u->figs.push_back(std::move(f));
            }
        const float p4 = (in.flags & units::kFlagFortified) ? 1500.f : 1000.f;
        for (auto& f : u->figs) {
            const Member& m = *u->members[f.member];
            f.ms.state = m.machine->default_state;
            f.ms.opts.v[4] = p4;
            // 4406 round 2 (lead's decision, [I]; 15.8): an air-domain figure is created parked. The unit input has
            // no flying flag; flight is requested by move / attack (1400 / 1100 / 1180). Only where the member's
            // machine tests path 4 = 1000 (the airplane machine; the missile machine does not).
            if (u->art.members[f.member].domain == "Air" && m.p4.count(1000)) f.start_p4 = 1000;
            AnimStatus st = enterState(*this, *u, f, m, *graphs_, opt_.fades, SIZE_MAX, f.ms);
            if (!st.ok()) {
                u->st = st;
                break;
            }
            for (auto& fi : in.figures)
                if (fi.figure == f.index && fi.t > 0) f.start_t = fi.t;
        }
    }
    units_[std::make_pair(in.player, in.id)] = std::move(u);
}

void Controller::start(const units::UnitScene& scene) {
    addPlayers(scene.players);
    for (auto& u : scene.units) create(u);
}

void Controller::event(const TimelineEvent& ev, uint64_t seq) {
    (void)seq;
    const auto key = std::make_pair(ev.player, ev.id);
    if (ev.kind == EventKind::Create) {
        create(ev.unit);
        return;
    }
    auto it = units_.find(key);
    if (it == units_.end()) return;   // the timeline reader refuses events for unknown units
    Unit& u = *it->second;
    if (ev.kind == EventKind::Remove) {
        units_.erase(it);
        return;
    }
    if (!u.st.ok()) return;            // a failed unit stays failed until removed or created again
    auto inSet = [&](const FigureSet& s, const Fig& f) {
        if (!f.alive) return false;
        if (s.all) return true;
        for (auto x : s.figures)
            if (x == f.index) return true;
        return false;
    };
    switch (ev.kind) {
    case EventKind::Choice:
        for (auto& f : u.figs)
            if (inSet(ev.figures, f)) u.forced.push(static_cast<int32_t>(f.index), ev.point, ev.a);
        return;
    case EventKind::Steer:
        for (auto& f : u.figs)
            if (inSet(ev.figures, f)) {
                f.ms.opts.v[7] = ev.weight;
                f.ms.opts.v[8] = static_cast<float>(ev.a);
            }
        return;
    case EventKind::Reset:
    case EventKind::Place: {
        if (ev.kind == EventKind::Place) {
            u.in.plot_x = ev.a;
            u.in.plot_y = ev.b;
            u.in.facing = static_cast<int8_t>(ev.c);
        }
        UnitInput in = u.in;
        in.figures.clear();
        in.flags = static_cast<uint8_t>(in.flags & ~units::kFlagFortified);
        create(in);   // every figure back to the default state at time 0 (replaces the unit)
        return;
    }
    case EventKind::Embark: {
        UnitInput in = u.in;
        in.figures.clear();
        in.flags = static_cast<uint8_t>(ev.a ? (in.flags | units::kFlagEmbarked) : (in.flags & ~units::kFlagEmbarked));
        create(in);   // the art switches; its figures restart in the default state
        return;
    }
    case EventKind::Garrison:
        u.in.flags = static_cast<uint8_t>(ev.a ? (u.in.flags | units::kFlagGarrisoned)
                                               : (u.in.flags & ~units::kFlagGarrisoned));
        return;
    case EventKind::Damage:
        u.in.damage = static_cast<int16_t>(ev.a);
        break;
    case EventKind::Fortify:
        u.in.flags = static_cast<uint8_t>(ev.a ? (u.in.flags | units::kFlagFortified)
                                               : (u.in.flags & ~units::kFlagFortified));
        break;
    default:
        break;
    }
    EventBridge* br = opt_.bridge ? opt_.bridge : &basic_;
    std::vector<Request> reqs;
    AnimStatus st = br->requestsFor(ev, u.in, u.art, reqs);
    if (!st.ok()) {
        u.st = st;
        return;
    }
    for (auto& r : reqs) {
        if (r.path < 0 || r.path >= kPathOptions) {
            u.st = AnimStatus::Fail(AnimCode::BadEvent, std::string(eventKindName(ev.kind)) + ": path " +
                                                            std::to_string(r.path) + " out of range");
            return;
        }
        if (r.removes) continue;   // 4406 r3: removed below, nothing requested
        for (auto& f : u.figs) {
            if (!inSet(r.figures, f)) continue;
            const Member& m = *u.members[f.member];
            // 0439 (unit-controller.md 5, 14.1): a value the figure's machine never reads on path 4 is a no-op, not a
            // failure. The EXE's handlers post their action with no test of the unit's type or machine [C: fortify
            // 0x619610, work 0x564960], and no socket of this machine reads the value, so the EXE's unit keeps its
            // state (the DLL's GameplayUnitFortify(unit, 0) at every unit's setup reaches every civilian). Not written:
            // a write could flip a not-equal test of the current value [I: whether the EXE writes it is not read].
            if (r.path == 4 && r.value != 0 && !m.p4.count(r.value)) {
                ++counts_.unread_requests;
                continue;
            }
            f.ms.opts.v[r.path] = static_cast<float>(r.value);
            u.missing.clear();   // 4770: a request starts a new occurrence (13.4)
        }
    }
    if (ev.kind == EventKind::Action && opt_.action_skip != ActionSkip::None) {   // 4406 r2: ActionSkip
        u.action_armed = true;
        u.action_seq = seq;
    }
    for (auto& r : reqs)   // 4406 r3: removed figures (premise change 5) leave the unit at once: no program, no clip
        if (r.removes)
            u.figs.erase(std::remove_if(u.figs.begin(), u.figs.end(),
                                        [&](const Fig& f) { return inSet(r.figures, f); }),
                         u.figs.end());
    for (auto& r : reqs)   // 4404: a request's figures that die (damage, X4) play its code and stay dead
        if (r.dies)
            for (auto& f : u.figs)
                if (inSet(r.figures, f)) f.alive = false;
    if (ev.kind == EventKind::Kill || ev.kind == EventKind::Destroyed) {
        for (auto& f : u.figs)
            if (ev.kind == EventKind::Destroyed || inSet(ev.figures, f)) f.alive = false;   // they play death
    }
    if (ev.kind == EventKind::Damage) revive(u);
}

// 0439: a unit whose damage falls gets back the figures the damage rule gives it again (units::livingFigures, the
// placement's 0x4B4B80 rule), lowest missing index first (the EXE revives "from the first dead one forward",
// unit-formations.md 7), each new in its member's default state at time 0 as at a creation. Only figures the
// controller removed (ActionBridge::remove_fallen) come back; a figure kept dead in its death state (dies) does not
// (14.1: a dead machine cannot be revived). The placement draws livingFigures of the current damage, so without this a
// healed unit asks the pose source for a figure the controller does not hold (UnitRefused "no pose program", issue
// 0439, the great war infantry's figure 9 of unit-server-scene.md 3.4).
void Controller::revive(Unit& u) {
    const uint32_t total = u.art.figures();
    if (total == 0 || (u.in.flags & units::kFlagDead)) return;
    uint32_t want = 0;
    if (!units::livingFigures(total, u.in.damage, u.in.max_hp, -1, want).ok()) return;
    // 4736 (W36, ControllerOptions::revive_dead): a held dead figure below the living count (a fight's Kill keeps it) is
    // dropped here and given back below as a new figure; its dead machine is not revived (14.1)
    if (opt_.revive_dead)
        u.figs.erase(std::remove_if(u.figs.begin(), u.figs.end(),
                                    [&](const controller_detail::Fig& f) { return !f.alive && f.index < want; }),
                     u.figs.end());
    std::vector<bool> held(total, false);
    for (auto& f : u.figs)
        if (f.index < total) held[f.index] = true;
    const float p4 = (u.in.flags & units::kFlagFortified) ? 1500.f : 1000.f;
    // 4736 (W36): every missing index below the living count, not only as many as the count falls short: the placement
    // draws figures 0..want-1, and a unit created while damaged holds every figure, so a later rise removes a middle
    // range and the count alone left that hole open (UnitRefused "no pose program", impl-0637, MC #9206)
    for (uint32_t index = 0; index < total && index < want; ++index) {
        if (held[index]) continue;
        uint32_t mi = 0, first = 0;
        while (mi < u.art.members.size() && index >= first + static_cast<uint32_t>(u.art.members[mi].count))
            first += static_cast<uint32_t>(u.art.members[mi++].count);
        if (mi >= u.art.members.size()) break;
        controller_detail::Fig f;
        f.member = mi;
        f.index = index;
        const Member& m = *u.members[mi];
        f.ms.state = m.machine->default_state;
        f.ms.opts.v[4] = p4;
        AnimStatus st = enterState(*this, u, f, m, *graphs_, opt_.fades, SIZE_MAX, f.ms);
        if (!st.ok()) {
            u.st = st;
            return;
        }
        u.figs.push_back(std::move(f));
        ++counts_.revived;
        u.settle_pending = true;   // 0441
    }
    std::sort(u.figs.begin(), u.figs.end(),
              [](const controller_detail::Fig& a, const controller_detail::Fig& b) { return a.index < b.index; });
}

// Graphless states choose at once, with no time (the default chooser after create, reset or embark); run after the
// frame's events, so that a choice record in the same frame reaches the creation's random draws.
void Controller::settle(Unit& u) {
    if (!u.settle_pending) return;   // 0441: every figure has its graph and no start pending: nothing to do
    for (auto& f : u.figs) {
        const Member& m = *u.members[f.member];
        AnimStatus st;
        for (size_t n = 0; st.ok() && !m.graph[f.ms.state] && n < kMaxChain; ++n) {
            FigHost host(u, f, *m.machine, opt_.forced);
            const size_t from = f.ms.state;
            const MachineState prior = f.ms;   // 4770: kept when the target's clip is missing
            int taken = -1;
            st = updateMachine(*m.machine, f.ms, host, taken);
            if (!st.ok() || taken < 0) break;
            st = enterState(*this, u, f, m, *graphs_, opt_.fades, from, prior);
        }
        // 4406 round 2: the start request (a parked air figure, 1000) after the chooser's own write; the machine
        // follows it at once, with no time, until no socket holds (airplane: 1 -> 8 -> 7, the idle).
        if (st.ok() && f.start_p4 != 0) {
            f.ms.opts.v[4] = static_cast<float>(f.start_p4);
            f.start_p4 = 0;
            for (size_t n = 0; st.ok() && n < kMaxChain; ++n) {
                FigHost host(u, f, *m.machine, opt_.forced);
                const size_t from = f.ms.state;
                const MachineState prior = f.ms;   // 4770: kept when the target's clip is missing
                int taken = -1;
                st = updateMachine(*m.machine, f.ms, host, taken);
                if (!st.ok() || taken < 0) break;
                st = enterState(*this, u, f, m, *graphs_, opt_.fades, from, prior);
            }
        }
        if (st.ok() && !f.cur)
            st = AnimStatus::Fail(AnimCode::NoProgress, m.machine->file + ": state " +
                                                            std::to_string(m.machine->fsm.states[f.ms.state].id) +
                                                            " reaches no state with a graph");
        if (!st.ok()) {
            u.st = st;
            return;
        }
        if (f.start_t > 0) {
            f.cur->advance(f.start_t, f.ms.opts);
            f.start_t = 0;
        }
    }
    // 0441: still pending while a figure stays in a state without a graph (its chain took nothing this frame)
    u.settle_pending = false;
    for (auto& f : u.figs)
        if (!u.members[f.member]->graph[f.ms.state]) u.settle_pending = true;
}

void Controller::step(Unit& u, uint64_t seq, int steps, double h) {
    // 0441: per-figure step counts only where a rule can change them (ActionSkip, skips, a hook); else every figure
    // takes `steps` and no vector is allocated
    std::vector<int>& take = take_;
    // 4406 r2: ActionSkip: the whole unit takes no step on the order's frame (OrderFrame) or on the first frame after
    // it (NextFrame); a frame with no step at all (dt 0) does not consume it
    bool actionSkip = false;
    if (u.action_armed && steps > 0) {
        if (opt_.action_skip == ActionSkip::OrderFrame && seq == u.action_seq) actionSkip = true;
        if (opt_.action_skip == ActionSkip::NextFrame && seq != u.action_seq) actionSkip = true;
        if (actionSkip || opt_.action_skip == ActionSkip::OrderFrame) u.action_armed = false;
    }
    const bool uniform = !actionSkip && opt_.skips.empty() && !opt_.hook;
    if (!uniform) take.assign(u.figs.size(), steps);
    for (size_t i = 0; i < u.figs.size() && !uniform; ++i) {
        if (actionSkip) take[i] = 0;
        for (auto& r : opt_.skips)
            if (r.seq == seq && r.player == u.in.player && r.id == u.in.id &&
                (r.figure < 0 || r.figure == static_cast<int32_t>(u.figs[i].index)))
                take[i] -= r.steps;
        if (take[i] < 0) take[i] = 0;
        if (opt_.hook) {
            int n = opt_.hook->steps(seq, u.in.player, u.in.id, static_cast<int32_t>(u.figs[i].index), take[i]);
            take[i] = std::max(0, std::min(n, take[i]));
        }
    }
    std::vector<ActiveSource> act;
    // 4737: the unit's living figures for the triggers' tier (exe-audio.md 13: the EXE counts the unit's figures at
    // the trigger's handling; here the figures held and not dead when this frame's steps begin [I: alive only, lead-0042
    // #9324]). Deaths and removals come with the frame's events, before the steps, so one count serves the frame.
    int32_t living = 0;
    for (const auto& f : u.figs)
        if (f.alive) ++living;
    for (int k = 0; k < steps && u.st.ok(); ++k) {
        for (size_t i = 0; i < u.figs.size() && u.st.ok(); ++i) {
            if (!uniform && k >= take[i]) continue;
            Fig& f = u.figs[i];
            const Member& m = *u.members[f.member];
            if (f.cur && f.cur_state == f.ms.state) {
                f.cur->advance(static_cast<float>(h), f.ms.opts);
            }
            // 4468: the trigger walk only for a unit drawn this frame (0450: only drawn units are stepped at all)
            if (f.cur && f.cur_state == f.ms.state && !m.triggers.empty()) {
                act.clear();
                f.cur->activeTimes(f.ms.opts, act);   // 0441: the walk reads ec and times only
                for (auto& s : act)
                    for (size_t ti = 0; ti < m.triggers.size(); ++ti)
                        for (auto& t : m.triggers[ti]->triggers) {
                            if (t.ec != s.ec || !crossed(t.time, s)) continue;
                            TriggerEvent e;
                            e.player = u.in.player;
                            e.id = u.in.id;
                            e.figure = f.index;
                            e.instance = f.instance;   // 4782
                            e.type = t.type;
                            e.ec = t.ec;
                            e.trigger = t.id;
                            e.time = t.time;
                            float into = t.time >= s.prev_t ? t.time - s.prev_t : 0.f;
                            e.offset = static_cast<float>(k * h) + std::min(into, static_cast<float>(h));
                            auto ev = t.attrs.find("event");
                            if (ev != t.attrs.end()) e.event = ev->second;
                            auto bo = t.attrs.find("bone");
                            if (bo != t.attrs.end()) e.bone = bo->second;
                            e.duration = t.duration;   // 4722
                            if (ti < m.trigger_files.size()) e.file = m.trigger_files[ti];
                            // 4737: the reader keeps 0 for an absent tier; the EXE's default is -1 (always plays)
                            e.tier = t.attrs.count("tier") ? t.tier : -1;
                            e.unit_figures = living;
                            triggers_.push_back(e);
                        }
            }
            f.ms.time += h;
            if (f.fading) {
                f.fade_t += h;
                if (f.fade_t >= f.fade_d - 1e-9) {
                    f.fading = false;
                    f.frozen.clear();
                }
            }
            AnimStatus st;
            for (size_t n = 0; n < kMaxChain; ++n) {
                FigHost host(u, f, *m.machine, opt_.forced);
                const size_t from = f.ms.state;
                const MachineState prior = f.ms;   // 4770: kept when the target's clip is missing
                int taken = -1;
                st = updateMachine(*m.machine, f.ms, host, taken);
                if (!st.ok() || taken < 0) break;
                st = enterState(*this, u, f, m, *graphs_, opt_.fades, from, prior);
                if (!st.ok() || m.graph[f.ms.state]) break;   // only graphless states choose again at once
            }
            if (!st.ok()) u.st = st;
            if (!m.graph[f.ms.state]) u.settle_pending = true;   // 0441: left without a graph: settle next frame
        }
    }
}

void Controller::collect(const std::set<UnitKey>* drawn) {
    programs_.clear();
    statuses_.clear();
    for (auto& kv : units_) {
        Unit& u = *kv.second;
        if (u.st.ok() && drawn && !drawn->count(kv.first)) {
            // 4468: not drawn: no program kept; the failures below are those of the drawn path, found on the same frame
            for (auto& f : u.figs) {
                const BoundMachine& bm = *u.members[f.member]->machine;
                if (!f.cur) {
                    u.st = AnimStatus::Fail(AnimCode::NoProgress, bm.file + ": figure " + std::to_string(f.index) +
                                                                     " has no graph in state " +
                                                                     std::to_string(bm.fsm.states[f.ms.state].id));
                    break;
                }
                if (!f.cur->programMayFail()) continue;
                ++counts_.quiet_programs;
                PoseProgram p;
                AnimStatus st = f.cur->program(f.ms.opts, p);
                if (!st.ok()) {
                    u.st = st;
                    break;
                }
            }
        } else if (u.st.ok()) {
            std::vector<FigureProgram> mine;
            for (auto& f : u.figs) {
                FigureProgram fp;
                fp.player = u.in.player;
                fp.id = u.in.id;
                fp.figure = f.index;
                fp.instance = f.instance;   // 4782
                fp.member = f.member;
                const BoundMachine& bm = *u.members[f.member]->machine;
                fp.state = bm.fsm.states[f.ms.state].id;
                if (!f.cur) {
                    u.st = AnimStatus::Fail(AnimCode::NoProgress, bm.file + ": figure " + std::to_string(f.index) +
                                                                     " has no graph in state " +
                                                                     std::to_string(fp.state));
                    break;
                }
                PoseProgram p;
                AnimStatus st = f.cur->program(f.ms.opts, p);
                if (!st.ok()) {
                    u.st = st;
                    break;
                }
                if (f.fading && !f.frozen.nodes.empty() && !p.nodes.empty()) {
                    PoseProgram c = f.frozen;
                    const int32_t base = static_cast<int32_t>(c.nodes.size());
                    const int32_t mbase = static_cast<int32_t>(c.masks.size());
                    for (auto n : p.nodes) {
                        if (n.kind == PoseNode::Blend) {
                            n.a += base;
                            n.b += base;
                            if (n.mask >= 0) n.mask += mbase;
                        }
                        c.nodes.push_back(n);
                    }
                    for (auto& mk : p.masks) c.masks.push_back(mk);
                    PoseNode bl;
                    bl.kind = PoseNode::Blend;
                    bl.a = base - 1;
                    bl.b = static_cast<int32_t>(c.nodes.size()) - 1;
                    bl.w = static_cast<float>(std::min(1.0, f.fade_t / f.fade_d));
                    c.nodes.push_back(bl);
                    p = c;
                }
                fp.program = std::move(p);   // 0441: moved, not copied (twice)
                mine.push_back(std::move(fp));
            }
            if (u.st.ok())
                for (auto& fp : mine) programs_.push_back(std::move(fp));
        }
        UnitAnimStatus s;
        s.player = u.in.player;
        s.id = u.in.id;
        s.status = u.st;
        statuses_.push_back(s);
    }
}

AnimStatus Controller::apply(const TimelineFrame& frame) { return apply(frame, nullptr); }

AnimStatus Controller::apply(const TimelineFrame& frame, const std::set<UnitKey>* drawn) {
    triggers_.clear();
    counts_ = ApplyCounts();
    if (!isOpen()) return AnimStatus::Fail(AnimCode::BadEvent, "the controller is not open");
    if (!std::isfinite(frame.dt) || frame.dt < 0)
        return AnimStatus::Fail(AnimCode::BadEvent, "frame " + std::to_string(frame.seq) + ": dt not finite or negative");
    for (auto& ev : frame.events) event(ev, frame.seq);
    for (auto& kv : units_) {
        Unit& u = *kv.second;
        const bool shown = !drawn || drawn->count(kv.first) != 0;
        ++(shown ? counts_.drawn : counts_.quiet);
        if (!u.st.ok()) continue;
        settle(u);
        if (!u.st.ok()) continue;
        // 0450 (unit-controller.md 18, settled by 0442 [L]): the EXE does not advance a unit it does not draw; its clock
        // stands and goes on from there when it is drawn again. A unit not drawn this frame takes the frame's events and
        // its settle (above) but no clock time, no step and no machine update: as if the frame's dt were 0 for it.
        if (!shown) continue;
        int steps = 0;
        double len = opt_.clock.h;
        AnimStatus st;
        auto r = rates_.empty() ? rates_.end() : rates_.find(kv.first);
        if (r == rates_.end()) {
            st = u.clock.take(frame.dt, opt_.clock, steps, len);
        } else {
            // 0796: a unit of a fight played faster takes dt x rate, its per-frame cap scaled the same (the same real
            // time per frame is dropped on a hitch as at 1x)
            ClockParams cp = opt_.clock;
            cp.max_steps = (int)std::ceil(cp.max_steps * r->second);
            cp.max_dt *= r->second;
            st = u.clock.take(frame.dt * r->second, cp, steps, len);
        }
        if (!st.ok()) {
            u.st = st;
            continue;
        }
        step(u, frame.seq, steps, len);
    }
    collect(drawn);
    return AnimStatus::Ok();
}

// ---------------------------------------------------------------------------------------------- pre-check
AnimStatus Controller::checkArt(const units::UnitArt& art) {
    if (!isOpen()) return AnimStatus::Fail(AnimCode::BadEvent, "the controller is not open");
    for (auto& ma : art.members) {
        const Member* m = nullptr;
        AnimStatus st = member(ma, m);
        if (!st.ok()) return st;
        const BoundMachine& bm = *m->machine;
        std::vector<bool> seen(bm.fsm.states.size(), false);
        std::vector<size_t> todo(1, bm.default_state);
        seen[bm.default_state] = true;
        std::vector<const c5m::DagFile*> reached;   // 4410: the reachable graphs, for the track masks
        std::vector<std::string> reached_names;
        while (!todo.empty()) {
            size_t s = todo.back();
            todo.pop_back();
            if (m->graph[s]) {
                reached.push_back(m->graph[s]);
                reached_names.push_back(m->graph_name[s]);
                std::unique_ptr<GraphInstance> g;
                st = graphs_->bind(*m->graph[s], m->ecs, m->graph_name[s], g);
                if (!st.ok()) return AnimStatus::Fail(st.code, ma.member + " state " +
                                                                   std::to_string(bm.fsm.states[s].id) + ": " + st.text);
            } else if (bm.out[s].empty()) {
                return AnimStatus::Fail(AnimCode::NoProgress, ma.member + ": " + bm.file + " state " +
                                                                  std::to_string(bm.fsm.states[s].id) +
                                                                  " has no graph and no socket out");
            }
            for (size_t so : bm.out[s])
                if (!seen[bm.to[so]]) {
                    seen[bm.to[so]] = true;
                    todo.push_back(bm.to[so]);
                }
        }
        // 4410: track masks named by a reachable graph must be carried by the model's bones (13.3)
        bool masked = false;
        for (const c5m::DagFile* g : reached)
            for (const c5m::DagGraph& d : g->dags)
                for (const std::string& b : d.track_masks)
                    if (!b.empty()) masked = true;
        if (masked) {
            c5m::FileInfo fi;
            std::string why;
            bool read = false;
            if (opt_.model_reader) {
                read = opt_.model_reader(ma, fi, why);
            } else {
                const std::string gr2 = c5m::basename_lower(ma.files.mesh.name);
                c5m::Bytes bytes;
                std::string origin;
                c5m::Error e;
                if (gr2.empty() || !files_->read(gr2, bytes, &origin)) {
                    why = "model " + (gr2.empty() ? ma.model : gr2) + " not found";
                } else if (!c5m::read_gr2(bytes.data(), bytes.size(), opt_.strings, fi, &e)) {
                    why = gr2 + " (" + origin + "): " + c5m::err_name(e.code) + ": " + e.what;
                } else {
                    read = true;
                }
            }
            if (!read) return AnimStatus::Fail(AnimCode::TrackMaskNotBound, ma.member + ": model not read: " + why);
            st = checkTrackMasks(fi, reached, reached_names);
            if (!st.ok()) return AnimStatus::Fail(st.code, ma.member + ": " + st.text);
        }
    }
    return AnimStatus::Ok();
}

std::vector<ArtCheck> Controller::precheck(const std::vector<units::UnitArt>& arts) {
    std::vector<ArtCheck> out;
    for (auto& a : arts) {
        ArtCheck c;
        c.art_row = a.art_row;
        c.status = checkArt(a);
        out.push_back(c);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- 4775: combat entry
namespace {

class FnCombatClips : public CombatClips {
public:
    using Fn = std::function<ActionTiming(int32_t, int32_t, const CombatMemberArt&, int32_t)>;
    explicit FnCombatClips(Fn f) : f_(std::move(f)) {}
    ActionTiming timing(int32_t player, int32_t id, const CombatMemberArt& m, int32_t code) override {
        return f_(player, id, m, code);
    }
private:
    Fn f_;
};

}  // namespace

AnimStatus Controller::combatFigures(int32_t player, int32_t id, const CombatArtTables& tables,
                                     CombatUnitFigures& out) const {
    out = CombatUnitFigures();
    auto it = units_.find(std::make_pair(player, id));
    if (it == units_.end())
        return AnimStatus::Fail(AnimCode::BadEvent, "combat: unit " + std::to_string(player) + "/" +
                                                        std::to_string(id) + " is not held");
    const Unit& u = *it->second;
    if (!u.st.ok())
        return AnimStatus::Fail(AnimCode::BadEvent, "combat: unit " + std::to_string(player) + "/" +
                                                        std::to_string(id) + " failed: " + name(u.st.code) + ": " +
                                                        u.st.text);
    out.player = player;
    out.id = id;
    for (size_t mi = 0; mi < u.art.members.size(); ++mi) {
        const units::MemberArt& ma = u.art.members[mi];
        CombatMemberArt m;
        m.member = ma.member;
        m.count = ma.count;
        m.domain = ma.domain;
        auto row = tables.find(ma.member);
        if (row != tables.end()) {
            m.enable = row->second.enable;
            m.rush = row->second.rush;
            m.last_to_die = row->second.last_to_die;
            m.weapons = row->second.weapons;
            m.motion = row->second.motion;   // 4776: the member's motion columns
        }
        m.victory = mi < u.members.size() && u.members[mi] && u.members[mi]->p4.count(1800) != 0;
        // 4726 (unit-combat-server.md 12.5): whether the member's machine takes the anti-air request 1130 (0321 13.5:
        // ships_state_01 and the modern-ship machines do, the submarines' 1_omni_state_01 does not); -1 without a machine
        if (mi < u.members.size() && u.members[mi] && u.members[mi]->machine)
            m.anti_air = u.members[mi]->p4.count(1130) != 0 ? 1 : 0;
        out.members.push_back(m);
    }
    for (auto& f : u.figs)
        if (f.alive) out.standing.push_back(static_cast<uint16_t>(f.index));
    std::sort(out.standing.begin(), out.standing.end());
    // 4744: the figures the placement draws (0 .. living-1 of the unit's damage; a fighter's damage op waits behind its
    // fight, unit-combat-server.md 6.1, so this is the damage before it); CombatParams::fight_drawn_figures reads it
    uint32_t living = 0;
    if (units::livingFigures(u.art.figures(), u.in.damage, u.in.max_hp, u.in.alive, living).ok()) out.drawn = living;
    return AnimStatus::Ok();
}

std::unique_ptr<CombatClips> Controller::combatClips(std::function<float(const std::string& clip)> duration) const {
    const Controller* self = this;
    return std::unique_ptr<CombatClips>(new FnCombatClips(
        [self, duration](int32_t player, int32_t id, const CombatMemberArt& cm, int32_t code) {
            ActionTiming t;
            auto it = self->units_.find(std::make_pair(player, id));
            if (it == self->units_.end()) {
                t.why = "unit " + std::to_string(player) + "/" + std::to_string(id) + " is not held";
                return t;
            }
            const Unit& u = *it->second;
            const Member* m = nullptr;
            for (size_t mi = 0; mi < u.art.members.size() && !m; ++mi)
                if (u.art.members[mi].member == cm.member && mi < u.members.size()) m = u.members[mi];
            if (!m) {
                t.why = cm.member + ": not a member of " + std::to_string(player) + "/" + std::to_string(id);
                return t;
            }
            std::string clip;
            bool found = false, named = false;
            for (size_t i = 0; i < m->ecs.ecs.size() && !named; ++i)
                if (m->ecs.ecs[i] == code) {
                    named = true;
                    clip = m->ecs.clips[i];
                    found = i >= m->ec_found.size() || m->ec_found[i];
                }
            if (!named) {
                t.why = cm.member + ": its .fxsxml binds no clip to ec " + std::to_string(code);
                return t;
            }
            if (!found) {   // 4770: named by the .fxsxml, not shipped
                t.why = std::string(kMissingInInstall) + cm.member + " ec " + std::to_string(code) + " " + clip;
                return t;
            }
            const float d = duration ? duration(clip) : 0.f;
            if (!(d > 0)) {
                t.why = cm.member + " ec " + std::to_string(code) + " " + clip + ": no duration";
                return t;
            }
            t.found = true;
            t.duration = d;
            for (auto* ts : m->triggers)
                for (auto& tr : ts->triggers)
                    if (tr.ec == code && tr.type == c5m::TriggerType::Action) {
                        t.hits.push_back(tr.time);
                        t.hit_ids.push_back(tr.id);
                    }
            if (t.hits.empty()) {
                for (auto* ts : m->triggers)
                    for (auto& tr : ts->triggers)
                        if (tr.ec == code && tr.type == c5m::TriggerType::Transfer) {
                            t.hits.push_back(tr.time);
                            t.hit_ids.push_back(tr.id);
                        }
                t.transfer = !t.hits.empty();
            }
            return t;   // choreograph() sorts the hits with their ids
        }));
}

AnimStatus Controller::combat(const CombatInput& in, const CombatArtTables& tables, CombatClips& clips,
                              const CombatParams& p, CombatResult& out) const {
    out = CombatResult();
    CombatUnitFigures a, d;
    if (in.attacker.unit >= 0) {
        AnimStatus st = combatFigures(in.attacker.owner, in.attacker.unit, tables, a);
        if (!st.ok()) return st;
    }
    if (in.defender.unit >= 0) {
        AnimStatus st = combatFigures(in.defender.owner, in.defender.unit, tables, d);
        if (!st.ok()) return st;
    }
    out = choreograph(in, a, d, clips, p);
    return AnimStatus::Ok();
}

bool Controller::accepts(int32_t player, int32_t id, uint16_t figure, int32_t code) const {
    auto it = units_.find(std::make_pair(player, id));
    if (it == units_.end() || !it->second->st.ok()) return false;
    const Unit& u = *it->second;
    for (const auto& f : u.figs) {
        if (f.index != figure) continue;
        if (!f.alive || f.member >= u.members.size() || !u.members[f.member]) return false;
        const BoundMachine& bm = *u.members[f.member]->machine;
        if (f.ms.state >= bm.out.size()) return false;
        for (size_t so : bm.out[f.ms.state])
            for (const auto& c : bm.fsm.sockets[so].conditions)
                if (c.type == c5m::CondType::CheckPathOption && c.path == 4 && c.method == 1 && c.value == code)
                    return true;
        return false;
    }
    return false;
}

bool Controller::playing(int32_t player, int32_t id, uint16_t figure, int32_t code) const {
    auto it = units_.find(std::make_pair(player, id));
    if (it == units_.end() || !it->second->st.ok()) return false;
    for (const auto& f : it->second->figs) {
        if (f.index != figure) continue;
        if (!f.alive || !f.cur || f.cur_state != f.ms.state) return false;
        std::vector<ActiveSource> act;
        f.cur->activeTimes(f.ms.opts, act);
        for (const auto& s : act)
            if (s.ec == code || s.ec == code + 1000) return true;
        return false;
    }
    return false;
}

int32_t Controller::playingVariant(int32_t player, int32_t id, uint16_t figure, int32_t code) const {
    auto it = units_.find(std::make_pair(player, id));
    if (it == units_.end() || !it->second->st.ok()) return 0;
    for (const auto& f : it->second->figs) {
        if (f.index != figure) continue;
        if (!f.cur || f.cur_state != f.ms.state) return 0;   // 5053: no alive test, a dying figure plays its death
        std::vector<ActiveSource> act;
        f.cur->activeTimes(f.ms.opts, act);
        for (const auto& s : act)
            if (s.ec == code + 1000) return code + 1000;
        for (const auto& s : act)
            if (s.ec == code) return code;
        return 0;
    }
    return 0;
}

}}  // namespace c5r::unitanim
