// bridge.cpp - the event bridge (epic 0374, issue 4404 part B; bridge.h; unit-controller.md 1, 6.5, 14).
#include "c5r/unitanim/bridge.h"

#include <cstdlib>

#include "c5r/units/placement.h"

namespace c5r { namespace unitanim {

namespace {

struct WordCode {
    int32_t code;
    const char* word;
};
// 6.5's word table [I by name; every code is accepted by the members' machines].
const WordCode kWords[] = {
    {1000, "Idle"},     {1100, "Attack"},     {1120, "RunCharge"},     {1130, "AttackSurfaceToAir"},
    {1140, "AttackCharge"}, {1160, "AttackCity"}, {1180, "Bombard"}, {1200, "Death"},
    {1280, "BombardDefend"}, {1400, "Run"},   {1450, "Walk"},          {1500, "Fortify"},
    {1600, "CombatReady"},
};

AnimStatus bad(const TimelineEvent& ev, const units::UnitInput& u, const std::string& why) {
    std::string what = eventKindName(ev.kind);
    if (ev.kind == EventKind::Action) what += std::string(" ") + actionKindName(ev.action);
    return AnimStatus::Fail(AnimCode::BadEvent, what + " on " + std::to_string(ev.player) + "/" +
                                                    std::to_string(ev.id) + " (" + u.unit_type + "): " + why);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

}  // namespace

int32_t workTool(const std::string& build) {
    // unit-controller.md 14.1 (0439): the tool the EXE asks for by the build's Type. Codes: shovel 1910, rake 1920,
    // axe 1930, pickaxe 1940, hammer 1950; 1960 (railroad) is never asked for.
    struct BuildTool {
        const char* build;
        int32_t code;
    };
    static const BuildTool kTools[] = {
        {"BUILD_FARM", 1920},          {"BUILD_TERRACE_FARM", 1920}, {"BUILD_POLDER", 1920},
        {"BUILD_MINE", 1940},          {"BUILD_QUARRY", 1940},       {"BUILD_ROAD", 1910},
        {"BUILD_RAILROAD", 1910},      {"BUILD_SCRUB_FALLOUT", 1910}, {"BUILD_REMOVE_MARSH", 1910},
        {"BUILD_REMOVE_ROUTE", 1910},  {"BUILD_REMOVE_FOREST", 1930}, {"BUILD_REMOVE_JUNGLE", 1930},
    };
    for (auto& t : kTools)
        if (build == t.build) return t.code;
    return 1950;   // every other build, and a build index with no row
}

const char* actionWord(int32_t code) {
    for (auto& w : kWords)
        if (w.code == code) return w.word;
    return nullptr;
}

AnimStatus loadBridgeTables(c5m::TableSource& db, BridgeTables& out) {
    out.words.clear();
    out.build_event.clear();
    std::vector<c5m::DbRow> rows;
    c5m::Error e;
    if (!db.rows("ArtDefine_UnitMemberCombats", {"UnitMemberType", "EnableActions"}, rows, &e))
        return AnimStatus::Fail(AnimCode::BadLogicFile, "ArtDefine_UnitMemberCombats: " + e.what);
    for (auto& r : rows) {
        if (r.size() != 2 || r[0].null) continue;
        std::set<std::string>& w = out.words[r[0].text];
        if (r[1].null) continue;
        const std::string& s = r[1].text;
        size_t at = 0;
        while (at < s.size()) {
            size_t sp = s.find(' ', at);
            std::string word = s.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
            if (!word.empty()) w.insert(word);
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
    }
    rows.clear();
    if (!db.rows("Builds", {"Type", "EntityEvent"}, rows, &e))
        return AnimStatus::Fail(AnimCode::BadLogicFile, "Builds: " + e.what);
    for (auto& r : rows)
        if (r.size() == 2 && !r[0].null) out.build_event[r[0].text] = r[1].null ? std::string() : r[1].text;
    return AnimStatus::Ok();
}

AnimStatus parseWorkTable(const std::string& text, std::map<std::string, int32_t>& out) {
    out.clear();
    size_t at = 0, line = 0;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        std::string l = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        ++line;
        at = nl == std::string::npos ? text.size() + 1 : nl + 1;
        l = trim(l);
        if (l.empty() || l[0] == '#') continue;
        const size_t tab = l.find('\t');
        auto refuse = [&](const std::string& why) {
            out.clear();
            return AnimStatus::Fail(AnimCode::BadLogicFile, "work table line " + std::to_string(line) + ": " + why);
        };
        if (tab == std::string::npos || l.find('\t', tab + 1) != std::string::npos)
            return refuse("want <EntityEvent><TAB><code>");
        const std::string ev = l.substr(0, tab), num = l.substr(tab + 1);
        if (ev.empty()) return refuse("empty entity event");
        char* end = nullptr;
        const long v = std::strtol(num.c_str(), &end, 10);
        if (num.empty() || !end || *end != 0 || v < 1900 || v > 1999) return refuse("code '" + num + "' (1900-1999)");
        if (out.count(ev)) return refuse("entity event " + ev + " twice");
        out[ev] = static_cast<int32_t>(v);
    }
    return AnimStatus::Ok();
}

AnimStatus ActionBridge::requests(const TimelineEvent& ev, const units::UnitInput& unit, std::vector<Request>& out) {
    out.clear();
    return bad(ev, unit, "the bridge needs the unit's art (call requestsFor)");
}

AnimStatus ActionBridge::requestsFor(const TimelineEvent& ev, const units::UnitInput& unit, const units::UnitArt& art,
                                     std::vector<Request>& out) {
    out.clear();
    Request r;
    switch (ev.kind) {
    case EventKind::Fortify: r.value = ev.a ? 1500 : 1000; break;
    case EventKind::Move: r.value = 1400; break;   // 1450 shuffle: the input does not say (open, 14)
    case EventKind::Arrive: r.value = 1000; break;
    case EventKind::Destroyed: r.value = 1200; r.dies = true; break;
    case EventKind::Kill: r.value = 1200; r.figures = ev.figures; r.dies = true; break;
    case EventKind::Damage: {
        // The 0x4B4B80 rule (unit-formations.md 7; units::livingFigures): living = n - round(n x pct / 100), at
        // most n - 1 dead below 100 %; deaths are taken from the last figure backwards.
        const uint32_t n = art.figures();
        if (n == 0) return bad(ev, unit, "the unit's art has no figures");
        uint32_t before = 0, after = 0;
        c5r::Status s = units::livingFigures(n, static_cast<int16_t>(ev.b), unit.max_hp, -1, before);
        if (s.ok()) s = units::livingFigures(n, static_cast<int16_t>(ev.a), unit.max_hp, -1, after);
        if (!s.ok()) return bad(ev, unit, "the figure rule refused: " + s.str());
        if (after >= before) return AnimStatus::Ok();   // no figure falls (a revival is not modelled: 14)
        r.value = 1200;
        r.dies = true;
        if (remove_fallen) {   // 4406 r3: removed at once, no death clip (premise change 5)
            r.value = 0;
            r.dies = false;
            r.removes = true;
        }
        r.figures.all = false;
        for (uint32_t f = after; f < before; ++f) r.figures.figures.push_back(static_cast<uint16_t>(f));
        break;
    }
    case EventKind::Action:
        r.figures = ev.figures;
        switch (ev.action) {
        case ActionKind::Attack: r.value = 1100; break;
        case ActionKind::AttackCharge: r.value = 1140; break;
        case ActionKind::Charge: r.value = 1120; break;
        case ActionKind::AttackCity: r.value = 1160; break;
        case ActionKind::Bombard: r.value = 1180; break;
        case ActionKind::AntiAir: r.value = 1130; break;
        case ActionKind::Run: r.value = 1400; break;           // 4775: the choreography's approach
        case ActionKind::CombatReady: r.value = 1600; break;   // 4775
        case ActionKind::Idle: r.value = 1000; break;          // 4775: the reface
        case ActionKind::Walk: r.value = 1450; break;          // 4750: the defender's phase-2 stance (word Walk)
        case ActionKind::BombardDefend: r.value = 1280; break;
        case ActionKind::Victory: r.value = 1800; break;
        case ActionKind::Captured: r.value = 1195; break;
        case ActionKind::Activate: r.value = 1990; break;
        case ActionKind::Work: {
            if (ev.argument.empty()) {   // 0439: the work stop (build -1): the EXE posts IDLE [C, 0x564960]
                r.value = 1000;
                break;
            }
            if (!t_.work_codes.empty()) {   // a caller's work table (4404; by Builds.EntityEvent) replaces the rule
                auto b = t_.build_event.find(ev.argument);
                if (b == t_.build_event.end()) return bad(ev, unit, "build " + ev.argument + " is not in Builds");
                if (b->second.empty()) return bad(ev, unit, "build " + ev.argument + " has no EntityEvent");
                auto c = t_.work_codes.find(b->second);
                if (c == t_.work_codes.end())
                    return bad(ev, unit, "no work code for " + b->second + " (build " + ev.argument +
                                             ") in the caller's work table");
                r.value = c->second;
                break;
            }
            r.value = workTool(ev.argument);   // 0439: the EXE's rule by the build's Type (unit-controller.md 14.1)
            break;
        }
        case ActionKind::Paradrop:
            return bad(ev, unit, "no request code in the files for a paradrop (unit-controller.md 6.5, 14)");
        default:
            return bad(ev, unit, "unknown action");
        }
        break;
    default:
        return AnimStatus::Ok();   // turn, embark, garrison, visibility: no request
    }
    // EnableActions per member reached by the request's figures. 0439: not for fortify (1500 / 1000) and work (its
    // tools and the stop's 1000): the EXE's handlers post those with no test of the unit [C, 0x619610, 0x564960], and a
    // civilian's sleep (1500) has no word in its row. A member with no Combats row is not checked either (VP's
    // COLONIST2-4: the EXE draws the unit; what its word mask holds without a row is not read [I]).
    const bool posted = ev.kind == EventKind::Fortify || (ev.kind == EventKind::Action && ev.action == ActionKind::Work);
    const char* word = posted ? nullptr : actionWord(r.value);
    if (word) {
        uint32_t index = 0;
        for (auto& m : art.members) {
            bool reached = false;
            for (int k = 0; k < m.count; ++k, ++index) {
                if (r.figures.all) reached = true;
                else
                    for (auto x : r.figures.figures)
                        if (x == index) reached = true;
            }
            if (!reached) continue;
            auto w = t_.words.find(m.member);
            if (w == t_.words.end()) continue;   // 0439: no Combats row: not checked (above)
            // 5071 (unit-combat-server.md 18.46): the EXE asks 1140 after a 1120 of a member that enables RunCharge
            // (0321 5.2 state 10 [C]), a rush rider without AttackCharge too (the knight's strike from its run)
            if (r.value == 1140 && w->second.count("RunCharge")) continue;
            if (!w->second.count(word))
                return bad(ev, unit, "member " + m.member + "'s EnableActions lack " + word + " (code " +
                                         std::to_string(r.value) + ")");
        }
    }
    out.push_back(r);
    return AnimStatus::Ok();
}

}}  // namespace c5r::unitanim
