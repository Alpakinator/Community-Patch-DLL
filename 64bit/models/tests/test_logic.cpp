// test_logic.cpp - the logic file readers (c5models/logic.h; epic 0374, issue 4402): small synthetic documents
// written here (no game file text, D83), a synthetic .dge built with gr2_writer.h, every refusal (DoD G) and the
// counted XML lenience. The readers' run over the real files is the DoD B census (models/tools/logicdump).
#include "c5models/logic.h"
#include "gr2_writer.h"
#include "testing.h"

#include <cstring>
#include <string>

using namespace c5m;
using namespace gr2w;

namespace {

const char* kFsm =
    "<statemachine fsm=\"FGrannyStateMachine\" default_state=\"2\">\n"
    "  <states>\n"
    "    <state id=\"1\" type=\"FGrannyState\" x=\"0\" y=\"0\" name=\"A\" asset=\"a.dge\" action=\"1000\" sockets=\"1\" />\n"
    "    <state id=\"2\" type=\"FGrannyState\" name=\"B\" asset=\"\" action=\"0\" sockets=\"1\" />\n"
    "  </states>\n"
    "  <sockets>\n"
    "    <socket from=\"2\" too=\"1\" bidirectional=\"0\" order=\"3\" conditions=\"7\">\n"
    "      <conditions>\n"
    "        <condition type=\"FFSMConditionCheckPathOption\" order=\"0.25\" desc=\"\" method=\"6\" path=\"4\" value=\"1500\" />\n"
    "        <condition type=\"FFSMConditionEventRandomPathOption\" order=\"0.5\" desc=\"\" path=\"0\" value=\"1\" useweights=\"1\">\n"
    "          <weights><weight v=\"0\" w=\"0.6\" /><weight v=\"1\" w=\"0.4\" /></weights>\n"
    "        </condition>\n"
    "        <condition type=\"FFSMConditionOrOperator\" order=\"0.6\" desc=\"\" />\n"
    "        <condition type=\"FFSMConditionECComplete\" order=\"0.7\" desc=\"\" ec=\"1100\" value=\"0.98\" />\n"
    "        <condition type=\"FFSMConditionCheckStateTime\" order=\"0.8\" desc=\"\" method=\"5\" value=\"0.15\" />\n"
    "        <condition type=\"FFSMConditionRandomRoll\" order=\"0.9\" desc=\"\" value=\"0.0007\" invert=\"0\" />\n"
    "        <condition type=\"FFSMConditionSomethingNew\" order=\"1\" desc=\"\" />\n"
    "      </conditions>\n"
    "    </socket>\n"
    "    <socket from=\"1\" too=\"2\" bidirectional=\"0\" order=\"0\" conditions=\"1\">\n"
    "      <conditions><condition type=\"FFSMConditionEventSetPathOption\" order=\"0\" desc=\"\" path=\"4\" value=\"1.0\" /></conditions>\n"
    "    </socket>\n"
    "  </sockets>\n"
    "</statemachine>\n";

const char* kFts =
    "<trigger_system type=\"FGrannyTimedTriggerSystem\" lastid=\"9\">\n"
    "  <event_tracks>\n"
    "    <event_track ec=\"1100\"><track id=\"1\" name=\"Sound\" /><track id=\"4\" name=\"Action\" /></event_track>\n"
    "  </event_tracks>\n"
    "  <triggers>\n"
    "    <trigger type=\"FTimedTriggerAction\" id=\"3\" time=\"0.4889\" duration=\"0\" repeat=\"0\" ec=\"1100\" track=\"4\" tier=\"0\" action=\"1\" />\n"
    "    <trigger type=\"FTimedTriggerSound\" id=\"4\" time=\"-0.003\" duration=\"0.5\" repeat=\"0\" ec=\"1100\" track=\"1\" tier=\"-1\" event=\"X\" bone=\"\" />\n"
    "    <trigger type=\"FTimedTriggerSomethingElse\" id=\"5\" time=\"1\" />\n"
    "  </triggers>\n"
    "</trigger_system>\n";

bool fsm_refused(const std::string& doc, const char* why) {
    StateMachine m;
    m.default_state = 77;   // must be cleared on failure
    Error e;
    bool ok = parse_fsmxml(doc.data(), doc.size(), m, &e);
    std::printf("  fsm %-44s -> %s %s\n", why, ok ? "READ" : err_name(e.code), e.what.c_str());
    return !ok && e.code == Err::Xml && m.default_state == -1 && m.states.empty() && !e.what.empty();
}

bool fts_refused(const std::string& doc, const char* why) {
    TriggerSystem t;
    t.type = "stale";
    Error e;
    bool ok = parse_ftsxml(doc.data(), doc.size(), t, &e);
    std::printf("  fts %-44s -> %s %s\n", why, ok ? "READ" : err_name(e.code), e.what.c_str());
    return !ok && e.code == Err::Xml && t.type.empty() && t.triggers.empty() && !e.what.empty();
}

std::string replace(std::string s, const std::string& a, const std::string& b) {
    size_t p = s.find(a);
    if (p == std::string::npos) {
        std::printf("FAIL test setup: '%s' not in the document\n", a.c_str());
        ++t::failures();
        return s;
    }
    return s.replace(p, a.size(), b);
}

void test_fsm() {
    StateMachine m;
    Error e;
    CHECK(parse_fsmxml(kFsm, std::strlen(kFsm), m, &e));
    CHECK_EQ(m.fsm, std::string("FGrannyStateMachine"));
    CHECK_EQ(m.default_state, 2);
    CHECK_EQ(m.states.size(), (size_t)2);
    CHECK_EQ(m.states[0].asset, std::string("a.dge"));
    CHECK_EQ(m.states[0].action, 1000);
    CHECK_EQ(m.states[0].socket_count, 1);
    CHECK_EQ(m.states[0].attrs.at("x"), std::string("0"));
    CHECK_EQ(m.sockets.size(), (size_t)2);
    const FsmSocket& so = m.sockets[0];
    CHECK_EQ(so.from, 2);
    CHECK_EQ(so.to, 1);
    CHECK_EQ(so.order, 3);
    CHECK(!so.bidirectional);
    CHECK_EQ(so.conditions.size(), (size_t)7);
    const FsmCondition& c0 = so.conditions[0];
    CHECK(c0.type == CondType::CheckPathOption);
    CHECK_EQ(c0.method, 6);
    CHECK_EQ(c0.path, 4);
    CHECK_EQ(c0.value, 1500);
    CHECK_EQ(c0.fvalue, 1500.0f);
    CHECK_EQ(c0.order, 0.25f);
    const FsmCondition& c1 = so.conditions[1];
    CHECK(c1.type == CondType::EventRandomPathOption);
    CHECK(c1.use_weights);
    CHECK_EQ(c1.weights.size(), (size_t)2);
    CHECK_EQ(c1.weights[1].value, 1);
    CHECK_EQ(c1.weights[1].weight, 0.4f);
    CHECK(so.conditions[2].type == CondType::OrOperator);
    CHECK(so.conditions[3].type == CondType::ECComplete);
    CHECK_EQ(so.conditions[3].value, 1100);
    CHECK_EQ(so.conditions[3].fvalue, 0.98f);
    CHECK(so.conditions[4].type == CondType::CheckStateTime);
    CHECK_EQ(so.conditions[4].method, 5);
    CHECK_EQ(so.conditions[4].fvalue, 0.15f);
    CHECK(so.conditions[5].type == CondType::RandomRoll);
    CHECK_EQ(so.conditions[5].fvalue, 0.0007f);
    CHECK(!so.conditions[5].invert);
    CHECK(so.conditions[6].type == CondType::Unknown);   // kept by name; the runtime refuses it
    CHECK_EQ(so.conditions[6].type_name, std::string("FFSMConditionSomethingNew"));
    CHECK_EQ(m.sockets[1].conditions[0].value, 1);      // "1.0" is a number the machines compare as 1
    CHECK_EQ(m.lenience.dashed_comments, 0);
    CHECK_EQ(m.lenience.text_elements, 0);
    std::string d = dump(m);
    CHECK(d.find("condition type=CheckPathOption type_name=FFSMConditionCheckPathOption order=0.25 method=6 path=4 "
                 "value=1500 fvalue=1500 use_weights=0 invert=0 weights=\n") != std::string::npos);
    CHECK(d.find("weights=0:0.600000024,1:0.400000006\n") != std::string::npos);
    CHECK(d.find(" attrs action=1000;asset=a.dge;id=1;name=A;sockets=1;type=FGrannyState;x=0;y=0\n") != std::string::npos);

    // refusals (DoD G): each names the element and line; the output is cleared
    std::string doc(kFsm);
    CHECK(fsm_refused("<statemachine default_state=\"1\"><states></statemachine>", "malformed XML (unclosed)"));
    CHECK(fsm_refused(replace(doc, "<statemachine fsm", "<machine fsm"), "wrong root"));
    CHECK(fsm_refused(replace(doc, "default_state=\"2\"", ""), "root without default_state"));
    CHECK(fsm_refused(replace(doc, "default_state=\"2\"", "default_state=\"two\""), "default_state not a number"));
    CHECK(fsm_refused("<statemachine fsm=\"F\" default_state=\"1\"><sockets/></statemachine>", "no <states>"));
    CHECK(fsm_refused(replace(doc, "  </states>\n", "  </states>\n<states/>"), "<states> twice"));
    CHECK(fsm_refused(replace(doc, "  <sockets>\n", "  <extra/><sockets>\n"), "unknown element in root"));
    CHECK(fsm_refused(replace(doc, "<state id=\"1\"", "<stat id=\"1\""), "unknown element in <states>"));
    CHECK(fsm_refused(replace(doc, "<state id=\"1\"", "<state"), "state without id"));
    CHECK(fsm_refused(replace(doc, "<state id=\"1\"", "<state id=\"1.5\""), "state id not an integer"));
    CHECK(fsm_refused(replace(doc, "sockets=\"1\" />\n    <state id=\"2\"", "sockets=\"1\"><x/></state>\n    <state id=\"2\""),
                      "child of <state>"));
    CHECK(fsm_refused(replace(doc, "<socket from=\"2\"", "<socket"), "socket without from"));
    CHECK(fsm_refused(replace(doc, "too=\"1\"", "to=\"1\""), "socket without too"));
    CHECK(fsm_refused(replace(doc, "conditions=\"7\"", "conditions=\"6\""), "conditions count mismatch"));
    CHECK(fsm_refused(replace(doc, "bidirectional=\"0\" order=\"3\"", "bidirectional=\"maybe\" order=\"3\""),
                      "bidirectional not 0 or 1"));
    CHECK(fsm_refused(replace(doc, "<conditions>\n        <condition type=\"FFSMConditionCheckPathOption\"",
                              "<conditions>\n        <condition"), "condition without type"));
    CHECK(fsm_refused(replace(doc, "method=\"6\" path=\"4\"", "method=\"6\""), "path condition without path"));
    CHECK(fsm_refused(replace(doc, "value=\"1500\"", "value=\"x\""), "path value not a number"));
    CHECK(fsm_refused(replace(doc, "ec=\"1100\" value=\"0.98\"", "value=\"0.98\""), "ECComplete without ec"));
    CHECK(fsm_refused(replace(doc, "method=\"5\" value=\"0.15\"", "method=\"5\""), "CheckStateTime without value"));
    CHECK(fsm_refused(replace(doc, "value=\"0.0007\"", "value=\"\""), "RandomRoll value empty"));
    CHECK(fsm_refused(replace(doc, "<weights>", "<weights><bad/>"), "unknown element in <weights>"));
    CHECK(fsm_refused(replace(doc, "<weight v=\"0\" w=\"0.6\" />", "<weight w=\"0.6\" />"), "weight without v"));
    CHECK(fsm_refused(replace(doc, "<weight v=\"0\" w=\"0.6\" />", "<weight v=\"0\" />"), "weight without w"));
    CHECK(fsm_refused(replace(doc, "<weights>", "<weightz>"), "unknown element in <condition>"));
    CHECK(fsm_refused(replace(doc, "</conditions>\n    </socket>\n    <socket", "</conditions><conditions/>\n    </socket>\n    <socket"),
                      "<conditions> twice"));
    CHECK(fsm_refused(replace(doc, "<conditions>\n", "<condition>\n"), "unknown element in <socket>"));
}

void test_fts() {
    TriggerSystem t;
    Error e;
    CHECK(parse_ftsxml(kFts, std::strlen(kFts), t, &e));
    CHECK_EQ(t.type, std::string("FGrannyTimedTriggerSystem"));
    CHECK_EQ(t.attrs.at("lastid"), std::string("9"));
    CHECK_EQ(t.event_tracks.size(), (size_t)1);
    CHECK_EQ(t.event_tracks[0].ec, 1100);
    CHECK_EQ(t.event_tracks[0].tracks[1].id, 4);
    CHECK_EQ(t.event_tracks[0].tracks[1].name, std::string("Action"));
    CHECK_EQ(t.triggers.size(), (size_t)3);
    CHECK(t.triggers[0].type == TriggerType::Action);
    CHECK_EQ(t.triggers[0].time, 0.4889f);
    CHECK_EQ(t.triggers[0].attrs.at("action"), std::string("1"));
    CHECK(t.triggers[1].type == TriggerType::Sound);
    CHECK_EQ(t.triggers[1].tier, -1);
    CHECK_EQ(t.triggers[1].time, -0.003f);
    CHECK_EQ(t.triggers[1].duration, 0.5f);
    CHECK(t.triggers[2].type == TriggerType::Unknown);
    CHECK_EQ(t.triggers[2].track, -1);   // absent
    std::string d = dump(t);
    CHECK(d.find("trigger type=Action type_name=FTimedTriggerAction id=3 ec=1100 track=4 tier=0 time=0.488900006 "
                 "duration=0 repeat=0\n") != std::string::npos);

    std::string doc(kFts);
    CHECK(fts_refused(replace(doc, "<trigger_system type", "<triggers_system type"), "wrong root"));
    CHECK(fts_refused(replace(doc, "  <triggers>\n", "  <other/><triggers>\n"), "unknown element in root"));
    CHECK(fts_refused(replace(doc, "  </triggers>\n", "  </triggers><triggers/>\n"), "<triggers> twice"));
    CHECK(fts_refused(replace(doc, "<event_track ec=\"1100\">", "<event_track>"), "event_track without ec"));
    CHECK(fts_refused(replace(doc, "<track id=\"1\"", "<track"), "track without id"));
    CHECK(fts_refused(replace(doc, "<track id=\"1\" name=\"Sound\" />", "<trk id=\"1\" />"), "unknown element in <event_track>"));
    CHECK(fts_refused(replace(doc, "<trigger type=\"FTimedTriggerAction\"", "<trigger"), "trigger without type"));
    CHECK(fts_refused(replace(doc, "time=\"0.4889\"", "time=\"0.4889s\""), "time not a number"));
    CHECK(fts_refused(replace(doc, "id=\"3\"", "id=\"3.5\""), "id not an integer"));
    CHECK(fts_refused(replace(doc, "action=\"1\" />", "action=\"1\"><x/></trigger>"), "child of <trigger>"));
    CHECK(fts_refused(replace(doc, "repeat=\"0\" ec=\"1100\" track=\"4\"", "repeat=\"2\" ec=\"1100\" track=\"4\""),
                      "repeat not 0 or 1"));
    CHECK(fts_refused(replace(doc, "<triggers>", "<trigger>"), "unknown element under root"));
}

// The one lenience (MC #2017): a comment ends at the first "-->" (counted when its body holds "--"); character data
// is skipped (counted). Everything else malformed is refused.
void test_lenience() {
    std::string doc(kFts);
    std::string nested = replace(doc, "    <trigger type=\"FTimedTriggerSound\"",
                                 "    <!-- an opened comment\n    <trigger type=\"FTimedTriggerEffect\" id=\"8\" />\n"
                                 "    <!-- inner -->\n    <trigger type=\"FTimedTriggerSound\"");
    TriggerSystem t;
    Error e;
    CHECK(parse_ftsxml(nested.data(), nested.size(), t, &e));
    CHECK_EQ(t.triggers.size(), (size_t)3);              // the commented trigger dropped out
    CHECK_EQ(t.lenience.dashed_comments, 1);
    CHECK_EQ(t.lenience.text_elements, 0);
    std::string stray = replace(doc, "  <triggers>\n", "  stray words\n  <triggers>\n");
    CHECK(parse_ftsxml(stray.data(), stray.size(), t, &e));
    CHECK_EQ(t.lenience.text_elements, 1);
    CHECK_EQ(t.lenience.dashed_comments, 0);
    // still refused
    CHECK(fts_refused(replace(doc, "</trigger_system>", "<!-- never closed\n</trigger_system>"), "unterminated comment"));
    CHECK(fts_refused(replace(doc, "lastid=\"9\"", "lastid=9"), "unquoted attribute"));
    CHECK(fts_refused(replace(doc, "lastid=\"9\"", "lastid=\"9\" lastid=\"9\""), "duplicate attribute"));
    CHECK(fts_refused(replace(doc, "</triggers>", "</trigger>"), "mismatched end tag"));
    CHECK(fts_refused(doc + "<trigger_system/>", "second root"));
    CHECK(fts_refused(replace(doc, "event=\"X\"", "event=\"&bogus;\""), "unknown entity"));
    // the plain scanner keeps its behaviour for other callers: no lenience pointer, same result
    XmlElement root;
    XmlLenience len;
    CHECK(parse_xml(nested.data(), nested.size(), root, &e));
    CHECK(parse_xml(nested.data(), nested.size(), root, &e, &len));
    CHECK_EQ(len.dashed_comments, 1);
}

// Synthetic .dge: root {DAGs[]}; DAG {RootNodeIndex, Nodes[], Sources[], TrackMasks[]}; three nodes: a Subgraph fed by
// a PoseAnimSource, and a ScalarCallback; plus one node of an unknown type and one with NULL NodeData.
Bytes make_dge(bool with_dags = true) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    const uint32_t I32 = 19, R32 = 10, STR = 8, RTA = 3, VAR = 5;
    TypeWriter Ti(t); Ti.member(I32, "Int32"); uint32_t tInt = Ti.end();
    TypeWriter Tc(t); Tc.member(R32, "Current"); uint32_t tCur = Tc.end();
    TypeWriter Tp(t);
    Tp.member(I32, "SourceIndex"); Tp.member(I32, "TrackGroupIndex"); Tp.member(I32, "EventTrackIndex");
    Tp.member(I32, "EventTrackSpecified"); Tp.member(R32, "Speed"); Tp.member(R32, "Offset"); Tp.member(I32, "LoopCount");
    Tp.member(I32, "ClampedLoop"); Tp.member(I32, "AddClampOffset"); Tp.member(I32, "ContinueOffset");
    uint32_t tPaEdge = Tp.end();
    TypeWriter Ts(t); Ts.member(I32, "TypeMarker_NodeDataSubgraph"); Ts.member(RTA, "SubnodeIndices", tInt);
    Ts.member(R32, "MinDuration"); uint32_t tNdSub = Ts.end();
    TypeWriter Ta(t); Ta.member(I32, "TypeMarker_NodeDataPoseAnimSource"); Ta.member(RTA, "Edges", tPaEdge);
    uint32_t tNdPas = Ta.end();
    TypeWriter Tb(t); Tb.member(I32, "TypeMarker_NodeDataScalarCallback"); Tb.member(I32, "CallbackBindHint");
    Tb.member(STR, "CallbackStringHint"); Tb.member(RTA, "Edges", tCur); Tb.member(R32, "CallbackValue");
    uint32_t tNdCb = Tb.end();
    TypeWriter Tu(t); Tu.member(I32, "TypeMarker_NodeDataSomethingNew"); uint32_t tNdNew = Tu.end();
    TypeWriter Te(t); Te.member(I32, "ParameterType"); Te.member(I32, "InputNodeIndex"); Te.member(I32, "ConnectedOutput");
    Te.member(STR, "EdgeName"); Te.member(I32, "Internal"); uint32_t tIn = Te.end();
    TypeWriter To(t); To.member(I32, "ParameterType"); To.member(STR, "EdgeName"); To.member(I32, "Internal");
    uint32_t tOut = To.end();
    TypeWriter Tn(t); Tn.member(STR, "Name"); Tn.member(I32, "ParentIndex"); Tn.member(I32, "ExternalInputEdgeCount");
    Tn.member(RTA, "InputEdges", tIn); Tn.member(I32, "ExternalOutputEdgeCount"); Tn.member(RTA, "OutputEdges", tOut);
    Tn.member(VAR, "NodeData"); uint32_t tNode = Tn.end();
    TypeWriter Tsrc(t); Tsrc.member(I32, "EventCode"); uint32_t tSrc = Tsrc.end();
    TypeWriter Tm(t); Tm.member(STR, "BindingName"); uint32_t tMask = Tm.end();
    TypeWriter Td(t); Td.member(I32, "RootNodeIndex"); Td.member(RTA, "Nodes", tNode); Td.member(RTA, "Sources", tSrc);
    Td.member(RTA, "TrackMasks", tMask); uint32_t tDag = Td.end();
    TypeWriter Tr(t);
    if (with_dags) Tr.member(RTA, "DAGs", tDag);
    else Tr.member(I32, "Something");
    uint32_t tRoot = Tr.end();
    finish_names(t, {&Ti, &Tc, &Tp, &Ts, &Ta, &Tb, &Tu, &Te, &To, &Tn, &Tsrc, &Tm, &Td, &Tr});

