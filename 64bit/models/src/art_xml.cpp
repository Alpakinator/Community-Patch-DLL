// art_xml.cpp - small XML scanner, .fxsxml and WonderArtInfo parsers (epic 3100, issue 3104).
// Own code, GPL-3.0-or-later. Not a general XML parser: no namespaces, no DTD processing, UTF-8 only; enough for
// the game's art files (research/05-graphics-composition/evidence/3100/art/fxsxml_survey.txt).
#include "c5models/art.h"

#include <cstdlib>
#include <cstring>

namespace c5m {

const char* const kWonderArtFiles[] = {
    "civ5artdefines_wonders.xml",                      // Resource/Common/WonderModels.fpk
    "civ5artdefines_wonders_polynesia.xml",            // Assets/DLC/DLC_03/Buildings
    "civ5artdefines_dlc_ancientwonders_wonders.xml",   // Assets/DLC/DLC_06/Buildings/Wonders
    "civ5artdefines_dlc_07_wonders.xml",               // Assets/DLC/DLC_07/Buildings/Wonders
    "civ5artdefines_expansion_wonders.xml",            // Assets/DLC/Expansion/Buildings/Wonders
    "civ5artdefines_inherited_expansion2_wonders.xml", // Assets/DLC/Expansion2/Buildings/Wonders
    "civ5artdefines_expansion2_wonders.xml",           // Assets/DLC/Expansion2/Buildings/Wonders
};
const size_t kWonderArtFileCount = sizeof(kWonderArtFiles) / sizeof(kWonderArtFiles[0]);

const std::string* XmlElement::attr(const char* n) const {
    for (auto& a : attrs)
        if (a.first == n) return &a.second;
    return nullptr;
}

const XmlElement* XmlElement::child(const char* n) const {
    for (auto& c : children)
        if (c.name == n) return &c;
    return nullptr;
}

namespace {

struct Scanner {
    const char* p;
    const char* end;
    const char* begin;
    std::string error;
    int dashed_comments = 0;

