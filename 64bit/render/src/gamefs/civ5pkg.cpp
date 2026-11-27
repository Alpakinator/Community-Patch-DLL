// render/src/gamefs/civ5pkg.cpp - minimal XML reader and Civ5Pkg extraction (task 2901). Own code.
#include "c5r/gamefs/civ5pkg.h"
#include <cstring>

namespace c5r { namespace gamefs {

const XmlNode* XmlNode::child(const char* n) const {
    for (const auto& c : children) {
        if (c->name == n) return c.get();
    }
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::all(const char* n) const {
    std::vector<const XmlNode*> v;
    for (const auto& c : children) {
        if (c->name == n) v.push_back(c.get());
    }
    return v;
}

std::vector<std::string> XmlNode::texts(const char* path) const {
    std::vector<const XmlNode*> cur{this};
    const char* p = path;
    while (*p) {
        const char* slash = std::strchr(p, '/');
        std::string seg = slash ? std::string(p, slash) : std::string(p);
        std::vector<const XmlNode*> next;
        for (const XmlNode* n : cur) {
            for (const XmlNode* c : n->all(seg.c_str())) next.push_back(c);
        }
        cur.swap(next);
        if (!slash) break;
        p = slash + 1;
    }
    std::vector<std::string> out;
    for (const XmlNode* n : cur) out.push_back(n->text);
    return out;
}

namespace {

struct XmlParser {
    const char* p;
    const char* e;
    Status err;

    bool at(const char* s) const { size_t n = std::strlen(s); return static_cast<size_t>(e - p) >= n && std::memcmp(p, s, n) == 0; }
    bool skipPast(const char* s) {
        size_t n = std::strlen(s);
        while (static_cast<size_t>(e - p) >= n) {
            if (std::memcmp(p, s, n) == 0) { p += n; return true; }
            ++p;
        }
        p = e;
        return false;
    }
    static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    static bool nameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == ':' || c == '.';
    }
    void ws() { while (p < e && space(*p)) ++p; }
    static std::string decode(const std::string& s) {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '&') {
                static const char* ent[][2] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
                bool done = false;
                for (auto& en : ent) {
                    size_t n = std::strlen(en[0]);
                    if (s.compare(i, n, en[0]) == 0) { o += en[1]; i += n - 1; done = true; break; }
                }
                if (!done) o += '&';
            } else {
                o += s[i];
            }
        }
        return o;
    }
    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && space(s[a])) ++a;
        while (b > a && space(s[b - 1])) --b;
        return s.substr(a, b - a);
    }
    // Skips comments, PIs, DOCTYPE. Returns false on truncation (err set).
    bool skipMisc() {
        for (;;) {
            ws();
            if (at("<!--")) { if (!skipPast("-->")) return fail(Err::Truncated, "xml: unterminated comment"); continue; }
            if (at("<?")) { if (!skipPast("?>")) return fail(Err::Truncated, "xml: unterminated processing instruction"); continue; }
            if (at("<!DOCTYPE")) { if (!skipPast(">")) return fail(Err::Truncated, "xml: unterminated DOCTYPE"); continue; }
            return true;
        }
    }
    bool fail(Err c, const std::string& m) { if (err.ok()) err = Status::fail(c, m); return false; }

    bool element(std::unique_ptr<XmlNode>& out, int depth) {
        if (depth > 64) return fail(Err::BadLayout, "xml: nesting deeper than 64");
        if (p >= e) return fail(Err::Truncated, "xml: input ends before an element");
        if (*p != '<') return fail(Err::BadLayout, "xml: expected '<'");
        ++p;
        const char* ns = p;
        while (p < e && nameChar(*p)) ++p;
        if (p == ns) return p >= e ? fail(Err::Truncated, "xml: input ends in a tag") : fail(Err::BadLayout, "xml: empty tag name");
        out.reset(new XmlNode);
        out->name.assign(ns, p);
        for (;;) {
            ws();
            if (p >= e) return fail(Err::Truncated, "xml: input ends in tag <" + out->name + ">");
            if (*p == '/') {
                if (p + 1 >= e) return fail(Err::Truncated, "xml: input ends in tag <" + out->name + ">");
                if (p[1] != '>') return fail(Err::BadLayout, "xml: bad self-closing tag <" + out->name + ">");
                p += 2;
                return true;
            }
            if (*p == '>') { ++p; break; }
            const char* as = p;
            while (p < e && nameChar(*p)) ++p;
            if (p == as) return fail(Err::BadLayout, "xml: bad attribute in <" + out->name + ">");
            std::string an(as, p);
            ws();
            if (p >= e) return fail(Err::Truncated, "xml: input ends in an attribute");
            if (*p != '=') return fail(Err::BadLayout, "xml: attribute " + an + " has no value");
            ++p;
            ws();
            if (p >= e) return fail(Err::Truncated, "xml: input ends in an attribute");
            char q = *p;
            if (q != '"' && q != '\'') return fail(Err::BadLayout, "xml: attribute " + an + " value is not quoted");
            ++p;
            const char* vs = p;
            while (p < e && *p != q) ++p;
            if (p >= e) return fail(Err::Truncated, "xml: input ends in an attribute value");
            out->attrs[an] = decode(std::string(vs, p));
            ++p;
        }
        std::string text;
        for (;;) {
            if (p >= e) return fail(Err::Truncated, "xml: input ends inside <" + out->name + ">");
            if (at("<!--")) { if (!skipPast("-->")) return fail(Err::Truncated, "xml: unterminated comment"); continue; }
            if (at("<![CDATA[")) {
                p += 9;
                const char* cs = p;
                if (!skipPast("]]>")) return fail(Err::Truncated, "xml: unterminated CDATA");
                text.append(cs, p - 3);
                continue;
            }
            if (at("<?")) { if (!skipPast("?>")) return fail(Err::Truncated, "xml: unterminated processing instruction"); continue; }
            if (at("</")) {
                p += 2;
                const char* cs = p;
                while (p < e && nameChar(*p)) ++p;
                std::string cn(cs, p);
                ws();
                if (p >= e) return fail(Err::Truncated, "xml: input ends in a closing tag");
                if (*p != '>' || cn != out->name)
                    return fail(Err::BadLayout, "xml: closing tag </" + cn + "> does not match <" + out->name + ">");
                ++p;
                out->text = trim(decode(text));
                return true;
            }
            if (*p == '<') {
                std::unique_ptr<XmlNode> c;
                if (!element(c, depth + 1)) return false;
                out->children.push_back(std::move(c));
                continue;
            }
            text += *p++;
        }
    }
};

} // namespace

