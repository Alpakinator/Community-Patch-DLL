// c5r/unitanim/controller.h - the unit animation controller's entry (epic 0374, issue 4403; research/05-graphics-
// composition/unit-controller.md 1, 4, 5, 9 and 13.1).
//
// open() binds the controller to the player's files, an art source (0041's art resolution) and a graph factory (the
// DAG evaluator, 4409's bindGraph); apply(frame) applies the frame's events in order, then advances every unit's clock
// by the step rule (clock.h) and, per step, each living figure's graph and machine; then programs() holds each living
// figure's pose program and triggers() the timed triggers crossed during the frame. A unit the controller cannot
// advance gets a named status and no program (D118: never a default clip). Nothing throws.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "c5models/art.h"
#include "c5models/gr2.h"
#include "c5models/logic.h"
#include "c5r/unitanim/clock.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/graph.h"
#include "c5r/unitanim/logic_cache.h"
#include "c5r/unitanim/machine.h"
#include "c5r/unitanim/pose_program.h"
#include "c5r/unitanim/random.h"
#include "c5r/unitanim/status.h"
#include "c5r/unitanim/timeline.h"
#include "c5r/units/art.h"
#include "c5r/units/unit_scene.h"

namespace c5r { namespace unitanim {

// ---------------------------------------------------------------------------------------------- what open() takes
// The unit's art: UnitInput (art row, or unit type + owner) -> members and their files (0041's L0).
class ArtSource {
public:
    virtual ~ArtSource() = default;
    // UnitFailed with the art step's message when it refuses.
    virtual AnimStatus resolve(const units::UnitInput& u, const units::PlayerInput* owner, units::UnitArt& out) = 0;
};
// 0041's ArtResolver as an ArtSource (art_row given, or art_row(unit_type, owner's style and era), then resolve).
class ResolverArtSource : public ArtSource {
public:
    explicit ResolverArtSource(units::ArtResolver& r) : r_(r) {}
    AnimStatus resolve(const units::UnitInput& u, const units::PlayerInput* owner, units::UnitArt& out) override;
private:
    units::ArtResolver& r_;
};

// The DAG evaluator (4409): binds a graph file to a member. The product passes one that calls bindGraph (graph.h).
class GraphFactory {
public:
    virtual ~GraphFactory() = default;
    virtual AnimStatus bind(const c5m::DagFile& file, const EcMap& ecs, const std::string& file_name,
                            std::unique_ptr<GraphInstance>& out) = 0;
    // 4738 (additive): the clip's duration in seconds (0: it could not be read), the threshold of the EXE's
    // ECComplete; a negative answer (the default: a factory that knows no durations, the test doubles) keeps the
    // controller's earlier reading of ECComplete, the walk of the current graph (unit-controller.md 13.1).
    virtual float clipDuration(const std::string& clip) {
        (void)clip;
        return -1.0f;
    }
};

// An EXE-level cross-fade between two states (X1, 9; MC #2023): data per transition kind, never guessed. Entering
// `to_state` from `from_state` of `machine`, the figure's last program is frozen and the new state's program fades in
// linearly over `duration` seconds of clock steps.
struct CrossFadeRule {
    std::string machine;            // lower-case file name
    int32_t from_state = -1, to_state = -1;
    float duration = 0;
};
// The default rules: none (4406, lead-0374 2026-11-15 22:59: the graphs carry the cross-fades; the omni 6 -> 3 rule of
// 4403 doubled the fidget graph's own fade, unit-controller.md 14.2 and 15). Rules stay data a caller may pass.
std::vector<CrossFadeRule> defaultCrossFades();

// Path requests an event makes (the bridge, 4404, owns the mapping; unit-controller.md 1 and 6.5).
struct Request {
    FigureSet figures;              // all = every living figure
    int32_t path = 4;
    int32_t value = 0;
    bool dies = false;              // 4404 (additive): the figures of the set are marked dead (a damage death, X4)
    bool removes = false;           // 4406 r3 (additive): the figures of the set are removed at once, no code played
                                    // (X4 as 4405 B's Q7 shows it; premise change 5, #2141); path / value unused
};
class EventBridge {
public:
    virtual ~EventBridge() = default;
    // Requests for a request-kind event (Fortify, Move, Turn, Arrive, Action, Damage, Kill, Destroyed, Embark,
    // Garrison, Visibility). BadEvent with a text when the unit's machine cannot take it.
    virtual AnimStatus requests(const TimelineEvent& ev, const units::UnitInput& unit, std::vector<Request>& out) = 0;
    // 4404 (additive): the same with the unit's resolved art (members, counts), which the controller passes; a bridge
    // that checks members (bridge.h's ActionBridge: EnableActions, the figures a damage takes out) overrides it.
    virtual AnimStatus requestsFor(const TimelineEvent& ev, const units::UnitInput& unit, const units::UnitArt& art,
                                   std::vector<Request>& out) {
        (void)art;
        return requests(ev, unit, out);
    }
};
// Section 1's codes as far as the files fix them (6.5), until 4404's bridge: fortify 1500 / 1000, move 1400, arrive
// 1000, destroyed 1200 (all), kill 1200 (the set), action attack 1100, attack_city 1160, bombard 1180, charge 1120,
// attack_charge 1140, victory 1800, bombard_defend 1280, activate 1990, captured 1195, run 1400, combat_ready 1600,
// idle 1000 (4775); work and paradrop: BadEvent
// (no code in the files); turn, damage, embark, garrison, visibility: no request.
class BasicBridge : public EventBridge {
public:
    AnimStatus requests(const TimelineEvent& ev, const units::UnitInput& unit, std::vector<Request>& out) override;
};

struct ControllerOptions {
    ClockParams clock;
    uint64_t seed = 0;              // the timeline's scene seed
    bool forced = false;            // forced mode: EventRandomPathOption needs a choice record (ForcedChoiceMissing)
    std::vector<CrossFadeRule> fades = defaultCrossFades();
    std::vector<SkipRule> skips;    // K6's skips as data
    ActionSkip action_skip = ActionSkip::None;   // 4406 r2: the action-request skip rule as data (clock.h)
    StepHook* hook = nullptr;       // not owned
    EventBridge* bridge = nullptr;  // not owned; null = BasicBridge
    // 4736 (additive): a fall in damage also replaces a held DEAD figure (a fight's Kill keeps it) below the living count
    // by a new one in its member's default state, as the EXE revives "from the first dead one forward"
    // (unit-formations.md 7); the dead machine itself stays dead (unit-controller.md 14.1). Off: dead figures stay
    // (the dies mode). The server sets it beside ActionBridge::remove_fallen.
    bool revive_dead = false;
    // 4410 (additive): the pre-check's track masks (unit-controller.md 13.3). A member whose reachable graphs name a
    // track mask has its model read (the .fxsxml's mesh, through the controller's files, with `strings`; not owned)
    // and every binding checked against its skeletons (checkTrackMasks): TrackMaskNotBound when no bone carries it.
    // The unit string databases are needed to read the bones' mask names. `model_reader`, when set, replaces the read
    // (tests; a caller with its own model cache): false with a reason refuses the member (TrackMaskNotBound).
    const c5m::StringDatabase* strings = nullptr;
    std::function<bool(const units::MemberArt&, c5m::FileInfo&, std::string&)> model_reader;
};

// ---------------------------------------------------------------------------------------------- what apply() gives
struct FigureProgram {
    int32_t player = -1, id = -1;
    uint32_t figure = 0;            // member order (0041 P8)
    uint32_t member = 0;            // index into the unit's art members
    int32_t state = -1;             // the machine's state id
    PoseProgram program;
    // 4782 (additive): the figure's clip instance, a new id (unique per controller) each time it enters a state with a
    // graph; a graphless chooser keeps it. The fight clock's OwnerEnd per clip instance and the trigger stream key on it.
    uint64_t instance = 0;
};

struct TriggerEvent {
    int32_t player = -1, id = -1;
    uint32_t figure = 0;
    c5m::TriggerType type = c5m::TriggerType::Unknown;
    int32_t ec = 0, trigger = -1;   // the trigger's id
    float time = 0;                 // in the clip
    float offset = 0;               // seconds into the frame's steps when it was crossed
    std::string event, bone;        // attributes as written ("" when absent)
    uint64_t instance = 0;          // 4782 (additive): the clip instance that fired it (FigureProgram::instance)
    float duration = 0;             // 4722 (additive): the trigger's duration attribute (an Effect's life, s; 0 = none)
    std::string file;               // 4722 (additive): its trigger file, as the member's art names it
    // 4737 (additive; research/02-exe-internals/exe-audio.md 13): the trigger's `tier` attribute (-1 when the file has
    // none, as the EXE's reader defaults it) and the unit's living figures (held and not dead) at the step that crossed
    // it, from which the EXE's Sound-trigger handler derives the unit's tier (<= 3 -> 0, 4-8 -> 1, 9+ -> 2).
    int32_t tier = -1;
    int32_t unit_figures = -1;      // -1 = not known
};

struct UnitAnimStatus {
    int32_t player = -1, id = -1;
    AnimStatus status;
};

// One art row's logic, for the pre-check (DoD F): every member's machine, every graph a reachable state names, every
// trigger file; statuses as 5 (the first that applies).
struct ArtCheck {
    std::string art_row;
    AnimStatus status;
};

class Controller {
public:
    Controller();
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // Not owned; they outlive the controller.
    void open(c5m::FileProvider& files, ArtSource& art, GraphFactory& graphs, const ControllerOptions& opt);
    bool isOpen() const { return files_ != nullptr; }
    LogicCache* logic() { return cache_.get(); }   // after open(): to give parsed files directly (putMachine, ...)

