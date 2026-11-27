// logic_xml.cpp - the .fsmxml (state machine) and .ftsxml (timed trigger) readers (epic 0374, issue 4402).
// Own code, GPL-3.0-or-later. Behaviour of the files: research/05-graphics-composition/unit-controller.md 6.2, 6.4.
// Strict: the element tree must be the one the format uses (unknown elements are refused with Err::Xml and the
// line), required attributes must be present, numbers must parse whole. Attributes are all kept as written in
// `attrs`. The only lenience is parse_xml's, counted in `lenience` (art.h XmlLenience).
#include "c5models/logic.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace c5m {

namespace {

void set_err(Error* err, const std::string& what) {
    if (err) *err = {Err::Xml, what};
}

std::string at_line(const XmlElement& e) { return "line " + std::to_string(e.line) + ": <" + e.name + ">"; }

bool parse_number(const std::string& s, double& out) {
    const char* b = s.c_str();
    while (*b == ' ' || *b == '\t') ++b;
    if (!*b) return false;
    char* q = nullptr;
    out = std::strtod(b, &q);
    if (q == b) return false;
    while (*q == ' ' || *q == '\t') ++q;
    return *q == 0 && std::isfinite(out);
}

struct Reader {
    Error* err;
    bool fail(const XmlElement& e, const std::string& what) {
        set_err(err, at_line(e) + " " + what);
        return false;
    }
    // Copies every attribute into `attrs`.
    static void keep(const XmlElement& e, std::map<std::string, std::string>& attrs) {
        for (auto& a : e.attrs) attrs[a.first] = a.second;
    }
    // Optional integer attribute: absent leaves `v`; present must be an integral number.
    bool int_attr(const XmlElement& e, const char* name, int32_t& v, bool required = false) {
        const std::string* s = e.attr(name);
        if (!s) return required ? fail(e, std::string("without attribute ") + name) : true;
        double d = 0;
        if (!parse_number(*s, d) || d != std::floor(d) || d < -2147483648.0 || d > 2147483647.0)
            return fail(e, std::string(name) + "=\"" + *s + "\" is not an integer");
        v = (int32_t)d;
        return true;
    }
    // Optional number attribute as float (strtod, then rounded to float).
    bool float_attr(const XmlElement& e, const char* name, float& v, bool required = false) {
        const std::string* s = e.attr(name);
        if (!s) return required ? fail(e, std::string("without attribute ") + name) : true;
        double d = 0;
        if (!parse_number(*s, d)) return fail(e, std::string(name) + "=\"" + *s + "\" is not a number");
        v = (float)d;
        return true;
    }
    // A number that the machines compare as an integer: value = nearest integer, fvalue = the number.
    bool num_value(const XmlElement& e, const char* name, int32_t& v, float& fv) {
        const std::string* s = e.attr(name);
        if (!s) return true;
        double d = 0;
        if (!parse_number(*s, d) || d < -2147483648.0 || d > 2147483647.0)
            return fail(e, std::string(name) + "=\"" + *s + "\" is not a number");
        fv = (float)d;
        v = (int32_t)std::lround(d);
        return true;
    }
    bool bool_attr(const XmlElement& e, const char* name, bool& v) {
        const std::string* s = e.attr(name);
        if (!s) return true;
        if (*s == "0" || *s == "false" || *s == "False") v = false;
        else if (*s == "1" || *s == "true" || *s == "True") v = true;
        else return fail(e, std::string(name) + "=\"" + *s + "\" is not 0 or 1");
        return true;
    }
    bool str_attr(const XmlElement& e, const char* name, std::string& v, bool required = false) {
        const std::string* s = e.attr(name);
        if (!s) return required ? fail(e, std::string("without attribute ") + name) : true;
        v = *s;
        return true;
    }
    bool only_children(const XmlElement& e, const char* allowed) {
        for (auto& c : e.children)
            if (c.name != allowed) return fail(c, std::string("unexpected inside <") + e.name + ">");
        return true;
    }
};

CondType cond_type(const std::string& t) {
    static const struct { const char* name; CondType type; } k[] = {
        {"FFSMConditionCheckPathOption", CondType::CheckPathOption},
        {"FFSMConditionEventSetPathOption", CondType::EventSetPathOption},
        {"FFSMConditionEventRandomPathOption", CondType::EventRandomPathOption},
        {"FFSMConditionOrOperator", CondType::OrOperator},
        {"FFSMConditionECComplete", CondType::ECComplete},
        {"FFSMConditionCheckStateTime", CondType::CheckStateTime},
        {"FFSMConditionRandomRoll", CondType::RandomRoll},
        {"FFSMConditionAnyComplete", CondType::AnyComplete},
        {"FFSMConditionEventCopyVariable", CondType::EventCopyVariable},
        {"FFSMConditionEventSetVariable", CondType::EventSetVariable},
        {"FFSMConditionVariable", CondType::Variable},
        {"FFSMConditionTrue", CondType::True},
        {"FFSMConditionFalse", CondType::False},
    };
    for (auto& x : k)
        if (t == x.name) return x.type;
    return CondType::Unknown;
}

TriggerType trigger_type(const std::string& t) {
    if (t == "FTimedTriggerSound") return TriggerType::Sound;
    if (t == "FTimedTriggerStopSound") return TriggerType::StopSound;
    if (t == "FTimedTriggerEffect") return TriggerType::Effect;
    if (t == "FTimedTriggerTransfer") return TriggerType::Transfer;
    if (t == "FTimedTriggerAction") return TriggerType::Action;
    return TriggerType::Unknown;
}

bool read_condition(Reader& r, const XmlElement& e, FsmCondition& c) {
    Reader::keep(e, c.attrs);
    if (!r.str_attr(e, "type", c.type_name, true)) return false;
    c.type = cond_type(c.type_name);
    if (!r.float_attr(e, "order", c.order)) return false;
    switch (c.type) {
    case CondType::ECComplete:
        if (!r.int_attr(e, "ec", c.value, true) || !r.float_attr(e, "value", c.fvalue)) return false;
        break;
    case CondType::CheckStateTime:
        if (!r.int_attr(e, "method", c.method) || !r.float_attr(e, "value", c.fvalue, true)) return false;
        break;
    case CondType::RandomRoll:
        if (!r.float_attr(e, "value", c.fvalue, true) || !r.bool_attr(e, "invert", c.invert)) return false;
        break;
    case CondType::CheckPathOption:
    case CondType::EventSetPathOption:
    case CondType::EventRandomPathOption:
        if (!r.int_attr(e, "method", c.method) || !r.int_attr(e, "path", c.path, true) ||
            !r.num_value(e, "value", c.value, c.fvalue) || !r.bool_attr(e, "useweights", c.use_weights))
            return false;
        break;
    default:
        break;
    }
    for (auto& w : e.children) {
        if (w.name != "weights") return r.fail(w, "unexpected inside <condition>");
        if (!r.only_children(w, "weight")) return false;
        for (auto& x : w.children) {
            FsmWeight fw;
            float dummy = 0;
            if (!r.num_value(x, "v", fw.value, dummy) || !r.float_attr(x, "w", fw.weight, true)) return false;
            if (!x.attr("v")) return r.fail(x, "without attribute v");
            c.weights.push_back(fw);
        }
    }
    return true;
}

}  // namespace