Status parseXml(const char* data, size_t size, std::unique_ptr<XmlNode>& root) {
    root.reset();
    XmlParser x{data, data + size, Status::Ok()};
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF)
        x.p += 3;  // UTF-8 byte-order mark
    if (!x.skipMisc()) return x.err;
    if (x.p >= x.e) return Status::fail(Err::BadLayout, "xml: no root element");
    if (!x.element(root, 0)) {
        root.reset();
        return x.err;
    }
    if (!x.skipMisc()) { root.reset(); return x.err; }
    if (x.p < x.e) {
        root.reset();
        return Status::fail(Err::BadLayout, "xml: content after the root element");
    }
    return Status::Ok();
}

Status parseCiv5Pkg(const char* data, size_t size, Civ5Pkg& out) {
    std::unique_ptr<XmlNode> root;
    C5R_TRY(parseXml(data, size, root));
    if (root->name != "Civ5Package") return Status::fail(Err::BadLayout, "civ5pkg: root is <" + root->name + ">");
    out.guid.clear();
    out.paks.clear();
    out.gameplayDirs.clear();
    out.gameplayMapDirs.clear();
    out.skins.clear();
    out.priority = 0;
    if (const XmlNode* g = root->child("GUID")) out.guid = g->text;
    if (const XmlNode* pr = root->child("Priority")) {
        const std::string& t = pr->text;
        if (t.empty() || t.size() > 9) return Status::fail(Err::BadLayout, "civ5pkg: bad <Priority> '" + t + "'");
        int v = 0;
        for (char c : t) {
            if (c < '0' || c > '9') return Status::fail(Err::BadLayout, "civ5pkg: bad <Priority> '" + t + "'");
            v = v * 10 + (c - '0');
        }
        out.priority = v;
    }
    out.hasSteamApp = root->child("SteamApp") != nullptr;
    out.hasKey = root->child("Key") != nullptr;
    for (const std::string& f : root->texts("Paks/File")) if (!f.empty()) out.paks.push_back(f);
    for (const std::string& f : root->texts("Common/Paks/File")) if (!f.empty()) out.paks.push_back(f);
    for (const std::string& d : root->texts("Gameplay/Directory")) if (!d.empty()) out.gameplayDirs.push_back(d);
    for (const std::string& d : root->texts("Gameplay/MapDirectory")) if (!d.empty()) out.gameplayMapDirs.push_back(d);
    for (const XmlNode* s : root->all("UISkin")) {
        Civ5PkgSkin sk;
        auto get = [&](const char* k) { auto it = s->attrs.find(k); return it == s->attrs.end() ? std::string() : it->second; };
        sk.name = get("name");
        sk.set = get("set");
        sk.platform = get("platform");
        for (const std::string& d : s->texts("Skin/Directory")) if (!d.empty()) sk.skinDirs.push_back(d);
        for (const std::string& d : s->texts("GameplaySkin/Directory")) if (!d.empty()) sk.gameplaySkinDirs.push_back(d);
        for (const std::string& f : s->texts("Skin/Paks/File")) if (!f.empty()) sk.skinPaks.push_back(f);
        for (const std::string& f : s->texts("GameplaySkin/Paks/File")) if (!f.empty()) sk.gameplaySkinPaks.push_back(f);
        out.skins.push_back(std::move(sk));
    }
    return Status::Ok();
}

}} // namespace c5r::gamefs