    uint32_t root = o.put32(1);
    uint32_t root_ptr = o.put32(0);
    if (!with_dags) return write_file(s, tRoot, root);
    // DAG
    uint32_t dag = o.put32(0);   // RootNodeIndex
    o.put32(5); uint32_t nodes_ptr = o.put32(0);
    o.put32(1); uint32_t src_ptr = o.put32(0);
    o.put32(1); uint32_t mask_ptr = o.put32(0);
    o.ptr(root_ptr, S_OBJ, dag);
    // nodes (each 40 bytes): Name, ParentIndex, ExtIn, InputEdges(count, ptr), ExtOut, OutputEdges(count, ptr), NodeData(type, obj)
    struct N { uint32_t name, in_cnt, in_ptr, out_cnt, out_ptr, nd_type, nd_obj; };
    N n[5];
    uint32_t nodes = (uint32_t)o.b.size();
    for (int i = 0; i < 5; ++i) {
        n[i].name = o.put32(0); o.put32(i ? 0 : (uint32_t)-1); o.put32(i ? 0 : 1);
        n[i].in_cnt = o.put32(0); n[i].in_ptr = o.put32(0); o.put32(i == 0 ? 1 : 0);
        n[i].out_cnt = o.put32(0); n[i].out_ptr = o.put32(0);
        n[i].nd_type = o.put32(0); n[i].nd_obj = o.put32(0);
    }
    o.ptr(nodes_ptr, S_OBJ, nodes);
    const char* names[5] = {"out", "src", "cb", "new", "empty"};
    for (int i = 0; i < 5; ++i) o.ptr(n[i].name, S_OBJ, o.cstr(names[i]));
    // node 0 input edge "Pose out" <- node 1; output edge "Pose out"
    uint32_t in0 = o.put32(1); o.put32(1); o.put32(0); uint32_t in0_name = o.put32(0); o.put32(1);
    o.ptr(in0_name, S_OBJ, o.cstr("Pose out"));
    o.set32(n[0].in_cnt, 1); o.ptr(n[0].in_ptr, S_OBJ, in0);
    uint32_t out0 = o.put32(1); uint32_t out0_name = o.put32(0); o.put32(0);
    o.ptr(out0_name, S_OBJ, o.cstr("Pose out"));
    o.set32(n[0].out_cnt, 1); o.ptr(n[0].out_ptr, S_OBJ, out0);
    // node data
    uint32_t sub = o.put32(0); o.put32(2); uint32_t sub_ptr = o.put32(0); o.putf(0.0f);
    uint32_t subs = o.put32(1); o.put32(2);
    o.ptr(sub_ptr, S_OBJ, subs);
    o.ptr(n[0].nd_type, S_TYPE, tNdSub); o.ptr(n[0].nd_obj, S_OBJ, sub);
    uint32_t pas = o.put32(0); o.put32(1); uint32_t pas_ptr = o.put32(0);
    uint32_t pe = o.put32(0); o.put32((uint32_t)-1); o.put32(0); o.put32(0); o.putf(1.0f); o.putf(0.0f); o.put32(1);
    o.put32(1); o.put32(0); o.put32(1);
    o.ptr(pas_ptr, S_OBJ, pe);
    o.ptr(n[1].nd_type, S_TYPE, tNdPas); o.ptr(n[1].nd_obj, S_OBJ, pas);
    uint32_t cb = o.put32(0); o.put32(7); uint32_t cb_hint = o.put32(0); o.put32(2); uint32_t cb_ptr = o.put32(0);
    o.putf(4.0f);
    uint32_t cur = o.putf(0.0f); o.putf(0.25f);
    o.ptr(cb_hint, S_OBJ, o.cstr(""));
    o.ptr(cb_ptr, S_OBJ, cur);
    o.ptr(n[2].nd_type, S_TYPE, tNdCb); o.ptr(n[2].nd_obj, S_OBJ, cb);
    uint32_t nw = o.put32(0);
    o.ptr(n[3].nd_type, S_TYPE, tNdNew); o.ptr(n[3].nd_obj, S_OBJ, nw);
    // node 4: NULL NodeData (no fix-ups)
    uint32_t src = o.put32(1000);
    o.ptr(src_ptr, S_OBJ, src);
    uint32_t mask = o.put32(0);
    o.ptr(mask, S_OBJ, o.cstr("MASK_A"));
    o.ptr(mask_ptr, S_OBJ, mask);
    return write_file(s, tRoot, root);
}

