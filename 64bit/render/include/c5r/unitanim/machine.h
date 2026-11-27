// c5r/unitanim/machine.h - the state machine interpreter (epic 0374, issue 4403; semantics: research/05-graphics-
// composition/unit-controller.md 6.2, read by 4408).
//
// One update of one figure's machine: the sockets of the current state in file order; a socket's conditions are an OR
// of AND groups separated by OrOperator; a group holds when all its tests hold, evaluated in file order and stopping
// at the first that fails (so a RandomRoll after a failing test draws nothing); the first socket with a group that
// holds is taken, and only that group's events (EventSetPathOption, EventRandomPathOption) are applied, in file
// order. At most one transition per update. Tests: CheckPathOption method 1 (equal) and 6 (not equal);
// CheckStateTime method 5 (and 1, read the same) = time in the state >= the value; ECComplete = the host's graph has
// played that code to its end; RandomRoll = a per-update chance, drawn by the host (invert: fires when the draw
// fails). An empty group (an empty socket, or an event-only group) holds.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "c5models/logic.h"
#include "c5r/unitanim/graph.h"
#include "c5r/unitanim/status.h"

namespace c5r { namespace unitanim {

// A machine checked once per file: every socket's states exist, every condition type and method is implemented,
// every path is 0..kPathOptions-1. Statuses: BadSocket (a socket or the default state naming an unknown state, a
// state id twice), UnknownCondition (a type or method the interpreter lacks: Unknown, AnyComplete, EventCopyVariable,
// EventSetVariable, Variable, True, False; CheckPathOption methods other than 1 and 6; CheckStateTime methods other
// than 5 and 1), BadLogicFile (a path out of range, an EventRandomPathOption with useweights 1 and no positive
// weight, or with a negative value).
struct BoundMachine {
    std::string file;                                       // as the member names it, lower case
    c5m::StateMachine fsm;                                  // a copy
    std::map<int32_t, size_t> index;                        // state id -> index into fsm.states
    size_t default_state = 0;                               // index
    std::vector<std::vector<size_t>> out;                   // per state index: its sockets, file order
    std::vector<size_t> to;                                 // per socket: the target state's index
    std::vector<std::vector<std::pair<size_t, size_t>>> groups;   // per socket: [begin, end) condition ranges
};
AnimStatus bindMachine(const c5m::StateMachine& m, const std::string& file, BoundMachine& out);

// What the interpreter needs from its figure.
class MachineHost {
public:
    virtual ~MachineHost() = default;
    // ECComplete: the current state's graph has played the source of this code to its end (fraction: 0 = the end,
    // else that fraction of it; ECComplete's optional value).
    virtual bool ecComplete(int32_t ec, float fraction) = 0;
    // RandomRoll at socket `socket` (file order): fires with probability `chance` (free mode) or as forced.
    virtual AnimStatus roll(size_t socket, float chance, bool& fires) = 0;
    // EventRandomPathOption at socket `socket`: the value drawn (by the weights, or uniformly over 0..value).
    virtual AnimStatus option(size_t socket, const c5m::FsmCondition& c, int32_t& value) = 0;
};

struct MachineState {
    size_t state = 0;           // index into fsm.states
    PathOptions opts;
    double time = 0;            // seconds in the state (whole clock steps)
};

// One update. taken = the socket index taken (file order), or -1 when none holds. On a transition the target state
// is entered with time 0 and the group's events applied to opts; on a failing status the state is left as it was.
AnimStatus updateMachine(const BoundMachine& m, MachineState& s, MachineHost& host, int& taken);

// The value a free-mode EventRandomPathOption draws for a uniform u in [0, 1): useweights 1 by the listed weights,
// else uniformly over 0..value (4408 6.2 [I]; the 13 sockets with 0.6 / 0.4 under useweights 0 are a live check).
int32_t drawOption(const c5m::FsmCondition& c, double u);
// Whether `value` is one EventRandomPathOption can give (for forced choices).
bool optionAllowed(const c5m::FsmCondition& c, int32_t value);

}}  // namespace c5r::unitanim
