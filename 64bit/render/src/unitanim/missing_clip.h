// render/src/unitanim/missing_clip.h - issue 4770 (epic 0042; unit-controller.md 13.4): a state whose clip the member's
// .fxsxml names but the install does not ship. Internal to c5render (src, not include/): the public headers are not
// changed by 4770; the fields this stands in for are named in 13.4 (an AnimStatus flag, an ApplyCounts counter and a
// LOUD sink in ControllerOptions), for the lead to ask the MC for at C1.
//
// The rule as built (controller.cpp enterState): when a state's graph fails to bind with EcNotBound and the failure is
// a missing-in-install one (every source ec of the graph is named by the .fxsxml, and at least one of them, by the
// entry the bind uses, is a clip the art step did not find: c5m::FileRef::found false), then
//   - a figure that has a current graph keeps its machine state (state, path options and time in state as before the
//     update), its graph, clip and clip time; the unit stays Ok; one LOUD line per occurrence goes to the controller's
//     sink and the controller's count goes up by one. An occurrence is per unit, member and target state, and ends
//     when an event's request reaches a figure of the unit (a path write): until then that state is kept out again
//     silently, without a bind, for every figure of that member;
//   - a figure without one (a start: create, revive, or a chooser before any graph) fails as before with EcNotBound,
//     its text prefixed with kMissingInInstall (the flag).
// Every other EcNotBound (an ec the .fxsxml does not name, a clip that is found but gives no duration) fails as before.
#pragma once
#include <cstddef>
#include <functional>
#include <string>

#include "c5r/unitanim/status.h"

namespace c5r { namespace unitanim {

class Controller;

// The flag: the text prefix of an EcNotBound status that a missing-in-install clip caused (and of every LOUD line).
extern const char kMissingInInstall[];   // "missing in install: "

// True for an EcNotBound status whose text starts with kMissingInInstall.
bool missingInInstall(const AnimStatus& s);

// The LOUD lines of `c`: each is given to `sink` as it happens (one line, no newline). No sink (the default): the
// line goes to stderr with a newline. Kept until `c` is destroyed (open() keeps it).
using LoudSink = std::function<void(const std::string& line)>;
void setMissingClipSink(const Controller& c, LoudSink sink);

// Occurrences reverted by `c` since its last open() (a start that failed with the flag is not counted: its status
// says so).
size_t missingClipReverts(const Controller& c);

}}  // namespace c5r::unitanim
