// timeline_file.cpp - the unit timeline file (.c5anim, format 1): reader and writer (epic 0374, issue 4404 part A).
// Format: c5r/unitanim/timeline.h and research/05-graphics-composition/unit-controller.md 2. The player and unit
// fields are those of .c5units and are checked by its reader (units/scene_file.h), so the two formats cannot drift.
#include "c5r/unitanim/timeline.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>

#include "c5r/units/scene_file.h"

namespace c5r { namespace unitanim {

namespace {

using units::PlayerInput;
using units::UnitInput;
using units::UnitScene;

TimelineStatus refuse(TimelineErr c, size_t line, std::string msg) {
    TimelineStatus s;
    s.code = c;
    s.line = line;
    s.msg = std::move(msg);
    return s;
}

std::vector<std::string> splitTabs(const std::string& s) {
    std::vector<std::string> f;
    size_t start = 0;
    for (;;) {
        size_t p = s.find('\t', start);
        if (p == std::string::npos) {
            f.push_back(s.substr(start));
            return f;
        }
        f.push_back(s.substr(start, p - start));
        start = p + 1;
    }
}

bool parseI64(const std::string& s, long long lo, long long hi, long long& out) {
    if (s.empty() || s.size() > 24) return false;
    for (char ch : s)
        if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == '+')) return false;
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size() || v < lo || v > hi) return false;
    out = v;
    return true;
}

bool parseU64(const std::string& s, unsigned long long& out) {
    if (s.empty() || s.size() > 20) return false;
    for (char ch : s)
        if (ch < '0' || ch > '9') return false;
    errno = 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

bool parseF64(const std::string& s, double lo, double hi, double& out) {
    if (s.empty() || s.size() > 64) return false;
    for (char ch : s)
        if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E'))
            return false;
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end != s.c_str() + s.size() || !std::isfinite(v) || v < lo || v > hi) return false;
    out = v;
    return true;
}

bool goodText(const std::string& s) {
    if (s.empty() || s.size() > 256) return false;
    for (unsigned char ch : s)
        if (ch < 0x21 || ch == 0x7F) return false;
    return true;
}

std::string fmtF(double v, bool dbl) {
    char b[64];
    std::snprintf(b, sizeof b, dbl ? "%.17g" : "%.9g", v);
    return b;
}

const char* const kKindNames[] = {"create", "remove", "fortify", "move", "turn", "arrive", "place", "action",
                                  "damage", "kill", "destroyed", "embark", "garrison", "visibility", "reset",
                                  "choice", "steer"};
// Field count after "ev <player> <id> <kind>".
const int kKindArgs[] = {16, 0, 1, 2, 2, 1, 3, 3, 2, 1, 1, 1, 1, 2, 0, 3, 3};
const char* const kActionNames[] = {"-", "attack", "attack_city", "bombard", "charge", "attack_charge", "victory",
                                    "bombard_defend", "work", "captured", "activate", "paradrop",
                                    "anti_air",   // anti_air: 4404 (additive)
                                    "run", "combat_ready", "idle",   // 4775 (additive): the choreography's requests
                                    "walk"};   // 4750 (additive, appended): 1450, the defender's phase-2 stance
constexpr int kKindCount = 17;
constexpr int kActionCount = 17;
static_assert(sizeof(kActionNames) / sizeof(kActionNames[0]) == kActionCount, "one name per ActionKind");
static_assert(static_cast<int>(ActionKind::Walk) == kActionCount - 1, "ActionKind::Walk is the last kind");

bool parseFigures(const std::string& s, FigureSet& out, std::string& why) {
    out = FigureSet();
    if (s == "*") return true;
    out.all = false;
    std::set<long long> seen;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(',', start);
        std::string tok = s.substr(start, p == std::string::npos ? std::string::npos : p - start);
        long long v = 0;
        if (!parseI64(tok, 0, 63, v)) {
            why = "figure list '" + s + "': bad index '" + tok + "'";
            return false;
        }
        if (!seen.insert(v).second) {
            why = "figure list '" + s + "': index " + tok + " twice";
            return false;
        }
        out.figures.push_back(static_cast<uint16_t>(v));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return true;
}

