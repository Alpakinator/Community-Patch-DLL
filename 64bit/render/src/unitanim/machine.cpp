// machine.cpp - the state machine interpreter (epic 0374, issue 4403; machine.h; semantics unit-controller.md 6.2).
#include "c5r/unitanim/machine.h"

#include <cmath>

namespace c5r { namespace unitanim {

using c5m::CondType;
using c5m::FsmCondition;

namespace {

std::string where(const std::string& file, size_t socket, size_t cond) {
    return file + " socket " + std::to_string(socket) + " condition " + std::to_string(cond);
}

bool pathOk(int32_t p) { return p >= 0 && p < kPathOptions; }

bool isTest(CondType t) {
    return t == CondType::CheckPathOption || t == CondType::ECComplete || t == CondType::CheckStateTime ||
           t == CondType::RandomRoll;
}

}  // namespace

AnimStatus bindMachine(const c5m::StateMachine& m, const std::string& file, BoundMachine& out) {
    out = BoundMachine();
    BoundMachine b;
    b.file = file;
    b.fsm = m;
    for (size_t i = 0; i < m.states.size(); ++i) {
        if (!b.index.insert(std::make_pair(m.states[i].id, i)).second)
            return AnimStatus::Fail(AnimCode::BadSocket, file + ": state id " + std::to_string(m.states[i].id) +
                                                             " twice");
    }
    auto d = b.index.find(m.default_state);
    if (d == b.index.end())
        return AnimStatus::Fail(AnimCode::BadSocket, file + ": default state " + std::to_string(m.default_state) +
                                                         " is not a state of the file");
    b.default_state = d->second;
    b.out.resize(m.states.size());
    b.to.resize(m.sockets.size());
    b.groups.resize(m.sockets.size());
    for (size_t s = 0; s < m.sockets.size(); ++s) {
        const c5m::FsmSocket& so = m.sockets[s];
        auto f = b.index.find(so.from);
        auto t = b.index.find(so.to);
        if (f == b.index.end() || t == b.index.end())
            return AnimStatus::Fail(AnimCode::BadSocket, file + " socket " + std::to_string(s) + ": " +
                                                             std::to_string(so.from) + " -> " + std::to_string(so.to) +
                                                             " names an unknown state");
        b.out[f->second].push_back(s);
        b.to[s] = t->second;
        size_t begin = 0;
        for (size_t c = 0; c < so.conditions.size(); ++c) {
            const FsmCondition& k = so.conditions[c];
            switch (k.type) {
            case CondType::OrOperator:
                b.groups[s].push_back(std::make_pair(begin, c));
                begin = c + 1;
                break;
            case CondType::CheckPathOption:
                if (k.method != 1 && k.method != 6)
                    return AnimStatus::Fail(AnimCode::UnknownCondition,
                                            where(file, s, c) + ": CheckPathOption method " + std::to_string(k.method));
                if (!pathOk(k.path))
                    return AnimStatus::Fail(AnimCode::BadLogicFile,
                                            where(file, s, c) + ": path " + std::to_string(k.path) + " out of range");
                break;
            case CondType::EventSetPathOption:
                if (!pathOk(k.path))
                    return AnimStatus::Fail(AnimCode::BadLogicFile,
                                            where(file, s, c) + ": path " + std::to_string(k.path) + " out of range");
                break;
            case CondType::EventRandomPathOption: {
                if (!pathOk(k.path))
                    return AnimStatus::Fail(AnimCode::BadLogicFile,
                                            where(file, s, c) + ": path " + std::to_string(k.path) + " out of range");
                if (k.use_weights) {
                    bool any = false;
                    for (auto& w : k.weights) {
                        if (!(w.weight >= 0) || !std::isfinite(w.weight))
                            return AnimStatus::Fail(AnimCode::BadLogicFile, where(file, s, c) + ": a negative weight");
                        any = any || w.weight > 0;
                    }
                    if (!any)
                        return AnimStatus::Fail(AnimCode::BadLogicFile,
                                                where(file, s, c) + ": useweights 1 without a positive weight");
                } else if (k.value < 0) {
                    return AnimStatus::Fail(AnimCode::BadLogicFile,
                                            where(file, s, c) + ": negative value " + std::to_string(k.value));
                }
                break;
            }
            case CondType::ECComplete:
                break;
            case CondType::CheckStateTime:
                if (k.method != 5 && k.method != 1)
                    return AnimStatus::Fail(AnimCode::UnknownCondition,
                                            where(file, s, c) + ": CheckStateTime method " + std::to_string(k.method));
                break;
            case CondType::RandomRoll:
                if (!(k.fvalue >= 0 && k.fvalue <= 1))
                    return AnimStatus::Fail(AnimCode::BadLogicFile, where(file, s, c) + ": RandomRoll chance outside 0..1");
                break;
            default:
                return AnimStatus::Fail(AnimCode::UnknownCondition,
                                        where(file, s, c) + ": condition type '" +
                                            (k.type_name.empty() ? std::string("?") : k.type_name) +
                                            "' is not implemented");
            }
        }
        b.groups[s].push_back(std::make_pair(begin, so.conditions.size()));
    }
    out = std::move(b);
    return AnimStatus::Ok();
}

int32_t drawOption(const FsmCondition& c, double u) {
    if (u < 0) u = 0;
    if (u >= 1) u = 0.9999999999999999;
    if (c.use_weights) {
        double sum = 0;
        for (auto& w : c.weights) sum += w.weight > 0 ? w.weight : 0;
        double x = u * sum, acc = 0;
        int32_t last = 0;
        for (auto& w : c.weights) {
            if (!(w.weight > 0)) continue;
            acc += w.weight;
            last = w.value;
            if (x < acc) return w.value;
        }
        return last;
    }
    const int32_t n = c.value + 1;
    int32_t v = static_cast<int32_t>(std::floor(u * n));
    return v >= n ? n - 1 : v;
}

bool optionAllowed(const FsmCondition& c, int32_t value) {
    if (c.use_weights) {
        for (auto& w : c.weights)
            if (w.weight > 0 && w.value == value) return true;
        return false;
    }
    return value >= 0 && value <= c.value;
}

AnimStatus updateMachine(const BoundMachine& m, MachineState& s, MachineHost& host, int& taken) {
    taken = -1;
    if (s.state >= m.out.size())
        return AnimStatus::Fail(AnimCode::BadSocket, m.file + ": state index out of range");
    for (size_t so : m.out[s.state]) {
        const auto& conds = m.fsm.sockets[so].conditions;
        for (const auto& g : m.groups[so]) {
            bool holds = true;
            for (size_t c = g.first; c < g.second && holds; ++c) {
                const FsmCondition& k = conds[c];
                if (!isTest(k.type)) continue;
                switch (k.type) {
                case CondType::CheckPathOption: {
                    bool eq = s.opts.v[k.path] == static_cast<float>(k.value);
                    holds = k.method == 1 ? eq : !eq;
                    break;
                }
                case CondType::CheckStateTime:
                    holds = s.time + 1e-6 >= k.fvalue;   // whole clock steps against a float time
                    break;
                case CondType::ECComplete:
                    holds = host.ecComplete(k.value, k.fvalue);
                    break;
                case CondType::RandomRoll: {
                    bool fires = false;
                    AnimStatus st = host.roll(so, k.fvalue, fires);
                    if (!st.ok()) return st;
                    holds = k.invert ? !fires : fires;
                    break;
                }
                default:
                    break;
                }
            }
            if (!holds) continue;
            // the group holds: its events, in file order, then the transition
            PathOptions opts = s.opts;
            for (size_t c = g.first; c < g.second; ++c) {
                const FsmCondition& k = conds[c];
                if (k.type == CondType::EventSetPathOption) {
                    opts.v[k.path] = static_cast<float>(k.value);
                } else if (k.type == CondType::EventRandomPathOption) {
                    int32_t v = 0;
                    AnimStatus st = host.option(so, k, v);
                    if (!st.ok()) return st;
                    opts.v[k.path] = static_cast<float>(v);
                }
            }
            s.opts = opts;
            s.state = m.to[so];
            s.time = 0;
            taken = static_cast<int>(so);
            return AnimStatus::Ok();
        }
    }
    return AnimStatus::Ok();
}

}}  // namespace c5r::unitanim
