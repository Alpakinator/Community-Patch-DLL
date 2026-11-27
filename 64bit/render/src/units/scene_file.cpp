// render/src/units/scene_file.cpp - the .c5units reader and writer (epic 0041, issue 4355; unit-renderer.md section 3).
// One source for both directions: the writer reads its own text back with the reader before it returns it.
#include "c5r/units/scene_file.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace c5r { namespace units {

const char* sceneFileErrName(SceneFileErr e) {
    switch (e) {
        case SceneFileErr::Ok: return "Ok";
        case SceneFileErr::Io: return "Io";
        case SceneFileErr::BadHeader: return "BadHeader";
        case SceneFileErr::BadRecord: return "BadRecord";
        case SceneFileErr::BadValue: return "BadValue";
        case SceneFileErr::Duplicate: return "Duplicate";
        case SceneFileErr::Orphan: return "Orphan";
        case SceneFileErr::TooLarge: return "TooLarge";
    }
    return "?";
}

std::string SceneFileStatus::str() const {
    std::string s = sceneFileErrName(code);
    if (line) s += " at line " + std::to_string(line);
    if (!msg.empty()) s += ": " + msg;
    return s;
}

namespace {

SceneFileStatus refuse(SceneFileErr c, size_t line, std::string msg) {
    SceneFileStatus s;
    s.code = c;
    s.line = line;
    s.msg = std::move(msg);
    return s;
}

// A string field: not empty, no control character (bytes 0x00-0x1F and 0x7F). Bytes >= 0x80 (UTF-8) are allowed.
bool goodString(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char ch : s)
        if (ch < 0x20 || ch == 0x7F) return false;
    return true;
}

// A complete decimal integer: optional '-', then digits only, in [lo, hi].
bool parseInt(const std::string& f, long long lo, long long hi, long long& v) {
    if (f.empty() || f.size() > 20) return false;
    size_t i = (f[0] == '-') ? 1 : 0;
    if (i == f.size()) return false;
    for (size_t k = i; k < f.size(); ++k)
        if (f[k] < '0' || f[k] > '9') return false;
    errno = 0;
    char* end = nullptr;
    long long x = std::strtoll(f.c_str(), &end, 10);
    if (errno != 0 || end != f.c_str() + f.size()) return false;
    if (x < lo || x > hi) return false;
    v = x;
    return true;
}

// A complete finite decimal float (no leading space, no hex, no inf/nan words).
bool parseFloat(const std::string& f, float& v) {
    if (f.empty() || f.size() > 64) return false;
    for (char ch : f) {
        bool ok = (ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E';
        if (!ok) return false;
    }
    if (f[0] == '+') return false;
    char* end = nullptr;
    float x = std::strtof(f.c_str(), &end);
    if (end != f.c_str() + f.size()) return false;
    if (!std::isfinite(x)) return false;
    v = x;
    return true;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t t = line.find('\t', at);
        if (t == std::string::npos) {
            out.push_back(line.substr(at));
            return out;
        }
        out.push_back(line.substr(at, t - at));
        at = t + 1;
    }
}

bool blankLine(const std::string& line) {
    for (char ch : line)
        if (ch != ' ' && ch != '\t') return false;
    return true;
}