std::string fmtFigures(const FigureSet& f) {
    if (f.all) return "*";
    std::string s;
    for (size_t i = 0; i < f.figures.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(f.figures[i]);
    }
    return s;
}

bool goodPoint(const std::string& p) {
    std::string rest;
    if (p.compare(0, 5, "roll:") == 0)
        rest = p.substr(5);
    else if (p.compare(0, 7, "option:") == 0)
        rest = p.substr(7);
    else
        return false;
    size_t c = rest.rfind(':');
    if (c == std::string::npos || c == 0) return false;
    long long v = 0;
    return goodText(rest.substr(0, c)) && parseI64(rest.substr(c + 1), 0, 1 << 20, v);
}

// The .c5units text of the given players and (optionally) one unit: used to check and to format player and unit
// fields with the .c5units reader and writer themselves.
std::string miniScene(const std::vector<PlayerInput>& players, const std::string& unitLine) {
    std::string t = "c5units\t1\nscene\town\t0\n";
    for (const PlayerInput& p : players)
        t += "player\t" + std::to_string(p.player) + "\t" + p.colour + "\t" + p.art_style + "\t" + p.era + "\n";
    t += unitLine;
    return t;
}

class Parser {
public:
    Parser(const UnitScene* start, Timeline& out) : out_(out) {
        if (start) {
            for (const PlayerInput& p : start->players) players_[p.player] = p;
            for (const UnitInput& u : start->units) live_.insert({u.player, u.id});
        }
    }

