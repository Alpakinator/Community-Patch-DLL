// c5models/logic.h - the unit logic files as plain data: state machines (.fsmxml), timed triggers (.ftsxml) and
// Granny blend DAGs (.dge) (epic 0374; design: research/05-graphics-composition/unit-controller.md sections 5 and 6;
// formats: model-reader.md 6.4 (b)-(d)). The .fxsxml ec bindings and <BoneUsage> are FxsAsset's (art.h).
//
// Data model fixed by the lead at C1 (2026-11-15) so that the readers (issue 4402), the runtime (4403) and the graph
// evaluator (4409) code against one header. 4402 owns this file from now on and may ADD fields and error codes; it
// changes or removes none without the lead. Nothing of a game file is embedded here or anywhere in the repository
// (D83): these readers run on the player's files at run time.
//
// Readers are strict: a malformed file is refused with Err::Xml (XML files) or the .gr2 reader's codes (.dge) and a
// text naming the element, attribute or node; nothing is half-filled on failure (out is cleared).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "c5models/art.h"   // XmlLenience (parse_xml's counted lenience)
#include "c5models/gr2.h"   // Error, Err

namespace c5m {

// ---------------------------------------------------------------------------------------------- .fsmxml
// Condition types the files use (model-reader.md 6.4 (b), 7 types over 69 machines) and the five more the EXE names.
enum class CondType {
    Unknown = 0,              // a type name outside this list: kept with its name; the runtime refuses it
    CheckPathOption,          // method, path, value
    EventSetPathOption,       // path, value (an action on the transition, not a test)
    EventRandomPathOption,    // path, weights (value, weight) pairs
    OrOperator,               // separates AND groups
    ECComplete,               // the current clip / graph has completed
    CheckStateTime,           // a time in the state
    RandomRoll,               // a per-update chance, value
    AnyComplete, EventCopyVariable, EventSetVariable, Variable, True, False,   // named by the EXE, unused by files [I]
};
struct FsmWeight { int32_t value = 0; float weight = 0; };
struct FsmCondition {
    CondType type = CondType::Unknown;
    std::string type_name;                         // as written (kept for Unknown and for the dump)
    float order = 0;                               // the condition's `order`; file order = evaluation order (4408)
    int32_t method = 0, path = -1, value = 0;      // CheckPathOption (method 1 equal, 6 not equal) /
                                                   // EventSetPathOption / EventRandomPathOption; -1 = absent;
                                                   // ECComplete: value = the ec; CheckStateTime: method (5, 1)
    float fvalue = 0;                              // RandomRoll chance, CheckStateTime seconds, ECComplete's
                                                   // optional fraction (0.98; 0 = absent); 4402: also the exact
                                                   // number of `value` for Check/EventSet/EventRandomPathOption
                                                   // (value = its nearest integer; leader files write 1.1)
    bool use_weights = false, invert = false;      // EventRandomPathOption useweights; RandomRoll invert
    std::vector<FsmWeight> weights;                // EventRandomPathOption
    std::map<std::string, std::string> attrs;      // every attribute as written (for the dump and for fields not
                                                   // modelled above; the canonical dump lists them sorted)
};
struct FsmSocket {
    int32_t from = -1, to = -1;                    // state ids (`from`, `too` as the files spell it)
    bool bidirectional = false;                    // 0 on all 716 reached sockets (4408)
    int32_t order = 0;                             // within a state the sockets are written in ascending order
    std::vector<FsmCondition> conditions;          // in file order (= evaluation order); the `conditions`
                                                   // attribute is their count (checked by the reader)
    std::map<std::string, std::string> attrs;      // every attribute as written
};
struct FsmState {
    int32_t id = -1;
    std::string type, name;                        // type "FGrannyState"
    std::string asset;                             // the .dge file name as written ("" for chooser states)
    int32_t action = 0;                            // the ec the state reports as current action
    int32_t socket_count = 0;                      // the `sockets` attribute: the number of outgoing sockets
                                                   // (299 of 299 states, 4408; corrected 2026-11-15 21:34 from
                                                   // "socket references")
    std::map<std::string, std::string> attrs;      // every attribute as written
};
struct StateMachine {
    std::string fsm;                               // root attribute fsm ("FGrannyStateMachine")
    int32_t default_state = -1;
    std::vector<FsmState> states;                  // file order
    std::vector<FsmSocket> sockets;                // file order; a state's outgoing sockets are those with from = id
    // 4402 (additive):
    std::map<std::string, std::string> attrs;      // root attributes as written
    XmlLenience lenience;                          // parse_xml's counted lenience for this file
};
bool parse_fsmxml(const char* data, size_t size, StateMachine& out, Error* err);

// ---------------------------------------------------------------------------------------------- .ftsxml
enum class TriggerType { Unknown = 0, Sound, StopSound, Effect, Transfer, Action };
struct FtsTrack { int32_t id = -1; std::string name; };          // Sound 1, Effect 2, Transfer 3, Action 4
struct FtsEventTrack { int32_t ec = 0; std::vector<FtsTrack> tracks; };
struct FtsTrigger {
    TriggerType type = TriggerType::Unknown;
    std::string type_name;
    int32_t id = -1, ec = 0, track = -1, tier = 0;
    float time = 0, duration = 0;                   // seconds from the clip start [I]
    bool repeat = false;
    std::map<std::string, std::string> attrs;       // every attribute as written (event, bone, refid, speed, ...)
};
struct TriggerSystem {
    std::string type;                               // "FGrannyTimedTriggerSystem"
    std::vector<FtsEventTrack> event_tracks;
    std::vector<FtsTrigger> triggers;               // file order
    // 4402 (additive):
    std::map<std::string, std::string> attrs;       // root attributes as written (type, lastid)
    XmlLenience lenience;                           // parse_xml's counted lenience for this file
};
bool parse_ftsxml(const char* data, size_t size, TriggerSystem& out, Error* err);

// ---------------------------------------------------------------------------------------------- .dge
enum class DagNodeType {
    Unknown = 0, PoseAnimSource, Selection, Blend, BlockCurve, ScalarCallback, ScalarSource, Trackmask, Subgraph,
    // named by the EXE's runtime (granny_dag2_*), not met in the unit graphs [C, census]:
    Callback, ScalarSelect, ScalarSwitch, ScaleOffset, Sequence, TimeShift,
    Layer,                                          // 4402: met in leader graphs only
};
struct DagEdge {
    int32_t parameter_type = 0;
    int32_t input_node = -1;                        // index into DagGraph::nodes, -1 = external
    int32_t connected_output = 0;
    std::string name;                               // EdgeName ("Selector", "Param", "From", "To", "Mask", ...)
    bool internal = false;
};
struct DagBlock { int32_t type = 0; bool flip_x = false, flip_y = false; float duration = 0; };
struct DagNode {
    DagNodeType type = DagNodeType::Unknown;
    std::string type_name;                          // the NodeData variant's type name as stored
    std::string name;
    int32_t parent = -1;
    std::vector<DagEdge> inputs, outputs;
    // NodeData, by type (fields the census names; 4402 adds any it finds):
    int32_t source_index = -1, track_group_index = -1, loop_count = 0;   // PoseAnimSource
    float speed = 1, offset = 0;
    bool clamped_loop = false, continue_offset = false;
    int32_t default_edge = 0;                       // Selection
    std::vector<DagBlock> blocks;                   // BlockCurve
    float scale = 1;
    int32_t callback_hint = 0;                      // ScalarCallback CallbackBindHint (5, 6, 7)
    float callback_value = 0, scalar = 0;           // ScalarCallback CallbackValue; ScalarSource value
    int32_t binding_index = 0;                      // Trackmask
    float default_weight = 1, target_weight = 0;
    // 4402 (additive):
    int32_t external_input_edge_count = 0, external_output_edge_count = 0;
    bool has_node_data = true;                      // false: NodeData is NULL (type Unknown, type_name "")
    int32_t data_edge_count = 0;                    // NodeData.Edges count (PoseAnimSource: its fields are Edges[0])
    int32_t event_track_index = 0;                  // PoseAnimSource Edges[0]
    bool event_track_specified = false, add_clamp_offset = false;
    bool has_continue_offset = false;               // ContinueOffset is absent from some (leader) files' type
    std::vector<float> edge_currents;               // NodeData.Edges[].Current (ScalarCallback, BlockCurve,
                                                    // ScalarSource, ScalarSelect)
    std::string callback_string_hint;               // ScalarCallback
    bool has_callback_value = false;                // ScalarCallback: CallbackValue is absent from some types
    float scalar_min = 0, scalar_max = 0;           // ScalarSource Edges[0] (scalar = Edges[0].Current)
    std::vector<int32_t> subnodes;                  // Subgraph SubnodeIndices
    float min_duration = 0;                         // Subgraph
    std::vector<float> items;                       // ScalarSelect Items[].Value
    int32_t min_items = 0;                          // ScalarSelect
};
struct DagGraph {
    int32_t root = -1;                              // RootNodeIndex
    std::vector<DagNode> nodes;
    std::vector<int32_t> source_ecs;                // Sources[k].EventCode
    std::vector<std::string> track_masks;           // TrackMasks[k].BindingName
};
struct DagFile { std::vector<DagGraph> dags; };
// Reads a .dge (same container as .gr2) through the existing reader.
bool parse_dge(const uint8_t* data, size_t size, DagFile& out, Error* err);
// 4402 (additive): with a string database for files whose header field 0x58 is non-zero (every unit and VP .dge has
// inline strings, field 0; the overload above passes none). Errors: the .gr2 reader's codes; MissingMember when the
// root has no DAGs or a node, edge, source or block entry is NULL.
bool parse_dge(const uint8_t* data, size_t size, const StringDatabase* strings, DagFile& out, Error* err);

// 4402 (additive): names for dumps and censuses ("CheckPathOption", "PoseAnimSource", "Action"; "Unknown").
const char* cond_type_name(CondType t);
const char* node_type_name(DagNodeType t);
const char* trigger_type_name(TriggerType t);

// ---------------------------------------------------------------------------------------------- dumps
// Canonical text dumps for DoD B (one line per element, attributes sorted, floats %.9g); the Python reader's dump
// (models/tools/logicdump/) writes the same text, so a file's two dumps are compared by hash.
std::string dump(const StateMachine& m);
std::string dump(const TriggerSystem& t);
std::string dump(const DagFile& d);

}  // namespace c5m