    // Players (for art resolution) and the start scene's units (created before the first frame; a unit's figure
    // lines give its figures' start: their default state's graph advanced by t).
    void addPlayers(const std::vector<units::PlayerInput>& players);
    void start(const units::UnitScene& scene);

    // One frame. Returns BadEvent only for a frame-level fault (not opened, a non-finite or negative dt); per-unit
    // failures are in statuses().
    AnimStatus apply(const TimelineFrame& frame);
    // 4468 (additive; epic 0375, unit-controller.md 18): the same with the frame's drawn set, (player, id) of the units
    // the caller draws this frame; null = every unit is drawn (apply(frame)). Every unit takes the frame's events in
    // order and its settle. 0450 (the EXE's rule, settled by 0442 [L]): only a drawn unit takes its clock and its steps
    // (graph advance, state time, fade, machine updates with their random draws); a unit not drawn holds its clock, as
    // if the frame's dt were 0 for it, and goes on from there when it is drawn again (4468 advanced every unit). The
    // outputs: programs() holds the drawn units' figures only, triggers() the triggers the drawn units crossed only, and
    // a unit that is not drawn is not given a pose program, except where its graph's program can fail
    // (GraphInstance::programMayFail), which is then built and dropped so that a failure the events or the settle made
    // lands on the frame it happened. statuses() still lists every unit. So a unit's programs on the frames it is drawn
    // are those it has when it is drawn on every frame of a timeline whose dt is 0 on the frames it was not drawn.
    using UnitKey = std::pair<int32_t, int32_t>;
    AnimStatus apply(const TimelineFrame& frame, const std::set<UnitKey>* drawn);
    // 4468: the work of the last apply(): units drawn (stepped, with their outputs) and not drawn (quiet: 0450 no clock
    // and no step, only events and settle), programs built for units not drawn (graphs whose program can fail).
    struct ApplyCounts {
        size_t drawn = 0, quiet = 0, quiet_programs = 0;
        // 0439: path-4 requests a figure's machine never reads (no CheckPathOption on path 4 with that value, equal or
        // not equal) taken as no-ops, per figure; figures given back when a unit's damage falls (remove_fallen mode)
        size_t unread_requests = 0, revived = 0;
    };
    const ApplyCounts& lastApply() const { return counts_; }
    // 0796 (additive; quick_combat.h): a unit's clip clock runs at `rate` x the frame's dt (a fight played faster); 1, a
    // rate not above 0 or no entry: real time, the rule before 0796. clearRates() drops every entry.
    void setRate(const UnitKey& k, double rate) { if (rate > 0 && rate != 1.0) rates_[k] = rate; else rates_.erase(k); }
    void clearRates() { rates_.clear(); }