    TimelineStatus run(const std::string& text) {
        if (text.size() > kTimelineMaxFile)
            return refuse(TimelineErr::TooLarge, 0, "file over " + std::to_string(kTimelineMaxFile) + " bytes");
        size_t pos = 0, lineNo = 0;
        bool header = false, scene = false;
        TimelineFrame pending;
        bool havePending = false;
        bool haveFrame = false;
        uint64_t lastSeq = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > kTimelineMaxLine)
                return refuse(TimelineErr::TooLarge, lineNo, "line over " + std::to_string(kTimelineMaxLine) + " bytes");
            if (lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            if (line.find_first_not_of(" \t") == std::string::npos || line[0] == '#') continue;
            std::vector<std::string> f = splitTabs(line);
            const std::string& kind = f[0];
            if (!header) {
                if (kind != "c5anim" || f.size() != 2 || f[1] != "1")
                    return refuse(TimelineErr::BadHeader, lineNo, "first record must be c5anim<TAB>1");
                header = true;
                continue;
            }
            if (!scene) {
                if (kind != "scene") return refuse(TimelineErr::BadRecord, lineNo, "the scene record must follow the header");
                if (f.size() != 4) return refuse(TimelineErr::BadRecord, lineNo, "scene: 3 fields expected");
                unsigned long long seed = 0;
                if (f[1] != "-" && !goodText(f[1])) return refuse(TimelineErr::BadValue, lineNo, "scene: bad path");
                if (!parseU64(f[2], seed)) return refuse(TimelineErr::BadValue, lineNo, "scene: bad seed '" + f[2] + "'");
                if (f[3] != "free" && f[3] != "forced")
                    return refuse(TimelineErr::BadValue, lineNo, "scene: mode must be free or forced");
                out_.scene_path = f[1] == "-" ? "" : f[1];
                out_.seed = seed;
                out_.forced = f[3] == "forced";
                scene = true;
                continue;
            }
            if (kind == "scene") return refuse(TimelineErr::Duplicate, lineNo, "a second scene record");
            if (kind == "player") {
                TimelineStatus s = player(f, lineNo);
                if (!s.ok()) return s;
                continue;
            }
            if (kind == "ev") {
                if (pending.events.size() >= kTimelineMaxEventsPerFrame)
                    return refuse(TimelineErr::TooLarge, lineNo, "more than " +
                                  std::to_string(kTimelineMaxEventsPerFrame) + " events in one frame");
                TimelineEvent e;
                TimelineStatus s = event(f, lineNo, e);
                if (!s.ok()) return s;
                pending.events.push_back(std::move(e));
                havePending = true;
                continue;
            }
            if (kind == "frame") {
                if (f.size() != 5) return refuse(TimelineErr::BadRecord, lineNo, "frame: 4 fields expected");
                unsigned long long seq = 0;
                if (!parseU64(f[1], seq)) return refuse(TimelineErr::BadValue, lineNo, "frame: bad seq '" + f[1] + "'");
                if (haveFrame && seq <= lastSeq)
                    return refuse(TimelineErr::OutOfOrder, lineNo, "frame seq " + f[1] + " after " +
                                  std::to_string(lastSeq));
                double dt = 0;
                if (!parseF64(f[2], 0, kTimelineMaxDt, dt))
                    return refuse(TimelineErr::BadValue, lineNo, "frame: bad dt '" + f[2] + "'");
                pending.seq = seq;
                pending.dt = dt;
                pending.has_present = f[3] != "-";
                if (pending.has_present && !parseF64(f[3], -1e12, 1e12, pending.present_time))
                    return refuse(TimelineErr::BadValue, lineNo, "frame: bad present_time '" + f[3] + "'");
                pending.has_exe_frame = f[4] != "-";
                unsigned long long ef = 0;
                if (pending.has_exe_frame && !parseU64(f[4], ef))
                    return refuse(TimelineErr::BadValue, lineNo, "frame: bad exe_frame '" + f[4] + "'");
                pending.exe_frame = ef;
                if (out_.frames.size() >= kTimelineMaxFrames)
                    return refuse(TimelineErr::TooLarge, lineNo, "more than " + std::to_string(kTimelineMaxFrames) +
                                  " frames");
                out_.frames.push_back(std::move(pending));
                pending = TimelineFrame();
                havePending = false;
                haveFrame = true;
                lastSeq = seq;
                continue;
            }
            return refuse(TimelineErr::BadRecord, lineNo, "unknown record '" + kind + "'");
        }
        if (!header) return refuse(TimelineErr::BadHeader, 0, "no c5anim header");
        if (!scene) return refuse(TimelineErr::BadRecord, 0, "no scene record");
        if (havePending)
            return refuse(TimelineErr::BadRecord, 0, std::to_string(pending.events.size()) +
                          " event(s) after the last frame");
        return TimelineStatus();
    }

