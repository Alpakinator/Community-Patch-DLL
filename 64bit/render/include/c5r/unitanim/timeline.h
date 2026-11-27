// c5r/unitanim/timeline.h - the controller's input as a file: the unit timeline (.c5anim, format 1) (epic 0374, issue
// 4404 part A, written by the lead; format and rules: research/05-graphics-composition/unit-controller.md 1-2).
//
//   c5anim  1
//   scene   <.c5units path relative to this file, or -> <seed u64> <free|forced>
//   player  <player> <colour Type> <art_style> <era Type>          (as .c5units; for units created later)
//   ev      <player> <id> <kind> <args ...>                         (applied in file order at the next frame)
//   frame   <seq u64> <dt seconds> <present_time seconds|-> <exe_frame|->
//
// Event kinds and their arguments (after <player> <id> <kind>):
//   create      <the .c5units unit line's 16 fields after <player> <id>: unit_type art_row|- plot_x plot_y root_x|-
//               root_y|- root_z|- facing seed damage max_hp alive|- flags fade_from fade_to fade>
//   remove      (none)
//   fortify     <0|1>
//   move        <facing 0-5> <path length >= 0>
//   turn        <facing 0-5> <left|right>
//   arrive      <facing 0-5>
//   place       <plot_x> <plot_y> <facing 0-5>
//   action      <attack|attack_city|bombard|charge|attack_charge|victory|bombard_defend|work|captured|activate|
//               paradrop|anti_air|run|combat_ready|idle> <argument:the build Type for work (- = the work stop, 0439), else -> <figures>
//   damage      <damage> <previous damage>
//   kill        <figures>
//   destroyed   <in combat 0|1>
//   embark      <0|1>
//   garrison    <0|1>
//   visibility  <visible 0|1> <blend time seconds >= 0>
//   reset       (none)
//   choice      <figures> <point roll:<machine>:<socket>|option:<machine>:<socket>> <value>
//   steer       <figures> <path 7 weight 0..1> <path 8 selector 0-15>   (turn and aim; MC #2017)
// <figures> is "*" (all living figures) or a comma list of distinct figure indexes 0-63.
//
// One tab between fields, '#' comment lines, blank lines ignored, LF or CRLF. Strict: the first refusal stops the read
// with a named code and its line, and the output is left empty. Events must be closed by a frame (events after the
// last frame are refused). Units are those of the start scene plus those created; an event for any other (player, id),
// or after its remove, is UnknownUnit. The writer checks its own text by reading it back. Nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "c5r/units/unit_scene.h"