    const std::vector<FigureProgram>& programs() const { return programs_; }
    const std::vector<TriggerEvent>& triggers() const { return triggers_; }
    const std::vector<UnitAnimStatus>& statuses() const { return statuses_; }
    const AnimStatus* status(int32_t player, int32_t id) const;
    size_t units() const;
    // 4404 (additive): the unit's input as the events have updated it (plot, facing, damage, flags), for the driver's
    // .c5units frames; nullptr for a unit the controller does not hold.
    const units::UnitInput* unitInput(int32_t player, int32_t id) const;

    // 0470 (additive; unit-controller.md 19.7): the bound graph instances the held units' figures keep (one per figure
    // and state entered) and their memory by GraphInstance::footprint: `own` summed over the instances, `shared` once per
    // shared block. member_states: (member, state) pairs with a graph that some figure entered; files: distinct graph
    // files among them.
    struct GraphCensus {
        size_t figures = 0, instances = 0, member_states = 0, files = 0, shared_blocks = 0;
        size_t own[GraphFootprint::kParts] = {0, 0, 0, 0};
        size_t shared[GraphFootprint::kParts] = {0, 0, 0, 0};
    };
    GraphCensus graphCensus() const;

    // 4775 (additive; combat.h, unit-combat-server.md "Choreography as built (4775)"): the combat entry.
    // combatFigures: a held unit's members as the choreography needs them (EnableActions, rush, LastToDie and weapons
    // from `tables` by member Type, a member without a row has none and cannot attack; domain from the art; victory =
    // the member's machine reads 1800 on path 4; 4726: anti_air = the machine reads 1130 on path 4, 1 or 0) and its living
    // figures. BadEvent when the unit is not held or failed.
    AnimStatus combatFigures(int32_t player, int32_t id, const CombatArtTables& tables, CombatUnitFigures& out) const;
    // The held members' clip timings for the choreography: the code's ec bound by the member's .fxsxml; found false
    // when it binds none or the art step did not find its clip (4770's missing in install); the duration from
    // `duration` (the clip file's length, the caller's: ClipGraphFactory::readDurations in the driver; <= 0 = found
    // false); the hits: the Action triggers on that ec in the member's trigger files, else its Transfer triggers.
    // The object refers to this controller; valid while the controller is open and holds the unit.
    std::unique_ptr<CombatClips> combatClips(std::function<float(const std::string& clip)> duration) const;
    // choreograph() on the record's held units (a city side takes no unit). BadEvent when a unit side is not held or
    // failed; otherwise Ok, and `out` carries the choreography's own status (a refusal there is not the controller's).
    AnimStatus combat(const CombatInput& in, const CombatArtTables& tables, CombatClips& clips, const CombatParams& p,
                      CombatResult& out) const;
    // 4782 (additive): the live feedback of a CombatRun (combat.h): true when the figure's machine, in its current state,
    // has a socket that tests path 4 equal to `code` (its machine would take the request now). False for a figure or a
    // unit the controller does not hold, a failed unit, or a dead figure.
    bool accepts(int32_t player, int32_t id, uint16_t figure, int32_t code) const;
    // 4782 (additive): true when the figure's current graph plays a source of ec `code` or `code + 1000` (the B variant
    // the machine draws, unit-controller.md 6.5). False as accepts().
    bool playing(int32_t player, int32_t id, uint16_t figure, int32_t code) const;
    // 5053 (additive): the same walk as playing(), answering which variant plays: `code`, `code + 1000` or 0. A dead
    // figure is answered too (it plays its death: CombatParams::death_by_variant asks for 1200 / 2200).
    int32_t playingVariant(int32_t player, int32_t id, uint16_t figure, int32_t code) const;

