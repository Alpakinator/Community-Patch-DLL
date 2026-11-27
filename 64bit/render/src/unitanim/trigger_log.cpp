// trigger_log.cpp - the offline trigger log (ticket 4721): vp_trigger.h's records and names as text lines, the
// contract's record checks, the parser and the server's writer. Format: c5r/unitanim/trigger_log.h.
#include "c5r/unitanim/trigger_log.h"

#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace c5r { namespace unitanim {

namespace {

const char* const kKinds[VPTRIG_KIND_COUNT] = {"None", "SoundStart", "SoundStop", "EffectStart", "EffectMove",
                                               "EffectStop", "OwnerEnd"};
const char* const kReasons[VPTRIG_END_COUNT] = {"Natural", "Limit", "Refused", "Gone", "Reset", "ClipEnded"};
const char* const kNameKinds[VPTRIG_NAME_KIND_COUNT] = {"none", "sound3d", "effect", "triggerfile"};
struct FlagName { uint32_t bit; const char* name; };
const FlagName kFlags[] = {{VPTRIG_FLAG_LOOP, "loop"}, {VPTRIG_FLAG_FOLLOWS_OWNER, "follows"},
                           {VPTRIG_FLAG_PLAYED, "played"}};
const char* const kHeader = "# c5r trigger log ";

Status bad(const std::string& m) { return Status::fail(Err::BadLayout, m); }

bool finite(float f) { return std::isfinite(f) != 0; }

std::string fmtFloat(float f) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.9g", static_cast<double>(f));
    return b;
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

bool parseI64(const std::string& s, int64_t& v) {
    if (s.empty() || std::isspace(static_cast<unsigned char>(s[0]))) return false;
    errno = 0;
    char* end = nullptr;
    long long x = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) return false;
    v = static_cast<int64_t>(x);
    return true;
}
bool parseU64(const std::string& s, uint64_t& v) {
    if (s.empty() || s[0] == '-' || s[0] == '+' || std::isspace(static_cast<unsigned char>(s[0]))) return false;
    errno = 0;
    char* end = nullptr;
    int base = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    unsigned long long x = std::strtoull(s.c_str(), &end, base);
    if (errno != 0 || end != s.c_str() + s.size()) return false;
    v = static_cast<uint64_t>(x);
    return true;
}
bool parseI32(const std::string& s, int32_t& v) {
    int64_t x = 0;
    if (!parseI64(s, x) || x < INT32_MIN || x > INT32_MAX) return false;
    v = static_cast<int32_t>(x);
    return true;
}
bool parseU32(const std::string& s, uint32_t& v) {
    uint64_t x = 0;
    if (!parseU64(s, x) || x > UINT32_MAX) return false;
    v = static_cast<uint32_t>(x);
    return true;
}
bool parseFloat(const std::string& s, float& v) {
    if (s.empty() || std::isspace(static_cast<unsigned char>(s[0]))) return false;
    errno = 0;
    char* end = nullptr;
    float x = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    if (errno == ERANGE && !finite(x)) return false;   // overflow; a denormal's ERANGE (underflow) stays accepted
    v = x;
    return true;
}
bool parseFloats(const std::string& s, float* v, int n) {
    size_t at = 0;
    for (int i = 0; i < n; ++i) {
        size_t comma = s.find(',', at);
        bool last = i == n - 1;
        if (last != (comma == std::string::npos)) return false;
        std::string part = s.substr(at, last ? std::string::npos : comma - at);
        if (!parseFloat(part, v[i])) return false;
        at = comma + 1;
    }
    return true;
}
template <size_t N>
bool parseEnum(const std::string& s, const char* const (&names)[N], uint8_t& v) {
    for (size_t i = 0; i < N; ++i)
        if (s == names[i]) { v = static_cast<uint8_t>(i); return true; }
    uint32_t x = 0;
    if (!parseU32(s, x) || x > 255) return false;
    v = static_cast<uint8_t>(x);
    return true;
}
bool parseFlags(const std::string& s, uint32_t& v) {
    v = 0;
    if (s == "-") return true;
    size_t at = 0;
    for (;;) {
        size_t bar = s.find('|', at);
        std::string part = s.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
        bool known = false;
        for (const FlagName& f : kFlags)
            if (part == f.name) { v |= f.bit; known = true; }
        if (!known) {
            uint32_t x = 0;
            if (part.size() < 3 || part[0] != '0' || part[1] != 'x' || !parseU32(part, x) || x == 0) return false;
            v |= x;
        }
        if (bar == std::string::npos) return true;
        at = bar + 1;
    }
}
std::string fmtFlags(uint32_t flags) {
    std::string s;
    uint32_t rest = flags;
    for (const FlagName& f : kFlags)
        if (flags & f.bit) { s += (s.empty() ? "" : "|"); s += f.name; rest &= ~f.bit; }
    if (rest) {
        char b[16];
        std::snprintf(b, sizeof(b), "0x%" PRIx32, rest);
        s += (s.empty() ? "" : "|");
        s += b;
    }
    return s.empty() ? "-" : s;
}
std::string fmtEnum(uint8_t v, const char* const* names, size_t n) {
    if (v < n) return names[v];
    return std::to_string(static_cast<unsigned>(v));
}