class Reader {
public:
    SceneFileStatus run(const std::string& text, UnitScene& out) {
        if (text.size() > kSceneMaxFile)
            return refuse(SceneFileErr::TooLarge, 0, "file of " + std::to_string(text.size()) + " bytes, limit " +
                                                         std::to_string(kSceneMaxFile));
        size_t at = 0, lineNo = 0;
        // A UTF-8 byte-order mark at the start is accepted and skipped.
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
            at = 3;
        while (at < text.size()) {
            size_t nl = text.find('\n', at);
            size_t stop = (nl == std::string::npos) ? text.size() : nl;
            std::string line = text.substr(at, stop - at);
            at = (nl == std::string::npos) ? text.size() : nl + 1;
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > kSceneMaxLine)
                return refuse(SceneFileErr::TooLarge, lineNo, "line of " + std::to_string(line.size()) +
                                                                  " bytes, limit " + std::to_string(kSceneMaxLine));
            if (blankLine(line) || line[0] == '#') continue;
            SceneFileStatus s = record(line, lineNo);
            if (!s.ok()) return s;
        }
        if (!header_) return refuse(SceneFileErr::BadHeader, 0, "no c5units header (empty file)");
        if (!sceneSeen_) return refuse(SceneFileErr::BadRecord, 0, "no scene record");
        for (size_t i = 0; i < scene_.units.size(); ++i) {
            const UnitInput& u = scene_.units[i];
            if (!players_.count(u.player))
                return refuse(SceneFileErr::Orphan, unitLines_[i],
                              "unit (" + std::to_string(u.player) + ", " + std::to_string(u.id) +
                                  ") has no player record for player " + std::to_string(u.player));
        }
        out = std::move(scene_);
        return SceneFileStatus();
    }