private:
    TimelineStatus player(const std::vector<std::string>& f, size_t lineNo) {
        if (f.size() != 5) return refuse(TimelineErr::BadRecord, lineNo, "player: 4 fields expected");
        std::string line;
        for (size_t i = 0; i < f.size(); ++i) line += (i ? "\t" : "") + f[i];
        UnitScene sc;
        units::SceneFileStatus st = units::parseScene("c5units\t1\nscene\town\t0\n" + line + "\n", sc);
        if (!st.ok() || sc.players.size() != 1)
            return refuse(TimelineErr::BadValue, lineNo, "player: " + st.msg);
        const PlayerInput& p = sc.players[0];
        if (players_.count(p.player)) return refuse(TimelineErr::Duplicate, lineNo, "player " + f[1] + " twice");
        if (players_.size() >= kTimelineMaxPlayers)
            return refuse(TimelineErr::TooLarge, lineNo, "more than " + std::to_string(kTimelineMaxPlayers) + " players");
        players_[p.player] = p;
        out_.players.push_back(p);
        return TimelineStatus();
    }

    TimelineStatus event(const std::vector<std::string>& f, size_t lineNo, TimelineEvent& e) {
        if (f.size() < 4) return refuse(TimelineErr::BadRecord, lineNo, "ev: at least 3 fields expected");
        long long pl = 0, id = 0;
        if (!parseI64(f[1], 0, 63, pl)) return refuse(TimelineErr::BadValue, lineNo, "ev: bad player '" + f[1] + "'");
        if (!parseI64(f[2], -2147483647LL - 1, 2147483647LL, id))
            return refuse(TimelineErr::BadValue, lineNo, "ev: bad id '" + f[2] + "'");
        int k = -1;
        for (int i = 0; i < kKindCount; ++i)
            if (f[3] == kKindNames[i]) k = i;
        if (k < 0) return refuse(TimelineErr::BadRecord, lineNo, "unknown event kind '" + f[3] + "'");
        if (static_cast<int>(f.size()) - 4 != kKindArgs[k])
            return refuse(TimelineErr::BadRecord, lineNo, std::string(kKindNames[k]) + ": " +
                          std::to_string(kKindArgs[k]) + " argument(s) expected");
        e.kind = static_cast<EventKind>(k);
        e.player = static_cast<int32_t>(pl);
        e.id = static_cast<int32_t>(id);
        const std::pair<int32_t, int32_t> key(e.player, e.id);
        auto bad = [&](const std::string& what) { return refuse(TimelineErr::BadValue, lineNo, std::string(kKindNames[k]) + ": " + what); };
        long long v = 0;
        switch (e.kind) {
        case EventKind::Create: {
            if (live_.count(key)) return refuse(TimelineErr::Duplicate, lineNo, "create: unit " + f[1] + " " + f[2] + " exists");
            auto it = players_.find(e.player);
            if (it == players_.end()) return refuse(TimelineErr::Orphan, lineNo, "create: player " + f[1] + " has no player record");
            std::string line = "unit";
            for (size_t i = 1; i < f.size(); ++i)
                if (i != 3) line += "\t" + f[i];
            UnitScene sc;
            units::SceneFileStatus st = units::parseScene(miniScene({it->second}, line + "\n"), sc);
            if (!st.ok() || sc.units.size() != 1) return bad(st.msg);
            if (live_.size() >= kTimelineMaxUnits)
                return refuse(TimelineErr::TooLarge, lineNo, "more than " + std::to_string(kTimelineMaxUnits) + " units");
            e.unit = sc.units[0];
            live_.insert(key);
            return TimelineStatus();
        }
        default:
            break;
        }
        if (!live_.count(key))
            return refuse(TimelineErr::UnknownUnit, lineNo, std::string(kKindNames[k]) + ": unit " + f[1] + " " + f[2] +
                          " is not in the scene");
        std::string why;
        switch (e.kind) {
        case EventKind::Remove:
            live_.erase(key);
            break;
        case EventKind::Reset:
            break;
        case EventKind::Fortify: case EventKind::Embark: case EventKind::Garrison: case EventKind::Destroyed:
            if (!parseI64(f[4], 0, 1, v)) return bad("expected 0 or 1, got '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            break;
        case EventKind::Move:
            if (!parseI64(f[4], 0, 5, v)) return bad("bad facing '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            if (!parseI64(f[5], 0, 1 << 20, v)) return bad("bad path length '" + f[5] + "'");
            e.b = static_cast<int32_t>(v);
            break;
        case EventKind::Turn:
            if (!parseI64(f[4], 0, 5, v)) return bad("bad facing '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            if (f[5] != "left" && f[5] != "right") return bad("direction must be left or right");
            e.b = f[5] == "right" ? 1 : 0;
            break;
        case EventKind::Arrive:
            if (!parseI64(f[4], 0, 5, v)) return bad("bad facing '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            break;
        case EventKind::Place:
            if (!parseI64(f[4], 0, 65535, v)) return bad("bad plot_x '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            if (!parseI64(f[5], 0, 65535, v)) return bad("bad plot_y '" + f[5] + "'");
            e.b = static_cast<int32_t>(v);
            if (!parseI64(f[6], 0, 5, v)) return bad("bad facing '" + f[6] + "'");
            e.c = static_cast<int32_t>(v);
            break;
        case EventKind::Action: {
            int a = -1;
            for (int i = 1; i < kActionCount; ++i)
                if (f[4] == kActionNames[i]) a = i;
            if (a < 0) return bad("unknown action '" + f[4] + "'");
            e.action = static_cast<ActionKind>(a);
            if (e.action == ActionKind::Work) {
                // 0439: "-" is the work stop (the DLL's build -1), kept as an empty argument
                if (f[5] != "-" && !goodText(f[5])) return bad("work needs a build Type or - (the stop)");
                e.argument = f[5] == "-" ? std::string() : f[5];
            } else if (f[5] != "-") {
                return bad("argument must be - for " + f[4]);
            }
            if (!parseFigures(f[6], e.figures, why)) return bad(why);
            break;
        }
        case EventKind::Damage:
            if (!parseI64(f[4], 0, 10000, v)) return bad("bad damage '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            if (!parseI64(f[5], 0, 10000, v)) return bad("bad previous damage '" + f[5] + "'");
            e.b = static_cast<int32_t>(v);
            break;
        case EventKind::Kill:
            if (!parseFigures(f[4], e.figures, why)) return bad(why);
            break;
        case EventKind::Visibility: {
            if (!parseI64(f[4], 0, 1, v)) return bad("expected 0 or 1, got '" + f[4] + "'");
            e.a = static_cast<int32_t>(v);
            double bt = 0;
            if (!parseF64(f[5], 0, 3600, bt)) return bad("bad blend time '" + f[5] + "'");
            e.blend_time = static_cast<float>(bt);
            break;
        }
        case EventKind::Choice:
            if (!parseFigures(f[4], e.figures, why)) return bad(why);
            if (!goodPoint(f[5])) return bad("bad decision point '" + f[5] + "'");
            e.point = f[5];
            if (!parseI64(f[6], -2147483647LL - 1, 2147483647LL, v)) return bad("bad value '" + f[6] + "'");
            e.a = static_cast<int32_t>(v);
            break;
        case EventKind::Steer: {
            if (!parseFigures(f[4], e.figures, why)) return bad(why);
            double w = 0;
            if (!parseF64(f[5], 0, 1, w)) return bad("bad path 7 weight '" + f[5] + "'");
            e.weight = static_cast<float>(w);
            if (!parseI64(f[6], 0, 15, v)) return bad("bad path 8 selector '" + f[6] + "'");
            e.a = static_cast<int32_t>(v);
            break;
        }
        case EventKind::Create:
            break;
        }
        return TimelineStatus();
    }

    Timeline& out_;
    std::map<int32_t, PlayerInput> players_;
    std::set<std::pair<int32_t, int32_t>> live_;
};

// The unit line's 16 fields after "unit <player> <id>", formatted by the .c5units writer.
bool unitFields(const UnitInput& u, const PlayerInput& owner, std::string& out, std::string& why) {
    UnitScene sc;
    sc.players.push_back(owner);
    UnitInput copy = u;
    copy.figures.clear();
    sc.units.push_back(copy);
    std::string text;
    units::SceneFileStatus st = units::writeScene(sc, text);
    if (!st.ok()) {
        why = st.msg;
        return false;
    }
    size_t p = text.find("\nunit\t");
    if (p == std::string::npos) {
        why = "no unit line";
        return false;
    }
    std::string line = text.substr(p + 1, text.find('\n', p + 1) - p - 1);
    std::vector<std::string> f = splitTabs(line);
    if (f.size() != 19) {
        why = "unexpected unit line";
        return false;
    }
    out.clear();
    for (size_t i = 3; i < f.size(); ++i) out += "\t" + f[i];
    return true;
}

}  // namespace

const char* timelineErrName(TimelineErr e) {
    switch (e) {
    case TimelineErr::Ok: return "Ok";
    case TimelineErr::Io: return "Io";
    case TimelineErr::BadHeader: return "BadHeader";
    case TimelineErr::BadRecord: return "BadRecord";
    case TimelineErr::BadValue: return "BadValue";
    case TimelineErr::UnknownUnit: return "UnknownUnit";
    case TimelineErr::Duplicate: return "Duplicate";
    case TimelineErr::Orphan: return "Orphan";
    case TimelineErr::OutOfOrder: return "OutOfOrder";
    case TimelineErr::TooLarge: return "TooLarge";
    case TimelineErr::BadScene: return "BadScene";
    }
    return "?";
}

const char* eventKindName(EventKind k) {
    int i = static_cast<int>(k);
    return i >= 0 && i < kKindCount ? kKindNames[i] : "?";
}

const char* actionKindName(ActionKind k) {
    int i = static_cast<int>(k);
    return i >= 0 && i < kActionCount ? kActionNames[i] : "?";
}

std::string TimelineStatus::str() const {
    std::string s = timelineErrName(code);
    if (line) s += " at line " + std::to_string(line);
    if (!msg.empty()) s += ": " + msg;
    return s;
}

TimelineStatus parseTimeline(const std::string& text, const UnitScene* start, Timeline& out) {
    out = Timeline();
    Parser p(start, out);
    TimelineStatus s = p.run(text);
    if (!s.ok()) out = Timeline();
    return s;
}

TimelineStatus writeTimeline(const Timeline& tl, const UnitScene* start, std::string& text) {
    text.clear();
    std::map<int32_t, PlayerInput> players;
    if (start)
        for (const PlayerInput& p : start->players) players[p.player] = p;
    std::string t = "c5anim\t1\n";
    t += "scene\t" + (tl.scene_path.empty() ? std::string("-") : tl.scene_path) + "\t" + std::to_string(tl.seed) +
         "\t" + (tl.forced ? "forced" : "free") + "\n";
    for (const PlayerInput& p : tl.players) {
        t += "player\t" + std::to_string(p.player) + "\t" + p.colour + "\t" + p.art_style + "\t" + p.era + "\n";
        players[p.player] = p;
    }
    for (const TimelineFrame& fr : tl.frames) {
        for (const TimelineEvent& e : fr.events) {
            t += "ev\t" + std::to_string(e.player) + "\t" + std::to_string(e.id) + "\t" + eventKindName(e.kind);
            std::string why, fields;
            switch (e.kind) {
            case EventKind::Create: {
                auto it = players.find(e.player);
                if (it == players.end())
                    return refuse(TimelineErr::Orphan, 0, "create: player " + std::to_string(e.player) + " unknown");
                UnitInput u = e.unit;
                u.player = e.player;
                u.id = e.id;
                if (!unitFields(u, it->second, fields, why)) return refuse(TimelineErr::BadValue, 0, "create: " + why);
                t += fields;
                break;
            }
            case EventKind::Remove: case EventKind::Reset: break;
            case EventKind::Fortify: case EventKind::Embark: case EventKind::Garrison: case EventKind::Destroyed:
            case EventKind::Arrive: case EventKind::Kill:
                if (e.kind == EventKind::Kill) t += "\t" + fmtFigures(e.figures);
                else t += "\t" + std::to_string(e.a);
                break;
            case EventKind::Move: t += "\t" + std::to_string(e.a) + "\t" + std::to_string(e.b); break;
            case EventKind::Turn: t += "\t" + std::to_string(e.a) + (e.b ? "\tright" : "\tleft"); break;
            case EventKind::Place:
                t += "\t" + std::to_string(e.a) + "\t" + std::to_string(e.b) + "\t" + std::to_string(e.c);
                break;
            case EventKind::Action:
                t += std::string("\t") + actionKindName(e.action) + "\t" +
                     (e.argument.empty() ? std::string("-") : e.argument) + "\t" + fmtFigures(e.figures);
                break;
            case EventKind::Damage: t += "\t" + std::to_string(e.a) + "\t" + std::to_string(e.b); break;
            case EventKind::Visibility: t += "\t" + std::to_string(e.a) + "\t" + fmtF(e.blend_time, false); break;
            case EventKind::Choice:
                t += "\t" + fmtFigures(e.figures) + "\t" + e.point + "\t" + std::to_string(e.a);
                break;
            case EventKind::Steer:
                t += "\t" + fmtFigures(e.figures) + "\t" + fmtF(e.weight, false) + "\t" + std::to_string(e.a);
                break;
            }
            t += "\n";
        }
        t += "frame\t" + std::to_string(fr.seq) + "\t" + fmtF(fr.dt, true) + "\t" +
             (fr.has_present ? fmtF(fr.present_time, true) : std::string("-")) + "\t" +
             (fr.has_exe_frame ? std::to_string(fr.exe_frame) : std::string("-")) + "\n";
    }
    Timeline back;
    TimelineStatus s = parseTimeline(t, start, back);
    if (!s.ok()) return s;
    text = std::move(t);
    return TimelineStatus();
}

namespace {

bool readAll(const std::wstring& path, std::string& text, TimelineStatus& st, size_t limit) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        st = refuse(TimelineErr::Io, 0, "cannot open the file");
        return false;
    }
    char buf[65536];
    bool tooLarge = false, readErr = false;
    for (;;) {
        size_t got = std::fread(buf, 1, sizeof buf, f);
        if (got) text.append(buf, got);
        if (text.size() > limit) {
            tooLarge = true;
            break;
        }
        if (got < sizeof buf) {
            readErr = std::ferror(f) != 0;
            break;
        }
    }
    std::fclose(f);
    if (tooLarge) {
        st = refuse(TimelineErr::TooLarge, 0, "file over " + std::to_string(limit) + " bytes");
        return false;
    }
    if (readErr) {
        st = refuse(TimelineErr::Io, 0, "read error");
        return false;
    }
    return true;
}

std::wstring folderOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"/\\");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p + 1);
}

