// art_db.cpp - the game database for art resolution: winsqlite3.dll (the SQLite that ships with Windows, loaded
// from System32 only) and a TSV fallback. Epic 3100, issue 3104. Own code, GPL-3.0-or-later.
// The database is opened read-only and immutable; nothing here can write it.
#include "c5models/art.h"

#include <windows.h>

#include <cstring>

namespace c5m {

namespace {

// The subset of the SQLite C API used (signatures from sqlite3.h; x64 has one calling convention).
struct sqlite3;
struct sqlite3_stmt;
using fn_open_v2 = int (*)(const char*, sqlite3**, int, const char*);
using fn_close = int (*)(sqlite3*);
using fn_errmsg = const char* (*)(sqlite3*);
using fn_prepare_v2 = int (*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
using fn_step = int (*)(sqlite3_stmt*);
using fn_finalize = int (*)(sqlite3_stmt*);
using fn_column_count = int (*)(sqlite3_stmt*);
using fn_column_type = int (*)(sqlite3_stmt*, int);
using fn_column_text = const unsigned char* (*)(sqlite3_stmt*, int);
using fn_column_bytes = int (*)(sqlite3_stmt*, int);

const int kSqliteOk = 0, kSqliteRow = 100, kSqliteDone = 101, kSqliteNull = 5;
const int kOpenReadOnly = 0x00000001, kOpenUri = 0x00000040;

struct Api {
    HMODULE dll = nullptr;
    fn_open_v2 open_v2 = nullptr;
    fn_close close = nullptr;
    fn_errmsg errmsg = nullptr;
    fn_prepare_v2 prepare_v2 = nullptr;
    fn_step step = nullptr;
    fn_finalize finalize = nullptr;
    fn_column_count column_count = nullptr;
    fn_column_type column_type = nullptr;
    fn_column_text column_text = nullptr;
    fn_column_bytes column_bytes = nullptr;
    bool ok = false;
};

template <class F>
void bind(HMODULE m, const char* name, F& f) {
    f = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
}

const Api& api() {
    static Api a = [] {
        Api r;
        r.dll = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!r.dll) return r;
        bind(r.dll, "sqlite3_open_v2", r.open_v2);
        bind(r.dll, "sqlite3_close", r.close);
        bind(r.dll, "sqlite3_errmsg", r.errmsg);
        bind(r.dll, "sqlite3_prepare_v2", r.prepare_v2);
        bind(r.dll, "sqlite3_step", r.step);
        bind(r.dll, "sqlite3_finalize", r.finalize);
        bind(r.dll, "sqlite3_column_count", r.column_count);
        bind(r.dll, "sqlite3_column_type", r.column_type);
        bind(r.dll, "sqlite3_column_text", r.column_text);
        bind(r.dll, "sqlite3_column_bytes", r.column_bytes);
        r.ok = r.open_v2 && r.close && r.errmsg && r.prepare_v2 && r.step && r.finalize && r.column_count &&
               r.column_type && r.column_text && r.column_bytes;
        return r;
    }();
    return a;
}

// file: URI for a local path: forward slashes, a leading '/' before a drive letter, percent-encoding of everything
// but unreserved characters and '/' ':'.
std::string file_uri(const std::string& path) {
    std::string p = path;
    for (auto& c : p)
        if (c == '\\') c = '/';
    std::string u = "file:";
    if (p.size() >= 2 && p[1] == ':') u += '/';
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '_' || c == '.' || c == '~' || c == '/' || c == ':';
        if (keep) u += (char)c;
        else { u += '%'; u += hex[c >> 4]; u += hex[c & 15]; }
    }
    return u + "?mode=ro&immutable=1";
}

bool ident_ok(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

}  // namespace

struct SqliteTables::Impl {
    sqlite3* db = nullptr;
};

SqliteTables::SqliteTables() : p_(new Impl) {}

SqliteTables::~SqliteTables() {
    if (p_->db) api().close(p_->db);
}

bool SqliteTables::available() { return api().ok; }

bool SqliteTables::open(const std::string& path, Error* err) {
    const Api& a = api();
    if (!a.ok) {
        if (err) *err = {Err::NotFound, "winsqlite3.dll not found in System32 (or lacks the functions needed); "
                                        "use TsvTables with tools/artresolve/dump_tables.py"};
        return false;
    }
    std::wstring wpath;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
        wpath.assign((size_t)n, L'\0');
        if (n) MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wpath[0], n);
    }
    DWORD attr = GetFileAttributesW(wpath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (err) *err = {Err::NotFound, "database not found: " + path};
        return false;
    }
    if (p_->db) { a.close(p_->db); p_->db = nullptr; }
    std::string uri = file_uri(path);
    int rc = a.open_v2(uri.c_str(), &p_->db, kOpenReadOnly | kOpenUri, nullptr);
    if (rc != kSqliteOk) {
        std::string m = p_->db ? a.errmsg(p_->db) : "out of memory";
        if (p_->db) { a.close(p_->db); p_->db = nullptr; }
        if (err) *err = {Err::NotFound, "sqlite3_open_v2(" + uri + "): " + m};
        return false;
    }
    return true;
}