    int line_at(const char* q) const {
        int l = 1;
        for (const char* s = begin; s < q && s < end; ++s)
            if (*s == '\n') ++l;
        return l;
    }
    bool fail(const char* where, const std::string& what) {
        if (error.empty()) error = "line " + std::to_string(line_at(where)) + ": " + what;
        return false;
    }
    bool starts(const char* s) const {
        size_t n = std::strlen(s);
        return (size_t)(end - p) >= n && std::memcmp(p, s, n) == 0;
    }
    static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    static bool name_start(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' || (unsigned char)c >= 0x80;
    }
    static bool name_char(char c) { return name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.'; }
    void skip_space() {
        while (p < end && space(*p)) ++p;
    }
    bool skip_past(const char* term, const char* what) {
        const char* s = p;
        size_t n = std::strlen(term);
        while ((size_t)(end - p) >= n) {
            if (std::memcmp(p, term, n) == 0) {
                p += n;
                return true;
            }
            ++p;
        }
        return fail(s, std::string("unterminated ") + what);
    }
    // A comment ends at the first "-->" (the counted lenience, art.h XmlLenience): a body holding "--" is counted.
    bool skip_comment() {
        const char* body = p + 4;
        p = body;
        if (!skip_past("-->", "comment")) return false;
        for (const char* q = body; q + 1 < p - 3; ++q)
            if (q[0] == '-' && q[1] == '-') {
                ++dashed_comments;
                break;
            }
        return true;
    }
    bool read_name(std::string& out) {
        if (p >= end || !name_start(*p)) return fail(p, "expected a name");
        const char* s = p;
        while (p < end && name_char(*p)) ++p;
        out.assign(s, p);
        return true;
    }
    // Decodes entity references in [s, e).
    bool decode(const char* s, const char* e, std::string& out) {
        out.clear();
        while (s < e) {
            if (*s != '&') {
                out += *s++;
                continue;
            }
            const char* semi = (const char*)std::memchr(s, ';', (size_t)(e - s));
            if (!semi || semi - s > 12) return fail(s, "bad entity reference");
            std::string ent(s + 1, semi);
            if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "amp") out += '&';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (ent.size() > 1 && ent[0] == '#') {
                char* q = nullptr;
                unsigned long cp = ent[1] == 'x' || ent[1] == 'X' ? std::strtoul(ent.c_str() + 2, &q, 16)
                                                                 : std::strtoul(ent.c_str() + 1, &q, 10);
                if (!q || *q || cp == 0 || cp > 0x10FFFF) return fail(s, "bad character reference &" + ent + ";");
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) {
                    out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                } else {
                    out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
                }
            } else
                return fail(s, "unknown entity &" + ent + ";");
            s = semi + 1;
        }
        return true;
    }
    // Skips comments, processing instructions, DOCTYPE and white space between markup. Returns false on error.
    bool skip_misc() {
        for (;;) {
            skip_space();
            if (starts("<!--")) { if (!skip_comment()) return false; }
            else if (starts("<?")) { if (!skip_past("?>", "processing instruction")) return false; }
            else if (starts("<!DOCTYPE")) { if (!skip_past(">", "DOCTYPE")) return false; }
            else return true;
        }
    }
    bool element(XmlElement& el, int depth) {
        if (depth > 256) return fail(p, "elements nested too deeply");
        const char* open = p;
        el.line = line_at(p);
        ++p;   // '<'
        if (!read_name(el.name)) return false;
        for (;;) {
            const char* before = p;
            skip_space();
            if (p >= end) return fail(open, "unterminated start tag <" + el.name + ">");
            if (*p == '/') {
                if (p + 1 >= end || p[1] != '>') return fail(p, "expected '>' after '/' in <" + el.name + ">");
                p += 2;
                return true;
            }
            if (*p == '>') { ++p; break; }
            if (before == p) return fail(p, "expected white space before an attribute in <" + el.name + ">");
            std::string an, av;
            if (!read_name(an)) return false;
            skip_space();
            if (p >= end || *p != '=') return fail(p, "expected '=' after attribute " + an);
            ++p;
            skip_space();
            if (p >= end || (*p != '"' && *p != '\'')) return fail(p, "expected a quoted value for attribute " + an);
            char q = *p++;
            const char* s = p;
            while (p < end && *p != q) {
                if (*p == '<') return fail(p, "'<' in attribute value " + an);
                ++p;
            }
            if (p >= end) return fail(s, "unterminated value of attribute " + an);
            if (!decode(s, p, av)) return false;
            ++p;
            for (auto& a : el.attrs)
                if (a.first == an) return fail(s, "duplicate attribute " + an);
            el.attrs.emplace_back(std::move(an), std::move(av));
        }
        // content
        std::string text;
        for (;;) {
            if (p >= end) return fail(open, "element <" + el.name + "> not closed");
            if (*p == '<') {
                if (starts("<!--")) { if (!skip_comment()) return false; continue; }
                if (starts("<?")) { if (!skip_past("?>", "processing instruction")) return false; continue; }
                if (starts("<![CDATA[")) {
                    p += 9;
                    const char* s = p;
                    if (!skip_past("]]>", "CDATA section")) return false;
                    text.append(s, p - 3);
                    continue;
                }
                if (starts("</")) {
                    p += 2;
                    std::string cn;
                    const char* s = p;
                    if (!read_name(cn)) return false;
                    if (cn != el.name) return fail(s, "</" + cn + "> closes <" + el.name + ">");
                    skip_space();
                    if (p >= end || *p != '>') return fail(p, "expected '>' in end tag </" + cn + ">");
                    ++p;
                    break;
                }
                el.children.emplace_back();
                if (!element(el.children.back(), depth + 1)) return false;
                continue;
            }
            const char* s = p;
            while (p < end && *p != '<') ++p;
            std::string chunk;
            if (!decode(s, p, chunk)) return false;
            text += chunk;
        }
        size_t a = text.find_first_not_of(" \t\r\n");
        size_t e = text.find_last_not_of(" \t\r\n");
        el.text = a == std::string::npos ? std::string() : text.substr(a, e - a + 1);
        return true;
    }
};

void set_err(Error* err, Err code, const std::string& what) {
    if (err) *err = {code, what};
}

bool parse_ec(const std::string& s, std::vector<int>& out) {
    out.clear();
    const char* p = s.c_str();
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* q = nullptr;
        long v = std::strtol(p, &q, 10);
        if (q == p) return false;
        out.push_back((int)v);
        p = q;
        if (*p && *p != ',' && *p != ' ' && *p != '\t') return false;
    }
    return !out.empty();
}

}  // namespace

namespace {
int count_text_elements(const XmlElement& e) {
    int n = e.text.empty() ? 0 : 1;
    for (auto& c : e.children) n += count_text_elements(c);
    return n;
}
}  // namespace

bool parse_xml(const char* data, size_t size, XmlElement& root, Error* err) {
    return parse_xml(data, size, root, err, nullptr);
}

bool parse_xml(const char* data, size_t size, XmlElement& root, Error* err, XmlLenience* lenience) {
    if (lenience) *lenience = XmlLenience();
    if (size >= 2 && (((uint8_t)data[0] == 0xFF && (uint8_t)data[1] == 0xFE) ||
                      ((uint8_t)data[0] == 0xFE && (uint8_t)data[1] == 0xFF))) {
        set_err(err, Err::Xml, "UTF-16 XML is not supported");
        return false;
    }
    Scanner s{data, data + size, data, std::string()};
    if (size >= 3 && (uint8_t)data[0] == 0xEF && (uint8_t)data[1] == 0xBB && (uint8_t)data[2] == 0xBF) s.p += 3;
    root = XmlElement();
    bool ok = s.skip_misc();
    if (ok && (s.p >= s.end || *s.p != '<')) ok = s.fail(s.p, "no root element");
    if (ok) ok = s.element(root, 0);
    if (ok) ok = s.skip_misc();
    if (ok && s.p < s.end) {
        // trailing NULs (padding) are tolerated
        const char* q = s.p;
        while (q < s.end && *q == '\0') ++q;
        if (q < s.end) ok = s.fail(s.p, "content after the root element");
    }
    if (!ok) set_err(err, Err::Xml, s.error);
    if (ok && lenience) {
        lenience->dashed_comments = s.dashed_comments;
        lenience->text_elements = count_text_elements(root);
    }
    return ok;
}

