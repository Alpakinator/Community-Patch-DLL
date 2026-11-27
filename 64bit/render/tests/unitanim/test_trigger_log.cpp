// render/tests/unitanim/test_trigger_log.cpp - ticket 4721: the offline trigger log (c5r/unitanim/trigger_log.h): the
// round trip of every record kind and of a whole log through the file, the contract's record checks with each failure,
// and the parser's refusals. The names are made up (no game name in git, D83).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "c5r/unitanim/trigger_log.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::unitanim;

namespace {

VPTrigRecord blank(uint8_t kind) {
    VPTrigRecord r;
    std::memset(&r, 0, sizeof(r));
    r.version = VPTRIG_VERSION;
    r.kind = kind;
    r.player = 1;
    r.unit = 8193;
    r.figure = 2;
    r.owner = 0x00ABCDEFu;
    r.fightSeq = 7;
    r.instance = 55;
    r.ec = 1100;
    r.trigger = 3;
    r.file = 3;
    return r;
}
VPTrigRecord soundStart() {
    VPTrigRecord r = blank(VPTRIG_SOUND_START);
    r.seq = 1;
    r.frameId = 345;
    r.handle = 17;
    r.name = 1;
    r.flags = VPTRIG_FLAG_LOOP;
    r.pos[0] = 1.5f; r.pos[1] = -2.25f; r.pos[2] = 0.1f;
    r.clipTime = 0.4f;
    r.late = 1.0f / 60.0f;
    return r;
}
VPTrigRecord effectStart() {
    VPTrigRecord r = blank(VPTRIG_EFFECT_START);
    r.seq = 2;
    r.frameId = 345;
    r.handle = 18;
    r.name = 2;
    r.flags = VPTRIG_FLAG_FOLLOWS_OWNER;
    r.pos[0] = 1e-30f; r.pos[1] = 3.4e38f; r.pos[2] = -0.0f;
    r.scale = 1.25f;
    r.orient[0] = 0.f; r.orient[1] = 0.70710678f; r.orient[2] = 0.f; r.orient[3] = 0.70710678f;
    r.vel[0] = 12.5f; r.vel[1] = 0.f; r.vel[2] = -9.81f;
    r.clipTime = 0.123456789f;
    r.late = 0.f;
    return r;
}
VPTrigRecord fightEnd(uint8_t reason) {
    VPTrigRecord r = blank(VPTRIG_OWNER_END);
    r.seq = 9;
    r.frameId = 400;
    r.reason = reason;
    r.figure = -1;
    r.instance = 0;
    r.ec = 0;
    r.trigger = -1;
    r.file = 0;
    if (VPTrigPlayedFor(reason)) r.flags = VPTRIG_FLAG_PLAYED;
    return r;
}

bool same(const VPTrigRecord& a, const VPTrigRecord& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

void roundTrip(const VPTrigRecord& r) {
    CHECK_OK(checkTriggerRecord(r));
    std::string line = formatTriggerRecord(r);
    VPTrigRecord back;
    std::memset(&back, 0xCC, sizeof(back));
    Status st = parseTriggerRecord(line, back);
    if (!st) std::printf("  line: %s\n  %s\n", line.c_str(), st.str().c_str());
    CHECK_OK(st);
    CHECK(same(r, back));
    CHECK_EQ(formatTriggerRecord(back), line);
}

// one change to a valid record, and the check must refuse it naming `want`
template <class F>
void refused(VPTrigRecord r, F change, const char* want) {
    change(r);
    Status st = checkTriggerRecord(r);
    CHECK(!st.ok());
    if (st.msg.find(want) == std::string::npos) {
        std::printf("  expected '%s' in '%s'\n", want, st.msg.c_str());
        CHECK(false);
    }
}

}  // namespace

C5R_TEST(every_kind_round_trips_exactly) {
    roundTrip(soundStart());
    roundTrip(effectStart());
    VPTrigRecord stop = blank(VPTRIG_SOUND_STOP);
    stop.seq = 3; stop.frameId = 350; stop.handle = 17; stop.pos[0] = 4.f;
    roundTrip(stop);
    VPTrigRecord move = effectStart();
    move.kind = VPTRIG_EFFECT_MOVE; move.seq = 4; move.name = 0; move.flags = 0; move.scale = 0.5f;
    roundTrip(move);
    VPTrigRecord estop = blank(VPTRIG_EFFECT_STOP);
    estop.seq = 5; estop.handle = 18;
    roundTrip(estop);
    for (uint8_t reason = 0; reason < VPTRIG_END_COUNT; ++reason) {
        VPTrigRecord e = fightEnd(reason);
        if (reason == VPTRIG_END_CLIP_ENDED) { e.figure = 4; e.instance = 77; }
        roundTrip(e);
    }
    VPTrigRecord unknownOwner = fightEnd(VPTRIG_END_GONE);
    unknownOwner.owner = 0;   // owner not known: the fight's seq names it
    roundTrip(unknownOwner);
    // extremes of every integer field
    VPTrigRecord x = soundStart();
    x.seq = std::numeric_limits<uint64_t>::max(); x.frameId = std::numeric_limits<int64_t>::max();
    x.owner = 0xFFFFFFFFu; x.fightSeq = 0xFFFFFFFFu; x.player = std::numeric_limits<int32_t>::max();
    x.unit = std::numeric_limits<int32_t>::min(); x.handle = 0xFFFFFFFFu; x.instance = ~0ull;
    x.name = 0xFFFFFFFFu; x.file = 0xFFFFFFFFu; x.ec = std::numeric_limits<int32_t>::min(); x.trigger = -1;
    std::string line = formatTriggerRecord(x);
    VPTrigRecord back;
    CHECK_OK(parseTriggerRecord(line, back));
    CHECK(same(x, back));
}

C5R_TEST(floats_round_trip_bit_for_bit) {
    // 20,000 floats spread over the exponent range, through 9 significant digits and back
    uint32_t bits = 0x00000001u;
    for (int i = 0; i < 20000; ++i) {
        bits = bits * 1664525u + 1013904223u;
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        if (!std::isfinite(f)) continue;
        VPTrigRecord r = soundStart();
        r.pos[0] = f; r.pos[1] = -f; r.clipTime = f;
        VPTrigRecord back;
        CHECK_OK(parseTriggerRecord(formatTriggerRecord(r), back));
        CHECK(same(r, back));
    }
}

C5R_TEST(the_line_format_is_as_documented) {
    VPTrigRecord e = fightEnd(VPTRIG_END_LIMIT);
    CHECK_EQ(formatTriggerRecord(e),
             std::string("R v=1 seq=9 F=400 kind=OwnerEnd reason=Limit flags=played owner=11259375 fseq=7 player=1 "
                         "unit=8193 figure=-1 handle=0 inst=0 name=0 file=0 pos=0,0,0 scale=0 orient=0,0,0,0 "
                         "vel=0,0,0 ec=0 trig=-1 t=0 late=0"));
    TriggerName n;
    n.index = 4; n.kind = VPTRIG_NAME_SOUND3D; n.text = "TEST_SCRIPT WITH SPACE";
    CHECK_EQ(formatTriggerName(n), std::string("N 4 sound3d TEST_SCRIPT WITH SPACE"));
    TriggerName back;
    CHECK_OK(parseTriggerName(formatTriggerName(n), back));
    CHECK_EQ(back.index, n.index);
    CHECK_EQ(back.kind, n.kind);
    CHECK_EQ(back.text, n.text);
    // keys in any order are read
    VPTrigRecord r;
    CHECK_OK(parseTriggerRecord("R late=0 t=0 trig=-1 ec=0 vel=0,0,0 orient=0,0,0,0 scale=0 pos=0,0,0 file=0 name=0 "
                                "inst=0 handle=0 figure=-1 unit=8193 player=1 fseq=7 owner=11259375 flags=played "
                                "reason=Limit kind=OwnerEnd F=400 seq=9 v=1", r));
    CHECK(same(r, e));
}

C5R_TEST(the_contract_checks_refuse_each_fault) {
    const VPTrigRecord s = soundStart(), fx = effectStart(), end = fightEnd(VPTRIG_END_NATURAL);
    refused(s, [](VPTrigRecord& r) { r.version = 2; }, "version 2");
    refused(s, [](VPTrigRecord& r) { r.kind = 0; }, "kind 0");
    refused(s, [](VPTrigRecord& r) { r.kind = 7; }, "kind 7");
    refused(s, [](VPTrigRecord& r) { r.frameId = -1; }, "below 0");
    refused(s, [](VPTrigRecord& r) { r.flags |= 0x10u; }, "unknown flag bits 0x10");
    refused(s, [](VPTrigRecord& r) { r.reserved0 = 1; }, "reserved0");
    refused(s, [](VPTrigRecord& r) { r.figure = -2; }, "figure -2");
    refused(s, [](VPTrigRecord& r) { r.pos[1] = std::numeric_limits<float>::infinity(); }, "not finite");
    refused(s, [](VPTrigRecord& r) { r.late = std::nanf(""); }, "not finite");
    refused(s, [](VPTrigRecord& r) { r.late = -0.5f; }, "late below 0");
    refused(s, [](VPTrigRecord& r) { r.handle = 0; }, "without a handle");
    refused(s, [](VPTrigRecord& r) { r.name = 0; }, "without a name");
    refused(s, [](VPTrigRecord& r) { r.owner = 0; r.fightSeq = 0; r.unit = -1; }, "without an owner");
    refused(s, [](VPTrigRecord& r) { r.reason = 1; }, "reason set");
    refused(s, [](VPTrigRecord& r) { r.flags |= VPTRIG_FLAG_PLAYED; }, "played on a record");
    refused(s, [](VPTrigRecord& r) { r.scale = 1; }, "effects only");
    refused(s, [](VPTrigRecord& r) { r.orient[3] = 1; }, "effects only");
    refused(fx, [](VPTrigRecord& r) { r.scale = 0; }, "EffectStart with scale");
    refused(fx, [](VPTrigRecord& r) { r.kind = VPTRIG_EFFECT_STOP; }, "loop / follows");
    refused(fx, [](VPTrigRecord& r) { r.kind = VPTRIG_EFFECT_MOVE; r.flags = 0; }, "with a name");
    refused(end, [](VPTrigRecord& r) { r.flags = 0; }, "played must be set");
    refused(end, [](VPTrigRecord& r) { r.reason = VPTRIG_END_REFUSED; }, "played must be clear");
    refused(end, [](VPTrigRecord& r) { r.reason = 6; }, "reason 6");
    refused(end, [](VPTrigRecord& r) { r.handle = 1; }, "handle or a name");
    refused(end, [](VPTrigRecord& r) { r.figure = 0; }, "with a figure or an instance");
    refused(end, [](VPTrigRecord& r) { r.owner = 0; r.fightSeq = 0; }, "naming no fight");
    refused(end, [](VPTrigRecord& r) { r.reason = VPTRIG_END_CLIP_ENDED; }, "ClipEnded without");
    refused(end, [](VPTrigRecord& r) { r.scale = 2; }, "effects only");
    // R8: played exactly for Natural, Limit (and ClipEnded, which never reaches the hold)
    CHECK(VPTrigPlayedFor(VPTRIG_END_NATURAL));
    CHECK(VPTrigPlayedFor(VPTRIG_END_LIMIT));
    CHECK(!VPTrigPlayedFor(VPTRIG_END_REFUSED));
    CHECK(!VPTrigPlayedFor(VPTRIG_END_GONE));
    CHECK(!VPTrigPlayedFor(VPTRIG_END_RESET));
    CHECK(VPTrigIsFightEnd(end));
    VPTrigRecord clip = fightEnd(VPTRIG_END_CLIP_ENDED);
    clip.figure = 1; clip.instance = 9;
    CHECK(!VPTrigIsFightEnd(clip));
    CHECK(!VPTrigIsFightEnd(s));
}

C5R_TEST(the_parser_refuses_bad_lines) {
    VPTrigRecord r;
    const std::string good = formatTriggerRecord(soundStart());
    CHECK_OK(parseTriggerRecord(good, r));
    auto replaced = [&](const char* from, const char* to) {
        std::string l = good;
        size_t at = l.find(from);
        CHECK(at != std::string::npos);
        return l.replace(at, std::strlen(from), to);
    };
    CHECK_ERR(parseTriggerRecord("X v=1", r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(good + " extra=1", r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(good + " v=1", r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" late=", " xlate="), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" seq=1 ", " "), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" seq=1 ", " seq=-1 "), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" seq=1 ", " seq=1x "), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced("kind=SoundStart", "kind=SoundBegin"), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced("flags=loop", "flags=loop|bogus"), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" pos=", " pos=1,2 x="), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced("player=1", "player=4294967296"), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced("player=1", "player=1.5"), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" t=", " t=1e999 x="), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced(" t=", "  t="), r), Err::BadLayout);
    CHECK_ERR(parseTriggerRecord(replaced("v=1", "v=70000"), r), Err::BadLayout);
    // an unknown flag bit and an unknown kind number are read (the check refuses them, not the parser)
    CHECK_OK(parseTriggerRecord(replaced("flags=loop", "flags=loop|0x40"), r));
    CHECK_EQ(r.flags, VPTRIG_FLAG_LOOP | 0x40u);
    CHECK_OK(parseTriggerRecord(replaced("kind=SoundStart", "kind=9"), r));
    CHECK_EQ(r.kind, static_cast<uint8_t>(9));
    TriggerName n;
    CHECK_ERR(parseTriggerName("N 0 sound3d A", n), Err::BadLayout);
    CHECK_ERR(parseTriggerName("N 1 music A", n), Err::BadLayout);
    CHECK_ERR(parseTriggerName("N 1 sound3d", n), Err::BadLayout);
    CHECK_ERR(parseTriggerName("N 1 sound3d " + std::string(120, 'A'), n), Err::BadLayout);
    CHECK_OK(parseTriggerName("N 1 sound3d " + std::string(119, 'A'), n));
    CHECK_ERR(parseTriggerName("N 1 effect A\tB", n), Err::BadLayout);
}