void test_dge() {
    Bytes b = make_dge();
    DagFile f;
    Error e;
    bool ok = parse_dge(b.data(), b.size(), f, &e);
    if (!ok) std::printf("  dge error %s %s\n", err_name(e.code), e.what.c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK_EQ(f.dags.size(), (size_t)1);
    const DagGraph& g = f.dags[0];
    CHECK_EQ(g.root, 0);
    CHECK_EQ(g.nodes.size(), (size_t)5);
    CHECK_EQ(g.source_ecs.size(), (size_t)1);
    CHECK_EQ(g.source_ecs[0], 1000);
    CHECK_EQ(g.track_masks.size(), (size_t)1);
    CHECK_EQ(g.track_masks[0], std::string("MASK_A"));
    const DagNode& n0 = g.nodes[0];
    CHECK(n0.type == DagNodeType::Subgraph);
    CHECK_EQ(n0.type_name, std::string("TypeMarker_NodeDataSubgraph"));
    CHECK_EQ(n0.name, std::string("out"));
    CHECK_EQ(n0.parent, -1);
    CHECK_EQ(n0.external_input_edge_count, 1);
    CHECK_EQ(n0.inputs.size(), (size_t)1);
    CHECK_EQ(n0.inputs[0].name, std::string("Pose out"));
    CHECK_EQ(n0.inputs[0].input_node, 1);
    CHECK_EQ(n0.inputs[0].parameter_type, 1);
    CHECK(n0.inputs[0].internal);
    CHECK_EQ(n0.outputs.size(), (size_t)1);
    CHECK_EQ(n0.subnodes.size(), (size_t)2);
    CHECK_EQ(n0.subnodes[1], 2);
    const DagNode& n1 = g.nodes[1];
    CHECK(n1.type == DagNodeType::PoseAnimSource);
    CHECK_EQ(n1.data_edge_count, 1);
    CHECK_EQ(n1.source_index, 0);
    CHECK_EQ(n1.track_group_index, -1);
    CHECK_EQ(n1.speed, 1.0f);
    CHECK_EQ(n1.loop_count, 1);
    CHECK(n1.clamped_loop);
    CHECK(n1.has_continue_offset);
    CHECK(n1.continue_offset);
    const DagNode& n2 = g.nodes[2];
    CHECK(n2.type == DagNodeType::ScalarCallback);
    CHECK_EQ(n2.callback_hint, 7);
    CHECK(n2.has_callback_value);
    CHECK_EQ(n2.callback_value, 4.0f);
    CHECK_EQ(n2.edge_currents.size(), (size_t)2);
    CHECK_EQ(n2.edge_currents[1], 0.25f);
    CHECK(g.nodes[3].type == DagNodeType::Unknown);    // kept; the evaluator refuses it
    CHECK_EQ(g.nodes[3].type_name, std::string("TypeMarker_NodeDataSomethingNew"));
    CHECK(!g.nodes[4].has_node_data);
    CHECK(g.nodes[4].type == DagNodeType::Unknown);
    std::string d = dump(f);
    CHECK(d.find("dag root=0 nodes=5 sources=1000 track_masks=MASK_A\n") != std::string::npos);
    CHECK(d.find(" in Pose out:1:1:0:1\n") != std::string::npos);

    // refusals: the .gr2 reader's codes, MissingMember without DAGs; the output is cleared
    DagFile stale;
    stale.dags.resize(3);
    Bytes junk(256, 0x41);
    CHECK(!parse_dge(junk.data(), junk.size(), stale, &e));
    CHECK(e.code == Err::BadMagic);
    CHECK(stale.dags.empty());
    Bytes half(b.begin(), b.begin() + (long)(b.size() / 2));
    CHECK(!parse_dge(half.data(), half.size(), stale, &e));
    CHECK(e.code != Err::None);
    std::printf("  dge truncated -> %s %s\n", err_name(e.code), e.what.c_str());
    Bytes nodags = make_dge(false);
    stale.dags.resize(2);
    CHECK(!parse_dge(nodags.data(), nodags.size(), stale, &e));
    CHECK(e.code == Err::MissingMember);
    CHECK(stale.dags.empty());
    std::printf("  dge without DAGs -> %s %s\n", err_name(e.code), e.what.c_str());
}

}  // namespace

int main() {
    test_fsm();
    test_fts();
    test_lenience();
    test_dge();
    return t::finish();
}