namespace c5r { namespace unitanim {

enum class TimelineErr {
    Ok = 0,
    Io,           // the file (or its start scene) could not be opened, read or written
    BadHeader,    // the first record is not "c5anim<TAB>1"
    BadRecord,    // unknown record or event kind, wrong field count, scene record missing or not second, events not
                  // closed by a frame
    BadValue,     // a number that does not parse completely, a value out of range, a bad figure list or point
    UnknownUnit,  // an event for a unit that is not in the scene (never created, or removed)
    Duplicate,    // a second scene record, a player twice, a create of a unit that exists, a figure twice in a list
    Orphan,       // a create whose player has no player record (in the start scene or this file)
    OutOfOrder,   // a frame whose seq does not increase
    TooLarge,     // over the limits below
    BadScene,     // the start scene file was refused by the .c5units reader (message: its status)
};
const char* timelineErrName(TimelineErr e);

constexpr size_t kTimelineMaxFrames = 1000000;
constexpr size_t kTimelineMaxEventsPerFrame = 65536;
constexpr size_t kTimelineMaxUnits = 4096;          // live at once
constexpr size_t kTimelineMaxPlayers = 64;
constexpr size_t kTimelineMaxLine = 4096;
constexpr size_t kTimelineMaxFile = 256u << 20;
constexpr double kTimelineMaxDt = 3600.0;

struct TimelineStatus {
    TimelineErr code = TimelineErr::Ok;
    size_t line = 0;      // 1-based; 0 = the file as a whole
    std::string msg;
    bool ok() const { return code == TimelineErr::Ok; }
    std::string str() const;   // "<code> at line <n>: <msg>" or "<code>: <msg>"
};

enum class EventKind : uint8_t {
    Create, Remove, Fortify, Move, Turn, Arrive, Place, Action, Damage, Kill, Destroyed, Embark, Garrison,
    Visibility, Reset, Choice, Steer,
};
const char* eventKindName(EventKind k);

enum class ActionKind : uint8_t {
    None, Attack, AttackCity, Bombard, Charge, AttackCharge, Victory, BombardDefend, Work, Captured, Activate,
    Paradrop,
    AntiAir,           // 4404 (additive): "anti_air", the attack of a member with AttackSurfaceToAir (1130)
    // 4775 (additive; lead-0042 #7555): the combat choreography's per-figure requests (combat.h)
    Run,               // "run": 1400 for a figure set (the approach; `move` is the whole unit's)
    CombatReady,       // "combat_ready": 1600
    Idle,              // "idle": 1000 for a figure set (the reface; `arrive` is the whole unit's)
    // 4750 (additive, appended; lead-0042 #10392): stock's phase-2 shuffle of a non-answering defender
    // (CombatParams::defend_from_stance)
    Walk,              // "walk": 1450 for a figure set
};
const char* actionKindName(ActionKind k);

struct FigureSet {
    bool all = true;
    std::vector<uint16_t> figures;     // when !all: distinct, in file order
};

struct TimelineEvent {
    EventKind kind = EventKind::Reset;
    int32_t player = -1, id = -1;
    units::UnitInput unit;             // Create (player and id equal the event's)
    // Integer arguments by kind:
    //   Fortify, Embark, Garrison: on = a; Destroyed: in_combat = a; Visibility: visible = a
    //   Move: facing = a, path length = b;  Turn: facing = a, right = b (1) / left (0);  Arrive: facing = a
    //   Place: plot_x = a, plot_y = b, facing = c;  Damage: damage = a, previous = b;  Choice: value = a;  Steer: path 8 = a
    int32_t a = 0, b = 0, c = 0;
    float blend_time = 0;              // Visibility
    float weight = 0;                  // Steer: path 7
    ActionKind action = ActionKind::None;
    std::string argument;              // Action: the build Type for work ("" = the work stop, 0439), else ""
    FigureSet figures;                 // Action, Kill, Choice
    std::string point;                 // Choice: "roll:<machine>:<socket>" or "option:<machine>:<socket>"
};

struct TimelineFrame {
    uint64_t seq = 0;
    double dt = 0;                     // seconds, the step the caller gives the controller
    bool has_present = false;
    double present_time = 0;           // seconds (recordings: the captured frame's Present time)
    bool has_exe_frame = false;
    uint64_t exe_frame = 0;
    std::vector<TimelineEvent> events; // in file order, applied before the step
};

struct Timeline {
    std::string scene_path;            // as written ("" for "-")
    uint64_t seed = 0;
    bool forced = false;
    std::vector<units::PlayerInput> players;   // player records of this file (the start scene's are in the scene)
    std::vector<TimelineFrame> frames;
};

// Text -> timeline. `start` is the start scene the scene record names (nullptr or an empty scene for "-"); its units
// exist before the first frame and its players are known. On a refusal `out` is reset to Timeline{}.
TimelineStatus parseTimeline(const std::string& text, const units::UnitScene* start, Timeline& out);
// Timeline -> text (LF). Checked by reading back against `start`; on a refusal `text` is cleared.
TimelineStatus writeTimeline(const Timeline& tl, const units::UnitScene* start, std::string& text);

// File forms: the start scene is read from scene_path relative to the timeline file's folder.
TimelineStatus readTimelineFile(const std::wstring& path, Timeline& out, units::UnitScene& start);
TimelineStatus writeTimelineFile(const std::wstring& path, const Timeline& tl, const units::UnitScene* start);

}}  // namespace c5r::unitanim