bool nameTextOk(const std::string& t) {
    if (t.empty() || t.size() >= VPTRIG_NAME_TEXT) return false;
    for (char c : t)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    return true;
}

// the record's keys in the written order
enum Key { kV, kSeq, kF, kKind, kReason, kFlagsK, kOwner, kFseq, kPlayer, kUnit, kFigure, kHandle, kInst, kName, kFile,
           kPos, kScale, kOrient, kVel, kEc, kTrig, kT, kLate, kKeyCount };
const char* const kKeys[kKeyCount] = {"v", "seq", "F", "kind", "reason", "flags", "owner", "fseq", "player", "unit",
                                      "figure", "handle", "inst", "name", "file", "pos", "scale", "orient", "vel",
                                      "ec", "trig", "t", "late"};

} // namespace

const char* triggerKindName(uint8_t kind) { return kind < VPTRIG_KIND_COUNT ? kKinds[kind] : "?"; }
const char* triggerReasonName(uint8_t reason) { return reason < VPTRIG_END_COUNT ? kReasons[reason] : "?"; }
const char* triggerNameKindName(uint32_t kind) { return kind < VPTRIG_NAME_KIND_COUNT ? kNameKinds[kind] : "?"; }

Status checkTriggerRecord(const VPTrigRecord& r) {
    if (r.version != VPTRIG_VERSION)
        return Status::fail(Err::BadVersion, "version " + std::to_string(r.version) + ", expected " +
                                                 std::to_string(VPTRIG_VERSION));
    if (r.kind == VPTRIG_NONE || r.kind >= VPTRIG_KIND_COUNT) return bad("kind " + std::to_string(r.kind));
    if (r.frameId < 0) return bad("frame id " + std::to_string(r.frameId) + " below 0");
    if (r.flags & ~VPTRIG_FLAGS_KNOWN) return bad("unknown flag bits " + fmtFlags(r.flags & ~VPTRIG_FLAGS_KNOWN));
    if (r.reserved0 != 0) return bad("reserved0 not 0");
    if (r.figure < -1) return bad("figure " + std::to_string(r.figure));
    const float* fl[] = {r.pos, r.pos + 1, r.pos + 2, &r.scale, r.orient, r.orient + 1, r.orient + 2, r.orient + 3,
                         r.vel, r.vel + 1, r.vel + 2, &r.clipTime, &r.late};
    for (const float* f : fl)
        if (!finite(*f)) return bad("a float field is not finite");
    if (r.late < 0) return bad("late below 0");
    const bool start = r.kind == VPTRIG_SOUND_START || r.kind == VPTRIG_EFFECT_START;
    const bool effect = r.kind == VPTRIG_EFFECT_START || r.kind == VPTRIG_EFFECT_MOVE || r.kind == VPTRIG_EFFECT_STOP;
    if ((r.flags & (VPTRIG_FLAG_LOOP | VPTRIG_FLAG_FOLLOWS_OWNER)) && !start)
        return bad(std::string("loop / follows on a ") + kKinds[r.kind] + " record (Start records only)");
    if (r.kind == VPTRIG_OWNER_END) {
        if (r.reason >= VPTRIG_END_COUNT) return bad("OwnerEnd reason " + std::to_string(r.reason));
        if (((r.flags & VPTRIG_FLAG_PLAYED) != 0) != VPTrigPlayedFor(r.reason))
            return bad(std::string("OwnerEnd ") + kReasons[r.reason] + ": played must be " +
                       (VPTrigPlayedFor(r.reason) ? "set" : "clear") + " (R8)");
        if (r.handle != 0 || r.name != 0) return bad("OwnerEnd with a handle or a name");
        const bool clip = r.reason == VPTRIG_END_CLIP_ENDED;
        if (clip && (r.figure < 0 || r.instance == 0 || r.unit < 0 || r.player < 0))
            return bad("OwnerEnd ClipEnded without its player, unit, figure and clip instance (R7)");
        if (!clip && (r.figure != -1 || r.instance != 0))
            return bad(std::string("OwnerEnd ") + kReasons[r.reason] + " of a fight with a figure or an instance (R7)");
        if (!clip && r.owner == 0 && r.fightSeq == 0) return bad("fight OwnerEnd naming no fight (owner and fseq 0)");
    } else {
        if (r.reason != 0) return bad(std::string("reason set on a ") + kKinds[r.kind] + " record");
        if (r.flags & VPTRIG_FLAG_PLAYED) return bad("played on a record that is not OwnerEnd");
        if (r.handle == 0) return bad(std::string(kKinds[r.kind]) + " without a handle");
    }
    if (start) {
        if (r.name == 0) return bad(std::string(kKinds[r.kind]) + " without a name");
        if (r.owner == 0 && r.fightSeq == 0 && (r.player < 0 || r.unit < 0))
            return bad(std::string(kKinds[r.kind]) + " without an owner (R7)");
    } else if (r.name != 0 && r.kind != VPTRIG_OWNER_END) {
        return bad(std::string(kKinds[r.kind]) + " with a name (Start records only)");
    }
    if (!effect) {
        bool zero = r.scale == 0 && r.vel[0] == 0 && r.vel[1] == 0 && r.vel[2] == 0;
        for (float o : r.orient) zero = zero && o == 0;
        if (!zero) return bad(std::string(kKinds[r.kind]) + " with an orientation, scale or velocity (effects only)");
    } else if (r.kind == VPTRIG_EFFECT_START && !(r.scale > 0)) {
        return bad("EffectStart with scale " + fmtFloat(r.scale) + " (above 0)");
    }
    return Status::Ok();
}