C5R_TEST(a_whole_log_round_trips_through_the_file) {
    TriggerLog log;
    const char* texts[] = {"TEST_SOUND_SWING", "TEST_EFFECT_SPARKS", "test_unit.ftsxml"};
    const uint32_t kinds[] = {VPTRIG_NAME_SOUND3D, VPTRIG_NAME_EFFECT, VPTRIG_NAME_TRIGGERFILE};
    for (uint32_t i = 0; i < 3; ++i) {
        TriggerName n;
        n.index = i + 1; n.kind = kinds[i]; n.text = texts[i];
        log.names.push_back(n);
    }
    log.records.push_back(soundStart());
    log.records.push_back(effectStart());
    VPTrigRecord clip = fightEnd(VPTRIG_END_CLIP_ENDED);
    clip.seq = 5; clip.frameId = 380; clip.figure = 2; clip.instance = 55;
    log.records.push_back(clip);
    log.records.push_back(fightEnd(VPTRIG_END_NATURAL));

    const std::string path = c5r::test::workDir() + "/test_trigger_log.txt";
    {
        TriggerLogWriter w;
        CHECK_OK(w.open(path));
        // R5: a record before its name is refused, nothing written
        CHECK_ERR(w.record(log.records[0]), Err::BadLayout);
        TriggerName skip = log.names[1];
        CHECK_ERR(w.name(skip), Err::BadLayout);   // index 2 before 1
        for (const TriggerName& n : log.names) CHECK_OK(w.name(n));
        VPTrigRecord badRec = log.records[0];
        badRec.handle = 0;
        CHECK_ERR(w.record(badRec), Err::BadLayout);
        for (const VPTrigRecord& r : log.records) CHECK_OK(w.record(r));
        CHECK_EQ(w.records(), static_cast<uint64_t>(4));
        CHECK_OK(w.close());
    }
    TriggerLog back;
    CHECK_OK(readTriggerLog(path, back));
    CHECK_EQ(back.names.size(), log.names.size());
    for (size_t i = 0; i < log.names.size(); ++i) {
        CHECK_EQ(back.names[i].index, log.names[i].index);
        CHECK_EQ(back.names[i].kind, log.names[i].kind);
        CHECK_EQ(back.names[i].text, log.names[i].text);
    }
    CHECK_EQ(back.records.size(), log.records.size());
    for (size_t i = 0; i < log.records.size(); ++i) CHECK(same(back.records[i], log.records[i]));
    // the writer's file equals formatTriggerLog's text
    std::string text;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        CHECK(f != nullptr);
        char buf[4096];
        size_t n = 0;
        while (f && (n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        if (f) std::fclose(f);
    }
    CHECK_EQ(text, formatTriggerLog(log));
    std::remove(path.c_str());

    // comments, blank lines and CRLF are read
    std::string crlf;
    for (char c : "# c5r trigger log 1\n# a comment\n\n" + formatTriggerName(log.names[0]) + "\n" +
                      formatTriggerName(log.names[1]) + "\n" + formatTriggerName(log.names[2]) + "\n" +
                      formatTriggerRecord(log.records[0]) + "\n") {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    TriggerLog fromCrlf;
    CHECK_OK(parseTriggerLog(crlf, fromCrlf));
    CHECK_EQ(fromCrlf.records.size(), static_cast<size_t>(1));
}

C5R_TEST(the_log_parser_refuses_bad_logs) {
    TriggerLog out;
    TriggerName n1;
    n1.index = 1; n1.kind = VPTRIG_NAME_SOUND3D; n1.text = "TEST_SOUND";
    TriggerName n2 = n1;
    n2.index = 2; n2.kind = VPTRIG_NAME_EFFECT; n2.text = "TEST_EFFECT";
    TriggerName n3 = n1;
    n3.index = 3; n3.kind = VPTRIG_NAME_TRIGGERFILE; n3.text = "test.ftsxml";
    const std::string head = "# c5r trigger log 1\n";
    const std::string names = formatTriggerName(n1) + "\n" + formatTriggerName(n2) + "\n" + formatTriggerName(n3) + "\n";
    const std::string rec = formatTriggerRecord(soundStart()) + "\n";
    CHECK_OK(parseTriggerLog(head + names + rec, out));
    CHECK_ERR(parseTriggerLog("", out), Err::Truncated);
    CHECK_ERR(parseTriggerLog("R v=1\n", out), Err::BadMagic);
    CHECK_ERR(parseTriggerLog("# c5r trigger log 2\n", out), Err::BadVersion);
    CHECK_ERR(parseTriggerLog(head + rec, out), Err::BadLayout);                       // names not declared (R5)
    CHECK_ERR(parseTriggerLog(head + formatTriggerName(n2) + "\n", out), Err::BadLayout);   // name 2 first
    CHECK_ERR(parseTriggerLog(head + names + "Z what\n", out), Err::BadLayout);
    VPTrigRecord earlier = soundStart();
    earlier.seq = 2; earlier.frameId = 344;
    CHECK_ERR(parseTriggerLog(head + names + rec + formatTriggerRecord(earlier) + "\n", out), Err::BadLayout);   // R1
    VPTrigRecord sameSeq = soundStart();
    CHECK_ERR(parseTriggerLog(head + names + rec + formatTriggerRecord(sameSeq) + "\n", out), Err::BadLayout);
    VPTrigRecord badVersion = soundStart();
    badVersion.version = 3;
    CHECK_ERR(parseTriggerLog(head + names + formatTriggerRecord(badVersion) + "\n", out), Err::BadVersion);
    CHECK_ERR(readTriggerLog(c5r::test::workDir() + "/no_such_trigger_log.txt", out), Err::Io);
}

C5R_TEST_MAIN
