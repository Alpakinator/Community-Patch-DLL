// render/tools/bakecompare/json.h - a small JSON reader for the comparator's inputs (issue 3404): objects, arrays,
// strings (escapes \" \\ \/ \b \f \n \r \t and \uXXXX below 0x80; others become '?'), numbers, true / false / null.
// Depth limit 64. Own code.
#pragma once
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bakecompare {

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json* get(const char* k) const {
        if (kind != Object) return nullptr;
        auto it = o.find(k);
        return it == o.end() ? nullptr : &it->second;
    }
    bool num(const char* k, double& out) const {
        const Json* j = get(k);
        if (!j || j->kind != Number) return false;
        out = j->n;
        return true;
    }
    bool str(const char* k, std::string& out) const {
        const Json* j = get(k);
        if (!j || j->kind != String) return false;
        out = j->s;
        return true;
    }
};

class JsonParser {
public:
    // Returns false with a message on any syntax error or trailing content.
    bool parse(const std::string& text, Json& out, std::string& err) {
        t_ = &text;
        i_ = 0;
        err_.clear();
        bool ok = value(out, 0);
        if (ok) {
            ws();
            if (i_ != t_->size()) ok = fail("content after the value");
        }
        err = err_;
        return ok;
    }

private:
    const std::string* t_ = nullptr;
    size_t i_ = 0;
    std::string err_;

    bool fail(const char* why) {
        if (err_.empty()) err_ = std::string(why) + " at byte " + std::to_string(i_);
        return false;
    }
    void ws() {
        while (i_ < t_->size() && ((*t_)[i_] == ' ' || (*t_)[i_] == '\t' || (*t_)[i_] == '\n' || (*t_)[i_] == '\r')) ++i_;
    }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (t_->compare(i_, n, w) != 0) return fail("bad literal");
        i_ += n;
        return true;
    }
    bool string(std::string& out) {
        if (i_ >= t_->size() || (*t_)[i_] != '"') return fail("expected a string");
        ++i_;
        out.clear();
        while (i_ < t_->size()) {
            char c = (*t_)[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= t_->size()) break;
            char e = (*t_)[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (i_ + 4 > t_->size()) return fail("short \\u escape");
                    unsigned v = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = (*t_)[i_++];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("bad \\u escape");
                    }
                    out += v < 0x80 ? static_cast<char>(v) : '?';
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return fail("nesting deeper than 64");
        ws();
        if (i_ >= t_->size()) return fail("unexpected end");
        char c = (*t_)[i_];
        if (c == '{') {
            v.kind = Json::Object;
            ++i_;
            ws();
            if (i_ < t_->size() && (*t_)[i_] == '}') { ++i_; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i_ >= t_->size() || (*t_)[i_] != ':') return fail("expected ':'");
                ++i_;
                Json child;
                if (!value(child, depth + 1)) return false;
                v.o[k] = std::move(child);
                ws();
                if (i_ < t_->size() && (*t_)[i_] == ',') { ++i_; continue; }
                if (i_ < t_->size() && (*t_)[i_] == '}') { ++i_; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.kind = Json::Array;
            ++i_;
            ws();
            if (i_ < t_->size() && (*t_)[i_] == ']') { ++i_; return true; }
            for (;;) {
                Json child;
                if (!value(child, depth + 1)) return false;
                v.a.push_back(std::move(child));
                ws();
                if (i_ < t_->size() && (*t_)[i_] == ',') { ++i_; continue; }
                if (i_ < t_->size() && (*t_)[i_] == ']') { ++i_; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.kind = Json::String;
            return string(v.s);
        }
        if (c == 't') { v.kind = Json::Bool; v.b = true; return lit("true"); }
        if (c == 'f') { v.kind = Json::Bool; v.b = false; return lit("false"); }
        if (c == 'n') { v.kind = Json::Null; return lit("null"); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* p = t_->c_str() + i_;
            char* end = nullptr;
            v.kind = Json::Number;
            v.n = std::strtod(p, &end);
            if (end == p) return fail("bad number");
            i_ += static_cast<size_t>(end - p);
            return true;
        }
        return fail("unexpected character");
    }
};

}  // namespace bakecompare