bool parse_fxsxml(const char* data, size_t size, FxsAsset& out, Error* err) {
    out = FxsAsset();
    XmlElement root;
    if (!parse_xml(data, size, root, err)) return false;
    if (root.name != "Asset") {
        set_err(err, Err::Xml, "root element is <" + root.name + ">, expected <Asset>");
        return false;
    }
    auto need_file = [&](const XmlElement& e, std::string& v) {
        const std::string* f = e.attr("file");
        if (!f || f->empty()) {
            set_err(err, Err::Xml, "line " + std::to_string(e.line) + ": <" + e.name + "> without a file attribute");
            return false;
        }
        v = *f;
        return true;
    };
    for (auto& e : root.children) {
        if (e.name == "Mesh") {
            std::string f;
            if (!need_file(e, f)) return false;
            if (out.mesh_count++ == 0) out.mesh = f;
        } else if (e.name == "Animation") {
            FxsAnimation a;
            if (!need_file(e, a.file)) return false;
            if (const std::string* ec = e.attr("ec")) {
                a.ec_text = *ec;
                if (!parse_ec(*ec, a.ec)) {
                    set_err(err, Err::Xml, "line " + std::to_string(e.line) + ": ec=\"" + *ec + "\" is not a list of integers");
                    return false;
                }
            }
            if (const std::string* n = e.attr("name")) a.name = *n;
            if (const std::string* l = e.attr("layer")) a.layer = *l;
            out.animations.push_back(std::move(a));
        } else if (e.name == "Texture") {
            std::string f;
            if (!need_file(e, f)) return false;
            out.textures.push_back(f);
        } else if (e.name == "BoneUsage") {
            for (auto& b : e.children) {
                if (b.name == "Bone" && b.attr("name")) out.bones.push_back(*b.attr("name"));
                else out.unknown["BoneUsage/" + b.name]++;
            }
        } else if (e.name == "DecisionTree") {
            std::string f;
            if (!need_file(e, f)) return false;
            out.decision_trees.push_back(f);
        } else if (e.name == "TimedTrigger") {
            FxsTimedTrigger t;
            if (!need_file(e, t.file)) return false;
            if (const std::string* s = e.attr("state")) t.state = *s;
            if (const std::string* m = e.attr("model")) t.model = *m;
            out.timed_triggers.push_back(std::move(t));
        } else if (e.name == "AnimGraph") {
            std::string f;
            if (!need_file(e, f)) return false;
            out.anim_graphs.push_back(f);
        } else if (e.name == "StateMachine") {
            std::string f;
            if (!need_file(e, f)) return false;
            out.state_machines.push_back(f);
        } else {
            out.unknown[e.name]++;
        }
    }
    if (out.mesh_count == 0) {
        set_err(err, Err::Xml, "<Asset> has no <Mesh>");
        return false;
    }
    return true;
}

bool parse_wonder_art(const char* data, size_t size, const std::string& source, std::vector<WonderArt>& out,
                      Error* err) {
    XmlElement root;
    if (!parse_xml(data, size, root, err)) return false;
    if (root.name != "WonderArtInfos") {
        set_err(err, Err::Xml, source + ": root element is <" + root.name + ">, expected <WonderArtInfos>");
        return false;
    }
    for (auto& w : root.children) {
        if (w.name != "WonderArtInfo") continue;
        const XmlElement* type = w.child("Type");
        if (!type || type->text.empty()) {
            set_err(err, Err::Xml, source + ": line " + std::to_string(w.line) + ": <WonderArtInfo> without <Type>");
            return false;
        }
        for (auto& st : w.children) {
            if (st.name != "WonderArtState") continue;
            WonderArt a;
            a.type = type->text;
            a.source = source;
            if (const std::string* s = st.attr("state")) a.state = *s;
            if (const XmlElement* sc = st.child("fScale")) {
                char* q = nullptr;
                a.scale = std::strtof(sc->text.c_str(), &q);
                if (q == sc->text.c_str()) {
                    set_err(err, Err::Xml, source + ": line " + std::to_string(sc->line) + ": fScale is not a number");
                    return false;
                }
            }
            const XmlElement* g = st.child("Granny");
            if (!g || g->text.empty()) {
                set_err(err, Err::Xml, source + ": line " + std::to_string(st.line) + ": <WonderArtState> without <Granny>");
                return false;
            }
            a.granny = g->text;
            out.push_back(std::move(a));
        }
    }
    return true;
}

}  // namespace c5m