std::string formatTriggerRecord(const VPTrigRecord& r) {
    char b[160];
    std::string s = "R";
    std::snprintf(b, sizeof(b), " v=%u seq=%" PRIu64 " F=%" PRId64, static_cast<unsigned>(r.version), r.seq, r.frameId);
    s += b;
    s += " kind=" + fmtEnum(r.kind, kKinds, VPTRIG_KIND_COUNT);
    s += " reason=" + ((r.kind == VPTRIG_OWNER_END || r.reason != 0) ? fmtEnum(r.reason, kReasons, VPTRIG_END_COUNT)
                                                                     : std::string("-"));
    s += " flags=" + fmtFlags(r.flags);
    std::snprintf(b, sizeof(b), " owner=%" PRIu32 " fseq=%" PRIu32 " player=%" PRId32 " unit=%" PRId32
                  " figure=%" PRId32 " handle=%" PRIu32 " inst=%" PRIu64 " name=%" PRIu32 " file=%" PRIu32,
                  r.owner, r.fightSeq, r.player, r.unit, r.figure, r.handle, r.instance, r.name, r.file);
    s += b;
    s += " pos=" + fmtFloat(r.pos[0]) + "," + fmtFloat(r.pos[1]) + "," + fmtFloat(r.pos[2]);
    s += " scale=" + fmtFloat(r.scale);
    s += " orient=" + fmtFloat(r.orient[0]) + "," + fmtFloat(r.orient[1]) + "," + fmtFloat(r.orient[2]) + "," +
         fmtFloat(r.orient[3]);
    s += " vel=" + fmtFloat(r.vel[0]) + "," + fmtFloat(r.vel[1]) + "," + fmtFloat(r.vel[2]);
    std::snprintf(b, sizeof(b), " ec=%" PRId32 " trig=%" PRId32, r.ec, r.trigger);
    s += b;
    s += " t=" + fmtFloat(r.clipTime) + " late=" + fmtFloat(r.late);
    return s;
}