private:
    SceneFileStatus record(const std::string& line, size_t n) {
        std::vector<std::string> f = splitTabs(line);
        const std::string& kind = f[0];
        if (!header_) {
            if (kind != "c5units" || f.size() != 2)
                return refuse(SceneFileErr::BadHeader, n, "first record must be \"c5units<TAB>1\", got \"" +
                                                              line.substr(0, 64) + "\"");
            if (f[1] != "1") return refuse(SceneFileErr::BadHeader, n, "format " + f[1].substr(0, 32) + " (only 1)");
            header_ = true;
            return SceneFileStatus();
        }
        if (kind == "scene") return sceneRec(f, n);
        if (kind == "player") return playerRec(f, n);
        if (kind == "unit") return unitRec(f, n);
        if (kind == "figure") return figureRec(f, n);
        if (kind == "c5units") return refuse(SceneFileErr::Duplicate, n, "second c5units header");
        return refuse(SceneFileErr::BadRecord, n, "unknown record kind \"" + kind.substr(0, 64) + "\"");
    }

    static SceneFileStatus count(const std::vector<std::string>& f, size_t want, size_t n) {
        if (f.size() == want) return SceneFileStatus();
        return refuse(SceneFileErr::BadRecord, n, f[0] + " record has " + std::to_string(f.size()) +
                                                      " fields, wants " + std::to_string(want));
    }
    static SceneFileStatus bad(size_t n, const char* field, const std::string& v, const char* want) {
        return refuse(SceneFileErr::BadValue, n, std::string(field) + " \"" + v.substr(0, 64) + "\": " + want);
    }

    SceneFileStatus sceneRec(const std::vector<std::string>& f, size_t n) {
        SceneFileStatus s = count(f, 3, n);
        if (!s.ok()) return s;
        if (sceneSeen_) return refuse(SceneFileErr::Duplicate, n, "second scene record");
        if (f[1] != "exe" && f[1] != "own") return bad(n, "position_source", f[1], "exe or own");
        float w = 0;
        if (!parseFloat(f[2], w)) return bad(n, "water_level", f[2], "a finite float");
        scene_.position_source = f[1];
        scene_.water_level = w;
        sceneSeen_ = true;
        return SceneFileStatus();
    }

    SceneFileStatus playerRec(const std::vector<std::string>& f, size_t n) {
        SceneFileStatus s = count(f, 5, n);
        if (!s.ok()) return s;
        long long p = 0;
        if (!parseInt(f[1], 0, 63, p)) return bad(n, "player", f[1], "an integer 0..63");
        if (!goodString(f[2])) return bad(n, "colour", f[2], "a Type name without control characters");
        if (!goodString(f[3])) return bad(n, "art_style", f[3], "a Type name without control characters");
        if (!goodString(f[4])) return bad(n, "era", f[4], "a Type name without control characters");
        if (players_.count(static_cast<int32_t>(p)))
            return refuse(SceneFileErr::Duplicate, n, "second player record for player " + f[1]);
        if (scene_.players.size() >= kSceneMaxPlayers)
            return refuse(SceneFileErr::TooLarge, n, "more than 64 players");
        PlayerInput pi;
        pi.player = static_cast<int32_t>(p);
        pi.colour = f[2];
        pi.art_style = f[3];
        pi.era = f[4];
        players_.insert(pi.player);
        scene_.players.push_back(std::move(pi));
        return SceneFileStatus();
    }

    SceneFileStatus unitRec(const std::vector<std::string>& f, size_t n) {
        SceneFileStatus s = count(f, 19, n);
        if (!s.ok()) return s;
        UnitInput u;
        long long v = 0;
        if (!parseInt(f[1], 0, 63, v)) return bad(n, "player", f[1], "an integer 0..63");
        u.player = static_cast<int32_t>(v);
        if (!parseInt(f[2], 0, INT32_MAX, v)) return bad(n, "id", f[2], "an integer 0..2147483647");
        u.id = static_cast<int32_t>(v);
        if (!goodString(f[3]) || f[3] == "-") return bad(n, "unit_type", f[3], "a Type name");
        u.unit_type = f[3];
        if (f[4] != "-") {
            if (!goodString(f[4])) return bad(n, "art_row", f[4], "a Type name or -");
            u.art_row = f[4];
        }
        if (!parseInt(f[5], 0, 32767, v)) return bad(n, "plot_x", f[5], "an integer 0..32767");
        u.plot_x = static_cast<int32_t>(v);
        if (!parseInt(f[6], 0, 32767, v)) return bad(n, "plot_y", f[6], "an integer 0..32767");
        u.plot_y = static_cast<int32_t>(v);
        const bool dash7 = f[7] == "-", dash8 = f[8] == "-", dash9 = f[9] == "-";
        if (dash7 || dash8 || dash9) {
            if (!(dash7 && dash8 && dash9)) return bad(n, "root", f[7] + " " + f[8] + " " + f[9], "all three or none");
            u.has_root = false;
        } else {
            if (!parseFloat(f[7], u.root_x)) return bad(n, "root_x", f[7], "a finite float or -");
            if (!parseFloat(f[8], u.root_y)) return bad(n, "root_y", f[8], "a finite float or -");
            if (!parseFloat(f[9], u.root_z)) return bad(n, "root_z", f[9], "a finite float or -");
            u.has_root = true;
        }
        // issue 4464: -1..7. 0..5 are the six DirectionTypes; 6 (NUM_DIRECTION_TYPES), 7 (the fork's NO_DIRECTION,
        // which a Lua InitUnit without a facing leaves: unit-formations.md 12.7.7 (4)) and -1 (stock's NO_DIRECTION,
        // 3907's catalogue) are what the DLL can hold besides; the module's facingYaw gives them 1 rad, as the EXE's
        // switch (unit-formations.md 11.2). Was 0..5 (the .c5units writer refused the DLL's 7, 4458).
        if (!parseInt(f[10], -1, 7, v)) return bad(n, "facing", f[10], "an integer -1..7");
        u.facing = static_cast<int8_t>(v);
        if (!parseInt(f[11], 0, UINT32_MAX, v)) return bad(n, "seed", f[11], "an integer 0..4294967295");
        u.formation_seed = static_cast<uint32_t>(v);
        long long maxHp = 0, dmg = 0;
        if (!parseInt(f[13], 1, 32767, maxHp)) return bad(n, "max_hp", f[13], "an integer 1..32767");
        if (!parseInt(f[12], 0, maxHp, dmg)) return bad(n, "damage", f[12], "an integer 0..max_hp");
        u.damage = static_cast<int16_t>(dmg);
        u.max_hp = static_cast<int16_t>(maxHp);
        if (f[14] == "-") {
            u.alive = -1;
        } else {
            if (!parseInt(f[14], 0, static_cast<long long>(kSceneMaxFigures), v))
                return bad(n, "alive", f[14], "an integer 0..64 or -");
            u.alive = static_cast<int16_t>(v);
        }
        if (!parseInt(f[15], 0, 127, v)) return bad(n, "flags", f[15], "an integer 0..127 (bits 0-6)");
        u.flags = static_cast<uint8_t>(v);
        if (!parseFloat(f[16], u.fade_from)) return bad(n, "fade_from", f[16], "a finite float");
        if (!parseFloat(f[17], u.fade_to)) return bad(n, "fade_to", f[17], "a finite float");
        if (!parseFloat(f[18], u.fade)) return bad(n, "fade", f[18], "a finite float");
        std::pair<int32_t, int32_t> key(u.player, u.id);
        if (unitKeys_.count(key))
            return refuse(SceneFileErr::Duplicate, n, "second unit (" + f[1] + ", " + f[2] + ")");
        if (scene_.units.size() >= kSceneMaxUnits) return refuse(SceneFileErr::TooLarge, n, "more than 4096 units");
        unitKeys_.insert(key);
        scene_.units.push_back(std::move(u));
        unitLines_.push_back(n);
        figures_.clear();
        return SceneFileStatus();
    }

    SceneFileStatus figureRec(const std::vector<std::string>& f, size_t n) {
        if (scene_.units.empty()) return refuse(SceneFileErr::Orphan, n, "figure record before any unit record");
        SceneFileStatus s = count(f, 4, n);
        if (!s.ok()) return s;
        long long v = 0;
        if (!parseInt(f[1], 0, static_cast<long long>(kSceneMaxFigures) - 1, v))
            return bad(n, "figure", f[1], "an integer 0..63");
        if (!goodString(f[2])) return bad(n, "clip", f[2], "an animation name without control characters");
        FigureInput fi;
        fi.figure = static_cast<uint16_t>(v);
        fi.clip = f[2];
        if (!parseFloat(f[3], fi.t) || fi.t < 0) return bad(n, "t", f[3], "a finite float >= 0");
        UnitInput& u = scene_.units.back();
        if (figures_.count(fi.figure))
            return refuse(SceneFileErr::Duplicate, n, "second figure " + f[1] + " in unit (" +
                                                          std::to_string(u.player) + ", " + std::to_string(u.id) + ")");
        if (u.figures.size() >= kSceneMaxFigures) return refuse(SceneFileErr::TooLarge, n, "more than 64 figures");
        figures_.insert(fi.figure);
        u.figures.push_back(std::move(fi));
        return SceneFileStatus();
    }

    bool header_ = false, sceneSeen_ = false;
    UnitScene scene_;
    std::set<int32_t> players_;
    std::set<std::pair<int32_t, int32_t>> unitKeys_;
    std::set<uint16_t> figures_;
    std::vector<size_t> unitLines_;
};