    // The pre-check of one art row's members (no timeline).
    AnimStatus checkArt(const units::UnitArt& art);
    std::vector<ArtCheck> precheck(const std::vector<units::UnitArt>& arts);

    struct Unit;
    struct Member;

private:
    AnimStatus member(const units::MemberArt& m, const Member*& out);
    void create(const units::UnitInput& u);
    void event(const TimelineEvent& ev, uint64_t seq);
    void settle(Unit& u);
    void revive(Unit& u);   // 0439: figures back when the damage falls
    // h: the step's length (clock.h; 4406); drawn units only (0450)
    void step(Unit& u, uint64_t seq, int steps, double h);
    void collect(const std::set<UnitKey>* drawn);

    c5m::FileProvider* files_ = nullptr;
    ArtSource* art_ = nullptr;
    GraphFactory* graphs_ = nullptr;
    ControllerOptions opt_;
    BasicBridge basic_;
    std::unique_ptr<LogicCache> cache_;
    std::map<std::string, std::unique_ptr<Member>> members_;     // by member Type + .fxsxml
    std::map<int32_t, units::PlayerInput> players_;
    std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Unit>> units_;
    std::vector<FigureProgram> programs_;
    std::vector<TriggerEvent> triggers_;
    std::vector<UnitAnimStatus> statuses_;
    ApplyCounts counts_;
    std::vector<int> take_;   // 0441: step()'s per-figure step counts, reused
    uint64_t instances_ = 0;  // 4782: the last clip-instance id given
    std::map<UnitKey, double> rates_;   // 0796: setRate
};

// 4782 (additive): a CombatRun's feedback from a controller (Controller::accepts).
class ControllerFeedback : public CombatFeedback {
public:
    explicit ControllerFeedback(const Controller& c) : c_(c) {}
    bool accepts(int32_t player, int32_t id, uint16_t figure, int32_t code) override {
        return c_.accepts(player, id, figure, code);
    }
    bool playing(int32_t player, int32_t id, uint16_t figure, int32_t code) override {
        return c_.playing(player, id, figure, code);
    }
    int32_t playingVariant(int32_t player, int32_t id, uint16_t figure, int32_t code) override {   // 5053
        return c_.playingVariant(player, id, figure, code);
    }
private:
    const Controller& c_;
};

}}  // namespace c5r::unitanim
