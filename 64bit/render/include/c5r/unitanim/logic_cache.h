// c5r/unitanim/logic_cache.h - the logic files of the unit art, read once per name through the install's and mods'
// files (epic 0374, issue 4403; unit-controller.md 5). Names are looked up as models' FileProvider does: lower-case
// basename. A failure is cached with its status, so a file is read at most once.
#pragma once
#include <map>
#include <memory>
#include <string>

#include "c5models/art.h"
#include "c5models/logic.h"
#include "c5r/unitanim/machine.h"
#include "c5r/unitanim/status.h"

namespace c5r { namespace unitanim {

// "Assets/Units/X.fsmxml" -> "x.fsmxml".
std::string logicName(const std::string& as_written);

class LogicCache {
public:
    explicit LogicCache(c5m::FileProvider& files) : files_(files) {}
    // MachineNotFound, BadLogicFile (the reader's message), or bindMachine's statuses.
    AnimStatus machine(const std::string& name, const BoundMachine*& out);
    // GraphNotFound, BadLogicFile.
    AnimStatus graph(const std::string& name, const c5m::DagFile*& out);
    // TriggersNotFound, BadLogicFile, UnknownTrigger (a trigger whose type the controller does not implement).
    AnimStatus triggers(const std::string& name, const c5m::TriggerSystem*& out);
    size_t files() const { return machines_.size() + graphs_.size() + triggers_.size(); }
    // Parsed data given directly (tests, and callers that hold parsed files): the name then never reads a file.
    // putMachine binds and returns bindMachine's status (also cached).
    AnimStatus putMachine(const std::string& name, const c5m::StateMachine& m);
    void putGraph(const std::string& name, const c5m::DagFile& d);
    void putTriggers(const std::string& name, const c5m::TriggerSystem& t);

private:
    template <class T> struct Entry { AnimStatus st; std::unique_ptr<T> v; };
    c5m::FileProvider& files_;
    std::map<std::string, Entry<BoundMachine>> machines_;
    std::map<std::string, Entry<c5m::DagFile>> graphs_;
    std::map<std::string, Entry<c5m::TriggerSystem>> triggers_;
};

}}  // namespace c5r::unitanim