std::wstring widen(const std::string& s) {   // the path field is ASCII-checked text (goodText)
    return std::wstring(s.begin(), s.end());
}

}  // namespace

TimelineStatus readTimelineFile(const std::wstring& path, Timeline& out, UnitScene& start) {
    out = Timeline();
    start = UnitScene();
    std::string text;
    TimelineStatus st;
    if (!readAll(path, text, st, kTimelineMaxFile)) return st;
    // The scene record is the second record; read it first to load the start scene.
    Timeline head;
    {
        size_t lines = 0, pos = 0;
        std::string firstTwo;
        while (pos < text.size() && lines < 64) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            ++lines;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (lines == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            if (line.find_first_not_of(" \t") == std::string::npos || line[0] == '#') continue;
            firstTwo += line + "\n";
            if (line.compare(0, 6, "scene\t") == 0) break;
        }
        TimelineStatus hs = parseTimeline(firstTwo, nullptr, head);
        if (!hs.ok()) {
            // Report it with the real line number: the whole text fails at the same record.
            TimelineStatus full = parseTimeline(text, nullptr, out);
            out = Timeline();
            return full.ok() ? hs : full;
        }
    }
    if (!head.scene_path.empty()) {
        units::SceneFileStatus ss = units::readSceneFile(folderOf(path) + widen(head.scene_path), start);
        if (!ss.ok()) return refuse(TimelineErr::BadScene, 0, head.scene_path + ": " + ss.str());
    }
    TimelineStatus s = parseTimeline(text, &start, out);
    if (!s.ok()) start = UnitScene();
    return s;
}

TimelineStatus writeTimelineFile(const std::wstring& path, const Timeline& tl, const UnitScene* start) {
    std::string text;
    TimelineStatus s = writeTimeline(tl, start, text);
    if (!s.ok()) return s;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return refuse(TimelineErr::Io, 0, "cannot create the file");
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) return refuse(TimelineErr::Io, 0, "write error");
    return TimelineStatus();
}

}}  // namespace c5r::unitanim