Status parseTriggerRecord(const std::string& line, VPTrigRecord& out) {
    if (line.size() < 2 || line[0] != 'R' || line[1] != ' ') return bad("not a record line (R ...)");
    VPTrigRecord r;
    std::memset(&r, 0, sizeof(r));
    bool seen[kKeyCount] = {};
    size_t at = 2;
    while (at < line.size()) {
        size_t sp = line.find(' ', at);
        std::string tok = line.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        at = sp == std::string::npos ? line.size() : sp + 1;
        if (tok.empty()) return bad("two spaces in a row");
        size_t eq = tok.find('=');
        if (eq == std::string::npos || eq == 0) return bad("token '" + tok + "' is not key=value");
        std::string key = tok.substr(0, eq), val = tok.substr(eq + 1);
        int k = -1;
        for (int i = 0; i < kKeyCount; ++i)
            if (key == kKeys[i]) k = i;
        if (k < 0) return bad("unknown key '" + key + "'");
        if (seen[k]) return bad("key '" + key + "' twice");
        seen[k] = true;
        bool ok = true;
        uint32_t u32 = 0;
        switch (k) {
        case kV: ok = parseU32(val, u32) && u32 <= 0xFFFF; r.version = static_cast<uint16_t>(u32); break;
        case kSeq: ok = parseU64(val, r.seq); break;
        case kF: ok = parseI64(val, r.frameId); break;
        case kKind: ok = parseEnum(val, kKinds, r.kind); break;
        case kReason: if (val == "-") r.reason = 0; else ok = parseEnum(val, kReasons, r.reason); break;
        case kFlagsK: ok = parseFlags(val, r.flags); break;
        case kOwner: ok = parseU32(val, r.owner); break;
        case kFseq: ok = parseU32(val, r.fightSeq); break;
        case kPlayer: ok = parseI32(val, r.player); break;
        case kUnit: ok = parseI32(val, r.unit); break;
        case kFigure: ok = parseI32(val, r.figure); break;
        case kHandle: ok = parseU32(val, r.handle); break;
        case kInst: ok = parseU64(val, r.instance); break;
        case kName: ok = parseU32(val, r.name); break;
        case kFile: ok = parseU32(val, r.file); break;
        case kPos: ok = parseFloats(val, r.pos, 3); break;
        case kScale: ok = parseFloat(val, r.scale); break;
        case kOrient: ok = parseFloats(val, r.orient, 4); break;
        case kVel: ok = parseFloats(val, r.vel, 3); break;
        case kEc: ok = parseI32(val, r.ec); break;
        case kTrig: ok = parseI32(val, r.trigger); break;
        case kT: ok = parseFloat(val, r.clipTime); break;
        case kLate: ok = parseFloat(val, r.late); break;
        default: ok = false; break;
        }
        if (!ok) return bad("bad value for '" + key + "': '" + val + "'");
    }
    for (int i = 0; i < kKeyCount; ++i)
        if (!seen[i]) return bad(std::string("key '") + kKeys[i] + "' missing");
    out = r;
    return Status::Ok();
}

std::string formatTriggerName(const TriggerName& n) {
    return "N " + std::to_string(n.index) + " " + triggerNameKindName(n.kind) + " " + n.text;
}

Status parseTriggerName(const std::string& line, TriggerName& out) {
    if (line.size() < 2 || line[0] != 'N' || line[1] != ' ') return bad("not a name line (N ...)");
    size_t a = line.find(' ', 2);
    if (a == std::string::npos) return bad("name line without a kind");
    size_t b = line.find(' ', a + 1);
    if (b == std::string::npos) return bad("name line without a text");
    TriggerName n;
    if (!parseU32(line.substr(2, a - 2), n.index) || n.index == 0 || n.index >= VPTRIG_NAME_CAPACITY)
        return bad("bad name index '" + line.substr(2, a - 2) + "'");
    std::string kind = line.substr(a + 1, b - a - 1);
    n.kind = VPTRIG_NAME_NONE;
    for (uint32_t i = 1; i < VPTRIG_NAME_KIND_COUNT; ++i)
        if (kind == kNameKinds[i]) n.kind = i;
    if (n.kind == VPTRIG_NAME_NONE) return bad("bad name kind '" + kind + "'");
    n.text = line.substr(b + 1);
    if (!nameTextOk(n.text)) return bad("bad name text (empty, over 119 bytes or a control character)");
    out = n;
    return Status::Ok();
}

std::string formatTriggerLog(const TriggerLog& log) {
    std::string s = std::string(kHeader) + std::to_string(VPTRIG_VERSION) + "\n";
    for (const TriggerName& n : log.names) s += formatTriggerName(n) + "\n";
    for (const VPTrigRecord& r : log.records) s += formatTriggerRecord(r) + "\n";
    return s;
}

