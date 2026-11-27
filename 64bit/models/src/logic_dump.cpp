// logic_dump.cpp - canonical text dumps of the logic files (epic 0374, issue 4402, DoD B). Own code,
// GPL-3.0-or-later. models/tools/logicdump/logicdump.py writes the same text from the Python reader, so the two are
// compared by SHA-256 per file. Format (one line per element, '\n' line ends):
//   floats   printf "%.9g" of the float widened to double (XML numbers: strtod, then rounded to float)
//   strings  as written, with TAB, CR and LF replaced by a space (XML attribute value normalisation)
//   attrs    " attrs " + "k=v" joined by ";" in byte order of k (every attribute as written)
//   lists    joined by ","; parts of an entry by ":"
// .fsmxml: "statemachine ..." + attrs, then per state "state ..." + attrs, per socket "socket ..." + attrs, per
// condition "condition ..." + attrs. .ftsxml: "trigger_system ..." + attrs, "event_track ...", "trigger ..." + attrs.
// .dge: "dge dags=N", per DAG "dag ...", per node "node ...", " in ...", " out ...", " data ..." (every data field,
// whatever the type). The field lists are in the functions below; logicdump.py mirrors them.
#include "c5models/logic.h"

#include <cstdio>

namespace c5m {

namespace {

std::string f9(float f) {
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", (double)f);
    return b;
}
std::string s(const std::string& v) {
    std::string o = v;
    for (char& c : o)
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return o;
}
std::string d(long long v) { return std::to_string(v); }
std::string attrs(const std::map<std::string, std::string>& a) {
    std::string o = " attrs ";
    bool first = true;
    for (auto& kv : a) {
        if (!first) o += ";";
        first = false;
        o += s(kv.first) + "=" + s(kv.second);
    }
    return o + "\n";
}
std::string floats(const std::vector<float>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + f9(v[i]);
    return o;
}

}  // namespace

const char* cond_type_name(CondType t) {
    switch (t) {
    case CondType::CheckPathOption: return "CheckPathOption";
    case CondType::EventSetPathOption: return "EventSetPathOption";
    case CondType::EventRandomPathOption: return "EventRandomPathOption";
    case CondType::OrOperator: return "OrOperator";
    case CondType::ECComplete: return "ECComplete";
    case CondType::CheckStateTime: return "CheckStateTime";
    case CondType::RandomRoll: return "RandomRoll";
    case CondType::AnyComplete: return "AnyComplete";
    case CondType::EventCopyVariable: return "EventCopyVariable";
    case CondType::EventSetVariable: return "EventSetVariable";
    case CondType::Variable: return "Variable";
    case CondType::True: return "True";
    case CondType::False: return "False";
    default: return "Unknown";
    }
}

const char* node_type_name(DagNodeType t) {
    switch (t) {
    case DagNodeType::PoseAnimSource: return "PoseAnimSource";
    case DagNodeType::Selection: return "Selection";
    case DagNodeType::Blend: return "Blend";
    case DagNodeType::BlockCurve: return "BlockCurve";
    case DagNodeType::ScalarCallback: return "ScalarCallback";
    case DagNodeType::ScalarSource: return "ScalarSource";
    case DagNodeType::Trackmask: return "Trackmask";
    case DagNodeType::Subgraph: return "Subgraph";
    case DagNodeType::Callback: return "Callback";
    case DagNodeType::ScalarSelect: return "ScalarSelect";
    case DagNodeType::ScalarSwitch: return "ScalarSwitch";
    case DagNodeType::ScaleOffset: return "ScaleOffset";
    case DagNodeType::Sequence: return "Sequence";
    case DagNodeType::TimeShift: return "TimeShift";
    case DagNodeType::Layer: return "Layer";
    default: return "Unknown";
    }
}

const char* trigger_type_name(TriggerType t) {
    switch (t) {
    case TriggerType::Sound: return "Sound";
    case TriggerType::StopSound: return "StopSound";
    case TriggerType::Effect: return "Effect";
    case TriggerType::Transfer: return "Transfer";
    case TriggerType::Action: return "Action";
    default: return "Unknown";
    }
}

std::string dump(const StateMachine& m) {
    std::string o = "statemachine fsm=" + s(m.fsm) + " default_state=" + d(m.default_state) + " states=" +
                    d((long long)m.states.size()) + " sockets=" + d((long long)m.sockets.size()) +
                    " dashed_comments=" + d(m.lenience.dashed_comments) + " text_elements=" +
                    d(m.lenience.text_elements) + "\n" + attrs(m.attrs);
    for (auto& st : m.states)
        o += "state id=" + d(st.id) + " type=" + s(st.type) + " name=" + s(st.name) + " asset=" + s(st.asset) +
             " action=" + d(st.action) + " socket_count=" + d(st.socket_count) + "\n" + attrs(st.attrs);
    for (auto& so : m.sockets) {
        o += "socket from=" + d(so.from) + " to=" + d(so.to) + " bidirectional=" + d(so.bidirectional) +
             " order=" + d(so.order) + " conditions=" + d((long long)so.conditions.size()) + "\n" + attrs(so.attrs);
        for (auto& c : so.conditions) {
            std::string w;
            for (size_t i = 0; i < c.weights.size(); ++i)
                w += (i ? "," : "") + d(c.weights[i].value) + ":" + f9(c.weights[i].weight);
            o += "condition type=" + std::string(cond_type_name(c.type)) + " type_name=" + s(c.type_name) +
                 " order=" + f9(c.order) + " method=" + d(c.method) + " path=" + d(c.path) + " value=" +
                 d(c.value) + " fvalue=" + f9(c.fvalue) + " use_weights=" + d(c.use_weights) + " invert=" +
                 d(c.invert) + " weights=" + w + "\n" + attrs(c.attrs);
        }
    }
    return o;
}

std::string dump(const TriggerSystem& t) {
    std::string o = "trigger_system type=" + s(t.type) + " event_tracks=" + d((long long)t.event_tracks.size()) +
                    " triggers=" + d((long long)t.triggers.size()) + " dashed_comments=" +
                    d(t.lenience.dashed_comments) + " text_elements=" + d(t.lenience.text_elements) + "\n" +
                    attrs(t.attrs);
    for (auto& et : t.event_tracks) {
        std::string k;
        for (size_t i = 0; i < et.tracks.size(); ++i)
            k += (i ? "," : "") + d(et.tracks[i].id) + ":" + s(et.tracks[i].name);
        o += "event_track ec=" + d(et.ec) + " tracks=" + k + "\n";
    }
    for (auto& tr : t.triggers)
        o += "trigger type=" + std::string(trigger_type_name(tr.type)) + " type_name=" + s(tr.type_name) +
             " id=" + d(tr.id) + " ec=" + d(tr.ec) + " track=" + d(tr.track) + " tier=" + d(tr.tier) + " time=" +
             f9(tr.time) + " duration=" + f9(tr.duration) + " repeat=" + d(tr.repeat) + "\n" + attrs(tr.attrs);
    return o;
}

std::string dump(const DagFile& f) {
    std::string o = "dge dags=" + d((long long)f.dags.size()) + "\n";
    for (auto& g : f.dags) {
        std::string src, masks;
        for (size_t i = 0; i < g.source_ecs.size(); ++i) src += (i ? "," : "") + d(g.source_ecs[i]);
        for (size_t i = 0; i < g.track_masks.size(); ++i) masks += (i ? "," : "") + s(g.track_masks[i]);
        o += "dag root=" + d(g.root) + " nodes=" + d((long long)g.nodes.size()) + " sources=" + src +
             " track_masks=" + masks + "\n";
        for (size_t i = 0; i < g.nodes.size(); ++i) {
            const DagNode& n = g.nodes[i];
            o += "node i=" + d((long long)i) + " type=" + node_type_name(n.type) + " type_name=" + s(n.type_name) +
                 " name=" + s(n.name) + " parent=" + d(n.parent) + " ext_in=" + d(n.external_input_edge_count) +
                 " ext_out=" + d(n.external_output_edge_count) + " has_data=" + d(n.has_node_data) +
                 " data_edges=" + d(n.data_edge_count) + "\n";
            o += " in ";
            for (size_t k = 0; k < n.inputs.size(); ++k) {
                const DagEdge& e = n.inputs[k];
                o += (k ? "," : "") + s(e.name) + ":" + d(e.parameter_type) + ":" + d(e.input_node) + ":" +
                     d(e.connected_output) + ":" + d(e.internal);
            }
            o += "\n out ";
            for (size_t k = 0; k < n.outputs.size(); ++k) {
                const DagEdge& e = n.outputs[k];
                o += (k ? "," : "") + s(e.name) + ":" + d(e.parameter_type) + ":" + d(e.internal);
            }
            std::string blocks, subnodes;
            for (size_t k = 0; k < n.blocks.size(); ++k)
                blocks += (k ? "," : "") + d(n.blocks[k].type) + ":" + d(n.blocks[k].flip_x) + ":" +
                          d(n.blocks[k].flip_y) + ":" + f9(n.blocks[k].duration);
            for (size_t k = 0; k < n.subnodes.size(); ++k) subnodes += (k ? "," : "") + d(n.subnodes[k]);
            o += "\n data src=" + d(n.source_index) + " tg=" + d(n.track_group_index) + " loop=" + d(n.loop_count) +
                 " speed=" + f9(n.speed) + " offset=" + f9(n.offset) + " clamped=" + d(n.clamped_loop) +
                 " cont=" + d(n.continue_offset) + " hascont=" + d(n.has_continue_offset) + " eti=" +
                 d(n.event_track_index) + " ets=" + d(n.event_track_specified) + " aco=" + d(n.add_clamp_offset) +
                 " def=" + d(n.default_edge) + " blocks=" + blocks + " scale=" + f9(n.scale) + " hint=" +
                 d(n.callback_hint) + " cval=" + f9(n.callback_value) + " hascval=" + d(n.has_callback_value) +
                 " shint=" + s(n.callback_string_hint) + " scalar=" + f9(n.scalar) + " smin=" + f9(n.scalar_min) +
                 " smax=" + f9(n.scalar_max) + " bind=" + d(n.binding_index) + " dw=" + f9(n.default_weight) +
                 " tw=" + f9(n.target_weight) + " currents=" + floats(n.edge_currents) + " subnodes=" + subnodes +
                 " mindur=" + f9(n.min_duration) + " items=" + floats(n.items) + " minitems=" + d(n.min_items) +
                 "\n";
        }
    }
    return o;
}

}  // namespace c5m
