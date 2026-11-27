// logic_cache.cpp - the logic files read once per name (epic 0374, issue 4403; logic_cache.h).
#include "c5r/unitanim/logic_cache.h"

#include <cctype>

namespace c5r { namespace unitanim {

std::string logicName(const std::string& w) {
    size_t p = w.find_last_of("/\\");
    std::string b = p == std::string::npos ? w : w.substr(p + 1);
    for (auto& ch : b) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return b;
}

AnimStatus LogicCache::machine(const std::string& name, const BoundMachine*& out) {
    out = nullptr;
    const std::string n = logicName(name);
    auto it = machines_.find(n);
    if (it == machines_.end()) {
        Entry<BoundMachine> e;
        c5m::Bytes bytes;
        std::string origin;
        if (!files_.read(n, bytes, &origin)) {
            e.st = AnimStatus::Fail(AnimCode::MachineNotFound, n + ": in neither the install nor the mods");
        } else {
            c5m::StateMachine m;
            c5m::Error err;
            if (!c5m::parse_fsmxml(reinterpret_cast<const char*>(bytes.data()), bytes.size(), m, &err)) {
                e.st = AnimStatus::Fail(AnimCode::BadLogicFile, n + " (" + origin + "): " + err.what);
            } else {
                e.v.reset(new BoundMachine());
                e.st = bindMachine(m, n, *e.v);
                if (!e.st.ok()) e.v.reset();
            }
        }
        it = machines_.insert(std::make_pair(n, std::move(e))).first;
    }
    out = it->second.v.get();
    return it->second.st;
}

AnimStatus LogicCache::graph(const std::string& name, const c5m::DagFile*& out) {
    out = nullptr;
    const std::string n = logicName(name);
    auto it = graphs_.find(n);
    if (it == graphs_.end()) {
        Entry<c5m::DagFile> e;
        c5m::Bytes bytes;
        std::string origin;
        if (!files_.read(n, bytes, &origin)) {
            e.st = AnimStatus::Fail(AnimCode::GraphNotFound, n + ": in neither the install nor the mods");
        } else {
            e.v.reset(new c5m::DagFile());
            c5m::Error err;
            if (!c5m::parse_dge(bytes.data(), bytes.size(), *e.v, &err)) {
                e.st = AnimStatus::Fail(AnimCode::BadLogicFile, n + " (" + origin + "): " + err.what);
                e.v.reset();
            } else if (e.v->dags.empty()) {
                e.st = AnimStatus::Fail(AnimCode::BadLogicFile, n + " (" + origin + "): no DAG");
                e.v.reset();
            }
        }
        it = graphs_.insert(std::make_pair(n, std::move(e))).first;
    }
    out = it->second.v.get();
    return it->second.st;
}

AnimStatus LogicCache::triggers(const std::string& name, const c5m::TriggerSystem*& out) {
    out = nullptr;
    const std::string n = logicName(name);
    auto it = triggers_.find(n);
    if (it == triggers_.end()) {
        Entry<c5m::TriggerSystem> e;
        c5m::Bytes bytes;
        std::string origin;
        if (!files_.read(n, bytes, &origin)) {
            e.st = AnimStatus::Fail(AnimCode::TriggersNotFound, n + ": in neither the install nor the mods");
        } else {
            e.v.reset(new c5m::TriggerSystem());
            c5m::Error err;
            if (!c5m::parse_ftsxml(reinterpret_cast<const char*>(bytes.data()), bytes.size(), *e.v, &err)) {
                e.st = AnimStatus::Fail(AnimCode::BadLogicFile, n + " (" + origin + "): " + err.what);
                e.v.reset();
            } else {
                for (const auto& t : e.v->triggers) {
                    if (t.type == c5m::TriggerType::Unknown) {
                        e.st = AnimStatus::Fail(AnimCode::UnknownTrigger,
                                                n + ": trigger " + std::to_string(t.id) + " of type '" + t.type_name +
                                                    "' is not implemented");
                        e.v.reset();
                        break;
                    }
                }
            }
        }
        it = triggers_.insert(std::make_pair(n, std::move(e))).first;
    }
    out = it->second.v.get();
    return it->second.st;
}

AnimStatus LogicCache::putMachine(const std::string& name, const c5m::StateMachine& m) {
    const std::string n = logicName(name);
    Entry<BoundMachine> e;
    e.v.reset(new BoundMachine());
    e.st = bindMachine(m, n, *e.v);
    if (!e.st.ok()) e.v.reset();
    AnimStatus st = e.st;
    machines_[n] = std::move(e);
    return st;
}

void LogicCache::putGraph(const std::string& name, const c5m::DagFile& d) {
    Entry<c5m::DagFile> e;
    e.v.reset(new c5m::DagFile(d));
    graphs_[logicName(name)] = std::move(e);
}

void LogicCache::putTriggers(const std::string& name, const c5m::TriggerSystem& t) {
    const std::string n = logicName(name);
    Entry<c5m::TriggerSystem> e;
    e.v.reset(new c5m::TriggerSystem(t));
    for (const auto& x : t.triggers)
        if (x.type == c5m::TriggerType::Unknown) {
            e.st = AnimStatus::Fail(AnimCode::UnknownTrigger, n + ": trigger " + std::to_string(x.id) + " of type '" +
                                                                  x.type_name + "' is not implemented");
            e.v.reset();
            break;
        }
    triggers_[n] = std::move(e);
}

}}  // namespace c5r::unitanim