void addFloat(std::string& s, float v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
    s += '\t';
    s += buf;
}
void addInt(std::string& s, long long v) {
    s += '\t';
    s += std::to_string(v);
}
void addStr(std::string& s, const std::string& v) {
    s += '\t';
    s += v;
}

}  // namespace

SceneFileStatus parseScene(const std::string& text, UnitScene& out) {
    Reader r;
    SceneFileStatus s = r.run(text, out);
    if (!s.ok()) out = UnitScene();
    return s;
}

SceneFileStatus writeScene(const UnitScene& sc, std::string& text) {
    text.clear();
    if (sc.format != 1)
        return refuse(SceneFileErr::BadHeader, 0, "UnitScene.format " + std::to_string(sc.format) + " (only 1)");
    // Strings first: a tab or a line end inside one would change the records' shape instead of being refused.
    for (const PlayerInput& p : sc.players)
        if (!goodString(p.colour) || !goodString(p.art_style) || !goodString(p.era))
            return refuse(SceneFileErr::BadValue, 0, "player " + std::to_string(p.player) +
                                                         ": colour, art_style and era must be non-empty without "
                                                         "control characters");
    for (const UnitInput& u : sc.units) {
        std::string who = "unit (" + std::to_string(u.player) + ", " + std::to_string(u.id) + ")";
        if (!goodString(u.unit_type) || u.unit_type == "-")
            return refuse(SceneFileErr::BadValue, 0, who + ": unit_type must be a Type name");
        if (!u.art_row.empty() && (!goodString(u.art_row) || u.art_row == "-"))
            return refuse(SceneFileErr::BadValue, 0, who + ": art_row must be empty or a Type name");
        if (u.alive < -1)
            return refuse(SceneFileErr::BadValue, 0, who + ": alive " + std::to_string(u.alive) + " (-1 or 0..64)");
        for (const FigureInput& fi : u.figures)
            if (!goodString(fi.clip))
                return refuse(SceneFileErr::BadValue, 0, who + ": figure " + std::to_string(fi.figure) +
                                                             " clip must be non-empty without control characters");
    }
    std::string s = "c5units\t1\n";
    s += "scene";
    addStr(s, sc.position_source);
    addFloat(s, sc.water_level);
    s += '\n';
    for (const PlayerInput& p : sc.players) {
        s += "player";
        addInt(s, p.player);
        addStr(s, p.colour);
        addStr(s, p.art_style);
        addStr(s, p.era);
        s += '\n';
    }
    for (const UnitInput& u : sc.units) {
        s += "unit";
        addInt(s, u.player);
        addInt(s, u.id);
        addStr(s, u.unit_type);
        addStr(s, u.art_row.empty() ? std::string("-") : u.art_row);
        addInt(s, u.plot_x);
        addInt(s, u.plot_y);
        if (u.has_root) {
            addFloat(s, u.root_x);
            addFloat(s, u.root_y);
            addFloat(s, u.root_z);
        } else {
            s += "\t-\t-\t-";
        }
        addInt(s, u.facing);
        addInt(s, u.formation_seed);
        addInt(s, u.damage);
        addInt(s, u.max_hp);
        if (u.alive < 0) s += "\t-";
        else addInt(s, u.alive);
        addInt(s, u.flags);
        addFloat(s, u.fade_from);
        addFloat(s, u.fade_to);
        addFloat(s, u.fade);
        s += '\n';
        for (const FigureInput& fi : u.figures) {
            s += "figure";
            addInt(s, fi.figure);
            addStr(s, fi.clip);
            addFloat(s, fi.t);
            s += '\n';
        }
    }
    // Every other rule (ranges, duplicates, limits, a negative alive other than -1) is the reader's.
    UnitScene check;
    SceneFileStatus st = parseScene(s, check);
    if (!st.ok()) return st;
    text = std::move(s);
    return st;
}

