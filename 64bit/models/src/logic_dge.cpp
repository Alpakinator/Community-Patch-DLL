// logic_dge.cpp - the .dge (Granny blend DAG, "AnimGraph") reader (epic 0374, issue 4402). Own code,
// GPL-3.0-or-later. Container = .gr2 (RawFile); root {DAGs[], ExtendedData}; per DAG {RootNodeIndex,
// InstanceDataSize, Nodes[], Sources[{EventCode}], TrackMasks[{BindingName}]}; per node {Name, ParentIndex, NodeXPos,
// NodeYPos, ExternalInputEdgeCount, InputEdges[], ExternalOutputEdgeCount, OutputEdges[], InstanceOffset,
// NodeData: variant whose type's first member is TypeMarker_NodeData<Type>}. Behaviour: unit-controller.md 6.3.
// Editor positions, InstanceDataSize, InstanceOffset and the output edges' ForwardIndex / Forwarded hold editor or
// run-time values and are not read.
#include "c5models/logic.h"

#include <new>

namespace c5m {

namespace {

DagNodeType node_type(const std::string& marker) {
    static const char kPrefix[] = "TypeMarker_NodeData";
    if (marker.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return DagNodeType::Unknown;
    std::string t = marker.substr(sizeof(kPrefix) - 1);
    static const struct { const char* name; DagNodeType type; } k[] = {
        {"PoseAnimSource", DagNodeType::PoseAnimSource}, {"Selection", DagNodeType::Selection},
        {"Blend", DagNodeType::Blend}, {"BlockCurve", DagNodeType::BlockCurve},
        {"ScalarCallback", DagNodeType::ScalarCallback}, {"ScalarSource", DagNodeType::ScalarSource},
        {"Trackmask", DagNodeType::Trackmask}, {"Subgraph", DagNodeType::Subgraph},
        {"Callback", DagNodeType::Callback}, {"ScalarSelect", DagNodeType::ScalarSelect},
        {"ScalarSwitch", DagNodeType::ScalarSwitch}, {"ScaleOffset", DagNodeType::ScaleOffset},
        {"Sequence", DagNodeType::Sequence}, {"TimeShift", DagNodeType::TimeShift}, {"Layer", DagNodeType::Layer},
    };
    for (auto& x : k)
        if (t == x.name) return x.type;
    return DagNodeType::Unknown;
}

bool flag(const Object& o, const char* name) { return o.i32(name) != 0; }

void read_edges(const Array& a, std::vector<DagEdge>& out, bool input) {
    for (uint32_t i = 0; i < a.size(); ++i) {
        Object e = a.at(i);
        if (!e) throw ParseError(Err::MissingMember, "NULL edge");
        DagEdge d;
        d.parameter_type = e.i32("ParameterType");
        d.name = e.str("EdgeName");
        d.internal = flag(e, "Internal");
        if (input) {
            d.input_node = e.i32("InputNodeIndex");
            d.connected_output = e.i32("ConnectedOutput");
        }
        out.push_back(std::move(d));
    }
}

void read_node_data(const Object& nd, DagNode& n) {
    n.type_name = nd.type()->members.empty() ? std::string() : nd.type()->members[0].name;
    n.type = node_type(n.type_name);
    Array edges;
    if (nd.has("Edges")) {
        edges = nd.array("Edges");
        n.data_edge_count = (int32_t)edges.size();
        for (uint32_t i = 0; i < edges.size(); ++i) {
            Object e = edges.at(i);
            if (e && e.has("Current")) n.edge_currents.push_back(e.f32("Current"));
        }
    }
    switch (n.type) {
    case DagNodeType::PoseAnimSource:
        if (edges.size()) {
            Object e = edges.at(0);
            if (!e) throw ParseError(Err::MissingMember, "NULL PoseAnimSource edge");
            n.source_index = e.i32("SourceIndex");
            n.track_group_index = e.i32("TrackGroupIndex");
            n.event_track_index = e.i32("EventTrackIndex");
            n.event_track_specified = flag(e, "EventTrackSpecified");
            n.speed = e.f32("Speed");
            n.offset = e.f32("Offset");
            n.loop_count = e.i32("LoopCount");
            n.clamped_loop = flag(e, "ClampedLoop");
            n.add_clamp_offset = flag(e, "AddClampOffset");
            n.has_continue_offset = e.has("ContinueOffset");
            if (n.has_continue_offset) n.continue_offset = flag(e, "ContinueOffset");
        }
        break;
    case DagNodeType::Selection:
        n.default_edge = nd.i32("DefaultEdge");
        break;
    case DagNodeType::BlockCurve: {
        Array b = nd.array("Blocks");
        for (uint32_t i = 0; i < b.size(); ++i) {
            Object o = b.at(i);
            if (!o) throw ParseError(Err::MissingMember, "NULL block");
            DagBlock k;
            k.type = o.i32("BlockType");
            k.flip_x = flag(o, "FlipX");
            k.flip_y = flag(o, "FlipY");
            k.duration = o.f32("Duration");
            n.blocks.push_back(k);
        }
        n.scale = nd.f32("Scale");
        break;
    }
    case DagNodeType::ScalarCallback:
        n.callback_hint = nd.i32("CallbackBindHint");
        n.callback_string_hint = nd.str("CallbackStringHint");
        n.has_callback_value = nd.has("CallbackValue");
        if (n.has_callback_value) n.callback_value = nd.f32("CallbackValue");
        break;
    case DagNodeType::ScalarSource:
        if (edges.size()) {
            Object e = edges.at(0);
            if (!e) throw ParseError(Err::MissingMember, "NULL ScalarSource edge");
            n.scalar = e.f32("Current");
            n.scalar_min = e.f32("Min");
            n.scalar_max = e.f32("Max");
        }
        break;
    case DagNodeType::Trackmask:
        n.binding_index = nd.i32("BindingIndex");
        n.default_weight = nd.f32("DefaultWeight");
        n.target_weight = nd.f32("TargetWeight");
        break;
    case DagNodeType::Subgraph: {
        Array s = nd.array("SubnodeIndices");
        for (uint32_t i = 0; i < s.size(); ++i) {
            Object o = s.at(i);
            if (!o) throw ParseError(Err::MissingMember, "NULL subnode index");
            n.subnodes.push_back(o.i32("Int32"));
        }
        n.min_duration = nd.f32("MinDuration");
        break;
    }
    case DagNodeType::ScalarSelect: {
        Array s = nd.array("Items");
        for (uint32_t i = 0; i < s.size(); ++i) {
            Object o = s.at(i);
            if (!o) throw ParseError(Err::MissingMember, "NULL item");
            n.items.push_back(o.f32("Value"));
        }
        n.min_items = nd.i32("MinItems");
        break;
    }
    default:
        break;
    }
}

void read_dag(const Object& d, DagGraph& g) {
    g.root = d.i32("RootNodeIndex");
    Array nodes = d.array("Nodes");
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        Object o = nodes.at(i);
        if (!o) throw ParseError(Err::MissingMember, "NULL node " + std::to_string(i));
        DagNode n;
        n.name = o.str("Name");
        n.parent = o.i32("ParentIndex");
        n.external_input_edge_count = o.i32("ExternalInputEdgeCount");
        n.external_output_edge_count = o.i32("ExternalOutputEdgeCount");
        read_edges(o.array("InputEdges"), n.inputs, true);
        read_edges(o.array("OutputEdges"), n.outputs, false);
        Object nd = o.variant("NodeData");
        n.has_node_data = !nd.null();
        if (n.has_node_data) read_node_data(nd, n);
        g.nodes.push_back(std::move(n));
    }
    Array src = d.array("Sources");
    for (uint32_t i = 0; i < src.size(); ++i) {
        Object o = src.at(i);
        if (!o) throw ParseError(Err::MissingMember, "NULL source");
        g.source_ecs.push_back(o.i32("EventCode"));
    }
    Array masks = d.array("TrackMasks");
    for (uint32_t i = 0; i < masks.size(); ++i) {
        Object o = masks.at(i);
        if (!o) throw ParseError(Err::MissingMember, "NULL track mask");
        g.track_masks.push_back(o.str("BindingName"));
    }
}

}  // namespace

bool parse_dge(const uint8_t* data, size_t size, const StringDatabase* strings, DagFile& out, Error* err) {
    out = DagFile();
    RawFile f;
    if (!f.load(data, size, strings, err)) return false;
    DagFile d;
    try {
        Object root = f.root();
        if (!root || !root.has("DAGs")) throw ParseError(Err::MissingMember, "root has no DAGs (not a .dge)");
        Array dags = root.array("DAGs");
        for (uint32_t i = 0; i < dags.size(); ++i) {
            Object o = dags.at(i);
            if (!o) throw ParseError(Err::MissingMember, "NULL DAG " + std::to_string(i));
            DagGraph g;
            read_dag(o, g);
            d.dags.push_back(std::move(g));
        }
    } catch (const ParseError& e) {
        if (err) *err = {e.code, e.what()};
        return false;
    } catch (const std::bad_alloc&) {
        if (err) *err = {Err::OutOfMemory, "out of memory reading a .dge"};
        return false;
    }
    out = std::move(d);
    return true;
}

bool parse_dge(const uint8_t* data, size_t size, DagFile& out, Error* err) {
    return parse_dge(data, size, nullptr, out, err);
}

}  // namespace c5m
