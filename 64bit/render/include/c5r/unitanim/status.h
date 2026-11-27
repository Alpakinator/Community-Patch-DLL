// c5r/unitanim/status.h - the controller's per-unit status (epic 0374; unit-controller.md 5). Interface by the lead at
// C1; 4403 owns it from now on and may add codes (never reuse or renumber one).
//
// A unit whose status is not Ok gets no pose program and is not drawn (0041's rule). No status ever substitutes a
// default clip or a partial pose (D118, D125, R6/D132; MC condition (4) of #2000): every failure is named and logged
// LOUD by the caller.
#pragma once
#include <cstdint>
#include <string>

namespace c5r { namespace unitanim {

enum class AnimCode : uint8_t {
    Ok = 0,
    MachineNotFound,        // a .fsmxml the member's .fxsxml names, in neither install nor mods
    GraphNotFound,          // a .dge a state names
    TriggersNotFound,       // a .ftsxml the member names
    BadLogicFile,           // a reader refused a logic file (text: file and reader message)
    UnknownCondition,       // a condition type the interpreter does not implement
    UnknownNode,            // a DAG node type the evaluator does not implement
    UnknownTrigger,         // a trigger type the controller does not implement
    EcNotBound,             // an ec a state, graph source or trigger names that the member's .fxsxml lacks
    BadSocket,              // a socket to an unknown state
    NoProgress,             // a state whose graph advances no time and has no socket out
    ForcedChoiceMissing,    // forced mode: a random decision with no recorded choice
    BadEvent,               // an event the unit's machine cannot take (text: event and why)
    UnitFailed,             // 0041's module refused the unit (text: its UnitStatus)
    TrackMaskNotBound,      // 4410: a graph's track mask binding that no bone of the model's skeleton carries
                            // (no GrannyTrackMasks data: unit-controller.md 13.3); never drawn with a guessed mask
};

struct AnimStatus {
    AnimCode code = AnimCode::Ok;
    std::string text;       // names the file, state, socket, node, ec or record
    bool ok() const { return code == AnimCode::Ok; }
    static AnimStatus Ok() { return AnimStatus(); }
    static AnimStatus Fail(AnimCode c, std::string t) { AnimStatus s; s.code = c; s.text = std::move(t); return s; }
};

const char* name(AnimCode c);   // "Ok", "MachineNotFound", ... (status.cpp, 4403)

}}  // namespace c5r::unitanim