SceneFileStatus readSceneFile(const std::wstring& path, UnitScene& out) {
    out = UnitScene();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return refuse(SceneFileErr::Io, 0, "cannot open the file");
    std::string text;
    char buf[65536];
    bool tooLarge = false, readErr = false;
    for (;;) {
        size_t got = std::fread(buf, 1, sizeof buf, f);
        if (got) text.append(buf, got);
        if (text.size() > kSceneMaxFile) {
            tooLarge = true;
            break;
        }
        if (got < sizeof buf) {
            readErr = std::ferror(f) != 0;
            break;
        }
    }
    std::fclose(f);
    if (tooLarge)
        return refuse(SceneFileErr::TooLarge, 0, "file over " + std::to_string(kSceneMaxFile) + " bytes");
    if (readErr) return refuse(SceneFileErr::Io, 0, "read error");
    return parseScene(text, out);
}

SceneFileStatus writeSceneFile(const std::wstring& path, const UnitScene& scene) {
    std::string text;
    SceneFileStatus s = writeScene(scene, text);
    if (!s.ok()) return s;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return refuse(SceneFileErr::Io, 0, "cannot create the file");
    size_t put = std::fwrite(text.data(), 1, text.size(), f);
    bool closed = std::fclose(f) == 0;
    if (put != text.size() || !closed) return refuse(SceneFileErr::Io, 0, "write error");
    return s;
}

}}  // namespace c5r::units