bool SqliteTables::query(const std::string& sql, std::vector<DbRow>& out, Error* err) {
    const Api& a = api();
    out.clear();
    if (!p_->db) {
        if (err) *err = {Err::NotFound, "database not open"};
        return false;
    }
    sqlite3_stmt* st = nullptr;
    if (a.prepare_v2(p_->db, sql.c_str(), -1, &st, nullptr) != kSqliteOk) {
        if (err) *err = {Err::NotFound, std::string("prepare: ") + a.errmsg(p_->db) + " in: " + sql};
        return false;
    }
    int n = a.column_count(st);
    int rc;
    while ((rc = a.step(st)) == kSqliteRow) {
        DbRow r((size_t)n);
        for (int i = 0; i < n; ++i) {
            if (a.column_type(st, i) == kSqliteNull) continue;
            const unsigned char* t = a.column_text(st, i);
            int len = a.column_bytes(st, i);
            r[(size_t)i].null = false;
            if (t) r[(size_t)i].text.assign((const char*)t, (size_t)len);
        }
        out.push_back(std::move(r));
    }
    bool ok = rc == kSqliteDone;
    if (!ok && err) *err = {Err::NotFound, std::string("step: ") + a.errmsg(p_->db)};
    a.finalize(st);
    return ok;
}

bool SqliteTables::rows(const std::string& table, const std::vector<std::string>& columns, std::vector<DbRow>& out,
                        Error* err) {
    if (!ident_ok(table)) {
        if (err) *err = {Err::NotFound, "bad table name " + table};
        return false;
    }
    std::string sql = "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (!ident_ok(columns[i])) {
            if (err) *err = {Err::NotFound, "bad column name " + columns[i]};
            return false;
        }
        sql += (i ? ", \"" : "\"") + columns[i] + "\"";
    }
    sql += " FROM \"" + table + "\" ORDER BY rowid";
    return query(sql, out, err);
}

// ---------------------------------------------------------------------------------------------- TSV
namespace {

bool unescape_field(const std::string& f, DbValue& v) {
    if (f == "\\N") { v.null = true; v.text.clear(); return true; }
    v.null = false;
    v.text.clear();
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '\\') { v.text += f[i]; continue; }
        if (++i >= f.size()) return false;
        switch (f[i]) {
        case 't': v.text += '\t'; break;
        case 'n': v.text += '\n'; break;
        case 'r': v.text += '\r'; break;
        case '\\': v.text += '\\'; break;
        default: return false;
        }
    }
    return true;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> r;
    size_t s = 0;
    for (;;) {
        size_t t = line.find('\t', s);
        r.push_back(line.substr(s, t == std::string::npos ? std::string::npos : t - s));
        if (t == std::string::npos) break;
        s = t + 1;
    }
    return r;
}

}  // namespace

bool TsvTables::add(const std::string& table, const std::string& text, Error* err) {
    Table t;
    size_t pos = 0;
    int line_no = 0;
    bool header = true;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            if (line.size() >= 3 && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF)
                line.erase(0, 3);
            t.columns = split_tabs(line);
            header = false;
            continue;
        }
        if (line.empty()) continue;
        auto f = split_tabs(line);
        if (f.size() != t.columns.size()) {
            if (err) *err = {Err::NotFound, table + ".tsv line " + std::to_string(line_no) + ": " +
                                                std::to_string(f.size()) + " fields, header has " +
                                                std::to_string(t.columns.size())};
            return false;
        }
        DbRow r(f.size());
        for (size_t i = 0; i < f.size(); ++i)
            if (!unescape_field(f[i], r[i])) {
                if (err) *err = {Err::NotFound, table + ".tsv line " + std::to_string(line_no) + ": bad escape"};
                return false;
            }
        t.rows.push_back(std::move(r));
    }
    if (header) {
        if (err) *err = {Err::NotFound, table + ".tsv is empty"};
        return false;
    }
    tables_[table] = std::move(t);
    return true;
}

bool TsvTables::load_dir(const std::string& dir, Error* err) {
    std::wstring w;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, dir.data(), (int)dir.size(), nullptr, 0);
        w.assign((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, dir.data(), (int)dir.size(), &w[0], n);
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((w + L"\\*.tsv").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = {Err::NotFound, "no .tsv files in " + dir};
        return false;
    }
    bool ok = true;
    do {
        std::wstring fn = fd.cFileName;
        int n = WideCharToMultiByte(CP_UTF8, 0, fn.data(), (int)fn.size(), nullptr, 0, nullptr, nullptr);
        std::string name((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, fn.data(), (int)fn.size(), &name[0], n, nullptr, nullptr);
        Bytes b;
        if (!read_whole_file(dir + "/" + name, b)) {
            if (err) *err = {Err::NotFound, "cannot read " + dir + "/" + name};
            ok = false;
            break;
        }
        if (!add(name.substr(0, name.size() - 4), std::string(b.begin(), b.end()), err)) { ok = false; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

bool TsvTables::rows(const std::string& table, const std::vector<std::string>& columns, std::vector<DbRow>& out,
                     Error* err) {
    out.clear();
    auto it = tables_.find(table);
    if (it == tables_.end()) {
        if (err) *err = {Err::NotFound, "table " + table + " not loaded"};
        return false;
    }
    std::vector<size_t> idx;
    for (auto& c : columns) {
        size_t i = 0;
        while (i < it->second.columns.size() && it->second.columns[i] != c) ++i;
        if (i == it->second.columns.size()) {
            if (err) *err = {Err::NotFound, "column " + table + "." + c + " not in the TSV"};
            return false;
        }
        idx.push_back(i);
    }
    for (auto& r : it->second.rows) {
        DbRow o;
        for (size_t i : idx) o.push_back(r[i]);
        out.push_back(std::move(o));
    }
    return true;
}

}  // namespace c5m