Status parseTriggerLog(const std::string& text, TriggerLog& out) {
    TriggerLog log;
    size_t at = 0;
    int lineNo = 0;
    bool header = false;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string where = "line " + std::to_string(lineNo) + ": ";
        if (!header) {
            if (!startsWith(line, kHeader)) return Status::fail(Err::BadMagic, where + "not a trigger log");
            uint32_t v = 0;
            if (!parseU32(line.substr(std::strlen(kHeader)), v))
                return Status::fail(Err::BadMagic, where + "no version in the header");
            if (v != VPTRIG_VERSION)
                return Status::fail(Err::BadVersion, where + "version " + std::to_string(v) + ", expected " +
                                                         std::to_string(VPTRIG_VERSION));
            header = true;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == 'N') {
            TriggerName n;
            Status st = parseTriggerName(line, n);
            if (!st) return bad(where + st.msg);
            if (n.index != log.names.size() + 1)
                return bad(where + "name " + std::to_string(n.index) + " out of order (R5: expected " +
                           std::to_string(log.names.size() + 1) + ")");
            log.names.push_back(n);
        } else if (line[0] == 'R') {
            VPTrigRecord r;
            Status st = parseTriggerRecord(line, r);
            if (!st) return bad(where + st.msg);
            st = checkTriggerRecord(r);
            if (!st) return Status::fail(st.code, where + st.msg);
            if (r.name > log.names.size() || r.file > log.names.size())
                return bad(where + "a name or file index not declared before it (R5)");
            if (!log.records.empty() && r.frameId < log.records.back().frameId)
                return bad(where + "frame id goes back (R1)");
            if (!log.records.empty() && r.seq <= log.records.back().seq) return bad(where + "seq not increasing (R1)");
            log.records.push_back(r);
        } else {
            return bad(where + "unknown line");
        }
    }
    if (!header) return Status::fail(Err::Truncated, "empty trigger log");
    out = std::move(log);
    return Status::Ok();
}

Status readTriggerLog(const std::string& path, TriggerLog& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return Status::fail(Err::Io, "cannot open " + path);
    std::string text;
    char buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    bool err = std::ferror(f) != 0;
    std::fclose(f);
    if (err) return Status::fail(Err::Io, "read error in " + path);
    return parseTriggerLog(text, out);
}

TriggerLogWriter::~TriggerLogWriter() { close(); }

Status TriggerLogWriter::open(const std::string& path) {
    close();
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) return Status::fail(Err::Io, "cannot create " + path);
    path_ = path;
    names_ = 0;
    records_ = 0;
    std::string h = std::string(kHeader) + std::to_string(VPTRIG_VERSION) + "\n";
    if (std::fputs(h.c_str(), f_) < 0) return Status::fail(Err::Io, "write error in " + path_);
    return Status::Ok();
}

Status TriggerLogWriter::name(const TriggerName& n) {
    if (!f_) return Status::fail(Err::Io, "trigger log not open");
    if (n.index != names_ + 1)
        return bad("name " + std::to_string(n.index) + " out of order (R5: expected " + std::to_string(names_ + 1) + ")");
    if (n.kind == VPTRIG_NAME_NONE || n.kind >= VPTRIG_NAME_KIND_COUNT) return bad("bad name kind");
    if (!nameTextOk(n.text)) return bad("bad name text (empty, over 119 bytes or a control character)");
    std::string l = formatTriggerName(n) + "\n";
    if (std::fputs(l.c_str(), f_) < 0) return Status::fail(Err::Io, "write error in " + path_);
    ++names_;
    return Status::Ok();
}

Status TriggerLogWriter::record(const VPTrigRecord& r) {
    if (!f_) return Status::fail(Err::Io, "trigger log not open");
    C5R_TRY(checkTriggerRecord(r));
    if (r.name > names_ || r.file > names_) return bad("a name or file index not declared before it (R5)");
    std::string l = formatTriggerRecord(r) + "\n";
    if (std::fputs(l.c_str(), f_) < 0) return Status::fail(Err::Io, "write error in " + path_);
    ++records_;
    return Status::Ok();
}

Status TriggerLogWriter::close() {
    if (!f_) return Status::Ok();
    bool err = std::fflush(f_) != 0;
    err = (std::fclose(f_) != 0) || err;
    f_ = nullptr;
    if (err) return Status::fail(Err::Io, "write error in " + path_);
    return Status::Ok();
}

}} // namespace c5r::unitanim