bool parse_fsmxml(const char* data, size_t size, StateMachine& out, Error* err) {
    out = StateMachine();
    XmlElement root;
    XmlLenience len;
    if (!parse_xml(data, size, root, err, &len)) return false;
    Reader r{err};
    StateMachine m;
    m.lenience = len;
    if (root.name != "statemachine") return r.fail(root, "is the root, expected <statemachine>");
    Reader::keep(root, m.attrs);
    if (!r.str_attr(root, "fsm", m.fsm) || !r.int_attr(root, "default_state", m.default_state, true)) return false;
    int n_states = 0, n_sockets = 0;
    for (auto& g : root.children) {
        if (g.name == "states") {
            if (n_states++) return r.fail(g, "appears twice");
            if (!r.only_children(g, "state")) return false;
            for (auto& e : g.children) {
                FsmState s;
                Reader::keep(e, s.attrs);
                if (!r.int_attr(e, "id", s.id, true) || !r.str_attr(e, "type", s.type) ||
                    !r.str_attr(e, "name", s.name) || !r.str_attr(e, "asset", s.asset) ||
                    !r.int_attr(e, "action", s.action) || !r.int_attr(e, "sockets", s.socket_count))
                    return false;
                if (!e.children.empty()) return r.fail(e.children[0], "unexpected inside <state>");
                m.states.push_back(std::move(s));
            }
        } else if (g.name == "sockets") {
            if (n_sockets++) return r.fail(g, "appears twice");
            if (!r.only_children(g, "socket")) return false;
            for (auto& e : g.children) {
                FsmSocket so;
                Reader::keep(e, so.attrs);
                int32_t declared = -1;
                if (!r.int_attr(e, "from", so.from, true) || !r.int_attr(e, "too", so.to, true) ||
                    !r.bool_attr(e, "bidirectional", so.bidirectional) || !r.int_attr(e, "order", so.order) ||
                    !r.int_attr(e, "conditions", declared))
                    return false;
                int n_cond = 0;
                for (auto& cs : e.children) {
                    if (cs.name != "conditions") return r.fail(cs, "unexpected inside <socket>");
                    if (n_cond++) return r.fail(cs, "appears twice");
                    if (!r.only_children(cs, "condition")) return false;
                    for (auto& c : cs.children) {
                        FsmCondition fc;
                        if (!read_condition(r, c, fc)) return false;
                        so.conditions.push_back(std::move(fc));
                    }
                }
                if (declared >= 0 && (size_t)declared != so.conditions.size())
                    return r.fail(e, "declares " + std::to_string(declared) + " conditions and holds " +
                                         std::to_string(so.conditions.size()));
                m.sockets.push_back(std::move(so));
            }
        } else {
            return r.fail(g, "unexpected inside <statemachine>");
        }
    }
    if (!n_states) return r.fail(root, "has no <states>");
    out = std::move(m);
    return true;
}

bool parse_ftsxml(const char* data, size_t size, TriggerSystem& out, Error* err) {
    out = TriggerSystem();
    XmlElement root;
    XmlLenience len;
    if (!parse_xml(data, size, root, err, &len)) return false;
    Reader r{err};
    TriggerSystem t;
    t.lenience = len;
    if (root.name != "trigger_system") return r.fail(root, "is the root, expected <trigger_system>");
    Reader::keep(root, t.attrs);
    if (!r.str_attr(root, "type", t.type)) return false;
    int n_tracks = 0, n_trigs = 0;
    for (auto& g : root.children) {
        if (g.name == "event_tracks") {
            if (n_tracks++) return r.fail(g, "appears twice");
            if (!r.only_children(g, "event_track")) return false;
            for (auto& e : g.children) {
                FtsEventTrack et;
                if (!r.int_attr(e, "ec", et.ec, true) || !r.only_children(e, "track")) return false;
                for (auto& k : e.children) {
                    FtsTrack tr;
                    if (!r.int_attr(k, "id", tr.id, true) || !r.str_attr(k, "name", tr.name)) return false;
                    if (!k.children.empty()) return r.fail(k.children[0], "unexpected inside <track>");
                    et.tracks.push_back(std::move(tr));
                }
                t.event_tracks.push_back(std::move(et));
            }
        } else if (g.name == "triggers") {
            if (n_trigs++) return r.fail(g, "appears twice");
            if (!r.only_children(g, "trigger")) return false;
            for (auto& e : g.children) {
                FtsTrigger tr;
                Reader::keep(e, tr.attrs);
                if (!r.str_attr(e, "type", tr.type_name, true) || !r.int_attr(e, "id", tr.id) ||
                    !r.int_attr(e, "ec", tr.ec) || !r.int_attr(e, "track", tr.track) ||
                    !r.int_attr(e, "tier", tr.tier) || !r.float_attr(e, "time", tr.time) ||
                    !r.float_attr(e, "duration", tr.duration) || !r.bool_attr(e, "repeat", tr.repeat))
                    return false;
                if (!e.children.empty()) return r.fail(e.children[0], "unexpected inside <trigger>");
                tr.type = trigger_type(tr.type_name);
                t.triggers.push_back(std::move(tr));
            }
        } else {
            return r.fail(g, "unexpected inside <trigger_system>");
        }
    }
    out = std::move(t);
    return true;
}

}  // namespace c5m
